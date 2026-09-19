from pathlib import Path
import subprocess,json,time,hashlib,os,shutil,concurrent.futures
q=Path('/tmp/mr-s9-active').read_text().strip();q=Path(q);logs=q/'logs';logs.mkdir(exist_ok=True)
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator')
assert 'GIT_INDEX_FILE' not in env
assert Path(shutil.which('git')).resolve()==Path('/usr/bin/git').resolve()
results=[]
def run(name,cmd,root):
 print('START',name,flush=True);start=time.monotonic();log=logs/(name+'.log')
 with log.open('wb') as f:rc=subprocess.run(cmd,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 row=dict(name=name,command=cmd,cwd=str(root),exit=rc,seconds=time.monotonic()-start,sha256=hashlib.sha256(log.read_bytes()).hexdigest(),git_executable=shutil.which('git'),GIT_INDEX_FILE=None)
 (logs/(name+'-result.json')).write_text(json.dumps(row,indent=2)+'\n');print('END',name,rc,flush=True);return row
results.append(run('pair',['python3','tools/measure_board.py','pair','--output','.pio-measure/s9-r5','--jobs','1'],q/'boards'))
assert results[-1]['exit']==0
source=q/'boards/.pio-measure/s9-r5/gateway';dest=q/'tools/.pio-measure/r5-fixture/gateway';dest.mkdir(parents=True,exist_ok=True)
for name in ['manifest.json','firmware.elf']:shutil.copy2(source/name,dest/name)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 jobs=[pool.submit(run,'tools',['python3','-m','unittest','discover','-v','-s','tools','-p','test_*.py'],q/'tools'),pool.submit(run,'census',['bash','tools/warning_census.sh'],q/'boards')]
 for fut in concurrent.futures.as_completed(jobs):results.append(fut.result())
(q/'results.json').write_text(json.dumps(results,indent=2)+'\n')
assert all(x['exit']==0 for x in results),[(x['name'],x['exit']) for x in results]
print('SCOPED RETURN COMPLETE',flush=True)
