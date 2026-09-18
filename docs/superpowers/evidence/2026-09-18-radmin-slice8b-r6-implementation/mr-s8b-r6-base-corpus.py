from pathlib import Path
import subprocess,json,hashlib,os,time
q=Path('/tmp/mr-s8b-r6-nwy4hz9d');r=Path('/tmp/mr-s8b-r4-_d7gpj1e/snapshot');shared=Path('/home/staszek/MeshRoute');sim='/home/staszek/lora-universal-simulator';logs=q/'base-corpus-logs';logs.mkdir();b=q/'sim-base-fresh';results=[]
# The archived admission snapshot is production-identical to the declared c07b77f base.
paths=subprocess.check_output(['git','ls-tree','-r','--name-only','c07b77f','lib/','src/','test/','platformio.ini','simulation/'],cwd=shared).decode().splitlines()
for p in paths:
 assert (r/p).read_bytes()==subprocess.check_output(['git','show','c07b77f:'+p],cwd=shared),p
for name,cmd in [('configure',['cmake','-S',sim,'-B',str(b),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release']),('build',['cmake','--build',str(b),'--target','lus','--parallel','3','--verbose']),('corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-base-fresh'),'--lus',str(b/'orchestrator/lus'),'--jobs','3','--require-anchors']),('validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-base-fresh')])]:
 print('START BASE',name,flush=True);start=time.monotonic();log=logs/(name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,cwd=str(r),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'base-corpus-results.json').write_text(json.dumps(results,indent=2)+'\n');assert rc==0;print('END BASE',name,flush=True)
