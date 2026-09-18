
// Existing writer/sink characterization at the brief's prescribed placement.
// The remote event is synthetic: the observer has not been implemented.
#define ARDUINO 100 // expose only the real Print adapter in this host characterization
#include "dispatch_sink.h"
#undef ARDUINO
#include "console_json.h"
#include <vector>
#include <string>
#include <cstdio>
static std::vector<std::string> lines;
static void ble_capture(const char* s,size_t n){lines.emplace_back(s,n);}
int main(int argc,char**) {
 unsigned checks=0,failed=0;auto ck=[&](bool b,const char* s){++checks;if(!b){++failed;std::printf("FAIL %s\n",s);}};
 LineSink sink(ble_capture);char event[245]{},push[512]{};
 using meshroute::EventField; EventField fields[]={EF_S("id","0000000000000064"),EF_S("event","acked"),EF_I("ctr",42)};
 auto n=meshroute::console::write_event(event,sizeof event,"remote_carrier",fields,3);
 meshroute::Push pu{};pu.kind=meshroute::PushKind::send_e2e_acked;pu.ctr=42;pu.dst=2;
 auto m=meshroute::console::write_push(push,sizeof push,pu);
 ck(n>0 && n<=244,"remote event bounded");ck(event[n-1]=='\n',"writer terminates event");ck(m>0,"real push writer");
 if(argc>1)ble_capture(push,m); // proposed after-fanout placement: negative control for BEFORE ordering
 ck(sink.write(reinterpret_cast<const uint8_t*>(event),n)==n,"sink accepts event");
 ck(lines.size()==1,"newline flush is synchronous before explicit flush");
 if(argc==1)ble_capture(push,m);
 ck(lines.size()==2,"both existing and remote events delivered");
 ck(lines[0].find("remote_carrier")!=std::string::npos,"pinned placement sends controller event FIRST");
 ck(lines[1].find("remote_carrier")==std::string::npos,"existing JSON push follows controller event");
 sink.flush();ck(lines.size()==2,"later flush cannot reorder completed lines");
 for(auto& s:lines)std::printf("%s",s.c_str());
 std::printf("fanout characterization: %u checks / %u failed\n",checks,failed);return failed?1:0;
}
