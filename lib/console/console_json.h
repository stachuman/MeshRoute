// MeshRoute — lib/console/console_json.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Bounded, heap-free NDJSON line writers shared by the device console and the
// sim's FirmwareNode (one serializer, two backends — schema cannot drift).
// hal.h discipline: no std::string/json/heap, C++17-includable, -fno-exceptions.
// See docs/specs/2026-05-30-device-console-design.md.
#pragma once
#include "command.h"   // CmdResult, Push, CmdCode, PushKind  (lib/core)
#include "hal.h"       // EventField                          (lib/core)
#include "node.h"      // NodeConfig                          (lib/core)
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

// Bounded, heap-free JSON writer. Every append is overflow-safe: once `cap`
// is reached `overflow` latches and further appends are no-ops; finish()
// then returns 0 so callers never emit a truncated line.
struct JsonBuf {
    char*  buf;
    size_t cap;
    size_t pos = 0;
    bool   overflow = false;
    JsonBuf(char* b, size_t c) : buf(b), cap(c) {}
    void   ch(char c);
    void   lit(const char* s);            // raw literal, no escaping
    void   str(const char* s, size_t n);  // quoted, JSON-escaped string value
    void   key(const char* k);            // `"k":`
    void   i64(int64_t v);
    void   u32(uint32_t v);
    void   f64(double v);
    size_t finish();                       // append '\n', NUL-terminate if room; 0 if overflow
};

// Complete NDJSON line serializers (return bytes written incl. '\n', 0 on overflow).
size_t write_ack   (char* buf, size_t cap, const CmdResult& r);
size_t write_push  (char* buf, size_t cap, const Push& p, const NodeConfig* cfg = nullptr);   // cfg: config_adopted membership fields (R6.3)
size_t write_event (char* buf, size_t cap, const char* type, const EventField* f, size_t n);
size_t write_log   (char* buf, size_t cap, const char* msg);
size_t write_err   (char* buf, size_t cap, const char* code, const char* msg);  // msg nullable
size_t write_ready (char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode,
                    uint32_t inbox_epoch, uint64_t now_ms,
                    const char* name = nullptr, size_t name_len = 0,    // /mrid node name; omitted when empty
                    const uint8_t* ed_pub = nullptr,                    // §4: full Ed25519 pubkey (64 hex) for the QR `p`; omitted when null
                    uint8_t duty_pct = 0, uint32_t duty_avail_ms = 0);  // duty readout: snapshot so the app shows it on connect (refreshed via `duty`)
// `duty` query reply (companion polls it for the silent-countdown banner): {"ev":"duty","pct":,"avail_ms":,"enabled":}.
size_t write_duty  (char* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled);
// ---- Node / Network screens over BLE (companion Phase 3 — roadmap Theme D) ----------------------
// Runtime telemetry not in NodeConfig, passed individually so console_json stays dependency-light.
// batt_mv < 0 ⇒ no battery reader ⇒ the field is OMITTED (never a wrong/garbage voltage).
struct StatusFields {
    uint64_t uptime_ms = 0;
    uint32_t duty_ms   = 0;     // airtime used in the last hour (ms)
    uint16_t txq       = 0;     // async-TX queue depth (idles at 0)
    uint16_t txdrop    = 0;     // outbound-queue overflow drops
    uint32_t rx        = 0;     // frames received
    uint32_t tx        = 0;     // frames transmitted
    uint8_t  routes    = 0;     // route-table size
    bool     pending   = false; // a flight in progress
    bool     lbt       = false; // listen-before-talk enabled
    int32_t  batt_mv   = -1;    // battery millivolts; <0 = unavailable (omit)
};
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state,
                    const StatusFields& s);

// One route-table row + the stream terminator (mirrors the inbox pull pattern; streamed via tx_line).
struct RouteRow {
    uint8_t  dest = 0, next = 0, hops = 0;
    int16_t  score = 0;        // Q4 dB route score
    bool     gw = false;
    uint8_t  layer = 0;
    uint32_t age_ms = 0;       // since last_seen
    uint8_t  cand = 0;         // candidate next-hops held (1..K)
};
size_t write_route    (char* buf, size_t cap, const RouteRow& r);
size_t write_routes_end(char* buf, size_t cap, uint32_t count);

// The node config as one JSON object (read-only display v1). Device extras not in NodeConfig are
// pre-converted to integers here (no float on the wire — newlib-nano printf can't do %f/%lld).
struct CfgExtras {
    uint8_t  node_id   = 0;
    uint32_t freq_hz   = 0;     // operating frequency in Hz (mhz×1e6, computed device-side)
    int8_t   tx_power  = 0;     // dBm
    uint32_t duty_x1000 = 0;    // duty_cycle×1000 (0.1 → 100); app shows /10 %
    const char* ble_mode = "off";
    uint16_t ble_period = 0;    // periodic advertising period (minutes)
    uint32_t ble_pin   = 0;
    int32_t  lat_e7    = 0;     // node location, degrees × 1e7 (0 = unset)
    int32_t  lon_e7    = 0;
};
size_t write_cfg(char* buf, size_t cap, const NodeConfig& c, const CfgExtras& x);

// Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md). The pull stream = inbox_dm* then
// inbox_channel* (oldest-first) then inbox_end; mark_read acks via write_inbox_marked. Fields individual to
// keep this file free of inbox.h.
size_t write_inbox_dm     (char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint16_t ctr,
                           uint32_t sender_hash, uint64_t rx_ms, const char* body, size_t body_len,
                           bool enc = false);   // §8b: "enc":true when the DM was delivered sealed; omitted (=false) otherwise
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len);
size_t write_inbox_end    (char* buf, size_t cap, uint32_t dm_seq, uint32_t chan_seq, uint32_t epoch, uint32_t count,
                           uint64_t now_ms);
size_t write_inbox_marked (char* buf, size_t cap, const char* kind, uint32_t seq);

const char* cmdcode_name(CmdCode c);
const char* pushkind_name(PushKind k);

}  // namespace meshroute::console
