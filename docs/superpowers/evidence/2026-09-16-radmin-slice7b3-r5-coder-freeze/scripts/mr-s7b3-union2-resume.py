from pathlib import Path
import subprocess,json,time,hashlib,re
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';out=q/'union2';audit=json.loads((out/'selectors.json').read_text());frozen=json.loads((out/'native-inputs.json').read_text());results=json.loads((out/'results.json').read_text())
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
def acceptable(row):
 if row['exit']==0:return True
 s=(out/(row['target']+'.log')).read_text();merged=s.split('-- MERGED REPORT — target ',1)[-1]
 return row['target']=='sliceBmac' and row['exit']==1 and re.findall(r'^  FAIL (\w+)',merged,re.M)==['M04'] and len(re.findall(r'-> RED \(\d+ assertion\(s\) failed, match count 1\)',merged))+1==next(x['configured'] for x in audit['rows'] if x['target']==row['target']) and 'real tree untouched: all 58 target files byte-identical' in s
assert all(acceptable(row) for row in results),[(x['target'],x['exit']) for x in results if not acceptable(x)]
completed={x['target'] for x in results};remaining=[t for t in audit['union'] if t not in completed];print('RESUME',len(completed),'completed;',len(remaining),'remaining',flush=True)
for target in remaining:
 cmd=['python3','tools/probe_ui_model_mutations.py','--target='+target,'--workers=3'];print('START',target,flush=True);start=time.monotonic();log=out/(target+'.log');assert not log.exists()
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 row=dict(target=target,command=cmd,exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest());results.append(row);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',target,rc,flush=True)
 if not acceptable(row):raise SystemExit(rc or 2)
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items());print('FULL UNION FINISHED',len(results),audit['configured'],flush=True)
