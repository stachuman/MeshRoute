// MeshRoute — lib/core/node.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The protocol node. Depends ONLY on hal.h (no Arduino/RadioLib/sol/json), so
// it runs unchanged on both HAL backends: FirmwareNode in the simulator and the
// MeshCore-PHY device backend. Bounded, fixed-size state (no heap in hot paths).
//
// SCOPE (as built, 2026-06): the Node spans the FULL same-layer + cross-layer stack —
// beacon emit + DV routing (aging/TTL prune/discovery FSM), the MAC data plane (RTS/CTS/
// DATA/ACK/NACK + throttle/triggered/cascade/LBT/NAV), hash-locate (H), E2E DM crypto
// (sealed-sender), node_id DAD + heal, channel gossip + flood, peer-liveness, and the
// dual-layer gateway. The class is split across partial-class TUs (node_*.cpp).
// The ONE unbuilt design slice is R6 leaf-config membership (lineage/epoch/config_hash
// beacon header + CONFIG_PULL) — docs/specs/2026-06-05-identity-leaf-membership-join-design.md
// + the R6 implementation spec. (Historical iteration docs: docs/specs/2026-05-29-r1-* / r2-*.)
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "mr_features.h"   // compile-time feature split (MR_FEAT_*); state/APIs below are #if-gated by these
#include "hal.h"
#include "command.h"
#include "inbox.h"
#include "protocol_constants.h"
#include "frame_codec.h"   // §mobile 5a: LayerRecord (the learned-directory record) — codec structs are header-only
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <optional>
#include "node_carriers.h"   // value-carrier structs (LayerConfig/NodeConfig/RtEntry/TxItem/... — node-legibility Slice 2, 2026-07-15)
#include "recent_ring.h"     // recent_ring_hit/_mark/_age_out — the ONE bounded seen-recently ring discipline (§3-B.1)
#include "jittered_tx_stash.h"   // jtx_stash_arm/_ring_arm/_ring_armed — the ONE de-storm stash discipline (§3-B.5)

namespace MESHROUTE_NS {

struct m_out;          // frame_codec.h — fwd-decl so the channel ingest seam doesn't pull the codec into node.h
struct rts_out;        // frame_codec.h — fwd-decl for the FLOOD RTS-M handler seam (handle_flood_rts)
struct SuspectEntry;   // frame_codec.h — fwd-decl for the §P4 suspect-gossip apply seam (apply_suspect_gossip)
struct beacon_entry;   // frame_codec.h — fwd-decl for the Slice 3 bidi detection scan seam (update_link_bidi_from_beacon)


struct data_unicast_inner;   // frame_codec.h — fwd-decl for bridge_cross_layer's const-ref param (full type in node_mac_rx.cpp)

class Node {
public:
    Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name = nullptr);

    bool on_init(const NodeConfig& cfg);                                 // cfg borrowed; false = REFUSED (bad dual-layer config, §3.2)
    // Reassign identity post-construct (device boots id=0 then loads it from NV; the join runtime sets it
    // too). 0 = unprovisioned (do_send is refused). 0xFF is reserved and ignored.
    void set_identity(uint8_t node_id, uint32_t key_hash32);
    // DP1 (Phase-1 E2E): install the X25519 ECDH secret + our Ed25519 pubkey, so we can seal/open DMs.
    // Identity is GLOBAL (a gateway shares ONE across both layers). Until set, crypto_ready()==false and any
    // seal/open FAILS LOUD (never silently falls back to cleartext). Backends derive these from the /mrid seed
    // (device) or the per-node scenario seed (sim).
    void set_crypto_identity(const uint8_t x_secret[32], const uint8_t ed_pub[32]);
    // §remote-mgmt (spec 2026-07-13): the pinned admin pubkey (trust anchor for gated rcmds) + the replay counter floor.
    // RAM state; fw_main loads from / persists to the NV Blob (admin_pubkey/admin_counter_floor/admin_provisioned).
#if MR_FEAT_REMOTE_MGMT
    bool           admin_provisioned() const { return _admin_provisioned; }
    const uint8_t* admin_pubkey()      const { return _admin_provisioned ? _admin_pubkey : nullptr; }
    uint32_t       admin_counter_floor() const { return _admin_counter_floor; }
    void admin_set_pubkey(const uint8_t ed_pub[32]) { for (int i=0;i<32;++i) _admin_pubkey[i]=ed_pub[i]; _admin_provisioned = true; }
    void admin_load(const uint8_t ed_pub[32], uint32_t floor, bool provisioned) { for (int i=0;i<32;++i) _admin_pubkey[i]=ed_pub[i]; _admin_counter_floor = floor; _admin_provisioned = provisioned; }
    bool admin_counter_check_advance(uint32_t counter) { if (counter > _admin_counter_floor) { _admin_counter_floor = counter; return true; } return false; }
#else
    bool           admin_provisioned() const { return false; }
    const uint8_t* admin_pubkey()      const { return nullptr; }
    uint32_t       admin_counter_floor() const { return 0; }
    void admin_set_pubkey(const uint8_t*) {}
    void admin_load(const uint8_t*, uint32_t, bool) {}
    bool admin_counter_check_advance(uint32_t) { return false; }
#endif
    void on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta);  // bytes valid during call only
    void on_timer(uint32_t timer_id);                                    // dispatch on Node-owned id
    void on_radio_busy(const BusyInfo& info);                            // deferred-TX retry/giveup
    void on_preamble_detected(uint64_t time_ms);                         // SX1262 IRQ / throttle witness
    CmdResult on_command(const Command& c);                              // the typed app<->firmware seam
    bool      next_push(Push& out);                                      // drain the async push ring (CMD_SYNC_NEXT)
    // Persistent inbox (durable DM + channel history). A backend installs durable stores via
    // inbox().on_init(dm, chan) AFTER Node::on_init; until then the inbox is disabled (record-on-delivery
    // is inert). The node records on its DM/channel deliver paths; a companion pulls incrementally.
    Inbox&    inbox() { return _inbox; }

    // OTA remote diagnostics (`rcmd`, 2026-06-24): a console-style query / response carried over a DATA DM
    // (DATA_TYPE_REMOTE_CMD / _RESP). lib/core is the GENERIC transport — fw_main owns the query whitelist + execution.
    struct RemoteInbound {                       // a received remote cmd/resp, staged for the main loop (NOT the inbox)
        bool    active = false;
        bool    is_response = false;             // true = a response to our cmd; false = a command for us to execute
        uint8_t from = 0;                        // the originator (pa.origin) — where the response goes back
        uint8_t len = 0;
        uint8_t body[protocol::inbox_max_body] = {};
    };
    uint16_t send_remote_cmd     (uint8_t dst, const uint8_t* body, uint8_t len);   // -> a DATA_TYPE_REMOTE_CMD DM (rides routing/ACK)
    uint16_t send_remote_response(uint8_t dst, const uint8_t* body, uint8_t len);   // -> a DATA_TYPE_REMOTE_RESP DM
    bool     take_remote_inbound(RemoteInbound& out);                              // drain the single inbound slot (fw_main, each loop)

    // Exposed for the R3.x determinism golden test. The retry-jitter RANGE is a
    // cross-engine alignment contract: 3*airtime_routing(RTS_LEN=8) must equal
    // the Lua's, or the lua-vs-meshroute forced-retry streams de-align (see the
    // node.cpp definition comment). Pure, const, no side effects.
    uint32_t  retry_jitter_ms() const;                                   // 3*airtime(routing, RTS_LEN=8)

    // R4.0 duty-cycle budget tier (route-free; from the rolling airtime window). Lua dv:3555-3571.
    // Public for the tier-table unit test (the emit only observes >=CRITICAL, so the HEALTHY/STRAINED
    // boundary needs a direct call). Pure, const.
    enum class BudgetTier : uint8_t { healthy = 0, strained = 1, critical = 2, exhausted = 3 };
    BudgetTier compute_budget_tier() const;                              // HEALTHY when duty_cycle<=0 (disabled)
    // id_bind binding provenance + trust. source = WHERE it came from; confidence = whether it may OVERWRITE
    // a conflicting binding. authoritative (self / owner-confirmed hash-bind) overwrites + wins the
    // dedup-by-hash; claimed (beacon / cached / snooped) refuses a same-id conflict.
    enum class IdBindSource : uint8_t { self = 0, bcn = 1, h_query = 2, h_relay = 3 };
    enum class IdBindConf   : uint8_t { claimed = 0, authoritative = 1 };
    // ★★★ §id-hash S1b (2026-08-01, QA finding P1c): emit_hash_query's DISPOSITION. It used to be `void` with FOUR
    // silent early-outs, and `on_command`'s reqpubkey arm returned `queued` regardless — which the BLE transport then
    // turned into `{"ev":"reqpubkey_sent"}`, i.e. the contract's "the on-air request was flooded", for a request that
    // provably never left the node. ⇒ the originator now REPORTS, and the command arm maps the report to an honest
    // CmdCode. ⓘ Widening `void` -> this enum changed ZERO call sites: all four other callers already discarded the
    // (absent) return, and there is no [[nodiscard]] — so this is not the caller sweep C1 would have made a separate
    // slice, it is a return-type widening plus one reader.
    enum class HQueryOutcome : uint8_t {
        sent = 0,          // ★ the TX path ACCEPTED the frame — handed to the radio, or accepted into the LBT defer
                           //   ring. ⚠ NOT a claim of airtime (owner ruling 2026-08-01): a deferred frame reaches the
                           //   radio when a timer fires, so "it flew" is unsatisfiable synchronously. The ONLY value
                           //   that may produce `reqpubkey_sent`. A deferred frame that dies later is reported LATE
                           //   (`tx_deferred_lost`, node.cpp's defer arm).
        degenerate,        // key_hash32 == 0, or it is OUR OWN hash: there is no other node to ask
        no_identity,       // want_pubkey with no crypto identity — the MUTUAL exchange needs our ed_pub
        no_return_route,   // a mobile whose origin is still its own LOCAL id and is not team-scoped: no way back
        encode_failed,     // pack_h refused (the frame did not fit the buffer)
        // ★ §id-hash S1c (QA round 2): tx_initiating DROPPED it — the 4-slot LBT defer ring was full. Distinct from
        // the four above because it is TRANSIENT: the remedy is "retry in a moment", not "change something".
        tx_dropped
    };
    // overheard < authoritative < pinned. pinned = a QR/manually-scanned key (E2E provisioning §1): the MITM-resistant
    // tier — NEVER overwritten by an on-air answer, NEVER LRU-evicted, NEVER aged out (NV-backed on device).
    enum class PeerKeyConf  : uint8_t { overheard = 0, authoritative = 1, pinned = 2 };
    // ★ §AB2: THREE surfaces carry this level as a raw uint8_t, because neither can include this header — command.h's
    // `Push::peer_conf` (node.h includes command.h, not the reverse) and src/device_nv.h's kPeerConf* (the device record
    // layer stays free of the protocol engine). Pin the numeric encoding HERE, at the definition, so a reorder of the
    // enumerators fails the build instead of silently relabelling a stored flash byte or an app-facing JSON string.
    static_assert(static_cast<uint8_t>(PeerKeyConf::overheard)     == 0
               && static_cast<uint8_t>(PeerKeyConf::authoritative) == 1
               && static_cast<uint8_t>(PeerKeyConf::pinned)        == 2,
                  "node.h: PeerKeyConf's numeric encoding is mirrored by command.h Push::peer_conf and device_nv.h "
                  "kPeerConf* — renumbering it relabels a persisted flash byte and the peer_key_cached JSON");
    // ★★★ §AB4 — WHERE A RETAINED POSITION CAME FROM, i.e. WHICH KEY VOUCHES FOR IT (spec §2.7.2). This is a TRUST
    // ANCHOR, not a transport label, and the two anchors are NOT interchangeable:
    //   peer — a DM carrying DATA_FLAG_LOCATION, sealed to US and opened with OUR key. PAIRWISE ⇒ "this specific peer
    //          said so". node_mac.cpp's send gate makes a `-l` DM sealed BY CONSTRUCTION (an unsealed one is refused
    //          `unsealable`), so opening it also proves we hold that peer's key. ★ THE ONLY VALUE REACHABLE TODAY.
    //   team — a team CHANNEL post sealed to the shared team content key. GROUP ⇒ only "some holder of the team key
    //          said so". ✅ LIVE since §chan-crypt CL2b: `send_channel <ch> "…" -t -l -e` packs pack_loc6 into the
    //          SEALED inner (bit1) and node_channel.cpp's ingest_channel_m stores it here. ★ AB4's prediction HELD —
    //          CL2b added a SOURCE and nothing else: one `peer_loc_set` call, no schema change, no new field, no new
    //          PushKind, so the app ships ONE renderer and the weaker claim was never retro-fitted into the stronger
    //          one's slot. ★★ CORRECTED BY §chan-crypt CL2c (owner 2026-08-01): the `team` row is keyed by the
    //          sender's FULL key_hash32 CARRIED IN THE SEALED INNER (bit2, REQUIRED beside bit1), no longer INFERRED
    //          through `_team_keys` — inference failed when no beacon had been heard and mis-attributed after a
    //          DAD-re-picked team_local_id. The carried hash is still cross-checked against the msg-id's 16 hash bits;
    //          a post that fails that, or names a zero hash, is refused outright (no push, no retention).
    // ★★ THE BOUND THIS RECORDS, ACCEPTED DELIBERATELY (the I9 pattern) — DO NOT "FIX" IT: the team content key is
    // SHARED, so sealing a channel post proves MEMBERSHIP, NOT IDENTITY. Every member holds the same team_ch_priv, so
    // ANY KEYHOLDER CAN FORGE ANOTHER MEMBER'S source_hash and publish a false position attributed to a teammate.
    // T-K2's "anti-spoof within the team" means an OUTSIDER cannot spoof a member; it does not and cannot mean a member
    // cannot spoof a member. The owner's model — a hiking group holding one content key trusts itself — is reasonable;
    // `loc_src` is what keeps it HONEST, by never letting a group-anchored position be rendered as a pairwise one.
    enum class PeerLocSrc : uint8_t { peer = 0, team = 1 };
    // Why e2e_seal_inner returned 0 (the seal failed). Lets enqueue_data fail LOUD distinctly per cause instead of
    // treating every 0 as "no pubkey" (which floods a WANT_PUBKEY + drops the DM). no_pubkey is the ONLY case that
    // floods; the rest are local refusals (no_identity=R3, too_large=R2, bad_rng=R7, cross_layer=v1 scope).
    enum class SealOutcome  : uint8_t { ok = 0, no_identity, no_pubkey, too_large, bad_rng, cross_layer };
    bool       is_blind(uint8_t next_hop) const;                         // _blind_until active? (read-only; bounded by neighbour count)
    // ① mobile-as-transit avoidance (Lua dv:1325-1334): learn the is_mobile beacon bit; NEVER relay THROUGH a mobile
    // peer (it roams away), but DO deliver TO one (the next_hop==dest carve-out). Hard-exclude, not a score penalty.
    bool       is_mobile_peer(uint8_t id) const;
#if MR_FEAT_TEAM   // §featuresplit: the TEAM API stubs to inert when off -> call sites (route-select/enqueue_data/handle_rts) unchanged
    bool       is_team_peer(uint8_t id) const;   // §mobile 6.2: id is a KNOWN same-team peer (route to it via _rt_team)
    // ★★ §id-hash S3 (spec 2026-08-01 §3-D1/§3-D2) — THE CONFIDENCE FLOOR REACHES EVERY READER OF _team_keys.
    // `source`/`confidence` are MANDATORY on the setter, exactly as on `id_bind_set` (U1/U3, and C2: a defaulted
    // confidence is precisely the silent fallback that would let S4a's on-air ingest forget to say "this is a claim").
    // The three readers take a FLOOR + an optional `actual` out-param:
    //   • the floor DEFAULTS to `authoritative`, which is what makes S3 inert — every pre-S3 caller keeps its exact
    //     semantics, because today the only writer (a heard beacon) writes `authoritative` and nothing else exists;
    //   • `actual` exists because a caller CANNOT LABEL A CLAIM IT CANNOT SEE (spec §3-D1) — the peer-book row's
    //     `team_authoritative` is filled from it.
    // ★ `team_id_of_key` is the one v1 of the spec missed: it is the REVERSE reader on the LIVE plaintext
    //   send-by-hash path (do_send, node_hashlocate.cpp), so without a floor here a claimed row would leak straight
    //   into sending — spec §3-D7, a claimed binding must never drive an L2c redirect.
    void       team_key_set(uint8_t id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence);   // §enc: cache a same-team peer's key_hash32; team-scoped, NOT _id_bind. §S3: UPGRADE-ONLY — see node_routing.cpp
    bool       team_key_of_id(uint8_t id, uint32_t& out, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;      // §enc: team-scoped id->key_hash32 (for a CRYPTED send BY team_local_id); false = unknown/below the floor
    bool       team_id_of_key(uint32_t key_hash32, uint8_t& out_id, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;   // §mobile 6.4: reverse team-scoped hash->team_local_id (a PLAINTEXT send-by-hash to a HEARD teammate); false = unknown/below the floor
    // §team-ch-key (T-K1, spec 2026-07-26 §2.1): the TEAM CHANNEL keypair — the CONTENT key, distinct from
    // membership. RAM state; src/fw_main.cpp loads it from / src/firmware_config.cpp persists it to the NV Blob
    // (team_ch_pub / team_ch_priv / team_ch_key_present, v22) — the same shape as the admin_* trio above.
    bool           team_channel_key_present() const { return _team_ch_key_present; }
    const uint8_t* team_channel_pub()  const { return _team_ch_key_present ? _team_ch_pub  : nullptr; }   // nullptr = no key (never a zero buffer a caller could mistake for one)
    const uint8_t* team_channel_priv() const { return _team_ch_key_present ? _team_ch_priv : nullptr; }
    bool team_channel_key_mint();                                                   // `team new`: draw 32 B from the HAL CSPRNG -> canonical pair. false = REFUSED (dead RNG); state untouched
    bool team_channel_key_adopt(const uint8_t pub[32], const uint8_t priv[32]);     // `team new tkpub=/tkpriv=` · T-K4 QR. false = REFUSED (all-zero, or pub doesn't match priv); state untouched
    bool team_channel_key_adopt_priv(const uint8_t priv[32]);                       // §T-K3: adopt from the PRIVATE HALF ALONE (the sealed grant carries only tkpriv) — same derivation path, pub re-derived. false = REFUSED (all-zero/degenerate); state untouched
    void team_channel_key_load(const uint8_t pub[32], const uint8_t priv[32], bool present);   // boot restore from NV — VERBATIM, no re-derivation (mirrors admin_load)
    // §o3-key-lifetime (owner ruling 2026-07-31): DESTROY the pair. The one un-doer beside the four establish paths,
    // and `set_team_id` is its only caller — a content key belongs to the team it was granted for and must not
    // outlive it. NOT reachable from the console: no verb drops a key on its own (the pair is UNRECOVERABLE).
    void team_channel_key_clear();
#else
    bool       is_team_peer(uint8_t) const { return false; }
    void       team_key_set(uint8_t, uint32_t, IdBindSource, IdBindConf) {}
    bool       team_key_of_id(uint8_t, uint32_t&, IdBindConf = IdBindConf::authoritative, IdBindConf* = nullptr) const { return false; }
    bool       team_id_of_key(uint32_t, uint8_t&, IdBindConf = IdBindConf::authoritative, IdBindConf* = nullptr) const { return false; }
    // §team-ch-key: inert on a static build (gateway_*, MR_FEAT_TEAM 0) — a node with no team plane can hold no
    // team content key, so the mint/adopt verbs REFUSE (false) rather than silently pretending to succeed (C2).
    bool           team_channel_key_present() const { return false; }
    const uint8_t* team_channel_pub()  const { return nullptr; }
    const uint8_t* team_channel_priv() const { return nullptr; }
    bool team_channel_key_mint() { return false; }
    bool team_channel_key_adopt(const uint8_t*, const uint8_t*) { return false; }
    bool team_channel_key_adopt_priv(const uint8_t*) { return false; }
    void team_channel_key_load(const uint8_t*, const uint8_t*, bool) {}
    void team_channel_key_clear() {}   // §o3-key-lifetime: nothing to destroy — this build holds no team content key
#endif
    // §team-ch-key T-K3 (spec §2.3) — the SEALED key-grant DM (DATA_TYPE_TEAM_KEY_GRANT). Both halves live here in
    // lib/core rather than in src/: the RECEIPT is on the RX path anyway, and putting the ORIGINATION beside it means
    // the native suite can drive the whole round trip (no scenario runs a console verb, so a send half left in
    // firmware_config.cpp would have had zero automated coverage — the T-K1/T-K1b lesson).
    // ★ DELIBERATELY NOT MR_FEAT_TEAM-gated, same reasoning as T-K1b's team_export_key: NodeConfig::team_id is ungated
    // and the team_channel_* accessors have #else stubs, so on a MR_FEAT_TEAM 0 build these compile unchanged and
    // answer no_key / no_team BY CONSTRUCTION — ONE app-facing code path, and a stray type-19 can never be delivered
    // as a DM there either (C2: no silent success, no raw key bytes in an inbox).
    enum class TeamKeyGrantTx : uint8_t {
        queued = 0,     // handed to send_by_hash (ctr!=0 = airborne now; ctr==0 = parked behind an H resolve)
        no_team,        // we are not in a team (team_id == 0) — there is nothing to grant
        no_key,         // we hold no team channel keypair (`team new` mints one; a joiner receives one)
        no_identity,    // no E2E crypto identity, so we cannot seal — and a grant is NEVER sent in the clear
        no_pubkey,      // no AUTHORITATIVE/PINNED pubkey for the target ⇒ the remedy is `reqpubkey <hash>` or a QR import
        self,           // the target hash is OUR OWN key — granting to yourself is a no-op mis-address, not a send
        delegated,      // we are a REGISTERED MOBILE with no resolved binding ⇒ send_by_hash would delegate, and the
                        //   MOBILE_SEND wrapper's enclosed-type slot is already SEALED_RELAY ⇒ the TYPE would be lost
        too_large       // team_name too long for the body (cannot happen from the console — a defence-in-depth refusal)
    };
    enum class TeamKeyGrantRx : uint8_t {
        adopted = 0,    // key installed (idempotent: a re-grant OVERWRITES — that is how re-keying lands)
        not_sealed,     // the type-19 arrived UNSEALED -> drop loud (a plaintext grant is a bug or an attack)
        bad_len,        // body shorter than the 37-B floor, or [4][1][name_len][32] does not account for it EXACTLY
        long_name,      // name_len > 32 (the codebase-wide name cap) -> malformed body, refuse rather than truncate
        no_team,        // we are not in a team, so no grant can match us
        team_mismatch,  // the grant names a DIFFERENT team (spec §2.3 Q2 ruling: team_id is carried defensively)
        bad_key         // tkpriv is all-zero / degenerate (team_channel_key_derive refused it)
    };
    TeamKeyGrantTx team_key_grant_send(uint32_t target_hash, const char* name, uint8_t name_len, Plane plane = Plane::AUTO, uint16_t* out_ctr = nullptr);
    TeamKeyGrantRx team_key_grant_receive(const uint8_t* body, uint8_t body_len, uint32_t granter_hash, uint8_t granter_node);
    // §P2-3 (2026-07-21): is `next` a LOCAL id — a mobile last-mile (addr_len==1) OR a known same-team peer? The guard that
    // keeps a mobile/team LOCAL next OUT of the static node_id-indexed planes (bidi/liveness/rt-rerank). It was hand-pasted at
    // 11 write-sites (node_mac_rx ×9, node_cascade ×2 — two carry "audit-caught missed twin" comments) as
    // `addr_len==1 || is_team_peer(next)`. On a static build is_team_peer stubs false, so the guard is exactly the old inline.
    bool       next_is_local_id(uint8_t addr_len, uint8_t next) const { return addr_len == 1 || is_team_peer(next); }
    // §6.4: a unicast dst is FOR US — our static node_id OR our team-plane id. Off-grid node_id==_team_local_id so the
    // first term already covers it; this matters for a DUAL member (node_id=static id) delivering a DM sent to its team id.
    bool       for_me_dst(uint8_t dst) const {
#if MR_FEAT_TEAM
        return dst == _node_id || (_cfg.team_id != 0 && _team_local_id != 0 && dst == _team_local_id);
#else
        return dst == _node_id;   // no team plane -> only our static id
#endif
    }
    // §P2-1 (mixed-leaf team): the ONE definition of "a team-scoped frame is FOR US" -> LEAF-EXEMPT. A team's `create`
    // fixes the shared PHY; members homed onto DIFFERENT layers (nibbles) share team_id + PHY and MUST stay team-reachable.
    // `their_team` = the frame-carried team id (beacon type-5 TLV / H.team_id / F.team_id / DATA-M team_id). team_id==0
    // (every static, every lone mobile) -> false, so the pre-parse leaf gate stands UNCHANGED (s18/static byte-identical).
    // Consumed at beacon + H + channel M; F/RTS carry the equivalent inline (handle_f_team / team_rts_for_us) — ONE rule,
    // so a fifth RX path can't drift.
    bool       same_team(uint32_t their_team) const { return _cfg.team_id != 0 && their_team == _cfg.team_id; }
    // §P2-1 Level 2 (ruled option (a)): a TEAM member REFUSES a home whose PHY differs from its team-provisioned layers[0]
    // (freq/bw/routing_sf/cr; NOT layer_id — cross-LAYER same-PHY re-home is exactly the supported mixed-leaf case; NOT
    // sf_list — F-SF-1 keeps that across registration). bw/cr are compared EFFECTIVE (0 = inherit the global radio_bw_hz/cr,
    // matching adopt_mobile_phy). team_id==0 -> true (a lone mobile adopts any PHY = today's behavior -> byte-identical).
    bool       team_phy_ok(const LayerConfig& phy) const {
        if (_cfg.team_id == 0) return true;
        const LayerConfig& mine = _cfg.layers[0];
        const uint32_t bw_a = phy.bw_hz  ? phy.bw_hz  : _cfg.radio_bw_hz;
        const uint32_t bw_b = mine.bw_hz ? mine.bw_hz : _cfg.radio_bw_hz;
        const uint8_t  cr_a = phy.cr     ? phy.cr     : _cfg.radio_cr;
        const uint8_t  cr_b = mine.cr    ? mine.cr    : _cfg.radio_cr;
        return phy.freq_mhz == mine.freq_mhz && bw_a == bw_b && phy.routing_sf == mine.routing_sf && cr_a == cr_b;
    }
    // §P2-3 (2026-07-21): a team-scoped unicast (RTS/CTS/DATA/ACK) addressed to OUR team-plane id — the 4× hand-pasted
    // team-acceptance edge (handle_rts team_rts_for_us + team reverse-learn; handle_rts_for_us for_team_rts; handle_data
    // for_team_data). team_id==0 (every static, every lone mobile) -> false, so a static build is byte-identical.
    bool       team_addr_for_us(uint8_t next, uint8_t addr_len) const {
#if MR_FEAT_TEAM
        return _cfg.team_id != 0 && _team_local_id != 0 && next == _team_local_id && addr_len == 1;
#else
        (void)next; (void)addr_len; return false;
#endif
    }
    // §team-parity T6 (spec §3/T6 + the §11 owner ruling, 2026-07-28): "will this OUTBOUND flight be routed on the TEAM
    // plane?" — the SAME predicate rt_find (node_routing.cpp:22) already uses to pick the table, kept as one expression so
    // the identity a flight CLAIMS (stamp_origin) cannot drift from the table it is ROUTED on. TEAM forces the team plane;
    // AUTO dispatches by is_team_peer(dst); GLOBAL forces the static plane even when `dst` numerically collides a
    // teammate's team id (§18) — that carve-out is why this is not simply `plane != GLOBAL`.
    // ⚠ MISSING (deliberate, C1): node_routing.cpp:22 still carries the expression INLINE rather than calling this. It is
    // provably the same expression, but routing rt_find through it is a pure refactor and this is a feature slice — the
    // same C1 split T2 made when it built and then reverted its learn_direct_neighbor hoist. A cleanup slice owns it.
    bool       flight_is_team_plane(Plane plane, uint8_t dst) const {
#if MR_FEAT_TEAM
        return plane == Plane::TEAM || (plane == Plane::AUTO && is_team_peer(dst));
#else
        (void)plane; (void)dst; return false;   // §featuresplit: no team plane -> every flight is static
#endif
    }
    bool       route_uses_mobile_as_transit(uint8_t dest, uint8_t next_hop) const;
    uint8_t    get_neighbor_tier(uint8_t node_id) const;                 // R4.2 tier read (TTL-expiring lazy-prune); public for tests
    void       schedule_triggered_beacon();                             // R4.3 trigger jitter + min-interval defer; public for tests
    int        mark_neighbor_budget_tier(uint8_t node_id, uint8_t tier, const char* source, bool local_only); // :4320; public for tests
    // R4.4 originator anti-spam (dv:3205-3277). track = ledger append (prune+dedup-first); compute = the
    // sliding-window metric. kind: 0=rts, 1=cts. Draw-free. Public for tests.
    void       track_originator_observation(uint8_t sender, uint8_t kind, uint8_t ctr_lo, uint32_t air);
    void       compute_originator_metric(uint8_t sender, int& apparent, uint32_t& total_air,
                                         uint8_t& rts, uint8_t& cts) const;

    // ---- device-console diagnostics: const LIVE reads consumed by fw_main's routes/cfg/status seam.
    uint8_t           node_id()        const { return _node_id; }
#if MR_FEAT_TEAM
    uint8_t           team_local_id()  const { return _team_local_id; }   // §mobile 6.4: the team-plane id (0 = not team-DAD'd)
    void              set_team_local_id(uint8_t id) { _team_local_id = id; _team_dad_pending = false; }   // §mobile 6.4: load a PERSISTED id at boot (id!=0 -> CONFIRMED, no re-DAD, announce + defend) OR ZERO it on leaving the team (id==0)
#else
    uint8_t           team_local_id()  const { return 0; }
    void              set_team_local_id(uint8_t) {}
#endif
    // §per-layer-id (2026-07-05): the id to PERSIST as nv.node_id (restore maps it to layers[0].node_id). A GATEWAY's
    // node_id() is the ACTIVE-leaf mirror (activate_layer stamps _node_id = _active leaf's node_id, flipping with the
    // window) — persisting it clobbers layer0's canonical id. layers[0].node_id is the stable, explicit gateway id (no
    // per-leaf DAD writes it back). A single-layer node has NO per-leaf id + DAD updates only _node_id, so persist that.
    uint8_t           canonical_node_id() const { return _cfg.is_gateway ? _cfg.layers[0].node_id : _node_id; }
    // The FULL 8-bit layer_id of the ACTIVE leaf (a gateway alternates leaves on the window schedule). Public so the
    // device console (`debug on`) can announce which layer the gateway is currently LISTENING on. Single-layer: layers[0].layer_id.
    uint8_t           active_layer_id() const { return _cfg.layers[static_cast<size_t>(_active - &_layers[0])].layer_id; }
    // The ACTIVE leaf's INDEX into _layers[] / _cfg.layers[] (0..n_layers-1) — NOT its wire layer_id (that is
    // active_layer_id() above). Added 2026-07-27 for the layer-explicit paths (flood_state_free / purge_tx_carriers),
    // which must name a leaf that is not necessarily the active one. The four inline `_active - &_layers[0]` uses in
    // this block predate it and are deliberately left alone (C1: converting them is a refactor, not this fix).
    uint8_t           active_layer_index() const { return static_cast<uint8_t>(_active - &_layers[0]); }
    // Per-layer BW/CR (2026-07-04): the ACTIVE leaf's bandwidth/coding-rate for the airtime model — the same
    // runtime->config index idiom as active_layer_id(). 0 in the LayerConfig = inherit the global radio_bw_hz/radio_cr
    // (a single-layer node's sole layer inherits, so these read identically to the scalars = byte-identical behavior).
    uint32_t          active_bw_hz()   const {
        const uint32_t b = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].bw_hz;
        return b > 0 ? b : _cfg.radio_bw_hz;
    }
    uint8_t           active_cr()      const {
        const uint8_t c = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].cr;
        return c > 0 ? c : _cfg.radio_cr;
    }
    // §layer-freq (2026-07-27): the ACTIVE leaf's RF carrier in MHz — the third member of the same family
    // (same runtime->config index idiom; 0 in the LayerConfig = inherit the global radio_freq_mhz). Exists so
    // activate_layer can push the EFFECTIVE carrier unconditionally and therefore RESET an inherit-leaf back
    // off the previous leaf's override. Returns 0.0 only when NOTHING configured a carrier (no per-layer
    // override anywhere and no global) — a documented HAL no-op, and the legacy single-carrier behaviour.
    double            active_freq_mhz() const {
        const double f = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].freq_mhz;
        return f > 0.0 ? f : _cfg.radio_freq_mhz;
    }
    uint32_t          key_hash32()     const { return _key_hash32; }
    void              set_name(const char* name, uint8_t len) { _name_len = len > sizeof _name ? (uint8_t)sizeof _name : len; for (uint8_t i = 0; i < _name_len; ++i) _name[i] = name[i]; }   // §1.3: load the /mrid name into the core (for the pubkey exchange + display)
    uint8_t           name_len()       const { return _name_len; }
    uint8_t           effective_name(char* out, uint8_t cap) const;   // §1.3: the stored name, or "MeshRoute node: 0x<hash>" (the STABLE hash — the id can change) when empty. Returns the length written (never null-terminates).
    bool              crypto_ready()   const { return _crypto_ready; }   // DP1: a crypto identity is installed
    const NodeConfig& config()         const { return _cfg; }
    NodeConfig&       mutable_config()       { return _cfg; }   // LIVE tweak of dynamically-read cfg (device `cfg set`):
                                                                // touch ONLY fields the MAC re-reads each use (sf_list/lbt/
                                                                // beacon/nav/hop_cap/leaf_id/gateway), NOT on_init-cached (duty).
    // Live `cfg set` of the radio knobs (control SF / BW / CR / carrier) — updates the config the MAC + airtime
    // read, WITHOUT re-initing the Node (routes / in-flight flight survive). LBT-derived delays are cached at
    // on_init and go stale on a live change, but LBT is off by default and needs a reboot to enable.
    // §layer-freq (2026-07-27): freq_mhz joined the set because a gateway's next window switch pushes
    // active_freq_mhz() — a live `cfg set freq` that left _cfg.radio_freq_mhz stale would retune the radio now
    // and then snap it BACK to the old global on the next INHERIT-leaf activation.
    // ⚠ MISSING (pre-existing, NOT fixed here): this only repairs the INHERIT fallback. On a real board
    // fw_main pre-resolves the inherit into layers[0]/[1].freq_mhz at NV-load, so both leaves hold an
    // EXPLICIT boot carrier that still wins over the updated global — i.e. a live `cfg set freq` on a gateway
    // still snaps back at the next window switch, exactly as before this slice. The cure is to update the
    // per-layer values too (or to stop pre-resolving in fw_main); both change metal behaviour and need a
    // bench-gated slice of their own, so they are deliberately out of scope.
    void set_radio_cfg(uint8_t routing_sf, uint32_t bw_hz, uint8_t cr, double freq_mhz) {
        _cfg.routing_sf = routing_sf; _cfg.radio_bw_hz = bw_hz; _cfg.radio_cr = cr; _cfg.radio_freq_mhz = freq_mhz;
    }
    // R6.3 §2 (provisioning verbs / decision b): re-derive the duty-cycle budget after a LIVE duty change
    // (the join/create verbs, adopt_c_config, cfg-set duty) so enforcement applies without a reboot. Mirrors
    // the on_init derivation (dv:8497) — closes the known duty-live gap (the budget was on_init-cached only).
    void recompute_duty_budget() {
        _duty_cycle_budget_ms = (_cfg.duty_cycle > 0.0)
            ? static_cast<uint64_t>(_cfg.duty_cycle * _cfg.duty_cycle_window_ms) : 0;
    }
    // R6.3 provisioning verbs: reset BOTH the live epoch and the max-seen tracker together (a join/leave -> 0,
    // a create -> 1). Keeps _max_seen_epoch from leaking an old leaf's numbering into a fresh lineage's writes.
    void reset_leaf_epoch_state(uint16_t epoch) { _cfg.config_epoch = epoch; _max_seen_epoch = epoch; }
    // Reset the join FSM so a re-DAD actually RUNS on a reprovision (join/create verbs). set_identity(0) alone leaves
    // _joined set, and CmdKind::join is idempotent-once-joined -> the DAD never fires. Shared with forced_rejoin.
    void reset_join_for_reprovision();
    // Reprovision (join/create/leave verbs ONLY — NOT the heal): the old network's routes are stale. Drop ALL learned
    // routes / id-bindings / gateway schedules so the node starts the new network with a clean table. (The heal keeps
    // its routes — same network, only the id changed — so this is NOT in reset_join_for_reprovision.)
    void clear_routing_state();
    // §clean-team (2026-07-27): the TEAM plane's half of that clear, extracted so it has exactly ONE implementation and
    // TWO callers (U1) — clear_routing_state() above (the static reprovision verbs, which ALSO wipe the static plane)
    // and set_team_id() below (a team switch, which must NOT: the static network did not change, so a HOMED team mobile
    // keeps its static routes, its id-bindings and its home registration). Full inventory + the deliberate
    // NOT-cleared list are documented at the definition (node.cpp).
#if MR_FEAT_TEAM
    void clear_team_routing_state();
#else
    void clear_team_routing_state() {}   // §featuresplit: no team plane compiled in -> nothing to clear
#endif
    // §clean-team-channel + §clean-join-carriers (2026-07-27): drop every carrier that could still put a frame ON THE AIR
    // built from scope/state we are about to invalidate. ONE mechanism, TWO axes (U1) — the axis selects the per-carrier
    // predicate AND the leaf scope; see the definition (node_channel.cpp) for the full carrier table, the dependent-state
    // audit (must-scrub / harmless-and-why / not-affected) and the named residuals. READ IT before "completing" anything
    // it deliberately leaves alone.
    // ★ NOT #if MR_FEAT_TEAM-gated (it used to be, as purge_team_channel_state): the reprovision axis has NOTHING to do
    // with the team plane and `leave` IS dispatched on the gateway build (firmware_commands.cpp — only join/create are
    // MR_N_LAYERS<2), so a team-gated stub would silently stop wiping the channel plane on exactly that profile. Every
    // field the predicates read (ChannelEntry::team_id, FloodState::team_flood, channel_flavor_team) is ungated.
    enum class PurgeAxis : uint8_t {
        team_switch,   // set_team_id(): the OLD TEAM's rows/carriers only — the node's LEAF plane is still valid and SURVIVES. ACTIVE leaf only.
        reprovision,   // clear_routing_state(): the whole NETWORK changed -> EVERY staged/in-flight frame goes, on EVERY leaf.
    };
    void purge_tx_carriers(PurgeAxis axis);
    // §clean-team (2026-07-27, owner bench report "when creating a new team / joining a team, existing team routes need
    // to be cleared"): THE team-switch entry point — every LIVE writer of _cfg.team_id goes through here (`team new`,
    // `team <id>`, `team 0` — `cfg set team_id` was REMOVED 2026-07-31, §team-id-cfg-removal/B27), so the switch is
    // coherent in ONE place instead of per-verb. Returns true iff the team ACTUALLY changed (a same-team no-op clears
    // nothing — nothing is stale — and the caller skips its re-DAD). ⚠ NOT for the boot/NV path, which writes
    // _cfg.team_id directly: at boot nothing is stale AND the persisted team-DAD id must survive (see node.cpp).
    // ★★ B28/R2 (2026-07-31): adopting a NON-ZERO team also sets `_cfg.is_mobile` (role_enforce, node_role.h) — a team
    // member IS a mobile. ONE-DIRECTIONAL: `team 0` never clears it. On a build with MR_FEAT_MOBILE 0 a non-zero team
    // is REFUSED (returns false, nothing moves) instead — see the definition.
    bool set_team_id(uint32_t team_id);
    // `prep-restart` middle-tier reset: drop EVERY volatile/learned table (routes + channel buffer + liveness + pending
    // TX/RX + flood + digest/pull + dedup maps + parked/l2c/mediated) to a fresh-but-PROVISIONED state. KEEPS _cfg
    // (node_id/layer/sf_list/lineage), the crypto identity, and the DAD join — no re-join needed. (node.cpp)
    void clear_learned_state();
    // Set by a verb reprovision (do_dad); join_adopt consumes it to restart discovery ONCE the new id is stable, so the
    // fast-cadence beacons + the REQ_SYNC route-bootstrap go out under the adopted id (not the transient 0).
    void set_rediscover_pending(bool v) { _pending_rediscover = v; }
    void restart_discovery();    // re-enter discovery (fast beacon cadence + REQ_SYNC pull) to rebuild routes
    uint8_t           rt_count()       const { return _active->_rt_count; }
    uint8_t           mobile_reg_count() const { return _active ? _active->_mobile_reg_n : 0; }   // §mobile 2a: mobiles registered to this host (test/diagnostic accessor)
    bool              mobile_reg_at(uint8_t i, uint32_t& key_hash, uint8_t& local_id, bool& has_pubkey) const {   // §mobile: read a hosted-mobile entry (the `routes` dump)
        if (!_active || i >= _active->_mobile_reg_n) return false;
        const auto& e = _active->_mobile_reg[i]; key_hash = e.key_hash32; local_id = e.mobile_local_id; has_pubkey = e.has_pubkey; return true; }
    // §mobile console: user/app-driven network control (fw_main handle_mobile reuses the FSM + the pull; NO new wire).
    bool              mobile_autoregister_on() const { return _cfg.mobile_autoregister; }
#if MR_FEAT_MOBILE
    uint8_t           mobile_home_id() const { return _my_mobile_reg.active ? _my_mobile_reg.home_id : 0; }   // §mobile 2b: our host (0 = unregistered)
    bool              mobile_registered()      const { return _my_mobile_reg.active; }
    uint8_t           mobile_local_id()        const { return _my_mobile_reg.my_local_id; }
    uint16_t          mobile_reg_epoch()       const { return _my_mobile_reg.epoch; }
    uint8_t           mobile_home_layer()      const { return _my_mobile_reg.home_leaf_id; }
    uint8_t           learned_layers_count()   const { return _learned_layers_n; }
    const LayerRecord& learned_layer(uint8_t i) const { return _learned_layers[i]; }
#else
    uint8_t           mobile_home_id()         const { return 0; }
    bool              mobile_registered()      const { return false; }
    uint8_t           mobile_local_id()        const { return 0; }
    uint16_t          mobile_reg_epoch()       const { return 0; }
    uint8_t           mobile_home_layer()      const { return 0; }
    uint8_t           learned_layers_count()   const { return 0; }
#endif
    uint8_t           bridged_layer_cap()      const { return protocol::cap_bridged_layers; }
    const BridgedLayer& bridged_layer(uint8_t i) const { return _bridged_layers[i]; }
#if MR_FEAT_MOBILE
    // §autoregister ruling (2026-07-21): with mobile_autoregister=false the FSM emits NO DISCOVERs on its own — the app
    // ARMS one here. _mobile_arm_once is the one-shot the DISCOVER half consumes (registration_armed()); team-DAD is unaffected.
    void              mobile_register_current() { _mobile_arm_once = true; (void)_hal.after(0, kMobileDiscoverTimerId); }             // DISCOVER on the current PHY now
    void              mobile_register_phy(const LayerConfig& phy) { adopt_mobile_phy(phy); _mobile_arm_once = true; (void)_hal.after(0, kMobileDiscoverTimerId); }  // retune + DISCOVER
    void              mobile_register_scan()    { _mobile_scan_idx = 0; _mobile_arm_once = true; (void)_hal.after(0, kMobileDiscoverTimerId); }  // cycle [current] ∪ learned
    void              mobile_send_layer_query(uint8_t gw) {                                                  // manual pull: MOBILE_LAYER_QUERY -> gw
        uint8_t q = 0; (void)enqueue_data(gw, &q, 0, DATA_FLAG_SOURCE_HASH, "mobile_layer_query", false, DATA_TYPE_MOBILE_LAYER_QUERY, CryptIntent::off);
    }
    uint8_t           mobile_offers_n() const { return _mobile_offers_n; }                        // §mobile 2b: OFFERs collected this window (test/diag)
#else
    uint8_t           mobile_offers_n() const { return 0; }
#endif
    const RtEntry&    rt_at(uint8_t i) const { return _active->_rt[i]; }   // 0..rt_count()-1; candidates[0] is the primary
#if MR_FEAT_TEAM
    uint8_t           rt_team_count()  const { return _active->_rt_team_count; }   // §mobile 6.2: the TEAM plane (test/diag)
    const RtEntry&    rt_team_at(uint8_t i) const { return _active->_rt_team[i]; }
#else
    uint8_t           rt_team_count()  const { return 0; }
    // rt_team_at: no stub — a !MR_FEAT_TEAM build has no _rt_team; its (test/fw_main-diag) callers are guarded #if MR_FEAT_TEAM
#endif
    // Console testing aid: manually force / drop a route, to stress the routing algorithms with arbitrary or
    // inconsistent routes. route_inject returns true if the candidate took (rt_merge can reject if better candidates
    // already hold the K slots). route_remove drops a dest's whole entry.
    bool route_inject(uint8_t dest, uint8_t next_hop, uint8_t hops, int16_t score_q4) {
        RtCandidate c{}; c.next_hop = next_hop; c.hops = hops; c.score = score_q4;
        c.last_seen_ms = _hal.now(); c.learned_leaf = _cfg.leaf_id;
        rt_merge(dest, c);
        const RtEntry* e = rt_find(dest);
        if (e) for (uint8_t i = 0; i < e->n; ++i) if (e->candidates[i].next_hop == next_hop) return true;
        return false;
    }
    bool route_remove(uint8_t dest) {
        for (uint8_t i = 0; i < _active->_rt_count; ++i)
            if (_active->_rt[i].dest == dest) { rt_remove(i); return true; }
        return false;
    }
    int16_t peer_penalty_q4(uint8_t node_id) const { return liveness_penalty_q4(node_id); }   // liveness (suspect/silent/dead) penalty on a next-hop; routes dump shows effective = score - pen
    LinkBidi          link_bidi_state(uint8_t node_id) const { return static_cast<LinkBidi>(_active->_link_bidi[node_id]); }  // bidi plane read (test/status); unknown for any unprobed link
    uint64_t          link_bidi_confirmed_ms(uint8_t node_id) const { return _active->_link_bidi_confirmed_ms[node_id]; }    // last-confirmation ms (test/status); 0 = never confirmed
#if MR_FEAT_TEAM
    // §team-parity T5: the TEAM-plane twins (test/status). Keyed by team_local_id, read out of the _team_liveness slot —
    // NEVER _link_bidi (invariant I8). No slot for that id => the link was never probed => unknown / 0.
    LinkBidi          team_link_bidi_state(uint8_t team_local_id) const {
        const PeerLiveness* s = team_liveness_find(team_local_id);
        return s ? static_cast<LinkBidi>(s->team_bidi_state) : LinkBidi::unknown;
    }
    uint32_t          team_link_bidi_confirmed_s(uint8_t team_local_id) const {
        const PeerLiveness* s = team_liveness_find(team_local_id);
        return s ? s->team_bidi_confirmed_s : 0u;
    }
#endif
#ifdef MESHROUTE_NATIVE
    uint8_t           link_bidi_at(uint8_t node_id) const { return _active->_link_bidi[node_id]; }   // raw LinkBidi (test/white-box)
    void              test_update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* e, uint8_t n, bool complete, bool team_plane = false) { update_link_bidi_from_beacon(advertiser, e, n, complete, team_plane); }  // white-box: drive the Slice-3 detection scan directly (§T5: on either plane)
    void              test_ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta) { ingest_beacon(bytes, len, meta); }  // white-box: drive ingest_beacon directly (Slice 3 end-to-end)
    int16_t           test_team_penalty_q4(uint8_t next_hop) const { return liveness_penalty_q4(next_hop, /*team_plane=*/true); }   // white-box: the team-plane liveness penalty (§clean-join R3 reset check)
    // §S0 (cold-boot mobile-id alias) white-box seams.
    uint8_t           test_find_free_mobile_id(uint32_t key_hash32) { return find_free_mobile_id(key_hash32); }   // top-down allocation + static-exclusion
    bool              test_route_uses_mobile_as_transit(uint8_t dest, uint8_t next) const { return route_uses_mobile_as_transit(dest, next); }   // the transit filter (alias carve)
    // ★ §id-hash S1d test seam: fire one LBT deferred-TX slot's timer. The id range is private and MUST stay so —
    // a test hard-coding 15 would silently drive the wrong timer if the range ever moves.
    void              test_fire_lbt_defer(uint8_t slot) { on_timer(kLbtDeferTimerId + slot); }
    // ★ §id-hash S4b test seam, same rule as the line above: the periodic aging sweep is where the two-stage
    // `reqpubkey` intent times out (spec §5 step 5), and its timer id is private so a test cannot hard-code 2.
    void              test_fire_aging() { on_timer(kAgingTimerId); }
    void              test_mark_mobile_peer(uint8_t id) { _active->_mobile_peer[id >> 3] |= static_cast<uint8_t>(1u << (id & 7)); }   // simulate an is_mobile beacon setting the SET-only bit
    bool              test_id_bind_set(uint8_t id, uint32_t key_hash32, bool authoritative) { return id_bind_set(id, key_hash32, IdBindSource::bcn, authoritative ? IdBindConf::authoritative : IdBindConf::claimed); }
    void              test_defer_send(uint8_t dst, uint16_t ctr, uint8_t redrain_count) { TxItem it{}; it.dst = dst; it.ctr = ctr; it.redrain_count = redrain_count; defer_send(it); }   // drive the defer-loop giveup directly
    uint8_t           test_deferred_count() const { return _active->_deferred_n; }
    // §S3 part2 white-box: suspend the become_free tx-drain so an enqueued frame STAYS in the queue for a wire-golden read
    // (else the MAC immediately drains it into an RTS/CTS flight and the DATA never sits in the queue). Sets the same
    // half-duplex guard become_free honors. Reset to false to resume normal draining.
    void              test_suspend_tx_drain(bool on) { if (on) _active->_pending_tx.emplace(); else _active->_pending_tx.reset(); }
    // §S3 part2 white-box: inspect the tx queue (a last-mile forward stays queued while the drain is suspended — the wire golden reads it here).
    uint8_t           test_tx_queue_n() const { return _active->_tx_queue_n; }
    uint8_t           test_tx_type(uint8_t i)     const { return _active->_tx_queue[i].type; }
    uint8_t           test_tx_flags(uint8_t i)    const { return _active->_tx_queue[i].flags; }
    uint8_t           test_tx_dst(uint8_t i)      const { return _active->_tx_queue[i].dst; }
    uint8_t           test_tx_origin(uint8_t i)   const { return _active->_tx_queue[i].origin; }   // §team-parity T6 white-box: the id stamp_origin chose for this flight (the whole subject of Part A)
    uint8_t           test_tx_addr_len(uint8_t i) const { return _active->_tx_queue[i].addr_len; }
    const uint8_t*    test_tx_inner(uint8_t i, uint8_t& len) const { len = _active->_tx_queue[i].inner_len; return _active->_tx_queue[i].inner; }
    // §team-ch-key T-K3 white-box: the two STRUCTURAL sealed-only guards live on private origination seams whose only
    // reachable caller (team_key_grant_send) forces CryptIntent::on — i.e. exactly the "future caller" the guards exist
    // to stop. Without these seams the guards would be algebra-only, which is the coverage claim this arc refuses to
    // make. They forward VERBATIM, add no logic, and each test also runs the type-0 control through the same seam so a
    // pass cannot come from the path being broken.
    uint16_t          test_do_send_typed(uint8_t dst, const uint8_t* body, uint8_t body_len, CryptIntent crypt,
                                         uint32_t override_dst_hash, uint8_t type) {
        return do_send(dst, body, body_len, /*flags=*/0, crypt, override_dst_hash, type);
    }
    bool              test_enqueue_cross_layer_typed(uint8_t gw_node, uint32_t dst_hash, const uint8_t* layer_ids,
                                                     uint8_t n_layers, const uint8_t* body, uint8_t body_len,
                                                     uint16_t* out_ctr, uint8_t type) {
        return enqueue_cross_layer(gw_node, dst_hash, layer_ids, n_layers, /*cur=*/0, body, body_len, /*flags=*/0, out_ctr, type);
    }
    // §S3 part2 white-box: seed a live hosted-mobile entry (has_pubkey) without driving the full CLAIM+probe path (that's covered by s22).
    void              test_add_host_mobile(uint32_t key_hash32, uint8_t local_id, const uint8_t ed_pub[32]) {
        if (_active->_mobile_reg_n >= protocol::cap_host_mobiles) return;
        auto& e = _active->_mobile_reg[_active->_mobile_reg_n];
        e = { key_hash32, local_id, /*epoch=*/1, _hal.now() };
        for (int i = 0; i < 32; ++i) e.ed_pub[i] = ed_pub[i];
        e.has_pubkey = true;
        _active->_mobile_reg_n++;
    }
    // §S7 T-B white-box: mark THIS node as a registered mobile (mobile-side) without driving the full DISCOVER/CLAIM (s22 covers that).
    void              test_set_my_mobile_reg(uint8_t home_id, uint8_t local_id) {
        _my_mobile_reg = { /*active=*/true, home_id, local_id, /*home_key_hash32=*/0u, _cfg.leaf_id, /*epoch=*/1, _hal.now() };
    }
#endif
    // A heard 1-hop gateway's stored window schedule (nullptr if none known) + the ms to defer an RTS to its window.
    // For the `routes` console dump: surface a gateway route's unique state (period / per-leaf windows / heard-age).
    const GatewaySchedule* rt_gateway_schedule(uint8_t gw_node_id) const { return find_gw_schedule(gw_node_id); }
    uint32_t          rt_gateway_defer_ms(uint8_t gw_node_id) const       { return gateway_schedule_base_defer_ms(gw_node_id, nullptr); }  // base (no jitter) — stable display
    void              rt_resort_for_pick(uint8_t dest) { refresh_route_order(dest, "test_pick"); }   // test: force the pick-time re-sort (freshness/penalty applied)
    void              test_set_link_one_way(uint8_t next_hop) {                    // §bidi test: drive a one_way transition + its fan-out (mirrors the real Slice-3 detection)
        _active->_link_bidi[next_hop] = static_cast<uint8_t>(LinkBidi::one_way);
        resort_routes_for_neighbor_penalty(next_hop, "test_one_way", /*local_only=*/true);
    }
    void    test_learn_route(uint8_t dest, uint8_t via, uint8_t hops, int16_t snr_q4, bool team_plane) { learn_route_via(dest, via, hops, snr_q4, team_plane); }  // §S7 test seam: install a 1-hop route into _rt (team_plane=false) or _rt_team (true) without beacon setup
    void    note_link_confirmed(uint8_t next_hop, bool team_plane = false);   // local bidi confirm (real CTS / complete-heard-set hit): set confirmed + stamp + fan out. §T5: team_plane -> _team_liveness[].team_bidi_* + team_resort (NEVER _link_bidi)
    void    decay_link_bidi(uint8_t next_hop, bool team_plane = false);   // confirmed + stale past bidi_confirm_ttl_ms -> unknown (MF6: NEVER -> one_way). §T5: team_plane -> the team slot
    void    set_link_bidi_for_test(uint8_t next_hop, LinkBidi v) { _active->_link_bidi[next_hop] = static_cast<uint8_t>(v); }  // test seam: seed a bidi state directly
#if MR_FEAT_TEAM
    // §team-parity T5 test seam: seed a TEAM bidi state directly (creates the slot, like the real one_way/confirm path).
    void    set_team_link_bidi_for_test(uint8_t team_local_id, LinkBidi v) {
        PeerLiveness* s = team_liveness_slot(team_local_id, /*create=*/true);
        if (s) s->team_bidi_state = static_cast<uint8_t>(v);
    }
#endif
    bool    candidate_degraded(const RtCandidate& c, bool team_plane = false) const;   // LIVE: c.degraded_from_wire || bidi(c.next_hop)==one_way on the GIVEN plane (never a sticky cache, MF5/OI1). §T5: team_plane reads the team bidi slot (was wire-only)
    int16_t bidi_penalty_q4(uint8_t next_hop, bool team_plane = false) const;          // §bidi: one_way next-hop -> bidi_penalty_one_way_q4, unknown/confirmed -> 0 (PURE; composed into effective_score at node_routing.cpp:100, SORT-only — never a next_hop_selectable gate). §T5: team_plane -> the team bidi slot
    size_t            test_build_suspect_ext(uint8_t* out, size_t cap) { return build_suspect_ext(out, cap); }                 // §P4 test: drive the gossip encoder
    void              test_apply_suspect_gossip(const SuspectEntry* e, uint8_t n, uint8_t src) { apply_suspect_gossip(e, n, src); }   // §P4 test: drive the gossip apply
    void              test_emit_beacon(const char* kind) { emit_beacon(kind); }   // §5 census/advertise tests: drive a deterministic beacon (bypasses the throttle)
    bool              has_pending_tx() const { return _active->_pending_tx.has_value(); }
    bool              tx_queue_full()  const { return _active->_tx_queue_n >= kTxQueueCap; }   // enqueue_data SILENTLY drops when full -> callers (firmware scheduled-send) gate on this before originating
    uint64_t          nav_until_ms()   const { return _nav_until_ms; }  // NAV reservation deadline (0 = clear); test/status accessor
    uint32_t          test_nav_duration_rts(uint8_t sf, uint8_t payload_len, uint8_t data_cr) const { return nav_duration_rts(sf, payload_len, data_cr); }  // M6: white-box the payload_len clamp · §rts-cr-overhear: white-box the peer-CR term
    uint32_t          test_nav_duration_cts(uint8_t sf, uint8_t payload_len, uint8_t data_cr) const { return nav_duration_cts(sf, payload_len, data_cr); }  // §rts-cr-overhear: white-box the peer-CR term + the payload_len=0 max-frame fallback
    // ---- channel-plane inspection (public, like rt_count) + the two seams tests drive directly ----
    uint16_t          channel_buffer_count() const { return _active->_channel_buffer_n; }
    bool              channel_has(uint32_t id) const { return channel_buffer_find(id) >= 0; }
    // ---- id_bind (hash-locate substrate) inspection: tests + the H resolver drive these.
    uint16_t          id_bind_count() const { return _active->_id_bind_n; }
    bool              joined()        const { return _joined; }        // DAD: adopted a node_id (test/app accessor)
    bool              in_discovery()  const { return _active && _active->_discovery_mode; } // per-active-leaf; _active-guard for pre-init safety
    uint32_t          steady_beacon_period_ms() const {   // §team-multihop (spec 2026-07-15 Change A): a TEAM member's steady cadence = team_beacon_period_ms (more responsive than static's 15 min, for a roaming team); a static node (team_id==0) keeps beacon_period_ms -> s18-inert
        return (_cfg.is_mobile && _cfg.team_id != 0) ? _cfg.team_beacon_period_ms : _cfg.beacon_period_ms; }
    // §team-parity T0 (spec 2026-07-27 §3/T0): the hop ceiling FOR A PLANE — the DV combined-hops cap, the F RREQ
    // TTL and the RREQ/RREP hop-cap backstops all key off it. Same shape as steady_beacon_period_ms above (U3), and
    // deliberately NOT MR_FEAT_TEAM-gated so it compiles identically on the three gateway_* envs (MR_FEAT_TEAM 0):
    // a `{}`/stub form would be invisible to the byte-identity corpus, which is exactly how the 2026-07-27ze gateway
    // channel-wipe near-miss happened.
    // ★★ DONE vs MISSING after T1 (spec §3/T1). DONE — four of the six consumers now pass the real plane, so the
    // TEAM discovery plane runs on team_hop_cap (8) end to end: node_cascade.cpp:145 (team cascade-exhaustion RREQ
    // TTL) · node_cascade.cpp:317 (deferred-drain requery TTL, `team_rreq`) · node_route_discovery.cpp:224, which
    // feeds BOTH the RREQ `f.hops >= cap` guard and the RREP `f.hops > 2*cap` backstop. The cap and the TTL therefore
    // stay in the relationship the static plane has always had.
    // MISSING — two consumers still hardcode `false`, each for its own reason, neither an oversight:
    //   · node_beacon.cpp:884 (DV combined-hops cap) — ★ T3 ATTEMPTED THE FLIP, MEASURED IT AND BACKED IT OUT
    //     (2026-07-28). It is NOT the "halves the team DV radius across 9 scenarios" change T0/T1 predicted: measured,
    //     **0 of those 9 move** (their team paths are <= 3 hops, so 8 and 16 decide identically). Its one real effect
    //     is to disarm s35a/s38, and NO value of team_hop_cap restores them, for a structural reason recorded in full
    //     at that site. Re-attempting it needs s35a re-authored FIRST. The config prerequisite is landed (T3 gave
    //     team_hop_cap a `cfg set` + readouts + a sim dispatch row), so the flip itself is one token.
    //   · node_cascade.cpp:164 (§P3 dead-primary rediscovery) — STATIC-ONLY BY CONSTRUCTION and nothing flips it
    //     later; the team branch above it returns early to avoid the static _peer_liveness array it reads.
    uint8_t           hop_cap_for(bool team_plane) const { return team_plane ? _cfg.team_hop_cap : _cfg.dv_hop_cap; }
    // Duty-cycle consumption readout (console `duty` + companion). 0..100% of the rolling-window budget (100 = the node
    // must stay silent); avail_ms = ms until SOME airtime ages back in (0 when there's headroom); enabled=false = no
    // limit. Pure accessor — surfaces what duty_over_budget already computes; no state change.
    struct DutyStatus { uint8_t pct; uint32_t avail_ms; bool enabled; };
    DutyStatus        duty_status() const;
    // Anti-spam v2 (MF1/MF8): the channel-cap duty basis D = duty_cycle * originator_window_ms — a 5-MINUTE budget
    // (1% -> 3000 ms). Deliberately NOT _duty_cycle_budget_ms (a 1-HOUR budget, 12x too big for the 5-min cap window).
    // Returns 0 when duty is disabled (duty_cycle <= 0) — the sentinel the legacy-flat-cap fallback (MF2) keys on.
    uint32_t          channel_duty_budget_ms() const {
        return (_cfg.duty_cycle > 0.0)
            ? static_cast<uint32_t>(_cfg.duty_cycle * protocol::originator_window_ms)
            : 0u;
    }
    // Anti-spam v2 (2026-06-30): the SF/mesh/duty-aware per-origin CHANNEL cap (distinct floods/origin/window). Pure,
    // const, draw-free. MF2: duty disabled (channel_duty_budget_ms()==0) -> the legacy flat cap. Else MF1/MF3:
    // T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(),...); C = max(1, D/T_ch); shared C/N_active among origins.
    uint16_t          channel_cap_origin() const;
    // `limits` query snapshot (companion anti-spam/headroom screen). Live-computed on demand: counters +
    // the channel_capacity_C()/channel_cap_origin() formula (cheap, idempotent, no state change). *_next_ms = the
    // true "when can I send next" = max(burst-floor remaining, channel window cap-wait, duty recovery).
    struct LimitsSnapshot {
        uint32_t win_ms, win_left_ms, n, ch_sf, ch_cap, ch_used,
                 ch_min_ms, ch_next_ms, ch_ceiling, dm_min_ms, dm_next_ms, duty_ms, duty_used_ms;
    };
    LimitsSnapshot    limits_snapshot() const;
    // id_bind reverse lookup. ★ §id-hash S3: the AUTHORITATIVE-only rule is now a PARAMETER, not a hard filter, and
    // the default reproduces the old behaviour exactly (`false` = unknown, expired, or below the floor ⇒ DST_HASH
    // omitted). The floor is the twin of team_key_of_id's above — one policy, one spelling, both planes (U1).
    // ⚠ WHY IT HAD TO BECOME A PARAMETER (spec §3-D1, "load-bearing"): the hard filter made a `claimed` binding
    //   invisible to EVERY reader, so S4a would have landed a mechanism nothing could observe or display.
    bool              key_hash_of_id(uint8_t id, uint32_t& out, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;  // Public for the send-path test.
    int               mobile_home_find(uint32_t mobile_hash, uint8_t* home_layer_out = nullptr) const;   // §mobile 3c/5b: cached mobile_hash -> home_id (+layer out-param), or -1 (TTL-checked). Public for the send-path test.
    void              mobile_home_set(uint32_t mobile_hash, uint8_t home_id, uint8_t epoch = 0, uint8_t home_layer = 0);  // §mobile 3c/4a/5b: insert/refresh (evict oldest if full); freshest-epoch wins. SILENT.
    void              mobile_home_age_out();                            // §mobile 3c: TTL drop (alongside id_bind_age_out)
    int               mobile_home_on_leaf(uint8_t leaf, uint32_t mobile_hash) const;    // §5b: mobile_home_cache lookup on a SPECIFIC leaf (the cross-layer bridge, not _active)
    void              on_mobile_hash_bind_response(const uint8_t* inner, uint8_t inner_len);  // §mobile 4a: a MOBILE_H_ANSWER -> cache M->home (epoch), NO id_bind. public = deliver seam + test
    void              on_mobile_hash_bind_pubkey_response(const uint8_t* inner, uint8_t inner_len);  // §mobile Part 2 Fix 8: a MOBILE_H_ANSWER_PUBKEY -> cache peer_key(M) + M->home, NO id_bind. public = deliver seam + test
    void              on_mobile_key_forward(const uint8_t* body, uint8_t len);   // §S3 part2 (mobile side): cache the home-forwarded requester key (self-consistency-checked) + push peer_key_cached; closes the recipient-side decrypt gap. public = deliver seam + test
    uint8_t           claim_epoch()   const { return _claim_epoch; }
    void              restore_join_state(uint8_t claim_epoch, bool joined) { _claim_epoch = claim_epoch; _joined = joined; }  // boot: reload persisted DAD state (NV)
    // Channel send-ctr persistence (metal reboot id-reuse fix): the self-keyed _peer_send_counter entry = the LAST
    // channel ctr this node minted. channel_ctr() reads it (0 if none); restore_channel_ctr seeds it at boot so the
    // first post-boot next_ctr(_node_id) CONTINUES (no re-mint of an already-used channel_msg_id). Call after on_init
    // (when _active + _node_id are valid). Host-testable.
    uint16_t          channel_ctr() const { auto it = _active->_peer_send_counter.find(_node_id); return it != _active->_peer_send_counter.end() ? it->second : 0; }
    void              restore_channel_ctr(uint16_t v) { _active->_peer_send_counter[_node_id] = v; }
    // D7 (companion-contract): generalize the channel_ctr lease to a per-peer high-water FLOOR — the DM dedup identity
    // (sender_hash, ctr) must not collide after a reboot re-mints ctrs. peer_ctr_high() = the MAX ctr across ALL
    // _peer_send_counter entries (the self/channel counter is just one of them); the lease persists THIS + margin.
    // restore_peer_ctr_floor seeds the boot floor so every per-peer next_ctr resumes ABOVE the pre-reboot high-water.
    uint16_t          peer_ctr_high() const { uint16_t m = 0; for (const auto& kv : _active->_peer_send_counter) if (kv.second > m) m = kv.second; return m; }
    void              restore_peer_ctr_floor(uint16_t v) { _active->_peer_ctr_floor = v; }
    uint16_t          test_next_ctr(uint8_t dst) { return next_ctr(dst); }   // D7 test seam: drive the (floor-applied) per-peer counter
    // §6 DAD tiebreak (pure): higher claim_epoch wins; tie -> lower key_hash32 wins. Public for the convergence test.
    static bool       join_tiebreak_wins(uint8_t my_epoch, uint32_t my_key, uint8_t their_epoch, uint32_t their_key);
    // -> node_id, or -1 (skips expired); opt. out: the binding's confidence (soft/hard resolve).
    // §AB3: made `const` (it only reads _id_bind + writes *conf_out) so the const address-book view can reuse it
    // instead of forking a fourth _id_bind scan (U1). Pure widening — no call site changes, no behaviour changes.
    int               id_bind_find_by_hash(uint32_t key_hash32, IdBindConf* conf_out = nullptr) const;
    // E2E peer-pubkey cache (Phase 1 §6). Public for the seal/open paths + tests. hash-verified (ed_pub[:4]==hash),
    // authoritative-never-downgraded, evict-oldest at cap_peer_keys, TTL-aged. Per the ACTIVE layer.
    bool              peer_key_set(uint32_t key_hash32, const uint8_t ed_pub[32], PeerKeyConf conf, const char* name = nullptr, uint8_t name_len = 0);   // false: ed_pub[:4]!=hash. §1.3: name (if given) is REFRESHED on every call (mutable), the key never downgrades.
    uint8_t           peer_name_find(uint32_t key_hash32, char* out, uint8_t cap) const;   // §1.3: the cached name for a peer hash (0 = unknown/none); for `nameof`
    // ★ §AB2 (spec 2026-07-29 §2.3) — the write twin of peer_name_find, and THE ONE name writer: peer_key_set now
    // delegates its two name-copy sites here rather than keeping a third clamp-and-copy (U1).
    // false = no row holds this hash ⇒ the `peername` verb refuses loud (C2); it NEVER creates a keyless placeholder row
    // (spec §2.3), which would fight peer_key_set's ed_pub[:4]==hash invariant.
    // ★★ DELIBERATELY NOT ROUTED THROUGH peer_key_set, and this is the one design call in AB2 worth reading:
    //   (a) peer_key_set REQUIRES an ed_pub that hash-verifies, so a rename would have to read the key back out and
    //       hand it straight in — via peer_key_find, which AGES, so renaming a peer whose TTL had lapsed would refuse
    //       while `nameof` still answered with its name: two verbs disagreeing about one row (spec §2.5's defect class);
    //   (b) it refreshes last_seen_ms — a rename is not a sighting, and silently extending a key's TTL lease is a
    //       behaviour nobody asked for;
    //   (c) its `existing_pinned && conf != pinned` early return would have to be defeated by passing `pinned` back in,
    //       i.e. the console ASSERTING a confidence it never verified — exactly what AB1's "never silently promoted"
    //       ruling exists to stop.
    // ⇒ a user-initiated rename SUCCEEDS on a pinned peer (the key is IMMUTABLE, the name is MUTABLE — PeerKey's own
    // contract) and touches neither ed_pub nor confidence nor last_seen_ms.
    bool              peer_name_set(uint32_t key_hash32, const char* name, uint8_t name_len);
    bool              peer_key_find(uint32_t key_hash32, uint8_t ed_pub_out[32], PeerKeyConf* conf_out = nullptr);  // false: absent/aged
    bool              peer_confirmed(uint32_t key_hash32) const;   // §S2: have we OPENED a sealed frame from this peer? (no entry -> false -> INTRO attaches on first contact)
    // §remote-mgmt: node_id -> its learned key_hash32 (from the _id_bind beacon table), 0 if we've heard no beacon for it.
    // Lets the admin-issue path resolve a target id -> hash -> ed_pub (peer_key_find) to seal a command to it.
    // ✔ FIXED 2026-07-31 (§idbind-loop) — the loop below was wrong in TWO ways, both closed by its ONE bound. Kept on
    //    record because the shape is an easy one to reintroduce, and because the second defect is invisible by reading:
    //    (1) ★★ IT NEVER RETURNED ON A MISS. The counter was `uint8_t` against `protocol::cap_id_bind` (256), so
    //        `i < 256` was ALWAYS true — `i` wrapped 255->0 and `return 0` was unreachable. Being a `const`,
    //        side-effect-free function, that was UB, not merely a hang: elide/garbage/hang all legal, per -O level and
    //        target. ⇒ on a device, resolving an id we hold no binding for HUNG (watchdog reset) at BOTH call sites,
    //        both behind `unlock`: src/firmware_remote.cpp (`rcmd <unknown-id> <gated-verb>`) and src/fw_main.cpp (a
    //        sealed rcmd RESPONSE from a node whose beacon we never heard). ⓘ The proof it was never intended:
    //        firmware_remote.cpp's very next line is `if (!th) { … "unknown id (no beacon heard from it yet)" … }` —
    //        a miss-handler written for a function that could not report a miss. That branch is LIVE again.
    //    (2) it scanned the whole 256-slot ARRAY instead of the live `_id_bind_n` prefix, and the compacting removers
    //        (id_bind_age_out / id_bind_evict_other_hash_holders / node_join.cpp's prior-id drop) leave STALE COPIES
    //        in the tail, so it could hand back the hash of an EVICTED/AGED binding. `_id_bind_n` fixes this too.
    //    Bound + index copied from the sibling key_hash_of_id (node_hashlocate.cpp) — `uint16_t i < _id_bind_n`, and
    //    `_id_bind_n` is itself `uint16_t`, so the comparison is warning-clean (U1). Both directions are now pinned in
    //    test/test_node_hashlocate.cpp: the miss returns 0 (defect 1) and an id evicted by the rehome self-heal no
    //    longer resolves out of the tail (defect 2).
    // ⚠ STILL DIVERGENT FROM key_hash_of_id, deliberately and out of this slice's scope (C1): that sibling is also
    //   AUTHORITATIVE-gated and TTL-gated; this one is neither, so it can answer from a `claimed` or lapsed row. The
    //   residual is narrow (the hash only feeds peer_key_find, which ages independently, and id_bind_set maintains the
    //   id<->hash bijection) — but it is a real difference, not an oversight in the reading.
    uint32_t          key_hash_for_id(uint8_t id) const {
        if (!_active || id == 0) return 0;
        for (uint16_t i = 0; i < _active->_id_bind_n; ++i)
            if (_active->_id_bind[i].node_id == id && _active->_id_bind[i].key_hash32) return _active->_id_bind[i].key_hash32;
        return 0;
    }
    void              peer_key_age_out();                                                              // drop entries past peer_key_ttl_ms
    uint16_t          peer_key_count() const { return _active->_peer_keys_n; }

    // ==================== ★★★ §AB4 — RETAINED PEER LOCATION (spec 2026-07-29 §2.7) ====================
    // See the _peer_loc ring's own note (down with the Node-global members) for RAM-only, for why neither PeerKey nor
    // _team_keys is the home, and for the trust bound. These two are the whole public surface.
    //
    // ★ THE ONE SETTER, for BOTH sources (U1/U2 — one conversion path, as seed_blob_from_live is for the carriers).
    // The live caller is node_mac_rx.cpp's DELIVER path, inside the `if (loc_present)` block that ALREADY parses the
    // position, ALREADY distinguishes sealed from plaintext and ALREADY emits `peer_location` — only RETENTION was
    // missing, so nothing is re-extracted here (U1). Refreshes an existing hash in place; appends while there is room;
    // evicts the STALEST slot when full. Returns false only on key_hash32 == 0 (C2: 0 is the "no hash" sentinel that
    // peer_book_by_hash also refuses, never a queryable identity).
    // ⚠⚠ CALLER-ENFORCED, NOT ENFORCED HERE: only an AUTHENTICATED position may be stored. The receive site tests
    // `crypted_ok && sender_hash` and emits `peer_location_unauth` instead of calling this when it fails (owner ruling
    // O6). The check lives THERE because that is where the evidence is — `crypted_ok` is the seal result, and this
    // function cannot re-derive it from four scalars. ★ A plaintext DATA_FLAG_LOCATION is still parsed and still pushed
    // to the app exactly as before; it is only never RETAINED. Rationale: an unauthenticated position is spoofable by
    // anyone in range, and a spoofed position in an address book is WORSE than an absent one, because the UI presents it
    // as fact.
    // ★ TWO CALLERS NOW, one per source, and each does its OWN evidence test before calling — for the same reason:
    //   · node_mac_rx.cpp's DELIVER path -> PeerLocSrc::peer (tests `crypted_ok && sender_hash`);
    //   · node_channel.cpp's ingest_channel_m -> PeerLocSrc::team (§chan-crypt CL2b; the seal under the shared team
    //     content key IS the trust anchor per owner ruling O5, and the test there is ATTRIBUTION — §chan-crypt CL2c:
    //     the sender's full key_hash32 READ OFF THE SEALED INNER (bit2, required beside a position), non-zero, and
    //     cross-checked against the msg-id's 16 hash bits. It no longer resolves through `_team_keys`).
    bool              peer_loc_set(uint32_t key_hash32, int32_t lat_e7, int32_t lon_e7, PeerLocSrc src);
    // The read twin. `age_s` is derived from the stored second-stamp against `_hal.now()` — see PeerLoc::t_s for the
    // rollover reasoning and for why a backwards clock reports MAXIMALLY STALE rather than 0. false = no position held
    // for that hash (the NORMAL case, not an error), and the out-params are then untouched.
    bool              peer_loc_find(uint32_t key_hash32, int32_t& lat_e7, int32_t& lon_e7,
                                    uint32_t& age_s, PeerLocSrc& src) const;
    uint8_t           peer_loc_count() const { return _peer_loc_n; }   // live slots (test/diagnostic; mirrors peer_key_count)

    // ==================== ★★ §AB3 — THE GENERATED ADDRESS BOOK (spec 2026-07-29 §2.1) ====================
    // A JOIN on key_hash32 over the THREE tables that already exist — _peer_keys (name/key/conf/confirmed),
    // _id_bind (static node_id) and _team_keys/_team_peer (team_local_id). **GENERATED, never stored:** there is no
    // fourth table, so the view costs ZERO RAM and cannot go stale. A stored copy would need syncing on every
    // peer_key_set / id_bind_set / team_key_set and every eviction, which is precisely how the ledgers this arc spent
    // itself un-drifting drifted.
    // ★ WHY IT EXISTS AT ALL (spec §2.5, bench-proven 2026-07-30): `reqpubkey <id>` reads _team_keys and `hashof <id>`
    // read _id_bind, so `reqpubkey 228` cached 0x6C297145 and `hashof 228` still answered `unknown`. Each verb was
    // right about ITS table and neither answered the question. ⇒ every id/hash-shaped console query now reads THIS
    // view, so they cannot disagree again.
    // ⚠⚠ THE FORBIDDEN REPAIR, recorded so nobody re-takes it: do NOT make `hashof` work by writing the team hash into
    // _id_bind. That is the plane-blind ingest closed on 2026-07-31 (§id-bind-plane) — a team-scoped answer writing the
    // static map is an I2 breach. A team id belongs in _team_keys; the VIEW is what joins it to the hash for display.
    struct PeerBookRow {
        uint32_t    hash;               // 0 = an ID-ONLY row (we hold an id but no hash for it)
        char        name[protocol::peer_name_max];
        uint8_t     name_len;           // 0 = no name cached (normal — most rows have neither name nor key)
        uint8_t     static_id;          // 0 = none — the STATIC node_id plane (_id_bind)
        uint8_t     team_id;            // 0 = none — the TEAM local-id plane (_team_keys/_team_peer). §18: BOTH may be set.
        PeerKeyConf conf;               // meaningful ONLY when has_key; see console::peerkeyconf_name (the ONE spelling)
        // ★ has_key = we hold a USABLE pubkey (peer_key_find would succeed) ⇒ conf/peer_confirmed meaningful and a seal
        // is possible at conf >= authoritative. An AGED non-pinned row still contributes its NAME and its ids but
        // reports has_key=false + conf=overheard, so the app can never offer an encryption that would fail (§0.1).
        bool        has_key;
        bool        peer_confirmed;     // §S2: they hold OUR key (a sealed reply can come back)
        bool        static_authoritative;   // the _id_bind binding is AUTHORITATIVE (what key_hash_of_id vouches for); false = claimed/second-hand
        // ★★ §id-hash S3 (spec §3-D1): the TEAM plane's twin, and D6's "display a claim LABELLED as a claim" is
        // unimplementable without it — a renderer that cannot see the tier will print a claim as a fact. Meaningful
        // only when `team_id != 0` AND the row carries a hash (an id-only team row asserts no binding at all, exactly
        // like `static_authoritative` on the `_rt` id-only rows pass 2b emits).
        // ⓘ TRUE ON EVERY ROW S3 CAN PRODUCE, and that is not the §2.4 "tier with no producer" smell: the underlying
        //   `TeamKey::confidence` HAS a producer here (the heard beacon writes `authoritative`), so this field is a
        //   faithful readout of live state, not an enumerator nothing writes. S4a's on-air ingest makes it false.
        bool        team_authoritative;
        uint8_t     team_alias_dropped; // ⚠ >0: that many OTHER _team_keys rows carry this hash and LOST the freshest-wins
                                        // race. The emit MUST say so (spec §2.1) — never silently pick a winner.
        // ★★ §AB4 — the joined _peer_loc row (spec §2.7). ADDITIVE: every pre-AB4 caller compiles and reads unchanged.
        // ⚠ ABSENCE IS NORMAL, NOT AN ERROR — most rows will never carry a position, so a renderer must handle
        // has_location == false as the ordinary case (the contract says so too).
        bool        has_location;       // false ⇒ lat_e7/lon_e7/loc_age_s/loc_src are ALL meaningless, do not render them
        int32_t     lat_e7;
        int32_t     lon_e7;
        // ★★ MANDATORY ALONGSIDE THE POSITION, never optional: a position shown without its age gets read as CURRENT,
        // which is exactly the misleading-stale-fix failure the RAM-only ruling exists to prevent.
        uint32_t    loc_age_s;
        PeerLocSrc  loc_src;            // the TRUST ANCHOR — pairwise (`peer`) vs group (`team`). See PeerLocSrc.
    };
    using PeerBookVisit = void (*)(const PeerBookRow& row, void* ctx);   // a plain fn-ptr: lib/core carries no std::function
    // Walk the book, ONE row on the stack at a time (never an array — cardinality is driven by _id_bind's 256, and a
    // 256-row buffer is ~12 KB on the loop-task stack: the do_post_ack overflow lesson).
    // include_id_rows=false ⇒ ★ the §2.6(a) BOUND: only rows backed by _peer_keys (≤ cap_peer_keys = 16), enriched with
    //   static_id/team_id. That is the JSON address book, bounded BY CONSTRUCTION rather than by a paging protocol.
    // include_id_rows=true  ⇒ the full up-to-256 diagnostic list (`peers all`, TEXT console only). Emitting that over
    //   BLE walks straight back into the self-inflicted-console-flood WEDGE this project already fixed once (mrcon).
    // Returns the row count. fn may be nullptr (count only).
    uint16_t          peer_book_walk(bool include_id_rows, PeerBookVisit fn, void* ctx) const;
    // The hash-shaped query: the joined row for one hash. false = no table holds it. (`nameof` reads this.)
    bool              peer_book_by_hash(uint32_t key_hash32, PeerBookRow& out) const;
    // ★ The ID-shaped query — spec §2.5's actual fix. Searches BOTH namespaces and reports WHICH matched; when one
    // number matches in both (the §18 dual-identity space) it fills BOTH rows so nothing is silently picked.
    // Returns a bitmask: bit0 = `static_out` valid, bit1 = `team_out` valid; 0 = the id is unknown in both planes.
    // ⚠ It resolves through key_hash_of_id and team_key_of_id — the SAME two functions the send path and
    // `reqpubkey <team-id>` (node.cpp CmdKind::reqpubkey) already use, not a fourth hand-rolled scan (U1).
    uint8_t           peer_book_by_id(uint8_t id, PeerBookRow& static_out, PeerBookRow& team_out) const;
    static constexpr uint8_t kPeerBookStatic = 0x1, kPeerBookTeam = 0x2;

    // E2E seal/open (Phase 1 §4/§5). Public for the send/receive paths + tests. SAME-LAYER DMs only in v1
    // (cross-layer CRYPTED out of scope). Recipient/sender pubkey resolved from the peer-key cache; ECDH+KDF+nonce
    // via _x_secret. e2e_seal_inner builds [dst_hash 4][ciphertext][tag 16] + the 8-B nonce-seed (§1c: pt = origin‖…); returns
    // inner_len, or 0 with `outcome` set to WHY (no_identity / no_pubkey / too_large / bad_rng / cross_layer) so the
    // caller FAILS LOUD distinctly, never cleartext. Only no_pubkey warrants a WANT_PUBKEY flood.
    size_t e2e_seal_inner(uint8_t* inner, size_t cap, uint8_t seed8[8], uint8_t flags, uint32_t dst_key_hash32,
                          uint8_t origin, uint16_t ctr, uint32_t source_hash, int32_t lat_e7, int32_t lon_e7,
                          const uint8_t* body, uint8_t body_len, SealOutcome& outcome);
    // e2e_open_inner: open under ONE candidate `sender_hash`; VERIFY the sealed source_hash == sender_hash. false = no
    // key / tag fail. `origin_out` = the DM's origin (1a: read from the cleartext AAD; 1c: recovered from the seal).
    bool   e2e_open_inner(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t sender_hash, uint32_t& origin_out, uint32_t& source_hash_out, bool& has_location_out,
                          int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out);
    // §1a sealed-sender: TRIAL DECRYPTION — try each AUTHORITATIVE/PINNED cached peer key; the Poly1305 tag is the
    // oracle. First verifying key → decrypt + that key's owner IS the sender (sender_hash_out) + recover origin. false =
    // NO cached key opens it (caller DROPS silently — option a: no push/ack/inbox). No cleartext sender hint on the wire.
    bool   e2e_open_trial(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags, uint16_t ctr,
                          uint32_t& sender_hash_out, uint32_t& origin_out, uint32_t& source_hash_out,
                          bool& has_location_out, int32_t& lat_out, int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out);
    // §S4 SEALED_RELAY (DATA_TYPE_SEALED_RELAY): build the relay BODY [seal_ctr 2 LE][seed8 8][ct‖tag] — seal `body`
    // to `target_hash` under OUR identity (source_hash = _key_hash32), SAME-LAYER-shaped (aad = target_hash, nonce =
    // dm_nonce(seed8, seal_ctr, target_hash)); seal_ctr = ++_relay_seal_ctr (CARRIED, not the frame ctr, so a
    // delegating home re-originates under its OWN frame ctr for MAC dedup while the recipient still derives the nonce
    // WE used). Returns the body length, or 0 with `outcome` set (no_identity/no_pubkey/too_large) -> caller FAILS LOUD.
    uint8_t build_sealed_relay_body(uint32_t target_hash, const uint8_t* body, uint8_t body_len,
                                    uint8_t* out, uint8_t out_cap, SealOutcome& outcome);
    // §S4: open a SEALED_RELAY body whose CLEAR sender is `source_hash` (directed, no trial). Verifies the SEALED
    // source_hash == source_hash (anti-spoof) and IGNORES the sealed origin byte (§1c layer-local garbage). false = no
    // key / tag fail / short / spoof -> caller DROPS (never delivers ciphertext).
    bool   e2e_open_relay(const uint8_t* relay_body, size_t len, uint32_t source_hash,
                          uint8_t* body_out, uint8_t& body_len_out);
    // ★ §hashbind-plane: `team_plane` is REQUIRED, not defaulted (C2) — it is the §18 plane split, and a caller that
    // forgets it must fail to compile rather than silently re-open the I2 breach. true => the answer rode the TEAM
    // plane, so its node_id is a TEAM LOCAL id and the static _id_bind write is SKIPPED (PostAck::team_plane).
    void              on_hash_bind_response(const uint8_t* inner, uint8_t inner_len, bool authoritative, bool team_plane);   // C.1: the origin consumed an H_ANSWER DATA -> cache (h_query) + drain. authoritative from the frame TYPE. public = the deliver seam + test driver
    void              on_hash_bind_snoop(const uint8_t* inner, uint8_t inner_len, bool authoritative, bool team_plane);      // C.2: a forwarder snooped an H_ANSWER in transit -> cache-on-pass (h_relay). authoritative from the frame TYPE. public = the relay seam + test driver
    void              on_hash_bind_pubkey(const uint8_t* inner, uint8_t inner_len);   // E2E §6: a DATA TYPE 5 (delivered OR relayed-through) -> cache the ed_pub authoritative (verify ed_pub[:4]==hash)
    bool              channel_entry_dirty(uint32_t id) const { const int i = channel_buffer_find(id); return i >= 0 && _active->_channel_buffer[i].dirty; }
    bool              channel_payload_eq(uint32_t id, const uint8_t* p, uint16_t len) const {
        const int i = channel_buffer_find(id);
        if (i < 0 || _active->_channel_buffer[i].payload_len != len) return false;
        for (uint16_t k = 0; k < len; ++k) if (_active->_channel_buffer[i].payload[k] != p[k]) return false;
        return true;
    }
    static uint32_t   channel_msg_id_mint(uint8_t origin, uint32_t key_hash32, uint8_t ctr);   // origin<<24|(kh&0xffff)<<8|ctr (dv:2239)
    void    ingest_channel_m(const m_out& m, uint8_t from);  // M-frame merge (dv:10942); public for tests
    // Origin-level DATA dedup (loop/retransmit detection): record (origin,dst,ctr)->expiry + the prev-hop.
    // Prunes expired, then ROLLS (evicts the oldest = min-expiry) at the 256 cap instead of refusing. Public for tests.
    void    record_seen_origin(uint64_t sokey, uint8_t from, uint64_t now_ms);
    size_t  seen_origin_count() const { return _active->_seen_origins.size(); }
    bool    seen_origin_live(uint64_t sokey, uint64_t now_ms) const {
        auto it = _active->_seen_origins.find(sokey); return it != _active->_seen_origins.end() && it->second > now_ms; }

    // ---- Peer-liveness + freshness plane (routing-liveness port). Public for tests + the hooks. PHASE 1 = STATE
    // ONLY: tracked + emitted, NOT yet applied to scoring/selection/cascade (that is Phase 2/3). ----
    void    record_peer_rts_timeout(uint8_t node_id, uint8_t ctr_lo, bool team_plane = false);   // a same-hop RTS/ACK giveup -> count + tier (suspect@1/silent@3/dead@6). §2c: team_plane -> _team_liveness
    void    clear_peer_suspect(uint8_t node_id, const char* source, bool team_plane = false);   // a frame heard FROM node_id -> alive -> clear its tiers. §2c: team_plane -> _team_liveness (recovery-on-heard)
    void    mark_dest_seen(uint8_t node_id);                           // stamp last-seen-as-transmitter (freshness input)
    uint8_t peer_suspect_level(uint8_t node_id);                       // 0 healthy / 1 suspect / 2 silent / 3 dead (clears expired tiers lazily)
    bool    is_next_hop_fresh(uint8_t node_id) const;                  // now - dest_seen <= next_hop_live_ttl_ms (self = always fresh); DEFINED, not consulted in P1

private:
    // Node-owned timer-id namespace (Hal::after re-arm-by-id, cap 64). Reserve
    // 4+ for the R3 RTS/CTS/ACK timers.
    static constexpr uint32_t kBeaconTimerId           = 1;
    static constexpr uint32_t kAgingTimerId            = 2;
    static constexpr uint32_t kTriggeredBeaconTimerId  = 3;
    // R3 data-plane (MAC) timers — single-flight per node, so one live instance each.
    static constexpr uint32_t kRtsTimeoutTimerId       = 4;   // sender: CTS-wait
    static constexpr uint32_t kAckTimeoutTimerId       = 5;   // sender: ACK-wait
    static constexpr uint32_t kPendingRxExpiryTimerId  = 6;   // receiver: DATA-wait
    static constexpr uint32_t kCtsToDataGapTimerId     = 7;   // sender: CTS-rx -> DATA-tx gap
    static constexpr uint32_t kQueueWakeupTimerId      = 8;   // become_free: not-ready re-arm
    static constexpr uint32_t kPostAckTimerId          = 9;   // receiver: ACK-air -> deliver/forward
    static constexpr uint32_t kRetryBackoffTimerId     = 10;  // sender: jittered RTS retry
    // Cascade-to-alt / no-route defer plane.
    static constexpr uint32_t kDeferredDrainTimerId    = 11;  // periodic 1s drain of _deferred (TTL giveup)
    static constexpr uint32_t kCascadeRequeueTimerId   = 12;  // backoff before re-draining a requeued flight
    static constexpr uint32_t kNackWaitTimerId         = 13;  // NACK BUSY_RX wait-same-hop one-shot
    static constexpr uint32_t kReqSyncTimerId          = 14;  // REQ_SYNC boot loop: re-arm every req_sync_retry_ms while discovery+route-starved (dv:9167)
    static constexpr uint32_t kBeaconJitterTimerId     = 27;  // R4.3 silence-jitter deferred periodic beacon; BASE of a 4-slot ring [27..30] (cleanup #D: two defers in one jitter window must BOTH fire, dv per-closure)
    static constexpr uint8_t  kBeaconJitterSlots       = 4;
    static constexpr uint32_t kRtsDutyDeferTimerId     = 31;  // cleanup #A redo: over-budget RTS duty-defer re-check/hand
    static constexpr uint32_t kLbtDeferTimerId         = 15;  // R4.5 LBT deferred-TX re-fire; BASE of a 4-slot range [15..18]
    static constexpr uint32_t kRadioBusyRetryTimerId   = 19;  // R4.5b on_radio_busy stash re-issue; BASE of a 4-slot range [19..22]
    static constexpr uint32_t kDutyDeferTimerId        = 23;  // tx_with_retry duty-cycle pre-check defer; BASE of a 4-slot range [23..26]
    static constexpr uint32_t kSyncResponseTimerId     = 32;  // REQ_SYNC jittered response fire; BASE of a kSyncRespSlots ring [32..47] (one slot per pending requester)
    static constexpr uint8_t  kSyncRespSlots           = protocol::cap_sync_response_pending;
    static constexpr uint32_t kChannelPullTimerId      = 48;  // channel CHANNEL_PULL jittered fire; BASE of a kChannelPullSlots ring [48..55]
    static constexpr uint8_t  kChannelPullSlots        = protocol::cap_channel_pull_pending;
    static constexpr uint32_t kMBcastClearTimerId      = 56;  // M-broadcast fire-and-forget: clear pending_tx after the DATA-M airtime (no ACK)
    static constexpr uint32_t kOverhearRetuneTimerId   = 57;  // overhear ARM: retune RX back to routing_sf after the DATA-M window
    static constexpr uint32_t kJoinClaimGuardTimerId   = 58;  // node_id DAD: claim guard window -> adopt-or-deny
    static constexpr uint32_t kJoinRetryTimerId        = 59;  // node_id DAD: jittered re-claim after a lost claim/heal
    static constexpr uint32_t kJoinListenTimerId       = 60;  // node_id DAD: listen window before the FIRST claim (L1: hear the leaf, then pick)
    static constexpr uint32_t kFloodRebcastTimerId     = 61;  // channel flood rebroadcast fire; BASE of a ring [61..63] (slot = id - base); LAST of the dense 1..63 block
    // ---- Slice 3 dual-layer gateway scheduler band [64..79] (kCap raised 64->80 in 3b). PER-LAYER timers: slot = layer
    // index (0..1). Inert on a single-layer node (n_layers==1 never arms them). The window open/close + per-leaf beacon
    // are the only PERSISTENT per-layer timers; the MAC exchange timers (RTS/ACK/retry rings) are active-layer-shared.
    static constexpr uint32_t kLayerWindowTimerId      = 64;  // per-layer window-OPEN / leaf-switch fire; BASE of [64..65]
    static constexpr uint32_t kLayerWindowCloseTimerId = 66;  // per-layer window-CLOSE / return fire;      BASE of [66..67]
    static constexpr uint32_t kLayerBeaconTimerId      = 68;  // per-leaf beacon cadence (gateway);         BASE of [68..69]
    // The gateway band [64..69] and the channel re-offer ring [70..73] are ROLE-EXCLUSIVE: a gateway (n_layers==2)
    // is out of the channel provider plane (§7 — never originates a flood, so never arms re-offer), and a normal node
    // (n_layers==1) never arms the gateway timers. So they coexist within kCap=80 with no overlap; the after() id<kCap
    // bound stays intact (the canary era: the wheel is exonerated only because it bounds).
    static constexpr uint32_t kChannelReofferTimerId   = 70;  // channel ORIGIN re-offer jittered fire; BASE of a ring [70..73] (slot = id - base)
    static constexpr uint8_t  kChannelReofferSlots     = protocol::cap_channel_reoffer_pending;
    static constexpr uint32_t kMobileDiscoverTimerId   = 74;  // §mobile 2b: registration FSM — DISCOVER kick / periodic re-CLAIM
    static constexpr uint32_t kMobileClaimGuardTimerId = 75;  // §mobile 2b: collect-OFFERs window close -> pick strongest + CLAIM
    static constexpr uint32_t kMobileLayerQueryTimerId = 76;  // §mobile 5a: pull the layer directory from a gateway (periodic while registered)
    static constexpr uint32_t kTeamDadGuardTimerId     = 77;  // §mobile 6.4: team-DAD claim guard window close -> confirm _team_local_id
    static constexpr uint32_t kPresenceProbeTimerId    = 78;  // §S6: mobile presence check period T (dynamic) + probe retry re-arm
    static constexpr uint32_t kPresenceRosterTimerId   = 79;  // §S6: home roster-coalesce window close -> emit ONE roster
    static constexpr uint32_t kMobileOfferBackoffTimerId = 80;// §S6/QA-3b: host OFFER de-storm — jitter the OFFER so two hosts don't answer a DISCOVER at the SAME ms (the same-ms collision that made a mobile adopt the WEAK home)
    static constexpr uint32_t kHForwardTimerId         = 81;  // §F-XL-1: jittered h_forward de-storm — BASE of a kHForwardSlots ring [81..84] (slot = id - base)
    static constexpr uint32_t kRreqForwardTimerId      = 85;  // §F-XL-2: jittered rreq_forward de-storm — BASE of a kRreqForwardSlots ring [85..88] (slot = id - base)
    static constexpr uint32_t kParkRefloodTimerId      = 89;  // §F-SL-1: parked-send H re-flood scan (single one-shot, re-armed to the earliest pending re-flood)
    static constexpr uint32_t kE2eAckDeadlineTimerId   = 90;  // shelf item (i): E2E-ack deadline scan (single one-shot, re-armed to the earliest pending -a send's deadline — park_reflood idiom)
    // [78..80] = the presence plane + OFFER de-storm; [81..84] = the h_forward de-storm ring; [85..88] = the rreq_forward de-storm ring; [89] = the parked-send re-flood scan; [90] = the E2E-ack deadline scan; the timer wheel cap is 91 (kCap in timer_wheel.h).

    // ---- beacon emit / ingest ----------------------------------------------
    void emit_beacon(const char* kind);                            // "periodic" | "triggered"
    void periodic_beacon_fire();                                   // R4.3 throttle body (dv:7695-7851)
    void deferred_beacon_jitter_fire(uint8_t slot);                // R4.3 post-silence-jitter re-check (dv:7801-7849); #D ring slot
    bool _beacon_jitter_pending[kBeaconJitterSlots] = {};          // #D: which ring slots have a deferred periodic beacon armed
    bool beacon_max_idle_force(uint64_t now, bool emit_events);    // R4.3 max-idle B+C override (dv:7734-7784)
    void ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta);
    uint16_t cfg_config_hash() const;                             // R6.1: leaf_config_hash over THIS node's active cfg (u16)
    // R6.1 §6.4 join-participation gate: a node may originate F/DATA only once its leaf config is "synced" — UNMANAGED
    // (lineage 0, backward-compat, always allowed) OR managed-and-adopted (config_epoch > 0; leaf-create/CONFIG_PULL set it).
    // An un-synced managed joiner must listen + CONFIG_PULL only (no F/DATA pollution before it's a member).
    bool     leaf_config_synced() const { return _cfg.lineage_id == 0 || _cfg.config_epoch > 0; }
    int16_t route_score_from_snr(int16_t snr_q4) const;            // dv_dual_sf.lua:3053
    static void emit_rt_update(Hal& hal, uint8_t dest, uint8_t next, int16_t score_q4, uint8_t hops, const char* slot);   // rt_update telemetry (dest/next/score/hops/slot) — shared by the beacon merge sites + the hop_budget-NACK bump (node_mac_rx)
    // Direct (hops=1) neighbour learning from a received frame's immediate sender — the C++
    // learn_rx_source / learn_direct_from_frame. Returns true on a real change (new/promote/
    // refresh) so the caller can fire the triggered beacon. sender must be a real id (0..254);
    // 0xFF (unknown/reserved) and self are no-ops. C++ has no id-bind/dest-seen/liveness plane,
    // so (unlike the Lua) those sub-actions are absent.
    bool    learn_direct_neighbor(uint8_t sender, int16_t snr_q4, bool is_gw, bool team_plane = false);   // §6.2: team_plane -> learn into _rt_team
    void    learn_route_via(uint8_t dest, uint8_t via, uint8_t hops, int16_t snr_q4, bool team_plane = false);  // multi-hop install (F path); §team-multihop: team_plane -> _rt_team + _team_peer bit
    // F route discovery (AODV RREQ/RREP) — node_route_discovery.cpp. §team-multihop (spec 2026-07-15 Plane 2): team_plane forks
    // the whole family onto the TEAM plane (team_scoped F, origin/dst = team_local_id, _rt_team, team-private _rreq state).
    void    handle_f(const uint8_t* bytes, size_t len, const RxMeta& meta);
    void    handle_f_common(const struct f_out& f, const RxMeta& meta, bool team, uint8_t me);   // §P2-2: shared F RREQ/RREP body; static/team entry wrappers apply the plane-only gates then hand off
    void    emit_route_request(uint8_t dst, uint8_t ttl, bool team_plane = false);
    void    send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst, bool team_plane = false);
    bool    rreq_seen_recently(uint8_t origin, uint8_t dst, bool team_plane = false);
    void    mark_rreq_seen(uint8_t origin, uint8_t dst, bool team_plane = false);
    bool    rreq_rate_ok(uint8_t dst, uint8_t ttl, bool team_plane = false);
    void    age_out_rreq_last();                                  // periodic (kAgingTimerId): drop spent rate-limit entries — BOTH planes
#if MR_FEAT_TEAM
    void    handle_f_team(const struct f_out& f, const RxMeta& meta);   // §team-multihop: same-team-only F handler (gated on team_id, on _rt_team) — full static/other-team separation
#endif
    // Hash-locate (H) plane — node_hashlocate.cpp. id_bind = the key_hash32->node_id binding table, the
    // substrate the H resolver answers from (Lua dv:4677+). Populated by beacons (every BCN carries the
    // sender's key_hash32) + self + hash-bind responses.
    bool    id_bind_set(uint8_t node_id, uint32_t key_hash32, IdBindSource source, IdBindConf confidence); // insert/update; dedup-by-hash; authoritative overwrites a conflict, claimed refuses
    uint8_t id_bind_evict_other_hash_holders(uint32_t key_hash32, uint8_t keep_node_id);   // rejoin self-heal: one hash -> one node_id
    // ★ §id-hash S2b-fix (QA P1a): the CONFIDENCE guard on that self-heal — the node_id (>= 0) that holds this hash
    // AUTHORITATIVELY and is not `except_node_id`, else -1. Gates are key_hash_of_id's verbatim (U1).
    int     id_bind_auth_holder_other(uint32_t key_hash32, uint8_t except_node_id) const;
    void    id_bind_age_out();                                    // drop expired (TTL); emit id_bind_aged
    // ★ §AB3 view internals (node_hashlocate.cpp). peer_book_fill_from_peer_key does the reverse (hash -> id) JOINS.
    int     peer_key_slot_of(uint32_t key_hash32) const;                         // _peer_keys index, or -1 (NOT age-gated)
    void    peer_book_fill_from_peer_key(uint16_t slot, PeerBookRow& r) const;   // _peer_keys[slot] + both reverse id joins
    void    peer_book_join_ids(PeerBookRow& r) const;                            // hash -> static_id (+conf) and hash -> team_id (+alias count) + the §AB4 location
    // ★ §AB4: hash -> _peer_loc. ONE implementation, called from peer_book_join_ids (which covers the ENTIRE app-facing
    // surface — the bounded JSON book, `nameof` and `hashof` all reach the view through it) and additionally from
    // peer_book_walk's unkeyed passes (2)/(3), which resolve their ids directly and so never call join_ids. Those two
    // extra call sites are what makes `peers all` complete and what stops CL2 — whose channel-sourced positions land on
    // hashes with NO _peer_keys row by design (O5) — having to hunt for a missed join.
    void    peer_book_join_loc(PeerBookRow& r) const;
#if MR_FEAT_TEAM
    // ⚠ THE AMBIGUOUS REVERSE LOOKUP, and it is real on THIS table only. _id_bind maintains one-hash-one-id
    // (id_bind_set calls id_bind_evict_other_hash_holders on BOTH its accept paths) so hash -> static_id cannot alias.
    // _team_keys has NO such dedup — team_key_set upserts BY ID only — so a teammate that re-ran team-DAD leaves its
    // OLD (id, hash) row live until the 48 h TTL or the LRU reclaims it, and TWO team ids then carry one hash.
    // ⇒ resolve by FRESHEST last_seen_ms and report how many rows lost, so the emit can say a loser was dropped.
    // Gate is team_key_of_id's VERBATIM gate (team_id != 0 && is_team_peer(id) && within id_bind_ttl_ms) so the forward
    // and reverse directions cannot disagree — the whole point of the view.
    // ★★ §id-hash S3: ONE DELIBERATE DIVERGENCE FROM THAT "VERBATIM" CLAIM, and it is the spec's §3-D6 display floor.
    // team_key_of_id now takes a confidence floor defaulting to `authoritative`; this reader takes NONE and accepts
    // every tier, because its only callers are the address-book VIEW (peer_book_join_ids + walk passes 2/3), whose
    // floor D6 sets at `claimed` — "shows a claim, LABELLED as one". It reports the winner's tier through `conf_out`
    // so the row can carry that label. A floor here would silently hide claims from the one surface meant to show
    // them; a hidden claim is how an operator concludes "nothing is there" about a binding that exists.
    // ⚠ It is PRIVATE and must stay so: it is the display reader, never a send-path one.
    uint8_t team_id_of_key_freshest(uint32_t key_hash32, uint8_t& alias_dropped, IdBindConf* conf_out = nullptr) const;
#else
    uint8_t team_id_of_key_freshest(uint32_t, uint8_t& alias_dropped, IdBindConf* conf_out = nullptr) const { alias_dropped = 0; if (conf_out) *conf_out = IdBindConf::claimed; return 0; }
#endif
    void    handle_h(const uint8_t* bytes, size_t len, const RxMeta& meta);   // H flood: resolve (own-hash OR id_bind) + suppress, else forward TTL-1
    void    h_forward_fire(uint8_t slot);                                     // §F-XL-1: fire the jittered (de-stormed) h_forward stashed in ring slot
    void    rreq_forward_stash(const uint8_t* buf, size_t n);                 // §F-XL-2: stash a built RREQ-forward frame + arm a jittered fire (shared by static + team relays)
    void    rreq_forward_fire(uint8_t slot);                                  // §F-XL-2: fire the jittered (de-stormed) rreq_forward stashed in ring slot
    bool    hash_query_seen_recently(uint8_t origin, uint32_t query_key32, bool hard, bool want_pubkey, bool team_scoped, bool by_id);   // per-(PLANE,origin,KEY,VARIANT) dedup; VARIANT = hard + want_pubkey (§2: a WANT_PUBKEY isn't suppressed by a prior plain HARD); §T6/B: team_scoped = the plane; §S4a: by_id = the KEY SPACE
    void    mark_hash_query_seen(uint8_t origin, uint32_t query_key32, bool hard, bool want_pubkey, bool team_scoped, bool by_id);
    // §id-hash S4a / spec §3-D4: the last bool is `binding_verifiable`, NOT "we answered". It selects the plain vs
    // AUTHORITATIVE answer TYPE, i.e. whether the id->hash assertion in this frame is one the receiver can check.
    // An owner's BY_ID answer passes FALSE: it owns the key, but an id is an address, not a commitment.
    void    send_hash_bind_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, uint32_t key_hash32, bool binding_verifiable, bool mobile_proxy = false, uint8_t epoch = 0, bool team_scoped = false); // B: routed DATA(H_ANSWER inner) home; §mobile 4a: mobile_proxy -> MOBILE_H_ANSWER TYPE + epoch; §F-TR-2: team_scoped -> route the answer on the TEAM plane (_rt_team + team RREQ), not AUTO (which falls to the static plane when the origin isn't yet a known team peer)
    void    send_hash_bind_pubkey_response(uint8_t to_origin, uint8_t target_layer, uint8_t node_id, const uint8_t ed_pub[32], uint32_t dst_hash = 0, bool team_scoped = false);  // E2E §6: routed DATA TYPE 5 (the owner's ed_pub). Wave 2: dst_hash!=0 (mobile requester) -> DST_HASH so the home last-miles it; §F-TR-2: team_scoped -> TEAM plane
    const uint8_t* host_mobile_ed_pub(uint32_t key_hash32) const;  // §mobile Part 2 Fix 7: the cached ed_pub for a hosted mobile (live direct proxy + has_pubkey), else nullptr
    void    send_mobile_pubkey_answer(uint8_t to_origin, uint8_t target_layer, uint8_t home_id, uint32_t key_hash32, uint8_t epoch, const uint8_t ed_pub[32]);  // §mobile Part 2 Fix 7: DATA TYPE 13 (home routing ‖ the mobile's ed_pub)
    uint32_t cache_want_pubkey_requester(const h_out& h);          // §S3 part2/3: validate + cache a WANT_PUBKEY H's appended requester key (self-consistency + non-zero + the mobile/team id_bind gate), fire peer_key_cached. Returns the requester hash (0 = rejected). Used by the home proxy-answer branch + the mobile TX-free overhear cache.
    void    forward_requester_key_to_mobile(uint32_t mobile_hash, const uint8_t requester_ed_pub[32], const char* name, uint8_t name_len);   // §S3 part2: 1-hop last-mile DATA_TYPE_MOBILE_KEY_FORWARD to a hosted mobile (dedup same-requester via _mobile_reg[].last_key_fwd_hash32)
    // D — send-by-hash trigger (the deferred "address by key_hash32") + verify-on-use.
    uint16_t send_by_hash(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt = CryptIntent::def, uint32_t reply_to_hash = 0, uint16_t mobile_ctr = 0, Plane plane = Plane::AUTO, uint8_t type = 0, bool suppress_intro = false); // authoritative binding -> send now; soft/unknown -> park + flood (soft binding -> HARD verify). §mobile: reply_to_hash!=0 = the HOME re-originating for its mobile (stamps SOURCE_HASH=mobile hash); reply_to_hash==0 + is_mobile+registered = the mobile ITSELF -> delegate to its home (DATA_TYPE_MOBILE_SEND). mobile_ctr = the mobile's original ctr (ctr_M) -> the ctr_H->ctr_M reverse-ack map (0 = not delegated). §S2: type=0 lets INTRO auto-attach at origination (reply_to_hash==0); type=DATA_TYPE_INTRO = the HOME re-originating an already-prefixed delegated INTRO (no re-attach, threaded to the wire TYPE)
    // §S2: decide + build the INTRO first-contact prefix [ed_pub 32][name_len 1][name] for a plaintext hash-addressed send to dst_hash. Returns the prefix length (33 + name_len), or 0 = no attach (send plain: no identity / sealed intent / already peer_confirmed / cfg off / would overflow the DM body cap — message delivery beats key bootstrap).
    uint8_t  intro_attach_prefix(uint32_t dst_hash, CryptIntent crypt, uint8_t body_len, uint8_t* pfx, uint8_t pfx_cap);
    // §id-hash S4a: `by_id` flips the query KEY SPACE — `query_key32` is then a node/team id ("who owns id N?"),
    // canonical per frame_codec.h. The default keeps the three best-effort callers byte-identical (C1).
    HQueryOutcome emit_hash_query(uint32_t query_key32, bool hard, bool want_pubkey = false, Plane plane = Plane::AUTO, bool by_id = false);   // H flood for query_key32 (hard = verify-on-use; want_pubkey = E2E §6, ask the owner's ed_pub). Wave 2: TEAM => team_scoped + origin=team_local_id (answer routes via _rt_team); GLOBAL => not team-scoped
    void    park_send(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt = CryptIntent::def, uint32_t reply_to_hash = 0, uint16_t mobile_ctr = 0, uint8_t type = 0, bool reflood = false, bool reflood_hard = false, Plane reflood_plane = Plane::AUTO);   // M3: crypt stamped at park so a parked CRYPTED send flies sealed on drain. §mobile: reply_to_hash carried so a parked delegated send keeps the mobile's reply address; mobile_ctr -> the ctr_H->ctr_M reverse-ack map on drain. §S2: type (DATA_TYPE_INTRO) preserved so a parked INTRO re-originates with its TYPE + prefix intact on drain. §F-SL-1: reflood => re-emit the H (hard/plane preserved) on a jittered bounded retry while parked
    void    park_reflood_arm();                                  // §F-SL-1: (re)arm kParkRefloodTimerId to the earliest pending re-flood (cancel if none)
    void    park_reflood_fire();                                 // §F-SL-1: scan parked sends -> re-emit the H for those due (bounded); re-arm
    void    park_send_layer(uint32_t key_hash32, const uint8_t* body, uint8_t body_len, uint8_t flags);   // Slice 4d: a cross-layer-capable park (resolves layer + gateway on the H-answer); flags carry the app's E2E_ACK_REQ etc.
    void    drain_parked_sends(uint32_t key_hash32, uint8_t resolved_id, uint8_t target_layer = 0xFF);   // a binding arrived -> fly the parked DMs to it (target_layer from the H-answer, 0xFF = beacon re-drain / unknown)
    // Slice 4d: cross-layer origination — select a bridging gateway (schedule-verified) + build the CROSS_LAYER DM.
    uint8_t select_gateway_for_leaf(uint8_t target_leaf);        // a gateway (1-hop schedule OR multi-hop _bridged_layers) bridging to target_leaf; 0 = none. Two-pass: routed-preferred, then unrouted fallback (non-const: prunes aged rows)
    // ★ §xl-crypt-intent (2026-07-29): `crypt` is MANDATORY (no default) BY DESIGN — this signature used to end at
    // `type`, so both call sites structurally DISCARDED the per-message crypt intent and a `-e` DM to a mobile whose
    // home sits on another layer went out IN THE CLEAR with no refusal (C2's exact failure class). Making the parameter
    // required means a future caller cannot re-open the hole by omission: it must state on/off/def and the seal-or-refuse
    // decision below is then unavoidable. Param ORDER mirrors do_send (…, flags, crypt, …, type) per U3.
    // ★★ §xl-deleg-ack (BUG FIX 2026-07-30): `override_source_hash` is likewise MANDATORY (no default) and the return
    // type went `void` -> the DM ctr, for the SAME reason applied to a delivery guarantee instead of a confidentiality
    // one. This signature used to end at `type`, so the mobile_home_find arm structurally dropped BOTH halves of the
    // delegation contract its same-layer sibling honours — the mobile's SOURCE_HASH and the ctr_H->ctr_M map entry —
    // and a home re-originating for its hosted mobile toward a target homed on a THIRD layer aired SOURCE_HASH = its
    // OWN key. Consequence on a plaintext DM: the reversed 4e ack addresses the home, is consumed there, and the
    // mobile never sees it. On a DELEGATED SEALED DM it is worse — the mobile sealed under its own hash, so the
    // recipient's directed open runs the ECDH against the WRONG identity, the Poly1305 tag fails, and the DM is
    // dropped with no trace on metal (MR telemetry is sim-only). 0 = nothing flew (next_ctr never mints 0), so a
    // caller can guard its deleg_ack_put on the return exactly as the do_send sites do.
    uint16_t send_cross_layer(uint8_t dst_node, uint32_t dst_hash, uint8_t target_layer, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt, uint8_t type, uint32_t override_source_hash);  // pick G + enqueue, else err_no_gateway (4d.2: park+ROUTE_QUERY); flags honored on the DM. §S2: type (e.g. INTRO) threads to the cross-layer DM's TYPE. want_crypt => the body is SEALED into a DATA_TYPE_SEALED_RELAY (never a cleartext downgrade); a seal failure or an already-TYPED send fails LOUD. §GapB: override_source_hash (!=0) = the delegating MOBILE's hash -> the inner SOURCE_HASH. Returns the DM ctr, 0 = not sent
    // Explicit-path origination (console/companion send_layer, §5): route a cross-layer DM along the user-supplied
    // layer path [our_layer, hops...] cur=1, NO H-query. Returns SYNCHRONOUSLY (no orphan push): CmdCode::queued (+
    // out_ctr = the MAC ctr the app correlates async pushes by), err_no_gateway (no gateway serves hops[0]'s leaf),
    // or err_too_large (the inner overflows). The handler validates the path before calling (hop_count, layer!=0, hops[0]!=ours).
    CmdCode originate_layer_path(uint32_t dst_hash, const uint8_t* hops, uint8_t hop_count, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t& out_ctr, uint8_t type = 0, uint32_t override_source_hash = 0);   // §GapB: type (E2E_ACK for a re-originated ack) + override_source_hash (the delegating mobile's hash) thread through to enqueue_cross_layer
    bool    enqueue_cross_layer(uint8_t gw_node, uint32_t dst_hash, const uint8_t* layer_ids, uint8_t n_layers, uint8_t cur, const uint8_t* body, uint8_t body_len, uint8_t flags, uint16_t* out_ctr = nullptr, uint8_t type = 0, uint32_t override_source_hash = 0);  // build the layer-path inner -> next-hop G; honors flags & E2E_ACK_REQ; *out_ctr=ctr on success; false = fit/queue fail. §GapB: type stamps the frame TYPE; override_source_hash (!=0) sets the inner SOURCE_HASH (the delegating mobile) instead of _key_hash32
    void     send_xl_ack(const data_unicast_inner& dm, uint16_t acked_ctr);   // §GapB: a cross-layer E2E-ack = a NORMAL send to (reversed path, dm.source_hash) type=E2E_ACK. STATIC recipient -> enqueue_cross_layer; MOBILE recipient -> delegate_send_layer (never self-originate an XL frame).
#if MR_FEAT_MOBILE
    uint16_t delegate_send_layer(uint32_t dst_hash, const uint8_t* hops, uint8_t hop_count, uint8_t enclosed_type, const uint8_t* body, uint8_t body_len, uint8_t flags);   // §S1: a REGISTERED mobile WRAPS a cross-layer send to its HOME (DATA_TYPE_MOBILE_SEND + CROSS_LAYER path, body prefixed by enclosed_type). Returns the wrapper ctr (0 = no home / invalid). The home unwraps + re-originates via originate_layer_path.
#endif
    void    drain_resolved_parked_sends();                       // beacon-tick re-drain: any parked hash now authoritatively bound
    void    age_out_parked_sends();                              // give up on parked sends past send_defer_ttl_ms
    // Diagnostic `resolve` (CmdKind::resolve): locate a hash WITHOUT sending a DM. Authoritative cache hit (or
    // own hash) -> answer now; else park a notify-only request + flood H. The answer/timeout rides hash_resolved.
    void    request_resolve(uint32_t key_hash32, bool hard);     // the resolve entrypoint (called by on_command)
    void    park_resolve_request(uint32_t key_hash32);           // park a notify-only resolve (de-dups by hash)
    void    push_hash_resolved(uint32_t key_hash32, uint8_t node_id, bool authoritative);  // enqueue the push (node_id 0 = timeout)
    // ★★★ §id-hash S4b (spec §5): the two-stage by-id `reqpubkey`. See the `PendingIdPubkey` ring for the state and
    // node_hashlocate.cpp for each function's reasoning — including why the stage-2 escalation is NOT the auto-resolve
    // `§no-auto-reqpubkey` forbids.
    bool    id_pubkey_intent_arm(uint8_t id, uint8_t plane);     // refresh-or-insert; FALSE = ring full -> the caller refuses BEFORE airtime
    void    id_pubkey_intent_clear(uint8_t id, uint8_t plane);   // undo an arm whose stage-1 frame the TX path then rejected
    void    id_pubkey_intent_consume(uint8_t id, bool team_plane, uint32_t key_hash32);   // an id->hash answer landed: fire stage 2 (spec §5 step 3)
    void    age_out_pending_id_pubkey();                         // periodic (kAgingTimerId, beside age_out_parked_sends): bounded timeout -> loud giveup
    // L2c verify-on-delivery: a DM whose DST_HASH != our key was misdelivered by an id collision —
    // FORWARD it (identity-preserving, not re-originated) toward the real owner of want_hash. The HEAL
    // (renumber) is confirmation-gated: deferred to the HARD-H resolution, fired only when want_hash resolves
    // back to OUR own id (a proven same-id collision) — see design §7.1.
    void    l2c_handle_misdelivery(const PostAck& pa, uint32_t want_hash);
    void    l2c_park_redirect(uint32_t want_hash, const PostAck& pa);                 // hold a misdelivered DM for forward-on-resolution
    bool    l2c_enqueue_forward(uint8_t to_id, uint8_t origin, uint16_t ctr, uint8_t ctr_lo, uint8_t flags,
                                uint8_t type, const uint8_t* inner, uint8_t inner_len, const uint8_t nonce_seed[8]);   // fresh ORIGINATOR-budget leg; type/nonce_seed threaded (S1: a typed/CRYPTED redirect keeps them); false = dropped (queue full)
    void    l2c_confirmed_collision(uint32_t want_hash);                              // HARD-H resolved want_hash->our id => key-only heal (called AFTER the drain loop)
    bool    l2c_redirected_recently(uint32_t want_hash);         // one redirect action per hash per window (anti-flood)
    void    l2c_mark_redirected(uint32_t want_hash);
    // node_id auto-assignment (DAD + heal) — node_join.cpp.
    int     join_choose_candidate_id();                          // prefer previous id, else a random free slot (-1 = leaf full)
    uint8_t find_free_mobile_id(uint32_t key_hash32);            // §mobile 2a: host-assign a free LOCAL id, TOP-DOWN 254..17 (0 = pool full; idempotent for a known key); §S0 excludes id_bind/_rt statics
    void    evict_aliased_hosted_mobile(uint8_t node_id, uint32_t static_key_hash32);   // §S0(b): an authoritative static binding lands for a hosted mobile's local id -> evict the mobile (it re-registers)
    bool    join_start_claim(const char* reason);                // pick a candidate, bump epoch, broadcast J_CLAIM, arm the guard
    void    join_claim_guard_fire();                             // kJoinClaimGuardTimerId: adopt (no objection) or deny+retry
    void    join_adopt(uint8_t node_id);                         // set_identity + joined + self-bind + beacon
    void    handle_j(const uint8_t* bytes, size_t len, const RxMeta& meta);   // J RX dispatch (CLAIM/DENY; DISCOVER/OFFER later)
    void    addr_conflict_send_deny(uint8_t node_id, uint32_t owner_key, uint32_t claimant_key, uint8_t reason,
                                    bool team_scoped = false, uint32_t team_id = 0);  // owner defends its id; §W2c: team_scoped -> a 19-B team-mediated DENY (team_id-gated at the loser)
    void    forced_rejoin(const char* reason);                   // lost the heal tiebreak -> yield id + re-claim
    // §mobile 2b: the mobile-side registration FSM (node_mobile.cpp). Armed only for _cfg.is_mobile (static never enters).
    // §featuresplit: dropped (with the whole registration FSM) on a static/gateway build; the timer-dispatch cases are gated too.
#if MR_FEAT_MOBILE
    void    mobile_discover_fire();                             // DISCOVER + open the collect-OFFERs window
    void    mobile_claim_guard_fire();                         // window close: pick strongest OFFER -> CLAIM + adopt; else backoff
    // §S6 presence plane (mobile side) — node_presence.cpp. REPLACES the periodic re-CLAIM + layer poll.
    void    presence_probe_fire();                             // kPresenceProbeTimerId: send a probe (jittered/suppressed) or retry; k_miss -> HOME LOST
    void    presence_arm_check(uint32_t delay_ms);             // (re)arm the check timer at now+delay (dynamic T)
    void    presence_ingest_roster(const uint8_t* frame, size_t len, const RxMeta& meta);   // mobile: a roster heard -> refresh/re-register/re-home eval
    void    presence_note_candidate(uint8_t home_id, uint8_t home_layer, int16_t snr_q4);   // §S6.4-C: overheard beacon/roster -> candidate home
    void    presence_mark_incompatible(uint8_t home_id, uint8_t home_layer);   // §D16: mark a candidate home INCOMPATIBLE (wrong wire_version roster) -> FSM B/C skips its DISCOVER
    uint8_t presence_cand_alloc_slot();   // §P2-6: append while room, else evict the STALEST (min last_seen_ms) — never clobber the best candidate (was evict-slot-0)
    void    presence_maybe_rehome();                           // §S6.4-C: sustained-better candidate + dwell -> voluntary re-DISCOVER
    void    presence_on_adopt();                               // called from the mobile adopt path: seed clocks + arm the first check probe
#endif
    // §S6 presence plane (home side) — always compiled (a home is a static); host-gated (dormant on a non-host).
    void    presence_ingest_probe(const uint8_t* frame, size_t len, const RxMeta& meta);   // home: a probe heard -> refresh registry + SNR EWMA + custody; schedule a coalesced roster
    void    mobile_reg_touch(uint8_t slot, int16_t snr_q4);    // §3-D: refresh a hosted mobile's last_heard_ms + step its per-mobile SNR EWMA. ONE path shared by the probe (node_join) + beacon (node_beacon) sites so the triple-site (CLAIM seeds; these two update) can't drift. NOT used by CLAIM (which SEEDS a fresh slot, not EWMA-updates).
    void    presence_roster_fire();                            // kPresenceRosterTimerId: emit ONE coalesced roster
    void    presence_schedule_roster();                        // arm the coalesce timer (rate-limit floored)
    void    presence_mark_deleg_fail(uint32_t mobile_hash);    // §B2: a delegated send for this hosted mobile failed loud -> set the roster deleg_fail bit + schedule a roster
    void    presence_emit_roster();                            // build + LBT-broadcast the roster from _mobile_reg + tiers + has_key + dir_epoch
    void    presence_notify_old_home(uint32_t mobile_hash, uint8_t new_local_id, uint16_t new_epoch);  // §S6.4-D: NEW home originates the redirect breadcrumb to the stashed last_home
    uint8_t presence_compute_dir_epoch() const;               // §S6/D6: XOR aggregate of known gateway dir_epochs (a gateway derives its own layer-set epoch)
    // §mobile 6.4: team-DAD — a team member self-assigns a persistent _team_local_id on the team plane (no static host).
#if MR_FEAT_TEAM
    int     team_dad_choose_candidate_id();                    // a free team id (not a _team_peer / _rt_team dest / our current), 17..254; -1 if full
    void    team_dad_guard_fire();                            // guard-window close -> confirm _team_local_id (team_dad_adopted)
#else
    int     team_dad_choose_candidate_id() { return -1; }
    void    team_dad_guard_fire() {}
#endif
public:
#if MR_FEAT_TEAM
    void    team_dad_fire();                                  // (re-)pick + tentatively claim a _team_local_id + arm the guard (public: handle_team / tests)
#else
    void    team_dad_fire() {}
#endif
private:
#if MR_FEAT_MOBILE
    // §autoregister ruling (2026-07-21): the SINGLE gate on the DISCOVER/registration half of the FSM. autoregister ON =
    // autonomous (today). OFF = the app drives every registration attempt via `mobile register` (a one-shot _mobile_arm_once);
    // absent a manual arm the mobile NEVER DISCOVERs/registers — after a home-loss it stays off-grid-quiet (the team plane
    // still works per F-PS-1, and team-DAD runs regardless as it rides the FSM tick BEFORE this gate).
    bool    registration_armed() const { return _cfg.mobile_autoregister || _mobile_arm_once; }
    void    mobile_reset_registration(const char* reason);     // drop registration -> re-enter discovery
    // §mobile 5a: the scan-set = [the mobile's own/bootstrap PHY] ∪ [the LEARNED layer directory]. On boot (nothing learned)
    // that's just layers[0] -> single-PHY = 2b-identical; neighbours appear only after a successful directory pull.
    uint8_t scan_set_count() const { return static_cast<uint8_t>(1 + _learned_layers_n); }
    LayerConfig scan_phy(uint8_t idx) const {                  // BY VALUE (a learned record is synthesized into a LayerConfig); idx 0 = own layer, 1..n = learned
        if (idx == 0 || _learned_layers_n == 0) return _cfg.layers[0];
        const LayerRecord& r = _learned_layers[(idx - 1) % _learned_layers_n];
        LayerConfig c{}; c.layer_id = r.layer_id; c.routing_sf = r.sf;
        c.freq_mhz = static_cast<double>(r.freq_khz) / 1000.0; c.bw_hz = r.bw_hz;
        c.allowed_sf_bitmap = static_cast<uint16_t>(1u << r.sf);   // the learned control SF as the DATA-SF set
        return c;
    }
    void    adopt_mobile_phy(const LayerConfig& phy, bool retune_radio = true);   // §mobile 5a: adopt the host's PHY (config scalars leaf/sf_list ALWAYS; radio retune only when retune_radio — single-PHY is already tuned)
    void    mobile_layer_query_fire();                         // §mobile 5a: pull the layer directory from a gateway (armed while registered)
    int     nearest_bridging_gateway();                        // §mobile 5a: a bridging gateway we can route to (learned type-4 TLV), or -1
    void    learned_layers_ingest(const uint8_t* body, size_t len);   // §mobile 5a: parse [count][record…] -> upsert _learned_layers (dedup, TTL, evict-oldest)
#endif
    // §mobile 3b/4: stamp a fresh outbound TxItem's origin + self-mark. A REGISTERED MOBILE bills its home_node (an
    // accountable GLOBAL id; the mobile's E2E identity still rides sender_hash) and self-marks (mobile_src -> the host
    // keeps our local-id out of the global rt, Fix 2). A static/host node = _node_id, unmarked (byte-identical).
    // ★★ §team-parity T6 (spec §3/T6, OWNER-RULED §11 2026-07-28): ONE ORIGIN NAMESPACE PER PLANE. A TEAM-plane flight
    // stamps our team_local_id(); a static/global flight keeps the pre-T6 home_id-or-node_id rule verbatim. Before T6 the
    // namespace depended on how the SENDER happened to be attached — an OFF-GRID member stamped its team id only because
    // node_id == team_local_id there, while a HOMED member stamped its HOME's STATIC id (measured live: s28 node 3 and s29
    // node 3 both aired origin=101 on a `-t` DM). Consequences that were MEASURED on 2026-07-28, not reasoned:
    //   • the acking teammate takes `origin` as its ack destination, so a homed member's team `-a` addressed the HOME on
    //     the STATIC plane — 1.77 s and correct WHEN static infrastructure was reachable (a plane crossing that violates
    //     R3, "team-internal routing only"), and FOUR static-plane RREQ floods for the home's id then
    //     send_failed{e2e_ack_timeout} when it was not — the bench's own leaf-4-vs-leaf-7 shape;
    //   • T2's DATA-origin learn had to be fenced by is_team_peer(origin) (node_mac_rx.cpp:694) because "is this origin a
    //     team id?" was undecidable. T6 makes it decidable; relaxing that fence was NOT folded in here (C1).
    //     ✔ 2026-07-28: §team-parity T7 has since REMOVED that fence, re-measuring this function's effect first — over
    //     all 36 scenarios every plain-DM team-plane origin is now inside the team id space, so the ✖ MISSING half of
    //     that site's note (the §0 bench case) is closed. This block is T7's premise; do not weaken it without re-reading
    //     the soundness note at that site.
    // Anti-spam accountability on the team plane moves from the home to the member, DELIBERATELY (the §11 ruling).
    // ⚠ `team_local_id() != 0` is a hard precondition, not caution: a member whose team-DAD is still pending has
    // _team_peer bits set (node_beacon.cpp:776 does not require our own id) while team_local_id() is still 0, and
    // stamping origin 0 would air the reserved sentinel. It falls back to the pre-T6 expression there. `send -t` is
    // already refused loud in that window (node.cpp:1138); an AUTO send to a team peer is not — MISSING, deferred,
    // because refusing it is a new loud failure on a path that works today (C1/C2 both point away from folding it in).
    // Static reduction: flight_is_team_plane() is false for every static node (is_team_peer stubs/reads false when
    // team_id==0) and for every GLOBAL send, so the expression reduces to the pre-T6
    // `mob ? _my_mobile_reg.home_id : _node_id` VERBATIM. Build profiles: the team arm is MR_FEAT_TEAM-gated and
    // flight_is_team_plane() itself returns a compile-time false on the three gateway_* envs (MR_FEAT_TEAM 0).
    // `dst` is passed rather than read from item.dst because every caller assigns item.dst AFTER this call — passing it
    // keeps that order untouched (no reorder risk across the five call sites) and makes the plane decision explicit.
    void    stamp_origin(TxItem& item, Plane plane, uint8_t dst) const {
#if MR_FEAT_MOBILE
        const bool mob = _cfg.is_mobile && _my_mobile_reg.active;
#if MR_FEAT_TEAM
        if (flight_is_team_plane(plane, dst) && team_local_id() != 0) {
            item.origin     = team_local_id();   // the id EVERY teammate can route (node_mac.cpp:70's invariant, now true)
            item.mobile_src = mob;               // unchanged: the wire mark is `pt.mobile_src || team_next` (node_mac.cpp:817)
            return;
        }
#else
        (void)plane; (void)dst;
#endif
        item.origin = mob ? _my_mobile_reg.home_id : _node_id;
        item.mobile_src = mob;
#else
        (void)plane; (void)dst;
        item.origin = _node_id;   // §featuresplit: a static/gateway node never bills a home — always self-origin, unmarked
        item.mobile_src = false;
#endif
    }
    void    join_deny_id(uint8_t id);                            // add to the denied list (1-day TTL)
    bool    join_id_denied(uint8_t id) const;                    // is this id currently denied (not expired)?
    void    age_out_denied_ids();                                // drop denied entries past dad_denied_id_ttl_ms
    bool    mediated_recently(uint8_t node_id, uint32_t loser_hash) const;  // L2a: did we already DENY this (id,loser) this window?
    void    mark_mediated(uint8_t node_id, uint32_t loser_hash);            // L2a: record a sent mediated DENY
    void    age_out_mediated();                                             // drop mediation records past the suppress window
    // Q REQ_SYNC plane (boot route-bootstrap) — node_query.cpp.
    void    req_sync_loop_fire();                                  // kReqSyncTimerId: send + re-arm while discovery+starved (dv:9167)
    void    send_req_sync_q(const char* reason, bool force = false, bool team_plane = false);  // broadcast a REQ_SYNC Q (no draw; dv:8032). force=bypass boot-flag+route-rich guards (reactive route-miss pull). §team-parity T0: team_plane is INERT — no caller passes true until T4
    void    send_config_pull(uint8_t to, uint16_t lineage, uint16_t epoch);  // R6.2: 1-hop CONFIG_PULL to a heard member
    void    send_c_config(uint8_t to);                            // C frame (cmd 0xB): control-plane answer to a CONFIG_PULL carrying OUR leaf config (an empty-sf_list joiner CAN receive it)
    void    handle_c(const uint8_t* bytes, size_t len, const RxMeta& meta);   // C RX dispatch (cmd 0xB) -> adopt if addressed to us on our leaf
    void    adopt_c_config(const uint8_t* body, size_t len);      // adopt a pulled config (cfg + recompute + persist Push); lineage from the last-heard beacon
public:
    bool    leaf_config_write();                                   // R6.3 §4.1: operator config write -> epoch=max_seen+1 + re-advertise (managed only)
private:
    void    handle_q(const uint8_t* bytes, size_t len, const RxMeta& meta);   // Q RX dispatch (dv:11767)
    void    schedule_sync_response(uint8_t requester, bool requester_mobile, bool team_plane); // jittered full-table reply (the backoff DRAW; dv:8064)
    void    sync_response_fire(uint8_t slot);                      // kSyncResponseTimerId+slot: emit_beacon("sync") unless suppressed
    bool    q_responded_recently(uint8_t opcode, uint8_t src, uint8_t dest);  // q_responded_to dedup (ttl q_respond_ttl_ms)
    void    mark_q_responded(uint8_t opcode, uint8_t src, uint8_t dest);      // refresh/append (evict-oldest; dv:11778)
    // Channel-message gossip plane (ROADMAP §3) — node_channel.cpp. Phase 1: buffer + origination + DATA-M ingest.
    // Single-layer; gateways skip (Principle 11). seen_by is a 256-bit bitmap (neighbour id -> bit) so the
    // safe-eviction cover-check is O(neighbours). Struct defs here so they precede both the decls + the state.
    struct ChannelEntry {
        uint32_t id;                 // channel_msg_id: origin<<24 | (key_hash32&0xffff)<<8 | ctr
        uint8_t  channel_id;
        uint8_t  flavor;
        uint8_t  origin;             // == (id >> 24): the minting node
        bool     dirty;              // advertise in the BCN digest until bcn_ad_count hits K (Phase 2)
        uint8_t  bcn_ad_count;
        uint64_t received_at;
        uint8_t  seen_by[32];        // 256-bit set of neighbours known to hold this msg (eviction safety)
        uint16_t payload_len;
        uint8_t  payload[protocol::channel_msg_max_payload_bytes];
        uint32_t team_id = 0;        // §mobile 6.3: 0 = a normal leaf channel message; !=0 = a team-scoped message (flavor has channel_flavor_team). Re-emitted on gossip/re-broadcast.
    };
public:
    // Public so native tests can inspect the per-origin channel ledger directly (like channel_buffer_count()).
    struct ChannelOriginEvent  { uint32_t id; uint64_t t_ms; };
    struct ChannelOriginLedger {
        ChannelOriginEvent ev[protocol::cap_channel_origin_events];   // MF7: sized by the new const
        uint8_t  n = 0;
        uint64_t last_flood_ms = 0;   // Slice 2: per-origin last admitted flood — the channel_min_interval_ms burst floor
    };
private:
    // Channel FLOOD in-progress state (2026-06-08 redesign). One slot per concurrent flood mid-backoff;
    // slot i owns rebroadcast timer kFloodRebcastTimerId+i. active while awaiting_data (overhear) OR while
    // its rebroadcast timer is armed; freed on fire / coverage-cancel / no-unmarked / anti-spam drop.
    struct FloodState {
        bool     active = false;
        bool     awaiting_data = false;   // RTS-M seen, DATA-M not yet (fast-self-pull candidate)
        bool     team_flood = false;      // §mobile 6.3: the RTS-M was mobile_src (a TEAM channel flood). The RTS carries no team_id (only the DATA-M does), so we CANNOT know if it is OUR team until the DATA-M -> do NOT fast-self-pull (would emit a CHANNEL_PULL for a possibly-FOREIGN team). ingest_channel_m team-gates the DATA-M + frees a foreign state.
        uint32_t id = 0;                  // channel_msg_id
        uint8_t  src = 0;                 // who relayed it to us (pull target / neighbour-learn)
        int16_t  rx_snr_q4 = 0;           // SNR of the winning RTS-M (drives the backoff)
        uint8_t  bitmap[32] = {};         // working coverage (OR'd from every heard RTS-M for this id)
        uint8_t  body[protocol::channel_msg_max_payload_bytes] = {};  // cached for the re-flood DATA-M
        uint8_t  body_len = 0, channel_id = 0, flavor = 0, hop_left = 0;
    };
    struct ChannelPullPending  { bool active; uint32_t id; uint8_t target; uint64_t requested_at; uint64_t fire_at; };
    struct ChannelPullRecent   { uint32_t id; uint64_t t_ms;       // re-pull dedup (Lua channel_pull_recent)
                                 bool same_key(const ChannelPullRecent& o) const { return id == o.id; } };
    // Part 2: per-origin re-offer (timer kChannelReofferTimerId+slot). team = a TEAM flood: a single relay does NOT confirm coverage on a mixed multi-hop chain, so it re-offers all its retries (P-BUDGET s28 class). holder (§F-CH-RELAY) = a RELAY (not the origin) re-offering to cover its own still-unmarked downstream team neighbours — coverage-driven (seen_by), deterministic jitter, team-only.
    // ★★ §b38-b40 FIELD ORDER IS LOAD-BEARING — the four bools lead, then the 4-aligned `id`, then `ctr`+`retries_left`
    // in the 2 bytes that follow it. MEASURED: 12 B before this slice (active/pad3/id/retries_left/team/holder/pad1) and
    // 12 B after, i.e. `relay_seen` (+1 B) and `ctr` (+2 B) BOTH cost zero — they land in the 3-byte hole that sat after
    // `active` and the 1-byte tail pad. x cap_channel_reoffer_pending(4) x MR_N_LAYERS, so a grown record would move
    // sizeof(Node) (the D2 trigger); the static_assert below is the tripwire, and it is per-ABI, not native-only.
    struct ChannelReofferPending {
        bool     active;
        bool     team;
        bool     holder;
        bool     relay_seen;     // §b38: a relay of THIS origination was overheard at least once. Remembered rather than discarded, so a TEAM post can report the truth (see channel_reoffer_confirm) AND keep re-offering for its far members. Also the once-only latch: the outcome push is emitted exactly once per origination and exhaustion must never contradict a `true` already sent.
        uint32_t id;
        uint16_t ctr;            // §b40: the FULL 16-bit originating ctr (next_ctr), so channel_sent can be correlated past 255 posts. ⚠ A LOCAL CORRELATION HANDLE ONLY — the wire carries just `ctr & 0xff` (the channel msg-id's low byte, channel_msg_id_mint), so no peer can echo more than 8 bits and this must never be matched against a received id. 0 on a holder slot (a relay owns no origination).
        uint8_t  retries_left;
    };
    static_assert(sizeof(ChannelReofferPending) == 12 && offsetof(ChannelReofferPending, id) == 4
                      && offsetof(ChannelReofferPending, ctr) == 8,
                  "node.h: ChannelReofferPending grew — §b38's relay_seen / §b40's ctr left the padding they were "
                  "placed in, so the record now costs real bytes x cap_channel_reoffer_pending x MR_N_LAYERS and "
                  "sizeof(Node) has moved (see the layout tripwire at the end of this header)");
    void    channel_reoffer_register(uint32_t id, bool team, uint16_t ctr);   // Part 2: arm a re-offer slot on flood origination (retries_left = team ? channel_reoffer_team_max_retries : channel_reoffer_max_retries). §b40: `ctr` is the origination's full 16-bit next_ctr, remembered for the channel_sent push.
    void    channel_holder_reoffer_register(uint32_t id);          // §F-CH-RELAY: a team-flood HOLDER arms a coverage-driven re-offer after it re-broadcasts, iff it still has unmarked hops-1 team neighbours (deterministic jitter, no RNG draw)
    void    channel_reoffer_fire(uint8_t slot);                    // Part 2: timer fire — re-flood if not yet confirmed + retries remain, else free (holder slot: re-check seen_by coverage instead of the confirm flag). §b38: the exhaustion give-up emits channel_sent{relayed:false} ONLY when no relay was ever seen.
    void    channel_reoffer_confirm(uint32_t id);                  // Part 2: a relay of OUR message was overheard -> report the outcome (channel_sent{relayed:true}, once) and, on the NON-team plane, cancel the pending re-offer (dedicated signal, NOT seen_by). §b38: a TEAM origin reports AND keeps re-offering; a HOLDER slot reports nothing.
    int     channel_buffer_find(uint32_t id) const;                // index of the entry, or -1 (dv:3426)
    bool    channel_mark_seen_by(uint32_t id, uint8_t neighbour);  // set seen_by bit; true if newly set (dv:3434)
public:
    bool    channel_origin_admit(uint8_t origin, uint32_t msg_id, bool team_plane = false); // per-(PLANE,origin) distinct-count anti-spam (dv:3456). Public: the receiver-HOOK test seam (drives the cap + 10s burst floor directly). §T6/B: team_plane keys the ledger; the default keeps every existing static/leaf caller (incl. the native hook tests) on the STATIC key verbatim.
    // Slice 6: the send-outcome feedback pushes. Public so native tests can drive them (the reoffer-exhaustion path
    // enqueues channel_sent{relayed:false}); called internally from do_send_channel / become_free / channel_reoffer_*.
    void    emit_send_blocked(bool channel, SendFailReason reason, uint32_t next_ms);   // Slice 6a: the send_blocked push (self-gate)
    void    emit_channel_sent(bool relayed, uint16_t ctr);                              // Slice 6c: OWN channel post re-offer outcome
private:
    int     channel_buffer_pick_eviction(bool* safe) const;        // oldest-all-seen else oldest; index (dv:3485)
    bool    channel_entry_fully_seen(const ChannelEntry& e) const; // 2026-06-23: every live 1-hop neighbour holds e (or none to serve) -> retire-OK (holder-aware retirement; NOT shared with pick_eviction — opposite nn==0 meaning)
    void    channel_buffer_add(const ChannelEntry& e);             // insert; evict if full (dv:3511)
    void    cancel_channel_pull(uint32_t id, uint8_t overheard_from, bool peer_q = false); // pull cancel: peer_q=true -> a peer's Q pulled it (dv:11831); else we received it (dv:11006)
    uint16_t do_send_channel(uint8_t channel_id, const uint8_t* body, uint8_t body_len, bool crypt = false, bool with_location = false);  // send_channel origination (dv:12126). §chan-crypt CL2a: crypt=true SEALS the body under the team content key (+ channel_flavor_crypted). §chan-crypt CL2b: with_location=true also packs THIS node's pack_loc6 position into the sealed inner (bit1) — it REQUIRES crypt (Node::on_command refuses `-l` on any post that would not be sealed, ruling O6) and is ignored without it, so a position can never reach a plaintext body. BOTH defaulted false so the two OTHER callers — the home's delegated re-originate (handle_channel_post) and src/fw_main.cpp's `testch` workload — stay plaintext AND position-free by construction.
#if MR_FEAT_MOBILE
    bool    do_send_channel_delegated(uint8_t channel_id, const uint8_t* body, uint8_t body_len);  // §S7 T-B: a registered mobile delegates a GLOBAL/leaf channel post to its home (MOBILE_SEND wrapper, enclosed DATA_TYPE_CHANNEL_POST). false = no home (off-grid) -> caller fails loud.
#endif
    // Phase 2: digest emit/ingest + the jittered pull (THE draw). SELECT/COMMIT split (B, 2026-06-23): build is side-effect-free
    // (fills `picked`); the per-ad ad_count++/retire is COMMITTED separately.
    // ★★ §tx-admission TX3 (owner ruling 2026-08-02) — WHERE THE COMMIT HAPPENS AND WHAT "SENT" MEANS. It is NOT
    // `emit_beacon`, and it is NOT literal airtime; both halves of the old wording here were wrong after TX3.
    // "Sent" = **ACCEPTED BY THE TRANSMITTER/DeviceHal** — the strongest boundary this architecture can observe —
    // and the commit lives at the two sites that reach it: the IMMEDIATE path (`tx_flood` after `_hal.tx` answers
    // ok, node_mac.cpp) and the DEFERRED path (the LBT timer's `lbt_complete`, node.cpp — the selected ids ride the
    // `DeferredLbt` slot so a late rejection leaves both `bcn_ad_count` and `dirty` untouched).
    // ⚠ WHAT THE BOUNDARY IS NOT: a later `DeviceHal::pump_tx` radio-start error drops the frame AFTER admission and
    //   is OUTSIDE the guarantee. Nothing here claims the beacon reached the air.
    size_t  build_channel_digest_ext(uint8_t* out, size_t cap, uint32_t* picked, uint8_t& npicked);  // SELECT: dirty ids -> BCN ext-TLV; NO side effects (dv:1426)
    void    commit_channel_digest_advertised(const uint32_t* ids, uint8_t n);  // COMMIT (transmitter-admitted, see above): ad_count++ + holder-aware retire
    void    process_channel_digest(uint8_t src, const uint32_t* ids, uint8_t count);  // diff -> mark/schedule pull (dv:3546)
    void    channel_pull_fire(uint8_t slot);                       // kChannelPullTimerId+slot: re-check overhear -> tx the pull
    bool    channel_pull_recently(uint32_t id) const;             // re-pull dedup window (dv:3567)
    void    channel_pull_mark(uint32_t id);                        // record a fired pull (channel_pull_recent)
    // Phase 2c: the CHANNEL_PULL responder + M-broadcast tx.
    void    handle_channel_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count);  // dv:11821
    void    enqueue_channel_m(uint8_t target, const ChannelEntry& e);  // M-inner DATA -> tx_queue (dv:11875)
    bool    channel_m_in_flight(uint32_t id) const;              // an M-payload for `id` already pending/queued (dv:11850 dedup)
    bool    channel_have_id_lo16(uint16_t lo) const;             // do we hold a channel msg whose id low-16 == lo? (overhear skip, dv:2081)
    // M-broadcast fire-and-forget tx (no CTS/ACK; chosen_data_sf = max allowed; dv:6997/7044).
    void    issue_m_broadcast();                                  // set up the m_broadcast flight from _pending_tx + fire the RTS
    void    tx_m_broadcast_rts();                                 // pack+tx the M_BROADCAST (or FLOOD) RTS + arm the RTS->DATA gap (no CTS wait)
    // ---- §chan-crypt CL2a — the SEALED team channel post (T-K2 §2.2; node_channel.cpp) --------------
    // ★★ THE KEYING MODEL, and it is NOT the DM's: a channel post is a BROADCAST TO A GROUP, and every member holds
    // the same `team_ch_priv`. So the AEAD key is SYMMETRIC — derived from the team content key alone — NOT a per-pair
    // ECDH like e2e_seal_inner. Consequences, both deliberate:
    //   ✔ EVERY keyholder opens EVERY post, with no prior pairwise key exchange. That is the requirement T-K2 states
    //     in its own words ("Everyone holding team_ch_priv opens"); a per-sender ECDH could not meet it (a keyholder
    //     lacking the SENDER's pubkey would hold the team key and still see nothing), and it would cost one X25519
    //     per cached peer key per received post on the RX path.
    //   ✖ NO in-team sender AUTHENTICATION — sealing proves MEMBERSHIP, not identity. That bound is already ruled and
    //     recorded at PeerLocSrc above ("ANY KEYHOLDER CAN FORGE ANOTHER MEMBER'S source_hash"); this is the code that
    //     bound describes. An OUTSIDER still cannot forge or read a post.
    bool    channel_content_key(uint8_t key[32]) const;   // BLAKE2b("MR-TEAM-CH-v1" | team_ch_priv)[:32]. false = keyless (key untouched) — the ONE derivation, shared by seal and open (U2)
    uint8_t channel_seal_body(uint32_t msg_id, uint8_t channel_id, const uint8_t* pt, uint8_t pt_len,
                              uint8_t* out, uint8_t out_cap, SealOutcome& outcome);   // -> [seal_ctr 2 LE][seed8 8][ct‖tag]; 0 = refused (outcome says why). Mirrors build_sealed_relay_body's body shape exactly (U1)
    bool    channel_open_body(uint32_t msg_id, uint8_t channel_id, const uint8_t* body, uint16_t body_len,
                              uint8_t* out, uint8_t& out_len);   // false = no key / malformed / TAG FAIL -> the caller drops the CONTENT (it still relays the frame)
    // ---- channel FLOOD plane (2026-06-08 redesign; node_channel.cpp) -------------------------------
    int     flood_state_find(uint32_t id);                        // active slot for id, or -1
    int     flood_state_alloc(uint32_t id);                       // free slot, or -1 (all active -> DROP to repair; never evict, §6)
    void    flood_state_free(uint8_t slot);                       // clear active + cancel its rebroadcast timer (the ACTIVE leaf)
    void    flood_state_free(uint8_t layer, uint8_t slot);        // ...on an EXPLICIT leaf; the 1-arg form delegates here (purge_tx_carriers's reprovision axis sweeps every leaf)
    void    flood_set_my_coverage(uint8_t* bm, bool team = false) const;   // §S7 T-A plane-keyed: team=false -> my static id + _rt hops==1 + HOSTED MOBILES (§S7 T-B: a home covers its registered mobiles); team=true -> my team_local_id + _rt_team hops==1. Idempotent (originate-seed AND rebroadcast cover). The bitmap indexes ONE id-space per flood (§18: team ids never mix with static ids).
    bool    flood_any_unmarked(const uint8_t* bm, bool team = false) const; // §S7 T-A: any coverage target unmarked? team-plane consults _rt_team (+ its own team id-space); static consults _rt + hosted mobiles
    void    enqueue_flood_m(uint8_t channel_id, uint8_t flavor, uint32_t id, const uint8_t* body, uint8_t body_len,
                            const uint8_t* bitmap32, uint8_t hop_left);   // build+enqueue a FLOOD m-broadcast (no target)
    bool    handle_flood_rts(const rts_out& r, const uint8_t* in_bitmap, int16_t snr_q4);  // §4.2 RX of a FLOOD RTS-M; true = fresh state -> retune to catch DATA-M
    void    flood_forward_decision(uint8_t slot);                // §4.5 after DATA-M ingest: silent | arm backoff
    void    flood_rebroadcast_fire(uint8_t slot);                // kFloodRebcastTimerId+slot: re-flood {unmarked+me}, hop_left--
    // void    flood_log_coverage(const char* tag, uint32_t id, const uint8_t* bm) const;  // FLOOD-DBG disabled 2026-06-23 (def #if 0'd in node_channel.cpp; re-enable for bench diag)
    void    flood_fast_self_pull(uint8_t slot);                  // §4.4: caught RTS-M, missed DATA-M -> pull from src
    uint8_t max_data_sf() const;                                  // highest SF in allowed_sf_bitmap (largest = most robust)
    uint8_t max_data_sf_index() const;                            // its index in the ascending allowed set (the RTS sf_index)
    static uint32_t m_inner_id(const uint8_t* inner);             // channel_msg_id (BE) from an M-inner buffer [id4|ch|fl|body]

    // ---- route table (DV merge) --------------------------------------------
    enum class MergeAction : uint8_t { none, new_dest, primary_refresh, promote, alt_install };
    RtEntry*    rt_find(uint8_t dest, Plane plane = Plane::AUTO);   // Wave 2: plane-aware dispatch (AUTO=is_team_peer, TEAM=_rt_team, GLOBAL=_rt)
    RtEntry*    rt_insert(uint8_t dest);                           // sorted insert; nullptr if full
    void        rt_remove(uint8_t idx);                            // R2: drop _rt[idx], keep sort
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand);   // dv_dual_sf.lua:4484
    // §mobile 6.2: the SAME DV core, over an arbitrary (table,count) — default via the wrappers above = `_active->_rt`
    // (static plane, byte-identical). The §6.2 team plane passes `_rt_team`/`_rt_team_count`. team_plane skips the
    // route_uses_mobile_as_transit block (a same-team peer IS a legal transit in its own table).
    RtEntry*    rt_find(uint8_t dest, RtEntry* rt, uint8_t rt_count);
    RtEntry*    rt_insert(uint8_t dest, RtEntry* rt, uint8_t& rt_count);
    void        rt_remove(uint8_t idx, RtEntry* rt, uint8_t& rt_count);
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand, RtEntry* rt, uint8_t& rt_count, bool team_plane);
    void        sort_candidates(RtEntry& e, bool team_plane = false);   // §2c: team_plane threads to route_strictly_better/effective_score (team liveness + skip freshness + wire-only degraded)
    // route_strictly_better/effective_score take the candidate LIST (cands,n) as context so the
    // R4.2 budget penalty can count viable alternatives (Lua signature (a,b,viab,candidates)). The
    // penalty is 0 for every HEALTHY-tier next_hop, so effective_score == score until a tier is marked.
    bool        route_strictly_better(const RtCandidate& a, const RtCandidate& b,
                                      const RtCandidate* cands, uint8_t n, bool gw_dest = false, bool team_plane = false) const;  // :4227 (gw_dest: cross-layer freshness-exempt). §2c: team_plane -> team liveness in effective_score + SKIP the is_next_hop_fresh viability gate (no team freshness array)
    bool        is_gateway_dest(uint8_t dest) const;          // §cross-layer: dest is a gateway egress (freshness-exempt)
    int16_t     effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n, bool team_plane = false) const; // :4050. §2c: team_plane -> liveness_penalty_q4(team) + NO static bidi_penalty_q4
    int16_t     budget_penalty_q4(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const; // :3887
    int16_t     liveness_penalty_q4(uint8_t next_hop, bool team_plane = false) const;   // §P2: suspect 192 / silent 640 / dead 1280 Q4 (const read). §2c: team_plane -> scans _team_liveness
    // Slice 3: the bidirectionality DETECTION scan. For advertiser P's beacon heard-set (its hops==1 entries),
    // a [dest==self] entry proves P hears us -> confirmed (note_link_confirmed); an ABSENT self in a COMPLETE
    // page proves P does NOT hear us -> one_way; an absent self in a TRUNCATED page is unconfirmed (no change).
    void        update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete, bool team_plane = false);   // §T5: team_plane -> "self" is team_local_id() and the verdict lands in the team bidi slot
    int         resort_routes_for_neighbor_penalty(uint8_t node_id, const char* source, bool local_only);      // :4255
#if MR_FEAT_TEAM
    void        team_resort_routes_through(uint8_t team_local_id, const char* reason = "team_liveness");   // §2c: re-sort _rt_team routes through a demoted/recovered team next-hop (proactive candidates[0] update). §T5: `reason` so the bidi confirm/recover is distinguishable from the liveness tier in rt_penalty_rerank (default = the pre-T5 literal ⇒ both existing call sites byte-identical)
#endif
    RtEntry*    refresh_route_order(uint8_t dst, const char* reason, Plane plane = Plane::AUTO);   // re-sort ONE dest's candidates (catch a tier change since the last sort), dv:4455
    void        maybe_emit_rt_full();

    // ---- R2 route-plane hardening ------------------------------------------
    void     age_out_stale_routes();                               // dv_dual_sf.lua:5249 — ages BOTH planes
    void     age_out_stale_routes(RtEntry* rt, uint8_t& rt_count, bool team_plane);   // §6.2: over an arbitrary table; team_plane clears the _team_peer bit on a full eviction
    uint32_t ttl_for_hops(uint8_t hops) const;                     // hops<=1 neighbor else remote
    void     rt_prune_cycle(uint8_t dest, uint8_t sender);         // 3-cycle prune  :5193
    void     rt_prune_cycle(uint8_t dest, uint8_t sender, RtEntry* rt, uint8_t& rt_count);   // §6.2: over an arbitrary table
    // ---- Peer-liveness internals (routing-liveness port) -------------------
    struct PeerLiveness;                                              // fwd decl (full def below, near the LayerRuntime member structs)
    PeerLiveness* peer_liveness_slot(uint8_t node_id, bool create);   // find (or LRU-create) the per-node slot; nullptr if absent + !create
    PeerLiveness* peer_liveness_slot(uint8_t node_id, bool create, PeerLiveness* tbl, uint8_t& n, uint8_t cap);   // §P2-4: shared table-ref core (static _peer_liveness / team _team_liveness)
    uint8_t       apply_timeout_tier(PeerLiveness& s, uint16_t count, uint64_t now);   // §P2-4: shared rts-timeout tier cascade -> level 0-3 (mutates first_timeout_ms; sets NO until fields)
    bool          clear_liveness_tiers(PeerLiveness& s);              // §P2-4: shared recovery clear-core -> true if anything was live (the emit-only-if-had gate)
#if MR_FEAT_TEAM
    PeerLiveness* team_liveness_slot(uint8_t team_local_id, bool create);   // §2c: self-slotted mirror over _team_liveness (team_local_id-keyed, own LRU); NEVER _peer_liveness / _team_keys
    // §team-parity T5: the CONST twin of team_liveness_slot(id, create=false) — the team bidi READS (bidi_penalty_q4,
    // candidate_degraded) are const and cannot take the non-const slot accessor. nullptr = no slot = LinkBidi::unknown.
    // ⚠ MISSING (deliberate, C1): liveness_penalty_q4 (node_routing.cpp:94) still carries this identical linear scan
    // INLINE. It is provably the same loop, but folding it onto this helper is a pure refactor and T5 is a feature
    // slice — the same C1 split T6 made for `rt_find` vs flight_is_team_plane and T2 made for its learn_direct_neighbor
    // hoist. A cleanup slice owns both. There is exactly ONE scan to fold, not a family.
    const PeerLiveness* team_liveness_find(uint8_t team_local_id) const;
#endif
    bool          e2e_ack_spoofer_flagged(uint8_t src);               // anti-spoof: has `src` been caught faking RTS_FLAG_E2E_ACK within the penalty window? (its exemption is then revoked). Non-const: peer_liveness_slot is non-const.
    void          mark_peer_suspect(uint8_t node_id, uint8_t level, const char* source, uint8_t remote_src = 0);   // set the tier expiry + resort (§P4: remote_src!=0 => gossip-learned: local_only resort + NO advertise-table write; remote_src is also echoed in the event)
    size_t        build_suspect_ext(uint8_t* out, size_t cap);    // §P4: locally-observed suspect/dead peers -> a type-1 or type-2 BCN ext-TLV; 0 = none (dv:1373 build_suspect_nodes_ext)
    void          apply_suspect_gossip(const SuspectEntry* e, uint8_t n, uint8_t bcn_src);   // §P4: a received suspect-TLV -> mark_peer_suspect(remote); skip self + the gossiper (dv:9627)
    void     maybe_exit_discovery(const char* reason);            // :7517

    // ---- R3 data plane (MAC: RTS-CTS-DATA-ACK) -----------------------------
    // override_dst_hash (§mobile 3c): when non-zero, the DM's DST_HASH is stamped with THIS hash (the queried mobile hash M)
    // instead of key_hash_of_id(dst) — so a mobile's home_node sees dst_hash != its key and last-mile-forwards (not consumes).
    uint16_t do_send(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, CryptIntent crypt = CryptIntent::def,
                     uint32_t override_dst_hash = 0, uint8_t type = 0, uint32_t override_source_hash = 0, Plane plane = Plane::AUTO);  // returns the ctr. §mobile delegate: type=MOBILE_SEND + override_source_hash=the mobile's hash (home re-originating on its behalf)
    uint16_t enqueue_data(uint8_t dst, const uint8_t* body, uint8_t body_len, uint8_t flags, const char* tx_event,
                          bool app_dm = false, uint8_t type = 0, CryptIntent crypt = CryptIntent::def, uint32_t override_dst_hash = 0, uint32_t override_source_hash = 0,
                          uint8_t addr_len = 0, Plane plane = Plane::AUTO);   // §mobile: addr_len=1 ORIGINATES a last-mile DM to a hosted mobile's LOCAL id (E2E-ack back to a mobile); 0 = normal global-id send (byte-identical)
    void     send_e2e_ack(uint8_t to_origin, uint16_t acked_ctr, uint32_t sender_hash = 0);   // E2E ACK reply (TYPE=E2E_ACK; e2e_ack_tx). §mobile: sender_hash a hosted mobile -> last-mile the ack to it (origin was home-stamped == a self-send)
    // §GapB (2026-07-18): send_e2e_ack_cross_layer RETIRED — the reversed-path XL ack is now a normal send via send_xl_ack (declared above).
    // §mobile reverse-ack (delegated): a home re-originates a hosted mobile's send under its OWN ctr (ctr_H). When the
    // target's E2E-ack (for ctr_H) comes home, translate ctr_H -> the mobile's original ctr (ctr_M) so the last-miled ack
    // matches what the mobile is waiting on. A DIRECT send (home only forwarded) has NO entry -> out stays acked_ctr.
    void     deleg_ack_put(uint32_t mobile_hash, uint16_t ctr_h, uint16_t ctr_m);                 // §GapB p2: keyed by the MOBILE's hash (XL acker ids alias across leaves — id-keying is WRONG). Record {mobile_hash,ctr_H}->ctr_M (evict oldest/expired)
    bool     deleg_ack_translate(uint32_t mobile_hash, uint16_t acked_ctr, uint16_t& out_mobile_ctr);   // true = translated (delegated); false = pass-through (direct/miss)
    // ★ E2E-ack DEADLINE (shelf item (i), 2026-07-24) — node_mac.cpp. Arm/clear are emit-free (byte-neutral when acks arrive).
    void     e2e_ack_arm(uint32_t key, bool is_xl, uint8_t dst, uint16_t ctr, uint32_t budget_ms);   // silent: a -a send minted its ctr -> track until send_e2e_acked or the deadline. Full ring -> skip + telemetry (the command path pre-refuses).
    void     e2e_ack_clear(uint8_t acker_origin, uint16_t acked_ctr, uint32_t sender_hash);          // silent: a send_e2e_acked arrived -> drop the matching pending entry (a late/unknown ack is a harmless no-op)
    bool     e2e_ack_ring_full() const;                                    // ring-full pre-check for on_command (refuse a new -a send loudly)
    void     e2e_ack_deadline_arm_timer();                                 // re-arm the ONE one-shot to the earliest pending deadline (park_reflood_arm idiom)
    void     e2e_ack_deadline_fire();                                      // timer body: expire elapsed entries -> send_failed{e2e_ack_timeout}, then re-arm
    void     enqueue_push(const Push& p);                                  // append to the bounded ring
    // §3-B.2: THE one send-failure push. Every `send_failed` the companion app sees goes through here, so the
    // {kind,reason,dst,ctr} field set can never be filled short at a new site (the S1/L9 field-drop class). dst/ctr
    // are REQUIRED, not defaulted: several callers legitimately mean 0 ("no addressable dst yet") and that must read
    // as a decision, not an omission. The reason is the CONTRACT string the app keys on (console_json.cpp
    // sendfailreason_name) — pass the site's own; this helper never invents one.
    void     push_send_failed(SendFailReason reason, uint8_t dst, uint16_t ctr);
    // §3-B.9: THE one wire_version join-refusal — the windowed Push+emit ritual shared by the BEACON pre-parse
    // version wall (node_beacon.cpp handle_beacon) and the P-plane roster wall (node_mobile.cpp §D16). Takes the
    // peer's advertised version; fills OUR version as `dst` itself so a caller can't report the mismatch one-sided.
    // NOT extended to the other three join_refused flavours (phy_mismatch / sf_list_mismatch / leaf_full): they carry
    // different payload fields and emit no telemetry, so one helper would need a parameter per difference.
    void     push_join_refused_wire(uint8_t their_ver);
    void     push_peer_key_cached(uint32_t key_hash32);                    // §S6: peer_key_cached push carrying the cached name (copied at cache time; body empty when unknown)
    void     become_free();                                       // dv_dual_sf.lua:7433 (FIFO single-drain)
    void     issue_send(const TxItem& item);                      // :7018 pending_tx + RTS
    void     clear_nack_wait() { _hal.cancel(kNackWaitTimerId); _nack_wait_pending = false; }   // drop a stale BUSY_RX wait
    void     handle_rts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'R' -> CTS
    void     handle_cts (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'C' -> DATA
    void     handle_data(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'D' -> deliver/forward + ACK
    void     handle_channel_data(const uint8_t* b, size_t n, const RxMeta& m);  // on_recv 'M' (cmd 0xA) -> leaf gate + ingest
    void     handle_ack (const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'K' -> done
    void     handle_nack(const uint8_t* b, size_t n, const RxMeta& m);   // on_recv 'N' -> blind+wait / cascade
    void     do_data_tx();                                        // kCtsToDataGapTimerId fire
    void     do_post_ack();                                       // kPostAckTimerId fire (deliver|forward)
    // ---- Slice 3 dual-layer gateway: leaf activation (3d's window scheduler drives activate_layer on timers,
    // gated by layer_swap_blocked). Retunes SF + per-leaf identity + the active-layer scalars/SNR-floor/LBT timing;
    // migrates the per-leaf sync-response ring (shared timer ids) off the LEAVING leaf so a stale fire can't hit it.
    void     activate_layer(uint8_t i);
    bool     layer_swap_blocked() const;                          // §4 busy-guard: never switch mid-exchange
    int16_t  routing_snr_floor_for(uint8_t routing_sf) const;     // SF_DEMOD_THRESHOLD[sf] + sf_margin (per-leaf)
    void     window_switch_fire();                                // Slice 3d: gateway window scheduler (kLayerWindowTimerId) — alternate the active leaf
    void     maybe_emit_gateway_beacon();                         // Slice 3d: per-leaf beacon at window-activation (if the active leaf is due)
    bool     gateway_announce_has_headroom() const;               // rolling airtime < gw_announce_duty_pct % of the duty budget
    void     set_window_anchors(uint8_t active_leaf);             // Slice 3e: refresh each leaf's _next_open_ms (the countdown anchor)
    void     window_grid_now(uint8_t* active_leaf, uint32_t* ms_to_boundary) const;  // Slice 3d GRID: which leaf is active now + ms to its close
    void     store_gateway_schedule(const GatewaySchedule& gs);   // Slice 3e.2: remember a heard gateway's schedule (evict-oldest)
    const GatewaySchedule* find_gw_schedule(uint8_t gw_node_id) const;
    uint32_t gateway_schedule_base_defer_ms(uint8_t gw_node_id, uint32_t* out_jmax) const;  // PURE: base defer + jitter range (no RNG draw)
    uint32_t gateway_schedule_defer_ms(uint8_t gw_node_id);       // Slice 3e.2 SEND path: base + herd-jitter draw (NON-const: draws RNG)
    uint32_t gateway_window_align_beacon(uint32_t nominal_ms);    // gw-window broadcast sync: bias the PERIODIC beacon to a gw-neighbour window-open (NON-const: herd-jitter draw)
    uint8_t  count_direct_neighbors() const;                     // §3e herd sizing: rt entries whose primary candidate is 1-hop
    uint8_t  gateway_spread_nibble() const;                      // §3e: this gateway's 0..15 herd-spread hint (Lua dv:1692)
    uint32_t exchange_airtime_ms() const;                        // §3e: RTS+CTS+gap+DATA+ACK airtime (DATA len = rolling mean)
    // Gateway-doorstep hold (Lua gateway_doorstep_hold@6351): an RTS/ACK timeout to a known gateway —
    // patient window-aware requeue instead of the generic cascade. Returns true if consumed.
    bool     gateway_doorstep_hold();
    // ---- Multi-hop gateway discovery (2026-06-14, type-4 BCN TLV): the originator's gateway SELECTION half ------
    void     ingest_bridged_layer(uint8_t gw_id, uint8_t dest_leaf);   // last-write-wins (one row per gw_id)
    void     prune_aged_bridged_layers(uint64_t now);                  // invalidate rows older than bridged_layers_ttl_ms
    size_t   build_gateway_layer_ext(uint8_t* out, size_t cap);        // our beacon's type-4 TLV (self-advert + re-gossip); 0 = none (s18-inert)
    // ---- Slice 4c.1: cross-layer DM bridge (the keystone) ------------------------------------------------------
    void     bridge_cross_layer(const PostAck& pa, const data_unicast_inner& ui);  // re-inject a transit cross-layer DM onto the far leaf
    int      id_on_leaf_by_hash(uint8_t leaf, uint32_t key_hash32) const;          // resolve key_hash32 -> node_id on a SPECIFIC leaf's id_bind (-1 = unknown); NEVER via _active->
    void     seed_seen_origin_on_leaf(uint8_t leaf, uint8_t origin, uint8_t dst, uint16_t ctr);  // loop-suppress the re-inject on the far leaf
    bool     push_xl_handoff(const XlHandoff& h);                 // buffer a handoff; false = full (refuse loud)
    void     drain_xl_handoffs_for_leaf(uint8_t leaf);           // on activate_layer(leaf): move matching handoffs into the leaf's tx_queue
    // Slice 4a': active_layer_id() (the FULL 8-bit layer_id of the ACTIVE leaf, stamped on every delivered DM/channel
    // record + Push so the app knows which layer a message arrived on) is now PUBLIC (device-console diagnostics block).
    void     start_rts_timeout();
    void     start_ack_timeout();
    void     start_pending_rx_expiry(uint8_t payload_len);
    void     rts_timeout_fire();                                  // :6326
    void     ack_timeout_fire();                                  // :6546
    void     pending_rx_expiry_fire();                            // :6699
    void     tx_rts_retry();                                      // re-pack SAME-ctr_lo RTS
    // R4.5 listen-before-talk. tx_initiating wraps an INITIATING TX (RTS/handle_rts NACK) — LBT pre-check,
    // defer at busy_until + rand(0,lbt_backoff+1) (dv:3680); tx_flood wraps a beacon (LBT + max-defer DROP +
    // duty pre-check, dv:3765); lbt_complete runs the deferred TX (+ the RTS staleness check + start_rts_timeout).
    enum class LbtKind : uint8_t { rts = 0, nack = 1, flood = 2 };
    // R4.5b frame-type tag (echoed by the sim in on_radio_busy; identifies a blocked TX heap-free).
    enum class FrameTag : uint16_t { rts = 0, cts = 1, data = 2, ack = 3, nack = 4, beacon = 5 };
    // ★ §id-hash S1c/S1d: returns FALSE iff the frame was DROPPED — a full LBT defer ring, or a HAL rejection. A
    // successful DEFER is TRUE: it was ACCEPTED, which is the boundary the owner ruled (2026-08-01); it is NOT a
    // promise that it flew, and a deferred frame that dies later is reported by node.cpp's defer arm.
    // NOT `[[nodiscard]]` — the ~22
    // best-effort callers legitimately ignore it; only `emit_hash_query` reads it, because only it feeds a contract
    // event that ASSERTS a frame left. See the definition for why lbt_complete's RTS-only bails are excluded.
    bool     tx_initiating(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    // §3-B.5 de-storm fire, shared by ALL THREE jittered_tx_stash.h members (§F-XL-1 H-forward ring,
    // §F-XL-2 RREQ-forward ring, §S6/QA-3b mobile-OFFER slot): tx the stashed frame at routing_sf as a
    // flood, then clear the slot so a re-entry can't double-send. The tx itself cannot live in the header
    // (it needs tx_initiating + _cfg), so this is the one Node-side half of the discipline.
    void     jtx_fire(uint8_t* buf, uint8_t& len);
    void     rts_duty_defer_fire();                                // cleanup #A redo: re-check duty + hand the deferred RTS (or re-defer / drop-if-stale)
    // ★ §tx-admission TX2: false = the frame was DROPPED or SKIPPED — duty pre-check, busy-too-long, a FULL LBT
    // defer ring, or a HAL rejection. TRUE = ADMITTED (handed to the radio, or accepted into the defer ring and
    // scheduled) — the same acceptance boundary the owner ruled for `reqpubkey_sent`.
    // ⚠ LOAD-BEARING BEYOND TELEMETRY: `emit_beacon` gates `commit_channel_digest_advertised` on this, so a `true`
    // for a dropped beacon burns an advertisement horizon and can retire a digest nobody received.
    // ★ §tx-admission TX3: `digest_ids`/`n` are the channel-digest entries this beacon advertised. tx_flood now OWNS
    // the commit, because only it knows WHERE admission happened: immediate `_hal.tx` == ok -> commit here; accepted
    // into the defer ring -> the ids ride the slot and node.cpp's defer arm commits iff DeviceHal later answers ok;
    // dropped (duty / busy-too-long / ring full / HAL reject) -> NO commit, the entry stays dirty.
    bool     tx_flood(const uint8_t* bytes, size_t len, int16_t sf,
                      const uint32_t* digest_ids = nullptr, uint8_t digest_n = 0);
    // §tx-admission TX1: FALSE iff the HAL REJECTED the frame (a definitive drop). The two RTS-only early-outs
    // return true — a stale-flight CANCEL abandons a flight that is already gone, and the duty defer re-arms
    // kRtsDutyDeferTimerId — so neither is a loss to report. Unreachable from the one reader anyway: emit_hash_query
    // always passes LbtKind::flood.
    bool     lbt_complete(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind, uint32_t rts_flight_gen);
    bool     schedule_lbt_defer(const uint8_t* bytes, size_t len, int16_t sf, LbtKind kind,   // free-slot stash
                                uint32_t rts_flight_gen, uint32_t delay,
                                const uint32_t* digest_ids = nullptr, uint8_t digest_n = 0);   // false = ring full (dropped)
    // NAV (virtual carrier sense, nav_enabled): an overheard unicast RTS/CTS reserves the medium for the rest
    // of that exchange; the node defers its own unsolicited TX (tx_initiating/tx_flood) until it clears. The
    // duration helpers are PURE (native-testable); nav_arm extends _nav_until_ms (max). Conservative SF/size.
    // §rts-cr-overhear: `data_cr` is the CR of the PEER that will send the reserved DATA — the whole frame we
    // are reserving for is somebody else's, so it is NOT active_cr(). No default: every caller must state
    // where its CR came from (C2 fail loud), because the two overheard-CTS callers CANNOT know it (see below).
    uint32_t nav_duration_rts(uint8_t data_sf, uint8_t payload_len, uint8_t data_cr) const;  // overheard RTS -> CTS+DATA+ACK+gaps
    uint32_t nav_duration_cts(uint8_t data_sf, uint8_t payload_len, uint8_t data_cr) const;  // overheard CTS -> DATA(exact, or max if payload_len=0)+ACK+gaps
    void     nav_arm(uint32_t duration_ms);                                 // _nav_until_ms = max(_nav_until_ms, now+dur)
    bool     reserve_yield(uint32_t reserve_ms);                            // spec 2026-06-28: push the pending CTS/ACK timeout past an overheard reserve involving our next-hop, NO retry burned; lifetime-bounded (no starvation). Returns true if yielded.
    // ★★★ §tx-admission TX1 (2026-08-01): tx_with_retry's disposition. It used to be a `bool handed` that was TRUE
    // whenever the frame reached `_hal.tx`, **whatever the HAL answered** — and `DeviceHal::tx` returns `busy` on a
    // full 8-entry ring, bumps `txq_drops` and DOES NOT RETAIN the frame. So a definitive hardware drop read as
    // "handed" all the way up to the app.
    // ⚠⚠ THE THREE-WAY SPLIT IS LOAD-BEARING, and a two-way `false-on-rejection` would have caused a REGRESSION —
    // this is the finding that shaped the slice. `tx_with_retry` has THREE readers (duty_defer_fire, and both
    // do_data_tx arms) and every one of them uses the answer to decide *"arm the post-TX state?"*. Their existing
    // `false` means **"not sent, but a re-send timer IS armed"** (the duty defer). A HAL rejection arms NOTHING, so
    // folding it into that same `false` would suppress `start_ack_timeout()` on a dropped DATA and leave the flight
    // with **no recovery at all** — strictly worse than the reporting defect being fixed. For a retry-eligible frame
    // the MAC timeout IS the recovery (`device_hal.h`'s "MAC timeouts recover the frame"), so those readers must keep
    // arming it. They therefore branch on `!= deferred_retry_armed`, which is EXACTLY their old `true`.
    // ⇒ only `slot < 0` frames (RTS / beacon-flood) have no stash and no MAC timeout, and among those only the H
    //   query reports a disposition to an app — which is why S1d reads this and nothing else does.
    enum class TxHandOff : uint8_t {
        handed = 0,             // the HAL accepted it (queued for the radio)
        deferred_retry_armed,   // NOT sent — the duty pre-check deferred it and kDutyDeferTimerId+slot will re-run
        rejected                // the HAL REFUSED it (busy / too_long / radio_error): dropped, nothing will retry
    };
    // R4.5b: the central TX helper (Lua tx_with_retry dv:3599) — stash the retry-eligible frame + set the
    // frame-type tag + duty pre-check + _hal.tx. Every TX except the beacon routes through it.
    TxHandOff tx_with_retry(const uint8_t* bytes, size_t len, int16_t sf, FrameTag tag);   // §tx-admission TX1: handed / deferred_retry_armed / rejected
    void     retry_stashed(uint8_t slot);                          // re-issue a stashed frame (kRadioBusyRetryTimerId+slot)
    void     duty_defer_fire(uint8_t slot);                        // re-run tx_with_retry from the stash after a duty defer (kDutyDeferTimerId+slot)
    bool     duty_over_budget(size_t len, int16_t sf, uint32_t* wait_ms);   // check_duty_cycle dv:3573; *wait_ms = defer time when over budget
    static int retry_slot_of(FrameTag tag);                        // FrameTag -> stash slot (0..3) or -1 (not eligible)
    static const char* label_of_frame(FrameTag tag);              // FrameTag -> "RTS"/"CTS"/...
    // ---- cascade-to-alt walk + no-route defer+Q ----------------------------
    uint8_t  pick_next_cascade_hop(const PendingTx& pt);          // two-pass walk :5430; 0 = none (NON-const: refreshes the route order first)
    bool     next_hop_selectable(const RtCandidate& c, const PendingTx& pt,
                                 bool allow_uphill) const;        // minimal filter :3990
    void     cascade_to_alt(const char* trigger);                 // on giveup: switch hop or requeue :6456
    void     try_cascade_requeue(const PendingTx& pt, const char* giveup_event);  // exhaustion -> requeue/giveup :6190
    static SendFailReason giveup_fail_reason(const char* giveup_event);   // Slice 6b: "rts_*"->no_cts, "data_ack_*"->no_ack, else none
    // §3-B.2: the TERMINAL giveup of the live flight — tell the app, drop the flight, re-service the queue. Deliberately
    // does NOT absorb the caller's `return`: the 6 sites return differently (bare `return`, `return true`, or fall out of
    // an if/else to a shared `return`), and hiding control flow inside a helper is worse than the duplication it saves.
    void     giveup_flight(SendFailReason reason, uint8_t dst, uint16_t ctr);   // push_send_failed + _pending_tx.reset() + become_free(), in that order
public:
    // ④ load-adaptive cascade budget (Lua cascade_load_skip dv:6275): the effective requeue budget at a given TX-queue
    // depth = cascade_requeue_max − max(0, depth − threshold), clamped ≥0. Pure (depth + constants); static for tests.
    static int cascade_effective_max(uint8_t queue_depth);
private:
    uint32_t requeue_backoff_ms(uint8_t requeue_count) const;     // pure base*2^(n-1) capped :6209
    uint8_t  effective_rts_max_retries(uint8_t requeue_count) const;  // max(0, max-requeue_count) :3119
    void     defer_send(const TxItem& item);                      // no route yet -> hold (originator) :5545
    void     try_drain_deferred();                                // TTL-first, route-exists drain :6765
    bool     alt_tried(const PendingTx& pt, uint8_t hop) const;
    void     mark_tried(PendingTx& pt, uint8_t hop);
    uint16_t next_ctr(uint8_t dst);                               // per-(self,dst) counter (NOT rand)
    uint8_t  select_data_sf(uint8_t rts_sf_index, int16_t rx_snr_q4) const;  // adaptive DATA SF, Lua :3043+:3027
    uint32_t airtime_routing_ms(uint16_t len) const;             // floor-exact, for timeout sizing
    // Anti-spam v2 (MF3/MF8): the channel-capacity C = max(1, D/T_ch) with T_ch = RTS-M + DATA-M airtime. THE single
    // source of C — BOTH channel_cap_origin() (the enforced cap) and limits_snapshot() (the ch_ceiling shown to the
    // user) call it, so the displayed ceiling can never drift from the enforced math. Returns 0 when duty is disabled.
    uint32_t channel_capacity_C() const;
    // retry_jitter_ms() is declared in the public section (R3.x golden test).

    // ============================ Node-global state (cleanup 2026-07-15: by-concern sections; behavior-preserving) ============================
    Hal&     _hal;                // injected platform (radio/clock/timers) — ctor init-list [1]

    // ---- IDENTITY (Node-global) ----
    uint8_t  _node_id;            // ctor init-list [2]; reassignable via _hal.set_protocol_id (join/lease)
    // TEAM-plane id — DELIBERATELY kept HERE (between _node_id & _key_hash32): on 4B-pointer targets these two bytes
    // fill the pre-_key_hash32 padding, so relocating them costs +8 B on the team/mobile builds (measured via the
    // per-board .bss diff — native's 8B alignment hides it). Layout-invariance wins over grouping; do NOT move this pair.
    // (The rest of the team/mobile-member plane groups in a later increment; this stays put.)
#if MR_FEAT_TEAM
    uint8_t  _team_local_id = 0;  // §mobile 6.4: the member's id on the TEAM plane (self-assigned by team-DAD, no host; persistent). 0 = not team-DAD'd (a non-team node, or a team member mid-DAD). The 6.2 team plane (_team_peer/_rt_team, team beacon src, team frames) keys on THIS; the static plane keys on _node_id. §18: _rt_team keeps the two id-spaces from colliding.
    bool     _team_dad_pending = false;  // §mobile 6.4: true during the team-DAD guard window (tentative _team_local_id) -> a same-team src collision RE-PICKS; after (confirmed) -> DEFEND (DENY).
    // §team-ch-key (T-K1): 1 = _team_ch_pub/_team_ch_priv (E2E CRYPTO section below) hold a real keypair.
    // ★ DELIBERATELY SPLIT from the keys it describes, for the SAME measured reason the pair above sits here:
    // byte 19 is pure alignment padding ahead of the 4-aligned _key_hash32, so this flag costs ZERO bytes at
    // this offset and EIGHT beside the keys (the 65 B of new state would round the +64 up to +72 — measured,
    // see the sizeof(Node) ledger at the end of this header). Layout-invariance over grouping, as above.
    bool     _team_ch_key_present = false;
#endif
    uint32_t _key_hash32;         // ctor init-list [3]; stable long identity
    char     _name[32] = {};      // §1.3: human label (the /mrid IdBlob.name, <=32 B); empty -> effective_name() defaults to "MeshRoute node: 0x<hash>"
    uint8_t  _name_len = 0;

    // ---- E2E CRYPTO (Node-global) ----
    uint8_t  _x_secret[32] = {};  // DP1: X25519 ECDH secret (Phase-1 E2E DM crypto)
    uint8_t  _ed_pub[32]   = {};  // DP1: our Ed25519 pubkey (advertised so peers can ECDH to us)
#if MR_FEAT_TEAM
    // §team-ch-key (T-K1, spec 2026-07-26 §2.1): the TEAM CHANNEL content keypair. X25519, dedicated — NOT
    // derived from the identity seed above, NOT a team_id input. Stored in CANONICAL (clamped) form; see
    // team_channel_key_derive in identity.h for why that matters. The has-key flag lives in the IDENTITY
    // block above (it is free there and costs 8 B here — see the note at it).
    // ⚠ MARK OF WHAT IS *NOT* DONE:
    //   · DONE — T-K3 reads them: team_key_grant_send seals _team_ch_priv into a TYPE-19 grant, and
    //     team_key_grant_receive adopts one. So the pair has a producer AND a consumer.
    //   · ✅ RULED AND BUILT (§o3-key-lifetime, owner ruling 2026-07-31): set_team_id() DOES clear them now —
    //     the first of the two candidate fixes this note used to leave open (clear on switch), chosen over
    //     storing an owning team_id beside the pair. A `team <other-id>` switch and `team 0` both leave the
    //     node KEYLESS; the recovery is a teammate's re-grant or a QR, never a local regeneration.
    //     ⓘ The one exception lives at the console caller, not here: handle_team carries a pair minted/adopted
    //     FOR THE TEAM BEING JOINED across the switch (see its §o3-key-lifetime note).
    //   · STILL MISSING — the SEAL. The encrypted channel flavour + seal/open and the un-keyed-receiver drop
    //     push (T-K2, now slice CL2a) are the reason the clear above had to land FIRST: until something seals,
    //     the clear is inert, which is exactly what made it attributable as its own slice.
    uint8_t  _team_ch_pub[32]  = {};
    uint8_t  _team_ch_priv[32] = {};
#endif
    bool     _crypto_ready = false;
    uint16_t _relay_seal_ctr = 0; // §S4 SEALED_RELAY: a dedicated per-node nonce ctr, CARRIED in the relay body (NOT the MAC frame ctr — that stays the delegating home's for dedup). Uniqueness rides the random seed8; this ctr is defense-in-depth (matches the same-layer seal's ctr role). Pre-incremented per relay seal.

    // ---- REMOTE-MGMT (Node-global) ----
#if MR_FEAT_REMOTE_MGMT
    uint8_t  _admin_pubkey[32] = {};   // §remote-mgmt: pinned admin Ed25519 pubkey (trust anchor)
    uint32_t _admin_counter_floor = 0; // §remote-mgmt: replay floor (persisted, write-coalesced)
    bool     _admin_provisioned = false;
#endif
    RemoteInbound _remote_inbound{};   // §remote-mgmt: single inbound `rcmd`/resp slot (one in flight; a 2nd while pending drops). Drained by fw_main. UNCONDITIONAL — NOT `#if MR_FEAT_REMOTE_MGMT`-gated (relocated from the class tail, cleanup 2026-07-15).
    // ★★ §chan-crypt CL2a: the sealed-channel-post nonce ctr — the SAME shape as `_relay_seal_ctr` above, a dedicated
    // per-node counter CARRIED in the body `[seal_ctr 2][seed8 8][ct‖tag]` (the M frame has no ctr field at all;
    // enqueue_channel_m DERIVES its MAC ctr from the msg id, so there is nothing on the frame to reuse).
    // ⚠⚠ AND HERE THE CTR IS EVEN LESS LOAD-BEARING THAN THE RELAY ONE, deliberately: every member seals under the
    // SAME team content key, so two members trivially hold the same counter value. Cross-member nonce separation
    // rides (a) the FRESH 8-B random seed and (b) the channel_msg_id bound into the nonce — which carries the origin
    // id AND 16 bits of the sender's key_hash32, so two members differ even on an identical (seed, ctr). This ctr is
    // defence in depth for the SELF-collision case only. Pre-incremented per seal.
    // ★ PLACEMENT — MEASURED, not chosen for tidiness, and it is worth the two lines: this belongs semantically next
    // to `_relay_seal_ctr` (native offset 186) and putting it there costs EIGHT bytes, not two. Measured by offsetof:
    // at 188 it pushes `_admin_pubkey` 188->190, which pushes the 4-aligned `_admin_counter_floor` 220->224 (2 new pad
    // bytes) and the 8-aligned `_cfg` 472->480 (6 more) => sizeof(Node) +8. HERE it lands at 470, in the 2-byte
    // alignment pad that already sat between `_remote_inbound`'s end and the 8-aligned `_cfg`, and costs ZERO.
    // EIGHTH application of the radio_freq_mhz / team_hop_cap / HashQuerySeen.team_scoped / T5-PeerLiveness /
    // T-K1-_team_ch_key_present / AB4-_peer_loc_n / CL2a-team_channel_crypt padding-placement rule.
    uint16_t _channel_seal_ctr = 0;

    // ---- CONFIG ----
    NodeConfig _cfg;             // borrowed copy from on_init

    // ---- INBOX ----
    Inbox    _inbox;             // persistent inbox (disabled until a backend installs stores; see inbox())

    // ======== ROUTING witnesses (Node-global) ========
    int16_t  _routing_snr_floor_q4 = 0;   // SF_DEMOD_THRESHOLD[routing_sf] + sf_margin_q4
    bool     _rt_full_emitted = false;

    // ======== BEACON / R2 discovery (Node-global) ========
    uint8_t  _beacon_offset = 0;             // sliding stable-page rotation cursor
    bool     _pending_rediscover = false;     // reprovision verb -> restart discovery at the next join_adopt (id stable)
    bool     _triggered_beacon_pending = false;  // coalesce: gates BEFORE the rand draw
    uint64_t _last_beacon_tx_ms = 0;

    // ======== DUTY / airtime (Node-global) ========
    uint64_t _duty_cycle_budget_ms = 0;          // R4.0: floor(duty_cycle*window), derived in on_init; 0 = disabled
    uint16_t _dm_payload_mean = 0;               // §3e: EWMA (alpha 5/16) of DATA payloads we pass; 0 = no sample (use assumption)
    // (_window_epoch_ms relocated to the GATEWAY / CROSS-LAYER scheduler section below — it was orphaned here among the duty/R4.3 witnesses; cleanup 2026-07-15.)
    // ---- R4.3 CHANNEL-BUSY witnesses (adaptive throttle; pure timestamps, no rand) ----
    // R4.3 adaptive-throttle witnesses (channel-busy detector). Pure timestamps, no rand.
    uint64_t _last_rx_routing_sf_ms = 0;         // any successful decode OR preamble-detect (dv:9164/12231); 0 = never
    uint64_t _last_rx_bcn_ms        = 0;         // last beacon ingest (the max-idle B+C filter; dv:9559)

    // ======== MAC / FLIGHT witnesses (Node-global): LBT ring · duty-defer · tx-stash · flight_gen ========
    // R4.5 LBT: derived delays (on_init) + a small RING of deferred-TX slots. The Lua uses independent
    // per-defer closures (dv:3704/3808), so two concurrent busy-channel defers BOTH fire — a single stash
    // would drop the first + desync the rand stream. Each slot has its own timer id (kLbtDeferTimerId+slot).
    // buf holds a full beacon (beacon_max_bytes=151) — a smaller buf would TRUNCATE a deferred page (review #04).
    uint32_t _lbt_backoff_ms        = 0;
    uint32_t _flood_lbt_max_defer_ms = 0;
    uint64_t _nav_until_ms          = 0;         // NAV: medium reserved (by an overheard unicast RTS/CTS) until this ms; 0 = clear
    static constexpr uint8_t kLbtSlots = 4;
    // ★★★ §tx-admission TX3 (OWNER RULING 2026-08-02): a deferred BEACON carries the digest ids it advertised, so the
    // channel-digest commit can happen at the TRANSMITTER-ADMISSION boundary instead of at ring entry.
    // ⚠ THE RULING REVERSED THE EARLIER CALL, and the reason is worth keeping: the `reqpubkey_sent` ruling settled an
    // APP EVENT; digest retirement is an INDEPENDENTLY load-bearing state machine (`++bcn_ad_count`, and on horizon
    // `dirty = false`), so extending the app-event boundary to it was a design decision, not an implementation
    // detail. It now has its own ruling: **"sent" = accepted by the transmitter/DeviceHal** — the strongest boundary
    // this architecture can observe.
    // ★ WHAT THIS BOUNDARY IS NOT: a later `DeviceHal::pump_tx` radio-start error (`start_transmit` -> radio_error)
    //   drops the frame AFTER admission and is **outside** the guarantee. Do not describe it as covered.
    // ⓘ `digest_ids[0] == 0` terminates: a live channel id can never be 0 (`channel_msg_id_mint` packs origin >= 1
    //   into the high byte), and `channel_buffer_find(0)` therefore misses — so no count byte is needed.
    static constexpr uint8_t kDeferDigestIds = 3;   // == the digest select cap (emit_beacon picks <= 3)
    struct DeferredLbt { bool pending = false; uint8_t kind = 0; uint8_t len = 0; int16_t sf = 0;
                         uint32_t rts_flight_gen = 0;   // RTS staleness key (flight_gen, not the old 4-bit ctr_lo proxy)
                         uint32_t digest_ids[kDeferDigestIds] = {};   // §TX3: beacon only; 0 = unused
                         uint8_t buf[protocol::beacon_max_bytes] = {}; };
    DeferredLbt _deferred_lbt[kLbtSlots];
    // Cleanup #A redo: an over-budget RTS is duty-deferred in a DEDICATED slot (NOT the shared LBT ring — that reuse
    // was net-worse, review wgvbtirmu). One slot: there is only ever one pending_tx/flight. flight_gen staleness makes
    // the long (~1h) duty wait safe. kRtsDutyDeferTimerId fires rts_duty_defer_fire (re-check duty / hand / re-defer).
    uint32_t _flight_gen = 0;     // monotonic; bumped per new pending_tx (issue_send)
    struct RtsDutyDefer { bool pending = false; uint16_t len = 0; int16_t sf = 0; uint32_t flight_gen = 0;
                          uint8_t buf[16] = {}; };   // RTS pack is <=9 B (RTS_LEN 8 + M-broadcast 2)
    RtsDutyDefer _rts_duty_defer;
    // R4.5b on_radio_busy retry: a per-frame-type tag (echoed by the sim) lets on_radio_busy identify a blocked
    // TX (heap-free, no string label). The retry-eligible frames (CTS/DATA/ACK/NACK; RTS/beacon are NOT) are
    // STASHED so a busy-channel block re-issues them up to TX_DEFER_MAX_RETRIES. tx_stash keyed by the retry slot.
    static constexpr uint8_t kRetrySlots = 4;   // cts, data, ack, nack
    struct TxStashSlot { bool valid = false; uint16_t len = 0; int16_t sf = 0; uint8_t retries_left = 0;
                         uint32_t flight_gen = 0;   // L9: DATA slot — the EXACT pending_tx flight this DATA belongs to (re-arm guard). Was the 4-bit ctr_lo (dv:10271) whose 1/16 aliasing let a re-arm fire against a since-replaced flight; flight_gen is the monotonic per-flight identity (issue_send) so the match is exact.
                         // reissue_pending: a busy/duty re-issue timer is ARMED for this slot (vs. a stale clean-sent
                         // buffer that is `valid` but already on the air). layer_swap_blocked() gates on THIS, not
                         // `valid` — else a gateway's first cleanly-sent ACK leaves `valid` set forever + the layer
                         // swap never fires (the bridged DM on the other leaf never transmits). See node.cpp swap guard.
                         bool reissue_pending = false;
                         uint8_t buf[protocol::lora_max_frame_bytes] = {}; };
    TxStashSlot _tx_stash[kRetrySlots];
    // R3 data-plane state (single flight per node) — the pipeline arrays MOVED into LayerRuntime (Slice 2a).
    // kTxQueueCap stays a Node-level static constexpr (compile-time array dim, identical for every layer;
    // visible by unqualified name inside the nested LayerRuntime + in Node member fns — no per-layer state).
    static constexpr uint8_t kTxQueueCap = 8;

    // ======== LayerRuntime member TYPE defs — defined here for def-before-use; the INSTANCES live in LayerRuntime, ========
    //          NOT in Node (0 Node-layout impact). Struct-extraction to a private header is a later, separate slice.
    // F route-discovery dedup state (Lua route_request_seen / route_request_last). Members in LayerRuntime.
    // recent_ring.h contract: `t_ms` + `same_key` (the dedup key, defined ONCE for both the query and the mark).
    struct RReqSeen { uint8_t origin; uint8_t dst; uint64_t t_ms;       // relay flood-dedup
                      bool same_key(const RReqSeen& o) const { return origin == o.origin && dst == o.dst; } };
    struct RReqLast { uint8_t dst; uint8_t ttl; uint64_t t_ms; };      // per-dst origination rate-limit
    // Hash-locate id_bind table (Lua dv:4677): key_hash32 -> node_id, beacon-populated. Bounded array
    // (array sized at the protocol max; _cfg.cap_id_bind gates additions). One timestamp: id_bind_set
    // always carries the key, so last_seen == last_key_seen (the plain-refresh split lands with C.2). Member in LayerRuntime.
    struct IdBind { uint32_t key_hash32; uint64_t last_seen_ms; uint8_t node_id; uint8_t source; uint8_t confidence; };
    // §mobile 3c: a mobile's stable hash -> its home_node id (sender-side proxy cache; id_bind can't hold it). No bijection.
    struct MobileHomeBinding { uint32_t mobile_hash; uint64_t last_seen_ms; uint8_t home_id; uint8_t epoch = 0; uint8_t home_layer = 0; };  // §mobile 4a epoch (freshest-proxy wins) + §5b home_layer (the home's full layer_id, for cross-layer routing)
    // E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub. Immutable + hash-verifiable (ed_pub[:4]==key_hash32),
    // so a TYPE-5 owner answer is cached AUTHORITATIVE even relayed/cached-on-pass (can't decay). Member in LayerRuntime.
    struct PeerKey { uint32_t key_hash32; uint64_t last_seen_ms; uint8_t ed_pub[32]; uint8_t confidence; char name[32]; uint8_t name_len; bool peer_confirmed; };   // §1.3: name rides with the key — IMMUTABLE key, MUTABLE name (refreshed on every pubkey message). §S2: peer_confirmed = we've OPENED a SEALED frame from this peer (they hold our key) -> stop attaching INTRO to plaintext sends toward them. Set on e2e_open_trial success ONLY (never on a plaintext receipt).
    // §AB2: peer_name_set / peer_key_set / push_peer_key_cached / on_command's peername refusal all size the name by
    // protocol::peer_name_max instead of the bare literal `32` they used to repeat. Pin the two together so widening
    // `name[]` without widening the constant (or vice versa) fails the build rather than truncating silently.
    static_assert(sizeof(PeerKey::name) == protocol::peer_name_max, "node.h: PeerKey::name and protocol::peer_name_max disagree");
    // H hash-locate flood dedup (Lua hash_query_seen): per-(origin,key_hash32), hash_query_seen_ttl_ms window. Member in LayerRuntime.
    struct HashQuerySeen { uint8_t origin; uint32_t key_hash32; uint64_t t_ms; bool hard; bool want_pubkey;   // §2: WANT_PUBKEY is its own variant
                           bool team_scoped;   // ★ §team-parity T6/B: the PLANE discriminator (see below)
                           // §2: `hard` and `want_pubkey` are part of the KEY — a HARD (verify-on-use) or a
                           // WANT_PUBKEY query must NOT be suppressed by a prior plain/SOFT one's seen-entry.
                           // ★★ §team-parity T6/B (spec §3/T6 Part B, closing §10.3/§9-Q4): `team_scoped` is part of the
                           // KEY. Before T6 this ring was documented (LayerRuntime, "§P2-7 AUDIT") as safe *only by an
                           // UNWRITTEN role-exclusion invariant* — "no node today processes BOTH the static and the team
                           // H-flood plane". VERIFIED AT SOURCE, and the invariant is DEFEATABLE BY LIVE CONFIG, not
                           // merely fragile: handle_h returns before any mark for a static H iff `_cfg.is_mobile`
                           // (node_hashlocate.cpp:584) and for a team H iff `!same_team` (:603) — so a node with
                           // `team_id != 0 && !is_mobile` marks BOTH.
                           // ★★ UPDATED 2026-07-31 (§role-model / B28/R2): that config is now OUTLAWED, so this alias is
                           // UNREACHABLE BY CONFIG rather than merely keyed around — `set_team_id` DOES set is_mobile
                           // when the new team is non-zero (role_enforce, node_role.h), the NV boot restore normalises
                           // the same implication, and `cfg set mobile 0` REFUSES while in a team. (`cfg set team_id`,
                           // the other spelling this note used to name, was removed entirely — B27.) The `team_scoped`
                           // key below STAYS: it is the correct plane discriminator regardless, it costs zero bytes,
                           // and belt-and-braces beats resting a suppress-direction failure on a config invariant.
                           // The alias is not hypothetical either: `key_hash32` is a node's key, IDENTICAL on both planes
                           // (a dual member has one key, a static id and a team_local_id), so a team H and a static H for
                           // the SAME target collide as soon as the two queriers' 8-bit origins collide (§18) — and the
                           // failure direction is SUPPRESS: hash_query_seen_recently returns true and the H is never
                           // forwarded, so a locate dies silently instead of reaching the owner.
                           // Cost: ZERO bytes — `hard`/`want_pubkey`/`team_scoped` share the same 8-byte tail slot
                           // (1+3pad+4+8 then 3 bools + 5 pad = 24 B, unchanged), so sizeof(Node) does not move.
                           bool by_id;   // ★ §id-hash S4a: the KEY-SPACE discriminator (see below)
                           // ★★ §id-hash S4a (spec §4): `by_id` joins the key for the SAME reason `team_scoped` did,
                           // and the collision is arithmetic rather than hypothetical: `key_hash32` here holds the H
                           // frame's raw bytes 2-5, so a BY_ID query for **id 114** and a by-hash query for
                           // **0x00000072** are the same 32-bit value from the same origin. Without this bit one
                           // suppresses the other's FORWARD and a locate dies silently — the same failure DIRECTION
                           // team_scoped guards against. The canonical encoding (frame_codec.h
                           // `h_by_id_key_canonical`) is what makes the value itself unambiguous; this bit is what
                           // keeps the two key SPACES apart.
                           // Cost: still ZERO bytes — a FOURTH bool in the same 5-byte tail pad. MEASURED by the
                           // sizeof(Node) assert below (221024, unmoved) + offsetof, not inferred.
                           bool same_key(const HashQuerySeen& o) const {
                               return origin == o.origin && key_hash32 == o.key_hash32 && hard == o.hard
                                   && want_pubkey == o.want_pubkey && team_scoped == o.team_scoped
                                   && by_id == o.by_id; } };
    // ★ §id-hash S4a — THE ZERO-COST PLACEMENT, MEASURED HERE RATHER THAN INFERRED (and native-only: the board proof
    // is the per-env RAM diff in the slice report, which reads +0/+0/+0). Layout: origin@0, pad@1-3, key_hash32@4,
    // t_ms@8, then hard@16 / want_pubkey@17 / team_scoped@18 and FIVE bytes of tail pad. `by_id` takes @19, the first
    // of those five (⚠ I first WROTE 18 from inference and the assert below failed the build — which is the assert
    // earning its place, and the reason this line quotes a measurement). The record stays 24 B, so the
    // `[cap_hash_query_seen=64] x MR_N_LAYERS` array does not grow. NINTH application of the radio_freq_mhz /
    // team_hop_cap / HashQuerySeen.team_scoped / T5-PeerLiveness / T-K1 padding-placement rule.
    // ⓘ NOT vacuous: change `24` to `32` here, or move `by_id` above `key_hash32`, and the build fails.
    static_assert(sizeof(HashQuerySeen) == 24, "node.h: HashQuerySeen grew — the §S4a by_id bool must fit the existing tail pad");
    static_assert(offsetof(HashQuerySeen, by_id) == 19, "node.h: HashQuerySeen::by_id moved out of the tail pad");
    // Peer-liveness + freshness plane (routing-liveness port, Lua dv:3986-4545): per-next-hop RTS/ACK-timeout
    // accounting -> suspect/silent/dead tiers (each with an expiry), + dest_seen for next-hop freshness. Bounded
    // LRU table per LayerRuntime (the direct-neighbour set). node_id 0 = empty slot.
    // §P4 gossip: suspect_advertise_until_ms / dead_advertise_until_ms hold the GOSSIP window (what to put in our BCN
    // suspect-TLV), set ONLY by LOCAL rts_timeout evidence (mark_peer_suspect remote_src==0). REMOTE-learned tiers write
    // the *_until_ms routing fields but NOT these -> a node never re-gossips a suspicion it heard (anti-storm, dv:1388).
    // ★★★ §team-parity T5 (spec §3/T5) — team_bidi_state / team_bidi_confirmed_s ARE the TEAM-plane bidirectionality
    // plane: the team mirror of the STATIC _link_bidi[256] + _link_bidi_confirmed_ms[256] (below, 2304 B), which a
    // 256-entry team copy could never be on an nRF52840. They are MEANINGFUL ONLY in _team_liveness (keyed by
    // team_local_id); in _peer_liveness they are dead space — exactly as suspect_advertise_until_ms /
    // dead_advertise_until_ms are dead in _team_liveness ("A team slot NEVER carries advertise fields",
    // node_routing.cpp:644). Same precedent, in this same struct. The `team_` prefix is deliberate and load-bearing as
    // documentation: a reader who finds `_peer_liveness[i].team_bidi_state` has found a bug, because the static plane's
    // answer is _link_bidi[node_id]. ⇒ invariant I8 ("team bidi state is plane-private and never indexes _link_bidi")
    // is enforced STRUCTURALLY here — the team state is not an index into anything node_id-shaped.
    // ★★ PLACEMENT IS THE WHOLE COST OF T5's STATE, AND IT IS ZERO — measured, not argued. node_id occupies byte 0 and
    // rts_timeouts needs 2-alignment, so byte 1 was pure padding; first_timeout_ms needs 8-alignment, so bytes 4..7 were
    // pure padding. The two new members take exactly those five holes ⇒ sizeof(PeerLiveness) is 72 BEFORE and 72 AFTER,
    // so neither _peer_liveness[cap_peer_liveness=64] nor _team_liveness[cap_team_liveness=16] grows a byte on any
    // layer and sizeof(Node) does NOT move. Fourth application of the radio_freq_mhz / team_hop_cap /
    // HashQuerySeen.team_scoped placement rule.
    // ⚠ THE SPEC'S §3/T5 SHAPE WAS MEASURED AND DECLINED, with its arithmetic CONFIRMED: a dedicated
    // `_team_bidi[16]` of {uint8_t id; uint8_t state; uint32_t confirmed_s} is 8 B/entry × 16 = 128 B per layer (the
    // u64-ms form is 16 B × 16 = 256 B — alignment does NOT collapse the two, so the spec's "u32 halves it" is right).
    // Declined because (a) 128 B × MR_N_LAYERS of nRF52840 RAM buys nothing this fit does not, and (b) it would be a
    // SECOND 16-entry team_local_id-keyed table sitting beside _team_liveness with the identical cap, identical
    // self-slotted LRU and identical lifetime — the U1 field-drop fork (add a teammate to one, forget the other).
    // SECONDS, not ms: bidi_confirm_ttl_ms is 1200000 (protocol_constants.h:207) so second granularity is ample, and
    // u32 seconds is what the 4-byte hole holds. 0 = never confirmed (same sentinel as _link_bidi_confirmed_ms).
    // ⚠ NOT cleared by clear_liveness_tiers, DELIBERATELY: hearing a frame from a teammate proves it is ALIVE, which is
    // liveness; it does not prove it hears US, which is bidi. MF6 also forbids one_way decaying on mere staleness. The
    // ONLY reset is a fresh slot (peer_liveness_slot's `tbl[best] = PeerLiveness{}` on create/LRU-evict), which is
    // correct — a re-used slot is a different peer.
    struct PeerLiveness { uint8_t node_id; uint8_t team_bidi_state; uint16_t rts_timeouts; uint32_t team_bidi_confirmed_s;
                          uint64_t first_timeout_ms;
                          uint64_t suspect_until_ms; uint64_t silent_until_ms; uint64_t dead_until_ms; uint64_t dest_seen_ms;
                          uint64_t suspect_advertise_until_ms; uint64_t dead_advertise_until_ms;
                          uint64_t e2e_ack_spoof_until_ms = 0; };   // anti-spoof: while now < this, the peer's RTS_FLAG_E2E_ACK is IGNORED (backstop re-applies)
    // ★★ §team-parity T5 PLACEMENT TRIPWIRE — deliberately NOT `#ifdef MESHROUTE_NATIVE`. The 0-byte claim is an
    // ALIGNMENT claim, so it has to be proven on every ABI that compiles this header (ARM/AAPCS and Xtensa both align
    // uint64_t to 8, which is what makes the two holes exist there too), not just on the host. If a future member add or
    // reorder spills out of the holes this fails the BOARD build loudly instead of silently costing
    // 8 B × (cap_peer_liveness 64 + cap_team_liveness 16) × MR_N_LAYERS of RAM. If a reorder is intentional, re-measure
    // and update these three numbers together with the sizeof(Node) baseline below.
    static_assert(sizeof(PeerLiveness) == 72, "PeerLiveness grew: the T5 team-bidi fields no longer fit its padding holes — re-measure the RAM cost before updating this");
    static_assert(offsetof(PeerLiveness, team_bidi_state)       == 1, "T5: team_bidi_state left byte 1 (the hole after node_id)");
    static_assert(offsetof(PeerLiveness, team_bidi_confirmed_s) == 4, "T5: team_bidi_confirmed_s left bytes 4..7 (the hole before the 8-aligned first_timeout_ms)");

    // ======== PARKED-SEND / hash-resolve + L2c redirect + deleg-ack (Node-global) ========
    // send-by-hash DMs parked awaiting a hash-bind resolution (D); drained by on_hash_bind_response, aged on the timer.
    // is_redirect=true => an L2c misdelivered DM held for FORWARD (not re-send): `body`=the full inner (incl.
    // DST_HASH), and origin/ctr/ctr_lo are preserved so the resolution forwards it identity-intact. The redirect
    // leg is re-budgeted as a fresh route (originator-style), so no hop fields are carried. resolved_id==our id
    // at drain = a CONFIRMED collision (the heal trigger, design §7.1).
    struct ParkedSend { uint32_t key_hash32; uint64_t parked_at_ms; uint8_t flags; uint8_t body_len;
                        uint32_t reply_to_hash = 0;   // §mobile delegate: the HOME re-originating for its mobile parks with the mobile's hash -> SOURCE_HASH on drain, so the target's reply routes back to the mobile (0 = our own hash)
                        bool is_redirect = false; bool is_resolve = false; bool cross_layer = false; uint8_t origin = 0; uint16_t ctr = 0; uint8_t ctr_lo = 0;
                        uint8_t type = 0;   // S1/M7a: a redirect's DataType (E2E_ACK/H_ANSWER); preserved across park+heal so the forwarded frame keeps its type (only meaningful when is_redirect)
                        CryptIntent crypt = CryptIntent::def;   // M3 (2026-07-04): the per-message crypt intent stamped at park time so a `sendhashx`(crypt=on) parked awaiting a binding still flies CRYPTED on drain (never silently downgrades to cleartext, node.h invariant); threaded into both drains' do_send
                        uint8_t nonce_seed[8] = {};   // §1c: a CRYPTED redirect's originator seed (preserved across the park+heal); zero for a plain send (re-sealed on drain)
                        uint16_t mobile_ctr = 0;      // §mobile reverse-ack: a delegated re-origination carries the MOBILE's original ctr (ctr_M) so the drain records ctr_H->ctr_M (0 = not a delegated send)
                        // §F-SL-1: bounded jittered H re-flood while parked (send_by_hash origination path only). reflood=false =>
                        // this entry never re-floods (a redirect / resolve / cross-layer park keeps today's single-flood behaviour).
                        bool     reflood = false; bool reflood_hard = false; Plane reflood_plane = Plane::AUTO;
                        uint8_t  reflood_count = 0; uint64_t reflood_at_ms = 0;   // next scheduled re-flood (wall clock); the scan re-arms the shared timer to the earliest
                        uint8_t body[protocol::max_payload_bytes_hard_cap]; };   // is_resolve: notify-only diag (a `resolve`), no body. cross_layer (Slice 4d): a send_layer awaiting (node_id,target_layer)
    ParkedSend _parked_sends[protocol::cap_parked_sends] = {};
    // §mobile reverse-ack (delegated): {acker (the static target's id), ctr_H} -> ctr_M. Populated when THIS home
    // re-originates a hosted mobile's delegated send under its OWN ctr (ctr_H); consumed when the target's E2E-ack (for
    // ctr_H) comes home -> translate to ctr_M so the last-miled ack matches the ctr the mobile is waiting on. A small TTL
    // ring; empty on a node that hosts no mobiles -> inert (s18 byte-identical). See deleg_ack_put/deleg_ack_translate.
    struct DelegAck { uint32_t mobile_hash = 0; uint16_t ctr_h = 0; uint16_t ctr_m = 0; uint64_t ts_ms = 0; bool valid = false; };
    static constexpr uint8_t kDelegAckCap = 8;
    DelegAck _deleg_acks[kDelegAckCap] = {};
    // ★ E2E-ack DEADLINE ring (shelf item (i), 2026-07-24): sends awaiting their DATA_TYPE_E2E_ACK. ARMED (emit-free) when an
    // app DM with DATA_FLAG_E2E_ACK_REQ mints its ctr; CLEARED (emit-free) on the matching send_e2e_acked; EXPIRED ->
    // send_failed{e2e_ack_timeout}. NOT mobile/team-gated (a static -a send arms too). The MATCH mirrors what send_e2e_acked
    // carries {dst=acker origin, ctr=acked, sender_hash}: an XL send's ack is CROSS_LAYER (sender_hash set) -> key==sender_hash;
    // a same-layer id send's ack is same-layer (sender_hash==0) -> dst==acker origin; a delegated/wildcard entry (key==0)
    // matches on ctr alone (the delegated reverse-ack can arrive either same-layer or XL — see node_mac.cpp e2e_ack_arm).
    struct PendingE2eAck {
        uint32_t key    = 0;      // XL: the far-target key_hash32 (== send_e2e_acked.sender_hash). Same-layer id send: the dst id. 0 = wildcard (delegated) -> match by ctr only.
        uint16_t ctr    = 0;      // the minted ctr the app correlates on (== the acked ctr in the returning DATA_TYPE_E2E_ACK)
        uint8_t  dst    = 0;      // same-layer id send: the dst id, echoed on the send_failed{e2e_ack_timeout} push (0 for hash/XL/delegated)
        bool     is_xl  = false;  // the ack returns CROSS_LAYER (sender_hash carried) -> match by key==sender_hash; else same-layer (sender_hash==0)
        bool     used   = false;
        uint64_t deadline_ms = 0;
    };
    static constexpr uint8_t cap_pending_e2e_acks = protocol::cap_pending_e2e_acks;
    PendingE2eAck _pending_e2e_acks[cap_pending_e2e_acks] = {};
    // ★★★ §id-hash S4b (spec §5) — THE `resolve-id-for-pubkey` INTENT RING. The state that makes `reqpubkey <id>` ONE
    // command again when the id has no binding: stage 1 asks "who owns id N?", this remembers that we asked and WHY,
    // and the answer's arrival fires the ordinary HARD WANT_PUBKEY query by the returned hash (spec §5 steps 1-3).
    // Node-GLOBAL, like its `_pending_e2e_acks` neighbour and for the same reason: the answer can arrive on any layer.
    //
    // ★★ THIS IS NOT THE AUTO-RESOLUTION `§no-auto-reqpubkey` FORBIDS — the reasoning lives at the escalation site
    // (node_hashlocate.cpp, id_pubkey_intent_consume) so a reader meets it where the second query is actually emitted.
    //
    // ⓘ `id != 0` IS THE USED SENTINEL, deliberately instead of the `bool used` its neighbour carries. A by-id query
    // key is canonical-gated (`h_by_id_key_canonical`: never 0, never 255), so 0 is structurally impossible for a live
    // intent — and one field that cannot disagree with another beats two that can. `PendingE2eAck` needs its flag only
    // because its key 0 is a legal wildcard.
    struct PendingIdPubkey {
        uint64_t deadline_ms = 0;   // armed at now + protocol::id_pubkey_intent_ttl_ms; swept by age_out_pending_id_pubkey
        uint8_t  id          = 0;   // the queried id IN ITS OWN PLANE'S SPACE (static node_id / team local id) — 0 = slot free
        uint8_t  plane       = 0;   // Plane::TEAM(1) / Plane::GLOBAL(2), never AUTO: the plane is resolved before arming
    };
    // ⚠ MEASURED, NOT INFERRED (D2). 16 B/slot: the 8-aligned `deadline_ms` first, then the two bytes in the 6-byte
    // tail pad it already forces — so the ring costs exactly 4 x 16 = 64 B and, being a multiple of 8, opens NO hole
    // at its insertion point (immediately after the 8-aligned end of `_pending_e2e_acks`, `_peer_loc`'s own precedent).
    // Reversing the field order — the two bytes FIRST — measures 16 as well, so the placement is not load-bearing here;
    // it is written this way because `deadline_ms` is the field the sweep reads.
    static_assert(sizeof(PendingIdPubkey) == 16, "node.h: PendingIdPubkey grew — x cap_pending_id_pubkey, and sizeof(Node) has moved");
    static_assert(offsetof(PendingIdPubkey, id) == 8, "node.h: PendingIdPubkey::id left the tail pad after deadline_ms");
    static constexpr uint8_t cap_pending_id_pubkey = protocol::cap_pending_id_pubkey;
    PendingIdPubkey _pending_id_pubkey[cap_pending_id_pubkey] = {};
    // ★★★ §AB4 — RETAINED PEER LOCATION (address-book spec 2026-07-29 §2.7/§2.7.1, owner-ruled 2026-07-31).
    // key_hash32 -> that peer's last known position, so `peers` / `nameof` can show it. Node-GLOBAL (not LayerRuntime,
    // unlike its _peer_keys/_id_bind/_team_keys neighbours): a key_hash32 is a layer-INDEPENDENT identity, so a
    // position learned on one leaf is equally true on the other — per-leaf copies would cost 2x the RAM to hold two
    // answers to a question that has one. Nothing here is #if MR_FEAT_TEAM: the live source is a SEALED DM, which a
    // static-only build (gateway) receives exactly as a team member does.
    //
    // ★★★ RAM ONLY — DELIBERATELY VOLATILE. NO NV RECORD, AND THE REASONS ARE HERE SO NO LATER SLICE "COMPLETES" IT:
    //   (1) A STALE POSITION IS WORSE THAN NONE. An app that renders a three-hour-old fix as current is actively
    //       misleading — and a reboot-restored position has no upper bound on its staleness at all.
    //   (2) A CAPTURED OR STOLEN NODE MUST NOT YIELD EVERY TEAMMATE'S LAST KNOWN POSITION. Keys in flash are a
    //       deliberate, bounded exposure; a movement history is not.
    // AB1 persists names and `authoritative` keys precisely because those are stable facts. Location is THE deliberate
    // exception, and §1.4's "one table, lossy backup" picture stays true. ⇒ src/device_nv.h's PeerRec gains NOTHING.
    //
    // ★★ AND IT IS NOT `PeerKey`, NOT `_team_keys` — both obvious homes are wrong, recorded so neither is re-taken:
    //   • NOT PeerKey: under owner ruling O5 a CHANNEL-sourced position must be storable with NO ed_pub for that hash,
    //     and peer_key_set *verifies* ed_pub[:4] == key_hash32, so a keyless row would fight a live invariant.
    //   • NOT _team_keys: the §id-bind-plane slice established three reasons — it has no confidence dimension, it FEEDS
    //     TEAM-DAD MEDIATION (seeding it from non-beacon traffic manufactures spurious DENYs), and it is a beacon-fed
    //     evict-oldest LRU whose last_seen_ms means "heard NOW", not "positioned then".
    // ⇒ a dedicated ring keyed by key_hash32 (§1.2: ids are addresses, the hash is the identity).
    //
    // ★ NOT recent_ring.h either, and this one is a REFUSED FORCED FIT rather than an oversight: that header's four
    // steps (scan / refresh / append / evict-oldest) are exactly right, but its ENTRY CONTRACT mandates `uint64_t t_ms`
    // and this record's whole size budget rests on a 4-byte stamp — measured, the u64 form is 32 B/slot vs 20 B, i.e.
    // +192 B of nRF52840 RAM to share ~10 lines of template. The MEANING differs too: recent_ring is a "have I seen
    // this recently" SUPPRESSION window with a TTL and an age-out sweep, whereas this is a retained VALUE whose stamp
    // is a DISPLAYED age and which must never expire behind the app's back. There is also no duplicated key predicate
    // to unify — that doubled `…_recently`/`mark_…` predicate is the entire reason that header exists.
    struct PeerLoc {
        uint32_t   key_hash32;   // the identity. 0 ⇒ unused slot (the _peer_loc_n prefix is the live region)
        int32_t    lat_e7;       // 1e-7 degrees, the same scale the wire and NodeConfig::lat_e7 use
        int32_t    lon_e7;
        // ★ SECONDS, not milliseconds — `_hal.now()` is ms and this is `now()/1000`. Two reasons: the 4-byte stamp is
        // what keeps the record at 20 B (a u64 ms stamp measures 32 B — see the recent_ring note above), and the only
        // consumer is `loc_age_s`, a human-facing age where sub-second precision is meaningless.
        // ★ ROLLOVER: uint32 seconds spans ~136 years of uptime, so on a monotonic since-boot clock the wrap is
        // UNREACHABLE and no wrap handling is warranted. peer_loc_find still clamps a now < t_s reading (a test that
        // rewinds TestHal, or a clock that ever moved backwards) to the MAXIMALLY STALE answer, never to 0 — 0 would
        // render a garbage position as current, which is failure mode (1) above.
        uint32_t   t_s;
        // ★★ THE TRUST ANCHOR (spec §2.7.2). It is stored, not derived, and it exists FROM THE START even though only
        // `peer` can occur today — so CL2 adds a SOURCE, never a schema change, and the weaker group-anchored claim can
        // never be silently rendered as the stronger pairwise one.
        PeerLocSrc src;
        // ★ NAMED, not implicit. sizeof(PeerLoc) is 20 either way (measured: 16 without `src`, 20 with, offsetof(src)==16),
        // but IMPLICIT tail padding is INDETERMINATE after `PeerLoc{}` — so any memcmp-style comparison over the record
        // would be unsound. Naming it makes those 3 bytes zero-initialised like every other member. (AB1's lesson: it
        // briefed a 70 B record and measured 72.) ⇒ 20 B x cap_peer_loc(16) = 320 B, NOT the 256 B §2.7.1 predicted —
        // that figure pre-dates the rescope that added `loc_src`, and the two cannot both hold.
        uint8_t    reserved[3];
    };
    // ★ PINNED AT THE DEFINITION, per-TARGET — the PeerLiveness == 72 precedent (node.h:1646), and better than a native
    // test could be: this fires on every board toolchain, not just native. 20 B x cap_peer_loc = the exact 320 B the
    // sizeof(Node) ledger accounts for, measured on all six flag-sets. `offsetof(src) == 16` is what proves `reserved`
    // occupies the REAL tail hole rather than adding a fifth word: widen any field, or drop `reserved`, and this fails
    // the build instead of silently costing (or indeterminately zeroing) 16 slots' worth of RAM.
    static_assert(sizeof(PeerLoc) == 20 && alignof(PeerLoc) == 4 && offsetof(PeerLoc, src) == 16,
                  "node.h: PeerLoc's layout moved — re-measure the ring's RAM cost (20 B x cap_peer_loc) and update "
                  "the sizeof(Node) ledger; and keep the tail padding a NAMED member, never implicit");
    static constexpr uint8_t cap_peer_loc = protocol::cap_peer_loc;
    PeerLoc    _peer_loc[cap_peer_loc] = {};
    uint8_t    _peer_loc_n = 0;
    uint8_t    _parked_sends_n = 0;
    // L2c redirect-suppression ring: a misdelivered DM we've already redirected for this hash recently,
    // so a still-poisoned binding (collision unhealed) can't re-trigger an endless redirect→deliver→redirect.
    struct L2cRedirect { uint32_t key_hash32; uint64_t t_ms;
                         bool same_key(const L2cRedirect& o) const { return key_hash32 == o.key_hash32; } };
    L2cRedirect _l2c_redirect[protocol::cap_l2c_redirect] = {};
    uint8_t     _l2c_redirect_n = 0;

    // ======== JOIN / DAD id-assignment (Node-global — node_join.cpp; also _join_denied/_mediated_recent below, past the mobile block) ========
    // node_id auto-assignment (DAD + heal) — node_join.cpp; design 2026-06-05-node-id-auto-assignment-design.md.
    bool     _joined = false;                                    // adopted a node_id via DAD (vs cfg/NV-provisioned)
    bool     _join_listen_pending = false;                       // a join was requested; listening before the first claim (L1)
    uint8_t  _claim_epoch = 0;                                   // VESTIGIAL (key-only tiebreak): reserved on wire/NV, not consulted
    struct JoinClaim { bool active; uint8_t proposed; uint32_t key_hash32; uint8_t claim_epoch; uint8_t nonce; uint64_t started_ms; };
    JoinClaim _join_claim{};                                     // the single in-flight claim (active=false when none)

    // ======== MOBILE-MEMBER identity (Node-global, #if MR_FEAT_MOBILE — roaming-endpoint plane; compiles out on static/gateway) ========
    // §mobile 2b (mobile-side registration): a mobile has ONE attachment (identity-level, single-layer). DORMANT unless
    // _cfg.is_mobile — the FSM timer is armed only for a mobile, so a static node never touches any of this.
    // §featuresplit: the whole mobile-MEMBER (roaming endpoint) plane compiles out on a static/gateway build (MR_FEAT_MOBILE=0);
    // the header stubs the accessors + FSM to inert, so the static routing plane is untouched.
#if MR_FEAT_MOBILE
    struct MyMobileReg {
        bool     active = false;              // registered to a host?
        uint8_t  home_id = 0;                 // the host's node_id (our registrar / home)
        uint8_t  my_local_id = 0;             // our host-assigned local-id (== _node_id once adopted)
        uint32_t home_key_hash32 = 0;         // stable home identity (home-lost / redirect)
        uint8_t  home_leaf_id = 0;            // the leaf we registered on
        uint16_t epoch = 0;                   // §17 registration epoch (mobile-incremented per (re)register)
        uint64_t last_heard_home_ms = 0;      // last BCN from home_id (home-lost timeout)
    };
    MyMobileReg _my_mobile_reg{};
    struct OfferCand { uint8_t responder_id; uint32_t responder_hash; uint8_t proposed_local_id; float snr_db;
                       uint8_t leaf_id; uint8_t data_sf_bitmap; };   // §mobile: the HOST's leaf (from the OFFER) — adopted on registration. data_sf_bitmap is ADVISORY (F-SF-1): the mobile keeps its OWN configured sf_list; this byte only feeds the `mobile_sf_list_mismatch` diagnostic
    OfferCand _mobile_offers[protocol::cap_mobile_offers] = {};   // OFFERs collected during a DISCOVER window
    uint8_t   _mobile_offers_n = 0;
    uint32_t  _mobile_backoff_ms = 0;                             // exp-backoff when no host answers (0 = first try)
    uint8_t   _mobile_scan_idx = 0;                              // §mobile 5a: which scan-set PHY the home-lost mobile is currently DISCOVERing on
    LayerRecord _learned_layers[protocol::cap_learned_layers] = {};   // §mobile 5a: neighbouring layers pulled from a gateway (candidate cross-layer PHYs, dedup by composite id)
    uint8_t   _learned_layers_n = 0;
    uint64_t  _learned_layers_ms = 0;                           // §mobile 5a: last directory refresh (TTL)
    uint64_t  _learned_layers_seen_ms[protocol::cap_learned_layers] = {};   // §3-A.6/P2-6: per-entry last-seen -> evict-STALEST when full (was evict-slot-0, which could clobber the freshest)
    // §S6 presence plane (mobile side): the probe/check FSM that REPLACES the periodic re-CLAIM + layer poll.
    uint8_t   _presence_miss     = 0;                           // consecutive unanswered probes (k_miss -> HOME LOST)
    uint32_t  _presence_T_ms     = protocol::presence_check_base_ms;   // current dynamic check period (quality-driven)
    uint8_t   _presence_my_tier  = protocol::presence_q_ok;     // my link tier from the last roster
    uint8_t   _presence_dir_epoch = 0;                          // last-seen layer-directory aggregate (pull on change)
    bool      _presence_dir_epoch_seen = false;                 // have we seen ANY roster dir_epoch yet
    bool      _presence_prescan  = false;                       // weak/critical -> collect candidate homes from beacons/rosters
    bool      _presence_key_confirmed = false;                  // §S6 A.4: home confirmed our key (roster has_key=1) -> stop attaching ed_pub to probes
    bool      _presence_reg_confirmed = false;                  // §S6: home confirmed our REGISTRATION (our hash seen in ITS roster) — else a lost CLAIM is re-sent (replaces the retired reclaim keepalive's heal role)
    bool      _mobile_arm_once = false;                         // §autoregister ruling (2026-07-21): one-shot manual `mobile register` arm — consumed by the DISCOVER half when mobile_autoregister is OFF (fits the existing bool-run padding; sizeof(Node) unchanged)
    uint64_t  _last_adopt_ms     = 0;                           // §S6.4-C dwell anchor (last (re)adopt)
    uint64_t  _presence_last_pull_ms = 0;                       // D6 safety-pull clock
    int16_t   _presence_home_rx_q4 = 0;                         // §S6/D14: my RX EWMA (Q4) of my HOME's frames (home->me direction; paired with _presence_my_tier = me->home)
    // §S6.4-C candidate home. D14 bidirectional: snr_q4 = my RX of its roster/beacon (cand->me); echo_tier = its echo of MY probe (me->cand), 0xFF = unknown. Selection ranks by the WORSE of the two.
    struct PresenceCand { uint8_t home_id; uint8_t home_layer; int16_t snr_q4; uint8_t echo_tier; uint64_t first_seen_ms; uint64_t last_seen_ms; bool incompatible = false; };  // §D16: incompatible = heard a wrong-wire_version roster from this home -> never DISCOVER at it
    PresenceCand _presence_cand[protocol::cap_presence_candidates] = {};   // §S6.4-C overheard candidate homes (strongest-sustained wins)
    uint8_t   _presence_cand_n   = 0;
#endif
    struct DeniedId { uint8_t id; uint64_t t_ms;                 // a slot that lost a claim/heal (§13: 1-day TTL)
                      bool same_key(const DeniedId& o) const { return id == o.id; } };
    DeniedId _join_denied[protocol::cap_join_denied] = {};
    uint8_t  _join_denied_n = 0;
    struct MediatedRecent { uint8_t node_id; uint32_t loser_hash; uint64_t t_ms;      // L2a: suppress per-(id,loser) re-DENY
                            bool same_key(const MediatedRecent& o) const { return node_id == o.node_id && loser_hash == o.loser_hash; } };
    MediatedRecent _mediated_recent[protocol::cap_mediated_recent] = {};
    uint8_t        _mediated_recent_n = 0;
    // Q REQ_SYNC plane state (node_query.cpp). _last_req_sync_tx_ms rate-limits the originator (dv:8035);
    // _q_responded is the responder dedup ring (key opcode|src|dest, ttl q_respond_ttl_ms) — Lua refuses on
    // cap-full, we evict-oldest (matches the F-dedup idiom; equivalent below cap, robust for a long-running
    // device). _sync_pending is the bounded jitter-response ring (Lua: an unbounded table of after()-closures;
    // one slot per requester, fired by kSyncResponseTimerId+slot). Both arrays MOVED into LayerRuntime (Slice 2b):
    // keyed by a REMOTE leaf-local id (q.src / requester), so a gateway's two leaves must NOT share them
    // (Principle 5 — else node-5@leafA aliases node-5@leafB and the gateway drops one leaf's sync reply).
    uint64_t _last_req_sync_tx_ms = 0;   // self-state (our own last REQ_SYNC tx) — stays Node-global, not per-layer
    uint64_t _last_config_pull_tx_ms = 0;   // R6.2: rate-limit our CONFIG_PULL tx
    uint16_t _max_seen_epoch = 0;           // R6.3 §4.1: highest config_epoch seen for OUR lineage (a write = max_seen+1)
    uint64_t _last_join_refused_ms = 0;     // R6.3 §7c: rate-limit the join_refused{wire_version} push
    struct QResponded { uint8_t opcode; uint8_t src; uint8_t dest; uint64_t t_ms;
                        bool same_key(const QResponded& o) const { return opcode == o.opcode && src == o.src && dest == o.dest; } };
    // §B4 `team_plane`: which plane's pull this slot answers (team_sync vs req_sync), carried from handle_q's opcode
    // dispatch so sync_response_fire's `rt_total` describes the SAME table the scheduler gated on. It must be STORED,
    // not re-derived at fire time — a homed member is team-active while answering a static REQ_SYNC (C3). Costs 0 B:
    // it lands in the alignment pad the three existing bools already share before the 8-aligned uint64_t pair, so
    // sizeof(SyncPending) stays 24 and sizeof(Node) is unchanged (the node.h assert is the tripwire).
    struct SyncPending { bool active; bool suppressed; uint8_t requester; bool requester_mobile;
                         bool team_plane; uint64_t requested_at; uint64_t fire_at; };
    // Channel-message gossip plane state + dedup maps + the originator ring — MOVED into LayerRuntime (Slice 2a;
    // struct defs ChannelEntry/FloodState/etc. are above the channel method decls; OrigEvent/OrigRing below).
    // R4.4 originator anti-spam: per-sender sliding-window ledger of overheard RTS/CTS. kind: 0=rts, 1=cts.
    // FIXED RING (not a std::vector) — the old map-of-vectors rebuilt a vector on every overheard frame
    // (alloc/free per frame), which fragments the nRF52 heap; this keeps the events in a fixed in-struct
    // array (no per-frame heap), evicting the oldest on overflow. Insertion-ordered so the dedup-FIRST
    // refresh still matches the Lua ipairs scan. The std::map (sender -> ring) stays — its node alloc is
    // once per NEW sender (bounded by neighbours), not per frame (the accepted determinism relaxation).
    struct OrigEvent { uint64_t t; uint8_t kind; uint8_t ctr_lo; uint32_t air; };
    struct OrigRing  { OrigEvent ev[protocol::cap_originator_events]; uint8_t count = 0; };
    // _per_sender_originator MOVED into LayerRuntime (Slice 2a).

    // ---- LayerRuntime (2026-06-12-gateway-dual-layer-design.md §2) -----------------------------------------
    // Per-layer (per-leaf) runtime state. A normal node has n_layers=1 and only _layers[0] is used; a gateway
    // (later slices) has 2 EQUAL layers and swaps _active at each window switch. Slice 2a is a PURE NO-OP hoist:
    // these members moved here VERBATIM (initializers preserved), every reader redirected through _active->.
    // The array dimension is MR_N_LAYERS (protocol_constants.h): 1 on a leaf build (the array is one element —
    // identical RAM to the pre-dual-layer firmware), 2 only on [env:gateway]. on_init REFUSES n_layers==2 when
    // MR_N_LAYERS<2, so _layers[1] is never reached on a 1-element build (audited: 405 reads all go via _active=&_layers[0]).
    // Nested struct so the in-class helper struct defs (RtEntry, TxItem, PendingTx, ..., IdBind, ChannelEntry,
    // FloodState, OrigRing, ...) stay visible by unqualified name. Node is never copied, so the raw _active
    // pointer + default member initializer is safe.
    struct LayerRuntime {
        // ============ PER-LEAF runtime state — one instance per _layers[i]; _active selects the current leaf (a leaf build ============
        // ============ has 1, a gateway 2, non-aliasing). By-concern sections (cleanup 2026-07-15); member TYPE defs are above. ============

        // ==== ROUTING — static DV plane ====
        // Routing table (DV).
        RtEntry  _rt[protocol::cap_routes];
        uint8_t  _rt_count = 0;       // distinct dests, kept sorted ascending by dest
        // §mobile 6.2: a SEPARATE team-plane DV table (a teammate's LOCAL id can collide with a static global id — §18 —
        // so the two planes MUST NOT share `_rt`). A team mobile (is_mobile+team_id) learns/advertises/routes here; a
        // static node / lone mobile leaves it empty (byte-identical). Same RtEntry + the same DV core (table-param).
        // ==== ROUTING — team DV plane (#if MR_FEAT_TEAM; a team local-id can collide a static global id → §18 keeps them SEPARATE) ====
#if MR_FEAT_TEAM   // §featuresplit: the team plane compiles out on a static-only build (gateway) -> frees ~45 KB (_rt_team ×2)
        RtEntry  _rt_team[protocol::cap_routes] = {};
        uint8_t  _rt_team_count = 0;
        uint8_t  _team_peer[32] = {};   // 256-bit set of KNOWN same-team peers (by beacon src) — mirror _mobile_peer; read by is_team_peer
        // §enc: a same-team peer's key_hash32 (its beacon carries it — we were DROPPING it for is_mobile beacons). A
        // team-SCOPED id->key map, NEVER _id_bind (the static plane, §18). Lets an ENCRYPTED send BY team_local_id derive
        // DST_HASH (the pubkey still arrives via the team-scoped WANT_PUBKEY, cached by this same hash). Empty for a
        // static/lone node (team_id==0) -> s18-inert.
        // ★★ §id-hash S3 (spec 2026-08-01 §3-D2): the CONFIDENCE LADDER _id_bind has always had and this table never
        // did. `IdBind` is {key_hash32, last_seen_ms, node_id, source, confidence} — mirrored here field for field, and
        // REUSING IdBindSource/IdBindConf rather than inventing a third enum (U1): the two tables answer the same shape
        // of question (an id is an ADDRESS, not a commitment — spec §2), so they must not carry different vocabularies.
        // ⓘ The zero defaults are `{IdBindSource::self, IdBindConf::claimed}` and the CLAIMED half is deliberate: an
        //   unwritten row must never read as first-hand. (`_team_keys_n` bounds validity, so this is belt-and-braces.)
        // ⚠ PLACEMENT IS THE WHOLE COST STORY and it is asserted below, not inferred: both bytes land in the 3-byte
        //   hole that already sat between `id` and the 4-aligned `key_hash32`, so sizeof(TeamKey) stays 16.
        struct TeamKey { uint8_t id = 0; uint8_t source = 0; uint8_t confidence = 0; uint32_t key_hash32 = 0; uint64_t last_seen_ms = 0; };
        // ★ MEASURED, not inferred (spec §3-D2's "⚠ MEASURE sizeof(TeamKey): offsetof, not inference"). The two
        // offsets are what prove the bytes went into the EXISTING hole; sizeof alone could not tell a free placement
        // from a lucky tail pad. If a future field pushes this to 24 the cost is 8 B x 16 slots x MR_N_LAYERS.
        static_assert(sizeof(TeamKey) == 16, "node.h: TeamKey grew — the §id-hash S3 ladder was supposed to be FREE (x16 slots x MR_N_LAYERS)");
        static_assert(offsetof(TeamKey, source) == 1 && offsetof(TeamKey, confidence) == 2,
                      "node.h: TeamKey's ladder bytes left the id-adjacent hole — re-measure the cost before accepting");
        static_assert(offsetof(TeamKey, key_hash32) == 4 && offsetof(TeamKey, last_seen_ms) == 8,
                      "node.h: TeamKey's hash/stamp moved — the ladder bytes opened a hole instead of filling one");
        TeamKey  _team_keys[16] = {};
        uint8_t  _team_keys_n = 0;
        // §team-multihop (spec 2026-07-15 Plane 2 / §5): TEAM-plane F route-discovery dedup + rate-limit — team-PRIVATE
        // copies of _rreq_seen/_rreq_last (keyed by a team_local_id origin/dst) so a team RREQ can NEVER alias a static one
        // (§18 the two id-spaces can collide). Right-sized to team scale (16), NOT the static caps. Empty for a static node.
        RReqSeen _rreq_seen_team[16] = {};
        uint8_t  _rreq_seen_team_n = 0;
        RReqLast _rreq_last_team[16] = {};
        uint8_t  _rreq_last_team_n = 0;
        // §team-multihop 2c (spec 2026-07-16): the TEAM-plane liveness table — a self-contained mirror of _peer_liveness,
        // keyed by team_local_id, with its OWN on-demand LRU (team_liveness_slot). NEVER _peer_liveness / _team_keys
        // (which evicts by crypto-key recency — a different lifetime). Proactive dead-relay demotion; empty for a static node.
        PeerLiveness _team_liveness[protocol::cap_team_liveness] = {};
        uint8_t      _team_liveness_n = 0;
#endif
        // ==== MAC / FLIGHT pipeline (single flight per node): tx-queue · pending-tx/rx · post-ack · no-route defer ====
        // R3 data-plane state (single flight per node).
        TxItem                   _tx_queue[kTxQueueCap];
        uint8_t                  _tx_queue_n = 0;          // FIFO depth
        std::optional<PendingTx> _pending_tx;
        std::optional<PendingRx> _pending_rx;
        PostAck                  _post_ack;
        // No-route defer queue (insertion-order array; drained TTL-first on a beacon
        // route-change or the 1s periodic timer). _drain_armed gates the periodic timer.
        DeferredSend             _deferred[protocol::cap_deferred_sends];
        uint8_t                  _deferred_n = 0;
        bool                     _drain_armed = false;
        // ==== ROUTE-DISCOVERY (F) dedup ====
        // F route-discovery dedup state (Lua route_request_seen / route_request_last).
        RReqSeen _rreq_seen[protocol::cap_route_request_seen] = {};
        uint8_t  _rreq_seen_n = 0;
        RReqLast _rreq_last[protocol::cap_route_request_last] = {};
        uint8_t  _rreq_last_n = 0;

        // ==== DAD / id-bind (hash-locate: key_hash32 → node_id) — STRADDLES crypto: also feeds key_hash_for_id + E2E DST_HASH ====
        // Hash-locate id_bind table (Lua dv:4677): key_hash32 -> node_id, beacon-populated.
        IdBind   _id_bind[protocol::cap_id_bind] = {};
        uint16_t _id_bind_n = 0;

        // ==== CRYPTO / PEERS (sender-side hash→home · E2E pubkey cache · H flood dedup) — _mobile_home_cache ships in the STATIC build (not #if MR_FEAT_MOBILE) ====
        // §mobile 3c: sender-side mobile_hash -> home_id cache. id_bind CAN'T hold this (one-hash-per-id, and the home
        // owns its own authoritative hash), so a mobile's stable hash -> its home_node lives here. NO bijection (many
        // mobiles -> one home). Populated by the proxy-answer signature; read by send_by_hash. SILENT (no telemetry).
        MobileHomeBinding _mobile_home_cache[protocol::cap_mobile_home_cache] = {};
        uint8_t           _mobile_home_cache_n = 0;
        // E2E peer-pubkey cache (Phase 1 §6): key_hash32 -> ed_pub (authoritative, hash-verified).
        PeerKey  _peer_keys[protocol::cap_peer_keys] = {};
        uint16_t _peer_keys_n = 0;
        // H hash-locate flood dedup (Lua hash_query_seen): per-(origin,key_hash32), hash_query_seen_ttl_ms window.
        HashQuerySeen _hash_query_seen[protocol::cap_hash_query_seen] = {};
        uint8_t       _hash_query_seen_n = 0;

        // ==== LIVENESS / freshness + CROSS-PLANE-SHARED substrate (node_id-indexed, NO plane discriminator) ====
        // ★ KNOWN LEAK SITE (tech-debt, fix deferred to the PlaneRuntime split — NOT this legibility pass): the node_id-indexed
        //   arrays below (_dest_seen_ms/_link_bidi/_link_bidi_confirmed_ms/_mobile_peer/_link_reprobe_last_ms) AND the maps
        //   _blind_until/_neighbor_budget_tier(_set_at)/_per_sender_originator (in the DEDUP-MAPS section) carry NO plane
        //   discriminator — a team/mobile LOCAL-id write (e.g. _blind_until[team_local_id]; note_link_confirmed → _link_bidi[next_hop])
        //   aliases the SAME slot a colliding static node_id uses. Correct today (planes rarely co-active on one link); do NOT read
        //   these as plane-clean. See [[meshroute-plane-separation]].
        // ★ §P2-7 AUDIT (2026-07-20) — five MORE plane-blind / dirty-blind ledgers, documented here (behavior fixes = Wave 2/3).
        // ★★ THE FIRST THREE ARE NOW PLANE-KEYED — §team-parity T6/B (2026-07-28, spec §3/T6 Part B), which closes §10.3 /
        //    §9-Q4 and the [[meshroute-plane-separation]] re-audit-by-plane item. The pre-T6 text of each is kept below as
        //    the record of what was fixed, because every "safe today" reason it gives is an assertion that the TEAM PLANE IS
        //    QUIET — and R1 (full team/static routing parity) is this arc deliberately ending that.
        //   • _per_origin_channel (DEDUP section below): ✅ FIXED — keyed `(plane<<8)|origin` (node_channel.cpp
        //     channel_origin_admit). Pre-T6: "keyed by the BARE origin id, NO plane bit — a team origin and a static origin
        //     that collide numerically share ONE windowed distinct-id cap slot (over-throttle risk). Safe today: the planes
        //     rarely co-relay the same origin id." ⚠ That reason was ALREADY FALSE: ingest_channel_m admits a plain leaf M
        //     (static origin) AND a team-scoped M (team origin) on the SAME node — "planes = BOTH" is written at its own
        //     :174 gate — so a team member co-relays both today.
        //   • _seen_origins (DEDUP section below): ✅ FIXED — bit 61 = the TEAM plane, on top of the pre-existing bit 62.
        //     ⚠ The pre-T6 text ("the PLAINTEXT flight key (origin<<24|dst<<16|ctr) has NO plane bit, so a team and a static
        //     PLAINTEXT DM alias iff origin+dst+ctr ALL collide") was DRIFTED, and this note is where it drifted: bit 62
        //     (`mobile_from`) had already separated team-or-mobile from static. What it did NOT separate was TEAM from
        //     MOBILE-STATIC, since a registered mobile's ordinary static DM sets mobile_from too. Bit 61 closes that.
        //     CRYPTED flights were always immune (disjoint 2^63 nonce-seed space — see the key comment there).
        //   • _hash_query_seen (above): ✅ FIXED — `team_scoped` is part of HashQuerySeen::same_key, at zero RAM cost.
        //     Pre-T6: "the H-flood dedup key has NO plane discriminator — safe ONLY by an UNWRITTEN role-exclusion
        //     invariant: no node today processes BOTH the static and the team H-flood plane." ⚠ That invariant is
        //     DEFEATABLE BY LIVE CONFIG: `cfg set team_id`/`team <id>` never sets is_mobile (node.cpp:413), and a
        //     `team_id!=0 && !is_mobile` node passes BOTH of handle_h's plane gates (node_hashlocate.cpp:584/:603).
        //   • _mediated_recent (below, Node-global): ★ DELIBERATELY **NOT** PLANE-KEYED — T6/B AUDITED IT AND REFUSED THE
        //     FIT, because it does not alias across planes at all. Its key is `(node_id, loser_hash)` and `loser_hash` is a
        //     32-bit KEY hash — a global node identity, not a per-plane id — so an alias would need ONE physical node to be
        //     the key-loser for the SAME numeric id on BOTH planes. That is impossible: the two writers' loser sets are
        //     disjoint on the single wire field `b.is_mobile`. The TEAM writer (node_beacon.cpp:788) requires
        //     `same_team_beacon`, which is `b.is_mobile && same_team(peer_team)` (:532). The STATIC writer
        //     (node_hashlocate.cpp:90, inside id_bind_set) requires `source == bcn && authoritative`, and the ONLY site
        //     that passes that pair is node_beacon.cpp:627 — guarded by `!b.is_mobile` (:626). (node_join.cpp:276/311 also
        //     pass `bcn` but with `claimed`, which the mediation gate rejects.) is_mobile is a static per-node config that
        //     never flips at runtime, so no key can be in both sets. Adding a plane byte would cost +256 B of nRF52840 RAM
        //     (MediatedRecent 16 B -> 24 B x cap_mediated_recent 32) and move sizeof(Node) for a provably empty set.
        //   • beacon_max_idle_force (node_beacon.cpp): its dirty-entry count scans the STATIC _rt only — a team member that
        //     advertises _rt_team can have its max-idle B+C beacons suppressed while dirty TEAM entries are still pending.
        //   • team_resort_routes_through (node_routing.cpp): a primary change reranks _rt_team AND dirty-marks the moved entry +
        //     schedules ONE triggered beacon (Wave-2 ruling 2.3 FIX), so the rerank IS advertised on the member's own cadence —
        //     emit_beacon's dirty pass selects from src_rt = _rt_team for a team member (team_emit). See that function's header.
        // Peer-liveness + freshness plane (routing-liveness port): per-next-hop timeout tiers. Bounded LRU.
        PeerLiveness  _peer_liveness[protocol::cap_peer_liveness] = {};
        uint8_t       _peer_liveness_n = 0;
        // dest_seen freshness map (Lua dest_seen_ms@1289): node_id -> last-seen ms, FULL 0..254 range, NO eviction
        // — decoupled from the bounded _peer_liveness table so seen-bitmap gossip can keep ANY peer fresh (the
        // create=false piggyback starved gossip-only peers). is_next_hop_fresh reads this. 0 = never seen.
        uint64_t      _dest_seen_ms[256] = {};
        // Bidirectionality plane (asymmetric-link routing, 2026-06-29). Index = node_id, value = a LinkBidi.
        // Zero-init => every link defaults to 'unknown' (selectable, unpenalized). FULL 0..254 range, NO eviction
        // (like _dest_seen_ms) so a gossip-only or quiet peer keeps its state. _link_bidi_confirmed_ms is the
        // DEDICATED decay source — last real-CTS / complete-heard-set confirmation time (do NOT overload _dest_seen_ms).
        uint8_t       _link_bidi[256] = {};
        uint64_t      _link_bidi_confirmed_ms[256] = {};
        // ① mobile-peer set (Lua mobile_peers@1325): 1 bit per node_id, SET-only (is_mobile is a static per-node config,
        // never flips at runtime — Lua dv:9603-9604 sets, never clears). Eviction-free (unlike _peer_liveness) so a
        // gossip-only mobile is still avoided. 256 bits = 32 B/layer. Read by is_mobile_peer.
        uint8_t       _mobile_peer[32] = {};
        // Slow-reprobe throttle (asymmetric-link slice 6): per-next-hop last single-probe time for a
        // _link_bidi==one_way sole route. FULL 0..254 range (index 255/0xFF is the reserved id, never written — matches the
        // sibling _dest_seen_ms/_link_bidi arrays; the "0..255" was a stale off-by-one comment), eviction-free (like _dest_seen_ms) so an
        // isolated next-hop is throttled even if its PeerLiveness slot was LRU-evicted. 0 = never reprobed
        // (clock-at-0 -> the FIRST giveup probes immediately, then once per link_reprobe_ttl_ms).
        uint64_t      _link_reprobe_last_ms[256] = {};

        // ==== CHANNEL / FLOOD (gossip plane: buffer · per-origin ledger · pull/re-offer rings · flood table) ====
        // Channel-message gossip plane state (node_channel.cpp).
        ChannelEntry _channel_buffer[protocol::cap_channel_buffer];
        uint16_t     _channel_buffer_n = 0;
        // ★ §team-parity T6/B: keyed `(plane<<8)|origin`, NOT the bare origin — see channel_origin_admit for the composer
        // and the LIVENESS-section note above for why the bare key was already aliasing (a team member ingests both a
        // plain leaf M and a team-scoped M, so it co-relays both planes today). std::map's own size does not depend on
        // the key type, so widening uint8_t -> uint16_t costs 0 bytes and does not move sizeof(Node).
        std::map<uint16_t, ChannelOriginLedger> _per_origin_channel;   // (plane<<8)|origin -> windowed distinct-id ledger
        ChannelPullPending _channel_pull_pending[protocol::cap_channel_pull_pending] = {};
        ChannelPullRecent  _channel_pull_recent[protocol::cap_channel_pull_recent] = {};
        uint8_t            _channel_pull_recent_n = 0;
        FloodState         _flood[protocol::cap_flood_pending] = {};   // channel-flood in-progress table (slot i -> timer kFloodRebcastTimerId+i)
        ChannelReofferPending _channel_reoffer_pending[protocol::cap_channel_reoffer_pending] = {};  // Part 2: per-origin re-offer table (slot i -> timer kChannelReofferTimerId+i)

        // ==== DEDUP / ctr maps (node_id- or flight-keyed). ★ _blind_until / _neighbor_budget_tier / _per_sender_originator are part of the no-plane-discriminator LEAK cluster (see the LIVENESS section header above) ====
        // dedup maps.
        std::map<uint8_t, uint16_t>  _peer_send_counter;   // next_ctr per dst
        uint16_t     _peer_ctr_floor = 0;                  // D7: per-peer next_ctr floor (persisted high-water; resumes DM ctrs above the pre-reboot value)
        std::map<uint32_t, LastAcked> _last_acked_from;    // key (src<<24|dst<<16|ctr_lo<<8|len)
        std::map<uint64_t, uint64_t>  _seen_origins;       // §1b TYPE-NAMESPACED flight key -> expiry_ms. PLAINTEXT =
                                                           // (origin<<24|dst<<16|ctr) in [0,2^32); CRYPTED = the full
                                                           // 8-B nonce-seed | (1<<63) in [2^63,2^64) — disjoint, can't alias.
        std::map<uint64_t, uint8_t>   _seen_origin_from;   // same key -> the prev-hop (LOOP_DUP discriminator)
        std::map<uint8_t, uint64_t>   _blind_until;        // next_hop -> absolute_ms it's deaf-on-routing (F1)
        // R4.2 persistent neighbor budget tier (routing-grade demotion beyond the short blind window).
        // mutable: get_neighbor_tier lazy-prunes the TTL-expired entry on read, like the Lua (dv:3863-3868).
        mutable std::map<uint8_t, uint8_t>  _neighbor_budget_tier;       // next_hop -> tier (1..3); absent/0 = HEALTHY
        mutable std::map<uint8_t, uint64_t> _neighbor_budget_tier_set_at; // next_hop -> absolute_ms the mark was set
        // R4.4 originator anti-spam: per-sender sliding-window ledger of overheard RTS/CTS (fixed ring).
        std::map<uint8_t, OrigRing> _per_sender_originator;  // sender_id -> recent events (fixed ring)

        // ==== Q REQ_SYNC plane dedup ====
        // Q REQ_SYNC plane dedup (Slice 2b — moved from Node scope; keyed by a REMOTE leaf-local q.src/requester,
        // so per-layer: a gateway's two leaves must not alias the same 8-bit id across distinct physical nodes).
        QResponded  _q_responded[protocol::cap_q_responded_to] = {};            // responder dedup ring (opcode|src|dest)
        uint8_t     _q_responded_n = 0;
        SyncPending _sync_pending[protocol::cap_sync_response_pending] = {};    // jittered full-table reply ring (per requester)

        // ==== per-leaf BEACON · HOST-MOBILE registry (#if MR_FEAT_MOBILE host side) · DISCOVERY · WINDOW timing ====
        // Slice 3d per-leaf beacon: a gateway beacons each leaf on its OWN cadence at window-activation (the shared
        // kBeaconTimerId is disabled for gateways — its single deadline halves the per-leaf cadence). 0 = never beaconed.
        uint64_t _last_beacon_ms = 0;
        // §mobile 2a (host registration): mobiles this host has accepted. Populated on a mobile CLAIM (claim-stands, no
        // reply); mobile_local_id is host-assigned from 17..254 (may overlap a global id — the Slice-1 mark disambiguates).
        // Per-leaf (a host serves one leaf). DORMANT unless a mobile registers -> the static mesh is unaffected.
        struct HostMobileEntry { uint32_t key_hash32; uint8_t mobile_local_id; uint16_t epoch; uint64_t last_heard_ms;
                                 uint8_t redirect_home_id = 0; uint8_t redirect_epoch = 0; uint8_t redirect_home_layer = 0;
                                 uint8_t ed_pub[32] = {}; bool has_pubkey = false;
                                 char name[32] = {}; uint8_t name_len = 0;
                                 uint32_t last_key_fwd_hash32 = 0; bool deleg_fail = false; };  // §mobile 4b redirect (0 home = none) + §5b the new home's LAYER + §Part 2 the mobile's E2E pubkey (Fix 5) + §1.3 the mobile's name (pushed w/ the key) + §S3 part2 last_key_fwd_hash32 (the last requester key we forwarded to this mobile — dedup a same-requester re-forward); at struct END for positional aggregate-inits
        HostMobileEntry _mobile_reg[protocol::cap_host_mobiles] = {};
        uint8_t         _mobile_reg_n = 0;
        // §S6 presence plane (home side): per-mobile SNR EWMA (Q4) PARALLEL to _mobile_reg (HostMobileEntry stays unchanged),
        // mapped to the roster's 2-bit quality tier; plus the roster coalesce/rate-limit clocks. All host-gated (dormant on
        // a non-host -> static-inert). INT16_MIN = no sample yet (seeds to the first probe SNR).
        int16_t         _mobile_snr_q4[protocol::cap_host_mobiles] = {};
        uint64_t        _last_roster_ms       = 0;    // rate-limit floor (presence_roster_min_interval_ms)
        bool            _roster_coalesce_pending = false;   // a probe opened a coalesce window; the timer will emit ONE roster
        // §S6.4-D new-home->old-home notify: on OFFERing a discovering mobile whose last_home != 0 != self, stash the
        // last-home so the CLAIM (adopt) can originate the breadcrumb (D10). Small ring; evict-oldest.
        struct PendingNotify { uint32_t mobile_hash; uint8_t last_home_id; uint8_t last_home_layer; uint32_t last_home_hash = 0; uint64_t stash_ms = 0; };  // §B4: last_home_hash addresses a CROSS-LAYER breadcrumb by hash (0 = same-layer / unknown). §3-A.6/P2-6: stash_ms -> evict-STALEST when full (was evict-slot-0)
        PendingNotify   _notify_pending[protocol::cap_host_mobiles] = {};
        uint8_t         _notify_pending_n = 0;
        uint8_t         _dir_epoch = 0;               // §S6/D6: gateway-derived layer-directory version this node advertises in the roster (XOR aggregate of known gw epochs; a gateway derives its own)
        // §S6 rev2 ECHO (D14/D15): the coalesce window's FIRST probe echo — echo_hash32 + its RX quality tier. Emitted in
        // the next roster iff pending (a searching-probe canvass answer). One echo per window (first wins).
        uint32_t        _roster_echo_hash = 0;
        uint8_t         _roster_echo_q = 0;
        bool            _roster_echo_pending = false;
        // §S6/QA-3b OFFER de-storm: a jittered mobile OFFER (stashed, fired by kMobileOfferBackoffTimerId) so co-located
        // hosts don't answer one DISCOVER at the SAME ms (the collision that let a mobile adopt the WEAK home). Single-slot
        // (last DISCOVER wins) — a v1 limitation; concurrent multi-mobile DISCOVERs at one host are rare.
        // The single-slot member of the jittered_tx_stash.h family (jtx_stash_arm + jtx_fire); the two
        // §F-XL rings are the ring-shaped members. Bare array + len rather than a struct, which is why
        // the single-slot entry point takes the buffer, its capacity and the length separately.
        uint8_t         _pending_offer[13] = {};
        uint8_t         _pending_offer_len = 0;
        // §per-layer discovery (2026-07-05): a GATEWAY bootstraps each leaf INDEPENDENTLY — the boot leaf must not trip
        // the OTHER leaf out of fast-cadence discovery (node-global discovery starved leaf 1 -> the 3h heartbeat). A
        // single-layer node has ONE leaf, so _active is always &_layers[0] => per-leaf ≡ the old node-global state
        // (byte-identical, proven by s18). Gateway-only BY CONSTRUCTION (is_gateway ≡ n_layers==2), no is_gateway branch.
        bool     _discovery_mode = false;        // fast cadence + full pages until exit
        uint64_t _discovery_started_ms = 0;
        uint64_t _discovery_until_ms = 0;
        uint16_t _discovery_bcn_rx_count = 0;
        // Slice 3e: absolute ms this leaf's window NEXT opens — the anchor for the receiver-anchored countdown
        // (schedule_record.offset). Set by the scheduler on every switch + at boot. countdown = (next_open-now) % period.
        uint64_t _next_open_ms = 0;
    };
#ifdef MESHROUTE_NATIVE
    // White-box test seam (native test build only — #ifdef'd out of every device build, zero firmware surface):
    // test/test_dual_layer.cpp points _active at each leaf + reads the per-LayerRuntime dedup maps to ASSERT the
    // Slice-2b non-aliasing property (§8). The gateway's real leaf-swap (activate_layer) lands in Slice 3.
    friend struct DualLayerTestAccess;
    friend struct E2eAckTestAccess;   // shelf item (i): white-box access to the pending-e2e-ack ring + arm/clear/fire (test_node_e2e_ack.cpp)
#endif
    // ======== GATEWAY / CROSS-LAYER scheduler (Node-global — spans leaves; survives a window swap) ========
    LayerRuntime  _layers[MR_N_LAYERS];
    LayerRuntime* _active = &_layers[0];
    uint8_t       _n_layers = 1;
    // Slice 3e.2: learned schedules of nearby GATEWAYS (Node-global — a node's view of the gateways it can reach,
    // independent of its own layers). Keyed by the gateway's node_id; evict-oldest on overflow.
    GatewaySchedule _gw_schedules[protocol::cap_gateway_neighbor_schedules];
    BridgedLayer    _bridged_layers[protocol::cap_bridged_layers];   // multi-hop gw_id->dest_leaf (type-4 TLV; ~8×11 B)
    // Slice 4c.1: cross-layer re-inject HANDOFFS (node-global — they span leaves; survive a window swap, drained on the
    // TARGET leaf's activate). A SMALL bounded ring (refuse-when-full LOUD, never drop-oldest a transit DM silently).
    XlHandoff _xl_handoffs[protocol::cap_gateway_handoffs];
    uint64_t _window_epoch_ms = 0;  // Slice 3d GRID anchor (boot instant = leaf-0's first window open); switch times = grid epoch + k·period (+window0), a busy slip never ratchets it. Grouped here w/ the cross-layer scheduler (was orphaned among the duty/R4.3 witnesses — cleanup 2026-07-15).

    // ---- own-origin anti-spam floors (Node-global timestamps; duty/anti-spam concern) ----
    uint64_t _ack_warn_until = 0;   // DM Inc 3: park new DM originations until this ms (set by a warn'd ACK)
    uint64_t _last_channel_origin_ms = 0;   // Slice 2: self side of channel_min_interval_ms (own channel posts)
    uint64_t _last_dm_origin_ms = 0;   // Slice 3: own-DM burst floor (dm_min_interval_ms); relays/floods/e2e-ack/rcmd exempt
    // ★ §chan-crypt CL2a: the `team_channel_no_key` PUSH floor (protocol::team_channel_no_key_push_min_ms). NOT an
    // origination floor like the three above — it is a RECEIVE-side rate limit — but it is the same kind of quantity
    // (a Node-global ms stamp) and it is placed at the END of this 8-aligned u64 run so it costs exactly its own
    // 8 bytes and opens NO hole: the next member is the 4-aligned `_nack_wait_flight_gen`.
    // ⚠ It holds the NEXT-ALLOWED instant, not the LAST-PUSHED one, and that is a bug fix not a style choice: with a
    // last-pushed stamp, 0 has to double as "never pushed", and a node whose clock is still 0 (boot, and every native
    // test) writes 0 back — so the limiter never engages and every unreadable post prompts. Next-allowed needs no
    // sentinel: 0 means "allowed now" both before the first push and never again after it.
    // The TELEMETRY twin is deliberately un-limited (the bench wants every occurrence).
    uint64_t _team_ch_nokey_push_next_ms = 0;
    // NACK BUSY_RX wait-same-hop: the captured ctr_lo the kNackWaitTimerId re-RTSes for.
    uint32_t                     _nack_wait_flight_gen = 0;   // L9: the EXACT flight the BUSY_RX same-hop re-RTS wait belongs to (was the 4-bit ctr_lo proxy — 1/16 alias could re-RTS a since-replaced flight)
    bool                         _nack_wait_pending = false;
    // §F-XL-1 (2026-07-18): jittered h_forward de-storm. Sibling relays that heard the SAME H flood copy re-tx it
    // with ZERO jitter today -> a deterministic same-ms collision at any common/downstream receiver (s27 hello-m4:
    // T2+T3 forward at the identical ms every handoff retry, T4 behind T3 decodes neither). Stash the built frame,
    // fire after a small random delay (kHForwardTimerId+slot); the existing LBT then defers the later sibling. A
    // small RING (not single-slot): a dense mesh has concurrent floods for DIFFERENT hashes, which single-slot would
    // clobber. Node-global (the H frame is self-contained incl. its leaf_id; a gateway's two leaves share the radio).
    // jittered_tx_stash.h contract: `buf[N]` + `len` (0 = nothing armed / already fired); jtx_ring_arm
    // owns the fit guard, the cursor and the timer-id pairing, jtx_ring_armed + jtx_fire own the firing.
    static constexpr uint8_t kHForwardSlots = 4;
    struct HForwardStash { uint8_t buf[8 + 32 + 4 + 1 + 32]; uint8_t len = 0; };   // <=77 B H frame (matches pack_h)
    HForwardStash _h_forward_stash[kHForwardSlots];
    uint8_t       _h_forward_rr = 0;   // round-robin write cursor
    // §F-XL-2 (2026-07-19): jittered rreq_forward de-storm — the SAME machinery as F-XL-1 for the AODV RREQ relay
    // (node_route_discovery.cpp, static + team). Sibling relays that heard the same RREQ flood re-broadcast it at the
    // identical ms -> deterministic collision. Stash + jittered fire (kRreqForwardTimerId+slot). The packed F frame is
    // self-contained (leaf_id / team scope baked in by pack_f), so ONE Node-global ring serves both planes; the RING
    // (not single-slot) covers concurrent discoveries for DIFFERENT dsts. The 16-B buf matches pack_f's bound.
    // Same jittered_tx_stash.h contract as the F-XL-1 ring above.
    static constexpr uint8_t kRreqForwardSlots = 4;
    struct RreqForwardStash { uint8_t buf[16]; uint8_t len = 0; };   // <=16 B F frame (matches pack_f's buf[16])
    RreqForwardStash _rreq_forward_stash[kRreqForwardSlots];
    uint8_t          _rreq_forward_rr = 0;   // round-robin write cursor
    // async push ring (the app channel; drained via next_push, drop-oldest on overflow)
    Push     _push_ring[protocol::cap_push_ring];
    uint8_t  _push_head = 0, _push_count = 0;
    // _remote_inbound relocated to the REMOTE-MGMT section (identity block above) — cleanup 2026-07-15.
};

#ifdef MESHROUTE_NATIVE
// LAYOUT-INVARIANCE tripwire (native layout ONLY — NOT a RAM budget: native sizeof != nRF52 sizeof, different
// pointer/enum/alignment). Purpose: the node.h legibility reorder (2026-07-15 by-concern member reorder) must not
// change Node's layout. If this fires after a *deliberate* member add/remove/type change, update the baseline
// consciously — it is a tripwire, not a frozen contract. The real nRF52 RAM check is the firmware.map .bss/.data diff.
static_assert(sizeof(Node) == 221088, "node.h: Node native layout changed — if intentional, update the baseline");   // 221024 -> 221088 (+64 §id-hash S4b — the `resolve-id-for-pubkey` intent ring, MEASURED by offsetof + the assert pair beside the struct, not inferred: sizeof(PendingIdPubkey) = 16 (offsetof(id) = 8, i.e. both bytes sit in the tail pad the 8-aligned `deadline_ms` already forces) x cap_pending_id_pubkey(4) = 64. Inserted immediately after `_pending_e2e_acks`, which ends 8-ALIGNED (PendingE2eAck[8] x 24) — §AB4's `_peer_loc` precedent at the same seam — and 64 is a multiple of 8, so every later member shifts by exactly 64 and NO hole opens anywhere. ⚠ NOT team-gated and deliberately so: a by-id `reqpubkey` on the STATIC plane is the bench defect this arc opened with (spec §0), so a gateway build carries the ring too. Per-board RAM deltas in the slice report.). 220976 -> 221024 (+48 §tx-admission TX3 — the OWNER-RULED transmitter-admission boundary for the channel digest: DeferredLbt gains `uint32_t digest_ids[3]`, MEASURED by offsetof not inferred: sizeof(DeferredLbt) 164 -> 176 (+12), offsetof(digest_ids)=12 and offsetof(buf) 12 -> 24, i.e. it lands in the 4-aligned run before `buf` and opens NO new hole; x kLbtSlots(4) = +48. ⚠ The BRIEFED estimate was +64 (3 ids + a count byte); the count byte was removed by making `digest_ids[0] == 0` the terminator — a live channel id can never be 0 (channel_msg_id_mint packs origin >= 1 into the high byte) — which is what bought the 16 bytes back. Per-board RAM deltas in the slice report.). 220968 -> 220976 (+8 §chan-crypt CL2a, and BOTH new members are measured by offsetof + a template-reveal, not inferred. TWO members were added and only ONE of them costs anything. (a) `_team_ch_nokey_push_ms`, a uint64_t, is appended to the EXISTING 8-aligned run of own-origin ms stamps (_ack_warn_until / _last_channel_origin_ms / _last_dm_origin_ms) and the member after that run is the 4-aligned `_nack_wait_flight_gen`, so it opens NO hole anywhere and costs exactly its own 8 bytes. (b) `_channel_seal_ctr`, a uint16_t, costs ZERO — and where it sits is the whole reason. Its SEMANTIC home is beside `_relay_seal_ctr` (native offset 186), and MEASURED there it costs EIGHT: at 188 it pushes _admin_pubkey 188->190, which pushes the 4-aligned _admin_counter_floor 220->224 (two new pad bytes) and then the 8-aligned _cfg 472->480 (six more). Placed instead immediately after `_remote_inbound` it lands at 470, inside the 2-byte pad that already sat before the 8-aligned _cfg, and sizeof is unchanged by it. EIGHTH application of the padding-placement rule below. (c) NodeConfig::team_channel_crypt costs ZERO TOO and is NOT part of this +8: sizeof(NodeConfig) 256 -> 256, because the bool takes native byte 95 — the one pad byte still left in the dv_hop_cap@93 / team_hop_cap@94 / radio_freq_mhz@96 hole that team_hop_cap's own note describes. SEVENTH application. ⚠ this line is native-ONLY, so the per-target proof is the compile-only sizeof reveal on the REAL toolchains with the REAL per-env flag sets, recorded in the §chan-crypt CL2a slice report — and NOW INLINE, because omitting them left the newest per-target numbers in this ledger reading as AB4's and therefore STALE BY +8, which §b39 caught: xiao_sx1262 / xiao_esp32s3 117048 -> **117056**, gateway / gateway_esp32s3 147664 -> **147672**, both *_mobile 117016 -> **117024**.). 220648 -> 220968 (+320 §AB4 — the retained-peer-location ring, and BOTH halves of the number are measured, not inferred. THE RECORD: sizeof(PeerLoc) = 20, alignof 4, offsetof(src) = 16 — i.e. §2.7.1's briefed `{u32 hash; i32 lat; i32 lon; u32 t_s}` really is 16 B with no padding (that premise HELD, measured), but the RESCOPE that added `loc_src` pushed it to 20 with a 3-byte tail hole, so the spec's "16 B exactly, x16 = 256 B" and its own loc_src requirement cannot both be true — 320 B is the honest figure. Those 3 bytes are declared as a NAMED `reserved[3]` member: identical size, but IMPLICIT tail padding is INDETERMINATE after `PeerLoc{}`, which would make any memcmp-style comparison over the record unsound (AB1's lesson, which briefed 70 B and measured 72). THE PLACEMENT, by offsetof native and cross-checked on ARM: `_peer_loc[16]` is inserted immediately after `_pending_e2e_acks` and before `_parked_sends_n`. `_pending_e2e_acks` is PendingE2eAck[8] x 24 B ending on an 8-ALIGNED boundary (native 5208+192 = 5400; ARM 5192+192 = 5384), and PeerLoc needs only 4, so the array opens NO hole and its 320 B (= 8x40, itself a multiple of the 8-alignment downstream) shifts every later member by exactly 320: native `_l2c_redirect` 5408 -> 5728, ARM 5392 -> 5712. ★ AND THE COUNT BYTE `_peer_loc_n` COSTS ZERO: `_parked_sends_n` (a uint8_t) was already followed by SEVEN bytes of pure alignment pad before the 8-aligned `_l2c_redirect` (native 5401..5407, ARM 5385..5391); `_peer_loc_n` takes one of them and the hole shrinks to SIX (native 5722..5727, ARM 5706..5711). SIXTH application of the radio_freq_mhz / team_hop_cap / HashQuerySeen.team_scoped / T5-PeerLiveness / T-K1-_team_ch_key_present padding-placement rule. ⚠ this line is native-ONLY, so the per-target proof is a compile-only sizeof reveal on the REAL toolchains with the REAL per-env flag sets, whose PRE column reproduces this ledger's §loc-per-send figures EXACTLY (that is what calibrates it): xiao_sx1262 116728->117048, gateway 147344->147664, xiao_mobile 116696->117016, xiao_esp32s3 116728->117048, gateway_esp32s3 147344->147664, xiao_esp32s3_mobile 116696->117016 — ★ +320 UNIFORMLY ON ALL SIX ABI x member-set cells, no cell disagreeing and no hole opening anywhere, INCLUDING the two MR_FEAT_TEAM 0 gateways: the ring is deliberately NOT team-gated, because its live source is a sealed DM and a static-only build receives one exactly as a team member does. ⇒ the six-env LINK grid was NOT run and did not need to be: the grid exists to find a per-target padding difference, and the six-cell probe measured that there is none — so the RAM/flash figures come from the standard three envs (T-K1's recorded precedent).). 220656 -> 220648 (−8 §loc-per-send, and this one is worth reading because it is the FIRST entry in this ledger that REMOVES a member — a single `bool`, and it paid back EIGHT bytes, not one. Arithmetic, MEASURED by offsetof + a template-reveal on all six board flag-sets, not inferred: NodeConfig's tail ran lat_e7 @164, lon_e7 @168, then the four bools loc_in_dm @172 / loc_in_m @173 / e2e_dm @174 / intro_attach @175, then n_layers @176, SEVEN bytes of pure alignment pad (177..183), and the 8-ALIGNED `LayerConfig layers[2]` (40 B each) @184, ending at 264 with no trailing pad. Deleting `loc_in_dm` slides the three surviving bools and n_layers down one, so n_layers lands at 175 — the LAST byte before the 8-byte boundary — and `layers` moves back to 176, ending at 256. ⇒ sizeof(NodeConfig) 264 -> 256. The byte was therefore NOT sitting in a hole (the padding-placement rule's usual case, five prior applications below): it sat exactly ON the boundary, so it was costing a FULL 8-byte quantum and its removal reclaims all of it, while the 7-byte hole after n_layers shrinks to 0. Node embeds one 8-aligned `_cfg` (native offset 472, unchanged because everything before it is untouched), so Node loses exactly what NodeConfig loses: -8, uniformly. ⚠ this line is native-ONLY, so the per-target proof is the compile-only reveal recorded in the §loc-per-send slice report: sizeof(Node) -8 on ALL SIX flag-sets (xiao_sx1262 116736->116728, gateway 147352->147344, xiao_esp32s3 116736->116728, xiao_mobile 116704->116696, gateway_esp32s3 147352->147344, xiao_esp32s3_mobile 116704->116696) with sizeof(NodeConfig) 264->256 on every one — i.e. it moved on every ABI × member-set cell, which is what FORCED the six-env D2 grid for this slice rather than the usual three.). 220592 -> 220656 (+64, NOT +65 and NOT +72 — §team-ch-key T-K1 adds 65 B of state and pays for 64. Arithmetic, measured by template-reveal on all six board flag-sets, not inferred: (a) _team_ch_pub[32] + _team_ch_priv[32] are inserted immediately after _ed_pub, i.e. between _ed_pub and _crypto_ready. 64 is a multiple of 8, so EVERY downstream member keeps its alignment and NO new hole opens anywhere: native _crypto_ready 121->185 (still odd, so the 2-aligned _relay_seal_ctr still needs no pad), _admin_pubkey 124->188, the 4-aligned _admin_counter_floor 156->220 (still 4-aligned), _remote_inbound 161->225 ending 406->470, and the 8-aligned _cfg 408->472 with its pre-existing 2-byte pad UNCHANGED. The arrays therefore cost exactly their own 64 B. (b) the has-key flag `_team_ch_key_present` costs ZERO: it is placed at native offset 19, in the alignment pad that already sat between _team_dad_pending (18) and the 4-aligned _key_hash32 (20) — the SAME hole node.h:1242's note keeps _team_local_id/_team_dad_pending here to exploit. Placing it beside the keys instead would have made the insert 65 B, flipping _crypto_ready to an EVEN offset (186) so _relay_seal_ctr needs a pad byte, and pushing _admin_counter_floor off its 4-alignment for two more — measured +72, i.e. the flag would have cost 8. FIFTH application of the radio_freq_mhz / team_hop_cap / HashQuerySeen.team_scoped / T5-PeerLiveness padding-placement rule. ⚠ this line is native-ONLY and unverifiable on a board ABI, so the per-target proof is the compile-only sizeof probe recorded in the T-K1 slice report: +64 on ARM-full, Xtensa-full and both *_mobile (MR_FEAT_REMOTE_MGMT 0), and +0 on both gateway_* (MR_FEAT_TEAM 0 strips all three members)). 220592 -> 220592 (+0 §team-parity T6/B, and the SLICE BRIEF EXPECTED THIS TO MOVE — it does not, measured by template-reveal not by the assert alone. Arithmetic, per ledger: (1) HashQuerySeen gains `bool team_scoped` and stays 24 B — origin(1)+pad(3)+key_hash32(4)+t_ms(8) then hard+want_pubkey+team_scoped(3)+pad(5); the third bool lands in the 6 bytes of tail padding the previous two already shared, so the ×cap_hash_query_seen(64) ×MR_N_LAYERS array is unchanged. (2) _per_origin_channel's key widens uint8_t -> uint16_t, and sizeof(std::map) does not depend on its key type (the key lives in heap-allocated nodes), so 0 B. (3) _seen_origins takes a bit in an EXISTING uint64_t key — no member added. (4) _mediated_recent was AUDITED AND DELIBERATELY LEFT ALONE: its two writers' key sets are provably disjoint on b.is_mobile (see the §P2-7 note), and MediatedRecent measures 16 B, so a plane byte would have made it 24 B ×cap_mediated_recent(32) = +256 B of nRF52840 RAM for an empty set. THAT is the +256 this line does not carry). 220592 -> 220592 (+0 §team-parity T0: NodeConfig.team_hop_cap, a uint8_t placed immediately after dv_hop_cap. Arithmetic: dv_hop_cap sits at native offset 93 and the next member (the 8-byte-aligned `double radio_freq_mhz`) at 96, so bytes 94-95 were pure alignment pad; the new member takes byte 94 and the hole shrinks to one byte. sizeof(NodeConfig) 264 -> 264 and sizeof(Node) 264-worth-of-config unchanged => the member costs literally nothing. This is the radio_freq_mhz placement rule below applied a second time, and it is why the value on this line did NOT move). 220584 -> 220592 (+8 §layer-freq: NodeConfig.radio_freq_mhz, a `double` placed between dv_hop_cap and the existing `double duty_cycle` so it fills the 8-byte-alignment padding slot already there — measured sizeof(NodeConfig) 256 -> 264, i.e. the member costs its own 8 B and opens NO new hole; the same slot after `radio_cr` would have cost 16). 220392 -> 220584 (+192 shelf-item-(i): PendingE2eAck[cap_pending_e2e_acks=8], 24 B each = the E2E-ack deadline ring, Node-global). 220384 -> 220392 (+8 Wave-4: NodeConfig.gw_schedule_readvert_ms u32 + alignment; the gateway schedule re-advertisement cadence). …218872 (§S6) -> 219000 (§GapA) -> 219312 (+312 §F-XL-1: HForwardStash[4] = 4×(77+1) de-storm ring; _h_forward_rr fits existing padding) -> 219568 (+256 §S3 part2: HostMobileEntry.last_key_fwd_hash32 = 4 B + 4 B align, ×16 ×2 layers) -> 219760 (+192 batch B: §B2 HostMobileEntry.deleg_fail packs into existing tail padding; §B4 PendingNotify.last_home_hash 4 B ×16 ×2 layers = +128; §D16 PresenceCand.incompatible +8 align ×cap = +64) -> 219952 (+192 batch A: §F-XL-2 RreqForwardStash[4] = 4×(16+1) de-storm ring + _rreq_forward_rr; §F-SL-1 ParkedSend += reflood state (bool/bool/Plane/u8 + u64 reflood_at_ms, 8-align) ×cap_parked_sends=8) -> 220384 (+432 §3-A.6 evict-STALEST timestamps: _learned_layers_seen_ms u64×4 = +32; PendingNotify.stash_ms u64 -> 12->24 B ×16 ×2 layers = +384 (+16 align); MINUS the dead _presence_claim_retries u8 (absorbed by padding))
#endif

}  // namespace meshroute
