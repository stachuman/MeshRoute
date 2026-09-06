#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Single-match header mutator — control support for `tools/probe_features/run.sh`.

★ WHY A HELPER INSTEAD OF `sed`. Every control in this probe must inject EXACTLY ONE change, verified. The
  production derivation deliberately spells the same line twice (`#  define MR_FEAT_RADMIN_CLIENT 1` appears in
  both the mobile arm and the host arm), so a bare `sed s|...|...|` would silently hit the wrong one, or both, and
  a control that mutated the wrong line is a control that measured something nobody declared. This helper makes
  the occurrence count an ASSERTION: `--expect N` occurrences must be found, and occurrence `--nth` is replaced.

⛔ IT NEVER WRITES INTO THE REPOSITORY. `--in` is read, `--out` is written; run.sh always points `--out` at a
   temporary shadow include directory. The runner re-hashes the real sources before and after the whole run.

USAGE:
  mutate.py --in H --out H' --find LINE --replace LINE [--expect 1] [--nth 1]
  mutate.py --in H --out H' --delete-block START END          # drop a whole `#  if`/`#  endif` guard, verbatim
Exit 0 on success (prints the changed line numbers); 1 with a diagnostic if the match count is not `--expect`.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def fail(msg: str) -> int:
    print(f"MUTATE-ERROR {msg}", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", required=True)
    ap.add_argument("--out", dest="dst", required=True)
    ap.add_argument("--find")
    ap.add_argument("--replace")
    ap.add_argument("--expect", type=int, default=1)
    ap.add_argument("--nth", type=int, default=1)
    ap.add_argument("--delete-block", nargs=2, metavar=("START", "END"))
    args = ap.parse_args()

    lines = Path(args.src).read_text(encoding="utf-8").splitlines(keepends=True)

    if args.delete_block:
        start, end = args.delete_block
        starts = [i for i, ln in enumerate(lines) if ln.rstrip("\n") == start]
        if len(starts) != args.expect:
            return fail(f"--delete-block START matched {len(starts)} line(s), expected {args.expect}: {start!r}")
        i = starts[args.nth - 1]
        j = next((k for k in range(i + 1, len(lines)) if lines[k].rstrip("\n") == end), None)
        if j is None:
            return fail(f"--delete-block END never found after line {i + 1}: {end!r}")
        out = lines[:i] + lines[j + 1:]
        print(f"deleted lines {i + 1}..{j + 1}")
    else:
        if args.find is None or args.replace is None:
            return fail("--find and --replace are both required unless --delete-block is used")
        hits = [i for i, ln in enumerate(lines) if ln.rstrip("\n") == args.find]
        if len(hits) != args.expect:
            return fail(f"--find matched {len(hits)} line(s), expected {args.expect}: {args.find!r}")
        i = hits[args.nth - 1]
        out = list(lines)
        out[i] = args.replace + "\n"
        print(f"replaced line {i + 1}")

    if out == lines:
        return fail("the mutation changed NOTHING (vacuous)")
    Path(args.dst).write_text("".join(out), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
