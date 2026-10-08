from pathlib import Path
import subprocess,json,time,os,tempfile
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;S=Path(tempfile.mkdtemp(prefix='w7-precheck-'));(O/'scratch.json').write_text(json.dumps({'path':str(S)})+'\n');runs=[]
def run(label,args):
 print('RUN',label,flush=True);t=time.monotonic()
 with (O/(label+'.log')).open('wb') as f:p=subprocess.run(args,cwd=R,stdout=f,stderr=subprocess.STDOUT,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'})
 runs.append(dict(label=label,args=args,exit=p.returncode,seconds=round(time.monotonic()-t,3)));(O/'runs.json').write_text(json.dumps(runs,indent=2)+'\n');print('DONE',label,p.returncode,flush=True)
 if p.returncode:raise SystemExit('Baseline failed: '+label)
run('native-build',['pio','test','-e','native']);run('native-binary',['./.pio/build/native/program'])
run('abi',['python3','-B','tools/probe_board_abi.py'])
run('firmware-ui',['tools/probe_firmware_ui/run.sh'])
run('board-ui',['tools/probe_board_ui/run.sh'])
run('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(S/'sim-build'),'-DCMAKE_BUILD_TYPE=Release','-DMESHROUTE_DIR='+str(R)])
run('sim-build',['cmake','--build',str(S/'sim-build'),'--target','lus','-j','4'])
run('corpus',['python3','-B','tools/run_corpus.py','--out',str(S/'corpus'),'--lus',str(S/'sim-build/orchestrator/lus'),'--require-anchors','--jobs','4'])
run('inventory',['python3','-B','tools/gen_command_inventory.py','--check'])
