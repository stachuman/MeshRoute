from pathlib import Path
import ast, importlib.util, json, subprocess, hashlib, sys, contextlib
q=Path(__file__).resolve().parent; r=q/'snapshot'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def emit(name, data): (q/name).write_text(json.dumps(data, indent=2)+'\n')
results=[]
def run(name, cmd, expected=0):
 p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 log=q/'logs'/(name+'.log');log.write_bytes(p.stdout)
 results.append(dict(name=name,command=cmd,exit=p.returncode,expected=expected,log=log.name,sha256=sha(log)))
 emit('codec-results.json',results);assert p.returncode==expected,p.stdout.decode();print(name,p.returncode,flush=True)
refpath=r/'docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py'
run('reference',[sys.executable,str(refpath),'--compare','test/test_remote_codec.cpp','--selftest'])
spec=importlib.util.spec_from_file_location('admission_reference',refpath);refmod=importlib.util.module_from_spec(spec);spec.loader.exec_module(refmod)
with (q/'logs/oracle.log').open('w') as log, contextlib.redirect_stdout(log):
 ref=refmod.original();old=ref.VECTORS | refmod.vectors(ref)
 new={}
 for domain,code,detail in [('auth',9,b''),('open',8,b''),('open',9,b''),('auth',8,b'\xd1'),('open',8,b'\xd1')]:
  d=ref.DOMAINS['resp_terminal_'+domain];payload=bytes([code])+detail
  name='kRefBody_resp_terminal_'+domain+f'_code{code:02x}'+('_detail' if detail else '')
  new[name]={'plaintext':payload.hex(),'header':d.header().hex(),'nonce':d.nonce().hex() if d.authed else None,'aad':d.aad().hex() if d.authed else None,'body':d.body(payload).hex()}
 emit('independent-boundary-vectors.json',{'method':'Original Slice-2 independent hashlib/PyNaCl reference; no production outputs consulted. Baseline input constants unchanged. Frozen author expectations; additions not yet in native tests.','existing_arrays':len(old),'existing_array_sha256':{n:hashlib.sha256(b).hexdigest() for n,b in old.items()},'proposed_additions':new})
 source=(r/'test/test_remote_codec.cpp').read_text();match=next(m for m in refmod.LITERAL.finditer(source) if m[1]=='kRefBaseKey');start=match.start(3);at=source.index('0x',start);bad=source[:at]+f'0x{int(source[at+2:at+4],16)^1:02x}'+source[at+4:];(q/'corrupted-literal.cpp').write_text(bad)
run('reference-corrupted',[sys.executable,str(refpath),'--compare',str(q/'corrupted-literal.cpp')],expected=1)
values={};tables={}
for n in ast.parse((r/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(n,ast.Assign):
  for t in n.targets:
   if isinstance(t,ast.Name):
    try: values[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):
     if t.id=='MUTS_BY_TARGET':tables={ast.literal_eval(k):v.id for k,v in zip(n.value.keys,n.value.values)}
floor=json.loads((r/'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-precheck/historical-mutation-floor.json').read_text());counts={};mismatches=[]
for t in floor['union']:
 counts[t]=len(values[tables[t]]);source=(r/values['TARGET_SRC'][t]).read_text()
 for label,pattern,replacement in values[tables[t]]:
  count=source.count(pattern)
  if count!=1:mismatches.append({'target':t,'label':label,'matches':count,'pattern':pattern})
emit('mutation-pattern-audit.json',{'configured_patterns':sum(counts.values()),'batteries':len(counts),'counts':counts,'mismatches':mismatches,'executed_native_mutations':False})
assert len(counts)==52 and sum(counts.values())==815
assert len(mismatches)==1 and mismatches[0]['target']=='radmin5rx'
run('terminal-consumers',['rg','-n','RemoteTerminal|kRemoteTerminalMax','lib/core','src','tools','--glob','*.h','--glob','*.cpp','--glob','*.py','--glob','!probe_ui_model_mutations.py'])
libs=sorted(str(p) for p in (r/'.pio/build/native').glob('lib*/*.a'))
run('domain-proof-compile',['g++','-std=c++20','-DMESHROUTE_NATIVE','-Ilib/core','-Ilib/monocypher',str(q/'result-domain-proof.cpp'),'-Wl,--start-group',*libs,'-Wl,--end-group','-o',str(q/'result-domain-proof')])
run('domain-proof',[str(q/'result-domain-proof')])
# Future allocation must FAIL against this baseline: proves the mode discriminates.
run('allocation-negative-control',[str(q/'result-domain-proof'),'--allocated'],expected=1)
print('CODEC AUTHOR AUDIT COMPLETE',flush=True)
