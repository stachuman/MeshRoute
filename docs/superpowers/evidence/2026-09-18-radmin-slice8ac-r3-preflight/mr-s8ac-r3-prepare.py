from pathlib import Path
import subprocess,tempfile,json,hashlib,shutil,os,stat
r=Path('/home/staszek/MeshRoute');q=Path(tempfile.mkdtemp(prefix='mr-s8ac-r3-'));g=q/'snapshot';(q/'logs').mkdir()
Path('/tmp/mr-s8ac-r3-active').write_text(str(q)+'\n')
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
(q/'inputs.json').write_text(json.dumps(inv,indent=2)+'\n')
brief='docs/superpowers/plans/2026-09-17-radmin-slice8ac-controller-and-local-delivery.md'
assert '**Revision 3' in (r/brief).read_text()
base='6086152b97b5934971d231a345b9939d5f2db1e9';head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip()
changed=subprocess.check_output(['git','diff','--name-only',base],cwd=r,text=True).splitlines();assert all(p.startswith('docs/') or p in ['MEMORY.md','tracker.md'] for p in changed)
prep=[p for p in inv if p in ['MEMORY.md','tracker.md',brief,'docs/2026-07-30-open-bug-register.md','docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md','docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md']]
(q/'preparation-inputs.json').write_text(json.dumps({p:inv[p] for p in prep},indent=2)+'\n')
for p in prep:
 dst=q/'preparation-files'/p;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/p,dst)
sim=Path('/home/staszek/lora-universal-simulator');simhead=subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim,text=True).strip();assert simhead=='06746a97de5764415d6fcef10b97bca90569b9c7';assert not subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim)
(q/'state.json').write_text(json.dumps(dict(head=head,base=base,brief=brief,brief_sha256=inv[brief]['sha256'],simulator_head=simhead,input_count=len(inv)),indent=2)+'\n')
(q/'preparation.patch').write_bytes(subprocess.check_output(['git','diff','--binary',base],cwd=r))
subprocess.run(['git','clone','--shared','--no-hardlinks',str(r),str(g)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
for rel in paths:
 src=r/rel;dst=g/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():
  resolved=src.resolve();dst.unlink(missing_ok=True);dst.symlink_to(g/resolved.relative_to(r) if resolved.is_relative_to(r) else resolved)
 else:shutil.copy2(src,dst)
script=Path('/tmp/mr-s8ac-layout.py').read_text().replace('/tmp/mr-codex-s8ac-preflight-active','/tmp/mr-s8ac-r3-active')
script=script.replace('    uintptr_t usb_sink;                       // supplied USB Print, must outlive request; zero for BLE\\n','').replace('static_assert(sizeof(s8model::PendingCore::usb_sink) == sizeof(void*));\\n','')
(q/'layout-driver.py').write_text(script)
print(q,inv[brief]['sha256'],flush=True)
subprocess.run(['python',str(q/'layout-driver.py')],check=True,cwd=g)
