"""Read-only identity extraction through the frozen runner's actual per-arm defines."""
from pathlib import Path
import ast,json,subprocess,re
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-brief-review';run=R/'tools/probe_board_ui/run.sh';s=run.read_text();prefix=s[:s.index('echo "== shared Heltec board-canvas probe: V3 traits =="')]
script=prefix+'''\npreprocess() {
 local -n defs=$1
 "$CXX" -std=gnu++20 -E -P "${defs[@]}" -I"$HERE/fakes" -I"$CANVAS" -I"$ROOT/lib/hal" -I"$ROOT/lib/core" -I"$ROOT/src" "$HERE/probe_main.cpp" > "$2"
}
preprocess V3_DEFS "$1" && preprocess V4_DEFS "$2"
'''
r=subprocess.run(['bash','-c',script,str(run),str(RAW/'v3-preprocessed.cpp'),str(RAW/'v4-preprocessed.cpp')],capture_output=True,text=True);assert r.returncode==0,r.stderr
result={}
for arm,want in [('v3',124),('v4',110)]:
 text=(RAW/(arm+'-preprocessed.cpp')).read_text();labels=re.findall(r'printf\("  FAIL %-58s  %s\\n",\s*\("([^"\n]+)"\)',text)
 assert 'for (int i = 0; i < 10; ++i)' in text
 ids=[]
 for label in labels:
  key=label.split()[0]
  ids.extend([f'canvas/{arm}/P4a/{i}' for i in range(10)] if key=='P4a' else [f'canvas/{arm}/{key}'])
 assert len(ids)==want and len(set(ids))==want,(arm,len(ids))
 result[arm]={'literal_CHK_sites':len(labels),'expanded_identities':len(ids),'ids':ids,'basis':'Actual-arm preprocessing of the pinned probe; P4a literal ten-iteration loop expanded independently.'}
tree=ast.parse((R/'tools/probe_board_ui/negctl.py').read_text())
for n in tree.body:
 if isinstance(n,ast.Assign) and getattr(n.targets[0],'id','') in ('MUT_V3','MUT_V4'):
  keys=[ast.literal_eval(e.elts[0]).split()[0] for e in n.value.elts];assert len(keys)==len(set(keys));result[n.targets[0].id]=keys
result['setup_note']='The first ad-hoc extractor assumed the wrong whitespace/parentheses in CHK expansion; it failed an exact-count assertion at zero. Corrected to the actual macro spelling; no result from the failed extraction was used.'
(E/'identity-census.json').write_text(json.dumps(result,indent=2)+'\n');print({a:r['expanded_identities'] for a,r in result.items() if isinstance(r,dict)})
