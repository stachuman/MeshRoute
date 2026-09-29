"""Real composer and parser on the counterfactual T=163 record; no production edits."""
from pathlib import Path
import tempfile,json,subprocess
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;A=R/'artifacts'/E.name
D=Path(tempfile.mkdtemp(prefix='w6-send-'));P=Path(json.loads((E/'reply-measure.json').read_text())['scratch'])/'src'
cpp=r'''
#include "firmware_ui_send.h"
#include "device_ble.h"
#include "console_parse.h"
#include <cstdio>
#include <cstring>
int main(){mrnv::UiPresetBlob cat;mrfw::preset_defaults(cat);char text[164];memset(text,'A',163);text[163]=0;
for(auto&s:cat.slot)mrfw::preset_slot_put(s,true,true,text,163);
int checks=0; printf("{\"ble_command_max\":%zu,\"ble_storage\":%zu,\"cases\":[",mrble::kProductLineMaxBytes,mrble::kLineStorageBytes);
int i=0;for(auto kind:{mrui::SendKind::dm,mrui::SendKind::channel_canned,mrui::SendKind::emergency})for(bool fix:{false,true}){
mrui::SendReq req{kind,254,uint8_t(kind==mrui::SendKind::dm?1:kind==mrui::SendKind::channel_canned?9:0),cat.generation};
char line[199],old[96];int n=mrui::ui_compose_send_line(line,sizeof line,req,cat,255,fix);meshroute::Command c{};
auto parsed=meshroute::console::parse_command(line,n,c);int oldn=mrui::ui_compose_send_line(old,sizeof old,req,cat,255,fix);
if(n<=0 || parsed!=meshroute::console::ParseErr::ok || c.body_len!=163 || oldn!=0){fprintf(stderr,"kind=%d n=%d parsed=%d oldn=%d line=%s\n",int(kind),n,int(parsed),oldn,line);return 2;}++checks;
printf("%s{\"kind\":%d,\"fix\":%s,\"line_bytes\":%d,\"parser\":%d,\"old96_return\":%d}",i++?",":"",int(kind),fix?"true":"false",n,int(parsed),oldn);
}printf("],\"cases_passed\":%d}\n",checks);}
'''
cpp=cpp.replace('#include "device_ble.h"', '#include "'+str(R/'src/device_ble.h')+'"')
(D/'query.cpp').write_text(cpp);(E/'send-query.cpp').write_text(cpp)
cmd=['g++','-std=c++20','-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(P)]+['-I'+str(R/p) for p in ['lib/core','lib/hal','lib/console','lib/monocypher/src']]+[str(D/'query.cpp'),str(R/'lib/console/console_parse.cpp'),'-o',str(D/'query')]
p=subprocess.run(cmd,capture_output=True);(A/'send-build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode()[-1800:]
p=subprocess.run([str(D/'query')],capture_output=True);assert p.returncode==0,(p.stdout,p.stderr)
(E/'send-measure.json').write_text(json.dumps(dict(scope=__doc__,command=cmd,result=json.loads(p.stdout)),indent=2)+'\n');print(p.stdout.decode())
