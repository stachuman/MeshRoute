#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// Controller lifecycle proofs. Real codec frames; fake carrier owns submitted bytes, never radio hardware.
#include "doctest.h"
#include "remote_client.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include <cstring>
#include <string>
#include <vector>
using namespace MESHROUTE_NS;
namespace {
struct Entropy {
    uint64_t next=100; bool ok=true;
    static bool fill(void* c,uint8_t* out,size_t n) {
        auto& e=*static_cast<Entropy*>(c);
        for(size_t i=0;i<n;++i) out[i]=static_cast<uint8_t>(e.next>>(8*i));
        ++e.next; return e.ok;
    }
};
struct Carrier : IRadminCarrier {
    bool full=false; RemoteClientSend answer=RemoteClientSend::queued;
    std::vector<std::vector<uint8_t>> requests,acks;
    bool tx_queue_full() const override {return full;}
    RemoteClientSend submit_request(const RemoteClientRoute&,RemoteSource,const RemoteCarrier&,std::span<const uint8_t> b,bool) override {
        if(answer==RemoteClientSend::queued)requests.emplace_back(b.begin(),b.end());
        return answer;
    }
    RemoteClientSend submit_ack(const RemoteClientRoute&,RemoteSource,const RemoteCarrier&,std::span<const uint8_t> b) override {
        if(answer==RemoteClientSend::queued)acks.emplace_back(b.begin(),b.end());
        return answer;
    }
};
struct Delivery : IRemoteLocalDelivery {
    bool up=true,ok=true; std::string text,terminal_name; uint32_t detail=0; unsigned terminals=0,retentions=0;
    bool connected(RemoteLocalTransport) const override{return up;}
    bool output(RemoteLocalTransport,uint64_t,uint16_t& seq,std::span<const uint8_t> b) override {
        text.append(reinterpret_cast<const char*>(b.data()),b.size());++seq;return ok;
    }
    bool terminal(RemoteLocalTransport,uint64_t,const char* name,uint32_t d,bool) override {terminal_name=name;detail=d;++terminals;return ok;}
    void retained(RemoteLocalTransport,uint64_t) override{++retentions;}
};
struct Client {
    RemoteClientState s{}; Identity controller{},target{}; Entropy entropy; Carrier carrier; Delivery delivery;
    uint8_t base[32]{},key[32]{}; uint64_t epoch=9,id=0; uint32_t now=10; RemoteClientRoute route{0x12345678,{},0};
    Client(){uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=uint8_t(i+1);identity_from_seed(controller,seed);
        for(unsigned i=0;i<32;++i)seed[i]=uint8_t(i+77);
        identity_from_seed(target,seed);
        CHECK(remote_client_base(base,controller,target.ed_pub)==RemoteStatus::ok);rekey();}
    void rekey(){CHECK(remote_kdf_session(key,base,epoch)==RemoteStatus::ok);}
    RemoteClientRequest request(RemoteLocalTransport t=RemoteLocalTransport::ble){
        RemoteClientRequest r{};r.identity=&controller;r.target_pub=target.ed_pub;
        static const uint8_t cmd[]={'s','t','a','t','u','s'};r.command=cmd;r.route=route;r.source_hash=controller.key_hash32;r.transport=t;
        r.credential_slot=4;r.target_book_slot=7;r.correlation_free=8;return r;
    }
    RemoteClientResult start(RemoteLocalTransport t=RemoteLocalTransport::ble){auto r=request(t);auto got=remote_client_start(s,r,now,Entropy::fill,&entropy,carrier);id=got.request_id;return got;}
    RemoteClientPending& p(){for(auto& r:s.pending)if(r.core.request_id==id)return r;return s.pending[0];}
    void pump(){remote_client_service(s,++now,Entropy::fill,&entropy,carrier,delivery);}
    std::vector<uint8_t> wire(RemoteRespOpcode op,uint64_t request_id,std::span<const uint8_t> body={},uint8_t seq=0,RemoteAdmission admission=RemoteAdmission::ingress_full){
        RemoteMessage m{};m.outer_type=DATA_TYPE_REMOTE_RESP;m.opcode=uint8_t(op);m.request_id=request_id;m.slot=3;m.admin_epoch=epoch;
        m.response_seq=seq;m.request_ctl=remote_ctl(uint8_t(op==RemoteRespOpcode::admission_result && RemoteClientPhase(p().core.state)==RemoteClientPhase::rollover_wait?RemoteCmdOpcode::safe_rollover:RemoteCmdOpcode::auth_execute),3);
        m.admission_code=admission;if(admission==RemoteAdmission::session_busy)m.admission_detail=2;
        uint8_t out[kRemoteClientChunkBytes+kRemoteOverheadAuthResponse]{};size_t n=0;RemoteKeys keys{base,key};
        CHECK(remote_body_encode(out,n,m,body,keys,{true,controller.key_hash32},remote_client_carrier(route,DATA_TYPE_REMOTE_RESP,0))==RemoteStatus::ok);
        return {out,out+n};
    }
    void receive(const std::vector<uint8_t>& b,uint32_t source=0x12345678){remote_client_receive(s,DATA_TYPE_REMOTE_RESP,b,{true,source},remote_client_carrier(route,DATA_TYPE_REMOTE_RESP,0),now);}
    void bootstrap(){receive(wire(RemoteRespOpcode::bootstrap,p().core.discovery_id));pump();}
    void output(const char* text,uint8_t seq=0){receive(wire(RemoteRespOpcode::output,id,{reinterpret_cast<const uint8_t*>(text),strlen(text)},seq));}
    void terminal(uint8_t code=0,uint8_t seq=0){uint8_t b[]={code,0x78,0x56,0x34,0x12};receive(wire(RemoteRespOpcode::terminal,id,{b,code==1?5u:1u},seq));}
};
}

// Synthetic carrier refusal only. This tests real controller classification, not a radio chain.
TEST_CASE("8b preflight initial capacity and carrier full have different typed results") {
    Client c; auto r=c.request(); r.e2e_ack=true; r.correlation_free=0;
    CHECK(remote_client_start(c.s,r,c.now,Entropy::fill,&c.entropy,c.carrier).error==RemoteClientError::correlation_full);
    CHECK(c.carrier.requests.empty()); CHECK(remote_client_pending_count(c.s)==0);
    // Populate a valid session using the real fixture keys, so the first submit is execute.
    auto& e=c.s.sessions[0]; e.valid=2; e.admin_epoch=c.epoch; e.acl_slot=3;
    memcpy(e.controller_pub,c.controller.ed_pub,32); memcpy(e.target_admin_pub,c.target.ed_pub,32);
    memcpy(e.base_key,c.base,32); memcpy(e.session_key,c.key,32);
    r.correlation_free=8; c.carrier.answer=RemoteClientSend::full;
    CHECK_FALSE(c.carrier.tx_queue_full());
    CHECK(remote_client_start(c.s,r,c.now,Entropy::fill,&c.entropy,c.carrier).error==RemoteClientError::radio_enqueue_failed);
    CHECK(c.carrier.requests.empty()); CHECK(remote_client_pending_count(c.s)==0);
    CHECK(c.s.counters.radio_enqueue_failure==1);
}
TEST_CASE("8b preflight full after bootstrap stays ready rather than reporting correlation_full") {
    Client c; auto r=c.request(); r.e2e_ack=true; r.correlation_free=8;
    const auto got=remote_client_start(c.s,r,c.now,Entropy::fill,&c.entropy,c.carrier);c.id=got.request_id;
    REQUIRE(got.error==RemoteClientError::none); REQUIRE(c.carrier.requests.size()==1);
    CHECK(RemoteClientPhase(c.p().core.state)==RemoteClientPhase::bootstrap_wait);
    c.carrier.answer=RemoteClientSend::full; c.bootstrap();
    CHECK(c.carrier.requests.size()==1); CHECK(RemoteClientPhase(c.p().core.state)==RemoteClientPhase::request_ready);
    CHECK(c.s.counters.radio_enqueue_failure==1); CHECK(c.delivery.terminals==0);
    CHECK(c.delivery.terminal_name.empty()); CHECK(remote_client_pending_count(c.s)==1);
}
