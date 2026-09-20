from pathlib import Path
import json,re,hashlib,subprocess,tarfile
q=Path(__file__).parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
# Every final result must be from this run, at the frozen candidate inputs.
expected_chain={'native-build','native-binary','inbox-client','abi','abi-b278','reference','reference-slice9','inventory-write','inventory-bare','inventory-check','authority','authority-controls','a0','literals','whitespace'}
for p in ['inbox_verbs','firmware_ui','console_sink','custody_usb','ble_line','features','deferred_actions']:expected_chain.update([p,p+'-no-neg'])
for mode,names in [('chain',expected_chain),('sim',{'sim-configure','sim-build','corpus','corpus-validate'}),('boards',{'pair','xiao-mobile','census'}),('tools',{'tools'})]:
 results=json.loads((q/('final-'+mode+'-results.json')).read_text());assert {x['name'] for x in results}==names,(mode,names-{x['name'] for x in results})
 for x in results:
  assert x['exit']==0,x
  assert sha(q/'final-logs'/(x['name']+'.log'))==x['sha256'],x['name']
row=json.loads((q/'final-logs/simulator-whitespace-result.json').read_text());assert row['exit']==0 and sha(q/'final-logs/simulator-whitespace.log')==row['sha256']
s=(q/'final-logs/native-binary.log').read_text();assert re.search(r'test cases:\s+2950\s+\|\s+2950 passed \| 0 failed \| 0 skipped',s);assert re.search(r'assertions:\s+195770\s+\|\s+195770 passed \| 0 failed',s)
s=(q/'final-logs/tools.log').read_text();assert re.search(r'^Ran 349 tests ',s,re.M) and re.search(r'^OK$',s,re.M);assert ' ... skipped' not in s
s=(q/'final-logs/census.log').read_text();assert s.endswith('PASS — 6 OLED env(s) match their pinned warning baseline\n')
selectors=json.loads((q/'selectors.json').read_text());results=json.loads((q/'union/results.json').read_text());assert len(results)==61;assert {x['target'] for x in results}==set(selectors['union']);rows=[]
for x in sorted(results,key=lambda x:x['target']):
 p=q/'union'/(x['target']+'.log');s=p.read_text();assert sha(p)==x['sha256'];m=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',s,re.M);assert len(m)==1,(x['target'],m)
 red,bad=map(int,m[0]);n=next(z['configured'] for z in selectors['rows'] if z['target']==x['target']);assert red+bad==n
 assert x['exit']==(1 if x['target']=='sliceBmac' else 0)
 fails=re.findall(r'^  FAIL (.+)$',s,re.M)
 if x['target']=='sliceBmac':assert bad==1 and len(fails)==1 and fails[0].startswith('M04 ')
 else:assert bad==0 and not fails
 bs=re.findall(r'clean baseline\s+(\d+) / (\d+) / (\d+)',s);assert bs and all(z==('2950','195770','0') for z in bs)
 rows.append(dict(target=x['target'],red=red,unusable=bad,configured=n,exit=x['exit']))
assert sum(x['red'] for x in rows)==983 and sum(x['unusable'] for x in rows)==1
preserved=json.loads((q/'final-gate-inputs.json').read_text());root=Path('/home/staszek/MeshRoute')
for d in [root,q/'final-gate',q/'final-boards',q/'union-inputs']:
 for name,v in preserved.items():
  if 'sha256'in v:
   expected=v['sha256']
   generated=json.loads((q/'inventory-generation-delta.json').read_text())
   if name==generated['path'] and d in [root,q/'final-gate']:expected=generated['after_sha256']
   assert sha(d/name)==expected,(d,name)
assert not subprocess.check_output(['git','diff','--cached','--name-status'],cwd=root)
sim=Path('/home/staszek/lora-universal-simulator');assert not subprocess.check_output(['git','status','--porcelain'],cwd=sim)
summary=dict(disposition='coder freeze ready for independent QA',all_required_gate_commands=38,results_inherited=0,native=dict(cases=2950,assertions=195770,failed=0,skipped=0),tools=dict(run=349,failed=0,skipped=0),mutation_batteries=61,mutation_configured=984,mutation_red=983,mutation_unusable=1,known_unusable='B342 / sliceBmac M04',vacuous=0,mutation_rows=rows,full_input_records=len(preserved),independent_QA='pending',Part_57f='metal NOT RUN')
(q/'gate-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('PASS: fresh 38-command gate; 61 batteries / 983 RED / known B342 / zero vacuous; complete frozen inputs unchanged.')
