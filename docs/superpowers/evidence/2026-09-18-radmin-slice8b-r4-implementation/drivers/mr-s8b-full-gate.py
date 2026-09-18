from pathlib import Path
import subprocess,json,hashlib,os,time,sys,concurrent.futures
q=Path('/tmp/mr-s8b-r4-_d7gpj1e');r=q/'final-gate';logs=q/'final-logs';logs.mkdir(exist_ok=True);mode=sys.argv[1]
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
def run(name,cmd,cwd=r):
 print('START',name,flush=True);start=time.monotonic();log=logs/(name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 row=dict(name=name,command=cmd,cwd=str(cwd),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest())
 (logs/(name+'-result.json')).write_text(json.dumps(row,indent=2)+'\n');print('END',name,rc,flush=True);return row
if mode=='chain':
 groups=[]
 for p in ['inbox_verbs','firmware_ui','console_sink','custody_usb','ble_line','features','deferred_actions']:
  groups.append([(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])])
 groups += [[('inbox-client',['env','MR_PROBE_ARM=client','bash','tools/probe_inbox_verbs/run.sh'])],
 [('abi',['python3','tools/probe_board_abi.py']),('abi-b278',['python3','tools/probe_b278_row_abi.py'])],
 [('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py'])],
 [('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('whitespace',['git','diff','--check'])],
 [('native-build',['env','PLATFORMIO_BUILD_DIR='+str(q/'native-build'),'pio','test','-e','native']),('native-binary',[str(q/'native-build/native/program')])]]
 def group(g):
  ans=[]
  for name,cmd in g:
   ans.append(run(name,cmd))
   if ans[-1]['exit']:break
  return ans
 results=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for result in pool.map(group,groups):results+=result
elif mode=='boards':
 results=[]
 for name,cmd in [('pair',['python3','tools/measure_board.py','pair','--output','.pio-measure/s8b-final','--jobs','1']),('xiao-mobile',['pio','run','-e','xiao_mobile','-j','1']),('census',['bash','tools/warning_census.sh'])]:
  results.append(run(name,cmd))
  if results[-1]['exit']:break
elif mode=='sim':
 b=q/'sim-final';results=[]
 for name,cmd in [('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(b),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('sim-build',['cmake','--build',str(b),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(b/'orchestrator/lus'),'--jobs','3','--require-anchors']),('corpus-validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final')])]:
  results.append(run(name,cmd))
  if results[-1]['exit']:break
else:raise SystemExit(mode)
(q/('final-'+mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n')
assert not [x for x in results if x['exit']],[(x['name'],x['exit']) for x in results if x['exit']]
print('GATE GROUP COMPLETE',mode,flush=True)
