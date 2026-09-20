from pathlib import Path
import subprocess,json,os,time,difflib,hashlib
q=Path(Path('/tmp/mr-s10-r2-active').read_text().strip());r=q/'proposal';out=q/'proposed-repairs';out.mkdir(exist_ok=True)
env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator',PLATFORMIO_BUILD_DIR=str(q/'proposal-native'))
rows=[]
def run(name,cmd):
 t=time.monotonic()
 with (out/(name+'.log')).open('wb') as f:rc=subprocess.run(cmd,cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 rows.append(dict(name=name,command=cmd,exit=rc,seconds=time.monotonic()-t));(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n');print(name,rc,flush=True);return rc
run('candidate-native-build',['pio','test','-e','native'])
assert run('candidate-native-binary',[str(q/'proposal-native/native/program')])==1
assert run('candidate-ownership',['python3','tools/probe_features/ownership.py'])==1
changes={}
n='test/test_custody_receive_g.cpp';p=r/n;old=p.read_text();needle='CHECK(sizeof(Node) == 235248); // R-RA-40: +80 ACCEPT-only deferred state';assert old.count(needle)==1
new=old.replace(needle,'CHECK(sizeof(Node) == 235208); // Slice 10 R-RA-6: -40 inert admin mirrors; custody action unchanged');p.write_text(new);changes[n]=(old,new)
n='tools/probe_features/ownership.py';p=r/n;old=p.read_text();start=old.index(" 'lib/core/node.h': [");end=old.index(" 'lib/core/node_mac_rx.cpp':",start)
new=old[:start]+''' # Slice 10 removes only the legacy admin accessor and mirror guards: ACCEPT-only 5 -> 3.
 'lib/core/node.h': ['#if MR_FEAT_RADMIN_CLIENT',
                     '#if MR_FEAT_RADMIN_CLIENT',
                     '#if MR_FEAT_RADMIN_ACCEPT',
                     '#if MR_FEAT_RADMIN_ACCEPT',
                     '#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_RADMIN_CLIENT',
                     '#if MR_FEAT_RADMIN_CLIENT',
                     '#if MR_FEAT_RADMIN_CLIENT',
                     '#if MR_FEAT_RADMIN_ACCEPT'],
'''+old[end:];p.write_text(new);changes[n]=(old,new)
patch=''.join(''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='a/'+n,tofile='b/'+n)) for n,(a,b) in changes.items());(out/'proposal.patch').write_text(patch)
assert run('proposal-native-build',['pio','test','-e','native'])==0
assert run('proposal-native-binary',[str(q/'proposal-native/native/program')])==0
assert run('proposal-features',['bash','tools/probe_features/run.sh'])==0
print('PRIVATE PROPOSAL VERIFIED; no shared unfenced file edited',flush=True)
