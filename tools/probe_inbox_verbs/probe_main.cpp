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
#include "fw_context.h"          // the REAL device context — g_node/g_hal + every global the router references
#include "firmware_commands.h"   // mrfw::dispatch — THE ROUTER UNDER TEST
#include "firmware_inbox.h"
#include "console_json.h"
#include "inbox.h"
#include "sched_send.h"
#include "identity.h"
#include "fault_log.h"
#include "board_rf_provider.h"   // meshroute::board_rf_instance() — the same seam fw_main.cpp:177 passes

#include <cstdio>
#include <cstring>

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
    size_t write(uint8_t b) override { if (n + 1 < sizeof buf) { buf[n++] = char(b); buf[n] = '\0'; } return 1; }
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
    struct Rec { uint32_t seq; uint8_t body[8]; uint16_t len; };
    Rec      rec[16] = {};
    uint16_t n_rec = 0;
    uint32_t persisted_next = 0, cursor = 0, epoch = 1;
    int      wipe_calls = 0, set_next_calls = 0, cursor_calls = 0;
    bool     fail_wipe = false, fail_set_next = false;

    bool begin() override { return true; }
    bool append(uint32_t seq, const uint8_t* r, uint16_t len) override {
        if (n_rec >= 16) return false;
        rec[n_rec].seq = seq; rec[n_rec].len = (len > 8) ? 8 : len;
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
    bool     seg_erase(uint16_t) override { ++erases; return true; }
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

uint8_t  g_remote_action = 0;   uint64_t g_remote_action_at = 0;
uint32_t g_rx_count = 0, g_sleep_count = 0;
uint32_t g_wake_gpio = 0, g_wake_ext1 = 0, g_wake_timer = 0;
uint32_t g_wake_arm_busy = 0, g_wake_arm_fail = 0, g_wake_disarm_fail = 0, g_wake_sleep_fail = 0;
bool     g_force_sleep = false, g_halted = false, g_host_present = false, g_radio_ok = true, g_fs_reformatted = false;
bool     g_last_reset_valid = false;
int8_t   g_tx_power = 0;
double   g_freq_mhz = 869.525;
uint8_t  g_ble_mode = 0, g_ble_period_min = 15;
uint32_t g_ble_pin = 123456;
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
void handle_lock(Print&)                      { routed("lock"); }
void handle_mobile(const char*, Print&)       { routed("mobile"); }
void handle_password(const char*, Print&)     { routed("password"); }
void handle_rcmd(const char*, Print&)         { routed("rcmd"); }
void handle_team(const char*, Print&)         { routed("team"); }
void handle_unlock(const char*, Print&)       { routed("unlock"); }
}  // namespace mrfw
void fw_reboot()                 { routed("reboot"); }
void fw_ota()                    { routed("ota"); }
void fw_prep_restart(Print&)     { routed("prep_restart"); }
void fw_crashtest(const char*, Print&) { routed("crashtest"); }
void fw_faults_dump(Print&)      { routed("faults_dump"); }
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

    printf("checks: %d   failures: %d\n", g_chk, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
