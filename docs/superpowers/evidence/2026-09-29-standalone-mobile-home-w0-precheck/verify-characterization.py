from pathlib import Path
import json
p=Path(__file__).resolve().parent
r=json.loads((p/'identity-results.json').read_text())
assert len(r['cases'])==32
for d in r['cases']:
 assert d['routed']==1 and d['writes']==1
 if d['fault'] in ['absent','oversize','unreadable']:
  assert d['saved_name_len']==(8 if d['command'].startswith('cfg set name') else 0)
  assert d['saved_lat']==(532500000 if d['command'].startswith('cfg set lat') else 0)
  assert d['saved_lon']==(225000000 if d['command'].startswith('cfg set lon') else 0)
 if d['fault']=='write_failed':
  assert d['saved_name_len']==9 and d['saved_lat']==521234567 and d['saved_lon']==211234567
  assert 'nv_save_failed' in d['output']
  if 'set lat' in d['command']: assert d['live_lat']==532500000
  if 'set lon' in d['command']: assert d['live_lon']==225000000
s=(p/'identity-output.txt').read_text()
assert '3031323334c5 out=> cfg ok name' in s
assert 'USB text=> cfg err nv_save_failed' in s
assert '\"name\":\"BAD NAME!\"' in s
assert s.count('load=1 n=10 text=ABCDEFGHIJ')==2
print('PASS: recorded before-state characterization, 32 fault cases plus focused measurements')
