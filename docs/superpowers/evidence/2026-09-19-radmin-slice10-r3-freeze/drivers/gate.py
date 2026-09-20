from pathlib import Path
import subprocess,json,hashlib,os,time,sys,concurrent.futures,shutil
q=Path(Path('/tmp/mr-s10-r3-active').read_text().strip());mode=sys.argv[1];r=q/('final-boards' if mode=='boards' else 'final-gate');logs=q/'final-logs';logs.mkdir(exist_ok=True)
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
assert 'GIT_INDEX_FILE' not in env
assert Path(shutil.which('git')).resolve()==Path('/usr/bin/git').resolve()
def run(name,cmd):
 print('START',name,flush=True);start=time.monotonic();log=logs/(name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 row=dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest())
 (logs/(name+'-result.json')).write_text(json.dumps(row,indent=2)+'\n');print('END',name,rc,flush=True);return row
def group(g):
 ans=[]
 for name,cmd in g:
  ans.append(run(name,cmd))
  if ans[-1]['exit']:break
 return ans
if mode=='chain':
 groups=[]
 for p in ['inbox_verbs','firmware_ui','console_sink','custody_usb','ble_line','features','deferred_actions']:
  groups.append([(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])])
 py='/home/staszek/mr-slice2-ref/bin/python'
 assert Path(py).exists()
 groups += [[('inbox-client',['env','MR_PROBE_ARM=client','bash','tools/probe_inbox_verbs/run.sh'])],
 [('abi',['python3','tools/probe_board_abi.py']),('abi-b278',['python3','tools/probe_b278_row_abi.py'])],
 [('reference',[py,'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py','--freeze-check','--compare','test/test_remote_codec.cpp','--selftest']),('reference-slice9',[py,'docs/superpowers/evidence/2026-09-19-radmin-slice9-reference.py','--freeze-check','--compare','test/test_remote_codec.cpp','--selftest'])],
 [('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('whitespace',['git','diff','--check'])],
 [('native-build',['env','PLATFORMIO_BUILD_DIR='+str(q/'native-build'),'pio','test','-e','native']),('native-binary',[str(q/'native-build/native/program')])]]
 groups.insert(0,groups.pop())
 results=[]
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  for result in pool.map(group,groups):results+=result
elif mode=='tools':
 source=q/'final-boards/.pio-measure/s10-final/gateway';dest=r/'.pio-measure/s10-fixture/gateway';dest.mkdir(parents=True,exist_ok=True)
 for name in ['manifest.json','firmware.elf']:
  assert (dest/name).is_file(), 'The fresh stock ELF fixture must exist before tools discovery'
 results=[run('tools',['python3','-m','unittest','discover','-v','-s','tools','-p','test_*.py'])]
elif mode=='boards':
 results=group([('pair',['python3','tools/measure_board.py','pair','--output','.pio-measure/s10-final','--jobs','1']),('xiao-mobile',['pio','run','-e','xiao_mobile','-j','1']),('census',['bash','tools/warning_census.sh'])])
elif mode=='sim':
 b=q/'sim-final';results=group([('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(b),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('sim-build',['cmake','--build',str(b),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(b/'orchestrator/lus'),'--jobs','3','--require-anchors']),('corpus-validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final')])])
else:raise SystemExit(mode)
(q/('final-'+mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n')
assert not [x for x in results if x['exit']],[(x['name'],x['exit']) for x in results if x['exit']]
print('GATE GROUP COMPLETE',mode,flush=True)
