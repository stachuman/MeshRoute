from pathlib import Path
import subprocess,os,json,time,sys
q=Path(Path('/tmp/mr-codex-s7b3-active').read_text().strip());r=q/'gate';mode=sys.argv[1];build=q/'sim-base'
if mode=='boards': cmds=[('boards',['python3','tools/measure_board.py','pair','--output','.pio-measure/s7b3-base','--jobs','1'])]
else:cmds=[('configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(build),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('compile',['cmake','--build',str(build),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-base'),'--lus',str(build/'orchestrator/lus'),'--jobs','3','--require-anchors']),('validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-base')])]
results=[]
for name,cmd in cmds:
 print('START',name,flush=True);now=time.monotonic();log=q/'logs'/('base-'+mode+'-'+name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator'),stdout=f,stderr=subprocess.STDOUT).returncode
 results.append({'name':name,'cmd':cmd,'cwd':str(r),'exit':rc,'seconds':time.monotonic()-now,'log':str(log)});(q/('base-'+mode+'-results.json')).write_text(json.dumps(results,indent=2)+'\n');print('END',name,rc,flush=True)
 if rc:raise SystemExit(rc)
