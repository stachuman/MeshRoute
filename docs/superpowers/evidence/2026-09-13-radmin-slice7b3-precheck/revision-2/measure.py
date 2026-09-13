from pathlib import Path
import subprocess,json,hashlib,importlib.util
q=Path(__file__).resolve().parent;r=Path('/home/staszek/MeshRoute');old=Path('/tmp/mr-qa-s7b3-author-8srh2_5f');snap=old/'snapshot'
spec=importlib.util.spec_from_file_location('abi',snap/'tools/probe_board_abi.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
oldruns=json.loads((r/'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-precheck/candidate-measurements.json').read_text())
results={}
for target in ('native','gateway','heltec_mobile'):
 cmd=[str(q/'one-row-candidate.cpp') if a==str(old/'candidate-layout.cpp') else str(q/(target+'.o')) if a==str(old/('candidate-'+target+'.o')) else a for a in oldruns[target]['command']]
 p=subprocess.run(cmd,cwd=snap,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/(target+'-compile.log')).write_text(p.stdout);assert p.returncode==0,(target,p.stdout)
 sizes=m.read_sizes(m.binutil(cmd[0],'nm'),q/(target+'.o'))
 assert sizes['mr_abi_size__meshroute_CandidateRemoteSessionState']==9104
 assert sizes['mr_abi_size__meshroute_CandidateDeferredAction']==248
 results[target]={'cmd':cmd,'rc':p.returncode,'sizes':sizes,'object_sha256':hashlib.sha256((q/(target+'.o')).read_bytes()).hexdigest()}
 print(target, 'one-row candidate',sizes['mr_abi_size__meshroute_CandidateRemoteSessionState'],'JoinRequest',sizes['mr_abi_size__mrfw_JoinRequest'],sizes['mr_abi_align__mrfw_JoinRequest'])
(q/'measurements.json').write_text(json.dumps(results,indent=2)+'\n')
