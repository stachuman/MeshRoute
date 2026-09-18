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
TEST_CASE("8ac controller authenticates discovery retains bytes and owes ACK only after local acceptance") {
    Client c;CHECK(c.start().error==RemoteClientError::none);CHECK(c.carrier.requests.size()==1);
    CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::incomplete);
    c.bootstrap();CHECK(c.carrier.requests.size()==2);CHECK(c.p().core.acl_slot==3);CHECK(c.p().core.credential_slot==4);
    c.output("hello");c.terminal(0,1);CHECK(remote_client_retained_count(c.s)==1);CHECK(c.carrier.acks.empty());
    CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::incomplete);
    c.pump();CHECK(c.delivery.text=="hello");CHECK(c.delivery.terminal_name=="completed");CHECK(c.carrier.acks.empty());
    c.delivery.up=false;c.pump();CHECK(remote_client_busy(c.s));c.delivery.up=true;c.pump();CHECK(c.delivery.text=="hellohello");
    CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none);c.pump();CHECK(c.carrier.acks.size()==1);CHECK(c.carrier.acks[0].size()==25);
    CHECK(remote_client_pending_count(c.s)==0);CHECK(remote_client_busy(c.s));CHECK(remote_client_key_in_use(c.s,4));CHECK(remote_client_target_in_use(c.s,7));
    c.now+=protocol::cascade_requeue_base_ms;c.pump();CHECK(c.carrier.acks.size()==2);CHECK(c.carrier.acks[0]==c.carrier.acks[1]);
    for(const auto& p:c.s.pending)for(auto b:p.sealed)CHECK(b==0);
    for(const auto& chunk:c.s.chunks)for(auto b:chunk.bytes)CHECK(b==0);
}
TEST_CASE("8ac controller exact manual retry compares epoch before replay and changed epoch is unknown") {
    for(bool changed:{false,true}) {
        Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();const auto original=c.carrier.requests.back();
        c.output("partial");c.now+=protocol::e2e_ack_deadline_xl_ms;c.pump();CHECK(c.delivery.terminal_name=="unknown");
        CHECK(remote_client_retry(c.s,c.id,c.now,Entropy::fill,&c.entropy)==RemoteClientError::none);c.pump();
        if(changed){++c.epoch;c.rekey();}
        c.receive(c.wire(RemoteRespOpcode::bootstrap,c.p().core.discovery_id));const auto before=c.carrier.requests.size();c.pump();
        if(changed){CHECK(c.carrier.requests.size()==before);CHECK(c.delivery.terminal_name=="unknown");CHECK(c.carrier.acks.empty());}
        else {CHECK(c.carrier.requests.size()==before+1);CHECK(c.carrier.requests.back()==original);c.terminal();c.pump();CHECK(c.delivery.terminal_name=="completed");}
    }
}
TEST_CASE("8ac controller classifies terminals admission protocol error and single safe rollover") {
    const char* names[]={"completed","scheduled","unknown_command","refused","output_truncated","internal_error","session_full","session_busy","action_busy"};
    for(uint8_t code=0;code<9;++code){Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();c.terminal(code);c.pump();CHECK(c.delivery.terminal_name==names[code]);if(code==1)CHECK(c.delivery.detail==0x12345678);}
    for(auto code:{RemoteAdmission::ingress_full,RemoteAdmission::session_busy,RemoteAdmission::executing,RemoteAdmission::preparation_failed}){
        Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();
        if(code!=RemoteAdmission::ingress_full){c.receive(c.wire(RemoteRespOpcode::admission_result,c.id,{},0,RemoteAdmission::session_full));c.pump();}
        c.receive(c.wire(RemoteRespOpcode::admission_result,code==RemoteAdmission::ingress_full?c.id:c.p().core.discovery_id,{},0,code));c.pump();CHECK(c.delivery.terminals==1);
        CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none);c.pump();CHECK(c.carrier.acks.empty());}
    Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();const auto oldid=c.id;
    c.receive(c.wire(RemoteRespOpcode::admission_result,c.id,{},0,RemoteAdmission::session_full));c.pump();
    CHECK(RemoteClientPhase(c.p().core.state)==RemoteClientPhase::rollover_wait);
    ++c.epoch; // rollover result authenticates under the old session, carries the new epoch
    c.receive(c.wire(RemoteRespOpcode::rollover_result,c.p().core.discovery_id));c.rekey();c.pump();
    c.id=c.s.pending[0].core.request_id;CHECK(c.id!=oldid);CHECK(c.p().core.admin_epoch==c.epoch);
    c.receive(c.wire(RemoteRespOpcode::admission_result,c.id,{},0,RemoteAdmission::session_full));const auto count=c.carrier.requests.size();c.pump();
    CHECK(c.delivery.terminal_name=="session_full");CHECK(c.carrier.requests.size()==count);
    Client p;CHECK(p.start().error==RemoteClientError::none);p.bootstrap();uint8_t code=0;p.receive(p.wire(RemoteRespOpcode::protocol_error,p.id,{&code,1}));p.pump();CHECK(p.delivery.terminal_name=="already_acknowledged");
}
TEST_CASE("8ac controller rejects wrong source ID sequence authentication and pressure without eviction") {
    Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();auto b=c.wire(RemoteRespOpcode::output,c.id,{reinterpret_cast<const uint8_t*>("x"),1});
    c.receive(b,77);CHECK(c.s.counters.unmatched_response==1);b[1]^=1;c.receive(b);CHECK(c.s.counters.unmatched_response==2);b[1]^=1;
    c.output("late",1);CHECK(c.s.counters.assembly_failure==1);b.back()^=1;c.receive(b);CHECK(c.s.counters.auth_failure==1);c.pump();
    c.receive(c.wire(RemoteRespOpcode::bootstrap,c.p().core.discovery_id));c.pump();c.receive(b);c.pump();CHECK(c.delivery.terminal_name=="authentication_failed");
    Client a;auto r=a.request();a.entropy.ok=false;CHECK(remote_client_start(a.s,r,0,Entropy::fill,&a.entropy,a.carrier).error==RemoteClientError::entropy_failed);CHECK_FALSE(remote_client_busy(a.s));
    a.entropy.ok=true;a.entropy.next=0;CHECK(a.start().error==RemoteClientError::duplicate_id);CHECK(a.carrier.requests.empty());
    a.carrier.answer=RemoteClientSend::unavailable;CHECK(a.start().error==RemoteClientError::carrier_unavailable);CHECK_FALSE(remote_client_busy(a.s));
    a.carrier.answer=RemoteClientSend::queued;CHECK(a.start().error==RemoteClientError::none);a.entropy.next=a.id;CHECK(a.start().error==RemoteClientError::duplicate_id);
    auto before=a.s; a.s.counters.request_table_pressure=UINT16_MAX;for(auto& p:a.s.pending)p.core.state=uint8_t(RemoteClientPhase::response_wait);
    CHECK(a.start().error==RemoteClientError::request_table_full);CHECK(a.s.counters.request_table_pressure==UINT16_MAX);a.s=before;
    a.s.assemblies[1].in_use=1;CHECK(a.start().error==RemoteClientError::assembly_full);a.s=before;
    a.s.retained[1].in_use=1;CHECK(a.start().error==RemoteClientError::result_full);a.s=before;
    r.e2e_ack=true;r.correlation_free=0;CHECK(remote_client_start(a.s,r,0,Entropy::fill,&a.entropy,a.carrier).error==RemoteClientError::correlation_full);
}
TEST_CASE("8ac controller USB short write retains then full reoffer creates debt") {
    Client c;CHECK(c.start(RemoteLocalTransport::usb).error==RemoteClientError::none);c.bootstrap();c.output("line");c.terminal(8,1);
    c.delivery.ok=false;c.pump();CHECK(remote_client_busy(c.s));CHECK(c.carrier.acks.empty());CHECK(c.delivery.retentions==1);
    c.delivery.ok=true;CHECK(remote_client_show(c.s,c.id,RemoteLocalTransport::usb,c.now,c.delivery)==RemoteClientError::none);CHECK(c.delivery.text=="lineline");
    c.pump();CHECK(c.carrier.acks.size()==1);CHECK(c.delivery.terminal_name=="action_busy");
    // USB can retain in its assembly while both BLE result headers are reserved.
    Client fallback;for(auto& row:fallback.s.retained){row.in_use=1;row.first_chunk=kRemoteClientNoChunk;}
    CHECK(fallback.start(RemoteLocalTransport::usb).error==RemoteClientError::none);fallback.bootstrap();
    fallback.terminal(1);fallback.pump();CHECK(fallback.delivery.terminal_name=="scheduled");
    CHECK(fallback.delivery.detail==0x12345678);CHECK(fallback.carrier.acks.size()==1);
}

// Independent inputs/outputs are frozen in the evidence-side controller-reference.py (hashlib + libsodium).
TEST_CASE("8ac controller independent full identity base session and sealed-request KAT") {
    Client c;
    auto hex=[](const uint8_t* p,size_t n){std::string out;static constexpr char h[]="0123456789abcdef";
        for(size_t i=0;i<n;++i){out+=h[p[i]>>4];out+=h[p[i]&15];}return out;};
    CHECK(hex(c.controller.ed_pub,32)=="d4f8e6f267271177c11d17d39810d747166572a1b6db8e352363d9786eb07983");
    CHECK(hex(c.target.ed_pub,32)=="fbff696c5d3540b35c430e6d2a15dba01fdc73f3c2a6db461d3c0c248b4999b2");
    CHECK(hex(c.base,32)=="adab3d61591ff4df31484bef45a275a443152f95d0c257ea8a06e33b2c5099e1");
    CHECK(hex(c.key,32)=="40a66ab000f86863c6dbe0569411572395fe97e96f33f01857590da097b92412");
    CHECK(c.start().error==RemoteClientError::none);c.bootstrap();
    CHECK(hex(c.carrier.requests.back().data(),c.carrier.requests.back().size())=="0364000000000000009bf33b307a1ebb87473254ca98247fc34667487fb06e");
}
TEST_CASE("8ac controller low-order refusal and B403 off-curve characterization") {
    Client c;
    // Ed encodings y=0,+1,-1 map to low-order Montgomery peers. Both signs of y=0 are included.
    for(unsigned form=0;form<4;++form){uint8_t peer[32]{},out[32];memset(out,0xa5,32);
        if(form==1)peer[0]=1;
        if(form==2){memset(peer,0xff,32);peer[0]=0xec;peer[31]=0x7f;}
        if(form==3)peer[31]=0x80;
        CHECK(remote_client_base(out,c.controller,peer)==RemoteStatus::bad_key);
        for(auto b:out)CHECK(b==0xa5);
    }
    for(uint8_t y:{2,7,8,11}){
        Client f;uint8_t peer[32]{},out[32]{};peer[0]=y;
        CHECK(remote_client_base(out,f.controller,peer)==RemoteStatus::ok);
        auto req=f.request();req.target_pub=peer;
        auto start=remote_client_start(f.s,req,f.now,Entropy::fill,&f.entropy,f.carrier);
        CHECK(start.error==RemoteClientError::none);f.id=start.request_id;
        // The actual target authenticates with its real identity, not the invalid book point.
        f.receive(f.wire(RemoteRespOpcode::bootstrap,f.p().core.discovery_id));
        CHECK(f.s.counters.auth_failure==1);CHECK(f.s.sessions[0].valid==1);CHECK(f.carrier.requests.size()==1);
    }
}

TEST_CASE("8ac controller entire eight-chunk target transcript fits the approved shared pool") {
    Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();
    size_t cap=0;CHECK(remote_body_cap(remote_client_carrier(c.route,DATA_TYPE_REMOTE_RESP,0),cap)==RemoteStatus::ok);
    CHECK(kRemoteClientChunkBytes==cap-kRemoteOverheadAuthResponse);
    std::string text(kRemoteClientChunkBytes,'x');
    for(uint8_t i=0;i<8;++i)c.output(text.c_str(),i);
    CHECK(c.s.counters.assembly_failure==0);c.terminal(0,8);c.pump();
    CHECK(c.delivery.text==std::string(kRemoteClientChunkBytes*8,'x'));CHECK(c.delivery.terminal_name=="completed");
}

#include "device_rng.h"
TEST_CASE("8ac checked entropy refuses host without publishing bytes") {
    uint8_t bytes[8];memset(bytes,0xa5,sizeof bytes);
    CHECK_FALSE(mrrng::fill_checked(bytes,sizeof bytes));
    for(auto b:bytes)CHECK(b==0xa5);
}

TEST_CASE("8ac controller session cache keys the full selected credential and captures identity") {
    Client c;CHECK(c.start().error==RemoteClientError::none);c.bootstrap();
    Identity second{};uint8_t seed[32];memset(seed,0x51,sizeof seed);identity_from_seed(second,seed);
    auto req=c.request();req.identity=&second;req.credential_slot=9;
    uint8_t selected[32];memcpy(selected,second.ed_pub,32);
    const auto result=remote_client_start(c.s,req,c.now,Entropy::fill,&c.entropy,c.carrier);
    CHECK(result.error==RemoteClientError::none);CHECK(c.s.sessions[1].valid==1);
    CHECK(RemoteClientPhase(c.s.pending[1].core.state)==RemoteClientPhase::bootstrap_wait);
    CHECK(c.s.pending[1].core.credential_slot==9);
    memset(&second,0,sizeof second); // selector/expanded identity transient changes after admission
    CHECK(memcmp(c.s.pending[1].core.controller_pub,selected,32)==0);
    CHECK(memcmp(c.s.sessions[1].controller_pub,selected,32)==0);
    RemoteClientState cold{};CHECK_FALSE(remote_client_busy(cold));for(const auto& s:cold.sessions)CHECK(s.valid==0);
}

TEST_CASE("8ac OPEN full byte budget packs frame tails atomically without ACK debt") {
    Client c;auto req=c.request();req.opcode=RemoteCmdOpcode::open_execute;
    const auto started=remote_client_start(c.s,req,c.now,Entropy::fill,&c.entropy,c.carrier);
    CHECK(started.error==RemoteClientError::none);c.id=started.request_id;
    auto send=[&](RemoteRespOpcode op,uint8_t seq,std::span<const uint8_t> body) {
        RemoteMessage m{};m.outer_type=DATA_TYPE_REMOTE_RESP;m.opcode=uint8_t(op);m.slot=kRemoteSlotSentinel;
        m.request_id=c.id;m.response_seq=seq;uint8_t bytes[255]{};size_t n=0;
        CHECK(remote_body_encode(bytes,n,m,body,{}, {true,c.controller.key_hash32},remote_client_carrier(c.route,DATA_TYPE_REMOTE_RESP,0))==RemoteStatus::ok);
        c.receive({bytes,bytes+n});
    };
    std::string full(8*kRemoteClientChunkBytes,'o');uint8_t seq=0;
    for(size_t off=0;off<full.size();off+=221) {
        send(RemoteRespOpcode::output,seq++,{reinterpret_cast<const uint8_t*>(full.data()+off),std::min(size_t{221},full.size()-off)});
    }
    CHECK(c.s.counters.assembly_failure==0);CHECK(c.s.assemblies[0].bytes_used==full.size());
    const uint8_t excess=1;send(RemoteRespOpcode::output,seq,{&excess,1});
    CHECK(c.s.counters.assembly_failure==1);CHECK(c.s.assemblies[0].bytes_used==full.size());
    const uint8_t complete=0;send(RemoteRespOpcode::terminal,seq,{&complete,1});c.pump();
    CHECK(c.delivery.text==full);CHECK(c.delivery.terminal_name=="completed");
    CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none);c.pump();CHECK(c.carrier.acks.empty());
}

TEST_CASE("8ac explicit BLE show adopts reconnect delivery including USB assembly fallback") {
    for(bool pool_full:{false,true}) {
        Client c;
        if(pool_full)for(auto& row:c.s.retained){row.in_use=1;row.first_chunk=kRemoteClientNoChunk;}
        CHECK(c.start(RemoteLocalTransport::usb).error==RemoteClientError::none);c.bootstrap();c.output("cross");c.terminal(0,1);
        c.delivery.ok=false;c.pump();CHECK(c.carrier.acks.empty());c.delivery.ok=true;
        CHECK(remote_client_show(c.s,c.id,RemoteLocalTransport::ble,c.now,c.delivery)==RemoteClientError::none);
        CHECK(c.p().core.local_transport==uint8_t(RemoteLocalTransport::ble));
        const auto delivered=c.delivery.text;c.delivery.up=false;c.pump();CHECK(c.delivery.text==delivered);
        c.delivery.up=true;c.pump();CHECK(c.delivery.text==delivered+"cross");
        const auto offered=c.delivery.text;c.pump();CHECK(c.delivery.text==offered);
        c.delivery.up=false;c.pump();
        CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none); // complete before disconnect
        c.pump();CHECK(c.carrier.acks.size()==1);
    }
}

TEST_CASE("8ac full ACK debt still permits explicit rollover recovery and controls never owe transcript ACK") {
    for(auto op:{RemoteCmdOpcode::safe_rollover,RemoteCmdOpcode::force_rollover}) {
        Client c;
        for(unsigned i=0;i<8;++i){
            CHECK(c.start().error==RemoteClientError::none);if(!i)c.bootstrap();
            c.terminal();c.pump();CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none);c.pump();
        }
        CHECK(c.start().error==RemoteClientError::ack_debt_full);
        auto req=c.request();req.opcode=op;req.command={};
        const auto control=remote_client_start(c.s,req,c.now,Entropy::fill,&c.entropy,c.carrier);
        CHECK(control.error==RemoteClientError::none);c.id=control.request_id;
        const auto unmatched=c.s.counters.unmatched_response;c.terminal();
        CHECK(c.s.counters.unmatched_response==unmatched+1);
        CHECK(RemoteClientPhase(c.p().core.state)==RemoteClientPhase::response_wait);
        ++c.epoch;c.receive(c.wire(RemoteRespOpcode::rollover_result,c.id));c.rekey();c.pump();
        CHECK(c.delivery.terminal_name=="rollover_completed");
        for(const auto& row:c.s.ack_debt)CHECK(row.in_use==0);
        CHECK(remote_client_ack(c.s,c.id,c.now)==RemoteClientError::none);
        CHECK(c.start().error==RemoteClientError::none);
    }
}
