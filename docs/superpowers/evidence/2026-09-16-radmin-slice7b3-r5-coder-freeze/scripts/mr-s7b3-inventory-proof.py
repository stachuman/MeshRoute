from pathlib import Path
import importlib.util,sys,dataclasses,json,hashlib
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';spec=importlib.util.spec_from_file_location('inventory',r/'tools/gen_command_inventory.py');m=importlib.util.module_from_spec(spec);sys.modules['inventory']=m;spec.loader.exec_module(m)
sets=[]
for root in [q/'original',r]:
 rows,*_=m.build_rows(str(root));normalized=[]
 for row in rows:
  d=dataclasses.asdict(row);d.pop('line',None);d.pop('source',None);normalized.append(d)
 sets.append(sorted(normalized,key=lambda d:json.dumps(d,sort_keys=True)))
print('rows',*[len(x) for x in sets]);assert len(sets[0])==len(sets[1])==204
if sets[0]!=sets[1]:
 print('FIELDS',sets[0][0].keys());print('FIRST DIFF',next((a,b) for a,b in zip(*sets) if a!=b));raise SystemExit(1)
encoded=json.dumps(sets[1],sort_keys=True).encode();(q/'inventory-semantics.json').write_text(json.dumps(dict(rows=204,sha256=hashlib.sha256(encoded).hexdigest(),normalized_rows=sets[1]),indent=2)+'\n');print('ALL 204 SEMANTIC ROWS IDENTICAL')
