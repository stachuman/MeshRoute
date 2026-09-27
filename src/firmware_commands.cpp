// MeshRoute — src/firmware_commands.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The console command cluster (see firmware_commands.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-15).
// Shared device state comes from fw_context.h; the STAY-set board-glue (do_reboot/do_ota/dump_faults/
// handle_crashtest/handle_prep_restart) is reached ONLY via the fw_context.h wrappers — this TU MUST NOT include
// device_fault.h (its ISR vectors + the MRFAULT_HW/MRFAULT_ESP32 macros are single-TU). Behaviour-preserving.
#include "firmware_commands.h"
#include "firmware_action_effects.h"
#include "firmware_command_authority.h"
#include "firmware_remote_activation.h"
#include "firmware_remote_executor.h"
#include "firmware_remote_actions.h"
#include "firmware_remote_client.h"
#include "fw_context.h"        // g_node + the shared state
#include "device_nv.h"         // mrnv::PeerBlob / load_peers / save_peers
#include <cstdio>              // snprintf
#include <cstring>             // memcpy
#include <cstdlib>             // strtol/strtoul (route/testsched/lookup parsing)
#include "firmware_config.h"   // dispatch re-fans-out to mrfw:: config verbs (gateway/join/create/team/mobile/leave/cfg_set)
#include "firmware_inbox.h"    // dispatch/ble: pull_inbox / mark_read / del_msg
#include "console_sink.h"      // mrcon (inline, ODR-merged). ⛔ V1 CORRECTION (§0b/[[B279]]): this note read
                              //   "do_regen + handlers print through it" — WITHDRAWN. `do_regen` and every
                              //   dispatcher-reachable handler print through the sink they are HANDED; the
                              //   direct `mrcon` writers left in this TU are the BOOT-ONLY ones
                              //   (peer_store_restore's one-line summary, preset_boot_restore_console).
#include "firmware_help.h"     // ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]]): this read "§0a/[[B208]]: the console
                              //   help authority — index, the NINE `help <topic>` SECTIONS and the ONE `help`/`?`
                              //   recognition." The nine sections were RETIRED by §0g (owner ruling 2026-09-05).
                              //   Current truth: the console help authority — the BARE PRIMARY-NAME index, the one
                              //   bounded refusal for every retired argument-bearing form, and the ONE `help`/`?`
                              //   recognition. dispatch() still keeps only the call.
#include "console_json.h"      // write_status/write_cfg/write_limits/write_route + StatusFields/CfgExtras
#include "frame_trace.h"       // g_mr_trace_on (handle_debug)
#include "sched_send.h"        // mrsched::Schedule (handle_testsched/teststatus + g_sched)
#include "device_rng.h"        // mrrng::fill (do_regen + the §RADMIN slice 3 checked seed draw)
// §RADMIN slice 3 — the two TARGET-SIDE STORES' PURE services. ⓘ THE INCLUDE IS **UNGATED**, and that is the
// [[B255]] idiom rather than an oversight: the three headers carry ⛔ no capability macro at all, so the native
// suite exercises every service arm without defining a product role. Every function in them is `inline`, so on a
// CLIENT build — where the bindings, the dispatch arms, the help names and the BLE guard are all compiled out —
// nothing references them and ⛔ nothing is emitted. Only the INSTANTIATION and the console surfacing are gated.
#include "firmware_admin_verbs.h"   // mrfw::admin_id_verb / acl_verb / admin_boot_report / admin_id_owns / acl_owns
#include "firmware_admin_runtime.h"  // §RADMIN slice 5: mrfw::AdminLiveInstall / IAdminRuntime / admin_runtime_boot
                                    //   + AdminIdService / AclService — PURE; this file binds the stores, the
                                    //   checked draw, a Print sink and the two dispatch arms, and ⛔ decides nothing
// §RADMIN slice 4 — the CONTROLLER half, UNGATED for the same [[B255]] reason: the header carries no capability
// macro, so an ACCEPT board compiles it and instantiates nothing.
#include "firmware_admin_client_verbs.h"   // mrfw::admin_key_verb / admin_target_verb / admin_client_boot_report
                                           //   + admin_client_ble_refuses / client_regen_* — PURE; this file binds
                                           //   the two stores, the checked draw, the future-caller predicates, a
                                           //   Print sink and the two dispatch arms, and ⛔ decides nothing
#if MR_FEAT_OLED   // ★ [[B255]] the include (see the gated binding block below; the header itself stays ungated)
#include "firmware_ui_preset_verbs.h"  // §UI-10/11 P2: mrfw::preset_verb / preset_boot_restore / the three NDJSON
                                       //   records — PURE; this file only binds the store, the gate and a Print
#endif   // MR_FEAT_OLED

#ifndef GIT_REV
#define GIT_REV "nogit"        // native / ad-hoc compilation ONLY — no board env can reach this any more
// ⛔ CORRECTED TWICE: §B213 (2026-08-18) killed "the nRF52 base env only" (§B200 heltec_v3 + §B213 xiao_esp32s3 had
//   both shipped `nogit` banners); §B253 (2026-08-28) kills "it exists so a stray env still compiles" — git_rev.py
//   now ABORTS a board build whose Git provenance is unusable and emits this sentinel on NO arm, and
//   probe_build_identity.py derives every env from `pio project config`, RED unless each non-native runs it once.
// ⚠ EDIT LINE-COUNT-NEUTRALLY: a 9-line growth here moved __LINE__ and the heltec_mobile payload at IDENTICAL size.
#endif

namespace mrfw {

// §B138: ONE device-image authority for both the USB banner and BLE `version`. Keeping the literal in this
// universal TU prevents separately compiled callers from acquiring different compile-time clock values and defeating
// linker string merging. These arrays have external read-only linkage through firmware_commands.h.
const char kBuildStamp[]  = __DATE__ " " __TIME__;
const char kGitRevision[] = GIT_REV;

meshroute::ActivationBudgetInputs remote_activation_live_inputs() {
    const auto& cfg = g_node.config();
    const uint8_t data_sf = g_node.max_data_sf();
    return {cfg.routing_sf, data_sf, g_node.active_bw_hz(), g_node.active_cr(),
            meshroute::protocol::preamble_sym,
            g_hal.rx_window_slop_ms(cfg.routing_sf), g_hal.rx_window_slop_ms(data_sf),
            meshroute::remote_scheduled_terminal_inner_len, false};
}

// ---- the /mrpeers address book: the I/O half (§AB1, spec 2026-07-29 §2.4) --------------------------------------
// ★ ONE 1160-B SCRATCH RECORD, STATIC, SHARED BY BOTH USERS BELOW — and `static` here is a HARD requirement, not
// tidiness. /mrpeers is whole-blob R/W and v2 doubled it (584 -> 1160 B), and BOTH call sites are stack-critical:
//   - peer_store_restore() runs from setup(), i.e. on the nRF52 Arduino loop task, whose stack is a FIXED 4 KB
//     (LOOP_STACK_SZ = 256*4 in cores/nRF5/main.cpp, not overridable) — 1160 B is 28% of the WHOLE stack, and this
//     tree has already HARDFAULTed on that stack once (fw_main.cpp §stability: `stackhw` fell to 72 B);
//   - peer_store_sync() runs from the console / push drain in the 8 KB g_mesh_task, whose deepest RX path
//     (hash-locate -> RREQ flood -> do_post_ack) already nests ~1.4 KB.
// Sharing it is safe because the two CANNOT overlap: setup() returns before loop() lazily creates g_mesh_task, and
// everything after that is single-threaded cooperative on that one task. Same reason (and the same wording) as the
// `static Blob cur` in mrnv::save() and the `static mrfault::FaultLog fl` in mrnv::factory_erase().
static mrnv::PeerBlob s_peers;

// Mirror ONE peer of the live address book into /mrpeers. Was `persist_pinned_peer` (pinned-only, one record built
// field-by-field at the call site); EXTENDED rather than forked (U1). Two halves, deliberately split:
//   - the SELECTION + EVICTION policy is mrnv::peer_rec_put (device_nv.h — pure, host-tested, mirrors
//     Node::peer_key_set's rules without forking them);
//   - this function is the I/O half plus THE ONE RAM->NV conversion path (U2): every field is read from the LIVE
//     table through the existing accessors (peer_key_find -> ed_pub + the CURRENT confidence, peer_name_find -> the
//     cached name), so no caller can hand the store a name or a confidence that disagrees with RAM.
mrnv::PeerPut peer_store_sync(uint32_t kh) {
    uint8_t ed[32];
    meshroute::Node::PeerKeyConf conf = meshroute::Node::PeerKeyConf::overheard;
    if (!g_node.peer_key_find(kh, ed, &conf)) return mrnv::PeerPut::refused_absent;   // C2: absent/aged in RAM -> nothing to mirror
                                                                                    // (reachable: a peer_key_cached push is drained
                                                                                    // after the cache event, and a cache-on-pass flood
                                                                                    // can evict the entry in between)
    char nm[32];
    const uint8_t nl = g_node.peer_name_find(kh, nm, sizeof nm);
    // ★ RE-READ THE STORE EVERY TIME, deliberately — do NOT treat `s_peers` as a warm cache after the first load.
    // `factory_erase()` (a `factory_reset confirm`) wipes /mrpeers behind this function's back, and a cached copy
    // would then be written straight back over the wipe. A read is cheap and wears nothing; only writes wear flash.
    if (!mrnv::load_peers(s_peers)) mrnv::peers_blob_init(s_peers);   // absent, or a REJECTED v1 record -> start a fresh v2 store
    const mrnv::PeerPut r = mrnv::peer_rec_put(s_peers, kh, ed, (uint8_t)conf, nm, nl);
    // ★ `unchanged` => DO NOT WRITE. The wear guard is the whole reason peer_rec_put reports an outcome instead of a
    // bool: v2 made every on-air key-learn a write candidate, and a re-cache of an identical record must cost no flash.
    if (r == mrnv::PeerPut::updated || r == mrnv::PeerPut::inserted || r == mrnv::PeerPut::evicted)
        (void)mrnv::save_peers(s_peers);   // §nv-unchecked [5/5]: the save verdict is DISCARDED, preserved deliberately
                                           // (see fw_main §nv-unchecked [1/5]) — best-effort NV; the RAM key works
                                           // regardless. Owner ruling owed. The POLICY verdict `r` IS returned.
    return r;
}

// E2E §2 + §AB1: reload the address book at boot. Re-installs each stored record AT ITS STORED CONFIDENCE — v1
// re-installed EVERYTHING as `pinned`, which silently asserted "a human verified this via QR" of a key learned on
// air. An unrecognised confidence byte is SKIPPED, never guessed at (mrnv::peer_conf_restorable). Called from
// setup() AFTER on_init so the LayerRuntime `_active` is live. Returns the number of records re-installed.
uint16_t peer_store_restore() {
    uint16_t ok = 0, pinned = 0, bad = 0;
    if (mrnv::load_peers(s_peers) && s_peers.count <= mrnv::kMaxPeerRecs) {
        for (uint16_t i = 0; i < s_peers.count; ++i) {
            const mrnv::PeerRec& rec = s_peers.rec[i];
            if (!mrnv::peer_conf_restorable(rec.confidence)) { ++bad; continue; }
            const bool pin = (rec.confidence == mrnv::kPeerConfPinned);
            uint8_t nl = rec.name_len;
            if (nl > sizeof rec.name) nl = (uint8_t)sizeof rec.name;      // a bit-rotted name_len must not overrun
            if (g_node.peer_key_set(rec.key_hash32, rec.ed_pub,
                                    pin ? meshroute::Node::PeerKeyConf::pinned
                                        : meshroute::Node::PeerKeyConf::authoritative,
                                    rec.name, nl)) { ++ok; pinned += pin ? 1 : 0; }
            else ++bad;                                                   // ed_pub[:4] != hash, or the cache is full of pinned keys
        }
    }
    // One boot line: the kPeersVersion 1->2 bump makes this read `0 restored` exactly once (the v1 record is
    // rejected), so the one-time loss of the pinned store is OBSERVABLE rather than silent.
    mrcon.print(F("  peers     = ")); mrcon.print(ok);   mrcon.print(F(" restored ("));
    mrcon.print(pinned);              mrcon.print(F(" pinned, "));
    mrcon.print((uint16_t)(ok - pinned)); mrcon.print(F(" authoritative)"));
    if (bad) { mrcon.print(F(" ⚠ ")); mrcon.print(bad); mrcon.print(F(" REJECTED")); }
    mrcon.println();
    return ok;
}

// ================================================================ §UI-10/11 slice P2 — THE `/mrui` CATALOG BINDING
// ★★★★ [[B255]] — **THE WHOLE BINDING IS `MR_FEAT_OLED`-GATED (owner ruling (b), 2026-08-28).** The one live catalog
//      is the `static mrfw::PresetCatalog cat` below, and QG measured it in the linked `gateway` ELF as
//      `preset_catalog()::cat` in `.bss` at exactly **0x46c = 1132 B** — paid by a headless board for a panel it does
//      not have. The ruling: *"a headless gateway cannot select these presets, and a uniform-but-unusable command
//      surface is not worth 1.1 KB on the tightest board; `gateway_heltec*` retains the feature (OLED enabled)"*.
// ⛔ THE GATE IS SOURCE-SIDE AND NOTHING ELSE MOVED: no `platformio.ini` flag, no change to the pure units, no
//    change to the `'MRU1'` record. Every `MR_FEAT_OLED=1` env compiles byte-for-byte what it compiled before.
// ⓘ `/mrui` INTERPLAY, VERIFIED AND ⛔ NEEDING NO CODE: `mrnv::save_ui_presets` has exactly ONE caller — the
//    `DeviceUiPresetStore::save` override inside this guard — so a gated build NEVER creates the record; and
//    `mrnv::kSlotUi` lives in the `"mr"` namespace / InternalFS root, so `factory_erase()` (`Preferences::clear()`
//    on ESP32, `InternalFS.format()` on nRF52) already takes any stale one. See `device_nv.h`'s kSlotUi ledger.
#if MR_FEAT_OLED   // ★ [[B255]] the catalog binding: the ONE instance, its adapters and the two entry points
// ★★ THIN ON PURPOSE, for the fourth time in this arc and for the same MEASURED reason (§B115): this TU is compiled
//    by NEITHER the native suite (`test_build_src = no`) NOR the simulator, and no corpus scenario runs a console
//    verb or a boot. ⇒ every DECISION — the grammar, the three records' bytes, the six reason spellings, the stable
//    slot order, the result->output rule and the boot diagnosis — lives in `src/firmware_ui_preset_verbs.h`, where
//    `test/test_firmware_ui_preset_verbs.cpp` drives it and `--target=uipresetverbs` attacks it. What is left here
//    is: bind the store, bind the `busy` fact, hold the ONE instance, adapt a `Print`, call.
// ⛔ NOT `device_cfg_store()` AND NOT `mrnv::Blob`: `/mrui` is a DIFFERENT record with a FOUR-valued read, and the
//    design's own rule is that *"editing a phrase must never reset radio, identity, team or key configuration"*.
//    ⛔ No `/mrcfg` writer is in scope on any path below.
namespace {
struct DeviceUiPresetStore : mrfw::IUiPresetStore {
    mrnv::UiPresetRead load(mrnv::UiPresetBlob& out) override { return mrnv::load_ui_presets(out); }
    bool save(const mrnv::UiPresetBlob& b)           override { return mrnv::save_ui_presets(b); }
};
// ★ THE `busy` FACT, ASKED AT HANDLING TIME AND ⛔ NEVER CACHED — P1's `IEmergencyGate` contract, honoured by
//   forwarding rather than by storing. `mrfw::ui_emergency_active()` is the per-profile seam declared in
//   firmware_commands.h (the OLED TU defines it; every other profile inlines `false`).
struct DeviceEmergencyGate : mrfw::IEmergencyGate {
    bool emergency_active() const override { return mrfw::ui_emergency_active(); }
};
// The `Print` adapter, and it is the WHOLE of the transport difference between USB and BLE: `dispatch` hands USB
// the global `mrcon` and BLE a `LineSink` (fw_main.cpp:549), both `Print`, and every byte below is composed once by
// the pure emitter and written verbatim to whichever arrived. ⛔ There is no second emission path to fork.
struct PresetPrintLines : mrfw::IPresetLines {
    explicit PresetPrintLines(Print& o) : _o(o) {}
    void line(const char* s, size_t n) override { _o.write(reinterpret_cast<const uint8_t*>(s), n); }
    Print& _o;
};
// ★ The retained storage diagnosis (pure — see PresetDiag): what the boot read found, cleared by the first
//   successful durable mutation, which is the owner's ruled repair.
mrfw::PresetDiag s_preset_diag;
}  // namespace
// ★★★★ THE RESIDENT COST IS PAID HERE, AND IT IS THE OWNER-RULED NO-STACK PLACEMENT (spec §5, P1's STACK GATE):
//      `sizeof(PresetCatalog)` = three 372-B records (the live catalog + the two transactional scratch members) +
//      the counters and the two references ≈ 1.15 KB of `.bss`. ⛔ The stack alternative was REFUSED because
//      `begin()` runs from `setup()`, i.e. on the nRF52 Arduino loop task's FIXED 4 KB stack, where this tree has
//      already HARDFAULTED once (`stackhw` down to 72 B). The precedent is fifteen lines up: `static mrnv::PeerBlob
//      s_peers` chose resident-over-stack for exactly this reason, at 1160 B.
// ⓘ Function-local statics, exactly as `join_profile_service()` / `device_cfg_store()` are: constructed on first
//   CALL, so there is no cross-TU initialisation-order question, and the panel (P3) and these verbs share ONE
//   instance and therefore ONE write policy.
mrfw::PresetCatalog& preset_catalog() {
    static DeviceUiPresetStore st;
    static DeviceEmergencyGate gate;
    static mrfw::PresetCatalog cat(st, gate);
    return cat;
}

// setup(): the boot load + the ruled diagnostic. ⛔ A CALL, ⛔ not a decision — `preset_boot_restore` asks
// `begin()`, consults `preset_boot_line` and prints NOTHING for `ok`/`absent` (an ordinary first boot is not a
// fault). Zero writes on every arm, including a corrupt record: the repair is a MUTATION's, never a read's.
void preset_boot_restore_console() {
    PresetPrintLines out(mrcon);
    (void)mrfw::preset_boot_restore(preset_catalog(), s_preset_diag, out);
}

// `ui preset …` — the family, through the ONE dispatch. The pure verb answers `false` for anything that is not this
// grammar, and THEN the usage line prints: a mistyped verb gets the grammar, a well-formed one gets NDJSON. (The
// `handle_joinprofile` shape, U3.)
void handle_ui(const char* args, size_t len, Print& out) {
    PresetPrintLines lines(out);
    if (mrfw::preset_verb(preset_catalog(), s_preset_diag, args, len, lines)) return;
    out.println(F("> ui err usage: ui preset list | ui preset set <emergency|dm1..dm8|channel1..channel8> "
                  "loc=<on|off> \"<text>\" | ui preset clear <dm1..dm8|channel1..channel8> | ui preset reset "
                  "<emergency|dm1..dm8|channel1..channel8|all>"));
}
#endif   // MR_FEAT_OLED — [[B255]]: end of the /mrui catalog binding

// ---- §RADMIN slice 3: the two TARGET STORES' DEVICE BINDINGS (ACCEPT builds only) -----------------------------
// ★★★ EVERYTHING BELOW IS GLUE AND ⛔ NOTHING BELOW IS A DECISION. The four adapters forward to `mrnv`, to
//     `mrrng` and to a `Print`; the three entry points construct STACK services, call the pure verb and return.
//     Every rule — the four storage states, the write policy, the last-owner and self-slot refusals, the grammar,
//     the fingerprint and every emitted byte — lives in the pure headers, where the native suite drives it and
//     `--target=radmin3{id,acl,verbs}` can attack it. (`handle_ui`'s shape, U3.)
// ★★ R-RA-8: ACCEPT = the static + gateway products. On a CLIENT board this block is ABSENT, so a `acl …` or
//    `admin-id …` line falls through `dispatch()`'s `return false` to the caller's UNSUPPORTED-VERB answer —
//    `> parse error` on USB and `{"err":"parse","msg":"unknown_cmd"}` over BLE — exactly as `mobile ` behaves off
//    `MR_FEAT_MOBILE` and `ui ` off `MR_FEAT_OLED`. ⛔ LOUD, NEVER SILENT, and ⛔ no callable stub is left behind.
// ⓘ ⛔ NO RESIDENT STATE, NO CACHE AND NO STATIC I/O BUFFER (design §6.2, Author decision §4.1): the services
//   hold two references each and are constructed per call, so `gateway` RAM must not move. Their transient record
//   scratch is AUTOMATIC and bounded — the stack demand is measured in the slice evidence, ⛔ not assumed.
#if MR_FEAT_RADMIN_ACCEPT
namespace {
const CommandContext* s_active_command_context = nullptr;
constexpr CommandContext kLocalCommandContext{CommandTransport::usb, CommandAuthority::local, true, 0, 0};
struct RemoteTarget final : IRadminTarget {
    bool next_admitted(meshroute::RadminIngressView& v) override { return g_node.radmin_next_admitted(v); }
    bool reserve_transcript(uint8_t s, uint16_t n) override { return g_node.radmin_reserve_transcript(s, n); }
    void transcript_append(uint8_t s, const uint8_t* p, size_t n) override { g_node.radmin_transcript_append(s, p, n); }
    void transcript_complete(uint8_t s, meshroute::RemoteTerminal r) override { g_node.radmin_transcript_complete(s, r); }
    bool tx_queue_full() override { return g_node.tx_queue_full(); }
    meshroute::RadminSend send_next_frame() override { return g_node.radmin_send_frame(); }
    void expire() override { g_node.radmin_service_expire(); }
    bool service_control() override { return g_node.radmin_service_control(); }
    bool next_open(meshroute::RadminOpenView& v) override { return g_node.radmin_next_open(v); }
    bool reserve_open(uint8_t i, uint16_t cap) override { return g_node.radmin_reserve_open(i, cap); }
    void open_append(uint8_t i, const uint8_t* p, size_t n) override { g_node.radmin_open_append(i, p, n); }
    void open_complete(uint8_t i, meshroute::RemoteTerminal r) override { g_node.radmin_open_complete(i, r); }
    meshroute::RadminSend send_open_frame() override { return g_node.radmin_send_open_frame(); }
};
struct TranscriptPrint final : Print {
    explicit TranscriptPrint(IRadminTranscriptSink& s) : sink(s) {}
    size_t write(uint8_t b) override { sink.append(&b, 1); return 1; }
    size_t write(const uint8_t* p, size_t n) override { sink.append(p, n); return n; }
    IRadminTranscriptSink& sink;
};
struct RemoteExec final : IRadminExec {
    RadminExecResult run(const char* p, size_t n, const CommandContext& ctx, IRadminTranscriptSink& sink) override {
        // Handlers' C-string parsers need a terminator beyond the validated byte span.
        // This wiped, bounded copy is per-call, not another resident command buffer.
        if (n > meshroute::console::remote_command_max_bytes) return {DispatchOutcome::refused, RefuseReason::bad_line};
        char line[meshroute::console::remote_command_max_bytes + 1] = {};
        if (n) memcpy(line, p, n);
        TranscriptPrint out(sink);
        const LineExec r = exec_console_line(line, n, LineFormat::text, out, nullptr, 0, ctx);
        crypto_wipe(line, sizeof(line));
        return {r.outcome, r.refuse, r.action_busy};
    }
};
// ⛔ THE TYPED WRAPPERS, ⛔ never `read_slot`/`write_slot` directly: `mrnv::load_admin_id` is the ONE place the
//    slot, the four-state classification and the exact-size policy are spelled.
struct DeviceAdminIdStore : mrfw::IAdminIdStore {
    mrnv::AdminIdRead load(mrnv::AdminIdBlob& out) override { return mrnv::load_admin_id(out); }
    bool save(const mrnv::AdminIdBlob& b) override          { return mrnv::save_admin_id(b); }
};
struct DeviceAclStore : mrfw::IAclStore {
    mrnv::AclRead load(mrnv::AclBlob& out) override { return mrnv::load_acl(out); }
    bool save(const mrnv::AclBlob& b) override      { return mrnv::save_acl(b); }
};
// ★★★ THE CHECKED DRAW, AND THE RETURN VALUE IS THE **ACTUAL** CHECK — ⛔ NEVER AN UNCONDITIONAL `true`.
//     `mrrng::fill` is `void` (`src/device_rng.h:46`) and on the HOST it writes ZEROS by design, so a binding that
//     answered `true` regardless would mint the world-known all-zero root on every dead-RNG device and REPORT
//     SUCCESS. ⚠⚠ What the `true` means is ONLY "32 bytes arrived and they are not all zero": a `void` draw can
//     block and cannot expose every failure mode, so this is ⛔ NOT a device-RNG health guarantee and [[B312]]
//     stays OPEN for the truthful first-RF entropy integration. ⛔ No clock or counter fallback is added.
struct DeviceAdminSeed : mrfw::IAdminSeedSource {
    bool fill(uint8_t out[32]) override {
        mrrng::fill(out, 32);
        return !mrfw::admin_buf_all_zero(out, 32);
    }
};
// ★★★ §RADMIN SLICE 5 — THE NODE BINDING OF THE LIVE SEAM, AND ⛔ NOTHING BELOW IS A DECISION. It forwards a
//     checked entropy draw and a prepared plan to the running `Node`; the ORDER (prepare -> save -> commit) lives
//     in `firmware_admin_identity.h` / `firmware_admin_acl.h`, and the PLAN is built by the pure
//     `firmware_admin_runtime.h` — both of which the native suite drives end to end and
//     `--target=radmin5runtime` can attack. This TU is compiled by neither the native suite nor the simulator
//     (§B115), so anything decided here would be unreachable by every automated gate; its cover is
//     `tools/probe_inbox_verbs`' executed controls.
// ⛔ `draw_epoch` RETURNS THE NODE'S OWN CHECKED ANSWER — ⛔ never an unconditional `true`. `Node::admin_draw_epoch`
//    refuses an all-zero draw (`IHal::rand_bytes` is `void`, so that is the strongest honest claim; [[B312]]
//    stays OPEN).
struct DeviceAdminRuntime : mrfw::IAdminRuntime {
    bool draw_epoch(uint64_t& out) override                                 { return g_node.admin_draw_epoch(out); }
    void commit(const meshroute::RemoteSessionInstall& plan) override       { g_node.admin_session_commit(plan); }
    void entropy_failed() override                                          { g_node.admin_session_entropy_failed(); }
};
// The `PresetPrintLines` shape (U3): the sink the caller was HANDED, ⛔ never `mrcon` and ⛔ never a global.
// ⓘ The body carries a trailing marker so `tools/probe_inbox_verbs`' [[B279]]-shaped control can target THIS
//   adapter and not the byte-identical `PresetPrintLines` above it — a sed that hit both would still redden, but
//   the reddening would be attributable to two subsystems instead of one.
struct AdminPrintLines : mrfw::IAdminLines {
    explicit AdminPrintLines(Print& o) : _o(o) {}
    void line(const char* s, size_t n) override { _o.write(reinterpret_cast<const uint8_t*>(s), n); }   // §RADMIN-3 sink
    Print& _o;
};
}  // namespace

// `admin-id show|generate|rotate confirm|reset confirm` — through the ONE dispatch.
static void handle_admin_id(const char* args, size_t len, Print& out) {
    DeviceAdminIdStore store;
    DeviceAdminSeed    seed;
    // §RADMIN slice 5: the live-install seam, constructed PER CALL on this task's stack — ⛔ no resident plan and
    // ⛔ no static image. Its `~AdminLiveInstall` scrubs the administration secret on every path out, including
    // the refusal ones.
    DeviceAdminRuntime   rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AdminIdService svc(store, seed, &live);
    AdminPrintLines lines(out);
    mrfw::admin_id_verb(svc, args, len, lines);
}
// `acl list|add|set|remove|reset` — through the ONE dispatch. Both services are handed over because `acl add`
// requires a SEPARATELY READ administration identity (design §6.4); the pure verb reads it on that path alone.
static void handle_acl(const char* args, size_t len, Print& out) {
    DeviceAdminIdStore id_store;
    DeviceAdminSeed    seed;
    DeviceAclStore     acl_store;
    DeviceAdminRuntime rt;
    mrfw::AdminLiveInstall live(rt);
    // ⛔ ONLY THE ACL SERVICE INSTALLS HERE. The identity service is handed over READ-ONLY (design §6.4: `acl add`
    //    requires a SEPARATELY READ administration root) and must not acquire a seam it would never use on this
    //    path — an `admin-id` mutation is the OTHER verb's, with its own ordering.
    mrfw::AdminIdService id(id_store, seed);
    mrfw::AclService     acl(acl_store, &live);
    AdminPrintLines lines(out);
    mrfw::acl_verb(acl, id, args, len, lines);
}
// ★★★ THE FAMILY'S TOP-LEVEL RECOGNITION, IN A FUNCTION OF ITS OWN — and the function exists for a MEASUREMENT
//     reason, not for tidiness. `tools/gen_command_inventory.py` records `transports` PER SURFACE, and
//     `dispatch()`'s surface is `serial,ble`. An arm written inline there would therefore publish `acl` and
//     `admin-id` as BLE-reachable in the authority table — the exact opposite of R-RA-29, which refuses the whole
//     family over BLE. ⇒ the two arms live in their OWN surface, recorded `serial`-only, reached from `dispatch`
//     and PROVEN so by `reached_from`. That is `help_command`'s shape and its reason (slice 0a), one family over.
// ★ THE LITERALS ARE HERE, INSIDE THE ACCEPT GATE, so the inventory records the family's product gate rather than
//   inheriting an ungated header's "—". `mrfw::admin_primary_is` is the SAME boundary predicate
//   `mrfw::admin_verb_owns` — the BLE guard's condition — evaluates, so the router and the guard cannot drift.
static bool admin_router_arm(const char* line, size_t len, Print& out) {
    if (mrfw::admin_primary_is(line, len, "admin-id")) { handle_admin_id(line + 8, len - 8, out); return true; }
    if (mrfw::admin_primary_is(line, len, "acl"))      { handle_acl(line + 3, len - 3, out); return true; }
    return false;
}
// setup(): the READ-ONLY boot report. ⛔ ZERO writes, ⛔ zero draws, ⛔ no auto-generation, ⛔ no key or fingerprint
// byte, and ⛔ nothing installed — there is no live cache in this slice to install into. Same shape and same
// reason as `preset_boot_restore_console()` and `peer_store_restore()`.
void admin_stores_boot_report_console() {
    DeviceAdminIdStore id_store;
    DeviceAdminSeed    seed;
    DeviceAclStore     acl_store;
    mrfw::AdminIdService id(id_store, seed);
    mrfw::AclService     acl(acl_store);
    AdminPrintLines lines(mrcon);
    // ⛔ THE TWO EXISTING PER-RECORD LINES ARE UNCHANGED, and they keep the DETAILED cause (absent / invalid /
    //    io_failed). The one line added below reports only the SESSION's resulting state.
    mrfw::admin_boot_report(id, acl, lines);
    // ★★★ §RADMIN SLICE 5 — THE LIVE INSTALL. Slice 3's boot report said in as many words "there is no live cache
    //     in this slice to install into"; there is now. ⛔ IT STILL WRITES NOTHING, DRAWS NO SEED AND
    //     AUTO-GENERATES NOTHING: it re-reads the two records, and only a VALID root plus a VALID ACL with an
    //     owner installs anything at all. Every other state installs the CLEARED image, so a node can never boot
    //     into a stale live ACL after its record went bad.
    // ⛔ NO KEY, SEED, EPOCH OR FINGERPRINT BYTE IS PRINTED — the line carries a state word and a count.
    mrnv::AdminIdBlob idb{};
    mrfw::SecretWipeGuard<mrnv::AdminIdBlob> g{idb};   // ⚠ carries the SEED on every path out
    const mrfw::AdminIdState id_state = mrfw::admin_id_state_of(mrnv::load_admin_id(idb), idb);
    mrnv::AclBlob aclb{};
    const mrfw::AclState acl_state = acl.read(aclb);   // ⛔ read-only; leaves a VALID EMPTY record on every non-ok arm
    DeviceAdminRuntime rt;
    const mrfw::AdminSessionBoot boot = mrfw::admin_runtime_boot(rt, id_state, idb.seed, acl_state, aclb);
    mrcon.print(F("> admin-session boot state="));
    mrcon.print(mrfw::admin_session_boot_state_name(boot.state));
    mrcon.print(F(" slots="));
    mrcon.println(boot.slots);
}
const CommandContext& active_command_context() {
    return s_active_command_context ? *s_active_command_context : kLocalCommandContext;
}
const CommandContext* CommandContextScope::exchange(const CommandContext* p) {
    const CommandContext* previous = s_active_command_context;
    s_active_command_context = p;
    return previous;
}
void remote_executor_service_once() {
    RemoteTarget target;
    RemoteExec exec;
    radmin_service_once(target, exec);
}
#else
// No mutable context state (and no executor) on CLIENT: local behaviour and board RAM stay unchanged.
const CommandContext& active_command_context() {
    static constexpr CommandContext local{CommandTransport::usb, CommandAuthority::local, true, 0, 0};
    return local;
}
const CommandContext* CommandContextScope::exchange(const CommandContext*) { return nullptr; }
#endif   // MR_FEAT_RADMIN_ACCEPT

// ---- §RADMIN slice 4: the two CONTROLLER STORES' DEVICE BINDINGS (CLIENT builds only) --------------------------
// ★★★ EVERYTHING BELOW IS GLUE AND ⛔ NOTHING BELOW IS A DECISION — the ACCEPT block's rule, one product role over.
//     The adapters forward to `mrnv`, to `mrrng` and to a `Print`; the entry points construct STACK services, call
//     the pure verb and return. Every rule — the four storage states, the write policy, the duplicate/in-use
//     refusals, the grammar, the fingerprint, the pagination and every emitted byte — lives in the pure headers,
//     where the native suite drives it and `--target=radmin4{key,targets,verbs}` can attack it.
// ★★ R-RA-8's other half: CLIENT = the four mobile products. On an ACCEPT board this block is ABSENT, so an
//    `admin-key …` or `admin-target …` line falls through `dispatch()`'s `return false` to the caller's
//    UNSUPPORTED-VERB answer — `> parse error` on USB and `{"err":"parse","msg":"unknown_cmd"}` over BLE.
//    ⛔ LOUD, NEVER SILENT, and ⛔ no callable stub is left behind.
#if MR_FEAT_RADMIN_CLIENT
namespace {
// ⛔ THE TYPED WRAPPERS, ⛔ never `read_slot`/`write_slot` directly.
struct DeviceMgmtKeyStore : mrfw::IMgmtKeyStore {
    mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob& out) override { return mrnv::load_mgmt_keys(out); }
    bool save(const mrnv::MgmtKeyBlob& b) override          { return mrnv::save_mgmt_keys(b); }
};
struct DeviceTargetStore : mrfw::ITargetStore {
    mrnv::TargetRead load(mrnv::TargetBlob& out) override { return mrnv::load_targets(out); }
    bool save(const mrnv::TargetBlob& b) override         { return mrnv::save_targets(b); }
};
// ★★★ THE CHECKED DRAW, AND THE RETURN VALUE IS THE **ACTUAL** CHECK — ⛔ NEVER AN UNCONDITIONAL `true`. Same
//     binding shape and same reason as the ACCEPT half's `DeviceAdminSeed`: `mrrng::fill` is `void` and writes
//     ZEROS on the HOST, so a binding that answered `true` regardless would mint the world-known all-zero master
//     key on every dead-RNG controller and REPORT SUCCESS. ⚠⚠ The `true` means ONLY "32 bytes arrived and they are
//     not all zero" — ⛔ NOT a device-RNG health guarantee. [[B312]] stays OPEN. ⛔ No clock or counter fallback.
struct DeviceMgmtKeySeed : mrfw::IAdminSeedSource {
    bool fill(uint8_t out[32]) override {
        mrrng::fill(out, 32);
        return !mrfw::admin_buf_all_zero(out, 32);
    }
};
// Controller stores cannot invalidate an identity or route while a session, request or ACK owns it.
struct DeviceMgmtKeyUse : mrfw::IMgmtKeyUse {
    bool slot_in_use(uint8_t slot) const override { return meshroute::remote_client_key_in_use(g_node.remote_client(), slot); }
    bool any_in_use() const override {
        for (uint8_t i=0; i<10; ++i) if (slot_in_use(i)) return true;
        return false;
    }
};
struct DeviceTargetUse : mrfw::ITargetUse {
    bool slot_in_use(uint8_t slot) const override { return meshroute::remote_client_target_in_use(g_node.remote_client(), slot); }
    bool any_in_use() const override {
        for (uint8_t i=0; i<32; ++i) if (slot_in_use(i)) return true;
        return false;
    }
};
struct DeviceClientRemoteDebt : mrfw::IClientRemoteDebt {
    bool busy() const override { return meshroute::remote_client_busy(g_node.remote_client()); }
};

static bool client_entropy(void*, uint8_t* out, size_t n) { return mrrng::fill_checked(out,n); }
static uint32_t client_console_drops(void*) { return mrcon.dropped_lines(); }
// The `AdminPrintLines` shape (U3): the sink the caller was HANDED, ⛔ never `mrcon` and ⛔ never a global.
// ⓘ The body carries its own trailing marker so `tools/probe_inbox_verbs`' [[B279]]-shaped control can target THIS
//   adapter and not the byte-identical ACCEPT one above it.
struct AdminClientPrintLines : mrfw::IAdminLines {
    explicit AdminClientPrintLines(Print& o) : _o(o) {}
    void line(const char* s, size_t n) override { _o.write(reinterpret_cast<const uint8_t*>(s), n); }   // §RADMIN-4 sink
    Print& _o;
};
}  // namespace

// ★★★★ THE ONE RESIDENT COST OF SLICE 4, AND IT IS THE OWNER-RULED NO-STACK PLACEMENT (design §6.2, brief §4.1):
//      2056 bytes of `.bss` for the PUBLIC target book, used as IO/CANDIDATE SCRATCH and ⛔ NEVER as a live
//      authority cache — every service entry point RELOADS and RECLASSIFIES into it, and a failing mutation
//      restores the row it touched before returning.
//      ⛔ THE STACK ALTERNATIVE WAS REFUSED for `s_peers`' measured reason, twelve hundred lines up:
//      `admin_client_stores_boot_report_console()` runs from `setup()`, i.e. on the nRF52 Arduino loop task's
//      FIXED 4 KB stack (LOOP_STACK_SZ = 256*4, not overridable), where this tree has already HARDFAULTED once
//      (`stackhw` down to 72 B). 2056 B is HALF that stack.
//      ★ SHARING IT BETWEEN THE BOOT REPORT AND THE CONSOLE IS SAFE FOR `s_peers`' EXACT REASON, and it is the
//      same proof: `setup()` returns before `loop()` lazily creates `g_mesh_task`, and everything after that is
//      single-threaded cooperative on that one task ⇒ the two users CANNOT overlap.
//      ⛔ THE KEYRING IS **NOT** HERE: `/mrmkeys` holds SECRETS, so its 368-byte record stays a guarded stack
//      transient inside the service and is wiped on every exit. A resident copy of ten master seeds is exactly
//      what design §6.2's "wipes transient expanded secret material" forbids.
static mrnv::TargetBlob s_targets;

static void handle_remote_client(const char* line, size_t len, Print& out, CommandTransport transport) {
    DeviceMgmtKeyStore key_store;
    DeviceMgmtKeySeed seed;
    DeviceMgmtKeyUse key_use;
    DeviceTargetStore target_store;
    DeviceTargetUse target_use;
    MgmtKeyService keys(key_store,seed,key_use,g_identity.ed_pub);
    TargetService targets(target_store,target_use);
    meshroute::NodeRadminClientCarrier carrier(g_node);
    const auto local=transport==CommandTransport::ble ? meshroute::RemoteLocalTransport::ble : meshroute::RemoteLocalTransport::usb;
    RemoteClientDelivery delivery(out, local==meshroute::RemoteLocalTransport::ble ? &out : nullptr,
                                  local==meshroute::RemoteLocalTransport::ble,client_console_drops);
    RemoteClientServices services{g_node.remote_client(),g_identity,keys,targets,s_targets,carrier,
        client_entropy,nullptr,static_cast<uint32_t>(g_hal.now()),g_node.remote_client_correlation_free(),1};
    remote_client_command(services,line,len,local,out,delivery);
    g_node.remote_client_arm();
}
void remote_client_service_once(Print& usb, Print* ble, bool ble_connected) {
    meshroute::NodeRadminClientCarrier carrier(g_node);
    RemoteClientDelivery delivery(usb,ble,ble_connected,client_console_drops);
    meshroute::remote_client_service(g_node.remote_client(),static_cast<uint32_t>(g_hal.now()),
                                    client_entropy,nullptr,carrier,delivery);
    g_node.remote_client_arm();
}


// `admin-key list|show|generate|import|export|remove|reset` — through the ONE dispatch.
static void handle_admin_key(const char* args, size_t len, Print& out) {
    DeviceMgmtKeyStore store;
    DeviceMgmtKeySeed  seed;
    DeviceMgmtKeyUse   use;
    // ⛔ `g_identity.ed_pub` is READ, never written: `self` is the node's own messaging identity and this family
    //    must not rotate it (`regen` owns that, and warns about it).
    mrfw::MgmtKeyService svc(store, seed, use, g_identity.ed_pub);
    AdminClientPrintLines lines(out);
    mrfw::admin_key_verb(svc, args, len, lines);
}
// `admin-target list|show|add|set|remove|reset` — through the ONE dispatch, over the ONE resident scratch.
static void handle_admin_target(const char* args, size_t len, Print& out) {
    DeviceTargetStore store;
    DeviceTargetUse   use;
    mrfw::TargetService svc(store, use);
    AdminClientPrintLines lines(out);
    mrfw::admin_target_verb(svc, s_targets, args, len, lines);
}
// ★★★ THE FAMILY'S TOP-LEVEL RECOGNITION, IN A FUNCTION OF ITS OWN — and for the ACCEPT half's MEASUREMENT
//     reason, not for tidiness: `tools/gen_command_inventory.py` records `transports` PER SURFACE and
//     `dispatch()`'s surface is `serial,ble`. Written inline there, the two BARE family names would be published
//     as BLE-reachable, which R-RA-30 refuses — only the `list`/`show` SUB-VERBS cross secured BLE, and the
//     inventory records that per row.
// ★ THE LITERALS ARE HERE, INSIDE THE CLIENT GATE, so the inventory records the family's product gate.
//   `mrfw::admin_primary_is` is the SAME boundary predicate `mrfw::admin_client_verb_owns` — the BLE guard's
//   condition — evaluates, so the router and the guard cannot drift.
static bool admin_client_router_arm(const char* line, size_t len, Print& out) {
    if (mrfw::admin_primary_is(line, len, "admin-key"))    { handle_admin_key(line + 9, len - 9, out); return true; }
    if (mrfw::admin_primary_is(line, len, "admin-target")) { handle_admin_target(line + 12, len - 12, out); return true; }
    return false;
}
// setup(): the READ-ONLY boot report. ⛔ ZERO writes, ⛔ zero draws, ⛔ no auto-generation, ⛔ no key, seed or
// fingerprint byte. Same shape and same reason as `admin_stores_boot_report_console()` above.
void admin_client_stores_boot_report_console() {
    DeviceMgmtKeyStore key_store;
    DeviceMgmtKeySeed  seed;
    DeviceMgmtKeyUse   key_use;
    DeviceTargetStore  tgt_store;
    DeviceTargetUse    tgt_use;
    mrfw::MgmtKeyService keys(key_store, seed, key_use, g_identity.ed_pub);
    mrfw::TargetService  targets(tgt_store, tgt_use);
    AdminClientPrintLines lines(mrcon);
    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);
}
#endif   // MR_FEAT_RADMIN_CLIENT

// E2E §3: a `peerkey` command -> install the RAM PINNED key (Node::on_command) + mirror it to /mrpeers + the ack.
size_t handle_peerkey(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint8_t* ep = cmd.u.peerkey.ed_pub;
    const uint32_t kh = (uint32_t)ep[0] | ((uint32_t)ep[1] << 8) | ((uint32_t)ep[2] << 16) | ((uint32_t)ep[3] << 24);
    if (g_node.on_command(cmd).code != meshroute::CmdCode::queued)        // false only when the cache is full of pinned keys
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"full\"}\n");
    // §nv-unchecked [5/5] (cont.): peer_store_sync reports its POLICY outcome — this CALLER discards it, preserved
    // deliberately: the `peerkey_set` ack below claims `"pinned":true` even when /mrpeers refused or failed to write.
    // Owner ruling owed. NB the RAM install above must come FIRST — the sync reads the name + confidence back out of
    // the live table, so a QR import inherits any name already learned on air for that hash.
    (void)peer_store_sync(kh);
    return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_set\",\"hash\":%lu,\"pinned\":true}\n", (unsigned long)kh);
}

// ★ §AB2 (spec 2026-07-29 §2.3): a `peername` command -> rename the RAM entry (Node::on_command -> peer_name_set) +
// mirror it to /mrpeers + the SYNCHRONOUS ack. Deliberately the same three-step shape as handle_peerkey above (U3), and
// the persistence is AB1's ONE-LINER: peer_store_sync takes only a hash and reads EVERY field back out of the live
// table, so no caller can persist a name that disagrees with RAM (U2). The RAM write must therefore come FIRST.
// ⚠ The NV outcome is DISCARDED here for the same reason it is in handle_peerkey (§nv-unchecked [5/5]): the ack claims
// success on the strength of the RAM write, which is what the operator asked for and what every read path uses. An
// owner ruling on surfacing NV failures is still owed and covers both verbs together.
// ★ A rename of a PINNED peer SUCCEEDS — peer_name_set touches neither key nor confidence, so peer_key_set's
// "pinned is immutable to an on-air set" rule is not in play (see node.h). peer_store_sync then re-mirrors the record at
// its UNCHANGED confidence, so a pinned peer stays pinned in /mrpeers with the new name.
size_t handle_peername(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint32_t kh = cmd.u.peername.key_hash32;
    const meshroute::CmdResult r = g_node.on_command(cmd);
    if (r.code != meshroute::CmdCode::queued)
        return meshroute::console::write_peer_name_err(out, cap,
                   r.code == meshroute::CmdCode::err_too_large ? "too_long" : "unknown_hash");
    (void)peer_store_sync(kh);
    return meshroute::console::write_peer_name_set(out, cap, kh,
               reinterpret_cast<const char*>(cmd.body), cmd.body_len);
}

// ---- device-console diagnostics (host tool: tools/meshroute_client.py) ---------------------------
// Print the live routing table in the meshroute_client `routes` wire format.
// `route add <dest> <next_hop> <hops> [score_q4]` / `route del <dest>` — manually force / drop a route. A TESTING lever
// to stress the routing algorithms with arbitrary or deliberately-inconsistent routes. `score_q4` is the same Q4-dB
// value the `routes` dump shows; rt_merge competes the injected candidate like any learned one (high score -> primary).
static void handle_route_cmd(const char* args, Print& out) {
    while (*args == ' ') ++args;
    char* e;
    if (!strncmp(args, "add", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const char* p = args + 3;
        const long dest = strtol(p, &e, 10); const bool d1 = (e != p); p = e;
        const long next = strtol(p, &e, 10); const bool d2 = (e != p); p = e;
        const long hops = strtol(p, &e, 10); const bool d3 = (e != p); p = e;
        long score = strtol(p, &e, 10); if (e == p) score = 160;          // optional; default ~10 dB (Q4) = a sticky primary
        if (d1 && d2 && d3 && dest >= 1 && dest <= 254 && next >= 1 && next <= 254 && hops >= 1 && hops <= 255) {
            const bool ok = g_node.route_inject((uint8_t)dest, (uint8_t)next, (uint8_t)hops, (int16_t)score);
            out.print(F("> route add dest=")); out.print(dest); out.print(F(" via=")); out.print(next);
            out.print(F(" hops="));            out.print(hops); out.print(F(" score=")); out.print(score);
            out.println(ok ? F(" — installed (see `routes`)") : F(" — REJECTED (better candidates hold the slots)"));
            return;
        }
    } else if (!strncmp(args, "del", 3) && (args[3] == ' ' || args[3] == '\0')) {
        const long dest = strtol(args + 3, &e, 10);
        if (e != args + 3 && dest >= 1 && dest <= 254) {
            const bool ok = g_node.route_remove((uint8_t)dest);
            out.print(F("> route del dest=")); out.print(dest); out.println(ok ? F(" — removed") : F(" — not found"));
            return;
        }
    }
    out.println(F("> route err usage: route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
}

static void dump_routes(Print& out) {
    const uint64_t now = g_hal.now();
    //out.print(F("[routes] n=")); out.println(g_node.rt_count());
    if (!g_node.rt_count()) out.println(F("empty"));
    for (uint8_t i = 0; i < g_node.rt_count(); ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];           // candidates[0] = the primary next-hop
        out.print(F("[route] dest="));   out.print(e.dest);
        out.print(F(" next="));          out.print(c.next_hop);
        out.print(F(" hops="));          out.print(c.hops);
        out.print(F(" score="));         out.print(c.score);
        out.print(F(" pen="));           out.print(g_node.peer_penalty_q4(c.next_hop));   // liveness penalty on this next-hop (effective = score - pen)
        out.print(F(" gw="));            out.print(c.is_gateway ? 1 : 0);
        out.print(F(" leaf="));          out.print(c.learned_leaf);
        out.print(F(" age_ms="));        out.print((uint32_t)(now - c.last_seen_ms));
        out.print(F(" cand="));          out.println(e.n);
        // A gateway route carries unique state: its advertised window schedule (period + per-leaf windows) — known
        // when we've heard the gateway 1-hop. Print it on a continuation line so a node can see when the gw is reachable.
        if (c.is_gateway) {
            const meshroute::GatewaySchedule* gs = g_node.rt_gateway_schedule(e.dest);
            if (gs && gs->valid) {
                out.print(F("[route]   gw_sched period="));  out.print(gs->period_ms);
                out.print(F("ms heard_ms="));                out.print((uint32_t)(now - gs->heard_ms));
                out.print(F(" defer_ms="));                  out.print(g_node.rt_gateway_defer_ms(e.dest));
                out.print(F(" n_rec="));                     out.print(gs->n_rec);
                for (uint8_t r = 0; r < gs->n_rec; ++r) {
                    out.print(F(" [leaf"));   out.print(gs->rec[r].leaf_id);
                    out.print(F(" win"));     out.print(gs->rec[r].window_ms);
                    out.print(F("@"));        out.print(gs->rec[r].offset_ms);
                    out.print(F("]"));
                }
                out.println();
            } else {
                out.println(F("[route]   gw_sched unknown (not heard 1-hop)"));
            }
        }
    }
    //out.println(F("[routes] end"));
    // §mobile 6.2: the TEAM-plane routing table (_rt_team) — same RtEntry fields as the static plane; team routes are never
    // gateways (no gw schedule). Printed for ANY team member (team_id!=0) EVEN WHEN EMPTY (n=0) so the operator can tell a
    // team member with no peers-yet (a PHY mismatch / just-joined) from a non-team node — a static/non-team node prints nothing.
#if MR_FEAT_TEAM   // §featuresplit: the diagnostic is compiled out on a static-only build (no _rt_team)
    if (g_node.config().team_id != 0) {
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)g_node.config().team_id);
        out.print(F("team_id=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(g_node.team_local_id());
        out.print(F(" n=")); out.println(g_node.rt_team_count());
        for (uint8_t i = 0; i < g_node.rt_team_count(); ++i) {
            const meshroute::RtEntry& e = g_node.rt_team_at(i);
            const meshroute::RtCandidate& c = e.candidates[0];       // candidates[0] = the primary next-hop
            out.print(F("[team-route] dest="));   out.print(e.dest);
            out.print(F(" next="));                out.print(c.next_hop);
            out.print(F(" hops="));                out.print(c.hops);
            out.print(F(" score="));               out.print(c.score);
            out.print(F(" leaf="));                out.print(c.learned_leaf);
            out.print(F(" age_ms="));              out.print((uint32_t)(now - c.last_seen_ms));
            out.print(F(" cand="));                out.println(e.n);
        }

    }
#endif   // MR_FEAT_TEAM
    // §mobile: the hosted-mobile registry (this node is a HOME) — mobiles reachable by a DIRECT last-mile from here.
    // mobile_reg_count()==0 on a non-host / static build -> nothing printed.
    if (g_node.mobile_reg_count()) {
        out.print(F("hosted-mobiles n=")); out.println(g_node.mobile_reg_count());
        for (uint8_t i = 0; i < g_node.mobile_reg_count(); ++i) {
            uint32_t kh = 0; uint8_t lid = 0; bool hpk = false;
            if (!g_node.mobile_reg_at(i, kh, lid, hpk)) continue;
            char hx[9]; snprintf(hx, sizeof hx, "%08lX", (unsigned long)kh);
            out.print(F("[hosted-mobile] hash=0x")); out.print(hx);
            out.print(F(" local_id="));              out.print(lid);
            out.print(F(" pubkey="));                out.println(hpk ? F("yes") : F("no"));
        }
    }
}

// allowed_sf_bitmap -> "7,12" CSV (SF index = bit position). 0 = unconfigured.
// ★ §B95: takes its SINK. It used to write to the global `mrcon` unconditionally, so `dump_cfg(Print& out)` sent the
// whole row to `out` and the sf_list values to USB — the row lost its field on any other sink, and on USB the two
// writers interleaved. A formatter must never reach past the sink it was given (invariant 5). No global-writing
// overload is kept: the compiler must break every future call site that forgets the sink.
void print_sf_list(Print& out, uint16_t bitmap) {
    bool first = true;
    for (uint8_t sf = 5; sf <= 12; ++sf)
        if (bitmap & (1u << sf)) { if (!first) out.print(','); out.print(sf); first = false; }
    if (first) out.print('-');
}

static void dump_cfg(Print& out) {
    const meshroute::NodeConfig& c = g_node.config();
    // Grouped, one section per line — readable on a raw serial monitor. Keys match the `cfg set <key>` names.
    out.print(F("node_id="));     out.println(g_node.node_id());
    const double show_freq = (c.is_mobile && c.layers[0].freq_mhz > 0.0) ? c.layers[0].freq_mhz : g_freq_mhz;   // §mobile: a retune stores the live freq in layers[0]; g_freq_mhz stays the boot/global
    out.print(F("  radio : freq="));    out.print(show_freq, 4);
    out.print(F(" routing_sf="));       out.print(c.routing_sf);
    out.print(F(" sf_list="));          print_sf_list(out, c.allowed_sf_bitmap);
    out.print(F(" bw="));               out.print(g_node.active_bw_hz() / 1000.0, 2); out.print(F(" kHz"));   // W2b: kHz, matching `cfg set bw`. The ACTIVE leaf's BW (a gateway alternates per window; single-layer == the global)
    out.print(F(" cr="));               out.print((int)g_node.active_cr());
    out.print(F(" tx_power="));         out.println((int)g_tx_power);
    out.print(F("  proto : duty="));    out.print(c.duty_cycle * 100.0, 2); out.print('%');   // W2b: percent, matching `cfg set duty`
    out.print(F(" beacon_ms="));        out.print(c.beacon_period_ms);
    out.print(F(" hop_cap="));          out.print(c.dv_hop_cap);
    out.print(F(" team_hop_cap="));     out.print(c.team_hop_cap);   // §team-parity T3: printed beside its static twin (unconditionally, like hop_cap) so the two radii are readable as a PAIR — they still DIFFER on the DV leg (node_beacon.cpp:884), which is exactly why an operator needs to see both
    out.print(F(" lbt="));              out.print(c.lbt_enabled ? 1 : 0);
    out.print(F(" nav="));              out.print(c.nav_enabled ? 1 : 0);
    out.print(F(" intra_relay="));      out.print(c.intra_layer_relay ? 1 : 0);   // §gateway: relay same-leaf DMs? (default OFF)
    out.print(F(" host_mobiles="));     out.print(c.host_mobiles ? 1 : 0);        // §mobile 2a: accept/host mobiles? (default ON)
    out.print(F(" nav_ignore="));       out.println(c.nav_ignore_rts ? 1 : 0);
    out.print(F("  aspam : active_fraction=")); out.print(c.channel_active_fraction, 3);   // anti-spam v2 promoted knobs (in the config_hash)
    out.print(F(" ch_min_ms="));        out.print(c.channel_min_interval_ms);
    out.print(F(" dm_min_ms="));        out.println(c.dm_min_interval_ms);
    out.print(F("  layer : "));                                            // R6.3 §3: the full 1..255 layer id (NV-side) + its wire leaf nibble (clash check)
    { mrnv::Blob lb{}; if (mrnv::load(lb) && lb.layer0_id) { out.print(F("layer=")); out.print(lb.layer0_id); out.print(F(" ")); } }
    out.print(F("leaf="));              out.print(c.leaf_id);            // leaf = layer & 0x0F (the byte-0 wire filter)
    out.print(F(" gateway="));          out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gateway_only="));     out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile="));           out.println(c.is_mobile ? 1 : 0);
    if (c.team_id) {   // §mobile 6.1/6.4: team plane — the team scope + OUR id on it. team_local_id 0 = not team-DAD'd yet
        char tx[9]; snprintf(tx, sizeof tx, "%08lX", (unsigned long)c.team_id);
        const uint8_t tid = g_node.team_local_id();
        out.print(F(" team=0x")); out.print(tx);
        out.print(F(" team_local_id=")); out.print(tid);
        // §6.4 Option X: off-grid the team-DAD'd id IS node_id (the mobile link-layer carries team DMs). Flag the plane state
        // so a bench operator can tell an off-grid member (node_id==team id) from a dual one (static node_id + separate team id).
        if (tid == 0)                       out.print(F(" (team-DAD pending)"));
        else if (g_node.node_id() == tid)   out.print(F(" (off-grid: node_id==team id)"));
        else                                out.print(F(" (dual: static node_id + team id)"));
        // §team-ch-key (T-K1): the CONTENT-key lock state (spec §2.5 "surface the lock state per team"). Boolean
        // ONLY — the pair itself is a SECRET and is deliberately NOT printable here. It is also the ONLY on-metal
        // observable that `team new` actually minted, since the corpus cannot reach the console (see the COVERAGE
        // note in the slice report). An EXPORT for the T-K4 QR is a separate, deliberately-unbuilt decision.
        out.print(F(" team_ch_key=")); out.print(g_node.team_channel_key_present() ? 1 : 0);
        out.println();
    }
    if (c.is_mobile) {                                                        // §mobile: registration state (bench diagnostic) — did we register, and with whom?
        // ★★ §B214 (BUG FIX 2026-08-18) — THE LABEL IS THE FSM STATE, NOT "IS A HOME ID SET".
        // ⛔ This read `if (mobile_home_id()) REGISTERED else "UNREGISTERED (scanning)"`, wrong in BOTH directions and
        //   metal-confirmed 2026-08-18:
        //   · it said "(scanning)" whenever there was no home — INCLUDING `dormant`, where nothing is scheduled. The
        //     same capture's `mobile status` read attachment=dormant / home_desired=false / retry_window_ms=0. It made
        //     the operator ask how often the node would probe; the answer was "never". ★ A display-shaped field must
        //     not ASSERT AN ACTIVITY — the mirror of [[B210]], whose `team-DAD` line asserted airtime that never flew.
        //   · WORSE, a FALSE POSITIVE: `claiming` already holds a PROVISIONAL home id, so it printed
        //     `REGISTERED home=<id>` BEFORE roster confirmation.
        // ★ A `switch`, never an if-chain (node.h:550 — `-Wswitch` cannot see if-chains, and three enum->string bugs in
        //   this tree came from exactly that), and the names come from the ONE formatter `attach_state_name` (U1).
        // ⓘ The enum (node.h:534) and the formatter (:553) sit OUTSIDE `#if MR_FEAT_MOBILE` (:572), so this compiles on
        //   a stripped build too; the accessor's `#else` stub answers `dormant` (:623).
        const uint8_t h = g_node.mobile_home_id();
        const meshroute::Node::MobileAttachState as = g_node.mobile_attach_state();
        out.print(F("  mobile-reg: "));
        switch (as) {
            case meshroute::Node::MobileAttachState::attached:
                // ⛔ `attached` with NO home id is an INCONSISTENCY and must be VISIBLE: falling through to
                //    "unregistered" is precisely how a broken attachment would hide.
                if (h) { out.print(F("REGISTERED home=")); out.println(h); }
                else     out.println(F("INCONSISTENT: attached with no home id"));
                break;
            case meshroute::Node::MobileAttachState::dormant:
            case meshroute::Node::MobileAttachState::seeking:
            case meshroute::Node::MobileAttachState::claiming:
            case meshroute::Node::MobileAttachState::recovering:
                out.print(F("UNREGISTERED (")); out.print(meshroute::Node::attach_state_name(as)); out.println(')');
                break;
        }
    }
    // ★★★ §MH-S5 §10 / [[B154]] — the per-row hosting view: each row as DIRECT or REDIRECT, plus its age.
    // §10 asked for it and the FIELD LEDGER reassigned it here; before this the line printed only a COUNT, so a
    // bench operator could not tell a live hosted mobile from a redirect breadcrumb, nor see how close either was
    // to §9.1's 25-minute expiry. ⓘ Ages are printed in whole seconds — the boundary is 1500 s away, so ms
    // resolution would be noise on a console line, and §9's decisions are all minute-scale.
    if (g_node.mobile_reg_count()) {
        out.print(F("  hosting=")); out.print(g_node.mobile_reg_count()); out.println(F(" mobile(s)"));
        uint32_t kh = 0; uint8_t lid = 0, rh = 0; bool rd = false; uint64_t age = 0;
        for (uint8_t i = 0; i < g_node.mobile_reg_count(); ++i) {
            if (!g_node.host_mobile_row(i, kh, lid, rd, rh, age)) break;
            out.print(F("    m[")); out.print(i); out.print(F("] hash=0x")); out.print(kh, HEX);
            out.print(F(" local=")); out.print(lid);
            if (rd) { out.print(F(" REDIRECT->")); out.print(rh); } else out.print(F(" DIRECT"));
            out.print(F(" age=")); out.print(static_cast<uint32_t>(age / 1000)); out.print(F("s/"));
            out.print(static_cast<uint32_t>(meshroute::protocol::mobile_liveness_ms / 1000)); out.println(F("s"));
        }
    }
    out.print(F("  member: lineage_id=")); out.print(c.lineage_id);          // R6.1 leaf-config membership: 0 = UNMANAGED. A managed leaf (lineage!=0) only routes same-lineage peers -> a lineage-0 gateway is silently dropped (node_beacon.cpp:462). This is the field to compare across nodes.
    out.print(F(" config_epoch="));     out.print(c.config_epoch);
    if (c.leaf_name_len) { out.print(F(" leaf_name=\"")); for (uint8_t i = 0; i < c.leaf_name_len; ++i) out.print(c.leaf_name[i]); out.print(F("\"")); }
    out.println();
    // ★ §UI-10/11 P2 — THE `/mrui` CATALOG'S STATUS SURFACE (design §3.2.3: a corrupt record *"emits a visible
    // boot/status warning"*). Two lines, and the split is deliberate:
    //   · the FACTS are live reads of the catalog — the generation a companion compares for equality, the two
    //     active counts `ui_presets_end` publishes, and the SAVE count, which is the flash-wear guard measured
    //     rather than argued;
    //   · the WARNING is `preset_boot_line`'s exact owner-approved text (U1 — ⛔ never re-worded here), printed
    //     only for the two fault states and only while it is still TRUE: a successful mutation rewrites the
    //     complete canonical record, so `PresetDiag` clears the `invalid` diagnosis on the first `ok` verdict.
    // ⓘ [[B255]]: gated with the catalog it reads. Off `MR_FEAT_OLED` there is no catalog to report a generation,
    //   an active count or a save count FOR, and no boot read to have produced a diagnosis — so the two lines are
    //   ABSENT rather than reporting zeros about a store this build never touches (C2: a zero would be a lie).
#if MR_FEAT_OLED   // ★ [[B255]] the `status` surface
    {
        const mrfw::PresetCatalog& pc = preset_catalog();
        out.print(F("  presets: generation="));   out.print(pc.generation());
        out.print(F(" dm_active="));              out.print(pc.enabled_count(mrfw::PresetKind::dm));
        out.print(F(" channel_active="));         out.print(pc.enabled_count(mrfw::PresetKind::channel));
        out.print(F(" saves="));                  out.println(pc.saves());
        const char* pl = s_preset_diag.line();
        if (pl) out.println(pl);
    }
#endif   // MR_FEAT_OLED
    out.print(F("  ble   : ble_mode=")); out.print(g_ble_mode == 0 ? F("off") : g_ble_mode == 1 ? F("on") : F("periodic"));
    out.print(F(" ble_period="));       out.print(g_ble_period_min);
    out.print(F(" ble_pin="));          out.println(g_ble_pin);
    const auto activation = remote_activation_resolve(g_remote_action_activation_ms, remote_activation_live_inputs());
    out.print(F("  radmin: activation_ms=")); out.print(activation.effective_ms);
    out.print(F(" state=")); out.print(activation_state_name(activation.state));
    out.print(F(" cfg=")); out.print(g_remote_action_activation_ms);
    out.print(F(" floor=")); out.print(activation.floor_ms);
    out.print(F(" default=")); out.print(activation.default_ms);
    out.print(F(" ceiling=")); out.println(meshroute::remote_action_activation_max_ms);
    // Arduino Print formats floats via its own dtostrf (NOT newlib printf), so 7-decimal degrees print fine.
    // §loc-per-send (2026-07-31): `loc_dm=` is GONE from this dump — the toggle it reported no longer exists. Location is
    // a PER-SEND `-l` flag on `send`, so there is no persistent state to show; lat/lon below are still the node's fix.
    out.print(F("  loc   : e2e_dm="));  out.print(c.e2e_dm ? 1 : 0);
    out.print(F(" team_channel_crypt=")); out.print(c.team_channel_crypt ? 1 : 0);   // §chan-crypt CL2a: seal a `-t` post by default when a key is held. Read together with `team_ch_key=` below: key+crypt = encrypted, key+!crypt = opted out, !key = cannot encrypt at all
    out.print(F(" intro_attach="));     out.print(c.intro_attach ? 1 : 0);
    out.print(F(" lat="));              out.print(g_lat_e7 / 1e7, 7);
    out.print(F(" lon="));              out.println(g_lon_e7 / 1e7, 7);
    // Dual-layer gateway: an ADDITIVE second line per leaf (single-layer dump above is unchanged). Prints each
    // leaf's node_id/layer_id/routing_sf + the (possibly on_init-derived) window_ms/offset of the active config.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[cfg.layer")); out.print(li);
            out.print(F("] node_id="));    out.print(L.node_id);
            out.print(F(" layer_id="));    out.print(L.layer_id);
            out.print(F(" routing_sf="));  out.print(L.routing_sf);
            out.print(F(" sf_list="));     print_sf_list(out, L.allowed_sf_bitmap);
            out.print(F(" bw="));          out.print((L.bw_hz > 0 ? L.bw_hz : c.radio_bw_hz) / 1000.0, 2); out.print(F(" kHz"));   // W2b: kHz, matching `cfg set l1_bw`. Per-layer BW (0 = inherit -> the effective/global)
            out.print(F(" cr="));          out.print((int)(L.cr > 0 ? L.cr : c.radio_cr));
            out.print(F(" beacon_ms="));   out.print(L.beacon_period_ms);
            out.print(F(" window_period_ms=")); out.print(L.window_period_ms);
            out.print(F(" window_ms="));   out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

// ★ §B61 (2026-08-03): the LAST arm is an #error, not a fallback. This is the ONLY board-discriminating code in the
// firmware (3 of the 26 board-macro conditional sites; the other 23 are chip-family OR-chains that merely name a
// BOARD_* as an alias), and its value is read by `version` / print_banner — the one line a bench operator trusts to
// tell two boards apart. The old `#else return "native"` was UNREACHABLE in all eleven envs (native does not compile
// this TU: platformio.ini:73 + test_build_src=no), so it was never a host arm — it was a silent fallback that would
// have let a NEW board env with a forgotten -DBOARD_* link, boot, and LIE about its own identity. Per C2 that must
// fail loud instead. `MESHROUTE_NATIVE` gets its own explicit arm so a future host build that DOES compile this TU
// has a legitimate answer rather than tripping the #error.
const char* board_name() {
#if defined(BOARD_XIAO_WIO_SX1262)
    return "xiao_nrf52";
#elif defined(BOARD_XIAO_ESP32S3)
    return "xiao_esp32s3";
#elif defined(BOARD_HELTEC_V3)
    return "heltec_v3";
#elif defined(BOARD_HELTEC_V4)
    return "heltec_v4";
#elif defined(MESHROUTE_NATIVE)
    return "native";
#else
#error "No supported BOARD_* or MESHROUTE_NATIVE target selected"
#endif
}
// The `version` banner — build stamp + git rev + board + the last reset reason (ON DEMAND, no reset). Refactored
// from the old boot prints so setup() and the `version` command share one source. (spec 2026-06-24 §6)
void print_banner(Print& out) {
    char buf[160];
    mrfault::format_version_banner(buf, sizeof buf, kBuildStamp, kGitRevision, board_name());
    out.println(buf);
    mrfault::format_last_reset(g_last_reset_valid ? &g_last_reset : nullptr, buf, sizeof buf);
    out.println(buf);
}

// Board/RF bring-up truth, deliberately USB-text only for V4-3. Hardware readiness and requested-configuration
// validity remain separate; rfok is their conjunction. Never infer a board revision from the build name: the FEM
// driver's runtime classification is the authority.
void print_rf_diagnostics(Print& out) {
    const meshroute::BoardRfDrive drive = g_iradio.output_drive_for(g_tx_power);
    const bool rfcfg = g_iradio.rf_config_valid(g_tx_power);
    out.print(F("fem="));        out.print(meshroute::board_rf_kind_name(g_iradio.board_rf_kind()));
    out.print(F(" lna="));       out.print(meshroute::board_rf_lna_state_name(g_iradio.board_rf_lna_state()));
    out.print(F(" radiohw="));   out.print(g_radio_ok ? 1 : 0);
    out.print(F(" rfcfg="));     out.print(rfcfg ? 1 : 0);
    out.print(F(" rfok="));      out.print((g_radio_ok && rfcfg) ? 1 : 0);
    out.print(F(" rfmodefail=")); out.print(g_iradio.rf_mode_failures());
    out.print(F(" rfbandfail=")); out.print(g_iradio.rf_band_failures());
    out.print(F(" rfout="));     out.print((int)g_tx_power);
    out.print(F(" rfchip="));
    if (drive.valid) out.print((int)drive.chip_dbm);
    else             out.print('-');
}

static void dump_status(Print& out) {
    out.print(F("uptime_ms="));  out.print((uint32_t)g_hal.now());
    out.print(F(" rx="));                 out.print(g_rx_count);
    out.print(F(" tx="));                 out.print(g_iradio.tx_count());
    out.print(F(" isr="));                out.print(g_iradio.isr_count());   // DIO1 edges — isr=0 ⇒ pin/mask; isr>0 & rx=0 ⇒ drain/re-arm
    out.print(F(" rxbad="));              out.print(g_iradio.rxbad_count());  // failed-decode RX (CRC storm) — a clean counter delta (per-event print is `debug on`-gated)
    out.print(F(" rxarm="));              out.print(g_iradio.rx_arm_failures());  // L5: startReceive() re-arm failures — non-zero = an SPI glitch left RX transiently un-armed (was silent before)
    out.print(F(" txq="));                out.print(g_hal.txq_depth());      // async-TX queue depth (should idle at 0)
    out.print(F(" txdrop="));             out.print(g_hal.txq_drops());      // outbound-queue overflow drops (should stay 0)
    out.print(F(" txfail="));             out.print(g_hal.tx_failed_arms()); // admitted frames whose radio arm failed
    out.print(F(" txoutdrop="));          out.print(g_hal.tx_outcome_drops()); // completion reports lost to ring overflow
    out.print(' ');                        print_rf_diagnostics(out);
    // ★★★ §MH-S4b §10 — the two HOST-side OFFER admission counters, beside `txdrop` because §10 names it as their
    // precedent. Both existed since §MH-S2/[[B146]] as native-only accessors; the debt of making them device-visible
    // was explicitly assigned to S4 by the earlier slices and is discharged here. ⓘ Unconditional: a `0` is the
    // reading "this never happened", and omitting it makes that indistinguishable from "never counted".
    out.print(F(" offerfull="));          out.print(g_node.mobile_offer_ring_full_count());   // pending-OFFER ring admissions refused `full` (§5.3.2) — non-zero = mobiles are colliding on the ring
    out.print(F(" offerrej="));           out.print(g_node.mobile_offer_reject_count());      // armed OFFERs OUR OWN transmitter refused (defer ring full / HAL rejection) — a LOCAL fact, never a mobile's fault
    out.print(F(" ctrrefuse="));          out.print(g_node.mobile_ctr_admission_refused_count());   // B251: hosted-mobile counter translation refused before hop ACK (queue/correlation full)
    out.print(F(" txto="));               out.print(g_hal.tx_timeouts());    // TX-watchdog recoveries — a missed TxDone (should stay 0)
    out.print(F(" slept="));              out.print(g_sleep_count);          // idle light-sleep entries that ACTUALLY HALTED — climbs = the gate fires (0 = never sleeps)
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
    // ★★★ §B200 — WHAT WOKE US, AND WHAT REFUSED TO ARM. ⓘ ESP32-only (the same guard as `board_sleep_until`'s
    //   light-sleep branch, which is the only writer): nRF52 uses WFE and has no wake-cause API, so printing these
    //   there would make "never measured" indistinguishable from "never happened" — the ambiguity `offerfull=` above
    //   was written unconditional to AVOID, arrived at from the other side.
    // ★★ `wk_gpio/wk_ext1/wk_tmr` are the attribution bench 23.1(b) could not make: a 1 s sleep cap means the MCU
    //   wakes anyway, so only a per-cause tally can say the DIO1 edge (or the button) actually delivered the CPU.
    // ⛔ `wkdisarm=` NON-ZERO IS THE SERIOUS ONE: the platform refused to take the level interrupt down after a
    //   sleep, i.e. a running core carried an armed level — [[B200]]'s exact precondition. Sleep is off for the boot.
    out.print(F(" wk_gpio="));            out.print(g_wake_gpio);            // woken by the user button (GPIO)
    out.print(F(" wk_ext1="));            out.print(g_wake_ext1);            // woken by DIO1 RxDone (the radio)
    out.print(F(" wk_tmr="));             out.print(g_wake_timer);           // woken by the ≤1 s deadline cap
    out.print(F(" wkbusy="));             out.print(g_wake_arm_busy);        // sleeps skipped: the button was HELD at the arm (normal)
    out.print(F(" wkarmfail="));          out.print(g_wake_arm_fail);        // the platform refused to ARM -> sleep disabled for the boot
    out.print(F(" wkdisarm="));           out.print(g_wake_disarm_fail);     // ⛔ refused to DISARM -> the storm precondition
    out.print(F(" wksleepfail="));        out.print(g_wake_sleep_fail);      // §R2.1: light sleep REFUSED (reject/too-short) — the CPU never slept, so it is not in `slept=`
#endif
    out.print(F(" sleep="));              out.print(g_force_sleep ? F("forced") : (g_host_present ? F("off-host") : F("auto"))); // policy: auto=headless→sleeps, off-host=awake (host seen), forced=`sleep` cmd
    out.print(F(" lbt="));                out.print(g_node.config().lbt_enabled ? 1 : 0);
    out.print(F(" nf="));                 out.print(g_iradio.noise_floor(), 0); // LBT noise floor (dBm)
    out.print(F(" duty_ms="));            out.print((uint32_t)g_hal.airtime_used_ms(3600000));
    out.print(F(" routes="));             out.print(g_node.rt_count());
    out.print(F(" pending="));            out.print(g_node.has_pending_tx() ? 1 : 0);
    out.print(F(" reset="));                                                     // v2: the fault-log's newest CAUSE ("-" = none)
    if (g_last_reset_valid) out.print(mrfault::fault_cause_str(g_last_reset.cause));
    else                    out.print('-');
    out.print(F(" halted="));             out.print(g_halted ? 1 : 0);        // prep-restart: 1 = intentionally dormant, not wedged
#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)
    out.print(F(" stackhw="));            out.print(loop_stack_free_bytes()); // ADDENDUM 4: loop-task min free stack bytes — the jump-to-0x0 was this overflowing; must stay well >0
#endif
    if (g_fs_reformatted) out.print(F(" fs=REFORMATTED"));                        // Part 2: InternalFS was corrupt this boot -> reformatted (re-provision)
#if defined(NRF52_PLATFORM) && defined(PIN_VBAT) && !defined(MR_NO_BATT)
    // Battery diagnostic. VBAT (P0.31) reads the CELL through a ÷3 divider — NEVER USB's 5 V (max ~4.2 V).
    // Verify vs a multimeter on the battery: mv = raw × ADC_MULTIPLIER(3.0) × AREF_VOLTAGE(3.0) / 4.096.
    pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
    analogReadResolution(12); analogReference(AR_INTERNAL_3_0);
    const int braw = analogRead(PIN_VBAT);
    out.print(F(" batt_raw="));           out.print(braw);
    out.print(F(" batt_mv="));            out.print((int)((braw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f));
#endif
#if MR_FEAT_RADMIN_ACCEPT
    const auto radmin = g_node.radmin_counters(); // scalar snapshot only; never expose the secret-bearing state view
    out.print(F(" radmin_inbound_refusal=")); out.print(radmin.inbound_refusal);
    out.print(F(" radmin_open_rate_refusal=")); out.print(radmin.open_rate_refusal);
    out.print(F(" radmin_transcript_exhaustion=")); out.print(radmin.transcript_exhaustion);
    out.print(F(" radmin_response_enqueue_failure=")); out.print(radmin.response_enqueue_failure);
    out.print(F(" radmin_response_seal_failure=")); out.print(radmin.response_seal_failure);
    remote_action_print_status(out);
#endif
#if MR_FEAT_RADMIN_CLIENT
    remote_client_status(out,g_node.remote_client());
#endif
    out.println();
}

// `duty` — duty-cycle consumption readout: 0..100% of the rolling-window budget (100 = the node must stay silent),
// + when at 100% how long until airtime ages back in. `disabled` when there is no duty limit.
static void dump_duty(Print& out) {
    const auto d = g_node.duty_status();
    if (!d.enabled) { out.println(F("disabled (no duty limit)")); return; }
    out.print(d.pct); out.print('%');
    if (d.pct >= 100) { out.print(F(" — SILENT, ~")); out.print((d.avail_ms + 500) / 1000); out.print(F(" s to availability")); }
    out.println();
}

// "7,12" -> allowed_sf_bitmap (bit per SF index 5..12); 0 if none valid.
// parse_sf_list moved to firmware_config_parse.h (pure, native-tested); `using mrfw::parse_sf_list` above.

// Apply the RADIO operating point from the (just-saved) NV blob LIVE — no reboot. `reconfig` re-tunes the
// radio (freq/SF/BW/CR changed); a tx_power-only change skips the re-tune (it's set per-TX via the Hal).
// apply_radio_live moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::apply_radio_live` (top).

// Print the node's key_hash32 (hex, from g_identity) + name (from /mrid).
// ⛔ V1 COMMENT CORRECTION (§0b/[[B279]]): this line used to end *"Shared by boot, `status`, `regen`"* — WITHDRAWN.
//    The source has exactly TWO callers — `setup()`'s boot banner (fw_main.cpp) and `do_regen` below; `dump_status`
//    never calls it. ⇒ it is shared by BOOT and `regen`, and each caller now NAMES ITS OUTPUT AUTHORITY (boot passes
//    `mrcon`, the dispatcher passes the `Print&` it was handed). There is deliberately NO parameterless overload and
//    NO default sink: a formatter that can silently choose the global console is exactly what [[B279]] was.
void print_identity(const mrnv::IdBlob& idb, Print& out) {
    char hx[9];
    snprintf(hx, sizeof hx, "%08lX", (unsigned long)g_identity.key_hash32);
    out.print(F("  key_hash32= 0x")); out.print(hx);
    if (idb.name_len > 0 && idb.name_len <= sizeof idb.name) {
        out.print(F("  name=\""));
        for (uint16_t i = 0; i < idb.name_len; ++i) out.print(idb.name[i]);
        out.print(F("\""));
    }
    out.println();
}

// `regen` — mint a NEW identity (fresh HW-RNG seed) -> persist /mrid -> re-derive -> re-seed the node's
// self binding. Keeps `name` + `node_id` (the short address is independent of the keypair). The new
// key_hash32 propagates on the next beacon; peers re-bind by it (the old one ages out of their id_bind).
// §0b/[[B279]]: it answers on the sink `dispatch()` HANDED it — USB `mrcon`, or the BLE `LineSink` — never the
// global console. ⛔ The operation ORDER below is untouched and load-bearing: identity + crypto identity are
// installed only AFTER a successful `save_id`, so a refused write leaves the running node exactly as it was.
static void do_regen(Print& out) {
#if MR_FEAT_RADMIN_CLIENT
    // ★★★ §RADMIN slice 4 — THE CONTROLLER ADMISSION, EVALUATED BEFORE ANY DRAW, WRITE OR IDENTITY CHANGE.
    //     `regen` rotates the node's MESSAGING identity, which is the `self` key every managed target's ACL was
    //     granted against, so doing it while a source-bound request/assembly/retained result/response ACK is
    //     outstanding would strand work that can never be answered under the new key.
    // ⛔ THE PRODUCTION BINDING ANSWERS "NO DEBT" because Slice 4 has NO PRODUCER (Slice 8a is the first). The
    //    predicate is bound now so the rule lands in ONE place rather than beside a future request handler.
    {
        DeviceClientRemoteDebt debt;
        if (!mrfw::client_regen_admitted(debt)) {
            AdminClientPrintLines lines(out);
            mrfw::client_regen_emit_busy(lines);
            return;                                              // ⛔ zero draws, zero writes, no identity change
        }
    }
#endif   // MR_FEAT_RADMIN_CLIENT
    mrnv::IdBlob idb{};
    mrnv::load_id(idb);                                          // preserve the existing name (if any)
    mrrng::fill(idb.seed, sizeof idb.seed);
    idb.magic = mrnv::kIdMagic; idb.version = mrnv::kIdVersion;
    if (!mrnv::save_id(idb)) { out.println(F("> regen err nv_save_failed")); return; }
    meshroute::identity_from_seed(g_identity, idb.seed);
    g_node.set_identity(g_node.node_id(), g_identity.key_hash32);
    g_node.set_crypto_identity(g_identity.x_secret, g_identity.ed_pub);   // DP1: re-install the E2E crypto identity
    out.print(F("> regen ok"));
    print_identity(idb, out);
#if MR_FEAT_RADMIN_CLIENT
    // ★ THE ONE CLIENT WARNING, AFTER the complete success line and on the SAME sink — ⛔ never on a refusal and
    //   ⛔ never on a save failure (both returned above). ⓘ `preserved` is a claim this slice PROVES: the path
    //   above writes `/mrid` and nothing else, and neither controller store is loaded, drawn from or rewritten.
    {
        AdminClientPrintLines lines(out);
        mrfw::client_regen_emit_note(lines);
    }
#endif   // MR_FEAT_RADMIN_CLIENT
}

// `factory_reset` — confirm-gated full NV wipe -> reboot factory-fresh (default config + a NEW identity + no peers
// + empty inbox). The literal `confirm` token guards against an accidental paste (irreversible).
ActionOutcome action_factory_reset_apply(ActionBackend backend, Print& out, Print& reboot_out,
                                         ActionObserver observer) {
    out.println(F("> factory reset — erasing all NV, rebooting…"));
    // §5: drop the durable inbox RECORDS (their store's domain); factory_erase does the NV slots + the meta.
    // ⛔ [[B134]] QG blocker 3: BOTH results are now checked. `wipe()` used to return `void`, so this verb
    //    rebooted claiming a factory state while records stayed RECOVERABLE on flash — a data-retention lie in
    //    the worst direction, since a user told the history is gone will act as if it is. ⚠ Both stores are
    //    wiped BEFORE the `&&` short-circuits could skip one: a partial erase must still erase what it can.
    // ★ "MAY remain" is the RULED wording (2026-08-29) and the qualifier is load-bearing: every segment can
    //   erase cleanly and the METADATA save still fail, so a flat "messages remain" would be its own overclaim.
    const bool dm_ok = g_inbox_dm.wipe(), ch_ok = g_inbox_ch.wipe();
    if (!dm_ok || !ch_ok) out.println(F("> factory_reset WARN: inbox erase incomplete (messages may remain on flash)"));
    const bool nv_ok = mrnv::factory_erase();
    if (!nv_ok) out.println(F("> factory_reset WARN: an NV slot did not erase (boot re-defaults it)"));
    const auto reset_outcome = !dm_ok || !ch_ok
        ? (nv_ok ? ActionOutcome::inbox_partial : ActionOutcome::inbox_nv_partial)
        : (nv_ok ? ActionOutcome::started : ActionOutcome::nv_partial);
    return action_reboot_apply(backend, reboot_out, observer, reset_outcome);
}

static void handle_factory_reset(const char* arg, size_t n, Print& out) {
    const auto admission = action_factory_reset_admit(arg, n, action_build_support());
    // Build-level support is for a later remote consumer; preserve the local erase even without reset HW.
    if (admission.status != ActionAdmissionStatus::confirmation) {
        action_factory_reset_apply(admission.plan.backend, out, mrcon);
    } else {
        out.println(F("> factory_reset WIPES ALL flash (config + identity + peers + inbox) and reboots to factory. Type 'factory_reset confirm' to proceed."));
    }
}

// `sleep` / `sleep on` -> light-sleep when idle even though a host is present (the explicit override the user
// asked for); `sleep off` -> cancel it, stay awake. A headless node (no console byte this boot) light-sleeps
// on its own — this command is only for a node you're connected to. After `sleep on` the console goes quiet
// (light-sleep gates the UART) — reconnect to get it back (DTR resets the board). The node still wakes on RX
// (a peer DM prints RECV) and on its scheduled timers. No-op on -DMR_NO_POWERSAVE builds (the gate is gone).
ActionOutcome action_sleep_apply(ActionKind kind, Print& out, ActionObserver observer) {
    if (kind == ActionKind::sleep_off) {
        g_force_sleep = false;
        out.println(F("> sleep off — staying awake while a host is connected"));
    } else if (kind == ActionKind::sleep_on) {
        g_force_sleep = true;
        out.println(F("> sleep on — light-sleeping when idle; reconnect to wake the console (still wakes on RX)"));
    } else {
        return action_report(observer, ActionOutcome::backend_failed);
    }
    return action_report(observer, ActionOutcome::completed);
}

static void handle_sleep(const char* arg, size_t n, Print& out) {
    const auto admission = action_sleep_admit(arg, n, action_build_support());
    // MR_NO_POWERSAVE still sets the local flag and prints; it only removes the sleep policy.
    action_sleep_apply(admission.plan.kind, out);
}

// `debug on` / `debug off` (also `debug 1`/`debug 0`) — gate the decoded per-frame «rx/»tx console trace
// (frame_trace.h g_mr_trace_on). §3: default OFF at boot; `debug on` enables it for the session.
static void handle_debug(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    const bool off = (n >= 3 && !strncmp(arg, "off", 3)) || (n >= 1 && arg[0] == '0');
    meshroute::g_mr_trace_on = !off;
    out.println(off ? F("> debug off — RX/TX frame trace silenced") : F("> debug on — tracing RX/TX frames"));
}

// `lookup <hash>` — local id_bind cache peek (NO airtime): resolve a key_hash32 -> node short-id from what
// this node already knows (beacons / prior H answers). Hash is hex (e.g. `lookup 8a3f1c02`). For a network
// resolve of an unknown hash, use `resolve` (floods H).
static void handle_lookup(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> lookup err: hash must be 0x-prefixed (e.g. lookup 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);   // 0x-only (kills id-vs-hash ambiguity)
    meshroute::Node::IdBindConf conf = meshroute::Node::IdBindConf::claimed;
    const int id = g_node.id_bind_find_by_hash(hash, &conf);
    out.print(F("[lookup] 0x")); out.print(hash, HEX);
    if (id < 0) { out.println(F(" -> miss")); return; }
    out.print(F(" -> id=")); out.print(id);
    out.println(conf == meshroute::Node::IdBindConf::authoritative ? F(" (authoritative)") : F(" (claimed)"));
}

// §1.3 `nameof 0x<hash>` — the cached human name for a peer's key_hash32 (learned via the pubkey exchange, refreshed on each).
// ★ §AB3: reads the GENERATED address-book view (spec §2.1), not peer_name_find alone, so it also reports which
// namespace(s) that hash answers to. `nameof`, `hashof` and `peers` now share ONE read path by construction — the
// §2.5 defect was three verbs each reading one table and disagreeing about one identity.
static void handle_nameof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (n < 3 || arg[0] != '0' || (arg[1] != 'x' && arg[1] != 'X')) { out.println(F("> nameof err: hash must be 0x-prefixed (e.g. nameof 0x8a3f1c02)")); return; }
    const uint32_t hash = (uint32_t)strtoul(arg + 2, nullptr, 16);
    meshroute::Node::PeerBookRow row{};
    (void)g_node.peer_book_by_hash(hash, row);   // false = nothing known -> the row is zeroed and every field omits
    // §S6: JSON answer {"ev":"peer_name","hash":<dec u32>[,"name":"…"][,"static_id":N][,"team_id":N]} — app-facing query verb.
    const size_t m = meshroute::console::write_peer_name(s_inbox_jb, sizeof s_inbox_jb, hash, row.name, row.name_len,
                                                        row.static_id, row.team_id);
    if (m) out.write(s_inbox_jb, m);
}

// One `[hashof]` answer line. `queried` is what the operator typed; `row.team_id` may DIFFER when two team ids alias one
// hash — that is the ambiguity spec §2.1 forbids resolving silently, so the alias is named on the line.
static void hashof_print_row(Print& out, uint8_t queried, bool team_plane, const meshroute::Node::PeerBookRow& row) {
    out.print(F("[hashof] id=")); out.print(queried);
    out.print(team_plane ? F(" team -> 0x") : F(" static -> 0x")); out.print(row.hash, HEX);
    // ★ §id-hash S3: name the CONFIDENCE of the binding this line just resolved — the same `(auth)`/`(claimed)`
    // vocabulary `peers` uses (U1, one spelling for one fact). `hashof` is the verb an operator reaches for before
    // spending airtime on a hash, so it is exactly where a claim must not be dressed as a fact.
    out.print(team_plane ? (row.team_authoritative ? F("(auth)") : F("(claimed)"))
                         : (row.static_authoritative ? F("(auth)") : F("(claimed)")));
    if (row.name_len) { out.print(F(" name=\"")); out.write(row.name, row.name_len); out.print('"'); }
    if (team_plane && row.team_id && row.team_id != queried) {          // freshest-wins picked a DIFFERENT id for this hash
        out.print(F(" (ALIASED: team id ")); out.print(row.team_id); out.print(F(" is FRESHER for this hash)"));
    }
    if (row.team_alias_dropped) { out.print(F(" +")); out.print(row.team_alias_dropped); out.print(F(" stale team-id alias dropped")); }
    out.println();
}

// `hashof <id> [-t|-s]` — an id -> its key_hash32, answered from the VIEW (spec §2.5).
// ★★ THE DEFECT THIS FIXES, bench-proven 2026-07-30: `reqpubkey <id>` reads the TEAM key cache and `hashof <id>` read
// only the STATIC _id_bind, so `reqpubkey 228` cached 0x6C297145 and `hashof 228` still said `unknown`. Each verb was
// correct about its own table; neither answered the question. Now BOTH namespaces are searched and the matching plane is
// NAMED — and when one number matches in both (the §18 dual-identity space) BOTH lines print, never one silently chosen.
// ⚠⚠ The tempting repair — writing the team hash into _id_bind — is FORBIDDEN (spec §2.5): that is the plane-blind
// ingest closed on 2026-07-31 (§id-bind-plane), an I2 breach. The fix is a READ of the view; nothing is written here.
// `-t` / `-s` narrow the search when the caller already knows the plane (the `send -t` / `reqpubkey -t` idiom, U3).
static void handle_hashof(const char* arg, size_t n, Print& out) {
    while (n && *arg == ' ') { ++arg; --n; }
    if (!n) { out.println(F("> hashof err bad_args (id 1..254 [-t|-s])")); return; }
    bool only_team = false, only_static = false;
    for (size_t i = 0; i + 1 < n; ++i)
        if (arg[i] == '-') { if (arg[i + 1] == 't') only_team = true; else if (arg[i + 1] == 's') only_static = true; }
    if (only_team && only_static) { out.println(F("> hashof err bad_args (-t and -s are exclusive)")); return; }
    const int id = atoi(arg);
    if (id < 1 || id > 254) { out.print(F("[hashof] id=")); out.print(id); out.println(F(" -> unknown (id must be 1..254)")); return; }
    meshroute::Node::PeerBookRow st{}, tm{};
    const uint8_t mask = g_node.peer_book_by_id((uint8_t)id, st, tm);
    const bool show_static = (mask & meshroute::Node::kPeerBookStatic) && !only_team;
    const bool show_team   = (mask & meshroute::Node::kPeerBookTeam)   && !only_static;
    if (show_static) hashof_print_row(out, (uint8_t)id, /*team_plane=*/false, st);
    if (show_team)   hashof_print_row(out, (uint8_t)id, /*team_plane=*/true,  tm);
    if (!show_static && !show_team) {
        out.print(F("[hashof] id=")); out.print(id);
        out.print(F(" -> unknown"));
        if (only_team)        out.print(F(" (team plane; drop -t to search both)"));
        else if (only_static) out.print(F(" (static plane; drop -s to search both)"));
        // ★ §err-reason/B33: the old text here said "try `reqpubkey`" and that advice was CIRCULAR. So name the two
        // remedies that actually work with no hash in hand: a beacon (the on-air source of the binding), or an
        // out-of-band QR import, which needs no prior hash. U3: same shape as `team grantkey`'s already-correct
        // unheard-target refusal in firmware_config.cpp. ⓘ The sibling `-t`/`-s` lines above are correct — untouched.
        // ⚠ V1 2026-08-01 (§id-hash S1): the old wording explained the circularity as *"`reqpubkey <bare id>` means a
        // team_local_id, so it resolves through team_key_of_id"* — TRUE THEN, STALE NOW. S1 made `reqpubkey <id>` read
        // `peer_book_by_id`, i.e. THIS VERY VIEW, on BOTH planes.
        // ⚠⚠ V1 AGAIN 2026-08-02 (§id-hash S4a): the "⚠ NOT `reqpubkey <id>`" advice this line used to carry is now
        // WRONG, and reversing it is the whole point of the slice. `reqpubkey <id>` no longer refuses on an unknown
        // id — it asks the mesh "who owns id N?" (H_FLAG_BY_ID), which is the ONE remedy that works for a peer we
        // route to but have never heard. The answer lands as a CLAIM, so this view will then show it labelled
        // `(claimed)` rather than `(auth)`.
        else                  out.print(F(" (neither plane — no beacon heard, so its key_hash32 is unknown. Remedy: `reqpubkey <id>` (or `-s`/`-t`) floods a BY-ID query asking who owns it — the answer is a CLAIM, shown here as (claimed), and the node then fetches the pubkey itself (§S4b: one command, not two). Alternatives: wait for / provoke a beacon, or import its QR out-of-band with `peerkey <hex64>`)"));
        out.println();
    }
}

// ★★ §id-hash S1 (spec 2026-08-01 §1-A): a refused `reqpubkey` gets the SAME quality of remedy text handle_hashof
// above gives — the bare CmdCode (`err_no_binding` / `err_ambiguous_plane`) names the wall, not the way round it, and
// the way round it is DIFFERENT per refusal. Deliberately in this TU and not in fw_main (U3: fw_main is board/runtime
// glue; the wording belongs next to handle_hashof's so a future edit sees both).
// ★ §id-hash S1b (QA P1c) EXTENDS IT to the three send-side refusals emit_hash_query can now report, and those need it
// MORE than the resolution ones: `err_no_gateway` and `err_too_large` are REUSED codes (U1), so on this verb the token
// alone is actively confusing — nobody reads "no gateway" as "your mobile has no home to answer through". They apply to
// the by-HASH form too, so they are handled before the id-only arms.
void print_reqpubkey_hint(Print& out, const meshroute::Command& cmd, const meshroute::CmdResult& r) {
    if (cmd.kind != meshroute::CmdKind::reqpubkey) return;
    const uint8_t id = cmd.u.resolve.dst_id;
    if (r.code == meshroute::CmdCode::err_no_identity) {
        out.println(F("> reqpubkey: this node holds NO crypto identity, and the exchange is MUTUAL (our own ed_pub rides"
                      " the query), so nothing was aired. Remedy: provision an identity (`regen`), then retry."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_no_gateway) {
        out.println(F("> reqpubkey: nothing aired — this node is an UNREGISTERED mobile, so its id is a LOCAL id the"
                      " owner has no route back to. Remedy: register with a home (`mobile register`), or ask on the team"
                      " plane with `-t` if the target is a teammate."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_tx_queue_full) {
        // ★ §id-hash S1d: the ONLY transient refusal on this verb — say so, or the operator treats it like the others
        // and starts changing configuration that was never wrong.
        // ⚠ IT DELIBERATELY DOES NOT NAME WHICH QUEUE. Two can reject: the Node's LBT defer ring (channel busy) and
        // the radio's own outbound queue (radio saturated). This text cannot tell them apart, and naming one would
        // be a wrong diagnosis half the time — the same reasoning that kept the code out of `err_ack_ring_full`.
        out.println(F("> reqpubkey: nothing was sent — a bounded TX queue rejected the frame (the radio or the channel"
                      " is saturated). TRANSIENT: just retry in a moment. Nothing is misconfigured."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_resolve_pending_full) {
        // ★ §id-hash S4b: the second TRANSIENT refusal on this verb, and it must not be mistaken for the first.
        // err_tx_queue_full means the radio/channel is saturated; this means WE are already waiting on that many
        // unresolved by-id questions — each of which is a flood in flight. Different wait, different remedy.
        out.println(F("> reqpubkey: nothing aired — this node is already waiting on the maximum number of unresolved"
                      " by-id requests (each one is an H flood in flight). TRANSIENT: retry once one of them resolves"
                      " or times out (~25 s). Nothing is misconfigured."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_unsupported) {
        out.println(F("> reqpubkey: nothing aired — the target is not a queryable peer (hash 0, or this node's own"
                      " key_hash32). Remedy: name another node's hash or id."));
        return;
    }
    if (r.code == meshroute::CmdCode::err_ambiguous_plane) {
        // §3-D9: the operator must pick a plane, and since §id-hash S4a there are TWO ways to get here — RESOLVED in
        // both planes (two known, possibly different, peers), or UNRESOLVED on a node that genuinely lives on both,
        // where the by-id query itself has to choose one. Both refuse for the same reason: the next step spends
        // AIRTIME, and D9 will not guess at an airtime boundary.
        out.print(F("> reqpubkey: id ")); out.print(id);
        out.println(F(" is ambiguous — this node has BOTH planes (§18: a static node_id and a team local id may be the"
                      " same number, and need not be the same peer), so either both hold a binding or neither does and"
                      " the by-id query must pick one. Remedy: say which — `reqpubkey <id> -s` (static) or `-t` (team)."
                      " `hashof <id>` prints whichever rows exist, with their hashes."));
        return;
    }
    if (r.code != meshroute::CmdCode::err_no_binding || id == 0) return;
    // ★ §id-hash S4a: this refusal is now REACHABLE FROM ONE PLACE ONLY — an explicit `-t` on a node that is not in a
    // team. Every other unresolved by-id request flies a BY_ID query instead of refusing, so the old "no beacon has
    // bound this id" wording (kept below for the unreachable arms, in case a future slice re-opens them) no longer
    // describes the common case.
    out.print(F("> reqpubkey: no id->hash binding for ")); out.print(id);
    if (r.plane == 1)      out.println(F(" on the TEAM plane, and this node is not IN a team (`team_id` is 0) — so there"
                                        " is no team plane to ask on and the `-t` query was not aired. Remedy: drop `-t`"
                                        " (the static by-id query is then sent), or join a team first."));
    else if (r.plane == 0) out.println(F(" in EITHER plane, and no by-id query could be aired for it. Remedy: check the"
                                        " id is 1..254 and not this node's own, or use `reqpubkey 0x<hash>` directly."));
    else                   out.println(F(" on the STATIC plane, and no by-id query could be aired for it. Remedy: drop"
                                        " `-s` to let the resolver choose, or use `reqpubkey 0x<hash>` directly."));
}

// ★★ §AB3 `peers` / `peers all` — the GENERATED address book as text (spec §2.1). The bounded form (rows backed by the
// 16-slot peer-key cache) is the address book proper; `peers all` adds the up-to-256 id-only diagnostic rows and is
// TEXT-CONSOLE ONLY per the §2.6(a) ruling — a few hundred rows over BLE is the self-inflicted console-flood wedge this
// project has already fixed once (the mrcon drop-never-block sink). U3: line shape mirrors dump_routes' `[route] k=v`.
static void peers_text_row(const meshroute::Node::PeerBookRow& r, void* ctx) {
    Print& out = *static_cast<Print*>(ctx);
    out.print(F("[peer]"));
    if (r.hash) { out.print(F(" hash=0x")); out.print(r.hash, HEX); }
    if (r.name_len) { out.print(F(" name=\"")); out.write(r.name, r.name_len); out.print('"'); }
    // ★ §id-hash S2: the (auth)/(claimed) suffix is printed ONLY when there is a hash to be authoritative ABOUT. An
    // id-only row (the new `_rt` pass 2b, and pass 2's hash==0 shape) holds NO binding at all, and labelling that
    // `static_id=48(claimed)` would report a claim nobody made — the opposite of the honesty this dump exists for.
    // A bare `static_id=48` reads correctly: we route to it, we cannot name it.
    if (r.static_id) { out.print(F(" static_id=")); out.print(r.static_id);
                       if (r.hash) out.print(r.static_authoritative ? F("(auth)") : F("(claimed)")); }
    // ★ §id-hash S3: the TEAM plane gains the same label, under the same "only when there is a hash to be
    // authoritative ABOUT" rule (an id-only team row from pass (4) asserts no binding). Owner ruling: an id->hash
    // answer heard on air is a CLAIM, never a fact — so the operator must be able to SEE which one they are looking
    // at. Until S4a there is no claimed producer, so this reads `(auth)` on every row a live node can build.
    if (r.team_id)   { out.print(F(" team_id="));   out.print(r.team_id);
                       if (r.hash) out.print(r.team_authoritative ? F("(auth)") : F("(claimed)")); }
    if (r.has_key) { out.print(F(" conf=")); out.print(meshroute::console::peerkeyconf_name(r.conf));
                     out.print(F(" confirmed=")); out.print(r.peer_confirmed ? 1 : 0); }
    else if (r.hash && r.name_len) out.print(F(" key=AGED(unusable — reqpubkey to refresh)"));
    // ★★ §AB4 the retained position (spec §2.7). The AGE and the SOURCE ride with it, never the pin alone: an age-less
    // position reads as current, and a `team`-anchored one is only "some holder of the team key said so" (the shared
    // content key proves membership, not identity) where a `peer`-anchored one was sealed to us. RAM-only — a reboot
    // clears every one of these on purpose. Most rows carry no position at all, and that is normal, so nothing prints.
    if (r.has_location) {
        out.print(F(" loc=")); out.print(r.lat_e7); out.print(','); out.print(r.lon_e7);
        out.print(F(" age=")); out.print(r.loc_age_s); out.print(F("s src="));
        out.print(meshroute::console::peerlocsrc_name(r.loc_src));
    }
    if (r.team_alias_dropped) { out.print(F(" +")); out.print(r.team_alias_dropped); out.print(F(" stale team-id alias dropped")); }
    out.println();
}
static void dump_peers(Print& out, bool all) {
    const uint16_t n = g_node.peer_book_walk(all, peers_text_row, &out);
    if (!n) out.println(F("empty"));
    out.print(F("[peers] count=")); out.print(n);
    out.print(all ? F(" (all: keyed + id-only)") : F(" (keyed only — `peers all` adds id-only rows)"));
    out.println();
}

// `peers` over BLE/companion: the BOUNDED book only — one {"ev":"peer",…} per _peer_keys-backed row then
// {"ev":"peers_end","count":N}. Mirrors handle_routes exactly (U3), including the shared s_inbox_jb scratch.
static void peers_json_row(const meshroute::Node::PeerBookRow& r, void* ctx) {
    Print& out = *static_cast<Print*>(ctx);
    const size_t m = meshroute::console::write_peer_row(s_inbox_jb, sizeof s_inbox_jb, r);
    if (m) out.write(s_inbox_jb, m);
}
void handle_peers(Print& out) {
    const uint16_t n = g_node.peer_book_walk(/*include_id_rows=*/false, peers_json_row, &out);
    const size_t m = meshroute::console::write_peers_end(s_inbox_jb, sizeof s_inbox_jb, n);
    if (m) out.write(s_inbox_jb, m);
}

// `whoami` — this node's own identity + role. The hash printed here is what a peer types into `sendhash` to
// reach you (the device can't surface its own key_hash32 any other way). Name is read from /mrid.
static void handle_whoami(Print& out) {
    out.print(F("[whoami] id=")); out.print(g_node.node_id());
    out.print(F(" hash=0x"));     out.print(g_node.key_hash32(), HEX);
    { char nm[32]; uint8_t nn = g_node.effective_name(nm, sizeof nm); out.print(F(" name=\"")); out.write(nm, nn); out.print('"'); }   // §1.3 / W1c D10: the STORED name exactly — name="" when unnamed (no default is made up)
    const meshroute::NodeConfig& c = g_node.config();
    out.print(F(" leaf="));   out.print(c.leaf_id);
    out.print(F(" gw="));     out.print(c.is_gateway ? 1 : 0);
    out.print(F(" gwonly=")); out.print(c.gateway_only ? 1 : 0);
    out.print(F(" mobile=")); out.println(c.is_mobile ? 1 : 0);
    // Dual-layer gateway: an ADDITIVE per-leaf line. Single-layer whoami above is BYTE-IDENTICAL to before.
    if (c.n_layers == 2) {
        for (uint8_t li = 0; li < 2; ++li) {
            const meshroute::LayerConfig& L = c.layers[li];
            out.print(F("[whoami.layer")); out.print(li);
            out.print(F("] node_id="));   out.print(L.node_id);
            out.print(F(" layer_id="));   out.print(L.layer_id);
            out.print(F(" routing_sf=")); out.print(L.routing_sf);
            out.print(F(" window_ms="));  out.print(L.window_ms);
            out.print(F(" window_offset_ms=")); out.println(L.window_offset_ms);
        }
    }
}

// gw_parse_err_str / gw_val_err_str / handle_gateway moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment A); `using mrfw::handle_gateway` (top).

// ---- `leaf` command REMOVED (2026-07-03) --------------------------------------------------------------------
// The low-level `leaf create` (which minted a leaf from the node's CURRENT settings) is folded into `create`
// (explicit key=value params; anti-spam knobs default rather than inherit). `leaf name <text>` -> `cfg set
// leaf_name "<text>"` (the config-hash rename that bumps the epoch). One leaf-mint verb now: `create`.

// ---- R6.3 normal-node provisioning verbs: join / create / leave — LIVE-APPLY (no reboot). Spec
//      2026-06-21-leaf-provisioning-console-verbs.md. `create` is the ONE leaf-mint verb (2026-07-03: the old
//      low-level `leaf create` folded in; `key=value` args); `cfg set <key>` stays the granular per-field path
//      (§4). Normal nodes ONLY (gateways are multi-layer -> a future join_as_gateway, §5). ------------------

// seed_blob_from_live + provision_apply_live moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B) — internal (static) there.

// handle_join / handle_create / handle_team / handle_mobile moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B); `using mrfw::handle_*` above (guarded #if MR_N_LAYERS<2 / MR_FEAT_MOBILE).

// handle_leave moved to firmware_config.{h,cpp} (cleanup 2026-07-14, Increment B); `using mrfw::handle_leave` above.

// ★★ §B95 (2026-08-04) + §0a/[[B208]] (2026-09-04) + §0g (2026-09-05): `dump_help()` IS GONE FROM THIS TU. The
// whole `help` / `?` recognition and the response itself live in `src/firmware_help.h`, which is a header precisely
// so an automated gate can compile and RUN it (`test_build_src = no` keeps this TU out of every host build).
// `dispatch()` below keeps exactly one call to it and parses no help of its own.
// ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]]): the sentence above used to say the header holds *"the help TEXT, the
//   compact TOPIC INDEX and the whole `help` / `?` / `help <topic>` recognition"*. All three nouns are retired —
//   §0g deleted the descriptions, the topic index and the topic sections. What the header holds now is the BARE
//   PRIMARY-NAME index plus one bounded refusal for every argument-bearing form.
// TWO rulings are pinned in that file and must not be undone from here:
//   • the `hl()` DIRECT-SERIAL BYPASS STAYS GONE. It wrote straight to `Serial` — not through the `Print& out`
//     it was handed, and not through `mrcon` — with its own per-line drain loop (`yield()`, up to 40 ms EACH,
//     so ~3 s of loop stall for one dump), and it emitted the CRLF only `if (Serial.availableForWrite() >= 2)`,
//     which is why bench H5-06 saw help lines with no line ending. Help writes through its supplied sink like
//     every other dump; console_sink.h is what guarantees whole-line admission. Do NOT reintroduce a per-line
//     drain, here or there — a formatter must not second-guess the transport.
//   • [[B208]]'s SIZE half stands and is measured: every help response fits inside MR_CONSOLE_STAGE_BYTES with
//     no `CONSOLE_DROP`. The single dump it replaced measured 88 lines / 7332 B on a full OLED build, i.e. 3.6x
//     the stage, so its tail was dropped as whole lines on EVERY run. ⛔ Do not answer that by enlarging the
//     stage or adding a pager.
//     ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]]): its CONTENT half read *"bare `help`/`?` print only a compact index
//       of THIS BUILD's TOPICS and `help <topic>` prints exactly one complete SECTION"* — WITHDRAWN, superseded by
//       §0g. Bare `help`/`?` print one line per PRIMARY COMMAND NAME this build compiles (bytewise sorted) plus the
//       manual pointer; `help <anything>` is a retired form and takes the one bounded refusal. The completeness
//       oracle is the generated inventory, not this comment.

// `limits` verb (USB): the companion anti-spam/headroom snapshot as one NDJSON line. Composed from limits_snapshot()
// then serialized via write_limits() into s_inbox_jb (declared just above) — same pattern as the other JSON dumps. A
// local read through the shared dispatcher; mirrors the BLE `limits` handler.
static void dump_limits(Print& out) {
    const auto s = g_node.limits_snapshot();
    meshroute::console::LimitsFields L;
    L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
    L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
    L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
    L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
    L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
    const size_t m = meshroute::console::write_limits(s_inbox_jb, sizeof s_inbox_jb, L);
    if (m) out.write(s_inbox_jb, m);   // JSON line to USB (mirrors the other write_* dumps)
}

// Firmware scheduled-send (spec 2026-06-24): arm the node to fire DMs/channel posts on an ms-offset schedule OVER THE
// RADIO, so the oracle touches USB only to arm + read (killing the continuous-stream USB-CDC death). `testsend <dst>
// <run> [-a] [-e] -t ms1,ms2,…` / `testch <ch> <run> -t ms1,ms2,…` — APPENDS (seq keeps counting). Offsets are ms
// from NOW (arm). The fired body = the harness tag `T<run>S<self>#<seq>` + `@<sendms>` (built in the loop tick).
static void handle_testsched(char* args, bool is_channel, Print& out) {
    char* toks[12]; int nt = 0;
    for (char* p = strtok(args, " "); p && nt < 12; p = strtok(nullptr, " ")) toks[nt++] = p;
    if (nt < 2) { out.println(F("> err usage: testsend <dst> <run> [-a] [-e] -t ms1,ms2,…  |  testch <ch> <run> -t ms1,ms2,…")); return; }
    const char* dst_s = toks[0];
    const char* run_s = toks[1];
    const char* list_s = nullptr; bool ack = false, enc = false;
    for (int i = 2; i < nt; ++i) {
        if      (!strcmp(toks[i], "-a")) ack = true;
        else if (!strcmp(toks[i], "-e")) enc = true;
        else if (!strcmp(toks[i], "-t") && i + 1 < nt) list_s = toks[i + 1];
    }
    if (!list_s) { out.println(F("> testsched err: missing -t <ms,ms,…>")); return; }
    // <run> must be ALNUM — the host reconcile regex `T([0-9A-Za-z]+)S…` only matches alnum, so a hyphen/dot/_ run
    // would send a body the harness CAN'T parse -> every message silently unreconciled. Fail loud instead.
    for (const char* q = run_s; *q; ++q)
        if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
            out.println(F("> testsched err: <run> must be alphanumeric (the host tag regex)")); return; }
    uint32_t target = 0, v = 0; uint8_t flags = 0;
    if (is_channel) {
        // ⚠ V1 2026-07-31: this used to read *"matches send_channel (rejects them)"* and half of that is now FALSE —
        // §chan-crypt CL1 made `send_channel` ACCEPT `-e`. The refusal here is kept DELIBERATELY (behaviour unchanged,
        // C1): the scheduled-send workload has no `-t`, so any `-e` it forwarded would be the `no_team` refusal, and
        // `mrsched` carries no plane bit to fix that with. ✖ MISSING: a sealed `testch` — TRIGGER: CL2 + a plane flag here.
        if (ack || enc) { out.println(F("> testch err: -a/-e not valid on a channel")); return; }   // -a matches send_channel (O3: no ack); -e is a testch-only limit, see above
        if (!mrsched::parse_dec(dst_s, 255, v)) { out.println(F("> testch err: channel 0..255")); return; }
        target = v; flags |= mrsched::kChannel;
    } else {
        uint32_t h;
        if (mrsched::parse_hash8(dst_s, h)) { target = h; flags |= mrsched::kHash; }
        else if (mrsched::parse_dec(dst_s, 254, v) && v >= 1) { target = v; }
        else { out.println(F("> testsend err: dst 1..254 or 8-hex hash")); return; }
        if (enc && !(flags & mrsched::kHash)) { out.println(F("> testsend err: -e (encrypt) needs an 8-hex hash dst")); return; }   // matches `send` (allow_e=by_hash)
        if (ack) flags |= mrsched::kAck;
        if (enc) flags |= mrsched::kEnc;
    }
    g_sched.set_run(run_s);
    uint32_t offs[128];
    const uint16_t no = mrsched::parse_offsets(list_s, offs, 128);
    if (no == 0) { out.println(F("> testsched err: no offsets parsed")); return; }
    if (no == 128) out.println(F("> testsched warn: offset list capped at 128 — split into more lines"));   // never silent
    const uint32_t base = (uint32_t)g_hal.now();
    uint16_t added = 0;
    for (uint16_t i = 0; i < no; ++i) if (g_sched.add(base + offs[i], target, flags) >= 0) ++added;
    out.print(F("> ")); out.print(is_channel ? F("testch") : F("testsend"));
    out.print(F(" run="));   out.print(g_sched.run);
    out.print(is_channel ? F(" ch=") : F(" dst=")); out.print(dst_s);
    out.print(F(" +"));      out.print(added);
    out.print(F(" armed=")); out.print(g_sched.armed());
    if (added < no) out.print(F(" (SCHED FULL — rest dropped)"));
    out.println();
}

static void handle_teststatus(Print& out) {
    const uint32_t mnow = (uint32_t)g_hal.now();
    const int32_t nx = g_sched.next_offset_ms(mnow);
    const char* state = (g_sched.armed() == 0) ? "idle" : (g_sched.done() ? "done" : "running");
    out.print(F("[teststatus] run=")); out.print(g_sched.run[0] ? g_sched.run : "-");
    out.print(F(" armed="));    out.print(g_sched.armed());
    out.print(F(" fired="));    out.print(g_sched.fired);
    out.print(F(" deferred=")); out.print(g_sched.deferred);
    out.print(F(" dropped="));  out.print(g_sched.dropped);
    out.print(F(" next="));     if (nx < 0) out.print('-'); else { out.print('+'); out.print(nx); }
    out.print(F(" state="));    out.println(state);
}


bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {   // §command-sink-consolidation: the single line->handler verb map (was service_debug); every response goes to `out`
#if MR_FEAT_RADMIN_CLIENT
    if (admin_primary_is(line,len,"remote")) { handle_remote_client(line,len,out,transport); return true; }
    if (admin_primary_is(line,len,"remote-retry")) { handle_remote_client(line,len,out,transport); return true; }
    if (admin_primary_is(line,len,"remote-result")) { handle_remote_client(line,len,out,transport); return true; }
    if (admin_primary_is(line,len,"remote-ack")) { handle_remote_client(line,len,out,transport); return true; }
#else
    (void)transport;
#endif
    if (help_command(line, len, out)) return true;   // §0a/[[B208]] + §0g — the ONE help router (firmware_help.h):
                                                    //   the bare primary-name index, or the bounded refusal for a
                                                    //   retired argument-bearing form. false = not a help line, so
                                                    //   `helpful` and every other verb fall through as before.
                                                    // ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]]): this annotation used
                                                    //   to offer "the index, ONE WHOLE TOPIC SECTION, or the
                                                    //   bounded refusal" — the middle outcome no longer exists.
    if (len == 7 && !strncmp(line, "version", 7))  { print_banner(out); return true; }
    if (len == 6 && !strncmp(line, "faults", 6))   { fw_faults_dump(out);  return true; }
    if (len == 12 && !strncmp(line, "prep-restart", 12)) { fw_prep_restart(out); return true; }
    if (len == 9 && !strncmp(line, "testclear", 9))    { g_sched.clear(); out.println(F("> testsched cleared")); return true; }
    if (len == 10 && !strncmp(line, "teststatus", 10)) { handle_teststatus(out); return true; }
    if ((len == 8 || (len > 8 && line[8] == ' ')) && !strncmp(line, "testsend", 8)) {   // strtok needs a mutable copy
        static char tb[512]; strncpy(tb, line + 8, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/false, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "testch", 6)) {
        static char tb[512]; strncpy(tb, line + 6, sizeof tb - 1); tb[sizeof tb - 1] = '\0'; handle_testsched(tb, /*channel=*/true, out);  return true; }
    if ((len == 9 || (len > 9 && line[9] == ' ')) && !strncmp(line, "crashtest", 9)) { fw_crashtest(line + 9, out); return true; }
    if (len == 6 && !strncmp(line, "routes", 6))   { dump_routes(out); return true; }
    // ★ §AB3: `peers` = the bounded address book; `peers all` = + the id-only diagnostic rows (text only, §2.6(a)).
    // C2: any other tail refuses loud rather than being read as `all`.
    if (len == 5 && !strncmp(line, "peers", 5))    { dump_peers(out, /*all=*/false); return true; }
    if (len > 5 && !strncmp(line, "peers ", 6)) {
        const char* a = line + 6; size_t an = len - 6;
        while (an && *a == ' ') { ++a; --an; }
        if (an == 3 && !strncmp(a, "all", 3)) { dump_peers(out, /*all=*/true); return true; }
        out.println(F("> peers err bad_args (usage: peers | peers all)")); return true;
    }
    if (len > 6 && !strncmp(line, "route ", 6))     { handle_route_cmd(line + 6, out); return true; }   // manual route inject/del (testing)
    if (len == 6 && !strncmp(line, "status", 6))   { dump_status(out); return true; }
    if (len == 4 && !strncmp(line, "duty", 4))     { dump_duty(out);   return true; }
    if (len == 6 && !strncmp(line, "limits", 6))   { dump_limits(out); return true; }   // companion anti-spam/headroom snapshot (local-only)
    if (len == 6 && !strncmp(line, "reboot", 6))   { fw_reboot();   return true; }
    if ((len == 13 || (len > 13 && line[13] == ' ')) && !strncmp(line, "factory_reset", 13)) { handle_factory_reset(line + 13, len - 13, out); return true; }
    if (len == 5 && !strncmp(line, "regen", 5))    { do_regen(out); return true; }
    if (len == 3 && !strncmp(line, "ota", 3))      { fw_ota();      return true; }
    if (len >  8 && !strncmp(line, "gateway ", 8)) { handle_gateway(line + 8, out); return true; }
#if MR_N_LAYERS < 2
    if (len >  5 && !strncmp(line, "join ", 5))    { handle_join(line + 5, out);    return true; }   // R6.3 provisioning verbs (normal-node, live)
    // §UI-15 slice 2: the /mrjoin PRESET store. ⓘ It cannot shadow `join ` above — that arm requires a SPACE at
    // index 4, which "joinprofile" does not have — but it is placed after it so the two read in verb order.
    if ((len == 11 || (len > 11 && line[11] == ' ')) && !strncmp(line, "joinprofile", 11)) { handle_joinprofile(line + 11, out); return true; }
    if (len >  7 && !strncmp(line, "create ", 7))  { handle_create(line + 7, out);  return true; }
    if (len >  5 && !strncmp(line, "team ", 5))     { handle_team(line + 5, out);    return true; }   // §mobile 6.1: `team new` (mint) / `team <id>` (join)
#if MR_FEAT_MOBILE
    if (len >  7 && !strncmp(line, "mobile ", 7))   { handle_mobile(line + 7, out);  return true; }   // §mobile console: register/gateways/query/status
#endif
#else   // §config-integrity: create/join are normal-node provisioning -> refuse on the gateway build (mirrors how `gateway` errors on a normal build) — else `create` silently re-provisions the gateway into a managed leaf.
    if ((len > 5 && !strncmp(line, "join ", 5)) || (len > 7 && !strncmp(line, "create ", 7))) {
        out.println(F("> err gateway_build (create/join are normal-node only; use `gateway l0=<layer>:<node>:<sf>:<sfs> l1=…`)"));
        return true;
    }
    // §UI-15 slice 2: the preset store follows join's plane — a gateway is provisioned by `gateway`, never by a
    // stored join profile. ⛔ Its own line rather than a widened condition above: the existing message names the
    // remedy for create/join and would be wrong here.
    if ((len == 11 || (len > 11 && line[11] == ' ')) && !strncmp(line, "joinprofile", 11)) {
        out.println(F("> err gateway_build (joinprofile is normal-node only)"));
        return true;
    }
#endif
    if (len == 5 && !strncmp(line, "leave", 5))    { handle_leave(out);           return true; }
    if (len >  8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8, out); return true; }
    if (len == 3 && !strncmp(line, "cfg", 3))      { dump_cfg(out);    return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "sleep", 5)) { handle_sleep(line + 5, len - 5, out); return true; }
    if ((len == 5 || (len > 5 && line[5] == ' ')) && !strncmp(line, "debug", 5)) { handle_debug(line + 5, len - 5, out); return true; }
    // §UI-10/11 P2 — the OLED preset catalog's administrative family. ⓘ ONE arm for the whole `ui …` namespace, so a
    // future `ui` sub-verb needs no second dispatch line; the sub-verb parse (and the refusal for an unknown one) is
    // the pure unit's. ⛔ It shadows nothing: no other verb in this router begins `ui`.
    // ★★ [[B255]] — **THE GATED-OUT ANSWER IS THIS ROUTER'S OWN ESTABLISHED ONE, AND ⛔ NOT A NEW LEXEME.** Off
    //    `MR_FEAT_OLED` the arm is ABSENT, so a `ui …` line falls through `dispatch()`'s `return false` to the
    //    caller's UNSUPPORTED-VERB answer — `> parse error` on USB (`fw_main.cpp` service_console) and
    //    `{"err":"parse","msg":"unknown_cmd"}` over BLE (ble_dispatch_line's write_err, console_json.cpp:414). That is
    //    EXACTLY how `mobile ` behaves off `MR_FEAT_MOBILE` and `password`/`unlock`/`lock` off `MR_FEAT_RADMIN_ACCEPT`
    //    — two precedents inside this very function — and `help` no longer advertises the family either, so the two
    //    surfaces agree. ⛔ LOUD, NEVER SILENT: the console names the refusal on both transports.
    // ⛔ THE `> err gateway_build (…)` SHAPE 15 LINES UP WAS CONSIDERED AND REJECTED: its lexeme names a
    //    `MR_N_LAYERS >= 2` build, and the `MR_FEAT_OLED=0` set is NOT that set — `xiao_sx1262`, `production`,
    //    `xiao_mobile`, `xiao_esp32s3*` are ordinary normal-node builds with no panel. Reusing that token here would
    //    have been a false statement, and coining a new one was forbidden.
#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm
    if ((len == 2 || (len > 2 && line[2] == ' ')) && !strncmp(line, "ui", 2)) { handle_ui(line + 2, len - 2, out); return true; }
#endif   // MR_FEAT_OLED
    // §RADMIN slice 3 — the two TARGET-STORE families, ACCEPT builds only (R-RA-8). ⓘ ONE arm each for the whole
    // namespace, so a future sub-verb needs no second dispatch line; the sub-verb parse and the refusal for an
    // unknown one are the pure unit's (`handle_ui`'s shape, U3). ⛔ Neither shadows anything: no other verb in this
    // router begins `acl` or `admin-id`.
    // ★★★ THE RECOGNITION IS THE **PURE PREDICATE** `mrfw::admin_primary_is`, ⛔ NOT A HAND-WRITTEN `strncmp`
    //     pair — and that is load-bearing rather than tidy: it is the SAME expression `ble_dispatch_line`'s
    //     R-RA-29 refusal evaluates through `mrfw::admin_verb_owns`, so the router and the guard cannot drift
    //     apart. The slice-0a defect was exactly such a drift — the help router grew argument-bearing forms
    //     while the BLE guard still matched `len == 4` — and `tools/probe_console_sink/ble_guard.py` EXECUTES the
    //     extracted guard against the corpus this predicate is measured on.
    // ⓘ The arm itself is `admin_router_arm` (above), a surface of its own — see the note at its definition for
    //   why the two literals may not sit inline in `dispatch`.
    // ★ EXACT TOKEN BOUNDARIES (space/tab or end): `admin-identity`, `admin-key` (Slice 4's CONTROLLER verb),
    //   `aclx` and `acls` are ⛔ NOT this family and fall through to the unknown-verb answer.
#if MR_FEAT_RADMIN_ACCEPT
    if (admin_router_arm(line, len, out)) return true;
#endif   // MR_FEAT_RADMIN_ACCEPT
    // §RADMIN slice 4 — the two CONTROLLER-STORE families, CLIENT builds only (R-RA-8's other half). ⓘ ONE arm
    // each for the whole namespace; the sub-verb parse and the refusal for an unknown one are the pure unit's.
    // ⛔ Neither shadows anything: no other verb in this router begins `admin-key` or `admin-target`, and the two
    // ACCEPT literals above (`admin-id`, `acl`) are refused by `admin_primary_is`' EXACT token boundary — on the
    // HOST, where BOTH gates are 1, `admin-id` reaches the ACCEPT arm and cannot fall into this one.
    // ★★★ THE RECOGNITION IS THE **PURE PREDICATE** `mrfw::admin_primary_is`, ⛔ NOT A HAND-WRITTEN `strncmp` pair
    //     — the same expression `ble_dispatch_line`'s R-RA-30 refusal reaches through
    //     `mrfw::admin_client_verb_owns`, so the router and the guard cannot drift apart.
#if MR_FEAT_RADMIN_CLIENT
    if (admin_client_router_arm(line, len, out)) return true;
#endif   // MR_FEAT_RADMIN_CLIENT
    if (len == 6 && !strncmp(line, "whoami", 6)) { handle_whoami(out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "lookup", 6)) { handle_lookup(line + 6, len - 6, out); return true; }
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "nameof", 6)) { handle_nameof(line + 6, len - 6, out); return true; }   // §1.3 peer name by hash
    if ((len == 6 || (len > 6 && line[6] == ' ')) && !strncmp(line, "hashof", 6)) { handle_hashof(line + 6, len - 6, out); return true; }
    if ((len == 10 || (len > 10 && line[10] == ' ')) && !strncmp(line, "pull_inbox", 10)) { handle_pull_inbox(line + 10, out); return true; }
    if ((len ==  9 || (len >  9 && line[9]  == ' ')) && !strncmp(line, "mark_read",   9)) { handle_mark_read(line + 9,  out); return true; }
    if ((len ==  7 || (len >  7 && line[7]  == ' ')) && !strncmp(line, "del_msg",     7)) { handle_del_msg(line + 7,   out); return true; }   // §3.5 durable single-record delete
    // §CUSTODY-D: the inbox-only clear, on THIS one dispatch so serial and BLE both reach it. ⛔ Shadows nothing —
    // `joinprofile` is the only other 11-byte verb and does not match this text.
    if ((len == 11 || (len > 11 && line[11] == ' ')) && !strncmp(line, "clear_inbox", 11)) { handle_clear_inbox(line + 11, len - 11, out); return true; }
    return false;
}

// ★★★ §RADMIN-0c — THE ONE TRANSPORT-NEUTRAL LOCAL EXECUTION SEAM. Read the contract in firmware_commands.h; this
// is the body. It is deliberately the WHOLE decision and NOTHING ELSE: one router offer, one parse, one execution,
// and one transport-selected rendering of the typed result.
//
// ⛔ THE FOUR THINGS IT OWNS, and there is no fifth:
//   1. the router-versus-parser fork, ONCE, router-first (the measured-empty intersection is the licence — see the
//      header, and `tools/probe_console_sink/ownership.py` for the permanent gate);
//   2. the typed dispatch of a parsed command to `handle_peerkey` / `handle_peername` / `Node::on_command`;
//   3. the rendering of that typed result in the caller's chosen envelope; and
//   4. a narrow completion telling the caller what happened, so no caller has to scrape output text.
// ⛔ NOT a command-name special case anywhere in here (`cmd.kind` is the parser's TYPED answer, not a re-test of
//    the line), NOT a second verb map, NOT a sink choice, NOT a retained `Command::body`.
//
// ★ WHY THE PEER-BOOK ARMS SIT AHEAD OF `on_command` ON BOTH FORMATS: `handle_peerkey`/`handle_peername` are not
//   formatters — they run `Node::on_command` THEMSELVES and then mirror the record to /mrpeers. Calling
//   `on_command` here first and them second would execute the command twice. Both callers already had exactly this
//   shape; it is preserved verbatim.
// ⓘ THE SIGNATURE STAYS ON ONE LINE ON PURPOSE: `tools/gen_command_inventory.py`'s function-span scanner requires
// the opening brace on the definition line (`_function_spans`), and this function is now a named hop in the wiring
// chain that proves `dispatch`'s and `parse_command`'s transport claims. Wrapping it makes the generator refuse.
LineExec exec_console_line(const char* line, size_t len, LineFormat fmt, Print& stream, char* reply, size_t reply_cap, const CommandContext& ctx) {
    const CommandContextScope scope(ctx);
    LineExec r{};
    r.line_err = meshroute::console::validate_command_line(line, len, ctx.line_max_bytes);
    if (r.line_err != meshroute::console::LineErr::ok) {
        r.outcome = DispatchOutcome::refused;
        r.refuse = RefuseReason::bad_line;
        if (ctx.authority != CommandAuthority::local) return r;  // the remote executor owns its typed terminal
        if (fmt == LineFormat::json) {
            r.state = LineExec::State::buffered;
            r.n = meshroute::console::write_err(reply, reply_cap, "bad_line", meshroute::console::line_err_name(r.line_err));
        } else {
            r.state = LineExec::State::streamed;
            stream.print(F("> err bad_line "));
            stream.print(meshroute::console::line_err_name(r.line_err));
            stream.write(static_cast<uint8_t>('\n'));
        }
        return r;
    }

    // Local output/transport policy is unchanged; only a remote context consults this metadata.
    if (ctx.authority != CommandAuthority::local) {
        const CommandPolicy* policy = command_policy_lookup(line, len);
        if (!policy || !command_authority_admits(*policy, ctx, line, len)) {
            r.outcome = DispatchOutcome::refused;
            r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified;
            return r;
        }
        if (radmin_disruptive_request(policy, ctx)) {
            r.outcome = DispatchOutcome::refused;
            r.refuse = RefuseReason::authority;
#if MR_FEAT_RADMIN_ACCEPT
            const auto terminal = remote_action_prepare(line, len, *policy, ctx, g_remote_action_activation_ms, stream);
            r.action_busy = terminal == meshroute::RemoteTerminal::action_busy;
            if (terminal == meshroute::RemoteTerminal::scheduled) r.outcome = DispatchOutcome::scheduled;
            else if (terminal == meshroute::RemoteTerminal::internal_error) r.outcome = DispatchOutcome::internal_failure;
#endif
            return r; // no public disruptive handler is called under a remote context
        }
    }

    // (1) the console verb router — one offer, through the sink the caller supplied.
    if (dispatch(line, len, stream, ctx.transport)) { r.state = LineExec::State::streamed; r.outcome = DispatchOutcome::completed; return r; }

    // (2) the command parser. ⓘ `cmd.body` borrows into `line`; everything that reads it runs below, inside this
    //     call, while the caller's buffer is still alive.
    meshroute::Command cmd{};
    r.parse_err = meshroute::console::parse_command(line, len, cmd);
    if (r.parse_err == meshroute::console::ParseErr::empty)    { r.state = LineExec::State::empty;     return r; }
    if (r.parse_err != meshroute::console::ParseErr::ok)       { r.state = LineExec::State::unmatched; return r; }
    r.outcome = DispatchOutcome::completed;

    if (fmt == LineFormat::json) {
        // ---- the companion envelope: ONE NDJSON line, staged in the caller's reply buffer (device_ble.h `g_out`).
        r.state = LineExec::State::buffered;
        if (cmd.kind == meshroute::CmdKind::peerkey)  { r.n = handle_peerkey(reply, reply_cap, cmd);  return r; }   // §2/§3: install + persist + contract ack
        if (cmd.kind == meshroute::CmdKind::peername) { r.n = handle_peername(reply, reply_cap, cmd); return r; }   // §AB2: rename + persist + the synchronous ack
        const meshroute::CmdResult cr = g_node.on_command(cmd);
        // ★★ §id-hash S1b (QA finding P1c): `cr.accepted`, NOT `cr.code == queued` alone. `reqpubkey_sent` means
        // "the TX path ACCEPTED it" (owner ruling 2026-08-02), NOT "the on-air request was FLOODED"; the two
        // accepted outcomes that hand the TX path nothing (the hosted-mobile local cache hit, and emit_hash_query's
        // silent early-outs, which now carry their own error codes) fall through to the generic write_ack, which is
        // the honest answer for both. ⚠ ACCEPTANCE IS NOT AIRTIME: a frame accepted into the LBT defer ring reaches
        // the radio when a timer fires; if it dies there, node.cpp's defer arm reports it late (`!!` operator log).
        // ★★ §id-hash S1: the RESULT carries the answer (`dst_hash` = the hash the query flew for, `plane` = which
        // plane resolved it), so this renderer reads it instead of re-deriving it and the two cannot disagree (U1).
        if (cmd.kind == meshroute::CmdKind::reqpubkey && cr.code == meshroute::CmdCode::queued && cr.accepted)
            r.n = meshroute::console::write_reqpubkey_sent(reply, reply_cap, cr.dst_hash, cr.plane);
        else
            r.n = meshroute::console::write_ack(reply, reply_cap, cr);
        return r;
    }

    // ---- the USB console envelope: human text, straight to the supplied sink.
    r.state = LineExec::State::streamed;
    if (cmd.kind == meshroute::CmdKind::peerkey) {          // §2/§3: install + persist + the contract ack
        char jb[80]; const size_t m = handle_peerkey(jb, sizeof jb, cmd);
        stream.write(reinterpret_cast<const uint8_t*>(jb), m);
        return r;
    }
    if (cmd.kind == meshroute::CmdKind::peername) {         // §AB2: rename + persist + the synchronous ack
        // 256 = the BLE g_out size, and it is the WORST CASE not a guess: 29 B envelope + 10 digits of hash + 8 B
        // `,"name":` + 2 quotes + a 32-B name whose every byte escapes to `\u00xx` (6x) = 240 + `}` + '\n' + NUL =
        // 244. A tighter buffer would make JsonBuf::finish() return 0 and the ack vanish SILENTLY (it is
        // overflow-safe, not overflow-loud), which is the failure to avoid.
        char jb[256]; const size_t m = handle_peername(jb, sizeof jb, cmd);
        stream.write(reinterpret_cast<const uint8_t*>(jb), m);
        return r;
    }
    const meshroute::CmdResult cr = g_node.on_command(cmd);
    stream.print(F("> "));
    // ★ §err-reason/B32 (bench-found 2026-07-31): print the CmdCode ITSELF, never a bare `err`. The old ternary
    // collapsed err_no_binding / err_unprovisioned / err_unknown_dst / err_too_large … into ONE indistinguishable
    // `err ctr=`, so a refusal named no reason: `reqpubkey 245` answered `err ctr=0 depth=0` and the operator could
    // not tell which wall he had hit. C2 — printing `err` without the reason is not "loud". cmdcode_name is the ONE
    // mapper (U1, no second switch here) and it is the SAME token the companion's {"ack":"…"} carries, so the text
    // and JSON arms of this very function cannot drift apart. ★ No `err ` word is prefixed and that is deliberate,
    // not an omission: every non-`queued` enumerator's string already begins with `err_` (so does the out-of-range
    // fallback "err_unknown"), so the token self-labels — an invariant this print site cannot test, and which is
    // therefore ASSERTED NATIVELY in test/test_console_json.cpp beside the enum-walker. The success line
    // `queued ctr=N depth=N` is byte-identical to before; only refusals gained a reason.
    stream.print(meshroute::console::cmdcode_name(cr.code)); stream.print(F(" ctr="));
    stream.print(cr.ctr); stream.print(F(" depth=")); stream.print(cr.queue_depth);
    // The send handle for hash/layer-addressed sends (dh != 0 = correlate by hash, not id).
    if (cr.dst_hash)   { stream.print(F(" dh=0x")); stream.print(cr.dst_hash, HEX); }
    if (cr.layer_path) { stream.print(F(" lp=0x")); stream.print(cr.layer_path, HEX); }
    // ★ §id-hash S1 (spec §3-D9): the plane the command executed on. Omitted when 0 (= not plane-scoped), so every
    // other verb's line is byte-identical to before. On `reqpubkey <id>` this is the answer to "which namespace did
    // you just spend airtime in", which a bare `queued` never said.
    if (cr.plane)      { stream.print(F(" plane=")); stream.print(meshroute::console::cmdplane_name(cr.plane)); }
    stream.println();
    // §id-hash S1: the remedy line for a refused reqpubkey, at handle_hashof parity; a `queued` prints nothing.
    print_reqpubkey_hint(stream, cmd, cr);
    return r;
}

// ---- Node / Network screens over BLE (companion Phase 3 — roadmap Theme D) -------------------------------
// Battery (VBAT) read — pins/divider/formula are the authoritative MeshCore XiaoNrf52Board method, using
// OUR variant.h macros (VBAT_ENABLE=D14/P0.14, PIN_VBAT=D32/P0.31/AIN7, ADC_MULTIPLIER=3.0, AREF=3.0 V):
//   mV = adc × ADC_MULTIPLIER × AREF_VOLTAGE / 4.096.
// initVariant() already holds VBAT_ENABLE LOW (divider always enabled — reading costs no extra power).
// An implausible read (USB-only / no cell / floating pin) returns -1 ⇒ the field is OMITTED, never garbage.
// Compile out with -DMR_NO_BATT (or on a board without PIN_VBAT, e.g. Heltec).
static int32_t read_batt_mv() {
#if !defined(NRF52_PLATFORM) || defined(MR_NO_BATT) || !defined(PIN_VBAT)
    return -1;   // non-nRF52 (Heltec/ESP32 uses a different ADC API), compiled out, or no VBAT divider
#else
    static bool adc_ready = false;
    if (!adc_ready) {                              // configure the ADC once (a reference switch needs settling)
        pinMode(VBAT_ENABLE, OUTPUT); digitalWrite(VBAT_ENABLE, LOW);
        pinMode(PIN_VBAT, INPUT);
        analogReadResolution(12);
        analogReference(AR_INTERNAL_3_0);
        delay(2);
        adc_ready = true;
    }
    const int adc = analogRead(PIN_VBAT);
    const int32_t mv = static_cast<int32_t>((adc * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096f);
    return (mv > 2000 && mv < 4500) ? mv : -1;     // 1S-LiPo plausible range; else omit
#endif
}

// Build the rich status snapshot from the device globals (the same data dump_status prints on USB).
meshroute::console::StatusFields make_status_fields() {
    meshroute::console::StatusFields s;
    s.uptime_ms = g_hal.now();
    s.duty_ms   = static_cast<uint32_t>(g_hal.airtime_used_ms(3600000));
    s.txq       = static_cast<uint16_t>(g_hal.txq_depth());
    s.txdrop    = static_cast<uint16_t>(g_hal.txq_drops());
    s.offer_full   = g_node.mobile_offer_ring_full_count();   // §MH-S4b §10: the two host-side OFFER admission counters (see StatusFields)
    s.offer_reject = g_node.mobile_offer_reject_count();
    s.rx        = g_rx_count;
    s.tx        = g_iradio.tx_count();
    s.routes    = g_node.rt_count();
    s.pending   = g_node.has_pending_tx();
    s.lbt       = g_node.config().lbt_enabled;
    s.batt_mv   = read_batt_mv();
    return s;
}
const char* node_state_str() { return g_node.node_id() == 0 ? "unprovisioned" : "operating"; }

// `routes` over BLE: stream one {"ev":"route",...} per table entry then {"ev":"routes_end","count":N}.
void handle_routes(Print& out) {
    const uint64_t now = g_hal.now();
    const uint8_t n = g_node.rt_count();
    for (uint8_t i = 0; i < n; ++i) {
        const meshroute::RtEntry& e = g_node.rt_at(i);
        const meshroute::RtCandidate& c = e.candidates[0];
        meshroute::console::RouteRow r;
        r.dest = e.dest; r.next = c.next_hop; r.hops = c.hops; r.score = c.score;
        r.gw = c.is_gateway; r.leaf = c.learned_leaf;
        r.age_ms = static_cast<uint32_t>(now - c.last_seen_ms); r.cand = e.n;
        const size_t m = meshroute::console::write_route(s_inbox_jb, sizeof s_inbox_jb, r);
        if (m) out.write(s_inbox_jb, m);
    }
    const size_t m = meshroute::console::write_routes_end(s_inbox_jb, sizeof s_inbox_jb, n);
    if (m) out.write(s_inbox_jb, m);
}

// Build the cfg extras (device globals not in NodeConfig) for write_cfg.
meshroute::console::CfgExtras make_cfg_extras() {
    meshroute::console::CfgExtras x;
    x.node_id    = g_node.node_id();
    x.freq_hz    = static_cast<uint32_t>(g_freq_mhz * 1000000.0 + 0.5);
    x.tx_power   = g_tx_power;
    x.duty_x1000 = static_cast<uint32_t>(g_node.config().duty_cycle * 1000.0 + 0.5);
    x.ble_mode   = g_ble_mode == 0 ? "off" : g_ble_mode == 1 ? "on" : "periodic";
    x.ble_period = g_ble_period_min;
    x.ble_pin    = g_ble_pin;
    const auto activation = remote_activation_resolve(g_remote_action_activation_ms, remote_activation_live_inputs());
    x.remote_action_activation_ms = activation.effective_ms;
    x.remote_action_activation_state = activation_state_name(activation.state);
    x.lat_e7     = g_lat_e7;
    x.lon_e7     = g_lon_e7;
    x.team_ch_key = g_node.team_channel_key_present();   // §team-ch-key (T-K1b): the JSON twin of dump_cfg's `team_ch_key=` line — BOOLEAN lock state only, never the pair
    return x;
}

// ★★ UI-7: the TYPED command executor (declared in firmware_commands.h — read the contract there). It is the exact
// typed parser/Node path, without text rendering. Since Slice 0c the transport callers use exec_console_line;
// the panel keeps this distinct contract. Slice 6 validates its bytes with the same shared validator.
// ⓘ `parse_command` leaves `cmd.body` BORROWING into `line`, so `on_command` runs inside this function while `line` is
//   still the caller's live buffer — never after a return.
ExecResult exec_command(const char* line, size_t len) {
    ExecResult r{};
    if (!line || len == 0) { r.parse_err = meshroute::console::ParseErr::empty; return r; }
    r.line_err = meshroute::console::validate_command_line(line, len, meshroute::console::local_command_max_bytes);
    if (r.line_err != meshroute::console::LineErr::ok) return r;
    meshroute::Command cmd{};
    r.parse_err = meshroute::console::parse_command(line, len, cmd);
    if (r.parse_err != meshroute::console::ParseErr::ok) return r;
    r.ok = true;
    r.result = g_node.on_command(cmd);
    return r;
}

}  // namespace mrfw
