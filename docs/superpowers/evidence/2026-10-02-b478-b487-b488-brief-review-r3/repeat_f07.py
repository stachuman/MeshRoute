"""QA diagnostic only: replay the already retained mutant; no rebuild/source change."""
from pathlib import Path
import collections,hashlib,json,os,platform,re,subprocess
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-return'
B=Path('/tmp/mr_mutation_run-3ckbcwpp/w0/.pio/build/native/program');rows=[]
for pad in range(0,256,16):
 env={'PATH':'/usr/bin:/bin','HOME':os.environ['HOME'],'MR_QA_PAD':'x'*pad}
 p=subprocess.run(['setarch',platform.machine(),'-R',str(B)],cwd=B.parents[3],env=env,capture_output=True,timeout=60)
 data=p.stdout+p.stderr;(O/f'f07-pad-{pad:03}.log').write_bytes(data)
 summaries=re.findall(rb'assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed',data)
 sites=sorted(set(x.decode() for x in re.findall(rb'(test/[\w./]+:\d+): ERROR',data)))
 rows.append({'pad':pad,'exit':p.returncode,'summary':[[int(x) for x in m] for m in summaries],'sites':sites,'sha256':hashlib.sha256(data).hexdigest()})
 assert p.returncode==1 and len(summaries)==1,(pad,p.returncode,p.stderr[:100])
common=set.intersection(*(set(r['sites']) for r in rows));varying=sorted(set.union(*(set(r['sites']) for r in rows))-common)
result={'diagnostic':'ASLR disabled, 16 fixed padding offsets spaced 16 bytes apart; retained prior QA F07 binary; no rebuild','binary':str(B),'binary_sha256':hashlib.sha256(B.read_bytes()).hexdigest(),'runs':rows,'distribution':dict(collections.Counter(r['summary'][0][2] for r in rows)),'varying_sites':varying,'always_failing_sites':len(common)}
(O/'f07-replay.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='runs'},indent=2))
