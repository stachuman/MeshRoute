from pathlib import Path
import hashlib,json,struct,collections,subprocess,importlib.util,tarfile,io,copy
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';out=q/'stack';out.mkdir(exist_ok=True)
original=q/'original'
if not (original/'platformio.ini').exists():
 original.mkdir(exist_ok=True); blob=subprocess.check_output(['git','archive','7442e6f'],cwd=r)
 with tarfile.open(fileobj=io.BytesIO(blob)) as tf:tf.extractall(original)
spec=importlib.util.spec_from_file_location('abi',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);spec.loader.exec_module(abi)
old=Path('/tmp/mr-codex-s7b3p1-gaz2dlhn/prior-coder/attribute.py').read_text();exec(old[old.index('def elf('):old.index('\nsummary =')])
results=[];summary={}
for env in ['native','gateway','heltec_mobile']:
 print('START',env,flush=True);data=abi.idedata(env);(out/(env+'-idedata.json')).write_text(json.dumps(data,indent=2)+'\n')
 source=out/'types.cpp';source.write_text('#include "firmware_remote_actions.h"\n'+''.join('char mr_size_'+n+'[sizeof(mrfw::'+n+')];\nchar mr_align_'+n+'[alignof(mrfw::'+n+')];\n' for n in ['ActionPlan','ActionAdmission','ActionSupport','ActionObserver','RemoteActionBytes'])+'char mr_size_DeferredActionRecord[sizeof(meshroute::DeferredActionRecord)];\n')
 obj=out/(env+'-types.o');cmd=abi.compile_command(data,source,obj);p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(env+'-types.log')).write_bytes(p.stdout);assert p.returncode==0,p.stdout[-3000:]
 nm=subprocess.check_output([abi.binutil(data['cxx_path'],'nm'),'-S','--size-sort',str(obj)]).decode();(out/(env+'-types.nm')).write_text(nm);print(nm,flush=True)
 if env=='native':continue
 paths=[r/'.pio-measure'/s/env for s in ['s7b3-base','s7b3-final-1']];secs=[];covs=[];symbols=[];manifests=[]
 for path in paths:
  sec,cov=elf(path/'firmware.elf');secs.append(sec);covs.append(cov);manifests.append(json.loads((path/'manifest.json').read_text()));symbols.append(collections.Counter(line for line in (path/'symbols.txt').read_text().splitlines() if line.split('\t')[1] not in ['FILE','NOTYPE']))
 allocated=[]
 for name in sorted(secs[0].keys()|secs[1].keys()):
  b=secs[0].get(name,{});a=secs[1].get(name,{})
  if (b or a)['flags']&2:allocated.append(dict(name=name,base_bytes=b.get('size',0),final_bytes=a.get('size',0),delta=a.get('size',0)-b.get('size',0),base_sha256=b.get('sha256'),final_sha256=a.get('sha256')))
 objects=[]
 for rel in ['src/fw_main.cpp','src/firmware_commands.cpp','src/firmware_remote_actions.cpp','lib/core/remote_session.cpp','lib/core/node.cpp','lib/core/node_mac_rx.cpp']:
  row=dict(path=rel)
  for state in ['base','final']:
   src=(original if state=='base' else r)/rel
   if not src.exists():row[state]=None;continue
   obj=out/(env+'-'+state+'-'+Path(rel).name+'.o');cmd=abi.compile_command(data,src,obj)+['-fstack-usage']
   if state=='base':cmd=[c.replace('-I'+str(r)+'/src','-I'+str(original)+'/src').replace('-I'+str(r)+'/lib','-I'+str(original)+'/lib') for c in cmd]
   p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(obj.stem+'.log')).write_bytes(p.stdout);results.append(dict(env=env,state=state,path=rel,command=cmd,exit=p.returncode));(out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(env,state,rel,p.returncode,flush=True)
   if p.returncode:print(p.stdout.decode()[-5000:]);raise SystemExit(p.returncode)
   size=subprocess.check_output([abi.binutil(data['cxx_path'],'size'),str(obj)]).decode();(out/(obj.stem+'.size')).write_text(size);row[state]=size
   symbols_obj=subprocess.check_output([abi.binutil(data['cxx_path'],'nm'),'-C','-S','--size-sort',str(obj)]).decode();(out/(obj.stem+'.nm')).write_text(symbols_obj)
  objects.append(row)
 removed=list((symbols[0]-symbols[1]).elements());added=list((symbols[1]-symbols[0]).elements());summary[env]=dict(allocated_sections=allocated,text_coverage=covs,removed_symbols=removed,added_symbols=added,objects=objects,measurements=[m['measurements'] for m in manifests],fixed_identity_equal=manifests[0]['fixed_identity']==manifests[1]['fixed_identity']);(q/'elf-attribution.json').write_text(json.dumps(summary,indent=2)+'\n')
 print(env,'allocated',[(x['name'],x['delta']) for x in allocated if x['delta']],flush=True)
 for name,syms in [('removed',removed),('added',added)]:
  print(name,flush=True)
  for line in sorted(syms,key=lambda s:-int(s.split('\t')[-1]))[:40]:print(line,flush=True)
