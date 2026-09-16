from pathlib import Path
import subprocess,shutil,json
r=Path('/home/staszek/MeshRoute');q=Path(Path('/tmp/mr-codex-s7b3-active').read_text().strip());dst=q/'gate'
paths=set(p.decode() for p in subprocess.check_output(['git','diff','--name-only','-z'],cwd=r).split(b'\0') if p)
paths.update(p.decode() for p in subprocess.check_output(['git','ls-files','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p)
paths={p for p in paths if p.startswith(('src/','lib/','test/','tools/')) or p=='platformio.ini'}
for p in paths:
 target=dst/p;target.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(r/p,target)
(q/'synced-source-paths.json').write_text(json.dumps(sorted(paths),indent=2)+'\n')
print('synced',len(paths),'implementation paths; frozen preparation documents preserved')
