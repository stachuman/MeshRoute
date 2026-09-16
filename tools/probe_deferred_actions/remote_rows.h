// 7b-3: real encrypted radio admission, session producer, firmware owner and P1 applies.
// Hardware primitives remain the labelled fakes/extracted owners in probe.cpp, not copied effects.
#include "firmware_remote_actions.h"
#include <algorithm>
#include "firmware_remote_activation.h"
struct RemoteActionRadioFixture : Radmin7RadioFixture {
    RemoteActionRadioFixture() { quiet_after_preparation=true; }
};
static unsigned remote_checks;
#define CHECK_REMOTE(c) do { ++remote_checks; if (!(c)) { std::fprintf(stderr,"  FAIL remote action %u: %s\n",remote_checks,#c); ++g_fail; } } while (0)
static void remote_hooks() {
    actions.clear(); mrprobe_nv().observe=action_event;
    g_pseg_dm.trace_tag="wipe-dm"; g_pseg_ch.trace_tag="wipe-ch";
    g_remote_action_activation_ms=299999;
}
static bool scalar_console_only() {
    mrcon.service();
    std::string text(Serial.out,Serial.n_out);
    for(size_t start=0; start<text.size();) {
        if(text.compare(start,16,"> remote-action ")!=0) { std::fprintf(stderr,"NONSCALAR: %s\n",text.c_str()); return false; }
        const auto end=text.find('\n',start); if(end==std::string::npos)return false; start=end+1;
    }
    return g_ble_n==0;
}
static void run_remote_actions() {
    using namespace meshroute; using namespace mrfw;
    struct Row { const char* line; ActionKind kind; };
    const Row selected[]={{"reboot",ActionKind::reboot},{"prep-restart",ActionKind::prep_restart},
        {"ota",ActionKind::ota},{"factory_reset confirm",ActionKind::factory_reset},
        {"sleep",ActionKind::sleep_on},{"sleep on",ActionKind::sleep_on},{"sleep off",ActionKind::sleep_off},
        {"sleep off...",ActionKind::sleep_off},{"sleep OFF",ActionKind::sleep_on},
        {"crashtest hang",ActionKind::crash_hang},{"crashtest fault-extra",ActionKind::crash_fault},
        {"crashtest reboot-extra",ActionKind::crash_reboot}};
    for(const auto& row:selected) {
        reset_action_fixture(); RemoteActionRadioFixture f; remote_hooks();
        char owned_line[64];snprintf(owned_line,sizeof owned_line,"%s",row.line);
        const auto policy=*command_policy_lookup(owned_line,strlen(owned_line));
        const auto admitted=remote_action_admit(policy,owned_line,strlen(owned_line),action_build_support(),true);
        const bool ready=admitted.status==ActionAdmissionStatus::ready;
        const auto text=f.command(owned_line,1,ready?RemoteTerminal::scheduled:RemoteTerminal::refused);
        CHECK_REMOTE(actions.empty()); CHECK_REMOTE(!g_halted && !g_force_sleep && !mrota::s_active);
        const auto& state=g_node.admin_session_state();
        if(!ready) {CHECK_REMOTE(state.action.phase==RemoteActionPhase::none);continue;}
        CHECK_REMOTE(state.action.phase==RemoteActionPhase::due);
        CHECK_REMOTE(state.action.trigger==RemoteActionTrigger::ack);
        CHECK_REMOTE(state.action.kind==static_cast<uint8_t>(row.kind));
        CHECK_REMOTE(state.action.activation_ms==299999);
        CHECK_REMOTE(state.action.request_id==f.rid && state.action.controller_slot==1);
        CHECK_REMOTE(remote_session_ingress_used(state)==0);
        CHECK_REMOTE(text==(row.kind==ActionKind::ota?(ACTION_BACKEND==2?"> ota backend=wifi\n":"> ota backend=ble-dfu\n"):""));
        // Request/cfg/debug no longer authorize the already prepared apply. No parser is called here.
        memset(owned_line,'X',sizeof owned_line);g_remote_action_activation_ms=1;g_mr_trace_on=false;
        if(row.kind==ActionKind::sleep_off)g_force_sleep=true;
        if(sigsetjmp(action_jump,1)==0) {
            if(row.kind==ActionKind::crash_hang)ualarm(20000,0);
            remote_action_service_once(); ualarm(0,0);
        }
        CHECK_REMOTE(state.action.phase==RemoteActionPhase::none);
        const DeferredActionRecord zero{};CHECK_REMOTE(memcmp(&state.action,&zero,sizeof zero)==0);
        CHECK_REMOTE(state.last_activation_kind==static_cast<uint8_t>(row.kind));
        CHECK_REMOTE(scalar_console_only());
        CHECK_REMOTE(std::string(Serial.out).find(" trigger=ack")!=std::string::npos);
        if(row.kind==ActionKind::prep_restart)CHECK_REMOTE(g_halted && actions.front()=="clear-learned");
        if(row.kind==ActionKind::sleep_on)CHECK_REMOTE(g_force_sleep);
        if(row.kind==ActionKind::sleep_off)CHECK_REMOTE(!g_force_sleep);
        if(row.kind==ActionKind::crash_hang)CHECK_REMOTE(jumped==1);
        if(row.kind==ActionKind::crash_fault)CHECK_REMOTE(!actions.empty() && actions.back()=="fault");
        if(row.kind==ActionKind::reboot||row.kind==ActionKind::crash_reboot||row.kind==ActionKind::factory_reset)
            CHECK_REMOTE(!actions.empty() && actions.back()=="reset");
        if(row.kind==ActionKind::ota) {
#if ACTION_BACKEND == 2
            CHECK_REMOTE(mrota::s_active && !actions.empty() && actions.back()=="server-start");
#else
            CHECK_REMOTE(action_power.GPREGRET.value==0xA8 && !actions.empty() && actions.back()=="reset");
#endif
        }
        const auto previous=actions;const auto serial=std::string(Serial.out);
        remote_action_service_once();CHECK_REMOTE(actions==previous);mrcon.service();CHECK_REMOTE(serial==Serial.out);
    }
    // Same supported command as an operator still goes through the actual authority gate.
    for(const char* line:{"reboot","prep-restart","ota","factory_reset confirm","sleep on","crashtest hang"}) {
        reset_action_fixture(); RemoteActionRadioFixture f;remote_hooks();
        const auto policy=*command_policy_lookup(line,strlen(line));
        const bool allowed=policy.cls==CommandClass::operator_;
        const auto a=remote_action_admit(policy,line,strlen(line),action_build_support(),true);
        f.command(line,2,allowed&&a.status==ActionAdmissionStatus::ready?RemoteTerminal::scheduled:RemoteTerminal::refused);
        CHECK_REMOTE(actions.empty());CHECK_REMOTE(!g_halted && !g_force_sleep);
    }
    for(const char* line:{"factory_reset","factory_reset confirm ","factory_reset Confirm","factory_reset confirmx",
                          "crashtest","crashtest faul","crashtest FAULT","crashtest reboo"}) {
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();f.command(line,1,RemoteTerminal::refused);
        CHECK_REMOTE(actions.empty() && g_node.admin_session_state().action.phase==RemoteActionPhase::none);
    }
    reset_action_fixture();{RemoteActionRadioFixture f;remote_hooks();g_mr_trace_on=false;
        f.command("crashtest hang",1,RemoteTerminal::refused);CHECK_REMOTE(actions.empty());}
    // Every one of the 36 policy entries maps to an actual common-seam refusal, including metadata aliases.
    unsigned refused=0,scheduled_scope=0;
    for(const auto& p:kCommandPolicy) {
        if(!p.disruptive)continue;
        const std::string verb=p.verb;
        if(verb=="reboot"||verb=="reboot (alias: prep-restart)"||verb=="prep-restart"||verb=="ota"
            ||verb=="factory_reset"||verb=="sleep"||verb=="crashtest") {++scheduled_scope;continue;}
        ++refused;std::string line=verb.substr(0,verb.find(" (alias:"));
        if(line=="join" && verb.find("alias:")!=std::string::npos)line="create";
        std::string sub=p.subverb;sub=sub.substr(0,sub.find(" (alias:"));
        if(sub!="—")line+=" "+sub;
        if(verb=="cfg set" && sub!="mobile_autoregister true")line+=" 1";
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();
        const auto writes=mrprobe_nv().writes;f.command(line.c_str(),1,RemoteTerminal::refused);
        CHECK_REMOTE(actions.empty() && !g_halted && !g_force_sleep);
        CHECK_REMOTE(g_node.admin_session_state().action.phase==RemoteActionPhase::none);
        CHECK_REMOTE(mrprobe_nv().writes==writes);
    }
    CHECK_REMOTE(refused==36);CHECK_REMOTE(scheduled_scope==12);
    // The real live binding supplies 7a's five states and both valid configured edges.
    for(unsigned mode=0;mode<6;++mode) {
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();
        if(mode==5) {
            auto cfg=g_node.config();cfg.routing_sf=10;cfg.allowed_sf_bitmap=1u<<10;
            cfg.radio_bw_hz=7800;cfg.radio_cr=8;CHECK_REMOTE(g_node.on_init(cfg));
        }
        const auto inputs=remote_activation_live_inputs();const auto minimum=remote_action_activation_min_ms(inputs);
        g_remote_action_activation_ms=mode==0?0:mode==1?minimum:mode==2?299999:mode==3?minimum-1:mode==4?300000:0;
        const auto resolved=remote_activation_resolve(g_remote_action_activation_ms,inputs);
        const bool valid=mode<3;
        CHECK_REMOTE((resolved.state==ActivationState::default_derived||resolved.state==ActivationState::configured)==valid);
        if(mode==5) {
            // Slow impossible PHY is an admission proof, not the fixture's 1-second radio-drain budget.
            ++f.rid;f.flight("prep-restart",1);remote_executor_service_once();
            const auto& st=g_node.admin_session_state();const auto si=remote_session_seen_find(st,1,f.rid);
            CHECK_REMOTE(si<kRadminSeenSlots);
            if(si<kRadminSeenSlots)CHECK_REMOTE(st.transcripts[st.seen[si].record.transcript_slot].terminal==static_cast<uint8_t>(RemoteTerminal::refused));
        } else f.command("prep-restart",1,valid?RemoteTerminal::scheduled:RemoteTerminal::refused);
        CHECK_REMOTE(actions.empty());
        CHECK_REMOTE(g_node.admin_session_state().action.activation_ms==(valid?resolved.effective_ms:0));
        CHECK_REMOTE(g_node.admin_session_state().action.phase==(valid?RemoteActionPhase::due:RemoteActionPhase::none));
    }
    // The real owner clears its transfer before effects. The wipe interposer forwards to real crypto_wipe.
    reset_action_fixture();{RemoteActionRadioFixture f;remote_hooks();f.command("prep-restart",1,RemoteTerminal::scheduled);
        wipe_watch_begin();remote_action_service_once();wipe_watch_end();unsigned owned_wipes=0;
        for(int i=0;i<g_nwipes;++i)if(g_wipes[i].n==sizeof(DeferredActionRecord)&&g_wipes[i].after_zero)++owned_wipes;
        CHECK_REMOTE(owned_wipes>=3);CHECK_REMOTE(g_halted);CHECK_REMOTE(scalar_console_only());
    }
    // Labelled external power-loss/pre-emption class: reconstruct the Node; no durable success is invented.
    reset_action_fixture();{RemoteActionRadioFixture before;remote_hooks();before.command("prep-restart",1,RemoteTerminal::scheduled,false,false);
        CHECK_REMOTE(g_node.admin_session_state().action.phase==RemoteActionPhase::armed);
        RemoteActionRadioFixture after;remote_hooks();remote_action_service_once();
        CHECK_REMOTE(actions.empty());CHECK_REMOTE(g_node.admin_session_state().action.phase==RemoteActionPhase::none);
        CHECK_REMOTE(g_node.admin_session_state().last_activation_kind==0&&g_node.admin_session_state().last_activation_outcome==0);
    }
    // Actual ACL/NV/runtime bindings, called at the common command seam. This is deliberately
    // a seam fixture (not a second radio flight): an unarmed terminal otherwise has reply-drain priority.
    for(bool armed:{false,true})for(unsigned mode=0;mode<6;++mode) {
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();
        if(mode==5) {CaptureSink local;CHECK_REMOTE(dispatch("acl remove 1 confirm",20,local));}
        ++f.rid;f.flight("prep-restart",0);remote_executor_service_once();g_mr_trace_on=false;
        if(armed){CHECK_REMOTE(g_node.radmin_send_frame()==RadminSend::queued);f.pump_response();}
        const auto original=g_node.admin_session_state().action;const auto epoch=g_node.admin_session_state().epoch[0];
        CHECK_REMOTE(original.phase==(armed?RemoteActionPhase::armed:RemoteActionPhase::prepared));
        const auto role=mode==2?CommandAuthority::remote_operator:mode==5?CommandAuthority::local:CommandAuthority::remote_owner;
        const auto transport=mode==5?CommandTransport::usb:CommandTransport::remote;
        const uint8_t actor=mode==1?0:mode==2?2:1;
        const CommandContext ctx{transport,role,mode==5,999,console::remote_command_max_bytes,actor};
        const char* line=mode==3?"acl set 0 owner":"acl remove 0 confirm";
        mrprobe_nv().fail_write=mode==4;CaptureSink out;
        const auto result=exec_console_line(line,strlen(line),LineFormat::text,out,nullptr,0,ctx);
        const bool changed=mode==0;
        CHECK_REMOTE((g_node.admin_session_state().epoch[0]!=epoch)==changed);
        CHECK_REMOTE(std::find(actions.begin(),actions.end(),"clear-learned")==actions.end()
            && std::find(actions.begin(),actions.end(),"wipe-dm")==actions.end()
            && std::find(actions.begin(),actions.end(),"reset")==actions.end());CHECK_REMOTE(!g_halted);
        if(changed&&!armed)CHECK_REMOTE(g_node.admin_session_state().action.phase==RemoteActionPhase::none);
        else CHECK_REMOTE(memcmp(&original,&g_node.admin_session_state().action,sizeof original)==0);
        if(mode==1)CHECK_REMOTE(out.has("self_slot"));
        if(mode==2)CHECK_REMOTE(result.outcome==DispatchOutcome::refused);
        if(mode==5)CHECK_REMOTE(out.has("last_owner"));
        if(armed){g_probe_millis+=static_cast<uint32_t>(original.activate_at_ms-g_hal.now());remote_action_service_once();CHECK_REMOTE(g_halted);}
    }
    // ACK lost: exactly the frozen ownership deadline, even after a local halt and changed configuration.
    reset_action_fixture();{RemoteActionRadioFixture f;remote_hooks();
        f.command("prep-restart",1,RemoteTerminal::scheduled,false,false);
        const auto& state=g_node.admin_session_state();CHECK_REMOTE(state.action.phase==RemoteActionPhase::armed);
        const auto deadline=state.action.activate_at_ms;const auto frozen=state.action;
        g_remote_action_activation_ms=0;g_halted=true;
        CHECK_REMOTE(deadline>g_hal.now());g_probe_millis+=static_cast<uint32_t>(deadline-g_hal.now()-1);
        remote_action_service_once();CHECK_REMOTE(actions.empty());CHECK_REMOTE(state.action.activate_at_ms==deadline);
        ++g_probe_millis;remote_action_service_once();CHECK_REMOTE(!actions.empty());CHECK_REMOTE(g_halted);
        CHECK_REMOTE(state.action.phase==RemoteActionPhase::none);
        CHECK_REMOTE(std::string(Serial.out).find("trigger=deadline")!=std::string::npos || (mrcon.service(),std::string(Serial.out).find("trigger=deadline")!=std::string::npos));
        CHECK_REMOTE(frozen.activation_ms==299999);CHECK_REMOTE(scalar_console_only());
        const auto done=actions;g_probe_millis+=100;remote_action_service_once();CHECK_REMOTE(actions==done);
    }
    // Cross-credential conflict uses the typed terminal, then the original promise still runs.
    reset_action_fixture();{RemoteActionRadioFixture f;remote_hooks();
        f.command("prep-restart",0,RemoteTerminal::scheduled,false,false);const auto original=g_node.admin_session_state().action;
        f.command("prep-restart",1,RemoteTerminal::action_busy);CHECK_REMOTE(actions.empty());
        CHECK_REMOTE(memcmp(&original,&g_node.admin_session_state().action,sizeof original)==0);
        g_probe_millis+=static_cast<uint32_t>(original.activate_at_ms-g_hal.now());remote_action_service_once();
        CHECK_REMOTE(g_halted);CHECK_REMOTE(scalar_console_only());
    }
    // Real status owner, scalar phase/presence/diagnostics; seeded phase fixture is explicitly synthetic.
    reset_action_fixture();{RemoteActionRadioFixture f;remote_hooks();
        auto& state=const_cast<RemoteSessionState&>(g_node.admin_session_state());
        for(const auto phase:{RemoteActionPhase::none,RemoteActionPhase::preparing,RemoteActionPhase::prepared,RemoteActionPhase::armed,RemoteActionPhase::due}) {
            state.action={};state.action.phase=phase;state.action.controller_slot=2;state.action.kind=static_cast<uint8_t>(ActionKind::sleep_off);
            state.action.activation_ms=12345;state.action.activate_at_ms=g_hal.now()+100;
            state.last_activation_kind=static_cast<uint8_t>(ActionKind::ota);state.last_activation_outcome=static_cast<uint8_t>(ActionOutcome::backend_failed);
            const auto snapshot=state;CaptureSink out;remote_action_print_status(out);
            const bool present=phase!=RemoteActionPhase::none,armed=phase==RemoteActionPhase::armed||phase==RemoteActionPhase::due;
            const char* name=phase==RemoteActionPhase::none?"none":phase==RemoteActionPhase::preparing?"preparing":phase==RemoteActionPhase::prepared?"prepared":phase==RemoteActionPhase::armed?"armed":"due";
            std::string expected=" radmin_action_phase=";expected+=name;expected+=" radmin_action_armed=";expected+=armed?"1":"0";
            if(present)expected+=" radmin_action_request_id=0000000000000000 radmin_action_slot=2 radmin_action_kind=sleep-off radmin_action_activation_ms=12345 radmin_action_remaining_ms="+std::string(armed?(phase==RemoteActionPhase::due?"0":"100"):"unarmed");
            expected+=" radmin_last_activation_kind=ota radmin_last_activation_outcome=backend_failed";
            CHECK_REMOTE(out.buf==expected);CHECK_REMOTE(memcmp(&snapshot,&state,sizeof state)==0);
            CaptureSink full;CHECK_REMOTE(dispatch("status",6,full));CHECK_REMOTE(std::string(full.buf).find(expected)!=std::string::npos);
        }
        state.action={};state.action.phase=RemoteActionPhase::due;state.action.kind=255;state.action.backend=255;
        remote_action_service_once();CHECK_REMOTE(actions.empty());CHECK_REMOTE(state.last_activation_kind==0&&state.last_activation_outcome==0);
    }
#if ACTION_BACKEND != 0
    // Full erase partial-result matrix observed before a non-returning reset, through remote applies.
    for(unsigned mask=0;mask<8;++mask) {
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();f.command("factory_reset confirm",1,RemoteTerminal::scheduled);
        g_pseg_dm.fail_erase=!(mask&1);g_pseg_ch.fail_erase=!(mask&2);mrprobe_nv().rw_ok=mask&4;
        nonreturning=true;if(sigsetjmp(action_jump,1)==0)remote_action_service_once();
        const auto expected=!(mask&1)||!(mask&2)?((mask&4)?ActionOutcome::inbox_partial:ActionOutcome::inbox_nv_partial)
            :((mask&4)?ActionOutcome::started:ActionOutcome::nv_partial);
        CHECK_REMOTE(g_node.admin_session_state().last_activation_outcome==static_cast<uint8_t>(expected));
        CHECK_REMOTE(g_node.admin_session_state().action.phase==RemoteActionPhase::none);
        CHECK_REMOTE(!actions.empty() && actions.back()=="reset");CHECK_REMOTE(scalar_console_only());
    }
#endif
#if ACTION_BACKEND == 2
    for(bool active:{false,true})for(bool backend_ok:{false,true}) {
        reset_action_fixture();RemoteActionRadioFixture f;remote_hooks();
        f.command("ota",1,RemoteTerminal::scheduled);mrota::s_active=active;mrota::WiFi.ok=backend_ok;
        remote_action_service_once();CHECK_REMOTE(mrota::s_active==(active||backend_ok));
        CHECK_REMOTE(std::find(actions.begin(),actions.end(),"server-stop")==actions.end());
        CHECK_REMOTE(g_node.admin_session_state().last_activation_outcome==static_cast<uint8_t>(active||backend_ok?ActionOutcome::completed:ActionOutcome::backend_failed));
        CHECK_REMOTE(scalar_console_only());
    }
#endif
}
