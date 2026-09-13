from pathlib import Path
import hashlib,json,subprocess,stat,os,shutil,difflib
r=Path('/home/staszek/MeshRoute');sim=Path('/home/staszek/lora-universal-simulator');q=Path(__file__).resolve().parent;coder=Path('/tmp/mr-codex-b388-87e0Pk');e=r/'docs/superpowers/evidence/2026-09-09-radmin-slice7b2-qa';prefix=str(e.relative_to(r))+'/'
sha=lambda b:hashlib.sha256(b).hexdigest()
def inventory(root):
 paths=sorted(set(x.decode() for x in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).split(b'\0') if x));result={}
 for name in paths:
  p=root/name;o=dict(kind='symlink' if p.is_symlink() else 'file',mode=stat.S_IMODE(p.lstat().st_mode))
  if p.is_symlink():o['target']=os.readlink(p)
  else:o['sha256']=sha(p.read_bytes())
  result[name]=o
 return result
before=inventory(r);sim_before=inventory(sim);(q/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n');(q/'simulator-before.json').write_text(json.dumps(sim_before,indent=2)+'\n')
expected=json.loads((e/'inputs-before.json').read_text());post=json.loads((e/'post-landing-integrity.json').read_text())
for p,delta in post['qa_documentation_changes'].items():assert expected[p]==delta['before'];expected[p]=delta['after']
report='docs/superpowers/evidence/2026-09-09-radmin-slice7b2-qa-gate.md';expected[report]=dict(mode=0o644,kind='file',sha256=post['qa_report_sha256'])
files=json.loads((e/'artifact-files.json').read_text())
for p,h in files.items():assert sha((e/p).read_bytes())==h;expected[prefix+p]=dict(mode=0o644,kind='file',sha256=h)
expected[prefix+'artifact-files.json']=dict(mode=0o644,kind='file',sha256=sha((e/'artifact-files.json').read_bytes()))
assert before.keys()==expected.keys();changed=[p for p in expected if expected[p]!=before[p]]
rx='lib/core/node_mac_rx.cpp';receipt='docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md';assert sorted(changed)==sorted([rx,receipt]),changed
assert all(expected[p]['mode']==before[p]['mode'] for p in changed)
oldroot=Path('/tmp/mr-qa-s7b2-gate-0phs0ag4/frozen');old=(oldroot/rx).read_bytes();new=(r/rx).read_bytes();a=old.splitlines(keepends=True);b=new.splitlines(keepends=True);assert len(a)==len(b)==3345
changed_lines=[i for i,(x,y) in enumerate(zip(a,b)) if x!=y];assert changed_lines==[2271,2272,2273],changed_lines
for i in changed_lines:
 for l in [a[i],b[i]]:assert l.lstrip().startswith(b'//') and b'\\' not in l
 reconstructed=list(b)
for i in changed_lines:reconstructed[i]=a[i]
assert b''.join(reconstructed)==old
assert sha(old)=='24addc50e6fe1a5a72e92e8e74c73697ae70d0cf4cce8045392dc75fd7108894';assert sha(new)=='4c1bee50d31298ba9ffcb0290ea936ae6a6ae309c5e52f8c847e043d74685eb6'
oldreceipt=(oldroot/receipt).read_bytes();assert (r/receipt).read_bytes().startswith(oldreceipt);assert sha(oldreceipt)==expected[receipt]['sha256']
(q/'source-comment.diff').write_text(''.join(difflib.unified_diff(old.decode().splitlines(True),new.decode().splitlines(True),fromfile='qa-gated/node_mac_rx.cpp',tofile='b388-return/node_mac_rx.cpp')))
# Independently compare the coder inventory, including modes/links, to our live scan.
ca=json.loads((coder/'after.json').read_text());cb=json.loads((coder/'before.json').read_text())
def normalized(x):
 out={}
 for p,v in x['inputs'].items():
  d=dict(kind=v['kind'],mode=int(v['mode'],8))
  if d['kind']=='file':d['sha256']=v['sha256']
  else:d['target']=v['target']
  out[p]=d
 return out
assert normalized(ca['meshroute'])==before;assert normalized(ca['simulator'])==sim_before
assert normalized(cb['meshroute'])==expected;assert normalized(cb['simulator'])==sim_before
assert sha((coder/'before.json').read_bytes())=='01572e48dffa1bb3bc166d2040994dcf619583ee3cabeaf5e54cc540bb6e33f1'
assert sha((coder/'after-inputs-except-receipt.json').read_bytes())=='4871f0a6e08c4431403de88615756af4a95fa1af68e949e2177c23537a10cacc'
repos={}
for name,root,head in [('MeshRoute',r,'564f460a3b755a104f146da70457e5c8c68e99b9'),('simulator',sim,'06746a97de5764415d6fcef10b97bca90569b9c7')]:
 actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();assert actual==head
 p=subprocess.run(['git','diff','--check'],cwd=root,capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
 repos[name]=dict(head=actual,diff_check_exit=0)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=sim,text=True)
# Full current tracked/untracked source snapshot, including all prior QA artifacts; never HEAD-only.
snapshot=q/'snapshot';snapshot.mkdir()
for name,v in before.items():
 p=r/name;d=snapshot/name;d.parent.mkdir(parents=True,exist_ok=True)
 if p.is_symlink():d.symlink_to(v['target'])
 else:shutil.copyfile(p,d);os.chmod(d,v['mode'])
assert inventory(r)==before and inventory(sim)==sim_before
result=dict(meshroute_inputs=len(before),simulator_inputs=len(sim_before),changed_since_qa=changed,unchanged_inputs=len(before)-len(changed),changed_lines_1_based=[i+1 for i in changed_lines],line_count=3345,exact_three_line_comment_replacement=True,receipt_append_only=True,rx_before_sha256=sha(old),rx_after_sha256=sha(new),receipt_after_sha256=sha((r/receipt).read_bytes()),prior_qa_report_sha256=sha((r/report).read_bytes()),old_qa_artifact_files_checked=len(files)+1,coder_inventory_matches=True,repositories=repos,simulator_clean=True)
(q/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
