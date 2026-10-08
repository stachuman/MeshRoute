from pathlib import Path
import hashlib,json,os,subprocess
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';rows=[]
for name,cmd,path,line in [('corpus-vacuity',['python3','-B','tools/test_compare_corpus_slice_s1b.py','TestVacuity.test_zz_blinding_the_checks_breaks_this_suite'],'tools/test_compare_corpus_slice_s1b.py',375),('worker-ledger',['python3','-B','tools/test_worker_formula_derived.py'],'tools/test_worker_formula_derived.py',144)]:
 p=subprocess.run(cmd,cwd=R,env=dict(os.environ,PYTHONWARNINGS='always::ResourceWarning',PYTHONTRACEMALLOC='1'),capture_output=True);d=p.stdout+p.stderr;(O/(name+'-warning.log')).write_bytes(d)
 assert p.returncode==0 and b'ResourceWarning: unclosed file' in d and ('%s", lineno %d'%(path.split('/')[-1],line)).encode() in d,(name,p.returncode,d[-2500:])
 before=subprocess.check_output(['git','show','10f3332:'+path],cwd=R);assert before==(R/path).read_bytes()
 rows.append({'case':name,'command':cmd,'exit':p.returncode,'allocation':path+':'+str(line),'same_at_original_base':True,'log_sha256':hashlib.sha256(d).hexdigest(),'warning':d.decode('utf8','backslashreplace')})
(O/'warning-repro.json').write_text(json.dumps({'finding':'B496','severity':'MINOR / test stream hygiene','outside_package':True,'measurements':rows},indent=2)+'\n');print('B496: both unclosed streams reproduced; exits0, same source at10f3332')
