from pathlib import Path
import subprocess,json,hashlib,re
q=Path(__file__).resolve().parent;r=q/'snapshot';previous=Path('/tmp/mr-codex-s7b30-C40uYn/corpus-final');current=q/'corpus';results=[]
for name,cmd in [('previous-corpus-validate',['python3','tools/run_corpus.py','--validate',str(previous)]),('corpus-compare',['python3','tools/run_corpus.py','--compare',str(previous),str(current)])]:
 p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/(name+'.log')).write_bytes(p.stdout);results.append({'name':name,'command':cmd,'exit':p.returncode});assert p.returncode==0,p.stdout.decode()
paths=sorted((current/'streams').iterdir());assert len(paths)==len(list((previous/'streams').iterdir()))==36
streams=[]
for p in paths:
 b=p.read_bytes();assert b==(previous/'streams'/p.name).read_bytes(),p
 streams.append({'name':p.name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
manifest=json.loads((current/'manifest.json').read_text());log=(q/'logs/sim-build.log').read_text();compiles=[line for line in log.splitlines() if re.match(r'\[\d+/\d+\]',line) and ' -c ' in line];assert len(compiles)==64
result={'commands':results,'previous':str(previous),'current':str(current),'streams_compared_byte_for_byte':len(streams),'fresh_compiler_actions':len(compiles),'lus_sha256':manifest['lus_sha256'],'baseline_sha256':manifest['baseline_sha256'],'streams':streams};(q/'corpus-identity.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: 36 byte-identical streams; both manifests validated; 64 fresh compiler actions')
