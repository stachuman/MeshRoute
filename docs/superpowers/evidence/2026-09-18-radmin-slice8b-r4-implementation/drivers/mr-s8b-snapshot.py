from pathlib import Path
import subprocess,os,shutil,hashlib,json,sys
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8b-r4-_d7gpj1e');dest=q/sys.argv[1]
assert not dest.exists()
paths=[p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p]
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(dest)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
manifest={}
for rel in sorted(set(paths)):
 src=r/rel;dst=dest/rel
 if not src.exists():dst.unlink(missing_ok=True);continue
 dst.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(dest/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
 else:shutil.copy2(src,dst);manifest[rel]=hashlib.sha256(src.read_bytes()).hexdigest()
(q/(sys.argv[1]+'-inputs.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print(dest,len(manifest))
