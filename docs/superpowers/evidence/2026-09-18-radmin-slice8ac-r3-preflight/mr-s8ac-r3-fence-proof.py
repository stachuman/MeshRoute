from pathlib import Path
import importlib.util,json,subprocess,hashlib,re
q=Path('/tmp/mr-s8ac-r3-active').read_text().strip();q=Path(q);r=q/'snapshot';o=q/'fence-proof';o.mkdir()
sp=importlib.util.spec_from_file_location('abi',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(sp);sp.loader.exec_module(abi)
# Exact source copy changes quote-include search origin so the private model header can be supplied.
src=r/'lib/core/node_mac.cpp';copy=o/'node_mac.cpp';copy.write_bytes(src.read_bytes());assert src.read_bytes()==copy.read_bytes()
res=[]
for target in ['native','gateway','heltec_mobile']:
 data=json.loads((q/'layout'/(target+'-idedata.json')).read_text())
 for model in [False,True]:
  label=target+('-slot-removed' if model else '-baseline');obj=o/(label+'.o')
  cmd=abi.compile_command(data,copy,obj)
  if model:
   for inc in [r/'test',q/'layout',q/'layout/proposed']:cmd.insert(1,'-I'+str(inc))
  p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('fence-'+label+'.log')).write_bytes(p.stdout)
  expected=1 if model and target!='gateway' else 0
  assert (p.returncode!=0)==bool(expected),(label,p.stdout.decode())
  if expected:assert b'_remote_inbound' in p.stdout and b'not declared' in p.stdout
  res.append(dict(profile=target,model=model,command=cmd,exit=p.returncode,expected_compile_refusal=bool(expected),source_sha256=hashlib.sha256(copy.read_bytes()).hexdigest()))
  print(label,'expected refusal' if expected else 'PASS',flush=True)
(o/'compile-results.json').write_text(json.dumps(res,indent=2)+'\n')
# Source-reader proof: strip only the obsolete helper from a private source-reader tree.
sp2=importlib.util.spec_from_file_location('own',r/'tools/probe_features/ownership.py');own=importlib.util.module_from_spec(sp2);sp2.loader.exec_module(own)
baseline=own.run_checks(r);assert all(ok for _,ok,_ in baseline)
rx=r/'lib/core/node_mac_rx.cpp';h=r/'lib/core/node.h';oldrx=rx.read_text();oldh=h.read_text()
start=oldrx.index('void Node::remote_inbound_stage(');end=oldrx.index('\n}',start)+2
newrx=oldrx[:start]+oldrx[end:]
needle='    remote_inbound_stage(pa, ui, /*is_response=*/true);';assert newrx.count(needle)==1
newrx=newrx.replace(needle,'    // SYNTHETIC: replacement consumer omitted; this proof only measures the removed legacy seam.')
newh=re.sub(r'^.*void\s+remote_inbound_stage[^\n]*\n','',oldh,flags=re.M)
try:
 rx.write_text(newrx);h.write_text(newh);removed=own.run_checks(r)
finally:rx.write_text(oldrx);h.write_text(oldh)
failed=[dict(check=c,message=m) for c,ok,m in removed if not ok];assert any(x['check']=='O6c' for x in failed)
(o/'ownership-results.json').write_text(json.dumps(dict(baseline_checks=len(baseline),baseline_failures=0,private_legacy_removal_failures=failed),indent=2)+'\n')
# Name the exact old behavior tests, without pretending this static census executes them.
t=(r/'test/test_node_r3.cpp').read_text();tests=[]
for match in re.finditer(r'TEST_CASE\(',t):
 end=t.find('\nTEST_CASE(',match.end());end=len(t) if end<0 else end
 body=t[match.start():end]
 if 'take_remote_inbound(' in body:tests.append(dict(line=t.count('\n',0,match.start())+1,title=body.split('\n',1)[0],calls=len(re.findall(r'(?<!// )\b(?:node|r\.node)\.take_remote_inbound\(',body))))
(o/'legacy-test-census.json').write_text(json.dumps(tests,indent=2)+'\n')
print('ownership baseline',len(baseline),'checks PASS; private helper removal rejected by',','.join(x['check'] for x in failed),flush=True)
print('legacy test cases with drain references',len(tests),flush=True)
