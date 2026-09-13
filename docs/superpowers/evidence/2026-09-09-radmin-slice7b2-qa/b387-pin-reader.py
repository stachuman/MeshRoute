#!/usr/bin/env python3
"""QA-only B387 reproduction using the actual strict reader; no repository edits."""
import argparse,ast,hashlib,json,os,re,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);a=p.parse_args()
reader=a.root/'tools/test_probe_console_sink.py';runner=a.root/'tools/probe_console_sink/run.sh'
s=reader.read_text();tree=ast.parse(s);fn=next(x for x in tree.body if isinstance(x,ast.FunctionDef) and x.name=='_run_sh_pins')
code=compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),str(reader),'exec')
original=runner.read_text();results={}
with tempfile.TemporaryDirectory(prefix='mr-qa-b387-') as td:
 path=Path(td)/'run.sh';ns={'os':os,'re':re,'PROBE_DIR':td};exec(code,ns)
 path.write_text(original);baseline=ns['_run_sh_pins']();assert baseline['PIN_STRUCTURAL']==83 and baseline['PIN_CONTROLS']==149
 print('baseline: actual strict reader accepts all',len(baseline),'pins; structural=83 controls=149')
 for pin in ['PIN_STRUCTURAL','PIN_CONTROLS']:
  exact=f'{pin}={baseline[pin]}';assert original.count(exact)==1
  path.write_text(original.replace(exact,exact+' # labelled reproduction of coder inline-comment defect'))
  try:ns['_run_sh_pins']()
  except AssertionError as e:
   assert pin in str(e);results[pin]=str(e);print('CONTROL RED',pin,str(e))
  else:raise AssertionError('strict reader accepted inline comment')
assert runner.read_text()==original
print('PASS: final runner readable; 2/2 inline-comment controls reject; source unchanged')
print(json.dumps({'reader_sha256':hashlib.sha256(reader.read_bytes()).hexdigest(),'runner_sha256':hashlib.sha256(runner.read_bytes()).hexdigest(),'pins':baseline,'controls':results},indent=2))
