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
