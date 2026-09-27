// MeshRoute — tools/probe_inbox_verbs/probe_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D FEATURE PROBE — the first automated cover the INBOX CONSOLE VERBS' PRODUCTION WIRING has ever had.
//
// ⛔⛔ WHY IT EXISTS, STATED AS THE DEFECT IT CLOSES RATHER THAN AS A FEATURE. `platformio.ini`'s native env sets
//     `test_build_src = no`, so NEITHER the doctest suite NOR the simulator compiles ANY `src/*.cpp`. The §CUSTODY-D
//     slice shipped a DESTRUCTIVE device command whose gates were: pure-unit tests of the token predicate, of
//     `Inbox::clear()` and of the ack writers — every one of them BELOW the seam. ★ QG's finding, verbatim in
//     substance: every one of those gates would have stayed GREEN if someone had
//        · deleted the `clear_inbox` arm from `dispatch()` (the verb becomes unreachable — `> parse error`),
//        · bypassed the confirmation check in `handle_clear_inbox` (a bare `clear_inbox` DESTROYS the inbox),
//        · deleted the early `return` after the refusal (it refuses AND clears — the worst of both),
//        · or passed a constant `true` into `inbox_clear_result()` (a failed clear prints `cleared`).
//     ⇒ the slice's own test file claimed the refusal was "modelled here by simply not calling clear()", which is
//       an honest description of a MIRROR and is exactly why a mirror could not have caught any of the four.
//     THIS FILE DRIVES THE REAL `mrfw::dispatch()` OVER THE REAL `src/firmware_commands.cpp` AND
//     `src/firmware_inbox.cpp`, linked against the real `lib/core` + `lib/console`.
//
// ★★ WHY A SIBLING PROBE AND NOT AN EXTENSION OF AN EXISTING ONE — the placement question, answered:
//     · `tools/probe_firmware_ui/` is keyed to the OLED PANEL: it links `src/firmware_ui.cpp` under two panel `-D`
//       arms and its md5 tripwire hashes the UI headers. The router is a different TU set, a different `-D` arm
//       (`[env:heltec_v3]`'s, with the ESP32 platform fakes) and a different fake surface. Folding it in would make
//       one probe's failure ambiguous between two subsystems.
//     · `tools/probe_console_sink/` is the closest relative — it already reasons about `firmware_commands.cpp` —
//       but it does so STRUCTURALLY (`structural.py` greps the source) and links no runnable router. A structural
//       check cannot answer "does `clear_inbox confirm` REACH the handler", which is the whole ask.
//     ⇒ a sibling, exactly as `probe_board_ui` / `probe_console_sink` / `probe_device_radio` are siblings. The
//       SHARED fakes (`probe_board_ui/fakes/Arduino.h`, `probe_device_radio/fakes/RadioLib.h`) are REUSED, not
//       forked (U1); only the ESP32 platform headers, which are this arm's alone, live in this probe's `fakes/`.
//
// ⛔ THE ROUTER IS DRIVEN, NEVER THE HANDLER. Every check below calls `mrfw::dispatch(line, len, sink)` — the ONE
//    entry both transports use (`fw_main.cpp:1082` USB / `fw_main.cpp:591` BLE). Calling `handle_clear_inbox`
//    directly would re-open precisely the hole this file closes.
//
// NB: `-fno-exceptions`; the probe reports with CHK and returns 0/1. It must never crash — see `classify_control`
//     in run.sh, which treats a dying mutant as UNUSABLE rather than as a successful reddening ([[B237]]).
#include "firmware_action_effects.h"
#include "fw_context.h"          // the REAL device context — g_node/g_hal + every global the router references
#include "firmware_commands.h"   // mrfw::dispatch — THE ROUTER UNDER TEST
#include "firmware_inbox.h"
#include "firmware_config.h"     // Slice 10: the real header-owned NV boot formatter
#include "console_json.h"
#include "inbox.h"
#include "sched_send.h"
#include "identity.h"
#include "fault_log.h"
#include "board_rf_provider.h"   // meshroute::board_rf_instance() — the same seam fw_main.cpp:177 passes
// §RADMIN-0b / [[B279]] — the two SINKS the slice is about, and the two subsystems `regen` moves:
#include "console_sink.h"        // `mrcon`, the REAL guarded global console — the sink `regen` must NOT choose
#include "dispatch_sink.h"       // `LineSink`, the PRODUCTION BLE sink (fw_main.cpp:598) — the sink it must USE
#include "device_nv.h"           // mrnv::IdBlob / load_id / save_id — /mrid, the record `regen` rewrites
#include "device_rng.h"          // mrrng::fill — the seed draw `regen` makes
// §RADMIN slice 3 — the two target stores' PURE services, so the probe can read back what the REAL router wrote
// through the REAL `mrnv::load_admin_id` / `load_acl` wrappers, and can render the stored seed's hex to prove it
// appears NOWHERE in any console line.
#include "firmware_admin_verbs.h"
// §RADMIN slice 4 — the two CONTROLLER stores' PURE services, so the CLIENT arm can read back what the REAL
// router wrote through the REAL `mrnv::load_mgmt_keys` / `load_targets` wrappers. ⛔ UNGATED, like the header
// above: both are capability-free ([[B255]]) and compile on either arm.
#include "firmware_admin_client_verbs.h"
#include <cctype>                // isxdigit (the "8 UPPERCASE hex digits" row, pinned without the format string)
#include "monocypher.h"          // crypto_wipe — WRAPPED below, so [[B321]]'s scratch wipe is OBSERVED, not grepped

// ================================================================================================================
// ★★★ [[B321]] — THE TEST-ONLY `crypto_wipe` INTERPOSER, AND WHY IT HAS TO EXIST.
//
//     `mrfw::parse_hex32`'s 32-byte staging buffer is a STACK FRAME of a function that has already returned by the
//     time any caller could look at it, so "the scratch was wiped" is UNOBSERVABLE from `test/`: reading that
//     storage afterwards is undefined behaviour and any "it was zero" assertion would be measuring luck. ⇒ the
//     observation is moved to the ONE place where the frame is still ALIVE — inside the call itself.
//
//     `-Wl,--wrap=crypto_wipe` makes the LINKER route every call to `__wrap_crypto_wipe`; `__real_crypto_wipe` is
//     the genuine monocypher symbol. This wrapper therefore watches the REAL production call: it copies the bytes
//     BEFORE the wipe, performs the REAL wipe, and re-reads the SAME live storage afterwards.
// ⛔ IT IS NOT A REPLACEMENT DECODER AND NOT A PRODUCTION CALLBACK: production is unmodified and unaware, the real
//    wipe still happens, and the wrapper is inert (a straight forward) unless a case arms it.
// ⛔ AND IT IS THE PROBE'S, NOT THE SUITE'S: `test/test_firmware_config_parse.cpp` deliberately claims only that
//    the GRAMMAR and the OUTPUT did not move. This file owns the wipe.
// ================================================================================================================
extern "C" void __real_crypto_wipe(void* p, size_t n);
struct MrWipeRec { const void* p = nullptr; size_t n = 0; unsigned char before[32] = {}; bool after_zero = false; };
static MrWipeRec g_wipes[32];
static int       g_nwipes     = 0;
static bool      g_wipe_watch = false;
extern "C" void __wrap_crypto_wipe(void* p, size_t n) {
    if (!g_wipe_watch || g_nwipes >= 32) { __real_crypto_wipe(p, n); return; }
    MrWipeRec& w = g_wipes[g_nwipes++];
    w.p = p;
    w.n = n;
    const size_t k = n < sizeof w.before ? n : sizeof w.before;
    std::memcpy(w.before, p, k);                       // ⓘ the frame is ALIVE — this is fully-defined behaviour
    __real_crypto_wipe(p, n);
    w.after_zero = true;
    for (size_t i = 0; i < k; ++i) if (static_cast<const unsigned char*>(p)[i] != 0) w.after_zero = false;
}
static void wipe_watch_begin() { g_nwipes = 0; g_wipe_watch = true; }
static void wipe_watch_end()   { g_wipe_watch = false; }

#include <cstdio>
#include <cstring>
#include <string>

// Slice 6: count calls through the REAL Node command entry, using the existing link-interposer idiom.
// This is observation, not a fake executor: every call is forwarded to the genuine implementation.
static unsigned g_command_calls = 0;
extern "C" meshroute::CmdResult __real__ZN9meshroute4Node10on_commandERKNS_7CommandE(meshroute::Node*, const meshroute::Command&);
extern "C" meshroute::CmdResult __wrap__ZN9meshroute4Node10on_commandERKNS_7CommandE(meshroute::Node* node, const meshroute::Command& command) {
    ++g_command_calls;
    return __real__ZN9meshroute4Node10on_commandERKNS_7CommandE(node, command);
}

// ================================================================================================================
// The probe's report primitive (the `probe_firmware_ui` idiom, verbatim: `  ok  ` / `  FAIL ` and a failure count).
// ================================================================================================================
static int g_fail = 0;
static int g_chk  = 0;
#define CHK(cond, ...) do { ++g_chk; if (cond) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
                            else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

// ================================================================================================================
// The transport sink. `dispatch()` writes every response through a `Print&`, exactly as the USB console and the BLE
// LineSink do, so capturing here captures the REAL bytes a companion would receive.
// ================================================================================================================
struct CaptureSink : public Print {
    char   buf[4096] = {};
    size_t n = 0;
    void (*newline_hook)(const char*) = nullptr;
    size_t write(uint8_t b) override {
        if (n + 1 < sizeof buf) { buf[n++] = char(b); buf[n] = '\0'; }
        if (b == '\n' && newline_hook) newline_hook(buf);
        return 1;
    }
    void   reset() { n = 0; buf[0] = '\0'; }
    bool   is(const char* s) const { return std::strcmp(buf, s) == 0; }
    bool   has(const char* s) const { return std::strstr(buf, s) != nullptr; }
};

// ================================================================================================================
// The store the verb actually destroys. A minimal `InboxStore` with the counters the four QG questions need:
//   · `wipe_calls`      — "was the clear performed AT ALL?"   (the refusal arms assert 0)
//   · `set_next_calls`  — "did the high-water persist run?"   (the erase-neither arm asserts the erase did not)
//   · `fail_wipe` / `fail_set_next` — the medium refusing, so the io_error ack is reached through the REAL verdict.
// ⓘ NOT the production `SegmentedInboxStore`: this probe is about the VERB's wiring, and the store's own behaviour
//   is pinned by test_segmented_inbox_store.cpp + the §CUSTODY-D native cases. Installed into the REAL
//   `g_node.inbox()`, so `handle_clear_inbox`'s `g_node.inbox().clear()` is the real `Inbox::clear()` over it.
struct ProbeStore : public meshroute::InboxStore {
    // B372: the former body[8] silently truncated even the 32-byte serialized header.
    struct Rec { uint32_t seq; uint8_t body[meshroute::inbox_record_max_bytes]; uint16_t len; };
    Rec      rec[16] = {};
    uint16_t n_rec = 0;
    uint32_t persisted_next = 0, cursor = 0, epoch = 1;
    int      wipe_calls = 0, set_next_calls = 0, cursor_calls = 0;
    bool     fail_wipe = false, fail_set_next = false;

    bool begin() override { return true; }
    bool append(uint32_t seq, const uint8_t* r, uint16_t len) override {
        if (n_rec >= 16 || len > meshroute::inbox_record_max_bytes) return false;
        rec[n_rec].seq = seq; rec[n_rec].len = len;
        for (uint16_t i = 0; i < rec[n_rec].len; ++i) rec[n_rec].body[i] = r[i];
        ++n_rec; return true;
    }
    uint16_t read_since(uint32_t since, ReadCb cb, void* ctx) const override {
        uint16_t v = 0;
        for (uint16_t i = 0; i < n_rec; ++i) {
            if (rec[i].seq <= since) continue;
            ++v;
            if (!cb(ctx, rec[i].seq, rec[i].body, rec[i].len)) break;
        }
        return v;
    }
    uint32_t persisted_next_seq() const override { return persisted_next; }
    bool     set_next_seq(uint32_t next) override {
        ++set_next_calls;
        if (fail_set_next) return false;
        persisted_next = next; return true;
    }
    uint32_t read_cursor() const override { return cursor; }
    bool     set_read_cursor(uint32_t s) override { ++cursor_calls; cursor = s; return true; }
    uint16_t count() const override { return n_rec; }
    uint32_t storage_epoch() const override { return epoch; }
    using meshroute::InboxStore::wipe;
    bool wipe(uint32_t target_epoch) override {
        ++wipe_calls;
        if (fail_wipe) return false;
        n_rec = 0; cursor = 0;
        if (target_epoch) epoch = target_epoch;
        return true;
    }
    void reset_counters() { wipe_calls = set_next_calls = cursor_calls = 0; fail_wipe = fail_set_next = false; }
};
static ProbeStore g_dm_store, g_ch_store;

// ================================================================================================================
// THE DEVICE CONTEXT. These are the SAME definitions `fw_main.cpp` makes — this probe stands in for that TU and for
// nothing else. ⛔ Every one is a DEFINITION of a symbol `fw_context.h` already declares: no local `extern` is
// restated here, so there is still exactly one declaration per global (fw_context_pure.h's 1:1 rule).
// ================================================================================================================
Module                  g_mod(-1, -1, -1, -1);
CustomSX1262            g_radio;
meshroute::ArduinoClock g_clock;
meshroute::Sx1262Radio  g_iradio(g_radio, meshroute::board_rf_instance());   // exactly fw_main.cpp:177
meshroute::DeviceHal    g_hal(g_clock, g_iradio);
meshroute::Node         g_node(g_hal, /*node_id=*/0, /*key_hash32=*/0, "node");
meshroute::Identity     g_identity;
mrsched::Schedule       g_sched;
char                    s_inbox_jb[1700];
uint8_t                 g_rxbuf[meshroute::protocol::max_payload_bytes_hard_cap + 32];

// The production inbox-store globals. ⛔ DEFINED, DELIBERATELY UNUSED: `factory_reset` references them, so the link
// needs them, but the verb under test reaches its stores through `g_node.inbox()` — which the probe wires to
// `g_dm_store`/`g_ch_store` below. Keeping them inert is what makes "no non-inbox store was touched" observable.
#if defined(MRINBOX_QSPI_READY) || defined(MRINBOX_ESP32_LITTLEFS)
struct ProbeSegs : public meshroute::ISegmentStore {
    bool     mount(bool* f) override { if (f) *f = false; return false; }
    bool     seg_size(uint16_t, uint32_t*) const override { return false; }
    bool     seg_append(uint16_t, const uint8_t*, uint16_t) override { return false; }
    uint32_t seg_read(uint16_t, uint8_t*, uint32_t) const override { return 0; }
    const char* trace_tag = nullptr;
    bool     seg_erase(uint16_t) override {
        ++erases;
        if (trace_tag && mrprobe_nv().observe) mrprobe_nv().observe(trace_tag);
        return !fail_erase;
    }
    bool     fail_erase = false;
    bool     any_segments(bool* ok) const override { if (ok) *ok = true; return false; }
    int      erases = 0;
};
struct ProbeMeta : public meshroute::IMetaStore {
    meshroute::MetaLoad load(void*, uint16_t) override { return meshroute::MetaLoad::absent; }
    bool save(const void*, uint16_t) override { ++saves; return true; }
    int  saves = 0;
};
static ProbeSegs g_pseg_dm, g_pseg_ch;
static ProbeMeta g_pmeta_dm, g_pmeta_ch;
meshroute::SegmentedInboxStore g_inbox_dm(g_pseg_dm, g_pmeta_dm, meshroute::protocol::inbox_dm_store_bytes,
                                          meshroute::protocol::inbox_segment_bytes);
meshroute::SegmentedInboxStore g_inbox_ch(g_pseg_ch, g_pmeta_ch, meshroute::protocol::inbox_chan_store_bytes,
                                          meshroute::protocol::inbox_segment_bytes);
static int production_store_touches() { return g_pseg_dm.erases + g_pseg_ch.erases + g_pmeta_dm.saves + g_pmeta_ch.saves; }
#else
meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_dm;
meshroute::FixedInboxStore<MR_RAM_INBOX_SLOTS> g_inbox_ch;
static int production_store_touches() { return int(g_inbox_dm.count()) + int(g_inbox_ch.count()); }
#endif

uint32_t g_rx_count = 0, g_sleep_count = 0;
uint32_t g_wake_gpio = 0, g_wake_ext1 = 0, g_wake_timer = 0;
uint32_t g_wake_arm_busy = 0, g_wake_arm_fail = 0, g_wake_disarm_fail = 0, g_wake_sleep_fail = 0;
bool     g_force_sleep = false, g_halted = false, g_host_present = false, g_radio_ok = true, g_fs_reformatted = false;
bool     g_last_reset_valid = false;
int8_t   g_tx_power = 0;
double   g_freq_mhz = 869.525;
uint8_t  g_ble_mode = 0, g_ble_period_min = 15;
uint32_t g_ble_pin = 123456;
uint32_t g_remote_action_activation_ms = 0;
int32_t  g_lat_e7 = 0, g_lon_e7 = 0;
mrfault::FaultRecord g_last_reset{};

// ================================================================================================================
// THE OTHER HANDLERS — stubs, and each one RECORDS THAT IT RAN. ⛔ That is not padding: it is how the probe proves
// the router sent `clear_inbox` to `handle_clear_inbox` and to NOTHING ELSE. A dispatch arm that fell through to a
// neighbouring verb would show up here as a foreign call, not merely as a missing ack.
// ================================================================================================================
static char g_routed[64] = {};
static void routed(const char* who) { std::snprintf(g_routed, sizeof g_routed, "%s", who); }
namespace mrfw {
void handle_cfg_set(const char*, Print&)      { routed("cfg_set"); }
void handle_create(const char*, Print&)       { routed("create"); }
void handle_gateway(const char*, Print&)      { routed("gateway"); }
void handle_join(const char*, Print&)         { routed("join"); }
void handle_joinprofile(const char*, Print&)  { routed("joinprofile"); }
void handle_leave(Print&)                     { routed("leave"); }
void handle_mobile(const char*, Print&)       { routed("mobile"); }
void handle_team(const char*, Print&)         { routed("team"); }
}  // namespace mrfw
#ifndef MR_PROBE_ACTION_EFFECTS
namespace mrfw {
// The standing router probe still fakes board effects; probe_deferred_actions separately executes their owners.
ActionSupport action_build_support() {
    return {ActionBackend::esp_reset, ActionBackend::wifi_ota, ActionBackend::esp_fault, true};
}
ActionOutcome action_reboot_apply(ActionBackend, Print&, ActionObserver observer, ActionOutcome outcome) {
    routed("reboot");
    return action_report(observer, outcome);
}
ActionOutcome action_ota_apply(ActionBackend, Print&, ActionObserver observer) {
    routed("ota"); return action_report(observer, ActionOutcome::completed);
}
ActionOutcome action_prep_restart_apply(Print&, ActionObserver observer) {
    routed("prep_restart"); return action_report(observer, ActionOutcome::completed);
}
ActionOutcome action_crash_apply(ActionPlan, Print&, Print&, ActionObserver observer) {
    routed("crashtest"); return action_report(observer, ActionOutcome::started);
}
}
void fw_reboot()                 { routed("reboot"); }
void fw_ota()                    { routed("ota"); }
void fw_prep_restart(Print&)     { routed("prep_restart"); }
void fw_crashtest(const char*, Print&) { routed("crashtest"); }
void fw_faults_dump(Print&)      { routed("faults_dump"); }
#endif // MR_PROBE_ACTION_EFFECTS: the action probe links the actual board-owned functions
// ⓘ `handle_del_msg` / `handle_mark_read` / `handle_pull_inbox` are DELIBERATELY NOT STUBBED: they live in the
//   REAL `src/firmware_inbox.cpp` this probe links, so W10 below observes their REAL acks — a stronger check than a
//   stub's name, and it also proves the new arm was INSERTED beside them rather than substituted for one.
//   `mrfault::*` likewise comes from the real `lib/core/fault_log.cpp`.

// ================================================================================================================
// Fixtures
// ================================================================================================================
static CaptureSink g_sink;

// Seed a realistic inbox: DM records + channel records through the REAL Inbox, so the high-water and the ack's
// `dm_seq`/`chan_seq` are values the production path produced rather than constants the probe chose.
static void seed_inbox(unsigned n_dm, unsigned n_ch) {
    g_dm_store = ProbeStore(); g_ch_store = ProbeStore();
    g_node.inbox().on_init(&g_dm_store, &g_ch_store);
    for (unsigned i = 0; i < n_dm; ++i)
        g_node.inbox().record_dm(5, 0, uint16_t(100 + i), 0, reinterpret_cast<const uint8_t*>("m"), 1, 1000 + i);
    for (unsigned i = 0; i < n_ch; ++i)
        g_node.inbox().record_channel(3, 0x07000000u + i, 0, reinterpret_cast<const uint8_t*>("c"), 1, 2000 + i);
    g_dm_store.reset_counters(); g_ch_store.reset_counters();
    g_routed[0] = '\0';
    g_sink.reset();
}
// ⛔ DRIVE THE ROUTER. Never `handle_clear_inbox` — see the header note.
static bool route(const char* line) { return mrfw::dispatch(line, std::strlen(line), g_sink); }

// ================================================================================================================
// §RADMIN-0b / [[B279]] FIXTURES — the SECOND transport, the NV medium's controls and the RNG replay.
// ⛔ The BLE side is the PRODUCTION `LineSink` over a probe capture — the exact shape `fw_main.cpp`'s
//    `ble_dispatch_line` uses (`LineSink ls(ble_sink); if (dispatch(line, len, ls)) { ls.flush(); }`). A bespoke
//    capture would prove the router writes SOMEWHERE; only the real sink proves it writes what BLE would ship.
// ================================================================================================================
static char   g_ble[4096] = {};
static size_t g_ble_n     = 0;
static void   ble_capture(const char* s, size_t n) {          // stands in for fw_main.cpp's `ble_sink` (mrble::tx_line)
    for (size_t i = 0; i < n && g_ble_n + 1 < sizeof g_ble; ++i) g_ble[g_ble_n++] = s[i];
    g_ble[g_ble_n] = '\0';
}
static void   ble_reset() { g_ble_n = 0; g_ble[0] = '\0'; }

// Reset the probe's deterministic entropy stream (fakes/esp_random.h).
static void rng_reset() { mrprobe_rng() = MrProbeRng(); }

// The seed `regen` MUST mint next, and the identity it MUST derive — computed by replaying the probe's own
// deterministic stream through the REAL `mrrng::fill` + the REAL `identity_from_seed`. ⛔ The probe models the
// entropy STREAM and nothing else: if production skipped the draw, drew a different count, or persisted the seed
// it had loaded, the bytes below stop matching.
static void expected_next_identity(uint8_t seed_out[32], meshroute::Identity& id_out) {
    rng_reset();
    mrrng::fill(seed_out, 32);
    meshroute::identity_from_seed(id_out, seed_out);
    rng_reset();                                  // put the stream back so production draws exactly these bytes
}

// Seed a VALID `/mrid` through `mrnv::save_id` — PRODUCTION'S OWN WRITER (U2: never a hand-built carrier), so the
// record `regen` later loads is one production produced. Leaves the medium healthy and the counters zeroed.
static mrnv::IdBlob seed_id(const char* name, uint16_t name_len, uint8_t seed_byte) {
    MrProbeNv& nv = mrprobe_nv();
    nv.reset();
    nv.ns_present = true; nv.rw_ok = true;                    // a written namespace on a healthy medium
    mrnv::IdBlob idb{};
    idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
    idb.name_len = name_len;
    for (uint16_t i = 0; i < name_len && i < sizeof idb.name; ++i) idb.name[i] = name[i];
    for (size_t i = 0; i < sizeof idb.seed; ++i) idb.seed[i] = uint8_t(seed_byte + i);
    (void)mrnv::save_id(idb);
    nv.writes = 0; nv.reads = 0;                              // the seeding write is the fixture's, not the verb's
    return idb;
}

// "`key_hash32= 0x` + EXACTLY 8 UPPERCASE hex digits" — pinned WITHOUT reusing the `%08lX` that builds the golden
// line, so the format specifier and the assertion cannot agree with each other while both being wrong.
static bool hash_field_is_8_upper_hex(const char* s) {
    const char* p = std::strstr(s, "key_hash32= 0x");
    if (!p) return false;
    p += std::strlen("key_hash32= 0x");
    int n = 0; bool lower = false;
    while (std::isxdigit(static_cast<unsigned char>(p[n]))) {
        if (p[n] >= 'a' && p[n] <= 'f') lower = true;
        ++n;
    }
    return n == 8 && !lower;
}

// Drive the router with a BLE-shaped transport, exactly as `ble_dispatch_line` does. Returns dispatch()'s verdict.
static bool route_ble(const char* line) {
    LineSink ls(ble_capture);
    const bool owned = mrfw::dispatch(line, std::strlen(line), ls);
    ls.flush();                       // ship any trailing partial line — the production call site does this too
    return owned;
}

// ★★ §RADMIN-0c — THE ONE `#ifndef` IN THIS FILE, AND IT IS A REUSE SEAM RATHER THAN A FEATURE FLAG.
// `tools/probe_inbox_verbs/transcript_main.cpp` (the slice's BEFORE/AFTER byte-identity comparator) needs EVERY
// fixture above — the real `g_node`/`g_hal` device context, the resettable inbox/NV/RNG fakes, `ble_capture`,
// `mrcon` — and needs its OWN `main()`. It therefore `#include`s THIS FILE with `MR0C_NO_MAIN` defined, so the two
// drivers share one fixture authority instead of forking a second one (U1: the sibling probes' standing lesson —
// two fakes is how two probes end up measuring two different devices). ⛔ The default gate defines nothing, so the
// 71 checks below compile and run EXACTLY as before; `run.sh`'s md5 tripwire covers this file either way.
#ifndef MR0C_NO_MAIN
#include "remote_exec_rows.h"
#include "remote_client_rows.h"
int main() {
    printf("== §CUSTODY-D inbox-verb wiring probe (REAL dispatch() + REAL handle_clear_inbox, host-linked) ==\n");

    // ------------------------------------------------------------------------------------------------------
    // W1 — THE ROUTE EXISTS AT ALL. If the dispatch arm is deleted, `dispatch()` falls through to `return false`
    //      and the transports answer their unknown-verb error; the verb is unreachable and every ack below is
    //      unreachable with it. This is the check the "dispatch arm removed" control has to redden.
    // ------------------------------------------------------------------------------------------------------
    seed_inbox(3, 2);
    const bool owned = route("clear_inbox");
    CHK(owned, "W1  dispatch() OWNS `clear_inbox` (the router reaches the verb at all)");
    CHK(std::strcmp(g_routed, "") == 0, "W1b ...and routed it to NO other verb's handler (saw '%s')", g_routed);

    // ------------------------------------------------------------------------------------------------------
    // W2 — THE REFUSAL, THROUGH THE ROUTER: exact bytes, and ⛔ NOTHING TOUCHED.
    //      `wipe_calls == 0` is the load-bearing half: it is the assertion the slice's native case could only
    //      MODEL, because a pure unit cannot observe a handler declining to call `clear()`.
    // ------------------------------------------------------------------------------------------------------
    CHK(g_sink.is("{\"ack\":\"clear_inbox\",\"result\":\"needs_confirm\"}\n"),
        "W2  bare `clear_inbox` answers EXACTLY the needs_confirm line [%s]", g_sink.buf);
    CHK(g_dm_store.wipe_calls == 0 && g_ch_store.wipe_calls == 0,
        "W2b ...and NEITHER store was wiped (dm=%d ch=%d)", g_dm_store.wipe_calls, g_ch_store.wipe_calls);
    CHK(g_dm_store.set_next_calls == 0 && g_ch_store.set_next_calls == 0,
        "W2c ...and the clear was not even ENTERED (no high-water persist ran)");
    CHK(g_dm_store.n_rec == 3 && g_ch_store.n_rec == 2,
        "W2d ...and every record is still there (dm=%u ch=%u)", g_dm_store.n_rec, g_ch_store.n_rec);

    // The hardened token corpus, each one through the REAL router.
    struct { const char* line; const char* why; } refuse[] = {
        { "clear_inbox confirm extra", "a SECOND token" },
        { "clear_inbox confirmation",  "a longer word starting with it" },
        { "clear_inbox confirmX",      "trailing junk" },
        { "clear_inbox confirm ",      "a trailing space" },
        { "clear_inbox CONFIRM",       "the wrong case" },
        { "clear_inbox yes",           "a different word" },
        { "clear_inbox   ",            "spaces only" },
    };
    for (auto& r : refuse) {
        seed_inbox(3, 2);
        const bool own = route(r.line);
        CHK(own && g_sink.is("{\"ack\":\"clear_inbox\",\"result\":\"needs_confirm\"}\n")
                && g_dm_store.wipe_calls == 0 && g_ch_store.wipe_calls == 0
                && g_dm_store.n_rec == 3 && g_ch_store.n_rec == 2,
            "W3  `%s` (%s) -> needs_confirm, INERT", r.line, r.why);
    }

    // ------------------------------------------------------------------------------------------------------
    // W4 — CONFIRMED SUCCESS, THROUGH THE ROUTER: the exact `cleared` ack, whose three numbers come from the real
    //      `Inbox` after the real clear — the epoch it computed, and the high-waters it PRESERVED.
    // ------------------------------------------------------------------------------------------------------
    seed_inbox(3, 2);
    const uint32_t epoch_before = g_node.inbox().storage_epoch();
    const int touches_before = production_store_touches();
    CHK(route("clear_inbox confirm"), "W4  dispatch() owns `clear_inbox confirm`");
    {
        char want[128];
        std::snprintf(want, sizeof want,
                      "{\"ack\":\"clear_inbox\",\"result\":\"cleared\",\"epoch\":%u,\"dm_seq\":3,\"chan_seq\":2}\n",
                      unsigned(epoch_before + 1));
        CHK(g_sink.is(want), "W4b ...and the ack is EXACTLY the cleared line [%s]", g_sink.buf);
    }
    CHK(g_dm_store.wipe_calls == 1 && g_ch_store.wipe_calls == 1,
        "W4c ...both stores wiped exactly once (dm=%d ch=%d)", g_dm_store.wipe_calls, g_ch_store.wipe_calls);
    CHK(g_dm_store.n_rec == 0 && g_ch_store.n_rec == 0, "W4d ...and both are empty afterwards");
    CHK(g_dm_store.set_next_calls >= 1 && g_ch_store.set_next_calls >= 1,
        "W4e ...the high-water was persisted BEFORE the erase (both stores)");
    CHK(g_dm_store.cursor == 0 && g_ch_store.cursor == 0, "W4f ...and both read cursors are reset");
    CHK(std::strcmp(g_routed, "") == 0, "W4g ...and no other verb's handler ran (saw '%s')", g_routed);
    CHK(production_store_touches() == touches_before,
        "W4h ...and the PRODUCTION g_inbox_dm/g_inbox_ch stores were not touched (%d)", production_store_touches());

    // ------------------------------------------------------------------------------------------------------
    // W5 — FAILURE / PARTIAL FAILURE, THROUGH THE ROUTER. ⛔ The one direction a destructive report must never
    //      fail in: `cleared` over records that are still on the medium.
    // ------------------------------------------------------------------------------------------------------
    seed_inbox(3, 2);
    g_dm_store.fail_wipe = true;
    const uint32_t e5 = g_node.inbox().storage_epoch();
    route("clear_inbox confirm");
    {
        char want[160];
        std::snprintf(want, sizeof want,
                      "{\"ack\":\"clear_inbox\",\"result\":\"io_error\",\"warning\":\"messages_may_remain\","
                      "\"epoch\":%u,\"dm_seq\":3,\"chan_seq\":2}\n", unsigned(e5));
        CHK(g_sink.is(want), "W5  a DM-store failure answers EXACTLY the io_error line [%s]", g_sink.buf);
    }
    CHK(!g_sink.has("\"cleared\""), "W5b ...and the word `cleared` appears NOWHERE in a failed clear's ack");
    CHK(g_dm_store.wipe_calls == 1 && g_ch_store.wipe_calls == 1,
        "W5c ...BOTH wipes were still attempted — no short-circuit (dm=%d ch=%d)",
        g_dm_store.wipe_calls, g_ch_store.wipe_calls);
    CHK(g_ch_store.n_rec == 0, "W5d ...and the healthy store WAS erased (a partial clear erases what it can)");

    seed_inbox(3, 2);
    g_ch_store.fail_wipe = true;
    route("clear_inbox confirm");
    CHK(g_sink.has("\"result\":\"io_error\"") && g_sink.has("\"warning\":\"messages_may_remain\"")
        && !g_sink.has("\"cleared\""),
        "W6  a CHANNEL-store failure answers io_error + the warning, never cleared [%s]", g_sink.buf);
    CHK(g_dm_store.n_rec == 0 && g_ch_store.n_rec == 3 - 3 + 2,
        "W6b ...the DM half completed and the channel half did not (dm=%u ch=%u)", g_dm_store.n_rec, g_ch_store.n_rec);

    seed_inbox(3, 2);
    g_dm_store.fail_set_next = true;
    route("clear_inbox confirm");
    CHK(g_sink.has("\"result\":\"io_error\"") && !g_sink.has("\"cleared\""),
        "W7  a high-water that will not persist answers io_error [%s]", g_sink.buf);
    CHK(g_dm_store.wipe_calls == 0 && g_ch_store.wipe_calls == 0,
        "W7b ...and NEITHER store was erased (dm=%d ch=%d) — the records are still the only witness",
        g_dm_store.wipe_calls, g_ch_store.wipe_calls);
    CHK(g_dm_store.n_rec == 3 && g_ch_store.n_rec == 2, "W7c ...so every record survives a refused clear");

    // ------------------------------------------------------------------------------------------------------
    // W8 — THE ARM'S BOUNDARY. The router matches on an exact verb + a space or end-of-line; a neighbouring token
    //      must NOT reach the handler. This is what a wrong length constant in the arm breaks.
    // ------------------------------------------------------------------------------------------------------
    seed_inbox(1, 1);
    CHK(!route("clear_inboxx confirm"), "W8  `clear_inboxx confirm` is NOT owned by the router (no prefix match)");
    CHK(g_dm_store.wipe_calls == 0 && g_sink.n == 0, "W8b ...and it produced no ack and no clear");
    seed_inbox(1, 1);
    CHK(!route("clear_inbo"), "W8c `clear_inbo` (short) is not owned either");
    seed_inbox(1, 1);
    CHK(!route("clear"), "W8d nor the bare word `clear`");

    // ------------------------------------------------------------------------------------------------------
    // W9 — ONE ROUTER, BOTH TRANSPORTS. `dispatch(line, len, Print&)` is the single entry `fw_main.cpp` calls from
    //      the USB console (:1082) and from the BLE LineSink (:591); the sink is the ONLY thing that differs.
    //      Driving the same line into a SECOND, independent sink must produce byte-identical output — which is
    //      what makes "serial and BLE both reach it" a measurement rather than a claim about two call sites.
    // ------------------------------------------------------------------------------------------------------
    {
        seed_inbox(2, 1);
        CaptureSink ble;
        mrfw::dispatch("clear_inbox", std::strlen("clear_inbox"), ble);
        const bool same = std::strcmp(ble.buf, "{\"ack\":\"clear_inbox\",\"result\":\"needs_confirm\"}\n") == 0;
        CHK(same, "W9  the same router serves a SECOND (BLE-shaped) sink byte-identically [%s]", ble.buf);
        CHK(g_dm_store.wipe_calls == 0, "W9b ...and that transport's refusal is inert too");
    }

    // ------------------------------------------------------------------------------------------------------
    // W10 — the neighbouring inbox verbs still route (the arm was INSERTED, not substituted for one of them).
    // ------------------------------------------------------------------------------------------------------
    seed_inbox(1, 1); route("del_msg dm 1");
    CHK(g_sink.has("\"ack\":\"del_msg\""), "W10  `del_msg` still reaches its own REAL handler [%s]", g_sink.buf);
    seed_inbox(1, 1); route("mark_read dm 1");
    CHK(g_sink.has("\"ack\":\"mark_read\""), "W10b `mark_read` still reaches its own REAL handler [%s]", g_sink.buf);
    seed_inbox(1, 1); route("pull_inbox 0 0");
    CHK(g_sink.has("\"ev\":\"inbox_end\""), "W10c `pull_inbox` still reaches its own REAL handler [%s]", g_sink.buf);


    // ==============================================================================================================
    // R — §RADMIN-0b / [[B279]]: `regen` ANSWERS ON THE SINK `dispatch()` HANDED IT.
    //
    // ⛔ THE DEFECT, STATED AS THE MEASUREMENT THAT FOUND IT: `dispatch(line, len, Print& out)` recognised `regen`
    //    and called a PARAMETERLESS `do_regen()`, whose error line, success prefix and shared `print_identity()`
    //    all wrote to the global `mrcon`. Over BLE that is a matched command with an EMPTY `LineSink` — the
    //    companion sees no answer at all — while the response leaks onto USB. Every check below fails against that
    //    shape and passes against the fixed one; run.sh's controls C8/C9/C10 restore each half of it in turn.
    // ⛔ THE ROUTER IS DRIVEN, NEVER `do_regen` — it is `static`, and calling the formatter directly would re-open
    //    exactly the wiring hole this section exists to close. The ONE direct call below is the BOOT formatter,
    //    which is a caller in its own right (`fw_main.cpp`'s `setup()`), and it is labelled as such.
    // ==============================================================================================================
    {
    static const char kName[]   = "probe-node";                       // a realistic operator label (10 B)
    const uint16_t    kNameLen  = uint16_t(sizeof kName - 1);
    static const char kMaxName[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"; // EXACTLY sizeof(IdBlob::name) = 32
    char want[320];

// ★★★ §RADMIN slice 4 — ON A CLIENT BUILD `do_regen` APPENDS EXACTLY ONE MORE RULED LINE after the success line,
//     on the SAME sink. The expectation below therefore GROWS by that line on the CLIENT arm and is UNCHANGED on
//     the ACCEPT arm — which is what makes *"ACCEPT `regen` output remains byte-identical"* a MEASURED claim
//     rather than an assertion, and what makes the CLIENT warning a byte-pinned one on the real router.
// ⓘ It ends "\n" and NOT "\r\n": the warning goes through the pure emitter's `IAdminLines::line` (one `write`
//   of composed bytes), whereas the success line goes through `Print::println`, which appends CR LF. That
//   asymmetry is production's and is pinned here rather than smoothed over.
#if MR_FEAT_RADMIN_CLIENT
#  define MR_REGEN_NOTE "> regen note old self ACL grants do not follow the new key; " \
                        "dedicated keys and targets preserved\n"
#else
#  define MR_REGEN_NOTE ""
#endif

    // ---------------------------------------------------------------------------------------------------------
    // R1..R8 — THE SAVE FAILURE, THROUGH THE BLE-SHAPED SINK. It runs FIRST on purpose: `crypto_ready()` is still
    //          false here, so "no crypto identity was installed" is an OBSERVED transition rather than a claim
    //          about a flag that was already true.
    // ---------------------------------------------------------------------------------------------------------
    const mrnv::IdBlob before = seed_id(kName, kNameLen, 0x10);
    meshroute::identity_from_seed(g_identity, before.seed);           // the identity the node is running on
    g_node.set_identity(7, g_identity.key_hash32);
    const uint32_t hash_before   = g_identity.key_hash32;
    const bool     crypto_before = g_node.crypto_ready();

    mrprobe_nv().fail_write = true;              // the medium accepts the open and then REFUSES the record
    rng_reset(); ble_reset(); Serial.reset(); mrprobe_nv().writes = 0;
    const bool own_fail_ble = route_ble("regen");
    mrcon.service();                             // drain the REAL guarded console — a leak would surface here
    CHK(own_fail_ble, "R1  dispatch() OWNS `regen` on a BLE-shaped LineSink");
    CHK(std::strcmp(g_ble, "> regen err nv_save_failed\r\n") == 0,
        "R2  a REFUSED /mrid save answers EXACTLY the error line on the SUPPLIED sink [%s]", g_ble);
    CHK(Serial.n_out == 0,
        "R3  ...and NOT ONE byte reached the global console (%u B on Serial)", unsigned(Serial.n_out));
    CHK(!std::strstr(g_ble, "regen ok") && !std::strstr(g_ble, "key_hash32"),
        "R4  ...no success prefix and no identity line accompany the failure");
    CHK(g_identity.key_hash32 == hash_before && g_node.key_hash32() == hash_before,
        "R5  ...the installed identity is UNCHANGED (0x%08lX)", (unsigned long)g_node.key_hash32());
    CHK(g_node.crypto_ready() == crypto_before,
        "R6  ...and NO crypto identity was installed (crypto_ready=%d)", int(g_node.crypto_ready()));
    CHK(mrprobe_nv().writes == 1,
        "R7  ...exactly ONE write was attempted and refused (writes=%d)", mrprobe_nv().writes);
    CHK(mrprobe_nv().holds("mr", "id", &before, sizeof before),
        "R8  ...and /mrid still holds EXACTLY the pre-command record — a refused write retained NOTHING");

    // ---------------------------------------------------------------------------------------------------------
    // R9..R10 — THE SAME FAILURE ON THE USB SINK. Same bytes, other transport, and the BLE side stays silent.
    // ---------------------------------------------------------------------------------------------------------
    seed_id(kName, kNameLen, 0x10);
    mrprobe_nv().fail_write = true;
    rng_reset(); ble_reset(); Serial.reset(); mrprobe_nv().writes = 0;
    const bool own_fail_usb = mrfw::dispatch("regen", 5, mrcon);
    mrcon.service();
    CHK(own_fail_usb && std::strcmp(Serial.out, "> regen err nv_save_failed\r\n") == 0,
        "R9  the SAME failure through `mrcon` is byte-identical on USB [%s]", Serial.out);
    CHK(g_ble_n == 0, "R10 ...and the BLE sink received nothing during the USB drive (%u B)", unsigned(g_ble_n));

    // ---------------------------------------------------------------------------------------------------------
    // R11..R22 — THE SUCCESS, THROUGH THE BLE-SHAPED SINK: the exact line, the persisted record, and the three
    //            identity installs. The expected hash is derived from the probe's own entropy stream, so it is
    //            not read back out of the very global the command writes.
    // ---------------------------------------------------------------------------------------------------------
    const mrnv::IdBlob rec = seed_id(kName, kNameLen, 0x10);
    uint8_t             exp_seed[32] = {};
    meshroute::Identity exp{};
    expected_next_identity(exp_seed, exp);
    ble_reset(); Serial.reset(); mrprobe_nv().writes = 0;
    const bool own_ok_ble = route_ble("regen");
    mrcon.service();
    std::snprintf(want, sizeof want, "> regen ok  key_hash32= 0x%08lX  name=\"%s\"\r\n" MR_REGEN_NOTE,
                  (unsigned long)exp.key_hash32, kName);
    CHK(own_ok_ble, "R11 dispatch() owns `regen` on the success path too");
    CHK(std::strcmp(g_ble, want) == 0,
        "R12 the BLE sink receives EXACTLY the success line [%s]", g_ble);
    CHK(Serial.n_out == 0,
        "R13 ...and NOT ONE byte leaked to the global console (%u B on Serial)", unsigned(Serial.n_out));
    CHK(hash_field_is_8_upper_hex(g_ble),
        "R14 ...the key_hash32 field is exactly EIGHT UPPERCASE hex digits");
    CHK(g_identity.key_hash32 == exp.key_hash32,
        "R15 ...g_identity was re-derived from the FRESH seed (0x%08lX)", (unsigned long)g_identity.key_hash32);
    CHK(g_node.key_hash32() == exp.key_hash32,
        "R16 ...the node's ROUTING identity was re-installed with it");
    CHK(g_node.crypto_ready(),
        "R17 ...and the E2E crypto identity was installed (crypto_ready false -> true)");
    CHK(mrprobe_nv().writes == 1, "R18 exactly ONE /mrid write was made (writes=%d)", mrprobe_nv().writes);
    {
        mrnv::IdBlob got{};
        const bool loaded = mrnv::load_id(got);
        CHK(loaded && got.magic == mrnv::kIdMagic && got.version == mrnv::kIdVersion,
            "R19 ...the saved record re-loads and carries the same magic/version");
        CHK(std::memcmp(got.seed, exp_seed, sizeof exp_seed) == 0 &&
            std::memcmp(got.seed, rec.seed, sizeof rec.seed) != 0,
            "R20 ...its seed is the FRESH draw, not the one it replaced");
        CHK(got.name_len == kNameLen && std::memcmp(got.name, kName, kNameLen) == 0,
            "R21 ...and the operator name was PRESERVED across the mint");
        // ⛔ THE STORAGE-HONESTY ROW. `expect_rec` is built INDEPENDENTLY — the seeded record with only its seed
        //    replaced by the modelled draw — so it answers "did the medium retain exactly what production wrote?"
        //    rather than "does the medium agree with itself?". Controls C12/C13 make the fake lie in both
        //    directions and this row (with R8) is what refuses the lie.
        mrnv::IdBlob expect_rec = rec;
        std::memcpy(expect_rec.seed, exp_seed, sizeof expect_rec.seed);
        CHK(mrprobe_nv().holds("mr", "id", &expect_rec, sizeof expect_rec),
            "R22 the medium retained EXACTLY the record production wrote (name/magic/version kept, seed fresh)");
    }

    // ---------------------------------------------------------------------------------------------------------
    // R23..R24 — THE SAME COMMAND ON USB. Same deterministic seed and name ⇒ the bytes must be IDENTICAL to the
    //            BLE-shaped success. This is the one measurement that makes "both transports get the same text"
    //            a fact rather than an argument about two call sites.
    // ---------------------------------------------------------------------------------------------------------
    seed_id(kName, kNameLen, 0x10);
    expected_next_identity(exp_seed, exp);
    ble_reset(); Serial.reset(); mrprobe_nv().writes = 0;
    const bool own_ok_usb = mrfw::dispatch("regen", 5, mrcon);
    mrcon.service();
    CHK(own_ok_usb && std::strcmp(Serial.out, want) == 0,
        "R23 the USB console receives BYTE-IDENTICAL bytes through `mrcon` [%s]", Serial.out);
    CHK(g_ble_n == 0, "R24 ...and the BLE sink stayed empty during the USB drive (%u B)", unsigned(g_ble_n));

    // ---------------------------------------------------------------------------------------------------------
    // R25..R26 — THE OPTIONAL-NAME BOUNDARY. `print_identity` emits the name segment only for
    //            `0 < name_len <= sizeof name`; both ends keep today's exact bytes.
    // ---------------------------------------------------------------------------------------------------------
    seed_id("", 0, 0x20);
    expected_next_identity(exp_seed, exp);
    ble_reset(); Serial.reset();
    route_ble("regen");
    mrcon.service();
    std::snprintf(want, sizeof want, "> regen ok  key_hash32= 0x%08lX\r\n" MR_REGEN_NOTE, (unsigned long)exp.key_hash32);
    CHK(std::strcmp(g_ble, want) == 0 && Serial.n_out == 0,
        "R25 a record with NO name emits no `name=` segment, and still nothing on USB [%s]", g_ble);

    seed_id(kMaxName, 32, 0x30);
    expected_next_identity(exp_seed, exp);
    ble_reset(); Serial.reset();
    route_ble("regen");
    mrcon.service();
    std::snprintf(want, sizeof want, "> regen ok  key_hash32= 0x%08lX  name=\"%s\"\r\n" MR_REGEN_NOTE,
                  (unsigned long)exp.key_hash32, kMaxName);
    CHK(std::strcmp(g_ble, want) == 0 && Serial.n_out == 0,
        "R26 a MAXIMUM-length (32 B) name is emitted in full, and still nothing on USB [%s]", g_ble);

    // ---------------------------------------------------------------------------------------------------------
    // R27..R30 — THE ROUTER BOUNDARY. `regen` is owned EXACTLY; the neighbours keep their existing non-ownership
    //            and must leave the identity and the medium alone.
    // ---------------------------------------------------------------------------------------------------------
    {
        struct { const char* line; const char* why; } not_regen[] = {
            { "regen ",     "a trailing space" },
            { "regenerate", "a longer word starting with it" },
            { "REGEN",      "the wrong case" },
            { "rege",       "a short prefix" },
        };
        int idx = 0;
        for (auto& r : not_regen) {
            seed_id(kName, kNameLen, 0x40);
            const uint32_t h = g_node.key_hash32();
            rng_reset(); ble_reset(); Serial.reset(); mrprobe_nv().writes = 0;
            const bool owned = route_ble(r.line);
            mrcon.service();
            CHK(!owned && g_ble_n == 0 && Serial.n_out == 0 && mrprobe_nv().writes == 0
                    && g_node.key_hash32() == h,
                "R%d `%s` (%s) is NOT owned as `regen` — no output, no NV write, no identity change",
                27 + idx, r.line, r.why);
            ++idx;
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // R31..R32 — THE BOOT FORMATTER, IN ITS PRODUCTION SHAPE. `setup()` is the formatter's OTHER caller and it
    //            now NAMES its sink: `print_identity(idb, mrcon)`. The bytes must be the identity tail of the
    //            success line, unchanged. (`fw_main.cpp` itself is not host-compilable; that the call site really
    //            reads `print_identity(idb, mrcon)` is pinned STRUCTURALLY by
    //            `tools/probe_console_sink/structural.py` S21, with its own control.)
    // ---------------------------------------------------------------------------------------------------------
    {
        mrnv::IdBlob boot{};
        const bool loaded = mrnv::load_id(boot);
        ble_reset(); Serial.reset();
        mrfw::print_identity(boot, mrcon);                 // ⓘ the BOOT caller's shape, verbatim
        mrcon.service();
        std::snprintf(want, sizeof want, "  key_hash32= 0x%08lX  name=\"%s\"\r\n",
                      (unsigned long)g_identity.key_hash32, kName);
        CHK(loaded && std::strcmp(Serial.out, want) == 0,
            "R31 the canonical formatter emits the boot banner line through the sink it is GIVEN [%s]", Serial.out);
        CHK(g_ble_n == 0, "R32 ...and nothing reached the BLE sink (%u B)", unsigned(g_ble_n));
    }
    }

    // ==============================================================================================================
    // ★★★ §RADMIN-0c — THE EXECUTED SEAM. `mrfw::exec_console_line()` is the ONE place both transports now make the
    //     router-versus-parser decision, execute the command and render the result. `src/fw_main.cpp` cannot be
    //     host-compiled, so the two ADAPTERS are pinned structurally (tools/probe_console_sink/structural.py
    //     S22..S29) — and everything that DECIDES anything is executed HERE, through a REAL `GuardedConsole` and a
    //     REAL `LineSink`, in both format arms. That split is the honest wiring gate; neither half alone would be.
    // ==============================================================================================================
    {
        // ---- provisioning. The seam's interesting arms need a node that can actually accept a command: an id, an
        //      identity and a legal DATA-SF set. ⛔ Done HERE, after every earlier row, so nothing above can shift.
        meshroute::NodeConfig xc{};
        xc.routing_sf = 7;
        xc.allowed_sf_bitmap = (uint16_t)(1u << 7);
        g_node.on_init(xc);
        g_node.set_identity(/*node_id=*/5, g_identity.key_hash32);
        seed_id("probe-node", 10, 0x70);   // the same operator label the R rows use, re-seeded for this block
        seed_inbox(2, 1);

        static char x_reply[256];        // fw_main.cpp's `g_out` capacity, verbatim (device_ble.h:202)
        static char x_line[512];
        const mrfw::CommandContext x_usb{mrfw::CommandTransport::usb, mrfw::CommandAuthority::local,
                                          true, 0, meshroute::console::local_command_max_bytes};
        // This direct-seam probe has no device_ble.h. The real BLE bound is structurally bound at its caller.
        const mrfw::CommandContext x_ble{mrfw::CommandTransport::ble, mrfw::CommandAuthority::local, false, 0, 274};

        // The TWO transport shapes, each built from the REAL sinks the production adapters use.
        auto run_text = [&](const char* line) {
            std::snprintf(x_line, sizeof x_line, "%s", line);
            mrcon.service(); Serial.reset(); ble_reset(); std::memset(x_reply, 0, sizeof x_reply);
            const mrfw::LineExec ex = mrfw::exec_console_line(x_line, std::strlen(x_line),
                                                              mrfw::LineFormat::text, mrcon, nullptr, 0, x_usb);
            mrcon.service();
            return ex;
        };
        auto run_json = [&](const char* line) {
            std::snprintf(x_line, sizeof x_line, "%s", line);
            mrcon.service(); Serial.reset(); ble_reset(); std::memset(x_reply, 0, sizeof x_reply);
            LineSink ls(ble_capture);
            const mrfw::LineExec ex = mrfw::exec_console_line(x_line, std::strlen(x_line),
                                                              mrfw::LineFormat::json, ls, x_reply, sizeof x_reply, x_ble);
            if (ex.state == mrfw::LineExec::State::streamed) ls.flush();   // the BLE adapter's rule, verbatim
            mrcon.service();
            return ex;
        };
        using St = mrfw::LineExec::State;

        // ---- X1/X2 — A ROUTER-OWNED COMMAND ON EACH ARM, WITH ZERO CROSS-SINK BYTES ------------------------------
        auto ex = run_text("whoami");
        CHK(ex.state == St::streamed && std::strstr(Serial.out, "[whoami]") && g_ble_n == 0,
            "X1  router-owned `whoami` on the TEXT arm streams to the supplied sink, 0 B cross-sink [%u/%u]",
            unsigned(Serial.n_out), unsigned(g_ble_n));
        ex = run_json("whoami");
        CHK(ex.state == St::streamed && ex.n == 0 && x_reply[0] == '\0'
                && std::strstr(g_ble, "[whoami]") && Serial.n_out == 0,
            "X2  ...and on the JSON arm it streams through the REAL LineSink, NEVER the 256-B direct buffer "
            "[ret=%u buf0=%d usb=%u]", unsigned(ex.n), int(x_reply[0]), unsigned(Serial.n_out));

        // ---- X3..X5 — A PARSER-OWNED COMMAND: executed ONCE, rendered in each transport's OWN envelope -----------
        const uint16_t ctr0 = g_node.peer_ctr_high();
        ex = run_text("send 5 \"hi\"");
        const uint16_t ctr1 = g_node.peer_ctr_high();
        CHK(ex.state == St::streamed && Serial.out[0] == '>' && !std::strstr(Serial.out, "{\"ack\"")
                && g_ble_n == 0,
            "X3  parser-owned `send` on the TEXT arm renders the TEXT envelope only [%s]", Serial.out);
        ex = run_json("send 5 \"hi\"");
        const uint16_t ctr2 = g_node.peer_ctr_high();
        CHK(ex.state == St::buffered && ex.n > 0 && !std::strncmp(x_reply, "{\"ack\":\"", 8)
                && g_ble_n == 0 && Serial.n_out == 0,
            "X4  ...and on the JSON arm exactly ex.n buffered bytes, 0 streamed, 0 USB [%s]", x_reply);
        CHK(!std::strstr(x_reply, "> ") && Serial.out[0] == '\0',
            "X5  the two envelopes never cross: no `> ` in the JSON reply and no JSON on the USB sink");
        CHK((uint16_t)(ctr1 - ctr0) == 1 && (uint16_t)(ctr2 - ctr1) == 1,
            "X6  each seam call executes the command EXACTLY ONCE (peer ctr %u->%u->%u)", ctr0, ctr1, ctr2);

        // ---- X7..X10 — THE PEER-BOOK VERBS REACH THEIR ESTABLISHED HANDLERS, not a bare on_command ---------------
        const char* kPk = "peerkey 112233445566778899001122334455667788990011223344556677889900aabb";   // 64 hex = 32 B; hash = ed_pub[0..3] LE = 0x44332211
        mrprobe_nv().writes = 0;
        ex = run_text(kPk);
        CHK(ex.state == St::streamed && std::strstr(Serial.out, "\"ev\":\"peerkey_set\"")
                && mrprobe_nv().writes == 1,
            "X7  `peerkey` on the TEXT arm reaches handle_peerkey (ack + exactly one /mrpeers write) [%s]",
            Serial.out);
        // ⚠⚠ §RADMIN slice 4 — A **DIFFERENT KEY** ON THIS ARM, and the change is a FIX rather than a convenience.
        //    Until slice 4 the probe's NV fake held 512-byte payloads, so a 1160-byte `/mrpeers` record was
        //    SILENTLY DROPPED by `MrProbeNvSlot::put` while the write was still counted. Every `peerkey` therefore
        //    re-read an ABSENT store, `peer_rec_put` answered `inserted`, and X8's `writes == 1` passed WITHOUT
        //    ever exercising production's wear guard. With the medium enlarged for `/mrtargets` the record now
        //    really persists, and re-pasting the SAME key is exactly the `unchanged` case that must cost ZERO
        //    writes (`firmware_commands.cpp`'s wear guard). ⇒ the row keeps its meaning — "the JSON arm reaches
        //    handle_peerkey and makes its one write" — by pinning a genuinely NEW peer, and the coalescing itself
        //    is asserted immediately below as X8b. Registered as a probe finding, not silently absorbed.
        const char* kPk2 = "peerkey aabb33445566778899001122334455667788990011223344556677889900aabb";
        mrprobe_nv().writes = 0;
        ex = run_json(kPk2);
        CHK(ex.state == St::buffered && std::strstr(x_reply, "\"ev\":\"peerkey_set\"")
                && mrprobe_nv().writes == 1 && Serial.n_out == 0,
            "X8  ...and on the JSON arm identically, into the reply buffer [%s]", x_reply);
        // ★ THE WEAR GUARD, now that the medium is honest enough to show it: re-pasting an IDENTICAL key is
        //   `unchanged` and must write NOTHING. This is production behaviour the old fake made unobservable.
        mrprobe_nv().writes = 0;
        ex = run_json(kPk2);
        CHK(ex.state == St::buffered && std::strstr(x_reply, "\"ev\":\"peerkey_set\"")
                && mrprobe_nv().writes == 0,
            "X8b re-pasting the SAME key is `unchanged` and costs ZERO flash writes (writes=%d)",
            mrprobe_nv().writes);
        ex = run_text("peername 0x44332211 \"bob\"");
        CHK(ex.state == St::streamed && std::strstr(Serial.out, "\"ev\":\"peer_name_set\""),
            "X9  `peername` on the TEXT arm reaches handle_peername [%s]", Serial.out);
        ex = run_json("peername 0x44332211 \"bob\"");
        CHK(ex.state == St::buffered && std::strstr(x_reply, "\"ev\":\"peer_name_set\"") && Serial.n_out == 0,
            "X10 ...and on the JSON arm identically [%s]", x_reply);

        // ---- X11..X13 — reqpubkey: the BLE-ONLY EVENT and the USB-ONLY REMEDY LINE, each on its own arm ----------
        ex = run_json("reqpubkey 0x11223344");
        const bool sent_event = std::strstr(x_reply, "\"ev\":\"reqpubkey_sent\"") != nullptr;
        CHK(ex.state == St::buffered && sent_event,
            "X11 an ACCEPTED `reqpubkey` keeps its BLE-specific reqpubkey_sent event [%s]", x_reply);
        ex = run_text("reqpubkey 0x11223344");
        CHK(ex.state == St::streamed && Serial.out[0] == '>' && !std::strstr(Serial.out, "reqpubkey_sent"),
            "X12 ...while the TEXT arm keeps the plain result line and no JSON event [%s]", Serial.out);
        // A REFUSED `reqpubkey` whose remedy text the hint DOES cover: this node's OWN key_hash32 is
        // `err_unsupported` ("not a queryable peer"). ⛔ Derived from the LIVE node, never a typed constant — a
        // hardcoded hash would stop being the node's own the day the fixture changed, and the row would go vacuous.
        char own[40];
        std::snprintf(own, sizeof own, "reqpubkey 0x%08lX", (unsigned long)g_node.key_hash32());
        ex = run_text(own);
        const bool hint_usb = std::strstr(Serial.out, "> reqpubkey:") != nullptr;
        ex = run_json(own);
        CHK(hint_usb && !std::strstr(x_reply, "reqpubkey:"),
            "X13 a refused `reqpubkey` prints the USB remedy line and NEVER puts prose in the JSON ack [%s]",
            x_reply);

        // ---- X14..X16 — OWNERSHIP: empty, unknown and malformed all come back for the CALLER to render -----------
        ex = run_text("   ");
        const bool empty_text = (ex.state == St::empty && Serial.n_out == 0 && g_ble_n == 0);
        ex = run_json("   ");
        CHK(empty_text && ex.state == St::empty && ex.n == 0 && g_ble_n == 0 && Serial.n_out == 0,
            "X14 a whitespace-only line is `empty` on BOTH arms and emits NOTHING anywhere");
        // Slice 9: the real seam refuses the whole removed family like any unknown command.
        // These are the caller envelopes; S22/S23 separately pin their actual fw_main spelling.
        bool unknown_family = true;
        for (const char* line : {"zzz_unknown_verb", "rcmd 1 status", "password x", "unlock x", "lock"}) {
            ex = run_text(line);
            const bool text_refused = ex.state == St::unmatched
                && ex.parse_err == meshroute::console::ParseErr::unknown_verb
                && Serial.n_out == 0 && g_ble_n == 0;
            if (text_refused) mrcon.println(F("> parse error"));
            mrcon.service();
            const bool text_exact = std::strcmp(Serial.out, "> parse error\r\n") == 0;
            ex = run_json(line);
            const bool json_refused = ex.state == St::unmatched
                && ex.parse_err == meshroute::console::ParseErr::unknown_verb && ex.n == 0
                && x_reply[0] == '\0' && g_ble_n == 0 && Serial.n_out == 0;
            if (json_refused) meshroute::console::write_err(x_reply, sizeof x_reply, "parse", "unknown_cmd");
            const bool json_exact = std::strcmp(x_reply, "{\"err\":\"parse\",\"msg\":\"unknown_cmd\"}\n") == 0;
            unknown_family = unknown_family && text_refused && text_exact && json_refused && json_exact;
            std::printf("   retired-refusal %s: USB=%s BLE=%s\n", line,
                        text_exact ? "> parse error" : "MISMATCH", x_reply);
        }
        CHK(unknown_family,
            "X15 every unknown/removed verb is unmatched on both arms; exact USB/BLE caller envelopes");
        ex = run_json("send 5 unquoted");
        CHK(ex.state == St::unmatched && ex.parse_err == meshroute::console::ParseErr::bad_args
                && ex.n == 0 && x_reply[0] == '\0',
            "X16 a MALFORMED parser-owned line is `unmatched`+bad_args — the named envelope stays the caller's");

        // ---- X17 — THE SUPPLIED SINK IS THE ONLY SINK. Handing the text arm a sink that is NOT `mrcon` must put
        //            every byte there and NONE on the global console ([[B279]]'s rule, applied to the whole path).
        {
            mrcon.service(); Serial.reset(); ble_reset();
            g_sink.reset();
            const mrfw::LineExec e2 = mrfw::exec_console_line("whoami", 6, mrfw::LineFormat::text, g_sink,
                                                              nullptr, 0, x_usb);
            mrcon.service();
            CHK(e2.state == St::streamed && g_sink.has("[whoami]") && Serial.n_out == 0 && g_ble_n == 0,
                "X17 the ROUTER arm writes ONLY to the Print& it is handed — a third sink receives it, `mrcon` "
                "gets 0 B [%u]", unsigned(Serial.n_out));
            // ⛔ AND THE SAME QUESTION FOR THE OTHER HALF, because the router arm cannot answer it: `dispatch()`
            //    receives `stream` as an ARGUMENT, so a seam that re-chose `mrcon` for its OWN writes would leave
            //    the router path perfectly correct and only corrupt the text rendering. Measured, not assumed:
            //    without this row the [[B279]]-shaped control (C18) stayed GREEN.
            mrcon.service(); Serial.reset(); ble_reset();
            g_sink.reset();
            const mrfw::LineExec e2b = mrfw::exec_console_line("send 5 \"hi\"", 11, mrfw::LineFormat::text,
                                                               g_sink, nullptr, 0, x_usb);
            mrcon.service();
            CHK(e2b.state == St::streamed && g_sink.has("> ") && Serial.n_out == 0 && g_ble_n == 0,
                "X18 ...and so does the TEXT RENDERING itself — the `> …` result line lands on the supplied sink, "
                "`mrcon` gets 0 B [%u]", unsigned(Serial.n_out));
        }

        // ---- X19 — THE BORROWED BODY DOES NOT OUTLIVE THE CALL. `Command::body` points into the caller's line;
        //            scribbling that line after the seam returns must not change a byte of the rendered reply.
        {
            std::snprintf(x_line, sizeof x_line, "send 5 \"ABCDEFGH\"");
            mrcon.service(); Serial.reset(); ble_reset(); std::memset(x_reply, 0, sizeof x_reply);
            LineSink ls(ble_capture);
            const mrfw::LineExec e3 = mrfw::exec_console_line(x_line, std::strlen(x_line), mrfw::LineFormat::json,
                                                              ls, x_reply, sizeof x_reply, x_ble);
            char snapshot[256];
            std::memcpy(snapshot, x_reply, sizeof snapshot);
            std::memset(x_line, 'Z', sizeof x_line);            // the caller's buffer is reused, as it is in production
            CHK(e3.state == St::buffered && std::memcmp(snapshot, x_reply, sizeof snapshot) == 0
                    && !std::strchr(x_reply, 'Z'),
                "X19 the rendered reply is a COPY: overwriting the input line after the call changes nothing");
        }

        // ---- X20 — `help` REALLY REACHES THE ROUTER THROUGH THE SEAM. This is what makes the BLE adapter's
        //            pre-seam `console_only` refusal load-bearing rather than decorative: without it the seam's
        //            router-first order would stream the whole index over NUS.
        ex = run_json("help");
        CHK(ex.state == St::streamed && ex.n == 0 && g_ble_n > 200
                && std::strstr(g_ble, "docs/manual/command-reference.md"),
            "X20 `help` reaching the seam DOES stream the whole index (%u B) — which is why BLE refuses it first",
            unsigned(g_ble_n));

        // ---- X21..X26 — ★★ W1c (design §4.6 D10): `whoami` PRINTS THE STORED NAME EXACTLY. An UNNAMED node prints
        //      `name=""` — never a made-up default and never an omitted field. Each row compares the WHOLE identity
        //      line, byte for byte, with one built from `g_node`'s own accessors (so id/hash/leaf/gw/gwonly/mobile are
        //      pinned as well), on BOTH transports. ⓘ The fake `Print` renders `HEX` as DECIMAL
        //      (tools/probe_board_ui/fakes/Arduino.h), so the expected `hash=0x…` digits follow the fake, exactly as the
        //      production call reaches it. The line each row checked is printed (CR/LF stripped) for the
        //      tools/lab/parsers.py::parse_whoami compatibility check. ⛔ Placed LAST in the block, and the fixture's
        //      name is restored afterwards, so no earlier row moves or changes state.
        {
            char saved[32];
            const uint8_t saved_n = g_node.effective_name(saved, sizeof saved);
            static char want[192], seen[192];
            auto expect_line = [&](const char* nm, uint8_t nn) {
                const meshroute::NodeConfig& c = g_node.config();
                std::snprintf(want, sizeof want, "[whoami] id=%u hash=0x%lu name=\"%.*s\" leaf=%u gw=%d gwonly=%d mobile=%d\r\n",
                              unsigned(g_node.node_id()), static_cast<unsigned long>(g_node.key_hash32()), int(nn), nm,
                              unsigned(c.leaf_id), c.is_gateway ? 1 : 0, c.gateway_only ? 1 : 0, c.is_mobile ? 1 : 0);
            };
            auto shown = [&](const char* src) {                       // the checked line, without its CR/LF
                size_t i = 0;
                for (; src[i] && src[i] != '\r' && src[i] != '\n' && i + 1 < sizeof seen; ++i) seen[i] = src[i];
                seen[i] = '\0';
            };
            const char n32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";   // a MAXIMUM (32-byte) name

            g_node.set_name("", 0);                                   // UNNAMED, through the public setter
            expect_line("", 0);
            ex = run_text("whoami"); shown(Serial.out);
            CHK(ex.state == St::streamed && std::strcmp(Serial.out, want) == 0 && g_ble_n == 0,
                "X21 an UNNAMED node's `whoami` on the TEXT arm is the exact identity line with name=\"\" [%s]", seen);
            ex = run_json("whoami"); shown(g_ble);
            CHK(ex.state == St::streamed && ex.n == 0 && std::strcmp(g_ble, want) == 0 && Serial.n_out == 0,
                "X22 ...and on the JSON arm, byte-identical through the REAL LineSink [%s]", seen);

            g_node.set_name("Bench 1", 7);
            expect_line("Bench 1", 7);
            ex = run_text("whoami"); shown(Serial.out);
            CHK(ex.state == St::streamed && std::strcmp(Serial.out, want) == 0 && g_ble_n == 0,
                "X23 a NAMED node prints name=\"Bench 1\" on the TEXT arm, the rest of the line intact [%s]", seen);
            ex = run_json("whoami"); shown(g_ble);
            CHK(ex.state == St::streamed && ex.n == 0 && std::strcmp(g_ble, want) == 0 && Serial.n_out == 0,
                "X24 ...and on the JSON arm, byte-identical [%s]", seen);

            g_node.set_name(n32, 32);
            expect_line(n32, 32);
            ex = run_text("whoami"); shown(Serial.out);
            CHK(ex.state == St::streamed && std::strcmp(Serial.out, want) == 0 && g_ble_n == 0,
                "X25 a MAXIMUM 32-byte name is printed whole between the quotes on the TEXT arm [%s]", seen);
            ex = run_json("whoami"); shown(g_ble);
            CHK(ex.state == St::streamed && ex.n == 0 && std::strcmp(g_ble, want) == 0 && Serial.n_out == 0,
                "X26 ...and on the JSON arm, byte-identical [%s]", seen);

            g_node.set_name(saved, saved_n);                          // restore the fixture's name
        }
    }


    // ================================================================================================
    // [[B321]] — THE SHARED HEX DECODER'S SECRET SCRATCH IS WIPED, OBSERVED ON THE REAL CALL.
    // ⓘ ON BOTH ARMS: `mrfw::parse_hex32` is the ONE path a 64-hex TEAM KEY or a `/mrmkeys` MASTER SEED takes into
    //   the firmware, and it is capability-free — so the property is measured on the product profile that imports
    //   seeds AND on the one that does not.
    // ================================================================================================
    {
        static const char kSecretHex[] =
            "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";
        uint8_t decoded[32] = {};
        wipe_watch_begin();
        const bool ok_hex = mrfw::parse_hex32(kSecretHex, decoded);
        wipe_watch_end();
        CHK(ok_hex, "Z1  the shipped decoder still ACCEPTS a well-formed 64-hex token (the grammar did not move)");
        // ★ EXACTLY ONE 32-byte wipe, and its CONTENT was the decoded secret — i.e. the buffer wiped is the
        //   SCRATCH that held the key material, not some unrelated 32 bytes.
        int n32 = 0, matched = 0, zeroed = 0;
        for (int i = 0; i < g_nwipes; ++i) {
            if (g_wipes[i].n != 32) continue;
            ++n32;
            if (std::memcmp(g_wipes[i].before, decoded, 32) == 0) ++matched;
            if (g_wipes[i].after_zero) ++zeroed;
        }
        CHK(n32 == 1, "Z2  the decode performs EXACTLY ONE 32-byte crypto_wipe (saw %d)", n32);
        CHK(matched == 1,
            "Z3  ...over the SCRATCH that held the decoded key material (content matched %d)", matched);
        CHK(zeroed == 1, "Z4  ...and the storage READ BACK ALL-ZERO while the frame was still alive (%d)", zeroed);

        // ⛔ A REFUSAL AFTER ALLOCATION WIPES TOO — the path a hand-written per-return list forgets first.
        static const char kBadHex[] =
            "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8fZZ";
        uint8_t junk[32] = {};
        wipe_watch_begin();
        const bool ok_bad = mrfw::parse_hex32(kBadHex, junk);
        wipe_watch_end();
        int bad32 = 0, bad_zero = 0;
        for (int i = 0; i < g_nwipes; ++i)
            if (g_wipes[i].n == 32) { ++bad32; if (g_wipes[i].after_zero) ++bad_zero; }
        CHK(!ok_bad, "Z5  a malformed token is still REFUSED (the output-on-failure did not move)");
        CHK(bad32 == 1 && bad_zero == 1,
            "Z6  ...and its scratch is wiped ON THE REFUSAL PATH too (%d wipe(s), %d zeroed)", bad32, bad_zero);

        // ⛔ A NULL INPUT ALLOCATES NO SCRATCH, so there is nothing to wipe and nothing is wiped.
        wipe_watch_begin();
        const bool ok_null = mrfw::parse_hex32(nullptr, junk);
        wipe_watch_end();
        int null32 = 0;
        for (int i = 0; i < g_nwipes; ++i) if (g_wipes[i].n == 32) ++null32;
        CHK(!ok_null && null32 == 0,
            "Z7  a NULL input allocates no scratch and performs NO wipe (%d)", null32);

        // ⓘ THE CONTROL OF THE OBSERVATION: the interposer must actually be in the link, or Z2..Z6 would read 0
        //   wipes and "0 == 1" would be the only thing failing. This row proves the wrapper SEES the real call.
        uint8_t live[32];
        std::memset(live, 0xC7, sizeof live);
        wipe_watch_begin();
        crypto_wipe(live, sizeof live);
        wipe_watch_end();
        CHK(g_nwipes == 1 && g_wipes[0].p == live && g_wipes[0].n == 32
            && g_wipes[0].before[0] == 0xC7 && g_wipes[0].after_zero,
            "Z8  the interposer is LINKED and observes the REAL crypto_wipe (n=%d)", g_nwipes);
    }

    // ★★ §RADMIN slice 4: THE BLOCK BELOW IS **ACCEPT-ARM ONLY**, and the gate is the PRODUCT's own macro rather
    //    than a probe switch: on the CLIENT arm `src/firmware_commands.cpp` compiles no target-store binding at
    //    all, so `acl`/`admin-id` are not router verbs there and every row below would be measuring a surface the
    //    product does not have. The CLIENT arm's own rows follow, and the ⛔ ABSENCE of this family on that arm is
    //    asserted there rather than left implicit.
#if MR_FEAT_RADMIN_ACCEPT
    // ================================================================================================
    // §RADMIN SLICE 3 — THE TWO TARGET STORES, THROUGH THE REAL ROUTER AND THE REAL NV WRAPPERS.
    //
    // ⛔⛔ WHY THESE ROWS EXIST, stated as the defect they close: the services, the grammar and every emitted
    //     byte are pinned by `test/test_firmware_admin_{identity,acl,verbs}.cpp` — but every one of those gates
    //     stays GREEN if the `dispatch()` arm is deleted, if the Print adapter writes to `mrcon` instead of the
    //     supplied sink, if the store binding is pointed at the wrong slot, or if the entropy binding returns an
    //     unconditional `true`. None of that is reachable from a host build of `test/`. THESE rows drive the REAL
    //     `mrfw::dispatch()` over the REAL `src/firmware_commands.cpp` against the byte-counted fake NV medium.
    //
    // ⛔ THIS IS A STATIC **ACCEPT** HOST PROFILE (`-DARDUINO -DBOARD_HELTEC_V3` ⇒ `MR_FEAT_RADMIN_ACCEPT=1`).
    //    It is ⛔ NOT execution of gateway hardware, ⛔ NOT a real BLE transport, and ⛔ NOT a flash test: the NV
    //    medium is a fake, so no wear, no power cut and no real filesystem is exercised. Bench Part 55a owns those.
    // ================================================================================================
    {
        auto& nv  = mrprobe_nv();
        auto& rng = mrprobe_rng();
        auto reset_nv = [&](bool writable) {
            nv = MrProbeNv{};
            nv.ns_present = writable;
            nv.rw_ok      = writable;
            rng = MrProbeRng{};
        };
        // A key the probe can paste, and the SAME 64 hex characters the assertions compare back out of the line.
        const char* kKeyA = "1111111111111111111111111111111111111111111111111111111111111111";
        const char* kKeyB = "2222222222222222222222222222222222222222222222222222222222222222";
        const char* kKeyC = "4444444444444444444444444444444444444444444444444444444444444444";   // §radmin-5: a THIRD distinct controller, for the refusal rows

        // ---- R30: the family REACHES the router at all, on both primary tokens -----------------------------
        reset_nv(true);
        CaptureSink s;
        CHK(mrfw::dispatch("admin-id show", 13, s), "R30 `admin-id show` is OWNED by the real dispatch()");
        s.reset();
        CHK(mrfw::dispatch("acl list", 8, s), "R30b `acl list` is OWNED by the real dispatch()");
        // ⛔ …and the near misses are NOT, so the router's boundary is the pure predicate's and not a prefix.
        s.reset();
        CHK(!mrfw::dispatch("admin-key show self", 19, s) && s.n == 0,
            "R30c `admin-key show self` (Slice 4's CONTROLLER verb) is NOT owned — zero bytes emitted");
        s.reset();
        CHK(!mrfw::dispatch("aclx", 4, s) && s.n == 0, "R30d `aclx` is NOT owned — zero bytes emitted");

        // ---- R31: an ABSENT store answers honestly and writes NOTHING --------------------------------------
        reset_nv(true);
        s.reset(); mrfw::dispatch("admin-id show", 13, s);
        CHK(s.is("> admin-id err absent\n") && nv.writes == 0 && rng.draws == 0,
            "R31 an absent /mradmid answers `absent` with 0 writes and 0 draws [w=%d d=%u]",
            nv.writes, unsigned(rng.draws));
        s.reset(); mrfw::dispatch("acl list", 8, s);
        CHK(s.is("> acl end count=0 owners=0 operators=0\n") && nv.writes == 0,
            "R31b an absent /mracl lists as EMPTY with 0 writes [%s]", s.buf);

        // ---- R32: generate writes EXACTLY ONE record, to the RIGHT namespace and key ------------------------
        reset_nv(true);
        s.reset(); mrfw::dispatch("admin-id generate", 17, s);
        const bool gen_ok = s.has("> admin-id generated fp=") && s.has(" pub=");
        CHK(gen_ok && nv.writes == 1, "R32 `admin-id generate` costs EXACTLY ONE durable write [w=%d] %s",
            nv.writes, gen_ok ? "" : s.buf);
        CHK(nv.find("mr", "admid") != nullptr && nv.find("mr", "admid")->len == sizeof(mrnv::AdminIdBlob),
            "R32b ...into namespace `mr`, key `admid`, exactly sizeof(AdminIdBlob)=%u bytes",
            unsigned(sizeof(mrnv::AdminIdBlob)));
        // ⛔ AND NO OTHER SLOT MOVED. `/mrid`, `/mrcfg`, `/mrpeers` and `/mracl` are untouched by a root mint.
        CHK(nv.find("mr", "id") == nullptr && nv.find("mr", "cfg") == nullptr
                && nv.find("mr", "peers") == nullptr && nv.find("mr", "acl") == nullptr,
            "R32c ...and NO unrelated slot was written (/mrid, /mrcfg, /mrpeers, /mracl all absent)");
        {
            mrnv::AdminIdBlob stored{};
            const mrnv::AdminIdRead st = mrnv::load_admin_id(stored);
            CHK(st == mrnv::AdminIdRead::ok && stored.magic == mrnv::kAdminIdMagic
                    && stored.version == mrnv::kAdminIdVersion && stored.reserved == 0
                    && !mrfw::admin_buf_all_zero(stored.seed, 32),
                "R32d ...and the stored record is a VALID v1 'MRA1' header with a NON-ZERO seed");
        }
        // The reload reproduces the SAME public key and fingerprint — the record really is the identity.
        {
            // ⛔ A BOUNDED COPY, ⛔ not snprintf("%s"): the sink is 4096 B and `-Werror=format-truncation`
            //    (correctly) refuses a format that could truncate. The `admin-id generated` line is ~110 B.
            char first[256] = {};
            std::strncpy(first, s.buf, sizeof first - 1);
            s.reset(); mrfw::dispatch("admin-id show", 13, s);
            const char* fp1 = std::strstr(first, " fp=");
            const char* fp2 = std::strstr(s.buf, " fp=");
            CHK(fp1 && fp2 && std::strcmp(fp1, fp2) == 0 && s.has("> admin-id ok fp="),
                "R32e a reload reproduces the SAME fingerprint and public key");
        }
        // ★★ THE BINDING REALLY ASKS THE PLATFORM. `mrrng::fill` draws 32 bytes as 8 x 32-bit `esp_random()`
        //    calls on this arm, so a binding that stopped drawing — or that answered from a cached/derived value —
        //    shows up as a DRAW COUNT, which no pure test can see (the binding lives in firmware_commands.cpp).
        // ⛔⛔ THE COUNT MOVED 8 -> 28 IN §RADMIN SLICE 5, AND THE DERIVATION IS EXACT rather than accommodated:
        //    a `generate` now PREPARES the live administration state before its durable save, and that
        //    preparation mints the TEN per-slot session epochs (design §4.1's complete candidate). Each epoch is
        //    64 bits, i.e. 8 B / 4 = 2 platform draws. ⇒ 8 (the 32-byte seed) + 10 x 2 (the epochs) = 28.
        //    ★ AND THE MOVE IS ITSELF THE EVIDENCE: it is the only place the ten draws are observable at all —
        //      the native suite drives a FAKE runtime, so a binding that stopped preparing would be green there.
        CHK(rng.draws == 28,
            "R32g the seed AND the ten prepared epochs came from EXACTLY 28 platform draws (32/4 + 10 x 8/4) [%u]",
            unsigned(rng.draws));
        // ★★★ §RADMIN SLICE 5 — THE LIVE INSTALL REALLY HAPPENED, observed on the REAL `g_node` rather than on a
        //     callback count: a successful `admin-id generate` leaves the running administration pair installed.
        //     ⓘ Readiness stays `disabled` here BY DESIGN — the conjunction also needs a valid ACL with an owner,
        //       and this fixture has none yet. That is asserted, so "installed" cannot be read as "accepting".
        CHK(g_node.admin_session_state().root_present == 1,
            "R32h ★ §radmin-5: the generated pair is INSTALLED into the running Node (root_present=1)");
        CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::disabled
            && g_node.admin_session_slots() == 0,
            "R32i ...and readiness is still `disabled` with 0 slots — a root alone is not an accepting node");
        // ⛔ NO SEED BYTE ESCAPES: the stored seed's hex must appear NOWHERE in the console output.
        {
            mrnv::AdminIdBlob stored{};
            (void)mrnv::load_admin_id(stored);
            char seed_hex[65];
            mrfw::admin_key_hex(stored.seed, seed_hex);
            CHK(!s.has(seed_hex), "R32f ⛔ the SEED never appears in any console line");
        }

        // ---- R33: `generate` on an EXISTING identity refuses with ZERO writes and ZERO draws ---------------
        {
            const int w0 = nv.writes; const uint32_t d0 = rng.draws;
            s.reset(); mrfw::dispatch("admin-id generate", 17, s);
            CHK(s.is("> admin-id err already_present\n") && nv.writes == w0 && rng.draws == d0,
                "R33 a second `generate` refuses `already_present` — 0 writes, 0 draws");
        }
        // ---- R34: the CONFIRM gate, on the real router: zero writes AND zero draws --------------------------
        {
            const int w0 = nv.writes; const uint32_t d0 = rng.draws;
            for (const char* line : { "admin-id rotate", "admin-id rotate confirmm", "admin-id rotate confirm x",
                                      "admin-id reset", "admin-id reset yes" }) {
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                CHK(s.is("> admin-id err bad_args\n"), "R34 `%s` -> bad_args", line);
            }
            CHK(nv.writes == w0 && rng.draws == d0,
                "R34b ...and the whole confirm-refusal set cost 0 writes and 0 entropy draws");
        }

        // ---- R35: the FIRST-OWNER ceremony, end to end, on the real router ---------------------------------
        {
            const int w0 = nv.writes;
            char line[128];
            std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
            s.reset(); mrfw::dispatch(line, std::strlen(line), s);
            CHK(s.has("> acl added slot=0 role=owner fp=") && s.has(kKeyA) && nv.writes == w0 + 1,
                "R35 `acl add owner <key>` seeds an ABSENT /mracl and adds the row in ONE write [%s]", s.buf);
            CHK(nv.find("mr", "acl") != nullptr && nv.find("mr", "acl")->len == sizeof(mrnv::AclBlob),
                "R35b ...into namespace `mr`, key `acl`, exactly sizeof(AclBlob)=%u bytes",
                unsigned(sizeof(mrnv::AclBlob)));
            // A SECOND owner, then a demotion, then a removal — the design's rotation, through the router.
            std::snprintf(line, sizeof line, "acl add owner %s", kKeyB);
            s.reset(); mrfw::dispatch(line, std::strlen(line), s);
            CHK(s.has("> acl added slot=1 role=owner"), "R35c a second owner lands in slot 1 [%s]", s.buf);
            s.reset(); mrfw::dispatch("acl set 1 operator", 18, s);
            CHK(s.is("> acl updated slot=1 role=operator\n"), "R35d `acl set 1 operator` updates [%s]", s.buf);
            const int w1 = nv.writes;
            s.reset(); mrfw::dispatch("acl set 1 operator", 18, s);
            CHK(s.is("> acl unchanged slot=1 role=operator\n") && nv.writes == w1,
                "R35e ...and repeating it costs ⛔ ZERO writes and says `unchanged`");
            s.reset(); mrfw::dispatch("acl remove 0 confirm", 20, s);
            CHK(s.is("> acl err last_owner\n") && nv.writes == w1,
                "R35f ⛔ the LAST OWNER cannot be removed — 0 writes [%s]", s.buf);
            s.reset(); mrfw::dispatch("acl list", 8, s);
            CHK(s.has("> acl slot=0 role=owner") && s.has("> acl slot=1 role=operator")
                    && s.has("> acl end count=2 owners=1 operators=1\n"),
                "R35g `acl list` renders both rows and the end line [%s]", s.buf);
            s.reset(); mrfw::dispatch("acl remove 1 confirm", 20, s);
            CHK(s.is("> acl removed slot=1\n"), "R35h an operator IS removable [%s]", s.buf);
        }

        // ---- R36: the `add` gate on the administration identity, over the real store ------------------------
        {
            reset_nv(true);                                   // no /mradmid at all
            char line[128];
            std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
            s.reset(); mrfw::dispatch(line, std::strlen(line), s);
            CHK(s.is("> acl err identity_absent\n") && nv.writes == 0,
                "R36 a first owner cannot be granted without a valid root — 0 writes [%s]", s.buf);
        }

        // ---- R37: ★ THE DEAD RNG. The REAL binding must refuse, and it must cost no write -------------------
        {
            reset_nv(true);
            rng.force_zero = true;
            s.reset(); mrfw::dispatch("admin-id generate", 17, s);
            CHK(s.is("> admin-id err entropy_failed\n") && nv.writes == 0 && rng.draws > 0,
                "R37 ★ an all-zero platform draw REFUSES (`entropy_failed`) — it ASKED (%u draws) and wrote "
                "NOTHING [%s]", unsigned(rng.draws), s.buf);
            CHK(nv.find("mr", "admid") == nullptr,
                "R37b ...and ⛔ no record was minted from the dead source");
            rng.force_zero = false;
        }

        // ---- R38: a FAILED durable write publishes no success ----------------------------------------------
        {
            reset_nv(true);
            nv.fail_write = true;
            s.reset(); mrfw::dispatch("admin-id generate", 17, s);
            CHK(s.is("> admin-id err nv_save_failed\n") && nv.writes == 1,
                "R38 a refused medium answers `nv_save_failed` after EXACTLY ONE attempt [%s]", s.buf);
            nv.fail_write = false;
        }

        // ---- R39: a CORRUPT record is invalid, and only the confirm-gated recovery may touch it ------------
        {
            reset_nv(true);
            mrnv::AclBlob bad{};
            mrnv::acl_blob_init(bad);
            bad.version = 99;                                  // a version the equality policy REJECTS
            nv.put("mr", "acl", reinterpret_cast<const unsigned char*>(&bad), sizeof bad);
            s.reset(); mrfw::dispatch("acl list", 8, s);
            CHK(s.is("> acl err store_invalid\n"), "R39 a wrong-version /mracl reads as `store_invalid` [%s]",
                s.buf);
            const int w0 = nv.writes;
            char line[128];
            std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
            s.reset(); mrfw::dispatch(line, std::strlen(line), s);
            CHK(nv.writes == w0, "R39b ⛔ an ordinary `acl add` over a corrupt record costs ZERO writes");
            s.reset(); mrfw::dispatch("acl reset confirm", 17, s);
            CHK(s.is("> acl recovered count=0 owners=0 operators=0\n") && nv.writes == w0 + 1,
                "R39c `acl reset confirm` recovers it in ONE write [%s]", s.buf);
        }

        // ---- R40: the BOOT WRAPPER on the SAME platform facts — read-only, two lines --------------------
        {
            reset_nv(true);
            mrcon.service(); Serial.reset();
            const int w0 = nv.writes; const uint32_t d0 = rng.draws;
            mrfw::admin_stores_boot_report_console();
            mrcon.service();
            CHK(nv.writes == w0 && rng.draws == d0,
                "R40 the boot report writes NOTHING and draws NOTHING [w=%d d=%u]",
                nv.writes - w0, unsigned(rng.draws - d0));
            CHK(std::strstr(Serial.out, "> admin-id boot state=absent") != nullptr
                && std::strstr(Serial.out, "> acl boot state=absent count=0") != nullptr,
                "R40b ...and reports BOTH stores' states on the console");
            CHK(!std::strstr(Serial.out, "pub=") && !std::strstr(Serial.out, "fp="),
                "R40c ...with ⛔ no key byte and no fingerprint on any boot line");
        }

        // ---- R42: ★★ THE `io_failed` ARM, EXECUTED ON THE REAL ESP32 READ SEQUENCE ------------------------
        //   The store will not open AND the namespace is NOT merely unwritten, so `PreferencesSlot::ns_absent`
        //   answers false, `nvs_read_slot` sets `SlotIo::backend_failed`, and the typed wrapper must classify
        //   `io_failed` — a fact about the DEVICE, over which ⛔ NOTHING may be written, not even recovery.
        // ⛔ THIS IS WHAT MAKES THE `&io` ARGUMENT MEASURED RATHER THAN ASSERTED: a wrapper that stopped asking
        //   the primitive for `SlotIo` would report a dead store as a FRESH DEVICE, and every row below flips.
        {
            reset_nv(true);
            nv.ns_present = false;                       // a read-only begin() fails …
            mrprobe_nvs().backend_dead = true;           // … and NOT because the namespace was never written
            const int w0 = nv.writes;
            {
                mrnv::AdminIdBlob ab{};
                mrnv::AclBlob cb{};
                CHK(mrnv::load_admin_id(ab) == mrnv::AdminIdRead::io_failed,
                    "R42 a dead NVS makes /mradmid `io_failed` — ⛔ never the fresh-device `absent`");
                CHK(mrnv::load_acl(cb) == mrnv::AclRead::io_failed,
                    "R42b ...and /mracl likewise");
            }
            s.reset(); mrfw::dispatch("admin-id show", 13, s);
            CHK(s.is("> admin-id err store_io_failed\n"), "R42c the console names it `store_io_failed` [%s]", s.buf);
            s.reset(); mrfw::dispatch("acl list", 8, s);
            CHK(s.is("> acl err store_io_failed\n"), "R42d ...on the ACL family too [%s]", s.buf);
            s.reset(); mrfw::dispatch("admin-id reset confirm", 22, s);
            CHK(s.is("> admin-id err store_io_failed\n") && nv.writes == w0,
                "R42e ⛔⛔ EVEN THE CONFIRM-GATED RECOVERY REFUSES over an unreadable store — 0 writes [%s]", s.buf);
            s.reset(); mrfw::dispatch("acl reset confirm", 17, s);
            CHK(s.is("> acl err store_io_failed\n") && nv.writes == w0,
                "R42f ...and so does the ACL recovery [%s]", s.buf);
            mrcon.service(); Serial.reset();
            mrfw::admin_stores_boot_report_console();
            mrcon.service();
            CHK(std::strstr(Serial.out, "state=io_failed") != nullptr && nv.writes == w0,
                "R42g the BOOT report says `io_failed` and still writes nothing");
            mrprobe_nvs().backend_dead = false;
        }

        // ================================================================================================
        // ---- §RADMIN SLICE 5 — THE LIVE-INSTALL WIRING, ON THE REAL `g_node` AND THE REAL PLATFORM FACTS.
        //
        // ⛔⛔ WHY THESE ROWS EXIST, as the defect they close: `test/test_firmware_admin_runtime.cpp` pins the
        //     prepare/commit/discard ORDER against a FAKE runtime, and every one of its cases stays green if
        //     `DeviceAdminRuntime` is never constructed, if the services are handed `nullptr` instead of the
        //     seam, if `commit()` forwards to nothing, if the boot install is never called, or if the boot line
        //     is printed without installing anything. None of that is reachable from a host build of `test/`,
        //     because `platformio.ini`'s native env compiles no `src/*.cpp` at all (§B115).
        // ⛔ AND IT IS STILL NOT METAL: the NV medium and the RNG are fakes, so no flash wear, no power cut and
        //    no real entropy source is exercised. [[B312]] and [[B317]] are untouched by every row below.
        // ================================================================================================
        {
            // ---- S5-1: BOOT INSTALL — a valid root + a valid ACL with an owner installs and reports `ready`
            reset_nv(true);
            {
                char line[128];
                s.reset(); mrfw::dispatch("admin-id generate", 17, s);
                std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                CHK(s.has("> acl added slot=0 role=owner"),
                    "S5-1a a root and a first OWNER are provisioned [%s]", s.buf);
            }
            const uint64_t e0_after_add = g_node.admin_session_state().epoch[0];
            CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::ready
                && g_node.admin_session_slots() == 1 && e0_after_add != 0,
                "S5-1b ★ the ACL add ACTIVATED the running session: ready, 1 slot, a NON-ZERO epoch");
            {
                mrcon.service(); Serial.reset();
                const int w0 = nv.writes;
                mrfw::admin_stores_boot_report_console();
                mrcon.service();
                CHK(std::strstr(Serial.out, "> admin-session boot state=ready slots=1") != nullptr,
                    "S5-1c ★ the BOOT line reports the installed state exactly [%s]", Serial.out);
                CHK(nv.writes == w0, "S5-1d ...and the boot install writes NOTHING");
                CHK(!std::strstr(Serial.out, "pub=") && !std::strstr(Serial.out, "fp="),
                    "S5-1e ...with ⛔ no key byte, fingerprint or epoch on the session line");
                CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::ready
                    && g_node.admin_session_slots() == 1,
                    "S5-1f ...and the node is STILL ready afterwards (a re-read installs the same image)");
            }
            // ---- S5-2: SLOT-LOCAL vs ROOT-WIDE invalidation, measured on the REAL epochs -------------------
            // ⓘ THE BASELINE IS RE-READ HERE, AFTER the boot install above, and that is a FACT rather than a
            //   convenience: a boot install mints a COMPLETE fresh candidate (§4.1), so the epochs it published
            //   are not the ones the earlier `acl add` did. Comparing across it would measure the boot, not the
            //   per-slot rule this block is about.
            const uint64_t e0_base = g_node.admin_session_state().epoch[0];
            {
                char line[128];
                std::snprintf(line, sizeof line, "acl add operator %s", kKeyB);
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                CHK(s.has("> acl added slot=1 role=operator"),
                    "S5-2a a second controller is granted [%s]", s.buf);
            }
            const uint64_t e0 = g_node.admin_session_state().epoch[0];
            const uint64_t e1 = g_node.admin_session_state().epoch[1];
            CHK(e0 == e0_base && e1 != 0 && e1 != e0,
                "S5-2b ★ the ADD minted a fresh epoch for the NEW slot and left slot 0's EXACTLY as it was");
            {
                s.reset(); mrfw::dispatch("acl set 1 owner", 15, s);
                CHK(s.has("> acl updated slot=1 role=owner"),
                    "S5-2c a role change on slot 1 succeeds [%s]", s.buf);
            }
            CHK(g_node.admin_session_state().epoch[0] == e0
                && g_node.admin_session_state().epoch[1] != e1,
                "S5-2d ★★ SLOT-LOCAL invalidation: slot 1's epoch rotated, slot 0's did NOT");
            {
                const uint64_t a0 = g_node.admin_session_state().epoch[0];
                const uint64_t a1 = g_node.admin_session_state().epoch[1];
                s.reset(); mrfw::dispatch("admin-id rotate confirm", 23, s);
                CHK(s.has("> admin-id rotated"), "S5-2e the administration ROOT rotates [%s]", s.buf);
                CHK(g_node.admin_session_state().epoch[0] != a0
                    && g_node.admin_session_state().epoch[1] != a1
                    && g_node.admin_session_state().acl_occupied == 2,
                    "S5-2f ★★ ROOT-WIDE invalidation: BOTH epochs rotated and the ACL SURVIVED");
            }
            // ---- S5-3: PREPARE FAILS BEFORE THE WRITE — a dead RNG costs zero durable writes ---------------
            {
                const int w0 = nv.writes;
                const uint64_t keep0 = g_node.admin_session_state().epoch[0];
                rng.force_zero = true;
                char line[128];
                std::snprintf(line, sizeof line, "acl add operator %s", kKeyC);
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                rng.force_zero = false;
                CHK(s.is("> acl err runtime_unavailable\n"),
                    "S5-3a ★★ a refused epoch draw answers `runtime_unavailable` [%s]", s.buf);
                CHK(nv.writes == w0, "S5-3b ...at ZERO durable writes — the preparation ran BEFORE the save");
                CHK(g_node.admin_session_state().acl_occupied == 2
                    && g_node.admin_session_state().epoch[0] == keep0,
                    "S5-3c ...and the RUNNING image is untouched (2 slots, slot 0's epoch exact)");
            }
            // ---- S5-4: NV FAILS AFTER A SUCCESSFUL PREPARE — nothing is installed --------------------------
            {
                const uint64_t keep0 = g_node.admin_session_state().epoch[0];
                const uint8_t  occ0  = g_node.admin_session_state().acl_occupied;
                nv.fail_write = true;
                char line[128];
                std::snprintf(line, sizeof line, "acl add operator %s", kKeyC);
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                nv.fail_write = false;
                CHK(s.is("> acl err nv_save_failed\n"),
                    "S5-4a a refused medium answers `nv_save_failed` [%s]", s.buf);
                CHK(g_node.admin_session_state().acl_occupied == occ0
                    && g_node.admin_session_state().epoch[0] == keep0,
                    "S5-4b ★★ the plan was DISCARDED: the running ACL, epochs and pair are EXACTLY as before");
                // ⚠ [[B317]]: this is a claim about the RUNNING state. It is ⛔ NOT a claim that the previous
                //   flash bytes survived a remove-then-write backend.
            }
            // ---- S5-5: ORDINARY `regen` does NOT rotate the administration root ---------------------------
            {
                const uint8_t before_pub[32] = {
                    g_node.admin_session_state().admin_ed_pub[0],  g_node.admin_session_state().admin_ed_pub[1],
                    g_node.admin_session_state().admin_ed_pub[2],  g_node.admin_session_state().admin_ed_pub[3],
                    g_node.admin_session_state().admin_ed_pub[4],  g_node.admin_session_state().admin_ed_pub[5],
                    g_node.admin_session_state().admin_ed_pub[6],  g_node.admin_session_state().admin_ed_pub[7],
                    g_node.admin_session_state().admin_ed_pub[8],  g_node.admin_session_state().admin_ed_pub[9],
                    g_node.admin_session_state().admin_ed_pub[10], g_node.admin_session_state().admin_ed_pub[11],
                    g_node.admin_session_state().admin_ed_pub[12], g_node.admin_session_state().admin_ed_pub[13],
                    g_node.admin_session_state().admin_ed_pub[14], g_node.admin_session_state().admin_ed_pub[15],
                    g_node.admin_session_state().admin_ed_pub[16], g_node.admin_session_state().admin_ed_pub[17],
                    g_node.admin_session_state().admin_ed_pub[18], g_node.admin_session_state().admin_ed_pub[19],
                    g_node.admin_session_state().admin_ed_pub[20], g_node.admin_session_state().admin_ed_pub[21],
                    g_node.admin_session_state().admin_ed_pub[22], g_node.admin_session_state().admin_ed_pub[23],
                    g_node.admin_session_state().admin_ed_pub[24], g_node.admin_session_state().admin_ed_pub[25],
                    g_node.admin_session_state().admin_ed_pub[26], g_node.admin_session_state().admin_ed_pub[27],
                    g_node.admin_session_state().admin_ed_pub[28], g_node.admin_session_state().admin_ed_pub[29],
                    g_node.admin_session_state().admin_ed_pub[30], g_node.admin_session_state().admin_ed_pub[31] };
                const uint64_t keep0 = g_node.admin_session_state().epoch[0];
                s.reset(); mrfw::dispatch("regen confirm", 13, s);
                CHK(std::memcmp(before_pub, g_node.admin_session_state().admin_ed_pub, 32) == 0
                    && g_node.admin_session_state().epoch[0] == keep0,
                    "S5-5 ★★ `regen` rotates the MESSAGING identity and leaves the ADMINISTRATION root and every "
                    "session epoch EXACTLY as they were — two secrets, two lifetimes [%s]", s.buf);
            }
            // ---- S5-7 / S5-8: ★★ [[B341]] — LIVE ACTIVATION ACROSS A REBOOT, on the REAL `g_node` ----------
            //   The native cases (§radmin-5/R16/R17) drive the same two sequences through the pure services; these
            //   drive them through the REAL router, the REAL NV wrappers and the REAL
            //   `admin_stores_boot_report_console()` — i.e. across the actual `setup()` path, which is the only
            //   place the boot install exists. ⛔ Neither instrument alone proves it: the native one cannot reach
            //   `firmware_commands.cpp`, and this one cannot reach the ordering rules.
            {
                // (B) root generated -> REBOOT with the ACL still absent -> `acl add owner` must ACTIVATE.
                reset_nv(true);
                s.reset(); mrfw::dispatch("admin-id generate", 17, s);
                CHK(s.has("> admin-id generated"), "S5-7a a root is provisioned with no ACL yet [%s]", s.buf);
                mrcon.service(); Serial.reset();
                mrfw::admin_stores_boot_report_console();          // ★ THE REBOOT
                mrcon.service();
                CHK(std::strstr(Serial.out, "> admin-session boot state=disabled slots=0") != nullptr,
                    "S5-7b ...and the boot correctly reports `disabled` (no ACL) [%s]", Serial.out);
                CHK(g_node.admin_session_state().root_present == 1,
                    "S5-7c ★★ [[B341]]: the VALID ROOT HALF IS INSTALLED across the boot — before the fix this "
                    "was 0 and every durable success below described state the node did not hold");
                char line[128];
                std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
                s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                CHK(s.has("> acl added slot=0 role=owner"), "S5-7d the first owner is granted [%s]", s.buf);
                CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::ready
                    && g_node.admin_session_slots() == 1,
                    "S5-7e ★★★ THE PROPERTY: a durable success ACTIVATED the running node — ready, 1 slot");
                // …and a fresh boot on the SAME medium installs the SAME readiness.
                mrcon.service(); Serial.reset();
                mrfw::admin_stores_boot_report_console();
                mrcon.service();
                CHK(std::strstr(Serial.out, "> admin-session boot state=ready slots=1") != nullptr
                    && g_node.admin_session_readiness() == meshroute::AdminReadiness::ready,
                    "S5-7f ...and a fresh boot on the same medium installs EXACTLY that [%s]", Serial.out);
            }
            {
                // (D) a valid owner ACL + a CORRUPT root -> REBOOT -> `admin-id reset confirm` must ACTIVATE.
                reset_nv(true);
                {
                    char line[128];
                    s.reset(); mrfw::dispatch("admin-id generate", 17, s);
                    std::snprintf(line, sizeof line, "acl add owner %s", kKeyA);
                    s.reset(); mrfw::dispatch(line, std::strlen(line), s);
                    CHK(s.has("> acl added slot=0 role=owner"), "S5-8a a root and an owner are provisioned [%s]", s.buf);
                }
                // CORRUPT the root record in place — the ACL stays valid.
                {
                    mrnv::AdminIdBlob bad{};
                    mrnv::admin_id_blob_init(bad);
                    bad.version = 99;                              // a version the equality policy REJECTS
                    nv.put("mr", "admid", reinterpret_cast<const unsigned char*>(&bad), sizeof bad);
                    s.reset(); mrfw::dispatch("admin-id show", 13, s);
                    CHK(s.is("> admin-id err store_invalid\n"),
                        "S5-8b the root record now reads `store_invalid` [%s]", s.buf);
                }
                mrcon.service(); Serial.reset();
                mrfw::admin_stores_boot_report_console();          // ★ THE REBOOT
                mrcon.service();
                CHK(std::strstr(Serial.out, "> admin-session boot state=disabled slots=0") != nullptr,
                    "S5-8c ...and the boot correctly reports `disabled` (no usable root) [%s]", Serial.out);
                CHK(g_node.admin_session_state().root_present == 0
                    && g_node.admin_session_state().acl_occupied == 1
                    && g_node.admin_session_state().epoch[0] != 0,
                    "S5-8d ★★ [[B341]]: the BAD half is CLEARED and the VALID ACL HALF IS INSTALLED with a "
                    "prepared epoch — before the fix `acl_occupied` was 0");
                s.reset(); mrfw::dispatch("admin-id reset confirm", 22, s);
                CHK(s.has("> admin-id recovered"), "S5-8e the operator recovers the root [%s]", s.buf);
                CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::ready
                    && g_node.admin_session_slots() == 1
                    && g_node.admin_session_state().acl_occupied == 1,
                    "S5-8f ★★★ THE PROPERTY: the recovery ACTIVATED the node and the ACL SURVIVED it");
            }

            // ---- S5-6: an EMPTY/INVALID prerequisite installs the CLEARED image, never a stale one --------
            {
                reset_nv(true);
                mrcon.service(); Serial.reset();
                mrfw::admin_stores_boot_report_console();
                mrcon.service();
                CHK(std::strstr(Serial.out, "> admin-session boot state=disabled slots=0") != nullptr,
                    "S5-6a an unprovisioned node boots `disabled slots=0` [%s]", Serial.out);
                CHK(g_node.admin_session_readiness() == meshroute::AdminReadiness::disabled
                    && g_node.admin_session_state().root_present == 0
                    && g_node.admin_session_state().acl_occupied == 0,
                    "S5-6b ★★ …and the RUNNING image was CLEARED — ⛔ no stale live ACL survives a bad record");
            }
        }

        // ---- R41: every response goes to the SUPPLIED sink — `mrcon` gets zero bytes -----------------------
        {
            reset_nv(true);
            mrcon.service(); Serial.reset(); ble_reset();
            s.reset();
            mrfw::dispatch("acl list", 8, s);
            mrcon.service();
            CHK(s.n > 0 && Serial.n_out == 0 && g_ble_n == 0,
                "R41 the whole response lands on the SUPPLIED sink; `mrcon` gets 0 B [%u] and BLE 0 B [%u]",
                unsigned(Serial.n_out), unsigned(g_ble_n));
        }
        reset_nv(false);
    }
#endif   // MR_FEAT_RADMIN_ACCEPT

    // ================================================================================================
    // §RADMIN SLICE 4 — THE TWO CONTROLLER STORES, THROUGH THE REAL ROUTER AND THE REAL NV WRAPPERS.
    //
    // ⛔⛔ WHY THESE ROWS EXIST, stated as the defect they close — the ACCEPT block's argument, on the other
    //     product role: `test/test_firmware_admin_{keyring,targets,client_verbs}.cpp` pin every service rule and
    //     every emitted byte, and every one of those gates stays GREEN if the `dispatch()` arm is deleted, if the
    //     Print adapter writes to `mrcon` instead of the supplied sink, if a store binding is pointed at the
    //     WRONG SLOT (`/mradmid` instead of `/mrmkeys` — design §6.4's crossing), if the entropy binding returns
    //     an unconditional `true`, or if the resident scratch is never reloaded. None of that is reachable from a
    //     host build of `test/`.
    //
    // ⛔ THIS IS A **CLIENT** HOST PROFILE (`-DARDUINO -DBOARD_HELTEC_V3 -DMR_PROFILE_MOBILE` ⇒
    //    `MR_FEAT_RADMIN_CLIENT=1`). It is ⛔ NOT execution of mobile hardware, ⛔ NOT a real BLE transport and
    //    ⛔ NOT a flash test: the NV medium is a fake, so no wear, no power cut and no real filesystem is
    //    exercised. Bench Part 56 owns those.
    // ⛔ AND IT IS NOT BLE ADMISSION EVIDENCE EITHER: a `LineSink`-shaped sink is a SINK, not a transport. R-RA-30's
    //    guard lives in `ble_dispatch_line` and is measured by `tools/probe_console_sink/ble_guard.py`.
    // ================================================================================================
#if MR_FEAT_RADMIN_CLIENT
    {
        CaptureSink s;                                    // this arm's OWN sink — the ACCEPT block's is gated out
        static const char kName[]  = "probe-node";        // the same realistic operator label the R rows use
        const uint16_t    kNameLen = uint16_t(sizeof kName - 1);
        auto& nv  = mrprobe_nv();
        auto& rng = mrprobe_rng();
        auto reset_nv = [&](bool writable) {
            nv = MrProbeNv{};
            nv.ns_present = writable;
            nv.rw_ok      = writable;
            rng = MrProbeRng{};
        };
        const char* kPubB = "2222222222222222222222222222222222222222222222222222222222222222";
        const char* kSeedC = "3333333333333333333333333333333333333333333333333333333333333333";
        // ★★ THIS NODE'S OWN messaging identity, installed from a seed the probe's deterministic RNG stream can
        //    never produce. ⛔ WITHOUT IT the first `generate` would be a LEGITIMATE `duplicate`: the R rows leave
        //    `g_identity` derived from the SAME reset stream the draw replays, so self and the fresh key would be
        //    one principal — the service refusing correctly, and the row measuring the fixture instead.
        {
            uint8_t self_seed[32];
            for (size_t i = 0; i < sizeof self_seed; ++i) self_seed[i] = uint8_t(0xB0 ^ (i * 3));
            meshroute::identity_from_seed(g_identity, self_seed);
            g_node.set_identity(7, g_identity.key_hash32);
        }

        // ---- Q1: the two families REACH the router, and the TARGET half does NOT exist on this build --------
        {
            reset_nv(true);
            s.reset(); CHK(mrfw::dispatch("admin-key list", 14, s), "Q1  `admin-key list` is OWNED by the real dispatch()");
            s.reset(); CHK(mrfw::dispatch("admin-target list", 17, s), "Q1b `admin-target list` is OWNED by the real dispatch()");
            // ⛔ THE MIRROR OF R30c: on a CLIENT build the TARGET family is not a verb at all, so it must fall
            //    through to the caller's unsupported-verb answer with ZERO bytes emitted here.
            s.reset(); CHK(!mrfw::dispatch("acl list", 8, s) && s.n == 0,
                           "Q1c `acl list` (the TARGET family) is NOT owned on a CLIENT build — 0 bytes");
            s.reset(); CHK(!mrfw::dispatch("admin-id show", 13, s) && s.n == 0,
                           "Q1d `admin-id show` is NOT owned on a CLIENT build — 0 bytes");
            s.reset(); CHK(!mrfw::dispatch("admin-keys", 10, s) && s.n == 0,
                           "Q1e `admin-keys` is NOT owned — the token boundary holds in the real router");
            s.reset(); CHK(!mrfw::dispatch("admin-targetx", 13, s) && s.n == 0,
                           "Q1f `admin-targetx` is NOT owned — the token boundary holds in the real router");
        }

        // ---- Q2: `admin-key show self` answers from the LIVE identity and reads NOTHING -----------------------
        {
            reset_nv(true);
            nv.reads = 0; rng.draws = 0;
            s.reset();
            mrfw::dispatch("admin-key show self", 19, s);
            char want_self[160];
            char fp[mrfw::kAdminFpHex + 1], pub[mrfw::kAdminKeyHex + 1];
            mrfw::admin_fp_hex(g_identity.ed_pub, fp);
            mrfw::admin_key_hex(g_identity.ed_pub, pub);
            std::snprintf(want_self, sizeof want_self, "> admin-key self fp=%s pub=%s\n", fp, pub);
            CHK(std::strcmp(s.buf, want_self) == 0,
                "Q2  `show self` prints THIS node's live identity, byte for byte [%s]", s.buf);
            CHK(nv.reads == 0 && rng.draws == 0,
                "Q2b ...and it reads NO record and draws NO entropy (reads=%d draws=%d)", nv.reads, rng.draws);
        }

        // ---- Q3: `generate` writes the KEYRING SLOT and nothing else -------------------------------------
        {
            reset_nv(true);
            nv.writes = 0; rng.draws = 0;
            s.reset();
            mrfw::dispatch("admin-key generate key0", 23, s);
            CHK(std::strncmp(s.buf, "> admin-key generated key0 fp=", 30) == 0,
                "Q3  `generate key0` succeeds through the REAL binding [%s]", s.buf);
            // ⓘ EIGHT draws, not one: `mrrng::fill` fills 32 bytes from a 32-bit source. The figure is the
            //   PLATFORM's arithmetic and is pinned rather than rounded — a binding that drew twice would show 16.
            CHK(nv.writes == 1 && rng.draws == 8,
                "Q3b ...at EXACTLY one write and one full 32-byte draw (writes=%d draws=%u)",
                nv.writes, unsigned(rng.draws));
            // ⛔ THE SLOT BINDING, MEASURED: the bytes must be under `mkeys`, ⛔ never under `admid` or `targets`.
            CHK(nv.find("mr", "mkeys") != nullptr,
                "Q3c ...and the record landed on the `/mrmkeys` backend key");
            CHK(nv.find("mr", "admid") == nullptr && nv.find("mr", "targets") == nullptr,
                "Q3d ...⛔ and NOT on /mradmid (design §6.4's crossing) nor on /mrtargets");
            // …and the SEED is the probe's deterministic draw, so "it wrote what it drew" is measurable.
            mrnv::MgmtKeyBlob got{};
            CHK(mrnv::load_mgmt_keys(got) == mrnv::MgmtKeyRead::ok && got.count == 1
                && mrfw::mgmt_key_row_occupied(got.rec[0]),
                "Q3e ...the record re-loads through the typed wrapper as a valid one-key ring");
        }

        // ---- Q4: an ALL-ZERO platform draw refuses, having ASKED, and mints nothing --------------------------
        {
            reset_nv(true);
            rng.force_zero = true;
            nv.writes = 0; rng.draws = 0;
            s.reset();
            mrfw::dispatch("admin-key generate key1", 23, s);
            CHK(std::strcmp(s.buf, "> admin-key err entropy_failed\n") == 0,
                "Q4  a DEAD (all-zero) platform draw answers `entropy_failed` [%s]", s.buf);
            CHK(rng.draws == 8, "Q4b ...having ASKED the platform for a FULL 32-byte draw (draws=%u)",
                unsigned(rng.draws));
            CHK(nv.writes == 0, "Q4c ...and having written NOTHING (writes=%d)", nv.writes);
            rng.force_zero = false;
        }

        // ---- Q5: a refused medium answers nv_save_failed after exactly one attempt ---------------------------
        {
            reset_nv(true);
            nv.fail_write = true; nv.writes = 0;
            s.reset();
            mrfw::dispatch("admin-key generate key0", 23, s);
            CHK(std::strcmp(s.buf, "> admin-key err nv_save_failed\n") == 0,
                "Q5  a refused medium answers `nv_save_failed` [%s]", s.buf);
            CHK(nv.writes == 1, "Q5b ...after EXACTLY one attempt (writes=%d)", nv.writes);
        }

        // ---- Q6: `export` prints the SEED, and it is the one the probe supplied ------------------------------
        {
            reset_nv(true);
            char cmd[128];
            std::snprintf(cmd, sizeof cmd, "admin-key import key2 %s", kSeedC);
            s.reset();
            mrfw::dispatch(cmd, std::strlen(cmd), s);
            CHK(std::strncmp(s.buf, "> admin-key imported key2 fp=", 29) == 0,
                "Q6  `import key2 <hex>` succeeds through the REAL binding [%s]", s.buf);
            nv.writes = 0;
            s.reset();
            mrfw::dispatch("admin-key export key2", 21, s);
            char want_exp[160];
            std::snprintf(want_exp, sizeof want_exp, "> admin-key exported key2 seed=%s\n", kSeedC);
            CHK(std::strcmp(s.buf, want_exp) == 0,
                "Q6b `export key2` returns EXACTLY the imported seed [%s]", s.buf);
            CHK(nv.writes == 0, "Q6c ...and writes nothing (writes=%d)", nv.writes);
        }

        // ---- Q7: EVERY ordinary refusal costs zero stores, zero draws and reaches NO foreign handler ---------
        {
            reset_nv(true);
            const char* refusals[] = { "admin-key", "admin-key bogus", "admin-key show key9",
                                       "admin-key remove key0", "admin-key reset",
                                       "admin-target", "admin-target bogus", "admin-target show label=nope",
                                       "admin-target remove label=nope confirm", "admin-target reset" };
            for (const char* line : refusals) {
                nv.writes = 0; rng.draws = 0;
                s.reset(); Serial.reset(); ble_reset();
                const bool owned = mrfw::dispatch(line, std::strlen(line), s);
                mrcon.service();
                CHK(owned && s.n > 0 && std::strstr(s.buf, " err ") != nullptr,
                    "Q7  `%s` is OWNED and answers a typed refusal [%s]", line, s.buf);
                CHK(nv.writes == 0 && rng.draws == 0,
                    "Q7b `%s` costs ZERO writes and ZERO draws (writes=%d draws=%d)", line, nv.writes, rng.draws);
                CHK(Serial.n_out == 0 && g_ble_n == 0,
                    "Q7c `%s` reaches NO foreign handler and no global sink (%u/%u B)", line,
                    unsigned(Serial.n_out), unsigned(g_ble_n));
            }
        }

        // ---- Q8: the TARGET BOOK, end to end, over the ONE resident scratch ----------------------------------
        {
            reset_nv(true);
            char cmd[200];
            std::snprintf(cmd, sizeof cmd, "admin-target add alpha %s hash=0xDEADBEEF layer=1,2,3", kPubB);
            nv.writes = 0;
            s.reset();
            mrfw::dispatch(cmd, std::strlen(cmd), s);
            CHK(std::strncmp(s.buf, "> admin-target added slot=0 fp=", 30) == 0,
                "Q8  `admin-target add` succeeds through the REAL binding [%s]", s.buf);
            CHK(nv.writes == 1, "Q8b ...at EXACTLY one write (writes=%d)", nv.writes);
            CHK(nv.find("mr", "targets") != nullptr && nv.find("mr", "mkeys") == nullptr,
                "Q8c ...and the record landed on `/mrtargets`, ⛔ not on the keyring's key");
            // The resident scratch is IO storage, not a cache: two identical listings must agree exactly.
            s.reset();
            mrfw::dispatch("admin-target list", 17, s);
            // ⛔ A BOUNDED COPY, not `snprintf("%s")`: the sink's buffer is 4096 B and a format-truncation
            //    warning is gate-blocking here (`-Werror`). The size is the page bound plus headroom.
            char first[2048] = {};
            const size_t first_n = s.n < sizeof first - 1 ? s.n : sizeof first - 1;
            std::memcpy(first, s.buf, first_n);
            s.reset();
            mrfw::dispatch("admin-target list", 17, s);
            CHK(std::strcmp(first, s.buf) == 0,
                "Q8d two identical listings over the ONE resident scratch agree byte for byte");
            CHK(std::strstr(s.buf, "hash=0xDEADBEEF layer=1,2,3") != nullptr
                && std::strstr(s.buf, "> admin-target end page=0 count=1\n") != nullptr,
                "Q8e ...and the row carries the stored hint and path verbatim [%s]", s.buf);
            nv.writes = 0;
            s.reset();
            mrfw::dispatch("admin-target remove label=alpha confirm", 39, s);
            CHK(std::strcmp(s.buf, "> admin-target removed slot=0\n") == 0,
                "Q8f `remove label=… confirm` succeeds [%s]", s.buf);
            CHK(nv.writes == 1, "Q8g ...at EXACTLY one write (writes=%d)", nv.writes);
        }

        // ---- Q9: the READ-ONLY BOOT report — two lines, zero writes, zero draws, no key byte -----------------
        {
            reset_nv(true);
            nv.writes = 0; rng.draws = 0;
            Serial.reset(); ble_reset();
            mrfw::admin_client_stores_boot_report_console();
            mrcon.service();
            CHK(std::strcmp(Serial.out, "> admin-key boot state=absent count=0\n"
                                        "> admin-target boot state=absent count=0\n") == 0,
                "Q9  the CLIENT boot report prints EXACTLY the two ruled lines on `mrcon` [%s]", Serial.out);
            CHK(nv.writes == 0 && rng.draws == 0,
                "Q9b ...writing nothing and drawing nothing (writes=%d draws=%d)", nv.writes, rng.draws);
            CHK(!std::strstr(Serial.out, "pub=") && !std::strstr(Serial.out, "seed=")
                && !std::strstr(Serial.out, "fp="),
                "Q9c ...and carrying NO key, seed or fingerprint byte");
            CHK(g_ble_n == 0, "Q9d ...and nothing reached the BLE sink (%u B)", unsigned(g_ble_n));
        }

        // ---- Q10: a DEAD BACKEND makes BOTH records `io_failed`, and even the resets refuse ------------------
        {
            reset_nv(true);
            nv.ns_present = false;                       // a read-only begin() fails …
            mrprobe_nvs().backend_dead = true;           // … and NOT because the namespace was never written
            nv.writes = 0;
            s.reset(); mrfw::dispatch("admin-key list", 14, s);
            CHK(std::strcmp(s.buf, "> admin-key err store_io_failed\n") == 0,
                "Q10 a DEAD backend is `store_io_failed`, ⛔ never the fresh-device `absent` [%s]", s.buf);
            s.reset(); mrfw::dispatch("admin-target list", 17, s);
            CHK(std::strcmp(s.buf, "> admin-target err store_io_failed\n") == 0,
                "Q10b ...on the book too [%s]", s.buf);
            s.reset(); mrfw::dispatch("admin-key reset confirm", 23, s);
            CHK(std::strcmp(s.buf, "> admin-key err store_io_failed\n") == 0,
                "Q10c ⛔ EVEN THE CONFIRM-GATED RECOVERY refuses — nothing is known, so nothing may be written");
            s.reset(); mrfw::dispatch("admin-target reset confirm", 26, s);
            CHK(std::strcmp(s.buf, "> admin-target err store_io_failed\n") == 0,
                "Q10d ...and on the book too");
            Serial.reset();
            mrfw::admin_client_stores_boot_report_console();
            mrcon.service();
            CHK(std::strcmp(Serial.out, "> admin-key boot state=io_failed count=0\n"
                                        "> admin-target boot state=io_failed count=0\n") == 0,
                "Q10e the boot report NAMES the state and still writes nothing [%s]", Serial.out);
            CHK(nv.writes == 0, "Q10f ...and not one write was attempted anywhere (writes=%d)", nv.writes);
            mrprobe_nvs().backend_dead = false;
        }

        // ---- Q11: EVERY response lands on the SUPPLIED sink — `mrcon` and BLE get zero bytes -----------------
        {
            reset_nv(true);
            mrcon.service(); Serial.reset(); ble_reset();
            s.reset();
            mrfw::dispatch("admin-key list", 14, s);
            mrcon.service();
            CHK(s.n > 0 && Serial.n_out == 0 && g_ble_n == 0,
                "Q11 the whole response lands on the SUPPLIED sink; `mrcon` 0 B [%u], BLE 0 B [%u]",
                unsigned(Serial.n_out), unsigned(g_ble_n));
        }

        // ---- Q12: ★★ `regen` PRESERVES BOTH CONTROLLER STORES, byte for byte, on success AND on failure ------
        //      This is what makes the ruled warning's `dedicated keys and targets preserved` a PROVEN claim
        //      rather than a comforting sentence: the two records are snapshotted before and compared after.
        {
            reset_nv(true);
            char cmd[200];
            std::snprintf(cmd, sizeof cmd, "admin-key import key0 %s", kSeedC);
            s.reset(); mrfw::dispatch(cmd, std::strlen(cmd), s);
            std::snprintf(cmd, sizeof cmd, "admin-target add alpha %s hash=0x1", kPubB);
            s.reset(); mrfw::dispatch(cmd, std::strlen(cmd), s);
            mrnv::MgmtKeyBlob keys_before{};
            mrnv::TargetBlob  book_before{};
            CHK(mrnv::load_mgmt_keys(keys_before) == mrnv::MgmtKeyRead::ok
                && mrnv::load_targets(book_before) == mrnv::TargetRead::ok,
                "Q12 both controller records are provisioned before the rotation");

            // ⛔ NOT `seed_id()` HERE: that fixture calls `nv.reset()`, which would wipe the two records this
            //    very case is about — the medium would then agree with itself for the wrong reason.
            {
                mrnv::IdBlob idb{};
                idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
                idb.name_len = kNameLen;
                for (uint16_t i = 0; i < kNameLen; ++i) idb.name[i] = kName[i];
                for (size_t i = 0; i < sizeof idb.seed; ++i) idb.seed[i] = uint8_t(0x40 + i);
                (void)mrnv::save_id(idb);
            }
            nv.writes = 0;
            ble_reset(); Serial.reset();
            route_ble("regen");
            mrcon.service();
            CHK(std::strstr(g_ble, "> regen ok") == g_ble,
                "Q12b `regen` still succeeds on a CLIENT build [%s]", g_ble);
            CHK(std::strstr(g_ble, "> regen note old self ACL grants do not follow the new key; "
                                   "dedicated keys and targets preserved\n") != nullptr,
                "Q12c ...and the ruled WARNING follows the success line on the SAME sink [%s]", g_ble);
            CHK(Serial.n_out == 0,
                "Q12d ...with NOT ONE byte on the global console (%u B)", unsigned(Serial.n_out));
            CHK(nv.writes == 1,
                "Q12e ...and EXACTLY ONE record was written — /mrid (writes=%d)", nv.writes);
            mrnv::MgmtKeyBlob keys_after{};
            mrnv::TargetBlob  book_after{};
            CHK(mrnv::load_mgmt_keys(keys_after) == mrnv::MgmtKeyRead::ok
                && std::memcmp(&keys_before, &keys_after, sizeof keys_after) == 0,
                "Q12f ★ the KEYRING is byte-identical across the rotation");
            CHK(mrnv::load_targets(book_after) == mrnv::TargetRead::ok
                && std::memcmp(&book_before, &book_after, sizeof book_after) == 0,
                "Q12g ★ the TARGET BOOK is byte-identical across the rotation");

            // …and on the FAILURE path neither the success line nor the warning is printed.
            nv.fail_write = true;
            ble_reset(); Serial.reset();
            route_ble("regen");
            mrcon.service();
            CHK(std::strcmp(g_ble, "> regen err nv_save_failed\r\n") == 0,
                "Q12h a REFUSED /mrid save prints the error and ⛔ NO warning [%s]", g_ble);
            CHK(!std::strstr(g_ble, "regen note"),
                "Q12i ...the CLIENT warning is ABSENT on the failure path");
            nv.fail_write = false;
        }
        reset_nv(false);
    }
#endif   // MR_FEAT_RADMIN_CLIENT

    // Slice 6: synthetic contexts through the real transport-neutral seam, on BOTH compiled arms.
    // No fw_main.cpp execution is claimed: its BLE head is structurally checked and has a bench residue.
    {
        using namespace mrfw;
        using namespace meshroute::console;
        const CommandContext local{CommandTransport::usb, CommandAuthority::local, true, 0, local_command_max_bytes};
        char reply[256];
        auto call = [&](const std::string& line, LineFormat format, const CommandContext& ctx) {
            g_sink.reset(); std::memset(reply, 0, sizeof reply); g_routed[0] = '\0'; g_command_calls = 0;
            return exec_console_line(line.data(), line.size(), format, g_sink, reply, sizeof reply, ctx);
        };
        for (const auto format : {LineFormat::text, LineFormat::json}) {
            for (const char bad : {'\0', '\r', '\n'}) {
                std::string line = "send 5 \"A?B\"";
                line[9] = bad;
                const LineErr reason = bad == '\0' ? LineErr::embedded_nul : bad == '\r' ? LineErr::embedded_cr : LineErr::embedded_lf;
                const auto result = call(line, format, local);
                CHK(result.outcome == DispatchOutcome::refused && result.refuse == RefuseReason::bad_line
                    && result.line_err == reason && result.parse_err == ParseErr::ok,
                    "Y1 validator refuses %s before parsing (format=%u)", line_err_name(reason), unsigned(format));
                const std::string expected = format == LineFormat::text ? std::string("> err bad_line ") + line_err_name(reason) + "\n"
                    : std::string("{\"err\":\"bad_line\",\"msg\":\"") + line_err_name(reason) + "\"}\n";
                CHK(format == LineFormat::text ? g_sink.is(expected.c_str()) && reply[0] == '\0'
                                               : std::strcmp(reply, expected.c_str()) == 0 && g_sink.n == 0,
                    "Y2 exact bad-line envelope, only on the supplied output (format=%u)", unsigned(format));
                CHK(g_command_calls == 0 && g_routed[0] == '\0', "Y3 bad bytes execute no Node command or router handler");
            }
            for (const size_t cap : {local_command_max_bytes, size_t(274), remote_command_max_bytes}) {
                CommandContext bounded = local; bounded.line_max_bytes = cap;
                const std::string line = "send 5 \"" + std::string(cap - 9, 'x') + "\"";
                const auto accepted = call(line, format, bounded);
                CHK(accepted.outcome == DispatchOutcome::completed && accepted.line_err == LineErr::ok
                    && g_command_calls == 1, "Y4 inclusive bound %u reaches the real Node once (format=%u); not an RF-admission claim", unsigned(cap), unsigned(format));
                const auto refused = call(line + "x", format, bounded);
                CHK(refused.outcome == DispatchOutcome::refused && refused.line_err == LineErr::too_long
                    && g_command_calls == 0, "Y5 cap+1 refuses before execution (%u, format=%u)", unsigned(cap), unsigned(format));
            }
            const char* lines[] = {"status", "status x", "version", "factory_reset confirm", "acl list", "acl reset confirm",
                                   "admin-key list", "rcmd 1 status", "unknown-verb"};
            // Admission is not completion: status x and the absent-role ACL arm can be unmatched after admission.
            const bool admitted[3][9] = {{true,false,false,false,false,false,false,false,false},
                                         {true,true,true,false,false,false,false,false,false},
                                         {true,true,true,false,true,false,false,false,false}};
            // This direct seam fixture has no admitted seen row/capturing transcript. On ACCEPT,
            // an owner factory reset reaches preparation and fails internally without any effect.
            for (unsigned a = 0; a < 3; ++a) {
                const CommandContext remote{CommandTransport::remote, static_cast<CommandAuthority>(a + 1), false, 42, remote_command_max_bytes};
                for (unsigned i = 0; i < 9; ++i) {
                    auto& nv = mrprobe_nv(); const int writes = nv.writes;
                    const int stores = production_store_touches();
                    const auto result = call(lines[i], format, remote);
                    const bool missing_transcript = MR_FEAT_RADMIN_ACCEPT && a == 2 && i == 3;
                    CHK(missing_transcript ? result.outcome == DispatchOutcome::internal_failure
                        : (result.outcome != DispatchOutcome::refused) == admitted[a][i],
                        "Y6 remote authority %u admission for %s (format=%u)", a + 1, lines[i], unsigned(format));
                    if (!admitted[a][i]) {
                        CHK(g_sink.n == 0 && reply[0] == '\0' && result.n == 0 && g_command_calls == 0
                            && g_routed[0] == '\0' && nv.writes == writes && production_store_touches() == stores
                            && result.refuse == (i >= 7 ? RefuseReason::unclassified : RefuseReason::authority),
                            "Y7 remote refusal has zero output, Node calls, handler calls and NV/store changes");
                    }
                    CHK(missing_transcript ? result.outcome == DispatchOutcome::internal_failure
                        : result.outcome != DispatchOutcome::scheduled && result.outcome != DispatchOutcome::internal_failure,
                        "Y8 only prepared admitted requests can schedule; a missing transcript fails internally");
                }
                std::string bad = "status"; bad.push_back('\0');
                const auto result = call(bad, format, remote);
                CHK(result.refuse == RefuseReason::bad_line && result.line_err == LineErr::embedded_nul
                    && g_sink.n == 0 && reply[0] == '\0' && g_command_calls == 0,
                    "Y9 remote validation refusal also writes nothing (authority=%u format=%u)", a + 1, unsigned(format));
            }
        }
        g_command_calls = 0;
        const char panel[] = "send 5 \"A\0B\"";
        const auto result = exec_command(panel, sizeof panel - 1);
        CHK(!result.ok && result.line_err == LineErr::embedded_nul && result.parse_err == ParseErr::ok
            && g_command_calls == 0, "Y10 real panel executor refuses NUL without parsing or executing");
        const auto good = exec_command("send 5 \"ok\"", 11);
        CHK(good.ok && good.line_err == LineErr::ok && g_command_calls == 1,
            "Y11 panel success proves the real Node command counter discriminates");
    }

    // B360, re-fixtured at v26: keep all eight typed-wrapper checks on BOTH arms.
    {
        auto& medium = mrprobe_nv();
        medium.reset(); medium.ns_present = true; medium.rw_ok = true;
        mrnv::Blob record{}; record.magic = mrnv::kMagic; record.version = 25;
        record.remote_action_activation_ms = 20000;
        CHK(sizeof record == 240, "A7-1 config record shrinks after legacy mirror removal");
        CHK(mrnv::save(record), "A7-2 actual wrapper stores the same-size v25 fixture");
        mrnv::Blob loaded{};
        CHK(!mrnv::load(loaded), "A7-3 actual loader rejects v25 below the floor");
        record.version = 26;
        CHK(mrnv::save(record), "A7-4 actual wrapper stores v26");
        CHK(mrnv::load(loaded), "A7-5 actual loader accepts v26");
        CHK(loaded.remote_action_activation_ms == 20000, "A7-6 activation raw field survives typed reload");
        record.version = 27;
        CHK(mrnv::save(record), "A7-7 actual wrapper stores the future-version fixture");
        CHK(!mrnv::load(loaded), "A7-8 actual loader rejects v27");
        medium.reset();
    }
    // Slice 10: REAL ESP32 slot primitives + typed config wrappers, byte-backed synthetic medium.
    // No claim to execute setup(), nv_load_stamped() or real flash: Part 57f owns those effects.
    {
        auto& medium = mrprobe_nv();
        medium.reset(); medium.ns_present = true; medium.rw_ok = true;
        struct StoreWitness { const mrnv::Slot* slot; size_t size; uint8_t fill; };
        const StoreWitness stores[] = {
            {&mrnv::kSlotAdmid, sizeof(mrnv::AdminIdBlob), 0x31},
            {&mrnv::kSlotAcl, sizeof(mrnv::AclBlob), 0x52},
            {&mrnv::kSlotMgmtKeys, sizeof(mrnv::MgmtKeyBlob), 0x73},
            {&mrnv::kSlotTargets, sizeof(mrnv::TargetBlob), 0x94},
        };
        // Distinct opaque witnesses prove storage isolation without invoking store provisioning policy.
        uint8_t witness[sizeof(mrnv::TargetBlob)]{};
        for (const auto& store : stores) {
            std::memset(witness, store.fill, store.size);
            CHK(mrnv::write_slot(*store.slot, witness, store.size),
                "A7-9 seed independent administration slot %s", store.slot->path);
        }
        uint8_t old_record[280]{};
        const uint32_t old_magic = mrnv::kMagic;
        const uint16_t old_version = 25;
        std::memcpy(old_record, &old_magic, sizeof old_magic);
        std::memcpy(old_record + sizeof old_magic, &old_version, sizeof old_version);
        CHK(mrnv::write_slot(mrnv::kSlotCfg, old_record, sizeof old_record),
            "A7-10 store actual 280-byte v25 image through the primitive");
        const int writes_before = medium.writes;
        mrnv::Blob loaded{};
        const bool old_loaded = mrnv::load(loaded);
        CHK(!old_loaded, "A7-11 real typed loader refuses the old image size");
        Serial.reset();
        mrfw::nv_boot_report_console(mrcon, old_loaded); mrcon.service();
        CHK(std::strcmp(Serial.out, "> nv: /mrcfg v26 not loaded (absent, or a pre-v26 record refused): compile-time defaults until the next cfg set\r\n") == 0,
            "A7-12 failed-load golden comes from the real formatter [%s]", Serial.out);
        CHK(medium.writes == writes_before, "A7-13 rejection and report never write storage");
        for (const auto& store : stores) {
            std::memset(witness, store.fill, store.size);
            CHK(medium.holds(store.slot->ns, store.slot->key, witness, store.size),
                "A7-14 rejected /mrcfg leaves %s byte-identical", store.slot->path);
        }
        mrnv::Blob current{};
        current.magic = mrnv::kMagic; current.version = mrnv::kVersion;
        current.remote_action_activation_ms = 20000;
        CHK(mrnv::save(current), "A7-15 persist current record through real save wrapper");
        const bool current_loaded = mrnv::load(loaded);
        CHK(current_loaded, "A7-16 following load accepts persisted v26");
        Serial.reset();
        mrfw::nv_boot_report_console(mrcon, current_loaded); mrcon.service();
        CHK(std::strcmp(Serial.out, "> nv: /mrcfg v26 loaded\r\n") == 0,
            "A7-17 loaded golden comes from the real formatter [%s]", Serial.out);
        CHK(medium.writes == writes_before + 1, "A7-18 only the one config save writes storage");
        for (const auto& store : stores) {
            std::memset(witness, store.fill, store.size);
            CHK(medium.holds(store.slot->ns, store.slot->key, witness, store.size),
                "A7-19 saved/reloaded /mrcfg leaves %s byte-identical", store.slot->path);
        }
        medium.reset(); Serial.reset();
    }
    radmin7_inbox_rows();
#if MR_FEAT_RADMIN_ACCEPT
    radmin7_air_rows();
    radmin72_open_rows();
#else
    CaptureSink client_status;
    CHK(mrfw::dispatch("status", 6, client_status), "R72-S3 CLIENT status still dispatches locally");
    for (const char* name : {"radmin_inbound_refusal=", "radmin_open_rate_refusal=", "radmin_transcript_exhaustion=",
                             "radmin_response_enqueue_failure=", "radmin_response_seal_failure="})
        CHK(!client_status.has(name), "R72-S4 CLIENT status has no target field: %s", name);
#endif
#if MR_FEAT_RADMIN_CLIENT
    remote_client_rows();
#endif
    printf("checks: %d   failures: %d\n", g_chk, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
#endif   // MR0C_NO_MAIN
