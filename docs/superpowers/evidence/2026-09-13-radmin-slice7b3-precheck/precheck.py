from pathlib import Path
import subprocess,json,hashlib,time
q=Path(__file__).resolve().parent;r=q/'snapshot';sim=Path('/home/staszek/lora-universal-simulator')
commands=[('native-build',['pio','test','-e','native']),('native',['./.pio/build/native/program']),('sim-configure',['cmake','-S',str(sim),'-B',str(q/'sim-build'),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('sim-build',['cmake','--build',str(q/'sim-build'),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus'),'--lus',str(q/'sim-build/orchestrator/lus'),'--jobs','3','--require-anchors']),('corpus-validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus')])]
results=[]
for name,cmd in commands:
 print('START',name,flush=True);start=time.monotonic();log=q/'logs'/(name+'.log')
 with log.open('w') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,cmd=cmd,cwd=str(r),rc=rc,seconds=time.monotonic()-start,log=log.name,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,rc,flush=True);assert rc==0
print('AUTHOR BASELINE COMPLETE',flush=True)
