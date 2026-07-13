// MeshRoute — lib/console/console_binary.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// TLV encoders for the REMOTE (rcmd) data responses — compact, forward-compatible, <=241 B DM. Serializes the same
// field structs as console_json.h. Remote-only (USB/BLE keep text/JSON). Frame = [ver=1][msg_type] + [tag][len][value LE];
// decoders skip unknown tags. No heap: every buffer is caller-owned. See docs/superpowers/specs/2026-07-13-remote-binary-response-encoders-design.md.
#pragma once
#include <cstdint>
#include <cstddef>
#include "console_json.h"   // LimitsFields, StatusFields, RouteRow, CfgExtras, NodeConfig

namespace meshroute::console::bin {

inline constexpr uint8_t VER = 1;
inline constexpr uint8_t MSG_STATUS = 0x01, MSG_CFG = 0x02, MSG_DUTY = 0x03, MSG_LIMITS = 0x04,
                         MSG_FAULTS = 0x05, MSG_ROUTES = 0x06, MSG_GATEWAY = 0x07;
inline constexpr uint8_t TAG_TRUNCATED = 0xFE;   // u8 = records omitted (saturating); 0/absent = complete

// ---- writer (returns false on overflow; `off` unchanged on failure) ----
inline bool put_bytes(uint8_t* b, size_t cap, size_t& off, uint8_t tag, const uint8_t* v, uint8_t n) {
    if (off + 2u + n > cap) return false;
    b[off] = tag; b[off + 1] = n;
    for (uint8_t i = 0; i < n; ++i) b[off + 2 + i] = v[i];
    off += 2u + n; return true;
}
inline bool put_u8 (uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint8_t v)  { return put_bytes(b, cap, off, tag, &v, 1); }
inline bool put_u16(uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint16_t v) { uint8_t t[2] = {uint8_t(v), uint8_t(v >> 8)}; return put_bytes(b, cap, off, tag, t, 2); }
inline bool put_u32(uint8_t* b, size_t cap, size_t& off, uint8_t tag, uint32_t v) { uint8_t t[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; return put_bytes(b, cap, off, tag, t, 4); }
inline bool put_i16(uint8_t* b, size_t cap, size_t& off, uint8_t tag, int16_t v)  { return put_u16(b, cap, off, tag, uint16_t(v)); }
inline bool put_i32(uint8_t* b, size_t cap, size_t& off, uint8_t tag, int32_t v)  { return put_u32(b, cap, off, tag, uint32_t(v)); }

inline size_t frame_begin(uint8_t* b, size_t cap, uint8_t msg_type) { if (cap < 2) return 0; b[0] = VER; b[1] = msg_type; return 2; }

// ---- reader ----
struct TlvReader { const uint8_t* p; size_t len; size_t off; uint8_t ver; uint8_t msg_type; };
inline bool reader_init(TlvReader& r, const uint8_t* buf, size_t len) {
    if (len < 2 || !buf) return false;
    r.p = buf; r.len = len; r.ver = buf[0]; r.msg_type = buf[1]; r.off = 2;
    return true;
}
inline bool reader_next(TlvReader& r, uint8_t& tag, const uint8_t*& val, uint8_t& n) {
    if (r.off + 2u > r.len) return false;
    tag = r.p[r.off]; n = r.p[r.off + 1];
    if (r.off + 2u + n > r.len) return false;     // value overruns -> malformed
    val = r.p + r.off + 2; r.off += 2u + n; return true;
}
inline uint8_t  get_u8 (const uint8_t* v, uint8_t n) { return n >= 1 ? v[0] : 0; }
inline uint16_t get_u16(const uint8_t* v, uint8_t n) { uint16_t x = 0; for (uint8_t i = 0; i < n && i < 2; ++i) x |= uint16_t(v[i]) << (8 * i); return x; }
inline uint32_t get_u32(const uint8_t* v, uint8_t n) { uint32_t x = 0; for (uint8_t i = 0; i < n && i < 4; ++i) x |= uint32_t(v[i]) << (8 * i); return x; }
inline int16_t  get_i16(const uint8_t* v, uint8_t n) { return int16_t(get_u16(v, n)); }
inline int32_t  get_i32(const uint8_t* v, uint8_t n) { return int32_t(get_u32(v, n)); }

// ===== per-verb encoders / decoders (bodies in console_binary.cpp) =====

// duty (0x03)
inline constexpr uint8_t TAG_DUTY_PCT = 0x01, TAG_DUTY_AVAIL = 0x02, TAG_DUTY_ENABLED = 0x03;
struct DutyOut { uint8_t pct = 0; uint32_t avail_ms = 0; bool enabled = false; };
size_t enc_duty(uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled);
bool   dec_duty(const uint8_t* buf, size_t len, DutyOut& out);

// limits (0x04) — tags 0x01..0x0D map to the 13 LimitsFields in declaration order
inline constexpr uint8_t TAG_LIM_WIN_MS=0x01, TAG_LIM_WIN_LEFT=0x02, TAG_LIM_N=0x03, TAG_LIM_CH_SF=0x04,
    TAG_LIM_CH_CAP=0x05, TAG_LIM_CH_USED=0x06, TAG_LIM_CH_MIN=0x07, TAG_LIM_CH_NEXT=0x08, TAG_LIM_CH_CEIL=0x09,
    TAG_LIM_DM_MIN=0x0A, TAG_LIM_DM_NEXT=0x0B, TAG_LIM_DUTY_MS=0x0C, TAG_LIM_DUTY_USED=0x0D;
struct LimitsOut { uint32_t win_ms=0, win_left_ms=0, n=0, ch_sf=0, ch_cap=0, ch_used=0, ch_min_ms=0,
    ch_next_ms=0, ch_ceiling=0, dm_min_ms=0, dm_next_ms=0, duty_ms=0, duty_used_ms=0; };
size_t enc_limits(uint8_t* buf, size_t cap, const LimitsFields& L);
bool   dec_limits(const uint8_t* buf, size_t len, LimitsOut& out);

// status (0x01) — the remote-diag superset
inline constexpr uint8_t TAG_ST_UPTIME=0x01, TAG_ST_RX=0x02, TAG_ST_TX=0x03, TAG_ST_TXQ=0x04, TAG_ST_TXDROP=0x05,
    TAG_ST_TXTO=0x06, TAG_ST_RXBAD=0x07, TAG_ST_ISR=0x08, TAG_ST_RXARM=0x09, TAG_ST_ROUTES=0x0A, TAG_ST_DUTY=0x0B,
    TAG_ST_PENDING=0x0C, TAG_ST_LBT=0x0D, TAG_ST_HALTED=0x0E, TAG_ST_SLEPT=0x0F, TAG_ST_STACKHW=0x10,
    TAG_ST_RESET=0x11, TAG_ST_BATT=0x12, TAG_ST_NF=0x13, TAG_ST_ID=0x14, TAG_ST_KEY=0x15;
struct StatusDiag { uint32_t txto=0, rxbad=0, isr=0, rxarm=0, slept=0; uint16_t stackhw=0; uint8_t reset_cause=0, halted=0; int8_t nf_dbm=0; };
struct StatusOut { uint32_t uptime_s=0, rx=0, tx=0, txto=0, rxbad=0, isr=0, rxarm=0, slept=0, duty_ms=0, key=0;
    uint16_t txq=0, txdrop=0, stackhw=0; int16_t batt_mv=0; uint8_t routes=0, pending=0, lbt=0, halted=0, reset_cause=0, id=0; int8_t nf_dbm=0; };
size_t enc_status(uint8_t* buf, size_t cap, uint8_t id, uint32_t key, const StatusFields& s, const StatusDiag& d);
bool   dec_status(const uint8_t* buf, size_t len, StatusOut& out);

// cfg (0x02) — NodeConfig + CfgExtras (mirrors write_cfg's field access; ble_mode as a u8 enum 0=off/1=on/2=periodic)
inline constexpr uint8_t TAG_CFG_NODE_ID=0x01, TAG_CFG_FREQ=0x02, TAG_CFG_ROUTING_SF=0x03, TAG_CFG_SF_LIST=0x04,
    TAG_CFG_BW=0x05, TAG_CFG_CR=0x06, TAG_CFG_TX_POWER=0x07, TAG_CFG_DUTY_X1000=0x08, TAG_CFG_BEACON_MS=0x09,
    TAG_CFG_HOP_CAP=0x0A, TAG_CFG_LBT=0x0B, TAG_CFG_NAV=0x0C, TAG_CFG_INTRA_RELAY=0x0D, TAG_CFG_HOST_MOB=0x0E,
    TAG_CFG_LEAF_ID=0x0F, TAG_CFG_GATEWAY=0x10, TAG_CFG_MOBILE=0x11, TAG_CFG_TEAM_ID=0x12, TAG_CFG_LINEAGE=0x13,
    TAG_CFG_EPOCH=0x14, TAG_CFG_BLE_MODE=0x15, TAG_CFG_BLE_PERIOD=0x16, TAG_CFG_BLE_PIN=0x17, TAG_CFG_LOC_DM=0x18,
    TAG_CFG_E2E_DM=0x19, TAG_CFG_LAT=0x1A, TAG_CFG_LON=0x1B;
struct CfgOut {
    uint8_t node_id=0, routing_sf=0, cr=0, hop_cap=0, lbt=0, nav=0, intra_relay=0, host_mobiles=0, leaf_id=0,
            is_gateway=0, is_mobile=0, ble_mode=0, loc_dm=0, e2e_dm=0; int8_t tx_power=0;
    uint16_t sf_list=0, lineage_id=0, config_epoch=0, ble_period=0;
    uint32_t freq_hz=0, bw=0, duty_x1000=0, beacon_ms=0, team_id=0, ble_pin=0; int32_t lat_e7=0, lon_e7=0;
};
size_t enc_cfg(uint8_t* buf, size_t cap, const NodeConfig& c, const CfgExtras& x);
bool   dec_cfg(const uint8_t* buf, size_t len, CfgOut& out);

// routes (0x06) — one record TLV per row; a trailing TAG_TRUNCATED u8 when the table didn't fit
inline constexpr uint8_t TAG_ROUTE_REC = 0x01;
inline constexpr uint8_t ROUTE_REC_LEN = 12;   // dest,next,hops,score(2),flags,leaf,age(4),cand
struct RouteOut { RouteRow rows[32]; uint8_t n = 0; uint8_t truncated = 0; };
size_t enc_routes(uint8_t* buf, size_t cap, const RouteRow* rows, uint8_t n, uint8_t* out_truncated);
bool   dec_routes(const uint8_t* buf, size_t len, RouteOut& out);

// faults (0x05) — one record TLV per fault; a trailing TAG_TRUNCATED u8 when they didn't fit
struct FaultRow { uint8_t cause = 0; uint32_t pc = 0, lr = 0; uint16_t count = 0; };
inline constexpr uint8_t TAG_FAULT_REC = 0x01;
inline constexpr uint8_t FAULT_REC_LEN = 11;   // cause,pc(4),lr(4),count(2)
struct FaultOut { FaultRow rows[16]; uint8_t n = 0; uint8_t truncated = 0; };
size_t enc_faults(uint8_t* buf, size_t cap, const FaultRow* rows, uint8_t n, uint8_t* out_truncated);
bool   dec_faults(const uint8_t* buf, size_t len, FaultOut& out);

// gateway (0x07) — this node's gateway config + window schedule
struct GatewayLeaf { uint8_t layer_id=0, node_id=0, routing_sf=0; uint16_t sf_list=0; uint32_t bw=0; uint8_t cr=0; uint32_t window_ms=0, window_offset_ms=0; };
struct GatewayFields { uint8_t n_layers=0; uint32_t window_period_ms=0; GatewayLeaf leaf[2]; };
inline constexpr uint8_t TAG_GW_NLAYERS=0x01, TAG_GW_PERIOD=0x02, TAG_GW_LEAF=0x03;
inline constexpr uint8_t GW_LEAF_LEN=18;   // layer_id,node_id,routing_sf,sf_list(2),bw(4),cr,window_ms(4),window_offset_ms(4)
struct GatewayOut { GatewayFields g; };
size_t enc_gateway(uint8_t* buf, size_t cap, const GatewayFields& g);
bool   dec_gateway(const uint8_t* buf, size_t len, GatewayOut& out);

}  // namespace meshroute::console::bin
