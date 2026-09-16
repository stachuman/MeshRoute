from pathlib import Path
import tarfile,hashlib,json
q=Path('/tmp/mr-codex-s7b3-0gt630zl');out=q/'archives';out.mkdir(exist_ok=True);results=[]
for name,paths in [('corpus.tar.xz',[q/'corpus-base',q/'corpus-final']),('measurements.tar.xz',[q/'gate/.pio-measure/s7b3-base',q/'gate/.pio-measure/s7b3-final-2',q/'stack2'])]:
 print('ARCHIVE',name,flush=True);target=out/name;assert not target.exists()
 with tarfile.open(target,'w:xz',preset=3) as archive:
  for path in paths:archive.add(path,arcname=path.name,recursive=True)
 results.append(dict(path=name,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),inputs=[str(p) for p in paths]));(out/'archives.json').write_text(json.dumps(results,indent=2)+'\n');print('DONE',name,target.stat().st_size,flush=True)
