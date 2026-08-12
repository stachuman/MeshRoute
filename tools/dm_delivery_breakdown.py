#!/usr/bin/env python3
"""Per-DM + per-channel-post delivery breakdown for a sim run.

Walks the events.ndjson and reconstructs the lifecycle of every
unicast DM (`send <dst> ...`) injected via the scenario's `commands`
block, classifying each by:

  arrived       — destination decoded the DATA frame at least once
  hop1_ack      — originator received an ACK from its first-hop
                  forwarder. NB: this is hop-by-hop ACK, NOT end-to-
                  end. The originator knows the next hop accepted
                  the frame; it does NOT know whether subsequent
                  hops succeeded. Only `arrived` (destination decoded
                  DATA) is the true delivery signal.
  giveup        — originator gave up before delivery (send_giveup)
  in_flight     — no terminal event observed by run end

Also reports the per-message path: every node that carried the
message (rts_tx / data_tx) plus the mean actual hop count, so the
firmware's route choice can be compared against the topology.

Messages are keyed by (origin_node_id, dst_node_id, ctr). In events,
`node` is the orchestrator slot index (0-based); `data.origin` and
`data.dst` are firmware node_ids (from config). The tool maps between
them via the scenario's node order.

Usage:
  dm_delivery_breakdown.py CONFIG.json [EVENTS.ndjson] [opts]
  dm_delivery_breakdown.py CONFIG.json --run

If EVENTS is omitted, the tool looks for the analyze.py convention
file /tmp/<config-stem>_analyze.ndjson — which means you can run
`analyze.py --run` once and then iterate on `dm_delivery_breakdown.py`
without re-simulating. Pass --run to re-execute lus before analysis.

Options:
  --mode {dm,channel,all}  Which view to emit. Default `all`: prints
                           both the per-DM table and the per-post
                           channel table. `dm` and `channel` filter
                           to one mode (handy when piping to JSON).
  --run                  Run lus on the config first (writes events
                         to /tmp/<stem>_analyze.ndjson if EVENTS not
                         given).
  --lus PATH             lus binary path (default: auto-detect the lora-sim build).
  --json                 Emit JSON instead of the table.
  --failures             DM mode: break failed DMs down by routing-layer
                         mechanism (no-route / next-hop-silent / post-gateway /
                         no-gateway / in-flight), same vs cross-layer. Cross-layer
                         failures that reached the gateway but never delivered are
                         sub-classified by WHERE the second leg died — "no route
                         to target" (gw never RTS'd the forward → awaiting RREP),
                         "first-hop stalled" (RTS'd, no hop-1 ACK), "lost
                         downstream" (handed off, lost >=2 hops out), or the
                         resolve-bound cases — plus a HOME/VISIT tally of the
                         target's layer relative to the gateway.
  --detail               Include per-message timeline (text mode) or
                         per-message event list (JSON mode).
  --pair PAIR[,PAIR...]  Filter DM rows to specific pairs. Form
                         src:dst, e.g. "heidi:carol,dave:peter".
  --post SUBSTR          Filter channel rows to posts whose payload
                         contains SUBSTR (case-insensitive). e.g.
                         --post news-3 → only the L1-news-3 post.
  --all                  Include pairs not in scenario commands.

Channel mode reports per channel post:
  reach        — count of distinct same-layer non-self nodes that
                 emitted `channel_msg_received` for the post's id
  expected     — non-gateway nodes in the originator's layer minus 1
  sources      — breakdown of how each recipient acquired the msg
                 (pull_target / forwarder / overheard / promiscuous)
  leaks        — count of recipients on a DIFFERENT layer than the
                 originator (Principle 11 violations; should be 0)

Examples:
  # Run lus + show per-pair summary
  python3 tools/dm_delivery_breakdown.py CONFIG --run

  # Reuse the events file analyze.py just produced
  python3 tools/dm_delivery_breakdown.py CONFIG

  # Single-pair forensic timeline
  python3 tools/dm_delivery_breakdown.py CONFIG \\
      --detail --pair heidi:carol

  # JSON with full per-message event lists (for tooling / diff)
  python3 tools/dm_delivery_breakdown.py CONFIG --json --detail
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
from collections import defaultdict, Counter

# --- LoRa airtime (mirrors dv_dual_sf.lua airtime_ms) + frame sizes ---
PREAMBLE_SYM = 16          # PROTOCOL default

# ★★★★ [[B162]] 2026-08-09 — THE FIXED FRAME LENGTHS ARE GONE. THIS FILE NO LONGER GUESSES A LENGTH.
#
# What stood here was `RTS_LEN, CTS_LEN, ACK_LEN, NACK_LEN, MAC_LEN = 8, 3, 3, 4, 4` plus
# `DATA_HDR_LEN = 8`, and every airtime figure this tool ever printed was computed from them.
# ⛔ EVERY ONE OF THOSE TWO HEADLINE VALUES WAS WRONG ON EVERY ARM, measured off the wire:
#   · `RTS_LEN = 8` matched NO frame the firmware has ever aired. The pre-§hybrid-rts unicast RTS is
#     **7 B** (BASE's census: {7: 8199, 9: 294, 43: 571}); from §HYBRID-RTS-S1 on it is **10 B / 11 B**.
#     The `8` traces to `node_mac.cpp`'s stale `RTS(8)` comment, which §HYBRID-RTS-S0 already corrected.
#   · `CTS_LEN = 3` matched no frame either: with NAV enabled the ordinary CTS carries its optional
#     4th byte, so BASE's whole census is **{4: 4926}**, and from §S2 on a TERMINAL CTS is **6/7 B**.
#
# ⛔⛔ AND THE FIX IS **NOT** ANOTHER FIXED PAIR — that only moves the error to the next wire change.
# ★ THE LENGTH IS READ OFF THE WIRE, PER EVENT: the orchestrator's PHY `tx` event carries `hex` (the
#   ACTUAL transmitted bytes) and its own `airtime_ms`. `frame_lengths_by_tx()` correlates each
#   firmware `*_tx` emit to its own PHY frame and takes that frame's true length.
# ★★ AND IT FAILS LOUD (C2), IT DOES NOT DEFAULT: `frame_shape()` below validates every (label, length)
#   pair against the CODEC's legal shapes and raises `FrameShapeError` on anything the firmware cannot
#   have produced. An emit that never aired is charged ZERO and COUNTED, never charged a guessed length —
#   the old constants silently priced 1029 of s18's 2467 `rts_tx` emits that the LBT never let air.
#
# THE CODEC IS THE AUTHORITY FOR EVERY SHAPE BELOW (verified 2026-08-09, V1):
#   `pack_rts`  `frame_codec.cpp:469`: need = flood ? 43 : (m_bcast ? 9 : (7u + in.id.width))
#                ⇒ 43 FLOOD · 9 M-BROADCAST · 7 legacy-unicast (no identity) · 10/11 unicast
#                  (id.width 3 plaintext / 4 crypted). ⚠ 43-vs-9 IS THAT WAY ROUND — the design doc
#                  had FLOOD and M swapped until 2026-08-08; the wire agrees with the codec (BASE:
#                  571 frames at 43, 294 at 9).
#   `pack_cts`  `frame_codec.cpp:341+`: ORDINARY = 3 B + an optional 4th NAV byte iff payload_len != 0;
#                TERMINAL (already_received) = `terminal_cts_wire_len()` = `3u + id.width` = 6/7
#                (`frame_codec.h:448`). ⇒ {3,4} ordinary, {6,7} terminal — and the LENGTH is the only
#                discriminator available in the stream, which is exactly why it must not be assumed.
#   ACK 3 B (`frame_codec.h:283`) · NACK 4 B (`frame_codec.h:456`) · DATA/BCN: variable, wire length only.
FRAME_SHAPES = {
    # label -> {wire length: canonical shape name}.  ⛔ NOT a default table: a length absent from a
    # label's dict is a FrameShapeError, never a fallback.
    "RTS":  {7: "unicast-legacy(no-identity)", 10: "unicast-plaintext", 11: "unicast-crypted",
             9: "m-broadcast", 43: "flood"},
    "CTS":  {3: "ordinary(no-nav)", 4: "ordinary(nav)",
             6: "terminal-plaintext", 7: "terminal-crypted"},
    "ACK":  {3: "ack"},
    "NACK": {4: "nack"},
}
# labels whose length is legitimately variable (payload-bearing) — no shape table can constrain them,
# so the wire length is taken verbatim and nothing is validated beyond "we have the bytes".
VARIABLE_LEN_LABELS = {"DATA", "DATA-M", "BCN", "Q", "H"}


class FrameShapeError(RuntimeError):
    """A frame whose shape/length the codec cannot produce, or which cannot be determined at all.
    ⛔ Raised, never swallowed: a tool that guesses a length is how [[B162]] was born."""


def frame_shape(label, nbytes):
    """Canonical shape name for a frame of `label` observed at `nbytes` on the wire.

    ★ FAILS LOUD on any (label, length) outside the codec's legal set, and on an unknown label —
    both are evidence the wire moved and this table did not."""
    # the PHY labels a retried/forwarded RTS and a duplicate CTS distinctly; the SHAPE is the same frame.
    base = {"RTS-fwd": "RTS", "RTS-rty": "RTS", "CTS-dup": "CTS"}.get(label, label)
    if base in VARIABLE_LEN_LABELS:
        return f"{base}({nbytes}B)"
    tbl = FRAME_SHAPES.get(base)
    if tbl is None:
        raise FrameShapeError(f"unknown frame label {label!r} at {nbytes} B — the wire has a shape "
                              f"this tool has never been told about; add it to FRAME_SHAPES "
                              f"FROM THE CODEC, do not guess")
    shape = tbl.get(nbytes)
    if shape is None:
        raise FrameShapeError(
            f"{base} frame of {nbytes} B is NOT a shape the codec can produce "
            f"(legal: {sorted(tbl)} = {tbl}). Either the wire changed and FRAME_SHAPES is stale, or "
            f"the stream is corrupt. ⛔ REFUSING TO PRICE IT — see [[B162]].")
    return shape


def lora_airtime_ms(sf, bw_hz, cr, len_bytes, preamble_sym=PREAMBLE_SYM):
    """Port of `lib/core/airtime.cpp:airtime_ms` (itself a port of dv_dual_sf.lua).
    ⓘ Verified against the PHY stream, e.g. SF7/BW125, 76 B -> 146 ms.

    ⛔⛔ [[B162b]] 2026-08-09 — THIS FUNCTION WAS MISSING THE SX126x SF5/SF6 CASE, AND IT IS
    CORPUS-LIVE. `lib/core/airtime.cpp:27-37` (V1, read at source) applies a **6.25-symbol** sync
    offset instead of 4.25 and a **+36** payload numerator constant instead of +44 whenever
    `sf == 5 || sf == 6` — SX126x datasheet §6.1.4, mirrored in RadioLib's
    `SX126x::calculateTimeOnAir`. This Python copy had **neither**, so every SF6 frame was mispriced
    by roughly −4…+3 ms. MEASURED before the fix: **402 of 23913 frames disagreed with the PHY's own
    `airtime_ms`, and every single disagreement was at SF6** — in `s15_three_layer` (68),
    `s15_three_layer_metal` (65), `s16_dense_gateway` (81) and, at **100% of their frames**,
    `s35a` (108), `s35b` (48) and `s38_team_origin_learn` (32).
    ⚠ The memory note *"deliberate SX126x SF5/SF6 framing in airtime_ms — don't simplify away"* was
    about the C++/Lua model; **nobody checked whether the Python instrument had it.** A ported formula
    is a second copy, and a second copy drifts.
    ★ Integer arithmetic mirrors the C++ deliberately (`(num + den - 1) // den` on positive `num`,
    clamped at 0) so the two cannot diverge on a rounding edge."""
    t_sym = (2 ** sf) / (bw_hz / 1000.0)
    low_sf = sf in (5, 6)
    t_pre = (preamble_sym + (6.25 if low_sf else 4.25)) * t_sym
    de = 1 if t_sym >= 16 else 0
    num = 8 * len_bytes - 4 * sf + (36 if low_sf else 44)
    den = 4 * (sf - 2 * de)
    pay_sym_extra = 0
    if den > 0:
        pay_sym_extra = max(int(math.ceil(num / den)) * cr, 0)
    pay_sym = 8 + pay_sym_extra
    return math.floor(t_pre + pay_sym * t_sym)


def resolve_lus(explicit):
    """Locate the lus binary so this tool runs from any checkout (e.g. MeshRoute/tools/).
    Order: explicit --lus, $LUS, cwd-relative build (lora-sim native), the sibling
    lora-universal-simulator checkout, then the absolute default."""
    if explicit:
        return explicit
    here = os.path.dirname(os.path.abspath(__file__))
    sibling = os.path.join(os.path.dirname(os.path.dirname(here)),
                           "lora-universal-simulator", "build", "orchestrator", "lus")
    for cand in (os.environ.get("LUS"), "build/orchestrator/lus", sibling,
                 "/home/staszek/lora-universal-simulator/build/orchestrator/lus"):
        if cand and os.path.exists(cand):
            return cand
    raise SystemExit("dm_delivery_breakdown: cannot find the lus binary. "
                     "Pass --lus PATH or set $LUS to the lora-universal-simulator build.")


def maybe_run(cfg_path, events_path, lus_path):
    print(f"# running {lus_path} {cfg_path} -> {events_path}", file=sys.stderr)
    res = subprocess.run([lus_path, cfg_path, events_path],
                         capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
        raise SystemExit(f"lus exited {res.returncode}")


# Map a timeline event to the on-wire frame type it corresponds to
# (the tag the visualizer / wire dump uses): B R C K N D Q H, plus
# "D-M" for the channel M-broadcast. Events that aren't a frame
# (queue ops, route decisions, terminal markers) get "".
EMIT_FRAME_TYPE = {
    # firmware-level (script_emit) events
    "rts_tx": "R", "rts_retry": "R", "rts_fwd": "R", "rts_attempt_detail": "R",
    "cts_rx": "C",
    "data_tx": "D", "data_rx": "D",
    "ack_rx": "K", "ack_snr_feedback": "K",
    "channel_pull_sent": "Q", "channel_pull_received": "Q",
    "channel_pull_suppressed": "Q", "channel_msg_pulled": "Q",
    "channel_msg_seen_by_neighbour": "B",   # learned via a peer's BCN digest
    "channel_dirty_cleared": "B",            # cleared while building a BCN
    "h_tx": "H", "h_rx": "H", "h_forward": "H", "h_resolved": "H",
}


def frame_type_for(ev):
    """Return the on-wire frame-type tag for a timeline event, or ''.

    PHY events (`phy_*`) carry the radio's own label (RTS/DATA-M/BCN/H/
    ...); reduce it to the single-letter tag (DATA-M -> D-M). Firmware
    events map via EMIT_FRAME_TYPE.
    """
    et = ev.get("type", "")
    if et.startswith("phy_"):
        lbl = ev.get("fields", {}).get("label")
        if lbl:
            return {"RTS": "R", "RTS-fwd": "R", "RTS-rty": "R",
                    "CTS": "C", "CTS-dup": "C", "ACK": "K", "NACK": "N",
                    "DATA": "D", "DATA-M": "D-M", "BCN": "B",
                    "Q": "Q", "H": "H"}.get(lbl, lbl)
        # phy rx/drop events are keyed by a kind suffix instead of a label.
        if et.endswith("_data_m"):
            return "D-M"
        if et.endswith("_bcn"):
            return "B"
        return "?"
    return EMIT_FRAME_TYPE.get(et, "")


# Emit types we include in the per-message timeline. Anything outside
# this set is filtered out as noise. Keep it focused on path tracking.
TIMELINE_EMITS = {
    "tx_enqueue",
    "tx_dequeue",
    "route_decision",
    "rts_attempt_detail",
    "rts_tx",
    "rts_retry",
    "rts_fwd",
    "tx_lbt_defer",
    "send_deferred",
    "send_defer_requery",
    "cts_rx",
    "data_tx",
    "data_rx",
    "ack_rx",
    "ack_snr_feedback",
    "send_drained",
    "send_giveup",
    "delivered",
}

# Per-event "interesting" fields shown in the text timeline (keys that
# exist in `data.*`). The selection avoids dumping payload bytes in
# every line — payload appears once in the header.
TIMELINE_FIELDS = (
    "next", "from", "to", "attempt_seq", "reason", "sf", "data_sf",
    "next_attempt_ms", "settle_ms", "waited_ms", "retry_idx", "depth",
)


# Fields in event data whose value is a firmware node id; we render
# them as "name(id)" wherever they appear in detail-mode output.
NODE_ID_FIELDS = ("next", "from", "to", "via_gateway")


ALIASED_IDS = {}   # short id -> [names] for ids shared by several nodes (see load_cfg)


def fmt_node(fid, id_to_name):
    """Consistent name(id) rendering. Falls back to #id if no mapping.

    An id shared by several nodes renders every candidate (`a|b(1)`) rather than
    silently picking whichever one landed in id_to_name last — see ALIASED_IDS.
    """
    if fid is None:
        return "?"
    names = ALIASED_IDS.get(fid)
    if names:
        return f"{'|'.join(names)}({fid})"
    name = id_to_name.get(fid)
    if name is None:
        return f"#{fid}"
    return f"{name}({fid})"


def _hash_key_to_int(k):
    """Config hashes are usually "0xHEX" strings; accept ints too."""
    if isinstance(k, int):
        return k
    if isinstance(k, str):
        k = k.strip()
        if k.startswith("0x") or k.startswith("0X"):
            return int(k, 16)
        return int(k)
    return None


# ==================================================================================================
# ★★★★ [[B182]] 2026-08-12 — THE TWO IDENTITY LAYERS. THIS CLASS IS THE LOGICAL ONE.
#
# ⛔⛔ THE DEFECT THIS EXISTS TO REMOVE: this file used to treat the CONFIGURED `node_id` and the
# ON-WIRE id as ONE identity space. A hosted mobile violates that in BOTH directions —
#   · static → mobile: the first leg's wire `dst` is the mobile's HOME, and the final delivery's
#     `dst` is a LEASED local id (measured, `s22`: `tx_enqueue{dst:17}` for a DM to `M1(60)`,
#     `delivered{dst:254}` at M1);
#   · mobile → static: `stamp_origin` stamps `mob ? home_id : _node_id`, so the wire `origin` is the
#     HOME's static `node_id` (measured, `s22`: `M1(60)` emits `tx_enqueue{origin:17}`).
# ⇒ the configured pair key and the record's wire key never met, and `s21` 0/3, `s22` 0/2+0/2 and
#   `s07` 0/59 were printed as delivery measurements for DMs that demonstrably ARRIVED.
#
# ⛔⛔ AND WHY THE OBVIOUS FIX IS WRONG — read this before "simplifying" the class away: NO GLOBAL
# ID ALIAS MAP CAN REPAIR IT, because a home id SIMULTANEOUSLY identifies a real static node, and the
# home genuinely originates its OWN DMs under that id at the same time. MEASURED IN THE CORPUS, not
# hypothesised: in `s07` FOUR wire triples `(origin, dst, ctr)` are each claimed by TWO different
# logical senders — `(19,27,1)` by `mobile_walk_central` and by `mobile_courier_south_north`;
# `(37,20,1)` by `mobile_bike_west_east` and by the real static `N7GRN5_Portage_Bay_r(37)`;
# `(17,49,1)` and `(20,18,1)` likewise. The id is not a disambiguator AT ANY INSTANT.
#
# ★★ THE SLOT IS. A scenario SLOT is the node's index in the config's `nodes` array: it is unique by
# construction, time-invariant, and independent of every runtime lease. It is therefore the ONLY
# sound key for a LOGICAL endpoint, and it also un-collapses `s27`'s five `node_id: 0` mobiles, which
# shared one label (`M5(0)`) and one pair row.
# ★ THE WIRE IDS ARE KEPT — for route/hop diagnostics, frame correlation and the airtime views ONLY.
#   ⛔ They are never again a logical pair identity.
class Slots:
    """The LOGICAL identity layer: scenario slot -> name / node_id / key_hash32 / layer.

    ⛔ Every resolver here REFUSES ambiguity by returning None (and the caller counts it). A slot is
    unique; an `id`/`hash`/`name` shared by two slots is genuinely undetermined from that token, and
    picking one is how a delivery gets attributed to the wrong node — the defect wearing a new hat."""

    def __init__(self, nodes):
        self.n = len(nodes)
        self.names = [n["name"] for n in nodes]
        self.ids = [n["node_id"] for n in nodes]
        self.layers = [((n.get("config") or {}).get("layer_id")) for n in nodes]
        self._by_name = defaultdict(list)
        self._by_id = defaultdict(list)
        self._by_hash = defaultdict(list)
        for s, n in enumerate(nodes):
            self._by_name[n["name"]].append(s)
            self._by_id[n["node_id"]].append(s)
            h = _hash_key_to_int(n.get("key_hash32"))
            if h is not None:
                self._by_hash[h].append(s)

    # --- rendering -------------------------------------------------------------------------------
    def label(self, slot):
        """`name(node_id)` for a slot. ★ Unlike fmt_node() this can never merge two nodes into one
        label, which is why `s27`'s five id-0 mobiles now render as M1/M2/M3/M4/M5 and not `M5(0)`."""
        if slot is None or not (0 <= slot < self.n):
            return "?"
        return f"{self.names[slot]}({self.ids[slot]})"

    # --- resolvers: exactly one slot, or None ----------------------------------------------------
    def of_name(self, name):
        c = self._by_name.get(name) or []
        return c[0] if len(c) == 1 else None

    def of_id(self, node_id):
        """⛔ Returns None for a SHARED id (s10's `l4_seed`/`l5_seed` are both `node_id` 1) and for
        the reserved sentinel 0, which s27/s28/s29/s37 give to every unprovisioned mobile."""
        if node_id in (None, 0):
            return None
        c = self._by_id.get(node_id) or []
        return c[0] if len(c) == 1 else None

    def of_hash(self, h):
        c = self._by_hash.get(h) or []
        return c[0] if len(c) == 1 else None

    def of_hash_in_layer(self, h, layer):
        c = [s for s in (self._by_hash.get(h) or []) if self.layers[s] == layer]
        return c[0] if len(c) == 1 else None

    def id_is_shared(self, node_id):
        return len(self._by_id.get(node_id) or []) > 1


def load_config(path):
    with open(path) as f:
        cfg = json.load(f)
    nodes = cfg["nodes"]
    # Node ids are positional now — the orchestrator assigns them by node order, so
    # configs no longer carry an explicit `node_id`. Normalize once so every downstream
    # `n["node_id"]` keeps working: honour an explicit id (legacy configs) else use the
    # 1-BASED slot index (i+1), matching SimController's protocol-id default — 0 is the
    # reserved "unprovisioned" sentinel. (Mutates the in-memory dicts only; file untouched.)
    for i, n in enumerate(nodes):
        n.setdefault("node_id", i + 1)
    # ★ 2026-07-25: a short id is unique only WITHIN a layer, so two nodes on different layers
    # legitimately share one (s10's `l4_seed` and `l5_seed` are both node_id 1). This dict used
    # to let the last one win, which silently RELABELLED the other node's rows — s10's layer-4
    # row rendered as `l5_seed(1)`. Report the collision instead of hiding it; fmt_node() shows
    # every candidate so no reader mistakes an aliased row for a specific node.
    _by_id = defaultdict(list)
    for n in nodes:
        _by_id[n["node_id"]].append(n["name"])
    global ALIASED_IDS
    # id 0 is excluded deliberately: it is the reserved "unprovisioned" sentinel (see the
    # normalisation note above), so the mobiles that share it in s27/s28/s29 are not aliased in the
    # STREAM — each leases a real id at registration and never appears as origin 0 there.
    # ⛔⛔ CORRECTED 2026-08-12 ([[B182]], V1): the sentence that stood here stopped at "in the
    # stream", and the register recorded the consequence — the premise was FALSE of the
    # CONFIGURED-SEND side, which read `node_id` straight out of the JSON, so `s27`'s five id-0
    # mobiles collapsed into ONE pair label (`M5(0)`) and one row of six unrelated sends, SILENTLY,
    # because this suppression hid the collision. ★ That half is no longer keyed on ids at all: the
    # configured intents and the pair table are keyed on the SCENARIO SLOT (see `Slots`), which is
    # unique by construction. This dict now describes ONLY the wire/route diagnostics that still
    # render an id (`fmt_node`), which is what it was always sound for.
    ALIASED_IDS = {i: names for i, names in _by_id.items()
                   if len(names) > 1 and i != 0}
    if ALIASED_IDS:
        # ★ STDERR, not stdout. This diagnostic first shipped on stdout and CORRUPTED `--json`:
        # it printed ahead of the payload, so `json.load()` on the tool's output died with
        # "Expecting value: line 1 column 1" for any aliased config (s10). Diagnostics belong on
        # stderr so the JSON stream stays machine-parseable and a human still sees the warning.
        # Programmatic consumers should read the ALIASED_IDS dict rather than scrape this text.
        w = sys.stderr
        print("!! AMBIGUOUS NODE IDS — these short ids are shared by several nodes, so rows keyed",
              file=w)
        print("   on them may merge or mislabel. Short ids are unique only within a layer:", file=w)
        for i, names in sorted(ALIASED_IDS.items()):
            layers = [str((n.get("config") or {}).get("layer_id")) for n in nodes
                      if n["node_id"] == i]
            print(f"     id {i}: {', '.join(names)}   (layers {', '.join(layers)})", file=w)
        print(file=w)
    id_to_name = {n["node_id"]: n["name"] for n in nodes}
    name_to_id = {n["name"]: n["node_id"] for n in nodes}
    slot_to_id = {i: n["node_id"] for i, n in enumerate(nodes)}
    # Cross-layer destinations are addressed by key_hash32 decimal in the
    # `send_layer` command; build (target_layer_id, hash) -> name so we
    # can resolve them. layer_id lives at config.layer_id (regular nodes)
    # or, for gateways visiting another layer, in gateway_layers[].
    hash_layer_to_name = {}
    id_to_layer = {}
    for n in nodes:
        cfg_block = n.get("config", {}) or {}
        layer = cfg_block.get("layer_id")
        if layer is not None:
            id_to_layer[n["node_id"]] = layer
        h = _hash_key_to_int(n.get("key_hash32"))
        if h is None:
            continue
        if layer is not None:
            hash_layer_to_name[(layer, h)] = n["name"]
    return (cfg, id_to_name, name_to_id, slot_to_id, hash_layer_to_name, id_to_layer,
            Slots(nodes))          # ★ [[B182]]: the LOGICAL layer, alongside the wire maps above


def gateway_layers(cfg):
    """Map each gateway id to its home layer and the list of layers it visits.
    Used to tag a cross-layer second-leg failure by whether the target sits on
    the gateway's HOME layer (present ~50% in long windows) or a VISIT layer."""
    gw_home, gw_visit = {}, {}
    for n in cfg.get("nodes", []):
        c = n.get("config") or {}
        visits = c.get("gateway_layers")
        if visits or c.get("is_gateway"):
            gw_home[n["node_id"]] = c.get("layer_id")
            gw_visit[n["node_id"]] = [v.get("layer_id") for v in (visits or [])]
    return gw_home, gw_visit


# ★★★★ [[B162]] 2026-08-09 — THE CONFIGURED-SEND GRAMMAR, AND WHY IT HAD TO BE WIDENED.
# `--mode dm --json` is now the AUTHORITY for unique deliveries, so a configured send this file cannot
# PARSE is a send that silently leaves the denominator — and that is not a hypothetical:
#   ⛔ `SEND_RE`'s dst group was matched against `name_to_id` ONLY, so a NUMERIC destination
#      (`send_e2e 2 …` — the authored form in `twin_9node_dm` (54 sends) and `sim_9node_base` (12))
#      resolved to nothing and the whole scenario reported **0 deliveries**.
#   ⛔ `send_hash` / `send_hashx` (by-key_hash32 addressing on the H plane, 41 corpus sends) and
#      `send_layerx` / `send_layerx_e2e` / `send_layer_e2e` (10 more) matched NO pattern at all —
#      `^send_layer\s+` cannot match `send_layerx `, and nothing matched `send_hash `.
#   ⇒ FOURTEEN of the 36 scenarios reported a corpus-authoritative delivery total of exactly ZERO while
#      demonstrably delivering messages. ★★ That zero was believed once already; it is the reason this
#      slice re-derives the grammar FROM THE SIMULATOR'S OWN DISPATCHER rather than from this file.
# ★ THE GRAMMAR IS THE SIM'S, VERIFIED 2026-08-09 against `orchestrator/runtime/NodeRuntimeWrapper.cpp`:
#   · `send|send_priority|send_e2e|send_e2e_priority <dst> <text>`  — dst = a node NAME **or** a numeric id
#   · `send_hash|send_hashx <key_hash32 HEX> <text>`   (`:848`,`:872`; "0x is an ignorable prefix, ALWAYS
#     HEX" — deliberately NOT `send_layer`'s radix rule, and the two must not be merged)
#   · `send_layer|send_layerx|send_layer_e2e|send_layerx_e2e <layer[,layer…]> <key_hash32 DECIMAL> <text>`
#   · a TRAILING ` -t` on any DM verb selects the TEAM plane (`dm_plane_from_tail`, :639). It changes the
#     PLANE, never the addressee, so it does not affect which pair a send is counted under.
#   · `send_channel[_g|_b] <channel id> <text>` is a CHANNEL post, not a DM — excluded here by design.
# ★★ AND THE RESIDUE IS NOW COUNTED, NOT DROPPED: `configured_pairs` returns `unparsed`, surfaced as
#    `totals.unresolved_configured_sends`. A verb this file has never heard of can no longer shrink the
#    denominator in silence — it shows up as a number beside the figure.
SEND_RE = re.compile(r"^send(?:_priority|_e2e|_e2e_priority)?\s+(\S+)\s+",
                     re.IGNORECASE)
SEND_LAYER_RE = re.compile(r"^send_layer(?:x)?(?:_e2e)?\s+(\S+)\s+(\S+)\s+", re.IGNORECASE)
SEND_HASH_RE = re.compile(r"^send_hash(?:x)?\s+(\S+)\s+", re.IGNORECASE)
SEND_CHANNEL_RE = re.compile(r"^send_channel\s+(\S+)\s+(.+)$", re.IGNORECASE)
# every DM-producing scenario verb, so an UNRECOGNISED one can be told apart from a channel post
DM_SEND_VERBS = re.compile(r"^send(_priority|_e2e|_e2e_priority|_hash|_hashx|"
                           r"_layer|_layerx|_layer_e2e|_layerx_e2e)?\s", re.IGNORECASE)


def resolve_dst_token(tok, slots, wire_to_slot=None):
    """A `send`/`send_e2e` destination: a node NAME, a config `node_id`, or a RUNTIME WIRE id -> SLOT.

    ⛔ Returns None rather than guessing. A numeric token outside 1..254 is refused: 0 is the reserved
    unprovisioned sentinel and 0xFF is reserved, so such a token is a scenario-authoring error, not an
    address (`src_hint`/id-reservation rules). ★ [[B182]]: a numeric token naming an id worn by TWO
    slots (s10's `node_id` 1) is ALSO refused — `Slots.of_id` returns None — because which node the
    author meant is genuinely undetermined from the number.

    ★★ THE THIRD FORM IS REAL CORPUS SYNTAX, not a courtesy: a team destination is routinely written as
    its RUNTIME team-local id (`send 254 hop_test -t` in s23, `send_e2e 220 … -t` in s37, `send_e2e
    213 …` in s38, `send 174/235 … -t` in s35a) — an id NO node carries as `node_id`. ⛔ Order matters
    and is the [[B162]] rule kept intact: a `node_id` match WINS, so a number that names a real node is
    never translated away; only an unclaimed number falls through to the observed wire alias, and only
    when exactly one slot ever wore it."""
    s = slots.of_name(tok)
    if s is not None:
        return s
    if tok.isdigit():
        v = int(tok)
        if 1 <= v <= 254:
            byid = slots.of_id(v)
            if byid is not None:
                return byid
            return (wire_to_slot or {}).get(v)
    return None


def configured_channel_posts(cfg, name_to_id):
    """Return list of dicts describing each `send_channel` command:
    {sender_id, sender_layer, channel_id, payload, sent_at_ms}."""
    nodes_by_id = {n["node_id"]: n for n in cfg["nodes"]}
    posts = []
    for c in cfg.get("commands", []):
        cmd = c.get("command", "")
        m = SEND_CHANNEL_RE.match(cmd)
        if not m:
            continue
        sender = c.get("node")
        sender_id = name_to_id.get(sender)
        if sender_id is None:
            continue
        try:
            channel_id = int(m.group(1))
        except ValueError:
            continue
        payload = m.group(2).strip()
        sender_node = nodes_by_id.get(sender_id, {})
        sender_layer = (sender_node.get("config") or {}).get("layer_id")
        posts.append({
            "sender_id":   sender_id,
            "sender_layer": sender_layer,
            "channel_id": channel_id,
            "payload":    payload,
            "sent_at_ms": c.get("at_ms"),
        })
    return posts


def configured_pairs(cfg, slots, hash_layer_to_name, wire_to_slot=None):
    """Return Counter of (origin_SLOT, dst_SLOT) -> how many sends the scenario *intends*.

    ★★★★ [[B182]] item 1, 2026-08-12 — KEYED ON THE SCENARIO SLOT, NOT ON `node_id`. This used to
    resolve every addressee to a config `node_id` and key the pair on it, which is one half of the
    hosted-mobile mis-attribution (`Slots`' header has the whole mechanism) and is ALSO why `s27`'s
    five `node_id: 0` mobiles collapsed into ONE pair (`M5(0) -> M5(0)`, six unrelated sends on one
    row, four of them then flagged UNSENT). Slots are unique by construction, so neither can recur.
    ⛔ AND THE RESOLVERS REFUSE: a name/id/hash worn by two slots yields None and lands in `unparsed`,
    where `unresolved_configured_sends` reports it. It is never resolved by picking one.

    Recognises both same-layer `send <name>` and cross-layer
    `send_layer <target_layer> <dst_key_hash32_decimal>`. For
    `send_layer`, the dst is resolved via (target_layer_id, hash) ->
    node_name, then to that node's slot.

    ★ Returns (pairs, intended, unparsed), the first two Counters (2026-07-25). Membership
    still works everywhere `pairs` is used as a pair filter, but the COUNTS are what
    let summarise() notice that an intended send produced no message at all;
    before that, such a send was absent from the table and silently vanished from
    the denominator — see the fail-loud note in summarise().

    ★★★★ [[B182]] 2026-08-12 — THE CROSS-LAYER EXCLUSION IS REMOVED, AND THE REASON IT EXISTED IS GONE
    WITH IT. The second counter used to be `same_layer_pairs`, holding ONLY the `send <name>` intents,
    under this (correct at the time) rationale: *"a `send_layer` record's effective_dst is the
    GATEWAY's id (the wire dst), never the final destination, so a cross-layer intent legitimately has
    no row under its own (origin, final_dst) key — injecting it would report a DELIVERED cross-layer DM
    as unsent (verified on s10's `cross-l4-to-l5-joiner`)."*
    ⇒ ★ That is a statement about WIRE-KEYED records. A record's pair identity is now the LOGICAL one
      (`assign_logical_pairs()`), whose destination for a cross-layer message is the RESOLVED TARGET
      slot — the same slot the intent names. Verified on `s10`: its two `send_layer` rows sit under
      their own `(sender, target)` keys and read 1/1, so injecting the intent adds `unsent` 0.
    ⛔ AND WITHOUT THE REMOVAL A CROSS-LAYER SEND CAN STILL VANISH FROM THE DENOMINATOR — measured on
      `s27`, where 9 configured `send_layer*` DMs produced no row of their own AT ALL because their
      records are attributed to the DELEGATING HOME, not the mobile. Silence is the one outcome this
      file may not produce; they now read as sent-and-not-arrived. ⓘ Cross-layer also keeps its
      independent "cross-layer DMs: X/Y" line, built from tx_enqueue_xl + the no-gateway drops.
    """
    pairs = Counter()
    intended = Counter()          # ★ [[B182]]: ALL intents, cross-layer included (see the docstring)
    unparsed = Counter()          # ★ [[B162]]: the residue, COUNTED — see the grammar note above
    # ★ [[B162]]: a LAYER-AGNOSTIC key_hash32 index, needed because `hash_layer_to_name` is keyed on
    # `config.layer_id` and a SINGLE-LAYER scenario does not set one — so that map is EMPTY for every
    # flat scenario, and every `send_hash` in s22/s24/s25/s26/s28/s29/s30/s34/s37/s38 resolved to
    # nothing. ⛔ Ambiguity is REFUSED, not resolved by picking: if two nodes share a key_hash32 the
    # addressee is genuinely undetermined from the command, and guessing is what [[B162]] is about.
    # ⓘ [[B182]]: `Slots.of_hash` / `of_hash_in_layer` are that index, in SLOT space.
    for c in cfg.get("commands", []):
        node = c.get("node")
        cmd = c.get("command", "")
        src_slot = slots.of_name(node)
        if src_slot is None and DM_SEND_VERBS.match(cmd) and not SEND_CHANNEL_RE.match(cmd):
            unparsed["send*: sending node name unresolved (unknown or shared by 2 slots)"] += 1
            continue
        m_layer = SEND_LAYER_RE.match(cmd)
        if m_layer:
            # layer field may be a comma-separated source-routed hop path
            # (e.g. "1,3"); the destination sits on the LAST hop's layer.
            # ⓘ Covers send_layer / send_layerx / send_layer_e2e / send_layerx_e2e: the crypt and
            #   e2e-ack variants differ in FLAGS, never in addressing, so one arm serves all four.
            try:
                target_layer = int(m_layer.group(1).split(",")[-1])
                target_hash = int(m_layer.group(2))        # send_layer's hash arg is DECIMAL
            except ValueError:
                unparsed["send_layer*: unparseable layer/hash"] += 1
                continue
            dst_slot = slots.of_hash_in_layer(target_hash, target_layer)
            if dst_slot is not None:
                pairs[(src_slot, dst_slot)] += 1
                intended[(src_slot, dst_slot)] += 1    # ★ [[B182]]: no longer excluded
            else:
                unparsed["send_layer*: target hash unresolved"] += 1
            continue
        m_hash = SEND_HASH_RE.match(cmd)
        if m_hash:
            # ★ send_hash/send_hashx address by key_hash32 on the H plane, ALWAYS HEX with `0x` an
            #   ignorable prefix (NodeRuntimeWrapper.cpp:754) — deliberately NOT send_layer's radix.
            #   The addressee is same-plane/same-layer as the SENDER, so resolve on the sender's layer.
            tok = m_hash.group(1)
            try:
                target_hash = int(tok[2:] if tok[:2].lower() == "0x" else tok, 16)
            except ValueError:
                unparsed["send_hash*: unparseable hash"] += 1
                continue
            lyr = slots.layers[src_slot]
            dst_slot = slots.of_hash_in_layer(target_hash, lyr)
            if dst_slot is None:                    # flat scenario: no layer_id to key on
                if len(slots._by_hash.get(target_hash) or []) > 1:
                    unparsed["send_hash*: key_hash32 shared by several nodes (REFUSED)"] += 1
                    continue
                dst_slot = slots.of_hash(target_hash)
            if dst_slot is not None:
                pairs[(src_slot, dst_slot)] += 1
                intended[(src_slot, dst_slot)] += 1
            else:
                unparsed["send_hash*: target hash unresolved"] += 1
            continue
        m = SEND_RE.match(cmd)
        if not m:
            # a DM verb this grammar does not know is REPORTED, not dropped (channel posts excluded)
            if DM_SEND_VERBS.match(cmd) and not SEND_CHANNEL_RE.match(cmd) \
               and not cmd.lower().startswith("send_channel"):
                unparsed[f"unrecognised DM verb: {cmd.split()[0]}"] += 1
            continue
        dst_slot = resolve_dst_token(m.group(1), slots, wire_to_slot)   # NAME / node_id / wire id
        if dst_slot is not None:
            pairs[(src_slot, dst_slot)] += 1
            intended[(src_slot, dst_slot)] += 1
        else:
            unparsed["send: dst token unresolved"] += 1
    return pairs, intended, unparsed


def parse_pair_filter(arg, slots):
    """`--pair src:dst,...` -> a set of (src_SLOT, dst_SLOT). ★ [[B182]]: slot space, like every other
    pair key in this file now."""
    if arg is None:
        return None
    pairs = set()
    for chunk in arg.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if ":" not in chunk:
            sys.exit(f"--pair entry must be 'src:dst', got {chunk!r}")
        s, d = chunk.split(":", 1)
        ss, ds = slots.of_name(s), slots.of_name(d)
        if ss is None or ds is None:
            sys.exit(f"--pair {chunk!r}: unknown or ambiguous node name "
                     f"(known: {sorted(set(slots.names))})")
        pairs.add((ss, ds))
    return pairs


def msg_key(data, default_origin, origin_ctr_index):
    """Build (origin, dst, ctr) for a script_emit data dict.

    Some events (ack_rx, cts_rx, ack_snr_feedback) omit dst because
    the originator/forwarder only knows the immediate `from` peer at
    that point. We resolve dst from a pre-populated (origin, ctr) ->
    dst index built from earlier events on the same message.
    """
    origin = data.get("origin")
    if origin is None:
        origin = data.get("src")
    if origin is None:
        origin = default_origin
    dst = data.get("dst")
    ctr = data.get("ctr")
    if ctr is None:
        ctr = data.get("ctr_lo")
    if origin is None or ctr is None:
        return None
    if dst is None:
        dst = origin_ctr_index.get((origin, ctr))
        if dst is None:
            return None
    return (origin, dst, ctr)


# ★★★ [[B162c]] 2026-08-09 — ONE NDJSON READER, AND IT IS WHITESPACE-INDEPENDENT AND REFUSES OUT LOUD.
#
# ⛔⛔ THE DEFECT THIS REPLACES, AND ITS LINEAGE. Two measurement functions fast-pathed on a LITERAL
# COMPACT SUBSTRING before parsing — `phy_tx_frames` on `'"type":"tx"'` and `raw_delivered_event_count`
# on `'"emit_type":"delivered"'`. Both are valid-NDJSON-blind: an emitter that writes
# `{"type": "tx", ...}` — ordinary `json.dumps()` spacing — matches NEITHER, so every count reads
# **exactly ZERO** and the tool prints that zero as a measurement. ⚠ MEASURED, not theorised: a
# synthetic valid stream written with default `json.dumps` formatting produced `phy_frames 0`,
# `raw_delivered 0`.
#
# ★★★ SAY THE LINEAGE, BECAUSE THIS IS ITS THIRD OCCURRENCE IN THIS ARC: **a substring fast-path is a
# silent parser, and a silent parser in a measurement path fails toward "nothing happened."** The same
# shape already cost this arc a fast-path filter that matched zero rows in BOTH arms of an A/B and was
# believed, and a `bw_hz/cr` constant taken from the first line. A discriminator that can only ever
# return "no data" when it is wrong is indistinguishable from a scenario in which nothing occurred.
#
# ★★ AND THE SECOND HALF OF THE FIX (C2): a line that does not parse is **REFUSED, COUNTED AND
# SURFACED** — never `continue`d in silence. The old walkers swallowed `JSONDecodeError` with a bare
# `continue`, so a truncated or corrupt stream measured LOW with no indication whatsoever. ⚠ And per
# the `alias_stats` lesson (a refusal counter that nobody reads is not a safeguard) the count is
# printed in BOTH the JSON (`totals.malformed_ndjson_lines`) and the text output, and the key is
# always present so a ZERO is printable and comparable.
#
# ⓘ The refusal ledger is keyed by path and stores LINE NUMBERS in a set, so the many passes this tool
# makes over one file UNION rather than multiply, and a partially-consumed generator can only ever
# under-report — never inflate. `ndjson_refusals()` forces one complete scan so the reported count is
# whole regardless of which walkers ran.
_NDJSON_REFUSED = {}            # events_path -> {lineno: raw line prefix}
_NDJSON_SCANNED = set()         # events_path values given a guaranteed full scan


def iter_ndjson(events_path):
    """Yield (lineno, event dict) for every NDJSON line that parses; REFUSE + RECORD the rest.

    ★ THE ONLY WAY THIS FILE MAY READ THE EVENT STREAM. ⛔ Do not re-add a substring pre-filter:
    it is a parser that cannot say "I did not understand", and this tool's failure mode is being
    believed. Blank lines are not refusals (a trailing newline is not corruption)."""
    ledger = _NDJSON_REFUSED.setdefault(events_path, {})
    with open(events_path) as f:
        for lineno, line in enumerate(f, 1):
            if not line.strip():
                continue
            try:
                e = json.loads(line)
            except ValueError:
                ledger[lineno] = line.strip()[:160]
                continue
            if not isinstance(e, dict):
                # A bare scalar/array is syntactically fine and semantically meaningless here.
                ledger[lineno] = line.strip()[:160]
                continue
            yield lineno, e


def ndjson_refusals(events_path):
    """Complete, memoized refusal census: {"lines": n, "examples": [(lineno, text), ...]}.

    ★ Forces one full pass so the count cannot be short just because no walker read to EOF."""
    if events_path is not None and events_path not in _NDJSON_SCANNED:
        for _ in iter_ndjson(events_path):
            pass
        _NDJSON_SCANNED.add(events_path)
    ledger = _NDJSON_REFUSED.get(events_path, {})
    ex = [(ln, ledger[ln]) for ln in sorted(ledger)[:3]]
    return {"lines": len(ledger), "examples": ex}


# ==================================================================================================
# ★★★ [[B162d]] 2026-08-09 — THE SINGLE CROSSING POINT FOR THE REFUSAL NOTICE.
#
# ⛔⛔ WHY THIS EXISTS AS *ONE* POINT AND NOT AS A BANNER REPEATED PER MODE. [[B162c]] added the
# refusal banner to the DM/channel text renderer and called the promise kept. It was not: `main()`
# dispatched to FIVE other outputs that returned BEFORE it — `--trace`, `--copies`, `--airtime`,
# `--tail` (four early `return`s) and, missed even by the report that found those four, the
# `--mode channel --json` payload, which is assembled inline and carried no refusal key. So on a
# stream with a corrupt line `--airtime` printed `★★ TOTAL AIRTIME (AUTHORITY) = … ms` with **no
# banner, no LOWER BOUND warning, rc 0**, and `--airtime --json` carried neither the count nor the
# examples. ⚠ The AUTHORITATIVE view was the one path the guarantee did not reach.
#
# ★★★ THIS IS THE FOURTH OCCURRENCE OF ONE SHAPE IN THIS ARC — a measurement path that fails toward
# "nothing happened" — AND THE SECOND *INSIDE* [[B162]] ITSELF:
#   1. §S0   a payload substring fast-path matched ZERO rows in BOTH arms of an A/B, and was believed;
#   2. [[B162c]] `phy_tx_frames` gated on the literal `'"type":"tx"'` → a clean `0 ms over 0 frames`;
#   3. [[B162c]] `raw_delivered_event_count` gated on `'"emit_type":"delivered"'` → a clean `0`;
#   4. [[B162d]] the banner that fixed (2)/(3) existed, but the authoritative renderer BYPASSED it.
#
# ★★★ THE STRUCTURAL LESSON, WHICH IS THE POINT OF THIS FUNCTION: **A FAIL-LOUD PROMISE MADE AT ONE
# EXIT IS NOT MADE AT ALL WHEN THERE ARE FIVE.** ⛔ A per-mode copy of the banner is the SAME DEFECT
# IN A NEW SHAPE — it keeps the guarantee proportional to the author's memory, so the next mode added
# to `main()` silently opts out. The guarantee is therefore enforced by TOPOLOGY, not by diligence:
#   · every TEXT path crosses `ndjson_refusal_crossing_point()`, called once in `main()` BEFORE the
#     mode dispatch, so no `return` can precede it;
#   · every JSON path goes through `emit_json()`, the ONLY function in this file permitted to write
#     JSON to stdout, which ATTACHES the census to whatever payload it is handed.
# `tools/test_dm_delivery_breakdown.py::test_10` asserts both of those propositions FROM THE PARSE
# TREE (a line-grep cannot tell live code from prose about code — [[B162c]]'s own lesson), each with
# a detector control proving the check can fail.
def emit_json(payload, events_path):
    """★ THE ONLY PLACE THIS FILE MAY WRITE A JSON PAYLOAD TO STDOUT. ⛔ Do not add a second one.

    Attaches the NDJSON refusal census to every payload, at the TOP LEVEL, with the keys ALWAYS
    PRESENT. ⚠ An absent key is indistinguishable from a zero to a consumer, and this tool is the
    arc's authority for delivery and airtime — so a clean stream emits `0` / `[]` / `false` rather
    than nothing. (`totals.malformed_ndjson_lines` in the DM payload is the same census read through
    the same `ndjson_refusals()`; it is kept for [[B162c]]'s published contract, not recomputed.)"""
    ref = ndjson_refusals(events_path)
    out = dict(payload)
    out["malformed_ndjson_lines"] = ref["lines"]
    out["malformed_ndjson_examples"] = [{"line": ln, "text": tx} for ln, tx in ref["examples"]]
    out["measurement_is_lower_bound"] = bool(ref["lines"])
    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")


def ndjson_refusal_crossing_point(args):
    """★★★ Called ONCE in `main()`, before the mode dispatch. Every output path crosses it.

    Text modes get the banner here — ahead of the first figure of ANY view, not just the DM table.
    JSON modes get nothing printed here (it would corrupt the payload) because `emit_json()` carries
    the census instead; the `sys.exit` below is what makes that delegation SOUND rather than a
    hopeful comment: after it, `args.json` is EXACTLY "this run emits JSON".

    ⛔ C2 — `--json` alongside `--trace`/`--copies`/`--tail` used to be SILENTLY IGNORED (text came
    out, the flag did nothing). That is this bug's own family: an option that fails toward "nothing
    happened". It is refused loudly instead of quietly honoured-in-name."""
    if args.json and (args.trace or args.copies or args.tail is not None):
        sys.exit("--json is not implemented for --trace / --copies / --tail (they are text-only "
                 "views). ⛔ Refusing rather than ignoring the flag: a silently-dropped output "
                 "option is how [[B162]] produced three wrong-but-confident answers. Use "
                 "`--airtime --json` or `--mode dm|channel|all --json`.")
    if args.json:
        return          # ★ emit_json() attaches the census to the payload — see its docstring.
    ref = ndjson_refusals(args.events)
    if not ref["lines"]:
        return
    print(f"!! {ref['lines']} NDJSON LINE(S) REFUSED — they are not parseable JSON objects, so "
          f"they were NOT measured. ⛔ EVERY figure below is a LOWER BOUND, not a measurement "
          f"([[B162c]]):")
    for ln, txt in ref["examples"]:
        print(f"     line {ln}: {txt}")
    print()


def walk_events_slots(events_path, slot_to_id):
    """Yield (time_ms, slot, firmware_id, emit_type, data) for every script_emit.

    ★★ [[B182]]: the SLOT is the emitting node's LOGICAL identity and `fid` its wire identity. The
    walker has always had the slot (`e["node"]`) and threw it away one line later — which is exactly
    how the two identity layers came to be conflated. Both are yielded now; `walk_events()` below is
    the wire-only adapter every diagnostic view still uses (U1: extended, not forked)."""
    for _lineno, e in iter_ndjson(events_path):
        if e.get("type") != "script_emit":
            continue
        slot = e.get("node")
        fid = slot_to_id.get(slot, slot)
        yield (e.get("time_ms", 0), slot, fid,
               e.get("emit_type"), e.get("data", {}))


def walk_events(events_path, slot_to_id):
    """Yield (time_ms, firmware_id, emit_type, data) for every script_emit."""
    for t_ms, _slot, fid, et, d in walk_events_slots(events_path, slot_to_id):
        yield (t_ms, fid, et, d)


def walk_phy_events(events_path, name_to_id):
    """Yield (time_ms, fid_or_None, phy_type, e) for physical-layer events.

    type in {tx, tx_deferred, rx, collision, drop_halfduplex,
    drop_sf_mismatch, drop_preamble_miss, drop_rx_blind, ...}. These events use
    string `node`/`from`/`to` fields, so we resolve via name_to_id.

    NB: the orchestrator emits collisions as type `collision` (not
    `drop_collision`) and off-SF drops as `drop_sf_mismatch` (not `drop_off_sf`).
    The old names matched nothing, so this walker was blind to collisions.
    """
    PHY_TYPES = {"tx", "tx_deferred", "rx", "collision", "drop_halfduplex",
                 "drop_sf_mismatch", "drop_preamble_miss", "drop_rx_blind"}
    for _lineno, e in iter_ndjson(events_path):
        t = e.get("type")
        if t not in PHY_TYPES:
            continue
        # `node` for tx/tx_deferred; `from`/`to` for rx/drop.
        yield e.get("time_ms", 0), t, e


def analyse(events_path, slot_to_id, hash_layer_to_name=None, id_to_layer=None, slots=None):
    hash_layer_to_name = hash_layer_to_name or {}
    msgs = {}
    # ★★★★ [[B182]] 2026-08-12 — THE LOGICAL-CORRELATION LEDGER (item 7). ⛔ EVERY KEY IS ALWAYS
    # PRESENT so a ZERO is printable and comparable, and every one is printed in BOTH `--json` and the
    # text table. ⚠ These counters are themselves instruments: the arc has 20+ that could not fail, so
    # each is positively controlled in `tools/test_dm_delivery_breakdown.py` (a fixture that DRIVES it
    # off zero), not merely asserted to exist.
    logical = Counter({
        # positive control: last-mile deliveries recovered through the (origin, ctr) correlation
        "lastmile_correlated": 0,
        # ★★ item 5: an (origin, ctr) last-mile match with SEVERAL candidate records -> REFUSED
        "lastmile_refused_ambiguous": 0,
        # a `delivered` that matched no record at all, by wire key or by (origin, ctr)
        "lastmile_unmatched": 0,
        # ⛔ one wire (origin,dst,ctr) claimed by SEVERAL emitting slots -> both logical sends refused
        "refused_wire_key_shared": 0,
        # ⛔ an undelivered record whose wire dst is a HOME its sender also addresses a hosted mobile
        #    of: home-or-mobile is undecidable from the stream -> REFUSED rather than filed at random
        "refused_ambiguous_dst": 0,
        # ⛔ SEVERAL slots app-delivered one wire triple: which send it was FOR is undetermined
        "refused_multi_delivery": 0,
        # ⛔ a record whose wire dst names an id worn by two slots (s10's `node_id` 1)
        "refused_shared_dst_id": 0,
        # ⛔ a record with no resolvable logical sender / destination at all
        "refused_no_logical_src": 0,
        "refused_unresolved_dst": 0,
        # ★★★★ [[B185]]/QG 2026-08-12 — THE DELEGATED-ORIGINATION COUNTERS. A hosted mobile whose target it
        # cannot resolve itself sends a MOBILE_SEND *wrapper* to its home, and the HOME re-originates the DM
        # under its OWN identity. ⇒ a third logical-identity break, and the firmware ALREADY names the link.
        "deleg_reattributed_samelayer": 0,    # via `deleg_ack_put{mobile_hash, ctr_h, ctr_m}` at the home
        "deleg_reattributed_xl": 0,           # via `mobile_delegate_xl{home, ctr, dst_hash}` at the mobile
        "deleg_refused_ambiguous": 0,         # ⛔ several delegating mobiles fit one re-origination -> REFUSED
        "deleg_wrappers_excluded": 0,         # the mobile->home wrapper leg: transport, not an app-level send
        # basis split: how much of the figure rests on OBSERVED app-delivery vs the wire-dst fallback
        "dst_from_delivery": 0,
        "dst_from_wire": 0,
    })
    # ★★★ THE DELEGATION LEDGER, read off the firmware's own emits (V1, verified at the producers):
    #   · `deleg_ack_put` — `lib/core/node_hashlocate.cpp:1788`, called at `:2052` (the parked re-origination) and
    #     `:1712` (a home hosting BOTH the delegator and the target). It fires ONLY when `reply_to_hash != 0 &&
    #     mobile_ctr != 0`, i.e. EXACTLY on a delegated re-origination, and it carries the three fields that close
    #     the identity gap: the delegating mobile's stable `mobile_hash`, the HOME's re-originated `ctr_h`, and the
    #     MOBILE's own `ctr_m`.
    #   · `mobile_delegate_xl` — `lib/core/node_mac.cpp:813`, at the MOBILE's slot, carrying `home`, its own `ctr`
    #     and the final target's `dst_hash`.
    # ⛔⛔ AN EARLIER DRAFT OF THIS FILE (and a register entry, since corrected) CLAIMED THE SAME-LAYER SHAPE NEEDED
    #    A NEW `lib/core` EMIT. THAT WAS WRONG: `deleg_ack_put` already exists and already names the mobile. The
    #    claim was made from `tx_enqueue`'s field list alone without looking for a sibling emit — a
    #    "the evidence does not exist" conclusion drawn from one call site.
    # ⓘ MEASURED on `s27`: 4 `deleg_ack_put` + 10 `mobile_delegate_xl` = 14, precisely its 14 delegated originations.
    deleg_same = defaultdict(set)   # (home_slot, ctr_h) -> {delegating mobile slots}
    deleg_xl = []                   # [{mobile_slot, home_id, ctr_m, dst_slot, t_ms, used}]
    deleg_wrappers = set()          # (mobile_slot, ctr_m, home_id) -> the wrapper leg, excluded from pairing
    # Observed hosting: home slot -> {hosted mobile slots}. ★ OBSERVED, never inferred from a config
    # flag: `mobile_registered` fires AT the home and carries the mobile's stable key_hash32, and
    # `mobile_adopted` fires AT the mobile and names its home's id. Both are read; neither is trusted
    # to be present.
    hosted_by = defaultdict(set)
    # mobile leased id -> {(mobile slot, home slot)} as observed at `mobile_adopted`. This is what
    # lets the last-mile correlation below insist that the delivery really is a HOSTED-MOBILE one.
    lease_owner = defaultdict(set)
    # First pass: build (origin, ctr) -> dst from events that carry dst.
    # Second pass below applies the index to events that lack dst.
    origin_ctr_to_dst = {}
    # Cross-layer arrival index: (target_id, payload) -> first t_ms.
    # The gateway rewrites origin/ctr on the second leg, so we key on the
    # target's user-facing payload. Source this from `delivered` (the target
    # firmware surfaced the message to the app), NOT `data_rx` (mere radio
    # receipt) -- data_rx also fires on relay-forwards, duplicates, and frames
    # that fail the inner/e2e check, which over-counts cross-layer arrival.
    # `delivered` carries dst (the resolved target id) and payload (the body),
    # giving a clean, collision-safe key.
    arrival_by_payload = {}
    # Cross-layer sends the originator dropped before any envelope/DATA (no
    # gateway route). These create NO record, so without this they'd be
    # invisible and the cross-layer denominator would be over-optimistic.
    drops = []
    # Cross-layer gateway-handoff giveups, keyed (origin, dst_key_hash32) —
    # the gateway received the envelope but couldn't resolve/route to the
    # target within its TTL. Distinct from a same-layer no-route giveup (both
    # surface as giveup_reason=defer_ttl, so they must be told apart here).
    gw_giveup = set()
    # Per-gateway layer-state timeline: {gw_id: [(t_ms, active_layer_id), ...]}.
    # Lets us tell, for a stalled-at-doorstep DM, whether the gateway was AWAY
    # on another layer (schedule/timing) or PRESENT-but-unresponsive (busy) at
    # the moment a neighbour RTS'd it.
    gw_layers = {}
    # --- Second-leg (cross-layer gateway forward) sub-classification ---
    # When an envelope reaches the gateway but the target never gets it, WHERE
    # did the forward die? handoff_enq: (sender, payload) -> [(gw, fwd_ctr,
    # target)] for each gateway_handoff_enqueued (binding resolved + forward
    # queued). delivered_fwd: (gw, target, fwd_ctr) the target actually decoded.
    # h_resolved / bind_set: did a resolver answer the gateway's 'H' query / did
    # the gateway learn the binding (resolve-bound drill). gw_fwd_origins: the
    # gateway ids that emit forwards (bounds the fwd-trace pass below).
    handoff_enq = defaultdict(list)
    delivered_fwd = set()
    h_resolved = set()
    bind_set = set()
    gw_fwd_origins = set()
    # Stage-funnel inputs. hops_by_payload: payload -> layer-hop path string
    # (e.g. "1,3") from the originator's envelope; len==1 -> single-gateway
    # (suburb<->center), len>=2 -> chained (suburb<->suburb via center).
    # transit_started: (origin, dst_key_hash32) that emitted a transit forward
    # (the first gw re-wrapped toward a second gw) -- i.e. Stage-2 was attempted.
    hops_by_payload = {}
    transit_started = set()
    # §xl (2026-06-17 fix): reconstruct cross-layer DMs from the CURRENT firmware vocabulary. `tx_enqueue_xl`
    # marks a cross-layer origination; the target's `delivered` PRESERVES the original (origin,ctr) (the gateway
    # no longer rewrites them) + carries the resolved target dst -> arrival is a clean (origin,ctr) match.
    # ★ 2026-07-25 keying fix. Both maps used to key on (origin, ctr) ALONE, which SILENTLY
    # DROPPED cross-layer sends: a short id is unique only WITHIN a layer, so two different
    # nodes can share one (s10's `l4_seed` and `l5_seed` are BOTH node_id 1), and once their
    # ctr sequences align, two genuine cross-layer originations collapse onto one key. s10
    # then reported "cross-layer DMs: 1/1" while the stream carried 2 tx_enqueue_xl and 2
    # matching `delivered` — an under-count that looked like a clean 100%.
    # Origination is now keyed by target_layer and arrival by the resolved target dst, both
    # of which the events already carry; nothing new has to be inferred.
    xl_orig = {}        # (origin, ctr, target_layer) -> (gw_wire_dst, target_layer, t_enqueue_ms)
    delivered_oc = {}   # (origin, ctr, target_dst) -> (target_dst, t_delivered_ms)  (first per destination)
    xl_no_gw = 0        # cross-layer sends dropped at origination: no gateway route (xl_send_no_gateway)
    # ★★★★ [[B162]] 2026-08-09 — THE WIRE-IDENTITY ALIAS PASS. WITHOUT IT THE TEAM PLANE IS INVISIBLE.
    # A team send stamps `team_local_id()` as the wire origin, NOT the node's static id (owner ruling,
    # §team-parity T6), and a registered mobile LEASES its id. So `tx_enqueue` at the sender's own slot
    # carries `origin = <its team-local id>` while `slot_to_id` says `<its config id>` — and the record
    # creation policy below (`fid == origin`) therefore matched NOTHING on the team plane.
    # ⛔ MEASURED CONSEQUENCE, not a hypothetical: TEN scenarios (s22/s23m/s25/s26/s28/s29/s30/s34/s37/s38)
    # produced ZERO records — `--all`, i.e. with no pair filter at all, printed "no matching DM messages
    # found" — so the authoritative figure for each was 0 while their streams carried real deliveries.
    # ★ THE ALIAS IS OBSERVED, NEVER INFERRED FROM A CONFIG FIELD: a node reveals its own wire id in its
    #   own emits — `tx_enqueue.origin` at the originating slot, and `delivered.dst` / `data_rx.dst` at the
    #   receiving slot. C3: this is a per-plane runtime id being read off the runtime, which is the only
    #   place it exists.
    # ★★ AMBIGUITY IS REFUSED, NOT RESOLVED: if one wire id is worn by several config nodes it is left
    #   untranslated (and counted), because picking one would mis-attribute a delivery — the exact defect
    #   shape [[B162]] records.
    wears = defaultdict(set)          # config id -> {wire ids it was seen using}
    wire_slot_claims = defaultdict(set)  # ★ [[B182]]: wire id -> {slots observed wearing it}
    for t_ms, slot, fid, et, d in walk_events_slots(events_path, slot_to_id):
        # ★ [[B182]]: the observed hosting relation, gathered in the pass that already exists (U1).
        if et == "mobile_registered" and slots is not None:
            m = slots.of_hash(d.get("key"))
            if m is not None and slot is not None:
                hosted_by[slot].add(m)
            if d.get("local_id") is not None and m is not None and slot is not None:
                lease_owner[d["local_id"]].add((m, slot))
        elif et == "mobile_adopted" and slots is not None:
            h = slots.of_id(d.get("home"))
            if h is not None and slot is not None:
                hosted_by[h].add(slot)
            if d.get("local_id") is not None and h is not None and slot is not None:
                lease_owner[d["local_id"]].add((slot, h))
        elif et == "deleg_ack_put" and slots is not None:
            m = slots.of_hash(d.get("mobile_hash"))
            ch, cm = d.get("ctr_h"), d.get("ctr_m")
            if m is not None and slot is not None and ch is not None:
                # ★★ KEYED ON `(home_slot, ctr_h, t_ms)`, AND THE TIMESTAMP IS LOAD-BEARING, NOT DECORATION.
                # `ctr` is per-DESTINATION (`next_ctr(dst)`), so ONE home legitimately holds several records at
                # `ctr_h == 1`. MEASURED on `s27`: `S1 tx_enqueue{ctr:1,dst:104}` (delegated, t=276322),
                # `{ctr:1,dst:105}` (delegated, t=1061206) and `{ctr:1,dst:106}` (S1's OWN send to M5,
                # t=1204903) — a `(slot, ctr)` key collided all three and STOLE S1's own send from it,
                # dropping `S1(101) -> M5(0)` from 1/1 to 0/1. ⇒ the emit is produced in the SAME call chain as
                # its `tx_enqueue`, with no clock advance between them (verified: all four `deleg_ack_put` in
                # `s27` share their `tx_enqueue`'s exact millisecond), so an EXACT `enqueued_ms` match is an
                # identity, not a time heuristic.
                deleg_same[(slot, ch, t_ms)].add(m)
                if cm:
                    deleg_wrappers.add((m, cm, slots.ids[slot]))
        elif et == "mobile_delegate_xl" and slots is not None:
            ds = slots.of_hash(d.get("dst_hash"))
            if slot is not None and d.get("home") is not None:
                deleg_xl.append({"mobile_slot": slot, "home_id": d["home"], "ctr_m": d.get("ctr"),
                                 "dst_slot": ds, "t_ms": t_ms, "used": False})
                if d.get("ctr"):
                    deleg_wrappers.add((slot, d["ctr"], d["home"]))
        if et == "tx_enqueue":
            o = d.get("origin")
            if o is not None and o != fid:
                wears[fid].add(o)
                wire_slot_claims[o].add(slot)          # ★ [[B182]]: the same claim, in SLOT space
        elif et == "delivered":
            # ⛔ `delivered` ONLY — NOT `data_rx`. A RELAY also emits `data_rx` carrying the message's
            # final `dst`, so treating that as "this node wears that id" made every relay claim the
            # destination's wire id. MEASURED: on `s26` it produced 2 conflicting claims, the alias table
            # was (correctly) emptied by the refusal, and the scenario still reported 0 — a fix that
            # silently did nothing. `delivered` is app-delivery AT the destination, which is the only
            # emit that proves the emitter is the addressee. (This file's own §xl note already says
            # `data_rx` "fires on relay-forwards" — the reason was on record before the mistake.)
            w = d.get("dst")
            if w is not None and w != fid:
                wears[fid].add(w)
                wire_slot_claims[w].add(slot)          # ★ [[B182]]: the same claim, in SLOT space
    # ★★★★ [[B182]] — THE SAME OBSERVATION, IN SLOT SPACE, FOR THE *CONFIGURED* SIDE.
    # ⛔ A scenario legitimately addresses a teammate by its RUNTIME WIRE id: `send 254 hop_test -t`
    # (s23), `send_e2e 220 … -t` (s37), `send_e2e 213 … -t` (s38), `send 174/235 … -t` (s35a). Those
    # numbers are team-local / leased ids that NO node carries as its config `node_id`, so the
    # slot resolver alone cannot see them — and [[B162]] handled it by translating the PAIR KEYS through
    # `wire_to_config` afterwards, the very step that cannot express a hosted mobile.
    # ★ Instead the TOKEN is resolved, once, to the slot that was OBSERVED wearing that wire id — and
    #   only when EXACTLY ONE slot ever wore it. ⛔ Two claimants ⇒ refused, not picked.
    # ⓘ `Slots.of_id` is tried FIRST by the caller, so a number that IS a real node's `node_id` is
    #   never translated away — the rule [[B162]] established and this keeps.
    wire_to_slot = {w: next(iter(ss)) for w, ss in wire_slot_claims.items() if len(ss) == 1}
    # ★★ PUBLISHED AS `logical_wire_slot_conflicts` — and ⛔ RENAMED FROM `configured_wire_token_refused`,
    # which was BOTH too strong AND silently discarded (it occurred exactly once in this file: at the line
    # that computed it). Two corrections in one, both of them item-7 failures in the very slice that added
    # item 7:
    #   · IT DOES NOT COUNT REFUSED COMMANDS. It counts WIRE IDS worn by SEVERAL SLOTS, whether or not any
    #     scenario command ever names one. A command actually affected shows up where it belongs, in
    #     `unresolved_configured_sends` (`resolve_dst_token` returns None -> the `unparsed` residue).
    #   · A computed-and-discarded counter is exactly the `alias_stats` defect this file documents at
    #     length. It now rides `totals.logical_*` and the text table like every other one.
    # ⓘ MEASURED: 2 on `s07`. It is a DIAGNOSTIC, not a refusal, so it is deliberately NOT in
    #   `LOGICAL_REFUSAL_KEYS` and does not raise the REFUSED banner.
    logical["wire_slot_conflicts"] = sum(1 for ss in wire_slot_claims.values() if len(ss) > 1)
    wire_to_config = {}
    wire_conflicts = set()
    for cid, ws in wears.items():
        for w in ws:
            if w in wire_to_config and wire_to_config[w] != cid:
                wire_conflicts.add(w)
            wire_to_config[w] = cid
    for w in wire_conflicts:
        wire_to_config.pop(w, None)   # ⛔ refuse: several nodes wore it
    # a wire id that IS a config id in its own right is never translated away
    for cid in set(slot_to_id.values()):
        wire_to_config.pop(cid, None)
    alias_stats = {"aliases": len(wire_to_config), "refused_conflicts": len(wire_conflicts)}

    for t_ms, slot, fid, et, d in walk_events_slots(events_path, slot_to_id):
        if et == "gateway_schedule_change":
            lyr = d.get("active_layer_id")
            if lyr is not None:
                gw_layers.setdefault(fid, []).append((t_ms, lyr))
            continue
        o = d.get("origin") or d.get("src")
        c = d.get("ctr")
        if c is None:
            c = d.get("ctr_lo")
        dst = d.get("dst")
        if o is not None and c is not None and dst is not None:
            origin_ctr_to_dst.setdefault((o, c), dst)
        if et == "delivered":
            payload = d.get("payload")
            if payload is not None and dst is not None:
                key = (dst, payload)
                if key not in arrival_by_payload:
                    arrival_by_payload[key] = t_ms
            # Second-leg arrival: for the gateway's re-issued forward, origin is
            # the gateway and ctr is the forward ctr.
            if o is not None and dst is not None and c is not None:
                delivered_fwd.add((o, dst, c))
            # §xl: the target's `delivered` preserves the ORIGINAL (origin,ctr) -> key cross-layer arrival on it.
            if o is not None and c is not None and (o, c, dst) not in delivered_oc:
                # ★ [[B182]]: the delivering SLOT is carried too — for a cross-layer record it is the
                #   logical recipient, and the third element used to be the only thing available.
                delivered_oc[(o, c, dst)] = (dst, t_ms, slot)
        elif et == "gateway_envelope_dropped":
            drops.append({"origin": d.get("origin", fid),
                          "target_layer_id": d.get("target_layer_id"),
                          "dst_key_hash32": d.get("dst_key_hash32"),
                          "reason": d.get("reason")})
        elif et == "gateway_handoff_giveup":
            o2, hk = d.get("origin"), d.get("dst_key_hash32")
            if o2 is not None and hk is not None:
                gw_giveup.add((o2, hk))
        elif et == "gateway_handoff_enqueued":
            so, pl, gw = d.get("origin"), d.get("payload"), d.get("via_gateway")
            fctr, tgt = d.get("ctr"), d.get("dst")
            if so is not None and gw is not None and fctr is not None:
                handoff_enq[(so, pl)].append((gw, fctr, tgt))
                gw_fwd_origins.add(gw)
        elif et == "h_resolved":
            ho, hk = d.get("origin"), d.get("key_hash32")
            if ho is not None and hk is not None:
                h_resolved.add((ho, hk))
        elif et == "gateway_remote_bind_set":
            hk = d.get("key_hash32")
            if hk is not None:
                bind_set.add((fid, hk))
        elif et == "gateway_envelope_enqueued":
            pl, hp = d.get("payload"), d.get("hops")
            if pl is not None and hp is not None:
                hops_by_payload.setdefault(pl, str(hp))
        elif et == "gateway_envelope_transit":
            to_, hk = d.get("origin"), d.get("dst_key_hash32")
            if to_ is not None and hk is not None:
                transit_started.add((to_, hk))
        elif et == "tx_enqueue_xl":                       # §xl: a cross-layer origination (dst = the gateway wire-dst)
            tl = d.get("target_layer")
            if o is not None and c is not None and (o, c, tl) not in xl_orig:
                # ★ [[B182]]: `slot` is the ORIGINATING scenario node — the logical sender (item 2) for
                #   the cross-layer records, which are built by the §xl post-pass and never see a
                #   `tx_enqueue`, so they would otherwise have no logical sender at all.
                xl_orig[(o, c, tl)] = (dst, tl, t_ms, slot)
        elif et == "xl_send_no_gateway":                  # §xl: cross-layer send dropped at origination (no gateway route)
            xl_no_gw += 1

    # Index for looking up the originator's record from gateway-side
    # handoff events. (origin, ctr) -> record_key (origin, dst, ctr)
    # where dst is the gateway short id used as the envelope wire-dst.
    origin_ctr_to_record_key = {}

    def rec_create(k):
        if k not in msgs:
            msgs[k] = {
                "origin":      k[0],
                "dst":         k[1],
                "ctr":         k[2],
                "enqueued_ms": None,
                "arrived_ms":  None,
                "ack_ms":      None,
                "giveup_ms":   None,
                "giveup_reason": None,
                "payload":     None,
                "carriers":    set(),
                # §hops (2026-06-17): nodes that data_rx'd this msg. `data_rx` CARRIES origin (unlike the relay
                # `data_tx`/`rts_tx`, which don't) → msg_key attributes each receive-hop to the right record, so
                # distinct receivers == hop count. This is the robust hop measure (relay tx can't be keyed).
                "rx_nodes":    set(),
                "events":      [],
                # Cross-layer extension. via_gateway flips True when the
                # originator's tx_enqueue carries it; target_id resolves
                # to the cross-layer destination (via key_hash32 lookup);
                # arrival_at_target_ms records data_rx at the resolved
                # target's slot (via payload matching, not ctr — the
                # gateway re-issues with a fresh origin/ctr).
                "via_gateway":             False,
                "target_layer_id":         None,
                "dst_key_hash32":          None,
                "target_id":               None,
                "arrival_at_target_ms":    None,
                "handoff_enqueued_ms":     None,
                "handoff_drained_ms":      None,
                "handoff_deferred_reason": None,
                "handoff_giveup_reason":   None,
                # ★★★★ [[B182]] — THE LOGICAL LAYER, carried alongside the wire fields above.
                # `src_slot` (item 2) is the SCENARIO NODE that emitted the record-creating
                # `tx_enqueue`, even when the wire `origin` is its home's static id.
                "src_slot":        None,
                # `src_slot_shared` (item 5) — TWO different slots emitted a `tx_enqueue` for this one
                # wire triple. MEASURED in `s07` on 4 triples. ⛔ The logical sender is then genuinely
                # undetermined, so the record is REFUSED for pairing rather than credited to whichever
                # slot happened to be first. The pair's denominator survives via `unsent` flooring.
                "src_slot_shared": False,
                # `delivered_slots` (item 3): slot -> first correlated `delivered` t_ms. The emitting
                # slot of a `delivered` IS the app addressee, so this is the logical RECIPIENT even
                # when `data.dst` is a leased local id.
                "delivered_slots": {},
                # slots reached through the (origin, ctr) last-mile correlation (item 4), a subset of
                # `delivered_slots`. Kept separate so the recovery is measurable, not just effective.
                "lastmile_slots":  set(),
                # filled by assign_logical_pairs(): the pair identity + how it was established.
                "logical_src":     None,
                "logical_dst":     None,
                "logical_basis":   None,
                # ★ [[B185]]: True on the mobile->home MOBILE_SEND wrapper leg (see the delegation ledger).
                "deleg_wrapper":   False,
            }
        return msgs[k]

    def rec_lookup(k):
        return msgs.get(k)

    # Forward-trace for the second-leg classifier. The (gw, ctr) key collides
    # across targets, so carriers/gw_rts are keyed (gw, ctr, target) (rts_tx /
    # data_tx carry dst=target); hop1_ack is keyed (gw, ctr, payload) (ack_rx
    # lacks dst but carries payload). Built only for gateway forward-origins.
    fwd_carriers = defaultdict(set)
    fwd_gw_rts = set()
    fwd_hop1_ack = set()

    def same_node(cfg_id, wire_id):
        """★ [[B162]]: is this emitting node the node that `wire_id` names? Compares in CONFIG-id space,
        so a team/mobile record keyed on a `team_local_id`/leased id still identifies its own endpoints.
        ⛔ Without this, `fid == dst` was false for every team delivery and `arrived_ms` stayed None —
        4 records existed on `s26` and all four read un-arrived while the stream carried 4 `delivered`."""
        if wire_id is None:
            return False
        return cfg_id == wire_id or wire_to_config.get(wire_id) == cfg_id

    # ★★★★ [[B182]] item 4 — THE STATIC→HOSTED-MOBILE LAST MILE, CORRELATED THROUGH `(origin, ctr)`.
    #
    # THE SHAPE, measured on `s22` (V1, at the stream, not from a doc):
    #     455820  S2  tx_enqueue {origin:30, dst:17, ctr:1}          <- record key (30,17,1); dst=HOME
    #     456835  S1  mobile_lastmile_fwd {local:254, origin:30}
    #     458299  M1  delivered  {origin:30, dst:254, ctr:1, payload:"static_to_mobile"}
    # ⇒ `(origin, ctr)` is PRESERVED across the last mile and only `dst` changes, home id -> leased id.
    # The exact-key lookup therefore misses, and this is the one correlation that can recover it.
    #
    # ★★ AND IT IS NARROWED BY OBSERVATION, NOT BY HOPE — all four conditions must hold:
    #   (a) the delivering slot is observed to HOLD the lease `dst` (from its own `mobile_adopted`);
    #   (b) the candidate record's wire dst is that lease's observed HOME;
    #   (c) the payload matches, where both are known — ⚠ a CONTROLLED CROSS-CHECK used ONLY to
    #       REJECT a candidate, never to select one: messages legitimately repeat the same text;
    #   (d) the record has not already been delivered at that slot.
    # ★★★ AND IF (a)-(d) LEAVE MORE THAN ONE CANDIDATE IT IS **REFUSED**, NOT PICKED (item 5). `ctr` is
    # per-destination (`next_ctr(dst)`), so one origin can legitimately hold (o,dstA,c) and (o,dstB,c)
    # at once; a guess there attributes a delivery to the WRONG MOBILE, which is this bug in new
    # clothing. The refusal is counted in `lastmile_refused_ambiguous` and printed.
    by_origin_ctr = defaultdict(list)
    pending_unmatched = []      # (origin, ctr, dst) — adjudicated after the §xl post-pass, see below

    def note_delivered(r, slot, t_ms, lastmile=False):
        if slot is None:
            return
        if slot not in r["delivered_slots"]:
            r["delivered_slots"][slot] = t_ms
        if lastmile:
            r["lastmile_slots"].add(slot)

    for t_ms, slot, fid, et, d in walk_events_slots(events_path, slot_to_id):
        # Second-leg forward trace: who carried the gateway's forward, did the
        # gateway itself RTS it, and did the gateway get the hop-1 ACK?
        o_ = d.get("origin")
        if o_ in gw_fwd_origins:
            c_ = d.get("ctr")
            if et in ("rts_tx", "rts_retry", "rts_fwd", "data_tx"):
                dst_ = d.get("dst")
                if c_ is not None and dst_ is not None:
                    fwd_carriers[(o_, c_, dst_)].add(fid)
                    if fid == o_ and et in ("rts_tx", "rts_retry"):
                        fwd_gw_rts.add((o_, c_, dst_))
            elif et == "ack_rx" and fid == o_ and c_ is not None:
                fwd_hop1_ack.add((o_, c_, d.get("payload")))

        # Gateway-side handoff events refer to the originator's record
        # via origin + ctr + via_gateway (the gateway short id). They
        # MUST NOT create new records — they only annotate existing
        # originator records with handoff lifecycle timestamps.
        if et in ("gateway_handoff_enqueued", "gateway_handoff_drained",
                  "gateway_handoff_deferred", "gateway_handoff_giveup"):
            o = d.get("origin")
            c = d.get("ctr")
            if c is None:
                c = d.get("ctr_lo")
            gw = d.get("via_gateway")
            if o is None or c is None or gw is None:
                continue
            r = rec_lookup((o, gw, c))
            if r is None:
                continue
            if et == "gateway_handoff_enqueued" and r["handoff_enqueued_ms"] is None:
                r["handoff_enqueued_ms"] = t_ms
            elif et == "gateway_handoff_drained" and r["handoff_drained_ms"] is None:
                r["handoff_drained_ms"] = t_ms
            elif et == "gateway_handoff_deferred":
                r["handoff_deferred_reason"] = d.get("reason")
            elif et == "gateway_handoff_giveup":
                r["handoff_giveup_reason"] = d.get("reason")
            continue

        k = msg_key(d, default_origin=fid,
                    origin_ctr_index=origin_ctr_to_dst)
        if k is None:
            continue
        origin, dst, ctr = k

        # ★★★★ [[B182]] item 4 — the LAST-MILE fallback, tried BEFORE the record-creation policy so a
        # `delivered` whose `dst` has become a leased id is not simply skipped as "no record".
        if et == "delivered" and k not in msgs and slots is not None:
            owners = lease_owner.get(dst) or set()
            homes = {h for (m, h) in owners if m == slot}
            oc = by_origin_ctr.get((origin, ctr), ())
            cands = []
            if homes:                                   # (a) this slot really holds lease `dst`
                for kk in oc:
                    rr = msgs.get(kk)
                    if rr is None or slots.of_id(rr["dst"]) not in homes:
                        continue                        # (b) record's wire dst == the observed home
                    pl = d.get("payload")
                    if (rr["payload"] is not None and pl is not None
                            and rr["payload"] != pl):
                        continue                        # (c) payload cross-check: REJECT only
                    if slot in rr["delivered_slots"]:
                        continue                        # (d) already delivered here
                    cands.append(kk)
            if len(cands) == 1:
                note_delivered(msgs[cands[0]], slot, t_ms, lastmile=True)
                logical["lastmile_correlated"] += 1
            elif len(cands) > 1:
                logical["lastmile_refused_ambiguous"] += 1
            elif homes and oc:
                # ⓘ Counted ONLY when (a) held AND an `(origin, ctr)` origination record EXISTS but
                #   every candidate was rejected by (b)/(c)/(d) — i.e. this REALLY is a hosted-mobile
                #   last mile whose own origination we hold and could not reconcile.
                # ⛔ Deliberately NOT counted otherwise, and BOTH narrowings here are MEASURED, not tidy —
                #   this counter twice fired on perfectly healthy traffic before they were added:
                #   (i) without the `oc` term, `s27` reported 10 "unmatched" that were its 10 correctly
                #       attributed CROSS-LAYER deliveries (those records are built by the §xl post-pass from
                #       `tx_enqueue_xl`, so they are not in `by_origin_ctr` during this pass at all);
                #   (ii) with it, 3 remained — cross-layer deliveries whose `(origin, ctr)` HAPPENS to
                #       coincide with a same-layer record at the same home. ⇒ the decision is DEFERRED to
                #       after the §xl post-pass, which is the only point at which "nothing claimed this
                #       delivery" is a fact rather than a guess about pass ordering.
                #   ★ A counter that fires on healthy traffic is the shape that made `alias_stats` unread.
                pending_unmatched.append((origin, ctr, dst))
            continue

        # Filter: only track DMs (no broadcasts, no channel msgs).
        # Channel msg events have separate emit types so this branch
        # is more a defensive filter than an active one.
        if d.get("flags") is not None and (d["flags"] & 0x80):
            continue

        # Record creation policy: ONLY tx_enqueue at fid==origin starts
        # a record. Everything else updates an existing record (and is
        # silently skipped if no record exists — which happens for the
        # gateway's re-issued second-leg frames whose `origin` field is
        # rewritten to the gateway's own id).
        # ★ [[B162]]: `fid == origin` is the STATIC-plane form.
        # (see same_node() — every id comparison in this pass runs through it) A team/mobile originator stamps its
        # per-plane wire id, so the alias learned above is an equally valid identification of "this node
        # originated it". ⛔ Nothing else may create a record — a relay still may not.
        # ★★★★ [[B182]] item 2, 2026-08-12 — THE ID TEST IS GONE; `tx_enqueue` ITSELF IS THE POLICY, AND
        # THE EMITTING SLOT IS THE **DEFAULT** LOGICAL SENDER.
        # ⛔⛔ CORRECTED IN PLACE 2026-08-12 (this comment used to say the emitting slot **IS** the logical sender,
        #    full stop — an invariant STRONGER THAN THE CODE, which is the defect class this arc has hit repeatedly):
        #    **IT IS FALSE FOR A DELEGATED RE-ORIGINATION.** When a hosted mobile cannot resolve its target itself it
        #    sends a MOBILE_SEND wrapper to its home and THE HOME emits the `tx_enqueue` — the home is the EMITTER and
        #    is NOT the application sender. ⇒ the emitting slot is the sender **unless** the firmware's own
        #    `deleg_ack_put` / `mobile_delegate_xl` names a delegating mobile for it, which
        #    `assign_logical_pairs()` applies. MEASURED: `s27` has 14 such re-originations out of 15 sends.
        # ⛔⛔ The `fid == origin or alias` test DROPPED THE
        # RECORD ENTIRELY for a hosted mobile's own DM, because `stamp_origin` stamps the HOME's static
        # id and [[B162]]'s alias pass (correctly) refuses to translate an id that is a real node's
        # config id. MEASURED: 32 `tx_enqueue` emits corpus-wide were discarded that way — `s07` 24,
        # `s27` 4, `s22_mobile_team` 2, `s28` 2 — so `s22`'s `M1(60) -> S2(30)` had NO RECORD AT ALL and
        # its 2 genuine arrivals could not be seen by any later pass.
        # ★ AND IT IS SOUND, verified at the firmware rather than assumed: `"tx_enqueue"` is passed by
        #   exactly TWO `enqueue_data` call sites — `node_mac.cpp:421` (`do_send`, an app origination)
        #   and `node_hashlocate.cpp:1709` (a home originating a DIRECT last mile to a mobile it hosts,
        #   `addr_len=1`) — both of which ARE originations BY the emitting node. A relay/forward never
        #   passes it (the E2E ack uses `e2e_ack_tx`, precisely so it is not counted as an app DM).
        # ⛔ ITEM 5 APPLIES HERE TOO: if a SECOND slot emits `tx_enqueue` for the same wire triple the
        #   logical sender is undetermined, and the record is marked shared and REFUSED for pairing.
        is_originator_enqueue = (et == "tx_enqueue")
        if is_originator_enqueue:
            r = rec_create(k)
            if r["src_slot"] is None:
                r["src_slot"] = slot
                by_origin_ctr[(origin, ctr)].append(k)
                # ★ the MOBILE->HOME wrapper leg of a delegated send: transport, not an app-level DM. Its
                #   app-level twin is the home's re-origination, re-attributed to this mobile below. Counting
                #   both would double the mobile's sends and invent a (mobile -> its own home) pair.
                if (slot, ctr, dst) in deleg_wrappers:
                    r["deleg_wrapper"] = True
            elif r["src_slot"] != slot and not r["src_slot_shared"]:
                r["src_slot_shared"] = True
                logical["refused_wire_key_shared"] += 1
        else:
            r = rec_lookup(k)
            if r is None:
                continue
        if et == "delivered" and same_node(fid, dst):
            # item 3: the emitter of a `delivered` IS the app addressee, so its SLOT is the logical
            # recipient. (`data_rx` is deliberately not used for this — a RELAY emits `data_rx`
            # carrying the message's final `dst`; this file's §xl note has said so since 2026-06.)
            note_delivered(r, slot, t_ms)
        if r["payload"] is None and "payload" in d:
            r["payload"] = d["payload"]

        # Outcome timestamps. Only the *first* of each kind is kept.
        if is_originator_enqueue and r["enqueued_ms"] is None:
            r["enqueued_ms"] = t_ms
            # Cross-layer detection: originator's tx_enqueue for a
            # send_layer carries via_gateway=True, target_layer_id,
            # dst_key_hash32. The wire `dst` is the gateway; the user-
            # facing target is resolved from the hash.
            if d.get("via_gateway") is True:
                r["via_gateway"] = True
                r["target_layer_id"] = d.get("target_layer_id")
                r["dst_key_hash32"] = d.get("dst_key_hash32")
                # Cross-layer target resolution is done in a post-pass
                # below so we have access to the full name_to_id map.
            origin_ctr_to_record_key[(origin, ctr)] = k
        elif et == "data_rx" and same_node(fid, dst) and r["arrived_ms"] is None:
            r["arrived_ms"] = t_ms
        elif et == "delivered" and same_node(fid, dst) and r["arrived_ms"] is None:
            # §1c sealed-sender: a CRYPTED DM's data_rx carries origin=0 (the origin is sealed), so it mis-keys
            # and never sets arrived_ms. The `delivered` event (app-delivery at the dst) carries the RECOVERED
            # origin -> it keys correctly. For PLAINTEXT, data_rx already set arrived_ms first (this is a no-op).
            r["arrived_ms"] = t_ms
        elif et == "ack_rx" and same_node(fid, origin) and r["ack_ms"] is None:
            r["ack_ms"] = t_ms
        elif et == "send_giveup" and same_node(fid, origin):
            r["giveup_ms"] = t_ms
            r["giveup_reason"] = d.get("reason") or d.get("terminal")

        # Carrier set: who actually transmitted for this message?
        if et in ("rts_tx", "data_tx", "rts_fwd", "rts_retry"):
            r["carriers"].add(fid)
        # §hops: each data_rx = one traversed hop (a relay or the dst received a transmission). data_rx carries
        # origin so it's keyed to the originator's record — this counts multi-hop where the relay tx (no origin) can't.
        if et == "data_rx":
            r["rx_nodes"].add(fid)

        # Timeline event capture.
        if et in TIMELINE_EMITS:
            fields = {kk: d[kk] for kk in TIMELINE_FIELDS if kk in d}
            r["events"].append({"t_ms": t_ms, "node": fid,
                                "type": et, "fields": fields})

    # Stable ordering for timeline rendering.
    for r in msgs.values():
        r["events"].sort(key=lambda x: (x["t_ms"], x["node"]))

    # §xl post-pass (2026-06-17): reconstruct one record per cross-layer origination + resolve arrival from
    # delivered_oc. Done here (after the same-layer main pass) so it isn't clobbered. The target's `delivered`
    # preserves (origin,ctr) and carries the resolved target dst, so no hash/payload resolution is needed.
    # Pair each origination with the delivery that is actually ITS OWN. Deliveries are keyed
    # (origin, ctr, target_dst); an origination knows its target_layer, so the correct delivery
    # is the one whose resolved dst sits in that layer. `consumed` stops two originations that
    # share (origin, ctr) from both claiming one delivery and double-counting it.
    xl_arrived = 0
    consumed = set()
    for (o, c, _tl), (gw, tlayer, te, src_slot) in sorted(xl_orig.items(), key=lambda kv: kv[1][2]):
        r = rec_create((o, gw, c))
        r["via_gateway"] = True
        r["target_layer_id"] = tlayer
        # ★ [[B182]] item 2 for the cross-layer path: the `tx_enqueue_xl` emitter is the logical sender.
        if r["src_slot"] is None:
            r["src_slot"] = src_slot
        elif r["src_slot"] != src_slot and not r["src_slot_shared"]:
            r["src_slot_shared"] = True
            logical["refused_wire_key_shared"] += 1
        if r["enqueued_ms"] is None:
            r["enqueued_ms"] = te
        cands = [k for k in delivered_oc
                 if k[0] == o and k[1] == c and k not in consumed]
        # Prefer a delivery whose target actually lives in this origination's target layer;
        # fall back to the earliest unclaimed one when the layer of the dst is unknown (a
        # config without layer_id, or an older stream), which reproduces the old behaviour.
        pick = next((k for k in cands
                     if tlayer is not None
                     and (id_to_layer or {}).get(k[2]) == tlayer), None)
        if pick is None:
            pick = min(cands, key=lambda k: delivered_oc[k][1], default=None)
        if pick is not None:
            consumed.add(pick)
            dd = delivered_oc[pick]
            r["arrival_at_target_ms"] = dd[1]
            r["target_id"] = dd[0]
            # ★ [[B182]] item 3: the slot that emitted the target's `delivered` is the logical recipient.
            note_delivered(r, dd[2], dd[1])
            xl_arrived += 1
    xl_stats = {"sent": len(xl_orig) + xl_no_gw, "enqueued": len(xl_orig),
                "arrived": xl_arrived, "no_gateway": xl_no_gw}
    # ★ Adjudicate the deferred last-mile misses now that the §xl post-pass has claimed what is its own.
    for oc_key in pending_unmatched:
        if oc_key not in consumed:
            logical["lastmile_unmatched"] += 1

    for tl in gw_layers.values():
        tl.sort()
    second_leg = {
        "handoff_enq":     handoff_enq,
        "delivered_fwd":   delivered_fwd,
        "h_resolved":      h_resolved,
        "bind_set":        bind_set,
        "fwd_gw_rts":      fwd_gw_rts,
        "fwd_hop1_ack":    fwd_hop1_ack,
        "fwd_carriers":    fwd_carriers,
        "hops_by_payload": hops_by_payload,
        "transit_started": transit_started,
    }
    deleg = {"same": dict(deleg_same), "xl": deleg_xl}
    return (msgs, arrival_by_payload, drops, gw_giveup, gw_layers, second_leg, xl_stats,
            wire_to_config, alias_stats, dict(hosted_by), logical, wire_to_slot, deleg)


def outcome(rec):
    """Per-message terminal outcome.

    NB: `ack_ms` is the FIRST-HOP ACK from the originator's next-hop
    forwarder. It does not mean end-to-end delivery — only `arrived`
    (destination data_rx) means that. For cross-layer (`via_gateway`)
    messages, arrival is detected at the resolved target via payload
    matching, since the gateway re-issues with a fresh origin/ctr.
    """
    arr = _arrived(rec)
    ack = rec["ack_ms"] is not None
    if arr and ack:
        return "arrived_and_hop1_acked"
    if arr:
        return "arrived_no_hop1_ack"
    if ack:
        return "hop1_acked_no_arrival"
    if rec["giveup_ms"] is not None:
        return "giveup"
    return "in_flight"


def effective_dst(rec):
    """User-facing destination id (cross-layer aware)."""
    if rec.get("via_gateway") and rec.get("target_id") is not None:
        return rec["target_id"]
    return rec["dst"]


def _arrived(rec):
    """True if the user-facing destination got the message.

    Cross-layer: arrival is at the resolved target (after gateway
    handoff), not at the gateway. Same-layer: arrival is at dst.
    """
    if rec.get("via_gateway"):
        return rec["arrival_at_target_ms"] is not None
    return rec["arrived_ms"] is not None


def logical_arrived(rec):
    """★★ [[B182]]: did the message reach its LOGICAL recipient (as opposed to its wire `dst`)?

    An OBSERVED app-delivery at a scenario slot is the strongest evidence there is, and it is the ONLY
    thing that can see a hosted-mobile last mile: for `s22`'s `S2 -> M1` the wire `dst` is the home and
    `_arrived()` goes true at the HOME's `data_rx` — a "success that isn't", one layer down.
    ⛔ The wire-`dst` arrival is still honoured when no `delivered` was correlated, so nothing that
    worked before this fix changes: `assign_logical_pairs()` has already REFUSED the records where the
    two readings could disagree (the wire dst is a home its sender also addresses a mobile at)."""
    if rec.get("delivered_slots"):
        return True
    return _arrived(rec)


def assign_logical_pairs(msgs, slots, hosted_by, intended_by_pair, logical, deleg=None):
    """★★★★ [[B182]] — SET `logical_src` / `logical_dst` / `logical_basis` ON EVERY RECORD.

    This is the one place the two identity layers meet, and the order of preference is the order of
    evidential strength:
      1. ★ ONE observed `delivered` slot (items 3+4) — the addressee identified itself. Includes the
         last-mile-correlated case, where `data.dst` was a leased local id.
      2. ⛔ SEVERAL observed `delivered` slots -> REFUSED (item 5). Two nodes app-delivered one wire
         triple; which one the send was FOR is undetermined.
      3. the wire `dst` resolved to a slot — the static↔static path, unchanged in effect.
         ⛔⛔ GUARDED: if that slot is a HOME observed to host a mobile THIS SENDER also addresses,
         then "the home" and "its hosted mobile" are indistinguishable at the wire for an undelivered
         send, and it is REFUSED (item 5) rather than filed at random. ★ WHY THAT GUARD IS NOT
         COSMETIC: without it a FAILED static→mobile send is filed under `(sender, home)` and depresses
         a real pair's rate, and a mobile-bound DM that reached the home but never the mobile would be
         COUNTED AS A DELIVERY for `(sender, home)` — inflating the authority.
      4. the wire `dst` names an id worn by two slots (s10's `node_id` 1) -> REFUSED.
    ⛔ A refused record keeps every wire/route field for the diagnostics and is simply absent from the
    pair table. ★ THE DENOMINATOR DOES NOT MOVE: `summarise()`'s `unsent` flooring already counts every
    configured send the table cannot match, so a refusal shows as sent-and-not-arrived, never as a send
    that vanished. That is the property that makes refusing SAFE here."""
    targets_of = defaultdict(set)
    for (s, dd) in (intended_by_pair or {}):
        targets_of[s].add(dd)
    deleg_same = (deleg or {}).get("same") or {}
    deleg_xl = (deleg or {}).get("xl") or []
    for r in msgs.values():
        r["logical_src"] = r["logical_dst"] = r["logical_basis"] = None
        if r["src_slot"] is None:
            logical["refused_no_logical_src"] += 1
            continue
        if r["src_slot_shared"]:
            continue                     # already counted, once per wire triple, in analyse()
        if r.get("deleg_wrapper"):
            logical["deleg_wrappers_excluded"] += 1
            continue                     # ★ [[B185]]: the transport leg, not an app-level send
        src = r["src_slot"]
        ds = r["delivered_slots"]
        if len(ds) == 1:
            r["logical_src"], r["logical_dst"] = src, next(iter(ds))
            r["logical_basis"] = ("lastmile-correlated-delivery" if r["lastmile_slots"]
                                  else "observed-delivery")
            logical["dst_from_delivery"] += 1
            continue
        if len(ds) > 1:
            logical["refused_multi_delivery"] += 1
            continue
        wd = effective_dst(r)
        h = slots.of_id(wd)
        if h is None:
            logical["refused_shared_dst_id" if slots.id_is_shared(wd)
                    else "refused_unresolved_dst"] += 1
            continue
        if targets_of.get(src, ()) and (targets_of[src] & hosted_by.get(h, set())):
            logical["refused_ambiguous_dst"] += 1
            continue
        r["logical_src"], r["logical_dst"] = src, h
        r["logical_basis"] = "wire-dst"
        logical["dst_from_wire"] += 1

    # ★★★★ [[B185]] 2026-08-12 — THE DELEGATED ORIGINATION: RE-ATTRIBUTE THE **SENDER** FROM THE HOME TO THE MOBILE.
    #
    # ⛔⛔ WHY THIS IS A SECOND PASS AND NOT A CLAUSE ABOVE: the cross-layer arm needs the record's LOGICAL
    # DESTINATION (resolved from the target's own `delivered`) as its discriminator, so it can only run after every
    # destination is settled. ★ AND THAT DISCRIMINATOR IS WHAT MAKES THE XL MATCH SOUND RATHER THAN A QUEUE GUESS: a
    # delegate is matched to the re-origination that DELIVERED TO THE TARGET THE DELEGATE NAMED, not to "the next XL
    # enqueue at that home".
    # ★★ AMBIGUITY IS REFUSED, the same rule as the `(origin, ctr)` last mile: if two delegating mobiles fit one
    #    re-origination the sender is undetermined, and a guess attributes a delivery to the WRONG MOBILE — this
    #    defect wearing a new hat.
    for r in msgs.values():
        if r["logical_src"] is None or r["logical_dst"] is None:
            continue
        def _refuse(rec):
            logical["deleg_refused_ambiguous"] += 1
            rec["logical_src"] = rec["logical_dst"] = None
            rec["logical_basis"] = "refused-ambiguous-delegation"

        # --- (a) SAME-LAYER, via `deleg_ack_put{mobile_hash, ctr_h, ctr_m}` emitted AT THE HOME, in the same call
        #     chain and the same millisecond as the re-originated `tx_enqueue`. ⇒ a per-message identity.
        cands = deleg_same.get((r["logical_src"], r["ctr"], r["enqueued_ms"]))
        if cands:
            ms = {m for m in cands if m != r["logical_dst"]}   # a mobile is not both sender and recipient
            if len(ms) == 1:
                r["logical_src"] = next(iter(ms))
                r["logical_basis"] = (r["logical_basis"] or "") + "+deleg-samelayer"
                logical["deleg_reattributed_samelayer"] += 1
            elif len(ms) > 1:
                _refuse(r)
            continue
        # --- (b) CROSS-LAYER, via `mobile_delegate_xl{home, ctr, dst_hash}` emitted AT THE MOBILE.
        if not r.get("via_gateway") or not deleg_xl:
            continue
        home_id = r["origin"]                  # the wire origin of an XL re-origination IS the home
        fits = [x for x in deleg_xl
                if not x["used"] and x["home_id"] == home_id and x["mobile_slot"] != r["logical_dst"]
                and x["dst_slot"] == r["logical_dst"]
                and (r["enqueued_ms"] is None or x["t_ms"] <= r["enqueued_ms"])]
        # ★★ THE REFUSAL RULE IS ABOUT THE **IDENTITY**, NOT THE PAIRING — and this distinction was MEASURED, not
        # reasoned: `s27`'s M1 delegates to M3 three times, so three unconsumed delegates fit each of the three
        # re-originations. A blanket "more than one candidate ⇒ refuse" threw all of them away and read `M1 -> M3`
        # as 1/3. ⇒ if every candidate names the SAME mobile the logical SENDER is fully determined (which pairing
        # goes with which message is not asked here, and does not change any figure); ⛔ only candidates naming
        # DIFFERENT mobiles are a genuine ambiguity, and those are refused.
        senders = {x["mobile_slot"] for x in fits}
        if len(senders) == 1:
            min(fits, key=lambda x: x["t_ms"])["used"] = True
            r["logical_src"] = next(iter(senders))
            r["logical_basis"] = (r["logical_basis"] or "") + "+deleg-xl"
            logical["deleg_reattributed_xl"] += 1
        elif len(senders) > 1:
            _refuse(r)


def summarise(msgs, pair_filter, slots, no_gw_by_pair=None,
              intended_by_pair=None):
    """Summarise per-pair delivery. ★★ [[B182]]: every pair key here is a (src_SLOT, dst_SLOT).

    ⛔ The pair identity comes from `assign_logical_pairs()`, NEVER from the record's wire ids. What
    stood here was a [[B162]] wire->config alias translation of `r["origin"]` / `effective_dst(r)`, and
    it CANNOT repair a hosted mobile in either direction, because the wire id in question IS a real
    static's config id — the alias pass deliberately (and correctly) never translates that away.

    `intended_by_pair` (Counter from configured_pairs) makes the denominator
    FAIL LOUD. ★ 2026-07-25: without it, a scenario's `send` whose message ended
    up addressed to a DIFFERENT dst than the command intended was filtered out by
    pair_filter and then vanished ENTIRELY -- not counted as failed, just absent.
    The pair reported e.g. 3/3 = 100% while 4 sends were intended and one never
    arrived. Real instances: s09/s09_metal/s10 (a `send <name>` to a dual-layer
    gateway resolves to whichever id the gateway wears in its CURRENT window, so
    the frame goes to the other layer's id and is unroutable), plus s15 and s16.
    An intended send that produced no matching message is now injected as
    sent-with-0-arrived, exactly as drop-only `no_gw` pairs already were.
    """
    no_gw_by_pair = no_gw_by_pair or {}
    intended_by_pair = intended_by_pair or {}
    by_pair = defaultdict(list)
    misaddressed = defaultdict(Counter)   # src slot -> Counter{observed dst slot: n}, a diagnostic
    for k, r in msgs.items():
        src, eff_dst = r.get("logical_src"), r.get("logical_dst")
        if src is None or eff_dst is None:
            # ⛔ REFUSED or unresolvable logical identity — counted in `logical`/`totals.logical_*` and
            # printed. It is deliberately NOT filed under a guessed pair; the `unsent` flooring below
            # keeps the pair's denominator whole, so a refusal reads as sent-and-not-arrived.
            continue
        if pair_filter is not None and (src, eff_dst) not in pair_filter:
            # Not intended for this pair. If the ORIGIN was supposed to send
            # somewhere, remember where its traffic actually went -- that is the
            # evidence explaining an unsent intended pair below.
            if any(o == src for (o, _d) in intended_by_pair):
                misaddressed[src][eff_dst] += 1
            continue
        by_pair[(src, eff_dst)].append(r)
    # Drop-only pairs (every send dropped before enqueue) have no records, so
    # they're absent from by_pair -- inject them so the loss is counted. Same for
    # intended-but-never-observed pairs (see the docstring).
    all_pairs = set(by_pair) | set(no_gw_by_pair) | set(intended_by_pair)
    rows = []
    for (origin, dst) in sorted(all_pairs):
        if pair_filter is not None and (origin, dst) not in pair_filter:
            continue
        recs = by_pair.get((origin, dst), [])
        no_gw = no_gw_by_pair.get((origin, dst), 0)
        # honest denominator: enqueued + dropped, floored at what the scenario
        # INTENDED for this pair. `unsent` = intended sends that produced no
        # message at all (mis-addressed or never issued) -- they count as sent
        # and un-arrived, and are reported loudly under the table.
        observed = len(recs) + no_gw
        unsent = max(0, intended_by_pair.get((origin, dst), 0) - observed)
        n = observed + unsent
        arrived = sum(1 for r in recs if logical_arrived(r))
        acked = sum(1 for r in recs if r["ack_ms"] is not None)
        giveup = sum(1 for r in recs if outcome(r) == "giveup")
        in_flight = sum(1 for r in recs if outcome(r) == "in_flight")
        # A pair is cross-layer if any record is via_gateway OR it had drops
        # (drops only come from send_layer, i.e. cross-layer).
        any_cross = any(r.get("via_gateway") for r in recs) or no_gw > 0
        # §hops: hop count = distinct data_rx receivers (origin-keyed, robust). Fall back to carriers for
        # records with no rx_nodes (e.g. the post-pass cross-layer records, whose data_rx keys on the target dst).
        hops_list = [len(r["rx_nodes"]) if r["rx_nodes"] else len(r["carriers"])
                     for r in recs if logical_arrived(r)]
        mean_hops = (sum(hops_list) / len(hops_list)) if hops_list else None
        giveup_reasons = [r["giveup_reason"] for r in recs
                          if r["giveup_reason"]]
        rows.append({
            "origin":     slots.label(origin),
            "dst":        slots.label(dst),
            "sent":       n,
            "arrived":    arrived,
            "acked":      acked,
            "giveup":     giveup,
            "no_gw":      no_gw,
            "in_flight":  in_flight,
            "mean_hops":  mean_hops,
            "giveup_reasons": giveup_reasons,
            "cross_layer": any_cross,
            "pair_key":   (origin, dst),   # SLOT pair, so callers can share the unsent map
            "unsent":     unsent,
            # Where this origin's traffic went instead, when it owes an unsent send. ★ [[B182]]: the
            # keys are RENDERED here (slot -> "name(id)") so no downstream renderer needs the slot map.
            "misaddressed": ({slots.label(d): n for d, n in misaddressed.get(origin, {}).items()}
                             if unsent else {}),
        })
    return rows


def render_table(rows):
    if not rows:
        print("(no matching DM messages found)")
        return
    # "h1_ack" = originator got the hop-1 ACK; NOT end-to-end.
    # See outcome() docstring for details.
    # Pair column is wider now: "alice(1) -> bob(2)" can hit ~22 chars
    # for two-digit IDs. "*" suffix marks cross-layer rows.
    header = ["pair", "sent", "arr", "arr%", "h1ack", "h1ack%",
              "giveup", "no_gw", "in_flight", "mean_hops"]
    # Auto-size the (variable, long) pair column from the actual names so the
    # numeric columns stay aligned. "*" suffix marks cross-layer rows.
    pairs = [f"{r['origin']} -> {r['dst']}{' *' if r.get('cross_layer') else ''}"
             for r in rows]
    pw = max([len(p) for p in pairs] + [len("pair"), len("TOTAL")])
    fmt = ("{:<%d} {:>4} {:>4} {:>5} {:>5} {:>6} {:>6} {:>5} {:>9} {:>9}" % pw)
    hdr = fmt.format(*header)
    print(hdr)
    print("-" * len(hdr))
    tot = {"sent": 0, "arrived": 0, "acked": 0,
           "giveup": 0, "no_gw": 0, "in_flight": 0}
    for r, pair in zip(rows, pairs):
        arr_pct = f"{100*r['arrived']/r['sent']:.0f}%" if r["sent"] else "-"
        ack_pct = f"{100*r['acked']/r['sent']:.0f}%" if r["sent"] else "-"
        mh = f"{r['mean_hops']:.1f}" if r["mean_hops"] is not None else "-"
        print(fmt.format(pair, r["sent"], r["arrived"], arr_pct,
                         r["acked"], ack_pct, r["giveup"],
                         r.get("no_gw", 0), r["in_flight"], mh))
        for k in tot:
            tot[k] += r.get(k, 0)
    print("-" * len(hdr))
    arr_pct = f"{100*tot['arrived']/tot['sent']:.0f}%" if tot["sent"] else "-"
    ack_pct = f"{100*tot['acked']/tot['sent']:.0f}%" if tot["sent"] else "-"
    print(fmt.format("TOTAL", tot["sent"], tot["arrived"], arr_pct,
                     tot["acked"], ack_pct, tot["giveup"],
                     tot["no_gw"], tot["in_flight"], "-"))
    reasons = defaultdict(int)
    for r in rows:
        for x in r["giveup_reasons"]:
            reasons[x] += 1
    if reasons:
        print()
        print("giveup reasons:")
        for k, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
            print(f"  {k:<40} {v}")
    # ★ FAIL LOUD: intended sends that produced no message at all. These used to
    # be dropped from the table AND the denominator, so the pair read 100% while
    # a send had silently failed. They are now in `sent` above; name them here
    # with the evidence, because a bare number does not tell you WHY.
    unsent_rows = [r for r in rows if r.get("unsent")]
    if unsent_rows:
        print()
        print("!! UNSENT — configured sends with NO matching message "
              "(counted as sent/0-arrived above):")
        for r in unsent_rows:
            print(f"  {r['origin']} -> {r['dst']}   intended-but-missing: {r['unsent']}")
            for dst, n in sorted(r["misaddressed"].items(), key=lambda kv: -kv[1]):
                print(f"      this origin instead addressed {dst}"
                      f" x{n}  <- likely the mis-resolved destination")
        print("  (a `send <name>` resolves to the target's id at command time; a"
              " dual-layer gateway's id")
        print("   alternates with its active window, so the frame can be"
              " addressed to the other layer.)")


def _ev_has(rec, etype, **fields):
    """True if rec's timeline has an event of etype matching the given fields."""
    for e in rec["events"]:
        if e["type"] == etype and all(e["fields"].get(k) == v
                                      for k, v in fields.items()):
            return True
    return False


def _targeted_gateway(rec):
    """True if any carrier RTS'd the gateway directly. For a cross-layer first
    leg the wire `dst` IS the gateway short id, so an rts_* with next==dst means
    the envelope reached a direct neighbour of the gateway and tried to hand off
    — i.e. it got to the gateway's doorstep. (If never true, the envelope never
    reached the gateway's neighbourhood = a routing failure, not availability.)"""
    gw = rec["dst"]
    for e in rec["events"]:
        if e["type"] in ("rts_tx", "rts_retry", "rts_fwd") \
           and e["fields"].get("next") == gw:
            return True
    return False


def _revisited_node(rec):
    """True if some node received this message's DATA more than once — a
    forwarding loop bounced it back to a node that already held it."""
    seen = set()
    for e in rec["events"]:
        if e["type"] == "data_rx":
            n = e["node"]
            if n in seen:
                return True
            seen.add(n)
    return False


def _gateway_present_at(gw_layers, gw_id, layer, t):
    """Was gateway gw_id active on `layer` at time t? Returns True/False, or
    None if unknown (no timeline / t precedes the first recorded transition)."""
    tl = gw_layers.get(gw_id) if gw_layers else None
    if not tl:
        return None
    active = None
    for tt, lyr in tl:            # sorted ascending
        if tt <= t:
            active = lyr
        else:
            break
    return None if active is None else (active == layer)


def _doorstep_away_or_busy(rec, gw_layers, id_to_layer):
    """For a doorstep stall, classify why the gateway didn't pick up. The first
    leg runs on the origin's layer, so the gateway had to be on that layer to
    answer. Check its layer-state at each RTS-to-gateway attempt: if it was on
    another layer at ALL of them -> 'away' (schedule/timing); if present for at
    least one -> 'busy' (congestion/half-duplex/collision). None = unknown."""
    if gw_layers is None or id_to_layer is None:
        return None
    origin_layer = id_to_layer.get(rec["origin"])
    if origin_layer is None:
        return None
    gw = rec["dst"]
    seen_any = present_any = False
    for e in rec["events"]:
        if e["type"] in ("rts_tx", "rts_retry", "rts_fwd") \
           and e["fields"].get("next") == gw:
            p = _gateway_present_at(gw_layers, gw, origin_layer, e["t_ms"])
            if p is None:
                continue
            seen_any = True
            present_any = present_any or p
    if not seen_any:
        return None
    return "busy" if present_any else "away"


def classify_second_leg(rec, sl, gw_home=None, gw_visit=None, id_to_layer=None):
    """Sub-classify a cross-layer message that REACHED the gateway but whose
    target never got it — i.e. where did the gateway's forward (second leg) die?

      no route to target  — forward was enqueued but the gateway NEVER RTS'd it
                            (it had no route on the target layer → awaiting RREP)
      first-hop stalled    — gateway RTS'd but never got its hop-1 ACK
      lost downstream      — gateway GOT its hop-1 ACK (handed off), but the msg
                            died >=2 hops out among the target layer's own relays
      forward not enqueued — gateway never even queued a forward (binding
                            unresolved); drilled via h_resolved / bind_set.

    Returns (label, location) where location is HOME/VISIT/"?" — the target's
    layer relative to the gateway (gateways are part-time on every layer they
    serve, so this says which presence regime the failure sits in).
    """
    origin = rec["origin"]
    payload = rec.get("payload")
    gw = rec["dst"]                      # cross-layer wire dst == gateway id
    khash = rec.get("dst_key_hash32")
    flist = sl["handoff_enq"].get((origin, payload), [])

    loc = "?"
    target = rec.get("target_id")
    tl = id_to_layer.get(target) if (id_to_layer and target is not None) else None
    if tl is not None and gw_home is not None:
        if tl == gw_home.get(gw):
            loc = "HOME"
        elif tl in (gw_visit.get(gw) or []):
            loc = "VISIT"

    if flist:
        gw_rts = any((g, ctr, tgt) in sl["fwd_gw_rts"] for (g, ctr, tgt) in flist)
        hop1 = any((g, ctr, payload) in sl["fwd_hop1_ack"] for (g, ctr, _t) in flist)
        if not gw_rts:
            label = "XL 2nd-leg: no route to target (gw never RTS'd forward)"
        elif not hop1:
            label = "XL 2nd-leg: first-hop stalled (gw RTS'd, no hop-1 ACK)"
        else:
            label = "XL 2nd-leg: lost downstream (handed off, lost >=2 hops out)"
    else:
        answered = (gw, khash) in sl["h_resolved"]
        learned = (gw, khash) in sl["bind_set"]
        if answered and not learned:
            label = "XL 2nd-leg: resolve reply missed gw (resolver answered, gw never learned)"
        elif answered and learned:
            label = "XL 2nd-leg: resolve learned late (binding arrived, no forward)"
        else:
            label = "XL 2nd-leg: forward never enqueued (binding unresolved)"
    return label, loc


def failure_category(rec, gw_giveup, gw_layers=None, id_to_layer=None):
    """Routing-layer failure taxonomy for a DM that did NOT arrive. Distinguishes
    route non-convergence (no route at all) from next-hop-silent (route exists,
    next-hop won't answer) from the cross-layer second leg. NB: giveup_reason
    `defer_ttl` alone is ambiguous (same-layer no-route vs gateway handoff), so
    we classify by carriers + timeline events + the gateway-giveup set."""
    car = len(rec["carriers"])
    if rec.get("via_gateway"):
        if (rec["origin"], rec.get("dst_key_hash32")) in gw_giveup:
            return "XL: gateway gave up (resolve/route to target)"
        if rec["arrived_ms"] is not None:
            # Sub-classified into the second-leg mechanism by classify_second_leg
            # (set on the record in main); fall back to the flat label if absent.
            return rec.get("second_leg") or "XL: reached gateway, lost after forward"
        if car == 0 and _ev_has(rec, "send_deferred", reason="no_route"):
            return "XL: origin had no route to gateway"
        if car >= 1:
            # First-leg stall sub-taxonomy (the dominant cross-layer bucket).
            # Did the envelope reach the gateway's doorstep, or never get there?
            if _targeted_gateway(rec):
                aob = _doorstep_away_or_busy(rec, gw_layers, id_to_layer)
                if aob == "away":
                    return "XL stall: doorstep, gateway AWAY on other layer (schedule/timing)"
                if aob == "busy":
                    return "XL stall: doorstep, gateway PRESENT but no pickup (busy/collision)"
                return "XL stall: AT gateway doorstep, no pickup (gateway away/busy)"
            if _revisited_node(rec):
                return "XL stall: routing LOOP, never reached gateway"
            return "XL stall: cascade/dead-end, never reached gateway"
        return "XL: other"
    if outcome(rec) == "in_flight":
        return "SL: in-flight at end"
    if car == 0 and _ev_has(rec, "send_deferred", reason="no_route"):
        return "SL: origin no route (requery failed)"
    if _ev_has(rec, "send_deferred", reason="all_candidates_silent"):
        return "SL: next-hop silent (cts/ack timeout)"
    return f"SL: giveup ({rec.get('giveup_reason')})"


def render_dm_failures(msgs, no_gw_by_pair, gw_giveup, pair_filter, id_to_name,
                       gw_layers=None, id_to_layer=None, unsent_by_pair=None):
    """`unsent_by_pair` = configured same-layer sends that produced NO message.

    ★ 2026-07-25: this view is the one BASELINE.md's gate recipe actually runs
    (`--failures`), and it shared the per-pair table's blind spot — a send whose
    message was addressed elsewhere matched no pair, so it was neither counted as
    delivered nor as failed and the view read 100%. Injected as its own mechanism,
    mirroring how no_gw_by_pair is already injected below.
    """
    unsent_by_pair = unsent_by_pair or {}
    cat = Counter()
    sl_loc = Counter()        # HOME/VISIT split of the second-leg failures
    ok = 0
    for r in msgs.values():
        # ★ [[B182]]: filter on the LOGICAL pair, like the table — the two views used to disagree about
        # which messages were in scope the moment a mobile was involved.
        lp = (r.get("logical_src"), r.get("logical_dst"))
        if None in lp:
            continue
        if pair_filter is not None and lp not in pair_filter:
            continue
        if logical_arrived(r):
            ok += 1
        else:
            cat[failure_category(r, gw_giveup, gw_layers, id_to_layer)] += 1
            if r.get("second_leg") and r.get("second_leg_loc"):
                sl_loc[r["second_leg_loc"]] += 1
    for (origin, dst), n in no_gw_by_pair.items():
        if pair_filter is not None and (origin, dst) not in pair_filter:
            continue
        cat["XL: no gateway known (never enveloped)"] += n
    for (origin, dst), n in unsent_by_pair.items():
        if pair_filter is not None and (origin, dst) not in pair_filter:
            continue
        cat["SL: configured send produced NO message "
            "(mis-addressed dst / never issued)"] += n
    fail = sum(cat.values())
    tot = ok + fail
    if tot == 0:
        print("(no matching DM messages)")
        return
    print(f"delivered {ok}/{tot} = {100*ok/tot:.1f}%;  {fail} failed, by mechanism:")
    for k, v in cat.most_common():
        print(f"  {v:>4} ({100*v/fail:4.1f}% of fails)  {k}")
    if sl_loc:
        tot_sl = sum(sl_loc.values())
        loc_str = ", ".join(f"{k} {v}" for k, v in sl_loc.most_common())
        print(f"  (2nd-leg target location, {tot_sl} fails: {loc_str})")


def _fmt_reasons(counter, top=4):
    """Compact 'reason (n)' list, most common first."""
    if not counter:
        return ""
    return "  ".join(f"{k} ({v})" for k, v in counter.most_common(top))


def render_xl_funnel(msgs, no_gw_by_pair, gw_giveup, second_leg,
                     pair_filter, id_to_name, gw_layers=None, id_to_layer=None):
    """Cross-layer stage funnel — WHERE in the pipeline do XL messages leak?

    Stages (see docs/DELIVERY_ANALYSIS.md 'Cross-layer delivery pipeline'):
      S0 enqueued        — originator built + queued the gateway envelope
      S1 reached gateway — first leg delivered the envelope to the egress gw
                           (data_rx at the wire dst == the gateway)
      S2 transited       — (chained suburb<->suburb only) the egress gw's
                           re-wrapped forward reached a SECOND gateway
      S3 final-leg queued— a gateway resolved the target + queued the forward
      S4 delivered       — the target decoded the DATA

    The headline rows count 'furthest stage reached'; the 'lost' column is the
    drop entering that stage, and the reasons come from failure_category — whose
    Stage-1 labels already split window-miss (gw AWAY on another layer) vs
    in-window (gw PRESENT -> stale-route loop / contention)."""
    handoff_enq = second_leg["handoff_enq"]
    hops_by_payload = second_leg["hops_by_payload"]
    transit_started = second_leg["transit_started"]

    recs = []
    for r in msgs.values():
        if not r.get("via_gateway"):
            continue
        lp = (r.get("logical_src"), r.get("logical_dst"))     # ★ [[B182]]: logical pair, as the table
        if None in lp:
            continue
        if pair_filter is not None and lp not in pair_filter:
            continue
        recs.append(r)
    dropped = 0
    for (origin, dst), n in no_gw_by_pair.items():
        if pair_filter is not None and (origin, dst) not in pair_filter:
            continue
        dropped += n

    def path_len(r):
        hp = hops_by_payload.get(r.get("payload"))
        return len([x for x in str(hp).split(",") if x != ""]) if hp else 1

    def furthest(r):
        if _arrived(r):
            return 4
        origin, payload = r["origin"], r.get("payload")
        if handoff_enq.get((origin, payload)):
            return 3
        if r["arrived_ms"] is not None:
            if (path_len(r) >= 2
                    and (origin, r.get("dst_key_hash32")) in transit_started):
                return 2     # reached egress gw + transit fired, 2nd gw never handed off
            return 1         # reached egress gw, no transit / no handoff
        return 0             # never reached the egress gw

    enq = len(recs)
    if enq + dropped == 0:
        print("(no cross-layer messages in scope)")
        return
    nstage = Counter()
    loss_first = Counter()   # furthest == 0 (died on first leg)
    loss_gw = Counter()      # furthest in {1,2} (reached gw, died at transit/resolve)
    loss_final = Counter()   # furthest == 3 (final-leg forward queued, never arrived)
    n_two = two_reached_gw1 = two_transited = two_delivered = 0
    for r in recs:
        s = furthest(r)
        nstage[s] += 1
        cat = failure_category(r, gw_giveup, gw_layers, id_to_layer) if s < 4 else None
        if s == 0:
            loss_first[cat] += 1
        elif s in (1, 2):
            loss_gw[cat] += 1
        elif s == 3:
            loss_final[cat] += 1
        if path_len(r) >= 2:
            n_two += 1
            if r["arrived_ms"] is not None:
                two_reached_gw1 += 1
            hk = handoff_enq.get((r["origin"], r.get("payload")), [])
            if _arrived(r) or any(g != r["dst"] for (g, _c, _t) in hk):
                two_transited += 1
            if _arrived(r):
                two_delivered += 1

    r1 = sum(nstage[j] for j in range(1, 5))   # reached egress gw
    r3 = sum(nstage[j] for j in range(3, 5))   # final-leg queued
    r4 = nstage[4]                             # delivered
    total = enq + dropped
    print(f"cross-layer messages: {total}   ({dropped} dropped at enqueue: no gateway route)")
    print(f"{'stage':<30}{'reached':>8}{'lost':>6}   why it was lost entering this stage")
    print("-" * 92)
    print(f"{'S0 enqueued (envelope built)':<30}{enq:>8}{dropped:>6}   "
          f"{'(pre-enqueue: no gateway route)' if dropped else ''}")
    print(f"{'S1 reached egress gateway':<30}{r1:>8}{enq - r1:>6}   {_fmt_reasons(loss_first)}")
    print(f"{'S3 final-leg queued by a gw':<30}{r3:>8}{r1 - r3:>6}   {_fmt_reasons(loss_gw)}")
    print(f"{'S4 delivered':<30}{r4:>8}{r3 - r4:>6}   {_fmt_reasons(loss_final)}")
    if n_two:
        print(f"  chained suburb<->suburb (2-gw): {n_two} sent · "
              f"{two_reached_gw1} reached gw1 · {two_transited} transited to gw2 · "
              f"{two_delivered} delivered")


def render_detail_text(msgs, pair_filter, id_to_name):
    # Filter on effective pair (cross-layer aware) so detail mode and
    # the summary table stay consistent on which messages appear.
    keys = []
    for k, r in msgs.items():
        lp = (r.get("logical_src"), r.get("logical_dst"))     # ★ [[B182]]: logical pair, as the table
        if pair_filter is None or lp in pair_filter:
            keys.append(k)
    keys.sort(key=lambda k: (k[0], k[1], k[2]))
    for k in keys:
        r = msgs[k]
        # For cross-layer messages, the wire dst is the gateway; the
        # logical/user-facing target is r["target_id"]. Show "via gw"
        # in the header so the reader sees where the handoff happened.
        origin_n = fmt_node(r["origin"], id_to_name)
        if r.get("via_gateway"):
            target_n = fmt_node(r.get("target_id"), id_to_name)
            via_n = fmt_node(r["dst"], id_to_name)
            head = f"=== {origin_n} -> {target_n} via {via_n} ctr={r['ctr']} ==="
        else:
            dst_n = fmt_node(r["dst"], id_to_name)
            head = f"=== {origin_n} -> {dst_n} ctr={r['ctr']} ==="
        out_label = outcome(r)
        hop_count = len(r["carriers"])
        head_parts = [head]
        if r["payload"] is not None:
            head_parts.append(f'payload="{r["payload"]}"')
        head_parts.append(f"outcome={out_label}")
        head_parts.append(f"carriers={hop_count}")
        if r["enqueued_ms"] is not None:
            head_parts.append(f"enq={r['enqueued_ms']}ms")
        if r.get("via_gateway") and r.get("arrival_at_target_ms") is not None:
            head_parts.append(f"arr_at_target={r['arrival_at_target_ms']}ms")
        if r["arrived_ms"] is not None:
            head_parts.append(f"arr_at_dst={r['arrived_ms']}ms")
        if r["ack_ms"] is not None:
            head_parts.append(f"ack={r['ack_ms']}ms")
        if r["giveup_ms"] is not None:
            head_parts.append(f"giveup={r['giveup_ms']}ms"
                              f"({r['giveup_reason']})")
        print(" ".join(head_parts))
        for ev in r["events"]:
            node_n = fmt_node(ev["node"], id_to_name)
            # Field values for known node-id fields get the name(id)
            # treatment so "next=alice(1)" reads cleanly.
            rendered = []
            for kk, vv in ev["fields"].items():
                if kk in NODE_ID_FIELDS and isinstance(vv, int):
                    rendered.append(f"{kk}={fmt_node(vv, id_to_name)}")
                else:
                    rendered.append(f"{kk}={vv}")
            field_str = " ".join(rendered)
            ftype = frame_type_for(ev)
            print(f"  {ev['t_ms']:>8} ms  [{ftype:>3}] {node_n:<12} "
                  f"{ev['type']:<22} {field_str}")
        print()


def raw_delivered_event_count(events_path):
    """★ THE CROSS-CHECK, AND ONLY THE CROSS-CHECK ([[B162]]). A raw count of `delivered` emits.

    ⛔ THIS IS NOT THE FIGURE OF RECORD and must never be quoted as one. It counts EVENTS, so it
    over-counts a logical send that was delivered to the app more than once ([[B159]]: duplicate app
    deliveries are a live, measured defect — 12 at BASE, 21 at DELETE), and it has no denominator: it
    cannot see a configured send that produced nothing at all. The AUTHORITY is
    `totals.unique_deliveries`, which aggregates FIRST ARRIVAL PER CONFIGURED LOGICAL SEND.
    ★ Both are emitted side by side, always, and always labelled — because this arc quoted the raw
    number as the metric and then compared it to a carried-forward table.

    ★★ [[B162c]] 2026-08-09 — THIS USED TO BE `if '"emit_type":"delivered"' in line`, a literal
    COMPACT-SUBSTRING test. On valid NDJSON written with ordinary `json.dumps()` spacing it matched
    NOTHING and this function returned **0** — the cross-check silently agreeing that nothing was ever
    delivered. It now PARSES (`iter_ndjson`), so the predicate is on the field, not on the whitespace.
    ⓘ The predicate is deliberately the exact one the substring encoded — `emit_type == "delivered"`,
    NOT additionally `type == "script_emit"` — so the repair is measurement-INERT on the compact
    corpus rather than quietly re-defining the cross-check while claiming to fix a parser."""
    n = 0
    for _lineno, e in iter_ndjson(events_path):
        if e.get("emit_type") == "delivered":
            n += 1
    return n


def logical_total_keys(logical):
    """★★★★ [[B182]] item 7 — THE LOGICAL-CORRELATION RESIDUE, AS `totals.logical_*` KEYS.

    ⛔⛔ WHY EVERY KEY IS ALWAYS PRESENT, EVEN AT ZERO, AND WHY THEY ARE PRINTED IN THE TEXT TABLE TOO:
    `alias_stats` was computed, returned to `main()` and then DISCARDED — three mentions, zero readers —
    while being NONZERO on the corpus. A refusal counter nobody reads is not a safeguard, and an ABSENT
    key is indistinguishable from a zero to a consumer. So a clean scenario prints `0`s, and a reader
    sees the residue instead of inferring soundness from a clean total.
    ★ `logical_lastmile_correlated` and `logical_dst_from_delivery` are the POSITIVE controls: if the
    two-layer machinery ever stops working they go to zero while the refusals stay zero too, which is
    the "fails toward nothing happened" shape this file exists to make visible."""
    lg = logical or {}
    return {f"logical_{k}": int(lg.get(k, 0)) for k in (
        "lastmile_correlated", "lastmile_refused_ambiguous", "lastmile_unmatched",
        "refused_wire_key_shared", "refused_ambiguous_dst", "refused_multi_delivery",
        "refused_shared_dst_id", "refused_no_logical_src", "refused_unresolved_dst",
        "deleg_reattributed_samelayer", "deleg_reattributed_xl", "deleg_refused_ambiguous",
        "deleg_wrappers_excluded", "wire_slot_conflicts", "dst_from_delivery", "dst_from_wire")}


LOGICAL_REFUSAL_KEYS = ("logical_lastmile_refused_ambiguous", "logical_lastmile_unmatched",
                        "logical_refused_wire_key_shared", "logical_refused_ambiguous_dst",
                        "logical_refused_multi_delivery", "logical_refused_shared_dst_id",
                        "logical_refused_no_logical_src", "logical_refused_unresolved_dst",
                        "logical_deleg_refused_ambiguous")


def delivery_totals(rows, xl_stats, events_path, unparsed_sends=None, alias_stats=None,
                    logical=None):
    """★★ [[B162]] — THE AUTHORITATIVE UNIQUE-DELIVERY FIGURE, made explicit and machine-readable.

    `unique_deliveries` = sum over the configured-send rows of `arrived`, i.e. ONE count per
    configured logical send that reached its user-facing destination (cross-layer aware: arrival is
    at the resolved target, not the gateway). Each record's `arrived_ms` is set by the FIRST arrival
    only, so a retried/duplicated delivery cannot inflate it.

    ⚠ WHY THIS EXISTS AS A BLOCK RATHER THAN BEING LEFT TO THE CALLER: it was left to the caller,
    every consumer re-derived it slightly differently, and the arc's headline number stopped
    reproducing. A figure with no single named producer is a figure that drifts."""
    out = {
        "unique_deliveries":    sum(r["arrived"] for r in rows),      # ★ AUTHORITATIVE
        "configured_sends":     sum(r["sent"] for r in rows),
        "hop1_acked":           sum(r["acked"] for r in rows),
        "giveup":               sum(r["giveup"] for r in rows),
        "in_flight":            sum(r["in_flight"] for r in rows),
        "no_gateway":           sum(r.get("no_gw", 0) for r in rows),
        "unsent":               sum(r.get("unsent", 0) for r in rows),
        "cross_layer":          dict(xl_stats) if xl_stats else None,
        "raw_delivered_events": raw_delivered_event_count(events_path),   # ⛔ CROSS-CHECK ONLY
        # ★ [[B162]]: configured sends this grammar could not resolve. MUST be 0 for the figure above to
        # be a complete accounting of the scenario's intent; a non-zero value is a LOUD warning that the
        # denominator is short, not a rounding detail.
        "unresolved_configured_sends": (sum(unparsed_sends.values()) if unparsed_sends else 0),
        "unresolved_detail":  (dict(unparsed_sends) if unparsed_sends else {}),
        # ★★ [[B162b]] 2026-08-09 — THE RUNTIME-ID ALIAS REFUSALS ARE NOW EXPOSED. They were computed
        # in `analyse()` (as `alias_stats`), returned all the way out to `main()`, and then DISCARDED:
        # three mentions in the whole file, zero readers. ⛔ That was a silent-corruption channel of
        # exactly the kind this bug is about — a team/runtime-id collision empties the alias table, so
        # records stop resolving, while `unresolved_configured_sends` can stay 0 because the SEND
        # parsed fine. It is `refused_conflicts` that says the authority may be short, and nothing
        # printed it. It is printed now, in BOTH the JSON and the text output.
        "wire_alias_aliases":  (alias_stats or {}).get("aliases"),
        "wire_alias_refused_conflicts": (alias_stats or {}).get("refused_conflicts"),
        # ★★ [[B162c]] 2026-08-09 — NDJSON LINES REFUSED BY THE PARSER. Every walker used to swallow a
        # `JSONDecodeError` with a bare `continue`, so a truncated/corrupt stream measured LOW in
        # silence — the same fail-toward-nothing-happened shape as the substring fast paths this slice
        # removed. ⛔ A nonzero value means the stream was NOT fully read and EVERY figure above is a
        # LOWER BOUND. The key is always present so a zero is printable and comparable, and it is
        # printed in the TEXT output too (the `alias_stats` lesson: a counter nobody reads is not a
        # safeguard).
        "malformed_ndjson_lines": ndjson_refusals(events_path)["lines"],
        "malformed_ndjson_examples": [
            {"line": ln, "text": tx} for ln, tx in ndjson_refusals(events_path)["examples"]],
        "authority":            "unique_deliveries (first arrival per configured logical send); "
                                "raw_delivered_events is a CROSS-CHECK and is NOT the figure of record",
    }
    # ★★★★ [[B182]] item 7: the logical-correlation counters ride the SAME totals block, so the
    # authority and its residue can never again be reported apart from each other.
    out.update(logical_total_keys(logical))
    return out


def render_json(rows, msgs, pair_filter, id_to_name, detail,
                xl_stats=None, events_path=None, unparsed_sends=None, alias_stats=None,
                logical=None, slots=None):
    out = {"summary": rows}
    if events_path is not None:
        out["totals"] = delivery_totals(rows, xl_stats, events_path, unparsed_sends,
                                        alias_stats, logical)
    if detail:
        keys = []
        for k, r in msgs.items():
            lp = (r.get("logical_src"), r.get("logical_dst"))   # ★ [[B182]]: logical pair
            if pair_filter is None or lp in pair_filter:
                keys.append(k)
        keys.sort(key=lambda k: (k[0], k[1], k[2]))
        messages = []
        for k in keys:
            r = msgs[k]
            def render_fields(fields):
                """Convert known node-id fields to name(id) strings."""
                out_f = {}
                for kk, vv in fields.items():
                    if kk in NODE_ID_FIELDS and isinstance(vv, int):
                        out_f[kk] = fmt_node(vv, id_to_name)
                    else:
                        out_f[kk] = vv
                return out_f
            entry = {
                # ⓘ [[B182]]: `origin`/`dst` stay the WIRE ids (route diagnostics); the LOGICAL pair
                #   and how it was established are their own fields, so the two are never conflated.
                "origin":      fmt_node(r["origin"], id_to_name),
                "dst":         fmt_node(r["dst"], id_to_name),
                "logical_src": (slots.label(r.get("logical_src")) if slots else r.get("logical_src")),
                "logical_dst": (slots.label(r.get("logical_dst")) if slots else r.get("logical_dst")),
                "logical_basis": r.get("logical_basis"),
                "ctr":         r["ctr"],
                "payload":     r["payload"],
                "outcome":     outcome(r),
                "enqueued_ms": r["enqueued_ms"],
                "arrived_ms":  r["arrived_ms"],
                "ack_ms":      r["ack_ms"],
                "giveup_ms":   r["giveup_ms"],
                "giveup_reason": r["giveup_reason"],
                "carriers":    sorted(fmt_node(c, id_to_name)
                                      for c in r["carriers"]),
                "hops":        len(r["carriers"]),
                "events":      [
                    {"t_ms":   ev["t_ms"],
                     "node":   fmt_node(ev["node"], id_to_name),
                     "type":   ev["type"],
                     "fields": render_fields(ev["fields"])}
                    for ev in r["events"]
                ],
            }
            if r.get("via_gateway"):
                entry["via_gateway"]            = True
                entry["target"]                 = fmt_node(r.get("target_id"),
                                                           id_to_name)
                entry["target_layer_id"]        = r.get("target_layer_id")
                entry["dst_key_hash32"]         = r.get("dst_key_hash32")
                entry["arrival_at_target_ms"]   = r.get("arrival_at_target_ms")
                entry["handoff_enqueued_ms"]    = r.get("handoff_enqueued_ms")
                entry["handoff_drained_ms"]     = r.get("handoff_drained_ms")
                entry["handoff_deferred_reason"]= r.get("handoff_deferred_reason")
                entry["handoff_giveup_reason"]  = r.get("handoff_giveup_reason")
                if r.get("second_leg"):
                    entry["second_leg"]         = r["second_leg"]
                    entry["second_leg_loc"]     = r.get("second_leg_loc")
            messages.append(entry)
        out["messages"] = messages
    emit_json(out, events_path)          # ★ [[B162d]] the one JSON sink; attaches the refusal census


CHANNEL_EVENT_TYPES = {
    "channel_msg_received",
    "channel_msg_overheard",
    "channel_msg_pulled",
    "channel_msg_already_present",
    "channel_msg_seen_by_neighbour",
    "channel_pull_sent",
    "channel_pull_received",
    "channel_pull_suppressed",
    "channel_overhear_armed",
    "channel_overhear_skipped_already_have",
    "channel_overhear_missed",
    "channel_broadcast_deduped",
    "channel_dirty_cleared",
    "channel_digest_emitted",
}

# data fields rendered when present, in channel-detail timeline lines.
CHANNEL_TIMELINE_FIELDS = (
    "source", "from", "to", "next", "channel_id",
    "reason", "overheard_from", "peer", "ad_count", "threshold",
    "chosen_data_sf", "addressed", "guard_ms",
)


def analyse_channel(events_path, slot_to_id, posts, name_to_id):
    """Walk events to find each post's msg_id, recipients, and event timeline.

    Each `send_channel` command gets matched to the originator's
    `channel_msg_received{source=self_originate}` event by
    (sender_id, channel_id, payload). That event carries the 32-bit
    `id` which uniquely identifies the post network-wide; every
    subsequent channel-* event with the same id is part of this
    post's lifecycle.

    Two-tier matching:
      - Events with explicit `id` -> matched by id
      - `channel_pull_received` carries `channel_ids[]` (a Q frame
        may request multiple ids in one frame); each requested id is
        added independently to its post's timeline.
      - `channel_digest_emitted` carries `dirty_ids[]`; same.
    """
    by_key = {}
    for p in posts:
        k = (p["sender_id"], p["channel_id"], p["payload"])
        by_key.setdefault(k, []).append(p)
        p["msg_id"] = None
        p["originated_ms"] = None
        p["recipients"] = {}
        p["already_present"] = 0
        p["events"] = []

    # Pass 1: find msg_id from each post's self_originate event.
    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        if et != "channel_msg_received":
            continue
        if d.get("source") != "self_originate":
            continue
        k = (fid, d.get("channel_id"), d.get("payload"))
        bucket = by_key.get(k)
        if not bucket:
            continue
        for p in bucket:
            if p["msg_id"] is None:
                p["msg_id"] = d.get("id")
                p["originated_ms"] = t_ms
                break

    by_msg_id = {p["msg_id"]: p for p in posts if p["msg_id"] is not None}
    # Partial-match index for overhear events that carry (sender, ctr_lo)
    # rather than the full 32-bit id. channel_msg_id_t layout (per
    # PROTOCOL §3.4.1): id = (origin<<24) | (keyhash_lo16<<8) | ctr_lo.
    by_sender_ctrlo = {}
    for mid, p in by_msg_id.items():
        sender = (mid >> 24) & 0xff
        ctr_lo = mid & 0xff
        by_sender_ctrlo[(sender, ctr_lo)] = p

    def _push_event(p, t_ms, fid, et, d, extra_id_field=None):
        """Append a copy of the event to the post's timeline."""
        fields = {k: d[k] for k in CHANNEL_TIMELINE_FIELDS if k in d}
        if extra_id_field is not None:
            fields["_id_in_list"] = extra_id_field
        p["events"].append({"t_ms": t_ms, "node": fid,
                            "type": et, "fields": fields})

    # Per-event-type keying — see the keys-by-type table in
    # CHANNEL_EVENT_TYPES discovery.
    SINGLE_ID_EVENTS = {
        "channel_msg_received",
        "channel_msg_overheard",
        "channel_msg_already_present",
        "channel_msg_seen_by_neighbour",
        "channel_broadcast_deduped",
        "channel_dirty_cleared",
    }
    MULTI_ID_EVENTS = {
        "channel_pull_sent",
        "channel_pull_received",
        "channel_pull_suppressed",
        "channel_msg_pulled",
    }
    SENDER_CTRLO_EVENTS = {
        "channel_overhear_armed",
        "channel_overhear_skipped_already_have",
        "channel_overhear_missed",
    }

    # Pass 2: collect recipient state + per-post event timeline.
    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        if et not in CHANNEL_EVENT_TYPES:
            continue
        if et in SINGLE_ID_EVENTS:
            p = by_msg_id.get(d.get("id"))
            if p is None:
                continue
            _push_event(p, t_ms, fid, et, d)
            if et == "channel_msg_received":
                if d.get("source") == "self_originate":
                    continue
                if fid not in p["recipients"]:
                    p["recipients"][fid] = {
                        "source": d.get("source"),
                        "from":   d.get("from"),
                        "t_ms":   t_ms,
                    }
            elif et == "channel_msg_already_present" and fid in p["recipients"]:
                p["already_present"] += 1
        elif et in MULTI_ID_EVENTS:
            ids = d.get("ids") or []
            for mid in ids:
                p = by_msg_id.get(mid)
                if p is None:
                    continue
                _push_event(p, t_ms, fid, et, d, extra_id_field=mid)
        elif et in SENDER_CTRLO_EVENTS:
            sender = d.get("sender")
            ctr_lo = d.get("ctr_lo")
            if sender is None or ctr_lo is None:
                continue
            p = by_sender_ctrlo.get((sender, ctr_lo))
            if p is None:
                continue
            _push_event(p, t_ms, fid, et, d)
        elif et == "channel_digest_emitted":
            # Originator's BCN included these dirty ids in its digest.
            ids = d.get("dirty_ids") or d.get("ids") or []
            for mid in ids:
                p = by_msg_id.get(mid)
                if p is None:
                    continue
                _push_event(p, t_ms, fid, et, d, extra_id_field=mid)

    # Pass 3: per-node "dirty window" for each msg — between first
    # observation (self_originate or channel_msg_received) and the
    # corresponding channel_dirty_cleared (or run end). Any BCN tx
    # by that node within that window is a CANDIDATE carrier of the
    # msg's digest (the digest TLV holds up to K=3 dirty ids; a BCN
    # may carry zero or one of any given dirty id depending on the
    # rotation, so this is a heuristic, not proof).
    by_msg_id = {p["msg_id"]: p for p in posts if p["msg_id"] is not None}
    node_dirty = defaultdict(dict)   # fid -> msg_id -> [start, end]
    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        mid = d.get("id")
        if mid not in by_msg_id:
            continue
        if et == "channel_msg_received":
            # First observation marks the start; subsequent
            # already_present events don't reset it.
            if mid not in node_dirty[fid]:
                node_dirty[fid][mid] = [t_ms, None]
        elif et == "channel_dirty_cleared":
            window = node_dirty[fid].get(mid)
            if window is not None and window[1] is None:
                window[1] = t_ms

    # Pass 4: physical-layer events for M-broadcasts + BCN ads.
    # - DATA-M tx (or tx_deferred) parses `id=0xHEX` from the M-payload
    #   tag inside `info`/`tx_info`; matches to msg_id directly.
    # - BCN tx events are attributed to all msgs whose dirty window
    #   contains this BCN tx's time at this node.
    # - rx / drop events are matched by `pkt` hash to a known tx.
    id_hex_re = re.compile(r"id=0x([0-9a-fA-F]+)")
    pkt_to_posts = {}    # pkt_hash -> {posts...} (BCN can carry multi)
    pkt_kind = {}        # pkt_hash -> "bcn" | "data_m"

    def _phy_data_extract(rec):
        """Extract msg id from the event's info string.

        `tx` events use field `info`; `tx_deferred` uses `tx_info`.
        Both formats embed `id=0xHEX` in the M-broadcast payload.
        """
        info = rec.get("info") or rec.get("tx_info") or ""
        m = id_hex_re.search(info)
        if m:
            return int(m.group(1), 16)
        return None

    # Need a name -> firmware id resolver for BCN attribution (phy
    # events carry node name strings). Build from posts: sender_id +
    # whatever is in id_bind in the events stream — but simpler to
    # rebuild from cfg passed via posts (each post knows its sender_id
    # but not the name->id map). The caller already has name_to_id;
    # we re-derive it inside this function from the unique sender_id
    # values + the events stream's own name<->slot inference is too
    # noisy. So instead, derive name->id from the script_emit pass:
    # tx_enqueue events have data.origin (firmware id) and the event's
    # `node` is the slot (we don't have the name there either).
    # Cleanest: peek at any phy event with `node` (string) and look up
    # its firmware id from a `tx_enqueue` script_emit at the same
    # event index — but that's heavy. Pragmatic shortcut: build name
    # -> id by reading phy `tx` events' `node` and matching to script
    # `tx_enqueue` events at the same t_ms.
    # For now, scan one extra pass over phy events to collect name set,
    # then look them up against the caller-provided id mapping below.
    # We accept the cost: walk phy events once to gather names, then
    # delegate name resolution to main() via a post-hook. Simplest:
    # store the raw phy events on the post and resolve later. Done in
    # render_channel_detail via name_to_id_local.

    for t_ms, phy_t, e in walk_phy_events(events_path, None):
        label = e.get("label")
        # --- DATA-M (channel broadcast) tx side ---
        if phy_t in ("tx", "tx_deferred") and label == "DATA-M":
            mid = _phy_data_extract(e)
            if mid is None:
                continue
            p = by_msg_id.get(mid)
            if p is None:
                continue
            p["events"].append({
                "t_ms": t_ms,
                "node_name": e.get("node"),
                "type": f"phy_{phy_t}",
                "fields": {
                    "label": label,
                    "sf": e.get("sf"),
                    "reason": e.get("reason"),
                    "busy_until_ms": e.get("busy_until_ms"),
                    "airtime_ms": e.get("airtime_ms"),
                    "pkt": e.get("pkt"),
                },
            })
            if phy_t == "tx" and e.get("pkt"):
                pkt_to_posts.setdefault(e["pkt"], set()).add(id(p))
                pkt_kind[e["pkt"]] = "data_m"
        # --- BCN tx side: attribute to every post whose dirty window
        #     at this node contains this t_ms ---
        elif phy_t == "tx" and label == "BCN":
            tx_name = e.get("node")
            tx_fid = name_to_id.get(tx_name) if tx_name else None
            if tx_fid is None:
                continue
            relevant_posts = []
            for mid, window in node_dirty.get(tx_fid, {}).items():
                start, end = window[0], window[1]
                if start is None:
                    continue
                if start <= t_ms and (end is None or t_ms <= end):
                    relevant_posts.append(by_msg_id[mid])
            if not relevant_posts:
                continue
            pkt = e.get("pkt")
            for p in relevant_posts:
                p["events"].append({
                    "t_ms": t_ms,
                    "node_name": tx_name,
                    "type": "phy_bcn_tx",
                    "fields": {
                        "label": "BCN",
                        "sf": e.get("sf"),
                        "airtime_ms": e.get("airtime_ms"),
                        "pkt": pkt,
                        # Mark heuristic: this BCN's dirty digest MAY
                        # have carried the msg id; we can't tell from
                        # the wire dump alone.
                        "in_dirty_window": True,
                    },
                })
                if pkt:
                    pkt_to_posts.setdefault(pkt, set()).add(id(p))
                    pkt_kind[pkt] = "bcn"
        # --- RX / drop side: match by pkt against either DATA-M or
        #     BCN known transmissions ---
        elif phy_t in ("rx", "collision", "drop_halfduplex",
                       "drop_sf_mismatch", "drop_preamble_miss", "drop_rx_blind"):
            pkt = e.get("pkt")
            post_ids = pkt_to_posts.get(pkt)
            if not post_ids:
                continue
            kind = pkt_kind.get(pkt, "?")
            # Reverse lookup id(p) -> p so we can iterate.
            id_to_post_obj = {id(p): p for p in posts}
            for pid in post_ids:
                p = id_to_post_obj.get(pid)
                if p is None:
                    continue
                p["events"].append({
                    "t_ms": t_ms,
                    "node_name": e.get("to") or e.get("node"),
                    "type": f"phy_{phy_t}_{kind}",
                    "fields": {
                        "from": e.get("from"),
                        "snr": e.get("snr"),
                        "rssi": e.get("rssi"),
                        "sf": e.get("sf"),
                        "pkt": pkt,
                    },
                })

    # Per-post derived stats.
    for p in posts:
        # Sort events; mix firmware-id and name-keyed events using
        # t_ms then a stable string.
        def sort_key(x):
            return (x["t_ms"], str(x.get("node", x.get("node_name", ""))))
        p["events"].sort(key=sort_key)
        # Cascade-depth tree: BFS-style fixed point on the `from` edges.
        # depth(origin)=0; depth(recipient)=depth(from)+1 if `from` is
        # known to have received the msg. Falls back to None for any
        # recipient whose `from` is missing or never resolves (e.g.
        # overhear with no from field, or pre-warmup-state weirdness).
        depths = {p["sender_id"]: 0}
        changed = True
        while changed:
            changed = False
            for rcv_id, info in p["recipients"].items():
                if rcv_id in depths:
                    continue
                from_id = info.get("from")
                if from_id is None:
                    continue
                if from_id in depths:
                    depths[rcv_id] = depths[from_id] + 1
                    info["depth"] = depths[rcv_id]
                    changed = True
        # Track unresolved depths (recipients whose `from` chain never
        # reaches origin — usually a sign of stale `from` data).
        for rcv_id, info in p["recipients"].items():
            if rcv_id not in depths:
                info["depth"] = None
        # Count secondary holders that re-broadcast: channel_msg_pulled
        # events fire at the holder when it sends an M-payload in
        # response to a Q. Count distinct nodes that fired this for the
        # post id.
        broadcasters = set()
        pulls_sent = 0
        for ev in p["events"]:
            if ev["type"] == "channel_msg_pulled":
                broadcasters.add(ev["node"])
            elif ev["type"] == "channel_pull_sent":
                pulls_sent += 1
        p["depths"] = {rid: depths[rid] for rid in p["recipients"]
                       if rid in depths}
        depth_vals = [v for v in p["depths"].values() if v is not None]
        p["max_depth"] = max(depth_vals) if depth_vals else None
        p["mean_depth"] = (sum(depth_vals) / len(depth_vals)
                           if depth_vals else None)
        p["broadcasters"] = broadcasters         # includes origin if it
                                                 # also responded to pulls
        p["pulls_sent"] = pulls_sent
    return posts


def summarise_channel(posts, cfg, id_to_name):
    """Per-post rows: reach, expected, sources, leaks."""
    # Same-layer non-gateway node count per layer.
    per_layer_nongw = defaultdict(int)
    node_layer = {}
    node_is_gw = {}
    for n in cfg["nodes"]:
        nid = n["node_id"]
        cfg_block = n.get("config") or {}
        layer = cfg_block.get("layer_id")
        is_gw = bool(cfg_block.get("is_gateway"))
        node_layer[nid] = layer
        node_is_gw[nid] = is_gw
        if not is_gw:
            per_layer_nongw[layer] += 1

    rows = []
    for p in posts:
        same_layer = 0
        leaks = 0
        sources = defaultdict(int)
        first_recv_ms = None
        last_recv_ms = None
        for rcv_id, info in p["recipients"].items():
            rcv_layer = node_layer.get(rcv_id)
            sources[info["source"] or "unknown"] += 1
            if rcv_layer == p["sender_layer"]:
                same_layer += 1
            else:
                leaks += 1
            if first_recv_ms is None or info["t_ms"] < first_recv_ms:
                first_recv_ms = info["t_ms"]
            if last_recv_ms is None or info["t_ms"] > last_recv_ms:
                last_recv_ms = info["t_ms"]
        expected = max(0, per_layer_nongw.get(p["sender_layer"], 0) - 1)
        spread_ms = (last_recv_ms - first_recv_ms) \
                    if (first_recv_ms is not None and last_recv_ms is not None) \
                    else None
        first_lat_ms = (first_recv_ms - p["originated_ms"]) \
                       if (first_recv_ms is not None
                           and p["originated_ms"] is not None) else None
        rows.append({
            "sender":      fmt_node(p["sender_id"], id_to_name),
            "layer":       p["sender_layer"],
            "channel_id":  p["channel_id"],
            "payload":     p["payload"],
            "sent_at_ms":  p["sent_at_ms"],
            "msg_id":      p["msg_id"],
            "reach":       same_layer,
            "expected":    expected,
            "leaks":       leaks,
            "sources":     dict(sources),
            "already_present": p["already_present"],
            "first_recv_lat_ms": first_lat_ms,
            "spread_ms":   spread_ms,
            "max_depth":   p.get("max_depth"),
            "mean_depth":  p.get("mean_depth"),
            "broadcasters": len(p.get("broadcasters") or []),
            "pulls_sent":  p.get("pulls_sent", 0),
        })
    return rows


def render_channel_table(rows):
    if not rows:
        print("(no channel posts in scenario)")
        return
    header = ["post (sender / payload)", "ch", "L",
              "reach", "reach%", "sources", "lat_ms", "depth",
              "bcst", "pulls", "leaks"]
    fmt = ("{:<38} {:>3} {:>2} {:>7} {:>6} "
           "{:<20} {:>7} {:>6} {:>4} {:>5} {:>5}")
    print(fmt.format(*header))
    print("-" * 112)
    total_reach = 0
    total_expected = 0
    total_leaks = 0
    for r in rows:
        reach_str = f"{r['reach']}/{r['expected']}"
        pct = (f"{100*r['reach']/r['expected']:.0f}%"
               if r["expected"] else "-")
        src_str = " ".join(
            f"{k[:3]}:{v}" for k, v in
            sorted(r["sources"].items(), key=lambda kv: -kv[1])
        ) if r["sources"] else "-"
        lat = (f"{r['first_recv_lat_ms']}"
               if r["first_recv_lat_ms"] is not None else "-")
        depth_str = (f"{r['max_depth']}"
                     if r["max_depth"] is not None else "-")
        head = f"{r['sender']:<14} {r['payload'][:22]:<22}"
        print(fmt.format(head, r["channel_id"], r["layer"],
                         reach_str, pct, src_str, lat,
                         depth_str, r["broadcasters"],
                         r["pulls_sent"], r["leaks"]))
        total_reach += r["reach"]
        total_expected += r["expected"]
        total_leaks += r["leaks"]
    print("-" * 112)
    pct = (f"{100*total_reach/total_expected:.0f}%"
           if total_expected else "-")
    print(fmt.format(
        f"TOTAL ({len(rows)} posts)",
        "-", "-",
        f"{total_reach}/{total_expected}", pct, "-", "-", "-", "-", "-",
        total_leaks))


def render_channel_detail(rows_meta, id_to_name, post_filter):
    """Per-post timeline. rows_meta is the list of post records
    (returned by analyse_channel), each with msg_id, recipients, events.
    """
    for p in rows_meta:
        if post_filter is not None:
            pat = post_filter.lower()
            if pat not in (p.get("payload") or "").lower():
                continue
        sender_n = fmt_node(p["sender_id"], id_to_name)
        head_parts = [
            f"=== {sender_n} -> ch{p['channel_id']} "
            f'"{p.get("payload","")}"',
            f"L{p.get('sender_layer')}",
            f"id=0x{(p.get('msg_id') or 0):08X}",
            f"reach={len(p['recipients'])}",
        ]
        if p.get("max_depth") is not None:
            head_parts.append(f"max_depth={p['max_depth']}")
            head_parts.append(f"mean_depth={p['mean_depth']:.2f}")
        if p.get("broadcasters"):
            head_parts.append(f"broadcasters={len(p['broadcasters'])}")
        head_parts.append(f"pulls_sent={p.get('pulls_sent', 0)}")
        if p.get("originated_ms") is not None:
            head_parts.append(f"orig={p['originated_ms']}ms")
        # Cascade total time: first->last recipient.
        if p["recipients"]:
            first_ms = min(info["t_ms"] for info in p["recipients"].values())
            last_ms = max(info["t_ms"] for info in p["recipients"].values())
            head_parts.append(f"first_recv={first_ms}ms")
            head_parts.append(f"last_recv={last_ms}ms")
            if p.get("originated_ms") is not None:
                head_parts.append(f"first_lat={first_ms - p['originated_ms']}ms")
                head_parts.append(f"cascade={last_ms - first_ms}ms")
        head_parts.append("===")
        print(" ".join(head_parts))
        # Recipients grouped by source + depth.
        if p["recipients"]:
            # Sort all recipients by depth then time so the cascade reads
            # top-down. Show one line per recipient.
            rcv_sorted = sorted(
                p["recipients"].items(),
                key=lambda kv: (kv[1].get("depth") if kv[1].get("depth") is not None else 99,
                                kv[1]["t_ms"])
            )
            for rcv_id, info in rcv_sorted:
                lat = (info["t_ms"] - p["originated_ms"]
                       if p.get("originated_ms") is not None else None)
                lat_s = f"+{lat}ms" if lat is not None else "?"
                depth = info.get("depth")
                depth_s = f"depth={depth}" if depth is not None else "depth=?"
                from_s = (f"from={fmt_node(info['from'], id_to_name)}"
                          if info.get("from") is not None else "from=?")
                src = info.get("source") or "unknown"
                print(f"  recv {fmt_node(rcv_id, id_to_name):<12} "
                      f"{depth_s:<9} {src:<14} {from_s:<18} "
                      f"@ {info['t_ms']:>8}ms ({lat_s})")
        else:
            print("  recipients: (none)")
        if not p["events"]:
            print("  (no channel-plane events captured for this id)")
            print()
            continue
        # Need a name->id helper for PHY events (which carry name strings).
        # Build it lazily from id_to_name's reverse.
        name_to_id_local = {v: k for k, v in id_to_name.items()}
        for ev in p["events"]:
            if "node" in ev:
                node_n = fmt_node(ev["node"], id_to_name)
            else:
                nm = ev.get("node_name")
                node_n = fmt_node(name_to_id_local.get(nm), id_to_name) \
                         if nm in name_to_id_local else (nm or "?")
            rendered = []
            for kk, vv in ev["fields"].items():
                if vv is None:
                    continue
                if kk in NODE_ID_FIELDS and isinstance(vv, int):
                    rendered.append(f"{kk}={fmt_node(vv, id_to_name)}")
                elif kk == "from" and isinstance(vv, str):
                    # PHY rx events carry `from` as a name string.
                    fid = name_to_id_local.get(vv)
                    rendered.append(f"{kk}={fmt_node(fid, id_to_name)}"
                                     if fid is not None else f"{kk}={vv}")
                else:
                    rendered.append(f"{kk}={vv}")
            field_str = " ".join(rendered)
            ftype = frame_type_for(ev)
            print(f"  {ev['t_ms']:>8} ms  [{ftype:>3}] {node_n:<12} "
                  f"{ev['type']:<40} {field_str}")
        print()


# Fields shown (when present) on each --trace line, in order. node-id fields are
# rendered name(id). Covers the same-layer path AND the cross-layer gateway chain.
_TRACE_FIELDS = ("origin", "dst", "next", "via_gateway", "gateway", "next_gateway",
                 "target_layer_id", "hops", "remaining_hops", "entered_layer",
                 "next_layer", "dst_key_hash32", "ctr", "ctr_lo", "attempt_seq", "reason")
_TRACE_NODE_FIELDS = {"origin", "dst", "next", "via_gateway", "gateway", "next_gateway"}


def render_trace(events_path, substr, slot_to_id, id_to_name):
    """Follow a message (or messages) end-to-end through every event that touches
    it, including the cross-layer gateway chain (transit / handoff / no_binding /
    H-query / remote-bind) that the per-message --detail timeline omits.

    Selection: any script_emit whose data has a string field containing SUBSTR
    (case-insensitive) -- this catches the origin send + the delivery (both carry
    the payload). We then also follow each `dst_key_hash32` seen on a matching
    event, so the gateway-side events (which carry the hash, not the text) come
    along. Output is one chronological line per event."""
    sub = substr.lower()

    def nm(nid):
        n = id_to_name.get(nid)
        return f"{n}({nid})" if n is not None else str(nid)

    def matches(d):
        for v in d.values():
            if isinstance(v, str) and sub in v.lower():
                return True
        return False

    # ⓘ [[B162c]]: both passes go through the one reader, so a corrupt line is refused and counted here
    # too rather than silently dropped by a bare `except ValueError: continue`.
    hashes = set()
    for _lineno, e in iter_ndjson(events_path):
        if e.get("type") == "script_emit" and matches(e.get("data", {})):
            h = e["data"].get("dst_key_hash32")
            if isinstance(h, int):
                hashes.add(h)

    rows = []
    for _lineno, e in iter_ndjson(events_path):
        if e.get("type") != "script_emit":
            continue
        d = e.get("data", {})
        if matches(d) or (isinstance(d.get("dst_key_hash32"), int)
                          and d["dst_key_hash32"] in hashes):
            rows.append(e)
    rows.sort(key=lambda e: e.get("time_ms", 0))

    print(f"TRACE '{substr}': {len(rows)} events; following hashes {sorted(hashes)}")

    def _node(e):
        idx = e.get("node")
        return nm(slot_to_id.get(idx, idx)) if isinstance(idx, int) else str(idx)
    node_w = max([len(_node(e)) for e in rows] + [4])   # auto-size for long names
    for e in rows:
        d = e.get("data", {})
        parts = []
        for k in _TRACE_FIELDS:
            if k in d:
                v = d[k]
                if k in _TRACE_NODE_FIELDS and isinstance(v, int):
                    v = nm(v)
                parts.append(f"{k}={v}")
        pl = d.get("payload")
        if isinstance(pl, str):
            parts.append(f"pl={pl[:20]}")
        print(f"  t={e.get('time_ms', 0):>8}  {_node(e):{node_w}s} "
              f"{e['emit_type']:30s} {' '.join(parts)}")


def analyse_copies(events_path, slot_to_id):
    """Count COPY-CREATING SWITCHES.

    A copy-creating switch is a forward that abandoned a next-hop which had
    ALREADY decoded the frame (emitted data_rx for that origin,ctr) and switched
    to a DIFFERENT node -> a 2nd live copy of a frame the abandoned hop already
    holds (and will forward). The four switch events that move a flight to a
    fresh next-hop are: path_cascade, tx_blind_alt, tx_silent_alt,
    tx_stale_next_alt. A switch whose abandoned hop never decoded is a legit
    reroute, NOT a copy.

    Also tallies per-message fan-out = distinct nodes that decoded each
    (origin,ctr) — copies inflate this above the delivering path length.

    Returns (decoders, payloads, switches, copies, reroutes):
      decoders[(origin,ctr)] = {node_id: earliest_data_rx_ms}
      payloads[(origin,ctr)] = a payload sample (str) for display
      switches / copies      = [(t, origin, ctr, from_next, to_next, label, payload)]
      reroutes               = int
    """
    SWITCH = {"path_cascade", "tx_blind_alt", "tx_silent_alt", "tx_stale_next_alt"}
    LABEL = {"tx_blind_alt": "blind_alt", "tx_silent_alt": "silent_alt",
             "tx_stale_next_alt": "stale_next"}
    decoders = defaultdict(dict)
    payloads = {}
    switches = []
    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        if et == "data_rx":
            key = (d.get("origin"), d.get("ctr"))
            if key[0] is None or key[1] is None:
                continue
            prev = decoders[key].get(fid)
            if prev is None or t_ms < prev:
                decoders[key][fid] = t_ms
            if key not in payloads and isinstance(d.get("payload"), str):
                payloads[key] = d["payload"]
        elif et in SWITCH:
            label = d.get("trigger") if et == "path_cascade" else LABEL[et]
            switches.append((t_ms, d.get("origin"), d.get("ctr"),
                             d.get("from_next"), d.get("to_next"),
                             label or et, d.get("payload")))
    copies, reroutes = [], 0
    for sw in switches:
        t, o, c, frm = sw[0], sw[1], sw[2], sw[3]
        rx = decoders.get((o, c), {})
        if frm in rx and rx[frm] <= t:   # abandoned hop decoded BEFORE the switch
            copies.append(sw)
        else:
            reroutes += 1
    return decoders, payloads, switches, copies, reroutes


def render_copies(events_path, slot_to_id, id_to_name, top=12):
    def nm(nid):
        n = id_to_name.get(nid)
        return f"{n}({nid})" if n is not None else str(nid)

    decoders, payloads, switches, copies, reroutes = analyse_copies(
        events_path, slot_to_id)
    by_label = Counter(sw[5] for sw in copies)

    print("=== Copies (copy-creating switches) ===")
    print("A copy-creating switch abandons a next-hop that had ALREADY decoded")
    print("the frame (data_rx for that origin,ctr) and forwards to a DIFFERENT")
    print("node -> a 2nd live copy. Switch events: path_cascade / tx_blind_alt /")
    print("tx_silent_alt / tx_stale_next_alt.\n")
    print(f"copy-creating switches : {len(copies)}")
    for label, n in sorted(by_label.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"    {label:<16} {n}")
    print(f"reroute switches (abandoned hop never decoded — not a copy) : {reroutes}")
    print(f"total switches         : {len(switches)}")

    fan = sorted(((k, len(v)) for k, v in decoders.items()), key=lambda kv: -kv[1])
    if fan:
        mean = sum(n for _, n in fan) / len(fan)
        print(f"\nfan-out (distinct decoders per origin,ctr): {len(fan)} messages, "
              f"mean {mean:.1f} decoders/msg")
        print(f"    {'(origin,ctr)':>14}  {'decoders':>8}  payload")
        for (o, c), n in fan[:top]:
            pl = payloads.get((o, c), "")
            print(f"    {str((o, c)):>14}  {n:>8}  {str(pl)[:28]}")

    if copies:
        det = sorted(copies)[:top]
        w = max([len(nm(s[3])) for s in det] + [len(nm(s[4])) for s in det]
                + [len("abandoned"), len("to")])   # auto-size for long names
        print(f"\n  copy detail (first {len(det)} of {len(copies)}):")
        print(f"    {'t_ms':>8}  {'abandoned':>{w}} -> {'to':<{w}} {'trigger':<12} payload")
        for (t, o, c, frm, to, label, pl) in det:
            print(f"    {t:>8}  {nm(frm):>{w}} -> {nm(to):<{w}} {label:<12} {str(pl)[:24]}")


EMIT_PHY_LABEL = {"rts_tx": "RTS", "rts_retry": "RTS", "rts_fwd": "RTS",
                  "cts_tx": "CTS", "ack_tx": "ACK", "nack_tx": "NACK", "data_tx": "DATA"}


# base labels whose airtime this tool charges to the DM/routing budget. BCN/Q/H are aired too, but
# they are not part of the per-message DM budget and were never in any figure this tool quoted —
# so they are censused and EXCLUDED from the total, explicitly, rather than being silently absent.
CHARGEABLE_PHY_LABELS = {"RTS", "CTS", "ACK", "NACK", "DATA"}


def phy_tx_frames(events_path, name_to_id):
    """★★★ [[B162b]] 2026-08-09 — THE PHY LOG IS GROUND TRUTH FOR AIRTIME. THIS IS THE PRIMARY PASS.

    ⛔⛔ WHAT CHANGED AND WHY IT IS AN INVERSION, NOT A PATCH. Until now this function was
    `frame_lengths_by_tx()`: an INDEX built for correlation, and `analyse_airtime` summed only the
    frames it managed to correlate to a firmware `*_tx` emit. ⇒ SUCCESSFUL ATTRIBUTION WAS A
    PRECONDITION FOR COUNTING, so every correlation failure silently REDUCED the total. A measurement
    that fails toward "less airtime" fails in exactly the wrong direction: it can only ever flatter
    the protocol, and it did — MEASURED on the corpus (arm CURRENT): **2557 of 23913 chargeable frames
    (10.7%) really aired and were charged ZERO.**

    ★ THE SHAPE NOW: the airtime TOTAL is computed here, directly, from EVERY chargeable PHY `tx`
    event, each priced with ITS OWN `sf`, `bw_hz`, `cr` and byte count — and cross-checked against
    that same event's own `airtime_ms`. Correlation to emits is a SECONDARY ATTRIBUTION VIEW
    (`analyse_airtime`), and anything it cannot attribute lands in an explicit
    `unattributed_airtime` bucket that is INCLUDED IN THE TOTAL. ⛔ AN AIRED FRAME NEVER VANISHES.

    Returns `(frames, index, ambiguous, census, xcheck)`:
      · `frames`  — every chargeable frame, in stream order, each with its own priced `ms`. THE TOTAL
                    IS `sum(f["ms"] for f in frames)` and it does not depend on any correlation.
      · `index`   — (firmware node id, base label, time_ms) -> frame, for the attribution view only.
                    ⓘ The two streams key nodes differently (PHY `tx` carries the node NAME,
                    `script_emit` the slot index), so the key uses the firmware id via `name_to_id`.
      · `ambiguous` — ★ EVERY key seen more than once, REGARDLESS OF LENGTH. ⛔ The previous version
                    marked a duplicate ambiguous only `if index[k] != nbytes`, so TWO SAME-LENGTH
                    transmissions in one millisecond collapsed into one — silently, and in direct
                    contradiction of this docstring's own contract. Same length is not same frame.
                    (Measured on the corpus: ZERO duplicate keys of either kind — but that is now a
                    measurement of the strict rule, not of a rule that could not have caught it.)
      · `census`  — per-label wire-length histogram over ALL tx labels, every one codec-validated.
      · `xcheck`  — agreement between this file's LoRa formula and the PHY's own `airtime_ms`,
                    with a deliberately-wrong `len+1` CONTROL so the agreement can be seen to be
                    discriminating rather than vacuous."""
    frames = []
    census = defaultdict(Counter)
    seen = Counter()
    # ★ keys pre-seeded to 0 so the reported shape is CONSTANT: an absent key reads as "no data" to a
    # human and raises KeyError in a consumer, and both failure modes here have historically been read
    # as "zero, fine". A zero must be printable and comparable ([[B162]] method rule).
    xcheck = Counter({"frames": 0, "agree": 0, "disagree": 0, "control_len_plus1_agree": 0})
    # ★★ [[B162c]] 2026-08-09 — THE SUBSTRING FAST PATH IS GONE. This loop used to open with
    #   `if '"type":"tx"' not in line: continue`, which is a PARSER made of `str.__contains__`: valid
    #   NDJSON with ordinary spacing (`{"type": "tx", ...}`) matched nothing, so `frames` came back
    #   EMPTY and the authoritative airtime total printed **0 ms over 0 frames** as a measurement.
    #   ⛔ Never re-add it. A substring fast-path is a silent parser, and a silent parser in a
    #   measurement path fails toward "nothing happened" — the third time this arc has been bitten by
    #   exactly that shape. Malformed lines are now REFUSED, COUNTED and SURFACED by `iter_ndjson`
    #   instead of being swallowed by the bare `except ValueError: continue` that stood here.
    for _lineno, e in iter_ndjson(events_path):
        if e.get("type") != "tx":
            continue
        hexs = e.get("hex")
        if hexs is None:
            # ⛔ FAIL LOUD: no bytes means no length, and a length is exactly what must not be guessed.
            raise FrameShapeError(
                f"PHY tx event at t={e.get('time_ms')} node={e.get('node')!r} "
                f"label={e.get('label')!r} carries NO `hex` — the frame length cannot be "
                f"determined. ⛔ REFUSING to substitute a constant (see [[B162]]).")
        nbytes = len(hexs) // 2
        label = e.get("label")
        base = {"RTS-fwd": "RTS", "RTS-rty": "RTS", "CTS-dup": "CTS"}.get(label, label)
        census[base][nbytes] += 1
        frame_shape(label, nbytes)          # validate every frame against the codec, loudly
        if base not in CHARGEABLE_PHY_LABELS:
            continue
        # ★ PER-FRAME PHY PARAMETERS (C2 — refuse, never default). ⛔ The previous version took
        #   `bw_hz, cr = 125000, 5` from the FIRST tx event in the file and applied it to every
        #   transmission in the scenario. MEASURED: `s32_dual_cr_gateway` carries CR 4/5 AND 4/8
        #   (4 frames mispriced) and `s33_mixed_cr_channel_overhear` carries both too (24 frames),
        #   and BOTH run at BW 250 kHz, not the 125 kHz default. A mixed-PHY scenario is exactly
        #   where an airtime figure matters most, and it was exactly where the tool was wrong.
        sf, bw_hz, cr = e.get("sf"), e.get("bw_hz"), e.get("cr")
        if sf is None or bw_hz is None or cr is None:
            raise FrameShapeError(
                f"PHY tx event at t={e.get('time_ms')} node={e.get('node')!r} label={label!r} "
                f"is missing sf/bw_hz/cr (sf={sf} bw_hz={bw_hz} cr={cr}) — its airtime cannot be "
                f"computed. ⛔ REFUSING to substitute a scenario-global constant ([[B162]]).")
        formula_ms = lora_airtime_ms(sf, bw_hz, cr, nbytes)
        reported = e.get("airtime_ms")
        if reported is not None:
            xcheck["frames"] += 1
            xcheck["agree" if formula_ms == reported else "disagree"] += 1
            # ★ the CONTROL: if a one-byte-wrong length agreed just as often, the agreement above
            #   would prove nothing about the length. It must be seen to be able to disagree.
            if lora_airtime_ms(sf, bw_hz, cr, nbytes + 1) == reported:
                xcheck["control_len_plus1_agree"] += 1
        # ★★ [[B162b]] THE PRICE IS THE PHY'S OWN `airtime_ms` WHEN THE EVENT CARRIES ONE. That
        #   value IS the channel occupancy the simulator actually modelled — the thing collisions,
        #   LBT and duty cycle were computed against — whereas the formula above is a SECOND COPY
        #   re-deriving it. When a frame states a fact, read the frame. (Same lineage as §S2d's
        #   ruling: inferring from a local model what the wire declares is this arc's signature
        #   error.) ⇒ the formula's role is the CROSS-CHECK, which is exactly how it caught the
        #   missing SF5/SF6 case above. ⛔ If neither exists there is no price: refuse.
        ms = reported if reported is not None else formula_ms
        if ms is None:
            raise FrameShapeError(
                f"PHY tx event at t={e.get('time_ms')} node={e.get('node')!r} has neither an "
                f"`airtime_ms` nor derivable parameters. ⛔ REFUSING to price it ([[B162b]]).")
        fid = name_to_id.get(e.get("node"))
        key = None if fid is None else (fid, base, e.get("time_ms"))
        if key is not None:
            seen[key] += 1
        frames.append({"key": key, "label": label, "base": base, "n": nbytes,
                       "sf": sf, "bw_hz": bw_hz, "cr": cr, "ms": ms,
                       "reported": reported, "t_ms": e.get("time_ms"),
                       "node": e.get("node")})
    # ★ EVERY duplicate key is ambiguous — length is not an identity.
    ambiguous = {k for k, v in seen.items() if v > 1}
    index = {}
    for fr in frames:
        k = fr["key"]
        if k is None or k in ambiguous:
            continue
        index[k] = fr
    return (frames, index, ambiguous,
            {k: dict(sorted(v.items())) for k, v in census.items()}, xcheck)


def analyse_airtime(events_path, slot_to_id, name_to_id=None):
    """Per-message ATTRIBUTION of airtime (ms), split by category: RTS+CTS, DATA, ACK/NACK.
    RTS/DATA/ACK/NACK carry (origin,ctr); CTS carries only (to,ctr_lo), so it is attributed to the
    most recent RTS from `to`.

    ★★ [[B162]] 2026-08-09 — EVERY LENGTH IS READ OFF THE WIRE, PER FRAME. Each `*_tx` emit is
    correlated to its own PHY `tx` frame and priced at THAT frame's byte count; the old fixed
    `RTS_LEN=8 / CTS_LEN=3 / DATA_HDR_LEN+len(payload)+MAC_LEN` estimates are gone.
    ⚠ [[B162b]] CORRECTS THIS NOTE'S OWN CLAIM: it said an emit with no PHY frame at its millisecond
      "never aired". That is true of most of them but NOT of all — an LBT-DEFERRED emit airs LATER, at
      a different timestamp. So "charged zero and counted" was the right instinct applied to the wrong
      set, and the frames that HAD aired were dropped from the total. See below.

    ★★★ [[B162b]] 2026-08-09 — THIS VIEW IS NO LONGER THE TOTAL, AND THAT IS THE POINT.
      The authoritative total is `phy_total_ms`, computed by `phy_tx_frames()` over EVERY chargeable
      PHY frame. What this function does is ATTRIBUTE that airtime to messages, and attribution can
      fail. ⛔ Before this change, a failure to attribute REMOVED the frame from the total: the
      correlation key is `(node, label, time_ms)`, and an LBT-DEFERRED frame AIRS LATER, so its PHY
      timestamp does not equal its emit timestamp and it fell into `never-aired (charged 0)` —
      although it had very much aired. MEASURED on `s18` (arm CURRENT): 4898 chargeable PHY frames,
      4720 priced, **178 real frames charged nothing**; corpus-wide 2557 of 23913.
      ⇒ Everything this view cannot attribute is now summed into `unattributed_ms` and INCLUDED in
      the reported total, which therefore always equals the PHY total by construction (asserted)."""
    msgs = {}

    def m(o, c):
        k = (o, c)
        if k not in msgs:
            msgs[k] = {"dst": None, "rsf": None, "rts_air": 0, "cts_air": 0,
                       "data_air": 0, "ack_air": 0}
        return msgs[k]

    # ★ [[B162]]: the per-frame length oracle. `name_to_id` bridges the PHY stream's node NAMES to the
    # emit stream's firmware ids; without it no correlation is possible, so that is a REFUSAL, not a
    # fallback to a constant.
    if name_to_id is None:
        raise FrameShapeError("analyse_airtime needs name_to_id to correlate emits to PHY frames; "
                              "without it every length would have to be assumed ([[B162]])")
    frames, tx_index, tx_ambiguous, tx_census, xcheck = phy_tx_frames(events_path, name_to_id)
    phy_total_ms = sum(f["ms"] for f in frames)
    # counters printed beside every figure — an instrument whose misses are invisible is the defect
    # this slice exists to fix.
    stats = Counter({"priced_from_wire": 0, "unaired": 0, "emit_unattributable": 0,
                     "emit_refused_ambiguous": 0, "emit_double_attribution": 0})
    shapes = Counter()
    consumed = set()

    def phy_frame(fid, et, t_ms):
        """The PHY frame this emit actually aired, or None. ⛔ Never returns a guess, never
        double-charges one frame, and never lets a refusal shrink the total — a refused frame stays
        in `unattributed_ms`, which is added back in below."""
        label = EMIT_PHY_LABEL.get(et)
        if label is None:
            return None
        k = (fid, label, t_ms)
        if k in tx_ambiguous:
            # ⛔ REFUSE to pick a side. ⓘ [[B162b]]: this used to `raise`, which was defensible when
            # the correlated sum WAS the total. It no longer is — the frame is already counted in
            # `phy_total_ms`, so refusing costs only attribution, and aborting the whole run over an
            # attribution gap would be a worse instrument. The refusal is counted and printed loudly
            # in BOTH the text and the JSON output instead.
            stats["emit_refused_ambiguous"] += 1
            return None
        fr = tx_index.get(k)
        if fr is None:
            stats["unaired"] += 1        # no frame at this (node,label,ms): deferred, or never aired
            return None
        if k in consumed:
            # two emits claiming one frame: charge it ONCE. Counted, never silently doubled.
            stats["emit_double_attribution"] += 1
            return None
        consumed.add(k)
        stats["priced_from_wire"] += 1
        shapes[frame_shape(fr["label"], fr["n"])] += 1
        return fr

    def oc(d, fid):
        """(origin, ctr) for an airtime charge, using `msg_key`'s OWN convention (U1): a relay's
        `*_tx` emit carries no `origin`, so the EMITTING node stands in as the key's origin.
        ⚠ [[B162]] disclosure — this is a REAL correction, not cosmetics. Before it, `analyse_airtime`
        required `data["origin"]` and `continue`d without it, and NO current-vocabulary `rts_tx` /
        `data_tx` emit carries one: measured, ALL 6007 chargeable emits in `s18` were skipped, so this
        view rendered `TOTAL (0 msgs)` and the old `RTS_LEN`/`CTS_LEN` constants were never even
        reached on that scenario. A length derivation that cannot be exercised cannot be verified.
        ⇒ The per-MESSAGE rows are therefore attribution-approximate for relayed hops (the frame is
        charged to (relay, ctr), not to the originator).
        ⚠ [[B162b]] RETRACTS THIS DOCSTRING'S OWN CONCLUSION — it claimed "every aired frame is now
        counted EXACTLY ONCE at its TRUE wire length, so the TOTAL is sound". THE TOTAL WAS NOT SOUND:
        2557 of 23913 aired frames corpus-wide were charged zero because their emit's timestamp did not
        match their PHY timestamp. Soundness now comes from `phy_total_ms`, which does not depend on any
        of this, and this function's job is attribution alone."""
        o = d.get("origin")
        if o is None:
            o = d.get("src")
        if o is None:
            o = fid
        c = d.get("ctr")
        if c is None:
            c = d.get("ctr_lo")
        return o, c

    recent_rts = {}   # (rts_sender_id, ctr_lo) -> (origin, ctr)
    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        if et in ("rts_tx", "rts_retry", "rts_fwd"):
            o, c = oc(d, fid)
            if o is None or c is None:
                stats["emit_unattributable"] += 1   # no (origin,ctr) -> not chargeable to a message
                continue
            r = m(o, c)
            r["dst"] = r["dst"] if r["dst"] is not None else d.get("dst")
            fr = phy_frame(fid, et, t_ms)
            if fr is not None:
                # ★ [[B162b]] the SF is the FRAME'S OWN, not the emit's declaration. They disagree:
                #   MEASURED 1348 of 12223 correlated rts/data frames corpus-wide (arm CURRENT),
                #   concentrated in the multi-SF gateway scenarios (s16 559/963, s15 251/794).
                r["rsf"] = r["rsf"] or fr["sf"]
                r["rts_air"] += fr["ms"]
            else:
                r["rsf"] = r["rsf"] or (d.get("tx_routing_sf") or 8)   # display only; nothing charged
            recent_rts[(fid, d.get("ctr_lo"))] = (o, c)
        elif et == "cts_tx":
            k = recent_rts.get((d.get("to"), d.get("ctr_lo")))
            if k is None:
                stats["emit_unattributable"] += 1
                continue
            r = m(*k)
            fr = phy_frame(fid, et, t_ms)
            if fr is not None:
                r["cts_air"] += fr["ms"]
        elif et == "data_tx":
            o, c = oc(d, fid)
            if o is None or c is None:
                stats["emit_unattributable"] += 1
                continue
            r = m(o, c)
            r["dst"] = r["dst"] if r["dst"] is not None else d.get("dst")
            fr = phy_frame(fid, et, t_ms)
            if fr is not None:
                r["data_air"] += fr["ms"]
        elif et in ("ack_tx", "nack_tx"):
            o, c = oc(d, fid)
            if o is None or c is None:
                stats["emit_unattributable"] += 1
                continue
            r = m(o, c)
            fr = phy_frame(fid, et, t_ms)
            if fr is not None:
                r["ack_air"] += fr["ms"]

    # ★★★ [[B162b]] THE RECONCILIATION. The attributed sum plus the unattributed bucket must equal
    # the PHY total EXACTLY — that identity is what makes "an aired frame never vanishes" a checked
    # property rather than a claim, so it is ASSERTED, not merely printed.
    attributed_ms = sum(r["rts_air"] + r["cts_air"] + r["data_air"] + r["ack_air"]
                        for r in msgs.values())
    unattrib = [f for f in frames
                if f["key"] is None or f["key"] in tx_ambiguous or f["key"] not in consumed]
    unattributed_ms = sum(f["ms"] for f in unattrib)
    if attributed_ms + unattributed_ms != phy_total_ms:
        raise FrameShapeError(
            f"AIRTIME RECONCILIATION FAILED: attributed {attributed_ms} + unattributed "
            f"{unattributed_ms} != PHY total {phy_total_ms}. A frame was double-charged or lost; "
            f"⛔ refusing to report a total that does not reconcile ([[B162b]]).")
    # ★ same rule as `xcheck`: a fixed key set, so a zero is visible rather than absent.
    ua = Counter({"frames": 0, "ms": 0, "no_matching_emit": 0, "no_matching_emit_ms": 0,
                  "ambiguous_key": 0, "ambiguous_key_ms": 0,
                  "node_not_in_config": 0, "node_not_in_config_ms": 0})
    for f in unattrib:
        ua["frames"] += 1
        ua["ms"] += f["ms"]
        if f["key"] is None:
            ua["node_not_in_config"] += 1
            ua["node_not_in_config_ms"] += f["ms"]
        elif f["key"] in tx_ambiguous:
            ua["ambiguous_key"] += 1
            ua["ambiguous_key_ms"] += f["ms"]
        else:
            ua["no_matching_emit"] += 1        # ★ overwhelmingly the LBT-deferred aired frame
            ua["no_matching_emit_ms"] += f["ms"]
        ua[f"by_label:{f['base']}"] += 1
    air = {
        "phy_total_ms":        phy_total_ms,          # ★ AUTHORITATIVE
        "attributed_ms":       attributed_ms,
        "unattributed_ms":     unattributed_ms,       # ★ INCLUDED IN THE TOTAL
        "chargeable_frames":   len(frames),
        "unattributed":        dict(ua),
        "ambiguous_keys":      len(tx_ambiguous),
        "xcheck":              dict(xcheck),
        "phy_params": {"bw_hz": sorted({f["bw_hz"] for f in frames}),
                       "cr":    sorted({f["cr"] for f in frames}),
                       "sf":    sorted({f["sf"] for f in frames})},
    }
    return msgs, air, stats, shapes, tx_census, len(tx_ambiguous)


def render_airtime(events_path, slot_to_id, id_to_name, top=20, name_to_id=None,
                   as_json=False):
    def nm(nid):
        n = id_to_name.get(nid)
        return f"{n}({nid})" if n is not None else str(nid)

    msgs, air, stats, shapes, tx_census, n_amb = analyse_airtime(
        events_path, slot_to_id, name_to_id)
    if as_json:
        # ★★ [[B162d]] THROUGH `emit_json`, NOT A LOCAL `json.dump`: this payload is the AUTHORITY and
        # it was the one that carried NEITHER the refusal count nor the examples.
        emit_json({"airtime": air,
                   "attribution_counters": dict(stats),
                   "priced_shapes": dict(sorted(shapes.items())),
                   "phy_length_census": tx_census,
                   "authority": "airtime.phy_total_ms — summed DIRECTLY over every chargeable PHY "
                                "tx frame at its OWN length/sf/bw/cr. attributed_ms + "
                                "unattributed_ms == phy_total_ms (asserted). The per-message "
                                "attribution view is SECONDARY and may be incomplete; the total "
                                "never is."},
                  events_path)
        return
    rows = []
    for (o, c), r in msgs.items():
        rtscts = r["rts_air"] + r["cts_air"]
        rows.append((rtscts + r["data_air"] + r["ack_air"], o, c, r, rtscts))
    rows.sort(reverse=True)
    s_rc = sum(r["rts_air"] + r["cts_air"] for _, _, _, r, _ in rows)
    s_d = sum(r["data_air"] for _, _, _, r, _ in rows)
    s_a = sum(r["ack_air"] for _, _, _, r, _ in rows)
    s_t = s_rc + s_d + s_a
    n = len(rows) or 1
    disp = rows[:top]
    labels = [f"{nm(o)} -> {nm(r['dst'])} ({c})" for _, o, c, r, _ in disp]
    w = max([len(x) for x in labels] + [len("origin -> dst (ctr)"),
                                        len("TOTAL (%d msgs)" % len(rows))])
    xc = air["xcheck"]
    ua = air["unattributed"]
    print("=== Airtime (ms) ===")
    # ★★★ [[B162b]]: THE PHY LOG IS GROUND TRUTH; ATTRIBUTION IS A CONVENIENCE. The total is printed
    # FIRST and is summed over the PHY frames themselves, so no correlation failure can shrink it.
    print(f"★★ TOTAL AIRTIME (AUTHORITY) = {air['phy_total_ms']} ms over "
          f"{air['chargeable_frames']} chargeable PHY frames "
          f"(RTS/CTS/ACK/NACK/DATA; BCN/Q/H aired but are not in the DM budget).")
    print(f"   Each frame priced at ITS OWN length/sf/bw/cr — sf={air['phy_params']['sf']} "
          f"bw_hz={air['phy_params']['bw_hz']} cr=4/{air['phy_params']['cr']} "
          f"preamble={PREAMBLE_SYM}.")
    print(f"   CROSS-CHECK vs the PHY's own airtime_ms: {xc.get('agree', 0)}/"
          f"{xc.get('frames', 0)} agree, {xc.get('disagree', 0)} disagree "
          f"— ⓘ CONTROL: a deliberately wrong len+1 agrees on only "
          f"{xc.get('control_len_plus1_agree', 0)}, so the agreement above discriminates.")
    print(f"   = attributed {air['attributed_ms']} ms + UNATTRIBUTED {air['unattributed_ms']} ms "
          f"({ua.get('frames', 0)} frames: no-matching-emit {ua.get('no_matching_emit', 0)} "
          f"[LBT-deferred frames air LATER than their emit], ambiguous-key "
          f"{ua.get('ambiguous_key', 0)}, node-not-in-config {ua.get('node_not_in_config', 0)})")
    print(f"   ⛔ THE UNATTRIBUTED BUCKET IS PART OF THE TOTAL — an aired frame is never dropped "
          f"([[B162b]]; the previous version dropped {ua.get('frames', 0)} of them here).")
    if n_amb:
        print(f"!! {n_amb} AMBIGUOUS (node,label,ms) KEY(S) — two frames indistinguishable from the "
              f"emit stream. Their airtime is counted but NOT attributed to any message.")
    # ★ [[B162]]: print the instrument's own counts BESIDE the figures.
    print(f"★ lengths READ OFF THE WIRE per frame (no fixed RTS/CTS/DATA length — [[B162]]): "
          f"priced_from_wire={stats['priced_from_wire']}  "
          f"emits-with-no-frame-at-that-ms={stats['unaired']}  "
          f"emits-without-(origin,ctr)={stats['emit_unattributable']}  "
          f"emits-refused-ambiguous={stats['emit_refused_ambiguous']}  "
          f"double-attribution-refused={stats['emit_double_attribution']}")
    print(f"  PHY frame-length census (the codec's legal shapes, all validated): {tx_census}")
    print(f"  priced shapes: {dict(sorted(shapes.items()))}\n")
    print("--- SECONDARY VIEW: per-message attribution (approximate for relayed hops; "
          "sums to `attributed`, NOT to the total above) ---")
    print(f"  {'origin -> dst (ctr)':{w}} {'rsf':>3} {'RTS+CTS':>8} {'DATA':>7} "
          f"{'ACK':>6} {'TOTAL':>7}")
    for (tot, o, c, r, rtscts), lbl in zip(disp, labels):
        print(f"  {lbl:{w}} {str(r['rsf'] or '?'):>3} {rtscts:>8} "
              f"{r['data_air']:>7} {r['ack_air']:>6} {tot:>7}")
    print(f"  {'-' * (w + 35)}")
    print(f"  {('attributed (%d msgs)' % len(rows)):{w}} {'':>3} {s_rc:>8} {s_d:>7} "
          f"{s_a:>6} {s_t:>7}")
    print(f"  {'mean / msg':{w}} {'':>3} {s_rc // n:>8} {s_d // n:>7} "
          f"{s_a // n:>6} {s_t // n:>7}")
    print(f"\n  split of the ATTRIBUTED part: RTS+CTS {100 * s_rc // max(s_t, 1)}%  "
          f"DATA {100 * s_d // max(s_t, 1)}%  ACK {100 * s_a // max(s_t, 1)}%")
    print(f"  reconciliation: attributed {s_t} + unattributed {air['unattributed_ms']} "
          f"= {air['phy_total_ms']} = TOTAL ✓ (asserted in analyse_airtime)")


def _haversine_km(lat1, lon1, lat2, lon2):
    """Great-circle distance in km."""
    r = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = math.radians(lat2 - lat1), math.radians(lon2 - lon1)
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(h))


def analyse_tail(events_path, slot_to_id):
    """Per-message routing-stress metrics for the airtime tail. Keyed by
    (origin, dst, ctr) — the same key the breakdown's per-pair table uses (ctr
    is per-(origin,dst) in this firmware). Counts: rts_tx + rts_retry at origin
    + intermediates, data_tx + distinct data-TX nodes (= chain length),
    tx_blind_alt + path_cascade (copy creation), ack_tx, delivered.
    Use with render_tail to read against geographic distance."""
    msgs = {}

    def m(o, dst, c):
        k = (o, dst, c)
        if k not in msgs:
            msgs[k] = {
                "rts_tx": 0, "rts_retry": 0,
                "data_tx": 0, "data_tx_nodes": set(),
                "ack_tx": 0,
                "blind_alt": 0, "path_cascade": 0,
                "delivered": False,
                "first_t": None, "last_t": None,
            }
        return msgs[k]

    for t_ms, fid, et, d in walk_events(events_path, slot_to_id):
        o, c, dst = d.get("origin"), d.get("ctr"), d.get("dst")
        if o is None or c is None or dst is None:
            continue
        r = m(o, dst, c)
        if r["first_t"] is None:
            r["first_t"] = t_ms
        r["last_t"] = t_ms
        if et == "rts_tx":
            r["rts_tx"] += 1
        elif et == "rts_retry":
            r["rts_retry"] += 1
        elif et == "data_tx":
            r["data_tx"] += 1
            r["data_tx_nodes"].add(fid)
        elif et == "ack_tx":
            r["ack_tx"] += 1
        elif et == "delivered":
            r["delivered"] = True
        elif et == "tx_blind_alt":
            r["blind_alt"] += 1
        elif et == "path_cascade":
            r["path_cascade"] += 1
    return msgs


def render_tail(events_path, slot_to_id, id_to_name, id_to_ll, top=10,
                link_km=2.0, name_to_id=None):
    """Airtime-tail profile: top N messages by total airtime, with hop counts
    + retry/copy stress + geographic distance. The `min_h` column is the
    minimum plausible hop count (= ceil(km / link_km), where link_km is the
    expected single-hop range; default 2 km is a reasonable urban SF8 figure).
    Compare `chain` (distinct DATA forwarders, = actual hop count) to `min_h`
    to see if a route is geographically inflated. `data_tx` ≥ `chain` if any
    forwarder retransmitted; `retr` = rts_retry events anywhere along the
    chain (origin retries + intermediate retries). `D` = delivered flag."""
    def nm(nid):
        n = id_to_name.get(nid)
        return f"{n}({nid})" if n is not None else str(nid)

    # Reuse analyse_airtime for airtime totals (its key is (origin, ctr) — but
    # (origin, dst, ctr) uniquely refines it since ctr is per-(origin,dst)).
    air_msgs = analyse_airtime(events_path, slot_to_id, name_to_id)[0]
    tail_msgs = analyse_tail(events_path, slot_to_id)

    # Cross-reference: for each (o, c) in air_msgs, the dst is in air_msgs[k]["dst"]
    rows = []
    for (o, c), ar in air_msgs.items():
        dst = ar["dst"]
        if dst is None:
            continue
        total_air = ar["rts_air"] + ar["cts_air"] + ar["data_air"] + ar["ack_air"]
        tail = tail_msgs.get((o, dst, c), {})
        rows.append((total_air, o, dst, c, ar, tail))
    rows.sort(reverse=True)

    print("=== Airtime-tail profile (top {} by airtime) ===".format(top))
    print("min_h = ceil(km / {:.1f}); chain = distinct DATA forwarders "
          "(= actual hops); retr = rts_retry events; blind/casc = copy-creating "
          "switches; D = delivered.\n".format(link_km))
    labels = [f"{nm(o)} -> {nm(dst)} ({c})" for _, o, dst, c, _, _ in rows[:top]]
    w = max([len(x) for x in labels] + [len("origin -> dst (ctr)")])
    hdr = (f"  {'origin -> dst (ctr)':{w}} {'km':>5} {'min_h':>5} {'chain':>5} "
           f"{'data_tx':>7} {'retr':>4} {'blind':>5} {'casc':>4} "
           f"{'air_ms':>7} {'D':>2}")
    print(hdr)
    for (total_air, o, dst, c, _, tail), lbl in zip(rows[:top], labels):
        if not tail:
            continue
        ll_o = id_to_ll.get(o)
        ll_d = id_to_ll.get(dst)
        km = (_haversine_km(*ll_o, *ll_d) if ll_o and ll_d else 0.0)
        min_h = max(1, math.ceil(km / link_km))
        chain = len(tail["data_tx_nodes"])
        d_flag = "Y" if tail["delivered"] else "n"
        print(f"  {lbl:{w}} {km:>5.1f} {min_h:>5d} {chain:>5d} "
              f"{tail['data_tx']:>7d} {tail['rts_retry']:>4d} "
              f"{tail['blind_alt']:>5d} {tail['path_cascade']:>4d} "
              f"{total_air:>7d} {d_flag:>2}")

    # Summary stats over the top-N: airtime share + retry tax.
    top_rows = [(tot, o, dst, c, tail) for tot, o, dst, c, _, tail in rows[:top]
                if tail]
    tail_air = sum(t for t, *_ in top_rows)
    tail_retr = sum(r[4]["rts_retry"] for r in top_rows)
    tail_chain = sum(len(r[4]["data_tx_nodes"]) for r in top_rows)
    total_air = sum(r[0] for r in rows)
    total_retr = sum(t.get("rts_retry", 0) for t in tail_msgs.values())
    print()
    # ⚠ [[B162b]]: this denominator is the ATTRIBUTED airtime of the rows this view built, NOT the
    # scenario's total airtime — the total is `--airtime`'s `phy_total_ms`, which also carries the
    # unattributed bucket. Labelled rather than renamed: this is a per-message tail profile and a
    # share-of-attributed is the right question for it; calling it "total" was the misleading part.
    print(f"  top-{top} share of ATTRIBUTED airtime: {100 * tail_air / max(total_air, 1):.1f}%  "
          f"({tail_air} / {total_air} ms)   ⓘ see `--airtime` for the PHY-direct TOTAL")
    print(f"  top-{top} retries / total retries: {tail_retr} / {total_retr}  "
          f"(mean {tail_retr / max(len(top_rows), 1):.1f}/msg)")
    if tail_chain:
        # Tail airtime per hop is the simplest "is each hop expensive?" probe.
        print(f"  top-{top} airtime per chain-hop: {tail_air // tail_chain} ms  "
              f"(nominal single-hop RTS+CTS+DATA+ACK ≈ 500–700 ms at SF8)")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("config")
    p.add_argument("events", nargs="?",
                   help="events.ndjson path (default: "
                        "/tmp/<stem>_analyze.ndjson, the analyze.py "
                        "convention)")
    p.add_argument("--run", action="store_true",
                   help="run lus on the config before analysing")
    p.add_argument("--lus", default=None,
                   help="lus binary path (default: auto-detect the lora-sim build)")
    p.add_argument("--json", action="store_true",
                   help="emit JSON instead of the table")
    p.add_argument("--detail", action="store_true",
                   help="include per-message timeline (text) or "
                        "per-message event list (json)")
    p.add_argument("--pair", default=None,
                   help="filter to pairs, e.g. 'heidi:carol,dave:peter'")
    p.add_argument("--all", action="store_true",
                   help="include pairs not present in scenario commands")
    p.add_argument("--mode", choices=("dm", "channel", "all"), default="all",
                   help="which view to emit (default: all)")
    p.add_argument("--failures", action="store_true",
                   help="DM mode: print failure breakdown by routing-layer "
                        "mechanism, incl. cross-layer second-leg sub-classes "
                        "(no-route-to-target / first-hop-stalled / lost-downstream "
                        "/ resolve-bound) + a HOME/VISIT target-location tally")
    p.add_argument("--post", default=None,
                   help="filter channel posts: payload substring "
                        "(case-insensitive), e.g. 'news-3'")
    p.add_argument("--trace", default=None, metavar="SUBSTR",
                   help="follow message(s) end-to-end: dump every event whose "
                        "payload contains SUBSTR (case-insensitive), plus the "
                        "gateway-chain events for the destination hash(es) they "
                        "resolve to. Shows where a cross-layer message dies "
                        "(transit / handoff / no_binding / H-query). "
                        "e.g. --trace xl-w015-e020")
    p.add_argument("--airtime", action="store_true",
                   help="airtime (ms). The TOTAL is summed DIRECTLY over every "
                        "chargeable PHY tx frame at its OWN length/sf/bw/cr and "
                        "cross-checked against the frame's own airtime_ms; the "
                        "per-message split (RTS+CTS / DATA / ACK) is a SECONDARY "
                        "attribution view, and whatever it cannot attribute lands "
                        "in an explicit unattributed bucket that is still IN the "
                        "total ([[B162b]]). Add --json for the machine-readable "
                        "form. Re-analyses existing ndjson, no re-sim.")
    p.add_argument("--copies", action="store_true",
                   help="count copy-creating switches: a forward that abandoned "
                        "a next-hop which already decoded the frame (data_rx) and "
                        "switched to a different node -> a 2nd live copy. Reports "
                        "total + breakdown by trigger + per-message fan-out. "
                        "Re-analyses existing ndjson (no re-sim).")
    p.add_argument("--tail", nargs="?", const=10, default=None, type=int,
                   metavar="N",
                   help="profile the airtime tail: top-N messages by airtime "
                        "with hop count (= distinct DATA forwarders), retries, "
                        "copy-creating switches, and geographic distance "
                        "(km src->dst, min_h at --tail-link-km). Tells you "
                        "whether airtime is dominated by inflated routes "
                        "(chain >> min_h) or by retry tax (retr/chain high). "
                        "Default N=10.")
    p.add_argument("--tail-link-km", type=float, default=2.0,
                   help="assumed single-hop range (km) for --tail's min_h "
                        "estimate (default 2.0 km, urban SF8 ballpark).")
    args = p.parse_args()

    if args.events is None:
        stem = os.path.splitext(os.path.basename(args.config))[0]
        args.events = f"/tmp/{stem}_analyze.ndjson"
    if args.run:
        maybe_run(args.config, args.events, resolve_lus(args.lus))
    if not os.path.exists(args.events):
        sys.exit(f"events file does not exist: {args.events}\n"
                 f"  (pass --run to generate it, or provide an "
                 f"explicit EVENTS path)")

    # ★★★ [[B162d]] THE SINGLE CROSSING POINT — MUST STAY AHEAD OF THE MODE DISPATCH BELOW, so that
    # no `return` in any mode can precede it. See `ndjson_refusal_crossing_point()` for why a
    # per-mode copy of this notice was REJECTED, and `test_10` for the AST proof that nothing has
    # slipped in front of it. ⓘ Placed before `load_config` so the banner is the FIRST thing on
    # stdout (load_config's own diagnostics go to stderr — see line ~370).
    ndjson_refusal_crossing_point(args)

    (cfg, id_to_name, name_to_id, slot_to_id, hash_layer_to_name, id_to_layer,
     slots) = load_config(args.config)

    if args.trace:
        render_trace(args.events, args.trace, slot_to_id, id_to_name)
        return

    if args.copies:
        render_copies(args.events, slot_to_id, id_to_name)
        return

    if args.airtime:
        render_airtime(args.events, slot_to_id, id_to_name, name_to_id=name_to_id,
                       as_json=args.json)
        return

    if args.tail is not None:
        id_to_ll = {n["node_id"]: (n["lat"], n["lon"])
                    for n in cfg["nodes"]
                    if n.get("lat") is not None and n.get("lon") is not None}
        render_tail(args.events, slot_to_id, id_to_name, id_to_ll,
                    top=args.tail, link_km=args.tail_link_km, name_to_id=name_to_id)
        return

    (msgs, arrival_by_payload, drops, gw_giveup, gw_layers, second_leg, xl_stats,
     wire_to_config, alias_stats, hosted_by, logical, wire_to_slot, deleg) = analyse(
        args.events, slot_to_id, hash_layer_to_name, id_to_layer, slots)
    gw_home, gw_visit = gateway_layers(cfg)

    # Post-pass: resolve cross-layer target_id + arrival_at_target_ms.
    # Done here (not in analyse) because we need name_to_id which the
    # caller already has.
    for r in msgs.values():
        if not r.get("via_gateway"):
            continue
        t_layer = r.get("target_layer_id")
        t_hash = r.get("dst_key_hash32")
        if t_layer is None or t_hash is None:
            continue
        t_name = hash_layer_to_name.get((t_layer, t_hash))
        if t_name is None:
            continue
        t_id = name_to_id.get(t_name)
        if t_id is None:
            continue
        r["target_id"] = t_id
        if r["payload"] is not None:
            r["arrival_at_target_ms"] = arrival_by_payload.get((t_id, r["payload"]))
        # Second-leg sub-classification for failures in the "reached gateway,
        # lost after forward" bucket (envelope decoded at the gw, target never
        # got it, and the gateway didn't formally give up resolving).
        if (r["arrived_ms"] is not None and not _arrived(r)
                and (r["origin"], r.get("dst_key_hash32")) not in gw_giveup):
            r["second_leg"], r["second_leg_loc"] = classify_second_leg(
                r, second_leg, gw_home, gw_visit, id_to_layer)

    # Resolve dropped cross-layer sends (no gateway route) to (origin, target)
    # pairs so they count toward the honest cross-layer denominator.
    # ★ [[B182]]: keyed on SLOTS, like every other pair key. Both ends are resolved through `Slots`,
    #   which REFUSES an ambiguous id rather than picking (`logical_refused_unresolved_dst` counts it).
    no_gw_by_pair = defaultdict(int)
    for dp in drops:
        t_slot = slots.of_hash_in_layer(dp["dst_key_hash32"], dp["target_layer_id"]) \
            if dp["dst_key_hash32"] is not None else None
        o_slot = slots.of_id(dp["origin"])
        if t_slot is not None and o_slot is not None:
            no_gw_by_pair[(o_slot, t_slot)] += 1
        else:
            logical["refused_unresolved_dst"] += 1

    # Pair filter: explicit --pair wins; else configured commands;
    # else (with --all) no filter at all.
    explicit = parse_pair_filter(args.pair, slots)
    # `intended` is only meaningful for the default (configured-commands) view:
    # an explicit --pair is the user narrowing the question, and --all removes the
    # filter entirely, so in neither case is a "missing intended send" well-defined.
    intended = None
    unparsed_sends = Counter()
    if explicit is not None:
        pair_filter = explicit
    elif args.all:
        pair_filter = None
    else:
        pair_filter, intended, unparsed_sends = configured_pairs(cfg, slots,
                                                                 hash_layer_to_name, wire_to_slot)

    # ★★★★ [[B182]] 2026-08-12 — THE [[B162]] WIRE->CONFIG CANONICALISATION OF THE PAIR KEYS IS GONE
    # FROM HERE, AND IT HAD TO GO: it translated `(origin, dst)` through the learned runtime alias to
    # get the configured intents and the records into ONE id space. That is exactly the map that cannot
    # express a hosted mobile — the wire id is a real static's config id, so the alias pass leaves it
    # untranslated (correctly), and the two halves never met. ★ Both halves are now in SLOT space from
    # the start: `configured_pairs()` keys intents on slots, `assign_logical_pairs()` keys records on
    # slots, and no translation step exists to drift. ⓘ `wire_to_config` is STILL BUILT and still used —
    # by `same_node()`, for wire-level arrival/ack/giveup correlation, which is its sound job.
    assign_logical_pairs(msgs, slots, hosted_by, intended, logical, deleg)

    rows = summarise(msgs, pair_filter, slots, no_gw_by_pair, intended)

    channel_rows = None
    posts_meta = None
    if args.mode in ("channel", "all"):
        posts_meta = configured_channel_posts(cfg, name_to_id)
        analyse_channel(args.events, slot_to_id, posts_meta, name_to_id)
        # Apply --post filter to BOTH the table and the detail view
        # so they stay consistent (mirrors --pair in DM mode).
        rows_for_summary = posts_meta
        if args.post:
            pat = args.post.lower()
            rows_for_summary = [
                p for p in posts_meta
                if pat in (p.get("payload") or "").lower()
            ]
        channel_rows = summarise_channel(rows_for_summary, cfg, id_to_name)

    if args.json:
        if args.mode == "channel":
            payload = {"channels": channel_rows or []}
        elif args.mode == "dm":
            render_json(rows, msgs, pair_filter, id_to_name, args.detail,
                        xl_stats, args.events, unparsed_sends, alias_stats, logical, slots)
            return
        else:
            # Inline-render the DM JSON view into a dict so we can pair it
            # with channels under one top-level structure.
            from io import StringIO
            buf = StringIO()
            old_stdout = sys.stdout
            sys.stdout = buf
            try:
                render_json(rows, msgs, pair_filter, id_to_name, args.detail,
                            xl_stats, args.events, unparsed_sends, alias_stats, logical, slots)
            finally:
                sys.stdout = old_stdout
            dm_payload = json.loads(buf.getvalue())
            payload = {**dm_payload, "channels": channel_rows or []}
        emit_json(payload, args.events)
        return

    # ⓘ [[B162d]]: the refusal banner USED TO BE PRINTED HERE, and that was the defect — this line is
    # reached only by `--mode dm|channel|all` in text form, so `--airtime`/`--trace`/`--copies`/
    # `--tail` had all returned above without it, and the `--mode channel --json` payload built just
    # above carried no refusal key either. It now lives at the single crossing point in `main()`,
    # ahead of the whole dispatch. ⛔ Do not re-add a copy here.

    if args.mode in ("dm", "all"):
        print("=== DM ===")
        render_table(rows)
        # ★ [[B162]] FAIL LOUD: a configured send this grammar could not resolve is missing from the
        # denominator. It used to vanish silently and made 14 of 36 scenarios report 0 deliveries.
        if unparsed_sends:
            print()
            print(f"!! {sum(unparsed_sends.values())} CONFIGURED SEND(S) COULD NOT BE RESOLVED — the "
                  f"delivery figure above is INCOMPLETE for this scenario:")
            for k, v in sorted(unparsed_sends.items(), key=lambda kv: -kv[1]):
                print(f"     {k}: {v}")
        # ★★ [[B162b]] FAIL LOUD ON THE OTHER HALF: a REFUSED runtime-id alias means one wire id was
        # worn by several nodes, so `analyse()` correctly declined to translate it — and every record
        # that needed that translation went unmatched. ⛔ `unresolved_configured_sends` stays 0 in that
        # case (the send text parsed perfectly), so this counter is the ONLY warning that the
        # authority may be short. It was computed and thrown away until now.
        if alias_stats and alias_stats.get("refused_conflicts"):
            print()
            print(f"!! {alias_stats['refused_conflicts']} RUNTIME-ID ALIAS CONFLICT(S) REFUSED — a "
                  f"wire id was worn by more than one node, so it was left untranslated. Deliveries "
                  f"that needed it CANNOT be matched and the figure above may be SHORT. "
                  f"({alias_stats.get('aliases', 0)} alias(es) accepted.) "
                  f"⛔ Not a rounding detail — see [[B162]].")
        # ★★★★ [[B182]] item 7 — THE LOGICAL-IDENTITY LEDGER, PRINTED HERE AND NOT ONLY IN `--json`.
        # ⛔ The whole reason this block exists as unconditional output is the `alias_stats` lesson: a
        # counter that lives only in a JSON key nobody opens is not a safeguard. The BASIS line is a
        # POSITIVE control — if the two-layer machinery breaks, `from delivery` drops to 0 while the
        # refusals stay 0, and a clean-looking total is exactly what a reader must not trust.
        lt = logical_total_keys(logical)
        print()
        print(f"logical identity ([[B182]]): pair basis — {lt['logical_dst_from_delivery']} from an "
              f"OBSERVED delivery ({lt['logical_lastmile_correlated']} of them recovered through the "
              f"hosted-mobile last mile), {lt['logical_dst_from_wire']} from the wire dst.")
        if lt["logical_deleg_reattributed_samelayer"] or lt["logical_deleg_reattributed_xl"] \
                or lt["logical_deleg_wrappers_excluded"]:
            print(f"   delegated originations re-attributed from the HOME to the sending mobile "
                  f"([[B185]]): {lt['logical_deleg_reattributed_samelayer']} same-layer (via "
                  f"deleg_ack_put) + {lt['logical_deleg_reattributed_xl']} cross-layer (via "
                  f"mobile_delegate_xl); {lt['logical_deleg_wrappers_excluded']} mobile->home wrapper "
                  f"leg(s) excluded as transport.")
        if lt["logical_wire_slot_conflicts"]:
            print(f"   ⚠ {lt['logical_wire_slot_conflicts']} WIRE-ID/SLOT CONFLICT(S): a runtime wire id was "
                  f"worn by more than one scenario slot, so it cannot resolve a numeric destination token. "
                  f"ⓘ A DIAGNOSTIC, not a refusal — a command actually affected is counted in "
                  f"`unresolved_configured_sends` above.")
        refused = {k: lt[k] for k in LOGICAL_REFUSAL_KEYS if lt[k]}
        if refused:
            print("!! LOGICAL CORRELATIONS REFUSED — these messages are ABSENT from the pair table "
                  "above (their configured sends are still counted, via UNSENT):")
            for k, v in sorted(refused.items(), key=lambda kv: -kv[1]):
                print(f"     {k}: {v}")
            print("   ⛔ Refused, not guessed: attributing one of these to the wrong endpoint is the "
                  "defect [[B182]] records, in a new shape.")
        else:
            print("   0 logical correlations refused (every counter zero).")
        # §xl: the authoritative cross-layer delivery metric (pair-grouping-independent — the per-pair table
        # above drops un-arrived cross-layer rows whose target can't be resolved without the seal). Reconstructed
        # from tx_enqueue_xl + the (origin,ctr)-matched `delivered`.
        if xl_stats["sent"]:
            s = xl_stats
            pct = f"{100*s['arrived']/s['sent']:.1f}%" if s["sent"] else "-"
            print(f"cross-layer DMs: {s['arrived']}/{s['sent']} delivered = {pct}"
                  f"   ({s['enqueued']} enqueued, {s['no_gateway']} dropped no-gateway)")
        if args.failures:
            print()
            print("=== Cross-layer stage funnel ===")
            render_xl_funnel(msgs, no_gw_by_pair, gw_giveup, second_leg,
                             pair_filter, id_to_name, gw_layers, id_to_layer)
            print()
            print("=== DM failures (by mechanism) ===")
            render_dm_failures(msgs, no_gw_by_pair, gw_giveup,
                               pair_filter, id_to_name, gw_layers, id_to_layer,
                               {r["pair_key"]: r["unsent"]
                                for r in rows if r.get("unsent")})
        if args.detail:
            print()
            render_detail_text(msgs, pair_filter, id_to_name)
    if args.mode in ("channel", "all"):
        if args.mode == "all":
            print()
        print("=== Channels ===")
        render_channel_table(channel_rows)
        if args.detail:
            print()
            render_channel_detail(posts_meta, id_to_name, args.post)


if __name__ == "__main__":
    main()
