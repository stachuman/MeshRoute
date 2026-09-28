from pathlib import Path
import json,subprocess,hashlib,ast,re,sys,difflib
sys.dont_write_bytecode=True
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;C=E.parent/'2026-09-28-b459';RAW=R/'artifacts/2026-09-28-b459-qa'
sha=lambda b:hashlib.sha256(b).hexdigest()
# Reconstruct the W2 base IN MEMORY from the complete diff, then bind it to QA's original hash.
cur=(R/'tools/probe_board_ui/run.sh').read_text();lines=cur.splitlines(keepends=True);patch=(C/'diff-board-run-vs-w2-base.diff').read_text().splitlines(keepends=True);out=[];cursor=0;i=0
while i<len(patch):
 m=re.match(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@',patch[i])
 if not m:i+=1;continue
 start=int(m[1])-1;new=[];old=[];i+=1
 while i<len(patch) and not patch[i].startswith('@@'):
  l=patch[i]
  if l[0] in ' +':new.append(l[1:])
  if l[0] in ' -':old.append(l[1:])
  i+=1
 assert lines[start:start+len(new)]==new
 out+=lines[cursor:start]+old;cursor=start+len(new)
base=''.join(out+lines[cursor:]);assert sha(base.encode())=='0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8'
sys.path.insert(0,str(E.parent/'2026-09-28-standalone-mobile-home-w2'))
from declared import declared_wiring
rows=[]
for text in [base,cur]:
 r=declared_wiring(text.splitlines());rows.append([{k:v for k,v in item.items() if k!='line'} for item in r])
assert rows[0]==rows[1] and len(rows[0])==60
# The precise source readers/predicates lie outside B459's infrastructure diff.
functions=['code_flat','code_of','fn_body','oled_guarded','has_oled_dispatch','has_ble_dispatch','env_defs']+sorted({r['pred'] for r in rows[0]})
unchanged=[]
for fn in functions:
 pat=r'^'+re.escape(fn)+r'\(\).*?(?=^\w+\(\)|\Z)'
 # Compare each original function's full definition via its known start and next standalone brace or one-line body.
 def body(s):
  ls=s.splitlines(keepends=True);n=next((i for i,l in enumerate(ls) if l.startswith(fn+'()')),None)
  if n is None:return None
  if re.search(r'}\s*(?:#.*)?\n?$',ls[n]):return ls[n]
  end=next(i for i in range(n+1,len(ls)) if ls[i].rstrip() == '}');return ''.join(ls[n:end+1])
 b=body(base);a=body(cur)
 if b is not None:assert b==a,fn;unchanged.append(fn)
# Negctl tuple labels and substitutions are structurally identical to the pinned base.
def muts(s):
 return {n.targets[0].id:ast.dump(n.value) for n in ast.parse(s).body if isinstance(n,ast.Assign) and getattr(n.targets[0],'id','') in ('MUT_V3','MUT_V4')}
oldneg=subprocess.check_output(['git','show','8079e54:tools/probe_board_ui/negctl.py'],cwd=R).decode();assert muts(oldneg)==muts((R/'tools/probe_board_ui/negctl.py').read_text())
oldtest=subprocess.check_output(['git','show','8079e54:tools/test_probe_firmware_ui.py'],cwd=R).decode();newtest=(R/'tools/test_probe_firmware_ui.py').read_text()
def binding(s):return next(ast.literal_eval(n.value) for n in ast.parse(s).body if isinstance(n,ast.Assign) and getattr(n.targets[0],'id','')=='ACCOUNTING_STATEMENT')
assert binding(oldtest)==binding(newtest);assert (R/'tools/probe_firmware_ui/run.sh').read_text().count(binding(newtest))==1
oldline=next(l for l in base.splitlines() if 'Control (c)' in l);assert oldline.replace('Control (c)','Control 2') in cur.splitlines()
# Fresh D6/P7 grep; capture all hits, including generic-name collisions, for classified read-only review.
patterns=[r'probe_board_ui|probe_accounting|test_probe_board_ui|test_probe_firmware_ui',r'account_controls|record_verdict|record_guard_failure|controls_final|MR_BOARD_UI_RECORDS|MR_BOARD_UI_NEGCTL_RECORDS|CHK_ITER',r'identities accounted|declarations census|controls run|controls accounted|expected\.tsv']
with (RAW/'reader-greps.txt').open('w') as f:
 for pattern in patterns:
  p=subprocess.run(['rg','-n',pattern,'lib','src','test','tools'],cwd=R,capture_output=True,text=True);assert p.returncode in (0,1);f.write('PATTERN '+pattern+'\n'+p.stdout)
# Historical W2 parser remains usable unchanged against this new output and trace.
cmd=['python3',str(E.parent/'2026-09-28-standalone-mobile-home-w2/reconcile.py'),str(R/'tools/probe_board_ui/run.sh'),str(R/'tools/probe_board_ui/negctl.py'),str(RAW/'step4.trace'),str(RAW/'step4-board.log'),str(E/'w2-reader-reconciliation.json'),str(E/'w2-reader-reconciliation.tsv')]
p=subprocess.run(cmd,cwd=R,capture_output=True,text=True);assert p.returncode==0,p.stdout+p.stderr
assert json.loads((E/'w2-reader-reconciliation.json').read_text())['summary']['verdict']=='PASS'
result={'reconstructed_w2_base_sha256':sha(base.encode()),'wiring_rows_identical':len(rows[0]),'ordered_sed_scripts_identical':sum(len(r['scripts']) for r in rows[0]),'unchanged_reader_predicates':unchanged,'negctl_AST_lists_identical':True,'ACCOUNTING_STATEMENT_unchanged_exactly_once':True,'B461_exact_token_correction':True,'historical_w2_reader':'PASS','readers':'Raw grep hits in artifacts/2026-09-28-b459-qa/reader-greps.txt; manually classified in QA receipt.'}
(E/'reader-audit.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: W2 source hash recovered; 60 checks / 186 control scripts unchanged; historical parser still works; D6 grep captured.')
