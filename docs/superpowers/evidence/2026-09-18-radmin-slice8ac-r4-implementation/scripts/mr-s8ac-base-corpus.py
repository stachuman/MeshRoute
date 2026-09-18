from pathlib import Path
import subprocess,hashlib,json,time
q=Path('/tmp/mr-s8ac-r4-b1edsao2');r=q/'snapshot';results=[]
def run(name,cmd,expected=0):
 log=q/'logs'/('base-'+name+'.log');start=time.monotonic()
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(name=name,command=cmd,exit=rc,seconds=time.monotonic()-start,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'base-corpus-results.json').write_text(json.dumps(results,indent=2)+'\n');assert rc==expected,(name,rc)
run('sim-configure',['cmake','-S','/home/staszek/lora-universal-simulator','-B',str(q/'sim-base'),'-G','Ninja','-DMESHROUTE_DIR='+str(r),'-DCMAKE_BUILD_TYPE=Release'])
run('sim-build',['cmake','--build',str(q/'sim-base'),'--target','lus','--parallel','3','--verbose'])
run('sim-corpus',['python3','tools/run_corpus.py','--out',str(q/'corpus-base'),'--lus',str(q/'sim-base/orchestrator/lus'),'--jobs','3','--require-anchors'])
run('sim-validate',['python3','tools/run_corpus.py','--validate',str(q/'corpus-base')])
run('canonical-compare',['python3','tools/run_corpus.py','--compare',str(q/'corpus-base'),str(q/'corpus-final2')],1)
a=json.loads((q/'corpus-base/manifest.json').read_text());b=json.loads((q/'corpus-final2/manifest.json').read_text());assert len(a['scenarios'])==len(b['scenarios'])==36
rows=[]
for x,y in zip(a['scenarios'],b['scenarios']):
 assert x['name']==y['name'] and x['anchor_match'] and y['anchor_match'];name=x['name'];pa=q/'corpus-base/streams'/(name+'.ndjson');pb=q/'corpus-final2/streams'/(name+'.ndjson')
 assert pa.exists() and pb.exists(),(pa,pb)
 result=subprocess.run(['cmp','--silent',str(pa),str(pb)]);assert result.returncode==0,name
 rows.append(dict(name=name,bytes=pa.stat().st_size,base_sha256=x['output_sha256'],final_sha256=y['output_sha256'],cmp_exit=0));assert x['output_sha256']==y['output_sha256']
(q/'corpus-byte-comparison.json').write_text(json.dumps(dict(validated_base_and_final=True,byte_identical=36,canonical_comparator='refuses different lus identities as expected; manifests preserved unchanged',rows=rows),indent=2)+'\n');print('BASE/FINAL 36/36 ACTUAL STREAMS BYTE-IDENTICAL')
