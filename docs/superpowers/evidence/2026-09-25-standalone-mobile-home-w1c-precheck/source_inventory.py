#!/usr/bin/env python3
import ast,collections,hashlib,json,re,runpy,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]).resolve(); out=Path(sys.argv[2]).resolve()
manifest=json.loads((out/'corpus/manifest.json').read_text()); scenarios=[];all_verbs=collections.Counter();suspicious=[]
def strings(x,path=''):
 if isinstance(x,dict):
  for k,v in x.items():yield from strings(v,path+'/'+k)
 elif isinstance(x,list):
  for i,v in enumerate(x):yield from strings(v,path+'/'+str(i))
 elif isinstance(x,str):yield path,x
for row in manifest['scenarios']:
 p=root/row['scenario_path'];data=json.loads(p.read_text());nodes=data['nodes'];names=[n.get('name') for n in nodes];commands=data.get('commands',[])
 verbs=collections.Counter(c.get('command','').split()[0] for c in commands if c.get('command','').split());all_verbs.update(verbs)
 for path,text in strings(data):
  if path.endswith('/command') and re.search(r'(^|\s)(set_name|rename|name)(\s|$)',text):suspicious.append({'scenario':row['name'],'path':path,'text':text})
 scenarios.append({'name':row['name'],'nodes':len(nodes),'missing_or_empty_names':[i for i,n in enumerate(names) if not isinstance(n,str) or not n],'embedded_nul_names':[i for i,n in enumerate(names) if isinstance(n,str) and '\0' in n],'node_names_sha256':hashlib.sha256(json.dumps(names,ensure_ascii=False).encode()).hexdigest(),'commands':len(commands),'verbs':dict(verbs),'lua_commands':[c for c in commands if 'lua' in c],'scripts':[n.get('script') for n in nodes if n.get('script')],'engines':dict(collections.Counter(n.get('engine','default') for n in nodes))})
assert len(scenarios)==36 and not suspicious and all(not r['missing_or_empty_names'] and not r['embedded_nul_names'] and not r['lua_commands'] for r in scenarios)
corpus={'total_nodes':sum(s['nodes'] for s in scenarios),'scenarios':scenarios,'command_verbs':dict(all_verbs),'rename_candidates':suspicious,'source_limitation':'require_field<string> proves presence/type, not nonemptiness; scenario contents establish nonempty names.'};(out/'scenario-name-census.json').write_text(json.dumps(corpus,indent=2)+'\n')
tree=ast.parse((root/'tools/probe_ui_model_mutations.py').read_text()); vals={};nodes={}
for n in tree.body:
 if isinstance(n,(ast.Assign,ast.AnnAssign)):
  for t in n.targets if isinstance(n,ast.Assign) else [n.target]:
   if isinstance(t,ast.Name):
    nodes[t.id]=n.value
    try:vals[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):pass
mapping=vals['TARGET_SRC'];bind={ast.literal_eval(k):v.id for k,v in zip(nodes['MUTS_BY_TARGET'].keys,nodes['MUTS_BY_TARGET'].values)}
files=['lib/core/node.cpp','lib/core/node.h','src/firmware_commands.cpp','src/fw_main.cpp','lib/console/console_parse.cpp','test/test_node_r3.cpp','test/test_node_hashlocate.cpp','test/test_frame_codec.cpp','tools/probe_inbox_verbs/probe_main.cpp','tools/probe_inbox_verbs/run.sh']
rows=[{'target':k,'source':v,'entries':len(vals[bind[k]])} for k,v in mapping.items() if v in files]
extra=[{'target':k,'source':v,'entries':len(vals[bind[k]])} for k,v in mapping.items() if v=='lib/core/node_hashlocate.cpp']
(out/'mutation-selectors.json').write_text(json.dumps({'method':'AST only; harness was not imported','mappings':len(mapping),'recommended_files':files,'selector_a':rows,'selector_a_total':sum(r['entries'] for r in rows),'if_node_hashlocate_changed_add':extra},indent=2)+'\n')
abi=ast.parse((root/'tools/probe_board_abi.py').read_text());table=next(n.value for n in abi.body if isinstance(n,ast.AnnAssign) and isinstance(n.target,ast.Name) and n.target.id=='PIN_TABLE');pins={ast.literal_eval(k):{'meshroute::Node':next(ast.literal_eval(vv) for kk,vv in zip(v.keys,v.values) if ast.literal_eval(kk)=='meshroute::Node')} for k,v in zip(table.keys,table.values)};(out/'abi-pins.json').write_text(json.dumps({k:v['meshroute::Node'] for k,v in pins.items()},indent=2)+'\n')
parser=runpy.run_path(str(root/'tools/lab/parsers.py'))['parse_whoami'];results=[]
for field,want in [(' name=""',''),(' name="Bench 1"','Bench 1'),('',None)]:
 text='[whoami] id=17 hash=0xDEADBEEF'+field+' leaf=0 gw=0 gwonly=0 mobile=1';got=parser([text]);assert got['name']==want and got['node_id']==17 and got['mobile'] is True;results.append({'input':text,'expected_name':want,'result':got})
(out/'whoami-parser-checks.json').write_text(json.dumps(results,indent=2)+'\n')
for term in ['effective_name','MeshRoute node:']:
 p=subprocess.run(['rg','-n','-F',term,'lib','src','test','tools'],cwd=root,capture_output=True,text=True);assert p.returncode==0;(out/('effective-name-users.txt' if term=='effective_name' else 'default-name-users.txt')).write_text(p.stdout)
print('scenarios',len(scenarios),'nodes',corpus['total_nodes'],'command verbs',dict(all_verbs),'scripts',sum(len(x['scripts']) for x in scenarios))
print('selector a',rows,'extra',extra)
print('ABI pins', {k:v['meshroute::Node'] for k,v in pins.items()});print('whoami parser: 3 cases PASS')
