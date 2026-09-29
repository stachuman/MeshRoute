from pathlib import Path
import subprocess,json,tempfile,time,os,shutil
R=Path('/home/staszek/MeshRoute'); E=Path(__file__).resolve().parent; A=R/'artifacts'/E.name
S=Path(tempfile.mkdtemp(prefix='meshroute-w6-precheck-')); (A/'scratch-path.txt').write_text(str(S)+'\n')
runs=[]
def run(name,args):
 t=time.monotonic()
 with (A/(name+'.log')).open('wb') as f: p=subprocess.run(args,cwd=R,stdout=f,stderr=subprocess.STDOUT,env={**os.environ,'LC_ALL':'en_US.UTF-8','PYTHONDONTWRITEBYTECODE':'1'})
 runs.append(dict(name=name,args=args,exit=p.returncode,seconds=round(time.monotonic()-t,3)))
 (E/'runs.json').write_text(json.dumps(runs,indent=2)+'\n')
 print(name,p.returncode,runs[-1]['seconds'],flush=True)
 if p.returncode: raise SystemExit('Baseline failed: '+name)
run('native-build',['pio','test','-e','native'])
run('native-binary',['./.pio/build/native/program'])
run('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(S/'sim-build'),'-DCMAKE_BUILD_TYPE=Release','-DMESHROUTE_DIR='+str(R)])
run('sim-build',['cmake','--build',str(S/'sim-build'),'--target','lus','-j','4'])
run('corpus',['python3','-B','tools/run_corpus.py','--out',str(S/'corpus'),'--lus',str(S/'sim-build/orchestrator/lus'),'--require-anchors','--jobs','4'])
run('abi',['python3','-B','tools/probe_board_abi.py'])
run('firmware-ui',['tools/probe_firmware_ui/run.sh'])
run('board-ui',['tools/probe_board_ui/run.sh'])
run('console-sink',['tools/probe_console_sink/run.sh'])
run('inbox-verbs',['tools/probe_inbox_verbs/run.sh'])
run('inbox-verbs-client',['tools/probe_inbox_verbs/run.sh','--client'])
run('inventory',['python3','-B','tools/gen_command_inventory.py','--check'])
print('BASELINES COMPLETE',flush=True)
