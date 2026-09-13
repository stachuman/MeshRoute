import hashlib,json,os,shutil,subprocess,time
from pathlib import Path
q=Path(__file__).resolve().parent; root=q/'gate'; env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator',PYTHONUNBUFFERED='1')
commands=[('abi',['python3','tools/probe_board_abi.py']),('b278',['python3','tools/probe_b278_row_abi.py'])]
for p in ['console_sink','inbox_verbs','firmware_ui','custody_usb','ble_line','features']:
 commands += [(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])]
commands += [('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py']),('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literal-check',['python3','tools/check_data_type_literals.py']),('census',['bash','tools/warning_census.sh'])]
records=[]
for name,cmd in commands:
 if name=='tools':
  artifact=q/'measure/.pio-measure/qa-base/gateway'
  # Wait only for our independent pristine base measurement, never borrow the coder ELF.
  deadline=time.monotonic()+3600
  while not (artifact/'manifest.json').is_file():
   assert time.monotonic()<deadline,'Independent ELF unavailable'; time.sleep(5)
  shutil.copytree(artifact,root/'.pio-measure/qa-real-elf/gateway',dirs_exist_ok=True)
 if name=='census':
  deadline=time.monotonic()+7200
  while not (q/'boards-complete').is_file():
   assert time.monotonic()<deadline,'Independent board pairs unavailable'; time.sleep(5)
 print('START',name,flush=True); start=time.monotonic(); log=q/'logs'/('chain-'+name+'.log')
 with log.open('w') as f: rc=subprocess.run(cmd,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 entry=dict(name=name,cmd=cmd,rc=rc,seconds=time.monotonic()-start,log=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest());records.append(entry)
 (q/'chain-results.json').write_text(json.dumps(records,indent=2)+'\n')
 print('END',name,rc,round(entry['seconds'],1),flush=True); assert rc==0,(name,rc)
print('CHAIN COMPLETE',flush=True)
