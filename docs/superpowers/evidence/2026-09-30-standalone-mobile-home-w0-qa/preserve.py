from pathlib import Path
import subprocess,hashlib,json,sys
root=Path('/home/staszek/MeshRoute'); raw=root/'artifacts/2026-09-30-standalone-mobile-home-w0-qa'
entry=json.loads((raw/'inputs-entry.json').read_text()); phase=sys.argv[1]
allowed={'docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md','tracker.md','MEMORY.md','docs/2026-09-20-metal-test-plan.md'} if phase=='after-landing' else set()
qa='docs/superpowers/evidence/2026-09-30-standalone-mobile-home-w0-qa'
out={'phase':phase,'repositories':{}}
for name,path in [('meshroute',root),('simulator',Path('/home/staszek/lora-universal-simulator'))]:
 def git(*args):return subprocess.check_output(['git',*args],cwd=path).decode()
 old=entry[name]; files=old['files']; changed={}; missing=[]
 for rel,meta in files.items():
  p=path/rel
  if 'gitlink' in meta:
   now={'gitlink':git('ls-files','-s','--',rel).rstrip('\n')}
  elif p.is_file():now={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
  else:missing.append(rel);continue
  if now!=meta:changed[rel]={'before':meta,'after':now}
 current=set(git('ls-files','-co','--exclude-standard','-z').strip('\0').split('\0'))
 added=sorted(current-files.keys())
 assert not missing,(name,missing)
 assert set(changed)<= (allowed if name=='meshroute' else set()),changed
 assert not added or (name=='meshroute' and phase=='after-landing' and all(p==qa+'.md' or p.startswith(qa+'/') for p in added)),added
 assert git('rev-parse','HEAD').strip()==old['head']
 assert not git('diff','--cached','--name-only').strip()
 dc=subprocess.run(['git','diff','--check'],cwd=path,capture_output=True);assert dc.returncode==0,dc.stdout
 status=git('status','--porcelain')
 if name=='simulator':assert not status
 out['repositories'][name]={'head':old['head'],'entry_paths':len(files),'unchanged_paths':len(files)-len(changed),'changed':changed,'missing':missing,'added':added,'staged':0,'whitespace_exit':0,'status':status}
coder=root/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0'
p=subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=coder,capture_output=True,text=True)
assert p.returncode==0 and p.stdout.count(': OK')==42
out['coder_checksum_entries_verified']=42
out['verdict']='PASS'
(raw/('preservation-'+phase+'.json')).write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'phase':phase,'verdict':'PASS','changes':{k:list(v['changed']) for k,v in out['repositories'].items()},'added':{k:len(v['added']) for k,v in out['repositories'].items()}},indent=2))
