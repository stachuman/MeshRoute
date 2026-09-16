from pathlib import Path
import subprocess,hashlib,json
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-codex-s7b3p1-preflight-active').read_text().strip();q=Path(q)
s=(r/'src/firmware_commands.cpp').read_text();a=s.index('static void handle_factory_reset(');b=s.index('\n}\n',a)+3;body=s[a:b]
header=r'''#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#define F(x) x
std::vector<std::string> events;
struct Print { void println(const char* s) { events.push_back(std::string("print:")+s+"\n"); } };
struct Inbox { const char* name; bool ok; bool wipe(){events.push_back(name);return ok;} };
Inbox g_inbox_dm{"wipe-dm",true},g_inbox_ch{"wipe-ch",true};
namespace mrnv { bool ok; bool factory_erase(){events.push_back("erase-nv");return ok;} }
void fw_reboot(){events.push_back("reboot");}
'''
main=r'''
int main(){
 Print out;unsigned cases=0;
 for(unsigned mask=0;mask<8;mask++){
  g_inbox_dm.ok=mask&1;g_inbox_ch.ok=mask&2;mrnv::ok=mask&4;events.clear();
  handle_factory_reset("confirm",7,out);
  std::vector<std::string> expected{
   "print:> factory reset — erasing all NV, rebooting…\n","wipe-dm","wipe-ch"};
  if(!g_inbox_dm.ok||!g_inbox_ch.ok) expected.push_back("print:> factory_reset WARN: inbox erase incomplete (messages may remain on flash)\n");
  expected.push_back("erase-nv");
  if(!mrnv::ok)expected.push_back("print:> factory_reset WARN: an NV slot did not erase (boot re-defaults it)\n");
  expected.push_back("reboot");
  if(events!=expected){std::cerr<<"ORDER/BYTES assertion failed, case "<<mask<<"\n";return 1;}
  cases++;
 }
 std::cout<<cases<<" factory-reset failure combinations: exact output/order PASS\n";
}
'''
(q/'factory-body.cpp.txt').write_text(body);results=[]
for label,content in [('baseline',body),('inbox-warning-after-nv',body.replace('        if (!dm_ok || !ch_ok) out.println(F("> factory_reset WARN: inbox erase incomplete (messages may remain on flash)"));\n        if (!mrnv::factory_erase()) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));','        if (!mrnv::factory_erase()) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));\n        if (!dm_ok || !ch_ok) out.println(F("> factory_reset WARN: inbox erase incomplete (messages may remain on flash)"));'))]:
 assert label=='baseline' or content!=body
 cpp=q/(label+'.cpp');binary=q/label;cpp.write_text(header+content+main)
 compile_cmd=['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)]
 c=subprocess.run(compile_cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/(label+'-compile.log')).write_bytes(c.stdout);assert c.returncode==0,c.stdout
 p=subprocess.run([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/(label+'.log')).write_bytes(p.stdout)
 assert p.returncode==(0 if label=='baseline' else 1)
 results.append({'name':label,'compile_command':compile_cmd,'compile_exit':c.returncode,'run_exit':p.returncode,'output':p.stdout.decode(),'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest()})
result={'source_file':'src/firmware_commands.cpp','source_sha256':hashlib.sha256((r/'src/firmware_commands.cpp').read_bytes()).hexdigest(),'first_line':s[:a].count('\n')+1,'last_line':s[:b].count('\n'),'extracted_production_function':True,'whole_TU_or_real_router':False,'hardware_or_NV_store_proof':False,'cases':8,'control':'Private extracted-body reorder; compiles and fails assertion; no production mutation','results':results}
(q/'factory-order-proof.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
