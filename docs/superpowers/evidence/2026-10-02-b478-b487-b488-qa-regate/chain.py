from pathlib import Path
import subprocess,time,json,hashlib,os,sys
r=Path('/home/staszek/MeshRoute');raw=r/'artifacts/2026-10-02-b478-b487-b488-qa-regate';sc=Path(__import__('tempfile').mkdtemp(prefix='mr-qa-regate-transcripts-')); (raw/'transcript-root.txt').write_text(str(sc));rows=[]
steps=[('scope',['python3','-B',str(raw/'preservation.py')],None),('syntax',['python3','-B',str(raw/'static_review.py')],None),('part-a',['python3','-B','tools/test_mutation_unusable_reason.py','-v'],None),('selftest',['/usr/bin/python3','-B','tools/probe_ui_model_mutations.py','--selftest-unusable'],dict(os.environ,PATH='')),('controls-a',['python3','-B',str(raw/'controls_a.py')],None),('mut-default',['python3','-B','tools/probe_ui_model_mutations.py','--target=w4aident'],None),('mut-w1',['python3','-B','tools/probe_ui_model_mutations.py','--target=w4aident','--workers=1'],None)]
for profile in ['full_headless','mobile']:
 for n in [1,2]:
  out=sc/f'{profile}-{n}';steps.append((f'transcript-{profile}-{n}',['python3','-B','tools/probe_inbox_verbs/transcript.py','--profile',profile,'--out',str(out),'--emit',str(out/'ledger.txt')],None))
 steps.append((f'compare-{profile}',['python3','-B','tools/probe_inbox_verbs/transcript.py','--compare',str(sc/f'{profile}-1/ledger.txt'),str(sc/f'{profile}-2/ledger.txt')],None))
steps += [('part-b',['python3','-B','tools/test_probe_inbox_transcript.py','-v'],None),('discovery',['python3','-B','-m','unittest','discover','-s','tools','-p','test_*.py'],None),('inventory',['python3','-B','tools/gen_command_inventory.py','--check'],None),('census',['python3','-B',str(raw/'static_review.py')],None),('reader-audit',['python3','-B',str(raw/'reader_audit.py')],None),('stability',['python3','-B',str(raw/'preservation.py')],None)]
for name,cmd,env in steps:
 print('START',name,flush=True);t=time.monotonic();log=raw/(name+'.log')
 with log.open('wb') as f:p=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT)
 row={'step':name,'command':cmd,'exit':p.returncode,'seconds':round(time.monotonic()-t,3),'log':str(log.relative_to(r)),'sha256':hashlib.sha256(log.read_bytes()).hexdigest()};rows.append(row);(raw/'runs.json').write_text(json.dumps(rows,indent=2)+'\n');print('END',name,p.returncode,row['seconds'],flush=True)
 if p.returncode:
  print(log.read_text(errors='backslashreplace')[-5000:],flush=True);sys.exit(1)
