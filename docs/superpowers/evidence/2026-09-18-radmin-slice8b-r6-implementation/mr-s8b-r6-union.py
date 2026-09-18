from pathlib import Path
import subprocess,json,time,hashlib,concurrent.futures,shutil
q=Path('/tmp/mr-s8b-r6-nwy4hz9d');r=q/'union-inputs';out=q/'final-union';audit=json.loads((out/'selectors.json').read_text());frozen=json.loads((out/'native-inputs.json').read_text())
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
assert not (out/'results.json').exists(), 'fresh run required'
results=[]; done=set()
remaining=[t for t in audit['union'] if t not in done]
for t in remaining:
 p=out/(t+'.log')
 if p.exists():shutil.copy2(p,out/(t+'-interrupted.log'))
def run(target):
 cmd=['python3','tools/probe_ui_model_mutations.py','--target='+target,'--workers=3'];print('START',target,flush=True);start=time.monotonic();log=out/(target+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 return dict(target=target,command=cmd,exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest())
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 for future in concurrent.futures.as_completed([pool.submit(run,t) for t in remaining]):
  res=future.result()
  results.append(res);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',res['target'],res['exit'],flush=True)
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
assert not [r for r in results if r['exit']],[(r['target'],r['exit']) for r in results if r['exit']]
print('FULL UNION FINISHED',len(audit['union']),audit['configured'],flush=True)
