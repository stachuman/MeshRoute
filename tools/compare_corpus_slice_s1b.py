#!/usr/bin/env python3
# MeshRoute — tools/compare_corpus_slice_s1b.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§B278 S1b corpus accountant — the correlation-lifecycle delta, and NOTHING else.

WHAT S1b IS ALLOWED TO CHANGE, from its dispatch brief's corpus obligation:

    "Permit only: (1) the appended `target_kind` on an existing `deleg_ack_reserved` line; (2) the appended
     `mobile_hash` and `ctr_h` on an existing `mobile_reverse_ack` line; and (3) a correctly bound
     `deleg_ack_released` or `deleg_ack_expired` line immediately before its corresponding lifecycle clear.
     Every other line, field, ordering relation and event remains byte-identical. Delivery, duplicate,
     failure, airtime and assertion figures must remain identical per stream."

S1b changes ADMISSION (the cross-mobile return-key refusal) as well as telemetry, which makes this
accountant's job different from Slice G's and, in one respect, harder: an admission change CAN move traffic,
and the whole claim of the slice is that on this corpus it does not. So the residue comparison is not merely
a tidiness check — it is the evidence that the refusal never fired on natural per-destination counters.

⛔⛔ THE RESIDUE COMPARISON IS **EXACT, WHOLE-LINE AND ORDERED** — the Slice-A/E/G strong form this repo
    already owns. A histogram is blind to a changed field, a moved event, a reordering and a lifecycle event
    bound to the wrong row; ⓘ `compare_corpus_slice_g.py` records the QG review that rejected exactly that
    weaker cut, and this file does not repeat it.

  ⇒ THE DISCIPLINE IS: **AFTER == BEFORE, MINUS AND PLUS WHOLE PERMITTED LINES, WITH TWO EVENTS ALLOWED TO
    GROW IN PLACE.** Two of the three permitted transformations MODIFY a line at its own stream position
    rather than adding one, so this comparator has a shape Slice G's did not need: a paired-line rule that
    admits an APPEND and refuses everything else about that same line — a changed existing value, a dropped
    field, a different node, a different time.

⚠ THE NDJSON WRITER SORTS `data` KEYS ALPHABETICALLY, so "appended LAST" is NOT observable here and this file
  never claims it is. The field ORDER contract is pinned natively (`EmitRecord::shape()` in the doctest
  suite); what this file owns is the KEY SET and the VALUES of every pre-existing key.

THE CHECKS
  C1  THE EXACT ORDERED RESIDUE. Every line that is not one of the three permitted shapes is byte-identical
      and in the same stream position. This subsumes `delivered`, `rx`, `tx`, `rts_tx`, `cts_rx`, `data_rx`,
      `ack_tx`, `dup_drop`, `send_failed`, `collision`, `rt_update`, every timestamp and every field of every
      other event — NOTHING is excluded from it.
  C2  THE PAIRED-APPEND RULE, per occurrence: a changed `deleg_ack_reserved` / `mobile_reverse_ack` line must
      sit at the SAME position, name the same node and time, keep every pre-existing key AT ITS OLD VALUE,
      and add EXACTLY the named new keys — no more, no fewer.
  C3  MOVEMENT LICENCE, both ways: a stream's bytes changed IFF it carries at least one permitted
      transformation. ⛔ A THIRD MOVER IS A STOP, and so is a predicted mover that did not move.
  C4  THE LIFECYCLE BIND. Every new `deleg_ack_released` / `deleg_ack_expired` names a row that is LIVE at
      that node at that instant, under a model built from the AFTER stream itself: `deleg_ack_reserved` opens
      a row, `deleg_ack_put` activates or direct-opens one, and released/expired/`mobile_reverse_ack` close
      one. An event naming no live row, or naming two, is a finding — never resolved by guessing.
  C5  EMITTED BEFORE THE CLEAR. An expiry is produced by a prune scan INSIDE reserve/put/translate, so its
      line must be immediately followed, at the SAME (node, time_ms), by the event of the operation that
      drove that scan. A trailing expiry would mean the row was cleared first and the values reported are
      whatever survived.
  C6  THE EXPIRY EDGE. Every expired row's age is >= `delegated_custody_ttl_ms`, READ FROM
      `lib/core/protocol_constants.h` (V1), never retyped here; and no row survives a scan at that node once
      it is past the edge.
  C7  FIELD DOMAINS. `target_kind` in {0,1}; `cause` in 0..6; `custody_state` in 0..2 — ⛔ `forwarded` (3) is
      S3's and must be UNREACHABLE in S1b. Every new field must be an INTEGER: `type(v) is int` (a JSON
      `false` and a `0.0` both compare equal to 0 in Python, and `isinstance(False, int)` is true because
      `bool` subclasses `int`, so only the type identity rejects them — G's C5 lesson, applied here).
  C8  THE PER-STREAM TRAFFIC LEDGER, asserted with its OWN sentence even though C1 already carries it:
      delivered / duplicate / send_failed / tx / airtime-bearing counts identical per stream. A slice that
      changes admission must say this out loud, not leave it implied.

USAGE
    python3 tools/compare_corpus_slice_s1b.py <before-run-dir> <after-run-dir>
    python3 tools/compare_corpus_slice_s1b.py <before> <after> --selftest

`--selftest` is not optional in a report: a green result over a real corpus is evidence ONLY if a doctored
view makes each check fail. ⓘ THE CONTROL COUNT IS **DERIVED** from the results list, never written in prose.
"""

from __future__ import annotations

import collections
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# ---- the three permitted transformations, named once ----------------------------------------------------------
RESERVED_EVENT = "deleg_ack_reserved"
REVACK_EVENT   = "mobile_reverse_ack"
RELEASED_EVENT = "deleg_ack_released"
EXPIRED_EVENT  = "deleg_ack_expired"
PUT_EVENT      = "deleg_ack_put"

APPENDED = {                              # event -> the keys S1b appends, and NOTHING else may appear
    RESERVED_EVENT: frozenset({"target_kind"}),
    REVACK_EVENT:   frozenset({"mobile_hash", "ctr_h"}),
}
NEW_EVENTS = (RELEASED_EVENT, EXPIRED_EVENT)

# The events whose emit can drive a prune scan, i.e. what an expiry line must be followed by (C5).
SCAN_DRIVERS = (RESERVED_EVENT, PUT_EVENT, REVACK_EVENT)

# Field domains (C7). `forwarded` == 3 is deliberately OUTSIDE the custody range: S1b must not reach it.
DOMAINS = {
    "target_kind":   (0, 1),
    "cause":         (0, 6),
    "custody_state": (0, 2),
}

# The traffic ledger C8 reports per stream. Chosen because each is a DIFFERENT axis an admission change could
# move: end-to-end success, duplicate suppression, user-visible failure, radio occupancy.
LEDGER_EVENTS = ("delivered", "dup_drop", "send_failed", "tx", "rts_tx", "data_rx", "ack_tx", "nack_tx",
                 "mobile_ctr_translated", "mobile_ctr_admission_refused", "deleg_ack_put_refused")

KEEP, PAIRED, NEW = "keep", "paired", "new"


class Refusal(Exception):
    """A fail-loud refusal: the instrument cannot make an honest comparison from this input."""


# ---- the TTL, READ FROM THE SOURCE (V1) -------------------------------------------------------------------------
_CONST_RE = re.compile(r"constexpr\s+\w+\s+(\w+)\s*=\s*([^;]+);")


def read_ttl_ms(root: Path = ROOT) -> int:
    """`protocol::delegated_custody_ttl_ms`, resolved one alias at a time from the real header.

    ⛔ NOT a literal and NOT a regex for digits: the constant is a chain of NAMES
    (`delegated_custody_ttl_ms = e2e_ack_deadline_xl_ms = 2 * gateway_send_giveup_ms`), so a "find the number"
    reader would have picked up a neighbour. If the chain stops resolving, this REFUSES rather than guessing.
    """
    text = (root / "lib" / "core" / "protocol_constants.h").read_text(encoding="utf-8")
    values: dict[str, str] = {}
    for m in _CONST_RE.finditer(text):
        values.setdefault(m.group(1), m.group(2).strip())
    if "delegated_custody_ttl_ms" not in values:
        raise Refusal("lib/core/protocol_constants.h no longer defines `delegated_custody_ttl_ms` — "
                      "the expiry edge cannot be verified against the source")
    expr = values["delegated_custody_ttl_ms"]
    for _ in range(8):
        if re.fullmatch(r"[0-9_+\-*/() ]+", expr):
            return int(eval(expr.replace("_", "")))          # noqa: S307 — the guard above admits arithmetic only
        nxt = re.sub(r"[A-Za-z_]\w*", lambda m: f"({values[m.group(0)]})" if m.group(0) in values else m.group(0),
                     expr)
        if nxt == expr:
            break
        expr = nxt
    raise Refusal(f"`delegated_custody_ttl_ms` does not resolve to an integer: {expr!r}")


# ---- line plumbing ----------------------------------------------------------------------------------------------
def md5_of(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def read_lines(path: Path):
    with path.open() as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line:
                yield line


_INTEREST = (RESERVED_EVENT, REVACK_EVENT, RELEASED_EVENT, EXPIRED_EVENT)


def classify(line: str):
    """(class, parsed-or-None). ⛔ `KEEP` lines are NEVER parsed — they are compared as RAW STRINGS, so a
    field this function has never heard of is still compared."""
    if not any(name in line for name in _INTEREST):
        return KEEP, None                                    # fast path: the overwhelming majority
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return KEEP, None
    if obj.get("type") != "script_emit":
        return KEEP, None
    ev = obj.get("emit_type")
    if ev in APPENDED:
        return PAIRED, obj
    if ev in NEW_EVENTS:
        return NEW, obj
    return KEEP, None


def _ident(obj):
    return (obj.get("node"), obj.get("time_ms"), obj.get("emit_type"))


def _row_key(data: dict):
    """The row identity a lifecycle event names, and it is THE PRODUCTION RESERVE KEY minus its kind byte:
    `{mobile_hash, ctr_m, layer, target}` (`node_hashlocate.cpp` `deleg_ack_reserve`).

    ⛔ `target` IS LOAD-BEARING AND WAS ADDED AFTER A MEASURED MISS. A first cut keyed only
    `{mobile_hash, ctr_m, layer}` and reported two false findings on `s07`: one hosted mobile there sends
    SEVEN flights that all carry `ctr_m == 1` (the mobile mints per destination too), so three live rows share
    that triple at once and the model kept overwriting them. The retained `target` is exactly what tells them
    apart — which is also why §4.2 makes the row keep it instead of collapsing it at activation.
    """
    return (data.get("mobile_hash"), data.get("ctr_m"), data.get("layer"), data.get("target"))


# ---- the comparison ---------------------------------------------------------------------------------------------
def compare_streams(name, before_lines, after_lines, b_md5, a_md5, ttl_ms, out):
    """Pure over two ITERABLES of raw lines, so the selftest can drive every control against a doctored view."""
    bad = 0
    appended_hits = collections.Counter()
    new_lines: list = []          # (index_in_after, obj)
    after_seq: list = []          # (emit_type, node, time_ms) for every PAIRED/NEW/put line, in order (C4/C5)
    put_lines: list = []

    bi, ai = iter(before_lines), iter(after_lines)
    idx = 0
    residue_bad = 0
    pending_b = None
    a_pos = 0

    def next_after():
        """Advance the AFTER stream, striking the AFTER-only new events into their sink."""
        nonlocal a_pos
        for raw in ai:
            a_pos += 1
            cls, obj = classify(raw)
            if cls is NEW:
                new_lines.append((a_pos, obj))
                after_seq.append((obj.get("emit_type"), obj.get("node"), obj.get("time_ms")))
                continue
            if cls is PAIRED:
                after_seq.append((obj.get("emit_type"), obj.get("node"), obj.get("time_ms")))
            elif PUT_EVENT in raw:
                try:
                    o = json.loads(raw)
                    if o.get("type") == "script_emit" and o.get("emit_type") == PUT_EVENT:
                        put_lines.append(o)
                        after_seq.append((PUT_EVENT, o.get("node"), o.get("time_ms")))
                except json.JSONDecodeError:
                    pass
            return raw, cls, obj
        return None, None, None

    while True:
        b = pending_b if pending_b is not None else next(bi, None)
        pending_b = None
        a, a_cls, a_obj = next_after()
        if b is None and a is None:
            break
        if b == a:
            idx += 1
            continue
        # A permitted PAIRED transformation: the SAME line at the SAME position, grown by exactly its keys.
        ok = False
        if b is not None and a is not None and a_cls is PAIRED:
            try:
                b_obj = json.loads(b)
            except json.JSONDecodeError:
                b_obj = None
            if b_obj is not None and _ident(b_obj) == _ident(a_obj):
                ev = a_obj.get("emit_type")
                bd, ad = b_obj.get("data") or {}, a_obj.get("data") or {}
                added = set(ad) - set(bd)
                dropped = set(bd) - set(ad)
                changed = [k for k in bd if k in ad and bd[k] != ad[k]]
                envelope_ok = {k: v for k, v in b_obj.items() if k != "data"} == \
                              {k: v for k, v in a_obj.items() if k != "data"}
                if envelope_ok and added == set(APPENDED[ev]) and not dropped and not changed:
                    appended_hits[ev] += 1
                    ok = True
                else:
                    out.append(f"  C2  FAIL {name}: `{ev}` at node {a_obj.get('node')} "
                               f"t={a_obj.get('time_ms')} is not a pure APPEND — added {sorted(added)}, "
                               f"dropped {sorted(dropped)}, changed {sorted(changed)}, "
                               f"envelope_ok={envelope_ok}")
                    bad += 1
                    ok = True                                # reported with its own message; not residue too
        if not ok:
            residue_bad += 1
            if residue_bad == 1:
                out.append(f"  C1  FAIL {name}: the ORDERED residue diverges at kept-line #{idx}")
                out.append(f"        BEFORE  {(b or '<end of stream>')[:190]}")
                out.append(f"        AFTER   {(a or '<end of stream>')[:190]}")
            if residue_bad > 3:
                out.append(f"  C1  FAIL {name}: … and further residue mismatches (stopped reporting)")
                break
        idx += 1
    if residue_bad:
        out.append(f"  C1  FAIL {name}: {residue_bad} non-permitted line(s) moved  <- STOP (corpus obligation)")
        bad += 1

    licensed = bool(appended_hits) or bool(new_lines)
    changed_bytes = (b_md5 != a_md5)
    if changed_bytes and not licensed:
        out.append(f"  C3  FAIL {name}: bytes moved ({b_md5}->{a_md5}) with NO permitted transformation "
                   f"— an unpredicted mover is a STOP, not a comparator entry")
        bad += 1
    if licensed and not changed_bytes:
        out.append(f"  C3  FAIL {name}: carries {sum(appended_hits.values())} appended + {len(new_lines)} new "
                   f"lifecycle line(s) and did NOT change")
        bad += 1

    # ⓘ C4/C5/C6/C7 are NOT done here. They need the ordered CORRELATION-EVENT view of the after stream, which
    #   `check_lifecycle` builds from `read_after_events` — a second, cheap pass that touches only lines whose
    #   raw text already contains a correlation event name. Keeping them out of this walk is deliberate: this
    #   function's one job is the exact ordered residue, and mixing a semantic model into it is how a residue
    #   comparison quietly turns back into a histogram.
    return bad, appended_hits, new_lines, put_lines, after_seq


def read_after_events(lines):
    """Ordered correlation-event view of one stream (parsed), used by the lifecycle model."""
    for raw in lines:
        if not any(n in raw for n in (RESERVED_EVENT, REVACK_EVENT, RELEASED_EVENT, EXPIRED_EVENT, PUT_EVENT)):
            continue
        try:
            obj = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if obj.get("type") == "script_emit" and obj.get("emit_type") in (
                RESERVED_EVENT, REVACK_EVENT, RELEASED_EVENT, EXPIRED_EVENT, PUT_EVENT):
            yield obj


def check_lifecycle(name, after_events, ttl_ms, out):
    """C4/C5/C6/C7 over the ordered correlation events of ONE after-stream.

    The model mirrors `node_hashlocate.cpp`'s ring, not a simplification of it: a reservation OPENS a row under
    the production reserve key, an activation refreshes its stamp and records the return counter, and an ACK
    translation / release / expiry CLOSES exactly one row. ⛔ Where the telemetry cannot decide WHICH row an
    event names, this says so and counts a finding — it never picks the nearest one.
    """
    bad = 0
    live: dict = {}                       # (node,) + _row_key -> {"t", "state", "ctr_h"}
    events = list(after_events)
    emitted_at_instant: dict = {}         # (node, time_ms) -> the first NON-expiry correlation event there (C5)

    def loose(node, mh, ctr_m, layer):
        """Rows at `node` matching the terms an event that carries NO target can offer."""
        return [k for k in live if k[0] == node and k[1] == mh and k[2] == ctr_m and k[3] == layer]

    for i, obj in enumerate(events):
        ev, node, t = obj.get("emit_type"), obj.get("node"), obj.get("time_ms")
        data = obj.get("data") or {}
        if ev != EXPIRED_EVENT:
            emitted_at_instant.setdefault((node, t), ev)
        # ★ C6-COMPLETENESS — THE OTHER HALF OF THE EDGE, and the one an "is this expiry legitimate?" check
        #   cannot supply: a MISSING expiry. All three prune sites (reserve / put / translate) clear EVERY
        #   expired row before their own event emits, and expiry lines precede that event, so by the time a
        #   scan-driving event is processed here NO row at that node may still be past its edge. A row that
        #   survives one is a lifecycle event the slice failed to emit.
        if ev in SCAN_DRIVERS:
            for k in [k for k in live if k[0] == node and t - live[k]["t"] >= ttl_ms
                      and not live[k].get("age_uncertain")]:
                out.append(f"  C6  FAIL {name}: a row {k[1:]} at node {node} was {t - live[k]['t']} ms old "
                           f"(edge {ttl_ms} ms) when `{ev}` scanned at t={t} and produced NO "
                           f"`{EXPIRED_EVENT}` — the lifecycle event is MISSING, not merely unproven")
                bad += 1
                live.pop(k, None)                     # report once; the model moves on as production would
        if ev == RESERVED_EVENT:
            live[(node,) + _row_key(data)] = {"t": t, "state": "reserved", "ctr_h": None}
        elif ev == PUT_EVENT:
            # ⚠ `deleg_ack_put` carries NO `target`, and S1b may not add one — its schema is frozen by the
            #   corpus obligation: only `deleg_ack_reserved` and `mobile_reverse_ack` gain fields. On `s07`
            #   ONE hosted mobile holds three live reservations at once, all with `ctr_m == 1`, so the loose
            #   triple genuinely cannot name which reservation an activation confirmed.
            # ⇒ THE TIE-BREAK IS AN ORDERING FACT, NOT A GUESS: a direct-transit reservation and its activation
            #   happen inside ONE `handle_data` call and therefore share `time_ms`. A same-instant reservation
            #   is the one this put confirmed. If that still does not decide, the model REFUSES to pick and
            #   marks the affected rows age-uncertain rather than reporting an age it cannot defend.
            cands = loose(node, data.get("mobile_hash"), data.get("ctr_m"), data.get("layer"))
            same_instant = [k for k in cands if live[k]["t"] == t and live[k]["state"] == "reserved"]
            pick = cands[0] if len(cands) == 1 else (same_instant[0] if len(same_instant) == 1 else None)
            if pick is not None:
                row = live[pick]
                row["t"] = t                                  # activation re-stamps the row (`e.ts_ms = now`)
                row["state"] = "active"
                row["ctr_h"] = data.get("ctr_h")
            elif not cands:
                # a DIRECT put (the park-fire sites / the wrapper XL arm): it opens its own row, and the event
                # carries no target, so the row is recorded with `target = None`.
                live[(node, data.get("mobile_hash"), data.get("ctr_m"), data.get("layer"), None)] = {
                    "t": t, "state": "active", "ctr_h": data.get("ctr_h")}
            else:
                for k in cands:
                    live[k]["age_uncertain"] = True
                out.append(f"  NOTE {name}: `{PUT_EVENT}` at node {node} t={t} matches {len(cands)} live "
                           f"reservations and none at this instant — the ages of those rows become LOWER "
                           f"BOUNDS, and the strict edge check below is suspended for them (never guessed)")
        elif ev == REVACK_EVENT:
            # ★ S1b's precision gain: the event now carries the row's OWN identity, so the bind is exact and
            #   does not go through the registration authority (which could not decide a re-used local id).
            cands = [k for k in live
                     if k[0] == node and k[1] == data.get("mobile_hash") and k[2] == data.get("ctr")
                     and live[k]["ctr_h"] == data.get("ctr_h")]
            if len(cands) == 1:
                live.pop(cands[0], None)
            elif not cands:
                out.append(f"  C4  FAIL {name}: `{REVACK_EVENT}` at node {node} t={t} translated a row that "
                           f"the model never saw ACTIVE ({data.get('mobile_hash')}, ctr_m={data.get('ctr')}, "
                           f"ctr_h={data.get('ctr_h')})")
                bad += 1
            else:
                out.append(f"  C4  FAIL {name}: `{REVACK_EVENT}` at node {node} t={t} matches {len(cands)} "
                           f"rows — an ACK that could clear either is the misdelivery §4.3 forbids")
                bad += 1
        elif ev in NEW_EVENTS:
            key = (node,) + _row_key(data)
            row = live.get(key)
            if row is None:
                out.append(f"  C4  FAIL {name}: `{ev}` at node {node} t={t} names NO live row "
                           f"{_row_key(data)} — a lifecycle event without a row is a fabricated clear")
                bad += 1
                continue
            if ev == EXPIRED_EVENT:
                age = t - row["t"]
                if age < ttl_ms and not row.get("age_uncertain"):
                    out.append(f"  C6  FAIL {name}: `{ev}` at node {node} t={t} expired a row aged {age} ms, "
                               f"below the named edge {ttl_ms} ms")
                    bad += 1
                if row["ctr_h"] is not None and data.get("ctr_h") != row["ctr_h"]:
                    out.append(f"  C4  FAIL {name}: `{ev}` at node {node} t={t} reports ctr_h "
                               f"{data.get('ctr_h')} but the row activated on {row['ctr_h']}")
                    bad += 1
                # ★ C5, AND ITS FIRST CUT WAS TOO WEAK — recorded because the selftest caught it, not a review:
                #   "immediately followed by a scan driver" passed a doctored stream in which the expiry had
                #   been moved BEHIND its own `deleg_ack_reserved`, because the very next correlation line was
                #   the `deleg_ack_put` of the same instant, which is also a driver.
                # ⇒ THE RULE IS THE INVARIANT ITSELF: a prune scan clears EVERY expired row before its
                #   operation emits, and the first scan of an instant leaves nothing for the later ones. So at
                #   one (node, time_ms) EVERY expiry must precede EVERY other correlation event. An expiry that
                #   trails one was emitted after the clear, and its values are whatever survived it.
                if (node, t) in emitted_at_instant:
                    out.append(f"  C5  FAIL {name}: `{ev}` at node {node} t={t} follows "
                               f"`{emitted_at_instant[(node, t)]}` at the same instant — a prune scan clears "
                               f"before its operation emits, so this was emitted AFTER the clear")
                    bad += 1
            live.pop(key, None)
        # C7 — field domains and integer identity, on every new/paired event
        for k, (lo, hi) in DOMAINS.items():
            if k in data:
                v = data[k]
                if type(v) is not int:                       # noqa: E721 — the type IDENTITY is the check
                    out.append(f"  C7  FAIL {name}: `{ev}` field `{k}` is {v!r} ({type(v).__name__}), "
                               f"not the INTEGER the schema rules")
                    bad += 1
                elif not (lo <= v <= hi):
                    out.append(f"  C7  FAIL {name}: `{ev}` field `{k}` = {v}, outside the ruled range "
                               f"[{lo},{hi}]" + (" — `forwarded` is S3's and must be UNREACHABLE"
                                                 if k == "custody_state" else ""))
                    bad += 1
        if ev == REVACK_EVENT or ev == RESERVED_EVENT:
            for k in APPENDED[ev]:
                if k not in data:
                    out.append(f"  C2  FAIL {name}: `{ev}` at node {node} t={t} is missing the appended "
                               f"field `{k}` — a MISSING field is not a zero")
                    bad += 1
                elif type(data[k]) is not int:               # noqa: E721
                    out.append(f"  C2  FAIL {name}: `{ev}` appended field `{k}` is not an integer")
                    bad += 1
    return bad


def ledger(lines):
    """C8's per-stream traffic ledger, counted from RAW lines (no parse on the hot path)."""
    c = collections.Counter()
    for raw in lines:
        for ev in LEDGER_EVENTS:
            if f'"emit_type":"{ev}"' in raw:
                c[ev] += 1
                break
    return c


# ---- driver -----------------------------------------------------------------------------------------------------
def run(before_dir: Path, after_dir: Path, out) -> int:
    b_streams = sorted((before_dir / "streams").glob("*.ndjson"))
    if not b_streams:
        out.append(f"REFUSED: no streams under {before_dir}/streams")
        return 1
    ttl_ms = read_ttl_ms()
    out.append(f"  expiry edge read from lib/core/protocol_constants.h: delegated_custody_ttl_ms = {ttl_ms} ms")
    bad, moved, totals = 0, [], collections.Counter()
    for bp in b_streams:
        ap = after_dir / "streams" / bp.name
        if not ap.exists():
            out.append(f"  FAIL {bp.stem}: no AFTER stream")
            bad += 1
            continue
        b_md5, a_md5 = md5_of(bp), md5_of(ap)
        n, hits, new_lines, _puts, _seq = compare_streams(bp.stem, read_lines(bp), read_lines(ap),
                                                          b_md5, a_md5, ttl_ms, out)
        bad += n
        bad += check_lifecycle(bp.stem, read_after_events(read_lines(ap)), ttl_ms, out)
        lb, la = ledger(read_lines(bp)), ledger(read_lines(ap))
        if lb != la:
            diff = {k: (lb.get(k, 0), la.get(k, 0)) for k in set(lb) | set(la) if lb.get(k, 0) != la.get(k, 0)}
            out.append(f"  C8  FAIL {bp.stem}: the traffic ledger moved {diff}  <- an admission change that "
                       f"moved delivery/duplicate/failure/airtime is a STOP")
            bad += 1
        totals.update(hits)
        totals["new_lifecycle"] += len(new_lines)
        if b_md5 != a_md5:
            moved.append((bp.stem, b_md5, a_md5, dict(hits), len(new_lines)))
    out.append("")
    out.append(f"  streams: {len(b_streams)} · moved: {len(moved)} · appended "
               f"{RESERVED_EVENT}={totals[RESERVED_EVENT]} {REVACK_EVENT}={totals[REVACK_EVENT]} · "
               f"new lifecycle lines={totals['new_lifecycle']}")
    for nm, b, a, hits, nl in moved:
        out.append(f"    {nm:44s} {b} -> {a}   appended={hits} new_lifecycle={nl}")
    return bad


# ---- selftest ---------------------------------------------------------------------------------------------------
def run_selftest(before_dir: Path, after_dir: Path, out) -> int:
    """Every control doctors ONE thing and must go RED. The count is DERIVED from the results list."""
    ttl_ms = read_ttl_ms()
    cand = None
    for bp in sorted((before_dir / "streams").glob("*.ndjson")):
        ap = after_dir / "streams" / bp.name
        if not ap.exists():
            continue
        if md5_of(bp) != md5_of(ap):
            if cand is None or bp.stat().st_size < cand[0].stat().st_size:
                cand = (bp, ap)
    if cand is None:
        out.append("SELFTEST REFUSED: no stream moved — every control would be vacuous")
        return 1
    bp, ap = cand
    B, A, name = list(read_lines(bp)), list(read_lines(ap)), bp.stem
    # A stream that moved but carries NO new lifecycle line cannot exercise C4/C5/C6; pick the richest mover
    # for those controls separately, and REFUSE rather than silently skip if none exists.
    rich = None
    for bq in sorted((before_dir / "streams").glob("*.ndjson")):
        aq = after_dir / "streams" / bq.name
        if aq.exists() and any(f'"emit_type":"{EXPIRED_EVENT}"' in ln for ln in read_lines(aq)):
            rich = (bq, aq)
            break
    if rich is None:
        out.append("SELFTEST REFUSED: no stream carries a `deleg_ack_expired` — the lifecycle controls "
                   "would be vacuous")
        return 1
    RB, RA = list(read_lines(rich[0])), list(read_lines(rich[1]))
    rich_name = rich[0].stem

    base: list = []
    n0, _, _, _, _ = compare_streams(name, B, A, "aaaaaaaa", "bbbbbbbb", ttl_ms, base)
    n0 += check_lifecycle(name, read_after_events(A), ttl_ms, base)
    if n0:
        out.append(f"SELFTEST REFUSED: the undoctored {name} is already RED — controls would be meaningless")
        out.extend("    " + m for m in base[:4])
        return 1

    results = []

    def control(label, mutate_after=None, mutate_before=None, use_rich=False):
        b = list(RB if use_rich else B)
        a = list(RA if use_rich else A)
        nm = rich_name if use_rich else name
        if mutate_before:
            mutate_before(b)
        if mutate_after:
            mutate_after(a)
        msgs: list = []
        k, _, _, _, _ = compare_streams(nm, b, a, "aaaaaaaa", "bbbbbbbb", ttl_ms, msgs)
        k += check_lifecycle(nm, read_after_events(a), ttl_ms, msgs)
        # the ledger check, exactly as the driver runs it
        if ledger(b) != ledger(a):
            msgs.append("  C8  FAIL: the traffic ledger moved")
            k += 1
        fired = k > 0
        results.append(fired)
        out.append(f"  {'RED  (control fired)' if fired else 'GREEN — CONTROL DID NOT FIRE'}  {label}")
        if fired:
            out.append(f"        {msgs[0].strip()}")

    def first_index(lines, pred):
        for i, ln in enumerate(lines):
            if pred(ln):
                return i
        return -1

    paired_i = first_index(A, lambda ln: classify(ln)[0] is PAIRED)
    if paired_i < 0:
        out.append("SELFTEST REFUSED: the chosen mover carries no appended line")
        return 1
    keep_i = first_index(A[paired_i + 1:], lambda ln: classify(ln)[0] is KEEP)
    if keep_i < 0:
        out.append("SELFTEST REFUSED: no ordinary line follows the first permitted line")
        return 1
    keep_i += paired_i + 1
    # ★ THE DOCTORED LINE SITS **AFTER** the first permitted line on purpose: a control acting at position 0
    #   would never exercise the striking, so it could pass against a walk that ignored the permitted shapes.

    def corrupt_field(a):
        o = json.loads(a[keep_i]); d = o.get("data")
        if isinstance(d, dict) and d:
            k = sorted(d)[0]
            d[k] = (d[k] + 1) if isinstance(d[k], (int, float)) and not isinstance(d[k], bool) else "X"
        else:
            o["time_ms"] = (o.get("time_ms") or 0) + 1
        a[keep_i] = json.dumps(o, separators=(",", ":"))
    control("A FIELD of an ordinary event is corrupted", corrupt_field)

    def reorder(a):
        j = first_index(a[keep_i + 1:], lambda ln: classify(ln)[0] is KEEP)
        j = keep_i + 1 + j
        a[keep_i], a[j] = a[j], a[keep_i]
    control("two ordinary events are REORDERED — same multiset, different stream", reorder)

    control("one ordinary event DISAPPEARS", lambda a: a.pop(keep_i))
    control("one `delivered` event is ADDED — the traffic ledger must move",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 1, "time_ms": 1, "emit_type": "delivered", "data": {}},
                separators=(",", ":"))))

    def drop_appended(a):
        o = json.loads(a[paired_i]); d = o.get("data") or {}
        for k in APPENDED[o["emit_type"]]:
            d.pop(k, None)
        a[paired_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("the APPENDED field is missing — a slice that only claimed to add it", drop_appended)

    def change_existing(a):
        o = json.loads(a[paired_i]); d = o.get("data") or {}
        for k in sorted(d):
            if k not in APPENDED[o["emit_type"]] and isinstance(d[k], int) and not isinstance(d[k], bool):
                d[k] = d[k] + 1
                break
        a[paired_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("an EXISTING field of an appended event changed value — an append is not a rewrite", change_existing)

    def extra_field(a):
        o = json.loads(a[paired_i]); (o.setdefault("data", {}))["custody_state"] = 1
        a[paired_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("an UNREVIEWED extra field rides along with the appended one", extra_field)

    def move_paired(a):
        j = first_index(a[paired_i + 1:], lambda ln: classify(ln)[0] is KEEP)
        j = paired_i + 1 + j
        a[paired_i], a[j] = a[j], a[paired_i]
    control("the appended event MOVED to a different stream position", move_paired)

    # ---- the lifecycle controls, on the stream that really carries an expiry -------------------------------
    exp_i = first_index(RA, lambda ln: f'"emit_type":"{EXPIRED_EVENT}"' in ln)

    control("an expected `deleg_ack_expired` is OMITTED — the row silently never closed",
            lambda a: a.pop(exp_i), use_rich=True)

    def fabricate(a):
        a.insert(exp_i, json.dumps(
            {"type": "script_emit", "node": 99, "time_ms": 7, "emit_type": EXPIRED_EVENT,
             "data": {"mobile_hash": 1, "ctr_m": 1, "ctr_h": 1, "target": 1, "layer": 0, "custody_state": 2}},
            separators=(",", ":")))
    control("a lifecycle event is INSERTED with no live row behind it", fabricate, use_rich=True)

    def wrong_identity(a):
        o = json.loads(a[exp_i]); (o.setdefault("data", {}))["mobile_hash"] = 0xDEAD
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("the expiry names the WRONG row identity", wrong_identity, use_rich=True)

    def wrong_target(a):
        o = json.loads(a[exp_i]); (o.setdefault("data", {}))["target"] = 251
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("the expiry reports a target the row never retained", wrong_target, use_rich=True)

    def after_the_clear(a):
        line = a[exp_i]
        a.pop(exp_i)
        a.insert(exp_i + 1, line)                    # now it trails its scan-driving event
    control("the expiry is emitted AFTER the clear (it no longer precedes its scan driver)",
            after_the_clear, use_rich=True)

    def early_edge(a):
        o = json.loads(a[exp_i]); o["time_ms"] = int(o["time_ms"]) - ttl_ms
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("a row expires BEFORE the named 300 s edge", early_edge, use_rich=True)

    def forwarded_state(a):
        o = json.loads(a[exp_i]); (o.setdefault("data", {}))["custody_state"] = 3
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("a row reports `forwarded` — S3's state, unreachable in S1b", forwarded_state, use_rich=True)

    def float_field(a):
        o = json.loads(a[exp_i]); (o.setdefault("data", {}))["custody_state"] = 2.0
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("`custody_state` is the FLOAT 2.0 — equal to 2 in Python, and not the integer the schema rules",
            float_field, use_rich=True)

    def bool_field(a):
        o = json.loads(a[exp_i]); (o.setdefault("data", {}))["custody_state"] = False
        a[exp_i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
    control("`custody_state` is JSON `false` — `bool` subclasses `int`, so only a type-identity test rejects it",
            bool_field, use_rich=True)

    def bad_cause(a):
        a.insert(exp_i, json.dumps(
            {"type": "script_emit", "node": json.loads(RA[exp_i])["node"],
             "time_ms": json.loads(RA[exp_i])["time_ms"], "emit_type": RELEASED_EVENT,
             "data": dict(json.loads(RA[exp_i])["data"], cause=99, target_kind=0)},
            separators=(",", ":")))
    control("a release carries a `cause` outside the seven-value authority", bad_cause, use_rich=True)

    # ---- the movement licence, both directions ------------------------------------------------------------
    def third_mover(a):
        a.insert(keep_i, json.dumps(
            {"type": "script_emit", "node": 1, "time_ms": 1, "emit_type": "rt_update", "data": {"dest": 9}},
            separators=(",", ":")))
    control("a THIRD stream-shaped movement with no permitted transformation behind it", third_mover)

    fired = sum(1 for r in results if r)
    total = len(results)                      # ⛔ DERIVED, never written down in prose
    out.append("")
    if fired != total:
        out.append(f"SELFTEST FAIL — {total - fired} of {total} control(s) did not fire. "
                   f"The checks above are NOT evidence.")
        return 1
    out.append(f"SELFTEST PASS — {fired}/{total} controls RED (count derived). "
               f"The result above is a measurement.")
    return 0


def main(argv) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    unknown = flags - {"--selftest"}
    if unknown or len(args) != 2:
        print(__doc__)
        print(f"REFUSED: unexpected argument(s) {sorted(unknown) or args}")
        return 2
    before_dir, after_dir = Path(args[0]), Path(args[1])
    out: list = []
    try:
        bad = run(before_dir, after_dir, out)
    except Refusal as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        return 2
    print("\n".join(out))
    if "--selftest" in flags:
        print("\nSELFTEST — every control must be RED:")
        sout: list = []
        try:
            sbad = run_selftest(before_dir, after_dir, sout)
        except Refusal as exc:
            print(f"REFUSED: {exc}", file=sys.stderr)
            return 2
        print("\n".join(sout))
        if sbad:
            return 1
    if bad:
        print(f"\nFAIL — {bad} finding(s).")
        return 1
    print("\nPASS — the ONLY corpus delta is §B278 S1b's three permitted correlation-lifecycle "
          "transformations, and the ordered residue is byte-identical.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
