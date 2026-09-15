from pathlib import Path
import re,json,importlib.util,subprocess,hashlib,os
q=Path(__file__).resolve().parent;r=q/'snapshot';shadow=q/'layout-model';shadow.mkdir(exist_ok=True)
os.environ['MESHROUTE_GIT_REV_OVERRIDE']='ac5f9a5'
spec=importlib.util.spec_from_file_location('abi',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);spec.loader.exec_module(abi)
# Explicit twelve-policy-row author proposal, not a production implementation.
# Nine semantic kinds encode the complete no-payload/bool/crash-mode argument.
action='''
// QA AUTHOR PROPOSAL ONLY: twelve policy rows, no production allocation.
enum class QaActionKind : uint8_t {
    none, reboot, prep_restart, ota, factory_reset,
    sleep_on, sleep_off, crash_hang, crash_fault, crash_reboot
};
enum class QaActionBackend : uint8_t { none, nrf_reset, esp_reset, nrf_dfu, wifi_ota, nrf_fault, esp_fault };
enum class QaActionPhase : uint8_t { none, preparing, prepared, armed, due };
enum class QaActionTrigger : uint8_t { none, ack, deadline };
struct QaDeferredActionRecord {
    uint64_t request_id, admin_epoch, activate_at_ms;
    uint32_t source_hash, activation_ms;
    uint8_t controller_slot, authority;
    QaActionKind kind;
    QaActionPhase phase;
    QaActionBackend backend;
    QaActionTrigger trigger;
};
'''
h=(r/'lib/core/remote_session.h').read_text();needle=re.search(r'struct TranscriptHeader \{.*?\n\};',h,re.S).group();h=h.replace(needle,needle[:-3]+'\n    uint32_t activation_ms;\n};',1)
needle=re.search(r'struct RemoteSessionState \{.*?\n\};',h,re.S).group();h=h.replace(needle,action+'\n'+needle[:-3]+'\n    QaDeferredActionRecord action;\n    uint8_t last_activation_kind, last_activation_outcome;\n};',1)
(shadow/'remote_session.h').write_text(h)
# Explicit private model re-pin only; the first run proved the old native pin fails by +80.
nh=(r/'lib/core/node.h').read_text()
assert nh.count('static_assert(sizeof(Node) == 230896') == 1
nh=nh.replace('static_assert(sizeof(Node) == 230896','static_assert(sizeof(Node) == 230976',1)
(shadow/'node.h').write_text(nh)
base='#include "node.h"\n'
for t in ['meshroute::Node','meshroute::RemoteSessionState','meshroute::TranscriptHeader']:
 ident=t.replace('::','_');base+=f'char mr_abi_size__{ident}[sizeof({t})];\nchar mr_abi_align__{ident}[alignof({t})];\n'
(q/'baseline-layout.cpp').write_text(base)
model=base+'char mr_abi_size__meshroute_QaDeferredActionRecord[sizeof(meshroute::QaDeferredActionRecord)];\nchar mr_abi_align__meshroute_QaDeferredActionRecord[alignof(meshroute::QaDeferredActionRecord)];\n'
model+='''static_assert(sizeof(meshroute::QaActionKind) == 1);
static_assert(sizeof(meshroute::QaActionBackend) == 1);
#if MR_FEAT_RADMIN_ACCEPT
char mr_abi_included__accept_action_resident[1];
#else
char mr_abi_included__client_no_action_resident[1];
#endif
'''
(q/'proposed-layout.cpp').write_text(model)
result={}
for target in ('native','gateway','heltec_mobile'):
 data=abi.idedata(target);(q/(target+'-idedata.json')).write_text(json.dumps(data,indent=2)+'\n');result[target]={}
 for kind in ('baseline','proposed'):
  obj=q/(target+'-'+kind+'.o');cmd=abi.compile_command(data,q/(kind+'-layout.cpp'),obj)
  if kind=='proposed':cmd.insert(1,'-I'+str(shadow))
  p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/(target+'-'+kind+'-compile.log')).write_bytes(p.stdout)
  entry={'command':cmd,'exit':p.returncode};result[target][kind]=entry
  (q/'plan-measurements.json').write_text(json.dumps(result,indent=2)+'\n')
  if p.returncode:print(p.stdout.decode()[-4500:]);raise SystemExit(p.returncode)
  entry['sizes']=abi.read_sizes(abi.binutil(data['cxx_path'],'nm'),obj);entry['object_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest()
  (q/'plan-measurements.json').write_text(json.dumps(result,indent=2)+'\n');print(target,kind,json.dumps(entry['sizes']),flush=True)
print('QA PROPOSED LAYOUT COMPLETE; compile-only shadows, no linked RAM or shared production edits',flush=True)
