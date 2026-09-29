// QA reproduction of the pre-existing alarm-only native fixture; real model, no production edit.
#include "firmware_ui_model.h"
#include <cstdio>
using namespace mrui;
int main() {
 UiSnapshot s{}; s.now_ms=1000; s.team_shown=3; s.team_total=3; s.unread_dm=2; s.unread_ch=5; s.batt_mv=3900;
 for(unsigned i=0;i<3;++i){s.team[i].id=10+i;s.team[i].last_heard_s=60;}
 mrnv::UiPresetBlob cat{};mrfw::preset_defaults(cat);ui_snapshot_publish_presets(s,cat);
 UiModel m;SendReq req{};
 m.on_gesture(Gesture::long_arm,s);m.on_gesture(Gesture::long_fire,s);
 for(int i=0;i<3;++i)m.on_gesture(Gesture::short_press,s);
 m.on_gesture(Gesture::double_press,s);m.on_gesture(Gesture::double_press,s);
 const bool no_compose=m.state().compose==Compose::none;
 const bool first=m.take_send_request(req);const bool alarm=first&&req.kind==SendKind::emergency;
 const bool ordinary=m.take_send_request(req);
 std::printf("compose_none=%d emergency_first=%d ordinary_request=%d\n",no_compose,alarm,ordinary);
 return !(no_compose&&alarm&&!ordinary);
}
