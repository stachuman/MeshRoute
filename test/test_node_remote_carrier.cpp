// Slice 8b: real registered-mobile wrapper -> home -> target -> last mile.
#include "doctest.h"
#include "node.h"
#include "frame_codec.h"
#include "identity.h"
#include "remote_client.h"
#include "../src/firmware_remote_client.h"
#include "support/test_hal.h"
#include <cstring>
#include <string>
#include <vector>
using namespace meshroute;
#include "support/remote_carrier_chain.h"
namespace {
Identity carrier_identity(uint8_t tag) {
    Identity id{};uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=tag+i;
    identity_from_seed(id,seed);return id;
}
struct CarrierSink : IRemoteLocalDelivery {
    std::string text,result;unsigned terminals=0;uint32_t detail=0;
    bool connected(RemoteLocalTransport)const override{return true;}
    bool output(RemoteLocalTransport,uint64_t,uint16_t& seq,std::span<const uint8_t> b)override{++seq;text.append(reinterpret_cast<const char*>(b.data()),b.size());return true;}
    bool terminal(RemoteLocalTransport,uint64_t,const char* s,uint32_t d,bool)override{result=s;detail=d;++terminals;return true;}
    void retained(RemoteLocalTransport,uint64_t)override{}
};
// Labelled re-entrant HAL fault: fill TX only after the carrier's capacity pre-check.
// This is not a concurrency model; it exposes the checked SendDispatch::refused mapping.
struct PressureHal : GHal {
    Node* fill_on_now=nullptr;
    uint64_t now() override {
        auto* node=fill_on_now;fill_on_now=nullptr;
        if(node) { node->test_suspend_tx_drain(true);const uint8_t noise=0;
            while(!node->tx_queue_full())(void)node->test_do_send_typed(1,&noise,1,CryptIntent::off,0,DATA_TYPE_REMOTE_RESP); }
        return GHal::now();
    }
};
struct CarrierChain {
    GPair pair;
    PressureHal hm;
    static constexpr uint8_t local=200;
    static constexpr uint32_t mobile_hash=0xc0ffee01;
    Node mobile{hm,local,mobile_hash};
    Identity controller=carrier_identity(10),admin=carrier_identity(80);
    NodeRadminClientCarrier carrier{mobile};
    CarrierSink sink;
    uint64_t entropy=100;
    uint64_t id=0;
    unsigned executions=0;
    static bool random(void* p,uint8_t* b,size_t n){auto& x=*static_cast<uint64_t*>(p);++x;for(size_t i=0;i<n;++i)b[i]=uint8_t(x>>(i*8));return true;}
    CarrierChain(bool registered=true) {
        auto cfg=g_cfg();cfg.is_mobile=true;CHECK(mobile.on_init(cfg));
        if(registered)mobile.test_set_my_mobile_reg(1,local);
        mobile.test_learn_route(1,1,1,40,false);pair.n1.test_learn_route(local,local,1,40,false);
        pair.n1.test_add_host_mobile(mobile_hash,local,controller.ed_pub);
        CHECK(pair.n1.test_id_bind_set(2,0x22222222,true));
        CHECK(pair.n2.test_id_bind_set(1,0x11111111,true));
        pair.n2.mobile_home_set(mobile_hash, 1, 1, 2);
        RemoteSessionInstall install{};install.set_root=true;install.set_acl=true;
        memcpy(install.x_secret,admin.x_secret,32);memcpy(install.ed_pub,admin.ed_pub,32);
        memcpy(install.acl[0].ed_pub,controller.ed_pub,32);install.acl[0].role=kRadminRoleOwner;
        install.epoch[0]=9;install.epoch_set[0]=true;
        pair.n2.admin_session_commit(install);CHECK(pair.n2.admin_session_state().epoch[0]!=0);
        step();drain(mobile);drain(pair.n1);drain(pair.n2);
    }
    void step(){pair.step();hm._now=pair.h1._now;}
    RemoteClientRequest request(bool ack=false) {
        RemoteClientRequest r{};static const uint8_t status[]={'s','t','a','t','u','s'};
        r.identity=&controller;r.target_pub=admin.ed_pub;r.command=status;r.route.target_hash=0x22222222;
        r.source_hash=mobile_hash;r.carrier=1;r.correlation_free=mobile.remote_client_correlation_free();r.e2e_ack=ack;
        r.transport=RemoteLocalTransport::ble;return r;
    }
    RemoteClientResult start(bool ack=false) {auto r=request(ack);auto result=remote_client_start(mobile.remote_client(),r,hm._now,random,&entropy,carrier);id=result.request_id;return result;}
    void service(){step();remote_client_service(mobile.remote_client(),hm._now,random,&entropy,carrier,sink);}
    // Same RTS/CTS/DATA/post-ACK exchange as the shared GPair; extends it to a mobile endpoint.
    bool hop(Node& from,GHal& hf,Node& to,GHal& ht,uint8_t type) {
        const auto rts=hf.last("RTS");if(rts.empty())return false;
        const auto cts_count=ht.label_count("CTS"),data_count=hf.label_count("DATA");
        step();to.on_recv(rts.data(),rts.size(),kRx);const auto cts=ht.last("CTS");
        if(ht.label_count("CTS")!=cts_count+1)return false;
        step();from.on_recv(cts.data(),cts.size(),kRx);step();from.on_timer(kCtsToDataGapTimerId);
        const auto bytes=hf.last("DATA");if(hf.label_count("DATA")!=data_count+1)return false;const auto d=parse_data(bytes);
        CHECK(d.has_value());if(!d)return false;CHECK(d->type==type);
        step();to.on_recv(bytes.data(),bytes.size(),kRx);const auto ack=ht.last("ACK");
        if(!ack.empty()){step();from.on_recv(ack.data(),ack.size(),kRx);}
        step();to.on_timer(kPostAckTimerId);return true;
    }
    bool outward() {
        if(!hop(mobile,hm,pair.n1,pair.h1,DATA_TYPE_MOBILE_SEND))return false;
        if(!hop(pair.n1,pair.h1,pair.n2,pair.h2,DATA_TYPE_REMOTE_CMD))return false;
        hm._now=pair.h1._now;
        const auto b=pair.h1.last("DATA");const auto d=parse_data(b);CHECK(d.has_value());if(!d)return false;
        CHECK(d->type==DATA_TYPE_REMOTE_CMD);auto inner=parse_unicast_inner(data_inner(b,*d),d->flags);
        CHECK(inner.has_value());if(!inner)return false;CHECK(inner->source_hash==mobile_hash);return true;
    }
    bool inward(uint8_t type=DATA_TYPE_REMOTE_RESP) {
        if(!hop(pair.n2,pair.h2,pair.n1,pair.h1,type))return false;
        hm._now=pair.h1._now;
        return hop(pair.n1,pair.h1,mobile,hm,type);
    }
    bool bootstrap(){if(!outward() || !inward())return false;service();return true;}
    bool execute(RemoteTerminal code=RemoteTerminal::completed) {
        RadminIngressView v{};if(!pair.n2.radmin_next_admitted(v))return false;
        CHECK(v.body.size()==6);CHECK(memcmp(v.body.data(),"status",6)==0);++executions;
        size_t cap=0;CHECK(remote_body_cap(v.reply_carrier,cap)==RemoteStatus::ok);
        if(!pair.n2.radmin_reserve_transcript(v.seen_index,static_cast<uint16_t>(cap-kRemoteOverheadAuthResponse)))return false;
        pair.n2.radmin_transcript_append(v.seen_index,reinterpret_cast<const uint8_t*>("up=42s"),6);
        if(code==RemoteTerminal::scheduled)
            CHECK(pair.n2.radmin_prepare_action(v.slot,v.request_id,2,0,7006)==RemoteTerminal::scheduled);
        pair.n2.radmin_transcript_complete(v.seen_index,code);return true;
    }
};
}
TEST_CASE("8b real mobile home target chain authenticates and returns transcript with response ACK") {
    CarrierChain c;CHECK(c.start().error==RemoteClientError::none);CHECK(c.bootstrap());CHECK(c.outward());
    CHECK(c.execute());CHECK(c.executions==1);
    CHECK(c.pair.n2.radmin_send_frame()==RadminSend::queued);CHECK(c.inward());c.service();CHECK(c.sink.terminals==0);
    CHECK(c.pair.n2.radmin_send_frame()==RadminSend::queued);CHECK(c.inward());c.service();
    CHECK(c.sink.text=="up=42s");CHECK(c.sink.result=="completed");CHECK(c.sink.terminals==1);
    CHECK(remote_client_ack(c.mobile.remote_client(),c.id,c.hm._now)==RemoteClientError::none);c.service();
    CHECK(c.outward());CHECK(remote_client_ack_debt_count(c.mobile.remote_client())==1);
    CHECK(c.mobile.remote_client_correlation_free()==8); // response ACK never requests another ACK
    const auto ack_wrapper=parse_data(c.hm.last("DATA"));CHECK(ack_wrapper.has_value());
    if(ack_wrapper)CHECK((ack_wrapper->flags&DATA_FLAG_E2E_ACK_REQ)==0);
    CHECK(c.pair.n2.admin_session_state().seen[0].record.state==uint8_t(SeenState::acknowledged));
}
TEST_CASE("8b real carrier refuses no home and bounds actual mobile E2E ring") {
    CarrierChain unregistered(false);CHECK(unregistered.start().error==RemoteClientError::carrier_unavailable);
    CHECK(unregistered.hm.tx_frames.empty());CHECK_FALSE(remote_client_busy(unregistered.mobile.remote_client()));
    CarrierChain c;
    // Labelled capacity fixture: opaque bytes, not a target authentication claim. Both real producers arm the ring.
    uint8_t bytes[25]{};RemoteClientRoute route{0x22222222,{},0};
    for(unsigned i=0;i<protocol::cap_pending_e2e_acks;++i) {
        route.hop_count=i%2;route.hops[0]=3;
        CHECK(c.carrier.submit_request(route,{true,CarrierChain::mobile_hash},remote_client_carrier(route,DATA_TYPE_REMOTE_CMD,1),bytes,true)==RemoteClientSend::queued);
        CHECK(c.mobile.remote_client_correlation_free()==protocol::cap_pending_e2e_acks-i-1);
        // Fly the wrapper to free TX capacity while leaving its E2E obligation pending.
        if (route.hop_count) CHECK(c.hop(c.mobile,c.hm,c.pair.n1,c.pair.h1,DATA_TYPE_MOBILE_SEND));
        else CHECK(c.outward());
    }
    CHECK(c.carrier.submit_request(route,{true,CarrierChain::mobile_hash},remote_client_carrier(route,DATA_TYPE_REMOTE_CMD,1),bytes,true)==RemoteClientSend::correlation_full);
    CHECK(c.mobile.test_tx_queue_n()==0);CHECK(c.hm.count("e2e_ack_untracked_ring_full")==0);
    CHECK(c.start(true).error==RemoteClientError::correlation_full);CHECK_FALSE(remote_client_busy(c.mobile.remote_client()));
}

TEST_CASE("8b real carrier chain preserves every 8ac terminal and automatic rollover meaning") {
    const char* names[]={"completed","scheduled","unknown_command","refused","output_truncated",
                         "internal_error","session_full","session_busy","action_busy"};
    for(uint8_t code=0;code<15;++code) {
        CAPTURE(code);
        CarrierChain c;CHECK(c.start().error==RemoteClientError::none);CHECK(c.bootstrap());
        auto& state=const_cast<RemoteSessionState&>(c.pair.n2.admin_session_state());
        // Labelled target pressure fixtures, identical to the 8ac loopback's owned-record shapes.
        if(code==9) { state.ingress[kRadminIngressControl].state=uint8_t(IngressState::reserved);
                     state.ingress[kRadminIngressControl].expires_at_ms=UINT64_MAX; }
        if(code>=11)for(unsigned i=0;i<kRadminSeenSlots;++i) {
            auto& r=state.seen[i].record;r.state=uint8_t(SeenState::acknowledged);
            r.controller_slot=0;r.request_id=1000+i;r.admin_epoch=state.epoch[0];r.transcript_slot=kRadminNoTranscript;
        }
        CHECK(c.outward());
        if(code==9 || code>=11) {
            CHECK(c.inward());c.service();
            if(code==9) { CHECK(c.sink.result=="ingress_full");continue; }
            CHECK(RemoteClientPhase(c.mobile.remote_client().pending[0].core.state)==RemoteClientPhase::rollover_wait);
            if(code==12) {state.transcripts[0].state=uint8_t(TranscriptState::ready);state.transcripts[0].controller_slot=0;}
            if(code==13)state.seen[0].record.state=uint8_t(SeenState::executing);
            if(code==14)c.pair.h2.dead_rng=true;
            CHECK(c.outward());
            CHECK(c.pair.n2.radmin_service_control());CHECK(c.inward());c.service();
            if(code>=12) {CHECK(c.sink.result==(code==12?"session_busy":code==13?"executing":"preparation_failed"));continue;}
            CHECK(c.mobile.remote_client().pending[0].core.request_id!=c.id);
            c.id=c.mobile.remote_client().pending[0].core.request_id;CHECK(c.outward());
        }
        if(code==10) {
            RadminIngressView v{};CHECK(c.pair.n2.radmin_next_admitted(v));
            state.seen[v.seen_index].record.state=uint8_t(SeenState::acknowledged);state.ingress[kRadminIngressControl]={};
            auto& pending=c.mobile.remote_client().pending[0];
            CHECK(c.carrier.submit_request(pending.core.route,{true,CarrierChain::mobile_hash},
                remote_client_carrier(pending.core.route,DATA_TYPE_REMOTE_CMD,1),{pending.sealed,pending.core.sealed_len},false)==RemoteClientSend::queued);
            CHECK(c.outward());CHECK(c.inward());c.service();CHECK(c.sink.result=="already_acknowledged");continue;
        }
        CHECK(c.execute(code==11?RemoteTerminal::completed:static_cast<RemoteTerminal>(code)));
        for(unsigned i=0;i<2;++i) {CHECK(c.pair.n2.radmin_send_frame()==RadminSend::queued);CHECK(c.inward());}
        c.service();CHECK(c.sink.text=="up=42s");CHECK(c.sink.result==(code==11?"completed":names[code]));
        if(code==1)CHECK(c.sink.detail==7006);
        CHECK(c.mobile.remote_client().counters.auth_failure==0);CHECK(c.mobile.remote_client().counters.assembly_failure==0);
    }
}

TEST_CASE("8b request ACK travels home only for authenticated admitted replayed or acknowledged requests") {
    for(unsigned mode=0;mode<6;++mode) {
        CAPTURE(mode);CarrierChain c;CHECK(c.start(mode!=5).error==RemoteClientError::none);CHECK(c.bootstrap());
        auto& pending=c.mobile.remote_client().pending[0];
        const auto bootstrap=parse_data(c.hm.last("DATA"));CHECK(bootstrap.has_value());
        if(bootstrap)CHECK((bootstrap->flags&DATA_FLAG_E2E_ACK_REQ)==0);
        auto& state=const_cast<RemoteSessionState&>(c.pair.n2.admin_session_state());
        if(mode==3) {state.ingress[kRadminIngressControl].state=uint8_t(IngressState::reserved);
                    state.ingress[kRadminIngressControl].expires_at_ms=UINT64_MAX;}
        CHECK(c.outward());
        if(mode==3 || mode==5) {
            // Refusal can queue its authenticated RPC reply, but never an E2E ACK.
            if(mode==3)CHECK(c.inward());
            CHECK(c.pair.h2.count("send_e2e_ack")==0);
            CHECK(c.mobile.remote_client_correlation_free()==(mode==5?8:7));continue;
        }
        CHECK(c.inward(DATA_TYPE_E2E_ACK));
        Push pu{};unsigned matched=0;
        while(c.mobile.next_push(pu))if(pu.kind==PushKind::send_e2e_acked) {
            CHECK(pu.ctr==pending.core.carrier_ctr);
            CHECK(remote_client_observe_ack(c.mobile.remote_client(),pu.ctr,false,pu.dst,pu.sender_hash,nullptr)==c.id);++matched;
        }
        CHECK(matched==1);CHECK(c.mobile.remote_client_correlation_free()==8);
        if(mode==0)continue;
        CHECK(c.execute());
        if(mode==2)state.seen[0].record.state=uint8_t(SeenState::acknowledged);
        std::vector<uint8_t> wire(pending.sealed,pending.sealed+pending.core.sealed_len);
        if(mode==4)wire.back()^=1;
        CHECK(c.carrier.submit_request(pending.core.route,{true,CarrierChain::mobile_hash},
            remote_client_carrier(pending.core.route,DATA_TYPE_REMOTE_CMD,1),wire,true)==RemoteClientSend::queued);
        CHECK(c.outward());
        if(mode==4) {
            CHECK(c.pair.n2.test_tx_queue_n()==0);CHECK(c.mobile.remote_client_correlation_free()==7);
            CHECK(c.mobile.remote_client().counters.auth_failure==0);continue;
        }
        // already_acknowledged emits a protocol result before its E2E ACK.
        if(mode==2)CHECK(c.inward());
        CHECK(c.inward(DATA_TYPE_E2E_ACK));matched=0;
        while(c.mobile.next_push(pu))if(pu.kind==PushKind::send_e2e_acked) {
            CHECK(remote_client_observe_ack(c.mobile.remote_client(),pu.ctr,false,pu.dst,pu.sender_hash,nullptr)==c.id);++matched;
        }
        CHECK(matched==1);CHECK(c.executions==1);
    }
}

TEST_CASE("8b real ring pressure after bootstrap waits without a radio failure and resumes on real ACK intake") {
    CarrierChain c;CHECK(c.start(true).error==RemoteClientError::none);CHECK(c.outward());CHECK(c.inward());
    auto& pending=c.mobile.remote_client().pending[0];
    CHECK(RemoteClientPhase(pending.core.state)==RemoteClientPhase::request_ready);
    uint8_t noise[25]{};uint16_t first=0;
    for(unsigned i=0;i<protocol::cap_pending_e2e_acks;++i) {
        CHECK(c.carrier.submit_request(pending.core.route,{true,CarrierChain::mobile_hash},
            remote_client_carrier(pending.core.route,DATA_TYPE_REMOTE_CMD,1),noise,true)==RemoteClientSend::queued);
        CHECK(c.outward());const auto d=parse_data(c.pair.h1.last("DATA"));CHECK(d.has_value());if(d && !i)first=d->ctr;
    }
    CHECK(c.mobile.remote_client_correlation_free()==0);
    const auto ctr=pending.core.carrier_ctr;const auto frames=c.hm.tx_frames.size();
    for(unsigned i=0;i<3;++i)c.service();
    CHECK(RemoteClientPhase(pending.core.state)==RemoteClientPhase::request_ready);
    CHECK(c.mobile.remote_client().counters.radio_enqueue_failure==0);CHECK(c.sink.terminals==0);
    CHECK(c.hm.tx_frames.size()==frames);CHECK(pending.core.carrier_ctr==ctr);
    // Labelled unrelated ACK injection: real target transmitter -> home translation -> real mobile ACK receiver releases one ring row.
    const uint8_t ack[]={uint8_t(first),uint8_t(first>>8)};
    CHECK(c.pair.n2.test_do_send_typed(1,ack,2,CryptIntent::off,CarrierChain::mobile_hash,DATA_TYPE_E2E_ACK)!=0);
    CHECK(c.inward(DATA_TYPE_E2E_ACK));CHECK(c.mobile.remote_client_correlation_free()==1);
    c.service();CHECK(RemoteClientPhase(pending.core.state)==RemoteClientPhase::response_wait);
    CHECK(pending.core.carrier_ctr!=ctr);CHECK(c.mobile.remote_client_correlation_free()==0);
    CHECK(c.outward());CHECK(c.execute());
}

TEST_CASE("8b real carrier TX refusal is typed full and initial correlation guard has a carrier backstop") {
    CarrierChain c;c.mobile.test_suspend_tx_drain(true);uint8_t noise=0;
    while(!c.mobile.tx_queue_full())(void)c.mobile.test_do_send_typed(1,&noise,1,CryptIntent::off,0,DATA_TYPE_REMOTE_RESP);
    CHECK(c.start().error==RemoteClientError::radio_enqueue_failed);CHECK_FALSE(remote_client_busy(c.mobile.remote_client()));
    CHECK(c.mobile.remote_client().counters.radio_enqueue_failure==1);
    // A missing home precedes pressure at the carrier boundary.
    CarrierChain unavailable(false);unavailable.mobile.test_suspend_tx_drain(true);
    while(!unavailable.mobile.tx_queue_full())(void)unavailable.mobile.test_do_send_typed(1,&noise,1,CryptIntent::off,0,DATA_TYPE_REMOTE_RESP);
    const auto r=unavailable.request();CHECK(unavailable.carrier.submit_request(r.route,{true,r.source_hash},remote_client_carrier(r.route,DATA_TYPE_REMOTE_CMD,1),{&noise,1},true)==RemoteClientSend::unavailable);
    CarrierChain full;uint8_t body[25]{};auto req=full.request(true);
    for(unsigned i=0;i<protocol::cap_pending_e2e_acks;++i) {
        CHECK(full.carrier.submit_request(req.route,{true,req.source_hash},remote_client_carrier(req.route,DATA_TYPE_REMOTE_CMD,1),body,true)==RemoteClientSend::queued);
        CHECK(full.outward());
    }
    // Labelled stale caller count bypasses only the initial core guard. The send-time carrier still refuses.
    req.correlation_free=8;
    // Prime a valid cache without a frame or forged reply by driving a genuine bootstrap on this full ring (-a is execute-only).
    const auto started=remote_client_start(full.mobile.remote_client(),req,full.hm._now,CarrierChain::random,&full.entropy,full.carrier);
    CHECK(started.error==RemoteClientError::none);full.id=started.request_id;
    CHECK(full.outward());CHECK(full.inward());full.service();
    CHECK(RemoteClientPhase(full.mobile.remote_client().pending[0].core.state)==RemoteClientPhase::request_ready);
    CHECK(full.mobile.remote_client().counters.radio_enqueue_failure==0);
}

TEST_CASE("8b cross-layer carrier claims only local ownership when the home cannot originate") {
    CarrierChain c;auto request=c.request();request.opcode=RemoteCmdOpcode::open_execute;
    request.route.hop_count=1;request.route.hops[0]=3;
    const auto result=remote_client_start(c.mobile.remote_client(),request,c.hm._now,CarrierChain::random,&c.entropy,c.carrier);
    CHECK(result.error==RemoteClientError::none);c.id=result.request_id;
    auto& pending=c.mobile.remote_client().pending[0];
    CHECK(RemoteClientPhase(pending.core.state)==RemoteClientPhase::response_wait);CHECK(pending.core.carrier_ctr!=0);
    CHECK(c.hop(c.mobile,c.hm,c.pair.n1,c.pair.h1,DATA_TYPE_MOBILE_SEND));
    CHECK(c.pair.h1.count("xl_delegate_no_route")==1);CHECK(c.sink.terminals==0);
    const auto first=c.hm.last("DATA");auto d=parse_data(first);CHECK(d.has_value());if(!d)return;
    auto ui=parse_unicast_inner(data_inner(first,*d),d->flags);CHECK(ui.has_value());if(!ui)return;
    CHECK(ui->has_cross_layer);CHECK(ui->n_layers==1);CHECK(ui->layer_ids[0]==3);CHECK(ui->body[0]==DATA_TYPE_REMOTE_CMD);
    const std::vector<uint8_t> exact(ui->body.begin(),ui->body.end());
    c.pair.now=pending.core.next_retry_ms-1;c.hm._now=c.pair.h1._now=c.pair.h2._now=c.pair.now;
    c.service();CHECK(c.sink.terminals==0);CHECK(pending.core.retries==1);
    CHECK(c.hop(c.mobile,c.hm,c.pair.n1,c.pair.h1,DATA_TYPE_MOBILE_SEND));
    CHECK(c.pair.h1.count("xl_delegate_no_route")==2);
    const auto retry=c.hm.last("DATA");d=parse_data(retry);CHECK(d.has_value());if(!d)return;
    ui=parse_unicast_inner(data_inner(retry,*d),d->flags);CHECK(ui.has_value());if(!ui)return;
    CHECK(std::vector<uint8_t>(ui->body.begin(),ui->body.end())==exact);
    c.pair.now=pending.core.outcome_deadline_ms-1;c.service();CHECK(c.sink.result=="unknown");
    CHECK(c.mobile.remote_client().counters.radio_enqueue_failure==0);
}

TEST_CASE("8b automatic exact resend replays a real target transcript without another execution") {
    CarrierChain c;CHECK(c.start().error==RemoteClientError::none);CHECK(c.bootstrap());CHECK(c.outward());CHECK(c.execute());
    CHECK(c.pair.n2.radmin_send_frame()==RadminSend::queued);CHECK(c.inward());c.service();
    CHECK(c.sink.terminals==0);auto& p=c.mobile.remote_client().pending[0];
    const auto ctr=p.core.carrier_ctr;c.pair.now=p.core.next_retry_ms-1;c.service();
    CHECK(p.core.carrier_ctr!=ctr);CHECK(c.outward());
    RadminIngressView ingress{};CHECK_FALSE(c.pair.n2.radmin_next_admitted(ingress));CHECK(c.executions==1);
    for(unsigned i=0;i<2;++i){CHECK(c.pair.n2.radmin_send_frame()==RadminSend::queued);CHECK(c.inward());}
    c.service();CHECK(c.sink.text=="up=42s");CHECK(c.sink.result=="completed");
    CHECK(c.mobile.remote_client().counters.assembly_failure==0);
}

TEST_CASE("8b plain TX pressure after bootstrap increments radio failure without a correlation terminal") {
    CarrierChain c;CHECK(c.start(true).error==RemoteClientError::none);CHECK(c.outward());CHECK(c.inward());
    c.mobile.test_suspend_tx_drain(true);uint8_t noise=0;
    while(!c.mobile.tx_queue_full())(void)c.mobile.test_do_send_typed(1,&noise,1,CryptIntent::off,0,DATA_TYPE_REMOTE_RESP);
    c.service();CHECK(c.mobile.remote_client().counters.radio_enqueue_failure==1);
    CHECK(RemoteClientPhase(c.mobile.remote_client().pending[0].core.state)==RemoteClientPhase::request_ready);
    CHECK(c.sink.terminals==0);CHECK(c.mobile.remote_client_correlation_free()==8);
}

TEST_CASE("8b real checked dispatch refusal under labelled HAL pressure preserves pending counter") {
    CarrierChain c;CHECK(c.start().error==RemoteClientError::none);CHECK(c.outward());CHECK(c.inward());
    auto& p=c.mobile.remote_client().pending[0].core;const auto ctr=p.carrier_ctr;
    c.hm.fill_on_now=&c.mobile;c.service();CHECK(c.hm.fill_on_now==nullptr);CHECK(c.mobile.tx_queue_full());
    CHECK(RemoteClientPhase(p.state)==RemoteClientPhase::request_ready);CHECK(p.carrier_ctr==ctr);
    CHECK(c.mobile.remote_client().counters.radio_enqueue_failure==1);CHECK(c.sink.terminals==0);
}

namespace {
// Mirrors the private timer ID, as the shared MAC fixture does for its timers.
constexpr uint32_t kCarrierE2eDeadlineTimerId = 90;
int carrier_test_lookup(void* node,uint32_t hash) {
    return static_cast<const Node*>(node)->id_bind_find_by_hash(hash);
}
struct ObservationSink {
    uint64_t id=0;unsigned calls=0;
    bool carrier(RemoteLocalTransport,uint64_t request,const char*,uint16_t,const mrfw::CarrierDetail*) {
        id=request;++calls;return true;
    }
};
void carrier_beacon(Node& receiver,uint8_t id,uint32_t hash) {
    // A received target-owned beacon, not an authoritative table injection.
    beacon_in b{};b.src=id;b.key_hash32=hash;b.leaf_id=2;
    uint8_t bytes[100]{};const auto n=pack_beacon(b,bytes);CHECK(n>0);
    receiver.on_recv(bytes,n,kRx);
    CHECK(receiver.id_bind_find_by_hash(hash)==id);
}
}
TEST_CASE("8b B413 known unknown and claimed targets all use distinct home wrappers") {
    for(unsigned binding=0;binding<3;++binding) {
        CAPTURE(binding);CarrierChain c;
        uint16_t counters[2]{};
        for(unsigned i=0;i<2;++i) {
            const uint8_t target=2+i;const uint32_t hash=i?0x33333333:0x22222222;
            if(binding==0)carrier_beacon(c.mobile,target,hash);
            if(binding==1)CHECK(c.mobile.id_bind_find_by_hash(hash)==-1);
            if(binding==2)CHECK(c.mobile.test_id_bind_set(target,hash,false));
            // Labelled carrier-only fixture: pending IDs and opaque request bytes. Authentication is
            // covered by the full chain above; here both live rows coexist through the real TX carrier.
            auto& row=c.mobile.remote_client().pending[i].core;
            row.request_id=100+i;row.state=uint8_t(RemoteClientPhase::response_wait);
            row.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;row.route.target_hash=hash;
            uint8_t body[25]{};body[1]=uint8_t(row.request_id);
            CHECK(c.carrier.submit_request(row.route,{true,CarrierChain::mobile_hash},
                remote_client_carrier(row.route,DATA_TYPE_REMOTE_CMD,1),body,true)==RemoteClientSend::queued);
            counters[i]=row.carrier_ctr;CHECK(counters[i]!=0);
            CHECK(c.hop(c.mobile,c.hm,c.pair.n1,c.pair.h1,DATA_TYPE_MOBILE_SEND));
            const auto data=parse_data(c.hm.last("DATA"));CHECK(data.has_value());
            if(data){CHECK(data->dst==c.mobile.mobile_home_id());CHECK(data->ctr==counters[i]);}
            if(i==0)CHECK(c.hop(c.pair.n1,c.pair.h1,c.pair.n2,c.pair.h2,DATA_TYPE_REMOTE_CMD));
        }
        CHECK(counters[0]!=counters[1]);CHECK(c.mobile.remote_client_correlation_free()==6);
        const auto before=c.mobile.remote_client();
        for(unsigned i=0;i<2;++i) {
            auto& row=c.mobile.remote_client().pending[i].core;
            Push push{};push.kind=PushKind::send_e2e_acked;push.dst=2+i;push.ctr=counters[i];
            ObservationSink sink;
            mrfw::remote_client_observe_push(c.mobile.remote_client(),push,sink,carrier_test_lookup,&c.mobile);
            CHECK(sink.calls==1);CHECK(sink.id==row.request_id);
            CustodyFailureRecord record{};record.failed_origin=1;record.failed_dst=2+i;
            record.failed_ctr=99;record.failed_type=DATA_TYPE_REMOTE_CMD;
            record.terminal_reason=CustodyFailureReason::cascade_count;
            record.notice_flags=custody_notice_flags(CustodyRootStage::cts,true,false,true);
            record.dst_hash32=row.route.target_hash;record.previous_hop=1;record.failed_next_hop=2+i;
            record.requeue_count=protocol::cascade_requeue_max;record.committed_hops=1;record.remaining_hops=4;
            CustodyTranslatedTail tail{};tail.original_reporter=2+i;tail.mobile_ctr=counters[i];
            tail.target_kind=CustodyTranslatedTargetKind::key_hash;tail.target_value=row.route.target_hash;
            push.kind=PushKind::custody_failure;push.body_len=pack_custody_failure_translated(record,tail,push.body);
            CHECK(push.body_len==custody_record_translated_len);sink={};
            mrfw::remote_client_observe_push(c.mobile.remote_client(),push,sink,carrier_test_lookup,&c.mobile);
            CHECK(sink.calls==1);CHECK(sink.id==row.request_id);
        }
        // Real deadline service: wrapper obligations survive the direct 60-s tier, expire at 300 s.
        drain(c.mobile);const auto submitted=c.hm._now;
        c.hm._now=submitted+protocol::e2e_ack_deadline_ms;c.mobile.on_timer(kCarrierE2eDeadlineTimerId);
        Push pu{};while(c.mobile.next_push(pu))CHECK(pu.reason!=SendFailReason::e2e_ack_timeout);
        c.hm._now=submitted+protocol::e2e_ack_deadline_xl_ms;c.mobile.on_timer(kCarrierE2eDeadlineTimerId);
        unsigned timeouts=0;
        while(c.mobile.next_push(pu))if(pu.reason==SendFailReason::e2e_ack_timeout) {
            CHECK(pu.dst==0);ObservationSink sink;
            mrfw::remote_client_observe_push(c.mobile.remote_client(),pu,sink,carrier_test_lookup,&c.mobile);
            CHECK(sink.calls==1);CHECK(sink.id==(pu.ctr==counters[0]?100:101));++timeouts;
        }
        CHECK(timeouts==2);CHECK(c.mobile.remote_client_correlation_free()==8);
        CHECK(memcmp(&before,&c.mobile.remote_client(),sizeof before)==0);
    }
}
TEST_CASE("8b B416 real Node binding veto is scoped to same-layer ACK candidates") {
    for(unsigned binding=0;binding<3;++binding) {
        CarrierChain c;auto& state=c.mobile.remote_client();auto& row=state.pending[0].core;
        row.request_id=1;row.state=uint8_t(RemoteClientPhase::response_wait);row.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;
        row.route.target_hash=0x22222222;row.carrier_ctr=42;
        if(binding)carrier_beacon(c.mobile,binding==1?2:3,row.route.target_hash);
        struct Lookup {Node* node;unsigned calls;};Lookup context{&c.mobile,0};
        auto lookup=[](void* ctx,uint32_t hash){auto& c=*static_cast<Lookup*>(ctx);++c.calls;return c.node->id_bind_find_by_hash(hash);};
        Push push{};push.kind=PushKind::send_e2e_acked;push.dst=2;push.ctr=42;ObservationSink sink;
        mrfw::remote_client_observe_push(state,push,sink,lookup,&context);
        CHECK(context.calls==1);CHECK(sink.calls==(binding==2?0:1));
        context.calls=0;sink={};push.kind=PushKind::send_failed;push.reason=SendFailReason::e2e_ack_timeout;push.dst=0;
        mrfw::remote_client_observe_push(state,push,sink,lookup,&context);
        CHECK(context.calls==0);CHECK(sink.calls==1);
        sink={};row.route.hop_count=1;push.kind=PushKind::send_e2e_acked;push.dst=9;push.sender_hash=row.route.target_hash;
        mrfw::remote_client_observe_push(state,push,sink,lookup,&context);
        CHECK(context.calls==0);CHECK(sink.calls==1);
    }
}
TEST_CASE("8b B415 characterization direct operator ACK can clear an equal-counter wrapper") {
    for(bool equal:{false,true}) {
        CarrierChain c;carrier_beacon(c.mobile,2,0x22222222);
        auto& row=c.mobile.remote_client().pending[0].core;
        row.request_id=1;row.state=uint8_t(RemoteClientPhase::response_wait);row.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;
        row.route.target_hash=0x22222222;uint8_t body[25]{};body[1]=1;
        CHECK(c.carrier.submit_request(row.route,{true,CarrierChain::mobile_hash},remote_client_carrier(row.route,DATA_TYPE_REMOTE_CMD,1),body,true)==RemoteClientSend::queued);
        CHECK(c.outward());
        c.mobile.test_suspend_tx_drain(true);
        if(!equal)(void)c.mobile.test_next_ctr(2);
        const uint8_t dm='x';Command command{};command.kind=CmdKind::send;
        command.u.send.dst_hash=row.route.target_hash;command.u.send.flags=DATA_FLAG_E2E_ACK_REQ;
        command.body=&dm;command.body_len=1;
        const auto result=c.mobile.on_command(command);const auto ctr=result.ctr;
        CHECK(result.code==CmdCode::queued);
        CHECK((ctr==row.carrier_ctr)==equal);CHECK(c.mobile.remote_client_correlation_free()==6);
        c.mobile.test_suspend_tx_drain(false);
        // Actual ACK receiver from the direct target. B415 is characterized, not fixed: first wildcard wins.
        GHal ha;Node acker(ha,2,0x22222222);CHECK(acker.on_init(g_cfg()));
        acker.test_learn_route(1,1,1,40,false);ha._now=c.hm._now;
        const uint8_t ack[]={uint8_t(ctr),uint8_t(ctr>>8)};
        CHECK(acker.test_do_send_typed(1,ack,2,CryptIntent::off,CarrierChain::mobile_hash,DATA_TYPE_E2E_ACK)!=0);
        drain(c.mobile);CHECK(c.hop(acker,ha,c.pair.n1,c.pair.h1,DATA_TYPE_E2E_ACK));
        CHECK(c.hop(c.pair.n1,c.pair.h1,c.mobile,c.hm,DATA_TYPE_E2E_ACK));
        Push push{};unsigned acks=0;
        while(c.mobile.next_push(push))if(push.kind==PushKind::send_e2e_acked) {
            ObservationSink sink;mrfw::remote_client_observe_push(c.mobile.remote_client(),push,sink,carrier_test_lookup,&c.mobile);
            CHECK(sink.calls==(equal?1:0));++acks;
        }
        CHECK(acks==1);CHECK(c.mobile.remote_client_correlation_free()==7);
    }
}
