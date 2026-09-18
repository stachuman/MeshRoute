from pathlib import Path
import hashlib,json,re,sys
sys.path.insert(0,"/home/staszek/MeshRoute/tools/probe_console_sink")
from structural import _body, _neutral
root=Path('/home/staszek/MeshRoute');out=Path('/tmp/mr-s8b-r5-mfhusrrg')
paths=['docs/superpowers/plans/2026-09-18-radmin-slice8b-mobile-controller-carrier.md','lib/core/remote_client.h','lib/core/node.h','lib/core/node_hashlocate.cpp','lib/core/node_mac_rx.cpp','lib/core/node_mac.cpp','src/firmware_remote_client.h','src/fw_main.cpp']
s={p:(root/p).read_text() for p in paths};b,h,n,hl,rx,mac,fw,main=[s[p] for p in paths]
checks=[]
def check(name,condition):
 checks.append({'name':name,'pass':bool(condition)});assert condition,name
check('revision 5 pins the five-argument pure ACK matcher', '`remote_client_observe_ack(const RemoteClientState&, uint16_t ctr, bool timed_out, uint8_t push_dst, uint32_t push_sender_hash)`' in b)
check('revision 5 requires the current mobile binding comparison', 'when the mobile holds a binding for\n`route.target_hash`, `push_dst` must equal it' in b)
check('revision 5 still specifies state-only firmware handoff', 'mrfw::remote_client_observe_push(g_node.remote_client(), pu, mrcon,' in b)
check('revision 5 prohibits additional observation state', 'nothing else is stored for observations' in b)
check('revision 5 keeps core matcher pure', 'Core stays pure and emit-free' in b)
check('route contains only target hash and layer path', 'struct RemoteClientRoute { uint32_t target_hash; uint8_t hops[3]; uint8_t hop_count; };' in h)
state=re.search(r'struct RemoteClientState \{(.*?)\n\};',h,re.S).group(1)
check('state has neither Node reference nor id-bind table',not re.search(r'Node|[&*]|id_bind',state))
check('Node exposes reusable const binding lookup', 'int               id_bind_find_by_hash(uint32_t key_hash32, IdBindConf* conf_out = nullptr) const;' in n)
check('binding lookup reads Node-owned table', 'Node::id_bind_find_by_hash' in hl and '_id_bind' in hl[hl.index('Node::id_bind_find_by_hash'):hl.index('Node::id_bind_find_by_hash')+1300])
check('current firmware emitter takes state and whole push, not Node', 'remote_client_observe_push(const meshroute::RemoteClientState& state, const meshroute::Push& pu, Delivery& delivery)' in fw)
check('current Print wrapper only forwards state push delivery', 'remote_client_observe_push(state, pu, delivery);' in fw)
check('current board call passes only state push sinks and connection', 'mrfw::remote_client_observe_push(g_node.remote_client(), pu, mrcon, &local_ble, mrble::connected());' in main)
check('same-layer ACK push contains no stable sender hash', 'const uint32_t acker_hash = ((pa.flags & DATA_FLAG_CROSS_LAYER) && ui && ui->has_source_hash) ? ui->source_hash : 0;' in rx)
check('ACK push contains actual origin and counter', 'pu.dst = pa.origin; pu.ctr = acked; pu.sender_hash = acker_hash;' in rx)
check('B413 sender arm is uniquely identifiable',_body(_neutral(hl),'uint16_t Node::send_by_hash').count('if (id >= 0 && conf == IdBindConf::authoritative)')==1)
check('existing XL delegate uses the home counter space', 'const uint16_t ctr = next_ctr(home);' in mac[mac.index('Node::delegate_send_layer'):])
report={'kind':'static source and contract audit; not a runtime proof','checks':checks,'passed':sum(x['pass'] for x in checks),'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'finding':'B416: required Node binding is not observable through the prescribed pure matcher or firmware handoff; needs explicit call-scoped lookup contract, no new resident state','bounded_counterexample':{'shared_input_values':{'route.target_hash':'0x22222222','request_id':1,'carrier_ctr':7,'execute_and_ack':True,'ctr':7,'timed_out':False,'push_dst':2,'push_sender_hash':0},'external_binding_A':2,'external_binding_B':3,'required_result_A':1,'required_result_B':0,'note':'Logical information-dependency counterexample, not executed Node fixtures. All prescribed input values agree; only the omitted Node binding differs.'}}
(out/'b416-source-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(report['passed'],'static checks PASS')
