"""Synthetic shell-plumbing proof of the current runner's verdict tail; no firmware/control execution."""
from pathlib import Path
import hashlib,json,re,subprocess
root=Path.cwd();raw=root/'artifacts/2026-09-27-standalone-mobile-home-w4a-qa';runner=root/'tools/probe_firmware_ui/run.sh';s=runner.read_text();tail=s[s.rindex('echo "controls: $n_ctl verified / $n_bad unusable"'):];assert 'exit $rc' in tail
prefix='set -uo pipefail\nrc=0; n_ctl=235; n_bad=0; b227_calls=236\nctl() { n_ctl=$((n_ctl+1)); }\n'
answers={}
for name,gate in [('last_control_runs','true'),('false_guard_skips','false'),('missing_guard_skips','qa_missing_guard_function')]:
 for guard in (False,True):
  program=prefix+gate+' && ctl\n'+('if [ "$((n_ctl+n_bad))" -ne "$b227_calls" ]; then echo "FAIL missing control verdict"; rc=1; fi\n' if guard else '')+tail
  p=subprocess.run(['bash','-c',program],text=True,capture_output=True);key=name+('-proposed-completeness-guard' if guard else '-stock-tail');answers[key]=dict(exit=p.returncode,stdout=p.stdout,stderr=p.stderr)
  expected=1 if guard and name!='last_control_runs' else 0;assert p.returncode==expected
invalid=root/'artifacts/2026-09-26-standalone-mobile-home-w4a/invalid-chain-1/gate5-fwui-default.log';h=hashlib.sha256(invalid.read_bytes()).hexdigest();assert h=='c71da8a209a891459c8940f753ea79f0feea5b3bcc92afd08c5622ae6be49e99';text=invalid.read_text(errors="replace")
result=dict(scope='Synthetic control dispatch plus exact current stock verdict tail, not a full probe replay; proposed guard exists only in this evidence',runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),tail=tail,runs=answers,coder_invalid_archive=dict(path=str(invalid),sha256=h,missing_helper_errors=text.count('once: command not found'),counts=re.findall(r'controls: \d+ verified / \d+ unusable',text),last_line=text.strip().splitlines()[-1]))
(raw/'control-completeness-proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
