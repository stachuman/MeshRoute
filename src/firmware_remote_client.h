// MeshRoute — controller console and local-delivery seams. No resident sink/service binding.
#pragma once
#include "remote_client.h"
#include "firmware_admin_client_verbs.h"
#include "console_line.h"
#include "frame_codec.h"
#include "command.h"
#include "console_json.h"
#include <cstdio>
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
    if (c.ack && c.opcode!=meshroute::RemoteCmdOpcode::auth_execute) return false;
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
struct CarrierDetail {
    uint8_t origin, reporter, layer;
    const char* reason;
};
// Shared bounded formatter: native goldens and the device delivery use these exact bytes.
inline size_t remote_client_carrier_format(char* line,size_t cap,meshroute::RemoteLocalTransport transport,
                                         const char* key,const char* event,uint16_t ctr,const CarrierDetail* detail) {
    using namespace meshroute;
    if (transport==RemoteLocalTransport::ble) {
        EventField f[]={EF_S("id",key),EF_S("event",event),EF_I("ctr",ctr),
            EF_I("origin",detail?detail->origin:0),EF_I("reporter",detail?detail->reporter:0),
            EF_I("layer",detail?detail->layer:0),EF_S("reason",detail?detail->reason:"")};
        const auto n=console::write_event(line,cap,"remote_carrier",f,detail?7:3);
        return n<=244?n:0;
    }
    const int n=detail?std::snprintf(line,cap,"> remote %s carrier %s ctr=%u origin=%u reporter=%u layer=%u reason=%s\n",
        key,event,ctr,detail->origin,detail->reporter,detail->layer,detail->reason):
        std::snprintf(line,cap,"> remote %s carrier %s ctr=%u\n",key,event,ctr);
    return n>0 && static_cast<size_t>(n)<cap?static_cast<size_t>(n):0;
}

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
    bool carrier(meshroute::RemoteLocalTransport,uint64_t,const char* event,uint16_t,const CarrierDetail*);
private:
    bool write(meshroute::RemoteLocalTransport,const char*,size_t);
    uint8_t utf8_tail_[4]{}; // stack adapter only: carry a code point across transcript chunks
    uint8_t utf8_tail_len_ = 0;
    Print& usb_; Print* ble_; bool ble_up_; uint32_t (*drops_)(void*); void* ctx_;
};
// Call-scoped lookup: the observer retains neither the Node nor the callback.
using RemoteClientBindLookup = int (*)(void*, uint32_t);
int remote_client_bind_lookup(void* node, uint32_t hash);
// ONE call per already-rendered push. Parse with the existing codec; retain neither event nor sink.
template<class Delivery>
inline void remote_client_observe_push(const meshroute::RemoteClientState& state, const meshroute::Push& pu, Delivery& delivery,
                                       RemoteClientBindLookup lookup, void* node) {
    using namespace meshroute;
    uint64_t id = 0; uint32_t target_hash = 0; uint16_t ctr = pu.ctr; const char* event = nullptr;
    CarrierDetail detail{}; const CarrierDetail* fields = nullptr;
    if (pu.kind == PushKind::send_e2e_acked) {
        id = remote_client_observe_ack(state, ctr, false, pu.dst, pu.sender_hash, &target_hash); event = "acked";
    } else if (pu.kind == PushKind::send_failed && pu.reason == SendFailReason::e2e_ack_timeout) {
        id = remote_client_observe_ack(state, ctr, true, pu.dst, pu.sender_hash, &target_hash); event = "ack_timeout";
    } else if (pu.kind == PushKind::custody_failure) {
        const std::span<const uint8_t> body{pu.body, pu.body_len};
        const auto record = parse_custody_failure(body);
        if (!record || !custody_record_is_translated(record->notice_flags)) return;
        const auto tail = parse_custody_translated_tail(body, *record);
        if (!tail) return;
        ctr = tail->mobile_ctr;
        id = remote_client_observe_custody(state, ctr, record->failed_type,
            static_cast<uint8_t>(tail->target_kind), tail->target_value);
        event = "custody_failure";
        detail = {record->failed_origin, tail->original_reporter, record->reporter_layer,
                  console::custodyreason_name(record->terminal_reason)};
        fields = &detail;
    }
    if (!id) return;
    for (const auto& row : state.pending) if (row.core.request_id == id) {
        if (pu.kind == PushKind::send_e2e_acked && !row.core.route.hop_count) {
            const int bound = lookup(node, target_hash);
            if (bound >= 0 && bound != pu.dst) return;
        }
        (void)delivery.carrier(static_cast<RemoteLocalTransport>(row.core.local_transport), id, event, ctr, fields);
        return;
    }
}
inline void remote_client_observe_push(const meshroute::RemoteClientState& state, const meshroute::Push& pu,
                                      Print& usb, Print* ble, bool ble_connected,
                                      RemoteClientBindLookup lookup, void* node) {
    RemoteClientDelivery delivery(usb, ble, ble_connected);
    remote_client_observe_push(state, pu, delivery, lookup, node);
}
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
