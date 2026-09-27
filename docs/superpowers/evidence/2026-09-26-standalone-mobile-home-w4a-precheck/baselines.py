from pathlib import Path
import json,subprocess,time
root=Path('/home/staszek/MeshRoute');raw=root/'artifacts/2026-09-26-standalone-mobile-home-w4a-precheck';s=Path(json.loads((raw/'scratch.json').read_text())['path']);runs=[]
def run(label,args):
 print('RUN',label,flush=True);t=time.monotonic()
 with (raw/(label+'.log')).open('w') as f:p=subprocess.run(args,cwd=root,stdout=f,stderr=subprocess.STDOUT)
 runs.append({'label':label,'argv':args,'exit':p.returncode,'seconds':round(time.monotonic()-t,3)});(raw/'runs.json').write_text(json.dumps(runs,indent=2)+'\n');print('DONE',label,p.returncode,flush=True)
 if p.returncode:raise SystemExit(1)
run('native-build',['pio','test','-e','native'])
run('native-binary',['./.pio/build/native/program'])
run('abi',['python3','-B','tools/probe_board_abi.py'])
run('firmware-ui',['tools/probe_firmware_ui/run.sh'])
run('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(s/'sim-build'),'-DCMAKE_BUILD_TYPE=Release','-DMESHROUTE_DIR='+str(root)])
run('sim-build',['cmake','--build',str(s/'sim-build'),'--target','lus','-j','4'])
run('corpus',['python3','-B','tools/run_corpus.py','--out',str(s/'corpus'),'--lus',str(s/'sim-build/orchestrator/lus'),'--jobs','4','--require-anchors'])
