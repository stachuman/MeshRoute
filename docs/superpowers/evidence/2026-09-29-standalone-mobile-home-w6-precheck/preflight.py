from pathlib import Path
import os,hashlib,json,subprocess
R=Path('/home/staszek/MeshRoute'); S=Path('/home/staszek/lora-universal-simulator'); E=Path(__file__).resolve().parent
prefix='docs/superpowers/evidence/'+E.name

def capture(root):
 files={}
 for rel in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'):
  if not rel or (root==R and rel.startswith(prefix)):continue
  p=root/rel
  if p.is_symlink():
   v=os.readlink(p); files[rel]={'kind':'symlink','target':v,'sha256':hashlib.sha256(v.encode()).hexdigest()}
  elif p.is_file():files[rel]={'kind':'file','bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
  else: files[rel]={'kind':'missing'}
 return dict(head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),status=subprocess.check_output(['git','status','--short'],cwd=root,text=True),files=files)
x={k:capture(v) for k,v in [('meshroute',R),('simulator',S)]}
assert x['meshroute']['head']=='70ff486b40c9b07001b33bcbcdf640ad494c1149'
assert x['simulator']['head']=='6585649ea5a780f0542b2931853a667be56a5b2b' and not x['simulator']['status']
assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=R)
assert subprocess.check_output(['git','diff','--name-only'],cwd=R,text=True).splitlines()==['docs/2026-07-30-open-bug-register.md']
(E/'inputs.json').write_text(json.dumps(x,indent=2)+'\n')
(E/'preparation.diff').write_bytes(subprocess.check_output(['git','diff','--','docs/2026-07-30-open-bug-register.md'],cwd=R))
print({k:(v['head'],len(v['files'])) for k,v in x.items()})
