from pathlib import Path
import os,hashlib,json,subprocess,tempfile
root=Path('/home/staszek/MeshRoute');raw=root/'artifacts/2026-09-26-standalone-mobile-home-w4a-precheck'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def snap(base):
 names=sorted(set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=base).decode().split('\0'))-{''});files={}
 for name in names:
  p=base/name;files[name]={'kind':'symlink','target':os.readlink(p)} if p.is_symlink() else {'kind':'file','sha256':sha(p)} if p.is_file() else {'kind':'missing'}
 return {'root':str(base),'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=base).decode().strip(),'status':subprocess.check_output(['git','status','--porcelain=v1','--untracked-files=all'],cwd=base).decode(),'files':files}
now=[snap(root),snap(Path('/home/staszek/lora-universal-simulator'))];assert now[0]['head']=='8360802904f7bd0023279d3da844453d61207ede';assert now[1]['head']=='6585649ea5a780f0542b2931853a667be56a5b2b' and not now[1]['status'];assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root).strip()
(raw/'inputs.json').write_text(json.dumps(now,indent=2)+'\n')
# W3's final landing is the latest full-tree authority, not HEAD alone.
w3=root/'docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w3-qa';before=json.loads((w3/'inputs.json').read_text());land=json.loads((w3/'preservation-postlanding.json').read_text());changed=[]
for i in range(2):
 allowed={x['path']:x['after'] for x in land[i]['authorized_changed']}
 for n,v in before[i]['files'].items():assert now[i]['files'].get(n)==allowed.get(n,v),n
 assert set(now[i]['files'])-set(before[i]['files'])==set(land[i]['added_qa_paths_at_check'])
changed=list(allowed)
# Verify both QA receipts, complete checksum inventories, and surviving W1c/W3 freeze hashes.
checks=[]
for tag in ['w1c','w1c-qa','w3','w3-qa']:
 d=root/('docs/superpowers/evidence/2026-09-25-standalone-mobile-home-'+tag);p=subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=d,text=True,capture_output=True);assert p.returncode==0,p.stdout+p.stderr;(raw/(tag+'-checksums.txt')).write_text(p.stdout);checks.append({'folder':str(d.relative_to(root)),'entries_verified':len(p.stdout.splitlines()),'index_sha256':sha(d/'SHA256SUMS')})
freeze=json.loads((w3/'preflight.json').read_text())['freeze_pins']
for n,h in freeze.items():assert sha(root/n)==h,n
result={'base_reconciles_exactly_to_w3_postlanding':True,'w1c_retained_as_part_of_w3_frozen_inputs':True,'inventoried_paths':[len(x['files']) for x in now],'checksums':checks,'w3_freeze_pins':freeze,'preparation':{n:sha(root/n) for n in ['AGENTS.md','docs/CODE_GUIDELINES.md','docs/2026-09-02-agent-roles.md','MEMORY.md','tracker.md','docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md','docs/2026-09-20-metal-test-plan.md','simulation/BASELINE.md']}}
(raw/'preflight.json').write_text(json.dumps(result,indent=2)+'\n');scratch=Path(tempfile.mkdtemp(prefix='meshroute-w4a-precheck-'));(raw/'scratch.json').write_text(json.dumps({'path':str(scratch)})+'\n');print(json.dumps(result,indent=2))
