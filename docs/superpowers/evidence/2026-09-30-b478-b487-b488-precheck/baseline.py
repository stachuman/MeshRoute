from pathlib import Path
import subprocess,time,json,hashlib
r=Path('/home/staszek/MeshRoute');raw=r/'artifacts/2026-09-30-b478-b487-b488-precheck';sc=Path('/tmp/mr-tool-precheck-as0y6n6_');results=[]
steps=[('native-build',['pio','test','-e','native']),('native',['./.pio/build/native/program']),('transcript-stock',['python3','-B','tools/probe_inbox_verbs/transcript.py','--out',str(sc/'transcript-stock'),'--emit',str(sc/'stock-transcript.txt')]),('discovery',['python3','-B','-m','unittest','discover','-s','tools','-p','test_*.py']),('inventory',['python3','-B','tools/gen_command_inventory.py','--check'])]
for name,cmd in steps:
 print('START',name,flush=True);t=time.time();log=raw/(name+'.log')
 with log.open('wb') as f:p=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT)
 result={'step':name,'command':cmd,'exit':p.returncode,'seconds':round(time.time()-t,3),'log':str(log),'sha256':hashlib.sha256(log.read_bytes()).hexdigest()};results.append(result);(raw/'runs.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,p.returncode,result['seconds'],flush=True)
 if p.returncode and name!='transcript-stock':print(log.read_text(errors='backslashreplace')[-3000:],flush=True);break
