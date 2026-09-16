from pathlib import Path
import json,hashlib,subprocess,shutil,stat,os,difflib
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-codex-s7b3-0gt630zl')
e=r/'docs/superpowers/evidence/2026-09-16-radmin-slice7b3-r5-checkpoint';e.mkdir(exist_ok=True)
brief='docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md'
before=json.loads((q/'inputs-before.json').read_text())
paths=sorted(set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).decode().split('\0'))-{''})
current={}
for rel in paths:
 if rel.startswith(str(e.relative_to(r))+'/'):continue
 p=r/rel;v={'mode':oct(stat.S_IMODE(p.lstat().st_mode))}
 if p.is_symlink():
  v['target']=os.readlink(p)
  if p.is_file():v['resolved_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 else:v['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
 current[rel]=v
(e/'inputs-at-stop-before-receipt.json').write_text(json.dumps(current,indent=2)+'\n')
changes={p:{'before':before.get(p),'at_stop':current.get(p)} for p in sorted(set(before)|set(current)) if before.get(p)!=current.get(p)}
(e/'input-deltas.json').write_text(json.dumps(changes,indent=2)+'\n')
for name in ['inputs-before.json','status-before.txt','preparation.patch','preflight-anchors.json','base-native-results.json','base-boards-results.json','base-corpus-results.json','synced-source-paths.json']:
 shutil.copy2(q/name,e/name)
shutil.copytree(q/'logs',e/'logs',dirs_exist_ok=True)
shutil.copy2('/tmp/mr-s7b3-native-first.log',e/'logs/shared-native-dependency-failure.log')
for name in ['mr-s7b3-prepare.py','mr-s7b3-base-gates.py','mr-s7b3-sync.py']:
 shutil.copy2(Path('/tmp')/name,e/name)
shutil.copytree(q/'action-iteration-1',e/'action-iteration-1',dirs_exist_ok=True)
shutil.copy2(q/'gate'/brief,e/'brief-52bd0095.md')
shutil.copy2(r/brief,e/'brief-at-stop.md')
(e/'brief-change.diff').write_text(''.join(difflib.unified_diff((q/'gate'/brief).read_text().splitlines(True),(r/brief).read_text().splitlines(True),fromfile='frozen-52bd0095',tofile='live-at-stop')))
(e/'implementation.patch').write_bytes(subprocess.check_output(['git','diff','--binary','HEAD','--','src','lib','test','tools','platformio.ini'],cwd=r))
for p in ['src/firmware_remote_actions.h','src/firmware_remote_actions.cpp']:
 dest=e/'untracked-source'/p;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/p,dest)
(e/'status-at-stop.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=r))
for board in ['gateway','heltec_mobile']:
 shutil.copy2(q/'gate/.pio-measure/s7b3-base'/board/'manifest.json',e/(board+'-base-manifest.json'))
shutil.copy2(q/'corpus-base/manifest.json',e/'corpus-base-manifest.json')
sim=Path('/home/staszek/lora-universal-simulator')
preservation={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip(),
 'brief_initial_sha256':hashlib.sha256((q/'gate'/brief).read_bytes()).hexdigest(),
 'brief_at_stop_sha256':hashlib.sha256((r/brief).read_bytes()).hexdigest(),
 'simulator_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim).decode().strip(),
 'simulator_status':subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim).decode(),
 'private_work_area':str(q),'changed_input_paths':list(changes),
 'implementation_complete':False,'full_gate_run':False,'independent_QA_run':False}
(e/'checkpoint.json').write_text(json.dumps(preservation,indent=2)+'\n')
print(json.dumps(preservation,indent=2))
