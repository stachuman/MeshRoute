from pathlib import Path
import os,json,hashlib,subprocess,re,ast
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-brief-review';report=E.parent/'2026-09-28-b459-brief-review.md'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inv=json.loads((E/'inputs.json').read_text());pres={}
for key,root in [('meshroute',R),('simulator',S)]:
 changed=[];missing=[]
 for rel,old in inv[key]['files'].items():
  p=root/rel
  if p.is_symlink():
   target=os.readlink(p);v={'type':'symlink','sha256':hashlib.sha256(target.encode()).hexdigest(),'target':target}
  elif p.is_file():v={'type':'file','sha256':sha(p)}
  else:v={'type':'missing'}
  if v!=old:changed.append(rel)
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();status=subprocess.check_output(['git','status','--short'],cwd=root,text=True);staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root,text=True)
 assert not changed and head==inv[key]['head'] and not staged
 if key=='simulator':assert not status
 now=set(subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'))-{''};added=sorted(now-set(inv[key]['files']))
 if key=='meshroute':assert all(p.startswith('docs/superpowers/evidence/2026-09-28-b459-brief-review') for p in added)
 else:assert not added
 white=subprocess.run(['git','diff','--check'],cwd=root,capture_output=True);assert white.returncode==0
 pres[key]={'head':head,'entry_paths_preserved':len(inv[key]['files']),'changed':changed,'added_review_outputs':added,'staged':'','status':status,'whitespace':'PASS'}
links=[]
for dest in re.findall(r'\]\(([^)]+)\)',report.read_text()):
 p=report.parent/dest.split('#')[0];links.append({'path':dest,'exists':p.exists()});assert p.exists()
for p in E.glob('*.py'):ast.parse(p.read_text())
(E/'validation.json').write_text(json.dumps({'links':links,'evidence_python_syntax':'PASS'},indent=2)+'\n')
(E/'final-preservation.json').write_text(json.dumps(pres,indent=2)+'\n')
(E/'raw-logs.json').write_text(json.dumps({str(p.relative_to(R)):{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(RAW.iterdir()) if p.is_file()},indent=2)+'\n')
files=sorted(p for p in E.rglob('*') if p.is_file() and p.name!='SHA256SUMS')+[report]
(E/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {os.path.relpath(p,E)}\n' for p in files))
for l in (E/'SHA256SUMS').read_text().splitlines():h,path=l.split('  ',1);assert sha(E/path)==h
print(json.dumps({'verdict':'HOLD','report_sha256':sha(report),'manifest_sha256':sha(E/'SHA256SUMS'),'checksum_entries':len(files),'all_entry_inputs_preserved':True},indent=2))
