"""AST/read-only census. Never import the executable mutation harness."""
from pathlib import Path
import ast,re,json,subprocess
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent
s=(R/'tools/probe_ui_model_mutations.py').read_text();t=ast.parse(s);vs={};ns={}
for n in t.body:
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
  k=n.targets[0].id;ns[k]=n.value
  try:vs[k]=ast.literal_eval(n.value)
  except (ValueError,TypeError):pass
mp=ns['MUTS_BY_TARGET'];names={ast.literal_eval(k):v.id for k,v in zip(mp.keys,mp.values)};src=vs['TARGET_SRC']
fence=['src/firmware_ui_model.h','src/firmware_ui.cpp','src/firmware_ui_editor.h','src/firmware_ui_chrome.h']
a=[k for k,v in src.items() if v in fence];b=['chrome','uistatus','uiteam','uiinvite','uiprov','uijoin','uipresets','uisend','config','devicenv','input','teamkeyring','provservice']
b=[k for k in b if k in names];rows=[];summary={}
zones={'src/firmware_ui_model.h':[(2254,2260),(2666,2690),(2990,2994),(3042,3136),(3232,3332),(3826,3870),(4282,4410),(4579,4596),(6242,6304)],'src/firmware_ui_chrome.h':[(325,402)],'src/firmware_ui_status.h':[(1,9999)]}
for k in dict.fromkeys(a+b):
 muts=vs[names[k]];txt=(R/src[k]).read_text();lines=txt.splitlines();summary[k]={'file':src[k],'entries':len(muts),'selector':'a' if k in a else 'b'}
 for x,n in zip(muts,ns[names[k]].elts):
  label,p,repl=x[:3];pos=[i for i,l in enumerate(lines,1) if p in l] if '\n' not in p else [txt[:m.start()].count('\n')+1 for m in re.finditer(re.escape(p),txt)]
  rows.append(dict(battery=k,line=n.lineno,label=label,source=src[k],pattern=p,replacement=repl,literal_matches=txt.count(p),positions=pos,in_candidate_region=any(lo<=i<=hi for i in pos for lo,hi in zones.get(src[k],[]))))
(O/'mutation-census.json').write_text(json.dumps(dict(fence=fence,selector_a=a,selector_b=b,batteries=summary,entries=rows),indent=2)+'\n')
print('A',[(k,summary[k]['entries']) for k in a]);print('B',[(k,summary[k]['entries']) for k in b]);print('union',sum(v['entries'] for v in summary.values()))
symbols=['HomeView','SetupOrigin','ReviewPhase','own_name','ui_display_byte','detail_page_rows','ui_nav_slot','home_activate','enter_setup_from_home','rename_node','take_send_request','take_inbox_request','ui_review_capture','ui_allows_sleep']
readers=[];cases=[]
paths=sorted(set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=R).decode().split('\0'))-{''})
for rel in paths:
 p=R/rel
 if not p.is_file() or p.suffix not in ['.h','.cpp','.py','.sh'] or not rel.startswith(('src/','lib/','test/','tools/')):continue
 lines=p.read_text(errors='replace').splitlines()
 for i,l in enumerate(lines,1):
  hits=[w for w in symbols if re.search(r'\b'+re.escape(w)+r'\b',l)]
  sourcehit=any(w in l for w in ['firmware_ui_model.h','firmware_ui.cpp','firmware_ui_status.h','firmware_ui_chrome.h','firmware_ui_input.h'])
  if hits or sourcehit:readers.append(dict(file=rel,line=i,symbols=hits,text=l))
 if rel.startswith('test/test_firmware_ui'):
  for i,l in enumerate(lines,1):
   if 'TEST_CASE(' in l and any(w in l.lower() for w in ['home','my device','provision','review','blank','wake','emergency','overlay','alarm','input','sleep','chrome','frame','name']):cases.append(dict(file=rel,line=i,label=l))
(O/'symbol-readers.json').write_text(json.dumps(readers,indent=2)+'\n');(O/'native-cases.json').write_text(json.dumps(cases,indent=2)+'\n')
# Probe controls that mention W7 candidate regions, with full source statements retained for disposition.
sh=(R/'tools/probe_firmware_ui/run.sh').read_text();controls=[]
for m in re.finditer(r'^\s*ctl "([^"]+)".*?(?=\n\s*(?:#|ctl |[A-Za-z_]+[=()])|\Z)',sh,re.M|re.S):
 if any(w in m[0] for w in ['home_view','home_activate','my_device','review','s_model','s_frame_state','on_gesture','draw_frame','take_send','settings_follow_screen','body_text(4']):controls.append(dict(line=sh[:m.start()].count('\n')+1,label=m[1],source=m[0]))
(O/'probe-control-candidates.json').write_text(json.dumps(controls,indent=2)+'\n');print('readers',len(readers),'cases',len(cases),'control candidates',len(controls))
