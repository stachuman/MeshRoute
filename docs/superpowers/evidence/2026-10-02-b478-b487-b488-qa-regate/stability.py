from pathlib import Path
import collections,hashlib,json,os,platform,re,subprocess
O=Path('/home/staszek/MeshRoute/artifacts/2026-10-02-b478-b487-b488-qa-regate');D=json.loads((O/'f07-diagnostic.json').read_text());W=Path(D['scratch'])/'w0';P=W/'.pio/build/native/program';raw=O/'replays';raw.mkdir();rows=[]
assert int(Path('/proc/sys/kernel/randomize_va_space').read_text())>0
for i in range(456):
 tag=('aslr-on' if i<200 else 'aslr-off-offset')+str(i if i<200 else i-200)
 cmd=[str(P)] if i<200 else ['setarch',platform.machine(),'-R',str(P)]
 env=None if i<200 else {'PATH':'/usr/bin:/bin','HOME':os.environ.get('HOME','/tmp'),'MRPAD':'x'*(i-200)}
 p=subprocess.run(cmd,cwd=W,env=env,capture_output=True);d=p.stdout+p.stderr;(raw/(tag+'.bin')).write_bytes(d)
 a=re.findall(rb'assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed',d);c=re.findall(rb'test cases: *(\d+) \| *(\d+) passed \| *(\d+) failed',d)
 sites=sorted(set(s.decode() for s in re.findall(rb'(test/[\w./]+:\d+): ERROR',d)))
 row={'tag':tag,'exit':p.returncode,'failed':int(a[-1][2]) if a else None,'cases':int(c[-1][2]) if c else None,'sites':sites,'sha256':hashlib.sha256(d).hexdigest()};rows.append(row)
 assert p.returncode==1 and row['failed']==13 and row['cases']==5 and len(sites)==13,(i,row)
 assert row['sites']==rows[0]['sites']
 if i%100==99:print('Replayed',i+1,flush=True)
assert 'test/test_firmware_ui_model.cpp:2756' in rows[0]['sites']
result={'verdict':'PASS','binary_sha256':hashlib.sha256(P.read_bytes()).hexdigest(),'scratch':str(W),'runs':456,'aslr_enabled':200,'offset_sweep':256,'failed_assertions':13,'failed_cases':5,'sites':rows[0]['sites'],'per_run':rows}
(O/'stability.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS 456/456, one 13-site set',flush=True)
