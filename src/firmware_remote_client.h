// MeshRoute — controller console and local-delivery seams. No resident sink/service binding.
#pragma once
#include "remote_client.h"
#include "firmware_admin_client_verbs.h"
#include "console_line.h"
class Print;
namespace mrfw {
enum class RemoteLocalVerb : uint8_t { execute, retry, show, ack };
struct RemoteLocalCommand {
    RemoteLocalVerb verb = RemoteLocalVerb::execute;
    const char* label = nullptr;
    size_t label_len = 0;
    const char* command = nullptr;
    size_t command_len = 0;
    uint64_t id = 0;
    uint8_t credential = meshroute::kRemoteClientSelf;
    meshroute::RemoteCmdOpcode opcode = meshroute::RemoteCmdOpcode::auth_execute;
    bool ack = false;
};
inline bool remote_client_owns(const char* line, size_t len) {
    return admin_primary_is(line,len,"remote") || admin_primary_is(line,len,"remote-retry") ||
           admin_primary_is(line,len,"remote-result") || admin_primary_is(line,len,"remote-ack");
}
inline bool remote_id_parse(const char* s,size_t n,uint64_t& out) {
    if (n!=16) return false;
    uint64_t v=0;
    for (size_t i=0;i<n;++i) {
        const char c=s[i];
        if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
        v=(v<<4)|static_cast<unsigned>(c<='9'?c-'0':c-'a'+10);
    }
    if (!v) return false;
    out=v; return true;
}
inline bool remote_local_parse(const char* line,size_t len,RemoteLocalCommand& out) {
    RemoteLocalCommand c{}; size_t i=0; const char* t=nullptr; size_t n=0;
    if (!admin_next_token(line,len,i,t,n)) return false;
    if (!admin_word_is(t,n,"remote")) {
        if (admin_word_is(t,n,"remote-retry")) c.verb=RemoteLocalVerb::retry;
        else if (admin_word_is(t,n,"remote-result")) c.verb=RemoteLocalVerb::show;
        else if (admin_word_is(t,n,"remote-ack")) c.verb=RemoteLocalVerb::ack;
        else return false;
        if (c.verb==RemoteLocalVerb::show && (!admin_next_token(line,len,i,t,n) || !admin_word_is(t,n,"show"))) return false;
        if (!admin_next_token(line,len,i,t,n) || !remote_id_parse(t,n,c.id) || !admin_tail_empty(line,len,i)) return false;
        out=c; return true;
    }
    if (!admin_next_token(line,len,i,c.label,c.label_len) || c.label_len>16 || !target_label_valid(c.label,static_cast<uint8_t>(c.label_len))) return false;
    bool enc=false,open=false,selected=false,tail=false;
    while (admin_next_token(line,len,i,t,n)) {
        if (admin_word_is(t,n,"-e")) enc=true;
        else if (admin_word_is(t,n,"-a")) c.ack=true;
        else if (admin_word_is(t,n,"open")) open=true;
        else if (admin_word_is(t,n,"--")) {
            while (i<len && (line[i]==' ' || line[i]=='\t')) ++i;
            c.command=line+i; c.command_len=len-i; tail=true; break;
        } else if (admin_word_is(t,n,"rollover")) {
            c.opcode=meshroute::RemoteCmdOpcode::safe_rollover;
            if (admin_next_token(line,len,i,t,n)) {
                if (!admin_word_is(t,n,"confirm") || !admin_tail_empty(line,len,i)) return false;
                c.opcode=meshroute::RemoteCmdOpcode::force_rollover;
            }
            tail=true; break;
        } else {
            const char* v=nullptr; size_t vn=0;
            if (selected || !admin_client_kv(t,n,"using",v,vn)) return false;
            if (!admin_word_is(v,vn,"self") && !admin_client_key_slot(v,vn,c.credential)) return false;
            selected=true;
        }
    }
    // -a/-e are the same lone tokens as console_parse::parse_send_tail, with independent meanings.
    if (!tail || enc==open || (open && (c.ack || selected || c.opcode!=meshroute::RemoteCmdOpcode::auth_execute))) return false;
    if (c.opcode==meshroute::RemoteCmdOpcode::auth_execute) {
        if (!c.command_len || meshroute::console::validate_command_line(c.command,c.command_len,meshroute::console::remote_command_max_bytes)!=meshroute::console::LineErr::ok) return false;
        if (open) {
            if (!(admin_word_is(c.command,c.command_len,"status") || admin_word_is(c.command,c.command_len,"routes"))) return false;
            c.opcode=meshroute::RemoteCmdOpcode::open_execute;
        }
    }
    out=c; return true;
}
void remote_id_format(char out[17],uint64_t id);

// Each object is a stack adapter. Drop accounting observes the complete write, including its newline.
class RemoteClientDelivery final : public meshroute::IRemoteLocalDelivery {
public:
    RemoteClientDelivery(Print& usb,Print* ble,bool ble_up,uint32_t (*drops)(void*)=nullptr,void* ctx=nullptr)
        : usb_(usb),ble_(ble),ble_up_(ble_up),drops_(drops),ctx_(ctx) {}
    void begin_result() override { utf8_tail_len_ = 0; }
    bool connected(meshroute::RemoteLocalTransport) const override;
    bool output(meshroute::RemoteLocalTransport,uint64_t,uint16_t&,std::span<const uint8_t>) override;
    bool terminal(meshroute::RemoteLocalTransport,uint64_t,const char*,uint32_t,bool) override;
    void retained(meshroute::RemoteLocalTransport,uint64_t) override;
private:
    bool write(meshroute::RemoteLocalTransport,const char*,size_t);
    uint8_t utf8_tail_[4]{}; // stack adapter only: carry a code point across transcript chunks
    uint8_t utf8_tail_len_ = 0;
    Print& usb_; Print* ble_; bool ble_up_; uint32_t (*drops_)(void*); void* ctx_;
};
struct RemoteClientServices {
    meshroute::RemoteClientState& state;
    const meshroute::Identity& self;
    MgmtKeyService& keys;
    TargetService& targets;
    mrnv::TargetBlob& scratch;
    meshroute::IRadminCarrier& carrier;
    meshroute::RemoteEntropyFn entropy;
    void* entropy_ctx;
    uint32_t now;
    uint8_t correlation_free;
    uint8_t carrier_kind;
};
// Owned verb recognition happens at the real router; this seam resolves stores and invokes the pure core.
void remote_client_command(RemoteClientServices&,const char*,size_t,meshroute::RemoteLocalTransport,Print&,RemoteClientDelivery&);
void remote_client_status(Print&,const meshroute::RemoteClientState&);
} // namespace mrfw
