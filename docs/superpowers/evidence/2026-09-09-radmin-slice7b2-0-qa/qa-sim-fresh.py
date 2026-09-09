import json,os,subprocess,time,hashlib
from pathlib import Path
q=Path(__file__).resolve().parent; root=q/'measure'; records=[]
def run(name,cmd,expected=0):
 t=time.monotonic(); log=q/'logs'/(name+'.log'); print('START',name,flush=True)
 with log.open('w') as f: rc=subprocess.run(cmd,cwd=root,env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator'),stdout=f,stderr=subprocess.STDOUT).returncode
 records.append(dict(name=name,cmd=cmd,rc=rc,seconds=time.monotonic()-t,log=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest())); (q/'sim-fresh-results.json').write_text(json.dumps(records,indent=2)+'\n'); print('END',name,rc,flush=True); assert rc==expected
run('sim-fresh-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(q/'sim-final-fresh'),'-G','Ninja','-DMESHROUTE_DIR='+str(root),'-DCMAKE_BUILD_TYPE=Release'])
run('sim-fresh-build',['cmake','--build',str(q/'sim-final-fresh'),'--parallel','3','--verbose'])
run('corpus-final-fresh',['python3','tools/run_corpus.py','--out',str(q/'corpus-final-fresh'),'--lus',str(q/'sim-final-fresh/orchestrator/lus'),'--jobs','3','--require-anchors'])
run('corpus-validate-final-fresh',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final-fresh')])
run('corpus-compare-fresh',['python3','tools/run_corpus.py','--compare',str(q/'corpus-base'),str(q/'corpus-final-fresh')],2)
