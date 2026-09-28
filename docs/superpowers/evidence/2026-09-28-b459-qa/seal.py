"""Seal independent QA evidence. Use prelanding before status edits, final afterward."""
from pathlib import Path
import json,hashlib,os,subprocess,sys,re,ast
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-qa';REPORT=E.parent/'2026-09-28-b459-qa.md';MODE=sys.argv[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inv=json.loads((E/'inputs.json').read_text());docset={'MEMORY.md','tracker.md','docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md'}
result={}
for key,root in [('meshroute',R),('simulator',S)]:
 changed={}
 for rel,old in inv[key]['files'].items():
  p=root/rel
  if p.is_symlink():
   t=os.readlink(p);v={'type':'symlink','sha256':hashlib.sha256(t.encode()).hexdigest(),'target':t}
  elif p.is_file():v={'type':'file','sha256':sha(p)}
  else:v={'type':'missing'}
  if v!=old:changed[rel]={'before':old,'after':v}
 if MODE=='prelanding' or key=='simulator':assert not changed,changed
 else:assert set(changed)==docset,changed
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();assert head==inv[key]['head']
 staged=subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root,text=True);assert not staged
 status=subprocess.check_output(['git','status','--short'],cwd=root,text=True)
 if key=='simulator':assert not status
 paths=set(subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0'))-{''};added=sorted(paths-set(inv[key]['files']));assert all(p.startswith('docs/superpowers/evidence/2026-09-28-b459-qa') for p in added)
 w=subprocess.run(['git','diff','--check'],cwd=root,capture_output=True);assert w.returncode==0,w.stdout
 result[key]={'head':head,'entry_paths':len(inv[key]['files']),'changed':changed,'added':added,'status':status,'staged':False,'whitespace':'PASS'}
coder_raw=json.loads((E.parent/'2026-09-28-b459/raw-logs.json').read_text())
entry=json.loads((E/'preflight.json').read_text())
exceptions={m['path']:m['current_sha256'] for m in entry['coder_raw_log_mismatches']}
for rel,item in coder_raw['files'].items():assert sha(R/coder_raw['dir']/rel)==exceptions.get(rel,item['sha256']),rel
result['coder_raw_files_preserved_since_QA_entry']=len(coder_raw['files'])
resname='prelanding-preservation.json' if MODE=='prelanding' else 'final-preservation.json';(E/resname).write_text(json.dumps(result,indent=2)+'\n')
if MODE=='prelanding':print('PASS: every entry input preserved before QA landing');sys.exit(0)
# Validate final result counts from our own logs rather than coder summaries.
checks={}
for name,count in [('step3-board-tests',31),('step7-b456-tests',19),('step8-discovery',406)]:
 text=(RAW/(name+'.log')).read_text(errors='surrogateescape');ran=re.findall(r'^Ran (\d+) tests? in ',text,re.M);assert ran==[str(count)],(name,ran);assert re.search(r'^Ran '+str(count)+r' tests? in [^\n]+\n\nOK\n',text,re.M);checks[name]={'tests':count,'skipped':0,'result':'OK'}
assert '197' in (RAW/'step9-inventory.log').read_text()
for p in E.glob('*.py'):ast.parse(p.read_text())
(E/'raw-logs.json').write_text(json.dumps({str(p.relative_to(R)):{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(RAW.iterdir()) if p.is_file()},indent=2)+'\n')
(E/'SHA256SUMS').touch(exist_ok=True)
links=[]
for dest in re.findall(r'\]\(([^)]+)\)',REPORT.read_text()):
 p=REPORT.parent/dest.split('#')[0];assert p.exists(),dest;links.append(dest)
(E/'validation.json').write_text(json.dumps({'test_counts':checks,'report_links':links,'evidence_python_syntax':'PASS'},indent=2)+'\n')
files=sorted(p for p in E.rglob('*') if p.is_file() and p.name!='SHA256SUMS')+[REPORT]
(E/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {os.path.relpath(p,E)}\n' for p in files))
for l in (E/'SHA256SUMS').read_text().splitlines():h,p=l.split('  ',1);assert sha(E/p)==h
print(json.dumps({'verdict':'PASS','report_sha256':sha(REPORT),'manifest_sha256':sha(E/'SHA256SUMS'),'checksum_entries':len(files),'preserved_frozen_inputs_except_documented_QA_landing':True},indent=2))
