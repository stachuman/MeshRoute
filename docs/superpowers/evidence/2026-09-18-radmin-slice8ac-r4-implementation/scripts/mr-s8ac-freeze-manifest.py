from pathlib import Path
import subprocess,json,hashlib,os,stat,datetime
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8ac-r4-b1edsao2');a=r/'docs/superpowers/evidence/2026-09-18-radmin-slice8ac-r4-implementation'
assert json.loads((q/'final-gate-summary.json').read_text())['mutation_red']==917
skip={str((a/n).relative_to(r)) for n in ['freeze-inputs.json','freeze.json']}
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p))
inv={}
for rel in paths:
 if rel in skip:continue
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip();assert head=='e3a5fa03eab11806b05094953a04e7c1524f221a'
qa=json.loads((q/'preparation-inputs.json').read_text());assert all(inv[p]['sha256']==v['sha256'] for p,v in qa.items())
sim=Path('/home/staszek/lora-universal-simulator');assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim,text=True).strip()=='06746a97de5764415d6fcef10b97bca90569b9c7';assert not subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim)
p=a/'freeze-inputs.json';p.write_text(json.dumps(inv,indent=2)+'\n');table='docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md';receipt='docs/superpowers/evidence/2026-09-17-radmin-slice8ac.md'
f=dict(timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_base='6086152b97b5934971d231a345b9939d5f2db1e9',head=head,input_paths=len(inv),inventory_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),inventory_excludes=sorted(skip),receipt=receipt,receipt_sha256=inv[receipt]['sha256'],qa_preparation_unchanged=True,simulator_head='06746a97de5764415d6fcef10b97bca90569b9c7',simulator_clean=True,shared_authority_table_sha256=inv[table]['sha256'],tested_candidate_authority_table_sha256=hashlib.sha256((q/'final-gate'/table).read_bytes()).hexdigest(),candidate_table_patch='r42-authority-transcription.patch',full_gate_summary_sha256=hashlib.sha256((a/'audit/final-gate-summary.json').read_bytes()).hexdigest())
(a/'freeze.json').write_text(json.dumps(f,indent=2)+'\n');print(json.dumps(f,indent=2))
