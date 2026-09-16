from pathlib import Path
import subprocess,tempfile,hashlib,json,shutil,os,stat
r=Path('/home/staszek/MeshRoute'); q=Path(tempfile.mkdtemp(prefix='mr-codex-s7b3-'));(q/'logs').mkdir()
Path('/tmp/mr-codex-s7b3-active').write_text(str(q)+'\n')
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
(q/'inputs-before.json').write_text(json.dumps(inv,indent=2)+'\n')
(q/'status-before.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=r))
(q/'preparation.patch').write_bytes(subprocess.check_output(['git','diff','--binary','HEAD'],cwd=r))
base=q/'gate';subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(base)],check=True,stdout=subprocess.DEVNULL)
for rel in paths:
 src=r/rel;dst=base/rel;dst.parent.mkdir(exist_ok=True,parents=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(base/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
 else:shutil.copy2(src,dst)
print(q,flush=True)
results=[]
for name,cmd in [('native-base-build',['pio','test','-e','native']),('native-base',['./.pio/build/native/program'])]:
 with (q/'logs'/(name+'.log')).open('wb') as log:rc=subprocess.run(cmd,cwd=base,stdout=log,stderr=subprocess.STDOUT).returncode
 results.append({'name':name,'cmd':cmd,'exit':rc});(q/'base-native-results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(name,rc,flush=True)
 if rc:break
