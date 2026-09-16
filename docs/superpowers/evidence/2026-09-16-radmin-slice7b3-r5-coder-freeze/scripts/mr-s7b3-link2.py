from pathlib import Path
import subprocess,json,time,hashlib
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';results=[]
for name,cmd in [('native-8-build',['pio','test','-e','native']),('native-8',['./.pio/build/native/program']),('boards-final-2',['python3','tools/measure_board.py','pair','--output','.pio-measure/s7b3-final-2','--jobs','1'])]:
 print('START',name,flush=True);t=time.monotonic();log=q/'logs'/(name+'.log')
 with log.open('wb') as out:rc=subprocess.run(cmd,cwd=r,stdout=out,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,exit=rc,seconds=time.monotonic()-t,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'link2-results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,rc,flush=True)
 if rc:raise SystemExit(rc)
