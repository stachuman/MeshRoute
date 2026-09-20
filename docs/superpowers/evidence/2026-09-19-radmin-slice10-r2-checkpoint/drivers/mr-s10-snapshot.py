from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s10-r2-active').read_text().strip();q=Path(q);d=q/sys.argv[1]
assert not d.exists()
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(d)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
paths=sorted(set(os.fsdecode(p) for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for name in paths:
 p=r/name;t=d/name
 if not p.exists() and not p.is_symlink():
  t.unlink(missing_ok=True);inv[name]={'deleted':True};continue
 t.parent.mkdir(parents=True,exist_ok=True)
 if p.is_symlink():
  target=p.resolve();t.unlink(missing_ok=True);t.symlink_to(d/target.relative_to(r) if target.is_relative_to(r) else target);inv[name]={'symlink':os.readlink(p)}
 else:shutil.copy2(p,t);inv[name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':p.stat().st_size}
(q/(sys.argv[1]+'-inputs.json')).write_text(json.dumps(inv,indent=2)+'\n')
print(d,len(inv))
