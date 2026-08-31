#!/usr/bin/env python3
# MeshRoute — tools/compare_corpus_slice_g.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§CUSTODY-G corpus accountant — the receiver's permitted delta, and NOTHING else.

WHAT SLICE G IS ALLOWED TO CHANGE, verbatim from the brief's amendment 2:

    "The permitted delta is exactly the addressed-0x81 outcome representation:
     `unsupported_internal{0x81}` disappears · `custody_failure` pushes appear · ALL DATA/RTS/CTS/ACK
     traffic, routes, deliveries, duplicates and airtime IDENTICAL. Any delivery movement is a STOP,
     not an owner-attribution candidate — unlike F, G adds no legitimate contention."

Slice G is receiver-LOCAL: it validates, stores and reports a frame that had ALREADY been received, ACKed and
then dropped at Slice B's fail-closed tail guard. It originates no frame, changes no route, and spends no
airtime. That makes this comparator's job different from Slice F's, and STRICTER:

  · Slice F ADDED TRAFFIC, so its accountant had to ATTRIBUTE movement (bind each notice to a terminal).
  · Slice G ADDS NO TRAFFIC, so this accountant has to prove movement is ABSENT everywhere else.

⛔⛔ THE RESIDUE COMPARISON IS **EXACT, WHOLE-LINE AND ORDERED** — the Slice-A/E strong form this arc already
    owns. ⚠ AN EARLIER CUT OF THIS FILE REDUCED EACH STREAM TO AN EVENT-TYPE HISTOGRAM AND WAS REJECTED AT
    QG REVIEW, correctly: a histogram is blind to a changed field, a changed timestamp, a reordering, a
    changed non-`script_emit` line, a custody push carrying the wrong `{dst, ctr}`, and a missing `seq`
    (which compared equal to zero). Counting the right things is not the same as proving nothing else moved.

  ⇒ THE DISCIPLINE IS: **AFTER == BEFORE, MINUS AND PLUS WHOLE PERMITTED LINES.** Strike the explicitly
    permitted lines from each side and every remaining line must be byte-identical, in the same order, in
    the same position of the stream. Nothing is parsed, normalised or summarised on that path — a raw
    string comparison is the only form that cannot be blind to a field somebody forgot to name.

THE PERMITTED LINES, and they are the ONLY three shapes:
    BEFORE-only   `unsupported_internal` whose `type` is 0x81   (the guard drop G replaces)
    AFTER-only    `custody_failure_rx`                          (the validated receipt)
    AFTER-only    `push` whose `kind` is `custody_failure`      (its live diagnostic)
    (`custody_failure_reject` is classified as a permitted AFTER shape ONLY so that check C6 owns its
     verdict with a named message instead of it surfacing as an anonymous residue mismatch. Its required
     count is ZERO.)

THE CHECKS
  C1  THE EXACT ORDERED RESIDUE. Every non-permitted line is byte-identical and in the same order. This
      subsumes and replaces the old histogram: `delivered`, `rx`, `tx`, `rts_tx`, `cts_rx`, `data_rx`,
      `ack_tx`, `collision`, `rt_update`, every timestamp, every field of every event, and every
      non-`script_emit` line are all covered because NOTHING is excluded from it.
  C1b The set of streams whose bytes changed is EXACTLY the set that carried an addressed 0x81, both ways.
  C2  THE BIND, per occurrence: every AFTER `custody_failure_rx` consumes a BEFORE `unsupported_internal`
      for 0x81 at the SAME (node, time_ms), and every such BEFORE line is consumed exactly once. ⛔ A count
      comparison would pass on a receiver that consumed a DIFFERENT frame at a different node.
  C3  No 0x81 guard drop survives — AND every guard drop of any OTHER type is untouched (the proof Slice B's
      guard was not weakened). ⓘ The second half is also carried by C1; it is asserted separately so the
      failure has its own sentence.
  C4  IDENTITY-BOUND PAIRING: each `custody_failure_rx` pairs with exactly one `push{custody_failure}` on
      **{node, time_ms, dst, ctr}** — not on counts. A push naming a different failed flight is the §15.2
      correlation-pair defect and must not pass.
  C5  Every `custody_failure_rx` carries an EXPLICIT integer `seq == 0`, tested as `type(seq) is int and
      seq == 0`. ⛔ A MISSING `seq` FAILS (`None` is not zero), and so do JSON `false` and `0.0`: in Python
      `False == 0` and `0.0 == 0` are both TRUE, and `isinstance(False, int)` is true as well because `bool`
      subclasses `int` — so the type IDENTITY is the only form that rejects them. ⚠ The bare-equality cut of
      this check was rejected at QG review, which reproduced both shapes passing with zero findings. The
      whole point of the field is that `0` means exactly one thing — storage disabled (§7.3; [[B134]]:
      `Inbox::on_init` has one production caller, `src/fw_main.cpp`, which the simulator does not compile).
  C6  Zero `custody_failure_reject`.

USAGE
    python3 tools/compare_corpus_slice_g.py <before-run-dir> <after-run-dir>
    python3 tools/compare_corpus_slice_g.py <before> <after> --selftest

`--selftest` is the §18.0.3 obligation and is not optional in a report: a green result over an unchanged
corpus is evidence ONLY if a doctored view makes each check fail. ⓘ THE CONTROL COUNT IS **DERIVED** from
the results list and never written down in prose — an earlier revision of this docstring said "eight" while
nine ran, which is the same class of stale figure the checks themselves exist to catch.
"""

from __future__ import annotations

import collections
import hashlib
import json
import sys
from pathlib import Path

CUSTODY_TYPE = 0x81                     # DATA_TYPE_CUSTODY_FAILURE (lib/core/frame_codec.h)
RX_EVENT     = "custody_failure_rx"
REJ_EVENT    = "custody_failure_reject"
GUARD_EVENT  = "unsupported_internal"
PUSH_KIND    = "custody_failure"

# Line classes. `keep` is everything the residue comparison must see; the other four are the permitted
# shapes, and NOTHING else may ever be added to this list without a ruling.
KEEP, GUARD_0X81, RX, PUSH, REJECT = "keep", "guard0x81", "rx", "push", "reject"


def classify(line: str):
    """(class, parsed-or-None) for one raw NDJSON line. ⛔ `KEEP` lines are never parsed — they are compared
    as RAW STRINGS, so a field this function does not know about is still compared."""
    if RX_EVENT not in line and GUARD_EVENT not in line and PUSH_KIND not in line:
        return KEEP, None                                   # fast path: the overwhelming majority
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return KEEP, None
    if obj.get("type") != "script_emit":
        return KEEP, None
    ev   = obj.get("emit_type")
    data = obj.get("data") or {}
    if ev == GUARD_EVENT:
        return (GUARD_0X81, obj) if data.get("type") == CUSTODY_TYPE else (KEEP, None)
    if ev == RX_EVENT:
        return RX, obj
    if ev == REJ_EVENT:
        return REJECT, obj
    if ev == "push" and data.get("kind") == PUSH_KIND:
        return PUSH, obj
    return KEEP, None


def ident(obj):
    d = obj.get("data") or {}
    return (obj.get("node"), obj.get("time_ms"), d.get("dst"), d.get("ctr"))


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


# ---------------------------------------------------------------------------- the comparison

def compare_streams(name, before_lines, after_lines, b_md5, a_md5, out):
    """Pure over two ITERABLES of raw lines, so the selftest can drive every control against a doctored view.

    Streams both sides in lockstep, skipping permitted lines and comparing every other line EXACTLY. Memory
    is O(permitted lines), not O(stream) — `s17_metro` alone is 1.18 M events.
    """
    bad = 0
    b_guard, a_rx, a_push, a_reject = [], [], [], []

    def step(it, sink_map):
        """Advance to the next KEEP line, routing permitted lines into their sinks. None at end of stream."""
        for raw in it:
            cls, obj = classify(raw)
            if cls is KEEP:
                return raw
            sink = sink_map.get(cls)
            if sink is None:
                # A permitted-for-the-OTHER-side shape appeared on this side. Not strikable here, so it must
                # face the residue comparison — which is exactly the honest verdict.
                return raw
            sink.append(obj)
        return None

    b_sinks = {GUARD_0X81: b_guard}
    a_sinks = {RX: a_rx, PUSH: a_push, REJECT: a_reject}
    bi, ai = iter(before_lines), iter(after_lines)
    idx = 0
    residue_bad = 0
    while True:
        b = step(bi, b_sinks)
        a = step(ai, a_sinks)
        if b is None and a is None:
            break
        if b != a:
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
        out.append(f"  C1  FAIL {name}: {residue_bad} non-permitted line(s) moved  <- STOP (amendment 2)")
        bad += 1

    # C1b — did the bytes move at all, and was it licensed?
    changed, licensed = (b_md5 != a_md5), bool(b_guard)
    if changed and not licensed:
        out.append(f"  C1b FAIL {name}: stream changed ({b_md5}->{a_md5}) but carried NO addressed 0x81")
        bad += 1
    if licensed and not changed:
        out.append(f"  C1b FAIL {name}: carried {len(b_guard)} addressed 0x81 and did NOT change")
        bad += 1

    # C2 — the per-occurrence bind, on (node, time_ms).
    pool = collections.Counter((o.get("node"), o.get("time_ms")) for o in b_guard)
    unbound = []
    for o in a_rx:
        k = (o.get("node"), o.get("time_ms"))
        if pool[k] > 0:
            pool[k] -= 1
        else:
            unbound.append(k)
    if unbound:
        out.append(f"  C2  FAIL {name}: {len(unbound)} custody_failure_rx bind to NO prior guard drop {unbound[:4]}")
        bad += 1
    if sum(pool.values()):
        left = [k for k, v in pool.items() if v > 0]
        out.append(f"  C2  FAIL {name}: {sum(pool.values())} guard drops of 0x81 were NOT replaced {left[:4]}")
        bad += 1

    # C3 — no 0x81 guard drop survives, and no OTHER type's guard drop moved.
    # ⓘ BOTH HALVES ARE OWNED BY C1's EXACT RESIDUE, and that is a stronger guarantee than a count:
    #   · a surviving `unsupported_internal{0x81}` in the AFTER stream is classified GUARD_0X81, which has no
    #     AFTER sink — `step` RETURNS it into the residue comparison, where it has no BEFORE partner at that
    #     position, so C1 fails and NAMES the offending line verbatim;
    #   · a guard drop of any OTHER type is a plain `keep` line, so any change to one — appearing,
    #     disappearing, moving, or a single field differing — is a residue mismatch.
    #   ⇒ there is deliberately NO separate counting pass here. A second walk of a 1.18 M-line stream would
    #     buy a differently-worded message and nothing else, and the selftest carries a named control for
    #     each half so the coverage is demonstrated rather than asserted.

    # C4 — identity-bound pairing on {node, time_ms, dst, ctr}.
    rx_ids   = collections.Counter(ident(o) for o in a_rx)
    push_ids = collections.Counter(ident(o) for o in a_push)
    if rx_ids != push_ids:
        only_rx   = [k for k in rx_ids   if rx_ids[k]   > push_ids.get(k, 0)]
        only_push = [k for k in push_ids if push_ids[k] > rx_ids.get(k, 0)]
        out.append(f"  C4  FAIL {name}: rx/push do not pair on (node,time,dst,ctr) — "
                   f"{len(a_rx)} rx vs {len(a_push)} push; rx-only {only_rx[:3]} push-only {only_push[:3]}")
        bad += 1

    # C5 — an EXPLICIT integer seq == 0. A missing field is a failure, not a zero.
    #
    # ⛔⛔ THE TYPE TEST IS `type(seq) is int`, ⛔ **NOT** `isinstance(seq, int)` AND ⛔ NOT a bare `seq == 0`,
    #     and all three of those are different checks in Python:
    #       · `seq == 0`               accepts JSON `false` (`False == 0`) AND `0.0` (`0.0 == 0`);
    #       · `isinstance(seq, int)`   still accepts `False`, because `bool` SUBCLASSES `int`;
    #       · `type(seq) is int`       is the only form that rejects both.
    #     ⚠ AN EARLIER CUT OF THIS CHECK USED THE BARE EQUALITY AND WAS REJECTED AT QG REVIEW, which
    #     reproduced a doctored stream carrying `"seq": false` and one carrying `"seq": 0.0` — both passed
    #     with ZERO findings. A `seq` that is a boolean or a float is not the field §7.3 rules on; it is a
    #     wire-shape defect, and "0 means storage disabled" is only a guarantee if the 0 is an integer 0.
    #     Both shapes now have their own control in the selftest.
    missing = [ident(o) for o in a_rx if "seq" not in (o.get("data") or {})]
    if missing:
        out.append(f"  C5  FAIL {name}: {len(missing)} custody_failure_rx carry NO `seq` field {missing[:3]}"
                   f"  (a MISSING seq is not a zero)")
        bad += 1
    bad_seq = []
    for o in a_rx:
        data = o.get("data") or {}
        if "seq" not in data:
            continue                                    # already reported above, with its own message
        seq = data.get("seq")
        valid = type(seq) is int and seq == 0           # noqa: E721 — the type identity IS the check
        if not valid:
            bad_seq.append(f"{seq!r} ({type(seq).__name__})")
    if bad_seq:
        out.append(f"  C5  FAIL {name}: {len(bad_seq)} custody event(s) carry a `seq` that is not the "
                   f"INTEGER 0 {bad_seq[:4]}")
        bad += 1

    # C6 — no malformed-input rejections anywhere.
    if a_reject:
        out.append(f"  C6  FAIL {name}: {len(a_reject)} custody_failure_reject events")
        bad += 1

    return bad, len(a_rx), len(b_guard)


# ---------------------------------------------------------------------------- driver

def run(before_dir: Path, after_dir: Path, out) -> int:
    b_streams = sorted((before_dir / "streams").glob("*.ndjson"))
    if not b_streams:
        out.append(f"REFUSED: no streams under {before_dir}/streams")
        return 1
    bad, total_rx, moved = 0, 0, []
    for bp in b_streams:
        ap = after_dir / "streams" / bp.name
        if not ap.exists():
            out.append(f"  FAIL {bp.stem}: no AFTER stream")
            bad += 1
            continue
        b_md5, a_md5 = md5_of(bp), md5_of(ap)
        n, rx, guards = compare_streams(bp.stem, read_lines(bp), read_lines(ap), b_md5, a_md5, out)
        bad += n
        total_rx += rx
        if b_md5 != a_md5:
            moved.append((bp.stem, b_md5, a_md5, guards))
    out.append("")
    out.append(f"  streams: {len(b_streams)} · moved: {len(moved)} · custody receipts: {total_rx}")
    for name, b, a, n in moved:
        out.append(f"    {name:44s} {b} -> {a}   ({n} addressed 0x81 consumed)")
    return bad


# ---------------------------------------------------------------------------- selftest

def run_selftest(before_dir: Path, after_dir: Path, out) -> int:
    """Every control doctors ONE thing and must go RED. The count is DERIVED from the results list."""
    # Pick the SMALLEST stream that actually moved, so the controls act on a real population in memory.
    cand = None
    for bp in sorted((before_dir / "streams").glob("*.ndjson")):
        ap = after_dir / "streams" / bp.name
        if not ap.exists():
            continue
        guards = sum(1 for ln in read_lines(bp) if classify(ln)[0] is GUARD_0X81)
        if guards and (cand is None or bp.stat().st_size < cand[0].stat().st_size):
            cand = (bp, ap)
    if cand is None:
        out.append("SELFTEST REFUSED: no stream carries an addressed 0x81 — the controls would be vacuous")
        return 1
    bp, ap = cand
    B = list(read_lines(bp))
    A = list(read_lines(ap))
    name = bp.stem
    # Sanity: the undoctored pair must be GREEN, or every control below would be RED for free.
    base_msgs: list = []
    base_bad, _, _ = compare_streams(name, B, A, "aaaaaaaa", "bbbbbbbb", base_msgs)
    if base_bad:
        out.append(f"SELFTEST REFUSED: the undoctored {name} is already RED — controls would be meaningless")
        out.extend("    " + m for m in base_msgs[:4])
        return 1

    results = []

    def control(label, mutate_after, mutate_before=None):
        b = list(B)
        a = list(A)
        if mutate_before:
            mutate_before(b)
        mutate_after(a)
        msgs: list = []
        n, _, _ = compare_streams(name, b, a, "aaaaaaaa", "bbbbbbbb", msgs)
        fired = n > 0
        results.append(fired)
        out.append(f"  {'RED  (control fired)' if fired else 'GREEN — CONTROL DID NOT FIRE'}  {label}")
        if fired:
            out.append(f"        {msgs[0].strip()}")

    def first_index(lines, pred):
        for i, ln in enumerate(lines):
            if pred(ln):
                return i
        return -1

    rx_i   = first_index(A, lambda ln: classify(ln)[0] is RX)
    push_i = first_index(A, lambda ln: classify(ln)[0] is PUSH)
    guard_i_b = first_index(B, lambda ln: classify(ln)[0] is GUARD_0X81)
    # ★ THE DOCTORED LINE IS CHOSEN **AFTER** THE FIRST PERMITTED LINES ON BOTH SIDES, deliberately: a control
    #   that acts at stream position 0 never exercises the striking, so it could pass against a comparison
    #   that walked raw lines and ignored the permitted shapes entirely. Placing it downstream forces the
    #   walk to have skipped a guard drop in BEFORE and an rx+push in AFTER before it can even reach the
    #   divergence — which is the property the whole minus/plus discipline rests on.
    after_permitted = max(rx_i, push_i, first_index(A, lambda ln: classify(ln)[0] is GUARD_0X81)) + 1
    keep_i = first_index(A[after_permitted:], lambda ln: classify(ln)[0] is KEEP)
    if keep_i < 0:
        out.append("SELFTEST REFUSED: no ordinary line follows the permitted lines in the chosen stream")
        return 1
    keep_i += after_permitted

    # ---- THE FOUR CONTROLS THE QG REQUIRED, each aimed at a hole the histogram form had ----------------
    def corrupt_field(a):
        o = json.loads(a[keep_i]); d = o.get("data")
        if isinstance(d, dict) and d:
            k = sorted(d)[0]
            d[k] = (d[k] + 1) if isinstance(d[k], (int, float)) and not isinstance(d[k], bool) else "X"
        else:
            o["time_ms"] = (o.get("time_ms") or 0) + 1
        a[keep_i] = json.dumps(o, separators=(",", ":"))
    control("A FIELD of an ordinary event is corrupted — the histogram form was BLIND to this", corrupt_field)

    def reorder(a):
        j = first_index(a[keep_i + 1:], lambda ln: classify(ln)[0] is KEEP)
        j = keep_i + 1 + j
        a[keep_i], a[j] = a[j], a[keep_i]
    control("two ordinary events are REORDERED — same multiset, different stream", reorder)

    def wrong_push_identity(a):
        o = json.loads(a[push_i]); (o.setdefault("data", {}))["dst"] = 251
        a[push_i] = json.dumps(o, separators=(",", ":"))
    control("a custody push names a DIFFERENT failed flight (`dst` changed) — §15.2's pair broken",
            wrong_push_identity)

    def drop_seq(a):
        o = json.loads(a[rx_i]); (o.get("data") or {}).pop("seq", None)
        a[rx_i] = json.dumps(o, separators=(",", ":"))
    control("a custody receipt carries NO `seq` field — a MISSING seq is not a zero", drop_seq)

    # ---- the controls carried over from the first revision, each still required -------------------------
    control("one `delivered` event is ADDED — the amendment-2 STOP condition",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 1, "time_ms": 1, "emit_type": "delivered", "data": {}},
                separators=(",", ":"))))
    control("one ordinary event DISAPPEARS — traffic is not identical",
            lambda a: a.pop(keep_i))
    control("an `unsupported_internal{0x81}` SURVIVES the slice",
            lambda a: a.insert(keep_i, B[guard_i_b]))
    control("a guard drop of ANOTHER type moved — Slice B's guard was weakened",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 1, "time_ms": 1, "emit_type": "unsupported_internal",
                 "data": {"type": 0x87, "origin": 1, "dst": 2, "ctr": 3}}, separators=(",", ":"))))
    control("a `custody_failure_rx` that binds to NO prior guard drop (the wrong frame consumed)",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 99, "time_ms": 12345, "emit_type": RX_EVENT,
                 "data": {"reporter": 1, "dst": 2, "ctr": 3, "seq": 0}}, separators=(",", ":"))))
    control("a guard drop that was never replaced (a notice silently stopped being consumed)",
            lambda a: None,
            mutate_before=lambda b: b.insert(0, json.dumps(
                {"type": "script_emit", "node": 98, "time_ms": 4321, "emit_type": GUARD_EVENT,
                 "data": {"type": CUSTODY_TYPE, "origin": 1, "dst": 2, "ctr": 3}}, separators=(",", ":"))))
    control("a diagnostic with NO push — the app would never hear about it",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 98, "time_ms": 4321, "emit_type": RX_EVENT,
                 "data": {"reporter": 1, "dst": 2, "ctr": 3, "seq": 0}}, separators=(",", ":"))),
            mutate_before=lambda b: b.insert(0, json.dumps(
                {"type": "script_emit", "node": 98, "time_ms": 4321, "emit_type": GUARD_EVENT,
                 "data": {"type": CUSTODY_TYPE, "origin": 1, "dst": 2, "ctr": 3}}, separators=(",", ":"))))

    def nonzero_seq(a):
        o = json.loads(a[rx_i]); (o.setdefault("data", {}))["seq"] = 7
        a[rx_i] = json.dumps(o, separators=(",", ":"))
    control("a NONZERO seq in a simulator run that wires no inbox store", nonzero_seq)

    # ★★ THE TWO TYPE-STRICTNESS CONTROLS. Both of these PASSED against the bare `seq != 0` form — `False == 0`
    #    and `0.0 == 0` are both true in Python, and `isinstance(False, int)` is true as well because `bool`
    #    subclasses `int`. Only `type(seq) is int` rejects them, and only these controls prove it does.
    def seq_false(a):
        o = json.loads(a[rx_i]); (o.setdefault("data", {}))["seq"] = False
        a[rx_i] = json.dumps(o, separators=(",", ":"))
    control("`seq` is JSON `false` — equal to 0 in Python, and NOT the integer 0 (`bool` subclasses `int`, "
            "so even `isinstance` would let it through)", seq_false)

    def seq_float(a):
        o = json.loads(a[rx_i]); (o.setdefault("data", {}))["seq"] = 0.0
        a[rx_i] = json.dumps(o, separators=(",", ":"))
    control("`seq` is the FLOAT `0.0` — equal to 0 in Python, and NOT the integer 0", seq_float)

    control("a malformed-input rejection appears",
            lambda a: a.insert(keep_i, json.dumps(
                {"type": "script_emit", "node": 1, "time_ms": 1, "emit_type": REJ_EVENT,
                 "data": {"type": CUSTODY_TYPE, "origin": 1, "dst": 2, "ctr": 3}}, separators=(",", ":"))))

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
    args  = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    unknown = flags - {"--selftest"}
    if unknown or len(args) != 2:
        print(__doc__)
        print(f"REFUSED: unexpected argument(s) {sorted(unknown) or args}")
        return 2
    before_dir, after_dir = Path(args[0]), Path(args[1])
    out: list = []
    bad = run(before_dir, after_dir, out)
    print("\n".join(out))
    if "--selftest" in flags:
        print("\nSELFTEST — every control must be RED:")
        sout: list = []
        sbad = run_selftest(before_dir, after_dir, sout)
        print("\n".join(sout))
        if sbad:
            return 1
    if bad:
        print(f"\nFAIL — {bad} finding(s).")
        return 1
    print("\nPASS — the ONLY corpus delta is the addressed-0x81 outcome representation, and the ordered "
          "residue is byte-identical.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
