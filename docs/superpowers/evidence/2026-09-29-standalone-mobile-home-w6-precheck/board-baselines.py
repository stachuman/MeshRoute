from pathlib import Path
import subprocess,json,time,os
R=Path('/home/staszek/MeshRoute');A=R/'artifacts/2026-09-29-standalone-mobile-home-w6-precheck';O=R/'.pio-measure/w6-precheck';results=[]
def run(name,args):
 t=time.monotonic()
 with (A/(name+'.log')).open('wb') as f:p=subprocess.run(args,cwd=R,stdout=f,stderr=subprocess.STDOUT,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'})
 results.append(dict(name=name,args=args,exit=p.returncode,seconds=round(time.monotonic()-t,3)))
 (A/'board-runs.json').write_text(json.dumps(results,indent=2)+'\n'); print(name,p.returncode,results[-1]['seconds'],flush=True)
 if p.returncode:raise SystemExit(p.returncode)
for n in (1,2):run('board-base-'+str(n),['python3','tools/measure_board.py','pair','--output',str(O/('base-'+str(n))),'--jobs=1'])
for env in ('gateway','heltec_mobile'):run('board-compare-'+env,['python3','tools/measure_board.py','compare',str(O/'base-1'/env/'manifest.json'),str(O/'base-2'/env/'manifest.json')])
