// 8ac: host executes the production router and emitter. NV, entropy and local transport are factual fakes.
#include "firmware_remote_client.h"
#if MR_FEAT_RADMIN_CLIENT
static void remote_client_rows() {
    using namespace meshroute;
    auto& nv=mrprobe_nv(); nv=MrProbeNv{};nv.ns_present=true;nv.rw_ok=true;
    mrnv::TargetBlob book{};mrnv::target_blob_init(book);
    Identity target{};uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=uint8_t(i+77);
    identity_from_seed(target,seed);
    auto& row=book.rec[0];std::memcpy(row.admin_pub,target.ed_pub,32);row.key_hash32=target.key_hash32;
    std::memcpy(row.label,"alpha",5);row.label_len=5;row.flags=1;book.count=1;
    CHK(mrfw::target_content_valid(book),"C8-1 valid immutable target fixture");
    CHK(mrnv::save_targets(book),"C8-2 real target-store wrapper writes fixture");
    g_node.remote_client()={};mrprobe_rng()=MrProbeRng{};
    const mrfw::CommandContext usb{mrfw::CommandTransport::usb,mrfw::CommandAuthority::local,true,0,201};
    const mrfw::CommandContext ble{mrfw::CommandTransport::ble,mrfw::CommandAuthority::local,false,0,201};
    CaptureSink out;char reply[256]{};
    for(const auto& context:{usb,ble}) {
        out.reset();const char* line="remote alpha -e -- status";
        const auto ex=mrfw::exec_console_line(line,strlen(line),mrfw::LineFormat::text,out,reply,sizeof reply,context);
        CHK(ex.state==mrfw::LineExec::State::streamed,"C8-3 real router owns remote on each transport");
        const char* expected=context.transport==mrfw::CommandTransport::usb?"> remote err carrier_unavailable\n":"{\"err\":\"remote\",\"msg\":\"carrier_unavailable\"}\n";
        CHK(out.is(expected),"C8-4 board carrier refuses with transport-specific bytes [%s]",out.buf);
        CHK(!remote_client_busy(g_node.remote_client()),"C8-5 unavailable carrier releases reservations");
    }
    CHK(mrprobe_rng().draws==8,"C8-6 two cold admissions draw two fresh eight-byte IDs each");
    out.reset();mrfw::dispatch("remote missing -e -- status",27,out);
    CHK(out.has("unknown_target"),"C8-7 label lookup is authoritative");
    mrprobe_rng().force_zero=true;out.reset();mrfw::dispatch("remote alpha -e -- status",25,out);
    CHK(out.has("duplicate_id"),"C8-8 zero ID never reaches carrier");mrprobe_rng().force_zero=false;
    out.reset();mrfw::dispatch("remote alpha -e -- prep-restart",31,out);
    CHK(out.has("prep-restart stops mesh radio and remote administration."),"C8-9 prep-restart warning precedes admission");
    const char* controls[]={"remote-retry 0123456789abcdef","remote-result show 0123456789abcdef","remote-ack 0123456789abcdef"};
    for(const auto* line:controls){out.reset();CHK(mrfw::dispatch(line,strlen(line),out),"C8-10 control verb owned");CHK(out.has("not_found"),"C8-11 unmatched request fails loud");}
    // Selected key is expanded by the real firmware command module, then wiped on a carrier refusal.
    out.reset();const std::string import="admin-key import key4 "+std::string(64,'3');
    CHK(mrfw::dispatch(import.data(),import.size(),out) && out.has("imported key4"),"C8-K1 dedicated credential provisioned through real router");
    out.reset();wipe_watch_begin();
    const char* keyed="remote alpha -e using=key4 -- status";
    CHK(mrfw::dispatch(keyed,strlen(keyed),out),"C8-K2 selected credential reaches real command module");
    wipe_watch_end();bool identity_wiped=false,seed_wiped=false;
    for(int i=0;i<g_nwipes;++i){
        const auto& w=g_wipes[i];
        if(w.n==sizeof(Identity)){bool selected=true;for(auto b:w.before)selected &= b==0x33;identity_wiped |= selected && w.after_zero;}
        if(w.n==sizeof(mrfw::MgmtKeySeed))seed_wiped |= w.after_zero;
    }
    CHK(identity_wiped && seed_wiped,"C8-K3 selected seed and expanded identity wiped while their stack frames are alive");
    CHK(out.has("carrier_unavailable"),"C8-K4 selected key never falls through to open");
    auto& held=g_node.remote_client();held={};
    held.pending[0].core.state=uint8_t(RemoteClientPhase::response_wait);
    held.pending[0].core.credential_slot=4;held.pending[0].core.target_book_slot=0;
    out.reset();const char* remove_key="admin-key remove key4 confirm";mrfw::dispatch(remove_key,strlen(remove_key),out);
    CHK(out.has("in_use"),"C8-K5 real key removal sees captured credential ownership");
    out.reset();const char* remove_target="admin-target remove label=alpha confirm";mrfw::dispatch(remove_target,strlen(remove_target),out);
    CHK(out.has("in_use"),"C8-K6 real target removal sees captured route ownership");
    held={};held.counters={11,22,33,44,55,66};out.reset();mrfw::dispatch("status",6,out);
    const char* counter_values[]={"radmin_client_request_table_pressure=11", "radmin_client_assembly_failure=22",
        "radmin_client_unmatched_response=33", "radmin_client_auth_failure=44",
        "radmin_client_local_result_pressure=55", "radmin_client_radio_enqueue_failure=66"};
    for(const auto* value:counter_values) CHK(out.has(value),"C8-S1 real status carries exact counter %s",value);
    held={};
    // Labelled synthetic completed-result fixture: the core lifecycle and real Node intake have their own
    // codec-backed native tests. Here it makes the real console router/emitter, rather than a second model, run.
    auto stage=[&] {
        auto& s=g_node.remote_client();s={};
        auto& p=s.pending[0];p.core.state=uint8_t(RemoteClientPhase::complete);p.core.request_id=0x123456789abcdef;
        p.core.flags=uint8_t(RemoteCmdOpcode::open_execute);p.core.local_transport=uint8_t(RemoteLocalTransport::ble);
        auto& r=s.retained[0];r.request_id=p.core.request_id;r.in_use=2;r.first_chunk=0;r.bytes_total=5;
        r.result_domain=uint8_t(RemoteResultKind::terminal);r.result_code=uint8_t(RemoteTerminal::action_busy);r.reoffer_from_zero=1;
        s.chunks[0].len=5;s.chunks[0].next=kRemoteClientNoChunk;std::memcpy(s.chunks[0].bytes,"hello",5);
    };
    stage();out.reset();mrfw::dispatch("remote-ack 0123456789abcdef",27,out);
    CHK(out.has("incomplete"),"C8-12 local ACK before complete delivery refuses");
    for(const auto& context:{usb,ble}) {
        stage();out.reset();const char* line="remote-result show 0123456789abcdef";
        (void)mrfw::exec_console_line(line,strlen(line),mrfw::LineFormat::text,out,reply,sizeof reply,context);
        const char* expected=context.transport==mrfw::CommandTransport::usb?
            "> remote 0123456789abcdef out hello\n> remote 0123456789abcdef action_busy\n":
            "{\"ev\":\"remote_output\",\"id\":\"0123456789abcdef\",\"seq\":0,\"body\":\"hello\"}\n{\"ev\":\"remote_terminal\",\"id\":\"0123456789abcdef\",\"result\":\"action_busy\"}\n";
        CHK(out.is(expected),"C8-13 real show route emits exact plaintext envelope [%s]",out.buf);
        CHK(remote_client_busy(g_node.remote_client())==(context.transport==mrfw::CommandTransport::ble),"C8-14 BLE retains while USB accepts");
    }
    out.reset();mrfw::dispatch("remote-ack 0123456789abcdef",27,out);
    CHK(!remote_client_busy(g_node.remote_client()),"C8-15 real ACK router releases delivered BLE result");
    stage();out.reset();
    mrfw::remote_client_service_once(out,&out,false);
    CHK(out.n==0 && remote_client_busy(g_node.remote_client()),"C8-L1 disconnected main-loop delivery retains result");
    mrfw::remote_client_service_once(out,&out,true);
    CHK(out.has("hello") && out.has("remote_terminal"),"C8-L2 firmware service reaches real local delivery");
    const auto emitted=out.n;mrfw::remote_client_service_once(out,&out,true);
    CHK(out.n==emitted,"C8-L3 connected result waits for local ACK without duplicate output");
    mrfw::remote_client_service_once(out,&out,false);mrfw::remote_client_service_once(out,&out,true);
    CHK(out.n==emitted*2,"C8-L4 reconnect reoffers complete result from zero");
    // All owned row classes block ordinary identity regeneration through its actual firmware binding.
    for(unsigned kind=0;kind<4;++kind){
        auto& s=g_node.remote_client();s={};
        if(kind==0)s.pending[0].core.state=uint8_t(RemoteClientPhase::response_wait);
        if(kind==1)s.assemblies[0].in_use=1;
        if(kind==2)s.retained[0].in_use=1;
        if(kind==3)s.ack_debt[0].in_use=1;
        out.reset();mrfw::dispatch("regen",5,out);
        CHK(out.has("remote_busy"),"C8-16 real regen binding sees each owned row class [%s]",out.buf);
    }
    g_node.remote_client()={};
    // Sink acceptance and conservative escaping are production decisions, even without a radio binding.
    mrfw::RemoteClientDelivery delivery(out,&out,true);
    out.reset();uint16_t seq=0;std::string body(24,'"');
    CHK(delivery.output(RemoteLocalTransport::ble,1,seq,{reinterpret_cast<const uint8_t*>(body.data()),body.size()}),"C8-17 escaped BLE output admitted");
    CHK(out.n<=244 && seq==1,"C8-18 BLE line obeys ATT ceiling");
    out.reset();delivery.begin_result();seq=0;body=std::string(50,'w');
    CHK(delivery.output(RemoteLocalTransport::ble,1,seq,{reinterpret_cast<const uint8_t*>(body.data()),body.size()}),"C8-L5 multi-event output accepted");
    size_t copied=0;for(size_t i=0;i<out.n;++i)copied+=out.buf[i]=='w';
    CHK(copied==50 && out.has("\"seq\":1"),"C8-L6 every byte survives with contiguous local sequences");
    out.reset();CHK(delivery.terminal(RemoteLocalTransport::usb,1,"scheduled",7006,true),"C8-19 scheduled line accepted");
    CHK(out.is("> remote 0000000000000001 scheduled activation_ms=7006\n"),"C8-20 scheduled USB bytes exact");
    out.reset();delivery.begin_result();seq=0;
    const uint8_t first[]={0xf0,0x9f},second[]={0x8c,0x8d};
    CHK(delivery.output(RemoteLocalTransport::ble,1,seq,first) && out.n==0,"C8-21 partial UTF-8 waits for next transcript chunk");
    CHK(delivery.output(RemoteLocalTransport::ble,1,seq,second) && out.has("🌍") && seq==1,"C8-22 split UTF-8 survives byte-exact");
    out.reset();delivery.begin_result();seq=0;
    CHK(delivery.output(RemoteLocalTransport::ble,1,seq,{}) && out.has("\"body\":\"\""),"C8-23 empty output remains a valid event");
    struct ShortSink : Print { size_t write(uint8_t) override { return 0; } } short_sink;
    mrfw::RemoteClientDelivery short_delivery(short_sink,nullptr,false);
    CHK(!short_delivery.output(RemoteLocalTransport::usb,1,seq,first),"C8-24 short Print write refuses acceptance");
    mrcon_detail::GuardedConsole staged;
    const std::string oversized(2200,'x');staged.write(reinterpret_cast<const uint8_t*>(oversized.data()),oversized.size());
    auto dropped=[](void* p){return static_cast<mrcon_detail::GuardedConsole*>(p)->dropped_lines();};
    mrfw::RemoteClientDelivery checked(staged,nullptr,false,dropped,&staged);
    CHK(!checked.output(RemoteLocalTransport::usb,1,seq,second) && staged.dropped_lines()!=0,
        "C8-25 real console-stage drop is not local acceptance despite Print returning full count");

    // 8b: real firmware observer and real Print/LineSink delivery. The pending record is explicitly synthetic.
    auto& observed=g_node.remote_client();observed={};auto& op=observed.pending[0].core;
    op.request_id=0x0123456789abcdef;op.state=uint8_t(RemoteClientPhase::response_wait);
    op.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;op.carrier_ctr=42;op.route.target_hash=0xa1b2c3d4;
    Push push{};push.kind=PushKind::send_e2e_acked;push.ctr=42;
    CaptureSink usb_events,ble_events;
    for(const auto transport:{RemoteLocalTransport::usb,RemoteLocalTransport::ble}) {
        op.local_transport=uint8_t(transport);usb_events.reset();ble_events.reset();
        mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,true,mrfw::remote_client_bind_lookup,&g_node);
        CHK(transport==RemoteLocalTransport::usb ?
            usb_events.is("> remote 0123456789abcdef carrier acked ctr=42\n") && ble_events.n==0 :
            ble_events.is("{\"ev\":\"remote_carrier\",\"id\":\"0123456789abcdef\",\"event\":\"acked\",\"ctr\":42}\n") && usb_events.n==0,
            "C8-B1 observer emits exact ACK only on the row transport");
    }
    usb_events.reset();ble_events.reset();const auto before_observation=observed;
    mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,false,mrfw::remote_client_bind_lookup,&g_node);
    CHK(usb_events.n==0 && ble_events.n==0,"C8-B2 disconnected BLE observation drops");
    CHK(std::memcmp(&observed,&before_observation,sizeof observed)==0,"C8-B3 no retained observation or lifecycle mutation");
    push.kind=PushKind::send_failed;push.reason=SendFailReason::e2e_ack_timeout;
    mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,true,mrfw::remote_client_bind_lookup,&g_node);
    CHK(ble_events.is("{\"ev\":\"remote_carrier\",\"id\":\"0123456789abcdef\",\"event\":\"ack_timeout\",\"ctr\":42}\n") && usb_events.n==0,
        "C8-B4 exact timeout event after reconnect, without replaying dropped ACK");
    CustodyFailureRecord custody{};custody.failed_origin=1;custody.reporter_layer=2;
    custody.failed_type=DATA_TYPE_REMOTE_CMD;custody.failed_dst=3;custody.failed_ctr=99;
    custody.terminal_reason=CustodyFailureReason::cascade_count;
    custody.notice_flags=custody_notice_flags(CustodyRootStage::cts,true,false,true);
    custody.dst_hash32=op.route.target_hash;custody.previous_hop=1;custody.failed_next_hop=3;
    custody.requeue_count=protocol::cascade_requeue_max;custody.committed_hops=1;custody.remaining_hops=4;
    CustodyTranslatedTail tail{};tail.original_reporter=3;tail.mobile_ctr=42;
    tail.target_kind=CustodyTranslatedTargetKind::key_hash;tail.target_value=op.route.target_hash;
    push.kind=PushKind::custody_failure;
    push.body_len=pack_custody_failure_translated(custody,tail,push.body);
    CHK(push.body_len==custody_record_translated_len,"C8-B7 real translated custody codec fixture");
    usb_events.reset();ble_events.reset();
    mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,true,mrfw::remote_client_bind_lookup,&g_node);
    CHK(ble_events.is("{\"ev\":\"remote_carrier\",\"id\":\"0123456789abcdef\",\"event\":\"custody_failure\",\"ctr\":42,\"origin\":1,\"reporter\":3,\"layer\":2,\"reason\":\"cascade_count\"}\n") && usb_events.n==0,
        "C8-B8 exact custody BLE fields through real delivery");
    op.local_transport=uint8_t(RemoteLocalTransport::usb);ble_events.reset();
    mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,true,mrfw::remote_client_bind_lookup,&g_node);
    CHK(usb_events.is("> remote 0123456789abcdef carrier custody_failure ctr=42 origin=1 reporter=3 layer=2 reason=cascade_count\n") && ble_events.n==0,
        "C8-B9 exact custody USB fields through real delivery");
    op.local_transport=uint8_t(RemoteLocalTransport::ble);
    // Existing JSON writer and synchronous LineSink: generic detail precedes the controller event.
    ble_reset();LineSink observer_ble(ble_capture);char generic[1700]{};
    const auto generic_n=console::write_push(generic,sizeof generic,push);
    const char* text="SEND FAILED\n";observer_ble.write(reinterpret_cast<const uint8_t*>(text),strlen(text));
    ble_capture(generic,generic_n);
    mrfw::remote_client_observe_push(observed,push,usb_events,&observer_ble,true,mrfw::remote_client_bind_lookup,&g_node);observer_ble.flush();
    const auto event_pos=std::string(g_ble).find("remote_carrier");
    CHK(std::string(g_ble).starts_with(std::string(text)+std::string(generic,generic_n)) && event_pos>=strlen(text)+generic_n,
        "C8-B5 real writers deliver text then generic JSON then controller event");
    // B416: synthetic pending/push, production observer + callback + real Node binding table.
    push={};push.kind=PushKind::send_e2e_acked;push.ctr=42;push.dst=2;
    const auto observe_binding=[&]{
        ble_events.reset();
        mrfw::remote_client_observe_push(observed,push,usb_events,&ble_events,true,mrfw::remote_client_bind_lookup,&g_node);
        return ble_events.n!=0;
    };
    auto learn_binding=[&](uint8_t id,uint32_t hash){
        beacon_in b{};b.src=id;b.key_hash32=hash;b.leaf_id=g_node.config().leaf_id;
        uint8_t wire[64]{};const auto n=pack_beacon(b,wire);
        g_node.on_recv(wire,n,RxMeta{12,-70,0,static_cast<int8_t>(id)});
        return mrfw::remote_client_bind_lookup(&g_node,hash)==id;
    };
    op.route.target_hash=0xe4160001;
    CHK(mrfw::remote_client_bind_lookup(&g_node,op.route.target_hash)==-1,"C8-B10 real lookup reports absent binding");
    CHK(observe_binding(),"C8-B11 no binding does not veto");
    CHK(learn_binding(2,op.route.target_hash),"C8-B12 real binding installed at ID 2");
    CHK(observe_binding(),"C8-B13 ID 2 accepts ACK from 2");
    op.route.target_hash=0xe4160002;
    CHK(learn_binding(3,op.route.target_hash),"C8-B14 different target installed at ID 3");
    CHK(!observe_binding(),"C8-B15 ID 3 vetoes equal-counter ACK from 2");
    push.kind=PushKind::send_failed;push.reason=SendFailReason::e2e_ack_timeout;push.dst=0;
    CHK(observe_binding(),"C8-B16 delegated timeout has no binding veto");
    push.kind=PushKind::send_e2e_acked;push.dst=2;push.sender_hash=op.route.target_hash;op.route.hop_count=1;
    CHK(observe_binding(),"C8-B17 matching XL hash has no binding veto");
    observed.ack_debt[0].in_use=1;observed.ack_debt[0].retries=4;out.reset();mrfw::dispatch("status",6,out);
    CHK(out.has("radmin_client_ack_debt=1"),"C8-B6 dormant debt visible through actual status binding");
    observed={};

}
#endif
