# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §4.1 — capture every B456 self-test CASE's exit status and output (the increment-1 refactor proof).
# Runs the stock `tools/test_probe_firmware_ui.py` suite unchanged, with its `run_case` wrapped so that every call it
# makes is recorded: (test, call#, runner kind, no-neg, rc, output). The runner kind is `stock` or `copy` (the bypass
# regression's temporary copy); paths never enter the record. The suite's own verdict is recorded too.
# Usage: python3 capture_b456.py <out.json>
import importlib.util, io, json, os, sys, unittest
sys.dont_write_bytecode = True
R = '/home/staszek/MeshRoute'
spec = importlib.util.spec_from_file_location('b456tests', os.path.join(R, 'tools', 'test_probe_firmware_ui.py'))
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
CAPTURED = []
orig = mod.AccountingSelftest.run_case
def shim(self, events, expected=None, calls=None, no_neg=False, runner=mod.RUNNER):
    rc, out = orig(self, events, expected=expected, calls=calls, no_neg=no_neg, runner=runner)
    CAPTURED.append({'test': self.id().rsplit('.', 1)[-1], 'runner': 'stock' if runner == mod.RUNNER else 'copy',
                     'no_neg': no_neg, 'rc': rc, 'out': out})
    return rc, out
mod.AccountingSelftest.run_case = shim
suite = unittest.defaultTestLoader.loadTestsFromTestCase(mod.AccountingSelftest)
buf = io.StringIO()
res = unittest.TextTestRunner(stream=buf, verbosity=2).run(suite)
per_test = {}
for c in CAPTURED:
    n = per_test.get(c['test'], 0) + 1; per_test[c['test']] = n; c['call'] = n
out = {'tests_run': res.testsRun, 'failures': len(res.failures), 'errors': len(res.errors), 'skipped': len(res.skipped),
       'ok': res.wasSuccessful(), 'n_calls': len(CAPTURED), 'calls': CAPTURED, 'runner_log': buf.getvalue()}
json.dump(out, open(sys.argv[1], 'w'), indent=1, ensure_ascii=False)
print(f"B456 suite: ran {res.testsRun}, failures {len(res.failures)}, errors {len(res.errors)}, skipped {len(res.skipped)}, "
      f"ok {res.wasSuccessful()}; {len(CAPTURED)} run_case calls captured across {len(per_test)} tests")
