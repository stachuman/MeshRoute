"""Pre-check only: real AST-extracted run_suite with real synthetic child output; no harness import or mutation."""
from pathlib import Path
from typing import NamedTuple
from types import SimpleNamespace
import ast,os,re,sys,subprocess,json,hashlib
ROOT=Path('/home/staszek/MeshRoute'); OUT=Path('/tmp/mr-tool-precheck-as0y6n6_/decode');OUT.mkdir(exist_ok=True)
source=(ROOT/'tools/probe_ui_model_mutations.py').read_text();tree=ast.parse(source)
defs=[x for x in tree.body if isinstance(x,(ast.ClassDef,ast.FunctionDef)) and x.name in ['SuiteFailure','run_suite','unusable_verdict']]
summary=b'test cases: 1 | 0 passed | 1 failed\nassertions: 1 | 0 passed | 1 failed\n'
green=b'test cases: 1 | 1 passed | 0 failed\nassertions: 1 | 1 passed | 0 failed\n'
cases=[('raw_bb_stdout',b'\xbb\n'+summary,b'',1,'run'),('raw_c5_stderr',summary,b'\xc5\n',1,'run'),('malformed_both',b'\x80\xff\n'+summary,b'\xc5',1,'run'),('valid_utf8', 'ł »\n'.encode()+summary,b'diagnostic\n',1,'run'),('split_valid_utf8',b'\xc5\x82\n'+summary,b'',1,'run'),('truncated_utf8',summary+b'\xc5',b'',1,'run'),('no_verdict',b'out\n',b'err\n',0,'run'),('signal_no_verdict',b'out\n',b'err\n',-11,'run'),('signal_with_failure_summary',summary,b'crash after summary\n',-11,'run'),('signal_with_success_summary',green,b'crash after summary\n',-11,'run'),('exit2_with_summary',summary,b'wrong exit\n',2,'run'),('build_raw_stderr',b'build\n',b'error: \xbb\n',1,'build'),('build_ascii',b'build\n',b'error: stopped\n',1,'build')]
results=[]
for name,stdout,stderr,rc,phase in cases:
 child=OUT/(name+'.py')
 stdout_writes=[stdout[:1],stdout[1:]] if name=='split_valid_utf8' else [stdout]
 code='import os,signal,resource\nresource.setrlimit(resource.RLIMIT_CORE,(0,0))\n'
 code+='\n'.join('os.write(1,'+repr(v)+')' for v in stdout_writes)+'\nos.write(2,'+repr(stderr)+')\n'
 code+=('os.kill(os.getpid(),signal.SIGSEGV)\n' if rc==-11 else 'raise SystemExit('+str(rc)+')\n')
 child.write_text(code)
 ground=subprocess.run([sys.executable,str(child)],capture_output=True)
 assert (ground.stdout,ground.stderr,ground.returncode)==(stdout,stderr,rc)
 (OUT/(name+'.stdout.bin')).write_bytes(ground.stdout);(OUT/(name+'.stderr.bin')).write_bytes(ground.stderr)
 calls=[]
 def invoke(cmd,**kwargs):
  calls.append(cmd)
  if cmd[0]=='pio' and phase!='build':return subprocess.CompletedProcess(cmd,0,'','')
  kwargs.pop('cwd',None)
  return subprocess.run([sys.executable,str(child)],**kwargs)
 env=dict(NamedTuple=NamedTuple,os=os,re=re,ROOT=str(ROOT),subprocess=SimpleNamespace(run=invoke))
 exec(compile(ast.Module(body=defs,type_ignores=[]),'<stock run_suite>','exec'),env)
 row={'name':name,'phase':phase,'child_exit':rc,'stdout_hex':stdout.hex(),'stderr_hex':stderr.hex()}
 try:
  result,out=env['run_suite'](); row.update(result=result,capture=out._asdict() if hasattr(out,'_asdict') else out,exception=None)
 except Exception as e:row.update(exception=type(e).__name__,detail=str(e))
 row['calls']=len(calls);results.append(row)
# Measure the existing 64-KiB view: a valid character straddling the cut loses one encoded byte.
env={}; selected=[x for x in defs if x.name=='unusable_verdict'];exec(compile(ast.Module(body=selected,type_ignores=[]),'<stock classifier>','exec'),env)
full='€'+'x'*65535
_,_,capture=env['unusable_verdict']('run',1,full)
retention={'input_bytes':len(full.encode()),'nominal_tail_bytes':65536,'actual_retained_bytes':len(capture.encode()),'tail_prefix_hex':full.encode()[-65536:-65528].hex(),'retained_prefix_hex':capture.encode()[:8].hex(),'note':'current errors=ignore drops a cut UTF-8 continuation byte in addition to the intentional 64 KiB prefix truncation'}
report={'scope':'labelled synthetic child outputs through the unchanged real function; not a full battery run','harness_sha256':hashlib.sha256(source.encode()).hexdigest(),'results':results,'retention':retention}
(OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'cases':[{'name':x['name'],'exit':x['child_exit'],'exception':x['exception'],'result':x.get('result')} for x in results],'retention':retention},indent=2))
