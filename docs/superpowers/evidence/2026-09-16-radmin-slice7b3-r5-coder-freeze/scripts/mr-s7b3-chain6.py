from pathlib import Path
import subprocess,json,hashlib,os,time,sys
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';mode=sys.argv[1];env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
cmds=[('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('whitespace',['git','diff','--check'])]
results=[]
for name,cmd in cmds:
 print('START',mode,name,flush=True);start=time.monotonic();log=q/'logs'/(mode+'-'+name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/(mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n');print('END',mode,name,rc,flush=True)
 if rc:raise SystemExit(rc)
