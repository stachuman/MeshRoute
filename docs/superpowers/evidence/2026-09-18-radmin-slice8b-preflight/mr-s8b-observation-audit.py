from pathlib import Path
import hashlib,json,re
q=Path('/tmp/mr-s8b-r2-active').read_text().strip();q=Path(q);r=q/'snapshot';checks=[]
def ck(name,ok):
 assert ok,name
 checks.append(name)
b=(r/'docs/superpowers/plans/2026-09-18-radmin-slice8b-mobile-controller-carrier.md').read_text();fw=(r/'src/fw_main.cpp').read_text();cmd=(r/'src/firmware_commands.cpp').read_text();core=(r/'lib/core/remote_client.h').read_text();rx=(r/'lib/core/node_mac_rx.cpp').read_text();mac=(r/'lib/core/node_mac.cpp').read_text();node=(r/'lib/core/node.cpp').read_text();hal=(r/'lib/core/hal.h').read_text()
fence=b.split('## 6. Fence',1)[1].split('## 7.',1)[0];sec4=b.split('## 4.',1)[1].split('## 5.',1)[0]
ck('brief requires three source-site hooks before existing pushes', 'each placed immediately before the existing' in sec4 and 'emit-free, push-preserving' in sec4)
ck('brief prohibits extra observation state', 'any resident byte beyond `carrier_ctr` in padding' in b)
ck('fw_main production glue is outside the fence','src/fw_main.cpp' not in fence)
ck('Node push dequeue implementation outside fence','lib/core/node.cpp' not in fence)
ck('HAL interface outside fence','lib/core/hal.h' not in fence)
ck('push drain precedes controller delivery service',fw.index('while (g_node.next_push(pu))')<fw.index('mrfw::remote_client_service_once(mrcon,&local_ble,mrble::connected());'))
ck('BLE adapter exists only in the later service scope', '#if MR_FEAT_RADMIN_CLIENT\n    { LineSink local_ble(ble_sink);' in fw)
ck('BLE sink is file-local', 'static void ble_sink(const char* s, size_t n)' in fw)
ck('delivery adapters are call scoped in firmware bindings',cmd.count('RemoteClientDelivery delivery(')==2)
ck('pure delivery interface receives no source-site sink', 'No callback or sink is stored.' in core and 'IRemoteLocalDelivery' not in rx and 'IRemoteLocalDelivery' not in mac)
ck('next_push consumes the only queue copy', 'out = _push_ring[_push_head];' in node and '--_push_count;\n    return true;' in node)
ck('HAL has no existing remote delivery hook', 'remote_client' not in hal and 'RemoteLocalTransport' not in hal)
ck('contract requires both home origin and original reporter', 'rec.failed_origin' in sec4 and 'direct\n  `original_reporter`' in sec4)
usb=re.search(r'`> remote <id16> carrier[^`]+`',sec4).group(0);ble=re.search(r'`\{"ev":"remote_carrier"[^`]+`',sec4).group(0)
ck('literal USB format has no reporter field','origin=<n>' in usb and 'reporter' not in usb)
ck('literal BLE format has no reporter field','"origin":<n>' in ble and 'reporter' not in ble)
anchors={}
for name,needle in [('src/fw_main.cpp','while (g_node.next_push(pu))'),('src/firmware_commands.cpp','void remote_client_service_once('),('lib/core/node.cpp','bool Node::next_push(Push& out)'),('lib/core/remote_client.cpp','RemoteClientError send_ready(')]:
 t=(r/name).read_text();anchors[name]=dict(symbol=needle,line=t[:t.index(needle)].count('\n')+1,sha256=hashlib.sha256(t.encode()).hexdigest())
(q/'observation-audit.json').write_text(json.dumps(dict(kind='source/fence audit, not an executable event-delivery proof',checks=checks,count=len(checks),anchors=anchors,usb_format=usb,ble_format=ble),indent=2)+'\n');print(len(checks),'source/fence checks PASS; no permitted existing observation handoff found')
