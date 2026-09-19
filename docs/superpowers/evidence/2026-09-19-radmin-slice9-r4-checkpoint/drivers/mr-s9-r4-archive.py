from pathlib import Path
import shutil,json,hashlib,tarfile,subprocess,re
q=Path('/tmp/mr-s9-r4-j37ybea_');root=Path('/home/staszek/MeshRoute');out=root/'docs/superpowers/evidence/2026-09-19-radmin-slice9-r4-checkpoint';out.mkdir(exist_ok=True)
def copy(f,rel):
 if f.is_file():
  t=out/rel;t.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,t)
for f in q.glob('*.json'):copy(f,f.name)
for f in q.glob('*.log'):copy(f,f.name)
for f in q.glob('*.patch'):copy(f,f.name)
for f in q.glob('*.txt'):copy(f,f.name)
for directory in ['final-logs','final-union','iteration1-logs','warning-attribution','B428-focused']:
 for f in (q/directory).rglob('*'):
  if f.is_file():copy(f,Path(directory)/f.relative_to(q/directory))
for directory in ['final-logs','final-union']:
 for f in (q/'pre-review-run'/directory).rglob('*'):
  if f.is_file():copy(f,Path('discarded-attempt')/directory/f.relative_to(q/'pre-review-run'/directory))
for f in (q/'pre-review-run').glob('*'):
 if f.suffix in ['.json','.log','.txt']:copy(f,Path('discarded-attempt')/f.name)
for f in Path('/tmp').glob('mr-s9-r4-*.py'):copy(f,Path('drivers')/f.name)
copy(Path('/tmp/mr-s9-snapshot.py'),'drivers/mr-s9-snapshot.py');copy(q/'measurement-bin/git','drivers/measurement-git-wrapper.py')
for mode in ['deferred_actions','deferred_actions-no-neg']:
 f=q/'final-logs'/(mode+'.log')
 if f.exists():
  m=re.search(r'output=(/tmp/mr-action-probe-\S+)',f.read_text())
  if m:
   d=Path(m[1])
   for p in d.rglob('*'):
    if p.is_file() and p.suffix in ['.json','.log','.cpp','.sh']:copy(p,Path(mode)/p.relative_to(d))
for side,d in [('base',Path('/tmp/mr-s9-r3-mg75mxe0/base/.pio-measure/s9-base-replay')),('candidate',q/'final-boards/.pio-measure/s9-final')]:
 for f in d.rglob('*'):
  if f.is_file() and f.suffix in ['.json','.txt']:copy(f,Path('boards')/side/f.relative_to(d))
archive=out/'board-builds.tar.xz'
if not archive.exists():
 with tarfile.open(archive,'w:xz',preset=3) as tar:
  for side,d in [('base',Path('/tmp/mr-s9-r3-mg75mxe0/base/.pio-measure/s9-base-replay')),('candidate',q/'final-boards/.pio-measure/s9-final')]:
   tar.add(d,arcname=side)
for side,d in [('base',Path('/tmp/mr-s9-r3-mg75mxe0/corpus-base')),('candidate',q/'corpus-final')]:copy(d/'manifest.json',Path('corpus')/side/'manifest.json')
print(out)
