#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B435: execute the real failure classifier and prove its two failure controls are detected."""

import ast
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import NamedTuple
import unittest
from unittest.mock import Mock


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tools" / "probe_ui_model_mutations.py"


class MutationUnusableReasonTests(unittest.TestCase):
    def run_selftest(self, harness):
        # No compiler, pio or rsync is available: this diagnostic must execute in-process.
        return subprocess.run([sys.executable, str(harness), "--selftest-unusable"],
                              cwd=harness.parent.parent, env=dict(os.environ, PATH=""),
                              capture_output=True, text=True, timeout=10)

    def test_selftest_distinguishes_and_retains_both_failures(self):
        result = self.run_selftest(HARNESS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("the mutant does not compile (build rc 1)", result.stdout)
        self.assertIn("the suite ran without a verdict (binary rc -11 / killed by signal 11)", result.stdout)
        self.assertIn("SELFTEST OK — build and run failures are told apart and retained", result.stdout)
        excerpts = [line.strip() for line in result.stdout.splitlines()
                    if line.startswith("        ") and "retained:" not in line]
        self.assertEqual(excerpts, [f"error: fabricated build failure {i}" for i in (7, 19, 31)] +
                         [f"binary output {i:02d}" for i in range(18, 30)])
        paths = [Path(p) for p in re.findall(r"^        retained: (.+)$", result.stdout, re.M)]
        self.assertEqual(paths, [ROOT / ".pio/mutation-unusable/selftest" / (n + ".log")
                                 for n in ("build", "run")])
        build_lines = [f"build noise {i:02d}" for i in range(40)]
        for i in (7, 19, 31):
            build_lines.insert(i, f"error: fabricated build failure {i}")
        self.assertEqual(paths[0].read_bytes(), ("\n".join(build_lines) + "\n").encode())
        self.assertEqual(paths[1].read_bytes(),
                         "".join(f"binary output {i:02d}\n" for i in range(30)).encode())

    def control(self, old, new):
        source = HARNESS.read_text()
        self.assertEqual(source.count(old), 1, "control must match one real site")
        with tempfile.TemporaryDirectory(prefix="mr-unusable-control-") as tmp:
            harness = Path(tmp) / "tools" / HARNESS.name
            harness.parent.mkdir()
            harness.write_text(source.replace(old, new))
            result = self.run_selftest(harness)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("SELFTEST FAILED", result.stdout)
            self.assertNotIn("SELFTEST OK", result.stdout)

    def test_collapsed_arm_control_is_red(self):
        self.control('if reason == "build":', 'if True:')

    def test_deleted_retention_control_is_red(self):
        self.control('path.write_text(capture, encoding="utf-8")', 'pass  # retained write deleted')

    def test_merged_label_is_absent(self):
        self.assertNotIn("does not compile / did not run", HARNESS.read_text())

    def suite_runner(self, outcomes):
        # Compile the actual run_suite definition with a subprocess double. Never import the harness's
        # top-level orchestrator, and never reproduce its decisions in a test-side implementation.
        tree = ast.parse(HARNESS.read_text())
        definitions = [node for node in tree.body
                       if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and
                       node.name in ("SuiteFailure", "run_suite")]
        self.assertEqual(len(definitions), 2)
        process = Mock(side_effect=outcomes)
        namespace = dict(NamedTuple=NamedTuple, os=os, re=re, ROOT=str(ROOT),
                         subprocess=Mock(run=process))
        exec(compile(ast.Module(body=definitions, type_ignores=[]), str(HARNESS), "exec"), namespace)
        return namespace["run_suite"], process

    def test_build_failure_cannot_run_a_stale_binary(self):
        for rc, stdout, stderr in [(1, "pio stopped\n", "Error: tool unavailable\n"),
                                   (0, "build output\n", "error: compilation failed\n")]:
            with self.subTest(rc=rc):
                run, process = self.suite_runner([
                    subprocess.CompletedProcess([], rc, stdout, stderr)])
                result, failure = run()
                self.assertIsNone(result)
                self.assertEqual(failure, ("build", rc, stdout + stderr))
                self.assertEqual(process.call_count, 1)

    def test_run_failure_retains_code_and_both_full_streams(self):
        stdout, stderr = "binary stdout\n" * 100, "binary stderr\n" * 100
        run, process = self.suite_runner([subprocess.CompletedProcess([], 0, "", ""),
                                         subprocess.CompletedProcess([], -11, stdout, stderr)])
        result, failure = run()
        self.assertIsNone(result)
        self.assertEqual(failure, ("run", -11, stdout + stderr))
        self.assertEqual(process.call_count, 2)

    def test_existing_doctest_verdict_is_unchanged(self):
        stdout = "test cases: 2950 | 2949 passed | 1 failed\nassertions: 195770 | 195767 passed | 3 failed\n"
        run, process = self.suite_runner([subprocess.CompletedProcess([], 0, "", ""),
                                         subprocess.CompletedProcess([], 1, stdout, "stderr diagnostic")])
        self.assertEqual(run(), ((3, 2950, 195770), stdout))
        self.assertEqual(process.call_count, 2)


if __name__ == "__main__":
    unittest.main()
