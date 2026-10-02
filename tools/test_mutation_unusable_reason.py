#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B435 + B478/B490: execute the real failure classifier, the real byte capture and the real orchestrator, and
prove that every repair's control is detected.

⛔ The harness is NEVER imported — its top level starts a battery. The function-level cases compile the harness's
own definitions out of its text by AST; the end-to-end cases run the real file, byte-identical, in a temporary tree
whose `pio` is a fake that builds a scripted program per installed mutant."""

import ast
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from typing import NamedTuple
import unittest
from unittest.mock import Mock


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tools" / "probe_ui_model_mutations.py"
VIEW_BYTES = 65536          # A4's bound on the `.log` view, in raw bytes
DEFINITIONS = ("SuiteOutput", "SuiteFailure", "escape_child_bytes", "bounded_view", "child_lines",
               "unusable_verdict", "unusable_log_path", "stream_record", "unretained_record", "retain_unusable",
               "run_suite")
CONSTANTS = ("_VIEW_BYTES",)


class HarnessDidNotRun(Exception):
    """A copy that did not start is a VACUOUS control, never a result — deliberately not an AssertionError."""


def ensure(condition, message):
    if not condition:
        raise AssertionError(message)


def harness_source():
    return HARNESS.read_text(encoding="utf-8")


def harness_namespace(source, root, process, required=True):
    """Compile the harness's OWN definitions out of `source`, with `subprocess.run` replaced by `process`."""
    body = []
    for node in ast.parse(source).body:
        if isinstance(node, (ast.ClassDef, ast.FunctionDef)) and node.name in DEFINITIONS:
            body.append(node)
        elif isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id in CONSTANTS
                                                  for t in node.targets):
            body.append(node)
    found = {getattr(node, "name", None) or node.targets[0].id for node in body}
    if required and found != set(DEFINITIONS) | set(CONSTANTS):
        raise LookupError(f"the harness no longer defines {sorted(set(DEFINITIONS) | set(CONSTANTS) - found)}")
    namespace = dict(NamedTuple=NamedTuple, Path=Path, hashlib=hashlib, json=json, os=os, re=re, ROOT=str(root),
                     subprocess=SimpleNamespace(run=process))
    exec(compile(ast.Module(body=body, type_ignores=[]), str(HARNESS), "exec"), namespace)
    return namespace


def literal(source, name):
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise LookupError(name)


def unescape(view):
    """Invert A4's escape; refuses anything the escape cannot have produced."""
    out, i = bytearray(), 0
    while i < len(view):
        if view[i] != "\\":
            ensure(0x20 <= ord(view[i]) <= 0x7E or view[i] in "\t\n", f"raw character {view[i]!r} in a view")
            out.append(ord(view[i]))
            i += 1
        elif view[i + 1:i + 2] == "\\":
            out.append(0x5C)
            i += 2
        else:
            ensure(re.fullmatch(r"x[0-9a-f]{2}", view[i + 1:i + 4]) is not None, f"bad escape at {i}")
            out.append(int(view[i + 2:i + 4], 16))
            i += 4
    return bytes(out)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def summary(cases, failed_cases, asserts, failed):
    """doctest's own two summary lines."""
    return (f"[doctest] test cases: {cases:6d} | {cases - failed_cases:6d} passed | {failed_cases} failed | "
            f"0 skipped\n[doctest] assertions: {asserts:6d} | {asserts - failed:6d} passed | {failed} failed |\n"
            ).encode()


# ===== A9.1 — REAL CHILDREN THROUGH THE REAL `run_suite` ==========================================================
FAIL1 = b"test cases: 1 | 0 passed | 1 failed\nassertions: 1 | 0 passed | 1 failed\n"
PASS1 = b"test cases: 1 | 1 passed | 0 failed\nassertions: 1 | 1 passed | 0 failed\n"
CASE_NO_ASSERT = b"test cases: 1 | 0 passed | 1 failed\nassertions: 1 | 1 passed | 0 failed\n"


class Case(NamedTuple):
    name: str
    stdout: bytes
    stderr: bytes
    rc: int                 # the child's exit; negative = killed by that signal
    phase: str              # "build": the child stands in the build slot; "run": the build is stubbed clean
    expected: tuple         # a verdict tuple, or (reason, exit) for a no-verdict failure
    defect: str             # which defect the base harness shows on this case ("" = none)


# The pre-check's 13 scenarios (its `decode-results.json` holds the same bytes), with their post-fix results.
PRECHECK = [
    Case("raw_bb_stdout", b"\xbb\n" + FAIL1, b"", 1, "run", (1, 1, 1), "B478"),
    Case("raw_c5_stderr", FAIL1, b"\xc5\n", 1, "run", (1, 1, 1), "B478"),
    Case("malformed_both", b"\x80\xff\n" + FAIL1, b"\xc5", 1, "run", (1, 1, 1), "B478"),
    Case("valid_utf8", "ł »\n".encode() + FAIL1, b"diagnostic\n", 1, "run", (1, 1, 1), ""),
    Case("split_valid_utf8", b"\xc5\x82\n" + FAIL1, b"", 1, "run", (1, 1, 1), ""),
    Case("truncated_utf8", FAIL1 + b"\xc5", b"", 1, "run", (1, 1, 1), "B478"),
    Case("no_verdict", b"out\n", b"err\n", 0, "run", ("run", 0), ""),
    Case("signal_no_verdict", b"out\n", b"err\n", -11, "run", ("run", -11), ""),
    Case("signal_with_failure_summary", FAIL1, b"crash after summary\n", -11, "run", ("run", -11), "B490"),
    Case("signal_with_success_summary", PASS1, b"crash after summary\n", -11, "run", ("run", -11), "B490"),
    Case("exit2_with_summary", FAIL1, b"wrong exit\n", 2, "run", ("run", 2), "B490"),
    Case("build_raw_stderr", b"build\n", b"error: \xbb\n", 1, "build", ("build", 1), "B478"),
    Case("build_ascii", b"build\n", b"error: stopped\n", 1, "build", ("build", 1), ""),
]
# A9.1's further exit-agreement cases.
AGREEMENT = [
    Case("zero_bytes", b"", b"", 0, "run", ("run", 0), ""),
    Case("success_exit0", PASS1, b"", 0, "run", (0, 1, 1), ""),
    Case("success_exit1", PASS1, b"", 1, "run", ("run", 1), "B490"),
    Case("failure_exit0", FAIL1, b"", 0, "run", ("run", 0), "B490"),
    Case("failed_case_without_failed_assertion", CASE_NO_ASSERT, b"", 1, "run", ("run", 1), "B490"),
]


def write_child(path, stdout, stderr, rc, split=False):
    """A REAL child process: it writes exactly these bytes and ends with exit `rc` (negative = that signal)."""
    writes = [stdout[:1], stdout[1:]] if split else [stdout]
    lines = ["import os, resource, signal", "resource.setrlimit(resource.RLIMIT_CORE, (0, 0))",
             "def put(fd, data):", "    while data:", "        data = data[os.write(fd, data):]"]
    lines += [f"put(1, {w!r})" for w in writes if w] + ([f"put(2, {stderr!r})"] if stderr else [])
    lines.append(f"os.kill(os.getpid(), {-rc})" if rc < 0 else f"raise SystemExit({rc})")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def run_scenario(source, tmp, case, required=True):
    """The harness's own `run_suite` against a real child. Only the BUILD response is stubbed, and only for a
    run-phase case; it answers in the type the caller asked for, so the base harness's text capture runs too."""
    child = write_child(Path(tmp) / f"{case.name}.py", case.stdout, case.stderr, case.rc,
                        split=case.name == "split_valid_utf8")
    calls = []

    def process(cmd, **kw):
        calls.append(cmd)
        if cmd[0] == "pio" and case.phase != "build":
            empty = "" if kw.get("text") else b""
            return subprocess.CompletedProcess(cmd, 0, empty, empty)
        kw.pop("cwd", None)
        return subprocess.run([sys.executable, str(child)], **kw)

    result, out = harness_namespace(source, tmp, process, required)["run_suite"]()
    return result, out, calls


def check_case(source, tmp, case, required=True):
    """Raise AssertionError unless `case` gives its exact post-fix result with BOTH raw streams back byte for byte."""
    result, out, calls = run_scenario(source, tmp, case, required)
    if isinstance(case.expected[0], str):
        ensure(result is None, f"{case.name}: CREDITED verdict {result!r} — expected a {case.expected[0]} failure "
                               f"with exit {case.expected[1]}")
        ensure((out.reason, out.rc) == case.expected, f"{case.name}: {(out.reason, out.rc)} != {case.expected}")
    else:
        ensure(result == case.expected, f"{case.name}: verdict {result!r} != {case.expected!r}")
        ensure(out.rc == case.rc, f"{case.name}: a credited run carries exit {out.rc}, not {case.rc}")
    ensure((out.stdout, out.stderr) == (case.stdout, case.stderr), f"{case.name}: the raw streams did not come back")
    ensure(len(calls) == (1 if case.phase == "build" else 2),
           f"{case.name}: {len(calls)} subprocess call(s) — a build failure must never launch the binary")


# ===== A9.2 — SIZE: more than 64 KiB, a multibyte sequence cut at the bound ========================================
SIZE_STDERR = b"\xc5\x82 stderr tail\n"
SIZE_STDOUT = b"a" * 100 + "€".encode() + b"x" * (VIEW_BYTES - 2 - len(SIZE_STDERR))


def check_size(source, tmp):
    total = SIZE_STDOUT + SIZE_STDERR
    ensure(len(total) > VIEW_BYTES and total[-VIEW_BYTES] == 0x82, "fixture: the bound must cut inside the euro sign")
    case = Case("size", SIZE_STDOUT, SIZE_STDERR, 3, "run", ("run", 3), "")
    result, failure, _calls = run_scenario(source, tmp, case)
    ensure(result is None and (failure.reason, failure.rc) == ("run", 3), f"size: {result!r} / {failure!r}")
    ns = harness_namespace(source, tmp, None)
    _line, _extra, view = ns["unusable_verdict"](failure)
    meta = dict(id="S01", label="S01 size case", worker=0, phase="run", exit=3, signal=None, note="")
    lines, problem = ns["retain_unusable"]("sizecase", "S01 size case", meta, failure.stdout, failure.stderr)
    ensure(problem is None, f"size: retention failed: {problem}")
    path = Path(tmp) / ".pio" / "mutation-unusable" / "sizecase" / "S01.log"
    ensure(lines[0] == f"        retained: {path}", f"size: {lines[0]!r}")
    for suffix, data in ((".stdout", SIZE_STDOUT), (".stderr", SIZE_STDERR)):
        kept = path.with_suffix(suffix).read_bytes()
        ensure((len(kept), sha256(kept)) == (len(data), sha256(data)), f"size: {suffix} lost raw bytes")
    record = json.loads(path.with_suffix(".json").read_text(encoding="ascii"))
    ensure(record["stdout"]["bytes"] == len(SIZE_STDOUT) and record["stdout"]["sha256"] == sha256(SIZE_STDOUT),
           "size: metadata stdout")
    ensure(record["stderr"]["bytes"] == len(SIZE_STDERR) and record["stderr"]["sha256"] == sha256(SIZE_STDERR),
           "size: metadata stderr")
    ensure(unescape(view) == total[-VIEW_BYTES:], "size: the view is not exactly the last 64 KiB of raw bytes")
    ensure(path.read_text(encoding="ascii") == view, "size: the .log is not the view")


# ===== A9.3/A9.4/A9.7 — END TO END THROUGH THE REAL ORCHESTRATOR ==================================================
FAKE_PIO = r'''
import hashlib, json, os, sys
plan = json.load(open(os.environ["MR_FAKE_PIO_PLAN"]))
if sys.argv[1:] != ["test", "-e", "native", "--without-testing"]:
    sys.stderr.write("fake pio: unexpected arguments %r\n" % (sys.argv[1:],))
    sys.exit(97)
with open(plan["target"], "rb") as f:
    state = plan["states"].get(hashlib.sha256(f.read()).hexdigest())
if state is None:
    sys.stderr.write("fake pio: the installed target is in no planned state\n")
    sys.exit(98)
program = os.path.join(".pio", "build", "native", "program")
if "build" in state:
    if os.path.exists(program):
        os.remove(program)
    os.write(1, bytes.fromhex(state["build"]["stdout"]))
    os.write(2, bytes.fromhex(state["build"]["stderr"]))
    sys.exit(state["build"]["rc"])
ending = ("os.kill(os.getpid(), %d)" % -state["rc"]) if state["rc"] < 0 else ("raise SystemExit(%d)" % state["rc"])
os.makedirs(os.path.dirname(program), exist_ok=True)
with open(program, "w") as f:
    f.write("#!%s\nimport os, resource, signal\nresource.setrlimit(resource.RLIMIT_CORE, (0, 0))\n"
            "def put(fd, data):\n    while data:\n        data = data[os.write(fd, data):]\n"
            "put(1, bytes.fromhex(%r))\nput(2, bytes.fromhex(%r))\n%s\n"
            % (sys.executable, state["stdout"], state["stderr"], ending))
os.chmod(program, 0o755)
'''
# A9.4: ONLY the parent decodes by an ASCII locale. The workers inherit a UTF-8 environment, because the real worker
# reads and hashes its UTF-8 target in locale text mode and cannot run under any other one (ledger ruling R1).
LAUNCHER = r'''
import codecs, locale, runpy, sys
locale.setlocale(locale.LC_CTYPE, "C")
if codecs.lookup(locale.getpreferredencoding(False)).name == "utf-8":
    sys.stderr.write("launcher: the parent's locale decoding is still UTF-8\n")
    sys.exit(99)
sys.argv = sys.argv[1:]
runpy.run_path(sys.argv[0], run_name="__main__")
'''

CLEAN = dict(stdout=summary(5, 0, 50, 0), stderr=b"", rc=0)
CLEAN_DIES = dict(stdout=summary(5, 0, 50, 0), stderr=b"fatal signal after the summary \xbb\n", rc=-11)
# Round-robin over two workers: w0 gets entries 0, 2, 4, 6, 8 and w1 gets 1, 3, 5, 7 — each worker gets all four of
# A9.3's scenarios: invalid UTF-8 on both streams with no creditable verdict; a failure summary then a signal; invalid
# UTF-8 beside a failure summary with exit 1 (RED); an ordinary RED. Entry 8 is a build failure with a raw byte.
MIXED = {
    0: dict(stdout=b"\xbbbinary started \xc5\n", stderr=b"\xff\xfe diagnostic\n", rc=3),
    1: dict(stdout=b"\x80half a line", stderr=b"\xc5\x82\xc5", rc=0),
    2: dict(stdout=summary(5, 1, 50, 2), stderr=b"Segmentation fault after the summary\n", rc=-11),
    3: dict(stdout=summary(5, 1, 50, 2), stderr=b"\xbb abort after the summary\n", rc=-6),
    4: dict(stdout=b"\xbb marker \xc5\n" + summary(5, 1, 50, 3), stderr=b"\xc5\n", rc=1),
    5: dict(stdout=summary(5, 2, 50, 4) + b"\xff", stderr=b"\x80", rc=1),
    6: dict(stdout=summary(5, 1, 50, 5), stderr=b"", rc=1),
    7: dict(stdout=summary(5, 1, 50, 6), stderr=b"", rc=1),
    8: dict(build=dict(stdout=b"Compiling .pio/build/native/src/main.o\n",
                       stderr=b"src/firmware_ui_model.h:1:1: error: \xbb stray byte\n", rc=1)),
}
MIXED_EXPECT = {0: ("UNUSABLE", "run", 3), 1: ("UNUSABLE", "run", 0), 2: ("UNUSABLE", "run", -11),
                3: ("UNUSABLE", "run", -6), 4: ("RED", 3), 5: ("RED", 4), 6: ("RED", 5), 7: ("RED", 6),
                8: ("UNUSABLE", "build", 1)}
# A9.7's runs: one UNUSABLE entry to retain, every other entry an ordinary RED (workers otherwise return 0 or 1).
FAULT = {i: dict(stdout=summary(5, 1, 50, 10 + i), stderr=b"", rc=1) for i in range(1, 9)}
FAULT[0] = dict(stdout=b"\xbb fault-case stdout\n", stderr=b"\xc5 fault-case stderr\n", rc=3)

# The exact one-site edits (A9.5, A9.7). Each must match exactly one site of the real harness text.
SITE_SHIP = "shipped.write(data)"
SITE_COMPARE = 'if (len(data), hashlib.sha256(data).hexdigest()) != (want.get("bytes"), want.get("sha256")):'
SITE_RAW_WRITE = "path.with_suffix(suffix).write_bytes(data)"
SITE_WRITE_HANDLING = "except OSError as error:"
SITE_POPEN = "p = subprocess.Popen(cmd, cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=wenv)"
SITE_NATIVE_CAPTURE = '.pio/build/native/program")], cwd=ROOT, capture_output=True)'
SITE_AGREEMENT = "    if agrees:"
TRANSFER_FAULT = (SITE_SHIP, 'shipped.write(data + (b"#" if name == "stderr" else b""))  # A9.7 transfer fault')


def edited(source, *edits):
    """An exact one-site edit per (old, new) pair; the copy must still compile — else the control is vacuous."""
    for old, new in edits:
        if source.count(old) != 1:
            raise HarnessDidNotRun(f"control site {old!r} matches {source.count(old)} sites, not exactly one")
        source = source.replace(old, new)
    compile(source, "<control copy>", "exec")
    return source


def plan_states(source, plan, clean):
    target = literal(source, "TARGET_SRC")["w4aident"]
    original = (ROOT / target).read_bytes()
    ensure(b"\r" not in original, "fixture: the target must survive the worker's text-mode round trip")
    encode = lambda b: {k: (v.hex() if isinstance(v, bytes) else encode(v) if isinstance(v, dict) else v)
                        for k, v in b.items()}
    states = {sha256(original): encode(clean)}
    text = original.decode("utf-8")
    for i, (label, pat, rep) in enumerate(literal(source, "MUTS_W4AIDENT")):
        ensure(text.count(pat) == 1, f"fixture: {label.split()[0]} must match its target exactly once")
        states[sha256(text.replace(pat, rep).encode("utf-8"))] = encode(plan.get(i, clean))
    return {"target": target, "states": states}


class Battery(NamedTuple):
    rc: int
    console: str
    stderr: str
    tree: Path
    scratch: Path
    sidecars: Path
    unchanged: bool


def run_battery(test, source, plan, workers, clean=CLEAN, obstruct=False, parent_c_locale=False):
    tmp = Path(tempfile.mkdtemp(prefix="mr-b478-e2e-")).resolve()
    test.addCleanup(shutil.rmtree, tmp, True)
    tree = tmp / "tree"
    harness = tree / "tools" / HARNESS.name
    harness.parent.mkdir(parents=True)
    harness.write_bytes(source.encode("utf-8"))
    states = plan_states(source, plan, clean)
    (tree / states["target"]).parent.mkdir(parents=True)
    shutil.copyfile(ROOT / states["target"], tree / states["target"])
    bindir = tmp / "bin"
    bindir.mkdir()
    (bindir / "pio").write_text(f"#!{sys.executable}\n" + FAKE_PIO, encoding="utf-8")
    (bindir / "pio").chmod(0o755)
    (tmp / "plan.json").write_text(json.dumps(states), encoding="utf-8")
    scratch, sidecars = tmp / "scratch", tmp / "sidecars"
    scratch.mkdir()
    sidecars.mkdir()
    if obstruct:          # A9.7: a plain FILE where `.pio/mutation-unusable/<target>` must be a directory
        blocked = tree / ".pio" / "mutation-unusable" / "w4aident"
        blocked.parent.mkdir(parents=True)
        blocked.write_text("a plain file where a directory must go\n")
    env = dict(os.environ, PATH=f"{bindir}{os.pathsep}{os.environ.get('PATH', '')}",
               MR_FAKE_PIO_PLAN=str(tmp / "plan.json"), MR_MUT_SCRATCH=str(scratch), TMPDIR=str(sidecars),
               MR_MUT_BASE="5,50", LC_ALL="C.UTF-8", PYTHONUTF8="0", PYTHONIOENCODING="utf-8")
    env.pop("MR_MUT_KEEP_SCRATCH", None)
    args = ["--target=w4aident", f"--workers={workers}"]
    cmd = ([sys.executable, "-c", LAUNCHER, str(harness), *args] if parent_c_locale
           else [sys.executable, str(harness), *args])
    watched = [harness, tree / states["target"]]
    before = [sha256(p.read_bytes()) for p in watched]
    p = subprocess.run(cmd, cwd=tree, env=env, capture_output=True, timeout=600)
    console = p.stdout.decode("utf-8", "backslashreplace")
    if p.returncode == 99 or "-- target w4aident:" not in console:
        raise HarnessDidNotRun(f"the harness copy did not start (exit {p.returncode}): "
                               f"{(console + p.stderr.decode('utf-8', 'backslashreplace'))[-2000:]}")
    return Battery(p.returncode, console, p.stderr.decode("utf-8", "backslashreplace"), tree, scratch, sidecars,
                   [sha256(p.read_bytes()) for p in watched] == before)


def check_cleanup(battery):
    ensure(not any(battery.scratch.iterdir()), f"scratch root left behind: {list(battery.scratch.iterdir())}")
    ensure(not any(battery.sidecars.iterdir()), f"worker sidecars left behind: {list(battery.sidecars.iterdir())}")
    ensure(battery.unchanged, "the temporary tree's harness or target changed")


def no_traceback(battery):
    ensure("Traceback" not in battery.console + battery.stderr,
           f"a traceback reached the console: {(battery.console + battery.stderr)[-3000:]}")


def retained_dir(battery):
    return battery.tree / ".pio" / "mutation-unusable" / "w4aident"


def check_retained(battery, ident, label, worker, phase, rc, stdout, stderr):
    base = retained_dir(battery) / f"{ident}.log"
    ensure(f"        retained: {base}\n" in battery.console, f"{ident}: no `retained:` line")
    ensure(base.with_suffix(".stdout").read_bytes() == stdout, f"{ident}: retained raw stdout differs")
    ensure(base.with_suffix(".stderr").read_bytes() == stderr, f"{ident}: retained raw stderr differs")
    meta = json.loads(base.with_suffix(".json").read_text(encoding="ascii"))
    want = dict(target="w4aident", id=ident, label=label, worker=worker, phase=phase, exit=rc,
                signal=-rc if rc < 0 else None)
    ensure({k: meta.get(k) for k in want} == want, f"{ident}: metadata {meta} != {want}")
    for name, data in (("stdout", stdout), ("stderr", stderr)):
        ensure((meta[name]["bytes"], meta[name]["sha256"]) == (len(data), sha256(data)), f"{ident}: {name} record")
    ensure(unescape(base.read_text(encoding="ascii")) == (stdout + stderr)[-VIEW_BYTES:], f"{ident}: .log view")


def verdict_lines(source, expect):
    out = {}
    for i, (label, _pat, _rep) in enumerate(literal(source, "MUTS_W4AIDENT")):
        e = expect[i]
        if e[0] == "RED":
            out[i] = f"  ok   {label} -> RED ({e[1]} assertion(s) failed, match count 1)"
        elif e[1] == "build":
            out[i] = f"  UNUSABLE {label} — the mutant does not compile (build rc {e[2]})"
        else:
            note = f" / killed by signal {-e[2]}" if e[2] < 0 else ""
            out[i] = f"  UNUSABLE {label} — the suite ran without a verdict (binary rc {e[2]}{note})"
    return out


def check_mixed(source, battery, workers):
    """A9.3: the designed identities, the retained evidence, each worker's lines in the parent console, exit 1."""
    no_traceback(battery)
    labels = literal(source, "MUTS_W4AIDENT")
    for i, line in verdict_lines(source, MIXED_EXPECT).items():
        ensure(f"\n{line}\n" in battery.console, f"merged report lacks {line!r}")
        ensure(f"  [w{i % workers}] {line}\n" in battery.console,
               f"worker {i % workers}'s own line did not reach the parent console intact: {line!r}")
        label, ident = labels[i][0], labels[i][0].split()[0]
        if MIXED_EXPECT[i][0] == "UNUSABLE":
            b = MIXED[i].get("build") or MIXED[i]
            check_retained(battery, ident, label, i % workers, MIXED_EXPECT[i][1], MIXED_EXPECT[i][2],
                           b["stdout"], b["stderr"])
        else:
            ensure(not (retained_dir(battery) / f"{ident}.log").exists(), f"{ident}: a RED entry was retained")
    for w in range(workers):
        ensure(re.search(rf"^  \[w{w}\] shard {w}: ", battery.console, re.M), f"worker {w}'s summary line is lost")
    ensure("\\xbbbinary started \\xc5" in battery.console, "the escaped child diagnostic is not on the console")
    ensure("\nmutations: 4 RED / 5 unusable\n" in battery.console, "the tally is not 4 RED / 5 unusable")
    ensure("RUN INTEGRITY FAILURE" not in battery.console, "an integrity failure on an ordinary run")
    ensure(battery.rc == 1, f"an ordinary mixed-verdict run exits 1, never 9 (got {battery.rc})")
    check_cleanup(battery)
    return [line for line in battery.console.splitlines() if line.startswith(("  ok   F", "  UNUSABLE F"))]


def check_clean_dies(source, battery, workers):
    """A9.3: a clean program that prints a success summary and then dies — every worker refuses (exit 2) and the
    parent retains each worker's baseline evidence."""
    no_traceback(battery)
    ensure(battery.rc == 2, f"a unanimous baseline refusal keeps exit 2 (got {battery.rc})")
    for w in range(workers):
        ensure(f"  [w{w}]   ABORT the clean tree does not build / did not run — no mutation was applied\n"
               in battery.console, f"worker {w} did not refuse")
        ensure(f"  [w{w}]         phase run: exit -11 / killed by signal 11\n" in battery.console,
               f"worker {w}'s refusal does not name the phase, exit and signal")
        ensure(f"  REFUSED worker {w}'s clean baseline (no-verdict) — phase run, exit -11 / killed by signal 11\n"
               in battery.console, f"the merge does not report worker {w}'s refusal")
        check_retained(battery, f"baseline-w{w}", f"baseline-w{w} clean baseline of worker {w}", w, "run", -11,
                       CLEAN_DIES["stdout"], CLEAN_DIES["stderr"])
    check_cleanup(battery)


def check_corrupted_transfer(source, battery):
    """A9.7: a shipped stream that differs from its declaration is a NAMED integrity failure (exit 9), never retained."""
    no_traceback(battery)
    ensure(battery.rc == 9, f"a corrupted transfer exits 9 (got {battery.rc})")
    ensure("RUN INTEGRITY FAILURE — this run's verdicts may NOT be read as a battery result" in battery.console,
           "no integrity block")
    ensure(re.search(r"##    F01 \(worker 0\): its shipped stderr does not match the worker's declaration \(declared "
                     r"(\d+) B sha256 [0-9a-f]{64}, received (\d+) B sha256 [0-9a-f]{64}\) — NOT retained",
                     battery.console), "the integrity block does not name the corrupted transfer")
    ensure("retained: " not in battery.console, "retention was claimed for a corrupted transfer")
    ensure(not (retained_dir(battery) / "F01.stderr").exists(), "a corrupted transfer was retained")
    ensure("MR_MUT_KEEP_SCRATCH=1" in battery.console, "no rerun hint")
    check_cleanup(battery)


def check_failed_write(source, battery, rc_expected, who):
    """A9.7: a failed retention write is handled — the named diagnostic with the failed path, the record of what
    remains, no traceback, the expected exit, and no claimed retention."""
    no_traceback(battery)
    ensure(battery.rc == rc_expected, f"expected exit {rc_expected}, got {battery.rc}")
    ensure("RUN INTEGRITY FAILURE — this run's verdicts may NOT be read as a battery result" in battery.console,
           "no integrity block")
    blocked = retained_dir(battery)
    for ident in who:
        ensure(f"##    {ident}: its evidence was NOT retained ({blocked}/{ident}.log: " in battery.console,
               f"{ident}: the integrity block does not name the failed path")
        ensure(f"        NOT RETAINED — {blocked}/{ident}.log: " in battery.console, f"{ident}: no record")
        ensure(re.search(rf"^        {re.escape(ident)}: phase run, exit -?\d+", battery.console, re.M),
               f"{ident}: the record lacks phase/exit")
        ensure(re.search(r"^        stdout: declared (\d+) B sha256 ([0-9a-f]{64}); recomputed \1 B sha256 \2$",
                         battery.console, re.M), f"{ident}: the record lacks the stream digests")
    ensure("bounded view (escaped; the last 65536 raw bytes of stdout + stderr):" in battery.console, "no view")
    ensure("retained: " not in battery.console, "retention was claimed after the write failed")
    ensure(blocked.is_file(), "the obstruction was replaced")
    check_cleanup(battery)


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
                    if line.startswith("        ") and "retained:" not in line and "raw streams:" not in line]
        self.assertEqual(excerpts, [f"error: fabricated build failure {i}" for i in (7, 19, 31)] +
                         [f"binary output {i:02d}" for i in range(18, 30)])
        paths = [Path(p) for p in re.findall(r"^        retained: (.+)$", result.stdout, re.M)]
        self.assertEqual(paths, [ROOT / ".pio/mutation-unusable/selftest" / (n + ".log")
                                 for n in ("build", "run")])
        build_lines = [f"build noise {i:02d}" for i in range(40)]
        for i in (7, 19, 31):
            build_lines.insert(i, f"error: fabricated build failure {i}")
        texts = [("\n".join(build_lines) + "\n").encode(), "".join(f"binary output {i:02d}\n" for i in range(30)).encode()]
        for path, text in zip(paths, texts):
            self.assertEqual(path.read_bytes(), text)
            self.assertEqual(path.with_suffix(".stdout").read_bytes() + path.with_suffix(".stderr").read_bytes(), text)
            self.assertNotEqual(path.with_suffix(".stderr").read_bytes(), b"", "the streams are retained separately")

    def control(self, old, new):
        source = harness_source()
        self.assertEqual(source.count(old), 1, "control must match one real site")
        with tempfile.TemporaryDirectory(prefix="mr-unusable-control-") as tmp:
            harness = Path(tmp) / "tools" / HARNESS.name
            harness.parent.mkdir()
            harness.write_text(source.replace(old, new), encoding="utf-8")
            result = self.run_selftest(harness)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("SELFTEST FAILED", result.stdout)
            self.assertNotIn("SELFTEST OK", result.stdout)

    def test_collapsed_arm_control_is_red(self):
        self.control('if reason == "build":', 'if True:')

    def test_deleted_retention_control_is_red(self):
        self.control('path.write_text(view, encoding="ascii")', 'pass  # retained write deleted')

    def test_merged_label_is_absent(self):
        self.assertNotIn("does not compile / did not run", harness_source())

    def suite_runner(self, outcomes):
        # Compile the actual run_suite definition with a subprocess double. Never import the harness's
        # top-level orchestrator, and never reproduce its decisions in a test-side implementation.
        process = Mock(side_effect=outcomes)
        return harness_namespace(harness_source(), ROOT, process)["run_suite"], process

    def test_build_failure_cannot_run_a_stale_binary(self):
        for rc, stdout, stderr in [(1, b"pio stopped\n", b"Error: tool unavailable\n"),
                                   (0, b"build output\n", b"error: compilation failed\n"),
                                   (0, b"error: in stdout\n", b"")]:
            with self.subTest(rc=rc, stdout=stdout):
                run, process = self.suite_runner([subprocess.CompletedProcess([], rc, stdout, stderr)])
                result, failure = run()
                self.assertIsNone(result)
                self.assertEqual((failure.reason, failure.rc, failure.stdout, failure.stderr),
                                 ("build", rc, stdout, stderr))
                self.assertEqual(process.call_count, 1)

    def test_run_failure_retains_code_and_both_full_streams(self):
        stdout, stderr = b"binary stdout\n" * 100, b"binary stderr\n" * 100
        run, process = self.suite_runner([subprocess.CompletedProcess([], 0, b"", b""),
                                         subprocess.CompletedProcess([], -11, stdout, stderr)])
        result, failure = run()
        self.assertIsNone(result)
        self.assertEqual((failure.reason, failure.rc, failure.stdout, failure.stderr), ("run", -11, stdout, stderr))
        self.assertEqual(process.call_count, 2)

    def test_existing_doctest_verdict_is_unchanged(self):
        stdout = b"test cases: 2950 | 2949 passed | 1 failed\nassertions: 195770 | 195767 passed | 3 failed\n"
        run, process = self.suite_runner([subprocess.CompletedProcess([], 0, b"", b""),
                                         subprocess.CompletedProcess([], 1, stdout, b"stderr diagnostic")])
        result, out = run()
        self.assertEqual(result, (3, 2950, 195770))
        self.assertEqual((out.rc, out.stdout, out.stderr), (1, stdout, b"stderr diagnostic"))
        self.assertEqual(process.call_count, 2)
        # B436: the first command must only build; a failing mutant's verdict comes from the binary.
        self.assertEqual(process.call_args_list[0].args[0],
                         ["pio", "test", "-e", "native", "--without-testing"])
        self.assertEqual(process.call_args_list[1].args[0],
                         [str(ROOT / ".pio/build/native/program")])
        for call in process.call_args_list:
            self.assertFalse({"text", "encoding", "errors", "universal_newlines"} & set(call.kwargs), call.kwargs)

    # ---- A9.1 -------------------------------------------------------------------------------------------------
    def test_precheck_scenarios_through_real_children(self):
        with tempfile.TemporaryDirectory(prefix="mr-b478-cases-") as tmp:
            for case in PRECHECK:
                with self.subTest(case=case.name):
                    check_case(harness_source(), tmp, case)

    def test_exit_agreement_cases_through_real_children(self):
        with tempfile.TemporaryDirectory(prefix="mr-b490-cases-") as tmp:
            for case in AGREEMENT:
                with self.subTest(case=case.name):
                    check_case(harness_source(), tmp, case)

    def test_escape_rule_is_total_and_lossless(self):
        ns = harness_namespace(harness_source(), ROOT, None)
        self.assertEqual(ns["_VIEW_BYTES"], VIEW_BYTES)
        every = bytes(range(256))
        self.assertEqual(unescape(ns["escape_child_bytes"](every)), every)
        self.assertEqual(ns["escape_child_bytes"](b"a\\b\tc\n\r\x00\x7f\x80\xff"),
                         "a\\\\b\tc\n\\x0d\\x00\\x7f\\x80\\xff")
        self.assertEqual(ns["escape_child_bytes"](b"ab\xff", 3), "ab", "a bound never splits an escape")
        self.assertEqual(ns["child_lines"](b"a\r\nb\n\nc\n"), [b"a\r", b"b", b"", b"c"], "line feeds only")

    # ---- A9.2 -------------------------------------------------------------------------------------------------
    def test_size_case_retains_every_raw_byte(self):
        with tempfile.TemporaryDirectory(prefix="mr-b478-size-") as tmp:
            check_size(harness_source(), tmp)

    # ---- A9.3 / A9.4 ------------------------------------------------------------------------------------------
    def test_end_to_end_mixed_battery_on_two_workers_and_on_one(self):
        source = harness_source()
        two = check_mixed(source, run_battery(self, source, MIXED, 2), 2)
        one = check_mixed(source, run_battery(self, source, MIXED, 1), 1)
        self.assertEqual(two, one, "the RED/UNUSABLE identities differ between the worker counts")

    def test_end_to_end_clean_tree_dying_after_its_summary_is_refused_and_retained(self):
        source = harness_source()
        check_clean_dies(source, run_battery(self, source, {}, 2, clean=CLEAN_DIES), 2)

    def test_outer_pipe_survives_a_parent_that_decodes_by_an_ascii_locale(self):
        source = harness_source()
        check_mixed(source, run_battery(self, source, MIXED, 2, parent_c_locale=True), 2)

    # ---- A9.7 -------------------------------------------------------------------------------------------------
    def test_integrity_corrupted_transfer_exits_9(self):
        source = edited(harness_source(), TRANSFER_FAULT)
        check_corrupted_transfer(source, run_battery(self, source, FAULT, 2))

    def test_integrity_failed_write_exits_9(self):
        source = harness_source()
        check_failed_write(source, run_battery(self, source, FAULT, 2, obstruct=True), 9, ["F01"])

    def test_integrity_unanimous_refusal_with_failed_write_keeps_exit_2(self):
        source = harness_source()
        battery = run_battery(self, source, {}, 2, clean=CLEAN_DIES, obstruct=True)
        check_failed_write(source, battery, 2, ["baseline-w0", "baseline-w1"])

    # ---- A9.5 — controls: each exact edit must make its named regression fail ----------------------------------
    def test_control_a_c1_strict_native_capture_fails_the_raw_byte_cases(self):
        source = edited(harness_source(), (SITE_NATIVE_CAPTURE, SITE_NATIVE_CAPTURE[:-1] + ", text=True)"))
        with tempfile.TemporaryDirectory(prefix="mr-b478-c1-") as tmp:
            for case in PRECHECK:
                if case.defect == "B478" and case.phase == "run":
                    with self.subTest(case=case.name):
                        with self.assertRaises(UnicodeDecodeError):
                            check_case(source, tmp, case)

    def test_control_a_c2_exit_agreement_removed_fails_the_b490_cases(self):
        source = edited(harness_source(), (SITE_AGREEMENT, "    if True:"))
        with tempfile.TemporaryDirectory(prefix="mr-b490-c2-") as tmp:
            for case in PRECHECK + AGREEMENT:
                if case.defect == "B490":
                    with self.subTest(case=case.name):
                        with self.assertRaisesRegex(AssertionError, "CREDITED verdict"):
                            check_case(source, tmp, case)

    def test_control_a_c3_raw_write_removed_fails_end_to_end_retention(self):
        source = edited(harness_source(), (SITE_RAW_WRITE, "pass  # control A-C3: the raw-stream write removed"))
        with self.assertRaises(AssertionError):
            check_mixed(source, run_battery(self, source, MIXED, 2), 2)

    def test_control_a_c4_raw_retention_cut_to_the_view_fails_the_size_case(self):
        source = edited(harness_source(), (SITE_RAW_WRITE, SITE_RAW_WRITE[:-1] + "[-_VIEW_BYTES:])"))
        with tempfile.TemporaryDirectory(prefix="mr-b478-c4-") as tmp:
            with self.assertRaises(AssertionError):
                check_size(source, tmp)

    def test_control_a_c5_text_mode_pump_fails_only_the_outer_pipe(self):
        source = edited(harness_source(), (SITE_POPEN, SITE_POPEN[:-1] + ", text=True, bufsize=1); "
                                           "p.stdout = (x.encode('utf-8') for x in p.stdout)"))
        check_mixed(source, run_battery(self, source, MIXED, 2), 2)       # discrimination: a UTF-8 parent passes
        with self.assertRaises(AssertionError):
            check_mixed(source, run_battery(self, source, MIXED, 2, parent_c_locale=True), 2)

    def test_control_a_c6_comparison_removed_fails_the_corrupted_transfer(self):
        source = edited(harness_source(), TRANSFER_FAULT, (SITE_COMPARE, "if False:  # control A-C6"))
        with self.assertRaises(AssertionError):
            check_corrupted_transfer(source, run_battery(self, source, FAULT, 2))

    def test_control_a_c7_write_handling_removed_fails_the_failed_write(self):
        source = edited(harness_source(), (SITE_WRITE_HANDLING, "except ZeroDivisionError as error:  # A-C7"))
        with self.assertRaises(AssertionError):
            check_failed_write(source, run_battery(self, source, FAULT, 2, obstruct=True), 9, ["F01"])


if __name__ == "__main__":
    unittest.main()
