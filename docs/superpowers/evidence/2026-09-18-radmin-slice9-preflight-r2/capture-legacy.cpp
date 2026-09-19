#include "admin_auth.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    using namespace meshroute;
    unsigned char a[32], n[32];
    for (int i=0;i<32;++i) { a[i]=0x11+i; n[i]=0x71+i; }
    Identity admin{}, node{};
    identity_from_seed(admin,a); identity_from_seed(node,n);
    const unsigned char cmd[6]={'s','t','a','t','u','s'};
    const unsigned char rand8[8]={0xde,0xad,0xbe,0xef,1,2,3,4};
    unsigned char frame[128]{}, pt[64]{};
    const auto len=admin_cmd_seal(frame,sizeof frame,admin,node.ed_pub,node.key_hash32,7,cmd,sizeof cmd,rand8,1);
    assert(len==40);
    AdminCmd decoded{};
    assert(admin_cmd_open(frame,len,admin.ed_pub,node,decoded,pt,sizeof pt));
    assert(decoded.node_key_hash==node.key_hash32);
    assert(decoded.counter==7);
    assert(decoded.cmd_len==sizeof cmd);
    assert(std::memcmp(decoded.cmd,cmd,sizeof cmd)==0);
    assert(frame[0]==rand8[0]);
    printf("checks=7 length=%zu body=",len);
    for (size_t i=0;i<len;++i) printf("%02x",frame[i]);
    printf("\n");
}
