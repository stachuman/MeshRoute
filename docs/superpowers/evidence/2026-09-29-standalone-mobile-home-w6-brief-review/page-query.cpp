
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

int main(){Store st;Gate gate;mrfw::PresetDiag diag;mrfw::preset_defaults(st.b);st.b.generation=0xffffffff;const std::string text(163,'X');for(auto&slot:st.b.slot)mrfw::preset_slot_put(slot,true,false,text.data(),text.size());mrfw::PresetCatalog cat(st,gate);cat.begin();Capture full;mrfw::preset_emit_list(cat,full);
std::cout<<"{\"pages\":[";
for(int page=1;page<=5;++page){std::string end=full.lines.back();auto pos=end.rfind('}');end.insert(pos,",\"page\":"+std::to_string(page)+",\"pages\":5");std::string expected;Serial.reset(0);mrcon_detail::GuardedConsole sink;PresetPrintLines out(sink);int count=0;
for(int i=(page-1)*4;i<std::min(page*4,17);++i){mrfw::preset_emit_record(cat,i,out);expected+=full.lines[i];++count;}out.line(end.data(),end.size());expected+=end;auto drops=sink.dropped_lines();auto immediate=Serial.wire.size();Serial.cap=256;Serial.auto_drain=256;for(int i=0;i<50;++i)sink.service();
std::cout<<(page>1?",":"")<<"{\"page\":"<<page<<",\"records\":"<<count<<",\"end_bytes\":"<<end.size()<<",\"bytes\":"<<expected.size()<<",\"dropped\":"<<drops<<",\"immediate_bytes\":"<<immediate<<",\"exact_after_drain\":"<<(Serial.wire==expected?"true":"false")<<"}";
}
std::cout<<"],\"reset_all\":[";bool comma=false;for(uint32_t gen:{0xffffffffu,999999999u,4294967294u}){st.b.generation=gen;for(auto&slot:st.b.slot)mrfw::preset_slot_put(slot,true,false,text.data(),text.size());cat.begin();Capture reset;mrfw::preset_verb(cat,diag,"preset reset all",16,reset);std::cout<<(comma?",":"")<<"{\"before_generation\":"<<gen<<",\"after_generation\":"<<cat.generation()<<",\"bytes\":"<<reset.all().size()<<",\"lines\":"<<reset.lines.size()<<"}";comma=true;}std::cout<<"]}\n";
}
