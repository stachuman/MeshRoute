// QA B388 comment reproduction only; frozen uncommitted 7b-2 atop 564f460.
// Reuses B386 provisioning; real codec/intake/expiry scan; no Node timer or radio claim.
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
    RemoteCarrier carrier{}; carrier.outer_data_type=DATA_TYPE_REMOTE_CMD;
    carrier.dst_hash_on_wire=true; carrier.source_hash_on_wire=true;
    auto receive=[&](uint32_t source,uint64_t now,uint64_t id) {
        RemoteMessage msg{}; msg.outer_type=DATA_TYPE_REMOTE_CMD; msg.slot=kRemoteSlotSentinel;
        msg.opcode=static_cast<uint8_t>(RemoteCmdOpcode::open_execute); msg.request_id=id;
        const uint8_t line[]={'s','t','a','t','u','s'}; uint8_t bytes[233]{}; size_t n=0;
        require(remote_body_encode(bytes,n,msg,line,{}, {true,source},carrier)==RemoteStatus::ok);
        RemoteRxInput in{}; in.now_ms=now; in.outer_type=DATA_TYPE_REMOTE_CMD;
        in.source={true,source}; in.body={bytes,n}; in.request_carrier=carrier;
        RemoteRxResult out{}; remote_session_receive(s,in,out); return out;
    };
    require(receive(11,1000,1).verdict==RemoteAdmitVerdict::open_staged);
    require(receive(22,2000,2).verdict==RemoteAdmitVerdict::open_staged);
    const auto before=s; const auto old_deadline=remote_session_earliest_deadline(s);
    require(old_deadline==301000);
    require(receive(22,301000,3).verdict==RemoteAdmitVerdict::open_peer_bound);
    require(s.inbound_refusal==before.inbound_refusal+1);
    require(s.open_rate_refusal==before.open_rate_refusal);
    require(s.staging[0].kind==static_cast<uint8_t>(OpenStagingKind::free));
    const OpenCapture empty{}; require(std::memcmp(&s.open[0],&empty,sizeof empty)==0);
    require(std::memcmp(&s.staging[1],&before.staging[1],sizeof s.staging[1])==0);
    require(std::memcmp(&s.open[1],&before.open[1],sizeof s.open[1])==0);
    const auto new_deadline=remote_session_earliest_deadline(s); require(new_deadline==302000);
    std::printf("B388: refused same-peer input; inbound %u -> %u; earlier row/capture wiped; surviving row unchanged; earliest deadline %llu -> %llu; %u checks PASS\n",before.inbound_refusal,s.inbound_refusal,(unsigned long long)old_deadline,(unsigned long long)new_deadline,checks);
}
