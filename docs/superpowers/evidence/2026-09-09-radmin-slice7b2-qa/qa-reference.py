from pathlib import Path
import subprocess,hashlib,json,time
q=Path(__file__).resolve().parent;root=q/'frozen';py='/home/staszek/mr-slice2-ref/bin/python';ref='docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py';records=[]
def run(name,args,expected):
 log=q/'logs'/(name+'.log'); t=time.monotonic()
 p=subprocess.run([py,ref,*args],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);log.write_text(p.stdout)
 records.append(dict(name=name,command=[py,ref,*args],cwd=str(root),rc=p.returncode,seconds=time.monotonic()-t,sha256=hashlib.sha256(log.read_bytes()).hexdigest(),log=str(log)))
 (q/'reference-results.json').write_text(json.dumps(records,indent=2)+'\n');print(name,p.returncode,p.stdout[-850:]);assert p.returncode==expected
run('reference',['--compare','test/test_remote_codec.cpp','--selftest'],0)
s=(root/'test/test_remote_codec.cpp').read_text();start=s.index('kAdmissionRefValid');hit=s.index('0x53',start);s=s[:hit]+s[hit:].replace('0x53','0x52',1);p=q/'one-byte-corrupt.cpp';p.write_text(s)
run('reference-corrupt',['--compare',str(p)],1)
