from pathlib import Path
import subprocess,importlib.util,json,tempfile,sys,unittest
R=Path('/home/staszek/MeshRoute'); O=R/'artifacts/2026-10-01-b478-b487-b488-qa'
spec=importlib.util.spec_from_file_location('qa_mut_tests',R/'tools/test_mutation_unusable_reason.py');m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m)
base=subprocess.check_output(['git','show','10f3332:tools/probe_ui_model_mutations.py'],cwd=R).decode(); final=m.harness_source(); rows=[]
with tempfile.TemporaryDirectory(prefix='mr-qa-before-') as td:
 for c in m.PRECHECK+m.AGREEMENT:
  if not c.defect: continue
  row={'case':c.name,'defect':c.defect}
  try:m.check_case(base,td,c,required=False);row['base']='unexpected pass'
  except Exception as e:row['base']=type(e).__name__;row['message']=str(e)
  want='UnicodeDecodeError' if c.defect=='B478' else 'AssertionError';assert row['base']==want
  if c.defect=='B490':assert 'CREDITED verdict' in row['message']
  m.check_case(final,td,c);row['final']='PASS';rows.append(row)
(O/'fails-before.json').write_text(json.dumps(rows,indent=2)+'\n');print('fails-before:',len(rows),'own-reason failures, final all PASS')
# Independently replay the three fault paths with the real test driver and expose their measured exits.
c=unittest.TestCase();runs=[]
try:
 for name,src,kwargs,check in [('transfer',m.edited(final,m.TRANSFER_FAULT),{},m.check_corrupted_transfer),('write',final,{'obstruct':True},None),('baseline-write',final,{'obstruct':True,'clean':m.CLEAN_DIES},None)]:
  battery=m.run_battery(c,src,{} if name=='baseline-write' else m.FAULT,2,**kwargs)
  if check:check(src,battery)
  else:m.check_failed_write(src,battery,2 if name=='baseline-write' else 9,['baseline-w0','baseline-w1'] if name=='baseline-write' else ['F01'])
  (O/(name+'-fault.log')).write_text(battery.console+battery.stderr)
  runs.append({'case':name,'exit':battery.rc,'traceback':'Traceback' in battery.console+battery.stderr,'unchanged':battery.unchanged})
finally:c.doCleanups()
(O/'faults.json').write_text(json.dumps(runs,indent=2)+'\n');print(runs)
