"""Seal W8 evidence and prove all entry inputs preserved except the one registered finding.
Replay only from an unsealed scratch copy; no production edit is performed.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path('/home/staszek/MeshRoute');SIM=Path('/home/staszek/lora-universal-simulator');OUT=Path(__file__).resolve().parent
REPORT=OUT.parent/(OUT.name+'.md');PREFIX='docs/superpowers/evidence/'+OUT.name

def command(args,cwd):return subprocess.check_output(args,cwd=cwd).decode().strip()
def fingerprint(p):
 if p.is_symlink():return {'symlink':os.readlink(p)}
 return {'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
def inventory(root):
 names=subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root).decode().split('\0')
 return {n:fingerprint(root/n)for n in sorted(set(names)-{''})}
entry=json.loads((OUT/'inputs.json').read_text());finding=json.loads((OUT/'register-finding.json').read_text());now=inventory(ROOT)
reg='docs/2026-07-30-open-bug-register.md'
changed=[p for p,v in entry['files'].items()if now.get(p)!=v];assert changed==[reg],changed
assert now[reg]['sha256']==finding['final_sha256']
s=(ROOT/reg).read_text();assert s.count(finding['row'])==1
prior=s.replace('\n'+finding['row'],'').replace('The next free finding is **B498** (B497 registered by the W8 pre-check, 2026-10-03: the reprovisioned enum comment overstates a no-air guarantee)','The next free finding is **B497**')
assert hashlib.sha256(prior.encode()).hexdigest()==entry['files'][reg]['sha256']
added=sorted(now.keys()-entry['files'].keys());assert all(p==PREFIX+'.md' or p.startswith(PREFIX+'/')for p in added),added
assert command(['git','rev-parse','HEAD'],ROOT)==entry['head']
assert not command(['git','diff','--cached','--name-only'],ROOT)
assert inventory(SIM)==entry['simulator']['files']
assert command(['git','rev-parse','HEAD'],SIM)==entry['simulator']['head']
assert not command(['git','status','--porcelain=v1'],SIM)
subprocess.run(['git','diff','--check'],cwd=ROOT,check=True);subprocess.run(['git','diff','--check'],cwd=SIM,check=True)
result={'verdict':'PASS','head':entry['head'],'simulator_head':entry['simulator']['head'],'entry_inputs':len(entry['files']),'simulator_inputs':len(entry['simulator']['files']),'allowed_register_change':finding,'changed_or_missing_other_inputs':[],'added_paths':added,'staged':[],'simulator_clean':True,'whitespace_check':'PASS'}
(OUT/'preservation-final.json').write_text(json.dumps(result,indent=2)+'\n')
files=sorted(p for p in OUT.rglob('*')if p.is_file()and p.name!='SHA256SUMS')
seal=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(OUT).as_posix()+'\n'for p in files)
seal+=hashlib.sha256(REPORT.read_bytes()).hexdigest()+'  ../'+REPORT.name+'\n';(OUT/'SHA256SUMS').write_text(seal)
for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)',REPORT.read_text()):
 if '://' not in target:assert (REPORT.parent/target.split('#',1)[0]).exists(),target
checked=subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=OUT,capture_output=True,text=True,check=True)
assert len(checked.stdout.splitlines())==len(files)+1
print(json.dumps({'preservation':'PASS','entry_inputs':len(entry['files']),'simulator_inputs':len(entry['simulator']['files']),'seal_entries':len(files)+1,'seal_sha256':hashlib.sha256((OUT/'SHA256SUMS').read_bytes()).hexdigest(),'report_sha256':hashlib.sha256(REPORT.read_bytes()).hexdigest()},indent=2))
