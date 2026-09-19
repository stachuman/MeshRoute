from pathlib import Path
import os,subprocess,json,hashlib,tarfile,shutil
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s9-r4-j37ybea_');rel='docs/superpowers/evidence/2026-09-19-radmin-slice9-r4-checkpoint';out=r/rel
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args,cwd=r):return subprocess.check_output(['git',*args],cwd=cwd)
def save(p,value):p.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
assert git('rev-parse','HEAD').decode().strip()=='84edd3ebfb08d807645d077269bace145ac2a2ab'
sim=Path('/home/staszek/lora-universal-simulator');assert git('rev-parse','HEAD',cwd=sim).decode().strip()=='6585649ea5a780f0542b2931853a667be56a5b2b';assert not git('status','--porcelain=v1',cwd=sim);assert not git('diff','--cached','--name-status')
for root in [r,sim]:subprocess.run(['git','diff','--check'],cwd=root,check=True)
subprocess.run(['python3','tools/gen_command_inventory.py','--check'],cwd=r,check=True,stdout=(out/'final-inventory-check.log').open('wb'))
subprocess.run(['python3','/tmp/mr-s9-r4-source-audit.py'],check=True)
# Only these previously-inventoried records changed after the complete instrument snapshot.
old=json.loads((q/'final-gate-inputs.json').read_text());d=[]
for p,v in old.items():
 f=r/p
 if v.get('deleted'):assert not f.exists();continue
 if 'sha256' not in v:continue
 h=sha(f.read_bytes())
 if h!=v['sha256']:d.append(dict(path=p,tested=v['sha256'],checkpoint=h))
assert {x['path'] for x in d}=={'tools/test_probe_features.py','docs/2026-07-30-open-bug-register.md','docs/superpowers/evidence/2026-09-18-radmin-slice9.md','docs/2026-07-31-bench-test-script.md'}
save(q/'post-gate-deltas.json',d)
# Compare index entries, not the incidental stat-cache bytes Git may refresh on reads.
verified=[]
for row in json.loads((q/'measurement-index-projection.json').read_text()):
 root=q/row['snapshot'];assert not git('diff','--cached','--name-status',cwd=root)
 entries=git('ls-files','--stage','-z',cwd=root);tree=git('ls-tree','-r','-z','HEAD',cwd=root)
 expected=b''.join(x.split(b' ',2)[0]+b' '+x.split(b' ',2)[2].split(b'\t',1)[0]+b' 0\t'+x.split(b'\t',1)[1]+b'\0' for x in tree.split(b'\0') if x);assert entries==expected
 env=dict(os.environ,GIT_INDEX_FILE=row['external_index']);actual=subprocess.check_output(['git','ls-files','--stage','-z'],cwd=root,env=env)
 expected=b''.join(x+b'\0' for x in entries.split(b'\0') if x and os.fsdecode(x.split(b'\t',1)[1]) not in row['deleted']);assert actual==expected
 verified.append(dict(snapshot=row['snapshot'],original_index_entries_match_HEAD=True,projected_index_entries_match_HEAD_minus_eight_deleted=True,original_index_byte_identical=sha((root/'.git/index').read_bytes())==row['original_index_sha256']))
save(q/'measurement-index-verification.json',verified)
subprocess.run(['python3','/tmp/mr-s9-r4-archive.py'],check=True)
# Archive every dirty tracked and prior untracked input; omit only this report directory itself.
paths=set(os.fsdecode(p) for p in git('diff','--name-only','-z','HEAD').split(b'\0') if p)
paths.update(os.fsdecode(p) for p in git('ls-files','--others','--exclude-standard','-z').split(b'\0') if p)
paths={p for p in paths if not p.startswith(rel+'/')};deleted=[];overlay={}
with tarfile.open(out/'checkpoint-overlay.tar.xz','w:xz',preset=3) as tar:
 for p in sorted(paths):
  f=r/p
  if not f.exists() and not f.is_symlink():deleted.append(p);continue
  tar.add(f,arcname=p,recursive=False)
  overlay[p]=({'symlink':os.readlink(f)} if f.is_symlink() else {'sha256':sha(f.read_bytes()),'size':f.stat().st_size})
assert len(deleted)==8
save(out/'checkpoint-deletions.json',deleted);save(out/'overlay-content-inventory.json',overlay)
with tarfile.open(out/'checkpoint-overlay.tar.xz','r:xz') as tar:
 assert set(tar.getnames())==set(overlay)
 for p,v in overlay.items():
  if 'sha256'in v:assert sha(tar.extractfile(p).read())==v['sha256']
  else:assert tar.getmember(p).linkname==v['symlink']
# This manifest includes the overlay but excludes itself and its artifact index (circular metadata).
exclude={rel+'/checkpoint-inputs.json',rel+'/artifact-sha256.json'}
names=set(os.fsdecode(p) for p in git('ls-files','--cached','--others','--exclude-standard','-z').split(b'\0') if p);inventory={}
for p in sorted(names-exclude):
 f=r/p
 if f.is_symlink():inventory[p]={'symlink':os.readlink(f)}
 elif f.is_file():inventory[p]={'sha256':sha(f.read_bytes()),'size':f.stat().st_size}
 else:inventory[p]={'deleted':True}
save(out/'checkpoint-inputs.json',{'base':'84edd3ebfb08d807645d077269bace145ac2a2ab','simulator':'6585649ea5a780f0542b2931853a667be56a5b2b','disposition':'HOLD B426/B427; checkpoint, not implementation freeze','excluded_circular_metadata':sorted(exclude),'inputs':inventory})
artifacts={str(p.relative_to(out)):sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'};save(out/'artifact-sha256.json',artifacts)
print('SEALED CHECKPOINT:',len(inventory),'input records;',len(overlay),'overlay files;',len(deleted),'deletions;',len(artifacts),'artifacts; disposition HOLD B426/B427')
