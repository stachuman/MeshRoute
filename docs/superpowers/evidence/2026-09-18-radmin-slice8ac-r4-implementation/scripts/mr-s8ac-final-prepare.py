from pathlib import Path
import subprocess,shutil,hashlib,json,os
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8ac-r4-b1edsao2');g=q/'final-gate';assert not g.exists()
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(g)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 src=r/rel;dst=g/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(g/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved);inv[rel]={'target':os.readlink(src)}
 else:shutil.copy2(src,dst);inv[rel]={'sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
(q/'final-inputs.json').write_text(json.dumps(inv,indent=2)+'\n')
subprocess.run(['git','apply',str(r/'docs/superpowers/evidence/2026-09-18-radmin-slice8ac-r4-implementation/r42-authority-transcription.patch')],cwd=g,check=True)
subprocess.run(['python3','tools/gen_command_inventory.py','--write'],cwd=g,check=True)
for rel in paths:
 if rel.startswith(('lib/','src/','test/','tools/')) or rel=='platformio.ini':
  src=r/rel;dst=q/'board-gate'/rel;dst.parent.mkdir(parents=True,exist_ok=True)
  if not src.is_symlink() and src.is_file():shutil.copy2(src,dst)
print(g)
