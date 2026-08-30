#!/usr/bin/env python3
# MeshRoute — compare_corpus_semantics.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""ORDERED EVENT-BY-EVENT semantic comparison of two corpus runs across a DATA-type renumbering.

WHY THIS EXISTS. The §CUSTODY-A namespace transition moves the TYPE byte of nearly every typed DATA frame, so
almost every scenario's md5 changes and the byte-identity tripwire says nothing at all. The claim that must be
proved instead is narrower and stronger:

    HASHES MAY MOVE EVERYWHERE; SEMANTICS MAY MOVE NOWHERE.

⛔⛔ AND IT IS NOT A COUNT COMPARISON. Equal event counts, equal delivery totals and equal analyzer figures are
    all consistent with one event having turned into another — the exact failure a renumbering could cause. So
    the comparison walks the two streams IN ORDER, line by line, and requires every event's name, every field,
    every value and every timestamp to be IDENTICAL, with exactly ONE normalization:

      · a `tx` event's raw `hex`, at byte offsets whose BEFORE value is an old DataType and whose AFTER value is
        that type's new number — and every such offset is REPORTED and CLASSIFIED, never waved through; and
      · the one telemetry field that carries a DataType by value — `enclosed_type` **on the
        `mobile_delegate_xl` event and no other**; the same field name on any other event is REJECTED.

    ⓘ The normalization is a MEASUREMENT, not an assumption: this tool does not rewrite the before-stream and
      re-hash it. It enumerates each differing byte, checks it against the old->new map, and prints the offset
      histogram, so "only the TYPE byte moved" is something you can read off the output rather than trust.

⛔⛔ THE MAP IS **NOT** APPLIED POSITION-BLIND, AND THE EARLIER WORDING HERE SAID IT WAS. Corrected 2026-08-29
  (QG round 3): this paragraph used to argue that a value-matched old->new mapping was "safe to apply
  position-blind", which is exactly the reasoning that produced this tool's first masking hole. Every
  normalization is now ANCHORED — a byte to a COMPUTED offset, a telemetry value to the EMITTING EVENT — and a
  valid-looking mapping anywhere else is a failure.

⚠ WHAT IS ACTUALLY TRUE, and it is a claim about SCOPE, not about position-blindness: the DATA AAD is 4 bytes of
  `dst_key_hash32` only (`node_hashlocate.cpp:821`) and the nonce derives from dst_hash/origin/ctr/seed — none of
  them the TYPE byte — so a sealed frame's ciphertext and tag are unaffected by the renumbering; and the TYPE
  byte's width is 1 in both namespaces, so no length, offset or airtime moves. ⇒ the only bytes that CAN
  legitimately differ are TYPE bytes. Which of them are ACCEPTED is decided by the derivation below, never by
  the value.

USAGE
    python3 tools/compare_corpus_semantics.py BEFORE_DIR AFTER_DIR
    python3 tools/compare_corpus_semantics.py BEFORE_DIR AFTER_DIR --selftest   # prove it can FAIL

⛔ §18.0.3 / the brief's requirement: a green comparison is evidence only if the instrument can go red. The
   `--selftest` runs SIX controls — FIVE negative and ONE positive. The negatives mutate the AFTER stream in five
   non-TYPE ways and require each to be REJECTED: a field value · an event name · an event's order · a dropped
   event · and the two camouflage attacks that caught this tool's own earlier versions — an ARBITRARY PAYLOAD
   BYTE remapped by a VALID old->new mapping (the wire plane), and an `enclosed_type` field carrying a VALID
   mapping on the WRONG EVENT (the telemetry plane). The positive requires the GENUINE
   `mobile_delegate_xl.enclosed_type` to still be ACCEPTED — a scoping fix that rejected the real carrier would
   make the PASS mean something else. A comparator that cannot fail is a formatter; one that can be defeated by
   the substitution it audits is worse than none; and one that fails on the thing it exists to normalize is not
   a gate either.
"""

import argparse
import json
import os
import sys
from collections import Counter

# The §CUSTODY-A old -> new landing (design §5.2). ⛔ Values only; the symbol names are for the report.
OLD_TO_NEW = {
    1: 0x88, 2: 0x89, 3: 0x80, 4: 0x8A, 5: 0x8B, 6: 0xA0, 7: 0xA1,
    8: 0x90, 9: 0x91, 10: 0x92, 11: 0x93, 12: 0x94, 13: 0x95,
    14: 0x02, 15: 0x01, 16: 0x96, 17: 0x03, 18: 0x04, 19: 0xA2,
}
NAME = {
    1: "H_ANSWER", 2: "AUTHORITATIVE_H_ANSWER", 3: "E2E_ACK", 4: "H_ANSWER_PUBKEY",
    5: "AUTH_H_ANSWER_PUBKEY", 6: "REMOTE_CMD", 7: "REMOTE_RESP", 8: "MOBILE_H_ANSWER",
    9: "MOBILE_BREADCRUMB", 10: "MOBILE_LAYER_QUERY", 11: "MOBILE_LAYER_ANSWER",
    12: "MOBILE_PUBKEY_PUSH", 13: "MOBILE_H_ANSWER_PUBKEY", 14: "MOBILE_SEND", 15: "INTRO",
    16: "MOBILE_KEY_FORWARD", 17: "SEALED_RELAY", 18: "CHANNEL_POST", 19: "TEAM_KEY_GRANT",
}
DATA_CMD_NIBBLE = 0x3          # meshroute_wire.h: Cmd::D
DATA_FLAG_APP = 0x80
TYPE_OFFSET = 8                # frame_codec.cpp pack_data: byte 8, iff APP


def packet_hash_hex(frame_bytes):
    """The simulator's own packet identity — FNV-1a/32 over the raw frame, 8 lower-case hex digits.
    Reproduced from `core/events/EventLog.cpp:102` (`packetHash`/`packetHashHex`). ⓘ It is a PURE FUNCTION OF
    THE BYTES, which is exactly why a moved TYPE byte moves it — and why re-deriving it here turns `pkt` from
    an unexplained difference into a CHECKED one."""
    h = 0x811C9DC5
    for c in frame_bytes:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return "%08x" % h


class Delta:
    """Every accepted normalization, recorded so the report can enumerate them."""

    def __init__(self):
        self.type_byte = Counter()       # (old, new) at the frame's own offset 8
        self.enclosed_byte = Counter()   # (offset, old, new) at a MOBILE_SEND wrapper's COMPUTED enclosed-type slot
        self.enclosed_field = Counter()  # (old, new) in mobile_delegate_xl.enclosed_type
        self.pkt_moves = 0               # `pkt` identities that followed a frame whose TYPE byte moved
        self.pkt_map = {}                # before_pkt -> after_pkt, LEARNED from tx events and their hex
        self.events_touched = 0


# ---- the DATA inner layout, computed the way `parse_unicast_inner` computes it -------------------------------
# ⛔⛔ THIS EXISTS BECAUSE THE FIRST VERSION OF THIS TOOL WAS A MASKING HOLE, and QG caught it rather than the
#     tool itself. That version accepted ANY byte after offset 8 whose before/after pair happened to be a valid
#     old->new DataType mapping, in ANY app-framed DATA frame. So a legitimate outer MOBILE_SEND renumber PLUS
#     an unrelated payload corruption 15 -> 1 at offset 10 compared CLEAN, filed under "enclosed type".
#     ⇒ a semantic gate defeatable by the very substitution it was built to audit.
#   THE FIX IS NOT A TIGHTER HEURISTIC, IT IS A DERIVATION: the enclosed-type slot is COMPUTED from the frame's
#   own flags and layout — the outer type must be MOBILE_SEND, the wrapper must actually carry an enclosed type,
#   and the byte must sit at exactly that computed offset. A byte anywhere else is a FAILURE, whatever its
#   value. The `an ARBITRARY payload byte remapped` control proves that clause bites.

DATA_FLAG_CROSS_LAYER = 0x40
DATA_FLAG_CRYPTED     = 0x20
DATA_FLAG_LOCATION    = 0x08
DATA_FLAG_SOURCE_HASH = 0x04
DATA_FLAG_DST_HASH    = 0x02
DATA_FLAG_MS_ENCLOSED_TYPE = 0x01     # aliases PRIORITY; the SAME-LAYER wrapper's "enclosed type present" mark
GW_ENV_MAX_HOPS = 8                   # protocol::gw_env_max_hops — only used to reject a malformed layer path

# The two spellings of MOBILE_SEND across the transition: the comparator must recognise the OUTER type on BOTH
# sides of the rename to know whether an inner byte is even eligible.
MOBILE_SEND_OLD, MOBILE_SEND_NEW = 14, 0x02


def unicast_body_offset(frame):
    """Absolute offset of the plaintext unicast BODY in `frame`, or None if it has none / is unparseable.

    Mirrors `parse_unicast_inner` (frame_codec.cpp:1004) term for term and in the same order:
        inner starts at DATA_HDR_LEN + 1 (APP)  ->  [dst_hash 4 iff DST_HASH]
                                                ->  [n_layers 1][cur 1][layer_ids n] iff CROSS_LAYER
                                                ->  (CRYPTED: the rest is ciphertext — NO plaintext body)
                                                ->  [origin 1][source_hash 4 iff SOURCE_HASH][loc 6 iff LOCATION]
                                                ->  body
    """
    if len(frame) < 9 or (frame[0] >> 4) != DATA_CMD_NIBBLE:
        return None
    flags = frame[1]
    if not (flags & DATA_FLAG_APP):
        return None                                   # untyped DM — no TYPE byte, no wrapper
    off = TYPE_OFFSET + 1                             # 9: past the 8-B header + the TYPE byte
    if flags & DATA_FLAG_DST_HASH:
        off += 4
    if flags & DATA_FLAG_CROSS_LAYER:
        if off + 2 > len(frame):
            return None
        n, cur = frame[off], frame[off + 1]
        if n == 0 or n > GW_ENV_MAX_HOPS or cur >= n:
            return None                               # the parser fails loud here; so does this
        off += 2 + n
    if flags & DATA_FLAG_CRYPTED:
        return None                                   # sealed: no plaintext enclosed-type byte can exist
    off += 1                                          # origin
    if flags & DATA_FLAG_SOURCE_HASH:
        off += 4
    if flags & DATA_FLAG_LOCATION:
        off += 6
    return off if off < len(frame) else None


def enclosed_type_offset(frame):
    """Absolute offset of a MOBILE_SEND wrapper's enclosed-type byte, or None if this frame carries none.

    The wrapper contract (frame_codec.h §S2; node_mac_rx.cpp:1581 / :1639):
      - XL wrapper (CROSS_LAYER)  — the 1-B enclosed-type prefix is ALWAYS present (keyed off has_cross_layer);
      - same-layer wrapper        — present IFF DATA_FLAG_MS_ENCLOSED_TYPE (the 0x01 alias) is set.
    ⛔ And ONLY when the OUTER type is MOBILE_SEND. Any other frame has no enclosed-type slot at all.
    """
    if len(frame) < 9 or (frame[0] >> 4) != DATA_CMD_NIBBLE:
        return None
    flags = frame[1]
    if not (flags & DATA_FLAG_APP):
        return None
    if frame[TYPE_OFFSET] not in (MOBILE_SEND_OLD, MOBILE_SEND_NEW):
        return None                                   # not a wrapper -> no enclosed type, whatever the bytes say
    if not (flags & (DATA_FLAG_CROSS_LAYER | DATA_FLAG_MS_ENCLOSED_TYPE)):
        return None
    return unicast_body_offset(frame)                 # the enclosed type is body[0]


def compare_hex(before_hex, after_hex, delta, where):
    """Return a list of failure strings. Records every accepted TYPE-byte move into `delta`.

    ⛔ EXACTLY TWO POSITIONS MAY MOVE, and both are DERIVED, never matched by value:
       - offset 8, the frame's own TYPE field, whenever the APP bit says one is present; and
       - the enclosed-type slot of a MOBILE_SEND wrapper, at the offset `enclosed_type_offset()` COMPUTES from
         that frame's flags and layout.
       A differing byte anywhere else FAILS, even when its before/after pair is a perfectly valid old->new
       DataType mapping. That last clause is the whole point — see the note at `unicast_body_offset`.
    """
    if len(before_hex) != len(after_hex):
        return ["%s: raw frame LENGTH changed (%d -> %d hex chars) — the TYPE byte is 1 B in both namespaces, "
                "so a length change is never a renumbering" % (where, len(before_hex), len(after_hex))]
    try:
        b = bytes.fromhex(before_hex)
        a = bytes.fromhex(after_hex)
    except ValueError:
        return ["%s: un-decodable hex" % where]
    diffs = [i for i in range(len(b)) if b[i] != a[i]]
    if not diffs:
        return []
    fails = []
    app = len(b) > 1 and (b[0] >> 4) == DATA_CMD_NIBBLE and bool(b[1] & DATA_FLAG_APP)
    # The permitted positions are computed from the BEFORE frame and cross-checked against the AFTER frame: the
    # two must agree, or a flag/layout byte moved — which a TYPE renumbering can never do.
    enc_b, enc_a = enclosed_type_offset(b), enclosed_type_offset(a)
    if enc_b != enc_a:
        return ["%s: the computed enclosed-type slot MOVED (%r -> %r) — flags or inner layout changed, which a "
                "TYPE renumbering can never do" % (where, enc_b, enc_a)]
    permitted = {}
    if app:
        permitted[TYPE_OFFSET] = "outer TYPE field"
    if enc_b is not None:
        permitted[enc_b] = "MOBILE_SEND wrapper enclosed-type slot"
    for i in diffs:
        old, new = b[i], a[i]
        slot = permitted.get(i)
        if slot is None:
            fails.append("%s: byte %d changed %#04x -> %#04x at a position that is NOT a TYPE field (permitted "
                         "here: %s) — a valid-looking DataType mapping in a payload byte is a CORRUPTION, not a "
                         "renumbering"
                         % (where, i, old, new,
                            ", ".join("%d=%s" % kv for kv in sorted(permitted.items())) or "none"))
            continue
        if OLD_TO_NEW.get(old) != new:
            fails.append("%s: byte %d (%s) changed %#04x -> %#04x, which is NOT an old->new DataType move" %
                         (where, i, slot, old, new))
            continue
        if i == TYPE_OFFSET:
            delta.type_byte[(old, new)] += 1
        else:
            delta.enclosed_byte[(i, old, new)] += 1
    if not fails:
        delta.events_touched += 1
    return fails


# The ONE telemetry event whose `enclosed_type` field carries a DataType by value.
# ⛔⛔ SCOPED TO THE EVENT, NOT THE FIELD NAME (QG round 3). The first version normalized ANY JSON field called
#     `enclosed_type` anywhere in any event, so `{"type":"unrelated_event","enclosed_type":15}` -> `…:1` compared
#     CLEAN. That is the SAME masking class as the byte-position hole one level up, in the telemetry plane
#     instead of the wire plane: a value-shaped rule with no anchor to what the value BELONGS to. The anchor is
#     the emitting event, and it is checked before the field is even looked at.
ENCLOSED_TYPE_EVENT = "mobile_delegate_xl"


def compare_event(nb, na, delta, where, emit_type=None):
    """Ordered, field-by-field. Everything must be identical except the normalized carriers.

    `emit_type` is the ENCLOSING script_emit's `emit_type`, threaded down so a nested field can be judged in the
    context of the event that emitted it rather than by its own name.
    """
    if type(nb) is not type(na):
        return ["%s: event JSON type changed" % where]
    if isinstance(nb, dict):
        if set(nb) != set(na):
            return ["%s: field SET changed: only-before=%s only-after=%s" %
                    (where, sorted(set(nb) - set(na)), sorted(set(na) - set(nb)))]
        fails = []
        for k in nb:
            if k == "hex" and isinstance(nb[k], str):
                fails += compare_hex(nb[k], na[k], delta, where + ".hex")
            elif k == "pkt" and nb[k] != na[k]:
                # ⓘ `pkt` is the simulator's FNV-1a identity OF THE RAW FRAME (EventLog.cpp:102), so it moves
                #   whenever a TYPE byte does — and it appears on rx/collision/drop events that carry no `hex`.
                #   It is accepted ONLY through the map LEARNED from the tx events of this same stream, where
                #   both sides' `pkt` were re-derived from their own bytes. ⇒ a `pkt` that changed without a
                #   corresponding frame change has nowhere to hide.
                if delta.pkt_map.get(nb[k]) == na[k]:
                    delta.pkt_moves += 1
                    delta.events_touched += 1
                else:
                    fails.append("%s.pkt: %r -> %r, which no transmitted frame's TYPE-byte move explains" %
                                 (where, nb[k], na[k]))
            elif k == "enclosed_type" and nb[k] != na[k]:
                # ⛔ THE EVENT IS THE ANCHOR. A field called `enclosed_type` on ANY other event is not a known
                #    DataType carrier, so a valid-looking old->new value there is an unexplained change, and it
                #    fails — see the note at ENCLOSED_TYPE_EVENT.
                if emit_type != ENCLOSED_TYPE_EVENT:
                    fails.append("%s.enclosed_type: %r -> %r on emit_type %r — only `%s` is a known DataType "
                                 "carrier, so a valid-looking mapping on any other event is an unexplained "
                                 "change, not a renumbering"
                                 % (where, nb[k], na[k], emit_type, ENCLOSED_TYPE_EVENT))
                elif OLD_TO_NEW.get(nb[k]) == na[k]:
                    delta.enclosed_field[(nb[k], na[k])] += 1
                    delta.events_touched += 1
                else:
                    fails.append("%s.enclosed_type: %r -> %r is not an old->new DataType move" %
                                 (where, nb[k], na[k]))
            else:
                fails += compare_event(nb[k], na[k], delta, where + "." + k, emit_type)
        return fails
    if isinstance(nb, list):
        if len(nb) != len(na):
            return ["%s: list length %d -> %d" % (where, len(nb), len(na))]
        fails = []
        for i, (x, y) in enumerate(zip(nb, na)):
            fails += compare_event(x, y, delta, "%s[%d]" % (where, i), emit_type)
        return fails
    if nb != na:
        return ["%s: %r -> %r" % (where, nb, na)]
    return []


def learn_pkt_map(before_lines, after_lines, delta, name):
    """PASS A — build before_pkt -> after_pkt from the tx events, and PROVE the identity is the frame's hash.

    ⛔ This is what keeps `pkt` from being a free pass. For every tx event, BOTH sides' `pkt` are re-derived
       from that side's own `hex`; a mismatch means `pkt` is not what this tool thinks it is and the whole
       normalization is refused. The map is then required to be a FUNCTION: one before-identity may never map
       to two different after-identities (that would be a frame that changed in two different ways).
    """
    fails = []
    for i, (lb, la) in enumerate(zip(before_lines, after_lines)):
        if lb == la:
            continue
        try:
            nb, na = json.loads(lb), json.loads(la)
        except json.JSONDecodeError:
            continue                                   # pass B reports it
        if not (isinstance(nb, dict) and isinstance(na, dict)):
            continue
        if "hex" not in nb or "pkt" not in nb or "hex" not in na or "pkt" not in na:
            continue
        try:
            hb, ha = bytes.fromhex(nb["hex"]), bytes.fromhex(na["hex"])
        except ValueError:
            continue
        if packet_hash_hex(hb) != nb["pkt"] or packet_hash_hex(ha) != na["pkt"]:
            fails.append("%s line %d: `pkt` is NOT the FNV-1a hash of the frame — the normalization's own "
                         "premise is false, so the comparison is refused" % (name, i + 1))
            continue
        prev = delta.pkt_map.get(nb["pkt"])
        if prev is not None and prev != na["pkt"]:
            fails.append("%s line %d: packet identity %s maps to BOTH %s and %s — one before-frame changed in "
                         "two different ways" % (name, i + 1, nb["pkt"], prev, na["pkt"]))
            continue
        delta.pkt_map[nb["pkt"]] = na["pkt"]
    return fails


def compare_stream(before_lines, after_lines, name):
    """Return (fails, delta). ORDERED: line i of before is compared to line i of after, always."""
    delta = Delta()
    fails = []
    if len(before_lines) != len(after_lines):
        fails.append("%s: EVENT COUNT changed %d -> %d" % (name, len(before_lines), len(after_lines)))
        return fails, delta
    fails += learn_pkt_map(before_lines, after_lines, delta, name)
    for i, (lb, la) in enumerate(zip(before_lines, after_lines)):
        if lb == la:
            continue                              # the overwhelming majority: byte-identical line
        try:
            nb, na = json.loads(lb), json.loads(la)
        except json.JSONDecodeError as e:
            fails.append("%s line %d: un-parseable JSON (%s)" % (name, i + 1, e))
            continue
        # The enclosing event's identity, taken from the BEFORE side. A CHANGED `emit_type` is caught as an
        # ordinary field difference by the walk itself, so this cannot be used to launder an event rename.
        ctx = nb.get("emit_type") if isinstance(nb, dict) else None
        fails += compare_event(nb, na, delta, "%s line %d" % (name, i + 1), ctx)
        if len(fails) > 40:
            fails.append("%s: … further differences suppressed" % name)
            break
    return fails, delta


def read(path):
    with open(path) as f:
        return f.read().splitlines()


def run(before_dir, after_dir, quiet=False):
    names = sorted(n[:-7] for n in os.listdir(before_dir) if n.endswith(".ndjson"))
    all_fails = []
    grand = Delta()
    rows = []
    for n in names:
        pb, pa = os.path.join(before_dir, n + ".ndjson"), os.path.join(after_dir, n + ".ndjson")
        if not os.path.exists(pa):
            all_fails.append("%s: missing in AFTER" % n)
            continue
        bl, al = read(pb), read(pa)
        fails, d = compare_stream(bl, al, n)
        all_fails += fails
        grand.type_byte.update(d.type_byte)
        grand.enclosed_byte.update(d.enclosed_byte)
        grand.enclosed_field.update(d.enclosed_field)
        grand.events_touched += d.events_touched
        grand.pkt_moves += d.pkt_moves
        rows.append((n, len(bl), d.events_touched,
                     sum(d.type_byte.values()), sum(d.enclosed_byte.values()),
                     sum(d.enclosed_field.values()), d.pkt_moves, "FAIL" if fails else "ok"))
    if not quiet:
        print("%-46s %8s %8s %8s %8s %7s %8s  %s" %
              ("scenario", "events", "touched", "typebyte", "enclbyte", "enclfld", "pktrefs", ""))
        for r in rows:
            print("%-46s %8d %8d %8d %8d %7d %8d  %s" % r)
        print()
        print("TYPE-byte moves at offset 8, by type:")
        for (o, nv), c in sorted(grand.type_byte.items()):
            print("   %-26s %3d -> %#04x   %6d frames" % (NAME.get(o, "?"), o, nv, c))
        if grand.enclosed_byte:
            print("ENCLOSED-type byte moves inside a frame body (offset, old -> new):")
            for (off, o, nv), c in sorted(grand.enclosed_byte.items()):
                print("   offset %3d   %-26s %3d -> %#04x   %6d frames" % (off, NAME.get(o, "?"), o, nv, c))
        if grand.enclosed_field:
            print("ENCLOSED-type TELEMETRY field moves (mobile_delegate_xl.enclosed_type):")
            for (o, nv), c in sorted(grand.enclosed_field.items()):
                print("   %-26s %3d -> %#04x   %6d events" % (NAME.get(o, "?"), o, nv, c))
        print()
    return all_fails, grand, rows


def selftest(before_dir, after_dir):
    """⛔ Six controls: FIVE independent non-TYPE changes that must be REJECTED, plus ONE positive proving the
    genuine `mobile_delegate_xl.enclosed_type` carrier is still ACCEPTED (a scope that rejects everything is not
    a scope). See the module docstring."""
    import tempfile
    import shutil
    base_fails, _, _ = run(before_dir, after_dir, quiet=True)
    if base_fails:
        print("SELFTEST ABORTED — the live comparison is not clean, so a negative control proves nothing:")
        for f in base_fails[:10]:
            print("   · " + f)
        return 1

    # pick a stream that actually MOVED, so the mutation is not hiding behind an untouched file
    victim = "s18_meshroute"
    lines = read(os.path.join(after_dir, victim + ".ndjson"))

    def mutate_field(ls):
        for i, ln in enumerate(ls):
            o = json.loads(ln)
            if o.get("type") == "tx" and "airtime_ms" in o:
                o["airtime_ms"] = o["airtime_ms"] + 1        # a NON-TYPE field, one event, one value
                ls[i] = json.dumps(o)
                return ls, "one non-TYPE field value altered (tx.airtime_ms +1)"
        raise SystemExit("selftest: no tx event with airtime_ms to mutate")

    def mutate_kind(ls):
        for i, ln in enumerate(ls):
            o = json.loads(ln)
            if o.get("type") == "script_emit":
                o["emit_type"] = o["emit_type"] + "_X"       # one EVENT NAME changed, counts unchanged
                ls[i] = json.dumps(o)
                return ls, "one event NAME changed (counts stay equal)"
        raise SystemExit("selftest: no script_emit to mutate")

    def mutate_order(ls):
        for i in range(len(ls) - 1):
            if ls[i] != ls[i + 1]:
                ls[i], ls[i + 1] = ls[i + 1], ls[i]          # ORDER only; the multiset is untouched
                return ls, "two adjacent events SWAPPED (same multiset, same counts)"
        raise SystemExit("selftest: nothing to swap")

    def mutate_drop(ls):
        del ls[len(ls) // 2]
        return ls, "one event DROPPED"

    def mutate_camouflage(ls):
        """★★★ THE CONTROL QG's REPRODUCTION DEMANDED, AND THE ONE THE FIRST INSTRUMENT FAILED.

        Corrupt an ARBITRARY PAYLOAD byte of a typed DATA frame using a VALID old->new DataType mapping — the
        camouflage attack. It is indistinguishable from a renumbering BY VALUE and distinguishable from one BY
        POSITION, which is exactly why the comparator must derive its permitted offsets instead of matching
        bytes. Before the fix this returned ZERO failures and was filed as an "enclosed type".
        """
        for i, ln in enumerate(ls):
            o = json.loads(ln)
            if o.get("type") != "tx" or "hex" not in o:
                continue
            fr = bytearray.fromhex(o["hex"])
            if len(fr) < 9 or (fr[0] >> 4) != DATA_CMD_NIBBLE or not (fr[1] & DATA_FLAG_APP):
                continue
            enc = enclosed_type_offset(fr)
            # any byte strictly after the TYPE field that is neither the TYPE field nor the enclosed-type slot
            for j in range(TYPE_OFFSET + 1, len(fr) - 4):
                if j == enc:
                    continue
                # rewrite it as though an old ordinal there had been renumbered
                old = (j * 7) % 19 + 1                      # a deterministic old ordinal, 1..19
                fr[j] = OLD_TO_NEW[old]
                o["hex"] = fr.hex()
                # keep `pkt` consistent with the corrupted bytes, so the ONLY thing left to notice is the
                # position of the changed byte — no free catch from the identity check.
                if "pkt" in o:
                    o["pkt"] = packet_hash_hex(bytes(fr))
                ls[i] = json.dumps(o)
                return ls, ("an ARBITRARY PAYLOAD byte remapped by a VALID old->new DataType mapping at "
                            "offset %d (the camouflage attack; `pkt` recomputed so position is the only tell)"
                            % j)
        raise SystemExit("selftest: no typed DATA frame with a spare payload byte to corrupt")

    ok = True
    # --- the four ordinary stream mutations + the two byte/field camouflage attacks ---
    for fn in (mutate_field, mutate_kind, mutate_order, mutate_drop, mutate_camouflage):
        tmp = tempfile.mkdtemp()
        try:
            for n in os.listdir(after_dir):
                shutil.copy(os.path.join(after_dir, n), os.path.join(tmp, n))
            mutated, label = fn(list(lines))
            with open(os.path.join(tmp, victim + ".ndjson"), "w") as f:
                f.write("\n".join(mutated) + "\n")
            fails, _, _ = run(before_dir, tmp, quiet=True)
            if fails:
                print("  RED   (control fired)  %s" % label)
                print("           first: %s" % fails[0][:150])
            else:
                print("  ⛔ GREEN (control DEAD) %s  <-- the comparator cannot see this" % label)
                ok = False
        finally:
            shutil.rmtree(tmp)
    # --- control 6: the FIELD-NAME camouflage, which needs BOTH sides patched to be a value change ---
    tmp_b, tmp_a = tempfile.mkdtemp(), tempfile.mkdtemp()
    try:
        for n in os.listdir(before_dir):
            shutil.copy(os.path.join(before_dir, n), os.path.join(tmp_b, n))
        for n in os.listdir(after_dir):
            shutil.copy(os.path.join(after_dir, n), os.path.join(tmp_a, n))
        bl, al = read(os.path.join(before_dir, victim + ".ndjson")), list(lines)
        idx = label = None
        for i, (lb, la) in enumerate(zip(bl, al)):
            ob, oa = json.loads(lb), json.loads(la)
            if ob.get("type") != "script_emit" or ob.get("emit_type") == ENCLOSED_TYPE_EVENT:
                continue
            if not isinstance(ob.get("data"), dict) or not isinstance(oa.get("data"), dict):
                continue
            ob["data"]["enclosed_type"] = 15                     # the pre-transition INTRO ordinal
            oa["data"]["enclosed_type"] = OLD_TO_NEW[15]         # ...and its VALID new value
            bl[i], al[i] = json.dumps(ob), json.dumps(oa)
            idx, label = i, ("an `enclosed_type` field carrying a VALID old->new mapping on emit_type %r — a "
                             "field name borrowing another event's exemption" % ob.get("emit_type"))
            break
        if idx is None:
            print("  ⛔ SELFTEST BROKEN — no non-`%s` script_emit to attach the field to" % ENCLOSED_TYPE_EVENT)
            ok = False
        else:
            with open(os.path.join(tmp_b, victim + ".ndjson"), "w") as f:
                f.write("\n".join(bl) + "\n")
            with open(os.path.join(tmp_a, victim + ".ndjson"), "w") as f:
                f.write("\n".join(al) + "\n")
            fails, _, _ = run(tmp_b, tmp_a, quiet=True)
            if fails:
                print("  RED   (control fired)  %s" % label)
                print("           first: %s" % fails[0][:150])
            else:
                print("  ⛔ GREEN (control DEAD) %s  <-- any event can borrow the exemption" % label)
                ok = False
    finally:
        shutil.rmtree(tmp_b)
        shutil.rmtree(tmp_a)

    # --- the POSITIVE half of the same scope: the REAL carrier must still be accepted ---
    _, grand, _ = run(before_dir, after_dir, quiet=True)
    real = sum(grand.enclosed_field.values())
    if real > 0:
        print("  GREEN (positive control) the REAL `%s.enclosed_type` is still ACCEPTED — %d value(s) "
              "normalized in the live comparison" % (ENCLOSED_TYPE_EVENT, real))
    else:
        print("  ⛔ RED (positive control BROKEN) the scoping rejects the genuine `%s.enclosed_type` carrier "
              "too — the PASS above would be for the wrong reason" % ENCLOSED_TYPE_EVENT)
        ok = False

    print()
    if ok:
        print("SELFTEST PASS — 6/6 controls (5 negative + 1 positive). The comparator is an instrument, not a "
              "formatter, and its scoping does not reject the carrier it exists to normalize.")
        return 0
    print("SELFTEST FAIL — at least one non-TYPE change passes unnoticed, or the genuine carrier is rejected.")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("before_dir")
    ap.add_argument("after_dir")
    ap.add_argument("--selftest", action="store_true",
                    help="six controls: five prove the comparison can FAIL for a non-TYPE change, one proves it "
                         "still ACCEPTS the genuine mobile_delegate_xl.enclosed_type carrier")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.before_dir, args.after_dir)

    fails, grand, rows = run(args.before_dir, args.after_dir)
    if fails:
        print("FAIL — %d SEMANTIC difference(s); the renumbering changed behaviour, not only bytes:" % len(fails))
        for f in fails[:40]:
            print("   · " + f)
        return 1
    moved = sum(1 for r in rows if r[2])
    print("PASS — %d scenario(s) compared event-by-event, in order. %d stream(s) carried typed DATA and "
          "differ ONLY in TYPE bytes; %d stream(s) are byte-identical." % (len(rows), moved, len(rows) - moved))
    print("       %d event(s) touched · %d frame TYPE byte(s) · %d enclosed-type byte(s) · %d telemetry "
          "field(s) · %d derived packet-identity reference(s)."
          % (grand.events_touched, sum(grand.type_byte.values()),
             sum(grand.enclosed_byte.values()), sum(grand.enclosed_field.values()), grand.pkt_moves))
    return 0


if __name__ == "__main__":
    sys.exit(main())
