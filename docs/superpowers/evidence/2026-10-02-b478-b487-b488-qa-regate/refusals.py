from pathlib import Path
import json,re,subprocess,tempfile
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';S=Path((O/'transcript-root.txt').read_text());good=S/'full_headless-1/ledger.txt';s=good.read_text(encoding='ascii');line=next(x for x in s.splitlines() if x.startswith('MR0C-PROFILE '));rows=[]
cases={'profile-bare-token':s.replace(line,line+' malformed'),'identity-count-5000-digits':re.sub(r'lines=\d+','lines='+'9'*5000,s,count=1),'LINE-length-5000-digits':re.sub(r'(LINE L001 len=)\d+',lambda m:m[1]+'9'*5000,s,count=1)}
with tempfile.TemporaryDirectory(prefix='mr-qa-malformed-') as td:
 for name,text in cases.items():
  bad=Path(td)/(name+'.txt');bad.write_text(text,encoding='ascii')
  for side,pair in [('A',(bad,good)),('B',(good,bad))]:
   p=subprocess.run(['python3','-B',str(R/'tools/probe_inbox_verbs/transcript.py'),'--compare',*map(str,pair)],cwd=R,capture_output=True);assert p.returncode==2 and b'Traceback' not in p.stderr and ('COMPARE REFUSED: '+side+' (').encode() in p.stderr,(name,side,p)
   rows.append({'case':name,'bad_side':side,'exit':p.returncode,'no_traceback':True,'refusal':p.stderr.decode().strip()})
(O/'B491-refusals.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS six stock-CLI named refusals with exit2 on fresh real ledger')
