from pathlib import Path
import importlib.util,json,subprocess,re,hashlib
q=Path('/tmp/mr-codex-s8ac-preflight-active').read_text().strip();q=Path(q);r=q/'snapshot';out=q/'layout';out.mkdir(exist_ok=True)
sp=importlib.util.spec_from_file_location('abi8',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(sp);sp.loader.exec_module(abi)
candidate=(r/'test/radmin_0e_candidate_types.h').read_text()
# Complete coder PROPOSAL; no production type or allocation. Preserve every historical member.
# u8 indexes use explicit none/self values; no key seeds or callable object is stored in core.
header='''#pragma once
#include <cstddef>
#include <cstdint>
#include "radmin_0e_candidate_types.h"
namespace s8model {
struct ReplyRoute { uint32_t target_hash; uint8_t hops[3]; uint8_t hop_count; };
'''
def struct(name):return re.search(r'struct '+name+r' \{.*?\n\};',candidate,re.S).group()
header+='using namespace radmin0e;\n'
s=struct('PendingRequestCore').replace('PendingRequestCore','PendingCore')
s=s.replace('    uint64_t request_id;', '    uintptr_t usb_sink;                       // supplied USB Print, must outlive request; zero for BLE\n    uint64_t discovery_id;                    // fresh bootstrap ID while exact execute bytes stay retained\n    uint64_t request_id;',1)
s=s[:-3]+'\n'+'''    ReplyRoute route;                         // immutable source-validation/carrier hints
    uint8_t credential_slot;                  // self or key0..key9, never inferred from mutable UI
    uint8_t target_book_slot;                 // existing in-use guards
};
'''
header+=s+'\nstruct PendingInline { PendingCore core; uint8_t sealed[kCandidateSealedRequestBytes]; };\n'
s=struct('SessionCacheEntry').replace('SessionCacheEntry','SessionEntry');s=s[:-3]+'\n'+'''    uint8_t credential_slot;
    uint8_t target_book_slot;
};
''';header+=s+'\n'
s=struct('ResponseAssemblyHeader').replace('ResponseAssemblyHeader','AssemblyHeader');s=s[:-3]+'\n'+'''    uint8_t result_domain;                    // terminal vs protocol error vs admission/local outcome
    uint8_t reserved;
    uint32_t result_detail;                   // scheduled delay / admission count, independent of output
};
''';header+=s+'\n'
s=struct('RetainedResultHeader').replace('RetainedResultHeader','RetainedHeader');s=s[:-3]+'\n'+'''    uint8_t result_domain;
    uint8_t result_code;
    uint32_t result_detail;                   // survives assembly release and local replay
};
''';header+=s+'\n'
s=struct('AckDebtEntry').replace('AckDebtEntry','AckEntry');s=s[:-3]+'\n'+'''    uint32_t source_hash;                     // originating SOURCE_HASH, never rebuilt from mutable state
    ReplyRoute route;
    uint8_t credential_slot;
    uint8_t target_book_slot;
    uint8_t carrier;
    uint8_t reserved;
};
''';header+=s+'\n'
header+='''struct Counters { uint16_t request_table_pressure, assembly_failure, unmatched_response,
    auth_failure, local_result_pressure, radio_enqueue_failure; };
struct HistoricalState {
    radmin0e::PendingRequestInline pending[4]; radmin0e::SessionCacheEntry sessions[4];
    radmin0e::ResponseAssemblyHeader assemblies[2]; radmin0e::ResponseChunk chunks[8];
    radmin0e::RetainedResultHeader retained[2]; radmin0e::AckDebtEntry ack_debt[8];
};
struct CountersOnlyState { HistoricalState historical; Counters counters; };
struct ProposedState {
    PendingInline pending[4]; SessionEntry sessions[4]; AssemblyHeader assemblies[2];
    radmin0e::ResponseChunk chunks[8]; RetainedHeader retained[2]; AckEntry ack_debt[8]; Counters counters;
};
}
'''
(out/'controller-model.h').write_text(header)
nh=(r/'lib/core/node.h').read_text();assert nh.count('    RemoteInbound _remote_inbound{};')==1
# Three explicit private models: historical capacity, counters alone, complete proposal.
for name,t in [('historical','HistoricalState'),('counters_only','CountersOnlyState'),('proposed','ProposedState')]:
 shadow=out/name;shadow.mkdir(exist_ok=True)
 h=nh.replace('    RemoteInbound _remote_inbound{};','    // MODEL: legacy client slot removed.',1)
 h=h.replace('    uint16_t _channel_seal_ctr = 0;','    uint16_t _channel_seal_ctr = 0;\n#if MR_FEAT_RADMIN_CLIENT\n    s8model::'+t+' _remote_client{};\n#endif',1)
 h='#include "controller-model.h"\n'+h
 # Named MODEL-only removal of native production-size assertion; baseline is compiled with it intact.
 h,n=re.subn(r'static_assert\(sizeof\(Node\) == 230976,', 'static_assert(sizeof(Node) > 0, // MODEL: report measured replacement below\n',h,count=1);assert n==1
 (shadow/'node.h').write_text(h)
 (shadow/'node-model.diff').write_text(subprocess.run(['diff','-u',str(r/'lib/core/node.h'),str(shadow/'node.h')],stdout=subprocess.PIPE).stdout.decode())
base='#include "controller-model.h"\n#include "node.h"\n'
types=['meshroute::Node','meshroute::Node::RemoteInbound','s8model::ReplyRoute','radmin0e::PendingRequestCore','radmin0e::PendingRequestInline','radmin0e::SessionCacheEntry','radmin0e::ResponseAssemblyHeader','radmin0e::ResponseChunk','radmin0e::RetainedResultHeader','radmin0e::AckDebtEntry','s8model::PendingCore','s8model::PendingInline','s8model::SessionEntry','s8model::AssemblyHeader','s8model::RetainedHeader','s8model::AckEntry','s8model::Counters','s8model::HistoricalState','s8model::CountersOnlyState','s8model::ProposedState']
for t in types:
 ident=t.replace('::','_');base+=f'char mr_abi_size__{ident}[sizeof({t})];\nchar mr_abi_align__{ident}[alignof({t})];\n'
base+='static_assert(sizeof(s8model::PendingCore::route) == 8);\nstatic_assert(sizeof(s8model::PendingCore::usb_sink) == sizeof(void*));\nstatic_assert(sizeof(s8model::PendingCore::discovery_id) == 8);\nstatic_assert(sizeof(s8model::AssemblyHeader::result_domain) == 1);\nstatic_assert(sizeof(s8model::RetainedHeader::result_detail) == 4);\n'
base+='char mr_abi_pointer_bytes[sizeof(void*)];\nchar mr_abi_client[MR_FEAT_RADMIN_CLIENT+1];\nchar mr_abi_accept[MR_FEAT_RADMIN_ACCEPT+1];\n'
(out/'layout.cpp').write_text(base)
results={}
for target in ['native','gateway','heltec_mobile']:
 data=abi.idedata(target);(out/(target+'-idedata.json')).write_text(json.dumps(data,indent=2)+'\n');results[target]={}
 for kind in ['baseline','historical','counters_only','proposed']:
  obj=out/(target+'-'+kind+'.o');cmd=abi.compile_command(data,out/'layout.cpp',obj);cmd.insert(1,'-I'+str(out));cmd.insert(1,'-I'+str(r/'test'))
  if kind!='baseline':cmd.insert(1,'-I'+str(out/kind))
  p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('layout-'+target+'-'+kind+'.log')).write_bytes(p.stdout)
  entry=dict(command=cmd,exit=p.returncode);results[target][kind]=entry;(out/'measurements.json').write_text(json.dumps(results,indent=2)+'\n')
  if p.returncode:print(p.stdout.decode()[-4000:]);raise SystemExit(p.returncode)
  entry['sizes']=abi.read_sizes(abi.binutil(data['cxx_path'],'nm'),obj);entry['object_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest();(out/'measurements.json').write_text(json.dumps(results,indent=2)+'\n')
  print(target,kind,entry['sizes']['mr_abi_size__meshroute_Node'],entry['sizes']['mr_abi_size__s8model_ProposedState'],flush=True)
print('COMPILE-ONLY MODEL COMPLETE: no linked RAM, no production edit or re-pin',flush=True)
