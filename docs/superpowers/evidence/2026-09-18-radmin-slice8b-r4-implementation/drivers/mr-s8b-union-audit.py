from pathlib import Path
import ast,subprocess,json,time,hashlib,sys
q=Path('/tmp/mr-s8b-r4-_d7gpj1e');r=q/'final-gate';shared=Path('/home/staszek/MeshRoute');out=q/'final-union';out.mkdir(exist_ok=True)
v={};table={}
for n in ast.parse((r/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(n,ast.Assign):
  for t in n.targets:
   if isinstance(t,ast.Name):
    try:v[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):
     if t.id=='MUTS_BY_TARGET':table={ast.literal_eval(k):x.id for k,x in zip(n.value.keys,n.value.values)}
historical=json.loads((r/'docs/superpowers/evidence/2026-09-18-radmin-slice8ac-r4-implementation/union/selectors.json').read_text());H=historical['union']
changed=set(subprocess.check_output(['git','diff','--name-only','c07b77f'],cwd=shared).decode().splitlines())|set(subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=shared).decode().splitlines())
S=sorted(t for t,p in v['TARGET_SRC'].items() if p in changed);union=sorted(set(S)|set(H))
rows=[]
for t in union:
 src=(r/v['TARGET_SRC'][t]).read_text();m=v[table[t]]
 for label,old,new in m:assert src.count(old)==1,(t,label,src.count(old))
 rows.append(dict(target=t,path=v['TARGET_SRC'][t],configured=len(m),sha256=hashlib.sha256(src.encode()).hexdigest()))
audit=dict(S=S,H=H,union=union,configured=sum(x['configured'] for x in rows),rows=rows)
(out/'selectors.json').write_text(json.dumps(audit,indent=2)+'\n');assert len(H)==59 and historical['configured']==918
paths=set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=shared).decode().split('\0'))-{''}
frozen={p:hashlib.sha256((r/p).read_bytes()).hexdigest() for p in paths if (r/p).is_file() and (p.startswith(('lib/','test/')) or p.startswith('src/') and p.endswith('.h') or p in ['platformio.ini','tools/probe_ui_model_mutations.py'])}
(out/'native-inputs.json').write_text(json.dumps(frozen,indent=2)+'\n');results=[]
print('AUDIT',len(union),audit['configured'],flush=True)
