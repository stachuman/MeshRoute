from pathlib import Path
import ast,subprocess,json,time,hashlib,sys
q=Path('/tmp/mr-s8ac-r4-b1edsao2');r=q/'gate';shared=Path('/home/staszek/MeshRoute');out=q/'union';out.mkdir(exist_ok=True)
v={};table={}
for n in ast.parse((r/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(n,ast.Assign):
  for t in n.targets:
   if isinstance(t,ast.Name):
    try:v[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):
     if t.id=='MUTS_BY_TARGET':table={ast.literal_eval(k):x.id for k,x in zip(n.value.keys,n.value.values)}
historical=json.loads((r/'docs/superpowers/evidence/2026-09-16-radmin-slice7b3-qa/union/summary.json').read_text());H=sorted(t for t in historical if not t.startswith('_'))
changed=set(subprocess.check_output(['git','diff','--name-only','6086152'],cwd=shared).decode().splitlines())|set(subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=shared).decode().splitlines())
S=sorted(t for t,p in v['TARGET_SRC'].items() if p in changed);union=sorted(set(S)|set(H))
rows=[]
for t in union:
 src=(r/v['TARGET_SRC'][t]).read_text();m=v[table[t]]
 for label,old,new in m:assert src.count(old)==1,(t,label,src.count(old))
 rows.append(dict(target=t,path=v['TARGET_SRC'][t],configured=len(m),sha256=hashlib.sha256(src.encode()).hexdigest()))
audit=dict(S=S,H=H,union=union,configured=sum(x['configured'] for x in rows),rows=rows)
(out/'selectors.json').write_text(json.dumps(audit,indent=2)+'\n');assert len(H)==56 and sum(historical[t]['configured'] for t in H)==880
paths=set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=shared).decode().split('\0'))-{''}
frozen={p:hashlib.sha256((r/p).read_bytes()).hexdigest() for p in paths if (r/p).is_file() and (p.startswith(('lib/','test/')) or p.startswith('src/') and p.endswith('.h') or p in ['platformio.ini','tools/probe_ui_model_mutations.py'])}
(out/'native-inputs.json').write_text(json.dumps(frozen,indent=2)+'\n');results=[]
priority=['radmin8client','radmin8verbs','radmin8rng','radmin1b','radmin5rx','radmin7rx']
priority=[t for t in priority if t in union]
for target in priority+[t for t in union if t not in priority]:
 cmd=['python3','tools/probe_ui_model_mutations.py','--target='+target,'--workers=3'];print('START',target,flush=True);start=time.monotonic();log=out/(target+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(target=target,command=cmd,exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',target,rc,flush=True)
 if rc:raise SystemExit(rc)
assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==h for p,h in frozen.items())
print('FULL UNION FINISHED',len(union),audit['configured'],flush=True)
