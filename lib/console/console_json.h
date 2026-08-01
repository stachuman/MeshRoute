// MeshRoute — lib/console/console_json.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Bounded, heap-free NDJSON line writers for the device console + the BLE companion transport.
// hal.h discipline: no std::string/json/heap, C++17-includable, -fno-exceptions.
// ⚠ V1 HISTORY 2026-07-25: this header used to claim the writers are "shared by the device console and
// the sim's FirmwareNode (one serializer, two backends — schema cannot drift)". That was FALSE at the time:
// the simulator compiled lib/core only, referenced nothing here, and hand-built its own `script_emit`/`push`
// telemetry JSON — so a hole in one of the enum→string mappers below was invisible to every scenario (which
// is why three of them shipped broken).
// ★ V1 UPDATE 2026-07-26 (Wave-4 #6) — PARTLY TRUE NOW, and the boundary is EXACT, so do not over-read it:
//   • the sim DOES compile this TU (CMake target `meshroute_console`), once, in the default namespace;
//   • exactly ONE function is CALLED from it — `pushkind_name`, which now renders the scenario oracle's push
//     `kind` field (orchestrator/runtime/ConsoleNames.cpp → NodeRuntimeWrapper.cpp drainPushes). For that one
//     mapper the console contract and the corpus oracle are the same table and can no longer drift, and the
//     29-scenario corpus does exercise it;
//   • EVERYTHING ELSE HERE IS STILL SIM-INVISIBLE — compiled by the sim, executed only by the firmware. The
//     write_* line serializers, cmdcode_name, sendfailreason_name and joinrefusereason_name are covered ONLY
//     by test/test_console_json.cpp (which walks every enumerator of every mapped enum) and by -Wswitch,
//     which is gate-blocking. Do not treat scenario byte-identity as evidence about them.
//   • ⚠ the sim reaches this file across a namespace boundary on the enum's UNDERLYING TYPE, so anything
//     here taking a Node/NodeConfig/Push must never be bridged that way — see ConsoleNames.h's HARD LIMIT.
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
// §2: {"ev":"reqpubkey_sent","hash":…[,"plane":"team"|"static"]} — the on-air pubkey-request twin.
// ★ §id-hash S1: `hash` is CmdResult::dst_hash (for a by-id request, the hash peer_book_by_id RESOLVED) and `plane` is
// CmdResult::plane. Both come straight off the result — the caller must NOT re-resolve the id, which is precisely the
// duplicate lookup that kept the one-table defect alive on the BLE transport. plane defaults to 0 = omit the field.
size_t write_reqpubkey_sent(char* buf, size_t cap, uint32_t hash, uint8_t plane = 0);
size_t write_push  (char* buf, size_t cap, const Push& p, const NodeConfig* cfg = nullptr);   // cfg: config_adopted membership fields (R6.3)
// join_started — the JSON verb ack for `join`/`create` (replaces the human success line so the app gets a parseable
// start-of-DAD event; the adopt itself rides the async join_adopted push). freq_khz/bw_hz are integer (no float on
// the wire). create -> "create":true + lineage + leaf_name (both omitted for a plain join).
struct JoinStartedFields {
    bool        create        = false;
    uint8_t     layer         = 0;       // full 1..255 layer id
    uint8_t     leaf          = 0;       // layer & 0x0F (the wire leaf nibble)
    uint32_t    lineage       = 0;       // create only
    const char* leaf_name     = nullptr; // create only
    size_t      leaf_name_len = 0;
    uint32_t    freq_khz      = 0;
    uint8_t     sf            = 0;
    uint32_t    bw_hz         = 0;
};
size_t write_join_started(char* buf, size_t cap, const JoinStartedFields& j);
size_t write_event (char* buf, size_t cap, const char* type, const EventField* f, size_t n);
size_t write_log   (char* buf, size_t cap, const char* msg);
size_t write_err   (char* buf, size_t cap, const char* code, const char* msg);  // msg nullable
// §S1: mobile/team snapshot for `ready` — ALL fields omit-when-inactive (a static/teamless node stays byte-identical).
// Sourced from the Node's !MR_FEAT_MOBILE-stubbed accessors (0/false), so the writer needs no #if.
struct MobileReadyFields {
    bool     is_mobile   = false;   // c.is_mobile — gates the mobile_* block
    bool     registered  = false;   // mobile_registered()
    uint8_t  home        = 0;       // mobile_home_id() (0 = unregistered)
    uint8_t  local       = 0;       // mobile_local_id()
    uint8_t  home_layer  = 0;       // mobile_home_layer() (emitted only when registered)
    uint8_t  hosting     = 0;       // mobile_reg_count() — static host: mobiles registered to us (omit when 0)
    uint32_t team_id     = 0;       // c.team_id (omit when 0; hex string)
    uint8_t  team_local  = 0;       // team_local_id() — our own id on the team overlay (omit when 0)
    bool     team_ch_key = false;   // §team-ch-key (T-K1b): team_channel_key_present() — the CONTENT-key LOCK STATE, the app's indicator. Emitted (explicitly true/false) only INSIDE the team block, i.e. iff team_id != 0 — same omit-when-inactive rule as `team` itself, so a static/teamless node's ready stays byte-identical. ⚠ the KEY ITSELF NEVER RIDES ready (unsolicited, fires on every connect) — only `team exportkey` discloses it.
};
size_t write_ready (char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode,
                    uint32_t inbox_epoch, uint64_t now_ms,
                    const char* name = nullptr, size_t name_len = 0,    // /mrid node name; omitted when empty
                    const uint8_t* ed_pub = nullptr,                    // §4: full Ed25519 pubkey (64 hex) for the QR `p`; omitted when null
                    uint8_t duty_pct = 0, uint32_t duty_avail_ms = 0,   // duty readout: snapshot so the app shows it on connect (refreshed via `duty`)
                    MobileReadyFields mob = MobileReadyFields{});       // §S1: mobile/team state (default = all-omit -> byte-identical)
// `duty` query reply (companion polls it for the silent-countdown banner): {"ev":"duty","pct":,"avail_ms":,"enabled":}.
size_t write_duty  (char* buf, size_t cap, uint8_t pct, uint32_t avail_ms, bool enabled);
// `limits` query reply (companion anti-spam/headroom screen). All fields are plain u32 — no float
// on the wire (newlib-nano printf has no %f/%lld). Live-computed by Node::limits_snapshot().
struct LimitsFields {
    uint32_t win_ms      = 0;   // originator_window_ms (the 5-min cap window)
    uint32_t win_left_ms = 0;   // ms until the current window rolls
    uint32_t n           = 0;   // rt_count() — mesh size the cap divides by
    uint32_t ch_sf       = 0;   // max_data_sf() — the DATA-M SF T_ch is priced at
    uint32_t ch_cap      = 0;   // channel_cap_origin() — this origin's per-window channel cap
    uint32_t ch_used     = 0;   // this node's own distinct floods this window
    uint32_t ch_min_ms   = 0;   // channel_min_interval_ms burst floor
    uint32_t ch_next_ms  = 0;   // ms until a channel post is actually allowed (0 = now)
    uint32_t ch_ceiling  = 0;   // C — total channel capacity (0 when duty disabled)
    uint32_t dm_min_ms   = 0;   // dm_min_interval_ms burst floor
    uint32_t dm_next_ms  = 0;   // ms until an own DM is actually allowed (0 = now)
    uint32_t duty_ms     = 0;   // channel_duty_budget_ms() — the 5-min D basis (0 = duty disabled)
    uint32_t duty_used_ms = 0;  // airtime used this 5-min window
};
size_t write_limits(char* buf, size_t cap, const LimitsFields& L);
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
    uint8_t  leaf = 0;         // the route's learned leaf nibble (layer & 0x0F)
    uint32_t age_ms = 0;       // since last_seen
    uint8_t  cand = 0;         // candidate next-hops held (1..K)
};
size_t write_route    (char* buf, size_t cap, const RouteRow& r);
size_t write_routes_end(char* buf, size_t cap, uint32_t count);

// ★★ §AB3 — the ADDRESS BOOK stream (spec 2026-07-29 §2.1), route/routes_end's shape exactly (U3):
//   {"ev":"peer","hash":<dec u32>,"conf":"…","confirmed":<bool>[,"name":"…"][,"static_id":N][,"team_id":N]
//    [,"team_alias":N][,"aged":true]}   …then  {"ev":"peers_end","count":N}
// ★ NO parallel row struct — it takes Node::PeerBookRow, the GENERATED view's own carrier, so there is exactly one
// definition of a book row in the tree (U2; write_cfg already takes NodeConfig the same way).
// ★★ BOUNDED BY CONSTRUCTION per the §2.6(a) ruling: the caller streams only rows backed by _peer_keys (≤ 16), so
// `hash` is always present and non-zero HERE. The up-to-256 id-only diagnostic list is TEXT-console only (`peers all`) —
// emitting it over BLE walks back into the self-inflicted console-flood wedge this project already fixed once.
// FIELD CONTRACT:
//   `conf`      — the level, NOT a boolean. ★ The app must gate "send encrypted" on conf >= authoritative; presence of
//                 a key is not sealing capability (spec §2.2, and §0.1 is the bug that proves it).
//   `aged`      — present only when the cached key is past its TTL and therefore UNUSABLE. `conf` is then already
//                 reported as "overheard", so a consumer that ignores `aged` still cannot over-claim.
//   `confirmed` — §S2 peer_confirmed: THEY hold OUR key, so a sealed reply can come back. Always present.
//   `team_alias`— >0 ⇒ that many OTHER team ids also cache this hash and lost the freshest-wins race. Never silently
//                 dropped (spec §2.1's explicit requirement that the emit says so).
//   ★★ §AB4 — `lat`/`lon`/`loc_age_s`/`loc_src`: the RETAINED POSITION, all four present together or all four absent.
//                 `lat`/`lon` are 1e-7 degrees (signed). **ABSENCE IS NORMAL, NOT AN ERROR** — most peers never send a
//                 position, and it is RAM-only so a node reboot clears every one of them (deliberately: a stale fix is
//                 worse than none, and a captured node must not yield the team's positions).
//   `loc_age_s` — SECONDS since we received it. ★ The app MUST render it alongside the position: a position shown
//                 without its age reads as CURRENT, which is the whole failure mode the RAM-only ruling prevents.
//   `loc_src`   — ★★ THE TRUST ANCHOR, and the app must render the distinction, not just the pin.
//                 `"peer"` = a DM sealed to US and opened with OUR key ⇒ PAIRWISE, "this specific peer said so".
//                 `"team"` = a channel post sealed to the SHARED team content key ⇒ GROUP, "some holder of the team key
//                 said so" — every member holds that key, so ANY member can publish a position attributed to another.
//                 That bound is accepted by design, not a defect; this field is what keeps it honest. ✅ BOTH values
//                 occur since §chan-crypt CL2b, which lit up `"team"` via `send_channel <ch> "…" -t -l -e`; the field
//                 was emitted from the start so the app ships ONE renderer and the weaker claim was never
//                 retro-fitted into the stronger one's slot.
size_t write_peer_row (char* buf, size_t cap, const Node::PeerBookRow& r);
size_t write_peers_end(char* buf, size_t cap, uint32_t count);
size_t write_peers_err(char* buf, size_t cap, const char* reason);   // {"ev":"peers_err","reason":"console_only"}

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
    bool     team_ch_key = false;  // §team-ch-key (T-K1b): team_channel_key_present() — the JSON twin of dump_cfg's `team_ch_key=0|1`. ALWAYS emitted (cfg is the explicit dump — same rule as team_id, which prints "00000000" rather than omitting).
};
size_t write_cfg(char* buf, size_t cap, const NodeConfig& c, const CfgExtras& x);

// §team-ch-key (T-K1b) — `team exportkey`: the ONLY disclosure of the team CONTENT key
// (ios-companion/INBOX_SYNC_CONTRACT.md "node → app: export the team channel keypair"; owner ruling 2026-07-29:
// available on EVERY transport, exfiltration risk accepted and recorded there).
// Both halves are 64 LOWER-CASE hex digits, no `0x` — byte-for-byte the form `team … tkpub=/tkpriv=` accepts, so an
// export → import round trip is TEXTUALLY EXACT (pinned in test_console_json.cpp against mrfw::parse_hex32).
// The bytes are emitted VERBATIM as stored: T-K1 persists the canonical RFC-7748 CLAMPED scalar (identity.h's
// clamping contract) precisely so that no consumer re-derives or normalises — tkpub is NOT re-derived from tkpriv.
// ⚠ team_id is DECIMAL u32 here (the contract's own example, 858993459 = 0x33333333), unlike ready's `team` /
// cfg's `team_id` hex strings. Same convention as sender_hash / channel_msg_id / peer_name's hash.
size_t write_team_key_export(char* buf, size_t cap, uint32_t team_id, const uint8_t pub[32], const uint8_t priv[32]);
// The keyless/teamless answer — a LOUD REFUSAL, not a null-bearing `team_key_export`. The contract left this open
// ("tkpub/tkpriv null — or a loud refusal"); T-K1b picks refusal. Rationale at team_export_key()
// (src/firmware_config.cpp), in one line: this file emits ZERO JSON `null` literals — every optional field is
// omit-when-absent — and a success envelope whose key fields are null is a success event reporting a failure (C2).
// reason: "no_team" (team_id == 0) | "no_key" (no keypair held, incl. every MR_FEAT_TEAM 0 build).
// §team-ch-key (T-K3) REUSES this ONE error event for every `team grantkey` refusal (U1 — no second err shape for the
// same verb family). Its reasons: "no_team" · "no_key" · "no_identity" · "no_pubkey" · "self" · "delegated" ·
// "too_large" · "bad_target". Two of them overlap `exportkey`'s by design: both verbs answer the same two questions.
size_t write_team_key_err   (char* buf, size_t cap, const char* reason);
// §team-ch-key (T-K3): `team grantkey <0xhash> [name="…"]` ACCEPTED — the sealed TYPE-19 grant is on its way. A
// DISTINCT success event (never `team_key_err` with a happy reason), carrying the correlation handle the app needs:
//   `hash` = the target's key_hash32 (decimal u32, matching team_key_export's convention and peer_name's `hash`)
//   `ctr`  = the DM ctr, or 0 when the send PARKED behind a hash resolve — the ordinary send-by-hash semantics
//   `parked` = true iff ctr == 0, stated explicitly so the app never has to infer intent from a magic 0
// ⚠ It carries NO key material and no team_name echo: the grant's confidentiality is the whole point of the feature,
// and `team exportkey` remains the ONE verb that discloses the pair.
size_t write_team_key_grant (char* buf, size_t cap, uint32_t target_hash, uint16_t ctr);

// §S3: `mobile status` + `mobile gateways` JSON (PODs in; src/ calls these from handle_mobile — no node.h dep here).
struct MobileStatusFields {
    bool     registered   = false;
    uint8_t  home         = 0, local = 0;
    uint16_t epoch        = 0;
    uint8_t  home_layer   = 0;      // omitted unless registered
    bool     autoregister = false;
    uint8_t  layer        = 0;      // the live PHY layer
    uint32_t freq_khz     = 0;      // integer kHz (no float on the wire)
    uint8_t  sf           = 0;
    uint32_t bw_hz        = 0;
    uint8_t  nets         = 0;      // learned-networks count
};
size_t write_mobile_status(char* buf, size_t cap, const MobileStatusFields& m);
size_t write_mobile_err   (char* buf, size_t cap, const char* reason);   // {"ev":"mobile_err","reason":"…"}
// `mobile gateways` streamed rows (routes/routes_end pattern): mobile_gw* then mobile_net* then mobile_gw_end.
size_t write_mobile_gw    (char* buf, size_t cap, uint8_t gw, uint8_t leaf);
size_t write_mobile_net   (char* buf, size_t cap, uint8_t layer, const char* name, size_t name_len,
                           uint32_t freq_khz, uint8_t sf, uint32_t bw_hz);
size_t write_mobile_gw_end(char* buf, size_t cap, uint8_t gws, uint8_t nets);
// §S6: `nameof` answer — {"ev":"peer_name","hash":<dec u32>[,"name":"…"][,"static_id":N][,"team_id":N]} (name omitted
// when unknown). ★ §AB3: the two id fields are ADDITIVE (omit-when-0) and come from the generated view, so `nameof`
// now answers the spec §2.5 question directly — *"0x6C297145 is team id 228"* — instead of leaving the caller to guess
// which namespace an id lives in. Existing consumers are unaffected: the first two fields and their order are unchanged.
size_t write_peer_name    (char* buf, size_t cap, uint32_t hash, const char* name, size_t name_len,
                           uint8_t static_id = 0, uint8_t team_id = 0);
// ★ §AB2: the `peername` verb's SYNCHRONOUS ack + refusal (spec 2026-07-29 §2.3, mechanism ruled in §2.6(b) — an ack,
// deliberately NOT a push, so no PushKind is touched). Same hash convention as write_peer_name above (decimal u32).
//   {"ev":"peer_name_set","hash":<dec u32>,"name":"…"}   — the name is ECHOED, so the app confirms what was stored
//   {"ev":"peer_name_err","reason":"unknown_hash"|"too_long"|"bad_args"}
size_t write_peer_name_set(char* buf, size_t cap, uint32_t hash, const char* name, size_t name_len);
size_t write_peer_name_err(char* buf, size_t cap, const char* reason);

// Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md). The pull stream = inbox_dm* then
// inbox_channel* (oldest-first) then inbox_end; mark_read acks via write_inbox_marked. Fields individual to
// keep this file free of inbox.h.
size_t write_inbox_dm     (char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint16_t ctr,
                           uint32_t sender_hash, uint64_t rx_ms, const char* body, size_t body_len,
                           bool enc = false,    // §8b: "enc":true when the DM was delivered sealed; omitted (=false) otherwise
                           uint8_t type = 0,    // the frame DATA_TYPE: 0 = a normal DM (field omitted); 3 (DATA_TYPE_E2E_ACK) -> "type":"e2e_ack" (a receipt)
                           uint8_t origin_layer = 0);   // §GapA-durable: "origin_layer":N when the DM crossed layers; omitted (=0) for same-layer
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t layer_id, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len,
                           uint32_t team_id = 0);   // §S5: "team_id":"…" emitted only when non-zero (omit-when-0, same rule as the live channel_recv)
size_t write_inbox_end    (char* buf, size_t cap, uint32_t dm_seq, uint32_t chan_seq, uint32_t epoch, uint32_t count,
                           uint64_t now_ms);
size_t write_inbox_marked (char* buf, size_t cap, const char* kind, uint32_t seq);

const char* cmdcode_name(CmdCode c);
// ★ §id-hash S1 (spec §3-D9): CmdResult::plane / ResolveCmd::plane -> "team" (1) | "static" (anything else). Takes the
// RAW uint8_t, not `Plane`: that enum is in node_carriers.h and the app seam (command.h) must not include it, so 0/1/2
// IS the contract encoding and this is its one spelling. Callers omit the field entirely when plane == 0.
const char* cmdplane_name(uint8_t plane);
const char* pushkind_name(PushKind k);
const char* sendfailreason_name(SendFailReason r);      // send_failed / send_blocked `reason` (out-of-range -> "none")
const char* joinrefusereason_name(JoinRefuseReason r);  // join_refused `reason` (out-of-range -> "none")
// ★ §AB2: peer_key_cached `conf` (out-of-range -> "overheard", the LEAST capable level — never over-claim a sealing
// capability). Takes Node::PeerKeyConf so -Wswitch guards completeness; Push carries it as a raw uint8_t because
// command.h cannot see node.h (see the encoding static_assert beside the enum).
const char* peerkeyconf_name(Node::PeerKeyConf c);
const char* peerlocsrc_name (Node::PeerLocSrc s);   // ★ §AB4: "peer" (pairwise) | "team" (group) — the retained position's TRUST ANCHOR

}  // namespace meshroute::console
