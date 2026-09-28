"""QA-only synthetic source faults; never edits the shared checkout.
Copies every frozen tracked/untracked input (not just HEAD), runs the stock
renderer probe in diagnostic --no-neg mode, and records its actual failures.
No diagnostic result substitutes for the default-mode control gate.
"""
import ast, hashlib, json, pathlib, shutil, subprocess, time
root=pathlib.Path('/home/staszek/MeshRoute')
raw=root/'artifacts/2026-09-28-standalone-mobile-home-w4b-qa'
inputs=json.loads((raw/'inputs.json').read_text())[0]
copy=pathlib.Path('/tmp/meshroute-w4b-qa-20260928-9dm_qgqi/focused-copy')
copy.mkdir(exist_ok=True)
for rel,meta in inputs['files'].items():
 src=root/rel; dst=copy/rel
 dst.parent.mkdir(parents=True,exist_ok=True)
 if meta['kind']=='symlink':
  assert str(src.readlink())==meta['target'],rel
  if not dst.is_symlink(): dst.symlink_to(meta['target'])
 elif meta['kind']=='missing':
  assert not src.exists(),rel
 else:
  assert meta['kind']=='file', (rel,meta)
  assert hashlib.sha256(src.read_bytes()).hexdigest()==meta['sha256'],rel
  shutil.copy2(src,dst)
model=copy/'src/firmware_ui_model.h'; clean=model.read_text()
entries={}
for node in ast.parse((copy/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(node,ast.Assign) and isinstance(node.targets[0],ast.Name) and node.targets[0].id in ('MUTS_MODEL','MUTS_W4BHOME'):
  for e in node.value.elts:
   v=ast.literal_eval(e)
   if v[0].split()[0] in ('M103','H25'): entries[v[0].split()[0]]=v
assert len(entries)==2
results=[]
for key in ('M103','H25'):
 label,old,new=entries[key]
 assert clean.count(old)==1,(key,clean.count(old))
 model.write_text(clean.replace(old,new))
 print('START synthetic '+key,flush=True)
 t=time.monotonic()
 with (raw/f'focused-{key}.log').open('wb') as log:
  p=subprocess.run(['bash',str(copy/'tools/probe_firmware_ui/run.sh'),'--no-neg'],cwd=copy,stdout=log,stderr=subprocess.STDOUT)
 text=(raw/f'focused-{key}.log').read_text(errors='backslashreplace')
 failures=[l for l in text.splitlines() if l.lstrip().startswith('FAIL')]
 r={'entry':key,'label':label,'anchor_matches':1,'old':old,'new':new,'baseline_model_sha256':hashlib.sha256(clean.encode()).hexdigest(),'mutant_model_sha256':hashlib.sha256(model.read_bytes()).hexdigest(),'exit_code':p.returncode,'seconds':round(time.monotonic()-t,3),'failures':failures,'count_lines':[l for l in text.splitlines() if ' passed / ' in l],'copy':str(copy),'copied_paths':len(inputs['files'])}
 results.append(r); (raw/'focused-controls.json').write_text(json.dumps(results,indent=2)+'\n')
 print('END synthetic '+key+' rc='+str(p.returncode)+' failures='+str(len(failures)),flush=True)
 if key=='M103': assert p.returncode==0 and not failures and 'NOT A GATE' in text
 else: assert p.returncode==1 and any('P3v' in l for l in failures)
 model.write_text(clean)
