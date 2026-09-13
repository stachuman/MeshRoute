from pathlib import Path
import os,json,hashlib,subprocess,shutil,stat
r=Path('/home/staszek/MeshRoute');q=Path(__file__).resolve().parent
paths=sorted(set(x.decode() for x in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=r).split(b'\0') if x))
def item(p):
 st=p.lstat();o={'mode':stat.S_IMODE(st.st_mode)}
 if p.is_symlink():o.update(kind='symlink',target=os.readlink(p))
 else:o.update(kind='file',sha256=hashlib.sha256(p.read_bytes()).hexdigest())
 return o
before=json.loads((q/'inputs-before.json').read_text());assert {p:item(r/p) for p in paths}==before
manifest=Path('/tmp/mr-codex-s7b2-rFHVpI/handoff-inputs-except-receipt.json');cm=json.loads(manifest.read_text())
assert hashlib.sha256(manifest.read_bytes()).hexdigest()=='984bebeb6a063b9d23187f8e86e8ce0d69cc188da73efbc21b2309a4a6138eb0'
for p,h in cm.items():
 o=before[p]; actual=hashlib.sha256(('SYMLINK:'+o['target']).encode()).hexdigest() if o['kind']=='symlink' else o['sha256'];assert actual==h,p
(q/'logs/snapshot-audit-initial.txt').write_text('First QA comparison stopped on four symlink hash encodings. Coder final_audit.py:125 hashes SYMLINK: plus target. QA now independently hashes that exact representation; all 1086 inputs match. No input mismatch or source change.\n')
(q/'coder-manifest.json').write_bytes(manifest.read_bytes())
changed=subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=r,text=True);(q/'changed-from-base.txt').write_text(changed)
(q/'shared-status.txt').write_text(subprocess.check_output(['git','status','--short'],cwd=r,text=True))
def overlay(dest,names):
 for name in names:
  p=r/name;d=dest/name;d.parent.mkdir(parents=True,exist_ok=True)
  if p.is_symlink():
   if d.exists() or d.is_symlink():d.unlink()
   d.symlink_to(os.readlink(p))
  else:shutil.copyfile(p,d);os.chmod(d,p.stat().st_mode & 0o777)
for kind in ['frozen','gate','mutations','measure']:
 dest=q/kind;subprocess.run(['git','clone','--shared','--quiet',str(r),str(dest)],check=True)
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=dest,text=True).strip()=='564f460a3b755a104f146da70457e5c8c68e99b9'
 selected=paths if kind!='measure' else [p for p in paths if p=='MEMORY.md' or p.startswith('docs/')]
 overlay(dest,selected)
 if kind!='measure':assert {p:item(dest/p) for p in paths}==before
assert {p:item(r/p) for p in paths}==before
receipt={'head':'564f460a3b755a104f146da70457e5c8c68e99b9','simulator_head':'06746a97de5764415d6fcef10b97bca90569b9c7','input_count':len(paths),'coder_manifest_matched':len(cm),'brief_sha256':before['docs/superpowers/plans/2026-09-08-radmin-slice7b2-session-open-status.md']['sha256'],'coder_report_sha256':before['docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md']['sha256'],'frozen_matches_shared':True,'root':str(q)}
(q/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
