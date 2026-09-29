"""Static reader/mutation census; never import the mutation runner (its import executes work)."""
from pathlib import Path
import ast,json,re
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent
p=R/'tools/probe_ui_model_mutations.py';source=p.read_text();tree=ast.parse(source);values={}; nodes={}
for n in tree.body:
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
  k=n.targets[0].id;nodes[k]=n.value
  try: values[k]=ast.literal_eval(n.value)
  except (ValueError,TypeError):pass
src=values['TARGET_SRC']; by=nodes['MUTS_BY_TARGET'];names={ast.literal_eval(k):v.id for k,v in zip(by.keys,by.values)}
fence=['src/device_nv.h','src/firmware_ui_presets.h','src/firmware_ui_preset_verbs.h','src/firmware_commands.cpp','src/firmware_ui_model.h','src/firmware_ui.cpp','src/firmware_ui_send.h']
a=[k for k,v in src.items() if v in fence]
b=['chrome','uistatus','uiteam','uiinvite','uiprov','uijoin','provservice','config','teamkeyring','consoleline']
rows=[];summary={}
for key in dict.fromkeys(a+b):
 var=names[key]; muts=values.get(var)
 assert isinstance(muts,list),(var,type(muts))
 text=(R/src[key]).read_text();summary[key]={'file':src[key],'count':len(muts),'selector':'a' if key in a else 'b'}
 for x,node in zip(muts,nodes[var].elts):
  # tuples are label, regex, replacement; save even out-of-region entries so the author can classify each by edit.
  label,pattern,repl=x[:3]; count=text.count(pattern)
  rows.append(dict(battery=key,line=node.lineno,label=label,source=src[key],pattern=pattern,replacement=repl,literal_matches=count))
(E/'mutation-census.json').write_text(json.dumps({'selector_a':a,'selector_b':b,'batteries':summary,'entries':rows},indent=2)+'\n')
print('selector_a',[(k,summary[k]['count']) for k in a]);print('selector_b',[(k,summary[k]['count']) for k in b]);print('total',sum(x['count'] for x in summary.values()))
# Every candidate pure-source reader: preserve matching source line plus check family context for tools which grep production.
pat=re.compile(r'firmware_ui(_model|_send|_presets|_preset_verbs)?\.(?:h|cpp)|device_nv\.h|firmware_commands\.cpp')
readers=[]
for p in (R/'tools').rglob('*'):
 if not p.is_file() or p.suffix not in ('.py','.sh','.cpp','.h') or '__pycache__' in p.parts:continue
 for n,l in enumerate(p.read_text(errors='replace').splitlines(),1):
  if pat.search(l):readers.append(dict(file=str(p.relative_to(R)),line=n,text=l))
(E/'source-readers.json').write_text(json.dumps(readers,indent=2)+'\n')
# Case names, preserving exact labels and source location; filter facts by test body under later manual review.
cases=[]
for p in sorted((R/'test').glob('test_firmware_ui*.cpp')):
 ls=p.read_text().splitlines(); hits=[(i,l) for i,l in enumerate(ls) if re.search(r'\bTEST_CASE\(',l)]
 for z,(i,l) in enumerate(hits):
  end=hits[z+1][0] if z+1<len(hits) else len(ls);body='\n'.join(ls[i:end])
  if any(s in body for s in ['preset','SendReq','send_gate_of','compose_gesture','Compose::','kDetail','detail_page','double_press']):cases.append(dict(file=str(p.relative_to(R)),line=i+1,end=end,label=l))
(E/'native-cases.json').write_text(json.dumps(cases,indent=2)+'\n')
print('readers',len(readers),'native case candidates',len(cases))
