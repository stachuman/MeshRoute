import subprocess,os,signal,json,time
from pathlib import Path
rows=[]
for line in subprocess.check_output(['ps','-eo','pid=,ppid=,args='],text=True).splitlines():
 p,pp,args=line.strip().split(None,2);rows.append((int(p),int(pp),args))
roots=[p for p,pp,args in rows if args in ['python3 /tmp/mr-s8b-full-gate.py chain','python3 /tmp/mr-s8b-full-gate.py boards','python3 /tmp/mr-s8b-union.py']]
children={}
for p,pp,args in rows:children.setdefault(pp,[]).append(p)
def descendants(p):
 out=[]
 for child in children.get(p,[]):out+=descendants(child)+[child]
 return out
stopped=[]
# Stop each driver first to prevent it scheduling a replacement, then its captured children.
for p in roots:
 ids=descendants(p)
 for target in [p]+ids:
  try:os.kill(target,signal.SIGTERM);stopped.append(target)
  except ProcessLookupError:pass
out=dict(reason='STOP-1 B413 reproduced; full gate incomplete, no implementation freeze',roots=roots,signalled=stopped)
Path('/tmp/mr-s8b-r4-_d7gpj1e/stopped-gates.json').write_text(json.dumps(out,indent=2)+'\n');print(out)
