from pathlib import Path
import json,hashlib,subprocess,os
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8b-r5-mfhusrrg');o=r/'docs/superpowers/evidence/2026-09-18-radmin-slice8b-preflight-r5';sim=Path('/home/staszek/lora-universal-simulator')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inv=json.loads((q/'inputs.json').read_text());state=json.loads((q/'state.json').read_text());prep=json.loads((q/'preparation-inputs.json').read_text())
def git(root,*args):return subprocess.check_output(['git',*args],cwd=root,text=True).strip()
def unchanged(p,v):
 f=r/p
 return f.is_symlink() and os.readlink(f)==v['target'] if 'target' in v else f.is_file() and sha(f)==v['sha256']
changed=[p for p,v in inv.items() if not unchanged(p,v)]
allowed={'tools/probe_console_sink/structural.py','docs/2026-07-30-open-bug-register.md','docs/superpowers/evidence/2026-09-18-radmin-slice8b.md'}
assert set(changed)<=allowed,changed
production=[p for p in inv if p.startswith(('lib/','src/','test/')) or p=='platformio.ini'];assert not set(changed)&set(production)
protected={p:sha(r/p) for p in prep if p!='docs/2026-07-30-open-bug-register.md'};assert all(protected[p]==prep[p]['sha256'] for p in protected)
assert git(r,'rev-parse','HEAD')==state['base'];assert git(sim,'rev-parse','HEAD')==state['simulator_head'];assert not git(sim,'status','--porcelain')
whitespace={}
for label,tree in [('meshroute',r),('simulator',sim)]:
 c=subprocess.run(['git','diff','--check'],cwd=tree,text=True,capture_output=True);whitespace[label]={'exit':c.returncode,'output':c.stdout+c.stderr};assert c.returncode==0
# Gate snapshot differs from admission only at the explicitly repaired instrument.
primary=[p for p in inv if p.startswith(('lib/','src/','test/','tools/')) or p=='platformio.ini']
wrong=[p for p in primary if sha(r/p)!=sha(q/'snapshot'/p)];assert not wrong,wrong
report={'verdict':'STOP-1 B416, scoped B414 repair only; no full chain or implementation freeze','base':state['base'],'simulator_head':state['simulator_head'],'simulator_status':git(sim,'status','--porcelain'),'brief_sha256':sha(r/state['brief']),'admission_input_count':len(inv),'production_test_platformio_unchanged':len(production),'matching_shared_snapshot_primary_count':len(primary),'snapshot_mismatches':wrong,'changed_admission_paths':changed,'protected_preparation_inputs':protected,'instrument_sha256':sha(r/'tools/probe_console_sink/structural.py'),'whitespace':whitespace,'staged_paths':git(r,'diff','--cached','--name-only')}
(o/'preservation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
