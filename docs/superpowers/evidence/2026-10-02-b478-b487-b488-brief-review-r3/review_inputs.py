from pathlib import Path
import hashlib,json,os,re,subprocess
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');O=R/'artifacts/2026-10-02-b478-b487-b488-brief-review-r3'
B=R/'docs/superpowers/plans/2026-09-30-b478-b487-b488-tool-repairs.md'
sha=lambda data:hashlib.sha256(data).hexdigest()
def inventory(root):
 names=sorted(set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root).decode().split('\0'))-{''})
 files={}
 for name in names:
  p=root/name
  files[name]={'symlink':os.readlink(p)} if p.is_symlink() else {'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
 return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip(),'files':files,'status':subprocess.check_output(['git','status','--porcelain=v1'],cwd=root).decode(),'staged':subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root).decode()}
I=inventory(R);I['simulator']=inventory(S);(O/'inputs.json').write_text(json.dumps(I,indent=2)+'\n')
assert sha(B.read_bytes())=='018c586bff200d4ef5175778145cbfb830ab9674f6ac4dac377480f4e6faaf89'
assert len(B.read_bytes().splitlines())==865
rows=[]
for number,line in enumerate(B.read_text().splitlines(),1):
 if not line.startswith('| `'):continue
 cells=[s.strip() for s in line.split('|')[1:-1]]
 paths=[]
 for token in re.findall(r'`([^`]+)`',cells[0]):
  if (R/token).is_file(): paths.append(token)
  elif paths and '/' not in token and (R/Path(paths[-1]).parent/token).is_file(): paths.append(str(Path(paths[-1]).parent/token))
 if not paths:continue
 hashes=re.findall(r'\b[0-9a-f]{64}\b',' | '.join(cells[1:]))
 if not hashes:continue
 if number<=153:
  assert len(paths)==1
  p=paths[0]
  if p=='tools/test_probe_inbox_transcript.py':assert len(hashes)==1
  else:
   data=subprocess.check_output(['git','show','10f3332:'+p],cwd=R);assert sha(data)==hashes[0],p
   rows.append({'path':p,'source':'base','sha256':hashes[0],'matches':True})
  h=hashes[-1];assert sha((R/p).read_bytes())==h,p
  rows.append({'path':p,'source':'candidate','sha256':h,'matches':True})
 else:
  assert len(paths)==len(hashes),(number,paths,hashes)
  for p,h in zip(paths,hashes):
   assert sha((R/p).read_bytes())==h,p
   rows.append({'path':p,'source':'read-only/preparation','sha256':h,'matches':True})
seals=[]
for prefix in ['2026-09-30-b478-b487-b488-precheck','2026-09-30-b478-b487-b488-brief-review','2026-09-30-b478-b487-b488-brief-rereview','2026-09-30-b478-b487-b488','2026-09-30-b478-b487-b488/round2','2026-10-01-b478-b487-b488-qa']:
 d=R/'docs/superpowers/evidence'/prefix;checked=[]
 for line in (d/'SHA256SUMS').read_text().splitlines():
  h,p=line.split(None,1);p=p.lstrip('*');f=d/p
  if not f.exists():f=R/p
  assert sha(f.read_bytes())==h,f
  checked.append(str(f.relative_to(R)))
 seals.append({'path':prefix,'entries':len(checked),'all_match':True})
old=json.loads((R/'docs/superpowers/evidence/2026-09-30-b478-b487-b488-precheck/inputs.json').read_text())
def same(root,p,before,now):
 if 'symlink' not in now: return before==now
 assert os.readlink(root/p)==subprocess.check_output(['git','show','10f3332:'+p],cwd=root).decode()
 if 'gitlink' in before: return subprocess.check_output(['git','ls-files','-s',p],cwd=root).decode().strip()==before['gitlink']
 return {'sha256':sha((root/p).read_bytes()),'bytes':(root/p).stat().st_size}==before
missing=sorted(set(old['meshroute']['files'])-I['files'].keys());changed=sorted(p for p in old['meshroute']['files'] if p in I['files'] and not same(R,p,old['meshroute']['files'][p],I['files'][p]));new=sorted(I['files'].keys()-old['meshroute']['files'].keys())
allowed={'docs/2026-07-30-open-bug-register.md','tracker.md','MEMORY.md','tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py','tools/probe_inbox_verbs/transcript.py','tools/probe_inbox_verbs/transcript_main.cpp'}
assert not missing and set(changed)==allowed,(missing,changed)
assert I['simulator']['files']==old['simulator']['files'] and not I['simulator']['status'] and not I['staged']
assert I['head']=='10f333298ffad1098daeec37e756ed79ab0fd97f' and I['simulator']['head']=='6585649ea5a780f0542b2931853a667be56a5b2b'
links=[]
for dest in re.findall(r'\]\(([^)]+)\)',B.read_text()):
 if dest.startswith(('http:','https:','#')):continue
 dest=dest.split('#',1)[0];p=B.parent/dest;assert p.exists(),dest;links.append(dest)
result={'brief_sha256':sha(B.read_bytes()),'brief_lines':865,'pins':rows,'seals':seals,'links':links,'changed_since_precheck':changed,'missing_since_precheck':missing,'new_since_precheck':new,'simulator_identical':True,'entry_paths':len(I['files']),'simulator_paths':len(I['simulator']['files'])}
(O/'pins.json').write_text(json.dumps(result,indent=2)+'\n');(O/'brief-r3.md').write_bytes(B.read_bytes())
print(json.dumps({'pins_checked':len(rows),'current_pins':sum(r['source']!='base' for r in rows),'seals':seals,'links':len(links),'paths':len(I['files']),'changed':changed,'new_paths':len(new)},indent=2))
