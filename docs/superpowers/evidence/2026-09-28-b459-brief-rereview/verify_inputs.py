from pathlib import Path
import json,hashlib,os,re,subprocess,ast
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');E=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
brief=R/'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md';assert sha(brief)=='443b95ea697353d36ef595975a002c1c1b3335dcb21cbe46886aea500d57de5a';assert len(brief.read_text().splitlines())==545
pins={}
for l in brief.read_text().splitlines():
 if not l.startswith('|'):continue
 paths=re.findall(r'`([^`]+)`',l.split('|')[1]);hashes=re.findall(r'`([a-f0-9]{64})`',l)
 if hashes:
  assert len(paths)==len(hashes)
  for f,h in zip(paths,hashes):assert sha(R/f)==h,f;pins[f]=h
checks={}
for directory in ['2026-09-28-b459-precheck','2026-09-28-b459-brief-review']:
 folder=E.parent/directory;count=0
 for line in (folder/'SHA256SUMS').read_text().splitlines():
  h,rel=line.split('  ',1);assert sha(folder/rel)==h,rel;count+=1
 checks[directory]={'entries':count,'manifest_sha256':sha(folder/'SHA256SUMS')}
old=json.loads((E.parent/'2026-09-28-b459-brief-review/inputs.json').read_text());current={};comparisons={}
for key,root in [('meshroute',R),('simulator',S)]:
 files={}
 for rel in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'):
  if not rel or rel.startswith('docs/superpowers/evidence/2026-09-28-b459-brief-rereview'):continue
  p=root/rel
  if p.is_symlink():
   t=os.readlink(p);files[rel]={'type':'symlink','sha256':hashlib.sha256(t.encode()).hexdigest(),'target':t}
  elif p.is_file():files[rel]={'type':'file','sha256':sha(p)}
  else:files[rel]={'type':'missing'}
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();status=subprocess.check_output(['git','status','--short'],cwd=root,text=True);staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root,text=True);assert head==old[key]['head'] and not staged
 changed=[f for f,v in old[key]['files'].items() if files.get(f)!=v];added=sorted(set(files)-set(old[key]['files']))
 if key=='meshroute':
  assert set(changed)=={'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md','docs/2026-07-30-open-bug-register.md'},changed
  assert all(f.startswith('docs/superpowers/evidence/2026-09-28-b459-brief-review') for f in added),added
 else:assert not status and not changed and not added
 comparisons[key]={'changed_since_revision_1_review_entry':changed,'explained_prior_review_additions':added,'head':head,'staged':staged}
 current[key]={'head':head,'status':status,'files':files}
new=['tools/probe_accounting.sh','tools/probe_board_ui/expected.tsv','tools/probe_board_ui/accounting.py','tools/test_probe_board_ui.py'];assert all(not (R/f).exists() for f in new)
py=ast.parse((R/'tools/test_probe_firmware_ui.py').read_text());binding=next(ast.literal_eval(n.value) for n in py.body if isinstance(n,ast.Assign) and getattr(n.targets[0],'id','')=='ACCOUNTING_STATEMENT');runner=(R/'tools/probe_firmware_ui/run.sh').read_text();assert runner.count(binding)==1
links=[{'target':t,'exists':(brief.parent/t.split('#')[0]).exists()} for t in re.findall(r'\]\(([^)]+)\)',brief.read_text())];assert all(l['exists'] for l in links)
(E/'inputs.json').write_text(json.dumps(current,indent=2)+'\n')
(E/'preflight.json').write_text(json.dumps({'brief_hash':sha(brief),'brief_lines':545,'pins':pins,'verified_prior_evidence':checks,'comparison':comparisons,'new_tool_paths_absent':new,'ACCOUNTING_STATEMENT':binding,'binding_occurrences':1,'links':links},indent=2)+'\n')
print(f'PASS: {len(pins)} pins, {sum(v["entries"] for v in checks.values())} prior checksums; only brief and register changed')
