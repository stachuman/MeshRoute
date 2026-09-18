from pathlib import Path
import subprocess,tempfile,json,hashlib,shutil,stat,os,time
r=Path('/home/staszek/MeshRoute');q=Path(tempfile.mkdtemp(prefix='mr-codex-s8ac-preflight-'));(q/'logs').mkdir();g=q/'snapshot'
Path('/tmp/mr-codex-s8ac-preflight-active').write_text(str(q)+'\n')
brief='docs/superpowers/plans/2026-09-17-radmin-slice8ac-controller-and-local-delivery.md'
assert hashlib.sha256((r/brief).read_bytes()).hexdigest()=='d7e4cf2a6098b5ce3fd7626cdeefc0080b8fbef4a13681416ae30b158a358521'
base='6086152b97b5934971d231a345b9939d5f2db1e9';head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip()
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
(q/'inputs.json').write_text(json.dumps(inv,indent=2)+'\n')
changed=subprocess.check_output(['git','diff','--name-only',base],cwd=r).decode().splitlines();assert all(p.startswith('docs/') or p in ['MEMORY.md','tracker.md'] for p in changed),changed
prep=sorted(set(subprocess.check_output(['git','diff','--name-only'],cwd=r).decode().splitlines())|set(subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=r).decode().splitlines()))
(q/'preparation-inputs.json').write_text(json.dumps({p:inv[p] for p in prep},indent=2)+'\n')
(q/'base-to-head-files.json').write_text(json.dumps(subprocess.check_output(['git','diff','--name-only',base,head],cwd=r).decode().splitlines(),indent=2)+'\n')
(q/'preparation.patch').write_bytes(subprocess.check_output(['git','diff','--binary',base],cwd=r));(q/'status-before.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=r))
state=dict(head=head,brief=brief,brief_sha256=hashlib.sha256((r/brief).read_bytes()).hexdigest(),brief_base=base,simulator_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd='/home/staszek/lora-universal-simulator').decode().strip(),input_count=len(inv),preparation_count=len(prep),all_code_test_tool_inputs_equal_brief_base=True)
assert state['simulator_head']=='06746a97de5764415d6fcef10b97bca90569b9c7';assert not subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],cwd='/home/staszek/lora-universal-simulator')
(q/'state.json').write_text(json.dumps(state,indent=2)+'\n')
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(g)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for rel in paths:
 src=r/rel;dst=g/rel;dst.parent.mkdir(exist_ok=True,parents=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(g/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
 else:shutil.copy2(src,dst)
print('SNAPSHOT',q,flush=True)
results=[]
for name,cmd in [('native-build',['pio','test','-e','native']),('native-binary',['./.pio/build/native/program'])]:
 start=time.monotonic()
 with (q/'logs'/(name+'.log')).open('wb') as f:rc=subprocess.run(cmd,cwd=g,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,exit=rc,seconds=time.monotonic()-start,log_sha256=hashlib.sha256((q/'logs'/(name+'.log')).read_bytes()).hexdigest()));(q/'native-results.json').write_text(json.dumps(results,indent=2)+'\n');print(name,rc,flush=True)
 if rc:break
