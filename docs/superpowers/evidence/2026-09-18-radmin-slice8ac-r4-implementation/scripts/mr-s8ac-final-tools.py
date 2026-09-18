from pathlib import Path
import subprocess,json,time,hashlib,os
q=Path('/tmp/mr-s8ac-r4-b1edsao2');name='tools-repin';log=q/'final-logs'/f'{name}.log';cmd=['python3','-m','unittest','discover','-s','tools','-p','test_*.py'];start=time.monotonic()
with log.open('wb') as f:rc=subprocess.run(cmd,cwd=q/'final-gate',env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator'),stdout=f,stderr=subprocess.STDOUT).returncode
result=dict(name=name,command=cmd,cwd=str(q/'final-gate'),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest());(q/'final-logs'/f'{name}-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(rc)
