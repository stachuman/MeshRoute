// Canonical eight-torsion inputs are derived independently by low-order-reference.py.
#include "remote_client.h"
#include <cstdio>
#include <cstring>
using namespace meshroute;
int main(){
 const char* points[]={"0100000000000000000000000000000000000000000000000000000000000000",
"c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
"0000000000000000000000000000000000000000000000000000000000000080",
"26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
"ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
"26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85",
"0000000000000000000000000000000000000000000000000000000000000000",
"c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa"}; unsigned checks=0,failed=0;
 auto check=[&](bool ok){++checks;if(!ok)++failed;};
 uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=i+1;
 Identity self{};identity_from_seed(self,seed);
 for(const auto* hex:points){uint8_t peer[32],out[32];memset(out,0xa5,sizeof out);
  for(unsigned i=0;i<32;++i){unsigned byte=0;std::sscanf(hex+2*i,"%2x",&byte);peer[i]=byte;}
  check(remote_client_base(out,self,peer)==RemoteStatus::bad_key);
  for(auto b:out)check(b==0xa5);
 }
 printf("canonical low-order peers: 8; %u checks / %u failed\n",checks,failed);return failed?1:0;
}
