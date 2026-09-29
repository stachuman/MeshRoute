from pathlib import Path
import json,subprocess,tempfile,hashlib
R=Path('/home/staszek/MeshRoute');E=R/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-review';P=R/'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck';D=Path(tempfile.mkdtemp(prefix='w6-brief-review-'))
q=r'''
#include "/home/staszek/MeshRoute/tools/probe_inbox_verbs/fakes/Preferences.h"
#include <cstdio>
int main(){unsigned char b[2852]{};for(size_t n: {size_t(2304),size_t(2305),size_t(2852)}){auto&nv=mrprobe_nv();nv.reset();nv.ns_present=true;nv.rw_ok=true;Preferences p;bool opened=p.begin("mr",false);size_t wrote=p.putBytes("ui",b,n);printf("requested=%zu opened=%d reported=%zu present=%d stored_len=%zu holds_exact=%d\n",n,opened,wrote,p.isKey("ui"),p.getBytesLength("ui"),nv.holds("mr","ui",b,n));}}
'''.replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>')
(D/'fake.cpp').write_text(q);(E/'fake-query.cpp').write_text(q)
cmd=['g++','-std=c++20','-O2',str(D/'fake.cpp'),'-o',str(D/'fake')]
p=subprocess.run(cmd,capture_output=True,text=True);assert p.returncode==0,p.stderr
r=subprocess.run([str(D/'fake')],capture_output=True,text=True,check=True);(E/'fake-result.txt').write_text(r.stdout)
old=json.loads((P/'reply-measure.json').read_text());scratch=Path(old['scratch'])/'src'
assert all(hashlib.sha256((scratch.parent/path).read_bytes()).hexdigest()==h for path,h in old['modified_scratch_headers'].items())
source=(P/'reply-query.cpp').read_text();prefix=source[:source.index('int main(){')]
body=r'''
int main(){Store st;Gate gate;mrfw::PresetDiag diag;mrfw::preset_defaults(st.b);st.b.generation=0xffffffff;const std::string text(163,'X');for(auto&slot:st.b.slot)mrfw::preset_slot_put(slot,true,false,text.data(),text.size());mrfw::PresetCatalog cat(st,gate);cat.begin();Capture full;mrfw::preset_emit_list(cat,full);
std::cout<<"{\"pages\":[";
for(int page=1;page<=5;++page){std::string end=full.lines.back();auto pos=end.rfind('}');end.insert(pos,",\"page\":"+std::to_string(page)+",\"pages\":5");std::string expected;Serial.reset(0);mrcon_detail::GuardedConsole sink;PresetPrintLines out(sink);int count=0;
for(int i=(page-1)*4;i<std::min(page*4,17);++i){mrfw::preset_emit_record(cat,i,out);expected+=full.lines[i];++count;}out.line(end.data(),end.size());expected+=end;auto drops=sink.dropped_lines();auto immediate=Serial.wire.size();Serial.cap=256;Serial.auto_drain=256;for(int i=0;i<50;++i)sink.service();
std::cout<<(page>1?",":"")<<"{\"page\":"<<page<<",\"records\":"<<count<<",\"end_bytes\":"<<end.size()<<",\"bytes\":"<<expected.size()<<",\"dropped\":"<<drops<<",\"immediate_bytes\":"<<immediate<<",\"exact_after_drain\":"<<(Serial.wire==expected?"true":"false")<<"}";
}
std::cout<<"],\"reset_all\":[";bool comma=false;for(uint32_t gen:{0xffffffffu,999999999u,4294967294u}){st.b.generation=gen;for(auto&slot:st.b.slot)mrfw::preset_slot_put(slot,true,false,text.data(),text.size());cat.begin();Capture reset;mrfw::preset_verb(cat,diag,"preset reset all",16,reset);std::cout<<(comma?",":"")<<"{\"before_generation\":"<<gen<<",\"after_generation\":"<<cat.generation()<<",\"bytes\":"<<reset.all().size()<<",\"lines\":"<<reset.lines.size()<<"}";comma=true;}std::cout<<"]}\n";
}
'''
(D/'page.cpp').write_text(prefix+body);(E/'page-query.cpp').write_text(prefix+body)
cmd2=old['command'][:];cmd2[cmd2.index(old['command'][-4]) if False else 0]='g++'
cmd2=[str(D/'page.cpp') if x.endswith('/query.cpp') else str(D/'page') if i==len(cmd2)-1 else x for i,x in enumerate(cmd2)]
p=subprocess.run(cmd2,capture_output=True,text=True);assert p.returncode==0,p.stderr
r2=subprocess.run([str(D/'page')],capture_output=True,text=True,check=True);v=json.loads(r2.stdout)
assert [x['bytes'] for x in v['pages']]==[1083,1077,1092,1097,371]
assert all(x['dropped']==0 and x['immediate_bytes']==0 and x['exact_after_drain'] for x in v['pages'])
assert [x['bytes'] for x in v['reset_all']]==[1508,1517,1517]
out={'scope':'Disposable measurement. Unmodified production Preferences fake; page grouping and appended page metadata are synthetic adapters over the precheck real-writer overlay, not a W6 implementation or router test. Existing overlay hashes verified.', 'scratch':str(D),'fake_command':cmd,'fake_sha256':hashlib.sha256((R/'tools/probe_inbox_verbs/fakes/Preferences.h').read_bytes()).hexdigest(),'fake_result':r.stdout,'page_command':cmd2,'page_result':v}
(E/'measurements.json').write_text(json.dumps(out,indent=2)+'\n');print(r.stdout);print(r2.stdout)
