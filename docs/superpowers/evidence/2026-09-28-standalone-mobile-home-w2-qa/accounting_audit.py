"""Independent QA accounting of this run's trace; omission controls use the reviewed source-based reconciler."""
from pathlib import Path
import subprocess,tempfile,json,re,shlex
r=Path('/home/staszek/MeshRoute');e=r/'docs/superpowers/evidence';q=Path(__file__).resolve().parent;c=e/'2026-09-28-standalone-mobile-home-w2';raw=r/'artifacts/2026-09-28-standalone-mobile-home-w2-qa'
trace=(raw/'board-ui.trace').read_text(); lines=trace.splitlines(); counts={}
for fn,field,want in [('wchk_in','w_ctl',186),('wchk_in','w_pass',60),('schk','s_pass',23),('trait_control','trait_pass',14),('missing_trait_control','missing_pass',12)]:
 vals=[int(m.group(1)) for line in lines if (m:=re.match(r'^\+'+fn+r'@\d+\|'+field+r'=(\d+)$',line))]
 assert vals==list(range(1,want+1)),(field,vals)
 counts[field]={'observed':len(vals),'ordered_1_through_expected':True}
assert not any(re.search(r'\|(w_fail|s_fail|trait_fail|missing_fail)=[1-9]',line) for line in lines)
log=(raw/'board-ui.log').read_text();assert not any(s in log for s in ['command not found','syntax error','unbound variable','Traceback'])
def is_w1(line):
 if not re.match(r'^\+wchk_in@\d+\|local file=',line):return False
 args=shlex.split(line.split('|',1)[1]); kv=dict(x.split('=',1) for x in args[1:])
 return kv['label'].split()[0]=='W1'
neg=[]
for name,remove in [('missing-check',is_w1),('missing-red',lambda line:re.match(r'^\+wchk_in@\d+\|w_ctl=186$',line))]:
 hits=sum(bool(remove(line)) for line in lines);assert hits==1,(name,hits)
 with tempfile.TemporaryDirectory(prefix='w2-qa-accounting-') as tmp:
  p=Path(tmp);(p/'trace').write_text('\n'.join(line for line in lines if not remove(line))+'\n')
  run=subprocess.run(['python3','-B',str(c/'reconcile.py'),str(r/'tools/probe_board_ui/run.sh'),str(r/'tools/probe_board_ui/negctl.py'),str(p/'trace'),str(raw/'board-ui.log'),str(p/'result.json'),str(p/'result.tsv')],capture_output=True,text=True,check=True)
  result=json.loads((p/'result.json').read_text());assert result['summary']['verdict']=='FAIL'
  neg.append({'case':name,'removed_lines':hits,'result':result['summary']['verdict'],'problems':result['summary']['problems']})
(q/'step4-independent-accounting.json').write_text(json.dumps({'counts':counts,'omission_controls':neg,'helper_scripts_reviewed_and_rerun':['declared.py','reconcile.py','per_control.py'],'QA_setup_correction':'Initial omission-selector regex expected label=quoted-value but bash quotes the whole assignment. It matched zero and failed loud before a result; fixed using shlex. Stock trace and all instrument results unchanged.'},indent=2)+'\n')
print('Trace increments PASS; missing-check and missing-RED self-controls rejected')
