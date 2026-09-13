// QA B386 AUTHOR REPRODUCTION ONLY, base 564f460.
// Reuses the committed B379 fixture setup; real session functions, no control producer or production edit.
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
    uint8_t seen[3]{};
    for (unsigned i=0; i<3; ++i) {
        const auto in=receive(i==0 ? 0 : 1,1000+i,RemoteCmdOpcode::auth_execute);
        require(in.verdict==RemoteAdmitVerdict::admit); seen[i]=in.seen_index;
        require(remote_transcript_reserve(s,in.seen_index,cap-kRemoteOverheadAuthResponse));
        const uint8_t output[]={0x31,0x00,static_cast<uint8_t>(i)};
        remote_transcript_append(s,in.seen_index,output,sizeof output);
        remote_transcript_complete(s,in.seen_index,RemoteTerminal::completed);
        remote_transcript_sent(s,in.seen_index); // retained, nonzero pending cursor
    }
    const auto before=s;
    const auto first=s.seen[seen[0]].record.transcript_slot;
    require(s.transcripts[first].order==0);
    RemoteSessionInstall rotation{}; rotation.epoch_set[0]=true; rotation.epoch[0]=103;
    remote_session_install(s,rotation);
    require(s.epoch[0]==103); require(s.epoch[1]==before.epoch[1]);
    require(remote_session_seen_used(s)==2);
    require(s.seen[seen[0]].record.state==static_cast<uint8_t>(SeenState::free));
    require(s.transcripts[first].state==static_cast<uint8_t>(TranscriptState::free));
    require(std::memcmp(s.ingress,before.ingress,sizeof s.ingress)==0);
    for(unsigned i=1;i<3;++i) {
        const auto si=seen[i]; const auto ti=s.seen[si].record.transcript_slot;
        require(std::memcmp(&s.seen[si],&before.seen[si],sizeof s.seen[si])==0);
        require(s.transcripts[ti].order==i-1);
        require(before.transcripts[ti].order==i);
        require(std::memcmp(&s.transcripts[ti],&before.transcripts[ti],sizeof s.transcripts[ti])!=0);
        auto expected=before.transcripts[ti]; --expected.order;
        require(std::memcmp(&s.transcripts[ti],&expected,sizeof expected)==0);
        const auto ci=s.transcripts[ti].first_chunk;
        require(std::memcmp(&s.chunks[ci],&before.chunks[ci],sizeof s.chunks[ci])==0);
        uint8_t old_wire[233]{}, new_wire[233]{}; size_t old_len=0,new_len=0;
        require(remote_transcript_encode(before,si,old_wire,old_len)==RemoteStatus::ok);
        require(remote_transcript_encode(s,si,new_wire,new_len)==RemoteStatus::ok);
        require(old_len==new_len && std::memcmp(old_wire,new_wire,old_len)==0);
        std::printf("survivor %u: order %u -> %u; full header differs; header minus order, seen, output chunk and pending seq1 wire unchanged\n",i,before.transcripts[ti].order,s.transcripts[ti].order);
    }
    require(remote_transcript_next(s)==seen[1]);
    std::printf("PASS: %u checks\n",checks);
}
