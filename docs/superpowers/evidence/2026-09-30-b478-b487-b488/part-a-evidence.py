# Author: coder (B478/B487/B488/B490 package) — Part A evidence: controls, integrity faults, fails-before (A9.5–A9.7)
"""Usage: python3 -B part-a-evidence.py <base-harness.py> <out.json> <console-dir>

Drives the REAL regression functions of tools/test_mutation_unusable_reason.py (imported; it never imports the
harness) and records, for each control and fault, the observed outcome — exception class and message, exit code,
the integrity lines. The full parent consoles go to <console-dir> (ignored artifacts); every figure is in <out.json>.

A9.6 (fails before): the new scenario cases run against the BASE harness text (b71270ee…): each B478 case must fail
by a decoder exception and each B490 case by a credited verdict. The base harness's own end-to-end runs are recorded
too (mixed default-locale, mixed with an ASCII-locale parent)."""
import hashlib, json, re, sys, tempfile, unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import test_mutation_unusable_reason as T  # noqa: E402 — the regression module; it never imports the harness

base_path, out_path, consoles = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
consoles.mkdir(parents=True, exist_ok=True)
base = base_path.read_text(encoding="utf-8")
real = T.harness_source()
holder = unittest.TestCase()
record = {"harness_sha256": hashlib.sha256(real.encode()).hexdigest(),
          "base_sha256": hashlib.sha256(base.encode()).hexdigest()}


def outcome(fn):
    try:
        fn()
    except T.HarnessDidNotRun as e:
        return {"outcome": "VACUOUS", "exception": "HarnessDidNotRun", "message": str(e)[:400]}
    except Exception as e:  # the observed class is the evidence
        return {"outcome": "FAILED", "exception": type(e).__name__, "message": str(e)[:400]}
    return {"outcome": "PASSED"}


def battery(name, source, plan, workers, **kw):
    b = T.run_battery(holder, source, plan, workers, **kw)
    (consoles / f"{name}.console").write_text(b.console + "\n---- stderr ----\n" + b.stderr, encoding="utf-8")
    integrity = [ln.strip() for ln in b.console.splitlines() if ln.startswith("  ##    ")]
    return b, {"exit": b.rc, "traceback": "Traceback" in b.console + b.stderr,
               "integrity_lines": integrity, "retained_claims": b.console.count("        retained: "),
               "console": f"{name}.console"}


tmp = tempfile.mkdtemp(prefix="mr-b478-evidence-")

# ---- A9.6 fails before: the scenario cases on the base harness ------------------------------------------------
fails_before = []
for case in T.PRECHECK + T.AGREEMENT:
    got = outcome(lambda: T.check_case(base, tmp, case, required=False))
    want = {"B478": "UnicodeDecodeError", "B490": "AssertionError"}.get(case.defect)
    ok = want is None or (got.get("exception") == want and (want != "AssertionError" or
                                                             "CREDITED verdict" in got.get("message", "")))
    fails_before.append(dict(case=case.name, defect=case.defect or None, base=got,
                             required=want and (want + (" (credited verdict)" if want == "AssertionError" else "")),
                             as_required=ok))
record["fails_before_cases"] = fails_before
_b, record["fails_before_e2e_mixed_default_locale"] = battery("base-mixed", base, T.MIXED, 2)
_b, record["fails_before_e2e_mixed_ascii_parent"] = battery("base-mixed-ascii-parent", base, T.MIXED, 2,
                                                            parent_c_locale=True)
# The pipe alone: an all-RED plan with ASCII-only child bytes, so the base worker cannot crash on its own capture.
ORDINARY = {i: dict(stdout=T.summary(5, 1, 50, 20 + i), stderr=b"", rc=1) for i in range(9)}
_b, record["fails_before_pipe_only_default_locale"] = battery("base-ordinary", base, ORDINARY, 2)
_b, record["fails_before_pipe_only_ascii_parent"] = battery("base-ordinary-ascii-parent", base, ORDINARY, 2,
                                                            parent_c_locale=True)
_b, record["after_pipe_only_ascii_parent"] = battery("after-ordinary-ascii-parent", real, ORDINARY, 2,
                                                     parent_c_locale=True)

# ---- the post-fix runs the controls are measured against ---------------------------------------------------------
runs = {}
for name, kw in [("mixed-w2", dict(plan=T.MIXED, workers=2)), ("mixed-w1", dict(plan=T.MIXED, workers=1)),
                 ("clean-dies-w2", dict(plan={}, workers=2, clean=T.CLEAN_DIES)),
                 ("outer-pipe-ascii-parent", dict(plan=T.MIXED, workers=2, parent_c_locale=True))]:
    b, runs[name] = battery(name, real, **kw)
    check = {"mixed-w2": lambda: T.check_mixed(real, b, 2), "mixed-w1": lambda: T.check_mixed(real, b, 1),
             "clean-dies-w2": lambda: T.check_clean_dies(real, b, 2),
             "outer-pipe-ascii-parent": lambda: T.check_mixed(real, b, 2)}[name]
    runs[name]["regression"] = outcome(check)
record["runs"] = runs

# ---- A9.7 integrity faults -----------------------------------------------------------------------------------------
faults = {}
fault_src = T.edited(real, T.TRANSFER_FAULT)
b, faults["corrupted-transfer"] = battery("fault-corrupted-transfer", fault_src, T.FAULT, 2)
faults["corrupted-transfer"]["regression"] = outcome(lambda: T.check_corrupted_transfer(fault_src, b))
b, faults["failed-write"] = battery("fault-failed-write", real, T.FAULT, 2, obstruct=True)
faults["failed-write"]["regression"] = outcome(lambda: T.check_failed_write(real, b, 9, ["F01"]))
b, faults["unanimous-refusal-failed-write"] = battery("fault-unanimous-refusal", real, {}, 2, clean=T.CLEAN_DIES,
                                                      obstruct=True)
faults["unanimous-refusal-failed-write"]["regression"] = outcome(
    lambda: T.check_failed_write(real, b, 2, ["baseline-w0", "baseline-w1"]))
record["integrity_faults"] = faults

# ---- A9.5 controls ---------------------------------------------------------------------------------------------------
controls = {}
src = T.edited(real, (T.SITE_NATIVE_CAPTURE, T.SITE_NATIVE_CAPTURE[:-1] + ", text=True)"))
controls["A-C1 strict decoding restored at the native-run capture"] = {
    "class": "function level", "required": "each raw-byte run-phase case fails by a decoder exception",
    "cases": {c.name: outcome(lambda: T.check_case(src, tmp, c)) for c in T.PRECHECK
              if c.defect == "B478" and c.phase == "run"}}
src = T.edited(real, (T.SITE_AGREEMENT, "    if True:"))
controls["A-C2 exit agreement removed"] = {
    "class": "function level", "required": "each B490 case fails by a credited verdict",
    "cases": {c.name: outcome(lambda: T.check_case(src, tmp, c)) for c in T.PRECHECK + T.AGREEMENT
              if c.defect == "B490"}}
src = T.edited(real, (T.SITE_RAW_WRITE, "pass  # control A-C3: the raw-stream write removed"))
b, info = battery("control-a-c3", src, T.MIXED, 2)
controls["A-C3 the parent's raw-stream write removed"] = dict(
    info, **{"class": "end to end", "required": "the end-to-end retention assertions fail"},
    regression=outcome(lambda: T.check_mixed(src, b, 2)))
src = T.edited(real, (T.SITE_RAW_WRITE, T.SITE_RAW_WRITE[:-1] + "[-_VIEW_BYTES:])"))
controls["A-C4 raw retention cut to the bounded view"] = {
    "class": "function level", "required": "the size case fails", "regression": outcome(lambda: T.check_size(src, tmp))}
src = T.edited(real, (T.SITE_POPEN, T.SITE_POPEN[:-1] + ", text=True, bufsize=1); "
                                    "p.stdout = (x.encode('utf-8') for x in p.stdout)"))
b1, info1 = battery("control-a-c5-utf8-parent", src, T.MIXED, 2)
b2, info2 = battery("control-a-c5-ascii-parent", src, T.MIXED, 2, parent_c_locale=True)
controls["A-C5 a text-mode pump restored"] = {
    "class": "end to end", "required": "the outer-pipe case fails (and the same copy passes with a UTF-8 parent)",
    "utf8_parent": dict(info1, regression=outcome(lambda: T.check_mixed(src, b1, 2))),
    "ascii_parent": dict(info2, regression=outcome(lambda: T.check_mixed(src, b2, 2)))}
src = T.edited(real, T.TRANSFER_FAULT, (T.SITE_COMPARE, "if False:  # control A-C6"))
b, info = battery("control-a-c6", src, T.FAULT, 2)
controls["A-C6 declared-versus-recomputed comparison removed (with the transfer fault)"] = dict(
    info, **{"class": "end to end", "required": "the corrupted-transfer regression fails"},
    regression=outcome(lambda: T.check_corrupted_transfer(src, b)))
src = T.edited(real, (T.SITE_WRITE_HANDLING, "except ZeroDivisionError as error:  # A-C7"))
b, info = battery("control-a-c7", src, T.FAULT, 2, obstruct=True)
controls["A-C7 the parent's failed-write handling removed"] = dict(
    info, **{"class": "end to end", "required": "the failed-write regression fails"},
    regression=outcome(lambda: T.check_failed_write(src, b, 9, ["F01"])))
for name, (old, new) in {"existing: the collapsed build arm": ('if reason == "build":', 'if True:'),
                         "existing: the deleted .log write": ('path.write_text(view, encoding="ascii")',
                                                              'pass  # retained write deleted')}.items():
    tc = T.MutationUnusableReasonTests("test_merged_label_is_absent")
    controls[name] = {"class": "self-test", "required": "the self-test is RED",
                      "regression": outcome(lambda: tc.control(old, new))}
    # `control` asserts the self-test FAILED; PASSED here means the control produced its required outcome
record["controls"] = controls

out_path.write_text(json.dumps(record, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
bad_before = [x["case"] for x in fails_before if not x["as_required"]]
print("fails-before:", "every B478/B490 case fails for its defect's own reason" if not bad_before else bad_before)
for k, v in runs.items():
    print(f"run {k}: exit {v['exit']} regression {v['regression']['outcome']}")
for k, v in faults.items():
    print(f"fault {k}: exit {v['exit']} traceback {v['traceback']} regression {v['regression']['outcome']}")
for k, v in controls.items():
    if "cases" in v:
        print(f"control {k}: " + ", ".join(f"{c}={o.get('exception', o['outcome'])}" for c, o in v["cases"].items()))
    elif "utf8_parent" in v:
        print(f"control {k}: utf8-parent {v['utf8_parent']['regression']['outcome']}, ascii-parent "
              f"{v['ascii_parent']['regression']['outcome']} ({v['ascii_parent']['regression'].get('message', '')[:80]})")
    else:
        r = v["regression"]
        print(f"control {k}: exit {v.get('exit', '-')} regression {r['outcome']} {r.get('exception', '')}: "
              f"{r.get('message', '')[:100]}")
for k, v in record.items():
    if k.startswith("fails_before_e2e"):
        print(f"{k}: exit {v['exit']} traceback {v['traceback']} retained claims {v['retained_claims']}")
