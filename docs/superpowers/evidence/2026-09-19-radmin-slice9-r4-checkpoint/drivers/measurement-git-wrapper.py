#!/usr/bin/python3
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
