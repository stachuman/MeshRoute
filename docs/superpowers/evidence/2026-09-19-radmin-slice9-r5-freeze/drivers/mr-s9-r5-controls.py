from pathlib import Path
import sys,ast,unittest,io,json,hashlib
q=Path('/tmp/mr-s9-active').read_text().strip();q=Path(q);root=q/'tools';sys.path.insert(0,str(root/'tools'))
import measure_board as mb
import test_measure_board as tests
source=(root/'tools/measure_board.py').read_text();node=next(x for x in ast.parse(source).body if isinstance(x,ast.FunctionDef) and x.name=='source_snapshot');body='\n'.join(source.splitlines()[node.lineno-1:node.end_lineno])+'\n'
original=mb.source_snapshot
controls=[('deletion arm removed','elif not path.exists() and encoded in unstaged_deletions:','elif False and not path.exists() and encoded in unstaged_deletions:'),('all absent paths accepted','elif not path.exists() and encoded in unstaged_deletions:','elif not path.exists():'),('manifest deletion entry dropped','deleted_files.append(os.fsdecode(encoded))','pass')]
results=[]
for label,old,new in controls:
 assert body.count(old)==1
 exec(compile(body.replace(old,new),str(root/'tools/measure_board.py'), 'exec'),vars(mb))
 stream=io.StringIO();test=tests.MeasureBoardTests('test_source_identity_structure');res=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.TestSuite([test]));mb.source_snapshot=original
 assert not res.wasSuccessful(),label
 row={'control':label,'source_match_count':1,'compiled':True,'failed_tests':len(res.failures),'error_tests':len(res.errors),'output':stream.getvalue()};results.append(row);print('RED',label,len(res.failures),len(res.errors))
assert (root/'tools/measure_board.py').read_text()==source
(q/'B425-controls.json').write_text(json.dumps(results,indent=2)+'\n')
