from pathlib import Path
import os,subprocess,shutil,json,hashlib
q=Path('/tmp/mr-s9-r4-j37ybea_');rows=[]
for name in ['final-boards','final-gate']:
 r=q/name;index=q/(name+'-measurement.index');original=r/'.git/index';original_hash=hashlib.sha256(original.read_bytes()).hexdigest();shutil.copy2(original,index)
 env=dict(os.environ,GIT_INDEX_FILE=str(index));deleted=[os.fsdecode(p) for p in subprocess.check_output(['git','ls-files','--deleted','-z'],cwd=r).split(b'\0') if p];assert len(deleted)==8
 subprocess.run(['git','update-index','--force-remove','--',*deleted],cwd=r,env=env,check=True)
 names=set(subprocess.check_output(['git','ls-files','-co','--exclude-standard','-z'],cwd=r,env=env).split(b'\0'))-{b''}
 expected=set(subprocess.check_output(['git','ls-files','-co','--exclude-standard','-z'],cwd=r).split(b'\0'))-{b''}-set(os.fsencode(p) for p in deleted);assert names==expected
 assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
 rows.append(dict(snapshot=name,external_index=str(index),deleted=deleted,original_index_sha256=original_hash,original_index_unchanged=True,live_inputs=len(names),index_sha256=hashlib.sha256(index.read_bytes()).hexdigest()))
(q/'measurement-index-projection.json').write_text(json.dumps(rows,indent=2)+'\n')
bin=q/'measurement-bin';bin.mkdir(exist_ok=True)
wrapper='''#!/usr/bin/python3
import os,sys
from pathlib import Path
q=Path('/tmp/mr-s9-r4-j37ybea_')
env=dict(os.environ);env.pop('GIT_INDEX_FILE',None)
cwd=Path.cwd()
# Only the measurement's two root-level read operations use the projected index.
# Dependency repositories and every git mutation always see their own original index.
if cwd in [q/'final-boards',q/'final-gate'] and sys.argv[1:2] in [['ls-files'],['status']]:
    env['GIT_INDEX_FILE']=str(q/(cwd.name+'-measurement.index'))
os.execve('/usr/bin/git',['git',*sys.argv[1:]],env)
'''
(bin/'git').write_text(wrapper);(bin/'git').chmod(0o755)
