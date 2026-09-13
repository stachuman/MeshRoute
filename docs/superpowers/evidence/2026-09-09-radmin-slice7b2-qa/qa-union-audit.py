from pathlib import Path
import json,re,hashlib
q=Path(__file__).resolve().parent; p=q/'union'; sel=json.loads((p/'selectors.json').read_text()); runs=json.loads((p/'results.json').read_text()); assert set(runs)==set(sel['union'])
rows=[]; unusable=[]
for target in sel['union']:
 t=(p/(target+'.log')).read_text(); n=sel['counts'][target]
 baseline=re.findall(r'clean baseline (\d+) / (\d+) / (\d+)\s+\(DERIVED from this tree\)',t)
 assert baseline and len(baseline)==min(4,n) and set(baseline)=={('2909','174485','0')},target
 red=re.findall(r'^\s*\[w\d+\].*?\bok\s+(\S+).*?-> RED \((\d+) assertion\(s\) failed, match count (\d+)\)',t,re.M)
 assert all(int(f)>0 and m=='1' for _,f,m in red),target
 summaries=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',t,re.M); assert len(summaries)==1,target
 nr,nu=map(int,summaries[0]); assert len(red)==nr and nr+nu==n,(target,len(red),summaries,n)
 assert nr==n-(1 if target=='sliceBmac' else 0) and nu==(1 if target=='sliceBmac' else 0),target
 restore=re.findall(r'worker (\d+):.*?source restored: md5 ([a-f0-9]+) \(MATCHES\)',t)
 assert len(restore)==len(baseline) and len(set(h for _,h in restore))==1,target
 assert 'real tree untouched: all 56 target files byte-identical to launch (md5)' in t,target
 if nu:
  lines=[l for l in t.splitlines() if 'M04' in l or 'suite still PASSES' in l]; assert lines and 'suite still PASSES' in '\n'.join(lines); unusable+=lines
 assert runs[target]['rc']==(1 if nu else 0)
 rows.append(dict(target=target,entries=n,red=nr,unusable=nu,baselines=len(baseline),baseline=[2909,174485,0],restored_md5=restore[0][1],log_sha256=hashlib.sha256((p/(target+'.log')).read_bytes()).hexdigest()))
old=json.loads((p/'targets-before.json').read_text()); assert old=={f:hashlib.sha256((q/'mutations'/f).read_bytes()).hexdigest() for f in old}
result=dict(targets=rows,total_entries=sum(r['entries'] for r in rows),total_red=sum(r['red'] for r in rows),total_unusable=sum(r['unusable'] for r in rows),total_worker_baselines=sum(r['baselines'] for r in rows),unusable_lines=unusable,restored_sources_sha256=old)
assert result['total_entries']==773 and result['total_red']==772 and result['total_unusable']==1
(q/'union-audit.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))
