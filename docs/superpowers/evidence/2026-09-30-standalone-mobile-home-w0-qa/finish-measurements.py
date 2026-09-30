from pathlib import Path
from collections import Counter
import json,re,hashlib,subprocess
root=Path('/home/staszek/MeshRoute'); raw=root/'artifacts/2026-09-30-standalone-mobile-home-w0-qa'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
# Independent reconciliation of declared mutations with actual terminal rows.
s=json.loads((raw/'source-review.json').read_text()); log=(raw/'mutations-devicenv.log').read_text()
rows=re.findall(r'^  \[w\d+\]   ok   (.+?) -> RED \((\d+) assertion\(s\) failed, match count (\d+)\)$',log,re.M)
expected=[x['label'] for x in s['match_counts']]
assert Counter(x[0] for x in rows)==Counter(expected),(len(rows),len(expected))
assert all(int(x[1])>0 and x[2]=='1' for x in rows)
baselines=re.findall(r'^  \[w\d+\]   ok   clean baseline (\d+) / (\d+) / (\d+)',log,re.M)
assert len(baselines)==8 and set(baselines)=={('3059','199638','0')}
mutation={'configured':len(expected),'red':len(rows),'workers':len(baselines),'worker_baseline':[3059,199638,0],'unusable':0,'vacuous':0,'identities_exact':True,'rows':[{'label':a,'failed_assertions':int(b),'matches':int(c)} for a,b,c in rows]}
(raw/'mutations-reconciliation.json').write_text(json.dumps(mutation,indent=2)+'\n')
# Each symbol identity includes section/type/binding; duplicates remain counted.
def symbols(p):return Counter(tuple(x.split('\t')) for x in p.read_text().splitlines())
def writable(p):
 out=set()
 for line in p.read_text().splitlines():
  m=re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+\S+\s+(\S+)',line)
  if m and 'W' in m[2] and 'A' in m[2]:out.add(m[1])
 return out
result={}
for env in ['gateway','heltec_mobile']:
 base=root/'.pio-measure/w0/base-1'/env; coder=root/'.pio-measure/w0/final-1'/env; qa=root/'.pio-measure/w0-qa/2026-09-30'/env
 b=json.loads((base/'manifest.json').read_text()); c=json.loads((coder/'manifest.json').read_text()); q=json.loads((qa/'manifest.json').read_text())
 equal=[];different={}
 for k in c:
  if c[k]==q[k]:equal.append(k)
  else:different[k]={'coder':c[k],'qa':q[k]}
 assert set(different)<= {'source','normal_pio_metadata'},list(different)
 for obj in q['artifacts'].values():
  p=qa/obj['filename']; assert sha(p)==obj['sha256'] and p.stat().st_size==obj['bytes']
 assert (qa/'symbols.txt').read_bytes()==(coder/'symbols.txt').read_bytes()
 assert (qa/'sections.txt').read_bytes()==(coder/'sections.txt').read_bytes()
 bs,qs=symbols(base/'symbols.txt'),symbols(qa/'symbols.txt')
 bw,qw=writable(base/'sections.txt'),writable(qa/'sections.txt')
 assert bw==qw
 rb=Counter({k:v for k,v in bs.items() if len(k)==6 and k[1]=='OBJECT' and k[4] in bw})
 rq=Counter({k:v for k,v in qs.items() if len(k)==6 and k[1]=='OBJECT' and k[4] in qw})
 assert rb==rq,(env,'RAM OBJECT changed')
 # Aggregate function sizes using full (untruncated) symbol names.
 def funcs(s):
  d=Counter()
  for k,n in s.items():
   if len(k)==6 and k[1]=='FUNC':d[k[0]]+=int(k[5])*n
  return d
 bf,qf=funcs(bs),funcs(qs)
 delta=[{'symbol':k,'base':bf[k],'qa':qf[k],'delta':qf[k]-bf[k]} for k in sorted(bf.keys()|qf.keys()) if bf[k]!=qf[k]]
 result[env]={'final_equal_top_level_fields':equal,'expected_metadata_differences':different,'measurements_base':b['measurements'],'measurements_qa':q['measurements'],'ram_delta':q['measurements']['ram_bytes']-b['measurements']['ram_bytes'],'flash_delta':q['measurements']['flash_bytes']-b['measurements']['flash_bytes'],'artifacts_qa':q['artifacts'],'sections_text_identical_to_coder':True,'symbols_text_identical_to_coder':True,'ram_object_multiset_unchanged':True,'writable_sections':sorted(qw),'ram_objects':sum(rq.values()),'function_deltas':delta,'symbol_delta_total':sum(x['delta'] for x in delta)}
(raw/'boards-compare.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'mutations':len(rows),'boards':{k:{x:v[x] for x in ['ram_delta','flash_delta','ram_object_multiset_unchanged','symbol_delta_total']} for k,v in result.items()}},indent=2))
