from pathlib import Path
import json,re,hashlib
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-01-b478-b487-b488-qa';S=Path('/tmp/mr-tool-qa-059bo2lh')
want=dict(zip(['F%02d'%i for i in range(1,10)],[39,169,36,4,4,25,44,6,23])); records={}
for arm in ['mut-default','mut-w1']:
 text=(O/(arm+'.log')).read_text();merge=text.split('-- MERGED REPORT',1)[1]
 hits=re.findall(r'^  ok   (F\d+) .*? -> RED \((\d+) assertion\(s\) failed, match count 1\)$',merge,re.M)
 assert len(hits)==9
 differences={k:{'expected':want[k],'observed':int(v)} for k,v in hits if int(v)!=want[k]}
 bases=re.findall(r'^  \[w(\d+)\]   ok   clean baseline (\d+) / (\d+) / (\d+)',text,re.M)
 assert len(bases)==(8 if arm=='mut-default' else 1);assert all(tuple(x[1:])==('3059','199638','0') for x in bases)
 assert 'mutations: 9 RED / 0 unusable' in merge and 'MISSING' not in merge and 'VACUOUS' not in merge
 records[arm]={'expected_differences':differences,'per_entry':dict(hits),'worker_baselines':bases,'unusable':0,'vacuous':0,'missing':0}
(O/'mutations.json').write_text(json.dumps(records,indent=2)+'\n')
pre=json.loads((R/'docs/superpowers/evidence/2026-09-30-b478-b487-b488-precheck/transcript-results.json').read_text());trans=[]
fw=(R/'src/fw_main.cpp').read_text();lines=fw.splitlines();si=next(i for i,x in enumerate(lines) if "line[pos] = '\\0';" in x);se=next(i for i in range(si+1,len(lines)) if lines[i].strip()=='pos = 0;');bi=next(i for i,x in enumerate(lines) if re.search(r'!strncmp\(line, "del_msg",\s+7\)', x));be=next(i for i in range(bi+1,len(lines)) if lines[i]=='}')
regions={'serial':'\n'.join(lines[si+1:se]),'ble':'\n'.join(lines[bi+1:be])}
runner=(R/'tools/probe_inbox_verbs/run.sh').read_text();boundary='rc=0\nif ! build_support;';assert runner.count(boundary)==1;prefix=runner.split(boundary)[0]
for item in pre:
 profile=item['profile'];pair=[]
 for n in [1,2]:
  d=S/f'{profile}-{n}';text=(d/'ledger.txt').read_text();ls=text.splitlines();rows={}
  assert len(ls)==4+3*item['matrix']
  for i in range(3,len(ls)-1,3):
   m=re.fullmatch(r'LINE (L\d+) len=(\d+) \[(.*)\]',ls[i]);assert m
   rows[m[1]]={'LINE':ls[i],'SER':ls[i+1],'BLE':ls[i+2]}
  assert len(rows)==item['matrix'];assert list(rows)==['L%03d'%i for i in range(1,item['matrix']+1)]
  for e in item['variants']['array_live']['examples']:
   assert rows[e['id']]['SER']==e['SER'];assert rows[e['id']]['BLE']==e['BLE']
  build=(d/'build.sh').read_text();assert build.startswith(prefix);app=build[len(prefix):]
  assert len(app.strip().splitlines())==4
  header=next(d.glob('mr0c-shadow-*/mr0c_adapters.h')).read_text()
  for name,body in regions.items():assert header.count(body)==1,name
  pair.append(rows)
  trans.append({'profile':profile,'run':n,'rows':len(rows),'precheck_example_rows_identical':len(item['variants']['array_live']['examples']),'record_sha256':hashlib.sha256('\n'.join(ls[3:-1]).encode()).hexdigest(),'caller_regions_verbatim':True,'builder_prefix_sha256':hashlib.sha256(prefix.encode()).hexdigest(),'appended_builder_lines':app.strip().splitlines()})
 assert pair[0]==pair[1]
(O/'transcript-reconciliation.json').write_text(json.dumps(trans,indent=2)+'\n');print('mutation identities reconciled (inspect expected_differences);  4 fresh ledgers, 10/10 precheck examples, exact caller regions and builder prefix PASS')
