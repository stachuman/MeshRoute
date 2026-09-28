from pathlib import Path
import json,hashlib,os,subprocess,ast
R=Path('/home/staszek/MeshRoute'); S=Path('/home/staszek/lora-universal-simulator'); E=Path(__file__).resolve().parent; C=E.parent/'2026-09-28-b459'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def capture(root):
 files={}
 for rel in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'):
  if not rel or rel.startswith('docs/superpowers/evidence/2026-09-28-b459-qa'): continue
  p=root/rel
  if p.is_symlink():
   t=os.readlink(p); files[rel]={'type':'symlink','sha256':hashlib.sha256(t.encode()).hexdigest(),'target':t}
  elif p.is_file(): files[rel]={'type':'file','sha256':sha(p)}
  else: files[rel]={'type':'missing'}
 return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'status':subprocess.check_output(['git','status','--short'],cwd=root,text=True),'files':files}
freeze=json.loads((C/'freeze-inventory.json').read_text()); before=json.loads((E.parent/'2026-09-28-b459-brief-rereview/inputs.json').read_text())
all_now={}; result={}
for key,root in [('meshroute',R),('simulator',S)]:
 now=capture(root); all_now[key]=now; assert now['head']==before[key]['head']; assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root)
 changed=[p for p,v in before[key]['files'].items() if now['files'].get(p)!=v]; added=sorted(set(now['files'])-set(before[key]['files']))
 if key=='meshroute':
  assert set(changed)==set(freeze['fence_base']),changed
  for p in added:
   assert p in freeze['fence_final'] or p.startswith('docs/superpowers/evidence/2026-09-28-b459-brief-rereview') or p.startswith('docs/superpowers/evidence/2026-09-28-b459/') or p=='docs/superpowers/evidence/2026-09-28-b459.md',p
 else: assert not changed and not added and not now['status']
 assert subprocess.run(['git','diff','--check'],cwd=root,capture_output=True).returncode==0
 result[key]={'head':now['head'],'paths':len(now['files']),'changed':changed,'explained_additions':added,'staged':False,'whitespace':'PASS'}
assert sha(R/'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md')==freeze['brief']['authorized']=='443b95ea697353d36ef595975a002c1c1b3335dcb21cbe46886aea500d57de5a'
for p,v in freeze['fence_final'].items(): assert sha(R/p)==v['sha256'],p
for p,h in freeze['increment_1_freeze'].items(): assert sha(R/p)==sha(C/'inc1-freeze'/p)==h,p
for key in ['preparation_unchanged','readonly']:
 for p,h in freeze[key].items(): assert sha(R/p)==h,p
checks={}
for dirname in ['2026-09-28-b459','2026-09-28-b459-precheck','2026-09-28-b459-brief-review','2026-09-28-b459-brief-rereview']:
 d=E.parent/dirname; lines=(d/'SHA256SUMS').read_text().splitlines()
 for l in lines:
  h,p=l.split('  ',1); assert sha(d/p)==h,(dirname,p)
 checks[dirname]={'count':len(lines),'manifest_sha256':sha(d/'SHA256SUMS')}
raw=json.loads((C/'raw-logs.json').read_text()); rawdir=R/raw['dir']
raw_mismatches=[]
for p,v in raw['files'].items():
 if sha(rawdir/p)!=v['sha256']:
  data=(rawdir/p).read_bytes()
  assert p=='ledger.md' and hashlib.sha256(data[:v['bytes']]).hexdigest()==v['sha256'],p
  raw_mismatches.append({'path':p,'recorded':v,'current_sha256':sha(rawdir/p),'current_bytes':len(data),'original_bytes_preserved_as_exact_prefix':True,'appended_text':data[v['bytes']:].decode()})
result.update(freeze=freeze,checksums=checks,coder_raw_logs_verified=len(raw['files'])-len(raw_mismatches),coder_raw_log_mismatches=raw_mismatches)
(E/'inputs.json').write_text(json.dumps(all_now,indent=2)+'\n'); (E/'preflight.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: 9 final hashes, 3 increment-1 snapshots, 6 preparation + 6 read-only pins, all evidence checksums; raw ledger append recorded separately; only 5 fenced existing files changed.')
