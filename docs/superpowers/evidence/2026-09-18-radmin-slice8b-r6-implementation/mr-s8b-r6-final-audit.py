from pathlib import Path
import json,re,hashlib,subprocess,os
q=Path('/tmp/mr-s8b-r6-nwy4hz9d');r=Path('/home/staszek/MeshRoute');sim=Path('/home/staszek/lora-universal-simulator');out=q/'final-union'
sel=json.loads((out/'selectors.json').read_text());results=json.loads((out/'results.json').read_text());assert len(results)==len(sel['union'])==61
red=unusable=0;rows=[]
for entry in results:
 t=entry['target'];s=(out/(t+'.log')).read_text();m=re.findall(r'^mutations: (\d+) RED / (\d+) unusable$',s,re.M);assert len(m)==1,(t,m)
 a,b=map(int,m[0]);assert a+b==next(x['configured'] for x in sel['rows'] if x['target']==t)
 assert b==(1 if t=='sliceBmac' else 0),(t,b);assert entry['exit']==(1 if t=='sliceBmac' else 0)
 assert not re.search(r'^\s*(?:\[w\d+\]\s*)?VACUOUS ',s,re.M),t
 if b:assert 'FAIL M04' in s and 'the suite still PASSES' in s
 red+=a;unusable+=b;rows.append(dict(target=t,red=a,unusable=b,exit=entry['exit'],sha256=hashlib.sha256(s.encode()).hexdigest()))
assert red==983 and unusable==1 and sel['configured']==984
repair=[]
for target,label in [('radmin8client','C39'),('radmin8client','C43'),('radmin8brx','X09'),('radmin8brx','X18')]:
 s=(out/(target+'.log')).read_text();m=re.findall(r'^  ok   '+label+r'.*RED \((\d+) assertion\(s\) failed, match count 1\)',s,re.M);assert len(m)==1,(target,label,m)
 repair.append(dict(target=target,label=label,failed_assertions=int(m[0]),matches=1,verdict='RED'))
summary=dict(batteries=61,configured=984,red=red,known_unusable=[dict(target='sliceBmac',label='M04',finding='B342',kind='compiled mutant survives, pre-existing carve-out')],vacuous=0,controls=repair,rows=sorted(rows,key=lambda x:x['target']))
(q/'mutation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
runs=[]
for p in (q/'final-logs').glob('*-result.json'):
 d=json.loads(p.read_text());d['required']=d['name']!='board_ui';assert d['exit']==(0 if d['required'] else 1),d['name'];runs.append(d)
assert any(d['name']=='tools' for d in runs)
for p in ['inputs.json','preparation-inputs.json','state.json']:assert (q/p).exists()
primary=json.loads((q/'frozen-primary-inputs.json').read_text());assert len(primary)==361
for root in [r,q/'final-gate',q/'union-inputs']:
 assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==h for p,h in primary.items()),str(root)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip()=='c07b77f50a16342d532618f760aad49e10208a5d'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim).decode().strip()=='6585649ea5a780f0542b2931853a667be56a5b2b'
assert subprocess.check_output(['git','status','--porcelain'],cwd=sim)==b''
assert subprocess.check_output(['git','diff','--cached','--name-only'],cwd=r)==b''
for root in [r,sim]:subprocess.run(['git','diff','--check'],cwd=root,check=True)
prep=json.loads((q/'preparation-inputs.json').read_text());changed=[p for p,h in prep.items() if hashlib.sha256((r/p).read_bytes()).hexdigest()!=h];assert changed==['docs/2026-07-30-open-bug-register.md']
(q/'required-gate-results.json').write_text(json.dumps(dict(verdict='required gate PASS with the established B342 carve-out; supplemental B418 remains open',instruments=sorted(runs,key=lambda x:x['name']),mutation_summary='mutation-summary.json'),indent=2)+'\n')
(q/'preservation.json').write_text(json.dumps(dict(primary_paths=361,primary_copies_equal=True,simulator_clean=True,staged_paths=[],changed_preparation_paths=changed,reason='coder findings B418/B419 recorded under M1; frozen brief unchanged',brief_sha256=prep['docs/superpowers/plans/2026-09-18-radmin-slice8b-mobile-controller-carrier.md']),indent=2)+'\n')
print('AUDIT PASS: 983 RED, B342 only; fresh C39/C43/X09 and B413 X18 RED; source inventories preserved')
