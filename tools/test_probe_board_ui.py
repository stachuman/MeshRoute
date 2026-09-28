#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""[[B459]] regression for tools/probe_board_ui/run.sh — the board-UI probe's completeness accounting.

WHAT THIS COVERS. Every case drives the REAL runner, never a Python model of it:
  · `--selftest-accounting <dir> [--no-neg]` replays synthetic records through the runner's own recorder, record
    importer, outcome functions, guards and its ONE finalization (`board_final`, the shared comparator), compiling
    nothing — so completeness per namespace, harness errors and the outcome contracts are measured in milliseconds;
  · runner COPIES in a scratch tree (the product sources copied read-only, the real shared library LINKED — the
    explicit mechanism by which a copy resolves `tools/probe_accounting.sh`) prove the static census and, with a few
    real `--no-neg` or default runs, the omissions only an executed run can show.

THE DEFECT (measured by the B459 pre-check, 2026-09-28): the runner counted what ran and compared nothing with what
should have run — a false or undefined guard before W1, an early return, a shortened negctl loop or one deleted
canvas CHK each still exited 0.

⛔ LOAD-BEARING, NOT DECORATIVE: removing the ONE final accounting statement from a copy lets an omission — and the loss
   of a whole child layer's records — through, while the stock runner refuses both.
⛔ THE HONEST LIMIT, NOT TESTED AS A CATCH: a check removed TOGETHER with its manifest line cannot be detected by the
   census or the run; only the manifest diff shows it to a reviewer.

RUN:  python3 -m unittest discover -s tools -p "test_*.py"     (or: cd tools && python3 test_probe_board_ui.py)
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
PROBE_DIR = TOOLS / "probe_board_ui"
RUNNER = PROBE_DIR / "run.sh"
MANIFEST = PROBE_DIR / "expected.tsv"
LIBRARY = TOOLS / "probe_accounting.sh"
ACCOUNTING_STATEMENT = ("pa_compare \"$expected\" \"$OBS\" \"$GUARDS\" '[[B459]]' 'probe identity' 'its recorder' "
                        "'identities accounted' || rc=1")
BANNER = "PROBE-ONLY — NOT A GATE (--no-neg: canvas mutation controls skipped)"
NAMESPACES = ["canvas/v3", "canvas/v4", "trait", "missing", "struct", "wiring", "wiring/control", "negctl/v3", "negctl/v4"]


def manifest() -> list[tuple[str, str]]:
    rows = []
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            ident, outcome = line.split("\t")[:2]
            rows.append((ident, outcome))
    return rows


def namespace(ident: str) -> str:
    p = ident.split("/")
    if p[0] == "wiring":
        return "wiring/control" if "/control/" in ident else "wiring"
    if p[0] in ("canvas", "negctl"):
        return f"{p[0]}/{p[1]}"
    return p[0]


def once(text: str, old: str, new: str) -> str:
    """One exact edit, which must match exactly once — a drifted anchor fails the test instead of editing nothing."""
    assert text.count(old) == 1, f"anchor matched {text.count(old)} times: {old[:60]!r}"
    return text.replace(old, new)


def once_re(text: str, pattern: str, repl: str) -> str:
    new, n = re.subn(pattern, repl, text)
    assert n == 1, f"pattern matched {n} times: {pattern[:60]!r}"
    return new


def fail_lines(out: str) -> set[str]:
    """Every `  FAIL …` line a run printed — a real-run case must show EXACTLY its injected faults, nothing masked."""
    return {l for l in out.splitlines() if l.startswith("  FAIL ")}


def missing(*idents: str) -> set[str]:
    return {f"  FAIL [[B459]] MISSING verdict — the probe identity never reached its recorder: {i}" for i in idents}


def drop_entry(text: str, first: str, following: str) -> str:
    """Remove one list entry: from the line starting with `first` up to the line starting with `following`."""
    lines = text.splitlines(keepends=True)
    a = next(k for k, l in enumerate(lines) if l.startswith(first))
    b = next(k for k in range(a + 1, len(lines)) if lines[k].startswith(following))
    return "".join(lines[:a] + lines[b:])


def records(rows) -> list[tuple[str, ...]]:
    return [("record", i, o) for i, o in rows]


class Selftest(unittest.TestCase):
    """The synthetic mode: the real recorder, importer, outcome functions, guards and finalization."""

    def run_st(self, events, expected=None, no_neg=False, files=None, runner=RUNNER):
        expected = manifest() if expected is None else expected
        with tempfile.TemporaryDirectory(prefix="b459-st-") as d:
            dd = Path(d)
            (dd / "expected.tsv").write_text("".join(f"{i}\t{o}\n" for i, o in expected))
            (dd / "events").write_text("".join("\t".join(e) + "\n" for e in events))
            for name, text in (files or {}).items():
                (dd / name).write_text(text)
            argv = ["bash", str(runner), "--selftest-accounting", str(dd)] + (["--no-neg"] if no_neg else [])
            p = subprocess.run(argv, capture_output=True, text=True, timeout=120)
        return p.returncode, p.stdout + p.stderr

    def assertPasses(self, events, **kw):
        rc, out = self.run_st(events, **kw)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.splitlines()[-1], "PASS", out)
        return out

    def assertFails(self, events, *why, **kw):
        rc, out = self.run_st(events, **kw)
        self.assertEqual(rc, 1, out)
        self.assertEqual(out.splitlines()[-1], "FAIL", out)
        for w in why:
            self.assertIn(w, out)
        return out

    # ---- the manifest itself --------------------------------------------------------------------------------------
    def test_manifest_has_every_namespace_and_no_duplicate(self):
        rows = manifest()
        ids = [i for i, _ in rows]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({namespace(i) for i in ids}, set(NAMESPACES))

    # ---- completeness in every namespace ----------------------------------------------------------------------------
    def test_complete_set_passes(self):
        out = self.assertPasses(records(manifest()))
        self.assertIn(f"identities accounted: {len(manifest())} declared, {len(manifest())} exactly once", out)

    def test_empty_expected_set_fails(self):
        self.assertFails([], "no probe identity is declared", expected=[])

    def test_duplicate_declaration_fails(self):
        rows = manifest()
        self.assertFails(records(rows), "label DECLARED twice", expected=rows + [rows[3]])

    def test_missing_first_middle_last_in_every_namespace(self):
        rows = manifest()
        for ns in NAMESPACES:
            members = [i for i, (ident, _) in enumerate(rows) if namespace(ident) == ns]
            for pos in (members[0], members[len(members) // 2], members[-1]):
                with self.subTest(namespace=ns, identity=rows[pos][0]):
                    ev = records(rows[:pos] + rows[pos + 1:])
                    self.assertFails(ev, f"MISSING verdict — the probe identity never reached its recorder: {rows[pos][0]}")

    def test_unknown_duplicate_and_equal_count_substitution_in_every_namespace(self):
        rows = manifest()
        for ns in NAMESPACES:
            members = [i for i, (ident, _) in enumerate(rows) if namespace(ident) == ns]
            a, b = members[0], members[-1]
            with self.subTest(namespace=ns, case="unknown"):
                ghost = rows[a][0] + "-ghost"
                self.assertFails(records(rows) + [("record", ghost, rows[a][1])], f"UNKNOWN label — no such probe identity is declared: {ghost}")
            with self.subTest(namespace=ns, case="duplicate"):
                self.assertFails(records(rows) + [("record",) + rows[a]], f"DUPLICATE verdict (2) for: {rows[a][0]}")
            with self.subTest(namespace=ns, case="missing+duplicate at an unchanged total"):
                ev = records(rows); ev[b] = ev[a]
                self.assertEqual(len(ev), len(rows))
                self.assertFails(ev, f"DUPLICATE verdict (2) for: {rows[a][0]}", f"never reached its recorder: {rows[b][0]}")

    def test_one_P4a_iteration_missing_fails(self):
        rows = manifest()
        for arm in ("v3", "v4"):
            with self.subTest(arm=arm):
                gone = f"canvas/{arm}/P4a/5"
                self.assertIn(gone, dict(rows))
                self.assertFails(records([r for r in rows if r[0] != gone]), f"never reached its recorder: {gone}")

    # ---- harness errors ---------------------------------------------------------------------------------------------
    def test_false_guard_and_undefined_guard_leave_the_identity_missing(self):
        rows = manifest()
        for kind in ("guard_nonzero", "guard_127"):
            with self.subTest(guard=kind):
                ev = records(rows); ev[300] = (kind, rows[300][0])
                self.assertFails(ev, f"never reached its recorder: {rows[300][0]}")

    def test_recorded_guard_failure_fails_with_every_observation_present(self):
        self.assertFails(records(manifest()) + [("guard", "a synthetic guard that failed")],
                         "a probe identity guard failed: a synthetic guard that failed")

    def test_wiring_control_harness_errors_are_rejected(self):
        rows = manifest()
        ctl = next(i for i, (ident, _) in enumerate(rows) if ident.endswith("/control/1"))
        for sed_rc, differs, pred_rc, outcome in (("4", "0", "-", "sed_failed"), ("0", "1", "2", "harness_error"),
                                                  ("0", "1", "127", "harness_error"), ("0", "0", "-", "vacuous"),
                                                  ("0", "1", "0", "still_true")):
            with self.subTest(outcome=outcome, pred_exit=pred_rc):
                ev = records(rows); ev[ctl] = ("wiring_control", rows[ctl][0], sed_rc, differs, pred_rc)
                self.assertFails(ev, f"outcome {outcome} is not the accepted red for: {rows[ctl][0]}")
        ev = records(rows); ev[ctl] = ("wiring_control", rows[ctl][0], "0", "1", "1")
        self.assertPasses(ev)

    def test_live_wiring_predicate_errors_are_rejected(self):
        rows = manifest()
        chk = next(i for i, (ident, _) in enumerate(rows) if ident.startswith("wiring/") and "/control/" not in ident)
        for pred_rc, outcome in (("1", "fail"), ("2", "harness_error"), ("127", "harness_error")):
            with self.subTest(pred_exit=pred_rc):
                ev = records(rows); ev[chk] = ("wiring", rows[chk][0], pred_rc)
                self.assertFails(ev, f"outcome {outcome} is not the accepted pass for: {rows[chk][0]}")
        ev = records(rows); ev[chk] = ("wiring", rows[chk][0], "0")
        self.assertPasses(ev)

    def canvas_split(self):
        rows = manifest()
        v4 = [r for r in rows if r[0].startswith("canvas/v4/")]
        rest = [r for r in rows if not r[0].startswith("canvas/v4/")]
        body = "".join(f"{i[len('canvas/v4/'):]}\t{o}\n" for i, o in v4)
        return rows, rest, v4, body

    def test_child_record_file_complete_passes(self):
        rows, rest, _, body = self.canvas_split()
        self.assertPasses(records(rest) + [("import", "canvas/v4", "v4.rec")], files={"v4.rec": body})

    def test_missing_child_record_file_fails(self):
        _, rest, v4, _ = self.canvas_split()
        self.assertFails(records(rest) + [("import", "canvas/v4", "absent.rec")],
                         "canvas/v4: the child wrote no record file", f"never reached its recorder: {v4[0][0]}")

    def test_child_exiting_without_all_its_records_fails(self):
        _, rest, v4, body = self.canvas_split()
        short = "".join(body.splitlines(keepends=True)[:-3])
        self.assertFails(records(rest) + [("import", "canvas/v4", "v4.rec")], f"never reached its recorder: {v4[-1][0]}",
                         files={"v4.rec": short})

    def test_malformed_and_truncated_records_fail(self):
        _, rest, _, body = self.canvas_split()
        for name, text, why in (("malformed", body.replace("\tpass\n", " pass\n", 1), "canvas/v4: malformed record 1"),
                                ("truncated", body.rstrip("\n"), "the record file is truncated")):
            with self.subTest(case=name):
                self.assertFails(records(rest) + [("import", "canvas/v4", "v4.rec")], why, files={"v4.rec": text})

    # ---- outcome contracts ------------------------------------------------------------------------------------------
    def replace_one(self, prefix, event):
        rows = manifest()
        i = next(k for k, (ident, _) in enumerate(rows) if ident.startswith(prefix))
        ev = records(rows); ev[i] = event(rows[i][0])
        return ev, rows[i][0]

    def test_trait_contract(self):
        for hits, brc, prc, outcome in (("0", "-", "-", "anchor_miss"), ("1", "1", "-", "no_compile"),
                                        ("1", "0", "0", "stayed_green")):
            with self.subTest(outcome=outcome):
                ev, ident = self.replace_one("trait/", lambda i: ("trait", i, hits, brc, prc))
                self.assertFails(ev, f"outcome {outcome} is not the accepted red for: {ident}")
        for prc in ("1", "134"):          # ⓘ ANY nonzero probe exit counts, a crash included — [[B473]], unchanged
            with self.subTest(probe_exit=prc):
                ev, _ = self.replace_one("trait/", lambda i: ("trait", i, "1", "0", prc))
                self.assertPasses(ev)

    def test_missing_trait_contract(self):
        ev, _ = self.replace_one("missing/", lambda i: ("missing", i, "1", "1", "1"))
        self.assertPasses(ev)
        for hits, brc, named, outcome in (("1", "0", "-", "compiled"), ("1", "1", "0", "unrelated_compile_error"),
                                          ("2", "-", "-", "anchor_miss")):
            with self.subTest(outcome=outcome):
                ev, ident = self.replace_one("missing/", lambda i: ("missing", i, hits, brc, named))
                self.assertFails(ev, f"outcome {outcome} is not the accepted named_compile_error for: {ident}")

    def test_negctl_kinds_are_accepted_and_kept_apart(self):
        rows = manifest()
        v3 = [r for r in rows if r[0].startswith("negctl/v3/")]
        rest = [r for r in rows if not r[0].startswith("negctl/v3/")]
        kinds = ["compile_fail" if k % 2 else "assert_red" for k in range(len(v3))]
        body = "".join(f"{i[len('negctl/v3/'):]}\tred\t{kd}\n" for (i, _), kd in zip(v3, kinds))
        out = self.assertPasses(records(rest) + [("import", "negctl/v3", "n.rec")], files={"n.rec": body})
        self.assertIn(f"negctl v3 assert_red: {kinds.count('assert_red')}", out)
        self.assertIn(f"negctl v3 compile_fail: {kinds.count('compile_fail')}", out)
        for bad in ("stayed_green", "not_applied"):
            with self.subTest(outcome=bad):
                text = body.replace("\tred\tassert_red\n", f"\t{bad}\n", 1)
                self.assertFails(records(rest) + [("import", "negctl/v3", "n.rec")], f"outcome {bad} is not the accepted red",
                                 files={"n.rec": text})

    # ---- --no-neg ---------------------------------------------------------------------------------------------------
    def test_no_neg_accounts_everything_but_negctl_and_never_passes(self):
        rows = [r for r in manifest() if not r[0].startswith("negctl/")]
        rc, out = self.run_st(records(rows), no_neg=True)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.splitlines()[-1], BANNER, out)
        self.assertNotIn("PASS", out.splitlines())
        self.assertIn(f"identities accounted: {len(rows)} declared, {len(rows)} exactly once", out)
        n_neg = sum(1 for r in manifest() if r[0].startswith("negctl/"))
        self.assertIn(f"negctl: {n_neg} identities SKIPPED by mode (--no-neg)", out)

    def test_no_neg_missing_inline_control_still_fails(self):
        rows = [r for r in manifest() if not r[0].startswith("negctl/")]
        gone = next(i for i, _ in rows if "/control/" in i)
        rc, out = self.run_st(records([r for r in rows if r[0] != gone]), no_neg=True)
        self.assertEqual(rc, 1, out)
        self.assertEqual(out.splitlines()[-2:], ["FAIL", BANNER], out)
        self.assertIn(f"never reached its recorder: {gone}", out)

    def test_no_neg_rejects_an_unexpected_negctl_observation(self):
        rows = [r for r in manifest() if not r[0].startswith("negctl/")]
        neg = next(r for r in manifest() if r[0].startswith("negctl/"))
        rc, out = self.run_st(records(rows) + [("record",) + neg], no_neg=True)
        self.assertEqual(rc, 1, out)
        self.assertIn(f"UNKNOWN label — no such probe identity is declared: {neg[0]}", out)

    # ---- load-bearing: the ONE final accounting statement (synthetic path) -----------------------------------------
    def test_bypassing_the_final_accounting_lets_an_omission_through(self):
        src = RUNNER.read_text(encoding="utf-8")
        self.assertEqual(src.count(ACCOUNTING_STATEMENT), 1, "the accounting statement must exist exactly once")
        rows = manifest()
        omission = records(rows[:40] + rows[41:])
        layer_lost = records([r for r in rows if not r[0].startswith("canvas/v4/")])
        for name, ev in (("one identity", omission), ("a whole child layer", layer_lost)):
            with self.subTest(case=name):
                self.assertEqual(self.run_st(ev)[0], 1)
                with tempfile.TemporaryDirectory(prefix="b459-bypass-") as d:
                    tools = Path(d) / "tools"; (tools / "probe_board_ui").mkdir(parents=True)
                    (tools / "probe_accounting.sh").symlink_to(LIBRARY)      # the REAL shared comparator
                    bypassed = tools / "probe_board_ui" / "run.sh"
                    bypassed.write_text(src.replace(ACCOUNTING_STATEMENT, ": # [[B459]] bypassed by the regression"))
                    rc, out = self.run_st(ev, runner=bypassed)
                self.assertEqual(rc, 0, "with the accounting bypassed the omission must go undetected — " + out)


class Copies(unittest.TestCase):
    """Runner copies in a scratch tree: the product sources COPIED (W44 walks them with a plain `find`, which does not
    follow a link), the real shared library LINKED, the probe's own files linked unless the case edits them."""

    @classmethod
    def make_tree(cls, d: Path, edits: dict) -> Path:
        for name in ("src", "lib", "variants"):
            shutil.copytree(REPO / name, d / name, symlinks=True)
        shutil.copy2(REPO / "platformio.ini", d / "platformio.ini")
        tools = d / "tools"; probe = tools / "probe_board_ui"; probe.mkdir(parents=True)
        (tools / "probe_accounting.sh").symlink_to(LIBRARY)
        for f in PROBE_DIR.iterdir():
            if f.name in ("__pycache__",):
                continue
            if f.name in edits:
                text = f.read_text(encoding="utf-8")
                new = edits[f.name](text)
                assert new != text, f"the edit to {f.name} changed nothing"
                (probe / f.name).write_text(new, encoding="utf-8")
            else:
                (probe / f.name).symlink_to(f)
        return probe / "run.sh"

    def run_copy(self, edits, *args, timeout=600):
        with tempfile.TemporaryDirectory(prefix="b459-copy-") as d:
            runner = self.make_tree(Path(d), edits)
            p = subprocess.run(["bash", str(runner), *args], capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr

    def assertCensusRejects(self, edits, *why):
        rc, out = self.run_copy(edits)
        self.assertEqual(rc, 1, out)
        self.assertIn("fail-closed: nothing is built or run", out)
        self.assertNotIn("== shared Heltec board-canvas probe", out)
        for w in why:
            self.assertIn(w, out)

    # ---- the static census: a source declaration removed while the manifest stays (B459R-3) ------------------------
    def test_census_rejects_a_removed_wiring_call(self):
        drop_w1 = lambda s: once_re(s, r'\nwchk "W1 [^\n]*\\\n[^\n]*\n', "\n")
        self.assertCensusRejects({"run.sh": drop_w1}, "census: MISSING verdict", "a declaration in the source: wiring/W1\n",
                                 "a declaration in the source: wiring/W1/control/1\n")

    def test_census_rejects_a_removed_wiring_control(self):
        self.assertCensusRejects({"run.sh": lambda s: once_re(s, r"( w7 '[^\n]*') \\\n[^\n]*\n", r"\1\n")},
                                 "never reached a declaration in the source: wiring/W7/control/2")

    def test_census_rejects_shortened_trait_missing_and_S6_lists(self):
        for name, edit, gone in (
                ("trait", lambda s: once_re(s, r'\ntrait_control "T14 [^\n]*', ""), "trait/T14"),
                ("missing", lambda s: once(s, "MR_UI_ADC_CTRL_FAILSAFE_PARK MR_UI_VBAT_ADC_SCALE; do\n  missing_trait_control",
                                           "MR_UI_ADC_CTRL_FAILSAFE_PARK; do\n  missing_trait_control"),
                 "missing/MR_UI_VBAT_ADC_SCALE"),
                ("S6", lambda s: once(s, "MR_UI_ADC_CTRL_FAILSAFE_PARK MR_UI_VBAT_ADC_SCALE; do\n  n_env",
                                      "MR_UI_ADC_CTRL_FAILSAFE_PARK; do\n  n_env"), "struct/S6/MR_UI_VBAT_ADC_SCALE")):
            with self.subTest(layer=name):
                self.assertCensusRejects({"run.sh": edit}, f"never reached a declaration in the source: {gone}")

    def test_census_rejects_a_removed_CHK_and_a_removed_negctl_control(self):
        self.assertCensusRejects({"probe_main.cpp": lambda s: once_re(s, r'\n    CHK\("P1b [^\n]*', "")},
                                 "canvas/v3/P1b", "canvas/v4/P1b")
        self.assertCensusRejects({"negctl.py": lambda s: drop_entry(s, " ('C1 ", " ('C2 ")}, "negctl/v3/C1")

    def test_census_rejects_an_undeclared_manifest_gap(self):
        self.assertCensusRejects({"expected.tsv": lambda s: once(s, "\nwiring/W1\tpass\n", "\n")},
                                 "UNKNOWN label — no such manifest identity is declared: wiring/W1")

    def test_census_rejects_an_unsupported_call_shape(self):
        self.assertCensusRejects({"run.sh": lambda s: once(s, '\nwchk "W1 ', '\nfalse && wchk "W1 ')},
                                 "an unrecognised declaration shape")

    # ---- runtime omissions with every declaration intact: only an executed run can show them ----------------------
    def test_runtime_omissions_with_declarations_intact_fail(self):
        # ⓘ every guard sits on its OWN line, so each call line stays exactly as the census reads it: these omissions
        #   are invisible to the census by construction, and the RUN must refuse them on its own.
        def omit(s):
            s = once(s, '  local id="wiring/${label%% *}" st outcome\n',
                     '  local id="wiring/${label%% *}" st outcome\n  [ "$id" = wiring/W1 ] && return 0\n')    # early return
            s = once(s, '\n  missing_trait_control "$name"\n',
                     '\n  [ "$name" = MR_UI_VBAT_ADC_SCALE ] && continue\n  missing_trait_control "$name"\n')   # shortened loop
            s = once(s, '; do\n  n_env=', '; do\n  [ "$nm" = MR_UI_VBAT_ADC_SCALE ] && continue\n  n_env=')       # shortened S6
            s = once_re(s, r'(\ntrait_control "T14 [^\n]*)', r'\nif false; then\1\nfi')                         # false guard
            return once_re(s, r'(\ntrait_control "T13 [^\n]*)', r'\nif __b459_undefined_guard__ 2>/dev/null; then\1\nfi')  # 127
        rc, out = self.run_copy({"run.sh": omit}, "--no-neg")
        self.assertEqual(rc, 1, out)
        n = len(manifest())                                         # the census PASSED: these omissions are runtime-only
        self.assertIn(f"declarations census: {n} declared, {n} exactly once with the accepted outcome, 0 guard failure(s)", out)
        self.assertEqual(fail_lines(out), missing("wiring/W1", "wiring/W1/control/1", "missing/MR_UI_VBAT_ADC_SCALE",
                                                  "struct/S6/MR_UI_VBAT_ADC_SCALE", "trait/T14", "trait/T13"), out)
        self.assertEqual(out.splitlines()[-2:], ["FAIL", BANNER], out)

    def test_a_shortened_negctl_iterator_fails_while_both_lists_stay_declared(self):
        rc, out = self.run_copy({"negctl.py": lambda s: once(s, "in enumerate(MUT):", "in enumerate(MUT[1:]):")})
        self.assertEqual(rc, 1, out)
        self.assertEqual(fail_lines(out), missing("negctl/v3/C1", "negctl/v4/C11a"), out)
        self.assertIn("real source verified UNCHANGED; 59 V3 controls run", out)
        self.assertEqual(out.splitlines()[-1], "FAIL", out)

    # ---- load-bearing on the REAL path: the same statement, a real run ---------------------------------------------
    def test_the_final_statement_is_what_refuses_real_omissions(self):
        faults = lambda s: once(once(s, '  local id="wiring/${label%% *}" st outcome\n',
                                     '  local id="wiring/${label%% *}" st outcome\n  [ "$id" = wiring/W1 ] && return 0\n'),
                                'MR_BOARD_UI_RECORDS="$OUT/canvas_v4.rec" "$OUT/probe_v4"', '"$OUT/probe_v4"')
        rc, out = self.run_copy({"run.sh": faults}, "--no-neg")
        self.assertEqual(rc, 1, out)
        v4 = [i for i, _ in manifest() if i.startswith("canvas/v4/")]
        self.assertEqual(fail_lines(out), missing("wiring/W1", "wiring/W1/control/1", *v4)
                         | {"  FAIL [[B459]] a probe identity guard failed: canvas/v4: the child wrote no record file"}, out)
        bypass = lambda s: once(faults(s), ACCOUNTING_STATEMENT, ": # [[B459]] bypassed by the regression")
        rc, out = self.run_copy({"run.sh": bypass}, "--no-neg")
        self.assertEqual(rc, 0, "with the accounting bypassed the real omissions must go undetected — " + out)


if __name__ == "__main__":
    unittest.main()
