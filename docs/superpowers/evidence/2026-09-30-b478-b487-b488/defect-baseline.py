# Author: coder (B478/B487/B488/B490 package) — the coder's own defect-baseline recipe (§4.1 baseline step 5)
"""Usage: python3 -B defect-baseline.py <harness.py> <decode-results.json> <out.json>

Feeds the pre-check's 13 scenarios — the exact bytes and exits recorded in decode-results.json — through REAL child
processes into the harness's own SuiteFailure/run_suite/unusable_verdict, extracted by AST (the harness is never
imported: its top level starts a battery). Only the build response is stubbed for run-phase cases; a build-phase case
runs the real child in the build slot. Exits 0 when every result, exception class and detail, capture and call count
reproduces decode-results.json, and when the 64-KiB view measurement reproduces its `retention` block."""
import ast, hashlib, json, os, re, subprocess, sys, tempfile
from pathlib import Path
from types import SimpleNamespace
from typing import NamedTuple

harness, ref_path, out_path = map(Path, sys.argv[1:4])
source = harness.read_bytes()
tree = ast.parse(source)
wanted = ("SuiteFailure", "run_suite", "unusable_verdict")
defs = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in wanted]
assert sorted(n.name for n in defs) == sorted(wanted), [n.name for n in defs]
ref = json.loads(ref_path.read_text())
work = Path(tempfile.mkdtemp(prefix="mr-b478-defect-"))

def child_script(name, stdout, stderr, rc):
    # split_valid_utf8's stdout reaches the pipe in two writes, cutting the 2-byte sequence (as the pre-check did)
    writes = [stdout[:1], stdout[1:]] if name == "split_valid_utf8" else [stdout]
    body = ["import os, signal, resource", "resource.setrlimit(resource.RLIMIT_CORE, (0, 0))"]
    body += [f"os.write(1, {w!r})" for w in writes if w] + ([f"os.write(2, {stderr!r})"] if stderr else [])
    body.append("os.kill(os.getpid(), signal.SIGSEGV)" if rc == -11 else f"raise SystemExit({rc})")
    path = work / f"{name}.py"
    path.write_text("\n".join(body) + "\n")
    return path

rows, mismatches = [], []
for want in ref["results"]:
    name, phase, rc = want["name"], want["phase"], want["child_exit"]
    stdout, stderr = bytes.fromhex(want["stdout_hex"]), bytes.fromhex(want["stderr_hex"])
    child = child_script(name, stdout, stderr, rc)
    ground = subprocess.run([sys.executable, str(child)], capture_output=True)
    assert (ground.stdout, ground.stderr, ground.returncode) == (stdout, stderr, rc), name
    calls = []
    def invoke(cmd, **kw):
        calls.append(cmd)
        if cmd[0] == "pio" and phase != "build":
            return subprocess.CompletedProcess(cmd, 0, "", "")
        kw.pop("cwd", None)
        return subprocess.run([sys.executable, str(child)], **kw)
    ns = dict(NamedTuple=NamedTuple, os=os, re=re, ROOT=str(harness.parent.parent), subprocess=SimpleNamespace(run=invoke))
    exec(compile(ast.Module(body=defs, type_ignores=[]), str(harness), "exec"), ns)
    row = {"name": name, "phase": phase, "child_exit": rc, "stdout_hex": want["stdout_hex"], "stderr_hex": want["stderr_hex"]}
    try:
        result, out = ns["run_suite"]()
        row.update(result=list(result) if result else None,
                   capture=out._asdict() if hasattr(out, "_asdict") else out, exception=None)
    except Exception as e:  # the defect under measurement: strict decoding raises before any verdict
        row.update(exception=type(e).__name__, detail=str(e))
    row["calls"] = len(calls)
    rows.append(row)
    for key in ("result", "capture", "exception", "detail", "calls"):
        if want.get(key) != row.get(key):
            mismatches.append({"name": name, "field": key, "pre_check": want.get(key), "measured": row.get(key)})

# The 64-KiB view: a 3-byte character straddling the cut (the pre-check's input) through the real classifier.
full = "€" + "x" * 65535
_, _, capture = ns["unusable_verdict"]("run", 1, full)
retention = {"input_bytes": len(full.encode()), "nominal_tail_bytes": 65536,
             "actual_retained_bytes": len(capture.encode()),
             "tail_prefix_hex": full.encode()[-65536:-65528].hex(), "retained_prefix_hex": capture.encode()[:8].hex()}
for k, v in retention.items():
    if ref["retention"].get(k) != v:
        mismatches.append({"name": "retention", "field": k, "pre_check": ref["retention"].get(k), "measured": v})

report = {"scope": "coder's own recipe: the 13 pre-check scenarios as real children through the unchanged, AST-extracted run_suite",
          "harness_sha256": hashlib.sha256(source).hexdigest(), "reference": str(ref_path),
          "reference_sha256": hashlib.sha256(ref_path.read_bytes()).hexdigest(),
          "results": rows, "retention": retention, "mismatches": mismatches,
          "verdict": "REPRODUCED" if not mismatches else "DIFFERS"}
out_path.write_text(json.dumps(report, indent=1, ensure_ascii=False) + "\n")
for r in rows:
    print(f"  {r['name']:30s} exit {r['child_exit']:>4}  calls {r['calls']}  "
          + (f"EXCEPTION {r['exception']}" if r["exception"] else f"result {r['result']}"))
print(f"  retention: {retention['actual_retained_bytes']} of {retention['nominal_tail_bytes']} bytes kept")
print(f"defect baseline {report['verdict']} against {ref_path.name} ({len(mismatches)} mismatch(es))")
sys.exit(0 if not mismatches else 1)
