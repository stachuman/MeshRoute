from pathlib import Path
import json,re,hashlib,collections
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;D=Path(json.loads((O/'scratch.json').read_text())['path'])
now=json.loads((D/'corpus/manifest.json').read_text());old=json.loads((R/'docs/superpowers/evidence/2026-10-03-standalone-mobile-home-w7-precheck/corpus-manifest.json').read_text())
a={x['name']:x for x in old['scenarios']};b={x['name']:x for x in now['scenarios']};assert a==b and len(a)==36
(O/'corpus-manifest.json').write_bytes((D/'corpus/manifest.json').read_bytes())
fw=(O/'firmware-ui.log').read_text(errors='surrogateescape');src=(R/'tools/probe_firmware_ui/run.sh').read_text()
exp=[]
for l in src.splitlines():
 m=re.match(r'^\s*ctl "(.*)" (yes|no) \\$',l)
 if m: exp.append((re.sub(r'\\([\\"$`])',r'\1',m[1]),m[2]))
obs=[]
for l in fw.splitlines():
 m=re.match(r'^  ok   (.+) -> RED \((\d+) check\(s\) failed\)$',l)
 if m:obs.append((m[1],'yes'))
 m=re.match(r'^  ok   (.+) \(build fails, as required\)$',l)
 if m:obs.append((m[1],'no'))
assert len(exp)==src.count('  ctl "') or len(exp)==len(re.findall(r'^\s*ctl "',src,re.M))
assert len(exp)==240 and collections.Counter(exp)==collections.Counter(obs),(len(exp),len(obs),set(exp)-set(obs))
(O/'control-reconciliation.json').write_text(json.dumps(dict(declared=len(exp),observed=len(obs),complete=True,labels=[dict(label=x,contract=y) for x,y in exp]),indent=2)+'\n')
fwchecks=re.findall(r'checks:\s*(\d+) passed / 0 failed',fw)
if not fwchecks: fwchecks=re.findall(r'checks.*',fw)
abi=(O/'abi.log').read_text();board=(O/'board-ui.log').read_text();nat=(O/'native-binary.log').read_text()
summary=dict(native=dict(cases=3059,assertions=199638,failed=0,skipped=0),firmware_ui=dict(arms=[int(x) for x in re.findall(r'(\d+) passed / 0 failed / \d+ total',fw)[:3]],check_summary_lines=[l for l in fw.splitlines() if 'passed / 0 failed /' in l],controls=240,independent_declared_observed_equal=True),abi=dict(checks=290,controls_red=9),board_ui=dict(identities=592,canvas=[124,110],traits=14,missing_trait=12,structural=23,wiring=60,wiring_controls=186,negctl=[60,3]),corpus=dict(scenarios=36,all_14_scenario_fields_equal=True,s18=b['s18_meshroute'],lus_sha256=now['inputs']['lus_sha256']),inventory=dict(rows=197),runs=json.loads((O/'runs.json').read_text()))
assert '3059 |   3059 passed | 0 failed | 0 skipped' in nat and '199638 | 199638 passed | 0 failed' in nat
assert 'PASS: board ABI (290 checks, 9/9 controls RED, 0 unusable)' in abi
assert 'identities accounted: 592 declared, 592 exactly once' in board
(O/'baseline-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k!='runs'},indent=2))
