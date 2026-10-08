from pathlib import Path
import subprocess,os,re,json,hashlib
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate'
cmd=['python3','-B','tools/probe_ui_model_mutations.py','--target=w4aident','--workers=1','F07']
with (O/'f07-retained-run.log').open('wb') as log:r=subprocess.run(cmd,cwd=R,env=dict(os.environ,MR_MUT_KEEP_SCRATCH='1'),stdout=log,stderr=subprocess.STDOUT)
text=(O/'f07-retained-run.log').read_text();root=Path(re.search(r'^-- scratch root: (\S+)',text,re.M)[1]);binary=root/'w0/.pio/build/native/program'
results=[]
for i in range(10):
 p=subprocess.run([str(binary)],cwd=root/'w0',capture_output=True);data=p.stdout+p.stderr;(O/f'f07-native-{i}.log').write_bytes(data)
 verdict=re.findall(rb'assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed',data)
 failures=re.findall(rb'([^\n]*:[0-9]+: (?:ERROR|FATAL ERROR):[^\n]*)',data)
 results.append({'run':i,'exit':p.returncode,'summaries':[[int(x) for x in v] for v in verdict],'failures':[x.decode('utf8','backslashreplace') for x in failures],'sha256':hashlib.sha256(data).hexdigest()})
(O/'f07-diagnostic.json').write_text(json.dumps({'labelled_diagnostic':'Stock F07-only run with scratch retained; repeat the same mutant binary without rebuilding. Source in worker has been restored by the stock runner; executable still contains F07, per the documented harness architecture.','command':cmd,'stock_exit':r.returncode,'scratch':str(root),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'runs':results},indent=2)+'\n')
print('stock',r.returncode,'scratch',root,'repeats',[(v['exit'],v['summaries']) for v in results],flush=True)
