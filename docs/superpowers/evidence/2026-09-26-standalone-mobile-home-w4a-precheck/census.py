from pathlib import Path
import ast,json,re,subprocess
root=Path.cwd(); raw=root/'artifacts/2026-09-26-standalone-mobile-home-w4a-precheck'
p=root/'tools/probe_ui_model_mutations.py'; tree=ast.parse(p.read_text()); vals={}
def value(x):
 if isinstance(x,ast.Name):return vals[x.id]
 if isinstance(x,ast.Dict):return {value(k):value(v) for k,v in zip(x.keys,x.values)}
 if isinstance(x,(ast.Tuple,ast.List)):return [value(v) for v in x.elts]
 if isinstance(x,ast.BinOp) and isinstance(x.op,ast.Add):return value(x.left)+value(x.right)
 return ast.literal_eval(x)
for n in tree.body:
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
  try: vals[n.targets[0].id]=value(n.value)
  except (ValueError,KeyError,TypeError):pass
sources=vals['TARGET_SRC']; groups=vals['MUTS_BY_TARGET']
result=[]
for key,path in sources.items():
 if key not in groups:continue
 src=(root/path).read_text(); entries=[]
 for ent in groups[key]:
  name,old,new=ent[:3]
  pos=src.find(old)
  entries.append({'name':name,'anchor':old,'replacement':new,'matches':src.count(old),'line':src[:pos].count('\n')+1 if pos>=0 else None})
 result.append({'battery':key,'path':path,'count':len(entries),'entries':entries})
(raw/'mutation-census.json').write_text(json.dumps(result,indent=2)+'\n')
for r in result:
 if 'firmware_ui_' in r['path']:print(r['battery'],r['path'],r['count'])
terms=r'label_from_hash|label_for_team_id|label_for_origin|kLabelCap|kInviteNameCap|kTeamLabelCols|_reply_who|TeamRow|InviteMember|ui_fmt_invite_row|invite_id_rows|invite_name_of|ui_fmt_member_hash_full|ui_fmt_member_fingerprint|ui_display_byte'
files=subprocess.check_output(['rg','--files','src','test','tools','lib'],text=True).splitlines()
reads=[]
for f in files:
 path=root/f
 if path.suffix not in ('.h','.cpp','.c','.py','.sh'):continue
 for i,l in enumerate(path.read_text(errors='replace').splitlines(),1):
  if re.search(terms,l):reads.append({'path':f,'line':i,'text':l})
(raw/'identity-reader-census.json').write_text(json.dumps(reads,indent=2)+'\n')
print('readers',len(reads),'in',len({r['path'] for r in reads}),'files')
