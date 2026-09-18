from pathlib import Path
import json,re,hashlib
q=Path('/tmp/mr-s8ac-r4-b1edsao2');l=q/'final-logs';chain=json.loads((q/'final-chain-results.json').read_text());retry=json.loads((l/'tools-repin-result.json').read_text());assert retry['exit']==0
final=[r for r in chain if r['name']!='tools']+[dict(retry,name='tools')];assert len(final)==26,len(final);assert all(r['exit']==0 for r in final)
tools=(l/'tools-repin.log').read_text();n=int(re.search(r'Ran (\d+) tests in',tools)[1]);assert re.search(r'^OK$',tools,re.M) and 'skipped=' not in tools
union=q/'final-union';audit=json.loads((union/'selectors.json').read_text());results=json.loads((union/'results.json').read_text());assert set(r['target'] for r in results)==set(audit['union']);assert all(r['exit']==(1 if r['target']=='sliceBmac' else 0) for r in results)
known=(union/'sliceBmac.log').read_text();assert 'FAIL M04' in known and 'the suite still PASSES; nothing measures this' in known
rows=[]
for r in results:
 s=(union/(r['target']+'.log')).read_text();m=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',s,re.M);assert len(m)==1,(r['target'],m);red,unusable=map(int,m[0]);assert unusable==(1 if r['target']=='sliceBmac' else 0)
 rows.append(dict(target=r['target'],red=red,unusable=unusable,configured=red+unusable))
red=sum(r['red'] for r in rows);unusable=sum(r['unusable'] for r in rows);assert len(rows)==59 and red==917 and unusable==1 and red+unusable==audit['configured']
corpus=json.loads((q/'corpus-final2/manifest.json').read_text());assert len(corpus['scenarios'])==36 and all(r['anchor_match'] and r['assertion_failures']==0 for r in corpus['scenarios'])
s18=next(r for r in corpus['scenarios'] if r['name']=='s18_meshroute')
native=(q/'logs/final-native-binary.log').read_text();assert '2947 |   2947 passed | 0 failed | 0 skipped' in native and '193734 | 193734 passed | 0 failed' in native
base_native=(q/'logs/base-native-binary.log').read_text();assert '2931 |   2931 passed | 0 failed | 0 skipped' in base_native and '189998 | 189998 passed | 0 failed' in base_native
assert json.loads((q/'corpus-byte-comparison.json').read_text())['byte_identical']==36
assert 'Strict comparison: 94/94 literals match' in (l/'extended-reference.log').read_text()
assert json.loads((q/'reference-corruption-control.json').read_text())['exit']==1
summary=dict(base_native_cases=2931,base_native_assertions=189998,actual_base_final_streams_identical=36,extended_reference_arrays=94,native_cases=2947,native_assertions=193734,native_failed=0,native_skipped=0,tools_tests=n,tools_skipped=0,corpus_anchors=36,s18_md5=s18['output_md5'],mutation_batteries=len(rows),mutation_red=red,mutation_unusable=unusable,mutation_configured=audit['configured'],known_exception='B342: sliceBmac M04 survives; battery exits 1; not counted RED',mutation_rows=rows,final_instruments=final)
(q/'final-gate-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k not in ['mutation_rows','final_instruments']},indent=2))
