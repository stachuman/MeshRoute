import ast,json
from pathlib import Path
root=Path('/home/staszek/MeshRoute');raw=root/'artifacts/2026-09-25-standalone-mobile-home-w3-precheck';tree=ast.parse((root/'tools/probe_ui_model_mutations.py').read_text());values={};assigns={}
# Parse literals only. Importing this executable harness would launch a battery.
for node in tree.body:
 if isinstance(node,ast.Assign) and len(node.targets)==1 and isinstance(node.targets[0],ast.Name):
  name=node.targets[0].id;assigns[name]=node
  try:values[name]=ast.literal_eval(node.value)
  except (ValueError,TypeError):pass
reg=assigns['MUTS_BY_TARGET'].value;registry={ast.literal_eval(k):values[v.id] for k,v in zip(reg.keys,reg.values)};targets=values['TARGET_SRC'];source=(root/'src/firmware_ui_model.h').read_text();named=['M12','M14','M15','M18','M20','M55','M56','M57','M59','M100','Y02'];rows=[]
for label,old,new in registry['model']:
 if label.split()[0] in named:
  assert source.count(old)==1,(label,source.count(old));rows.append({'id':label.split()[0],'label':label,'find':old,'replacement':new,'matches':source.count(old),'source_line':source[:source.index(old)].count('\n')+1})
out={'selector_a':[{'battery':k,'source':v,'entries':len(registry[k])} for k,v in targets.items() if v in ['src/firmware_ui_model.h','src/firmware_ui.cpp']],'dependency_candidates':[{'battery':k,'source':targets[k],'entries':len(registry[k])} for k in ['uipresets','uisend','config','uiprov','chrome','uistatus','uiinvite']],'named_anchors':rows,'model_anchors':[{'id':m[0].split()[0],'label':m[0],'find':m[1],'matches':source.count(m[1])} for m in registry['model']]}
(raw/'mutation-census.json').write_text(json.dumps(out,indent=2)+'\n');print('Selector a',out['selector_a']);print('Dependency candidates',out['dependency_candidates']);print('Requested anchors',[(r['id'],r['matches'],r['source_line']) for r in rows]);assert len(rows)==11
