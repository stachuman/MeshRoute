// MeshRoute — src/device_ble.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// BLE companion transport (Nordic UART Service over the S140 SoftDevice) — the iOS/phone-facing twin of the
// USB-CDC console. XIAO nRF52840 ONLY; an inert no-op stub on ESP32 (Heltec) + the native/host build, so
// fw_main's loop() calls compile unchanged on every target. PURE TRANSPORT: it owns Bluefruit + the BLEUart
// + the advertising-window policy + the inbound line buffer, and knows NOTHING of Node/commands — fw_main
// supplies a DispatchFn (one line -> one JSON reply) and hands it pre-formatted Push JSON to TX. Mirrors the
// device_nv.h / device_rng.h board guards. The companion link speaks the NDJSON schema of
// docs/specs/2026-05-30-device-console-design.md §4 (the same one the sim's FirmwareNode emits).
//
// REALITY SPLIT: compile-verified under the xiao env; the on-metal advertise / pair / RX-TX, and the
// LoRa-timing soak under a live S140 (spec §8.1), are the user's bench (Steps 6 + 9). The GATT chars are
// UNPAIRED until Step 6 secures them — begin() prints that loudly; this is a documented phase boundary, not
// a silent fallback.
//
// KEYSTONE: begin() is the ONLY caller of Bluefruit.begin(), and it sets mrrng::sd_enabled()=true IMMEDIATELY
// after. Once the SoftDevice owns the radio, the bare-metal NRF_RNG path in device_rng.h would HardFault, so
// every later regen()/seed draw must route through the SD entropy pool (device_rng.h / spec §8.1).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol_constants.h" // meshroute::protocol:: — the NAMED terms the inbound line capacity is derived from
#include "../lib/console/console_line.h" // binds the remote capacity even in standalone device-header probes

#if defined(ARDUINO) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #define MRBLE_NRF52 1
#endif

namespace mrble {

// ===================================================================================================================
// ★★ THE INBOUND LINE CAPACITY IS DERIVED FROM THE CONSOLE GRAMMAR — NEVER A LITERAL (R-RA-24' bound 2).
//
// WHY IT MOVED. This buffer is the BLE transport's admission edge, and whatever it refuses the operator sees as
// `line_too_long` — a TRANSPORT verdict — instead of the verb's own named result. At 160 B it refused the longest
// product lines outright: a full 239-byte DM body (`send`) and any `send_layer` carrying a real body never reached
// `parse_command` at all. USB (`fw_main.cpp`, 1024 B) has no such edge, so the two transports disagreed about which
// commands exist. Closing that disagreement is the whole change; the intake state machine below is untouched.
//
// ★ THE DEFINITION IS SYNTACTIC (owner-ruled 2026-09-04): the storage admits THE CANONICAL PARSER-ACCEPTED SPELLING
// OF EACH PRODUCT VERB — EACH ACCEPTED OPTION AT MOST ONCE — CARRYING THE LARGEST BODY ITS CARRIER ADMITS. It is
// deliberately NOT "every line that can succeed": the seal's overhead, `-l` being refused on a cross-layer send, a
// plane that needs a key, are SEMANTIC rules owned by `Node::on_command`, which refuses each of them BY NAME. Pricing
// them here would make a transport constant depend on protocol semantics and would hide those named refusals behind
// `line_too_long` — the exact failure this capacity exists to remove.
// ⚠ SAID PLAINLY SO IT IS NOT READ AS A PROMISE: the two BINDING lines below do NOT succeed. The maximal `send_layer`
// spelling returns `err_unsupported` (`-l` is refused on a cross-layer send — node.cpp's send_layer arm emits
// `location_refused`), and the maximal `send` spelling carries `-e`, so it refuses `SealOutcome::too_large`. They are
// TRANSPORT positives: their bytes must reach the dispatcher so the NAMED refusal is what comes back. The longest
// line that actually queues is the plaintext `send_layer 0x… 255,255,255 "<226-B body>" -a -K`.
// ⓘ The grammar also admits UNBOUNDED permissive spellings — a repeated flag (`parse_send_tail` de-duplicates only
// the body), runs of whitespace (`skip_ws`), leading zeros in a numeric token. Those have NO finite syntax maximum,
// so they are bounded by this transport BY DESIGN and are not (and cannot be) priced below.
//
// ⛔ NO NAKED NUMBER BELONGS IN THIS BLOCK. Every term is `sizeof("literal") - 1` or a named protocol constant, so
// deleting a flag, renaming a verb or moving `gw_env_max_hops` moves the buffer automatically. Two earlier passes
// priced `send` alone and derived 269, then 272; the verb that actually binds is `send_layer`. A literal freezes
// that class of mistake — which is why the checks below assert the RELATIONSHIP, not the value.
// ===================================================================================================================

// ---- shared grammar atoms -----------------------------------------------------------------------------------------
constexpr size_t kFlagTermBytes  = sizeof(" -a") - 1;   // " -X": the separating space + '-' + one letter (lone token)
constexpr size_t kBodyOpenBytes  = sizeof(" \"") - 1;   // the separating space + the opening quote
constexpr size_t kBodyCloseBytes = sizeof("\"") - 1;    // the closing quote
// `parse_hex32_0x` wants the `0x` prefix; `parse_hex32_tok` refuses more than 8 hex digits (one per u32 nibble).
constexpr size_t kHashTokenBytes = (sizeof("0x") - 1) + 2 * sizeof(uint32_t);

// ---- producer 1: `send <0xhash> "<body>" -a -e -t -K -l` -----------------------------------------------------------
// The by-hash call site accepts all five flags (allow_a, allow_e=by_hash, team, no_intro, loc); the body term is the
// DM semantic authority, which is what `Node::on_command` admits.
constexpr size_t kSendFlagCount    = 5;
constexpr size_t kSendLineMaxBytes = (sizeof("send ") - 1)
                                   + kHashTokenBytes
                                   + kBodyOpenBytes
                                   + meshroute::protocol::dm_max_body_bytes
                                   + kBodyCloseBytes
                                   + kSendFlagCount * kFlagTermBytes;

// ---- producer 2: `send_layer <0xhash> <l1,…,ln> "<body>" -a -e -K -l` — THE BINDING ONE ----------------------------
// `-t`/`-g` are refused on this verb (team/global are passed as nullptr), so four flags, not five.
// The hop path: the parser caps `hop_count` at `gw_env_max_hops - 1` and each hop id at 1..255 (three decimal digits),
// with one comma between hops. `originate_layer_path` then PREPENDS our own layer, so n_layers == gw_env_max_hops at
// that maximum. Depth is what binds: one more hop costs 4 line bytes (",255") and buys back only 1 body byte, so the
// DEEPEST path is also the LONGEST line.
constexpr size_t kLayerHopsMax      = meshroute::protocol::gw_env_max_hops - 1;
constexpr size_t kLayerHopDigitsMax = 3;                                            // "255" — the parser rejects > 255
constexpr size_t kLayerPathBytes    = kLayerHopsMax * kLayerHopDigitsMax + (kLayerHopsMax - 1);   // digits + commas
// ⚠ TRANSITIONAL MIRROR — there is NO named compile-time authority for the cross-layer body cap today.
// `pack_unicast_inner` (lib/core/frame_codec.cpp) sizes it at RUNTIME from the flag set `enqueue_cross_layer` uses
// (CROSS_LAYER | DST_HASH | SOURCE_HASH, lib/core/node_mac.cpp) against `TxItem.inner[]`, which is
// `max_payload_bytes_hard_cap`. ⛔ `data_inner_cap()` in frame_codec.h is the OUTER cap and is NOT this number.
// The five sizing terms are mirrored EXACTLY here, and `tools/probe_ble_line` PINS the mirror by executing the real
// packer at this cap (must succeed) and at cap+1 (must refuse). Extracting a shared constexpr inner-overhead helper
// beside the packer is a named lib/core follow-up, deliberately outside this slice.
constexpr size_t kXlInnerOriginBytes     = 1;                                          // [origin]
constexpr size_t kXlInnerDstHashBytes    = 4;                                          // DATA_FLAG_DST_HASH
constexpr size_t kXlInnerSourceHashBytes = 4;                                          // DATA_FLAG_SOURCE_HASH
constexpr size_t kXlInnerPathHdrBytes    = 2;                                          // [n_layers][cur]
constexpr size_t kXlInnerPathIdBytes     = meshroute::protocol::gw_env_max_hops;        // one id per layer, at max depth
constexpr size_t kXlInnerOverheadBytes   = kXlInnerOriginBytes + kXlInnerDstHashBytes + kXlInnerSourceHashBytes
                                         + kXlInnerPathHdrBytes + kXlInnerPathIdBytes;
constexpr size_t kSendLayerBodyCapBytes  = meshroute::protocol::max_payload_bytes_hard_cap - kXlInnerOverheadBytes;
constexpr size_t kSendLayerFlagCount     = 4;
constexpr size_t kSendLayerLineMaxBytes  = (sizeof("send_layer ") - 1)
                                         + kHashTokenBytes
                                         + (sizeof(" ") - 1)
                                         + kLayerPathBytes
                                         + kBodyOpenBytes
                                         + kSendLayerBodyCapBytes
                                         + kBodyCloseBytes
                                         + kSendLayerFlagCount * kFlagTermBytes;

// ---- producer 3: `remote <target> -e using=key9 -a -- <cmd>` (R-RA-18) — a CAPACITY PIN, not an implementation -----
// ⚠ Both widths below are TRANSITIONAL and labelled as such. The management-target label has no production authority
// yet (the target-book record is deferred to its own storage slice, and device_nv.h's node-name field is a DIFFERENT
// record — do not attribute it there); `using=keyN` is grounded in the design's exact slot names `key0`..`key9`, so a
// wider key-name grammar would invalidate that term. 201 mirrors R-RA-24' bound 3 (the smallest authenticated
// carrier's command capacity); console_line.h owns the validator bound and the assertion below binds this mirror.
constexpr size_t kRemoteTargetLabelBytes = 32;    // TRANSITIONAL design pin — replace with the target-book constant
constexpr size_t kRemoteCommandMaxBytes  = 201;   // TRANSITIONAL mirror of console_line.h's remote_command_max_bytes
static_assert(kRemoteCommandMaxBytes == meshroute::console::remote_command_max_bytes,
              "the BLE remote tail mirror must equal the shared validator's remote bound");
constexpr size_t kRemoteWrapperMaxBytes  = (sizeof("remote ") - 1)
                                         + kRemoteTargetLabelBytes
                                         + (sizeof(" -e") - 1)
                                         + (sizeof(" using=key9") - 1)
                                         + (sizeof(" -a") - 1)
                                         + (sizeof(" -- ") - 1);
constexpr size_t kRemoteLineMaxBytes     = kRemoteWrapperMaxBytes + kRemoteCommandMaxBytes;

// ---- the storage ---------------------------------------------------------------------------------------------------
constexpr size_t larger_of(size_t a, size_t b) { return a > b ? a : b; }
constexpr size_t kProductLineMaxBytes = larger_of(larger_of(kSendLineMaxBytes, kSendLayerLineMaxBytes),
                                                 kRemoteLineMaxBytes);
constexpr size_t kLineStorageBytes    = kProductLineMaxBytes + 1;   // + the NUL dispatch_current_line() writes

static_assert(kLineStorageBytes == kProductLineMaxBytes + 1,
              "the storage is EXACTLY the grammar maximum plus its NUL — no slack, no literal");
static_assert(kSendLayerLineMaxBytes == kProductLineMaxBytes,
              "`send_layer` is the binding producer; if another verb overtakes it, RE-DERIVE the maximum here "
              "rather than padding the buffer");
static_assert(kLineStorageBytes > kSendLineMaxBytes,
              "the maximal by-hash `send` line (a full dm_max_body_bytes body + its five accepted flags) must fit "
              "with its NUL, or a complete DM cannot be submitted over BLE");
static_assert(kLineStorageBytes > kRemoteLineMaxBytes,
              "the longest R-RA-18 `remote` wrapper plus its command tail must fit with its NUL");
static_assert(kSendLayerBodyCapBytes < meshroute::protocol::dm_max_body_bytes,
              "the cross-layer CARRIER is stricter than the DM semantic cap — that is why the send_layer term is "
              "derived from pack_unicast_inner's overhead and not from dm_max_body_bytes");

// fw_main supplies this: handle ONE inbound console line, writing a single NDJSON response line into `out`
// (NUL-terminated, '\n'-ended); returns bytes written (0 = no reply). Keeps device_ble.h free of any
// Node / command / console_json dependency (fw_main owns g_node + the encoders).
using DispatchFn = size_t (*)(const char* line, size_t len, char* out, size_t cap);

#if defined(MRBLE_NRF52)
bool begin(uint8_t mode, uint8_t period_min, uint32_t pin, const char* name, DispatchFn dispatch);
void on_tick(uint64_t now_ms);              // advertising-window policy: start/stop advertising per ble_mode
void service_rx();                          // poll the NUS RX FIFO -> line buffer -> dispatch -> TX the reply
void tx_line(const char* s, size_t n);      // TX one pre-formatted JSON line to the client (no-op if none)
bool connected();                           // a companion is connected (used to inhibit idle light-sleep)
#else
// Inert on ESP32 + native: every entry is a no-op so fw_main compiles unchanged on all targets.
inline bool begin(uint8_t, uint8_t, uint32_t, const char*, DispatchFn) { return false; }
inline void on_tick(uint64_t) {}
inline void service_rx() {}
inline void tx_line(const char*, size_t) {}
inline bool connected() { return false; }
#endif

}  // namespace mrble


#if defined(MRBLE_NRF52)
// ===== device implementation (XIAO nRF52840) — header-inline, included by the one device TU (fw_main) =====
#include <bluefruit.h>
#include "companion_policy.h"   // meshroute::CompanionPolicy / BleMode (lib/core) — the off/on/periodic scheduler
#include "device_rng.h"         // mrrng::sd_enabled() — the SD-RNG keystone flag
#include "console_sink.h"       // `mrcon` guarded sink (the BLE-path debug prints route through it too)
#include <string.h>
#include <stdio.h>              // snprintf — format the 6-digit passkey

namespace mrble {
namespace {

constexpr uint32_t kAdvWindowMs = 30000;    // periodic-mode advertising window (30 s); matches the CompanionPolicy test

BLEUart                    g_bleuart;        // Nordic UART Service (RXD write / TXD notify)
meshroute::CompanionPolicy g_policy;         // when to advertise (off/on/periodic)
DispatchFn                 g_dispatch = nullptr;
bool                       g_started     = false;
// Connection count, shared with the Bluefruit connect/disconnect callbacks. On the single-core nRF52840 those
// callbacks run in a higher-priority context that PREEMPTS loop() (not a parallel core), so a `volatile` byte
// is the correct, sufficient idiom: volatile forces a fresh load (no register caching) and a byte store is
// atomic — no memory barrier / critical section is needed (adding one would be cargo-cult). Do NOT "fix".
volatile uint8_t           g_conn_count  = 0;
volatile uint16_t          g_conn_handle = BLE_CONN_HANDLE_INVALID;   // for getMtu() — chunk long tx_line replies
char                       g_pin_str[7]  = {0}; // the 6-digit MITM passkey as a string. setPIN() stores it BY
                                                // POINTER (no copy), so it MUST outlive pairing -> a static.

char                       g_line[kLineStorageBytes];   // inbound line: one derived product line + its NUL
size_t                     g_pos        = 0;
bool                       g_overflow   = false;
char                       g_out[256];        // outbound JSON scratch (one NDJSON line)

// Bluefruit callbacks — run in the Bluefruit event-task context (NOT a hard ISR), so mrcon.print is safe here
// (the stock pairing_pin.ino example prints from these too). Concise on-USB signal for the bench bring-up.
void on_connect(uint16_t h)            { if (g_conn_count < 255) ++g_conn_count; g_conn_handle = h; mrcon.println(F("[ble] connected (pairing required before GATT)")); }
void on_disconnect(uint16_t, uint8_t r){ if (g_conn_count > 0)  --g_conn_count; g_conn_handle = BLE_CONN_HANDLE_INVALID; mrcon.print(F("[ble] disconnected reason=0x")); mrcon.println(r, HEX); }
void on_secured(uint16_t)             { mrcon.println(F("[ble] link secured (paired/bonded)")); }
void on_pair_complete(uint16_t, uint8_t st) { mrcon.print(F("[ble] pairing ")); mrcon.println(st == BLE_GAP_SEC_STATUS_SUCCESS ? F("OK") : F("FAILED")); }

void dispatch_current_line() {
    g_line[g_pos] = '\0';
    if (g_overflow) {                                       // a line longer than the buffer -> fail loud, drop it
        static const char kTooLong[] = "{\"err\":\"line_too_long\"}\n";
        tx_line(kTooLong, sizeof(kTooLong) - 1);
        g_overflow = false; g_pos = 0; return;
    }
    if (g_dispatch && g_pos > 0) {
        const size_t n = g_dispatch(g_line, g_pos, g_out, sizeof g_out);
        if (n) tx_line(g_out, n);
    }
    g_pos = 0;
}

}  // namespace

inline bool begin(uint8_t mode, uint8_t period_min, uint32_t pin, const char* name, DispatchFn dispatch) {
    if (mode == 0) return false;                            // off -> never bring the SoftDevice up
    g_dispatch = dispatch;

    // FIX: the default ATT MTU is 23 (20-B notification payload), so a 125-B `ready` / a long msg_recv
    // splits across ~7 notifications and only the first survives the SoftDevice's tiny default HVN queue
    // — the client gets a truncated line that never decodes. BANDWIDTH_MAX raises the MTU (to 247) AND
    // enlarges the notify queue. MUST precede Bluefruit.begin(). NOTE: a single notification still carries
    // only (MTU − 3) bytes, so a reply LONGER than that (cfg ~290 B, a wide status, a long inbox_dm) is
    // chunked across notifications by tx_line() — the app reassembles by '\n'.
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin(/*prph=*/1, /*central=*/0);
    // <-- KEYSTONE: Bluefruit.begin() just enabled the SoftDevice, so the SD now owns the radio. Set the flag
    // HERE, before any failure-return below (e.g. setPIN failing): once the SD is up it stays up, so a later
    // regen MUST use the SD RNG path regardless. Do NOT move this after setPIN — that would leave a setPIN-fail
    // path with the SD up but the flag false -> regen HardFaults on the bare-metal NRF_RNG.
    mrrng::sd_enabled() = true;
    Bluefruit.setTxPower(4);                               // dBm (nRF52840 supports up to +8; 4 is a safe default)
    if (name && name[0]) Bluefruit.setName(name);
    Bluefruit.autoConnLed(false);                          // don't assume a status LED is wired on the XIAO

    // Security (spec §A.3, MANDATORY): a STATIC 6-digit MITM passkey. setPIN() auto-sets mitm=1 / legacy-SC /
    // IO=DisplayOnly, so iOS shows a passkey-ENTRY prompt (the phone types this PIN). The string is stored BY
    // POINTER by the SoftDevice (g_pin_str is a static, so it outlives pairing). pin is cfg-validated 0..999999;
    // %06lu zero-pads to the required 6 chars (% 1000000 is a defensive clamp). Bonding auto-persists to InternalFS.
    snprintf(g_pin_str, sizeof g_pin_str, "%06lu", (unsigned long)(pin % 1000000u));
    if (!Bluefruit.Security.setPIN(g_pin_str)) return false;   // fail loud: refuse to serve INSECURE BLE (none > open)
    Bluefruit.Security.setPairCompleteCallback(on_pair_complete);
    Bluefruit.Security.setSecuredCallback(on_secured);

    Bluefruit.Periph.setConnectCallback(on_connect);
    Bluefruit.Periph.setDisconnectCallback(on_disconnect);

    // The §A.3 GATT gate: RXD/TXD require an encrypted + MITM-bonded link before a client can write/subscribe.
    // setPermission is inherited from BLEService and is the floor for both NUS characteristics — it MUST be set
    // BEFORE begin(). iOS pairs on the first touch of an encrypted char (its TX-notify subscribe). With this set,
    // the SoftDevice rejects unpaired RXD writes, so service_rx() only ever sees commands from a bonded client.
    g_bleuart.setPermission(SECMODE_ENC_WITH_MITM, SECMODE_ENC_WITH_MITM);
    g_bleuart.begin();

    // Advertising packet: iOS scans by the NUS SERVICE UUID, so addService(bleuart) is MANDATORY — it embeds
    // 6E400001-B5A3-F393-E0A9-E50E24DCCA9E into the advert. The human-readable name rides the scan response.
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(g_bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);       // auto re-advertise after a disconnect; on_tick still owns
                                                           //   the window (it stops a stray re-advert next idle tick)
    Bluefruit.Advertising.setInterval(32, 244);            // 0.625 ms units: 20 ms fast / 152.5 ms slow (Apple-friendly)
    Bluefruit.Advertising.setFastTimeout(30);

    g_policy.configure(static_cast<meshroute::BleMode>(mode), period_min, kAdvWindowMs);
    g_started = true;
    return true;
}

inline void on_tick(uint64_t now_ms) {
    if (!g_started) return;
    const meshroute::CompanionPolicy::Tick t = g_policy.on_tick(now_ms);
    // Reconcile against the ACTUAL stack state (isRunning), NOT a shadow flag: restartOnDisconnect(true) can
    // auto-restart advertising from the SoftDevice behind our back, which a shadow bool would miss (leaving a
    // stray advert that never gets stopped). Never touch advertising while a client is connected — prph=1 means
    // a single link, and the stack already stops advertising on connect, so there is nothing to start/stop.
    const bool running = Bluefruit.Advertising.isRunning();
    if      (t.should_advertise  && !running && g_conn_count == 0) Bluefruit.Advertising.start(0);  // 0 = no timeout
    else if (!t.should_advertise &&  running && g_conn_count == 0) Bluefruit.Advertising.stop();
}

inline void service_rx() {
    if (!g_started) return;
    while (g_bleuart.available()) {
        const char c = static_cast<char>(g_bleuart.read());
        if (c == '\r') continue;
        if (c == '\n')                          { dispatch_current_line(); }
        else if (g_pos < sizeof(g_line) - 1)    { g_line[g_pos++] = c; }
        else                                    { g_overflow = true; }   // keep eating until '\n', then fail loud
    }
}

inline void tx_line(const char* s, size_t n) {
    if (g_conn_count == 0) return;
    // A single g_bleuart.write() emits ONE notification of at most (ATT MTU − 3) bytes; a longer line (cfg
    // ~290 B, a wide status, a long inbox_dm) would lose its tail. Chunk by the negotiated MTU so EACH write
    // is a clean single notification; the app's LineAccumulator reassembles by '\n' regardless of the splits.
    BLEConnection* conn = Bluefruit.Connection(g_conn_handle);
    const uint16_t mtu  = conn ? conn->getMtu() : 23;
    const size_t   chunk = (mtu > 3) ? static_cast<size_t>(mtu - 3) : 20;
    size_t off = 0;
    while (off < n) {
        const size_t len = (n - off < chunk) ? (n - off) : chunk;
        g_bleuart.write(reinterpret_cast<const uint8_t*>(s + off), len);
        off += len;
    }
}

inline bool connected() { return g_conn_count > 0; }

}  // namespace mrble
#endif  // MRBLE_NRF52
