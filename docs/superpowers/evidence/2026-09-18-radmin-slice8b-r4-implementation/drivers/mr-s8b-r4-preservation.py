from pathlib import Path
import hashlib,json,subprocess,os
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8b-r4-_d7gpj1e');o=r/'docs/superpowers/evidence/2026-09-18-radmin-slice8b-r4-implementation';sim=Path('/home/staszek/lora-universal-simulator')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
initial=json.loads((q/'inputs.json').read_text());cand=json.loads((q/'final-gate-inputs.json').read_text());prep=json.loads((q/'preparation-inputs.json').read_text())
primary=[p for p in cand if p.startswith(('lib/','src/','test/','tools/')) or p=='platformio.ini']
checks={}
for label,tree in [('shared',r),('candidate_snapshot',q/'final-gate'),('b413_snapshot',q/'iter-gate')]:
 wrong=[p for p in primary if not (tree/p).is_file() or sha(tree/p)!=cand[p]];checks[label]={'paths':len(primary),'mismatches':wrong};assert not wrong,(label,wrong)
def unchanged(p,v):
 file=r/p
 if 'target' in v:return file.is_symlink() and os.readlink(file)==v['target']
 return file.is_file() and sha(file)==v['sha256']
currentchanges=[p for p,v in initial.items() if not unchanged(p,v)]
owned=set(json.loads((o/'candidate-changed-inputs.json').read_text()))|{'docs/2026-07-30-open-bug-register.md','docs/superpowers/evidence/2026-09-18-radmin-slice8b.md','docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md'}
unexpected=[p for p in currentchanges if p not in owned];assert not unexpected,unexpected
prep_result={p:{'initial':entry['sha256'],'current':sha(r/p),'unchanged':entry['sha256']==sha(r/p)} for p,entry in prep.items()}
assert all(x['unchanged'] for p,x in prep_result.items() if p!='docs/2026-07-30-open-bug-register.md')
def git(root,*args):return subprocess.check_output(['git',*args],cwd=root).decode().strip()
whitespace={}
for name,tree in [('MeshRoute',r),('simulator',sim)]:
 c=subprocess.run(['git','diff','--check'],cwd=tree,capture_output=True,text=True);whitespace[name]={'exit':c.returncode,'stdout':c.stdout,'stderr':c.stderr};assert c.returncode==0
stop=json.loads((q/'stopped-gates.json').read_text());remaining=[]
for pid in stop['signalled']:
 proc=Path('/proc')/str(pid)
 if proc.exists():
  try:
   cmd=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
   if cmd:remaining.append({'pid':pid,'command':cmd})
  except FileNotFoundError:pass
assert not remaining,remaining
sim_status=git(sim,'status','--porcelain');assert not sim_status
brief=json.loads((q/'state.json').read_text())
assert git(r,'rev-parse','HEAD')==brief['base'];assert git(sim,'rev-parse','HEAD')==brief['simulator_head'];assert sha(r/brief['brief'])==brief['brief_sha256']
report={'verdict':'STOP-1 B413; partial implementation, no freeze or full gate PASS','base':git(r,'rev-parse','HEAD'),'simulator_head':git(sim,'rev-parse','HEAD'),'simulator_status':sim_status,'brief_sha256':sha(r/brief['brief']),'candidate_input_count':len(cand),'primary_identity':checks,'preparation_inputs':prep_result,'original_input_count':len(initial),'changed_original_paths':currentchanges,'unexpected_original_changes':unexpected,'whitespace':whitespace,'task_gate_processes_remaining':remaining,'staged_paths':git(r,'diff','--cached','--name-only'),'shared_status':git(r,'status','--short')}
(o/'preservation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['base','simulator_head','brief_sha256','primary_identity','unexpected_original_changes','whitespace','task_gate_processes_remaining','staged_paths']},indent=2))
