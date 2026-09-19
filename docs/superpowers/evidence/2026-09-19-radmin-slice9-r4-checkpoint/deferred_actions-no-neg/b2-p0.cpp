#define ACTION_BACKEND 2
#define ACTION_POWERSAVE 0
// The real command TU is linked by the existing inbox builder. Reuse its one fixture authority.
#define MR0C_NO_MAIN
#define MR_PROBE_ACTION_EFFECTS
#include "../probe_inbox_verbs/probe_main.cpp"
#include <vector>
#include <string>
#include <csignal>
#include <csetjmp>
#include <unistd.h>
#include "device_ota.h"
#ifndef ACTION_BASELINE
#include "../probe_inbox_verbs/remote_exec_rows.h"
#endif

static std::vector<std::string> actions;
static std::vector<mrfw::ActionOutcome> outcomes;
static sigjmp_buf action_jump;
static volatile sig_atomic_t jumped;
static bool nonreturning;
static void action_alarm(int) { jumped = 1; siglongjmp(action_jump, 1); }
static void action_reset() { actions.push_back("reset"); if (nonreturning) siglongjmp(action_jump, 1); }
static void action_flush(Print& out) { out.flush(); actions.push_back("flush"); }
static void action_delay(uint32_t ms) { actions.push_back("delay:" + std::to_string(ms)); }
static void action_fault() { actions.push_back("fault"); if (nonreturning) siglongjmp(action_jump, 1); }
namespace mrfault { static void mark_expected_reset() { actions.push_back("mark-reset"); } }
struct ActionEsp { void restart() { action_reset(); } } action_esp;
struct ActionRegister {
    unsigned value = 0;
    void operator=(unsigned v) { value = v; actions.push_back("dfu:" + std::to_string(v)); }
};
struct ActionPower { ActionRegister GPREGRET; } action_power;
struct ActionWifi {
    bool ok = true;
    bool softAP(const char*) { actions.push_back("softap-start"); return ok; }
    const char* softAPIP() { return "192.0.2.1"; }
    void softAPdisconnect(bool) { actions.push_back("softap-stop"); }
};
struct ActionServer {
    void on(const char* path, int, void(*)()) { actions.push_back(std::string("http:")+path); }
    void begin() { actions.push_back("server-start"); }
    void stop() { actions.push_back("server-stop"); }
};
namespace mrota {
static bool s_active = false;
static void (*s_pre_reboot_hook)() = nullptr;
static ActionWifi WiFi;
static ActionServer s_server;
static void handle_root() {}
static void handle_update() {}
}

// The runner inserts exact production definitions; only hardware primitives are substituted.
namespace mrota {
const int HTTP_GET=0, HTTP_POST=1;
bool ota_start(Print& out) {
    if (s_active) return true;
    if (!WiFi.softAP("MeshRoute-OTA")) {
        out.println(F("OTA: SoftAP start FAILED"));
        return false;
    }
    out.print(F("OTA: SoftAP 'MeshRoute-OTA' IP="));
    out.println(WiFi.softAPIP());
    s_server.on("/",       HTTP_GET,  handle_root);
    s_server.on("/update", HTTP_POST, handle_update);
    s_server.begin();
    s_active = true;
    out.println(F("OTA: browse to the IP above, upload firmware.bin"));
    return true;
}
bool ota_start() { return ota_start(mrcon); }
void ota_stop() {
    if (!s_active) return;
    s_server.stop();
    WiFi.softAPdisconnect(true);
    s_active = false;
    mrcon.println(F("OTA: stopped"));
}
bool ota_active() { return s_active; }
void set_pre_reboot_hook(void (*fn)()) { s_pre_reboot_hook = fn; }
}

#undef BOARD_HELTEC_V3
#undef ARDUINO_ARCH_ESP32
#undef ESP32
#undef NRF52_SERIES
#undef NRF52_PLATFORM
#undef MRFAULT_ESP32
#undef MRFAULT_HW
#if ACTION_BACKEND == 1
#define NRF52_SERIES 1
#define NRF52_PLATFORM 1
#define MRFAULT_HW 1
#elif ACTION_BACKEND == 2
#define BOARD_HELTEC_V3 1
#define MRFAULT_ESP32 1
#define MRFAULT_HW 1
#endif
#if !ACTION_POWERSAVE
#define MR_NO_POWERSAVE 1
#endif
#define delay action_delay
#define NVIC_SystemReset action_reset
#define ESP action_esp
#define NRF_POWER (&action_power)
#define abort action_fault
mrfw::ActionSupport mrfw::action_build_support() {
    ActionSupport support{};
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    support.reboot = ActionBackend::nrf_reset;
    support.ota = ActionBackend::nrf_dfu;
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    support.reboot = ActionBackend::esp_reset;
    support.ota = ActionBackend::wifi_ota;
#endif
#if defined(NRF52_PLATFORM)
    support.fault = ActionBackend::nrf_fault;
#elif defined(MRFAULT_ESP32)
    support.fault = ActionBackend::esp_fault;
#endif
#if !defined(MR_NO_POWERSAVE)
    support.power_save = true;
#endif
    return support;
}
mrfw::ActionOutcome mrfw::action_reboot_apply(ActionBackend backend, Print& out, ActionObserver observer,
                                            ActionOutcome reset_outcome) {
    out.println(F("> rebooting")); action_flush(out); delay(100);
#if defined(MRFAULT_HW)
    mrfault::mark_expected_reset();   // v2 fault log: classify the upcoming reset as REBOOT, not UNEXPECTED
#endif
    action_report(observer, reset_outcome);
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    if (backend != ActionBackend::nrf_reset) return action_report(observer, ActionOutcome::backend_failed);
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    if (backend != ActionBackend::esp_reset) return action_report(observer, ActionOutcome::backend_failed);
    ESP.restart();
#else
    (void)backend;
    return action_report(observer, ActionOutcome::backend_failed);
#endif
    return action_report(observer, ActionOutcome::unexpected_return);
}
mrfw::ActionOutcome mrfw::action_ota_apply(ActionBackend backend, Print& out, ActionObserver observer) {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)
    if (backend != ActionBackend::nrf_dfu) return action_report(observer, ActionOutcome::backend_failed);
    out.println(F("> OTA: rebooting into BLE DFU now — this USB console will drop here."));
    out.println(F(">      Push firmware.zip via the Nordic DFU app (enable its auto-reboot). Double-tap RESET to abort."));
    action_flush(out); delay(500);
    mrfault::mark_expected_reset();   // v2 fault log: the OTA reset is a REBOOT, not UNEXPECTED
    NRF_POWER->GPREGRET = 0xA8;   // DFU_MAGIC_OTA_RESET
    action_report(observer, ActionOutcome::started);
    NVIC_SystemReset();
    return action_report(observer, ActionOutcome::unexpected_return);
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    if (backend != ActionBackend::wifi_ota) return action_report(observer, ActionOutcome::backend_failed);
    if (mrota::ota_start(out)) {
        mrota::set_pre_reboot_hook([] { mrfault::mark_expected_reset(); });
        out.println(F("> OTA: browse to the IP above, upload firmware.bin — node reboots on success"));
        return action_report(observer, ActionOutcome::completed);
    }
    out.println(F("> OTA: start FAILED"));
    return action_report(observer, ActionOutcome::backend_failed);
#else
    (void)backend; (void)out;
    return action_report(observer, ActionOutcome::backend_failed);
#endif
}
mrfw::ActionOutcome mrfw::action_crash_apply(ActionPlan plan, Print& out, Print& reboot_out,
                                           ActionObserver observer) {
    if (plan.kind == ActionKind::crash_hang) {
        out.println(F("> crashtest hang — spinning; the watchdog should reset in ~8 s")); action_flush(out);
        action_report(observer, ActionOutcome::started);
        for (;;) { /* no WDT feed -> DOG reset (nRF52); on a no-WDT build this hangs until power-cycle) */ }
    } else if (plan.kind == ActionKind::crash_fault) {
        out.println(F("> crashtest fault — forcing a crash")); action_flush(out);
#if defined(NRF52_PLATFORM)
        if (plan.backend != ActionBackend::nrf_fault) return action_report(observer, ActionOutcome::backend_failed);
        action_report(observer, ActionOutcome::started);
        action_fault();
        action_fault();
#elif defined(MRFAULT_ESP32)
        if (plan.backend != ActionBackend::esp_fault) return action_report(observer, ActionOutcome::backend_failed);
        action_report(observer, ActionOutcome::started);
        abort();
#else
        out.println(F("> (no HW fault path on this build)"));
        return action_report(observer, ActionOutcome::backend_failed);
#endif
        return action_report(observer, ActionOutcome::unexpected_return);
    } else if (plan.kind == ActionKind::crash_reboot) {
        out.println(F("> crashtest reboot — NVIC_SystemReset (SREQ)")); action_flush(out);
        return action_reboot_apply(plan.backend, reboot_out, observer);
    }
    return action_report(observer, ActionOutcome::backend_failed);
}
mrfw::ActionOutcome mrfw::action_prep_restart_apply(Print& out, ActionObserver observer) {
    g_node.clear_learned_state();                 // routes + channel buffer + liveness + pending + dedup -> empty (KEEPS _cfg + identity + join)
    // Drop the durable inbox RECORDS. ⛔ [[B134]] CORRECTED IN PLACE 2026-08-28: this line used to say
    // *"(no-op on the RAM/ESP32 store); the boot epoch bumps"*. Both halves have stopped being true on ESP32 —
    // wipe() is now a REAL segment erase there (SegmentedInboxStore::wipe), and the epoch no longer bumps
    // because the boot re-randomises it but because the NEXT boot's §10.1 detect sees empty records against a
    // meta that still remembers next_seq > 1. On the RAM arm it is still the reboot that clears the ring.
    // ⛔ [[B134]] QG blocker 3: the result is CHECKED and the success line is CONDITIONAL. `wipe()` used to return
    //    `void`, so this verb printed "inbox cleared" unconditionally — over records that may still be on flash.
    // ⚠ Both stores are wiped before the verdict is read: a partial erase must still erase what it can.
    // ★ THE FAILURE WORDING IS RULED (2026-08-29) AND APPLIED VERBATIM. It says "MAY remain", not "remain",
    //   because the two failure halves are not the same fact: every segment can erase cleanly and the METADATA
    //   save still fail, so "messages remain on flash" would itself be an overclaim — the honest report of a
    //   partial destructive operation is that it did not finish, not a promise about what survived.
    const bool inbox_dm_ok = g_inbox_dm.wipe(), inbox_ch_ok = g_inbox_ch.wipe();
    g_halted = true;                              // the loop now skips the operating block (dormant) but stays console-responsive
    if (inbox_dm_ok && inbox_ch_ok) {
        out.println(F("> prep-restart — routes + inbox cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean."));
    } else {
        out.println(F("> prep-restart WARN: inbox erase incomplete (messages may remain on flash)"));
        out.println(F("> prep-restart — routes cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean."));
    }
    return action_report(observer, inbox_dm_ok && inbox_ch_ok ? ActionOutcome::completed
                                                            : ActionOutcome::inbox_partial);
}
static void do_reboot() {
    const auto admission = mrfw::action_reboot_admit(mrfw::action_build_support());
    mrfw::action_reboot_apply(admission.plan.backend, mrcon);
}
static void do_ota() {
#if !(defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(BOARD_XIAO_WIO_SX1262)) \
    && (defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3))
    if (mrota::ota_active()) {
        mrota::ota_stop();
        mrcon.println(F("> OTA: stopped"));
        return;
    }
#endif
    const auto admission = mrfw::action_ota_admit(mrfw::action_build_support());
    mrfw::action_ota_apply(admission.plan.backend, mrcon);
}
static void handle_crashtest(const char* args, Print& out) {
    const auto admission = mrfw::action_crash_admit(args, strlen(args), meshroute::g_mr_trace_on,
                                                 mrfw::action_build_support());
    if (admission.status == mrfw::ActionAdmissionStatus::debug_disabled) {
        out.println(F("> crashtest err (enable `debug on` first — gated to avoid an accidental crash)"));
    } else if (admission.status == mrfw::ActionAdmissionStatus::usage) {
        out.println(F("> crashtest err usage: crashtest <hang|fault|reboot>"));
    } else {
        // Unsupported local fault still prints its original two lines; typed support stays explicit.
        mrfw::action_crash_apply(admission.plan, out, mrcon);
    }
}
static void handle_prep_restart(Print& out) {
    mrfw::action_prep_restart_apply(out);
}
void fw_reboot()                                { do_reboot(); }
void fw_ota()                                   { do_ota(); }
void fw_crashtest(const char* args, Print& out) { handle_crashtest(args, out); }
void fw_prep_restart(Print& out)                { handle_prep_restart(out); }
#undef delay
#undef NVIC_SystemReset
#undef ESP
#undef NRF_POWER
#undef abort
void fw_faults_dump(Print&) { routed("faults_dump"); }


extern "C" void __real__ZN9meshroute4Node19clear_learned_stateEv(meshroute::Node*);
extern "C" void __wrap__ZN9meshroute4Node19clear_learned_stateEv(meshroute::Node* node) {
    actions.push_back("clear-learned");
    __real__ZN9meshroute4Node19clear_learned_stateEv(node);
}
static void observe_action(void*, mrfw::ActionOutcome o) { outcomes.push_back(o); }
static unsigned probe_checks;
static bool prep_print_before_halt;
static void action_event(const char* event) { actions.push_back(event); }
static void action_newline(const char* all) {
    const std::string s(all);const auto pos=s.rfind('\n',s.size()-2);const auto line=s.substr(pos==std::string::npos?0:pos+1);
    if(line.find("WARN: inbox")!=std::string::npos) actions.push_back("warn-inbox");
    if(line.find("WARN: an NV")!=std::string::npos) actions.push_back("warn-nv");
    if(line.find("prep-restart")!=std::string::npos && !g_halted) prep_print_before_halt=true;
}

#define CHECK_ACTION(c) do { ++probe_checks; if (!(c)) { std::fprintf(stderr,"  FAIL action %u: %s\n",probe_checks,#c); ++g_fail; } } while (0)
static void reset_action_fixture() {
    actions.clear(); outcomes.clear(); g_sink.reset(); Serial.reset(); mrprobe_nv().reset();
    g_pseg_dm.erases = g_pseg_ch.erases = g_pmeta_dm.saves = g_pmeta_ch.saves = 0;
    g_pseg_dm.fail_erase = g_pseg_ch.fail_erase = false;
    g_halted = false; g_force_sleep = false; meshroute::g_mr_trace_on = true;
    mrota::s_active = false; mrota::s_pre_reboot_hook = nullptr; mrota::WiFi.ok = true; jumped = 0; nonreturning = false;
    g_probe_millis = 0;
    mrprobe_nv().observe=action_event;g_pseg_dm.trace_tag="wipe-dm";g_pseg_ch.trace_tag="wipe-ch";
    g_sink.newline_hook=action_newline;prep_print_before_halt=false;
}
static std::string hex(const char* p, size_t n) {
    std::string s; const char* digits="0123456789abcdef";
    for (size_t i=0;i<n;++i) { const auto b=static_cast<unsigned char>(p[i]);s+=digits[b>>4];s+=digits[b&15]; }
    return s;
}
static void transcript(const std::string& label) {
    mrcon.service();
    std::printf("TRANSCRIPT %s %s %s %d %d %d %d", label.c_str(),hex(g_sink.buf,g_sink.n).c_str(),
                hex(Serial.out,Serial.n_out).c_str(),g_halted,g_force_sleep,g_pseg_dm.erases,g_pseg_ch.erases);
    for(const auto& a:actions) std::printf(" [%s]",a.c_str());
    std::printf("\n");
}
static bool local(const char* line) {
    return mrfw::dispatch(line,strlen(line),g_sink);
}
#ifndef ACTION_BASELINE
#include "../probe_deferred_actions/remote_rows.h"
#endif
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    signal(SIGALRM,action_alarm);
    for (const char* line : {"factory_reset", "factory_reset confirm ", "factory_reset Confirm", "factory_reset confirmx"}) {
        reset_action_fixture(); CHECK_ACTION(local(line)); CHECK_ACTION(g_pseg_dm.erases==0 && g_pseg_ch.erases==0);
        CHECK_ACTION(actions.empty()); transcript(line);
    }
    for(unsigned mask=0;mask<8;++mask) {
        reset_action_fixture();g_pseg_dm.fail_erase=!(mask&1);g_pseg_ch.fail_erase=!(mask&2);mrprobe_nv().rw_ok=mask&4;
        CHECK_ACTION(local("factory_reset confirm"));
        CHECK_ACTION(g_pseg_dm.erases>0 && g_pseg_ch.erases>0);
        CHECK_ACTION(g_sink.has("inbox erase incomplete") == (!(mask&1)||!(mask&2)));
        CHECK_ACTION(g_sink.has("an NV slot did not erase") == !(mask&4));
        std::vector<std::string> order;
        for(const auto& event:actions) {
            if(event=="wipe-dm"||event=="wipe-ch") { if(order.empty()||order.back()!=event) order.push_back(event); }
            else if(event=="warn-inbox"||event=="nv-open"||event=="nv-clear"||event=="warn-nv") order.push_back(event);
        }
        std::vector<std::string> expected{"wipe-dm","wipe-ch"};
        if(!(mask&1)||!(mask&2))expected.push_back("warn-inbox");
        expected.push_back("nv-open");expected.push_back((mask&4)?"nv-clear":"warn-nv");
        CHECK_ACTION(order==expected);
        transcript("factory:"+std::to_string(mask));
    }
    for (const char* line : {"sleep", "sleep on", "sleep off", "sleep off...", "sleep OFF", "sleep of", "sleep other", "sleep   off"}) {
        reset_action_fixture(); CHECK_ACTION(local(line));
        const bool off = strcmp(line,"sleep off")==0 || strcmp(line,"sleep off...")==0 || strcmp(line,"sleep   off")==0;
        CHECK_ACTION(g_force_sleep==!off); transcript(line);
    }
    for (unsigned mask=0;mask<4;++mask) {
        reset_action_fixture(); g_pseg_dm.fail_erase=!(mask&1); g_pseg_ch.fail_erase=!(mask&2);
        CHECK_ACTION(local("prep-restart")); CHECK_ACTION(g_halted);
        CHECK_ACTION(g_pseg_dm.erases>0 && g_pseg_ch.erases>0);
        CHECK_ACTION(g_sink.has("inbox erase incomplete")== (mask!=3)); CHECK_ACTION(!prep_print_before_halt); CHECK_ACTION(actions.front()=="clear-learned"); transcript("prep:"+std::to_string(mask));
    }
    for(const char* line:{"reboot","prep-restart","ota","crashtest fault","crashtest fault-extra","crashtest reboot","crashtest reboo","crashtest bogus"}) {
        reset_action_fixture(); CHECK_ACTION(local(line));
        if(!strcmp(line,"reboot")||!strcmp(line,"crashtest reboot")) {
            std::vector<std::string> expected;
            if(!strcmp(line,"crashtest reboot")) expected.push_back("flush");
            expected.push_back("flush");expected.push_back("delay:100");
#if ACTION_BACKEND != 0
            expected.push_back("mark-reset");expected.push_back("reset");
#endif
            CHECK_ACTION(actions==expected);
        }
        transcript(line);
    }
    for(const char* line:{"crashtest fault","crashtest hang","crashtest bogus"}) {
        reset_action_fixture();meshroute::g_mr_trace_on=false;CHECK_ACTION(local(line));CHECK_ACTION(actions.empty());
        CHECK_ACTION(g_sink.has("enable `debug on` first"));transcript(std::string("debug-off:")+line);
    }
    reset_action_fixture();mrota::s_active=true;CHECK_ACTION(local("ota"));transcript("ota:active");
#if ACTION_BACKEND == 2
    CHECK_ACTION(!mrota::s_active);
#endif
    reset_action_fixture();mrota::WiFi.ok=false;CHECK_ACTION(local("ota"));transcript("ota:start-fail");
#if ACTION_BACKEND == 2
    CHECK_ACTION(!mrota::s_active);CHECK_ACTION(std::strstr(Serial.out,"> OTA: start FAILED\r\n")!=nullptr);
#endif
    reset_action_fixture();nonreturning=true;
    if(sigsetjmp(action_jump,1)==0) local("crashtest reboot");
    transcript("reboot:nonreturning");
    reset_action_fixture();
    if(sigsetjmp(action_jump,1)==0) { ualarm(20000,0); local("crashtest hang-extra"); ualarm(0,0); }
    CHECK_ACTION(jumped==1); transcript("hang:watchdog");
#ifndef ACTION_BASELINE
    const auto support=mrfw::action_build_support();
#if ACTION_BACKEND == 0
    CHECK_ACTION(support.reboot==mrfw::ActionBackend::none);
    CHECK_ACTION(mrfw::action_ota_admit(support).status==mrfw::ActionAdmissionStatus::unsupported);
    CHECK_ACTION(mrfw::action_crash_admit("fault",5,true,support).status==mrfw::ActionAdmissionStatus::unsupported);
#elif ACTION_BACKEND == 1
    CHECK_ACTION(support.reboot==mrfw::ActionBackend::nrf_reset);
    CHECK_ACTION(support.ota==mrfw::ActionBackend::nrf_dfu);
    CHECK_ACTION(support.fault==mrfw::ActionBackend::nrf_fault);
#else
    CHECK_ACTION(support.reboot==mrfw::ActionBackend::esp_reset);
    CHECK_ACTION(support.ota==mrfw::ActionBackend::wifi_ota);
    CHECK_ACTION(support.fault==mrfw::ActionBackend::esp_fault);
#endif
    CHECK_ACTION(support.power_save == (ACTION_POWERSAVE!=0));
    CHECK_ACTION(mrfw::action_sleep_admit("off",3,support).status == (ACTION_POWERSAVE ? mrfw::ActionAdmissionStatus::ready : mrfw::ActionAdmissionStatus::unsupported));
    for(unsigned mask=0;mask<8;++mask) {
        reset_action_fixture();g_pseg_dm.fail_erase=!(mask&1);g_pseg_ch.fail_erase=!(mask&2);mrprobe_nv().rw_ok=mask&4;
        nonreturning=true;
        if(sigsetjmp(action_jump,1)==0)
            mrfw::action_factory_reset_apply(support.reboot,g_sink,g_sink,{nullptr,observe_action});
        CHECK_ACTION(!outcomes.empty());
        const auto expected=!(mask&1)||!(mask&2) ? ((mask&4)?mrfw::ActionOutcome::inbox_partial:mrfw::ActionOutcome::inbox_nv_partial)
            : ((mask&4)?mrfw::ActionOutcome::started:mrfw::ActionOutcome::nv_partial);
        CHECK_ACTION(outcomes.front()==expected);mrcon.service();CHECK_ACTION(Serial.n_out==0);
    }
    reset_action_fixture();const auto prepared=mrfw::action_crash_admit("reboot",6,true,support);
    meshroute::g_mr_trace_on=false;mrfw::action_crash_apply(prepared.plan,g_sink,g_sink,{nullptr,observe_action});
    CHECK_ACTION(g_sink.has("crashtest reboot"));CHECK_ACTION(!g_sink.has("enable `debug"));
    mrcon.service();CHECK_ACTION(Serial.n_out==0);
    reset_action_fixture();
    mrfw::action_ota_apply(support.ota,g_sink,{nullptr,observe_action});
    mrcon.service();CHECK_ACTION(Serial.n_out==0);CHECK_ACTION(!outcomes.empty());
#if ACTION_BACKEND == 2
    CHECK_ACTION(mrota::s_active);CHECK_ACTION(mrota::s_pre_reboot_hook!=nullptr);
    CHECK_ACTION(outcomes.back()==mrfw::ActionOutcome::completed);
#elif ACTION_BACKEND == 1
    CHECK_ACTION(action_power.GPREGRET.value==0xA8);CHECK_ACTION(outcomes.back()==mrfw::ActionOutcome::unexpected_return);
#else
    CHECK_ACTION(outcomes.back()==mrfw::ActionOutcome::backend_failed);
#endif
    reset_action_fixture();mrota::s_active=true;
    mrfw::action_ota_apply(support.ota,g_sink,{nullptr,observe_action});
#if ACTION_BACKEND == 2
    CHECK_ACTION(mrota::s_active);CHECK_ACTION(actions.empty());CHECK_ACTION(outcomes.back()==mrfw::ActionOutcome::completed);
#endif
    mrcon.service();CHECK_ACTION(Serial.n_out==0);
#endif
    std::printf("ACTION CHECKS %u FAILED %d\n",probe_checks,g_fail);
#ifndef ACTION_BASELINE
    const unsigned radio_before=g_chk;
    run_remote_actions();
    std::printf("REMOTE ACTION CHECKS %u RADIO %u FAILED %d\n",remote_checks,g_chk-radio_before,g_fail);
#endif
    return g_fail?1:0;
}
