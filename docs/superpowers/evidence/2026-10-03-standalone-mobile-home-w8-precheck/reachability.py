"""Read-only witness through current UiModel; no future W8 implementation."""
from pathlib import Path
import tempfile,subprocess,json
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;D=Path(tempfile.mkdtemp(prefix='w8-reach-'))
src=r'''
#include "firmware_ui_model.h"
#include <cstdio>
int main() {
  using namespace mrui;
  UiSnapshot s{}; s.now_ms=1; s.team_build=true; s.team_id=0x12a1b2c3;
  s.team_key_present=true; s.my_team_id=0;
  mrnv::UiPresetBlob cat{}; mrfw::preset_defaults(cat); ui_snapshot_publish_presets(s,cat);
  UiModel m; m.on_tick(s);
  unsigned checks=0;
  auto check=[&](bool b,const char* why) { ++checks; std::printf("%s %s\n",b?"OK":"FAIL",why); return b; };
  if(!check(home_profile(s)==HomeProfile::id_pending,"Home ID-pending profile"))return 1;
  bool home_send=false; for(uint8_t i=0;i<m.state().home.count;++i)home_send|=m.state().home.items[i]==HomeItem::send;
  if(!check(!home_send,"Home hides SEND TO TEAM"))return 1;
  auto press=[&](Gesture g) { ++s.now_ms;m.on_gesture(g,s);m.on_tick(s); };
  while(m.state().home.selected!=HomeItem::menu)press(Gesture::short_press);
  press(Gesture::double_press);
  if(!check(m.state().list_view==ListView::passive && m.state().screen==Screen::status,"MENU enters Home rail"))return 1;
  for(unsigned i=0;i<3;++i)press(Gesture::short_press);
  if(!check(m.state().screen==Screen::send,"Rail reaches SEND before team-local ID"))return 1;
  press(Gesture::double_press);
  if(!check(m.state().compose==Compose::channel && m.state().list_view==ListView::interactive,"Double enters team phrase list before team-local ID"))return 1;
  std::printf("checks=%u team_local_id=%u catalog_rows=%u\n",checks,unsigned(s.my_team_id),unsigned(s.preset_ch.n));
}
'''
(D/'witness.cpp').write_text(src)
cmd=['g++','-std=c++20','-O2','-I'+str(R/'src'),'-I'+str(R/'lib/core'),'-I'+str(R/'lib/console'),'-I'+str(R/'lib/crypto'),'-I'+str(R/'lib/hal'),'-I'+str(R/'lib/radio'),'-I'+str(R/'lib/monocypher/src'),str(D/'witness.cpp'),'-o',str(D/'witness')]
b=subprocess.run(cmd,cwd=R,stdout=subprocess.PIPE,stderr=subprocess.PIPE);(O/'reach-build.log').write_bytes(b.stdout+b.stderr)
if b.returncode: raise SystemExit(b.returncode)
r=subprocess.run([str(D/'witness')],cwd=R,stdout=subprocess.PIPE,stderr=subprocess.PIPE);(O/'reach.log').write_bytes(r.stdout+r.stderr)
assert r.returncode==0
rep='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,?!-';assert len(rep)==42 and '"' not in rep and '\\' not in rep
forms={k:(fmt.format(n=255,text='A'*163)) for k,fmt in {'dm':'send {n} "{text}" -t -a','channel':'send_channel {n} "{text}" -t -e'}.items()}
(O/'reachability.json').write_text(json.dumps(dict(scratch=str(D),compile_argv=cmd,source=src,result=r.stdout.decode(),repertoire=rep,line_bounds={k:dict(bytes=len(v),including_nul=len(v)+1,line=v,contains_location=' -l' in v)for k,v in forms.items()},static_line_cap=199),indent=2)+'\n')
print(r.stdout.decode(),{k:len(v)+1 for k,v in forms.items()})
