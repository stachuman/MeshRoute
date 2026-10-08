from pathlib import Path
import sys,unittest,tempfile,json,subprocess
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';sys.path.insert(0,str(R/'tools'));import test_mutation_unusable_reason as T
src=T.harness_source();old=subprocess.check_output(['git','show','10f3332:tools/probe_ui_model_mutations.py'],cwd=R).decode();holder=unittest.TestCase();td=tempfile.mkdtemp(prefix='mr-qa-regressions-');record={}
def observe(f):
 try:f()
 except T.HarnessDidNotRun as e:raise AssertionError('A control did not run') from e
 except Exception as e:return {'outcome':'FAILED','exception':type(e).__name__,'message':str(e)}
 return {'outcome':'PASSED'}
def battery(tag,s,plan,**kwargs):
 b=T.run_battery(holder,s,plan,2,**kwargs);(O/(tag+'.log')).write_text(b.console+b.stderr);return b
before=[]
for case in T.PRECHECK+T.AGREEMENT:
 if not case.defect:continue
 v=observe(lambda:T.check_case(old,td,case,required=False));assert v['exception']==('UnicodeDecodeError' if case.defect=='B478' else 'AssertionError'),v
 if case.defect=='B490':assert 'CREDITED verdict' in v['message']
 T.check_case(src,td,case);before.append({'case':case.name,'defect':case.defect,'base':v,'final':'PASS'})
record['fails_before']=before
controls={}
strict=T.edited(src,(T.SITE_NATIVE_CAPTURE,T.SITE_NATIVE_CAPTURE[:-1]+', text=True)'))
controls['A-C1']={c.name:observe(lambda:T.check_case(strict,td,c)) for c in T.PRECHECK if c.defect=='B478' and c.phase=='run'}
assert all(v.get('exception')=='UnicodeDecodeError' for v in controls['A-C1'].values())
broken=T.edited(src,(T.SITE_AGREEMENT,'    if True:'))
controls['A-C2']={c.name:observe(lambda:T.check_case(broken,td,c)) for c in T.PRECHECK+T.AGREEMENT if c.defect=='B490'}
assert all(v.get('exception')=='AssertionError' for v in controls['A-C2'].values())
broken=T.edited(src,(T.SITE_RAW_WRITE,'pass  # QA control A-C3'));b=battery('A-C3',broken,T.MIXED);controls['A-C3']=observe(lambda:T.check_mixed(broken,b,2))
broken=T.edited(src,(T.SITE_RAW_WRITE,T.SITE_RAW_WRITE[:-1]+'[-_VIEW_BYTES:])'));controls['A-C4']=observe(lambda:T.check_size(broken,td))
broken=T.edited(src,(T.SITE_POPEN,T.SITE_POPEN[:-1]+", text=True, bufsize=1); p.stdout = (x.encode('utf-8') for x in p.stdout)"))
b=battery('A-C5-utf8',broken,T.MIXED);utf=observe(lambda:T.check_mixed(broken,b,2));b=battery('A-C5-ascii',broken,T.MIXED,parent_c_locale=True);asc=observe(lambda:T.check_mixed(broken,b,2));assert utf['outcome']=='PASSED' and asc['outcome']=='FAILED';controls['A-C5']={'utf8':utf,'ascii':asc,'ascii_exit':b.rc}
faults={}
for tag,s,kwargs,want in [('transfer',T.edited(src,T.TRANSFER_FAULT),{},9),('write',src,{'obstruct':True},9),('baseline-write',src,{'obstruct':True,'clean':T.CLEAN_DIES},2)]:
 b=battery('fault-'+tag,s,{} if tag=='baseline-write' else T.FAULT,**kwargs)
 if tag=='transfer':T.check_corrupted_transfer(s,b)
 else:T.check_failed_write(s,b,want,['baseline-w0','baseline-w1'] if tag=='baseline-write' else ['F01'])
 assert b.rc==want and 'Traceback' not in b.console+b.stderr;faults[tag]={'exit':b.rc,'no_traceback':True,'integrity':[l for l in b.console.splitlines() if l.startswith('  ##    ')]}
broken=T.edited(src,T.TRANSFER_FAULT,(T.SITE_COMPARE,'if False:  # A-C6'));b=battery('A-C6',broken,T.FAULT);controls['A-C6']=observe(lambda:T.check_corrupted_transfer(broken,b))
broken=T.edited(src,(T.SITE_WRITE_HANDLING,'except ZeroDivisionError as error:  # A-C7'));b=battery('A-C7',broken,T.FAULT,obstruct=True);controls['A-C7']=observe(lambda:T.check_failed_write(broken,b,9,['F01']));controls['A-C7']['exit']=b.rc;controls['A-C7']['unhandled_traceback']='Traceback' in b.console+b.stderr
for tag in ['A-C3','A-C4','A-C6','A-C7']:assert controls[tag]['outcome']=='FAILED' and controls[tag]['exception']=='AssertionError',(tag,controls[tag])
for key,pattern,replacement in [('collapsed','if reason == "build":','if True:'),('deleted-retention','path.write_text(view, encoding="ascii")','pass  # QA control')]:
 T.MutationUnusableReasonTests('test_merged_label_is_absent').control(pattern,replacement);controls[key]={'selftest':'RED (regression PASS)'}
record.update(controls=controls,integrity_faults=faults,verdict='PASS');(O/'controls-a.json').write_text(json.dumps(record,indent=2)+'\n');holder.doCleanups();print('PASS all seven new controls, both existing controls, fails-before',len(before),'fault exits 9/9/2')
