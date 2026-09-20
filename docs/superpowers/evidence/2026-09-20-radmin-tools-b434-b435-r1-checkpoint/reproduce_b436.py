#!/usr/bin/env python3
import importlib.util, json, os, pathlib, re, shutil, subprocess
p=pathlib.Path('/tmp/mr-b434-b435-r1-rwlxb7cv')
spec=importlib.util.spec_from_file_location('gate',p/'gate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
reference=gate.RAM/'reference-stage'
assert not reference.exists()
gate.run('reference-rsync',['rsync','-a','--exclude=.pio',str(gate.STAGE)+'/',str(reference)+'/'])
shutil.copyfile(p/'base-harness.py',reference/'tools/probe_ui_model_mutations.py')
env=dict(os.environ,MR_MUT_SCRATCH=str(gate.RAM/'scratch'),MR_MUT_KEEP_SCRATCH='1')
result=gate.run('b436-base-harness',['python3','tools/probe_ui_model_mutations.py','--target=radmin8node','--workers=3'],cwd=reference,env=env)
assert result['rc']==0,result
log=(p/'b436-base-harness.log').read_text();assert 'mutations: 1 RED / 0 unusable' in log
scratch=pathlib.Path(re.search(r'^-- scratch root: (\S+)',log,re.M).group(1))
binary=gate.run('b436-mutant-binary',[str(scratch/'w0/.pio/build/native/program')],cwd=reference)
assert binary['rc']==1,binary
print('Bounded diagnostic: pristine base harness marks the same real mutant RED; direct mutant binary exits 1.',flush=True)
