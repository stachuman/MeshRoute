from pathlib import Path
import importlib.util,json,subprocess,hashlib,re
q=Path('/tmp/mr-s8ac-r3-active').read_text().strip();q=Path(q);r=q/'snapshot';o=q/'layout';sp=importlib.util.spec_from_file_location('abi8audit',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(sp);sp.loader.exec_module(abi)
# Model-only private-offset inspection on the same compiler/defines; access checking changes no layout.
tu='''#include "controller-model.h"
#include "node.h"
#include <type_traits>
char mr_abi_off_cfg[__builtin_offsetof(meshroute::Node,_cfg)+1];
char mr_abi_off_channel_counter[__builtin_offsetof(meshroute::Node,_channel_seal_ctr)+1];
#if MR_FEAT_RADMIN_CLIENT
#if S8_PROPOSED
char mr_abi_off_client[__builtin_offsetof(meshroute::Node,_remote_client)+1];
#else
char mr_abi_off_legacy[__builtin_offsetof(meshroute::Node,_remote_inbound)+1];
#endif
#endif
#if MR_FEAT_RADMIN_ACCEPT
char mr_abi_off_accept[__builtin_offsetof(meshroute::Node,_radmin_session)+1];
#endif
'''
old=(r/'test/radmin_0e_candidate_types.h').read_text();mapping={'PendingRequestCore':'PendingCore','SessionCacheEntry':'SessionEntry','ResponseAssemblyHeader':'AssemblyHeader','RetainedResultHeader':'RetainedHeader','AckDebtEntry':'AckEntry'};members=[]
for name,new in mapping.items():
 body=re.search(r'struct '+name+r' \{(.*?)\n\};',old,re.S).group(1);body=re.sub(r'//[^\n]*','',body)
 fields=re.findall(r'uint(?:8|16|32|64)_t\s+(\w+)(?:\[[^]]+\])?;',body)
 for f in fields:
  tu+=f'static_assert(std::is_same_v<decltype(radmin0e::{name}::{f}), decltype(s8model::{new}::{f})>);\n';members.append((name,new,f))
new_members={'PendingCore':{'discovery_id':8,'route':8,'credential_slot':1,'target_book_slot':1},'SessionEntry':{'credential_slot':1,'target_book_slot':1},'AssemblyHeader':{'result_domain':1,'reserved':1,'result_detail':4},'RetainedHeader':{'result_domain':1,'result_code':1,'result_detail':4},'AckEntry':{'source_hash':4,'route':8,'credential_slot':1,'target_book_slot':1,'carrier':1,'reserved':1},'Counters':{x:2 for x in ['request_table_pressure','assembly_failure','unmatched_response','auth_failure','local_result_pressure','radio_enqueue_failure']}}
for t,fs in new_members.items():
 for f,n in fs.items():tu+=f'static_assert(sizeof(s8model::{t}::{f}) == {n});\n'
(o/'offsets.cpp').write_text(tu);result={}
for target in ['native','gateway','heltec_mobile']:
 d=abi.idedata(target);result[target]={}
 for variant in ['baseline','proposed']:
  obj=o/(target+'-'+variant+'-offsets.o');cmd=abi.compile_command(d,o/'offsets.cpp',obj);cmd[1:1]=['-fno-access-control','-I'+str(o),'-I'+str(r/'test'),'-DS8_PROPOSED='+('1' if variant=='proposed' else '0')]
  if variant=='proposed':cmd.insert(1,'-I'+str(o/'proposed'))
  p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('offsets-'+target+'-'+variant+'.log')).write_bytes(p.stdout);assert p.returncode==0,p.stdout.decode()[-2000:]
  result[target][variant]=dict(command=cmd,exit=p.returncode,offsets={k:v-1 for k,v in abi.read_sizes(abi.binutil(d['cxx_path'],'nm'),obj).items()},object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest())
(o/'offsets.json').write_text(json.dumps(dict(preserved_members=members,new_members=new_members,results=result),indent=2)+'\n')
m=json.loads((o/'measurements.json').read_text());summary={'note':'R-RA-45 pointer-free re-measurement; authorized by formula, not linked RAM','rows':{},'targets':{}}
for t,v in m.items():
 z=v['proposed']['sizes'];b=v['baseline']['sizes'];summary['targets'][t]=dict(base_Node=b['mr_abi_size__meshroute_Node'],proposed_Node=z['mr_abi_size__meshroute_Node'],Node_delta=z['mr_abi_size__meshroute_Node']-b['mr_abi_size__meshroute_Node'],state=z['mr_abi_size__s8model_ProposedState'],align=z['mr_abi_align__s8model_ProposedState'],legacy_type=b['mr_abi_size__meshroute_Node_RemoteInbound'])
 for oldname,newname,count in [('PendingRequestInline','PendingInline',4),('SessionCacheEntry','SessionEntry',4),('ResponseAssemblyHeader','AssemblyHeader',2),('ResponseChunk',None,8),('RetainedResultHeader','RetainedHeader',2),('AckDebtEntry','AckEntry',8)]:
  k='mr_abi_size__radmin0e_'+oldname;n='mr_abi_size__s8model_'+newname if newname else k
  row=summary['rows'].setdefault(oldname,dict(count=count,targets={}));row['targets'][t]=dict(old=b[k],proposed=z[n],delta=count*(z[n]-b[k]))
  
summary['counters']=12;summary['tail_padding']=4;summary['preserved_member_count']=len(members);summary['additional_member_checks']=sum(map(len,new_members.values()))
(o/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary['targets'],indent=2));print('Historical member type checks',len(members),'per compilation, all pass')
