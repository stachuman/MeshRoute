#include <doctest/doctest.h>
#include "firmware_action_effects.h"
#include <cstring>
#include <initializer_list>

using namespace mrfw;

TEST_CASE("action admission: confirmation and build support are separate from local grammar") {
    const ActionSupport supported{ActionBackend::nrf_reset, ActionBackend::nrf_dfu, ActionBackend::nrf_fault, true};
    for (const char* arg : {"confirm", " confirm", "   confirm"}) {
        const auto a = action_factory_reset_admit(arg, strlen(arg), supported);
        CHECK(a.status == ActionAdmissionStatus::ready);
        CHECK(a.plan.kind == ActionKind::factory_reset);
        CHECK(a.plan.backend == ActionBackend::nrf_reset);
        const auto unavailable = action_factory_reset_admit(arg, strlen(arg), {});
        CHECK(unavailable.status == ActionAdmissionStatus::unsupported);
        CHECK(unavailable.plan.kind == ActionKind::factory_reset);
    }
    for (const char* arg : {"", " ", "Confirm", "confirm ", "confirmx", "confir", "confirm confirm", "\tconfirm"}) {
        const auto a = action_factory_reset_admit(arg, strlen(arg), supported);
        CHECK(a.status == ActionAdmissionStatus::confirmation);
        CHECK(a.plan.kind == ActionKind::none);
    }
    CHECK(action_factory_reset_admit(nullptr, 0, supported).status == ActionAdmissionStatus::confirmation);
    CHECK(action_reboot_admit(supported).plan.backend == ActionBackend::nrf_reset);
    CHECK(action_ota_admit(supported).plan.backend == ActionBackend::nrf_dfu);
    CHECK(action_reboot_admit({}).status == ActionAdmissionStatus::unsupported);
    CHECK(action_ota_admit({}).status == ActionAdmissionStatus::unsupported);
    CHECK(action_prep_restart_admit().status == ActionAdmissionStatus::ready);
    CHECK(action_prep_restart_admit().plan.kind == ActionKind::prep_restart);
}

TEST_CASE("action admission: sleep preserves prefix grammar even without power saving") {
    const ActionSupport supported{ActionBackend::esp_reset, ActionBackend::wifi_ota, ActionBackend::esp_fault, true};
    struct Row { const char* arg; ActionKind kind; };
    for (const auto row : {Row{"", ActionKind::sleep_on}, {"on", ActionKind::sleep_on},
            {"off", ActionKind::sleep_off}, {"  off...", ActionKind::sleep_off},
            {"OFF", ActionKind::sleep_on}, {"of", ActionKind::sleep_on},
            {"\toff", ActionKind::sleep_on}, {"other", ActionKind::sleep_on}}) {
        const auto a = action_sleep_admit(row.arg, strlen(row.arg), supported);
        const auto b = action_sleep_admit(row.arg, strlen(row.arg), {});
        CHECK(a.status == ActionAdmissionStatus::ready);
        CHECK(a.plan.kind == row.kind);
        CHECK(b.status == ActionAdmissionStatus::unsupported);
        CHECK(b.plan.kind == row.kind);
        CHECK(a.plan.backend == ActionBackend::none);
    }
}

TEST_CASE("action admission: crash owns mode and backend with debug checked before grammar") {
    const ActionSupport supported{ActionBackend::esp_reset, ActionBackend::wifi_ota, ActionBackend::esp_fault, true};
    struct Row { const char* arg; ActionKind kind; ActionBackend backend; };
    for (const auto row : {Row{"hang", ActionKind::crash_hang, ActionBackend::none},
            {" hangmore", ActionKind::crash_hang, ActionBackend::none},
            {"fault", ActionKind::crash_fault, ActionBackend::esp_fault},
            {"  fault-extra", ActionKind::crash_fault, ActionBackend::esp_fault},
            {"reboot", ActionKind::crash_reboot, ActionBackend::esp_reset},
            {"reboot-extra", ActionKind::crash_reboot, ActionBackend::esp_reset}}) {
        const auto a = action_crash_admit(row.arg, strlen(row.arg), true, supported);
        CHECK(a.status == ActionAdmissionStatus::ready);
        CHECK(a.plan.kind == row.kind);
        CHECK(a.plan.backend == row.backend);
        CHECK(action_crash_admit(row.arg, strlen(row.arg), false, supported).status == ActionAdmissionStatus::debug_disabled);
    }
    for (const char* arg : {"", "han", "faul", "reboo", "FAULT", "\tfault", "bogus"}) {
        CHECK(action_crash_admit(arg, strlen(arg), true, supported).status == ActionAdmissionStatus::usage);
        CHECK(action_crash_admit(arg, strlen(arg), false, supported).status == ActionAdmissionStatus::debug_disabled);
    }
    CHECK(action_crash_admit("hang", 4, true, {}).status == ActionAdmissionStatus::ready);
    CHECK(action_crash_admit("fault", 5, true, {}).status == ActionAdmissionStatus::unsupported);
    CHECK(action_crash_admit("reboot", 6, true, {}).status == ActionAdmissionStatus::unsupported);
}

TEST_CASE("action admission: prepared choices outlive mutable source and admission inputs") {
    char args[] = " fault";
    ActionSupport support{ActionBackend::nrf_reset, ActionBackend::nrf_dfu, ActionBackend::nrf_fault, true};
    bool debug = true;
    const auto a = action_crash_admit(args, strlen(args), debug, support);
    memset(args, 'x', sizeof(args)); support = {}; debug = false;
    CHECK(a.status == ActionAdmissionStatus::ready);
    CHECK(a.plan.kind == ActionKind::crash_fault);
    CHECK(a.plan.backend == ActionBackend::nrf_fault);
    CHECK(action_crash_admit("fault", 5, debug, support).status == ActionAdmissionStatus::debug_disabled);
    CHECK(sizeof(ActionKind) == 1);
    CHECK(sizeof(ActionBackend) == 1);
    CHECK(sizeof(ActionPlan) == 2);
}

#include "firmware_remote_actions.h"
TEST_CASE("remote action carrier: every opaque byte is range checked in both directions") {
    for(unsigned byte=0;byte<256;++byte) {
        const auto kind=static_cast<ActionKind>(byte);
        const auto backend=static_cast<ActionBackend>(byte);
        const auto outcome=static_cast<ActionOutcome>(byte);
        const uint8_t k=byte<=9?byte:0,b=byte<=6?byte:0,o=byte<=7?byte:0;
        CHECK(remote_action_byte(kind,ActionKind::crash_reboot)==k);
        CHECK(remote_action_byte(backend,ActionBackend::esp_fault)==b);
        CHECK(remote_action_byte(outcome,ActionOutcome::unexpected_return)==o);
        CHECK(static_cast<uint8_t>(remote_action_enum(byte,ActionKind::crash_reboot))==k);
        CHECK(static_cast<uint8_t>(remote_action_enum(byte,ActionBackend::esp_fault))==b);
        CHECK(static_cast<uint8_t>(remote_action_enum(byte,ActionOutcome::unexpected_return))==o);
        const auto from_kind=remote_action_unpack(byte,2),from_backend=remote_action_unpack(1,byte);
        CHECK(from_kind.kind==static_cast<ActionKind>(k));
        CHECK(from_kind.backend==static_cast<ActionBackend>(byte<=9?2:0));
        CHECK(from_backend.kind==static_cast<ActionKind>(byte<=6?1:0));
        CHECK(from_backend.backend==static_cast<ActionBackend>(b));
        const auto to_kind=remote_action_pack({kind,ActionBackend::esp_reset});
        const auto to_backend=remote_action_pack({ActionKind::reboot,backend});
        CHECK(to_kind.kind==k);CHECK(to_kind.backend==(byte<=9?2:0));
        CHECK(to_backend.kind==(byte<=6?1:0));CHECK(to_backend.backend==b);
        if(byte>9)CHECK(std::strcmp(remote_action_kind_name(byte),"none")==0);
        if(byte>7)CHECK(std::strcmp(remote_action_outcome_name(byte),"none")==0);
    }
    const char* kinds[]={"none","reboot","prep-restart","ota","factory_reset","sleep-on","sleep-off","crash-hang","crash-fault","crash-reboot"};
    const char* outcomes[]={"none","started","completed","inbox_partial","nv_partial","inbox_nv_partial","backend_failed","unexpected_return"};
    for(uint8_t i=0;i<10;++i)CHECK(std::strcmp(remote_action_kind_name(i),kinds[i])==0);
    for(uint8_t i=0;i<8;++i)CHECK(std::strcmp(remote_action_outcome_name(i),outcomes[i])==0);
}
TEST_CASE("remote action admission selects committed grammar and leaves unsupported families refused") {
    const ActionSupport s{ActionBackend::esp_reset,ActionBackend::wifi_ota,ActionBackend::esp_fault,true};
    struct Row {const char* line;ActionAdmissionStatus status;ActionKind kind;};
    for(const auto& row:{Row{"reboot",ActionAdmissionStatus::ready,ActionKind::reboot},
            {"prep-restart",ActionAdmissionStatus::ready,ActionKind::prep_restart},
            {"ota",ActionAdmissionStatus::ready,ActionKind::ota},
            {"factory_reset confirm",ActionAdmissionStatus::ready,ActionKind::factory_reset},
            {"factory_reset confirm ",ActionAdmissionStatus::confirmation,ActionKind::none},
            {"sleep off...",ActionAdmissionStatus::ready,ActionKind::sleep_off},
            {"sleep OFF",ActionAdmissionStatus::ready,ActionKind::sleep_on},
            {"crashtest fault-extra",ActionAdmissionStatus::ready,ActionKind::crash_fault},
            {"crashtest faul",ActionAdmissionStatus::usage,ActionKind::none},
            {"cfg set node_id 7",ActionAdmissionStatus::unsupported,ActionKind::none},
            {"regen",ActionAdmissionStatus::unsupported,ActionKind::none}}) {
        const auto* p=command_policy_lookup(row.line,strlen(row.line));CHECK(p);if(!p)continue;
        const auto a=remote_action_admit(*p,row.line,strlen(row.line),s,true);
        CHECK(a.status==row.status);CHECK(a.plan.kind==row.kind);
    }
    const CommandPolicy crash{"crashtest","hang",CommandClass::owner,true};
    CHECK(remote_action_admit(crash,"crashtest hang",13,s,false).status==ActionAdmissionStatus::debug_disabled);
    const CommandPolicy reboot{"reboot","—",CommandClass::owner,true};
    for(const char* line:{"reboot ","reboot-extra","reboot\t","reboo"})
        CHECK(remote_action_admit(reboot,line,strlen(line),s,true).status!=ActionAdmissionStatus::ready);
}
