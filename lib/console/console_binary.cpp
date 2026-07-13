// MeshRoute — lib/console/console_binary.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_binary.h"
#include <cstring>   // strcmp (ble_mode enum)

namespace meshroute::console::bin {

// ---- duty (0x03) ----
size_t enc_duty(uint8_t* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled) {
    size_t off = frame_begin(buf, cap, MSG_DUTY);
    if (!off) return 0;
    if (!put_u8 (buf, cap, off, TAG_DUTY_PCT,     pct))             return 0;
    if (!put_u32(buf, cap, off, TAG_DUTY_AVAIL,   avail_ms))        return 0;
    if (!put_u8 (buf, cap, off, TAG_DUTY_ENABLED, enabled ? 1 : 0)) return 0;
    return off;
}
bool dec_duty(const uint8_t* buf, size_t len, DutyOut& out) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_DUTY) return false;
    uint8_t tag, n; const uint8_t* v;
    while (reader_next(r, tag, v, n)) switch (tag) {
        case TAG_DUTY_PCT:     out.pct      = get_u8 (v, n);      break;
        case TAG_DUTY_AVAIL:   out.avail_ms = get_u32(v, n);      break;
        case TAG_DUTY_ENABLED: out.enabled  = get_u8 (v, n) != 0; break;
        default: break;   // forward-compat: skip unknown
    }
    return true;
}

// ---- limits (0x04) ----
size_t enc_limits(uint8_t* buf, size_t cap, const LimitsFields& L) {
    size_t off = frame_begin(buf, cap, MSG_LIMITS); if (!off) return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_WIN_MS,   L.win_ms))      return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_WIN_LEFT, L.win_left_ms)) return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_N,        L.n))           return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_SF,    L.ch_sf))       return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_CAP,   L.ch_cap))      return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_USED,  L.ch_used))     return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_MIN,   L.ch_min_ms))   return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_NEXT,  L.ch_next_ms))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_CH_CEIL,  L.ch_ceiling))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DM_MIN,   L.dm_min_ms))   return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DM_NEXT,  L.dm_next_ms))  return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DUTY_MS,  L.duty_ms))     return 0;
    if (!put_u32(buf,cap,off,TAG_LIM_DUTY_USED,L.duty_used_ms))return 0;
    return off;
}
bool dec_limits(const uint8_t* buf, size_t len, LimitsOut& o) {
    TlvReader r; if (!reader_init(r,buf,len) || r.msg_type != MSG_LIMITS) return false;
    uint8_t tag,n; const uint8_t* v;
    while (reader_next(r,tag,v,n)) switch (tag) {
        case TAG_LIM_WIN_MS: o.win_ms=get_u32(v,n); break;      case TAG_LIM_WIN_LEFT: o.win_left_ms=get_u32(v,n); break;
        case TAG_LIM_N: o.n=get_u32(v,n); break;                case TAG_LIM_CH_SF: o.ch_sf=get_u32(v,n); break;
        case TAG_LIM_CH_CAP: o.ch_cap=get_u32(v,n); break;      case TAG_LIM_CH_USED: o.ch_used=get_u32(v,n); break;
        case TAG_LIM_CH_MIN: o.ch_min_ms=get_u32(v,n); break;   case TAG_LIM_CH_NEXT: o.ch_next_ms=get_u32(v,n); break;
        case TAG_LIM_CH_CEIL: o.ch_ceiling=get_u32(v,n); break; case TAG_LIM_DM_MIN: o.dm_min_ms=get_u32(v,n); break;
        case TAG_LIM_DM_NEXT: o.dm_next_ms=get_u32(v,n); break; case TAG_LIM_DUTY_MS: o.duty_ms=get_u32(v,n); break;
        case TAG_LIM_DUTY_USED: o.duty_used_ms=get_u32(v,n); break;
        default: break;
    }
    return true;
}

// ---- status (0x01) ----
size_t enc_status(uint8_t* buf, size_t cap, uint8_t id, uint32_t key, const StatusFields& s, const StatusDiag& d) {
    size_t off = frame_begin(buf, cap, MSG_STATUS); if (!off) return 0;
    if (!put_u32(buf,cap,off,TAG_ST_UPTIME, uint32_t(s.uptime_ms / 1000))) return 0;   // ms -> s
    if (!put_u32(buf,cap,off,TAG_ST_RX, s.rx))          return 0;
    if (!put_u32(buf,cap,off,TAG_ST_TX, s.tx))          return 0;
    if (!put_u16(buf,cap,off,TAG_ST_TXQ, s.txq))        return 0;
    if (!put_u16(buf,cap,off,TAG_ST_TXDROP, s.txdrop))  return 0;
    if (!put_u32(buf,cap,off,TAG_ST_TXTO, d.txto))      return 0;
    if (!put_u32(buf,cap,off,TAG_ST_RXBAD, d.rxbad))    return 0;
    if (!put_u32(buf,cap,off,TAG_ST_ISR, d.isr))        return 0;
    if (!put_u32(buf,cap,off,TAG_ST_RXARM, d.rxarm))    return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_ROUTES, s.routes))  return 0;
    if (!put_u32(buf,cap,off,TAG_ST_DUTY, s.duty_ms))   return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_PENDING, s.pending?1:0)) return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_LBT, s.lbt?1:0))    return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_HALTED, d.halted))  return 0;
    if (!put_u32(buf,cap,off,TAG_ST_SLEPT, d.slept))    return 0;
    if (!put_u16(buf,cap,off,TAG_ST_STACKHW, d.stackhw))return 0;
    if (!put_u8 (buf,cap,off,TAG_ST_RESET, d.reset_cause)) return 0;
    if (s.batt_mv >= 0 && !put_i16(buf,cap,off,TAG_ST_BATT, int16_t(s.batt_mv))) return 0;  // omit when <0
    if (!put_u8 (buf,cap,off,TAG_ST_NF, uint8_t(d.nf_dbm))) return 0;    // i8 packed in a 1-byte value
    if (!put_u8 (buf,cap,off,TAG_ST_ID, id))            return 0;
    if (!put_u32(buf,cap,off,TAG_ST_KEY, key))          return 0;
    return off;
}
bool dec_status(const uint8_t* buf, size_t len, StatusOut& o) {
    TlvReader r; if (!reader_init(r,buf,len) || r.msg_type != MSG_STATUS) return false;
    uint8_t tag,n; const uint8_t* v;
    while (reader_next(r,tag,v,n)) switch (tag) {
        case TAG_ST_UPTIME: o.uptime_s=get_u32(v,n); break;  case TAG_ST_RX: o.rx=get_u32(v,n); break;
        case TAG_ST_TX: o.tx=get_u32(v,n); break;            case TAG_ST_TXQ: o.txq=get_u16(v,n); break;
        case TAG_ST_TXDROP: o.txdrop=get_u16(v,n); break;    case TAG_ST_TXTO: o.txto=get_u32(v,n); break;
        case TAG_ST_RXBAD: o.rxbad=get_u32(v,n); break;      case TAG_ST_ISR: o.isr=get_u32(v,n); break;
        case TAG_ST_RXARM: o.rxarm=get_u32(v,n); break;      case TAG_ST_ROUTES: o.routes=get_u8(v,n); break;
        case TAG_ST_DUTY: o.duty_ms=get_u32(v,n); break;     case TAG_ST_PENDING: o.pending=get_u8(v,n); break;
        case TAG_ST_LBT: o.lbt=get_u8(v,n); break;           case TAG_ST_HALTED: o.halted=get_u8(v,n); break;
        case TAG_ST_SLEPT: o.slept=get_u32(v,n); break;      case TAG_ST_STACKHW: o.stackhw=get_u16(v,n); break;
        case TAG_ST_RESET: o.reset_cause=get_u8(v,n); break; case TAG_ST_BATT: o.batt_mv=get_i16(v,n); break;
        case TAG_ST_NF: o.nf_dbm=int8_t(get_u8(v,n)); break; case TAG_ST_ID: o.id=get_u8(v,n); break;
        case TAG_ST_KEY: o.key=get_u32(v,n); break;
        default: break;
    }
    return true;
}

// ---- cfg (0x02) ----
size_t enc_cfg(uint8_t* buf, size_t cap, const NodeConfig& c, const CfgExtras& x) {
    const uint8_t ble_mode = (std::strcmp(x.ble_mode, "on") == 0) ? 1 : (std::strcmp(x.ble_mode, "periodic") == 0) ? 2 : 0;
    size_t off = frame_begin(buf, cap, MSG_CFG); if (!off) return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_NODE_ID,    x.node_id))              return 0;
    if (!put_u32(buf,cap,off,TAG_CFG_FREQ,       x.freq_hz))             return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_ROUTING_SF, c.routing_sf))          return 0;
    if (!put_u16(buf,cap,off,TAG_CFG_SF_LIST,    c.allowed_sf_bitmap))   return 0;
    if (!put_u32(buf,cap,off,TAG_CFG_BW,         c.radio_bw_hz))         return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_CR,         c.radio_cr))            return 0;
    if (!put_i16(buf,cap,off,TAG_CFG_TX_POWER,   int16_t(x.tx_power)))   return 0;   // i8 value carried as a 2-byte i16 TLV (decoder sign-extends)
    if (!put_u32(buf,cap,off,TAG_CFG_DUTY_X1000, x.duty_x1000))          return 0;
    if (!put_u32(buf,cap,off,TAG_CFG_BEACON_MS,  c.beacon_period_ms))    return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_HOP_CAP,    c.dv_hop_cap))          return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_LBT,        c.lbt_enabled?1:0))     return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_NAV,        c.nav_enabled?1:0))     return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_INTRA_RELAY,c.intra_layer_relay?1:0))return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_HOST_MOB,   c.host_mobiles?1:0))    return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_LEAF_ID,    c.leaf_id))             return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_GATEWAY,    c.is_gateway?1:0))      return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_MOBILE,     c.is_mobile?1:0))       return 0;
    if (!put_u32(buf,cap,off,TAG_CFG_TEAM_ID,    c.team_id))             return 0;
    if (!put_u16(buf,cap,off,TAG_CFG_LINEAGE,    c.lineage_id))          return 0;
    if (!put_u16(buf,cap,off,TAG_CFG_EPOCH,      c.config_epoch))        return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_BLE_MODE,   ble_mode))              return 0;
    if (!put_u16(buf,cap,off,TAG_CFG_BLE_PERIOD, x.ble_period))          return 0;
    if (!put_u32(buf,cap,off,TAG_CFG_BLE_PIN,    x.ble_pin))             return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_LOC_DM,     c.loc_in_dm?1:0))       return 0;
    if (!put_u8 (buf,cap,off,TAG_CFG_E2E_DM,     c.e2e_dm?1:0))          return 0;
    if (!put_i32(buf,cap,off,TAG_CFG_LAT,        x.lat_e7))              return 0;
    if (!put_i32(buf,cap,off,TAG_CFG_LON,        x.lon_e7))              return 0;
    return off;
}
bool dec_cfg(const uint8_t* buf, size_t len, CfgOut& o) {
    TlvReader r; if (!reader_init(r,buf,len) || r.msg_type != MSG_CFG) return false;
    uint8_t tag,n; const uint8_t* v;
    while (reader_next(r,tag,v,n)) switch (tag) {
        case TAG_CFG_NODE_ID: o.node_id=get_u8(v,n); break;      case TAG_CFG_FREQ: o.freq_hz=get_u32(v,n); break;
        case TAG_CFG_ROUTING_SF: o.routing_sf=get_u8(v,n); break;case TAG_CFG_SF_LIST: o.sf_list=get_u16(v,n); break;
        case TAG_CFG_BW: o.bw=get_u32(v,n); break;               case TAG_CFG_CR: o.cr=get_u8(v,n); break;
        case TAG_CFG_TX_POWER: o.tx_power=int8_t(get_i16(v,n)); break; case TAG_CFG_DUTY_X1000: o.duty_x1000=get_u32(v,n); break;
        case TAG_CFG_BEACON_MS: o.beacon_ms=get_u32(v,n); break; case TAG_CFG_HOP_CAP: o.hop_cap=get_u8(v,n); break;
        case TAG_CFG_LBT: o.lbt=get_u8(v,n); break;              case TAG_CFG_NAV: o.nav=get_u8(v,n); break;
        case TAG_CFG_INTRA_RELAY: o.intra_relay=get_u8(v,n); break; case TAG_CFG_HOST_MOB: o.host_mobiles=get_u8(v,n); break;
        case TAG_CFG_LEAF_ID: o.leaf_id=get_u8(v,n); break;      case TAG_CFG_GATEWAY: o.is_gateway=get_u8(v,n); break;
        case TAG_CFG_MOBILE: o.is_mobile=get_u8(v,n); break;     case TAG_CFG_TEAM_ID: o.team_id=get_u32(v,n); break;
        case TAG_CFG_LINEAGE: o.lineage_id=get_u16(v,n); break;  case TAG_CFG_EPOCH: o.config_epoch=get_u16(v,n); break;
        case TAG_CFG_BLE_MODE: o.ble_mode=get_u8(v,n); break;    case TAG_CFG_BLE_PERIOD: o.ble_period=get_u16(v,n); break;
        case TAG_CFG_BLE_PIN: o.ble_pin=get_u32(v,n); break;     case TAG_CFG_LOC_DM: o.loc_dm=get_u8(v,n); break;
        case TAG_CFG_E2E_DM: o.e2e_dm=get_u8(v,n); break;        case TAG_CFG_LAT: o.lat_e7=get_i32(v,n); break;
        case TAG_CFG_LON: o.lon_e7=get_i32(v,n); break;
        default: break;
    }
    return true;
}

// ---- routes (0x06) — list, fit-N + truncated ----
size_t enc_routes(uint8_t* buf, size_t cap, const RouteRow* rows, uint8_t n, uint8_t* out_truncated) {
    size_t off = frame_begin(buf, cap, MSG_ROUTES); if (!off) return 0;
    uint8_t packed = 0;
    for (uint8_t i = 0; i < n; ++i) {
        const auto& r = rows[i];
        uint8_t rec[ROUTE_REC_LEN];
        rec[0]=r.dest; rec[1]=r.next; rec[2]=r.hops;
        rec[3]=uint8_t(r.score); rec[4]=uint8_t(uint16_t(r.score) >> 8);
        rec[5]=uint8_t(r.gw ? 1 : 0); rec[6]=r.leaf;
        rec[7]=uint8_t(r.age_ms); rec[8]=uint8_t(r.age_ms>>8); rec[9]=uint8_t(r.age_ms>>16); rec[10]=uint8_t(r.age_ms>>24);
        rec[11]=r.cand;
        if (!put_bytes(buf, cap, off, TAG_ROUTE_REC, rec, ROUTE_REC_LEN)) break;   // no room -> stop, mark truncated
        ++packed;
    }
    const uint8_t omitted = uint8_t(n - packed);
    if (omitted) (void)put_u8(buf, cap, off, TAG_TRUNCATED, omitted);   // best-effort; the out param always reports it
    if (out_truncated) *out_truncated = omitted;
    return off;
}
bool dec_routes(const uint8_t* buf, size_t len, RouteOut& o) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_ROUTES) return false;
    uint8_t tag, n; const uint8_t* v;
    while (reader_next(r, tag, v, n)) {
        if (tag == TAG_ROUTE_REC && n == ROUTE_REC_LEN && o.n < 32) {
            auto& d = o.rows[o.n++];
            d.dest=v[0]; d.next=v[1]; d.hops=v[2];
            d.score=int16_t(uint16_t(v[3]) | (uint16_t(v[4])<<8));
            d.gw = v[5]!=0; d.leaf=v[6];
            d.age_ms = uint32_t(v[7]) | (uint32_t(v[8])<<8) | (uint32_t(v[9])<<16) | (uint32_t(v[10])<<24);
            d.cand=v[11];
        } else if (tag == TAG_TRUNCATED) {
            o.truncated = get_u8(v, n);
        }   // else: unknown tag -> skip
    }
    return true;
}

// ---- faults (0x05) — list, fit-N + truncated ----
size_t enc_faults(uint8_t* buf, size_t cap, const FaultRow* rows, uint8_t n, uint8_t* out_truncated) {
    size_t off = frame_begin(buf, cap, MSG_FAULTS); if (!off) return 0;
    uint8_t packed = 0;
    for (uint8_t i = 0; i < n; ++i) {
        const auto& f = rows[i];
        uint8_t rec[FAULT_REC_LEN];
        rec[0]=f.cause;
        rec[1]=uint8_t(f.pc); rec[2]=uint8_t(f.pc>>8); rec[3]=uint8_t(f.pc>>16); rec[4]=uint8_t(f.pc>>24);
        rec[5]=uint8_t(f.lr); rec[6]=uint8_t(f.lr>>8); rec[7]=uint8_t(f.lr>>16); rec[8]=uint8_t(f.lr>>24);
        rec[9]=uint8_t(f.count); rec[10]=uint8_t(f.count>>8);
        if (!put_bytes(buf, cap, off, TAG_FAULT_REC, rec, FAULT_REC_LEN)) break;
        ++packed;
    }
    const uint8_t omitted = uint8_t(n - packed);
    if (omitted) (void)put_u8(buf, cap, off, TAG_TRUNCATED, omitted);
    if (out_truncated) *out_truncated = omitted;
    return off;
}
bool dec_faults(const uint8_t* buf, size_t len, FaultOut& o) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_FAULTS) return false;
    uint8_t tag, n; const uint8_t* v;
    while (reader_next(r, tag, v, n)) {
        if (tag == TAG_FAULT_REC && n == FAULT_REC_LEN && o.n < 16) {
            auto& d = o.rows[o.n++];
            d.cause=v[0];
            d.pc = uint32_t(v[1]) | (uint32_t(v[2])<<8) | (uint32_t(v[3])<<16) | (uint32_t(v[4])<<24);
            d.lr = uint32_t(v[5]) | (uint32_t(v[6])<<8) | (uint32_t(v[7])<<16) | (uint32_t(v[8])<<24);
            d.count = uint16_t(v[9]) | (uint16_t(v[10])<<8);
        } else if (tag == TAG_TRUNCATED) {
            o.truncated = get_u8(v, n);
        }
    }
    return true;
}

// ---- gateway (0x07) ----
size_t enc_gateway(uint8_t* buf, size_t cap, const GatewayFields& g) {
    size_t off = frame_begin(buf, cap, MSG_GATEWAY); if (!off) return 0;
    if (!put_u8 (buf,cap,off,TAG_GW_NLAYERS, g.n_layers))         return 0;
    if (!put_u32(buf,cap,off,TAG_GW_PERIOD,  g.window_period_ms)) return 0;
    for (uint8_t i = 0; i < g.n_layers && i < 2; ++i) {
        const auto& L = g.leaf[i];
        uint8_t rec[GW_LEAF_LEN];
        rec[0]=L.layer_id; rec[1]=L.node_id; rec[2]=L.routing_sf;
        rec[3]=uint8_t(L.sf_list); rec[4]=uint8_t(L.sf_list>>8);
        rec[5]=uint8_t(L.bw); rec[6]=uint8_t(L.bw>>8); rec[7]=uint8_t(L.bw>>16); rec[8]=uint8_t(L.bw>>24);
        rec[9]=L.cr;
        rec[10]=uint8_t(L.window_ms); rec[11]=uint8_t(L.window_ms>>8); rec[12]=uint8_t(L.window_ms>>16); rec[13]=uint8_t(L.window_ms>>24);
        rec[14]=uint8_t(L.window_offset_ms); rec[15]=uint8_t(L.window_offset_ms>>8); rec[16]=uint8_t(L.window_offset_ms>>16); rec[17]=uint8_t(L.window_offset_ms>>24);
        if (!put_bytes(buf,cap,off,TAG_GW_LEAF, rec, GW_LEAF_LEN)) return 0;
    }
    return off;
}
bool dec_gateway(const uint8_t* buf, size_t len, GatewayOut& o) {
    TlvReader r; if (!reader_init(r, buf, len) || r.msg_type != MSG_GATEWAY) return false;
    uint8_t tag, n; const uint8_t* v; uint8_t idx = 0;
    while (reader_next(r, tag, v, n)) {
        if (tag == TAG_GW_NLAYERS) o.g.n_layers = get_u8(v, n);
        else if (tag == TAG_GW_PERIOD) o.g.window_period_ms = get_u32(v, n);
        else if (tag == TAG_GW_LEAF && n == GW_LEAF_LEN && idx < 2) {
            auto& L = o.g.leaf[idx++];
            L.layer_id=v[0]; L.node_id=v[1]; L.routing_sf=v[2];
            L.sf_list = uint16_t(v[3]) | (uint16_t(v[4])<<8);
            L.bw = uint32_t(v[5]) | (uint32_t(v[6])<<8) | (uint32_t(v[7])<<16) | (uint32_t(v[8])<<24);
            L.cr = v[9];
            L.window_ms = uint32_t(v[10]) | (uint32_t(v[11])<<8) | (uint32_t(v[12])<<16) | (uint32_t(v[13])<<24);
            L.window_offset_ms = uint32_t(v[14]) | (uint32_t(v[15])<<8) | (uint32_t(v[16])<<16) | (uint32_t(v[17])<<24);
        }   // else skip
    }
    return true;
}

}  // namespace meshroute::console::bin
