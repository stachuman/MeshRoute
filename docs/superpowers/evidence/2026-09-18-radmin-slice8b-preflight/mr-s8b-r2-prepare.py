from pathlib import Path
import subprocess,tempfile,json,hashlib,shutil,os,stat
r=Path('/home/staszek/MeshRoute');q=Path(tempfile.mkdtemp(prefix='mr-s8b-r2-'));g=q/'snapshot';(q/'logs').mkdir();Path('/tmp/mr-s8b-r2-active').write_text(str(q)+'\n')
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():v['target']=os.readlink(p)
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
(q/'inputs.json').write_text(json.dumps(inv,indent=2)+'\n');base='c07b77f50a16342d532618f760aad49e10208a5d';assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip()==base
changed=subprocess.check_output(['git','diff','--name-only'],cwd=r,text=True).splitlines();assert all(p.startswith('docs/') or p in ['MEMORY.md','tracker.md'] for p in changed)
brief='docs/superpowers/plans/2026-09-18-radmin-slice8b-mobile-controller-carrier.md';assert '**Revision 2' in (r/brief).read_text();prep=changed+[brief];(q/'preparation-inputs.json').write_text(json.dumps({p:inv[p] for p in prep},indent=2)+'\n')
sim=Path('/home/staszek/lora-universal-simulator');simhead=subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim,text=True).strip();assert simhead=='6585649ea5a780f0542b2931853a667be56a5b2b';assert not subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim)
(q/'state.json').write_text(json.dumps(dict(base=base,brief=brief,brief_sha256=inv[brief]['sha256'],simulator_head=simhead,input_paths=len(inv)),indent=2)+'\n');(q/'preparation.patch').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=r))
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(g)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for rel in paths:
 src=r/rel;dst=g/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(g/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
 else:shutil.copy2(src,dst)
print(q);print(inv[brief]['sha256']);print(len(inv),'paths; QA preparation',len(prep))
