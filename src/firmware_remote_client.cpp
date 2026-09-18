#include "firmware_remote_client.h"
#include <Arduino.h>
#include "console_json.h"
#include "node.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace mrfw {
using namespace meshroute;
int remote_client_bind_lookup(void* node, uint32_t hash) {
    return static_cast<const Node*>(node)->id_bind_find_by_hash(hash);
}
void remote_id_format(char out[17],uint64_t id) {
    static constexpr char hex[]="0123456789abcdef";
    for (unsigned i=0;i<16;++i) out[i]=hex[(id>>((15-i)*4))&15];
    out[16]='\0';
}
bool RemoteClientDelivery::connected(RemoteLocalTransport t) const { return t==RemoteLocalTransport::usb || (ble_ && ble_up_); }
bool RemoteClientDelivery::write(RemoteLocalTransport t,const char* b,size_t n) {
    if (!connected(t)) return false;
    Print& out=t==RemoteLocalTransport::usb?usb_:*ble_;
    const auto before=drops_?drops_(ctx_):0;
    const auto written=out.write(reinterpret_cast<const uint8_t*>(b),n);
    return written==n && (t!=RemoteLocalTransport::usb || !drops_ || drops_(ctx_)==before);
}
bool RemoteClientDelivery::output(RemoteLocalTransport t,uint64_t id,uint16_t& seq,std::span<const uint8_t> bytes) {
    char key[17]; remote_id_format(key,id);
    if (t==RemoteLocalTransport::usb) {
        char prefix[40]; const int n=snprintf(prefix,sizeof prefix,"> remote %s out ",key);
        return n>0 && static_cast<size_t>(n)<sizeof prefix && write(t,prefix,static_cast<size_t>(n)) &&
            write(t,reinterpret_cast<const char*>(bytes.data()),bytes.size()) && write(t,"\n",1);
    }
    // EventField's existing string contract is NUL-terminated. A binary/NUL output cannot be claimed delivered.
    // Refuse and retain it for USB instead of truncating it or acknowledging bytes the companion never saw.
    for (auto b:bytes) if (!b) return false;
    const size_t previous=utf8_tail_len_;
    const size_t total=previous+bytes.size();
    auto byte_at=[&](size_t i) { return i<previous ? utf8_tail_[i] : bytes[i-previous]; };
    // Keep the only possible incomplete trailing code point for the next transcript chunk. Validation
    // stays in the existing JSON writer; this merely preserves a sequence across packet boundaries.
    size_t end=total;
    if(total) {
        size_t lead=total-1;
        while(lead && total-lead<4 && (byte_at(lead)&0xc0)==0x80) --lead;
        const auto first=byte_at(lead);
        const size_t width=(first&0xe0)==0xc0?2:(first&0xf0)==0xe0?3:(first&0xf8)==0xf0?4:1;
        if(width>total-lead) end=lead;
    }
    size_t off=0;
    if(end || !total) do {
        // Worst escaping is six bytes per input byte; 24 leaves room for ID and any local sequence.
        size_t take=std::min(size_t{24},end-off);
        if(off+take<end) while(take && (byte_at(off+take)&0xc0)==0x80) --take;
        if(!take && off<end) return false;
        char body[25]{};
        for(size_t i=0;i<take;++i) body[i]=static_cast<char>(byte_at(off+i));
        char line[245]; EventField f[]={EF_S("id",key),EF_I("seq",seq),EF_S("body",body)};
        const auto n=console::write_event(line,sizeof line,"remote_output",f,3);
        if (!n || n>244 || !write(t,line,n)) return false;
        ++seq; off+=take;
    } while(off<end);
    uint8_t tail[4]{};
    for(size_t i=end;i<total;++i) tail[i-end]=byte_at(i);
    std::memcpy(utf8_tail_,tail,sizeof tail);
    utf8_tail_len_=static_cast<uint8_t>(total-end);
    return true;
}
bool RemoteClientDelivery::terminal(RemoteLocalTransport t,uint64_t id,const char* name,uint32_t detail,bool scheduled) {
    if (t==RemoteLocalTransport::ble && utf8_tail_len_) return false;
    char key[17]; remote_id_format(key,id); char line[245];
    if (t==RemoteLocalTransport::ble) {
        EventField f[]={EF_S("id",key),EF_S("result",name),EF_I(scheduled?"activation_ms":"detail",detail)};
        const auto n=console::write_event(line,sizeof line,"remote_terminal",f,(scheduled || detail)?3:2);
        return n && n<=244 && write(t,line,n);
    }
    const int n=(scheduled || detail)?snprintf(line,sizeof line,"> remote %s %s %s=%lu\n",key,name,scheduled?"activation_ms":"detail",static_cast<unsigned long>(detail)):
                                      snprintf(line,sizeof line,"> remote %s %s\n",key,name);
    return n>0 && static_cast<size_t>(n)<sizeof line && write(t,line,static_cast<size_t>(n));
}
void RemoteClientDelivery::retained(RemoteLocalTransport t,uint64_t id) {
    char key[17]; remote_id_format(key,id); char line[96];
    if (t==RemoteLocalTransport::ble) {
        EventField f[]={EF_S("id",key)};
        const auto n=console::write_event(line,sizeof line,"remote_retained",f,1); if (n) (void)write(t,line,n);
    } else {
        const int n=snprintf(line,sizeof line,"> remote %s retained\n",key); if (n>0) (void)write(t,line,static_cast<size_t>(n));
    }
}
bool RemoteClientDelivery::carrier(RemoteLocalTransport t,uint64_t id,const char* event,uint16_t ctr,const CarrierDetail* detail) {
    char key[17]; remote_id_format(key,id); char line[245];
    const auto n=remote_client_carrier_format(line,sizeof line,t,key,event,ctr,detail);
    return n && write(t,line,n);
}
namespace {
void error(Print& out,RemoteLocalTransport t,const char* reason) {
    if (t==RemoteLocalTransport::ble) {
        char b[160];const auto n=console::write_err(b,sizeof b,"remote",reason);if(n)out.write(reinterpret_cast<const uint8_t*>(b),n);
    } else { out.print(F("> remote err "));out.print(reason);out.write(static_cast<uint8_t>('\n')); }
}
void acknowledgement(Print& out,RemoteLocalTransport t,const char* verb,uint64_t id) {
    char key[17];remote_id_format(key,id);
    if(t==RemoteLocalTransport::ble) {
        char b[96];console::JsonBuf j(b,sizeof b);j.ch('{');j.key("ack");j.str(verb,strlen(verb));j.ch(',');j.key("id");j.str(key,16);j.ch('}');
        const auto n=j.finish();if(n)out.write(reinterpret_cast<const uint8_t*>(b),n);
    } else {out.print(F("> "));out.print(verb);out.write(static_cast<uint8_t>(' '));out.print(key);out.println(F(" accepted"));}
}
}
void remote_client_command(RemoteClientServices& svc,const char* line,size_t len,RemoteLocalTransport t,Print& out,RemoteClientDelivery& delivery) {
    RemoteLocalCommand c{};
    if(!remote_local_parse(line,len,c)){error(out,t,"bad_args");return;}
    if(c.verb!=RemoteLocalVerb::execute) {
        RemoteClientError e=RemoteClientError::none;
        switch(c.verb) {
            case RemoteLocalVerb::retry:e=remote_client_retry(svc.state,c.id,svc.now,svc.entropy,svc.entropy_ctx);break;
            case RemoteLocalVerb::show:e=remote_client_show(svc.state,c.id,t,svc.now,delivery);break;
            case RemoteLocalVerb::ack:e=remote_client_ack(svc.state,c.id,svc.now);break;
            case RemoteLocalVerb::execute:break;
        }
        if(e!=RemoteClientError::none)error(out,t,remote_client_error_name(e));
        else if(c.verb!=RemoteLocalVerb::show)acknowledgement(out,t,c.verb==RemoteLocalVerb::ack?"remote-ack":"remote-retry",c.id);
        return;
    }
    const auto state=svc.targets.read(svc.scratch);
    if(state!=TargetState::ok){error(out,t,state==TargetState::io_failed?"store_io_failed":"store_invalid");return;}
    const auto mask=target_mask_by_label(svc.scratch,c.label,static_cast<uint8_t>(c.label_len));
    uint8_t slot=0;
    const auto pick=target_pick_from_mask(mask,slot);
    if(pick!=TargetPick::one){error(out,t,pick==TargetPick::many?"ambiguous_target":"unknown_target");return;}
    const auto& row=svc.scratch.rec[slot];
    Identity identity{};SecretWipeGuard<Identity> guard{identity};
    if(c.opcode!=RemoteCmdOpcode::open_execute) {
        if(c.credential==kRemoteClientSelf)identity=svc.self;
        else {
            auto seed=svc.keys.export_seed(c.credential);SecretWipeGuard<MgmtKeySeed> wipe{seed};
            if(!seed.ok){error(out,t,mgmt_key_err_name(seed.err));return;}
            identity_from_seed(identity,seed.seed);
        }
    }
    if(c.command && (admin_primary_is(c.command,c.command_len,"prep-restart") || admin_primary_is(c.command,c.command_len,"prep_restart"))) {
        static constexpr char warning[]="prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.";
        if(t==RemoteLocalTransport::ble){char b[245];EventField f[]={EF_S("body",warning)};const auto n=console::write_event(b,sizeof b,"remote_warning",f,1);if(n)out.write(reinterpret_cast<const uint8_t*>(b),n);}
        else {out.println(warning);}
    }
    RemoteClientRequest req{};req.identity=&identity;req.target_pub=row.admin_pub;
    req.command={reinterpret_cast<const uint8_t*>(c.command),c.command_len};req.source_hash=svc.self.key_hash32;
    req.route.target_hash=row.key_hash32;req.route.hop_count=row.hop_count;std::memcpy(req.route.hops,row.hops,3);
    req.credential_slot=c.credential;req.target_book_slot=slot;req.transport=t;req.opcode=c.opcode;req.e2e_ack=c.ack;
    req.correlation_free=svc.correlation_free;req.carrier=svc.carrier_kind;
    const auto result=remote_client_start(svc.state,req,svc.now,svc.entropy,svc.entropy_ctx,svc.carrier);
    if(result.error!=RemoteClientError::none)error(out,t,remote_client_error_name(result.error));
    else acknowledgement(out,t,"remote",result.request_id);
}
void remote_client_status(Print& out,const RemoteClientState& s) {
    const auto& c=s.counters;
    out.print(F(" radmin_client_request_table_pressure="));out.print(c.request_table_pressure);
    out.print(F(" radmin_client_assembly_failure="));out.print(c.assembly_failure);
    out.print(F(" radmin_client_unmatched_response="));out.print(c.unmatched_response);
    out.print(F(" radmin_client_auth_failure="));out.print(c.auth_failure);
    out.print(F(" radmin_client_local_result_pressure="));out.print(c.local_result_pressure);
    out.print(F(" radmin_client_radio_enqueue_failure="));out.print(c.radio_enqueue_failure);
    out.print(F(" radmin_client_pending="));out.print(remote_client_pending_count(s));
    out.print(F(" radmin_client_retained="));out.print(remote_client_retained_count(s));
    out.print(F(" radmin_client_ack_debt="));out.print(remote_client_ack_debt_count(s));
}
} // namespace mrfw
