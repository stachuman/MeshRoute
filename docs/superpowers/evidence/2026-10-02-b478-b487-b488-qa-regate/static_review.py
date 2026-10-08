from pathlib import Path
import ast,difflib,hashlib,json,subprocess,re
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';E=R/'docs/superpowers/evidence'
sha=lambda b:hashlib.sha256(b).hexdigest()
p='tools/probe_ui_model_mutations.py';before=subprocess.check_output(['git','show','10f3332:'+p],cwd=R).decode();after=(R/p).read_text()
def tables(s):
 out={}
 for n in ast.parse(s).body:
  if isinstance(n,ast.Assign):
   names=[x.id for t in n.targets for x in ast.walk(t) if isinstance(x,ast.Name)]
   if any(x.startswith(('MUTS_','_WORKERS')) or x in ('TARGET_SRC','PIN_CASES','PIN_ASSERTS') for x in names):out[','.join(names)]=n
  elif isinstance(n,ast.FunctionDef) and n.name in ('_usable_cores','_workers_default_formula'):out[n.name]=n
 return out
b=tables(before);a=tables(after);assert b.keys()==a.keys();differences=[]
for name in a:
 if ast.dump(a[name],include_attributes=False)!=ast.dump(b[name],include_attributes=False):differences.append(name)
assert differences==['MUTS_W4AIDENT'];old=ast.literal_eval(b['MUTS_W4AIDENT'].value);new=ast.literal_eval(a['MUTS_W4AIDENT'].value)
assert [i for i in range(len(old)) if old[i]!=new[i]]==[6] and old[6][:2]==new[6][:2]
expected='        const uint8_t keep = (len <= cols) ? len : uint8_t(cols - (cap > std::size_t(cols) + 1u ? 0u : 1u));';assert new[6][2]==expected
curhead=subprocess.check_output(['git','show','4c1a000:'+p],cwd=R).decode();diff=list(difflib.unified_diff(curhead.splitlines(),after.splitlines()));assert sum(s.startswith('+') and not s.startswith('+++') for s in diff)==2 and sum(s.startswith('-') and not s.startswith('---') for s in diff)==1
mapping={ast.literal_eval(k):v.id for k,v in a['MUTS_BY_TARGET'].value.keys and zip(a['MUTS_BY_TARGET'].value.keys,a['MUTS_BY_TARGET'].value.values)}
counts={k:len(ast.literal_eval(a[v].value)) for k,v in mapping.items()};targets=ast.literal_eval(a['TARGET_SRC'].value)
assert (len(counts),sum(counts.values()),len(set(targets.values())))==(109,1963,62)
assert (R/'src/firmware_ui_model.h').read_text().count(new[6][1])==1
(O/'census.json').write_text(json.dumps({'verdict':'PASS','batteries':109,'entries':1963,'targets':62,'counts':counts,'protected_AST_differences':differences,'only_changed_tuple':[old[6],new[6]],'against_HEAD_added':2,'against_HEAD_removed':1},indent=2)+'\n')
oldroot=Path('/tmp/mr_mutation_run-3ckbcwpp/w0');bp='tools/probe_inbox_verbs/transcript.py';bs=(oldroot/bp).read_text();ns=(R/bp).read_text()
assert sha(bs.encode())=='89ff8a07f61e5122f564d93e016c038c7774c616121fab1aff3f6f686da52c49'
def funcs(s):return {n.name:ast.dump(n,include_attributes=False) for n in ast.parse(s).body if isinstance(n,(ast.FunctionDef,ast.ClassDef))}
x,y=funcs(bs),funcs(ns);changed=[k for k in x if x[k]!=y[k]];assert changed==['parse_ledger']
handlers=lambda s:[ast.dump(n,include_attributes=False) for n in ast.walk(ast.parse(s)) if isinstance(n,ast.ExceptHandler)]
assert handlers(bs)==handlers(ns)
(O/'B491-source.diff').write_text(''.join(difflib.unified_diff(bs.splitlines(True),ns.splitlines(True),fromfile='QA-first-freeze',tofile='QA-regate')))
nums={}
for f in ['tools/test_mutation_unusable_reason.py','tools/test_probe_inbox_transcript.py']:
 def tests(s):return [n.name+'.'+m.name for n in ast.parse(s).body if isinstance(n,ast.ClassDef) for m in n.body if isinstance(m,ast.FunctionDef) and m.name.startswith('test_')]
 oldtests=[] if f.endswith('test_probe_inbox_transcript.py') else tests(subprocess.check_output(['git','show','10f3332:'+f],cwd=R).decode());newtests=tests((R/f).read_text());assert not set(oldtests)-set(newtests);nums[f]={'base':len(oldtests),'now':len(newtests),'added':sorted(set(newtests)-set(oldtests))}
assert nums['tools/test_mutation_unusable_reason.py']['base']==7 and all(v['now']==24 for v in nums.values())
result={'verdict':'PASS','B491_changed_symbols':changed,'verify_profile_unchanged':x['verify_profile']==y['verify_profile'],'all_exception_handlers_unchanged':True,'tests':nums,'discovery_expected':406+17+24,'syntax_files':[]}
for f in [p,'tools/test_mutation_unusable_reason.py',bp,'tools/test_probe_inbox_transcript.py']:
 compile((R/f).read_bytes(),f,'exec');result['syntax_files'].append(f)
(O/'static-review.json').write_text(json.dumps(result,indent=2)+'\n')
# Independently check the claim table against the complete caller grep, and reconstruct production budgets.
claim=json.loads((E/'2026-10-02-b478-b487-b488-return/a10-census.json').read_text());rows=claim['rows'];assert len(rows)==24
hits=[]
for root in ['src','test']:
 for file in (R/root).rglob('*'):
  if not file.is_file():continue
  for i,line in enumerate(file.read_text(errors='backslashreplace').splitlines(),1):
   code=line.split('//')[0]
   if not re.search(r'\b(ui_fmt_identity|label_from_hash|label_for_team_id|label_for_origin)\s*\(',code):continue
   if re.search(r'^\s*(inline IdentityFmt ui_fmt_identity|void label_from_hash|uint32_t label_for_team_id|void label_for_origin)\b',code):continue
   matches=[r for r in rows if r['file']==str(file.relative_to(R)) and r['call'] in line];assert len(matches)==1,(file,i,line);hits.append({'path':str(file.relative_to(R)),'line':i,'call':line.strip()})
for row in rows:assert (R/row['file']).read_text().count(row['call'])==row['occurrences']
checks={'src/firmware_ui_model.h':['kLabelCap     = 14','char     label[kLabelCap + 1]'], 'src/firmware_ui_team.h':['kTeamLabelCols = 6'],'src/firmware_ui_invite.h':['kInviteNameCap = 15','kInviteRowNameCols = 6'],'src/firmware_ui_status.h':['kHomeNameCols = 16','char id[kHomeNameCols + 1]'],'src/firmware_ui_send.h':['kReviewLabelCols = 7','char label[kReviewLabelCols + 1]'],'src/firmware_ui.cpp':['kBodyCols = 19','kComposeToPrefix[] = "to: "','kComposeToCols     = uint8_t(kBodyCols - int(sizeof kComposeToPrefix - 1))','kDeliveredCols     = uint8_t(kBodyCols)','char name6[kInviteCandCols + 1]']}
for f,parts in checks.items():
 for needle in parts:assert needle in (R/f).read_text(),(f,needle)
prod=[r for r in rows if r['file'].startswith('src/') and r['extent'] is not None]
assert [(r['extent'],r['capacity'],r['budget']) for r in prod]==[(8,8,7),(17,17,16),(15,15,14),(7,7,6),(15,15,6),(20,20,19),(16,16,15),(15,15,14)]
assert all(r['extent']>=r['capacity']>=r['budget']+1 for r in prod)
# Other executable probe access goes through the generated wrapper, whose P28 capacity<=15 and width14 are checked in the reader audit.
model=(R/'test/test_firmware_ui_model.cpp').read_text();calls=[]
for m in re.finditer(r'id_fmt\(b, (\d+),\s*([^,]+), (\d+), ([^,]+), (\w+)\)',model):
 cap,ln,c=int(m[1]),int(m[3]),m[5];cls='variable' if not c.isdigit() else 'short' if cap<int(c)+1 else 'exact' if cap==int(c)+1 else 'roomy'
 calls.append({'line':model.count('\n',0,m.start())+1,'capacity':cap,'name_len':ln,'budget':c,'class':cls,'active':c.isdigit() and cls=='roomy' and ln>int(c)})
from collections import Counter
assert dict(Counter(x['class'] for x in calls))=={'exact':25,'roomy':7,'variable':2,'short':4}
assert sum(x['active'] for x in calls)==5
(O/'callers.json').write_text(json.dumps({'verdict':'PASS','rows_verified':rows,'caller_grep':hits,'production_extent_capacity_width':[(r['site'],r['extent'],r['capacity'],r['budget']) for r in prod],'only_roomy_production_site':'TEAM','native_id_fmt_calls_verified':calls,'scope':'src/test plus probe trampoline audited separately'},indent=2)+'\n')
print('PASS protected tables 109/1963/62; syntax four; B491 only parse_ledger, handlers unchanged; 24 caller rows and eight production capacities verified')
