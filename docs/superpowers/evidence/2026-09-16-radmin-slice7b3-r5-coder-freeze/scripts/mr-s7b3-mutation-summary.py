from pathlib import Path
import json,re,hashlib
q=Path('/tmp/mr-codex-s7b3-0gt630zl');out=q/'union2';audit=json.loads((out/'selectors.json').read_text());results=json.loads((out/'results.json').read_text());assert len(results)==len(audit['union'])==56,(len(results),len(audit['union']))
counts={r['target']:r['configured'] for r in audit['rows']};rows=[];failures=[]
for r in results:
 p=out/(r['target']+'.log');s=p.read_text();m=list(re.finditer(r'^mutations: (\d+) RED / (\d+) unusable$',s,re.M))[-1];red,unusable=map(int,m.groups());merged=s.split('-- MERGED REPORT — target ',1)[1];nworker=min(3,counts[r['target']])
 baselines=re.findall(r'clean baseline (\d+) / (\d+) / (\d+).*?\((?:DERIVED per worker tree; )?(\d+) tree',merged);assert baselines==[('2931','189998','0',str(nworker))],(r['target'],baselines)
 rr=re.findall(r'-> RED \((\d+) assertion\(s\) failed, match count (\d+)\)',merged);assert len(rr)==red and all(int(a)>0 and n=='1' for a,n in rr),(r['target'],red,len(rr))
 restored=re.findall(r'worker (\d+): .*?source restored: md5 ([0-9a-f]+) \(MATCHES\)',merged);assert len(restored)==nworker,(r['target'],restored)
 bad=re.findall(r'^  FAIL (.+)$',merged,re.M);failures += [(r['target'],x) for x in bad];assert len(bad)==unusable
 assert 'real tree untouched: all 58 target files byte-identical' in s
 rows.append(dict(target=r['target'],red=red,unusable=unusable,workers=nworker,baseline='2931/189998/0',restored=restored,sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
assert len(failures)==1 and failures[0][0]=='sliceBmac' and failures[0][1].startswith('M04')
summary=dict(batteries=len(rows),configured=audit['configured'],red=sum(x['red'] for x in rows),unusable=sum(x['unusable'] for x in rows),worker_baselines=sum(x['workers'] for x in rows),vacuous=0,exceptions=failures,rows=rows);assert summary['red']==879 and summary['unusable']==1
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print({k:v for k,v in summary.items() if k not in ['rows','exceptions']})
