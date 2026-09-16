from pathlib import Path
import subprocess,json,hashlib,os,time,sys
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';mode=sys.argv[1];env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
if mode=='chain2':
 cmds=[]
 for p in ['console_sink','inbox_verbs','firmware_ui','custody_usb','ble_line','features']:
  cmds += [(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])]
 cmds += [('inbox-client',['env','MR_PROBE_ARM=client','bash','tools/probe_inbox_verbs/run.sh']),('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py']),('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('census',['bash','tools/warning_census.sh']),('whitespace',['git','diff','--check'])]
else:
 build=q/'sim-final';cmds=[('configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(build),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('build',['cmake','--build',str(build),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(build/'orchestrator/lus'),'--jobs','3','--require-anchors']),('validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final')])]
results=[]
for name,cmd in cmds:
 print('START',mode,name,flush=True);start=time.monotonic();log=q/'logs'/(mode+'-'+name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/(mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n');print('END',mode,name,rc,flush=True)
 if rc:raise SystemExit(rc)
