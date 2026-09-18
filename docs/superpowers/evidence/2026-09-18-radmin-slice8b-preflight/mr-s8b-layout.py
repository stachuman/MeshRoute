from pathlib import Path
import importlib.util,json,subprocess,hashlib
q=Path('/tmp/mr-s8b-r2-active').read_text().strip();q=Path(q);r=q/'snapshot';o=q/'layout';o.mkdir();sp=importlib.util.spec_from_file_location('abi8b',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(sp);sp.loader.exec_module(abi)
h=(r/'lib/core/remote_client.h').read_text();needle='    uint8_t target_book_slot;                 // existing in-use guards\n';assert h.count(needle)==1;candidate=h.replace(needle,needle+'    uint16_t carrier_ctr; // PRIVATE 8b layout proposal: bytes 118..119\n');(o/'remote_client.h').write_text(candidate)
n=(r/'lib/core/node.h').read_text().replace('#include "remote_client.h"','#include "'+str(o/'remote_client.h')+'"');(o/'node.h').write_text(n)
results={}
for target in ['native','heltec_mobile','gateway']:
 data=abi.idedata(target);results[target]={}
 for variant in ['baseline','candidate']:
  inc=str(r/'lib/core/node.h') if variant=='baseline' else str(o/'node.h')
  tu='#include "'+inc+'"\n#include <cstddef>\n'
  values={'core':'sizeof(meshroute::RemoteClientPendingCore)','row':'sizeof(meshroute::RemoteClientPending)','state':'sizeof(meshroute::RemoteClientState)','state_align':'alignof(meshroute::RemoteClientState)','node':'sizeof(meshroute::Node)','slot_offset':'offsetof(meshroute::RemoteClientPendingCore,target_book_slot)+1'}
  if variant=='candidate':values['carrier_offset']='offsetof(meshroute::RemoteClientPendingCore,carrier_ctr)+1'
  tu+='\n'.join('char mr_abi_'+k+'['+v+'];' for k,v in values.items());cpp=o/(target+'-'+variant+'.cpp');cpp.write_text(tu);obj=cpp.with_suffix('.o');cmd=abi.compile_command(data,cpp,obj);p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('layout-'+target+'-'+variant+'.log')).write_bytes(p.stdout);assert p.returncode==0,p.stdout.decode()[-1200:]
  sizes=abi.read_sizes(abi.binutil(data['cxx_path'],'nm'),obj);results[target][variant]=dict(command=cmd,exit=p.returncode,sizes=sizes,object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
 b=results[target]['baseline']['sizes'];c=results[target]['candidate']['sizes'];assert b=={k:v for k,v in c.items() if k!='mr_abi_carrier_offset'};assert c['mr_abi_carrier_offset']==119 and c['mr_abi_slot_offset']==118 and c['mr_abi_core']==120 and c['mr_abi_row']==352 and c['mr_abi_state']==4512 and c['mr_abi_state_align']==8
 print(target,c,flush=True)
(o/'results.json').write_text(json.dumps(results,indent=2)+'\n')
