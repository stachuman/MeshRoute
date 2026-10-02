"""QA feasibility measurement, not an implementation or native/full gate.
Copies current headers to /tmp; mutates only that copy. Real formatter calls.
"""
from pathlib import Path
import hashlib,json,shutil,subprocess,tempfile
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-return'
D=Path(tempfile.mkdtemp(prefix='mr-qa-bounded-f07-'))
old='        const uint8_t keep = (len <= cols) ? len : uint8_t(cols - 1);'
new='        const uint8_t keep = (len <= cols) ? len : uint8_t(cols - (cap > std::size_t(cols) + 1u ? 0u : 1u));'
main=r'''#include "firmware_ui_model.h"
#include <cstdio>
#include <cstring>
int main() {
 const char name[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
 unsigned calls=0, outside=0, no_nul=0;
 for(unsigned cols=0;cols<=32;++cols) for(unsigned len=0;len<=32;++len) for(unsigned cap=0;cap<=48;++cap) {
  char bytes[52]; std::memset(bytes,0x5a,sizeof bytes);
  (void)mrui::ui_fmt_identity(bytes+2,cap,name,len,0,cols); ++calls;
  bool changed=bytes[0]!=0x5a || bytes[1]!=0x5a;
  for(unsigned i=2+cap;i<sizeof bytes;++i) changed=changed || bytes[i]!=0x5a;
  outside+=changed;
  if(cap && !std::memchr(bytes+2,0,cap)) ++no_nul;
 }
 char roomy[48]; mrui::ui_fmt_identity(roomy,sizeof roomy,name,32,0,6);
 const bool witness=std::strcmp(roomy,"ABCDE" "\xBB")==0;
 unsigned tight_ok=0;
 for(unsigned cols: {6u,7u,16u}) {
  char out[48]; mrui::ui_fmt_identity(out,cols+1,name,32,0,cols);
  tight_ok+=std::strlen(out)==cols && static_cast<unsigned char>(out[cols-1])==0xbb;
 }
 std::printf("{\"calls\":%u,\"outside_declared_capacity\":%u,\"missing_nul\":%u,\"existing_roomy_budget_witness_passes\":%s,\"roomy_length\":%zu,\"tight_examples_unchanged\":%u}\n",calls,outside,no_nul,witness?"true":"false",std::strlen(roomy),tight_ok);
 return (outside || no_nul) ? 2 : 0;
}
'''
rows=[]
for arm in ['clean','bounded-proposal']:
 d=D/arm;d.mkdir()
 for p in (R/'src').glob('*.h'):shutil.copy2(p,d/p.name)
 p=d/'firmware_ui_model.h';data=p.read_text();assert data.count(old)==1
 if arm!='clean':p.write_text(data.replace(old,new))
 (d/'main.cpp').write_text(main)
 cmd=['g++','-std=c++20','-O1','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2','-I'+str(d),*['-I'+str(R/x) for x in ['lib/core','lib/hal','lib/console','lib/monocypher/src']],str(d/'main.cpp'),'-o',str(d/'check')]
 built=subprocess.run(cmd,capture_output=True);(O/f'bounded-{arm}-build.log').write_bytes(built.stdout+built.stderr)
 assert built.returncode==0,built.stderr.decode('utf8','backslashreplace')
 run=subprocess.run([str(d/'check')],capture_output=True);assert run.returncode==0,run.stderr
 result=json.loads(run.stdout);rows.append({'arm':arm,'exit':run.returncode,'result':result,'command':cmd,'model_sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
assert rows[0]['result']['existing_roomy_budget_witness_passes'] and not rows[1]['result']['existing_roomy_budget_witness_passes']
result={'diagnostic_only':True,'production_and_native_tests_unchanged':True,'scratch':str(D),'old':old,'proposed':new,'scope':'name lengths 0..32, column budgets 0..32, capacities 0..48; logical-capacity sentinels and NUL, plus current test_firmware_ui_model.cpp:2756 witness','rows':rows}
(O/'bounded-f07.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps([r['result'] for r in rows],indent=2))
