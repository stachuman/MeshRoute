from pathlib import Path
import concurrent.futures,subprocess,json,time,hashlib,os
q=Path('/tmp/mr-s8ac-r4-b1edsao2');r=q/'final-gate';logs=q/'final-logs';logs.mkdir(exist_ok=True);env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
groups=[]
for p in ['inbox_verbs','firmware_ui','console_sink','custody_usb','ble_line','features','deferred_actions']:
 groups.append([(p,['bash','tools/probe_'+p+'/run.sh']),(p+'-no-neg',['bash','tools/probe_'+p+'/run.sh','--no-neg'])])
groups.insert(1,[('inbox-client',['env','MR_PROBE_ARM=client','bash','tools/probe_inbox_verbs/run.sh'])])
groups.append([('abi',['python3','tools/probe_board_abi.py']),('abi-b278',['python3','tools/probe_b278_row_abi.py'])])
groups.append([('tools',['python3','-m','unittest','discover','-s','tools','-p','test_*.py'])])
groups.append([('inventory-write',['python3','tools/gen_command_inventory.py','--write']),('inventory-bare',['python3','tools/gen_command_inventory.py']),('inventory-check',['python3','tools/gen_command_inventory.py','--check']),('authority',['python3','tools/check_command_authority.py']),('authority-controls',['python3','tools/check_command_authority.py','--selftest']),('a0',['python3','tools/check_a0_matrix.py']),('literals',['python3','tools/check_data_type_literals.py']),('whitespace',['git','diff','--check'])])
# The full tools suite is independent; start it alongside the longest executable probes.
groups.insert(3,groups.pop(-2))
results=[]
def group_run(group):
 rows=[]
 for name,cmd in group:
  print('START',name,flush=True);start=time.monotonic();log=logs/(name+'.log')
  with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
  result=dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest());rows.append(result)
  (logs/(name+'-result.json')).write_text(json.dumps(result,indent=2)+'\n');print('END',name,rc,flush=True)
  if rc:break
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for future in concurrent.futures.as_completed([pool.submit(group_run,g) for g in groups]):
  results.extend(future.result());(q/'final-chain-results.json').write_text(json.dumps(results,indent=2)+'\n')
assert not [r for r in results if r['exit']],[(r['name'],r['exit']) for r in results if r['exit']]
print('FINAL CHAIN COMPLETE',len(results),flush=True)
