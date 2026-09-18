from pathlib import Path
import importlib.util,json,subprocess,hashlib
q=Path('/tmp/mr-codex-s8ac-preflight-active').read_text().strip();q=Path(q);r=q/'snapshot';o=q/'conversion';o.mkdir(exist_ok=True)
src=r'''// LABELLED SYNTHETIC preflight: real target helper included in this TU; no production hook/edit.
#include "remote_session.cpp"
#include <cstdio>
#include <cstring>
using namespace meshroute;
int main(){
 unsigned checks=0,fail=0;
 auto ck=[&](bool p,const char*n){++checks;if(!p){++fail;std::printf("FAIL %s\n",n);}};
 uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=static_cast<uint8_t>(i+1);
 Identity id{};identity_from_seed(id,seed);RemoteSessionState s{};
 std::memcpy(s.admin_x_secret,id.x_secret,32);std::memcpy(s.admin_ed_pub,id.ed_pub,32);
 uint8_t out[32],x[32],shared[32];
 uint8_t valid[32];std::memcpy(valid,id.ed_pub,32);
 std::memset(out,0xA5,32);ck(derive_base(s,valid,out)==RemoteStatus::ok,"valid identity accepted");
 uint8_t low[32]={};std::memset(out,0xA5,32);
 ck(derive_base(s,low,out)==RemoteStatus::bad_key,"low-order input refused");
 for(auto b:out)ck(b==0xA5,"low-order refusal preserves output");
 for(uint8_t y : {2,7,8,11}){
  uint8_t bad[32]={};bad[0]=y;ed_pub_to_x25519(x,bad);
  auto primitive=remote_ecdh_shared(shared,id,x);std::memset(out,0xA5,32);auto target=derive_base(s,bad,out);
  ck(primitive==RemoteStatus::ok,"off-curve Ed encoding produces accepted nonzero X25519 secret");
  ck(target==RemoteStatus::ok,"real target derive_base accepts off-curve Ed encoding");
  bool changed=false;for(auto b:out)changed|=b!=0xA5;ck(changed,"real target publishes base key");
  std::printf("off_curve_y=%u remote_ecdh_shared=%u target_derive_base=%u publishes_key=%u\n",y,static_cast<unsigned>(primitive),static_cast<unsigned>(target),changed);
 }
 std::printf("preflight characterization: %u checks, %u failed\n",checks,fail);return fail?1:0;
}
'''
(o/'proof.cpp').write_text(src)
p=2**255-19;d=(-121665*pow(121666,p-2,p))%p;math=[]
for y in [2,7,8,11]:
 rhs=((y*y-1)*pow((d*y*y+1)%p,p-2,p))%p;legendre=pow(rhs,(p-1)//2,p);assert legendre==p-1
 math.append(dict(y=y,encoding=y.to_bytes(32,'little').hex(),x_squared=rhs,legendre=legendre,off_curve=True))
(o/'independent-point-check.json').write_text(json.dumps(dict(field_prime=p,edwards_d=d,cases=math),indent=2)+'\n')
sp=importlib.util.spec_from_file_location('abi8conv',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(sp);sp.loader.exec_module(abi);data=abi.idedata('native');obj=o/'proof.o';cmd=abi.compile_command(data,o/'proof.cpp',obj)
# Discard uncalled target-state functions at standalone link; called helper is the unchanged production definition.
cmd+=['-ffunction-sections','-fdata-sections'];steps=[]
def run(name,c):
 p=subprocess.run(c,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('conversion-'+name+'.log')).write_bytes(p.stdout);steps.append(dict(name=name,command=c,exit=p.returncode));(o/'commands.json').write_text(json.dumps(steps,indent=2)+'\n');print(name,p.returncode,p.stdout.decode()[-2000:],flush=True);return p.returncode
if run('compile',cmd):raise SystemExit(1)
objs=[r/'.pio/build/native/lib676/core'/f for f in ['identity.o','remote_codec.o','dm_crypto.o','frame_codec.o']]+[r/'.pio/build/native/lib53d/monocypher/monocypher.o']
cmd=[data['cxx_path'],'-Wl,--gc-sections',str(obj),*[str(p) for p in objs],'-o',str(o/'proof')]
if run('link',cmd):raise SystemExit(1)
raise SystemExit(run('run',[str(o/'proof')]))
