from pathlib import Path
import subprocess,json,hashlib,time
q=Path(__file__).resolve().parent;root=q/'snapshot';fixture='docs/superpowers/evidence/2026-09-09-radmin-slice7b2-qa/b388.cpp'
commands=[('monocypher',['gcc','-Ilib/monocypher/src','-c','lib/monocypher/src/monocypher.c','-o',str(q/'monocypher.o')]),('compile',['g++','-std=c++20','-DMESHROUTE_NATIVE','-Ilib/core','-Ilib/monocypher/src',fixture,'lib/core/remote_session.cpp','lib/core/remote_codec.cpp','lib/core/identity.cpp','lib/core/dm_crypto.cpp',str(q/'monocypher.o'),'-o',str(q/'b388')]),('run',[str(q/'b388')])]
results=[]
for name,cmd in commands:
 start=time.monotonic();p=subprocess.run(cmd,cwd=root,capture_output=True,text=True);log=q/(name+'.log');log.write_text(p.stdout+p.stderr);results.append(dict(name=name,command=cmd,cwd=str(root),exit=p.returncode,seconds=time.monotonic()-start,log=log.name,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()));(q/'reproduction.json').write_text(json.dumps(results,indent=2)+'\n');print(name,p.returncode,p.stdout+p.stderr);assert p.returncode==0
assert '15 checks PASS' in (q/'run.log').read_text()
