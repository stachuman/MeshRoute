#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§B278 S0 — OFFLINE correlation-ring occupancy + custody-report-age census over a VALIDATED corpus snapshot.

WHAT THIS IS
    B278 §10.2 obliges S0 to measure, before any capacity or bound is accepted:
      · the maximum live correlation rows in every mobile-bearing corpus stream, at the ruled per-obligation
        expiry bounds (300 s ACK / 750 s custody);
      · how many corpus custody reports arrive more than 300 s after their outward origination.
    This file answers both from the canonical NDJSON the corpus runner already validated. ⛔ IT ADDS NO
    SIMULATOR BEHAVIOUR AND RUNS NO SCENARIO — it reads `<corpus>/streams/*.ndjson` produced and promoted by
    `tools/run_corpus.py`, whose manifest is re-validated here before a single event is parsed.

★★★ WHAT THE TELEMETRY COULD NOT SAY UNDER §B278 S0/S1a, AND WHAT §B278 S1b's F6 SURFACE CLOSED. Verified
    against `lib/core/node_hashlocate.cpp` / `node_mac_rx.cpp` (V1, not against comments):
      · ⛔ WAS: `deleg_ack_release()` emitted NOTHING, so a reservation released on admission failure, a
        spoofed source, a channel-post delegation, a bad XL path, a no-route XL delegation or a parked-send
        age-out was INVISIBLE. ★ NOW: `deleg_ack_released{mobile_hash, ctr_m, target, target_kind, layer,
        cause}` is emitted inside the helper, immediately before the matching RESERVED row is cleared, and its
        `cause` names WHICH of the seven release reasons applied. A no-match release still emits nothing —
        correctly, because nothing was released.
      · ⛔ WAS: expiry pruning emitted NOTHING; it was COMPUTABLE from the TTL but never observed. ★ NOW:
        `deleg_ack_expired{mobile_hash, ctr_m, ctr_h, target, layer, custody_state}` reports the row's EXACT
        pre-clear values from the one prune authority.
      · an EXACT-RETRY reservation refresh is STILL silent: `deleg_ack_reserve` updates `e.ts_ms` and returns
        without emitting. ⚠ But it is no longer an unbounded uncertainty: the row's END is now observed
        (release, expiry or ACK translation), so only the interior of a refreshed row's life is unrecorded.
      · `deleg_ack_translate()` emits nothing ITSELF; `mobile_reverse_ack` is emitted INSIDE its success
        branch, so a successful translate is observable one-for-one. ★ AND IT NOW CARRIES THE ROW'S OWN
        IDENTITY — `mobile_hash` and `ctr_h` — so the bind is EXACT and no longer needs the registration
        authority (see below).
      · an exact ACTIVE refresh in `deleg_ack_put` DOES emit `deleg_ack_put` again (same ctr_h/peer/layer), so
        that one refresh is visible; the RESERVED refresh is not.

    ⇒ THE OUTPUT STILL REPORTS A LABELLED PAIR OF BOUNDS, but they now COINCIDE for every row whose end was
      observed. `unclosed_rows` counts the rows still live at the end of a stream — the only remaining gap,
      and it is a property of where the scenario stops, not of the telemetry.

★★ THE REGISTRATION AUTHORITY IS RETIRED AS THE ACK BINDER, AND THAT IS AN UPGRADE, NOT A LOSS. Under S0/S1a
   `mobile_reverse_ack` carried only `{local, ctr}`, so turning the MOBILE-LOCAL id into a stable hash needed
   `mobile_registered` / `mobile_reg_expired` replayed per node — and a local id with none or several live
   keys made the release UNBINDABLE. §B278 S1b's F6 appends `mobile_hash` and `ctr_h` to the event itself, so
   the bind is now the row's own `{mobile_hash, ctr_m, ctr_h}`.
   ⛔ THE NEGATIVE COVERAGE DID NOT GO WITH IT — it MOVED, and the selftest names where: the old `O3s`
   (no registration) and `O3a` (two live keys on one local id) are replaced by `O3s` (an ACK naming a row this
   node never activated) and `O3a` (an ACK whose identity matches more than one live row). Both still REFUSE
   to bind, both still leave the row unfreed, and neither may be resolved by counter alone.
   ⛔ AND THE ANALYZER NOW FAILS LOUD ON THE FIELDS THEMSELVES: a `mobile_reverse_ack` missing `mobile_hash`
   or `ctr_h`, or carrying a non-integer one, REFUSES the whole run rather than silently reverting to a
   weaker binder. A missing field is not a zero.

★★★ §B278 S3 (2026-09-02) — THE FORWARD LEDGER, AND A CORRECTION IN PLACE. ⛔ THIS FILE PREVIOUSLY MODELLED
    `custody_state = 3 (forwarded)` AS AN END-STATE NOTHING COULD PRODUCE, because through S1b/S2 nothing
    wrote it. **That statement is now FALSE** and is kept visible rather than deleted: S3's
    `deleg_custody_mark_forwarded` (node_hashlocate.cpp) writes it, and only it, for a row whose translated
    report was `queued` or `parked`.
    ⇒ the census now CONSUMES `deleg_custody_forwarded{mobile_hash, dst, ctr_h, ctr_m, type}` and binds it by
      the row's COMPLETE identity — `{mobile_hash, ctr_h, ctr_m}` plus the return peer the event calls `dst` —
      to EXACTLY ONE live ACTIVE row that is not already forwarded. Zero or several matches is a FINDING
      (`forward_unbound`), never resolved by guessing and never dropped from a denominator; a missing or
      wrong-typed field REFUSES the whole run, exactly as the release/expiry readers do.
    ⇒ an expiry reporting `custody_state = 3` is now ACCEPTED, and is CROSS-CHECKED: the row it closes must
      carry a bound forward event, or the pair is reported as `forward_state_mismatch`. ⛔ A state the stream
      claims but no event produced is precisely the "a success that isn't" shape this census exists to refuse.
    ⓘ The 36-stream corpus contains ZERO forward events (no stream has a delegated eligible row AND a custody
      receipt at the same node), so this ledger is empty there — which is why its controls are synthetic.

★★ THE CUSTODY BINDER. Receipt `custody_failure_rx{reporter, dst, ctr, seq}` at node N. Candidate outward
   origins AT THE SAME NODE N, at or before the receipt, matching on `{dst, ctr}`:
     · `tx_enqueue{origin, dst, ctr, depth}`             (the wrapper / own-origination path)
     · `mobile_ctr_translated{mobile_hash, dst, ctr_m, ctr_h}` with `ctr_h == ctr`   (direct transit)
   EXACTLY ONE candidate binds. Zero -> `unbound`. More than one -> `ambiguous`. Both are their own fail-loud
   census rows and are NEVER dropped from the denominator. A bound report is classified DELEGATED only when a
   `deleg_ack_put` exists at that node with `ctr_h == ctr` AND `peer == dst` (the wire-visible return key of
   §4.4) — an unclassifiable activation is never treated as custody-eligible.

USAGE
    tools/b278_correlation_census.py --corpus <run_corpus.py --out dir>     # the measurement
    tools/b278_correlation_census.py --stream a.ndjson --stream b.ndjson    # instrument-only (synthetic input)
    tools/b278_correlation_census.py --corpus DIR --json out.json           # machine-readable, for the evidence
    tools/b278_correlation_census.py --selftest                             # the built-in controls

⛔ `--stream` IS AN INSTRUMENT MODE and says so in its banner: it skips the manifest authority, so a synthetic
   file can drive the binder's positive and negative shapes. Such input MUST NEVER enter the canonical corpus
   or the anchor table.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

# ---- the ruled bounds -------------------------------------------------------------------------------------------
# ★ DERIVED FROM THE SOURCE, NOT RETYPED FROM THE SPEC PROSE (V1). See `verify_protocol_constants()` below: the
#   census refuses to run if `lib/core/protocol_constants.h` no longer says what these lines claim.
ACK_TTL_MS = 300_000                       # protocol::e2e_ack_deadline_xl_ms  == kDelegAckTtlMs (current code)
SEEN_ORIGIN_TTL_MS = 450_000               # protocol::seen_origin_ttl_ms
CUSTODY_TTL_MS = ACK_TTL_MS + SEEN_ORIGIN_TTL_MS      # the R2 ruled ceiling: 750 000 ms
RING_CAP = 8                               # Node::kDelegAckCap

CUSTODY_AGE_THRESHOLD_MS = ACK_TTL_MS      # §10.2's "more than 300 s after outward origination"
# ★ §B278 S3: `Node::DelegAckCustody::forwarded`, named once so no reader spells the integer 3 inline.
CUSTODY_STATE_FORWARDED = 3


class CensusRefusal(Exception):
    """A fail-loud refusal: the instrument cannot make an honest measurement from this input."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CensusRefusal(message)


_CONST_RE = re.compile(
    r"(?:inline\s+|static\s+)*constexpr\s+(?:unsigned\s+|signed\s+)?\w+\s+(\w+)\s*=\s*([^;]+);")
_TOKEN_RE = re.compile(r"[A-Za-z_]\w*")
_SAFE_EXPR_RE = re.compile(r"^[0-9_+\-*/()% ]+$")


def _strip_comments(text: str) -> str:
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def resolve_cpp_constants(text: str, wanted: tuple[str, ...]) -> dict[str, int]:
    """Resolve `constexpr` integer constants from C++ SOURCE, following one level of naming at a time.

    ⛔ WHY THIS IS NOT A REGEX FOR A LITERAL. The two ruled bounds are DERIVED expressions in the header —
      `seen_origin_ttl_ms = gateway_send_giveup_ms + mac_exchange_margin_ms` and
      `e2e_ack_deadline_xl_ms = 2 * gateway_send_giveup_ms` — so a "find the digits after the `=`" reader would
      have silently failed or, worse, picked up a neighbouring number. This resolver substitutes named constants
      and evaluates ONLY a whitelisted arithmetic form; anything else REFUSES.
    """
    exprs = {name: expr.strip() for name, expr in _CONST_RE.findall(_strip_comments(text))}
    memo: dict[str, int] = {}

    def resolve(name: str, depth: int = 0) -> int:
        require(depth < 16, f"constant `{name}` does not resolve within 16 substitutions")
        if name in memo:
            return memo[name]
        require(name in exprs, f"the source defines no constexpr `{name}` — the bound cannot be derived")
        expr = exprs[name]
        for token in sorted(set(_TOKEN_RE.findall(expr)), key=len, reverse=True):
            if token in ("u", "U", "uL", "ul"):
                continue
            expr = expr.replace(token, str(resolve(token, depth + 1)))
        expr = expr.replace("u", "").replace("U", "")
        require(bool(_SAFE_EXPR_RE.match(expr)),
                f"`{name}` resolves to `{expr}`, which is not a plain arithmetic expression — REFUSING to guess")
        memo[name] = int(eval(expr, {"__builtins__": {}}, {}))          # noqa: S307 — whitelisted above
        return memo[name]

    return {name: resolve(name) for name in wanted}


def verify_protocol_constants(root: Path = ROOT) -> dict[str, int]:
    """Re-derive the ruled bounds and the ring capacity from `lib/core/protocol_constants.h` + `lib/core/node.h`.

    ⛔ A CENSUS THAT HARD-CODES A PROTOCOL CONSTANT IS MEASURING ITS OWN COPY. If a header moves and this file
      does not, the run REFUSES rather than reporting a stale bound as current truth.
    """
    header = root / "lib" / "core" / "protocol_constants.h"
    node_h = root / "lib" / "core" / "node.h"
    require(header.is_file(), f"cannot verify the ruled bounds: {header} is missing")
    require(node_h.is_file(), f"cannot verify the ring capacity: {node_h} is missing")
    found = resolve_cpp_constants(header.read_text(encoding="utf-8", errors="replace"),
                                  ("e2e_ack_deadline_xl_ms", "seen_origin_ttl_ms"))
    found.update(resolve_cpp_constants(node_h.read_text(encoding="utf-8", errors="replace"),
                                       ("kDelegAckCap",)))
    require(found["e2e_ack_deadline_xl_ms"] == ACK_TTL_MS,
            f"e2e_ack_deadline_xl_ms resolves to {found['e2e_ack_deadline_xl_ms']} in the source, this census "
            f"assumes {ACK_TTL_MS} — re-derive the bounds before quoting a number")
    require(found["seen_origin_ttl_ms"] == SEEN_ORIGIN_TTL_MS,
            f"seen_origin_ttl_ms resolves to {found['seen_origin_ttl_ms']} in the source, this census assumes "
            f"{SEEN_ORIGIN_TTL_MS}")
    require(found["kDelegAckCap"] == RING_CAP,
            f"kDelegAckCap is {found['kDelegAckCap']} in node.h, this census assumes {RING_CAP}")
    return found


# ---- events -----------------------------------------------------------------------------------------------------
@dataclass
class Event:
    node: int
    t: int
    kind: str
    data: dict


def parse_stream(path: Path) -> list[Event]:
    events: list[Event] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for lineno, line in enumerate(handle, 1):
            line = line.strip()
            if not line or '"script_emit"' not in line:
                continue
            try:
                raw = json.loads(line)
            except json.JSONDecodeError as exc:
                raise CensusRefusal(f"{path.name}:{lineno} is not JSON: {exc}") from exc
            if raw.get("type") != "script_emit":
                continue
            events.append(Event(node=raw["node"], t=raw["time_ms"],
                                kind=raw["emit_type"], data=raw.get("data") or {}))
    # `lus` writes in time order per node; sorting is a belt-and-braces so the replay never depends on it.
    events.sort(key=lambda e: (e.t,))
    return events


# ---- the row model ----------------------------------------------------------------------------------------------
@dataclass
class Row:
    """One modelled correlation row. `start` is when the slot was first taken (reserve, or put for a direct put)."""
    mobile_hash: int
    ctr_m: int
    layer: int
    start: int
    last_touch: int
    activated_at: int | None = None
    ctr_h: int | None = None
    peer: int | None = None
    target: int | None = None
    target_kind: int | None = None                 # §B278 S1b F6: the mobile API's addressing CHOICE
    released_at: int | None = None                 # observed ACK release (mobile_reverse_ack bound to this row)
    reserved_only: bool = True
    # ★ §B278 S1b F6 — THE OBSERVED END, and the whole reason the lo/hi interval can now close. `end_kind` is
    #   one of "ack" (a translation consumed it), "released" (`deleg_ack_released`), "expired"
    #   (`deleg_ack_expired`) or None (still live when the stream stopped).
    end_kind: str | None = None
    end_at: int | None = None
    release_cause: int | None = None
    custody_at_end: int | None = None
    # ★ §B278 S3: when a `deleg_custody_forwarded` bound to THIS row (None = never forwarded). It is a stamp,
    #   ⛔ not an end: a forwarded row stays ACTIVE and is still closed by its ACK, release or expiry.
    custody_forwarded_at: int | None = None

    def expiry(self, ttl_ms: int) -> int:
        return self.last_touch + ttl_ms

    def end(self, ttl_ms: int) -> int:
        """The instant the slot is free again under `ttl_ms`, given whatever evidence exists.

        ⛔ AN OBSERVED END WINS OVER THE MODELLED ONE, ALWAYS — including an end LATER than the modelled
        expiry. Under S0/S1a the only evidence was an ACK, so the modelled expiry was a ceiling; now a
        `deleg_ack_expired` reports the instant the row really went, and clamping that back to a computed
        ceiling would be the analyzer preferring its own arithmetic to a measurement.
        """
        if self.end_at is not None:
            return self.end_at
        expiry = self.expiry(ttl_ms)
        if self.released_at is not None and self.released_at < expiry:
            return self.released_at
        return expiry


@dataclass
class NodeCensus:
    node: int
    rows: list[Row] = field(default_factory=list)
    reserved_never_activated: int = 0
    direct_puts: int = 0                            # a put with no preceding reservation (park-fire / XL wrapper)
    active_refreshes: int = 0
    put_refused: list[int] = field(default_factory=list)
    ring_refusals: list[int] = field(default_factory=list)      # mobile_ctr_admission_refused reason == 2
    queue_refusals: list[int] = field(default_factory=list)     # mobile_ctr_admission_refused reason == 1
    ack_release_bound: int = 0
    ack_release_unbound: list[dict] = field(default_factory=list)
    ack_release_ambiguous: list[dict] = field(default_factory=list)
    put_bind_ambiguous: list[dict] = field(default_factory=list)
    # ---- §B278 S1b F6: the lifecycle events that close the intervals S0/S1a could only bound -------------
    releases: list[dict] = field(default_factory=list)          # bound `deleg_ack_released`
    release_unbound: list[dict] = field(default_factory=list)   # ⛔ never dropped from a denominator
    expiries: list[dict] = field(default_factory=list)          # bound `deleg_ack_expired`
    expiry_unbound: list[dict] = field(default_factory=list)
    release_causes: dict = field(default_factory=dict)           # cause -> count
    unclosed_rows: int = 0                                       # still live when the stream stopped
    # ---- §B278 S3: the forward ledger ------------------------------------------------------------------
    forwards: list[dict] = field(default_factory=list)            # bound `deleg_custody_forwarded`
    forward_unbound: list[dict] = field(default_factory=list)     # ⛔ never dropped from a denominator
    forward_state_mismatch: list[dict] = field(default_factory=list)   # expiry says 3, no forward event


class Registry:
    """`mobile_registered` / `mobile_reg_expired` replayed per node -> the live local_id -> {key} map.

    ⛔ NO LONGER THE ACK BINDER (§B278 S1b F6 put `mobile_hash`/`ctr_h` on `mobile_reverse_ack` itself). It is
    KEPT because it is still the only way to report WHICH local id an ACK went to alongside the hash the row
    names, and because a disagreement between the two is worth seeing — but it decides nothing.
    """

    def __init__(self) -> None:
        self._live: dict[int, dict[int, int]] = {}          # local_id -> {key: last_registered_ms}

    def register(self, key: int, local_id: int) -> None:
        self._live.setdefault(local_id, {})[key] = 1

    def expire(self, key: int, local_id: int) -> None:
        slot = self._live.get(local_id)
        if slot:
            slot.pop(key, None)

    def resolve(self, local_id: int) -> tuple[int | None, list[int]]:
        """(key, candidates). `key` is None unless EXACTLY ONE key is live on that local id."""
        candidates = sorted(self._live.get(local_id, {}))
        return (candidates[0] if len(candidates) == 1 else None), candidates


def build_node_census(events: list[Event]) -> dict[int, NodeCensus]:
    censuses: dict[int, NodeCensus] = {}
    registries: dict[int, Registry] = {}

    def census(node: int) -> NodeCensus:
        return censuses.setdefault(node, NodeCensus(node=node))

    def registry(node: int) -> Registry:
        return registries.setdefault(node, Registry())

    for ev in events:
        if ev.kind == "mobile_registered":
            registry(ev.node).register(int(ev.data["key"]), int(ev.data["local_id"]))
        elif ev.kind == "mobile_reg_expired":
            registry(ev.node).expire(int(ev.data["key"]), int(ev.data["local_id"]))
        elif ev.kind == "deleg_ack_reserved":
            c = census(ev.node)
            require("target_kind" in ev.data,
                    f"`deleg_ack_reserved` at node {ev.node} t={ev.t} carries no `target_kind` — §B278 S1b F6 "
                    f"appends it, so this stream predates the slice and cannot be censused as if it did not")
            require(type(ev.data["target_kind"]) is int,      # noqa: E721 — the type IDENTITY is the check
                    f"`deleg_ack_reserved.target_kind` is {ev.data['target_kind']!r}, not an integer")
            c.rows.append(Row(mobile_hash=int(ev.data["mobile_hash"]), ctr_m=int(ev.data["ctr_m"]),
                              layer=int(ev.data["layer"]), start=ev.t, last_touch=ev.t,
                              target=int(ev.data["target"]), target_kind=int(ev.data["target_kind"])))
        elif ev.kind == "deleg_ack_put":
            c = census(ev.node)
            mh, ctr_h = int(ev.data["mobile_hash"]), int(ev.data["ctr_h"])
            ctr_m, peer = int(ev.data["ctr_m"]), int(ev.data["peer"])
            layer = int(ev.data["layer"])
            # (a) an exact ACTIVE refresh: same return key AND the same answer, still live under the ACK TTL.
            refresh = [r for r in c.rows
                       if r.activated_at is not None and r.mobile_hash == mh and r.ctr_h == ctr_h
                       and r.peer == peer and r.layer == layer and r.ctr_m == ctr_m
                       and r.released_at is None and r.expiry(ACK_TTL_MS) > ev.t]
            if refresh:
                refresh[-1].last_touch = ev.t
                c.active_refreshes += 1
                continue
            # (b) activation of a live reservation. The reserve event carries no `target_kind`, so the bind uses
            #     the fields it does carry; more than one candidate is AMBIGUOUS and is never resolved by guessing.
            candidates = [r for r in c.rows
                          if r.activated_at is None and r.mobile_hash == mh and r.ctr_m == ctr_m
                          and r.layer == layer and r.released_at is None
                          and r.expiry(ACK_TTL_MS) > ev.t]
            if len(candidates) > 1:
                c.put_bind_ambiguous.append({"t": ev.t, "mobile_hash": mh, "ctr_m": ctr_m, "layer": layer,
                                             "candidates": len(candidates)})
                continue
            if candidates:
                row = candidates[0]
                row.activated_at = ev.t
                row.last_touch = ev.t
                row.ctr_h = ctr_h
                row.peer = peer
                row.reserved_only = False
                continue
            # (c) a DIRECT put: the park-fire sites and the XL wrapper arm activate with no prior reservation.
            c.direct_puts += 1
            c.rows.append(Row(mobile_hash=mh, ctr_m=ctr_m, layer=layer, start=ev.t, last_touch=ev.t,
                              activated_at=ev.t, ctr_h=ctr_h, peer=peer, reserved_only=False))
        elif ev.kind == "deleg_ack_released":
            c = census(ev.node)
            for f in ("mobile_hash", "ctr_m", "target", "target_kind", "layer", "cause"):
                require(f in ev.data, f"`deleg_ack_released` at node {ev.node} t={ev.t} carries no `{f}`")
                require(type(ev.data[f]) is int,               # noqa: E721
                        f"`deleg_ack_released.{f}` is {ev.data[f]!r}, not an integer")
            cause = int(ev.data["cause"])
            # A release only ever matches a RESERVED row, by the production reserve key.
            matches = [r for r in c.rows
                       if r.activated_at is None and r.end_kind is None
                       and r.mobile_hash == int(ev.data["mobile_hash"]) and r.ctr_m == int(ev.data["ctr_m"])
                       and r.layer == int(ev.data["layer"]) and r.target == int(ev.data["target"])]
            rec = {"t": ev.t, "mobile_hash": int(ev.data["mobile_hash"]), "ctr_m": int(ev.data["ctr_m"]),
                   "target": int(ev.data["target"]), "target_kind": int(ev.data["target_kind"]),
                   "cause": cause}
            if len(matches) == 1:
                matches[0].end_kind = "released"
                matches[0].end_at = ev.t
                matches[0].release_cause = cause
                c.releases.append(rec)
                c.release_causes[cause] = c.release_causes.get(cause, 0) + 1
            else:
                c.release_unbound.append(dict(rec, candidates=len(matches),
                                              why="no live RESERVED row with that reserve key"
                                                  if not matches else "several"))
        elif ev.kind == "deleg_custody_forwarded":
            # ★ §B278 S3 — the ONE forward binder. The event names the row: its mobile hash, the HOME counter
            #   it was keyed by, the mobile counter it will carry, and the return peer (`dst`). ⛔ FAIL LOUD on
            #   a missing or wrong-typed field rather than binding on whatever is present.
            c = census(ev.node)
            for f in ("mobile_hash", "dst", "ctr_h", "ctr_m", "type"):
                require(f in ev.data, f"`deleg_custody_forwarded` at node {ev.node} t={ev.t} carries no `{f}`")
                require(type(ev.data[f]) is int,              # noqa: E721 — the type IDENTITY is the check
                        f"`deleg_custody_forwarded.{f}` is {ev.data[f]!r}, not an integer")
            rec = {"t": ev.t, "mobile_hash": int(ev.data["mobile_hash"]), "dst": int(ev.data["dst"]),
                   "ctr_h": int(ev.data["ctr_h"]), "ctr_m": int(ev.data["ctr_m"]),
                   "type": int(ev.data["type"])}
            matches = [r for r in c.rows
                       if r.end_kind is None and r.activated_at is not None
                       and r.custody_forwarded_at is None
                       and r.mobile_hash == rec["mobile_hash"] and r.ctr_h == rec["ctr_h"]
                       and r.ctr_m == rec["ctr_m"] and r.peer == rec["dst"]]
            if len(matches) == 1:
                matches[0].custody_forwarded_at = ev.t
                c.forwards.append(rec)
            else:
                c.forward_unbound.append(dict(rec, candidates=len(matches),
                                              why="no live ACTIVE row with that complete identity"
                                                  if not matches
                                                  else "several live rows share it — never resolved by guessing"))
        elif ev.kind == "deleg_ack_expired":
            c = census(ev.node)
            for f in ("mobile_hash", "ctr_m", "ctr_h", "target", "layer", "custody_state"):
                require(f in ev.data, f"`deleg_ack_expired` at node {ev.node} t={ev.t} carries no `{f}`")
                require(type(ev.data[f]) is int,               # noqa: E721
                        f"`deleg_ack_expired.{f}` is {ev.data[f]!r}, not an integer")
            matches = [r for r in c.rows
                       if r.end_kind is None and r.mobile_hash == int(ev.data["mobile_hash"])
                       and r.ctr_m == int(ev.data["ctr_m"]) and r.layer == int(ev.data["layer"])
                       and (r.target is None or r.target == int(ev.data["target"]))]
            rec = {"t": ev.t, "mobile_hash": int(ev.data["mobile_hash"]), "ctr_m": int(ev.data["ctr_m"]),
                   "ctr_h": int(ev.data["ctr_h"]), "target": int(ev.data["target"]),
                   "custody_state": int(ev.data["custody_state"])}
            if len(matches) == 1:
                matches[0].end_kind = "expired"
                matches[0].end_at = ev.t
                matches[0].custody_at_end = int(ev.data["custody_state"])
                c.expiries.append(rec)
                # ★ §B278 S3 — `forwarded` (3) is now a REACHABLE final state, and it is ACCEPTED with a
                #   cross-check: the row must carry a bound forward event, or the stream is claiming a state
                #   no event produced.
                if int(ev.data["custody_state"]) == CUSTODY_STATE_FORWARDED \
                        and matches[0].custody_forwarded_at is None:
                    c.forward_state_mismatch.append(
                        dict(rec, why="the expiry reports `forwarded` but no `deleg_custody_forwarded` "
                                      "ever bound to this row"))
            else:
                c.expiry_unbound.append(dict(rec, candidates=len(matches),
                                             why="no live row with that identity" if not matches
                                                 else "several live rows share it"))
        elif ev.kind == "deleg_ack_put_refused":
            census(ev.node).put_refused.append(ev.t)
        elif ev.kind == "mobile_ctr_admission_refused":
            c = census(ev.node)
            (c.ring_refusals if int(ev.data.get("reason", 0)) == 2 else c.queue_refusals).append(ev.t)
        elif ev.kind == "mobile_reverse_ack":
            c = census(ev.node)
            # ★ §B278 S1b F6 — THE DIRECT BIND. The event now names the row: its mobile hash and the HOME
            #   counter it was keyed by. ⛔ FAIL LOUD if either is absent or wrong-typed rather than falling
            #   back to the retired registration authority: a silent fallback would report a WEAKER
            #   measurement under the same headline, which is exactly the shape this census exists to refuse.
            for f in ("mobile_hash", "ctr_h"):
                require(f in ev.data,
                        f"`mobile_reverse_ack` at node {ev.node} t={ev.t} carries no `{f}` — §B278 S1b F6 "
                        f"appends it; a stream without it cannot be bound exactly and must not be censused "
                        f"as if it could")
                require(type(ev.data[f]) is int,               # noqa: E721 — the type IDENTITY is the check
                        f"`mobile_reverse_ack.{f}` is {ev.data[f]!r}, not an integer")
            local_id, ctr_m = int(ev.data["local"]), int(ev.data["ctr"])
            key, ctr_h = int(ev.data["mobile_hash"]), int(ev.data["ctr_h"])
            reg_key, candidates = registry(ev.node).resolve(local_id)   # ⓘ diagnostic only; decides nothing
            matches = [r for r in c.rows
                       if r.activated_at is not None and r.mobile_hash == key and r.ctr_m == ctr_m
                       and r.ctr_h == ctr_h and r.end_kind is None and r.released_at is None]
            if len(matches) == 1:
                matches[0].released_at = ev.t
                matches[0].end_kind = "ack"
                matches[0].end_at = ev.t
                c.ack_release_bound += 1
            elif not matches:
                c.ack_release_unbound.append({"t": ev.t, "local": local_id, "ctr_m": ctr_m,
                                              "mobile_hash": key, "ctr_h": ctr_h,
                                              "registry_key": reg_key, "registry_candidates": candidates,
                                              "why": "no live ACTIVE row with that {mobile_hash, ctr_m, ctr_h}"})
            else:
                c.ack_release_ambiguous.append({"t": ev.t, "local": local_id, "ctr_m": ctr_m,
                                               "mobile_hash": key, "ctr_h": ctr_h,
                                               "candidates": len(matches)})
    for c in censuses.values():
        c.reserved_never_activated = sum(1 for r in c.rows if r.activated_at is None)
        # ★ §B278 S1b: the ONLY remaining lo/hi uncertainty. A row whose end was observed contributes the same
        #   span to both arms; one still live when the stream stopped is a property of where the SCENARIO
        #   ends, not of the telemetry, and is reported as its own number rather than folded into a bound.
        c.unclosed_rows = sum(1 for r in c.rows if r.end_kind is None)
    return censuses


def max_occupancy(rows: list[Row], ttl_ms: int, reserved_only_counts: bool) -> tuple[int, int, int, list[Row]]:
    """Sweep the half-open intervals -> (max, window_start, window_end, owning rows).

    `reserved_only_counts=False` is the LOWER arm: a reservation that was never observed to activate could have
    been released silently the same instant (`deleg_ack_release` emits nothing), so it contributes only its
    creation point, never a span.
    """
    spans: list[tuple[int, int, Row]] = []
    for row in rows:
        end = row.end(ttl_ms)
        # ★ §B278 S1b: the lo arm's whole reason was that a silent `deleg_ack_release` could have freed a
        #   never-activated row the same instant it was made. That release is OBSERVED now, so a row with an
        #   observed end contributes the SAME span to both arms — the two bounds coincide wherever the
        #   lifecycle was seen, which is the precision gain F6 was asked for.
        if row.activated_at is None and not reserved_only_counts and row.end_kind is None:
            end = row.start                                     # the instant it demonstrably existed
        spans.append((row.start, end, row))
    points = sorted({p for s, e, _ in spans for p in (s, e)})
    best, best_from, best_to, owners = 0, 0, 0, []
    for i, p in enumerate(points):
        live = [r for s, e, r in spans if s <= p <= e]
        if len(live) > best:
            best = len(live)
            best_from = p
            best_to = points[i + 1] if i + 1 < len(points) else p
            owners = live
    return best, best_from, best_to, owners


# ---- the custody-age binder -------------------------------------------------------------------------------------
@dataclass
class CustodyReport:
    stream: str
    node: int
    t: int
    reporter: int
    dst: int
    ctr: int
    seq: int
    origin_kind: str | None = None
    origin_t: int | None = None
    age_ms: int | None = None
    delegated: bool = False
    status: str = "unbound"                       # bound | unbound | ambiguous
    detail: str = ""
    # ⛔ DIAGNOSTIC ONLY, NEVER A BINDING. For an unbound/ambiguous receipt this lists every emit at the SAME node,
    #    at or before it, whose data carries the same {dst, ctr}. It exists so an unbound row is EXPLAINED rather
    #    than merely counted — it does not widen the binder, and it never turns an unbound receipt into a bound one.
    diagnostic_origins: list = field(default_factory=list)


def _diagnose(node_events: list[Event], rep: "CustodyReport") -> list[dict]:
    """⛔ DIAGNOSTIC ONLY. Every emit at this node, at or before the receipt, carrying the same {dst, ctr}."""
    out: list[dict] = []
    for ev in node_events:
        if ev.t > rep.t or ev.kind == "custody_failure_rx":
            continue
        data = ev.data
        if data.get("dst") != rep.dst:
            continue
        if data.get("ctr") == rep.ctr or data.get("ctr_h") == rep.ctr:
            out.append({"t": ev.t, "emit": ev.kind, "age_ms": rep.t - ev.t, "data": data})
    return out


def bind_custody_reports(stream: str, events: list[Event]) -> list[CustodyReport]:
    by_node_origins: dict[int, list[tuple[int, str, dict]]] = {}
    by_node_puts: dict[int, list[dict]] = {}
    by_node_all: dict[int, list[Event]] = {}
    reports: list[CustodyReport] = []
    for ev in events:
        by_node_all.setdefault(ev.node, []).append(ev)
    for ev in events:
        if ev.kind == "tx_enqueue":
            by_node_origins.setdefault(ev.node, []).append((ev.t, "tx_enqueue", ev.data))
        elif ev.kind == "mobile_ctr_translated":
            by_node_origins.setdefault(ev.node, []).append((ev.t, "mobile_ctr_translated", ev.data))
        elif ev.kind == "deleg_ack_put":
            by_node_puts.setdefault(ev.node, []).append(ev.data)
        elif ev.kind == "custody_failure_rx":
            reports.append(CustodyReport(stream=stream, node=ev.node, t=ev.t,
                                         reporter=int(ev.data["reporter"]), dst=int(ev.data["dst"]),
                                         ctr=int(ev.data["ctr"]), seq=int(ev.data.get("seq", 0))))
    for rep in reports:
        candidates: list[tuple[int, str, dict]] = []
        for t, kind, data in by_node_origins.get(rep.node, []):
            if t > rep.t:
                continue
            if kind == "tx_enqueue" and int(data["dst"]) == rep.dst and int(data["ctr"]) == rep.ctr:
                candidates.append((t, kind, data))
            elif kind == "mobile_ctr_translated" and int(data["dst"]) == rep.dst \
                    and int(data["ctr_h"]) == rep.ctr:
                candidates.append((t, kind, data))
        if not candidates:
            rep.status = "unbound"
            rep.detail = "no tx_enqueue / mobile_ctr_translated at this node matches {dst,ctr} at or before it"
            rep.diagnostic_origins = _diagnose(by_node_all.get(rep.node, []), rep)
            continue
        if len(candidates) > 1:
            rep.status = "ambiguous"
            rep.detail = (f"{len(candidates)} outward origins at this node share {{dst={rep.dst},ctr={rep.ctr}}}; "
                          f"⛔ the nearest is NOT chosen")
            rep.diagnostic_origins = _diagnose(by_node_all.get(rep.node, []), rep)
            continue
        t, kind, _data = candidates[0]
        rep.status = "bound"
        rep.origin_kind = kind
        rep.origin_t = t
        rep.age_ms = rep.t - t
        rep.delegated = any(int(p["ctr_h"]) == rep.ctr and int(p["peer"]) == rep.dst
                            for p in by_node_puts.get(rep.node, []))
    return reports


# ---- the report -------------------------------------------------------------------------------------------------
def analyse(streams: dict[str, Path]) -> dict:
    out: dict = {"streams": {}, "totals": {}}
    for name in sorted(streams):
        events = parse_stream(streams[name])
        censuses = build_node_census(events)
        reports = bind_custody_reports(name, events)
        entry: dict = {"nodes": {}, "custody_reports": [vars(r) for r in reports]}
        for node, c in sorted(censuses.items()):
            if not c.rows and not c.put_refused and not c.ring_refusals and not c.queue_refusals:
                continue
            models = {}
            for label, ttl in (("current_300s", ACK_TTL_MS), ("ruled_upper_750s", CUSTODY_TTL_MS)):
                for arm, counts in (("lo", False), ("hi", True)):
                    best, a, b, owners = max_occupancy(c.rows, ttl, counts)
                    models[f"{label}_{arm}"] = {
                        "max_rows": best, "window_from_ms": a, "window_to_ms": b,
                        "owners": [{"mobile_hash": r.mobile_hash, "ctr_m": r.ctr_m, "ctr_h": r.ctr_h,
                                    "peer": r.peer, "layer": r.layer, "start_ms": r.start,
                                    "end_ms": r.end(ttl), "released": r.released_at} for r in owners],
                    }
            entry["nodes"][str(node)] = {
                "rows": len(c.rows),
                "reserved_never_activated": c.reserved_never_activated,
                "releases": c.releases,
                "release_unbound": c.release_unbound,
                "release_causes": c.release_causes,
                "expiries": c.expiries,
                "expiry_unbound": c.expiry_unbound,
                "forwards": c.forwards,
                "forward_unbound": c.forward_unbound,
                "forward_state_mismatch": c.forward_state_mismatch,
                "unclosed_rows": c.unclosed_rows,
                "ends_observed": sum(1 for r in c.rows if r.end_kind is not None),
                "direct_puts": c.direct_puts,
                "active_refreshes": c.active_refreshes,
                "put_refused": len(c.put_refused),
                "ring_refusals": len(c.ring_refusals),
                "queue_refusals": len(c.queue_refusals),
                "ack_release_bound": c.ack_release_bound,
                "ack_release_unbound": c.ack_release_unbound,
                "ack_release_ambiguous": c.ack_release_ambiguous,
                "put_bind_ambiguous": c.put_bind_ambiguous,
                "models": models,
            }
        out["streams"][name] = entry

    all_reports = [r for s in out["streams"].values() for r in s["custody_reports"]]
    bound = [r for r in all_reports if r["status"] == "bound"]
    out["totals"] = {
        "custody_reports": len(all_reports),
        "bound": len(bound),
        "unbound": sum(1 for r in all_reports if r["status"] == "unbound"),
        "ambiguous": sum(1 for r in all_reports if r["status"] == "ambiguous"),
        "delegated_bound": sum(1 for r in bound if r["delegated"]),
        "ages_ms": sorted(r["age_ms"] for r in bound),
        "ages_over_300s": sum(1 for r in bound if r["age_ms"] > CUSTODY_AGE_THRESHOLD_MS),
        "delegated_ages_over_300s": sum(1 for r in bound
                                        if r["delegated"] and r["age_ms"] > CUSTODY_AGE_THRESHOLD_MS),
        "max_rows_current_300s_hi": max([n["models"]["current_300s_hi"]["max_rows"]
                                         for s in out["streams"].values()
                                         for n in s["nodes"].values()] or [0]),
        "max_rows_ruled_upper_750s_hi": max([n["models"]["ruled_upper_750s_hi"]["max_rows"]
                                             for s in out["streams"].values()
                                             for n in s["nodes"].values()] or [0]),
    }
    return out


def print_report(result: dict, banner: str = "") -> None:
    if banner:
        print(banner)
    print(f"  ruled bounds: ACK {ACK_TTL_MS} ms · custody {ACK_TTL_MS} + {SEEN_ORIGIN_TTL_MS} = "
          f"{CUSTODY_TTL_MS} ms · ring capacity {RING_CAP}")
    print("\n  PER-STREAM / PER-NODE OCCUPANCY  (lo = a never-activated, never-CLOSED reservation contributes no")
    print("                                    span, hi = it is held to its TTL. ★ §B278 S1b: a row whose END was")
    print("                                    OBSERVED — released, expired or ACK-translated — contributes the")
    print("                                    SAME span to both arms, so lo == hi wherever the lifecycle was")
    print("                                    seen; `open` counts the rows still live when the stream stopped,")
    print("                                    which is the only remaining gap and belongs to the scenario.)")
    header = (f"  {'stream':<40}{'node':>5}{'rows':>6}{'resOnly':>8}{'direct':>7}{'refr':>5}"
              f"{'refus':>6}{'ackRel':>7}{'rel':>5}{'exp':>5}{'open':>5}"
              f"{'300lo':>7}{'300hi':>7}{'750lo':>7}{'750hi':>7}")
    print(header)
    print("  " + "-" * (len(header) - 2))
    any_row = False
    for name, entry in sorted(result["streams"].items()):
        for node, n in sorted(entry["nodes"].items(), key=lambda kv: int(kv[0])):
            any_row = True
            print(f"  {name:<40}{node:>5}{n['rows']:>6}{n['reserved_never_activated']:>8}"
                  f"{n['direct_puts']:>7}{n['active_refreshes']:>5}"
                  f"{n['put_refused'] + n['ring_refusals']:>6}{n['ack_release_bound']:>7}"
                  f"{len(n['releases']):>5}{len(n['expiries']):>5}{n['unclosed_rows']:>5}"
                  f"{n['models']['current_300s_lo']['max_rows']:>7}"
                  f"{n['models']['current_300s_hi']['max_rows']:>7}"
                  f"{n['models']['ruled_upper_750s_lo']['max_rows']:>7}"
                  f"{n['models']['ruled_upper_750s_hi']['max_rows']:>7}")
    if not any_row:
        print("  (no stream in this input allocates a correlation row)")

    print("\n  MAXIMUM-OCCUPANCY WINDOWS (the owning identities at the maximum, ruled 750 s upper arm)")
    printed = False
    for name, entry in sorted(result["streams"].items()):
        for node, n in sorted(entry["nodes"].items(), key=lambda kv: int(kv[0])):
            model = n["models"]["ruled_upper_750s_hi"]
            if model["max_rows"] <= 0:
                continue
            printed = True
            print(f"    {name} node {node}: {model['max_rows']} row(s) over "
                  f"[{model['window_from_ms']}, {model['window_to_ms']}] ms")
            for owner in model["owners"]:
                print(f"        mobile_hash={owner['mobile_hash']} ctr_m={owner['ctr_m']} "
                      f"ctr_h={owner['ctr_h']} peer={owner['peer']} layer={owner['layer']} "
                      f"live=[{owner['start_ms']}, {owner['end_ms']}] released={owner['released']}")
    if not printed:
        print("    (none)")

    print("\n  FAIL-LOUD CENSUS (never dropped from a denominator)")
    for name, entry in sorted(result["streams"].items()):
        for node, n in sorted(entry["nodes"].items(), key=lambda kv: int(kv[0])):
            for label in ("ack_release_unbound", "ack_release_ambiguous", "put_bind_ambiguous",
                          "release_unbound", "expiry_unbound"):
                for item in n[label]:
                    print(f"    {name} node {node} {label}: {item}")

    print("\n  §B278 S1b LIFECYCLE LEDGER  (what F6 made observable: every release with its CAUSE, every "
          "expiry with the\n"
          "                              row's exact pre-clear custody state, and the rows still open at the "
          "end of the run)")
    printed_lc = False
    for name, entry in sorted(result["streams"].items()):
        for node, n in sorted(entry["nodes"].items(), key=lambda kv: int(kv[0])):
            if not (n["releases"] or n["expiries"] or n["unclosed_rows"] or n["forwards"]
                    or n["forward_unbound"] or n["forward_state_mismatch"]):
                continue
            printed_lc = True
            print(f"    {name} node {node}: releases={len(n['releases'])} "
                  f"causes={n['release_causes']} expiries={len(n['expiries'])} "
                  f"ends_observed={n['ends_observed']}/{n['rows']} still-open={n['unclosed_rows']}")
            for e in n["expiries"]:
                print(f"        expired  t={e['t']} mobile_hash={e['mobile_hash']} ctr_m={e['ctr_m']} "
                      f"ctr_h={e['ctr_h']} target={e['target']} custody_state={e['custody_state']}")
            for r in n["releases"]:
                print(f"        released t={r['t']} mobile_hash={r['mobile_hash']} ctr_m={r['ctr_m']} "
                      f"target={r['target']} target_kind={r['target_kind']} cause={r['cause']}")
            for f_ in n["forwards"]:
                print(f"        forwarded t={f_['t']} mobile_hash={f_['mobile_hash']} dst={f_['dst']} "
                      f"ctr_h={f_['ctr_h']} ctr_m={f_['ctr_m']} type={f_['type']}")
            for f_ in n["forward_unbound"]:
                print(f"        ⛔ FORWARD UNBOUND t={f_['t']} mobile_hash={f_['mobile_hash']} "
                      f"dst={f_['dst']} ctr_h={f_['ctr_h']} ctr_m={f_['ctr_m']} "
                      f"candidates={f_['candidates']} ({f_['why']})")
            for f_ in n["forward_state_mismatch"]:
                print(f"        ⛔ FORWARD STATE MISMATCH t={f_['t']} mobile_hash={f_['mobile_hash']} "
                      f"ctr_h={f_['ctr_h']} ({f_['why']})")
    if not printed_lc:
        print("    (no stream in this input closes or leaves open a correlation row)")

    tot = result["totals"]
    print("\n  CUSTODY-REPORT AGE LEDGER  (custody_failure_rx bound to its outward origin at the SAME node)")
    print(f"    receipts={tot['custody_reports']}  bound={tot['bound']}  unbound={tot['unbound']}  "
          f"ambiguous={tot['ambiguous']}  delegated-origin={tot['delegated_bound']}")
    print(f"    ages (ms), ascending: {tot['ages_ms']}")
    print(f"    ★ strictly greater than {CUSTODY_AGE_THRESHOLD_MS} ms: {tot['ages_over_300s']}   "
          f"(of which delegated-origin: {tot['delegated_ages_over_300s']})")
    for name, entry in sorted(result["streams"].items()):
        for rep in entry["custody_reports"]:
            print(f"      {name} node {rep['node']} t={rep['t']} reporter={rep['reporter']} dst={rep['dst']} "
                  f"ctr={rep['ctr']} seq={rep['seq']} -> {rep['status']}"
                  + (f" via {rep['origin_kind']}@{rep['origin_t']} age={rep['age_ms']} ms "
                     f"delegated={rep['delegated']}" if rep['status'] == 'bound' else f" ({rep['detail']})"))
            for diag in rep.get("diagnostic_origins", []):
                print(f"          ⛔ DIAGNOSTIC ONLY (not a binding): {diag['emit']}@{diag['t']} "
                      f"age={diag['age_ms']} ms data={diag['data']}")


# ---- the built-in controls --------------------------------------------------------------------------------------
def _synth(lines: list[tuple[int, int, str, dict]]) -> list[Event]:
    return [Event(node=n, t=t, kind=k, data=d) for n, t, k, d in lines]


def selftest() -> int:
    """Positive and sabotage controls with DERIVED expectations. Returns the number of failures."""
    global ACK_TTL_MS
    failures = 0

    def check(label: str, got, want) -> None:
        nonlocal failures
        if got != want:
            failures += 1
            print(f"  FAIL  {label}: got {got!r}, want {want!r}")
        else:
            print(f"  ok    {label}")

    print("§B278 census selftest")
    verify_protocol_constants()
    print("  ok    the ruled bounds re-derive from lib/core/protocol_constants.h + node.h")

    # ---- (P1) the POSITIVE custody bind: one exact delegated origin -> receipt at the same node -------------
    positive = _synth([
        (5, 1_000, "mobile_registered", {"key": 0xAAAA, "local_id": 254, "epoch": 1}),
        (5, 2_000, "deleg_ack_reserved", {"mobile_hash": 0xAAAA, "ctr_m": 1, "target": 30, "layer": 0,
                                          "target_kind": 0}),
        (5, 2_000, "deleg_ack_put", {"mobile_hash": 0xAAAA, "ctr_h": 7, "ctr_m": 1, "peer": 30, "layer": 0}),
        (5, 2_000, "mobile_ctr_translated", {"mobile_hash": 0xAAAA, "dst": 30, "ctr_m": 1, "ctr_h": 7}),
        (5, 402_000, "custody_failure_rx", {"reporter": 31, "dst": 30, "ctr": 7, "seq": 0}),
    ])
    rep = bind_custody_reports("P1", positive)
    check("P1 one receipt", len(rep), 1)
    check("P1 status bound", rep[0].status, "bound")
    check("P1 age is the derived difference", rep[0].age_ms, 402_000 - 2_000)
    check("P1 classified delegated (deleg_ack_put ctr_h==ctr and peer==dst)", rep[0].delegated, True)
    check("P1 age exceeds the 300 s threshold", rep[0].age_ms > CUSTODY_AGE_THRESHOLD_MS, True)

    # ---- (N1) WRONG NODE: the origin is at another node -> must NOT bind --------------------------------------
    wrong_node = _synth([(6 if e.kind == "mobile_ctr_translated" else e.node, e.t, e.kind, e.data)
                         for e in positive])
    rep = bind_custody_reports("N1", wrong_node)
    check("N1 wrong node -> unbound", rep[0].status, "unbound")

    # ---- (N2) WRONG DST ---------------------------------------------------------------------------------------
    wrong_dst = _synth([(e.node, e.t, e.kind, dict(e.data, dst=31) if e.kind == "mobile_ctr_translated"
                         else e.data) for e in positive])
    rep = bind_custody_reports("N2", wrong_dst)
    check("N2 wrong dst -> unbound", rep[0].status, "unbound")

    # ---- (N3) WRONG CTR ---------------------------------------------------------------------------------------
    wrong_ctr = _synth([(e.node, e.t, e.kind, dict(e.data, ctr_h=8) if e.kind == "mobile_ctr_translated"
                         else e.data) for e in positive])
    rep = bind_custody_reports("N3", wrong_ctr)
    check("N3 wrong ctr -> unbound", rep[0].status, "unbound")

    # ---- (N4) WRONG ORDER: the origin happens AFTER the receipt -----------------------------------------------
    wrong_order = _synth([(e.node, 500_000 if e.kind == "mobile_ctr_translated" else e.t, e.kind, e.data)
                          for e in positive])
    rep = bind_custody_reports("N4", wrong_order)
    check("N4 origin after the receipt -> unbound", rep[0].status, "unbound")

    # ---- (N5) AMBIGUOUS: two outward origins share {dst,ctr} at the same node ---------------------------------
    ambiguous = _synth([(e.node, e.t, e.kind, e.data) for e in positive]
                       + [(5, 3_000, "tx_enqueue", {"origin": 5, "dst": 30, "ctr": 7, "depth": 1})])
    rep = bind_custody_reports("N5", ambiguous)
    check("N5 two origins -> ambiguous, never the nearest", rep[0].status, "ambiguous")

    # ---- (N6) NOT DELEGATED: an origin with no matching deleg_ack_put return key ------------------------------
    not_delegated = _synth([(e.node, e.t, e.kind, e.data) for e in positive if e.kind != "deleg_ack_put"])
    rep = bind_custody_reports("N6", not_delegated)
    check("N6 no deleg_ack_put -> bound but NOT delegated", (rep[0].status, rep[0].delegated), ("bound", False))
    wrong_peer = _synth([(e.node, e.t, e.kind, dict(e.data, peer=99) if e.kind == "deleg_ack_put" else e.data)
                         for e in positive])
    rep = bind_custody_reports("N6b", wrong_peer)
    check("N6b deleg_ack_put with a different return key -> NOT delegated", rep[0].delegated, False)

    # ---- (O1) OCCUPANCY: eight rows inside 750 s, none released -----------------------------------------------
    eight = _synth([(9, 1_000 + i * 10_000, "deleg_ack_put",
                     {"mobile_hash": 0xBBBB, "ctr_h": i + 1, "ctr_m": i + 1, "peer": 40 + i, "layer": 0})
                    for i in range(8)])
    c = build_node_census(eight)[9]
    check("O1 eight direct puts modelled", (len(c.rows), c.direct_puts), (8, 8))
    check("O1 max occupancy at 300 s == 8", max_occupancy(c.rows, ACK_TTL_MS, True)[0], 8)
    check("O1 max occupancy at 750 s == 8", max_occupancy(c.rows, CUSTODY_TTL_MS, True)[0], 8)

    # ---- (O2) the 300/750 SPLIT is real: rows spaced so only the 750 s model sees them overlap ----------------
    spaced = _synth([(9, 0, "deleg_ack_put",
                      {"mobile_hash": 0xCCCC, "ctr_h": 1, "ctr_m": 1, "peer": 40, "layer": 0}),
                     (9, 400_000, "deleg_ack_put",
                      {"mobile_hash": 0xCCCC, "ctr_h": 2, "ctr_m": 2, "peer": 41, "layer": 0})])
    c = build_node_census(spaced)[9]
    check("O2 at 300 s the two rows never coexist", max_occupancy(c.rows, ACK_TTL_MS, True)[0], 1)
    check("O2 at 750 s they do  (this is R2's whole cost)", max_occupancy(c.rows, CUSTODY_TTL_MS, True)[0], 2)

    # ---- (O3) ACK RELEASE, bound DIRECTLY on the row's own identity (§B278 S1b F6) -----------------------------
    # ⛔ THE REGISTRATION EVENT IS STILL IN THE FIXTURE AND STILL DECIDES NOTHING: it is here so the diagnostic
    #    `registry_key` can be reported beside the hash the row names, and so a future divergence between the two
    #    is visible rather than invisible.
    released = _synth([
        (9, 0, "mobile_registered", {"key": 0xDDDD, "local_id": 254, "epoch": 1}),
        (9, 1_000, "deleg_ack_reserved", {"mobile_hash": 0xDDDD, "ctr_m": 4, "target": 30, "layer": 0,
                                          "target_kind": 0}),
        (9, 1_000, "deleg_ack_put", {"mobile_hash": 0xDDDD, "ctr_h": 9, "ctr_m": 4, "peer": 30, "layer": 0}),
        (9, 5_000, "mobile_reverse_ack", {"local": 254, "ctr": 4, "mobile_hash": 0xDDDD, "ctr_h": 9}),
    ])
    c = build_node_census(released)[9]
    check("O3 the ACK released exactly one row", c.ack_release_bound, 1)
    check("O3 the row's life ends at the ACK, not at the TTL", c.rows[0].end(CUSTODY_TTL_MS), 5_000)
    check("O3 reserve+put collapse to ONE row, not two", len(c.rows), 1)
    check("O3 the end is OBSERVED, so the row is not left open", (c.rows[0].end_kind, c.unclosed_rows),
          ("ack", 0))
    # ★ THE UPGRADE, MEASURED: removing the registration no longer breaks the bind. Under S0/S1a this exact
    #   input produced an UNBOUND release; the identity now rides on the event itself.
    no_reg = [e for e in released if e.kind != "mobile_registered"]
    c = build_node_census(no_reg)[9]
    check("O3 the registration authority is no longer needed for the bind",
          (c.ack_release_bound, len(c.ack_release_unbound)), (1, 0))

    # ---- (O3s, RE-SCOPED) the ACK names a row this node never activated -> REFUSE, row NOT freed ---------------
    #      Replaces the retired "no registration" arm: the negative coverage is the SAME question (can an ACK be
    #      applied without an identity that decides?), asked of the authority that now answers it.
    wrong_ctr_h = _synth([(e.node, e.t, e.kind,
                           dict(e.data, ctr_h=99) if e.kind == "mobile_reverse_ack" else e.data)
                          for e in released])
    c = build_node_census(wrong_ctr_h)[9]
    check("O3s an ACK whose ctr_h no row activated on -> unbound, row NOT freed",
          (c.ack_release_bound, len(c.ack_release_unbound)), (0, 1))
    check("O3s the row therefore lives to its TTL", c.rows[0].end(CUSTODY_TTL_MS), 1_000 + CUSTODY_TTL_MS)
    wrong_hash = _synth([(e.node, e.t, e.kind,
                          dict(e.data, mobile_hash=0xBEEF) if e.kind == "mobile_reverse_ack" else e.data)
                         for e in released])
    c = build_node_census(wrong_hash)[9]
    check("O3s an ACK naming another mobile's hash -> unbound, never paired by ctr alone",
          (c.ack_release_bound, len(c.ack_release_unbound)), (0, 1))

    # ---- (O3a, RE-SCOPED) TWO live rows share the ACK's whole identity -> refuse to guess ----------------------
    #      This is the shape §4.3's cross-mobile refusal exists to make impossible in production; the census must
    #      still refuse it if a stream ever shows one, rather than clearing the first match.
    twin = _synth([(9, 0, "mobile_registered", {"key": 0xDDDD, "local_id": 254, "epoch": 1}),
                   (9, 1_000, "deleg_ack_reserved", {"mobile_hash": 0xDDDD, "ctr_m": 4, "target": 30,
                                                     "layer": 0, "target_kind": 0}),
                   (9, 1_000, "deleg_ack_put", {"mobile_hash": 0xDDDD, "ctr_h": 9, "ctr_m": 4, "peer": 30,
                                                "layer": 0}),
                   (9, 1_500, "deleg_ack_put", {"mobile_hash": 0xDDDD, "ctr_h": 9, "ctr_m": 4, "peer": 31,
                                                "layer": 0}),
                   (9, 5_000, "mobile_reverse_ack", {"local": 254, "ctr": 4, "mobile_hash": 0xDDDD,
                                                     "ctr_h": 9})])
    c = build_node_census(twin)[9]
    check("O3a two rows sharing {mobile_hash, ctr_m, ctr_h} -> AMBIGUOUS, never the first match",
          (c.ack_release_bound, len(c.ack_release_ambiguous)), (0, 1))

    # ---- (O3f, NEW) the analyzer FAILS LOUD on a missing or wrong-typed identity field -------------------------
    for missing in ("mobile_hash", "ctr_h"):
        stripped = _synth([(e.node, e.t, e.kind,
                            {k: v for k, v in e.data.items() if k != missing}
                            if e.kind == "mobile_reverse_ack" else e.data) for e in released])
        try:
            build_node_census(stripped)
            failures += 1
            print(f"  FAIL  O3f a `mobile_reverse_ack` without `{missing}` did NOT refuse")
        except CensusRefusal:
            print(f"  ok    O3f a `mobile_reverse_ack` without `{missing}` REFUSES (a missing field is not a zero)")
    for bad_value in (False, 2.0, "9"):
        typed = _synth([(e.node, e.t, e.kind,
                         dict(e.data, ctr_h=bad_value) if e.kind == "mobile_reverse_ack" else e.data)
                        for e in released])
        try:
            build_node_census(typed)
            failures += 1
            print(f"  FAIL  O3f a `ctr_h` of {bad_value!r} did NOT refuse")
        except CensusRefusal:
            print(f"  ok    O3f a `ctr_h` of {bad_value!r} REFUSES (bool subclasses int; only a type identity "
                  f"rejects it)")
    no_kind = _synth([(e.node, e.t, e.kind,
                       {k: v for k, v in e.data.items() if k != "target_kind"}
                       if e.kind == "deleg_ack_reserved" else e.data) for e in released])
    try:
        build_node_census(no_kind)
        failures += 1
        print("  FAIL  O3f a `deleg_ack_reserved` without `target_kind` did NOT refuse")
    except CensusRefusal:
        print("  ok    O3f a `deleg_ack_reserved` without `target_kind` REFUSES (a pre-S1b stream is not this "
              "measurement)")

    # ---- (O4) the lo/hi arms differ EXACTLY when a reservation never activated AND never closed ----------------
    stranded = _synth([(9, 0, "deleg_ack_reserved", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                     "layer": 0, "target_kind": 0})])
    c = build_node_census(stranded)[9]
    check("O4 reserved-never-activated counted", c.reserved_never_activated, 1)
    check("O4 an unclosed row is reported as such, not folded into a bound", c.unclosed_rows, 1)
    check("O4 lo arm gives it no span", max_occupancy(c.rows, ACK_TTL_MS, False)[1:3], (0, 0))
    check("O4 hi arm holds it to the TTL", max_occupancy(c.rows, ACK_TTL_MS, True)[2], ACK_TTL_MS)

    # ---- (O5, NEW) §B278 S1b F6 CLOSES THE INTERVAL: an OBSERVED release makes lo == hi -------------------------
    #      This is the precision claim, measured rather than asserted: the SAME reservation, with and without the
    #      new `deleg_ack_released`, and the two occupancy arms coincide only when the lifecycle is observed.
    for cause in range(7):
        closed = _synth([(9, 0, "deleg_ack_reserved", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                       "layer": 0, "target_kind": 0}),
                         (9, 1_234, "deleg_ack_released", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                           "target_kind": 0, "layer": 0, "cause": cause})])
        c = build_node_census(closed)[9]
        if cause == 0:
            check("O5 the release closed the row exactly, with its cause",
                  (len(c.releases), c.rows[0].end_kind, c.rows[0].end_at, c.rows[0].release_cause,
                   c.unclosed_rows), (1, "released", 1_234, 0, 0))
            check("O5 lo and hi now COINCIDE — the S0/S1a gap is closed",
                  (max_occupancy(c.rows, ACK_TTL_MS, False)[2], max_occupancy(c.rows, ACK_TTL_MS, True)[2]),
                  (1_234, 1_234))
    check("O5 all seven release causes are accepted and counted separately",
          sorted(build_node_census(_synth(
              [(9, 0, "deleg_ack_reserved", {"mobile_hash": 0xF00D, "ctr_m": i + 1, "target": 30,
                                             "layer": 0, "target_kind": 0}) for i in range(7)]
              + [(9, 100, "deleg_ack_released", {"mobile_hash": 0xF00D, "ctr_m": i + 1, "target": 30,
                                                 "target_kind": 0, "layer": 0, "cause": i}) for i in range(7)]
          ))[9].release_causes.items()), [(i, 1) for i in range(7)])
    # ⛔ a release naming NO live reserved row is never applied and never dropped from the denominator.
    orphan = _synth([(9, 1_234, "deleg_ack_released", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                       "target_kind": 0, "layer": 0, "cause": 5})])
    c = build_node_census(orphan)[9]
    check("O5s an orphan release is UNBOUND, never invented into a row",
          (len(c.releases), len(c.release_unbound)), (0, 1))

    # ---- (O6, NEW) an EXPIRY closes the row at its exact instant and carries the pre-clear custody state -------
    expired = _synth([(9, 0, "deleg_ack_reserved", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                    "layer": 0, "target_kind": 0}),
                      (9, 0, "deleg_ack_put", {"mobile_hash": 0xF00D, "ctr_h": 5, "ctr_m": 1, "peer": 30,
                                               "layer": 0}),
                      (9, ACK_TTL_MS, "deleg_ack_expired", {"mobile_hash": 0xF00D, "ctr_m": 1, "ctr_h": 5,
                                                            "target": 30, "layer": 0, "custody_state": 2})])
    c = build_node_census(expired)[9]
    check("O6 the expiry closed the row at the named edge, with its exact custody state",
          (len(c.expiries), c.rows[0].end_kind, c.rows[0].end_at, c.rows[0].custody_at_end, c.unclosed_rows),
          (1, "expired", ACK_TTL_MS, 2, 0))
    orphan_exp = _synth([(9, 10, "deleg_ack_expired", {"mobile_hash": 0xF00D, "ctr_m": 1, "ctr_h": 5,
                                                       "target": 30, "layer": 0, "custody_state": 2})])
    c = build_node_census(orphan_exp)[9]
    check("O6s an orphan expiry is UNBOUND, never invented into a row",
          (len(c.expiries), len(c.expiry_unbound)), (0, 1))

    # ---- (O7, §B278 S3) the FORWARD ledger: bind, refuse, and the expiry cross-check ---------------------------
    def _row_pair(extra):
        return _synth([(9, 0, "deleg_ack_reserved", {"mobile_hash": 0xF00D, "ctr_m": 1, "target": 30,
                                                     "layer": 0, "target_kind": 0}),
                       (9, 0, "deleg_ack_put", {"mobile_hash": 0xF00D, "ctr_h": 5, "ctr_m": 1, "peer": 30,
                                                "layer": 0})] + extra)
    fwd = {"mobile_hash": 0xF00D, "dst": 30, "ctr_h": 5, "ctr_m": 1, "type": 0x8B}
    c = build_node_census(_row_pair([(9, 10, "deleg_custody_forwarded", dict(fwd))]))[9]
    check("O7 a forward event binds to EXACTLY ONE live active row and stamps it",
          (len(c.forwards), len(c.forward_unbound), c.rows[0].custody_forwarded_at, c.rows[0].end_kind),
          (1, 0, 10, None))
    # ⛔ the row is STAMPED, not CLOSED: a forwarded row is still ACTIVE and still owes its ACK.
    c = build_node_census(_row_pair([(9, 10, "deleg_custody_forwarded", dict(fwd)),
                                     (9, 20, "mobile_reverse_ack", {"local": 254, "ctr": 1,
                                                                    "mobile_hash": 0xF00D, "ctr_h": 5})]))[9]
    check("O7 a forwarded row is still closed by its later ACK",
          (len(c.forwards), c.rows[0].end_kind, c.rows[0].custody_forwarded_at), (1, "ack", 10))
    # ⛔ a forward naming NO live row is UNBOUND, never invented into one.
    c = build_node_census(_synth([(9, 10, "deleg_custody_forwarded", dict(fwd))]))[9]
    check("O7s an orphan forward is UNBOUND, never invented into a row",
          (len(c.forwards), len(c.forward_unbound)), (0, 1))
    # ⛔ a WRONG-IDENTITY forward (right counter, wrong return peer) is UNBOUND — counter-only binding is RED.
    c = build_node_census(_row_pair([(9, 10, "deleg_custody_forwarded", dict(fwd, dst=31))]))[9]
    check("O7s a forward whose return peer disagrees does NOT bind (counter-only matching is refused)",
          (len(c.forwards), len(c.forward_unbound)), (0, 1))
    # ⛔ AMBIGUITY: two live rows the forward event cannot tell apart -> never resolved by guessing.
    #    They differ only in `layer`, which the event deliberately does not carry (a same-layer duplicate would
    #    be an exact ACTIVE refresh of one row under this model, i.e. not two rows at all).
    amb = _synth([(9, 0, "deleg_ack_put", {"mobile_hash": 0xF00D, "ctr_h": 5, "ctr_m": 1, "peer": 30,
                                           "layer": 0}),
                  (9, 1, "deleg_ack_put", {"mobile_hash": 0xF00D, "ctr_h": 5, "ctr_m": 1, "peer": 30,
                                           "layer": 1}),
                  (9, 10, "deleg_custody_forwarded", dict(fwd))])
    c = build_node_census(amb)[9]
    check("O7s an AMBIGUOUS forward (two live rows share the wire-visible identity) is UNBOUND",
          (len(c.forwards), len(c.forward_unbound), c.forward_unbound[0]["candidates"]), (0, 1, 2))
    # ⛔ a MISSING or WRONG-TYPED field REFUSES the whole run — a missing field is not a zero.
    for bad, label in ((dict(fwd), "type"), (dict(fwd), "ctr_m")):
        broken = dict(bad); broken.pop(label)
        try:
            build_node_census(_row_pair([(9, 10, "deleg_custody_forwarded", broken)]))
            failures += 1
            print(f"  FAIL  O7s: a forward missing `{label}` did NOT refuse")
        except CensusRefusal:
            print(f"  ok    O7s a forward missing `{label}` REFUSES the run")
    try:
        build_node_census(_row_pair([(9, 10, "deleg_custody_forwarded", dict(fwd, ctr_h=True))]))
        failures += 1
        print("  FAIL  O7s: a BOOLEAN `ctr_h` did NOT refuse (bool subclasses int)")
    except CensusRefusal:
        print("  ok    O7s a boolean `ctr_h` REFUSES — only a type-IDENTITY test rejects it")
    # ---- (O8, §B278 S3) `custody_state = 3` at expiry is ACCEPTED, and cross-checked ---------------------------
    c = build_node_census(_row_pair([
        (9, 10, "deleg_custody_forwarded", dict(fwd)),
        (9, ACK_TTL_MS, "deleg_ack_expired", {"mobile_hash": 0xF00D, "ctr_m": 1, "ctr_h": 5, "target": 30,
                                              "layer": 0, "custody_state": CUSTODY_STATE_FORWARDED})]))[9]
    check("O8 an expiry reporting `forwarded` is ACCEPTED when a forward event bound to that row",
          (len(c.expiries), c.rows[0].custody_at_end, len(c.forward_state_mismatch), c.rows[0].end_kind),
          (1, CUSTODY_STATE_FORWARDED, 0, "expired"))
    c = build_node_census(_row_pair([
        (9, ACK_TTL_MS, "deleg_ack_expired", {"mobile_hash": 0xF00D, "ctr_m": 1, "ctr_h": 5, "target": 30,
                                              "layer": 0, "custody_state": CUSTODY_STATE_FORWARDED})]))[9]
    check("O8s an expiry CLAIMING `forwarded` with no forward event behind it is a FINDING",
          (len(c.expiries), len(c.forward_state_mismatch)), (1, 1))

    # ---- (S1) the constant guard REFUSES on a mutated bound ----------------------------------------------------
    saved = ACK_TTL_MS
    ACK_TTL_MS = saved + 1
    try:
        verify_protocol_constants()
        failures += 1
        print("  FAIL  S1: a mutated ACK bound did NOT refuse")
    except CensusRefusal:
        print("  ok    S1 a mutated ACK bound REFUSES against the header (no stale bound is reported as current)")
    finally:
        ACK_TTL_MS = saved

    print(f"\n{'PASS' if failures == 0 else 'FAIL'}: §B278 census selftest, {failures} failure(s)")
    return failures


def main() -> None:
    parser = argparse.ArgumentParser(description="B278 S0 offline correlation-ring / custody-age census.")
    parser.add_argument("--corpus", type=Path, help="a validated `tools/run_corpus.py --out` directory")
    parser.add_argument("--stream", type=Path, action="append",
                        help="an NDJSON stream (INSTRUMENT MODE -- skips the manifest authority)")
    parser.add_argument("--json", type=Path, help="also write the full measurement as JSON")
    parser.add_argument("--selftest", action="store_true", help="run the built-in positive/sabotage controls")
    args = parser.parse_args()

    if args.selftest:
        sys.exit(1 if selftest() else 0)

    try:
        verify_protocol_constants()
        banner = ""
        streams: dict[str, Path] = {}
        if args.corpus:
            manifest_path = args.corpus / "manifest.json"
            require(manifest_path.is_file(), f"{manifest_path} is missing — this is not a corpus run directory")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            records = manifest.get("records") or manifest.get("scenarios") or []
            require(len(records) == 36,
                    f"the manifest describes {len(records)} scenarios, not 36 — a partial corpus is not an "
                    f"authority for an occupancy claim")
            for rec in records:
                name = rec.get("scenario") or rec.get("name")
                path = args.corpus / "streams" / f"{name}.ndjson"
                require(path.is_file(), f"the manifest names {name} but {path} is absent")
                streams[name] = path
            banner = (f"§B278 S0 census over the VALIDATED corpus at {args.corpus}\n"
                      f"  36/36 manifest records present and their streams on disk")
        if args.stream:
            for path in args.stream:
                require(path.is_file(), f"{path} is not a file")
                streams[path.stem] = path
            banner += ("\n  ⛔ INSTRUMENT MODE: one or more --stream inputs bypass the manifest authority. "
                       "Synthetic input must NEVER enter the canonical corpus or the anchor table.")
        require(bool(streams), "nothing to analyse: pass --corpus or --stream")
        result = analyse(streams)
    except CensusRefusal as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        sys.exit(2)

    print_report(result, banner)
    if args.json:
        args.json.write_text(json.dumps(result, indent=1, sort_keys=True), encoding="utf-8")
        print(f"\n  JSON written to {args.json}")


if __name__ == "__main__":
    main()
