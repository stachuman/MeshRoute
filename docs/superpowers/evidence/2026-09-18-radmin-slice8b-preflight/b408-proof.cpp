// Synthetic pressure fixture, unchanged real Node ring and carrier producers.
#include "node.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "support/test_hal.h"
#include <cstdio>
#include <cstring>
namespace meshroute {
struct E2eAckTestAccess {
 static void arm(Node& n,uint16_t ctr){n.e2e_ack_arm(0,true,1,ctr,protocol::e2e_ack_deadline_xl_ms);}
 static unsigned used(const Node& n){unsigned z=0;for(const auto& e:n._pending_e2e_acks)z+=e.used;return z;}
 static bool full(const Node& n){return n.e2e_ack_ring_full();}
 static bool tracks(const Node& n,uint16_t ctr){for(const auto& e:n._pending_e2e_acks)if(e.used && e.ctr==ctr)return true;return false;}
 static Node::SendDispatch same(Node& n){const uint8_t body[]={0x03,2,0,0,0,0,0,0,0};Node::SendDispatch dsp{};(void)n.send_by_hash(0x22222222,body,sizeof body,DATA_FLAG_E2E_ACK_REQ,CryptIntent::off,0,0,Plane::GLOBAL,DATA_TYPE_REMOTE_CMD,true,&dsp);return dsp;}
 static uint16_t xl(Node& n){const uint8_t hops[]={2},body[]={0x03,1,0,0,0,0,0,0,0};return n.delegate_send_layer(0x22222222,hops,1,DATA_TYPE_REMOTE_CMD,body,sizeof body,DATA_FLAG_E2E_ACK_REQ);}
};
}
struct H:mrtest::TestHalBase {unsigned untracked=0;void emit(const char* k,const meshroute::EventField*,size_t) override {if(!strcmp(k,"e2e_ack_untracked_ring_full"))++untracked;}};
int main(){using namespace meshroute;unsigned checks=0,failed=0;auto ck=[&](bool ok,const char* name){++checks;if(!ok){++failed;printf("FAIL %s\n",name);}};H h;Node n(h,17,0x11111111);NodeConfig cfg;cfg.is_mobile=true;cfg.routing_sf=8;cfg.allowed_sf_bitmap=1u<<8;cfg.leaf_id=1;ck(n.on_init(cfg),"init");n.test_set_my_mobile_reg(1,17);ck(n.mobile_registered(),"registered");ck(n.remote_client_correlation_free()==8,"initial advertised free");ck(!E2eAckTestAccess::full(n),"initial ACK capacity");
for(unsigned i=0;i<8;++i){E2eAckTestAccess::arm(n,100+i);ck(E2eAckTestAccess::used(n)==i+1,"real ring armed");ck(n.remote_client_correlation_free()==8,"wrong ring stays eight");}
ck(E2eAckTestAccess::full(n),"real ring full");ck(n.test_deleg_ack_live_n()==0,"home ring remains empty");
const uint16_t xl=E2eAckTestAccess::xl(n);ck(xl!=0,"XL producer admits with full ring");ck(!E2eAckTestAccess::tracks(n,xl),"XL admission untracked");ck(h.untracked==1,"XL full telemetry");
const auto dsp=E2eAckTestAccess::same(n);ck(dsp.admit==Node::SendDispatch::Admit::queued,"same-layer producer admits with full ring");ck(dsp.ctr!=0,"same-layer ctr minted");ck(!E2eAckTestAccess::tracks(n,dsp.ctr),"same-layer admission untracked");ck(h.untracked==2,"same-layer full telemetry");ck(E2eAckTestAccess::used(n)==8,"no ring eviction");
printf("B408: %u checks / %u failed; advertised free=%u, real free=%u; two producer admissions untracked\n",checks,failed,n.remote_client_correlation_free(),8-E2eAckTestAccess::used(n));return failed?1:0;}
