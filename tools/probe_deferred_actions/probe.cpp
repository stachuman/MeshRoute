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
// @ACTION_OWNERS@

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
