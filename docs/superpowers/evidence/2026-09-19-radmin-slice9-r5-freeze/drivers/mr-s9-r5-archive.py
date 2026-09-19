from pathlib import Path
import shutil,json,tarfile
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s9-active').read_text().strip();q=Path(q);out=r/'docs/superpowers/evidence/2026-09-19-radmin-slice9-r5-freeze';out.mkdir(exist_ok=True)
def copy(f,rel):
 if f.is_file():
  p=out/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,p)
for f in q.glob('*'):
 if f.is_file() and f.suffix in ['.json','.log','.txt']:copy(f,f.name)
for f in (q/'logs').glob('*'):copy(f,Path('logs')/f.name)
for f in Path('/tmp').glob('mr-s9-r5-*.py'):copy(f,Path('drivers')/f.name)
copy(Path('/tmp/mr-s9-snapshot.py'),'drivers/snapshot.py')
d=q/'boards/.pio-measure/s9-r5'
for f in d.rglob('*'):
 if f.is_file() and f.suffix in ['.json','.txt']:copy(f,Path('boards')/f.relative_to(d))
if not (out/'stock-board-pair.tar.xz').exists():
 with tarfile.open(out/'stock-board-pair.tar.xz','w:xz',preset=3) as tar:tar.add(d,arcname='stock-board-pair')
print(out)
