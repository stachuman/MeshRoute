from pathlib import Path
import subprocess,json,time,hashlib,concurrent.futures,ast
q=Path(Path('/tmp/mr-s10-r2-active').read_text().strip());r=q/'union-inputs';out=q/'union';out.mkdir(exist_ok=True)
audit=json.loads((q/'selectors.json').read_text());(out/'selectors.json').write_text(json.dumps(audit,indent=2))
inputs=json.loads((q/'union-inputs-inputs.json').read_text());frozen={p:v['sha256'] for p,v in inputs.items() if p.startswith(('src/','lib/','test/','tools/')) and 'sha256' in v}
(out/'native-inputs.json').write_text(json.dumps(frozen,indent=2)+'\n')
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
results=[]
def run(target):
 cmd=['python3','tools/probe_ui_model_mutations.py','--target='+target,'--workers=3'];print('START',target,flush=True);start=time.monotonic();log=out/(target+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 return dict(target=target,command=cmd,exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest())
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 for future in concurrent.futures.as_completed([pool.submit(run,t) for t in audit['union']]):
  res=future.result();results.append(res);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',res['target'],res['exit'],flush=True)
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
assert all(r['exit']==(1 if r['target']=='sliceBmac' else 0) for r in results),[(r['target'],r['exit']) for r in results if r['exit']]
print('FULL UNION FINISHED',len(audit['union']),audit['configured'],flush=True)
