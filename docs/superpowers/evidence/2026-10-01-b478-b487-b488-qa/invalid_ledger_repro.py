#!/usr/bin/env python3
"""QA-only synthetic malformed-ledger cases, stock CLI, no build and no source edits."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys,tempfile
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--ledger',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
s=a.ledger.read_text(encoding='ascii'); profile=next(x for x in s.splitlines() if x.startswith('MR0C-PROFILE '))
cases={'profile-bare-token':s.replace(profile,profile+' malformed'),'count-5000-digits':re.sub(r'lines=\d+','lines='+'9'*5000,s,count=1),'length-5000-digits':re.sub(r'(LINE L001 len=)\d+',lambda m:m[1]+'9'*5000,s,count=1)}
rows=[]
with tempfile.TemporaryDirectory(prefix='mr-qa-invalid-ledger-') as td:
 for name,text in cases.items():
  assert text!=s
  file=Path(td)/(name+'.txt');file.write_text(text,encoding='ascii')
  r=subprocess.run([sys.executable,'-B',str(a.root/'tools/probe_inbox_verbs/transcript.py'),'--compare',str(file),str(file)],capture_output=True)
  rows.append({'case':name,'ledger_sha256':hashlib.sha256(text.encode()).hexdigest(),'exit':r.returncode,'stdout':r.stdout.decode('utf8','backslashreplace'),'stderr':r.stderr.decode('utf8','backslashreplace'),'contract_exit':2,'contract_met':r.returncode==2 and b'Traceback' not in r.stderr})
a.output.write_text(json.dumps(rows,indent=2)+'\n');print([(r['case'],r['exit'],r['contract_met']) for r in rows])
