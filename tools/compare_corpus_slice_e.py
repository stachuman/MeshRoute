#!/usr/bin/env python3
# MeshRoute — compare_corpus_slice_e.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""PREDICTED-DELTA gate for §CUSTODY-E ([[B263]]), where exactly ONE event class may disappear.

⛔⛔ A SEPARATE INSTRUMENT, AND THE TWO EXISTING ONES ARE DELIBERATELY LEFT ALONE.
    `tools/compare_corpus_semantics.py` proves "hashes may move; SEMANTICS may move nowhere" and
    `tools/compare_corpus_slice_b.py` proves Slice B's *multiset* claim on a set of re-timed streams. Slice E's
    claim is STRICTLY STRONGER than either, so it gets its own controls rather than a mode on someone else's.

THE CLAIM THIS GATE PROVES — "PURE DELETION OF ONE EVENT CLASS"
    For every one of the 36 streams, the AFTER stream is the BEFORE stream with ZERO OR MORE WHOLE LINES
    REMOVED, and every removed line is an `emit_type:"push"` event whose `data.kind` is `send_failed`.
    Equivalently and mechanically: delete the removed lines from BEFORE and the two files are BYTE-IDENTICAL.

    ⇒ D1  no line is ADDED (`+` in the diff) — anywhere, in any stream;
       D2  no line is MODIFIED (a `-`/`+` pair at the same place) — the pure-deletion form makes this the same
           check as D1, which is exactly why the strong form was chosen over a per-kind multiset;
       D3  every DELETED line is a `push` whose `kind` is `send_failed` — no other push kind, and no
           telemetry emit of any kind, may disappear;
       D4  the surviving lines keep their ORDER and their BYTES (implied by D1+D2, asserted explicitly);
       D5  the per-kind census of everything OTHER than `push{send_failed}` is identical in both arms.

WHY THE STRONG FORM IS AVAILABLE HERE AND WAS NOT IN SLICE B. Slice B changed an ADMISSION FLOOR, so a moved
stream re-times everything downstream of the first change and an ordered walk is meaningless. Slice E suppresses
an APP-RING PUSH: `push_send_failed` only calls `enqueue_push`, which feeds no routing, timing, admission or
airtime decision. Nothing downstream can move — so the honest check is the strictest one, and anything weaker
would let a real re-timing hide inside a "predicted mover".

★ THE ONE WAY THE CLAIM COULD FAIL, NAMED IN ADVANCE (it is why D1 is checked at all rather than assumed):
  `enqueue_push` is a DROP-OLDEST ring of `cap_push_ring`. If a node's ring ever filled between two drains,
  suppressing one push would change WHICH earlier push is evicted — and a push would APPEAR. D1 is that guard.

USAGE
    python3 tools/compare_corpus_slice_e.py BEFORE_DIR AFTER_DIR
    python3 tools/compare_corpus_slice_e.py BEFORE_DIR AFTER_DIR --selftest   # prove every arm can FAIL
"""

import argparse
import json
import os
import sys
from collections import Counter

SUPPRESSED_EMIT = "push"
SUPPRESSED_KIND = "send_failed"


def load_manifest(d):
    with open(os.path.join(d, "manifest.json")) as f:
        return json.load(f)


def stream_names(d):
    sd = os.path.join(d, "streams")
    return sorted(n[: -len(".ndjson")] for n in os.listdir(sd) if n.endswith(".ndjson"))


def read_lines(d, name):
    with open(os.path.join(d, "streams", name + ".ndjson")) as f:
        return f.read().splitlines()


def is_suppressible(line):
    """True iff `line` is exactly the ONE event class this slice may remove."""
    if '"emit_type":"' + SUPPRESSED_EMIT + '"' not in line:
        return False
    try:
        rec = json.loads(line)
    except ValueError:
        return False
    return (rec.get("emit_type") == SUPPRESSED_EMIT
            and isinstance(rec.get("data"), dict)
            and rec["data"].get("kind") == SUPPRESSED_KIND)


def kind_of(line):
    """A coarse event identity for the D5 census: the emit_type, refined by push kind."""
    try:
        rec = json.loads(line)
    except ValueError:
        return "?unparseable"
    et = rec.get("emit_type") or rec.get("type") or "?"
    d = rec.get("data")
    if et == SUPPRESSED_EMIT and isinstance(d, dict):
        return "push:" + str(d.get("kind"))
    return str(et)


def compare_stream(before, after):
    """Return (ok, removed_lines, failures[]) for one stream.

    The walk is a two-cursor subsequence match: AFTER must be BEFORE minus whole lines. Any AFTER line that is
    not the next surviving BEFORE line is an ADDED or MODIFIED line, which fails immediately.
    """
    removed, failures = [], []
    i = j = 0
    while i < len(before) and j < len(after):
        if before[i] == after[j]:
            i += 1
            j += 1
            continue
        # BEFORE[i] did not survive: it may only be the suppressible class.
        if not is_suppressible(before[i]):
            failures.append(f"line {i + 1} of BEFORE changed or vanished and is NOT a "
                            f"{SUPPRESSED_EMIT}{{{SUPPRESSED_KIND}}}: {before[i][:160]}")
            return False, removed, failures
        removed.append(before[i])
        i += 1
    # Tail: whatever BEFORE has left must also be suppressible; AFTER must have nothing left (D1).
    while i < len(before):
        if not is_suppressible(before[i]):
            failures.append(f"trailing BEFORE line {i + 1} vanished and is not suppressible: {before[i][:160]}")
            return False, removed, failures
        removed.append(before[i])
        i += 1
    if j < len(after):
        failures.append(f"AFTER has {len(after) - j} line(s) BEFORE does not — a line was ADDED, e.g. "
                        f"{after[j][:160]}")
        return False, removed, failures
    return True, removed, failures


def census_ok(before, after, removed):
    """D5: everything other than the removed class has an identical per-kind census."""
    cb, ca = Counter(kind_of(l) for l in before), Counter(kind_of(l) for l in after)
    key = "push:" + SUPPRESSED_KIND
    cb[key] -= len(removed)
    if cb[key] == 0:
        del cb[key]
    if ca.get(key) == 0:
        del ca[key]
    return cb == ca, cb, ca


def run(before_dir, after_dir, quiet=False):
    for d in (before_dir, after_dir):
        m = load_manifest(d)
        # ⛔ LINK ④ OF `run_corpus.py`'S AUTHORITY CHAIN, APPLIED HERE TOO: BOTH sides are validated before a
        #    single field is compared. Two identically FAILED runs comparing equal and printing PASS is the
        #    worst false-green shape there is, and it is the defect that file's own header records.
        rows = m.get("scenarios")
        if not isinstance(rows, list) or not rows:
            print(f"REFUSED: {d}/manifest.json has no scenario records")
            return 1
        bad = [r for r in rows if r.get("exit_status") != 0 or r.get("assertion_failures")]
        if bad:
            print(f"REFUSED: {d} is not a complete, failure-free run ({len(bad)} bad record(s))")
            return 1
    nb, na = stream_names(before_dir), stream_names(after_dir)
    if nb != na:
        print(f"REFUSED: stream sets differ ({len(nb)} vs {len(na)})")
        return 1

    total_removed, movers, failures = 0, [], []
    for name in nb:
        b, a = read_lines(before_dir, name), read_lines(after_dir, name)
        ok, removed, f = compare_stream(b, a)
        if not ok:
            failures.append((name, f))
            continue
        cok, cb, ca = census_ok(b, a, removed)
        if not cok:
            diff = {k: (cb.get(k, 0), ca.get(k, 0)) for k in set(cb) | set(ca) if cb.get(k) != ca.get(k)}
            failures.append((name, [f"per-kind census moved outside the permitted class: {diff}"]))
            continue
        if removed:
            movers.append((name, len(removed)))
            total_removed += len(removed)
        if not quiet:
            tag = f"-{len(removed):<4d} push{{{SUPPRESSED_KIND}}}" if removed else "byte-identical"
            print(f"  {name:<46s} {tag}")

    if not quiet:
        print()
        print(f"  streams          : {len(nb)}")
        print(f"  byte-identical   : {len(nb) - len(movers)}")
        print(f"  pure-deletion    : {len(movers)}  ({total_removed} {SUPPRESSED_EMIT}{{{SUPPRESSED_KIND}}} events removed)")
        for name, n in sorted(movers, key=lambda x: -x[1]):
            print(f"      {name:<44s} -{n}")
    if failures:
        print("\nFAIL — the permitted delta was exceeded:")
        for name, f in failures:
            for line in f:
                print(f"  {name}: {line}")
        return 1
    print("\nPASS: every stream is BEFORE minus zero or more whole "
          f"{SUPPRESSED_EMIT}{{{SUPPRESSED_KIND}}} lines; nothing added, nothing modified, "
          "no other event class moved.")
    return 0


# ---------------------------------------------------------------------------------------------------------
# ★★ THE CONTROLS. A comparator with no proof it can FAIL is a comparator that reports PASS. Each arm below
#    perturbs one property and REQUIRES a failure; the harness itself fails if any perturbation passes.
# ---------------------------------------------------------------------------------------------------------
def selftest():
    ok_line = ('{"type":"script_emit","node":1,"time_ms":10,"emit_type":"push",'
               '"data":{"ctr":1,"dst":2,"kind":"send_failed"}}')
    other_push = ('{"type":"script_emit","node":1,"time_ms":11,"emit_type":"push",'
                  '"data":{"ctr":1,"dst":2,"kind":"send_acked"}}')
    telemetry = ('{"type":"script_emit","node":1,"time_ms":12,"emit_type":"rts_giveup",'
                 '"data":{"dst":2,"ctr":1}}')
    base = [telemetry, ok_line, other_push, telemetry]

    cases = [
        ("a suppressed push is accepted",              base, [telemetry, other_push, telemetry],   True),
        ("nothing changed is accepted",                base, list(base),                            True),
        ("a DELETED telemetry emit is REFUSED",        base, [ok_line, other_push, telemetry],      False),
        ("a DELETED non-failed push is REFUSED",       base, [telemetry, ok_line, telemetry],       False),
        ("an ADDED line is REFUSED",                   base, base + [telemetry],                    False),
        ("a MODIFIED line is REFUSED",                 base,
         [telemetry, ok_line, other_push.replace('"dst":2', '"dst":9'), telemetry],                 False),
        ("a REORDERED pair is REFUSED",                base, [ok_line, telemetry, other_push, telemetry], False),
    ]
    bad = 0
    for label, b, a, want in cases:
        got, removed, _ = compare_stream(b, a)
        if got and want:
            cok, _, _ = census_ok(b, a, removed)
            got = cok
        status = "ok  " if got == want else "FAIL"
        if got != want:
            bad += 1
        print(f"  {status} {label:<44s} expected={'accept' if want else 'refuse'} got={'accept' if got else 'refuse'}")
    print(f"\n{'PASS' if bad == 0 else 'FAIL'}: {len(cases) - bad}/{len(cases)} controls behaved as required")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before", nargs="?")
    ap.add_argument("after", nargs="?")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.before or not args.after:
        ap.error("BEFORE_DIR and AFTER_DIR are required (or use --selftest)")
    return run(args.before, args.after, args.quiet)


if __name__ == "__main__":
    sys.exit(main())
