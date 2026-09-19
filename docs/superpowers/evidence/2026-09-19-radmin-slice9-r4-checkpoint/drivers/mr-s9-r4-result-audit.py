from pathlib import Path
import json,re,hashlib
q=Path('/tmp/mr-s9-r4-j37ybea_');union=q/'final-union';selectors=json.loads((union/'selectors.json').read_text());results=json.loads((union/'results.json').read_text());assert len(results)==61
assert {x['target'] for x in results}==set(selectors['union'])
rows=[];failure_lines=[]
for x in sorted(results,key=lambda x:x['target']):
 p=union/(x['target']+'.log');s=p.read_text();assert hashlib.sha256(p.read_bytes()).hexdigest()==x['sha256']
 m=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',s,re.M);assert len(m)==1,(x['target'],m);red,bad=map(int,m[0]);n=next(z['configured'] for z in selectors['rows'] if z['target']==x['target']);assert red+bad==n
 assert x['exit']==(1 if x['target']=='sliceBmac' else 0)
 fails=re.findall(r'^  FAIL (.+)$',s,re.M)
 if x['target']=='sliceBmac':assert bad==1 and len(fails)==1 and fails[0].startswith('M04 ')
 else:assert bad==0 and not fails
 baselines=re.findall(r'clean baseline\s+(\d+) / (\d+) / (\d+)',s);assert baselines and all(x==('2950','195768','0') for x in baselines)
 rows.append(dict(target=x['target'],red=red,unusable=bad,configured=n,exit=x['exit']));failure_lines +=fails
assert sum(x['red'] for x in rows)==983 and sum(x['unusable'] for x in rows)==1
for mode in ['chain','sim']:
 rs=json.loads((q/('final-'+mode+'-results.json')).read_text());assert all(x['exit']==0 for x in rs)
rs=json.loads((q/'final-boards-results.json').read_text());assert {x['name']:x['exit'] for x in rs}=={'pair':0,'xiao-mobile':0,'census':1}
rs=json.loads((q/'final-tools-results.json').read_text());assert rs[0]['exit']==1
s=(q/'final-logs/tools.log').read_text();assert re.search(r'^Ran 349 tests ',s,re.M) and 'FAILED (failures=2)' in s
assert ' ... skipped' not in s
fails=re.findall(r'^FAIL: (test_\S+).*$',s,re.M);assert set(fails)=={'test_legacy_surface_is_not_a_second_semantic_class','test_the_two_pins_reconcile_with_what_the_contract_actually_emits'}
focused=(q/'B428-focused/result.log').read_text();assert 'Ran 1 test' in focused and re.search(r'^OK$',focused,re.M)
summary={'disposition':'HOLD B426/B427; checkpoint only','mutation_batteries':61,'mutation_configured':984,'mutation_red':983,'mutation_unusable':1,'known_unusable':'B342 / sliceBmac M04, compiled survivor','vacuous':0,'mutation_rows':rows,'native':{'cases':2950,'assertions':195768,'failed':0,'skipped':0},'tools':{'run':349,'passed':347,'failed':2,'skipped':0,'failures':fails,'post_sweep_B428_focused_test':'PASS; not relabelled as a full-suite pass'},'warning_census':'six compiled cells, zero Wswitch, FAIL B427 old pins','source_validation':'§8 PASS withdrawn; dependencies omitted from fence','known_mutation_output':failure_lines}
(q/'gate-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print('Audited: 61 batteries / 983 RED / 1 known B342 / 984 / zero vacuous; full tools 349 with two disclosed failures, B428 corrected and focused PASS.')
