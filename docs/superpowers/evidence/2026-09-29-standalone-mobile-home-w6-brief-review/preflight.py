from pathlib import Path
import json,hashlib,subprocess,re,os
R=Path('/home/staszek/MeshRoute');E=R/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-review';E.mkdir(exist_ok=True)
b=R/'docs/superpowers/plans/2026-09-29-standalone-mobile-home-w6-phrases.md';s=b.read_text();rows=[]
for line in s.splitlines():
 if not line.startswith('| `'):continue
 parts=line.split('|');paths=re.findall(r'`([^`]+)`',parts[1]);hashes=re.findall(r'`([0-9a-f]{64})`',line)
 for path,h in zip(paths,hashes):
  p=R/path;v=hashlib.sha256(p.read_bytes()).hexdigest();item={'path':path,'expected_sha256':h,'actual_sha256':v,'match':h==v}
  if len(parts)==5 and re.fullmatch(r'\s*\d+\s*',parts[3]):item.update(expected_lines=int(parts[3]),actual_lines=p.read_bytes().count(b'\n'))
  rows.append(item)
assert len(rows)==31 and all(x['match'] for x in rows)
assert all(x.get('expected_lines')==x.get('actual_lines') for x in rows)
base=json.loads((R/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck/inputs.json').read_text());changes=[]
for p,v in base['meshroute']['files'].items():
 q=R/p;h=hashlib.sha256(os.readlink(q).encode() if q.is_symlink() else q.read_bytes()).hexdigest() if q.is_symlink() or q.is_file() else None
 if h!=v.get('sha256'):changes.append(p)
assert set(changes)=={'MEMORY.md','tracker.md','docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md'},changes
freeze={}
for path in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=R).decode().split('\0'):
 if path and not path.startswith(str(E.relative_to(R))):
  p=R/path
  if p.is_symlink() or p.is_file():freeze[path]=hashlib.sha256(os.readlink(p).encode() if p.is_symlink() else p.read_bytes()).hexdigest()
(E/'inputs.json').write_text(json.dumps({'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip(),'brief_sha256':hashlib.sha256(b.read_bytes()).hexdigest(),'pins':rows,'changed_since_precheck':changes,'files':freeze},indent=2)+'\n')
r=subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=R/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck',capture_output=True,text=True);assert r.returncode==0
(E/'precheck-checksums.txt').write_text(r.stdout)
print('31 pins and 25 line counts match; only four declared input changes; 45 precheck checksums match')
