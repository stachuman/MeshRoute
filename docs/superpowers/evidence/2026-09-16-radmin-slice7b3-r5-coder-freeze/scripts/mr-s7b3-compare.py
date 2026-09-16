from pathlib import Path
import subprocess,json,hashlib,re
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';results={}
p=subprocess.run(['python3','tools/run_corpus.py','--compare',str(q/'corpus-base'),str(q/'corpus-final')],cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs/corpus-canonical-compare.log').write_bytes(p.stdout);results['canonical_exit']=p.returncode;print(p.stdout.decode()[-2500:])
manifests=[json.loads((q/name/'manifest.json').read_text()) for name in ['corpus-base','corpus-final']]
print('manifest top keys',list(manifests[0]));print('row example',next(iter(manifests[0]['scenarios'].items())) if isinstance(manifests[0]['scenarios'],dict) else manifests[0]['scenarios'][0])
# Compare actual event-stream files, independently of reported checksums, after both validator passes.
files={p.relative_to(q/'corpus-base') for p in (q/'corpus-base').rglob('*.ndjson')};print('streams',len(files));assert len(files)==36,len(files)
rows=[]
for rel in sorted(files):
 b=(q/'corpus-base'/rel).read_bytes();a=(q/'corpus-final'/rel).read_bytes();assert b==a,rel
 rows.append(dict(path=str(rel),bytes=len(a),sha256=hashlib.sha256(a).hexdigest(),md5=hashlib.md5(a).hexdigest()))
results['streams']=rows;results['identical']=len(rows);(q/'corpus-byte-comparison.json').write_text(json.dumps(results,indent=2)+'\n')
print('ACTUAL STREAMS BYTE IDENTICAL',len(rows));print('s18',[x for x in rows if 's18' in x['path']])
s=(r/'test/test_remote_codec.cpp').read_text();pat=r'(kRefBody_resp_terminal_auth_code09\s*\[\s*\d*\s*\]\s*=\s*\{\s*)0x([0-9a-fA-F]{2})';m=re.search(pat,s);assert m
corrupt=q/'reference-corrupt.cpp';corrupt.write_text(s[:m.start(2)]+f'{int(m.group(2),16)^1:02x}'+s[m.end(2):])
cmd=['/home/staszek/mr-slice2-ref/bin/python','docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py','--freeze-check','--compare',str(corrupt)]
p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs/reference-corrupt.log').write_bytes(p.stdout);assert p.returncode==1,p.returncode;assert b'MISMATCH kRefBody_resp_terminal_auth_code09 at byte 0' in p.stdout
print('SEPARATE CORRUPTION REFUSED exit',p.returncode)
(q/'reference-corrupt-result.json').write_text(json.dumps(dict(command=cmd,exit=p.returncode,source_sha256=hashlib.sha256((r/'test/test_remote_codec.cpp').read_bytes()).hexdigest(),corruption_sha256=hashlib.sha256(corrupt.read_bytes()).hexdigest()),indent=2)+'\n')
