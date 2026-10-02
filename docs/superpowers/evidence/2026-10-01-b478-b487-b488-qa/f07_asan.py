from pathlib import Path
import subprocess,shutil,ast,json,os,hashlib
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-01-b478-b487-b488-qa';D=Path('/tmp/mr-tool-qa-059bo2lh/f07-asan');D.mkdir(exist_ok=True)
source=(R/'tools/probe_ui_model_mutations.py').read_text();n=next(n for n in ast.parse(source).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='MUTS_W4AIDENT' for t in n.targets));entry=ast.literal_eval(n.value)[6];_,old,new=entry
main='''#include "firmware_ui_status.h"
#include "firmware_ui_send.h"
#include <cstring>
int main(int argc, char**) {
 const char name[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
 char out[64]{};
 if(argc==1) { mrui::UiSnapshot s{}; std::memcpy(s.own_name,name,32);s.own_name_len=32;mrui::ui_home_me_line(out,sizeof out,s); }
 else { mrui::SendReq b{};b.kind=mrui::SendKind::dm;b.peer_known=true;b.peer_hash=0x12345678;b.peer_id=11;mrui::ui_review_header(out,sizeof out,b,name,32); }
 return out[0]==0;
}
'''
records=[]
for arm in ['clean','F07']:
 d=D/arm;d.mkdir(exist_ok=True)
 for p in (R/'src').glob('*.h'):shutil.copyfile(p,d/p.name)
 p=d/'firmware_ui_model.h';s=p.read_text();assert s.count(old)==1
 if arm=='F07':p.write_text(s.replace(old,new))
 (d/'main.cpp').write_text(main)
 cmd=['g++','-std=c++20','-g','-O0','-fsanitize=address','-fno-omit-frame-pointer','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2','-I'+str(d),*[ '-I'+str(R/x) for x in ['lib/core','lib/hal','lib/console','lib/monocypher/src']],str(d/'main.cpp'),'-o',str(d/'check')]
 r=subprocess.run(cmd,capture_output=True);(O/f'asan-{arm}-build.log').write_bytes(r.stdout+r.stderr);assert r.returncode==0,r.stderr.decode()
 for mode in ['home','review']:
  r=subprocess.run([str(d/'check')]+(['review'] if mode=='review' else []),env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True)
  data=r.stdout+r.stderr;(O/f'asan-{arm}-{mode}.log').write_bytes(data)
  records.append({'arm':arm,'mode':mode,'build_command':cmd,'exit':r.returncode,'stack_buffer_overflow':b'AddressSanitizer: stack-buffer-overflow' in data,'diagnostic':data.decode('utf8','backslashreplace')[:4800]})
(O/'f07-asan.json').write_text(json.dumps(records,indent=2)+'\n');print([(r['arm'],r['mode'],r['exit'],r['stack_buffer_overflow']) for r in records])
