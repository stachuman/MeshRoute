from pathlib import Path
import ast,subprocess,json,hashlib,re
R=Path('/home/staszek/MeshRoute'); OUT=R/'artifacts/2026-10-01-b478-b487-b488-qa'
def base(p):return subprocess.check_output(['git','show','10f3332:'+p],cwd=R).decode()
def sha(s):return hashlib.sha256(s.encode()).hexdigest()
def protected(text):
 out={}; vals={};counts={}
 for n in ast.parse(text).body:
  if isinstance(n,ast.Assign):
   names=[x.id for t in n.targets for x in ast.walk(t) if isinstance(x,ast.Name)]
   if any(x.startswith(('MUTS_', '_WORKERS')) or x in ('TARGET_SRC','PIN_CASES','PIN_ASSERTS') for x in names):
    out[','.join(names)]=sha(ast.dump(n,include_attributes=False))
   if len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
    key=n.targets[0].id
    if key.startswith('MUTS_') and key!='MUTS_BY_TARGET': vals[key]=ast.literal_eval(n.value)
    if key=='TARGET_SRC': targets=ast.literal_eval(n.value)
    if key=='MUTS_BY_TARGET': mapping={ast.literal_eval(k):v.id for k,v in zip(n.value.keys,n.value.values)}
  elif isinstance(n,ast.FunctionDef) and n.name in ('_usable_cores','_workers_default_formula'):
   out[n.name]=sha(ast.dump(n,include_attributes=False))
 return {'protected_ast':out,'counts':{k:len(vals[v]) for k,v in mapping.items()},'targets':targets}
p='tools/probe_ui_model_mutations.py';before=protected(base(p));after=protected((R/p).read_text());assert before==after
record={'identical':True,'batteries':len(after['counts']),'entries':sum(after['counts'].values()),'unique_target_files':len(set(after['targets'].values())),**after}
pre=json.loads((R/'docs/superpowers/evidence/2026-09-30-b478-b487-b488-precheck/union-census.json').read_text());assert record['counts']==pre['counts']
(OUT/'census.json').write_text(json.dumps(record,indent=2)+'\n')
trans='tools/probe_inbox_verbs/transcript.py'
b=ast.parse(base(trans));a=ast.parse((R/trans).read_text());funcs=['extract_regions','shape_of','derive_matrix','_c_lit','build_env','verify_profile']
unchanged={}
for name in funcs:
 old=next(n for n in b.body if isinstance(n,ast.FunctionDef) and n.name==name);new=next(n for n in a.body if isinstance(n,ast.FunctionDef) and n.name==name)
 if name=='derive_matrix': # boundary table is separate
  pass
 unchanged[name]=ast.dump(old,include_attributes=False)==ast.dump(new,include_attributes=False)
assert all(unchanged.values())
# Test derivation by AST, never importing the mutation harness.
def tests(s):return [f'{n.name}.{m.name}' for n in ast.parse(s).body if isinstance(n,ast.ClassDef) for m in n.body if isinstance(m,ast.FunctionDef) and m.name.startswith('test_')]
files=['tools/test_mutation_unusable_reason.py','tools/test_probe_inbox_transcript.py'];nums={}
for p in files:
 old=[] if p==files[1] else tests(base(p));new=tests((R/p).read_text());nums[p]={'before':len(old),'after':len(new),'removed':sorted(set(old)-set(new)),'added':sorted(set(new)-set(old))};assert not nums[p]['removed']
(OUT/'static-review.json').write_text(json.dumps({'transcript_unchanged_functions':unchanged,'test_derivation':nums,'expected_discovery':406+sum(v['after']-v['before'] for v in nums.values())},indent=2)+'\n')
print(record['batteries'],record['entries'],record['unique_target_files'],nums)
