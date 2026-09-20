from pathlib import Path
import subprocess,concurrent.futures,json,time,os
q=Path(Path('/tmp/mr-s10-r3-active').read_text().strip());r=q/'base';logs=q/'base-logs';logs.mkdir(exist_ok=True)
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
def run(n,c):
 print('START',n,flush=True);t=time.monotonic()
 with (logs/(n+'.log')).open('wb') as f:rc=subprocess.run(c,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 row=dict(name=n,command=c,exit=rc,seconds=time.monotonic()-t);(logs/(n+'.json')).write_text(json.dumps(row,indent=2));print('END',n,rc,flush=True);assert rc==0,n
 return row
def sim():
 b=q/'sim-base'
 for n,c in [('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(b),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('sim-build',['cmake','--build',str(b),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-base'),'--lus',str(b/'orchestrator/lus'),'--jobs','3','--require-anchors'])]:run(n,c)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 fs=[pool.submit(run,'pair',['python3','tools/measure_board.py','pair','--output','.pio-measure/s10-base','--jobs','1']),pool.submit(sim)]
 for f in concurrent.futures.as_completed(fs):f.result()
print('BASE COMPLETE',flush=True)
