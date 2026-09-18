from pathlib import Path
import subprocess,json,hashlib,os,time,sys
q=Path('/tmp/mr-s8ac-r4-b1edsao2');r=q/'gate';mode=sys.argv[1];env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
if mode=='chain':
 cmds=[('abi',['python3','tools/probe_board_abi.py']),('abi-b278',['python3','tools/probe_b278_row_abi.py'])]
 for p in ['console_sink','inbox_verbs','firmware_ui','custody_usb','ble_line','features','deferred_actions']:
  cmds += [(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])]
 cmds += [('inbox-client',['env','MR_PROBE_ARM=client','bash','tools/probe_inbox_verbs/run.sh']),('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('whitespace',['git','diff','--check'])]
elif mode=='sim':
 build=q/'sim-final';cmds=[('configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(build),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES='+str(q/'sim-controller.cmake')]),('build',['cmake','--build',str(build),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(build/'orchestrator/lus'),'--jobs','3','--require-anchors']),('validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final')])]
elif mode=='boards':
 r=q/'board-gate'
 cmds=[('pair',['python3','tools/measure_board.py','pair','--output','.pio-measure/s8ac-final','--jobs','1']),('xiao-mobile',['pio','run','-e','xiao_mobile','-j','1']),('census',['bash','tools/warning_census.sh'])]
else:raise SystemExit(mode)
results=json.loads((q/(mode+'-results.json')).read_text()) if (q/(mode+'-results.json')).exists() else []
passed={r['name'] for r in results if r['exit']==0}
for name,cmd in cmds:
 if name in passed: continue
 print('START',mode,name,flush=True);start=time.monotonic();log=q/'logs'/(mode+'-'+name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/(mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n');print('END',mode,name,rc,flush=True)
 if rc:raise SystemExit(rc)
