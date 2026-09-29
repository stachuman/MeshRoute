
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
struct PresetPrintLines : mrfw::IPresetLines {
    explicit PresetPrintLines(Print& o) : _o(o) {}
    void line(const char* s, size_t n) override { _o.write(reinterpret_cast<const uint8_t*>(s), n); }
    Print& _o;
};
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
