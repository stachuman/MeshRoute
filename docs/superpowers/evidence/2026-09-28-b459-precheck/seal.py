"""Seal pre-check evidence, preserving the committed-plus-uncommitted input inventory."""
from pathlib import Path
import hashlib,json,os,subprocess,re
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');E=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
def entry(p):
 if p.is_symlink():
  t=os.readlink(p);return {'type':'symlink','sha256':sha(t.encode()),'target':t}
 if p.is_file():return {'type':'file','sha256':sha(p.read_bytes())}
 return None
before=json.loads((E/'inputs.json').read_text());pres={}
for key,root in [('meshroute',R),('simulator',S)]:
 inv=before[key];changed={};missing=[]
 for rel,old in inv['files'].items():
  now=entry(root/rel)
  if now is None:missing.append(rel)
  elif old!=now:changed[rel]={'before':old,'after':now}
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
 status=subprocess.check_output(['git','status','--short'],cwd=root,text=True)
 staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root,text=True)
 assert head==inv['head'] and not staged and not missing,(key,head,staged,missing)
 if key=='meshroute':assert set(changed)=={'docs/2026-07-30-open-bug-register.md'},changed
 else:assert not changed and not status,(changed,status)
 files=set(subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'))-{''}
 added=sorted(files-set(inv['files']))
 if key=='meshroute':assert all(p=='docs/superpowers/evidence/2026-09-28-b459-precheck.md' or p.startswith('docs/superpowers/evidence/2026-09-28-b459-precheck/') for p in added),added
 else:assert not added,added
 pres[key]={'head':head,'entry_paths_verified':len(inv['files']),'changed':changed,'missing':missing,'new_paths':added,'staged':'','status':status,'whitespace_exit':subprocess.run(['git','diff','--check'],cwd=root,capture_output=True).returncode}
 assert pres[key]['whitespace_exit']==0
p=R/'docs/2026-07-30-open-bug-register.md';addition=json.loads((E/'register-additions.json').read_text());assert sha(p.read_bytes())==addition['after']
report=E.parent/'2026-09-28-b459-precheck.md'
pres['receipt']={'path':str(report.relative_to(R)),'sha256':sha(report.read_bytes())}
pres['scope']='Only new pre-check evidence and register finding additions; W2 and all other entry inputs preserved.'
(E/'final-preservation.json').write_text(json.dumps(pres,indent=2)+'\n')
# Self hash cannot appear inside its own manifest. The report is included via a relative parent path.
files=sorted(p for p in E.rglob('*') if p.is_file() and p.name!='SHA256SUMS')+[report]
lines=[f'{sha(p.read_bytes())}  {os.path.relpath(p,E)}' for p in files]
(E/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
for line in lines:
 h,rel=line.split('  ',1);assert sha((E/rel).read_bytes())==h
print(json.dumps({'entries':len(lines),'manifest_sha256':sha((E/'SHA256SUMS').read_bytes()),'receipt_sha256':sha(report.read_bytes()),'preservation':'PASS','register_next_free':'B473'},indent=2))
