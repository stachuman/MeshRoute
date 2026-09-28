"""QA reconstruction: prior independent pre-check declarations vs fresh execution trace; no candidate census imports."""
from pathlib import Path
import json,re,shlex,collections
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-qa'
def ns(k):
 p=k.split('/');return 'wiring/control' if '/control/' in k else '/'.join(p[:2]) if p[0] in ('canvas','negctl') else p[0]
# Expected canvas labels come from QA's pre-implementation actual-arm preprocessor census.
prior=json.loads((E.parent/'2026-09-28-b459-brief-review/identity-census.json').read_text()); expected=[]
for arm in ['v3','v4']:expected.extend((i,'pass') for i in prior[arm]['ids'])
rows=json.loads((E.parent/'2026-09-28-b459-precheck/board-reconciliation.json').read_text())['rows']
# Loop identities are independently checked against the runner text; patterns themselves remain unchanged.
s=(R/'tools/probe_board_ui/run.sh').read_text()
def loop(v):
 t=re.search(r'^for '+v+r' in (.*?); do',s,re.M|re.S).group(1).replace('\\\n',' ');return shlex.split(t)
for r in rows:
 k=r['kind'];ident=r['id']
 if k=='wiring':expected.append(('wiring/'+ident,'pass'));expected.extend((f'wiring/{ident}/control/{i}','red') for i in range(1,r['declared_controls']+1))
 elif k=='trait':expected.append(('trait/'+ident,'red'))
 elif k=='missing':expected.extend(('missing/'+v,'named_compile_error') for v in loop('name'))
 elif k=='structural':
  if r['declared']==1:expected.append(('struct/'+ident,'pass'))
  else:
   vals=loop({'S2':'h','S3':'s3','S6':'nm'}[ident]);assert len(vals)==r['declared'];expected.extend((f'struct/{ident}/'+v.split('|')[0],'pass') for v in vals)
 elif k.startswith('negctl-'):expected.append(('negctl/'+k[-2:]+'/'+ident,'red'))
assert len(expected)==592 and len(dict(expected))==592
manifest=[tuple(l.split('\t')[:2]) for l in (R/'tools/probe_board_ui/expected.tsv').read_text().splitlines() if l and not l.startswith('#')]
assert sorted(expected)==sorted(manifest)
trace=(RAW/'step4.trace').read_text().splitlines();log=(RAW/'step4-board.log').read_text().splitlines();observed=[];execution=collections.defaultdict(list);pred=[];wid=None;ordinal=0
for l in trace:
 m=re.match(r'^\+(\w+)@(\d+)\|(.*)',l)
 if not m:continue
 fun,_,cmd=m.groups()
 if fun=='pa_record' and cmd.startswith('printf '):
  words=shlex.split(cmd);observed.append(tuple(words[2:]))
 if fun=='wchk_in':
  if cmd.startswith('local file='):
   words=shlex.split(cmd[6:]);kv=dict(w.split('=',1) for w in words);wid='wiring/'+kv['label'].split()[0];ordinal=0;execution['wiring'].append(wid)
  elif cmd.startswith('sed '):ordinal+=1;execution['wiring/control'].append(f'{wid}/control/{ordinal}')
  elif re.fullmatch(r'(?:st|pst)=\d+',cmd):pred.append((wid,'control' if cmd.startswith('pst') else 'live',int(cmd.split('=')[1])))
 if fun=='main':
  for call,space in [('trait_control ','trait'),('missing_trait_control ','missing'),('schk ','struct')]:
   if cmd.startswith(call):execution[space].append(space+'/'+shlex.split(cmd)[1].split()[0])
# Printed negctl successes independent of observation recorder.
arm=None
for n,l in enumerate(log):
 if l=='== negative controls (each MUST fail) ==':arm='v3'
 elif l=='== V4 fixed-ADC source controls (each MUST fail) ==':arm='v4'
 elif arm and re.match(r'^C\d+[a-z0-9]* ',l):
  assert log[n+1].lstrip().startswith('->'),log[n+1];execution['negctl/'+arm].append('negctl/'+arm+'/'+l.split()[0])
assert len(observed)==592 and len({o[0] for o in observed})==592
assert sorted(o[:2] for o in observed)==sorted(expected)
for space,ids in execution.items():assert sorted(ids)==sorted(o[0] for o in observed if ns(o[0])==space),(space,len(ids))
for arm,count in [('v3',124),('v4',110)]:
 assert f'Heltec {arm.upper()} board_ui probe: {count} passed / 0 failed / {count} total' in log
 assert sum(ns(o[0])=='canvas/'+arm for o in observed)==count
live=[r[2] for r in pred if r[1]=='live'];mut=[r[2] for r in pred if r[1]=='control'];assert live==[0]*60 and mut==[1]*186
result={'expected_basis':'QA pre-check and pre-implementation actual-arm canvas census, source loop names; not coder census','identities':expected,'counts':dict(collections.Counter(ns(i) for i,o in expected)),'expected_equals_manifest_equals_observed':True,'terminal_observations':len(observed),'independent_execution_counts':{k:len(v) for k,v in execution.items()},'actual_live_exit_counts':dict(collections.Counter(live)),'actual_mutant_exit_counts':dict(collections.Counter(mut)),'negctl_kinds':dict(collections.Counter('/'.join([ns(o[0]),o[2]]) for o in observed if o[0].startswith('negctl/'))),'verdict':'PASS'}
(E/'reconciliation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: 592 unique identities; manifest=pre-change=observed; execution corroborates records; 60 actual live exits 0 and 186 actual mutant exits 1.')
