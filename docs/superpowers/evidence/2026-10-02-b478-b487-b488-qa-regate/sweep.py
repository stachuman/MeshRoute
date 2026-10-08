import ast,hashlib,json,subprocess,tempfile
from pathlib import Path
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';T=Path(tempfile.mkdtemp(prefix='mr-qa-a10-'))
def row(s):
 a=next(n for n in ast.parse(s).body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id=='MUTS_W4AIDENT')
 return next(r for r in ast.literal_eval(a.value) if r[0].startswith('F07 '))
_,p,new=row((R/'tools/probe_ui_model_mutations.py').read_text());_,op,old=row(subprocess.check_output(['git','show','4c1a000:tools/probe_ui_model_mutations.py'],cwd=R).decode());assert op==p
h=(R/'src/firmware_ui_model.h').read_text();assert h.count(p)==1
result={'scratch':str(T),'pattern':p,'new':new,'old':old,'arms':{}};windows={}
for arm,replacement in [('clean',p),('new',new),('old',old)]:
 d=T/arm;d.mkdir();header=h.replace(p,replacement);(d/'firmware_ui_model.h').write_text(header)
 cmd=['g++','-std=gnu++2a','-fno-exceptions','-fno-rtti','-O0','-g','-Wall','-Wextra','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2','-DMR_CONSOLE=1','-DPROTOCOL_VERSION=1','-DMR_RADIO_CANARY=1','-I'+str(d)]+['-I'+str(R/x) for x in ['src','lib/core','lib/hal','lib/console','lib/monocypher/src']]+[str(O/'sweep.cpp'),'-o',str(d/'sweep')]
 c=subprocess.run(cmd,capture_output=True);(O/(arm+'-compile.log')).write_bytes(c.stdout+c.stderr);assert c.returncode==0,c.stderr[-1000:]
 r=subprocess.run([str(d/'sweep'),str(d/'windows.bin')],capture_output=True);assert r.returncode==0,(arm,r.returncode,r.stderr)
 v=json.loads(r.stdout);v['exit']=r.returncode;v['header_sha256']=hashlib.sha256(header.encode()).hexdigest();result['arms'][arm]=v;windows[arm]=(d/'windows.bin').read_bytes()
grid=[(n,c,k) for _ in range(2) for n in range(33) for c in range(33) for k in range(49)]
for arm in ['new','old']:
 diffs=[g for i,g in enumerate(grid) if windows[arm][48*i:48*(i+1)]!=windows['clean'][48*i:48*(i+1)]]
 result['arms'][arm]['changed']=len(diffs);result['arms'][arm]['roomy_changes']=sum(n>c>=1 and k>=c+2 for n,c,k in diffs);result['arms'][arm]['exact_changes']=sum(n>c>=1 and k==c+1 for n,c,k in diffs)
for arm in ['clean','new']:
 v=result['arms'][arm];assert v['calls']==106722 and v['cap0_touched']==v['positive_without_nul']==v['outside']==0
assert result['arms']['old']['outside']==result['arms']['old']['positive_without_nul']==992 and result['arms']['old']['furthest']==1
assert result['arms']['new']['changed']==result['arms']['new']['roomy_changes']==35712
result['verdict']='PASS';(O/'sweep.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
