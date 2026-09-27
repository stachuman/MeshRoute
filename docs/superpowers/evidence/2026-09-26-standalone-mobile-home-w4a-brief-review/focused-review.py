"""Read-only brief-review measurements; no W4a implementation or full gate."""
from pathlib import Path
import ast,json,subprocess
root=Path.cwd();out=Path('/tmp/meshroute-w4a-brief-review')
tree=ast.parse((root/'tools/probe_ui_model_mutations.py').read_text()); vals={}
def value(x):
 if isinstance(x,ast.Name):return vals[x.id]
 if isinstance(x,ast.Dict):return {value(k):value(v) for k,v in zip(x.keys,x.values)}
 if isinstance(x,(ast.List,ast.Tuple)):return [value(v) for v in x.elts]
 if isinstance(x,ast.BinOp) and isinstance(x.op,ast.Add):return value(x.left)+value(x.right)
 return ast.literal_eval(x)
for n in tree.body:
 if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
  try:vals[n.targets[0].id]=value(n.value)
  except (ValueError,KeyError,TypeError):pass
sel_a=['model','sliceCbudget','uiteam','uiinvite'];sel_b=['uisend','sliceCsend','chrome','uinearbyrow','uigeo'];groups=[]
for name in sel_a+sel_b:
 p=vals['TARGET_SRC'][name];s=(root/p).read_text()
 groups.append(dict(battery=name,source=p,count=len(vals['MUTS_BY_TARGET'][name]),entries=[dict(name=x[0],matches=s.count(x[1])) for x in vals['MUTS_BY_TARGET'][name]]))
(out/'mutation-check.json').write_text(json.dumps(dict(selector_a=sel_a,selector_b=sel_b,batteries=groups),indent=2)+'\n')
# Algebra only, not implementation: check the approved name-only projection.
def display(bs,cols):
 clean=bytes(b if 32<=b<127 else 46 for b in bs)
 return clean if len(clean)<=cols else clean[:cols-1]+bytes([187])
checks=0
for n in range(33):
 for b in range(256):
  bs=bytes([b])*n;assert display(display(bs,14),6)==display(bs,6);checks+=1
 for i in range(n):
  bs=bytearray(65+j%26 for j in range(n));bs[i]=187
  assert display(display(bs,14),6)==display(bs,6);checks+=1
(out/'format-composition.json').write_text(json.dumps(dict(scope='mathematical measurement, not production implementation',checks=checks,failures=0),indent=2)+'\n')
# Build unchanged production row code and its existing T01/T09 mutants in /tmp.
src=(root/'src/firmware_ui_team.h').read_text(); controls={x[0].split()[0]:x for x in vals['MUTS_BY_TARGET']['uiteam']}
main=r'''
#include "firmware_ui_team.h"
#include <cstdio>
#include <cstring>
int main(){
 mrui::TeamRow t{};t.last_heard_s=12;
 const char formatted[]={'W','o','l','f','g',char(0xbb),0};
 std::memcpy(t.label,formatted,sizeof formatted);
 char out[mrui::kTeamLineCap];mrui::ui_team_row(out,sizeof out,false,t,mrui::GeoFix{});
 std::printf("formatted=");for(unsigned char c:out){if(!c)break;std::printf("%02x",c);}std::puts("");
 std::snprintf(t.label,sizeof t.label,"%s","Wolfgangetta");
 mrui::ui_team_row(out,sizeof out,false,t,mrui::GeoFix{});
 std::printf("synthetic_overlong=%s\n",out);
 mrui::UiSnapshot a{},b{};a.team_shown=b.team_shown=1;
 std::memcpy(a.team[0].label,formatted,sizeof formatted);b=a;
 std::printf("equal_formatted=%d\n",int(mrui::ui_team_rows_equal(a,b)));
 b.team[0].label[10]='X'; // synthetic invisible-tail corruption, not a published W4a row
 std::printf("different_invisible_tail=%d\n",int(mrui::ui_team_rows_equal(a,b)));
}
'''
incs=sorted({str(p.parent) for p in (root/'lib').rglob('*.h')});result={}
for key in ('base','T01','T09'):
 d=out/key;d.mkdir(exist_ok=True);text=src
 if key!='base':
  _,old,new=controls[key];assert text.count(old)==1;text=text.replace(old,new)
 (d/'firmware_ui_team.h').write_text(text);(d/'proof.cpp').write_text(main)
 cmd=['g++','-std=c++20','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2','-I'+str(d),'-I'+str(root/'src'),*['-I'+x for x in incs],str(d/'proof.cpp'),'-o',str(d/'proof')]
 run=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(d/'compile.txt').write_text(run.stdout)
 if run.returncode:raise RuntimeError(run.stdout)
 measured=subprocess.check_output([str(d/'proof')],text=True)
 result[key]=dict(command=cmd,compile_exit=0,output=measured)
print(json.dumps(dict(entries=sum(g['count'] for g in groups),bad_matches=[(g['battery'],e) for g in groups for e in g['entries'] if e['matches']!=1],composition_checks=checks,team_measurements=result),indent=2))
(out/'team-fixture-measurement.json').write_text(json.dumps(result,indent=2)+'\n')
