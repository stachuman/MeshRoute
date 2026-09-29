"""Counterfactual T=163 measurement of real writer/emitter/adapter/sinks; scratch copies only, not implementation."""
from pathlib import Path
import tempfile,shutil,subprocess,json,re,hashlib
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;A=R/'artifacts'/E.name
D=Path(tempfile.mkdtemp(prefix='w6-reply-measure-'));shutil.copytree(R/'src',D/'src')
def edit(fn,a,b):
 p=D/'src'/fn;s=p.read_text();assert s.count(a)==1,(fn,a,s.count(a));p.write_text(s.replace(a,b))
edit('device_nv.h','char    text[18];','char    text[164];')
edit('device_nv.h','kUiPresetTextMax = 17;','kUiPresetTextMax = 163;')
edit('device_nv.h','uint8_t  reserved_tail[3];','uint8_t  reserved_tail[1];')
edit('device_nv.h','sizeof(UiPresetSlot) == 21','sizeof(UiPresetSlot) == 167')
edit('device_nv.h','sizeof(UiPresetBlob) == 12 + 17 * 21 + 3','sizeof(UiPresetBlob) == 12 + 17 * 167 + 1')
edit('device_nv.h','kUiPresetVersion = 1;','kUiPresetVersion = 2;')
edit('firmware_ui_preset_verbs.h','kPresetLineMax = 160;','kPresetLineMax = 244;')
edit('firmware_ui_preset_verbs.h','j.u32(mrnv::kUiPresets);','j.u32(mrnv::kUiPresets); j.lit(",\\\"text_max\\\":"); j.u32(mrnv::kUiPresetTextMax);')
for old,new in [('    { nullptr,         0 },        //  3 dm3','    { "Where are you?",0 },        //  3 dm3'),('    { nullptr,         0 },        // 11 channel3','    { "Return to base now",0 },    // 11 channel3'),('    { nullptr,         0 },        // 12 channel4','    { "On my way",0 },             // 12 channel4')]:edit('firmware_ui_presets.h',old,new)
cpp=r'''
#include "firmware_ui_preset_verbs.h"
#define ARDUINO 100
#define MR_CONSOLE 1
#include "console_sink.h"
#include "dispatch_sink.h"
#include <iostream>
#include <algorithm>
#include <vector>
FakeSerial Serial;
struct Store:mrfw::IUiPresetStore {mrnv::UiPresetBlob b{}; int saves=0; mrnv::UiPresetRead state=mrnv::UiPresetRead::ok; mrnv::UiPresetRead load(mrnv::UiPresetBlob& o) override{o=b;return state;} bool save(const mrnv::UiPresetBlob& x)override{b=x;++saves;return true;}};
struct Gate:mrfw::IEmergencyGate{bool emergency_active()const override{return false;}};
struct Capture:mrfw::IPresetLines {std::vector<std::string> lines;void line(const char*s,size_t n)override{lines.emplace_back(s,n);} std::string all()const{std::string s;for(auto&l:lines)s+=l;return s;}};
@ADAPTER@
static std::string ble_wire; static int ble_flushes;
static void ble_flush(const char*s,size_t n){ble_wire.append(s,n);++ble_flushes;}
int main(){
 Store st; Gate gate; mrfw::PresetDiag diag;
 mrfw::preset_defaults(st.b);st.b.generation=0xffffffff;
 const std::string text(163,'X'); for(auto &slot:st.b.slot)mrfw::preset_slot_put(slot,true,false,text.data(),text.size());
 mrfw::PresetCatalog cat(st,gate);cat.begin();Capture full; mrfw::preset_emit_list(cat,full);
 std::cout<<"{\"line_sizes\":[";for(size_t i=0;i<full.lines.size();++i)std::cout<<(i?",":"")<<full.lines[i].size();
 std::cout<<"],\"list_bytes\":"<<full.all().size();
 char old[160],wide[244];auto sl=cat.slot(0);
 std::cout<<",\"old160_max_record_return\":"<<mrfw::write_ui_preset(old,sizeof old,0,sl)<<",\"wide244_max_record_return\":"<<mrfw::write_ui_preset(wide,sizeof wide,0,sl);
 std::cout<<",\"sink_cases\":[";bool comma=false;
 for(int fifo:{0,128,256})for(int drain:{0,128,256}){
  if(comma)std::cout<<",";comma=true;Serial.reset(fifo);Serial.auto_drain=drain;mrcon_detail::GuardedConsole sink;PresetPrintLines adapter(sink);
  mrfw::preset_emit_list(cat,adapter);auto drops=sink.dropped_lines();size_t immediate=Serial.wire.size();
  Serial.cap=256;Serial.auto_drain=256;for(int i=0;i<200;++i)sink.service();
  std::cout<<"{\"fifo\":"<<fifo<<",\"drain_per_poll\":"<<drain<<",\"bytes_immediate\":"<<immediate<<",\"dropped\":"<<drops<<",\"final_bytes\":"<<Serial.wire.size()<<",\"complete\":"<<(Serial.wire==full.all()?"true":"false")<<",\"drop_report\":"<<(Serial.wire.find("CONSOLE_DROP")!=std::string::npos?"true":"false")<<"}";
 }
 std::cout<<"]";
 LineSink ble(ble_flush);PresetPrintLines ba(ble);mrfw::preset_emit_list(cat,ba);ble.flush();
 std::cout<<",\"ble_linesink_bytes\":"<<ble_wire.size()<<",\"ble_linesink_flushes\":"<<ble_flushes<<",\"ble_linesink_exact\":"<<(ble_wire==full.all()?"true":"false");
 std::cout<<",\"per_kind_bytes\":["; for(int kind=0;kind<3;++kind){size_t n=full.lines.back().size();for(int i=0;i<17;++i)if(int(mrfw::preset_kind_of(i))==kind)n+=full.lines[i].size();std::cout<<(kind?",":"")<<n;}std::cout<<"]";
 Capture reset;mrfw::preset_verb(cat,diag,"preset reset all",16,reset);
 std::cout<<",\"reset_all_default_bytes\":"<<reset.all().size()<<",\"reset_all_records\":"<<reset.lines.size()<<",\"reset_writes\":"<<st.saves;
 Capture boot;st.state=mrnv::UiPresetRead::invalid;int before=st.saves;mrfw::preset_boot_restore(cat,diag,boot);
 std::cout<<",\"invalid_boot_bytes\":"<<boot.all().size()<<",\"boot_writes\":"<<st.saves-before;
 const char* cmd="preset set emergency loc=off \"";std::string arg=std::string(cmd)+text+'"';Capture set;
 bool handled=mrfw::preset_verb(cat,diag,arg.data(),arg.size(),set);
 std::cout<<",\"longest_set_command_bytes\":"<<arg.size()+3<<",\"set_handled\":"<<(handled?"true":"false")<<",\"set_exact\":"<<(cat.slot(0).len==163&&memcmp(cat.slot(0).text,text.data(),163)==0?"true":"false");
 std::cout<<",\"default_channel3_len\":"<<strlen("Return to base now")<<",\"first16_default\":\""<<std::string("Return to base now",16)<<"\"}"<<std::endl;
}
'''
src=(R/'src/firmware_commands.cpp').read_text();adapter=re.search(r'struct PresetPrintLines :.*?\n\};',src,re.S)[0]
cpp=cpp.replace('@ADAPTER@',adapter);(D/'query.cpp').write_text(cpp)
cmd=['g++','-std=c++20','-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fno-exceptions','-fno-rtti','-I'+str(R/'tools/probe_console_sink/fakes'),'-I'+str(D/'src'),'-I'+str(R/'lib/console'),'-I'+str(R/'lib/core'),'-I'+str(R/'lib/hal'),'-I'+str(R/'lib/monocypher/src'),str(D/'query.cpp'),str(R/'lib/console/console_json.cpp'),'-o',str(D/'query')]
p=subprocess.run(cmd,capture_output=True);(A/'reply-build.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode()[-1500:]
p=subprocess.run([str(D/'query')],capture_output=True);assert p.returncode==0;pdata=json.loads(p.stdout)
out=dict(scope=__doc__,scratch=str(D),command=cmd,adapter_sha256=hashlib.sha256(adapter.encode()).hexdigest(),result=pdata,modified_scratch_headers={str(p.relative_to(D)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (D/'src').glob('*') if p.name in ['device_nv.h','firmware_ui_presets.h','firmware_ui_preset_verbs.h']})
(E/'reply-measure.json').write_text(json.dumps(out,indent=2)+'\n');(E/'reply-query.cpp').write_text(cpp)
print(json.dumps(pdata,indent=2))
