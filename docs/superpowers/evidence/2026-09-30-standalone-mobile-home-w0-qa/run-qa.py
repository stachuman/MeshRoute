from pathlib import Path
import subprocess,time,json,sys,hashlib
root=Path('/home/staszek/MeshRoute'); raw=root/'artifacts/2026-09-30-standalone-mobile-home-w0-qa'; scratch=Path('/tmp/mr-w0-qa-akh045fd'); pre='docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck'
steps=[
 ('lus-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(scratch/'lus'),'-DCMAKE_BUILD_TYPE=Release','-DMESHROUTE_DIR='+str(root)]),
 ('lus-build',['cmake','--build',str(scratch/'lus'),'--target','lus','-j','16']),
 ('corpus',['python3','-B','tools/run_corpus.py','--out',str(scratch/'corpus'),'--lus',str(scratch/'lus/orchestrator/lus'),'--require-anchors']),
 ('corpus-validate',['python3','-B','tools/run_corpus.py','--validate',str(scratch/'corpus')]),
 ('abi',['python3','-B','tools/probe_board_abi.py']),
 ('abi-idblob',['python3','-B','tools/probe_board_abi.py','--extra-pins',pre+'/identity-abi.json']),
 ('inboxverbs',['bash','tools/probe_inbox_verbs/run.sh']),
 ('consolesink',['bash','tools/probe_console_sink/run.sh']),
 ('boardui',['bash','tools/probe_board_ui/run.sh']),
 ('deferred',['python3','-B','tools/probe_deferred_actions/run.py','--out',str(scratch/'deferred')]),
 ('deviceradio',['bash','tools/probe_device_radio/run.sh']),
 ('provtx',['bash','tools/probe_prov_tx/run.sh']),
 ('ownership',['python3','-B','tools/probe_features/ownership.py']),
 ('ownership-controls',['python3','-B','tools/probe_features/ownership.py','--controls']),
 ('inventory',['python3','-B','tools/gen_command_inventory.py','--check']),
 ('authority',['python3','-B','tools/check_command_authority.py']),
 ('authority-selftest',['python3','-B','tools/check_command_authority.py','--selftest']),
 ('literals',['python3','-B','tools/check_data_type_literals.py']),
 ('mutations-devicenv',['python3','-B','tools/probe_ui_model_mutations.py','--target=devicenv']),
 ('boards',['python3','-B','tools/measure_board.py','pair','--jobs=1','--output','.pio-measure/w0-qa/2026-09-30'])]
results=[]
for name,cmd in steps:
    print('START',name,flush=True); start=time.time(); log=raw/(name+'.log')
    with log.open('wb') as f: p=subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT)
    result={'step':name,'command':cmd,'exit':p.returncode,'seconds':round(time.time()-start,3),'log':str(log),'sha256':hashlib.sha256(log.read_bytes()).hexdigest()}; results.append(result)
    (raw/'runs.json').write_text(json.dumps(results,indent=2)+'\n'); print('END',name,'exit',p.returncode,'seconds',result['seconds'],flush=True)
    if p.returncode: print(log.read_text(errors='backslashreplace')[-5000:],flush=True);sys.exit(p.returncode)
print('QA EXECUTION COMPLETE',flush=True)
