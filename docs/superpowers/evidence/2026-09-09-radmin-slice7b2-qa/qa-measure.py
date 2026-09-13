import hashlib,json,os,shutil,subprocess,time
from pathlib import Path
q=Path(__file__).resolve().parent;root=q/'measure';env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator',PYTHONUNBUFFERED='1');records=[]
def run(name,cmd,expected=0):
 print('START',name,flush=True);t=time.monotonic();log=q/'logs'/(name+'.log')
 with log.open('w') as f:rc=subprocess.run(cmd,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 records.append(dict(name=name,cmd=cmd,rc=rc,seconds=time.monotonic()-t,log=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'measure-results.json').write_text(json.dumps(records,indent=2)+'\n')
 print('END',name,rc,round(records[-1]['seconds'],1),flush=True);assert rc==expected,(name,rc,str(log))
filters='--source-file=*test_firmware_remote_executor.cpp,*test_node_remote_session.cpp,*test_remote_session.cpp'
run('base-native-build',['pio','test','-e','native']);run('base-native',['./.pio/build/native/program']);run('base-native-remote',['./.pio/build/native/program',filters,'--reporters=xml'])
run('base-boards',['python3','tools/measure_board.py','pair','--output','.pio-measure/qa-base','--jobs','1'])
run('sim-base-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(q/'sim-base'),'-G','Ninja','-DMESHROUTE_DIR='+str(root),'-DCMAKE_BUILD_TYPE=Release'])
run('sim-base-build',['cmake','--build',str(q/'sim-base'),'--target','lus','--parallel','3','--verbose'])
run('corpus-base',['python3','tools/run_corpus.py','--out',str(q/'corpus-base'),'--lus',str(q/'sim-base/orchestrator/lus'),'--jobs','3','--require-anchors'])
run('corpus-validate-base',['python3','tools/run_corpus.py','--validate',str(q/'corpus-base')])
# Copy final bytes without preserving old mtimes. Every actual implementation/gate input joins this overlay.
for name in (q/'changed-from-base.txt').read_text().splitlines():
 if name.startswith(('lib/','src/','test/','tools/')) or name=='platformio.ini':
  p=q/'frozen'/name;d=root/name;shutil.copyfile(p,d);os.chmod(d,p.stat().st_mode & 0o777)
run('final-native-build',['pio','test','-e','native']);run('final-native',['./.pio/build/native/program']);run('final-native-remote',['./.pio/build/native/program',filters,'--reporters=xml'])
run('final-boards',['python3','tools/measure_board.py','pair','--output','.pio-measure/qa-final','--jobs','1'])
(q/'boards-complete').write_text('Both independently measured base/final board pairs complete; census may start.\n')
run('sim-final-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(q/'sim-final'),'-G','Ninja','-DMESHROUTE_DIR='+str(root),'-DCMAKE_BUILD_TYPE=Release'])
run('sim-final-build',['cmake','--build',str(q/'sim-final'),'--target','lus','--parallel','3','--verbose'])
run('corpus-final',['python3','tools/run_corpus.py','--out',str(q/'corpus-final'),'--lus',str(q/'sim-final/orchestrator/lus'),'--jobs','3','--require-anchors'])
run('corpus-validate-final',['python3','tools/run_corpus.py','--validate',str(q/'corpus-final')])
run('corpus-comparator',['python3','tools/run_corpus.py','--compare',str(q/'corpus-base'),str(q/'corpus-final')],expected=1)
a=json.loads((q/'corpus-base/manifest.json').read_text());b=json.loads((q/'corpus-final/manifest.json').read_text());assert a['scenario_count']==b['scenario_count']==36
rows=[]
for x,y in zip(a['scenarios'],b['scenarios']):
 assert x['name']==y['name'];n=x['name'];p=(q/'corpus-base/streams'/f'{n}.ndjson').read_bytes();z=(q/'corpus-final/streams'/f'{n}.ndjson').read_bytes()
 assert p==z and hashlib.sha256(p).hexdigest()==x['output_sha256']==y['output_sha256'],n
 assert x['anchor_match'] and y['anchor_match'] and x['assertion_failures']==y['assertion_failures']==0
 rows.append({k:y[k] for k in ['name','events','output_bytes','output_md5','output_sha256','anchor_match','assertion_failures']})
(q/'corpus-audit.json').write_text(json.dumps(dict(base_lus_sha256=a['lus_sha256'],final_lus_sha256=b['lus_sha256'],baseline_sha256=b['baseline_sha256'],actual_stream_byte_identity=len(rows),scenarios=rows),indent=2)+'\n')
print('MEASUREMENT COMPLETE; 36 actual streams byte-identical',flush=True)
