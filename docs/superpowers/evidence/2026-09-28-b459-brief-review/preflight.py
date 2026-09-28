from pathlib import Path
import json,hashlib,os,subprocess,re
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');E=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
b=R/'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md';assert sha(b)=='f485c49427838f1480fb5a8b32a0dd454f3e88a4c7eab65b54aaeeb46c7e1453';assert len(b.read_text().splitlines())==523
pins={}
for l in b.read_text().splitlines():
 if not l.startswith('|'):continue
 paths=re.findall(r'`([^`]+)`',l.split('|')[1]);hs=re.findall(r'`([a-f0-9]{64})`',l)
 if hs:
  assert len(paths)==len(hs),(paths,hs)
  for path,h in zip(paths,hs):assert sha(R/path)==h,path;pins[path]=h
pre=R/'docs/superpowers/evidence/2026-09-28-b459-precheck';checks=[]
for l in (pre/'SHA256SUMS').read_text().splitlines():
 h,rel=l.split('  ',1);assert sha(pre/rel)==h,rel;checks.append(rel)
old=json.loads((pre/'inputs.json').read_text());current={};comparison={}
def entries(root):
 out={}
 for rel in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'):
  if not rel:continue
  if rel.startswith('docs/superpowers/evidence/2026-09-28-b459-brief-review'):continue
  p=root/rel
  if p.is_symlink():
   target=os.readlink(p);out[rel]={'type':'symlink','sha256':hashlib.sha256(target.encode()).hexdigest(),'target':target}
  elif p.is_file():out[rel]={'type':'file','sha256':sha(p)}
  else:out[rel]={'type':'missing'}
 return out
for key,root in [('meshroute',R),('simulator',S)]:
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();assert head==old[key]['head']
 files=entries(root);status=subprocess.check_output(['git','status','--short'],cwd=root,text=True);staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root,text=True);assert not staged
 changed=[p for p,v in old[key]['files'].items() if files.get(p)!=v];added=sorted(set(files)-set(old[key]['files']))
 if key=='meshroute':
  assert set(changed)=={'docs/2026-07-30-open-bug-register.md','tracker.md','MEMORY.md'},changed
  assert all(p.startswith('docs/superpowers/evidence/2026-09-28-b459-precheck') or p=='docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md' for p in added),added
 else:assert not status and not changed and not added
 current[key]={'head':head,'status':status,'files':files};comparison[key]={'changed':changed,'added':added,'staged':staged}
new=['tools/probe_accounting.sh','tools/probe_board_ui/expected.tsv','tools/probe_board_ui/accounting.py','tools/test_probe_board_ui.py'];assert all(not (R/f).exists() for f in new)
links=[]
for target in re.findall(r'\]\(([^)]+)\)',b.read_text()):
 p=b.parent/target.split('#')[0];links.append({'target':target,'exists':p.exists()})
assert all(x['exists'] for x in links)
(E/'inputs.json').write_text(json.dumps(current,indent=2)+'\n')
(E/'preflight.json').write_text(json.dumps({'brief':{'path':str(b.relative_to(R)),'sha256':sha(b),'lines':523},'pins':pins,'precheck_checksums_verified':len(checks),'comparison_to_precheck_entry':comparison,'new_paths_absent':new,'links':links},indent=2)+'\n')
print(f'PASS: {len(pins)} brief pins, {len(checks)} pre-check checksums; only three declared preparation paths changed')
