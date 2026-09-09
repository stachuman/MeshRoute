import hashlib,json,os,shutil,subprocess,time
from pathlib import Path
q=Path(__file__).resolve().parent
root=q/'measure'
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator',PYTHONUNBUFFERED='1')
records=[]
def run(name,cmd,cwd=root,expected=0):
 print('START',name,flush=True); t=time.monotonic(); log=q/'logs'/(name+'.log')
 with log.open('w') as f: rc=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 item=dict(name=name,cmd=cmd,rc=rc,seconds=time.monotonic()-t,log=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest()); records.append(item)
 (q/'measure-results.json').write_text(json.dumps(records,indent=2)+'\n')
 print('END',name,rc,round(item['seconds'],1),flush=True); assert rc==expected,(name,rc,str(log))
run('base-native-build',['pio','test','-e','native'])
run('base-native',['./.pio/build/native/program'])
run('base-native-codec',['./.pio/build/native/program','--source-file=*test_remote_codec.cpp','--reporters=xml'])
run('base-boards',['python3','tools/measure_board.py','pair','--output','.pio-measure/qa-base','--jobs','1'])
run('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(q/'sim'),'-G','Ninja','-DMESHROUTE_DIR='+str(root),'-DCMAKE_BUILD_TYPE=Release'])
run('sim-base-build',['cmake','--build',str(q/'sim'),'--parallel','3','--verbose'])
shutil.copy2(q/'sim/orchestrator/lus',q/'lus-base')
run('corpus-base',['python3','tools/run_corpus.py','--out',str(q/'corpus-base'),'--lus',str(q/'sim/orchestrator/lus'),'--jobs','3','--require-anchors'])
# Final overlay is exactly the four reviewed source/test/instrument edits, at identical build paths.
for p in ['lib/core/remote_codec.h','lib/core/remote_codec.cpp','test/test_remote_codec.cpp','tools/probe_ui_model_mutations.py']:
 shutil.copy2(q/'frozen'/p,root/p)
run('final-native-build',['pio','test','-e','native'])
run('final-native',['./.pio/build/native/program'])
run('final-native-codec',['./.pio/build/native/program','--source-file=*test_remote_codec.cpp','--reporters=xml'])
run('final-boards',['python3','tools/measure_board.py','pair','--output','.pio-measure/qa-final','--jobs','1'])
run('sim-final-build',['cmake','--build',str(q/'sim'),'--parallel','3','--verbose'])
shutil.copy2(q/'sim/orchestrator/lus',q/'lus-final')
run('corpus-final',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(q/'sim/orchestrator/lus'),'--jobs','3','--require-anchors'])
for which in ['base','final']: run('corpus-validate-'+which,['python3','tools/run_corpus.py','--validate',str(q/('corpus-'+which))])
# This tool is a same-binary determinism comparator; expect its explicit binary mismatch refusal.
run('corpus-comparator',['python3','tools/run_corpus.py','--compare',str(q/'corpus-base'),str(q/'corpus-final')],expected=2)
print('MEASUREMENT COMPLETE',flush=True)
