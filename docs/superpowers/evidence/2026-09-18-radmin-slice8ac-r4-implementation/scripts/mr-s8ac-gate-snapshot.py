from pathlib import Path
import subprocess,hashlib,json,shutil,os
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8ac-r4-b1edsao2');g=q/'gate';assert not g.exists()
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(g)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 src=r/rel;dst=g/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(g/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
  inv[rel]={'target':os.readlink(src)}
 else:shutil.copy2(src,dst);inv[rel]={'sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
(q/'gate-inputs.json').write_text(json.dumps(inv,indent=2)+'\n')
# R-RA-42 reserves the shared table to QA: validate its exact proposed transcription only in this isolated gate tree.
patch=r/'docs/superpowers/evidence/2026-09-18-radmin-slice8ac-r4-implementation/r42-authority-transcription.patch'
subprocess.run(['git','apply',str(patch)],cwd=g,check=True)
(q/'sim-controller.cmake').write_text('''# Build-only integration, outside the simulator checkout. No simulator source changes.
function(meshroute_controller_sources)
  target_sources(meshroute_core_normal PRIVATE "${MESHROUTE_DIR}/lib/core/remote_client.cpp")
  target_sources(meshroute_core_gw PRIVATE "${MESHROUTE_DIR}/lib/core/remote_client.cpp")
endfunction()
cmake_language(DEFER CALL meshroute_controller_sources)
''')
print(g)
