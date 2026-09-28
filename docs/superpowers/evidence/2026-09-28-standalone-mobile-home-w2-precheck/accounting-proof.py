"""Labelled synthetic omissions; copies stock runner, never edits the shared probe."""
import pathlib,json,shutil,re,subprocess,hashlib
ROOT=pathlib.Path('/home/staszek/MeshRoute')
E=ROOT/'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck'
RAW=ROOT/'artifacts/2026-09-28-standalone-mobile-home-w2-precheck'
S=pathlib.Path((E/'scratch-path.txt').read_text().strip())/'accounting-projection'
S.mkdir(exist_ok=True)
for f,h in json.loads((E/'inputs.json').read_text())['meshroute']['files'].items():
 p=S/f;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/f,p)
p=S/'tools/probe_board_ui/run.sh'; original=p.read_text()
def omit(text,label):
 pattern=r'^wchk(?:_in)? [^\n]*"'+re.escape(label)+r' [^\n]*(?:\n[^\n]*)*?'
 lines=text.splitlines(keepends=True); ids=[i for i,l in enumerate(lines) if re.match(r'^wchk(?:_in)? ',l) and ('"'+label+' ') in l]; assert len(ids)==1,(label,ids)
 a=ids[0];b=a
 while lines[b].rstrip().endswith('\\'): b+=1
 return ''.join(lines[:a]+['# SYNTHETIC OMISSION '+label+' (pre-check proof only)\n']+lines[b+1:])
base=original
for label in ['W49','W51','W54']:base=omit(base,label)
results=[]
for label,script in [('omit-three-known-failing-checks',base),('also-omit-healthy-W1',omit(base,'W1'))]:
 p.write_text(script)
 with (RAW/(label+'.log')).open('w') as f:r=subprocess.run(['bash',str(p)],cwd=S,stdout=f,stderr=subprocess.STDOUT)
 log=(RAW/(label+'.log')).read_text()
 rows=[x for x in log.splitlines() if re.search(r'^(structural:|wiring:|traits:|missing traits:|real source verified|.*PROBE.*passed|.*checks.*passed)',x)]
 results.append({'synthetic_fault':label,'exit_code':r.returncode,'runner_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'summary':rows})
 (E/'accounting-proof.json').write_text(json.dumps({'stock_sha256':hashlib.sha256(original.encode()).hexdigest(),'scope':'runner omissions only; no product or predicates changed; not a product gate or repair','results':results},indent=2)+'\n')
 print(label,r.returncode,flush=True)
p.write_text(original)
