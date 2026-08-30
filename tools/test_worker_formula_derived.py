#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§GATE-SPEED DRIFT PIN — every PRINTED worker-count formula must DERIVE from the constants.

★★ THE INCIDENT THIS EXISTS FOR, and it is the [[B217]] class: an instrument that STATES a rule it does not USE.
`tools/probe_ui_model_mutations.py` prints its default worker formula in two banners. When §GATE-SPEED raised the
cap 6 -> 8 (2026-08-30), the printed *value* followed (it interpolates `_WORKERS_DEFAULT`) while the printed
*formula* stayed `min(usable cores N - 2, 6)` in BOTH banners — a line that told the operator the tool caps at 6
while it capped at 8. A supervisor spot-check found ONE of the two; a grep found the other, which was the one on
the hot path (every real run prints it; `--where` prints the other).

⛔ SO THE PIN IS NOT "the literal says 8". A literal that says 8 is exactly what drifted, one cap-change later.
The pin is STRUCTURAL: outside the constants' own definitions and ONE allowlisted prose line, no formula-shaped
literal may exist in the file at all — every printed formula must come from `_workers_default_formula()`.

⚠ AND THE PROSE IS PINNED TOO, not exempted. The one human-readable comment that spells the formula at the
constant must state the SAME two numbers the constants hold; a comment that drifts from the code it annotates is
the `update-stale-comments` defect, and allowlisting it unconditionally would have re-created the hole one line up.

Run it standalone for the control ledger:   python3 tools/test_worker_formula_derived.py
It is also a `test_*.py`, so the existing gate already runs it:
    python3 -m unittest discover -s tools -p "test_*.py"
(a check nobody runs is not a check — that is why this is not a `check_*.py`).
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "tools" / "probe_ui_model_mutations.py"

# A "formula-shaped literal": the word `cores`, then a subtraction and a cap, BOTH written as bare digits.
#   matches ->  "min(usable cores {_usable_cores()} - 2, 6)"      the drifted banners
#   matches ->  "min(usable cores - 2, 8)"                        the prose comment (allowlisted, then verified)
#   MISSES  ->  "min(usable cores {_usable_cores()} - {_WORKERS_RESERVED_CORES}, {_WORKERS_CAP})"   derived: no digits
FORMULA_LITERAL = re.compile(r"cores\b.{0,48}?-\s*(\d+)\s*,\s*(\d+)")

CAP_RE = re.compile(r"^_WORKERS_CAP\s*=\s*(\d+)\s*$", re.MULTILINE)
RESERVE_RE = re.compile(r"^_WORKERS_RESERVED_CORES\s*=\s*(\d+)\s*$", re.MULTILINE)
# The ONE allowlisted prose statement of the formula, at the constant. Anchored on its own text so that a SECOND
# prose restatement elsewhere is a hit rather than a silent second source of truth.
PROSE_RE = re.compile(r"^# ★ THE DEFAULT IS `min\(usable cores - (\d+), (\d+)\)`", re.MULTILINE)


def constants(text: str) -> tuple[int, int]:
    cap = CAP_RE.findall(text)
    reserve = RESERVE_RE.findall(text)
    assert len(cap) == 1, f"expected exactly one _WORKERS_CAP definition, found {len(cap)}"
    assert len(reserve) == 1, f"expected exactly one _WORKERS_RESERVED_CORES definition, found {len(reserve)}"
    return int(reserve[0]), int(cap[0])


def prose_numbers(text: str) -> tuple[int, int]:
    found = PROSE_RE.findall(text)
    assert len(found) == 1, f"expected exactly one prose statement of the formula, found {len(found)}"
    return int(found[0][0]), int(found[0][1])


def undeclared_literals(text: str) -> list[tuple[int, str]]:
    """Every formula-shaped literal that is NOT the allowlisted prose line."""
    hits = []
    for number, line in enumerate(text.splitlines(), 1):
        if not FORMULA_LITERAL.search(line):
            continue
        if PROSE_RE.match(line):        # the one allowlisted statement; its digits are checked separately
            continue
        hits.append((number, line.strip()))
    return hits


# The two literals that actually drifted, verbatim, as the negative controls. Reintroducing either must be seen.
DRIFTED_RUN_BANNER = (
    "          f\"{'' if _WORKERS != _WORKERS_DEFAULT else f' (default = "
    "min(usable cores {_usable_cores()} - 2, 6))'}\""
)
DRIFTED_WHERE_BANNER = (
    "    print(f\"workers    {_WORKERS} (default {_WORKERS_DEFAULT} = "
    "min(usable cores {_usable_cores()} - 2, 6))\")"
)


class WorkerFormulaDerivedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.text = TARGET.read_text(encoding="utf-8")

    def test_no_undeclared_formula_literal_survives(self) -> None:
        hits = undeclared_literals(self.text)
        self.assertEqual(
            hits, [],
            "a printed worker-count formula is spelled with literals instead of deriving from _WORKERS_CAP / "
            f"_WORKERS_RESERVED_CORES: {hits}")

    def test_prose_at_the_constant_agrees_with_the_constants(self) -> None:
        self.assertEqual(prose_numbers(self.text), constants(self.text),
                         "the prose comment states a different reserve/cap than the constants hold")

    def test_both_banners_call_the_single_formatter(self) -> None:
        # Positive control: the derivation is actually USED. Without this, deleting both banners would pass.
        self.assertEqual(self.text.count("_workers_default_formula()"), 3,
                         "expected the formatter's definition plus exactly two banner call sites")

    # --- negative controls: the scanner must SEE each way this has drifted or could drift -----------------------
    def test_control_run_banner_literal_is_seen(self) -> None:
        sabotaged = self.text.replace(
            "f\"{'' if _WORKERS != _WORKERS_DEFAULT else f' (default = {_workers_default_formula()})'}\"",
            DRIFTED_RUN_BANNER.strip(), 1)
        self.assertNotEqual(sabotaged, self.text, "the run banner's anchor did not match")
        self.assertTrue(undeclared_literals(sabotaged), "the scanner is BLIND to the drifted run banner (:7474)")

    def test_control_where_banner_literal_is_seen(self) -> None:
        sabotaged = self.text.replace(
            "print(f\"workers    {_WORKERS} (default {_WORKERS_DEFAULT} = {_workers_default_formula()})\")",
            DRIFTED_WHERE_BANNER.strip(), 1)
        self.assertNotEqual(sabotaged, self.text, "the --where banner's anchor did not match")
        self.assertTrue(undeclared_literals(sabotaged), "the scanner is BLIND to the drifted --where banner")

    def test_control_prose_drift_is_seen(self) -> None:
        reserve, cap = constants(self.text)
        sabotaged = PROSE_RE.sub(
            f"# ★ THE DEFAULT IS `min(usable cores - {reserve}, {cap - 2})`", self.text, count=1)
        self.assertNotEqual(sabotaged, self.text, "the prose anchor did not match")
        self.assertNotEqual(prose_numbers(sabotaged), constants(sabotaged),
                            "the prose/constant agreement check is BLIND to a drifted comment")

    def test_control_a_new_literal_anywhere_is_seen(self) -> None:
        # The pin is not "these two lines"; it is the whole file. A THIRD banner would be caught too.
        sabotaged = self.text + '\nprint(f"workers (default = min(usable cores {n} - 2, 6))")\n'
        self.assertTrue(undeclared_literals(sabotaged), "the scanner only looks at the two known banners")


def _ledger() -> int:
    """The `check_*.py` control ledger, for a human running this file directly."""
    text = TARGET.read_text(encoding="utf-8")
    reserve, cap = constants(text)
    print(f"\ntarget   {TARGET.relative_to(ROOT)}")
    print(f"constants  _WORKERS_RESERVED_CORES={reserve}  _WORKERS_CAP={cap}  "
          f"prose={prose_numbers(text)}  formatter call sites={text.count('_workers_default_formula()') - 1}\n")
    result = unittest.TextTestRunner(verbosity=0, stream=open("/dev/null", "w")).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(WorkerFormulaDerivedTests))
    failed = {test.id().rsplit(".", 1)[-1] for test, _ in result.failures + result.errors}
    for name in sorted(t.id().rsplit(".", 1)[-1] for t in
                       unittest.defaultTestLoader.loadTestsFromTestCase(WorkerFormulaDerivedTests)):
        negative = name.startswith("test_control_")
        ok = name not in failed
        if negative:
            print(f"  {'RED   (control fired) ' if ok else '⛔ GREEN (control DEAD)'} {name}")
        else:
            print(f"  {'GREEN (as required)   ' if ok else '⛔ RED   (drift present)'} {name}")
    print()
    if failed:
        print(f"FAIL — {len(failed)} check(s) did not behave: {sorted(failed)}")
        return 1
    print("PASS — every printed worker-count formula derives from the constants, and the prose agrees with them.")
    return 0


if __name__ == "__main__":
    raise SystemExit(_ledger())
