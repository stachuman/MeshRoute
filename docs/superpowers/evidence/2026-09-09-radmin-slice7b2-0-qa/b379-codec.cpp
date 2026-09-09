// QA AUTHOR PROOF ONLY. Hypothetical 7b-2 replies use the real unchanged codec/session engine.
// No production producer for these negative replies exists at the measured base.
#include "remote_session.h"
#include "frame_codec.h"
#include "identity.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace meshroute;
static unsigned checks;
static void require(bool ok) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL check %u\n",checks); std::exit(1); } }
int main() {
    Identity root{}, ctl[2]{};
    uint8_t seed[32]={31}; identity_from_seed(root,seed);
    for(unsigned i=0;i<2;++i) { seed[0]=41+i; identity_from_seed(ctl[i],seed); }
    RemoteSessionState s{}; remote_session_clear(s);
    RemoteSessionInstall install{}; install.set_root=true; install.set_acl=true;
    std::memcpy(install.x_secret,root.x_secret,32); std::memcpy(install.ed_pub,root.ed_pub,32);
    for(unsigned i=0;i<2;++i) {
        std::memcpy(install.acl[i].ed_pub,ctl[i].ed_pub,32);
        install.acl[i].role=kRadminRoleOwner; install.epoch_set[i]=true; install.epoch[i]=101+i;
    }
    remote_session_install(s,install); require(remote_session_accepting(s));
    uint8_t base[2][32]{}, session[2][32]{};
    for(unsigned i=0;i<2;++i) {
        uint8_t shared[32]{};
        require(remote_ecdh_shared(shared,ctl[i],root.x_pub)==RemoteStatus::ok);
        require(remote_kdf_base(base[i],shared,ctl[i].ed_pub,root.ed_pub)==RemoteStatus::ok);
        require(remote_kdf_session(session[i],base[i],s.epoch[i])==RemoteStatus::ok);
    }
    ReplyRoute route{}; route.carrier=static_cast<uint8_t>(RadminCarrierKind::same_layer);
    const auto reply_carrier=remote_reply_carrier(route);
    RemoteCarrier request_carrier{}; request_carrier.outer_data_type=DATA_TYPE_REMOTE_CMD;
    request_carrier.dst_hash_on_wire=true;
    auto receive=[&](unsigned slot,uint64_t id,RemoteCmdOpcode op) {
        RemoteMessage msg{}; msg.outer_type=DATA_TYPE_REMOTE_CMD; msg.slot=slot;
        msg.opcode=static_cast<uint8_t>(op); msg.request_id=id;
        const uint8_t text[]={'s','t','a','t','u','s'};
        auto body=op==RemoteCmdOpcode::auth_execute ? std::span<const uint8_t>(text,sizeof text) : std::span<const uint8_t>{};
        uint8_t encoded[233]{}; size_t written=0;
        require(remote_body_encode(encoded,written,msg,body,RemoteKeys{{},session[slot]},
            {true,ctl[slot].key_hash32},request_carrier)==RemoteStatus::ok);
        RemoteRxInput in{}; in.now_ms=100; in.outer_type=DATA_TYPE_REMOTE_CMD;
        in.body={encoded,written}; in.source={true,ctl[slot].key_hash32}; in.route=route;
        in.request_carrier=request_carrier; in.reply_carrier=reply_carrier;
        RemoteRxResult out{}; remote_session_receive(s,in,out); return out;
    };
    size_t cap=0; require(remote_body_cap(reply_carrier,cap)==RemoteStatus::ok);
    for(unsigned i=0;i<kRadminSeenSlots;++i) {
        const auto in=receive(0,1000+i,RemoteCmdOpcode::auth_execute); require(in.verdict==RemoteAdmitVerdict::admit);
        require(remote_transcript_reserve(s,in.seen_index,cap-kRemoteOverheadAuthResponse));
        remote_transcript_complete(s,in.seen_index,RemoteTerminal::completed);
        require(receive(0,1000+i,RemoteCmdOpcode::response_ack).verdict==RemoteAdmitVerdict::ack_released);
    }
    require(remote_session_seen_used(s)==16);
    require(receive(1,9000,RemoteCmdOpcode::auth_execute).verdict==RemoteAdmitVerdict::session_full);
    const uint64_t unchanged_epoch=s.epoch[1];
    RemoteMessage response{}; response.outer_type=DATA_TYPE_REMOTE_RESP;
    response.slot=1; response.opcode=static_cast<uint8_t>(RemoteRespOpcode::terminal); response.request_id=9000;
    RemoteLayout layout{}; require(remote_layout(response.outer_type,remote_ctl(response.opcode,response.slot),layout)==RemoteStatus::ok);
    uint8_t nonce_full[24]{}, nonce_done[24]{};
    require(remote_nonce(nonce_full,layout,response,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    uint8_t hypothetical_full[233]{}, completed[233]{}; size_t full_len=0,done_len=0;
    const uint8_t full_code=static_cast<uint8_t>(RemoteTerminal::session_full);
    require(remote_body_encode(hypothetical_full,full_len,response,{&full_code,1},RemoteKeys{{},session[1]},
        {true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    RemoteSessionInstall rotation{}; rotation.epoch_set[0]=true; rotation.epoch[0]=103;
    remote_session_install(s,rotation); require(s.epoch[1]==unchanged_epoch); require(remote_session_seen_used(s)==0);
    const auto retry=receive(1,9000,RemoteCmdOpcode::auth_execute); require(retry.verdict==RemoteAdmitVerdict::admit);
    require(remote_transcript_reserve(s,retry.seen_index,cap-kRemoteOverheadAuthResponse));
    remote_transcript_complete(s,retry.seen_index,RemoteTerminal::completed);
    require(remote_transcript_encode(s,retry.seen_index,completed,done_len)==RemoteStatus::ok);
    require(remote_nonce(nonce_done,layout,response,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(std::memcmp(nonce_full,nonce_done,24)==0); require(full_len==done_len);
    require(std::memcmp(hypothetical_full,completed,full_len)!=0);
    require((hypothetical_full[10]^completed[10])==(full_code^static_cast<uint8_t>(RemoteTerminal::completed)));
    std::printf("shared-pool proof: slot1 session_full -> rotate slot0 -> exact slot1 request admits; same slot1 epoch, terminal nonce equal, plaintext differs\n");

    // Labelled QA synthetic admission notices: no production target notice emitter exists yet.
    RemoteMessage notice=response;
    notice.opcode=static_cast<uint8_t>(RemoteRespOpcode::admission_result);
    notice.request_ctl=remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::auth_execute),notice.slot);
    notice.admission_code=RemoteAdmission::session_full;
    RemoteLayout nl{}; require(remote_layout(notice.outer_type,remote_ctl(notice.opcode,notice.slot),nl)==RemoteStatus::ok);
    uint8_t nn[24]{}, bn[24]{};
    require(remote_nonce(nn,nl,notice,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(response.response_seq==0); require(std::memcmp(nn,nonce_done,24)!=0);
    uint8_t nw[28]{}, repeat[28]{}; size_t nwlen=0, repeatlen=0;
    require(remote_body_encode(nw,nwlen,notice,{},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    require(remote_body_encode(repeat,repeatlen,notice,{},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    require(nwlen==28 && repeatlen==nwlen); require(std::memcmp(nw,repeat,nwlen)==0);
    RemoteDecoded nd{};
    require(remote_body_decode(nd,notice.outer_type,{nw,nwlen},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier,{})==RemoteStatus::ok);
    require(nd.result_kind==RemoteResultKind::admission && nd.msg.admission_code==RemoteAdmission::session_full && nd.authenticated);
    notice.request_ctl=remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover),notice.slot);
    notice.admission_code=RemoteAdmission::session_busy; notice.admission_detail=2;
    require(remote_nonce(nn,nl,notice,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(remote_body_encode(nw,nwlen,notice,{},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    notice.admission_detail=1;
    require(remote_nonce(bn,nl,notice,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(remote_body_encode(repeat,repeatlen,notice,{},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    require(std::memcmp(nn,bn,24)!=0); require(std::memcmp(nw,repeat,28)!=0);
    require(remote_body_decode(nd,notice.outer_type,{repeat,repeatlen},RemoteKeys{{},session[1]},{true,ctl[1].key_hash32},reply_carrier,{})==RemoteStatus::ok);
    require(nd.result_kind==RemoteResultKind::admission && nd.msg.admission_detail==1);
    std::printf("new domain proof: synthetic full notice nonce differs from real completed seq-zero transcript after shared-pool recovery; identical notice repeats; busy 2 -> 1 changes nonce and wire\n");
    response.request_id=9001;
    uint8_t busy2[2]={static_cast<uint8_t>(RemoteTerminal::session_busy),2};
    uint8_t busy1[2]={static_cast<uint8_t>(RemoteTerminal::session_busy),1};
    uint8_t first[233]{}, second[233]{}; size_t first_len=0,second_len=0;
    require(remote_body_encode(first,first_len,response,busy2,RemoteKeys{{},session[1]},
        {true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    require(remote_body_encode(second,second_len,response,busy1,RemoteKeys{{},session[1]},
        {true,ctl[1].key_hash32},reply_carrier)==RemoteStatus::ok);
    require(first_len==second_len); require(first[10]==second[10]); require((first[11]^second[11])==(2^1));
    require(remote_nonce(nonce_full,layout,response,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(remote_nonce(nonce_done,layout,response,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(std::memcmp(nonce_full,nonce_done,24)==0);
    std::printf("synthetic busy-count proof: same request/epoch/seq; count 2 -> 1; nonce equal; ciphertext XOR reveals detail XOR\n");
    response.opcode=static_cast<uint8_t>(RemoteRespOpcode::protocol_error);
    require(remote_layout(response.outer_type,remote_ctl(response.opcode,response.slot),layout)==RemoteStatus::ok);
    require(remote_nonce(nonce_done,layout,response,session[1],{true,ctl[1].key_hash32})==RemoteStatus::ok);
    require(std::memcmp(nonce_full,nonce_done,24)!=0);
    std::printf("existing protocol-error domain separation control holds\nPASS: %u checks\n",checks);
}
