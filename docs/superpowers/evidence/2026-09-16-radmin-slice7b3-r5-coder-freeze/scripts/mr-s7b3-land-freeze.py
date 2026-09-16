from pathlib import Path
import hashlib,json,subprocess,shutil,os
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=Path('/home/staszek/MeshRoute')
receipt_rel='docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md'
dest=r/'docs/superpowers/evidence/2026-09-16-radmin-slice7b3-r5-coder-freeze'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not dest.exists(), 'Do not overwrite an existing freeze'
chain=json.loads((q/'chain6-results.json').read_text())
assert len(chain)==9 and all(x['exit']==0 for x in chain), chain
tools=(q/'logs/chain6-tools.log').read_text()
assert 'Ran 351 tests in ' in tools and '\nOK\n' in tools and 'skipped=' not in tools
union=json.loads((q/'union2/summary.json').read_text())
assert (union['batteries'],union['configured'],union['red'],union['unusable'],union['vacuous'])==(56,880,879,1,0)
for a in json.loads((q/'archives/archives.json').read_text()):
 assert digest(q/'archives'/a['path'])==a['sha256'],a['path']
resume=json.loads((q/'resume-inputs.json').read_text())
assert digest(r/receipt_rel)==resume[receipt_rel]['sha256'], 'Receipt changed concurrently'
subprocess.run(['python3','/tmp/mr-s7b3-freeze-inputs.py'],check=True)
# Stage the record before append; no production, test, tool or maintained QA document edits here.
stage=q/'freeze-stage'
shutil.copytree(stage,dest)
for p in q.glob('*.json'):shutil.copy2(p,dest/p.name)
for name in ['preparation.patch','status-before.txt','reference-corrupt.cpp']:shutil.copy2(q/name,dest/name)
for name in ['logs','action-final3','action-final3-no-neg','union2']:
 shutil.copytree(q/name,dest/name)
shutil.copytree(q/'union',dest/'union-interrupted')
for p in (q/'archives').iterdir():shutil.copy2(p,dest/p.name)
scripts=dest/'scripts';scripts.mkdir()
for p in Path('/tmp').glob('mr-s7b3-*.py'):shutil.copy2(p,scripts/p.name)
shutil.copy2('/tmp/mr-s7b3-evidence-readme.md',dest/'README.md')
body=Path('/tmp/mr-s7b3-receipt-draft.md').read_text().replace('| Full discovery, no skips with real measured ELF;', '| **351 passed /0 skipped**, real measured ELF;')
assert '**351 passed /0 skipped**' in body
prior=(r/receipt_rel).read_bytes();(r/receipt_rel).write_bytes(prior+b'\n'+body.encode())
(dest/'receipt-binding.json').write_text(json.dumps(dict(path=receipt_rel,prior_sha256=hashlib.sha256(prior).hexdigest(),final_sha256=digest(r/receipt_rel),new_section_line=prior.count(b'\n')+2),indent=2)+'\n')
# Final source/doc preservation check after writing outputs, excluding only this declared output set.
inv=json.loads((stage/'inputs-at-freeze-before-receipt.json').read_text())
for rel,v in inv.items():
 if rel==receipt_rel:continue
 p=r/rel
 if p.is_symlink():
  assert os.readlink(p)==v['target'],rel
  if 'resolved_sha256' in v:assert digest(p)==v['resolved_sha256'],rel
 else:assert digest(p)==v['sha256'],rel
checks=[]
for label,cwd in [('shared',r),('private',q/'gate'),('simulator',Path('/home/staszek/lora-universal-simulator'))]:
 result=subprocess.run(['git','diff','--check'],cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (dest/'logs'/('freeze-'+label+'-whitespace.log')).write_bytes(result.stdout)
 assert result.returncode==0,(label,result.stdout)
 checks.append(dict(label=label,cwd=str(cwd),command=['git','diff','--check'],exit=result.returncode))
assert not subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],cwd='/home/staszek/lora-universal-simulator')
(dest/'final-integrity.json').write_text(json.dumps(dict(checks=checks,simulator_clean=True,input_contents_preserved=len(inv)-1,receipt_prefix_preserved=True),indent=2)+'\n')
for paths in json.loads((dest/'final-gate-index.json').read_text()).values():
 if isinstance(paths,list):
  for name in paths:assert (dest/name).exists(), ('missing final instrument',name)
index={str(p.relative_to(dest)):dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(dest.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'}
(dest/'artifact-sha256.json').write_text(json.dumps(index,indent=2)+'\n')
print('FREEZE',dest);print('ARTIFACTS',len(index));print('RECEIPT SHA256',digest(r/receipt_rel));print('INDEX SHA256',digest(dest/'artifact-sha256.json'))
