// LABELLED SYNTHETIC preflight: real target helper included in this TU; no production hook/edit.
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
