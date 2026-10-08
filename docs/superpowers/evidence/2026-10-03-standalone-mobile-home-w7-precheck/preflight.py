from pathlib import Path
import subprocess,json,hashlib,os
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');O=R/'artifacts/2026-10-03-standalone-mobile-home-w7-precheck';E=R/'docs/superpowers/evidence/2026-10-02-b478-b487-b488-qa-regate'
def inv(r):
 names=sorted(set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=r).decode().split('\0'))-{''})
 return dict(head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip(),status=subprocess.check_output(['git','status','--porcelain=v1'],cwd=r).decode(),files={p:({'symlink':os.readlink(r/p)} if (r/p).is_symlink() else {'sha256':hashlib.sha256((r/p).read_bytes()).hexdigest(),'bytes':(r/p).stat().st_size}) for p in names},staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=r).decode())
I=inv(R);I['simulator']=inv(S);(O/'inputs.json').write_text(json.dumps(I,indent=2)+'\n')
old=json.loads((E/'inputs.json').read_text());landing=json.loads((E/'qa-landing.json').read_text());expected=old['files'].copy()
for p,h in landing['final_status_hashes'].items(): expected[p]={'sha256':h,'bytes':(R/p).stat().st_size}
changed=[p for p,v in expected.items() if I['files'].get(p)!=v];new=sorted(I['files'].keys()-expected.keys());assert not changed,changed
assert all(p=='docs/superpowers/evidence/2026-10-02-b478-b487-b488-qa-regate.md' or p.startswith('docs/superpowers/evidence/2026-10-02-b478-b487-b488-qa-regate/') for p in new),new
seals=[]
for d in [E,R/'docs/superpowers/evidence/2026-10-02-b478-b487-b488-return']:
 for line in (d/'SHA256SUMS').read_text().splitlines():
  h,p=line.split(None,1);f=d/p.lstrip('*');f=f if f.exists() else R/p.lstrip('*');assert hashlib.sha256(f.read_bytes()).hexdigest()==h,f
 seals.append({'path':str(d.relative_to(R)),'entries':len((d/'SHA256SUMS').read_text().splitlines()),'sha256':hashlib.sha256((d/'SHA256SUMS').read_bytes()).hexdigest()})
assert I['head']==landing['head'] and not I['staged']
assert I['simulator']['files']==old['simulator']['files'] and I['simulator']['head']==landing['simulator_head'] and not I['simulator']['status']
(O/'preflight.json').write_text(json.dumps(dict(verdict='PASS',head=I['head'],simulator_head=I['simulator']['head'],inputs=len(I['files']),changed=[],new_qa_paths=new,qa_landing_hashes=landing['final_status_hashes'],seals=seals),indent=2)+'\n')
print('PASS',len(I['files']),'MeshRoute inputs; simulator clean; QA freeze and landing reproduced')
