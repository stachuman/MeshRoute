#!/usr/bin/env python3
import argparse, json, pathlib, re
OUT=pathlib.Path('/tmp/mr-b434-b435-r2-6kvsxgb3')
parser=argparse.ArgumentParser();parser.add_argument('--final',action='store_true');args=parser.parse_args()
inventory=json.loads((OUT/'union-inventory.json').read_text());rows=[];pending=[];total_baselines=0
for item in inventory:
    target=item['target'];meta=OUT/f'mut_{target}.json'
    if not meta.exists():pending.append(target);continue
    result=json.loads(meta.read_text());text=(OUT/f'mut_{target}.log').read_text()
    expected_bad=int(target=='sliceBmac');assert result['rc']==expected_bad,(target,result)
    assert result['argv'][-1]=='--workers=3'
    assert '-- selection: IN FULL (all '+str(item['entries'])+' entries)' in text,target
    assert text.count('-- MERGED REPORT — target ')==1,target
    merged=text.split('-- MERGED REPORT — target ',1)[1]
    summary=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',merged,re.M);assert len(summary)==1,target
    red,bad=map(int,summary[0]);assert (red,bad)==(item['entries']-expected_bad,expected_bad),(target,summary)
    nonred=[]
    for label in item['labels']:
        if target=='sliceBmac' and label.split()[0]=='M04':
            expected='  FAIL '+label+' — the suite still PASSES; nothing measures this';nonred.append(label)
            assert merged.splitlines().count(expected)==1,(target,label)
        else:
            matches=[line for line in merged.splitlines() if line.startswith('  ok   '+label+' -> RED (')]
            assert len(matches)==1,(target,label,matches)
            failures=re.fullmatch(r'\d+ assertion\(s\) failed, match count 1\)',matches[0].split(' -> RED (',1)[1]);assert failures,(target,label)
    assert not re.search(r'^  (UNUSABLE|VACUOUS) ',merged,re.M),target
    assert 're-pin PIN_CASES' not in text,target
    baselines=re.findall(r'\[w\d+\]   ok   clean baseline (\d+) / (\d+) / (\d+)',text)
    assert len(baselines)==min(3,item['entries']),(target,baselines)
    assert set(baselines)=={('2950','195770','0')},(target,baselines)
    total_baselines+=len(baselines)
    restores=re.findall(r'^  worker \d+: .* source restored: md5 [a-f0-9]+ \(MATCHES\)',merged,re.M)
    assert len(restores)==len(baselines),(target,restores)
    assert re.search(r'^real tree untouched: all \d+ target files byte-identical to launch \(md5\); no build ran in ',merged,re.M),target
    rows.append({'target':target,'configured':item['entries'],'RED':red,'known_unusable':bad,'known_labels':nonred,'vacuous':0,'baseline_workers':len(baselines),'rc':result['rc'],'elapsed_seconds':result['elapsed_seconds'],'banner_lines':0})
summary={'batteries_completed':len(rows),'configured_completed':sum(r['configured'] for r in rows),'RED':sum(r['RED'] for r in rows),'known_unusable':sum(r['known_unusable'] for r in rows),'vacuous':0,'banner_lines':0,'worker_baselines_verified':total_baselines,'baseline':[2950,195770,0],'pending':pending}
print(json.dumps({k:v for k,v in summary.items() if k != "pending"} | {"pending_count":len(pending)},indent=2))
if args.final:
    assert not pending and (len(rows),summary['RED'],summary['known_unusable'],summary['configured_completed'])==(61,983,1,984),summary
    assert (OUT/'stage-before.json').read_bytes()==(OUT/'stage-after.json').read_bytes()
    (OUT/'union-audit.json').write_text(json.dumps({'summary':summary,'batteries':rows,'stage_before_after':'byte-identical SHA-256 inventories'},indent=2,sort_keys=True)+'\n')
