from pathlib import Path
import subprocess,os,time,json,hashlib,py_compile,sys,re
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-qa';results=[]
def run(name,cmd,env=None):
 print('START',name,flush=True);start=time.monotonic()
 with (RAW/(name+'.log')).open('w') as f:p=subprocess.run(cmd,cwd=R,stdout=f,stderr=subprocess.STDOUT,env=env)
 item={'name':name,'command':cmd,'exit':p.returncode,'seconds':round(time.monotonic()-start,2),'log':str((RAW/(name+'.log')).relative_to(R))};results.append(item);(E/'gate-results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,p.returncode,item['seconds'],flush=True)
 assert p.returncode==0,name
 return (RAW/(name+'.log')).read_text(errors='surrogateescape')
# Step 1 was verified by preflight.py; do not overwrite that entry inventory.
run('step2-shell',['bash','-n','tools/probe_accounting.sh','tools/probe_board_ui/run.sh','tools/probe_firmware_ui/run.sh'])
# bash -n checks only its first script argument; run each independently.
for n in ['probe_board_ui','probe_firmware_ui']:run('step2-'+n,['bash','-n','tools/'+n+'/run.sh'])
for rel in ['tools/probe_board_ui/negctl.py','tools/probe_board_ui/accounting.py','tools/test_probe_board_ui.py','tools/test_probe_firmware_ui.py']:
 py_compile.compile(str(R/rel),cfile=str(RAW/(rel.replace('/','_')+'.pyc')),doraise=True)
run('step3-board-tests',['python3','tools/test_probe_board_ui.py','-v'])
# Trace is a separate file; stdout remains the stock default output.
cmd=['bash','-c','exec 9>"$1"; export BASH_XTRACEFD=9; export PS4=\'+${FUNCNAME[0]:-main}@${LINENO}|\'; bash -x tools/probe_board_ui/run.sh','qa',str(RAW/'step4.trace')]
run('step4-board',cmd)
no=run('step5-board-noneg',['bash','tools/probe_board_ui/run.sh','--no-neg']);assert no.splitlines()[-1]=='PROBE-ONLY — NOT A GATE (--no-neg: canvas mutation controls skipped)' and 'PASS' not in no.splitlines()
run('step6-reconcile',['python3',str(E/'reconcile.py')])
fw=run('step7-firmware-ui',['bash','tools/probe_firmware_ui/run.sh'])
run('step7-b456-tests',['python3','tools/test_probe_firmware_ui.py','-v'])
base=(R/'artifacts/2026-09-28-b459/base-fwui.log').read_text();assert fw==base,'firmware-UI output changed vs coder before-extraction log'
src=(R/'tools/probe_firmware_ui/run.sh').read_text();calls=re.findall(r'^ctl "((?:[^"\\]|\\.)*)" (yes|no) ',src,re.M)
# Label audit is written after source extraction is checked against its actual call syntax.
run('step8-discovery',['python3','-m','unittest','discover','-s','tools','-p','test_*.py'])
run('step9-inventory',['python3','tools/gen_command_inventory.py','--check'])
print('STEPS 1-9 COMPLETE; next step 10 independent reader audit and final preservation.',flush=True)
