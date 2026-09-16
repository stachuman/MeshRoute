from pathlib import Path
import hashlib,json,subprocess,os,stat,tarfile,shutil
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-codex-s7b3-0gt630zl');gate=q/'gate';out=q/'freeze-stage';out.mkdir(exist_ok=True)
brief='docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md';assert hashlib.sha256((r/brief).read_bytes()).hexdigest()=='f1d38f8673a411fff3d4f2d6c280daf96b015b86d13c97b882b6ce33094621c8'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip()=='7442e6f570abdcd74ceed20d4c0cb9e2855d0719'
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p));inv={}
for rel in paths:
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 inv[rel]=v
(out/'inputs-at-freeze-before-receipt.json').write_text(json.dumps(inv,indent=2)+'\n')
resume=json.loads((q/'resume-inputs.json').read_text());deltas={p:dict(resume=resume.get(p),final=inv.get(p)) for p in sorted(set(resume)|set(inv)) if resume.get(p)!=inv.get(p)}
(out/'input-deltas-since-resume.json').write_text(json.dumps(deltas,indent=2)+'\n')
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=r).decode().splitlines())|set(subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=r).decode().splitlines())
impl=sorted(p for p in changed if p.startswith(('lib/','src/','test/','tools/')) or p=='platformio.ini')
for p in impl:assert (r/p).read_bytes()==(gate/p).read_bytes(),p
(out/'implementation-inputs.json').write_text(json.dumps({p:inv[p] for p in impl},indent=2)+'\n')
generated='docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md'
prep={p:resume[p] for p in changed if p in resume and p!=generated and (p.startswith('docs/') or p in ['AGENTS.md','CLAUDE.md','MEMORY.md','tracker.md'])}
for p in prep:assert inv[p]==resume[p], ('preparation changed after re-pin',p)
for p in deltas:assert p in impl or p==generated, ('unexpected input changed',p)
assert (r/generated).read_bytes()==(gate/generated).read_bytes()
for p,h in json.loads((q/'union2/native-inputs.json').read_text()).items():
 assert hashlib.sha256((r/p).read_bytes()).hexdigest()==h, ('union input changed',p)
 assert hashlib.sha256((gate/p).read_bytes()).hexdigest()==h, ('gate union input changed',p)
assert not subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],cwd='/home/staszek/lora-universal-simulator')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd='/home/staszek/lora-universal-simulator').decode().strip()=='06746a97de5764415d6fcef10b97bca90569b9c7'

(out/'permitted-preparation.json').write_text(json.dumps(prep,indent=2,sort_keys=True)+'\n')
(out/'frozen.patch').write_bytes(subprocess.check_output(['git','diff','--binary','HEAD'],cwd=r));(out/'status-at-freeze.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=r));shutil.copy2(r/brief,out/'authorized-brief.md')
# Replay this archive after the base checkout and frozen.patch: it includes every pre-existing untracked QA input,
# plus the actual untracked implementation. The evidence being assembled is excluded because it did not exist yet.
untracked=subprocess.check_output(['git','ls-files','--others','--exclude-standard','-z'],cwd=r).split(b'\0')
with tarfile.open(out/'untracked-inputs.tar.xz','w:xz',preset=3) as tf:
 for raw in untracked:
  if raw:tf.add(r/raw.decode(),arcname=raw.decode(),recursive=False)
(out/'state.json').write_text(json.dumps(dict(head='7442e6f570abdcd74ceed20d4c0cb9e2855d0719',brief=brief,brief_sha256=hashlib.sha256((r/brief).read_bytes()).hexdigest(),simulator_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd='/home/staszek/lora-universal-simulator').decode().strip(),private_complete_checkout=str(gate),input_count=len(inv),implementation_count=len(impl),preparation_count=len(prep)),indent=2)+'\n');print('FROZEN INPUTS',len(inv),'IMPLEMENTATION',len(impl),'PREPARATION',len(prep))
