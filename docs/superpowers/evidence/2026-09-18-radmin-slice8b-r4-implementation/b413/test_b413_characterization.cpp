// Labelled counter characterization: synthetic pending rows and opaque RPC bodies;
// real registered Node carrier, queue, E2E ring, timeout producer and firmware observer.
#include "doctest.h"
#include <cstdlib>
#include "node.h"
#include "frame_codec.h"
#include "support/test_hal.h"
#include "../src/firmware_remote_client.h"
using namespace meshroute;
TEST_CASE("B413 known target bindings bypass wrapper and alias remote ACK correlations") {
    struct H : mrtest::TestHalBase { void emit(const char*,const EventField*,size_t) override {} } hal;Node node(hal,200,0xc0ffee01);NodeConfig cfg{};cfg.is_mobile=true;cfg.leaf_id=2;
    CHECK(node.on_init(cfg));node.test_set_my_mobile_reg(1,200);node.test_learn_route(1,1,1,40,false);
    const bool authoritative=std::getenv("MR_B413_SOFT_BINDINGS")==nullptr;
    CHECK(node.test_id_bind_set(2,0x22222222,authoritative));CHECK(node.test_id_bind_set(3,0x33333333,authoritative));
    node.test_suspend_tx_drain(true);hal._now=1000;
    NodeRadminClientCarrier carrier(node);auto& state=node.remote_client();
    for(unsigned i=0;i<2;++i) {
        auto& p=state.pending[i].core;p.request_id=i+1;p.state=uint8_t(RemoteClientPhase::response_wait);
        p.outcome_deadline_ms=301000;p.next_retry_ms=61000;
        p.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;p.route.target_hash=i?0x33333333:0x22222222;
        uint8_t opaque[31]{};opaque[0]=remote_ctl(uint8_t(RemoteCmdOpcode::auth_execute),0);opaque[1]=i+1;
        CHECK(carrier.submit_request(p.route,{true,0xc0ffee01},remote_client_carrier(p.route,DATA_TYPE_REMOTE_CMD,1),opaque,true)==RemoteClientSend::queued);
        CHECK(node.test_tx_type(i)==DATA_TYPE_REMOTE_CMD);CHECK(node.test_tx_dst(i)==i+2);
        CHECK((node.test_tx_flags(i)&DATA_FLAG_E2E_ACK_REQ)!=0);
        CHECK(p.carrier_ctr==1);
    }
    CHECK(node.test_tx_queue_n()==2);CHECK(node.remote_client_correlation_free()==6);
    CHECK(state.pending[0].core.carrier_ctr==state.pending[1].core.carrier_ctr);
    CHECK(remote_client_observe_ack(state,state.pending[1].core.carrier_ctr,true)==1);
    hal._now=60999;node.on_timer(90);CHECK(node.remote_client_correlation_free()==6);
    hal._now=61000;node.on_timer(90);CHECK(node.remote_client_correlation_free()==8);
    struct Sink {
        uint64_t id=0;unsigned calls=0;
        bool carrier(RemoteLocalTransport,uint64_t request,const char*,uint16_t,const mrfw::CarrierDetail*) {id=request;++calls;return true;}
    } sink;
    Push p{};unsigned timeouts=0;bool wrong=false;
    while(node.next_push(p))if(p.kind==PushKind::send_failed && p.reason==SendFailReason::e2e_ack_timeout) {
        ++timeouts;CHECK(p.ctr==1);mrfw::remote_client_observe_push(state,p,sink);
        CHECK(sink.id==1);if(p.dst==3)wrong=sink.id!=state.pending[1].core.request_id;
    }
    CHECK(timeouts==2);CHECK(sink.calls==2);CHECK(wrong);
}
