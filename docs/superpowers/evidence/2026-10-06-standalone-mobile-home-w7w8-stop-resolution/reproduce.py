from pathlib import Path
import ast,json,subprocess,os,re,typing,hashlib,time
P=Path(Path('/tmp/w7w8-stop-qa-location').read_text());R=Path('/home/staszek/MeshRoute')
source=(R/'tools/probe_ui_model_mutations.py').read_text();tree=ast.parse(source);nodes={};values={}
for n in tree.body:
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
  nodes[n.targets[0].id]=n.value
  try:values[n.targets[0].id]=ast.literal_eval(n.value)
  except (ValueError,TypeError):pass
mapping={ast.literal_eval(k):v.id for k,v in zip(nodes['MUTS_BY_TARGET'].keys,nodes['MUTS_BY_TARGET'].values)}
sel=[('model','Y02'),('model','Y07'),('uipresets','U13'),('uipresets','W6-P1'),('uipresets','W6-P3')]
mutants=[]
for battery,id_ in sel+[('model','W8-M04')]:
 matches=[m for m in values[mapping[battery]] if m[0].split()[0]==id_];assert len(matches)==1
 mutants.append((battery,id_,values['TARGET_SRC'][battery],matches[0][1],matches[0][2]))
fn_nodes=[n for n in tree.body if isinstance(n,(ast.ClassDef,ast.FunctionDef)) and n.name in ['SuiteOutput','SuiteFailure','run_suite']]
ns={'NamedTuple':typing.NamedTuple,'subprocess':subprocess,'os':os,'re':re};exec(compile(ast.Module(body=fn_nodes,type_ignores=[]),'<stock run_suite AST>','exec'),ns)
results=[]
def run(root,tag):
 ns['ROOT']=str(root);t=time.time();counts,out=ns['run_suite']();(P/(tag+'.stdout')).write_bytes(out.stdout);(P/(tag+'.stderr')).write_bytes(out.stderr)
 d={'tag':tag,'tree':str(root),'counts':counts,'rc':out.rc,'reason':getattr(out,'reason',None),'note':getattr(out,'note',None),'elapsed_seconds':round(time.time()-t,2),'stdout_sha256':hashlib.sha256(out.stdout).hexdigest(),'stderr_sha256':hashlib.sha256(out.stderr).hexdigest()};results.append(d);(P/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(d),flush=True);return d
base=P/'base';candidate=P/'candidate'
assert run(base,'base-clean')['counts']==(0,3059,199638)
for battery,id_,target,pattern,replacement in mutants[:5]:
 f=base/target;orig=f.read_text();assert orig.count(pattern)==1;f.write_text(orig.replace(pattern,replacement))
 try:
  d=run(base,'base-unsafe-'+id_);assert d['counts'] is None and d['rc']==-11
 finally:f.write_text(orig)
repairs=[('test/test_firmware_ui_model.cpp','    CHECK(std::strcmp(compose_empty_note(l), kNoPresetsText) == 0);','    CHECK((compose_empty_note(l) != nullptr && std::strcmp(compose_empty_note(l), kNoPresetsText) == 0));'),('test/test_firmware_ui_presets.cpp','    CHECK(std::strcmp(f.cat.slot(mrfw::kPresetEmergency).text, mrfw::kPresetDefaults[0].text) == 0);','    CHECK((mrfw::kPresetDefaults[0].text != nullptr && std::strcmp(f.cat.slot(mrfw::kPresetEmergency).text, mrfw::kPresetDefaults[0].text) == 0));'),('test/test_firmware_ui_presets.cpp','        CHECK(std::strcmp(mrfw::preset_boot_line(st),\n                          "  ui presets = DEFAULTS (old v1 record — re-enter custom phrases)") == 0);','        CHECK((mrfw::preset_boot_line(st) != nullptr && std::strcmp(mrfw::preset_boot_line(st),\n                          "  ui presets = DEFAULTS (old v1 record — re-enter custom phrases)") == 0));')]
(P/'proposed-guards.json').write_text(json.dumps(repairs,indent=2)+'\n')
for root in [base,candidate]:
 for path,old,new in repairs:
  f=root/path;s=f.read_text();assert s.count(old)==1;f.write_text(s.replace(old,new))
assert run(base,'base-guarded-clean')['counts']==(0,3059,199638)
for battery,id_,target,pattern,replacement in mutants[:5]:
 f=base/target;orig=f.read_text();assert orig.count(pattern)==1;f.write_text(orig.replace(pattern,replacement))
 try:
  d=run(base,'base-guarded-'+id_);assert d['counts'] and d['counts'][0]>0 and d['rc']==1
 finally:f.write_text(orig)
assert run(candidate,'candidate-guarded-clean')['counts']==(0,3123,203168)
battery,id_,target,pattern,replacement=mutants[-1];f=candidate/target;orig=f.read_text();assert orig.count(pattern)==1;f.write_text(orig.replace(pattern,replacement))
try:
 d=run(candidate,'candidate-'+id_);assert d['counts'] and d['counts'][0]>0 and d['rc']==1
finally:f.write_text(orig)
print('ALL CHECKPOINT EXPERIMENTS COMPLETE',flush=True)
