// MeshRoute — lib/core/node_channel.cpp  (channel-message gossip plane, ROADMAP §3)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The channel-message gossip plane: the channel buffer + per-origin anti-spam + send_channel
// origination, the managed FLOOD (fast primary, 2026-06-08) + the digest/CHANNEL_PULL repair
// backstop, and ingestion of the lean M frame (cmd 0xA, 2026-06-09 — its OWN frame, NOT a DATA
// inner; see frame_codec.h pack_m). Channel messages are LEAF-SCOPED — the M frame's byte-0
// leaf_id gates ingest. Gateways are consumer-not-provider per `gateway_only` (receive + pull for
// the owner; never rebroadcast/relay — see node.h §7). Mirrors dv_dual_sf.lua: channel_msg_id
// :2239, channel_buffer_find :3426, channel_buffer_mark_seen_by :3434, channel_origin_admit :3456,
// channel_buffer_pick_eviction :3485, channel_buffer_add :3511, the DATA-M ingest :10942, send_channel :12126.
//
// DELIBERATE device divergences from the Lua BASELINE (draw-free plane — no determinism impact):
//   - per-origin anti-spam ledger is a map<origin, FIXED array[20]> (Lua: unbounded per-origin
//     table). Per-origin events are bounded by the 20-distinct cap, so the fixed array holds them
//     EXACTLY — same throttle semantics, no heap growth.
//   - seen_by is a 256-bit bitmap per entry (Lua: a Lua set table) — O(1) set + O(neighbours) cover.
// Part of Node (declared in node.h).
#include "node.h"
#include "frame_codec.h"
#include "dm_crypto.h"   // §chan-crypt CL2a: dm_nonce / dm_seal / dm_open + DM_TAG_LEN / DM_NONCE_SEED_LEN
#include "identity.h"    // §chan-crypt CL2a: key_hash32_of — the team content key's 32-bit handle (the AAD's team binding)
#include "monocypher.h"  // §chan-crypt CL2a: crypto_blake2b (the content-key KDF) + crypto_wipe

#include <cstring>
#include <cstdio>      // snprintf — the DEBUG flood_log_coverage trace (trace_on-gated)

namespace MESHROUTE_NS {

// ★ §team-parity T6 (spec §3/T6): the plane an M-broadcast TxItem is stamped on — DECIDED DELIBERATELY, not swept along
// with the DM path, because a team CHANNEL post is not obviously the same case as a team DM (the brief said so and it is
// right: the channel plane already carries its OWN plane-aware origin at do_send_channel:271-273, `e.origin`, which is
// what the channel_msg_id and every buffer/dedup/anti-spam decision key on — TxItem::origin is a SECOND, parallel
// identity on the same frame).
// THE PREDICATE IS THE FLAVOR BIT, deliberately reusing the EXACT expression the very next line of both call sites
// already uses for `item.mobile_src` (U1 — one definition of "this M is team traffic"; a second, drifting definition of
// team-ness on the same two frames is precisely the S1/L9 field-drop rot). It is NOT the DM's plane predicate: an
// M-broadcast has no meaningful `dst` (0xFF for a flood, the PULLER for a pull-response), so is_team_peer(dst) would be
// nonsense here — the flavor is the frame's own, authoritative scope.
// ⚠ MEASURED, and the reason this is safe rather than merely plausible: TxItem/PendingTx::origin is CORPUS-INERT on both
// M paths. All five downstream readers exclude an M by construction — node_mac.cpp:576 and :792 both test
// `!is_channel_m`, and the node_cascade.cpp:124/178/307/427/440 + node_mac_rx.cpp:1379/1428 readers all sit behind
// awaiting_cts / awaiting_ack, which issue_m_broadcast clears (node_mac.cpp:621) for a fire-and-forget M. The M frame's
// own wire bytes are hand-built ([id4|channel_id|flavor|body]) and never include TxItem::origin, and its RTS carries
// `rin.src = _node_id` (node_mac.cpp:632), not the origin. Probe result in the T6 BASELINE note.
// Static reduction: flavor without channel_flavor_team ⇒ Plane::GLOBAL ⇒ flight_is_team_plane() false ⇒ the pre-T6
// `mob ? home_id : _node_id` verbatim. Build profiles: channel_flavor_team is an ungated protocol constant, so the
// helper compiles identically on the three gateway_* envs (MR_FEAT_TEAM 0), where stamp_origin's team arm is absent.
static inline Plane m_flavor_plane(uint8_t flavor) {
    return ((flavor & protocol::channel_flavor_team) != 0) ? Plane::TEAM : Plane::GLOBAL;
}

// ---- channel_msg_id mint (dv:2239): origin<<24 | (key_hash32 LOW 16)<<8 | ctr low 8, big-endian on wire.
uint32_t Node::channel_msg_id_mint(uint8_t origin, uint32_t key_hash32, uint8_t ctr) {
    return (static_cast<uint32_t>(origin) << 24)
         | ((key_hash32 & 0xffffu) << 8)
         | (static_cast<uint32_t>(ctr) & 0xffu);
}

// ---- seen_by bitmap helpers (neighbour id 0..255 -> bit) ---------------------------------------
static inline bool seen_test(const uint8_t* bm, uint8_t nbr) { return (bm[nbr >> 3] >> (nbr & 7)) & 1u; }

// DEBUG (trace_on only): one console line dumping my hops==1 neighbours + each one's covered bit in `bm`. This is
// THE diagnostic for the asymmetric-coverage suspicion (a flood seeds "nodes I hear" but coverage is "nodes that
// hear me"). Bounded stack buffer + the n<sizeof-8 guard make it truncation-safe (a small net is ~3 neighbours).
#if 0  // FLOOD-DBG disabled 2026-06-23 — re-enable (this + the call sites + the node.h decl) for bench flood diag
void Node::flood_log_coverage(const char* tag, uint32_t id, const uint8_t* bm) const {
    char buf[128];
    int n = snprintf(buf, sizeof buf, "flood %08lX %s nbrs:", (unsigned long)id, tag);
    for (uint8_t i = 0; i < _active->_rt_count && n > 0 && n < (int)sizeof buf - 8; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1)
            n += snprintf(buf + n, sizeof buf - (size_t)n, " %u=%c",
                          _active->_rt[i].dest, seen_test(bm, _active->_rt[i].dest) ? 'Y' : 'N');
    _hal.log(buf);
}
#endif
static inline bool seen_set(uint8_t* bm, uint8_t nbr) {        // returns true if newly set
    const uint8_t mask = static_cast<uint8_t>(1u << (nbr & 7));
    if (bm[nbr >> 3] & mask) return false;
    bm[nbr >> 3] |= mask; return true;
}

// ---- buffer find / seen_by (dv:3426 / 3434) ----------------------------------------------------
int Node::channel_buffer_find(uint32_t id) const {
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i) if (_active->_channel_buffer[i].id == id) return static_cast<int>(i);
    return -1;
}
bool Node::channel_mark_seen_by(uint32_t id, uint8_t neighbour) {
    const int i = channel_buffer_find(id);
    if (i < 0) return false;
    return seen_set(_active->_channel_buffer[i].seen_by, neighbour);
}
// Do we hold a channel msg whose id low-16 == lo? The M_BROADCAST RTS carries only the low 16 of the
// channel_msg_id; an overhearer uses this to SKIP the retune when it (probably) already has the msg (dv:2081).
bool Node::channel_have_id_lo16(uint16_t lo) const {
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i)
        if (static_cast<uint16_t>(_active->_channel_buffer[i].id & 0xffff) == lo) return true;
    return false;
}

// ---- per-origin anti-spam admission (dv:3456). Distinct-id count over a sliding window; a repeat
//      id REFRESHES (not re-counts) so a heavily re-gossiped legit msg can't false-throttle its
//      origin. origin==self bypasses (own posts use the origination self-cap). Returns admit. ----
bool Node::channel_origin_admit(uint8_t origin, uint32_t msg_id, bool team_plane) {
    if (_cfg.n_layers == 2) return false;                       // Principle 11: a dual-layer gateway is OUT of the channel plane (justifies cap_channel_buffer=8)
    if (origin == _node_id) return true;                        // self bypasses
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= _cfg.channel_origin_window_ms) ? now - _cfg.channel_origin_window_ms : 0;
    // ★★ §team-parity T6/B (spec §3/T6 Part B): the ledger key is (PLANE, origin), not the bare 8-bit origin. A team
    // member ingests BOTH a plain leaf M (origin = a STATIC node id) and a team-scoped M (origin = a TEAMMATE's
    // team_local_id) — ingest_channel_m says so at its own team gate ("a normal leaf M (team_id==0) falls through ->
    // ingested by everyone incl. team members (planes = BOTH)"), so the "the planes rarely co-relay the same origin id"
    // safety reason recorded in node.h was already false, not merely fragile. Two numerically-colliding origins (§18)
    // shared ONE windowed distinct-id ledger AND one last_flood_ms burst floor, so a teammate's post could consume a
    // static neighbour's budget and get the other's next post dropped by channel_min_interval_drop /
    // channel_drop_originator_throttle — a SUPPRESS failure, invisible except as a missing message.
    // Static reduction: team_plane=false ⇒ key == origin, the pre-T6 uint8_t value, so every static/leaf ledger row is
    // byte-identical. Build profiles: the composer is plain arithmetic on an ungated bool — no MR_FEAT_* dependence.
    const uint16_t ledger_key = static_cast<uint16_t>((team_plane ? 0x100u : 0u) | origin);
    ChannelOriginLedger& L = _active->_per_origin_channel[ledger_key];   // map insert-on-miss (default-constructed n=0)
    // Prune in place (keep in-window), refreshing the matching id; events are unique-id (dups refresh),
    // so the kept count IS the distinct count (matching the Lua's `seen` set).
    uint8_t k = 0; bool dup = false;
    for (uint8_t i = 0; i < L.n; ++i) {
        if (L.ev[i].t_ms < cutoff) continue;                    // aged out
        if (L.ev[i].id == msg_id) { L.ev[i].t_ms = now; dup = true; }
        L.ev[k++] = L.ev[i];
    }
    L.n = k;
    if (dup) return true;                                       // repeat id -> refreshed + admitted, not re-counted (never interval-blocked)
    uint16_t cap = channel_cap_origin();                        // Slice 1: SF/mesh/duty-aware cap (or the legacy flat cap when duty disabled)
    // B1 (2026-07-02 gate): the ledger L.ev[] holds only cap_channel_origin_events entries, but the policy cap
    // (channel_cap_origin) can reach ~32 at SF7/duty-ON. `L.ev[L.n++]` below would write past the array once L.n
    // passed 20. Clamp the ENFORCED count here to the array bound so the ledger can never be over-run (heap-OOB).
    if (cap > protocol::cap_channel_origin_events) cap = protocol::cap_channel_origin_events;
    // Slice 2 per-origin burst floor — a new DISTINCT flood too soon after this origin's last admitted one is dropped.
    // (Only the non-dup admit path reaches here; a refreshed dup returned above and is never interval-blocked.)
    if (L.last_flood_ms != 0 && now - L.last_flood_ms < _cfg.channel_min_interval_ms) {
        MR_EMIT("channel_min_interval_drop", EF_I("origin", origin), EF_I("msg_id", static_cast<int64_t>(msg_id)),
                EF_I("since_ms", static_cast<int64_t>(now - L.last_flood_ms)),
                EF_I("min_ms", static_cast<int64_t>(_cfg.channel_min_interval_ms)));
        return false;
    }
    if (L.n >= cap) {                                           // over the computed cap -> drop the frame entirely
        MR_EMIT("channel_drop_originator_throttle", EF_I("origin", origin), EF_I("msg_id", static_cast<int64_t>(msg_id)),
                EF_I("count", L.n), EF_I("threshold", cap), EF_I("window_ms", static_cast<int64_t>(_cfg.channel_origin_window_ms)));
        return false;
    }
    if (L.n < cap) { L.ev[L.n++] = { msg_id, now }; L.last_flood_ms = now; }   // record the new distinct id + stamp the flood time
    return true;
}

// ---- eviction pick (dv:3485): the OLDEST entry whose seen_by covers all live 1-hop neighbours
//      ("safe"); else the absolute oldest (index 0, "fallback"). No neighbours -> fallback. -------
int Node::channel_buffer_pick_eviction(bool* safe) const {
    *safe = false;
    if (_active->_channel_buffer_n == 0) return -1;
    uint8_t nbrs[protocol::cap_routes]; uint8_t nn = 0;         // live direct neighbours (rt hops==1)
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) nbrs[nn++] = _active->_rt[i].dest;
    if (nn == 0) return 0;                                      // no neighbours observed -> oldest (fallback)
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i) {         // oldest-first
        bool all = true;
        for (uint8_t j = 0; j < nn; ++j) if (!seen_test(_active->_channel_buffer[i].seen_by, nbrs[j])) { all = false; break; }
        if (all) { *safe = true; return static_cast<int>(i); }
    }
    return 0;                                                   // none fully seen -> oldest (fallback)
}

// ---- buffer add (dv:3511): evict (safe-then-oldest) when full, then append at the tail (FIFO). ---
void Node::channel_buffer_add(const ChannelEntry& e) {
    if (_active->_channel_buffer_n >= protocol::cap_channel_buffer) {
        bool safe = false;
        const int idx = channel_buffer_pick_eviction(&safe);
        if (idx >= 0) {
            [[maybe_unused]] const uint32_t evicted_id = _active->_channel_buffer[idx].id;
            // remove [idx], shift the tail down to keep insertion order (oldest at [0])
            const uint16_t tail = static_cast<uint16_t>(_active->_channel_buffer_n - idx - 1);
            if (tail) std::memmove(&_active->_channel_buffer[idx], &_active->_channel_buffer[idx + 1], tail * sizeof(ChannelEntry));
            --_active->_channel_buffer_n;
            MR_EMIT("channel_msg_evicted", EF_I("id", static_cast<int64_t>(evicted_id)), EF_S("mode", safe ? "safe" : "fallback"));
        }
    }
    _active->_channel_buffer[_active->_channel_buffer_n++] = e;
}

// ---- promiscuous-overhear pull cancel (dv:11006). We got `id` (overheard M-broadcast) OR saw a peer pull
//      it -> drop our matching pending pull(s) so we don't double-pull. The pending ring is populated by
//      process_channel_digest (Phase 2). -------------------------
void Node::cancel_channel_pull(uint32_t id, [[maybe_unused]] uint8_t overheard_from, bool peer_q) {
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i) {
        ChannelPullPending& p = _active->_channel_pull_pending[i];
        if (p.active && p.id == id) {
            p.active = false;
            if (peer_q) {   // a peer's Q already pulled this id -> stand down so we don't double-pull (dv:11831)
                MR_EMIT("channel_pull_suppressed", EF_I("id", static_cast<int64_t>(id)), EF_S("overheard_from", "peer_q"),
                        EF_I("peer", overheard_from));
            } else {        // we received the msg (overheard M-broadcast) -> drop the now-moot pull (dv:11006)
                MR_EMIT("channel_pull_suppressed", EF_I("id", static_cast<int64_t>(id)), EF_I("overheard_from", overheard_from));
            }
        }
    }
}

// ---- DATA-M ingestion (dv:10942): admit (gateways included) -> if !gateway, merge into the buffer
//      (new -> add+dirty+seen_by+cancel-pull; existing -> mark seen_by). The caller (handle_data)
//      handles the ACK / forward of the underlying DATA frame; this is the gossip side-effect. -----
void Node::ingest_channel_m(const m_out& m, uint8_t from) {
    if (_cfg.n_layers == 2) return;                            // Principle 11: a dual-layer gateway never ingests channel gossip
    if (m.leaf_id != _cfg.leaf_id && !same_team(m.team_id)) return;   // defensive leaf gate (dispatch already gated; tests call directly). §P2-1: a same-team M-frame is leaf-EXEMPT (mixed-leaf team channel); the m.team_id!=0 member gate below still contains it.
    if (m.team_id != 0) {                                      // §mobile 6.3: a TEAM-scoped M — only a member of THAT team ingests it. A static node / lone mobile / a DIFFERENT team drops it (never buffers, never re-floods -> team traffic stays off the static plane + out of other teams). A normal leaf M (team_id==0) falls through -> ingested by everyone incl. team members (planes = BOTH).
        if (!_cfg.is_mobile || _cfg.team_id != m.team_id) {
            const int fs = flood_state_find(m.channel_msg_id); // free any flood-state a DIFFERENT-team member alloc'd at the RTS, so it doesn't re-flood a foreign-team message
            if (fs >= 0) flood_state_free(static_cast<uint8_t>(fs));
            return;
        }
    }
    const uint32_t id     = m.channel_msg_id;
    const uint8_t  origin = static_cast<uint8_t>((id >> 24) & 0xff);    // the minter (dv:2912)
    // §team-parity T6/B: `m.team_id != 0` IS the plane of this M — the same field the member gate above uses (U1), and the
    // authoritative scope of the frame (a team M's origin is a team_local_id, a leaf M's is a static node id). Static
    // reduction: a leaf/global M has team_id==0 ⇒ team_plane=false ⇒ the pre-T6 bare-origin ledger key verbatim.
    if (!channel_origin_admit(origin, id, /*team_plane=*/m.team_id != 0)) {   // over per-(plane,origin) budget -> drop (not buffered/forwarded)
        const int fs = flood_state_find(id);                  // C1 (§4.3 step 1): free any flood-state so §4.4 does
        if (fs >= 0) flood_state_free(static_cast<uint8_t>(fs)); // NOT fast-self-pull a deliberately-throttled message
        return;
    }
    if (_cfg.is_gateway && _cfg.gateway_only) return;          // §7 CONSUMER: a gateway+owner stores+delivers; a pure bridge stays out
    // The lean M frame carries no addressing — a receipt is a "pull_target" iff we have a pull pending for this
    // id (we asked for it), else "overheard" (it reached us via the flood / a peer's broadcast). The flood/pull
    // mechanics below are unchanged; only the source label is now derived from our own state, not the wire.
    bool was_pulled = false;
    for (uint8_t i = 0; i < protocol::cap_channel_pull_pending; ++i)
        if (_active->_channel_pull_pending[i].active && _active->_channel_pull_pending[i].id == id) { was_pulled = true; break; }
    const int existing = channel_buffer_find(id);
    if (existing < 0) {                                        // NEW -> buffer it
        ChannelEntry e{};
        e.id = id; e.channel_id = m.channel_id; e.flavor = m.flavor; e.origin = origin;   // §6.3: e.flavor carries the team bit; e.team_id below scopes the buffered entry so a re-broadcast/pull re-stamps it
        e.team_id = m.team_id;
        e.dirty = true; e.bcn_ad_count = 0; e.received_at = _hal.now();
        seen_set(e.seen_by, from);                            // the immediate sender holds it
        e.payload_len = static_cast<uint16_t>(m.body.size() > protocol::channel_msg_max_payload_bytes
                                              ? protocol::channel_msg_max_payload_bytes : m.body.size());
        if (e.payload_len) std::memcpy(e.payload, m.body.data(), e.payload_len);
        channel_buffer_add(e);
        // Record-on-delivery + app push — but NOT for our OWN posts. A node can re-encounter a channel
        // message it minted (its buffer entry was evicted, then the message came back via a peer's re-flood
        // or a digest pull) -> origin == _node_id. The app already shows that post as "sent"; recording +
        // pushing it would echo it back as "received" (the app can't dedup — its sent copy has no
        // channel_msg_id). So skip the inbox/app side for self-originated messages; the gossip/flood
        // mechanics below still run (forwarding is unaffected).
        if (origin != _node_id) {
            // Record-on-delivery FIRST (once per msg): returns the inbox seq (0 if disabled). Store the FULL
            // 32-bit channel_msg_id (the exact identity the app dedups by). The live channel_recv push carries
            // the SAME channel_msg_id + seq -> the app unifies live+pulled + detects gaps (model B).
            const uint8_t rx_layer = active_layer_id();   // §2/Q13: the receiving layer (leaf-local; gateways skip channels)
            // ★★ §chan-crypt CL2a — DELIVERY-TO-SELF is the ONLY place a channel body is ever opened (T-K2 §2.2).
            // The BUFFERED entry deliberately stays SEALED: we may still have to serve this id to a puller or
            // re-flood it, and it must go back out byte-identical. So we open into a LOCAL buffer and the plaintext
            // reaches exactly two places, the durable record and the app push.
            // ⚠ A FAILURE HERE DROPS THE CONTENT, NEVER THE FRAME. The entry is already buffered above and the
            // flood/forward decision below still runs — a member who cannot read the post must still relay it for
            // everyone who can (that is precisely T-K2's content-blind-relay rule, and breaking it would let one
            // key-less member sever a team's channel).
            // ★ THE PLAINTEXT LANDS DIRECTLY IN THE PUSH'S OWN BODY — no scratch buffer of any kind, and that is a
            // MEASURED choice, not a stylistic one. `Push::body` is already a 241-B buffer on this frame and 241 >=
            // channel_seal_max_plaintext_bytes (174), so opening into it costs ZERO extra bytes. The two rejected
            // alternatives both cost real memory on the target that has least of it: a 174-B STACK array on the RX
            // path is the shape of the fault-log stack overflow (e2e_open_relay refuses it for exactly this reason),
            // and the function-STATIC that idiom uses instead measured +174 B of permanent nRF52840 .bss by `nm`
            // (xiao_sx1262 RAM +184 instead of +8) for a buffer that is live for microseconds. Hoisting `pu` is the
            // whole trick; it is default-constructed either way, just earlier.
            Push pu{};
            const uint8_t* app_body = e.payload;
            uint8_t        app_len  = static_cast<uint8_t>(e.payload_len);
            uint8_t        enc      = 0;
            bool           readable = true;
            if (e.flavor & protocol::channel_flavor_crypted) {
                uint8_t clear_len = 0;
                // ★ §chan-crypt CL2b: `opened` (the TAG passed) and `readable` (we can actually render it) are now TWO
                // different things — a well-sealed post with a malformed inner is opened but unreadable, and the two
                // outcomes need DIFFERENT remedies, so they cannot share one flag any more.
                const bool opened = channel_open_body(id, m.channel_id, e.payload, e.payload_len, pu.body, clear_len);
                readable = opened;
                if (opened) {
                    // ★★★ §chan-crypt CL2b/CL2c — PARSE THE SEALED INNER
                    // `[flags u8][source_hash 4 if bit2][loc 6 if bit1][text if bit0]`.
                    // The plaintext is already in `pu.body`; the text is shifted DOWN over the header so `pu.body`
                    // ends up holding exactly what the app and the inbox record want, with no second buffer.
                    const uint8_t  fl       = clear_len ? pu.body[0] : 0u;
                    const bool     has_text = (fl & protocol::channel_inner_flag_text) != 0;
                    const bool     has_loc  = (fl & protocol::channel_inner_flag_location) != 0;
                    const bool     has_src  = (fl & protocol::channel_inner_flag_source) != 0;
                    const uint16_t hdr      = protocol::channel_inner_overhead(fl);
                    // MALFORMED ⇒ DROP THE CONTENT, and it is a DISTINCT outcome from "cannot decrypt" — we hold the
                    // right key and the tag PASSED, so prompting for a key (team_channel_no_key) would send the
                    // operator after a remedy that changes nothing. Five ways to be malformed, all fail-loud (C2):
                    //   · no flags byte at all (a 26-B sealed body — zero-length inner);
                    //   · flags == 0 — neither text nor position, i.e. the empty post the SEND side refuses;
                    //   · an UNKNOWN bit — a future field of unknown width sits between the flags byte and the
                    //     variable-length text, so this reader cannot locate the text. Refusing beats guessing;
                    //   · ★ §chan-crypt CL2c: bit1 WITHOUT bit2 — a position with no sender. That is the single
                    //     cross-bit rule, and refusing it here is the whole point of the slice: an unattributable
                    //     position must not reach the app at all, let alone the address book. The CONVERSE is legal —
                    //     bit2 alone is a post that names its sender and carries no position, and it parses fine;
                    //   · a length that contradicts the flags (bit2/bit1 with no room for their fixed fields; bit0
                    //     with no text byte; or trailing bytes with bit0 CLEAR, which would be an unannounced field).
                    // ⚠ The frame is STILL BUFFERED AND STILL RELAYED — a content failure is never a frame failure
                    // (T-K2's content-blind-relay rule, verbatim the reasoning of the open-failure arm below).
                    const bool malformed = (clear_len < 1) || (fl == 0)
                                        || ((fl & ~protocol::channel_inner_flags_known) != 0)
                                        || (has_loc && !has_src)
                                        || (clear_len < hdr)
                                        || (has_text ? (clear_len == hdr) : (clear_len != hdr));
                    // ★★ §chan-crypt CL2c — THE CARRIED SENDER, and its CONSISTENCY ASSERTION against the msg-id.
                    // Both quantities live INSIDE the AEAD envelope (the inner is the ciphertext; the msg-id is bound
                    // into both the nonce and the AAD), so only a keyholder can produce a pair at all — and our own
                    // sender makes them agree by construction (do_send_channel writes `_key_hash32`, the same value
                    // channel_msg_id_mint hashed into the id's 16 bits). ⇒ a disagreement is a FORGED or CORRUPT post,
                    // never an ordinary condition, and it is refused rather than half-honoured.
                    // ⚠ A ZERO hash is refused for the same reason: it names nobody, `peer_loc_set` cannot key on it,
                    // and the send side already refuses to originate one (Node::on_command's no-identity gate).
                    // ⚠ THIS IS NOT AUTHENTICATION and the check must not be read as such — the team content key is
                    // SHARED, so a keyholder can mint an id and an inner that agree on ANY member's hash. See
                    // PeerLocSrc (node.h) for the bound this whole plane accepts. What it does buy is defence in depth
                    // against a corrupt/mis-built sender, for free, on a field we now carry anyway.
                    uint32_t sender_hash = 0;
                    if (!malformed && has_src) {
                        sender_hash = static_cast<uint32_t>(pu.body[1])
                                    | (static_cast<uint32_t>(pu.body[2]) << 8)
                                    | (static_cast<uint32_t>(pu.body[3]) << 16)
                                    | (static_cast<uint32_t>(pu.body[4]) << 24);
                    }
                    const bool src_bad = !malformed && has_src
                                      && (sender_hash == 0 || (sender_hash & 0xffffu) != ((id >> 8) & 0xffffu));
                    if (malformed || src_bad) {
                        readable = false;
                        if (src_bad) MR_EMIT("channel_inner_source_mismatch", EF_I("id", static_cast<int64_t>(id)),
                                             EF_I("channel_id", m.channel_id),
                                             EF_I("source_hash", static_cast<int64_t>(sender_hash)),
                                             EF_I("id_hash16", static_cast<int64_t>((id >> 8) & 0xffffu)));
                        else MR_EMIT("channel_inner_malformed", EF_I("id", static_cast<int64_t>(id)),
                                     EF_I("channel_id", m.channel_id), EF_I("flags", fl), EF_I("len", clear_len));
                    } else {
                        enc = 1;
                        if (has_loc) {
                            int32_t lat = 0, lon = 0;
                            unpack_loc6(std::span<const uint8_t>(pu.body + hdr - protocol::channel_inner_location_bytes,
                                                                 protocol::channel_inner_location_bytes), lat, lon);
                            pu.has_location = true; pu.lat_e7 = lat; pu.lon_e7 = lon;   // the SAME Push fields a located DM sets (U1)
                            // ★★★ §AB4 RETENTION, `team` SOURCE — the call AB4 built the `loc_src` field for and marked
                            // `✖ MISSING` with CL2 as its trigger (node.h PeerLocSrc). ONE call: no schema change, no new
                            // field, no new PushKind. The TRUST ANCHOR is the team CONTENT key (owner ruling O5 — "if a
                            // message is sent using team encrypted message we treat it as trusted"), so this position is
                            // stored whether or not we hold this peer's ed_pub — but it is GROUP-anchored, never pairwise,
                            // which is exactly what PeerLocSrc::team records and what the app must render differently.
                            // ★★ WHICH HASH — CL2b INFERRED it, CL2c READS IT OFF THE POST, and that is the slice:
                            //   · the msg-id carries only SIXTEEN hash bits (`origin<<24 | (key_hash32 & 0xffff)<<8 |
                            //     ctr8`), so it never could be the key on its own — a truncated hash collides across
                            //     peers and does not match the 32-bit key every other AB table joins on;
                            //   · CL2b therefore resolved `origin` (a team_local_id) through `_team_keys`, i.e. through
                            //     the sender's BEACON. ⚠ That FAILED in two measurable ways — no beacon heard
                            //     (`unknown_team_peer`) and a DAD-re-picked id resolving to the wrong peer — after
                            //     which a position was shown to the app and silently never retained;
                            //   · ⇒ the sender's FULL hash is now carried in the sealed inner (bit2), REQUIRED beside a
                            //     position, and cross-checked against the id above. `_team_keys` is NOT consulted here
                            //     any more: re-introducing it would re-introduce exactly the staleness this removes.
                            //     (`team_key_of_id` is untouched and still serves addressing — reqpubkey, crypted
                            //     send-by-team-id; only THIS reader stopped inferring.)
                            MR_EMIT("peer_location", EF_I("origin", origin),
                                    EF_I("hash", static_cast<int64_t>(sender_hash)),
                                    EF_I("lat_e7", lat), EF_I("lon_e7", lon), EF_S("src", "team"));
                            (void)peer_loc_set(sender_hash, lat, lon, PeerLocSrc::team);   // sender_hash is non-zero and id-consistent by the gate above
                        }
                        // ★ §chan-crypt CL2c: the sender rides the push under the field msg_recv already uses for the
                        // same quantity (U1). 0 for a post that did not name one — the app's "unknown sender" value,
                        // identical to a DM with no SOURCE_HASH.
                        pu.sender_hash = sender_hash;
                        app_len = static_cast<uint8_t>(clear_len - hdr);   // 0 when bit0 is clear (a position-only post)
                        if (app_len) std::memmove(pu.body, pu.body + hdr, app_len);   // OVERLAPPING (hdr >= 1 always) — memmove, never memcpy
                        app_body = pu.body;
                    }
                } else {
                    // ⚠ dm_open WIPES its output on a tag failure, so `pu.body` is all-zero here, not a partial
                    // forgery — and nothing is enqueued on this path anyway.
                    // Two causes, ONE remedy — "get the current team content key" — so ONE push kind, and the
                    // telemetry carries the distinction for the bench: `no_key` (we hold none) vs `open_failed`
                    // (we hold one that does not open this post: a stale key after a re-key, a foreign key, or a
                    // tampered/re-attributed body the AAD caught).
                    // (the have-key test is INLINE for the same reason the seal's reason string is — a local read
                    // only inside MR_EMIT is unused on a board build, and warnings are gate-blocking.)
                    MR_EMIT("channel_crypt_undecryptable", EF_I("id", static_cast<int64_t>(id)),
                            EF_I("channel_id", m.channel_id),
                            EF_S("reason", team_channel_priv() != nullptr ? "open_failed" : "no_key"));
                    const uint64_t nw = _hal.now();
                    if (nw >= _team_ch_nokey_push_next_ms) {          // next-ALLOWED, not last-pushed — see the member's note
                        _team_ch_nokey_push_next_ms = nw + protocol::team_channel_no_key_push_min_ms;
                        Push nk{};
                        nk.kind = PushKind::team_channel_no_key; nk.origin = origin; nk.channel_id = m.channel_id;
                        nk.layer_id = rx_layer; nk.channel_msg_id = id; nk.team_id = m.team_id;
                        enqueue_push(nk);
                    }
                }
            }
            if (readable) {                    // plaintext post, or a sealed one we opened. Unreadable -> NOTHING is
                                               // inboxed and NO channel_recv follows: the app must never see a row it
                                               // cannot render, and a ciphertext "message" is exactly that.
                const uint32_t seq = _inbox.record_channel(m.channel_id, id, rx_layer, app_body,
                                                           app_len, _hal.now(), m.team_id, enc);   // §S5: durable team scoping; §chan-crypt CL2a: enc
                pu.kind = PushKind::channel_recv; pu.origin = origin; pu.channel_id = m.channel_id;
                pu.layer_id = rx_layer;            // §2/Q13: the receiving layer
                pu.channel_msg_id = id;            // the FULL 32-bit channel id — the app's dedup identity (matches the inbox record)
                pu.team_id = m.team_id;            // §S4: team scoping (0 = a leaf channel -> write_push omits it)
                pu.seq = seq;                      // the inbox per-store seq (0 = inbox disabled -> write_push omits it)
                pu.enc = (enc != 0);               // §chan-crypt CL2a: the post arrived SEALED and `body` is the opened plaintext (the field existed, hardcoded false for channels)
                pu.body_len = static_cast<uint8_t>(app_len > protocol::max_payload_bytes_hard_cap
                                                   ? protocol::max_payload_bytes_hard_cap : app_len);
                if (app_body != pu.body)                 // the SEALED path already opened straight into pu.body
                    for (uint8_t i = 0; i < pu.body_len; ++i) pu.body[i] = app_body[i];
                enqueue_push(pu);
            }
        }
        MR_TELEMETRY(
            const char* src = was_pulled ? "pull_target" : "overheard";
            EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                               { .key = "channel_id", .type = EventField::T::i64, .i = m.channel_id },
                               { .key = "source",     .type = EventField::T::str, .s = src },
                               { .key = "from",       .type = EventField::T::i64, .i = from } };
            _hal.emit("channel_msg_received", f, 4); );
        if (!was_pulled) {                                    // we got it without asking -> the analyzer's flood/cascade-overlap signal (dv:11001)
            MR_EMIT("channel_msg_overheard", EF_I("id", static_cast<int64_t>(id)), EF_I("channel_id", m.channel_id), EF_I("from", from));
        }
        cancel_channel_pull(id, from);                        // we got it -> drop any pending pull for it
        // FLOOD §4.3 step 3: if a flood-state is waiting on this DATA-M (i.e. it arrived via a FLOOD RTS-M),
        // cache the body into it (needed to re-flood) and run the forward decision (§4.5: silent | arm backoff).
        const int slot = flood_state_find(id);
        if (slot >= 0) {
            FloodState& fs = _active->_flood[slot];
            fs.awaiting_data = false; fs.channel_id = m.channel_id; fs.flavor = m.flavor;
            fs.body_len = static_cast<uint8_t>(e.payload_len);
            for (uint8_t k = 0; k < fs.body_len; ++k) fs.body[k] = e.payload[k];
            flood_forward_decision(static_cast<uint8_t>(slot));
        }
    } else {                                                   // ALREADY HAVE IT -> just track the holder
        channel_mark_seen_by(id, from);
        channel_reoffer_confirm(id);                           // Part 2: a relay of OUR message (DATA-M/M-frame from `from`) was overheard -> stop re-offering
        MR_EMIT("channel_msg_already_present", EF_I("id", static_cast<int64_t>(id)), EF_I("from", from));
    }
}

// =============================================================================
// §chan-crypt CL2a — the SEALED team channel post (T-K2 §2.2, spec 2026-07-30 §2.1/§3.1)
// =============================================================================
//
// WHERE THE SEAL SITS, and why it is exactly one place at each end:
//   SEAL   — do_send_channel, immediately after the channel_msg_id is minted. `ChannelEntry::payload` then holds the
//            CIPHERTEXT, so the buffer, the FLOOD DATA-M, the cached re-flood body, the pull-response M and the
//            digest all carry it verbatim. ⇒ the relay plane stays CONTENT-BLIND by construction — not one line of
//            flood/pull/digest code changes, and a member WITHOUT the key still forwards the post normally.
//   OPEN   — ingest_channel_m, on DELIVERY-TO-SELF only, into a local buffer. The BUFFERED entry stays sealed
//            (we must be able to re-serve the exact bytes to a puller). Only the inbox record + the channel_recv
//            push see plaintext.
//
// ★★ §chan-crypt CL2b — WHAT IS INSIDE THE SEAL. The sealed plaintext is NOT the bare post text (it was, for exactly
// one slice): it is `[flags u8][location 6 B if bit1][text if bit0]`, built in do_send_channel and parsed in
// ingest_channel_m. `flags` is a FLAGS byte, not an enumerated type — see protocol_constants.h for the reasoning and
// for why 6-byte pack_loc6 rather than the 8 T-K2 sketched. NOTHING about the seal, the nonce, the AAD or the wire
// changed to accommodate it: the inner is opaque to all of them, which is why the whole location feature needed no
// wire_version bump and no new frame field. A PLAINTEXT post's body is still the bare text (bit1 requires the crypted
// flavour, spec §2.4 — a position in a clear channel post is register-B0's leak broadcast wider).
//
// ★★★ THE NONCE — the review point the spec names, spelled out in full because getting it wrong is catastrophic.
// The primitive is the DM's: `dm_nonce(nonce, rand8, ctr, x32) = BLAKE2b-512(rand8 | ctr LE2 | x32 LE4)[:24]`,
// reused unchanged (U1/U2 — no third scheme). A channel post supplies:
//   rand8  = 8 FRESH bytes from the HAL CSPRNG, drawn per post and CARRIED in the body. THE LOAD-BEARING UNIQUIFIER.
//   ctr    = ++_channel_seal_ctr, a dedicated per-node counter, CARRIED in the body. Defence in depth ONLY.
//   x32    = ★ the channel_msg_id — NOT the DM's dst_key_hash32, and NOT the team key hash. See below.
//
// ⚠⚠ WHY THE MSG-ID AND NOT THE TEAM KEY HASH, which is what the dispatch settled on. EVERY MEMBER SEALS UNDER THE
// SAME KEY, so the hazard is a nonce collision BETWEEN TWO MEMBERS, and the fix must be something that DIFFERS
// between them and is derivable by every reader from CLEARTEXT.
//   · The team key hash is IDENTICAL for every member, so it contributes exactly nothing to cross-member
//     separation — and it is not needed for domain separation either, since the AEAD KEY is already team-specific
//     (two teams' keystreams differ whatever the nonce). It is bound in the AAD instead, where it does real work.
//   · The channel_msg_id is `origin<<24 | (sender key_hash32 & 0xffff)<<8 | ctr8` (channel_msg_id_mint). It is the
//     M frame's own id field, IN THE CLEAR, so every reader has it before opening — and it carries 24 bits that
//     DISTINGUISH THE SENDER (the 8-bit plane-local origin + 16 bits of the sender's stable key hash). ⇒ two
//     members' nonces differ even if their seed8 AND their ctr collided.
//   · The dispatch also asked for "the sender bound in the AAD". ⚠ AAD BINDING CANNOT PREVENT KEYSTREAM REUSE —
//     XChaCha20's keystream is f(key, nonce) alone and the AAD only enters the Poly1305 tag. Binding the sender
//     into the NONCE is the property that was actually wanted, and that is what this does; the msg_id is ALSO in
//     the AAD, so the requirement is met in both places and the stronger one is the real one.
//   · The full 32-bit sender hash is deliberately NOT put on the wire to bind more: it would cost 4 B/post, would
//     leak the sender's stable identity to non-members (today only the 8-bit plane-local origin and 16 hash bits
//     leak, via the id), and would buy nothing under a shared key.
// R7's all-zero-seed refusal is KEPT verbatim (a dead RNG would collapse uniqueness onto the 16-bit ctr — refuse,
// never reuse). A per-boot seed or a ctr-only nonce would be keystream reuse under a static shared key.
//
// AAD = [team_content_key_hash32 4 LE][channel_msg_id 4 LE][channel_id 1], all cleartext-derivable by any reader:
//   · the KEY HASH binds the post to ITS TEAM's key (a sealed body cannot be replayed into another team's channel);
//   · the MSG ID binds the ciphertext to its WIRE IDENTITY — a relay cannot re-attribute a body to a different
//     origin/ctr, which for a flooded frame is a real integrity property, not decoration;
//   · the CHANNEL ID binds it to the channel it was posted on.
//
// Body on the wire = [seal_ctr 2 LE][seed8 8][ct‖tag] — byte-for-byte the shape build_sealed_relay_body already
// uses (node_hashlocate.cpp), adopted for exactly the same reason: the frame carries no ctr this end can use (an M
// frame has no ctr field at all — enqueue_channel_m DERIVES its MAC ctr from the id), so the seal ctr and the seed
// must ride in the body. U1: same layout, same offsets, same reasoning.
namespace {
constexpr char     kChanKeyDomain[]  = "MR-TEAM-CH-v1";        // domain separation — NO trailing NUL is hashed
constexpr size_t   kChanKeyDomainLen = sizeof(kChanKeyDomain) - 1;
constexpr uint8_t  kChanSealHdrLen   = 2 + DM_NONCE_SEED_LEN;  // [seal_ctr 2][seed8 8] = the clear prefix
constexpr uint8_t  kChanAadLen       = 4 + 4 + 1;              // [team_key_hash32][channel_msg_id][channel_id]
inline void chan_aad(uint8_t aad[kChanAadLen], uint32_t team_key_hash32, uint32_t msg_id, uint8_t channel_id) {
    aad[0] = uint8_t(team_key_hash32);       aad[1] = uint8_t(team_key_hash32 >> 8);
    aad[2] = uint8_t(team_key_hash32 >> 16); aad[3] = uint8_t(team_key_hash32 >> 24);
    aad[4] = uint8_t(msg_id);                aad[5] = uint8_t(msg_id >> 8);
    aad[6] = uint8_t(msg_id >> 16);          aad[7] = uint8_t(msg_id >> 24);
    aad[8] = channel_id;
}
}  // namespace
// The one place protocol_constants.h's numeric overhead meets dm_crypto.h's real sizes.
static_assert(protocol::channel_seal_overhead_bytes == 2 + DM_NONCE_SEED_LEN + DM_TAG_LEN,
              "protocol_constants.h: channel_seal_overhead_bytes no longer matches [seal_ctr 2][seed8][tag]");

// The team CONTENT key: ONE symmetric AEAD key per team keypair, derived from the private half every member holds.
// BLAKE2b-512("MR-TEAM-CH-v1" | team_ch_priv)[:32] — the dm_kdf shape (domain string, hash, truncate), separate
// domain string so this key can never coincide with a DM per-pair key or any future use of the same scalar.
// ★ NOT an ECDH to team_ch_pub: `team_ch_priv` is already shared with every member (T-K3 grants the private half,
// T-K4's QR carries it), so an ECDH would add no confidentiality while making a post unreadable to any keyholder
// that lacks the SENDER's pubkey — which contradicts T-K2's own requirement, "Everyone holding team_ch_priv opens".
// The security bound this accepts is stated once, at Node::PeerLocSrc (node.h): sealing proves MEMBERSHIP, not
// identity — a keyholder can impersonate another keyholder; an outsider can do neither.
// false = keyless (the buffer is left untouched — never a zero key). Inert on MR_FEAT_TEAM 0: team_channel_priv()
// is the `return nullptr` stub there, so the compiler folds this to `return false` and drops the body.
bool Node::channel_content_key(uint8_t key[32]) const {
    const uint8_t* priv = team_channel_priv();
    if (!priv) return false;
    uint8_t msg[kChanKeyDomainLen + 32];
    std::memcpy(msg, kChanKeyDomain, kChanKeyDomainLen);
    std::memcpy(msg + kChanKeyDomainLen, priv, 32);
    uint8_t full[64];
    crypto_blake2b(full, 64, msg, sizeof msg);       // BLAKE2b-512 ...
    std::memcpy(key, full, 32);                      // ... truncated to 32, exactly as dm_kdf does
    crypto_wipe(full, sizeof full);
    crypto_wipe(msg, sizeof msg);                    // msg held the raw private scalar — wipe it
    return true;
}

// SEAL `pt` -> `out` = [seal_ctr 2 LE][seed8 8][ct‖tag]. Returns the body length, or 0 with `outcome` set.
// The caller pre-flights the two conditions an operator can act on (no key / too large); the arms here are the
// structural backstops plus the one failure that cannot be pre-checked, R7's dead RNG.
uint8_t Node::channel_seal_body(uint32_t msg_id, uint8_t channel_id, const uint8_t* pt, uint8_t pt_len,
                                uint8_t* out, uint8_t out_cap, SealOutcome& outcome) {
    // no_identity is R3's "never seal under a zero key" outcome and it is the right one here too: we hold no key
    // material to seal WITH. (Not no_pubkey — there is no recipient pubkey in this keying model at all.)
    outcome = SealOutcome::no_identity;
    uint8_t key[32];
    if (!channel_content_key(key)) return 0;
    const uint8_t* pub = team_channel_pub();                       // non-null whenever priv is — one presence flag governs both
    const uint32_t tk_hash = key_hash32_of(pub);
    outcome = SealOutcome::too_large;
    const size_t total = static_cast<size_t>(kChanSealHdrLen) + pt_len + DM_TAG_LEN;
    if (total > out_cap) { crypto_wipe(key, 32); return 0; }
    uint8_t seed[DM_NONCE_SEED_LEN];
    _hal.rand_bytes(seed, DM_NONCE_SEED_LEN);                      // THE uniquifier — fresh per post, carried below
    // R7 (the e2e_seal_inner guard, verbatim and for a STRICTLY worse case): a broken CSPRNG returning an all-zero
    // seed collapses nonce uniqueness onto the 16-bit ctr — and here the key is SHARED, so that is keystream reuse
    // across the whole team, not just across one pair. Refuse loudly rather than seal under a degenerate nonce.
    bool seed_zero = true; for (size_t i = 0; i < DM_NONCE_SEED_LEN; ++i) if (seed[i]) { seed_zero = false; break; }
    if (seed_zero) { crypto_wipe(key, 32); outcome = SealOutcome::bad_rng; return 0; }
    const uint16_t seal_ctr = ++_channel_seal_ctr;
    uint8_t nonce[DM_NONCE_LEN]; dm_nonce(nonce, seed, seal_ctr, msg_id);   // ★ msg_id, not the key hash — see the header
    uint8_t aad[kChanAadLen];    chan_aad(aad, tk_hash, msg_id, channel_id);
    out[0] = uint8_t(seal_ctr); out[1] = uint8_t(seal_ctr >> 8);
    for (size_t i = 0; i < DM_NONCE_SEED_LEN; ++i) out[2 + i] = seed[i];
    uint8_t tag[DM_TAG_LEN];
    dm_seal(out + kChanSealHdrLen, tag, key, nonce, aad, sizeof aad, pt, pt_len);
    for (size_t i = 0; i < DM_TAG_LEN; ++i) out[kChanSealHdrLen + pt_len + i] = tag[i];
    crypto_wipe(key, 32);
    outcome = SealOutcome::ok;
    return static_cast<uint8_t>(total);
}

// OPEN a sealed body. false = we cannot read it — keyless, malformed, or the TAG FAILED (wrong/stale key, a
// tampered body, or a body re-attributed to another msg_id/channel_id, all of which the AAD catches). A false is
// always a CONTENT drop, never a frame drop: the caller still buffers and relays (content-blind relaying is the
// whole point of T-K2's un-keyed-member rule). `out` must hold channel_seal_max_plaintext_bytes.
bool Node::channel_open_body(uint32_t msg_id, uint8_t channel_id, const uint8_t* body, uint16_t body_len,
                             uint8_t* out, uint8_t& out_len) {
    out_len = 0;
    uint8_t key[32];
    if (!channel_content_key(key)) return false;                   // keyless — the caller raises team_channel_no_key
    if (body_len < static_cast<uint16_t>(kChanSealHdrLen + DM_TAG_LEN)) { crypto_wipe(key, 32); return false; }
    const size_t ct_len = static_cast<size_t>(body_len) - kChanSealHdrLen - DM_TAG_LEN;
    if (ct_len > protocol::channel_seal_max_plaintext_bytes) { crypto_wipe(key, 32); return false; }   // cannot have been produced by our own seal -> refuse rather than overrun `out`
    const uint16_t seal_ctr = static_cast<uint16_t>(body[0] | (body[1] << 8));
    uint8_t nonce[DM_NONCE_LEN]; dm_nonce(nonce, body + 2, seal_ctr, msg_id);
    uint8_t aad[kChanAadLen];    chan_aad(aad, key_hash32_of(team_channel_pub()), msg_id, channel_id);
    const bool ok = dm_open(out, key, nonce, aad, sizeof aad, body + kChanSealHdrLen, ct_len,
                            body + kChanSealHdrLen + ct_len);
    crypto_wipe(key, 32);
    if (!ok) return false;                                         // dm_open wiped `out` on failure — no forged plaintext escapes
    out_len = static_cast<uint8_t>(ct_len);
    return true;
}

// ---- send_channel origination (dv:12126): mint an id, buffer it dirty (the next BCN digest will
//      advertise it; neighbours pull on demand — no proactive broadcast). Counts toward the unified
//      self-origination budget. Returns the per-origin ctr used. -----------------------------------
uint16_t Node::do_send_channel(uint8_t channel_id, const uint8_t* body, uint8_t body_len, bool crypt, bool with_location) {
    const uint64_t now = _hal.now();
    // §F-PS-1/§18: a TEAM-scoped channel flood must originate under the TEAM id, never the host-assigned STATIC local
    // id. do_send_channel is reached on the mobile ONLY for a `-t` team post (node.cpp:947); a static leaf/GLOBAL post
    // and the home's delegated re-originate are is_mobile==false -> origin stays _node_id (byte-identical). For a
    // DUAL (registered) member _node_id is its host-assigned static local id, which would otherwise leak into the
    // TEAM-plane channel_msg_id (a cross-universe id: it can collide another member's team id + mislabels team
    // history). Mirror node_beacon.cpp:273 — the team plane keys on _team_local_id. Off-grid: _node_id ==
    // _team_local_id already, so `origin` is unchanged (no stream shift). Same guard predicate as the team-scope
    // block below (:317). Used for the self-cap tally, next_ctr, the msg-id mint, and e.origin — all coherently.
    uint8_t origin = _node_id;
#if MR_FEAT_TEAM
    if (_cfg.is_mobile && _cfg.team_id != 0 && _team_local_id != 0) origin = _team_local_id;
#endif
    // Slice 2 self-GATE (MF4): apply the per-origin cap + the 10s burst floor to OUR OWN posts. This path does NOT
    // route through channel_origin_admit (which self-bypasses at :80), so the gate lives here. Blocked -> emit
    // send_blocked{channel} and mint nothing (no ctr consumed, nothing buffered/flooded).
    const uint16_t cap = channel_cap_origin();
    uint16_t used = 0;                                        // own distinct floods currently held (inline; no public helper)
    for (uint16_t i = 0; i < _active->_channel_buffer_n; ++i)
        if (_active->_channel_buffer[i].origin == origin) ++used;
    const char* block_reason = nullptr; uint32_t next_ms = 0;
    if (_last_channel_origin_ms != 0 && now - _last_channel_origin_ms < _cfg.channel_min_interval_ms) {
        block_reason = "min_interval";
        next_ms = static_cast<uint32_t>(_cfg.channel_min_interval_ms - (now - _last_channel_origin_ms));
    } else if (used >= cap) {
        block_reason = "cap"; next_ms = 0;                    // window-cap wait; Slice 5 fills the exact recovery
    }
    if (block_reason) {
        MR_EMIT("send_blocked", EF_S("kind", "channel"), EF_S("reason", block_reason), EF_I("next_ms", static_cast<int64_t>(next_ms)));
        // Slice 6a: the telemetry above is stripped on device — this Push is the send_blocked signal the companion
        // actually receives. Map the gate's block_reason string to the SendFailReason enum (min_interval | cap).
        const SendFailReason r = (block_reason[0] == 'm') ? SendFailReason::min_interval : SendFailReason::cap;
        emit_send_blocked(/*channel=*/true, r, next_ms);
        // ★★ §b39 — PRODUCER (1) OF THE `ctr == 0` SENTINEL. next_ctr never mints 0 (node_mac.cpp:20 wraps
        // 65535 -> 1), so this 0 is unambiguous — and Node::on_command hands it through UNCHANGED as
        // CmdResult{ queued, 0 }, where the FULL contract is written out (the send_channel arm's return, node.cpp).
        // The reason and the retry-after ride the send_blocked push above; the SYNCHRONOUS result carries neither, so
        // a caller budgeting attempts must test `ctr != 0` rather than `code == queued`.
        return 0;                                             // not sent (no ctr minted)
    }
    const uint16_t c = next_ctr(origin);
    const uint32_t id = channel_msg_id_mint(origin, _key_hash32, static_cast<uint8_t>(c & 0xff));
    ChannelEntry e{};
    e.id = id; e.channel_id = channel_id; e.flavor = protocol::channel_flavor_public; e.origin = origin;
    if (_cfg.is_mobile && _cfg.team_id != 0) {                    // §mobile 6.3: a team member's channel post IS the team broadcast — scope it to the team + set the team flavor bit (the M-frame carries team_id; the RTS-M gets mobile_src). A static/lone node: team_id 0, flavor unchanged -> byte-identical.
        e.team_id = _cfg.team_id; e.flavor |= protocol::channel_flavor_team;
    }
    e.dirty = true; e.bcn_ad_count = 0; e.received_at = now;
    // ★★★ §chan-crypt CL2b/CL2c — THE SEALED INNER `[flags u8][source_hash 4 if bit2][loc 6 if bit1][text if bit0]`
    // IS ASSEMBLED HERE, in `e.payload`, and it is assembled ONLY on the crypt path.
    // ⚠ WHY IN `e.payload` AND NOT IN A SCRATCH BUFFER: `ChannelEntry e` is already on this frame and its `payload` is
    // already a channel_msg_max_payload_bytes array, so building in place costs ZERO extra stack. A 174-B local in
    // Node::on_command (the obvious alternative) would put a buffer of exactly the shape the fault-log stack overflow
    // came from on the command path — the same measurement CL2a made when it chose `Push::body` over a scratch array.
    // ⚠ WHY ONLY ON THE CRYPT PATH: a PLAINTEXT post's body stays the BARE TEXT, byte-for-byte as before. bit1 requires
    // the crypted flavour anyway (spec §2.4 — a position in a plaintext channel post is register-B0's leak broadcast
    // wider), so a flags byte on the plain path would change every existing post on the wire and buy nothing.
    // ⚠ `with_location` WITHOUT `crypt` IS IGNORED, not honoured: Node::on_command already refuses that combination
    // (ruling O6) and this is the structural backstop making "a position never travels in clear" true even if a future
    // caller forgets — the same defence-in-depth shape node_mac.cpp's TYPE-19 guard uses.
    if (crypt) {
        // bit0 ⟺ there IS text; bit1 ⟺ `-l`; bit2 ⟺ bit1 (§chan-crypt CL2c — a located post ALWAYS names its sender,
        // and that rule lives inside channel_inner_flags so no call site can forget it). `flags == 0` is UNREACHABLE
        // here — Node::on_command refuses an empty post with no position before it can reach this line — and that
        // refusal is the only thing making it so.
        const uint8_t inner_flags = protocol::channel_inner_flags(body_len != 0, with_location);
        const uint8_t off = static_cast<uint8_t>(protocol::channel_inner_overhead(inner_flags));   // 1, or 11 with a position
        e.payload[0] = inner_flags;
        // ★ BOTH field positions are DERIVED FROM `off`, never tracked by a second cursor: source_hash is always the
        // first field after the flags byte, and the position is always the LAST fixed field, i.e. it ends exactly
        // where the text begins. A running offset would be a second definition of the layout and would be free to
        // drift from channel_inner_overhead — the one thing this helper exists to prevent.
        if (inner_flags & protocol::channel_inner_flag_source) {
            const uint8_t src_at = 1;
            // ★★ §chan-crypt CL2c — `_key_hash32`, 4 B LE, and it is THE SAME VALUE the msg-id was minted from three
            // lines above (channel_msg_id_mint(origin, _key_hash32, …)). ⇒ the carried hash and the id's 16 hash bits
            // agree BY CONSTRUCTION, which is what makes the receiver's cross-check a real assertion about the SENDER
            // rather than a tautology about the wire. ⚠ NOT `origin`: origin is a plane-local, DAD-assigned
            // team_local_id (and for a registered mobile it is _team_local_id, not _node_id); the hash is the stable
            // 32-bit identity every address-book table joins on.
            // Node::on_command refuses a `-l` post from a node with NO identity (_key_hash32 == 0), so a zero can
            // never be written here — the receiver refuses one anyway, both sides fail loud on the same invariant.
            e.payload[src_at]     = static_cast<uint8_t>(_key_hash32);
            e.payload[src_at + 1] = static_cast<uint8_t>(_key_hash32 >> 8);
            e.payload[src_at + 2] = static_cast<uint8_t>(_key_hash32 >> 16);
            e.payload[src_at + 3] = static_cast<uint8_t>(_key_hash32 >> 24);
        }
        if (inner_flags & protocol::channel_inner_flag_location) {   // pack_loc6 — the SAME 6-B encoding the DM inner carries (U1)
            const uint8_t loc_at = static_cast<uint8_t>(off - protocol::channel_inner_location_bytes);
            pack_loc6(_cfg.lat_e7, _cfg.lon_e7, std::span<uint8_t>(e.payload + loc_at, protocol::channel_inner_location_bytes));
        }
        const uint16_t room = static_cast<uint16_t>(protocol::channel_msg_max_payload_bytes - off);
        const uint16_t n    = (body_len > room) ? room : body_len;   // defensive: on_command pre-flights the real cap (174 total)
        if (n) std::memcpy(e.payload + off, body, n);
        e.payload_len = static_cast<uint16_t>(off + n);
    } else {
        e.payload_len = (body_len > protocol::channel_msg_max_payload_bytes)
                        ? protocol::channel_msg_max_payload_bytes : body_len;
        if (e.payload_len) std::memcpy(e.payload, body, e.payload_len);
    }
    // ★★ §chan-crypt CL2a — SEAL IN PLACE, right here and nowhere else. `e.payload` currently holds the sealed INNER
    // (§chan-crypt CL2b's `[flags][loc6?][text]`, built just above); from this line on it is the CIPHERTEXT, so every
    // carrier downstream (the buffer entry, the FLOOD DATA-M, the cached re-flood body, a pull-response M, the digest)
    // transports it verbatim and CONTENT-BLIND — no flood/pull/digest code changes, and the flags byte and the
    // position are INSIDE the seal where no relay and no eavesdropper can read them.
    // ⚠ It must happen AFTER the mint: the nonce and the AAD both bind `id`, so a body sealed before the id existed
    // could not be opened. It must also happen BEFORE channel_buffer_add: the buffered copy is what we later re-serve.
    // The size and no-key refusals are pre-flighted by the caller (Node::on_command) so an operator gets a specific
    // message; reaching them here would be a bug, and R7's dead-RNG arm CANNOT be pre-flighted. All three fail LOUD
    // and mint NOTHING further: the id's ctr is burned (next_ctr already advanced — visible, not silent), no buffer
    // row, no flood, no beacon. C2, and the same "refuse rather than seal degenerately" rule R7 itself encodes.
    if (crypt) {
        uint8_t sealed[protocol::channel_msg_max_payload_bytes];
        SealOutcome oc = SealOutcome::ok;
        const uint8_t n = channel_seal_body(id, channel_id, e.payload, static_cast<uint8_t>(e.payload_len),
                                            sealed, sizeof sealed, oc);
        if (n == 0) {
            // NB the reason string is INLINE, not a local: MR_EMIT compiles to nothing on device (telemetry is
            // sim-only), so a local used only inside it is -Wunused-variable on every board build — and warnings
            // are gate-blocking here.
            MR_EMIT("channel_seal_failed", EF_I("id", static_cast<int64_t>(id)),
                    EF_I("channel_id", channel_id),
                    EF_S("reason", (oc == SealOutcome::bad_rng)   ? "bad_rng"
                                 : (oc == SealOutcome::too_large) ? "too_large" : "no_key"));
            push_send_failed(oc == SealOutcome::bad_rng   ? SendFailReason::bad_rng
                           : oc == SealOutcome::too_large ? SendFailReason::too_large
                                                          : SendFailReason::unsealable, /*dst=*/0, /*ctr=*/c);
            // ★★ §b39 — PRODUCER (2) OF THE `ctr == 0` SENTINEL, and the WORSE of the two: the ctr was already minted
            // and is BURNED here, so the send_failed push above names `c` — a handle the caller NEVER RECEIVES, because
            // Node::on_command returns this 0 as CmdResult{ queued, 0 } (its send_channel arm, node.cpp, where the full
            // contract is written out). ⇒ the reason arrives asynchronously and correlates with nothing. next_ctr never
            // mints 0 (node_mac.cpp:20), so the 0 itself cannot be confused with a real handle.
            return 0;                                         // NOT sent (the caller's `queued` becomes ctr=0)
        }
        std::memcpy(e.payload, sealed, n);
        e.payload_len = n;
        e.flavor |= protocol::channel_flavor_crypted;         // ALWAYS alongside the team bit (on_command refuses `-e` without `-t`)
        crypto_wipe(sealed, sizeof sealed);
    }
    channel_buffer_add(e);
    _last_channel_origin_ms = now;                            // Slice 2: stamp for the next self-interval check
    // (Slice 3 removes self_originate_observe(); do_send_channel no longer shares the removed DM self-cap ledger)
    MR_TELEMETRY(
        // `payload` carries the post text so the analyzer (dm_delivery_breakdown.py) can match a post to its
        // msg_id — emit-parity with the Lua self_originate event (the tool keys Pass 1 on the payload).
        // ★ §chan-crypt CL2a: read the CLEARTEXT ARGUMENT, not `e.payload` — which is the ciphertext once the post
        // is sealed. Byte-identical on the plaintext path (e.payload IS `body` truncated the same way, so the same
        // bytes with the same cap), and on the sealed path it keeps the analyzer's msg-id↔text join working instead
        // of writing 200 bytes of binary into NDJSON. Telemetry is SIM-ONLY (stripped on device), so no secret
        // leaves a real node by this route.
        char pbuf[protocol::channel_msg_max_payload_bytes + 1];
        const uint16_t pl = (body_len > protocol::channel_msg_max_payload_bytes)
                            ? protocol::channel_msg_max_payload_bytes : body_len;
        for (uint16_t i = 0; i < pl; ++i) pbuf[i] = static_cast<char>(body[i]);
        pbuf[pl] = '\0';
        EventField f[] = { { .key = "id",         .type = EventField::T::i64, .i = static_cast<int64_t>(id) },
                           { .key = "channel_id", .type = EventField::T::i64, .i = channel_id },
                           { .key = "payload",    .type = EventField::T::str, .s = pbuf },
                           { .key = "source",     .type = EventField::T::str, .s = "self_originate" } };
        _hal.emit("channel_msg_received", f, 4); );
    // FLOOD origination (§4.1): seed the coverage bitmap {me + my hops==1 neighbours} and broadcast the FLOOD RTS-M +
    // DATA-M. A data-incapable node (no data SF) is non-operational (user rule) -> skip the flood, buffer-only; the
    // repair digest still covers it. No default-SF fallback.
    // 2026-06-26: the {neighbours} seed is a DELIBERATE divergence from the Lua's empty seed (frugality — a relay skips
    // the origin's OWN neighbours, which got the post directly from the origin). A 24-seed asymmetric sweep proved the
    // "honest"/empty seed REGRESSES coverage (it drops the neighbour skip -> more rebroadcast contention -> collisions
    // kill deliveries: 247 mean reach 4.04 -> 3.17) with NO orphan benefit, so it was dropped (spec Part 1). The origin
    // RE-OFFER (Part 2) + the repair-pull cover the asymmetric case the Lua handles via heavier flooding. Re-offer
    // confirmation is the dedicated "overheard a relay" flag (channel_reoffer_confirm) — INDEPENDENT of this seed.
    if (max_data_sf() != 0) {
        const bool team = (e.flavor & protocol::channel_flavor_team) != 0;   // §S7 T-A: a team post seeds coverage on the TEAM peer set (team ids), not the static rt
        uint8_t bm[32] = {};
        flood_set_my_coverage(bm, team);                     // {self + hops==1 neighbours} on the flood's plane — the frugal seed (KEPT; the honest seed regressed)
        enqueue_flood_m(e.channel_id, e.flavor, e.id, e.payload, static_cast<uint8_t>(e.payload_len), bm, protocol::flood_hop_max);
        channel_reoffer_register(e.id, team, c);             // Part 2: own this message's propagation until a RELAY of it is overheard (team: until all retries — mixed-chain coverage). §b40: `c` is the FULL 16-bit ctr this function returns to the caller, so the outcome push correlates to the origination handle past 255 posts (the msg-id keeps only `c & 0xff`, by wire design).
    }
    schedule_triggered_beacon();                              // §4.1.7: make the repair digest prompt, not 15-min
    return c;
}

#if MR_FEAT_MOBILE
// §S7 T-B: a registered mobile delegates a GLOBAL/leaf channel post to its HOME. A mobile can't originate a leaf
// flood on the static plane (empty _rt -> nothing to seed/cover), so it wraps [DATA_TYPE_CHANNEL_POST][channel_id][text]
// under a PLAINTEXT MOBILE_SEND (SOURCE_HASH=mobile via stamp_origin so the home verifies ours; DST_HASH=our own hash =
// a placeholder that satisfies the home's has_dst_hash unwrap gate, never used for the channel path;
// DATA_FLAG_MS_ENCLOSED_TYPE). The home strips it + re-originates via do_send_channel under ITS OWN origin/ctr —
// anti-spam bills the home (deliberate: hosting implies consenting to the mobile's channel share; the home's self-GATE
// applies). Returns false when there is no home (off-grid) -> the caller fails loud (no silent drop).
// ★★ §b39 — AND `true` DOES NOT COME WITH A HANDLE: the wrapper DM's ctr is discarded below `(void)do_send(...)` and
// the channel ctr is minted by the HOME, so Node::on_command reports this SUCCESS as `queued ctr=0` — the same
// synchronous shape its two blocked/failed paths produce. The full sentinel contract is on that arm's return (node.cpp).
bool Node::do_send_channel_delegated(uint8_t channel_id, const uint8_t* body, uint8_t body_len) {
    if (!(_cfg.is_mobile && _my_mobile_reg.active)) return false;   // off-grid: no home to delegate to
    uint8_t wbody[protocol::max_payload_bytes_hard_cap];
    wbody[0] = DATA_TYPE_CHANNEL_POST; wbody[1] = channel_id;
    uint8_t n = (body_len > protocol::channel_msg_max_payload_bytes) ? protocol::channel_msg_max_payload_bytes : body_len;
    if (static_cast<size_t>(2 + n) > sizeof wbody) n = static_cast<uint8_t>(sizeof wbody - 2);
    for (uint8_t i = 0; i < n; ++i) wbody[2 + i] = body[i];
    (void)do_send(_my_mobile_reg.home_id, wbody, static_cast<uint8_t>(2 + n),
                  DATA_FLAG_MS_ENCLOSED_TYPE, CryptIntent::off,
                  /*override_dst_hash=*/_key_hash32, /*type=*/DATA_TYPE_MOBILE_SEND);
    return true;
}
#endif

// Slice 6a: the pre-TX self-gate feedback push. do_send_channel (the channel self-GATE) and become_free (the DM
// own-origin throttle) call this when THIS node's cap / min-interval blocks an origination, so the companion holds
// + retries after next_ms instead of firing blind. Local-only (node -> its own trusted companion; no OTA). NB the
// gate sites ALSO keep their MR_EMIT("send_blocked") telemetry (native tests count it); that telemetry is stripped
// on device, so this Push is the ONLY send_blocked signal the companion actually receives on metal.
void Node::emit_send_blocked(bool channel, SendFailReason reason, uint32_t next_ms) {
    Push pu{}; pu.kind = PushKind::send_blocked;
    pu.blocked_channel = channel; pu.reason = reason; pu.next_ms = next_ms;
    enqueue_push(pu);
}

// Slice 6c: the OWN-channel-post outcome push. relayed=true when a relay of our post is overheard
// (channel_reoffer_confirm); relayed=false when the origin re-offer exhausts all retries with no relay
// (channel_reoffer_fire's give-up branch) -> the companion backs off. Local-only (node -> its companion).
//
// ★★★ §b38 — `relayed` MEANS TWO DIFFERENT THINGS DEPENDING ON THE PLANE, AND THAT IS THE OWNER'S RULING
// (2026-08-01: *"we name it as first relay only — we cannot guarantee full flood"*). State it wherever this field
// is read, because the two readings are not interchangeable for a safety consumer:
//   NON-TEAM (leaf/global): "THE FLOOD COMPLETED." One overheard relay IS coverage here — the origin's frugal
//     {self + hops-1 neighbours} seed plus one holder taking it onward is the whole propagation model, so the
//     re-offer is a 1-shot that stops dead on confirm. Unchanged by §b38.
//   TEAM: "AT LEAST ONE RELAY WAS OBSERVED." A mixed multi-hop team chain has far members a single near relay
//     never reached (the s28 class), so this is an OBSERVATION, never a completion claim — the node deliberately
//     keeps re-offering all its retries after reporting (channel_reoffer_confirm). A consumer must read a team
//     `true` as "someone heard me", not "everyone heard me".
// ⚠ Before §b38 the team plane could emit ONLY `false` (the `true` branch sat behind an early `return`), so every
// successful team post reported failure and the shipped companion's stop-and-back-off rule fired on success.
// The once-only latch is ChannelReofferPending::relay_seen: exactly one channel_sent per origination, and
// exhaustion must never contradict a `true` already sent.
// ⚠ §b40 — `ctr` is the FULL 16-bit originating counter (rp.ctr), no longer `id & 0xff`. It is a LOCAL
// CORRELATION HANDLE ONLY: the wire carries just the low byte (channel_msg_id_mint stuffs `c & 0xff` into the
// msg-id, deliberately — see the `item.ctr` masks in enqueue_channel_m / enqueue_flood_m), so no peer can echo
// more than 8 bits and this value must never be matched against a received message id.
void Node::emit_channel_sent(bool relayed, uint16_t ctr) {
    Push pu{}; pu.kind = PushKind::channel_sent; pu.relayed = relayed; pu.ctr = ctr;
    enqueue_push(pu);
}

// =============================================================================
// Phase 2 — digest gossip + the jittered pull. The ONLY rand draw in the channel
// plane is process_channel_digest's pull jitter, at the Lua's exact gate-order
// (after the have/cap/recent gates, before storage) so the streams stay aligned.
// =============================================================================

// build_channel_digest_ext — SELECT (dv:1426): walk the buffer NEWEST-first, pick up to channel_dirty_max_per_bcn
// DIRTY ids, pack the ext-TLV, and return the picked ids in `picked`/`npicked`. SIDE-EFFECT-FREE (B, 2026-06-23):
// the per-advertisement ad_count++/retire is COMMITTED only when the TRANSMITTER ADMITS the beacon (§tx-admission
// TX3, owner-ruled 2026-08-02: `_hal.tx` answers ok on the immediate path, or the deferred LBT timer reaches it) —
// an LBT-suppressed, ring-dropped or HAL-rejected beacon burns no advertisement. ⚠ NOT literal airtime: a later
// pump_tx radio-start error is outside the boundary. DRAW-FREE. Returns the TLV byte count.
size_t Node::build_channel_digest_ext(uint8_t* out, size_t cap, uint32_t* picked, uint8_t& npicked) {
    uint8_t count = 0;
    for (int i = static_cast<int>(_active->_channel_buffer_n) - 1; i >= 0 && count < protocol::channel_dirty_max_per_bcn; --i) {
        const ChannelEntry& e = _active->_channel_buffer[static_cast<uint16_t>(i)];
        if (e.dirty) picked[count++] = e.id;                       // SELECT only — no bcn_ad_count / dirty mutation here
    }
    npicked = count;
    if (count == 0) return 0;
    return pack_channel_digest_tlv(picked, count, std::span<uint8_t>(out, cap));
}

// Holder-aware retirement predicate (A, 2026-06-23): does every LIVE 1-hop neighbour already hold `e`? Same neighbour
// set as channel_buffer_pick_eviction (rt hops==1), but DELIBERATELY NOT shared — eviction's nn==0 path is
// fallback-evict-oldest (*safe=false), the OPPOSITE of retirement's nn==0=retire; merging would flip eviction's
// telemetry mode (fallback->safe), a silent regression. nn==0 -> no live neighbour to serve -> nothing to advertise
// -> retire. Else true iff every live 1-hop neighbour is in e.seen_by (they all hold it -> the repair-pull is moot).
bool Node::channel_entry_fully_seen(const ChannelEntry& e) const {
    uint8_t nbrs[protocol::cap_routes]; uint8_t nn = 0;
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) nbrs[nn++] = _active->_rt[i].dest;
    if (nn == 0) return true;                                      // no live 1-hop neighbour -> nothing to serve -> retire OK
    for (uint8_t j = 0; j < nn; ++j) if (!seen_test(e.seen_by, nbrs[j])) return false;
    return true;                                                   // every live 1-hop neighbour holds it
}

// commit_channel_digest_advertised — COMMIT (B): the per-advertisement side effects for the ids the TRANSMITTER
// ACCEPTED. ★ BOUNDARY RE-RULED BY THE OWNER 2026-08-02: "sent" here means **accepted by the transmitter/DeviceHal**
// — the strongest thing this architecture can observe — NOT literal RF airtime. ⚠ A later `DeviceHal::pump_tx`
// radio-start error drops the frame AFTER admission and is OUTSIDE this guarantee; do not read it as covered.
// The callers are now the two ADMISSION points, not emit_beacon: `tx_flood`'s immediate `_hal.tx == ok`, and
// node.cpp's LBT-defer arm when the deferred `lbt_complete` reaches DeviceHal and it answers ok. Re-find by id (indices may shift between select + commit; n<=3 so
// the cost is nil). ++bcn_ad_count, then RETIRE on HOLDER COVERAGE (channel_entry_fully_seen) — a blind count no longer
// orphans a held-by-nobody origin; channel_dirty_max_advertisements is now just the horizon SAFETY backstop (the
// asymmetric neighbour we hear but that never pulls). A retired entry still answers pulls; buffer eviction is the bound.
void Node::commit_channel_digest_advertised(const uint32_t* ids, uint8_t n) {
    for (uint8_t k = 0; k < n; ++k) {
        const int idx = channel_buffer_find(ids[k]);
        if (idx < 0) continue;                                     // evicted between select + commit -> nothing to commit
        ChannelEntry& e = _active->_channel_buffer[static_cast<uint16_t>(idx)];
        ++e.bcn_ad_count;
        const bool seen    = channel_entry_fully_seen(e);          // every live 1-hop neighbour holds it (or none to serve)
        const bool horizon = e.bcn_ad_count >= _cfg.channel_dirty_max_advertisements;
        const bool retired = seen || horizon;
        if (retired) {
            e.dirty = false;                                       // retire from advertising (still answers pulls)
            MR_EMIT("channel_dirty_cleared", EF_I("id", static_cast<int64_t>(e.id)), EF_I("channel_id", e.channel_id),
                    EF_I("ad_count", e.bcn_ad_count), EF_S("reason", seen ? "seen" : "horizon"));  // which path retired it
        }
        // ★ metal trace (debug on): shows whether an orphan (seen=0/N) keeps advertising or retired early. THE key line.
        if (_hal.trace_on()) {
            uint8_t live = 0, seen_cnt = 0;
            for (uint8_t i = 0; i < _active->_rt_count; ++i)
                if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) { ++live; if (seen_test(e.seen_by, _active->_rt[i].dest)) ++seen_cnt; }
            char b[80];
            if (retired) snprintf(b, sizeof b, "chan %08lX ad=%u seen=%u/%u -> RETIRE(%s)", (unsigned long)e.id, e.bcn_ad_count, seen_cnt, live, seen ? "seen" : "horizon");
            else         snprintf(b, sizeof b, "chan %08lX ad=%u seen=%u/%u -> ADVERTISED", (unsigned long)e.id, e.bcn_ad_count, seen_cnt, live);
            _hal.log(b);
        }
    }
}

// re-pull dedup ring (Lua channel_pull_recent map). recently = a pull for `id` fired within the window.
bool Node::channel_pull_recently(uint32_t id) const {
    return recent_ring_hit(_active->_channel_pull_recent, _active->_channel_pull_recent_n,
                           ChannelPullRecent{ id, 0 }, _hal.now(), protocol::channel_pull_window_ms);
}
void Node::channel_pull_mark(uint32_t id) {
    recent_ring_mark(_active->_channel_pull_recent, _active->_channel_pull_recent_n,
                     ChannelPullRecent{ id, _hal.now() });
}

// process_channel_digest (dv:3546): for each advertised id — if we HAVE it, mark the advertiser as a
// holder (eviction safety); else (capped at cap_channel_pulls_per_bcn_cycle/beacon, skipping ids pulled
// within the window) schedule a JITTERED pull. THE DRAW is rand(0, jitter+1) at the Lua's gate-order
// (dv:3568: after the recent gate, before storage). Gateways skip the entire plane (Principle 11).
void Node::process_channel_digest(uint8_t src, const uint32_t* ids, uint8_t count) {
    if (_cfg.n_layers == 2) return;                            // Principle 11: a dual-layer gateway never pulls channel gossip
    if (_cfg.is_gateway && _cfg.gateway_only) return;          // §7 CONSUMER: a gateway+owner pulls ITS OWN holes; a pure bridge stays out
    const uint64_t now = _hal.now();
    uint8_t scheduled = 0;
    for (uint8_t k = 0; k < count; ++k) {
        const uint32_t id = ids[k];
        if (channel_buffer_find(id) >= 0) {                       // already have it -> track the holder
            channel_mark_seen_by(id, src);
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX HAVE", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        if (scheduled >= protocol::cap_channel_pulls_per_bcn_cycle) {           // per-beacon pull cap
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(cap)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        if (channel_pull_recently(id)) {                          // recent-window gate (BEFORE the draw)
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(recent)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        // THE DRAW (dv:3568) — rand(0, channel_pull_jitter_ms+1). Made here regardless of slot availability
        // (the Lua always draws + stores into an unbounded map), so the stream stays aligned.
        const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(_cfg.channel_pull_jitter_ms) + 1));
        ++scheduled;
        // one pending slot per id: reuse the id's live slot (overwrite/re-arm), else a free slot.
        int slot = -1;
        for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
            if (_active->_channel_pull_pending[s].active && _active->_channel_pull_pending[s].id == id) { slot = s; break; }
        if (slot < 0)
            for (uint8_t s = 0; s < protocol::cap_channel_pull_pending; ++s)
                if (!_active->_channel_pull_pending[s].active) { slot = static_cast<int>(s); break; }
        if (slot < 0) {                                           // ring full (Lua unbounded) — drop after the draw
            MR_EMIT("channel_pull_drop_full", EF_I("id", static_cast<int64_t>(id)));
            if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> skip(ringfull)", src, (unsigned long)id); _hal.log(b); }
            continue;
        }
        _active->_channel_pull_pending[slot] = { /*active*/true, id, src, /*requested_at*/now, /*fire_at*/now + jitter };
        if (_hal.trace_on()) { char b[72]; snprintf(b, sizeof b, "chan digest<-%u %08lX MISSING -> pull@%lums", src, (unsigned long)id, (unsigned long)jitter); _hal.log(b); }
        MR_EMIT("channel_pull_scheduled", EF_I("id", static_cast<int64_t>(id)), EF_I("target", src),
                EF_I("delay_ms", static_cast<int64_t>(jitter)));
        (void)_hal.after(jitter, kChannelPullTimerId + static_cast<uint32_t>(slot));
    }
}

// channel_pull_fire (the dv:3573 after()-closure): if the msg arrived via overhear before the jitter
// fired, suppress; else broadcast a CHANNEL_PULL Q for {id} to the advertiser + record the recent pull.
void Node::channel_pull_fire(uint8_t slot) {
    if (slot >= protocol::cap_channel_pull_pending) return;
    ChannelPullPending& p = _active->_channel_pull_pending[slot];
    if (!p.active) return;
    p.active = false;
    const uint32_t id = p.id; const uint8_t target = p.target;
    if (channel_buffer_find(id) >= 0) {                           // got it via promiscuous overhear -> stand down
        MR_EMIT("channel_pull_suppressed", EF_I("id", static_cast<int64_t>(id)), EF_S("overheard_from", "promiscuous_receive"));
        return;
    }
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = target;
    in.opcode = q_opcode::channel_pull; in.mobile = _cfg.is_mobile;
    in.channel_ids = std::span<const uint32_t>(&id, 1);
    uint8_t buf[16];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    // only trigger today: a BCN digest advertised an unknown id (dv:3600)
    MR_EMIT("channel_pull_sent", EF_I("id", static_cast<int64_t>(id)), EF_I("target", target), EF_S("trigger", "bcn_digest"));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    channel_pull_mark(id);                                        // dedup re-pulls for the window
    if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan pull %08lX -> %u", (unsigned long)id, target); _hal.log(b); }
}

// =============================================================================
// Phase 2c — the CHANNEL_PULL responder + M-broadcast tx. DRAW-FREE.
// =============================================================================

// id of an M-payload TxItem/PendingTx inner (first 4 bytes, BE channel_msg_id).
uint32_t Node::m_inner_id(const uint8_t* inner) {                // Node static (node.h) — shared by the TX path so the BE decode isn't hand-rolled per call site
    return (static_cast<uint32_t>(inner[0]) << 24) | (static_cast<uint32_t>(inner[1]) << 16)
         | (static_cast<uint32_t>(inner[2]) << 8)  |  static_cast<uint32_t>(inner[3]);
}
// Is an M-payload for `id` already in flight or queued? (the s12 pull-storm dedup, dv:11850-11867)
bool Node::channel_m_in_flight(uint32_t id) const {
    if (_active->_pending_tx && _active->_pending_tx->m_broadcast
        && _active->_pending_tx->inner_len >= 4 && m_inner_id(_active->_pending_tx->inner) == id) return true;
    for (uint8_t i = 0; i < _active->_tx_queue_n; ++i)
        if (_active->_tx_queue[i].is_channel_m
            && _active->_tx_queue[i].inner_len >= 4 && m_inner_id(_active->_tx_queue[i].inner) == id) return true;
    return false;
}

// Stage an M-payload (id|channel_id|flavor|body) and enqueue it as an M-broadcast to the puller — the RTS
// carries RTS_FLAG_M_BROADCAST so overhearers catch the lean M frame on the data SF (dv:11875-11894). dst =
// the puller so the RTS-M is routed to it (the legacy M_BROADCAST RTS needs a next-hop); the M frame itself
// is address-less (the puller matches by channel_msg_id) so there's no per-target ctr — derive it from the id.
void Node::enqueue_channel_m(uint8_t target, const ChannelEntry& e) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (the puller can re-pull)
    TxItem item{};
    stamp_origin(item, m_flavor_plane(e.flavor), target); item.dst = target; item.is_channel_m = true;
    item.mobile_src = (e.flavor & protocol::channel_flavor_team) != 0;   // §mobile 6.3: a TEAM pull-response is team traffic -> mobile_src (static overhearers skip)
    item.ctr    = static_cast<uint16_t>(e.id & 0xff); item.ctr_lo = static_cast<uint8_t>(e.id & 0x0F);  // id-derived (M frame has no ctr)
    item.inner[0] = static_cast<uint8_t>(e.id >> 24); item.inner[1] = static_cast<uint8_t>(e.id >> 16);
    item.inner[2] = static_cast<uint8_t>(e.id >> 8);  item.inner[3] = static_cast<uint8_t>(e.id);
    item.inner[4] = e.channel_id; item.inner[5] = e.flavor;
    for (uint16_t k = 0; k < e.payload_len; ++k) item.inner[6 + k] = e.payload[k];
    item.inner_len = static_cast<uint8_t>(6 + e.payload_len);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    if (_hal.trace_on()) { char b[64]; snprintf(b, sizeof b, "chan serve %08lX -> %u", (unsigned long)e.id, target); _hal.log(b); }
    MR_EMIT("channel_broadcast_tx", EF_I("id", static_cast<int64_t>(e.id)), EF_I("to", target));
}

// CHANNEL_PULL responder (dv:11821): cancel my own pending pulls for the requested ids (a peer is
// pulling them — overhear dedup), then — if WE are the addressed target — re-broadcast each held id as
// an M-payload (skipping ids already in flight/queue). Gateways (§7 PROVIDER half OFF): a gateway+owner
// now HOLDS channel messages, so the explicit is_gateway gate below serves a pull ONLY for a SELF-originated
// id — a gateway never relays another node's message (that airtime is reserved for the inter-leaf role).
void Node::handle_channel_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count) {
    if (_cfg.n_layers == 2) return;                              // Principle 11: a dual-layer gateway never serves a channel pull (holds no buffer)
    for (uint8_t i = 0; i < count; ++i) cancel_channel_pull(ids[i], src, /*peer_q=*/true);   // a peer pulled these -> cancel my pending pulls (dv:11831)
    if (dest != _node_id) return;                                // only the addressed target serves the pull
    bool any = false;
    for (uint8_t i = 0; i < count; ++i) {
        const int e = channel_buffer_find(ids[i]);
        if (e < 0) continue;                                     // we don't hold it
        if (_cfg.is_gateway && _active->_channel_buffer[e].origin != _node_id) continue;  // §7 PROVIDER off: a gateway serves a pull ONLY for its OWN message, never relays another node's
        if (!channel_m_in_flight(ids[i])) { enqueue_channel_m(src, _active->_channel_buffer[e]); any = true; }
        else {
            // an existing M-tx already satisfies this id
            MR_EMIT("channel_broadcast_deduped", EF_I("id", static_cast<int64_t>(ids[i])), EF_I("requester", src));
        }
        channel_mark_seen_by(ids[i], src);                       // the requester expects to receive it
    }
    MR_EMIT("channel_pull_received", EF_I("from", src));
    if (any) {
        MR_EMIT("channel_msg_pulled", EF_I("to", src));  // we held >=1 requested id and re-broadcast it to the puller (dv:11910)
        become_free();                                           // kick the queue to start the M-broadcast
    }
}

// =============================================================================
// Channel FLOOD plane (2026-06-08 redesign): fast primary propagation. The digest+pull above is now the
// repair backstop. A node floods a channel message to its hops==1 neighbours suppressed by a coverage
// bitmap; the flood self-terminates when no node has an unmarked neighbour. Single-radio constraint: at
// most one flood-state is `awaiting_data` (one open DATA-M overhear window) — see §4.2.
// =============================================================================

int  Node::flood_state_find(uint32_t id) {
    for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
        if (_active->_flood[i].active && _active->_flood[i].id == id) return i;
    return -1;
}
int  Node::flood_state_alloc(uint32_t id) {
    for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
        if (!_active->_flood[i].active) { _active->_flood[i] = FloodState{}; _active->_flood[i].active = true; _active->_flood[i].id = id; return i; }
    return -1;   // §6/C3: ALL slots active -> drop the new flood to the repair layer; NEVER evict an active slot
}
void Node::flood_state_free(uint8_t slot) { flood_state_free(active_layer_index(), slot); }
// Layer-EXPLICIT twin (2026-07-27 §clean-join-carriers): purge_tx_carriers's reprovision axis sweeps EVERY leaf, so it
// needs a free that does not go through _active. The 1-arg form above stays the name every other call site uses (all 11
// of them are active-layer by construction) — one implementation, no fork (U1). The rebroadcast timer ring is leaf-
// SHARED (one id per SLOT, not per layer — same as kSyncResponseTimerId), so the cancel is layer-independent.
void Node::flood_state_free(uint8_t layer, uint8_t slot) {
    // Bound on MR_N_LAYERS (the ARRAY extent), NOT _n_layers (the provisioned count): the 1-arg form had no layer guard
    // at all, and _active can be pointed past _n_layers by a test seam (DualLayerTestAccess::set_active), so guarding on
    // the runtime count could make an existing call a silent no-op. The array extent is the only safety bound needed.
    if (slot >= protocol::cap_flood_pending || layer >= MR_N_LAYERS) return;
    _hal.cancel(kFloodRebcastTimerId + slot);
    _layers[layer]._flood[slot] = FloodState{};   // active = false
}

// ---- Part 2: channel ORIGIN re-offer (spec 2026-06-25-channel-origin-reoffer.md) -------------------------------
// The origin owns its message's propagation until seen_by proves it got out. channel_reoffer_register arms a slot at
// flood origination; channel_reoffer_fire re-floods the cached body while seen_by stays empty, up to N retries.
void Node::channel_reoffer_register(uint32_t id, bool team, uint16_t ctr) {
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
        ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
        if (rp.active) continue;
        // ★ EVERY field is written, none inherited: the slot is reused and a stale value is a live bug, not cosmetics.
        // §F-CH-RELAY set the precedent with `holder` (a stale true takes channel_reoffer_fire's holder branch);
        // §b38 adds `relay_seen` (a stale true would SWALLOW this origination's honest relayed=false at exhaustion)
        // and §b40 adds `ctr` (a stale value would correlate the push to the PREVIOUS post).
        rp.active = true; rp.id = id; rp.team = team; rp.holder = false; rp.relay_seen = false; rp.ctr = ctr;
        rp.retries_left = team ? protocol::channel_reoffer_team_max_retries : protocol::channel_reoffer_max_retries;
        const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(protocol::channel_reoffer_jitter_ms) + 1));
        (void)_hal.after(protocol::channel_reoffer_delay_ms + jitter, kChannelReofferTimerId + s);
        return;
    }
    MR_EMIT("channel_reoffer_table_full", EF_I("id", static_cast<int64_t>(id)));   // >cap un-confirmed originations -> repair digest covers this one (rare)
}

// §F-CH-RELAY: arm a HOLDER re-offer (a relay covering its own still-unmarked downstream). Reuses the re-offer table +
// timer ring but is holder-flagged (coverage-driven fire, not confirm-flagged) and uses DETERMINISTIC jitter — never a
// _hal.rand_range draw (mirrors park_reflood_fire; BASELINE 2026-07-19d: an added shared-mt19937 draw reorders the whole
// downstream sequence and phantom-flips timing-fragile deliveries, so the coverage repair must be RNG-draw-free). No-op
// if a slot for this id is already active (armed on the FIRST re-broadcast; a relay re-broadcasts a given flood once).
void Node::channel_holder_reoffer_register(uint32_t id) {
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s)
        if (_active->_channel_reoffer_pending[s].active && _active->_channel_reoffer_pending[s].id == id) return;   // already owning this id (origin or holder slot)
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
        ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
        if (rp.active) continue;
        // §b38/§b40: a HOLDER owns no origination, so it owns no channel_sent future — `ctr` is 0 and `relay_seen`
        // is irrelevant here (channel_reoffer_confirm returns on `holder` before it can report anything). Both are
        // still written explicitly, for the same stale-slot reason as the origin path above.
        rp.active = true; rp.id = id; rp.team = true; rp.holder = true; rp.relay_seen = false; rp.ctr = 0;
        rp.retries_left = protocol::channel_holder_reoffer_max_retries;
        const uint32_t djit = (id * 2654435761u + static_cast<uint32_t>(_node_id) * 40503u) % (protocol::channel_reoffer_jitter_ms + 1);
        (void)_hal.after(protocol::channel_reoffer_delay_ms + djit, kChannelReofferTimerId + s);
        return;
    }
    MR_EMIT("channel_reoffer_table_full", EF_I("id", static_cast<int64_t>(id)));   // >cap in flight -> the digest/pull covers this one (rare)
}

void Node::channel_reoffer_fire(uint8_t slot) {
    if (slot >= protocol::cap_channel_reoffer_pending) return;
    ChannelReofferPending& rp = _active->_channel_reoffer_pending[slot];
    if (!rp.active) return;                                                         // confirmed (a relay was overheard) or freed -> done
    const int i = channel_buffer_find(rp.id);
    if (i < 0) { rp.active = false; return; }                                      // entry evicted -> nothing to re-offer
    const ChannelEntry& e = _active->_channel_buffer[i];
    // §F-CH-RELAY holder branch: re-offer ONLY while a hops-1 team neighbour is still UNMARKED in seen_by (coverage-
    // driven — a sibling/downstream re-broadcast that marks it stops this without a dedicated confirm signal). Bounded
    // by retries_left; deterministic re-arm jitter (no RNG draw). A NON-holder (origin) slot falls through to Part 2.
    if (rp.holder) {
        if (rp.retries_left == 0 || max_data_sf() == 0 || !flood_any_unmarked(e.seen_by, /*team=*/true)) { rp.active = false; return; }
        uint8_t hbm[32] = {};
        flood_set_my_coverage(hbm, /*team=*/true);
        enqueue_flood_m(e.channel_id, e.flavor, e.id, e.payload, static_cast<uint8_t>(e.payload_len), hbm, protocol::flood_hop_max);
        --rp.retries_left;
        MR_EMIT("channel_holder_reoffer_tx", EF_I("id", static_cast<int64_t>(e.id)), EF_I("retries_left", rp.retries_left));
        const uint32_t djit = (e.id * 2654435761u + static_cast<uint32_t>(_node_id) * 40503u
                               + static_cast<uint32_t>(rp.retries_left) * 2246822519u) % (protocol::channel_reoffer_jitter_ms + 1);
        (void)_hal.after(protocol::channel_reoffer_delay_ms + djit, kChannelReofferTimerId + slot);
        return;
    }
    // Slice 6c: exhausted (or data-incapable) -> give up; repair digest is the last resort.
    // ★ §b38: `!rp.relay_seen` is the once-only latch, and it is what makes the fix HONEST rather than merely louder.
    // A team origin reports `true` at its FIRST confirm and then keeps re-offering (channel_reoffer_confirm), so it
    // reaches this line with the outcome ALREADY sent — emitting here would contradict it with a `false` on the very
    // safety signal the app backs off on. Silence is correct: the future is closed, not lost.
    if (rp.retries_left == 0 || max_data_sf() == 0) {
        if (!rp.relay_seen) emit_channel_sent(false, rp.ctr);   // §b40: the FULL 16-bit origination ctr, not `e.id & 0xff`
        rp.active = false; return;
    }
    // RE-FLOOD the cached body with the SAME frugal seed as origination (flood_set_my_coverage — NOT empty, which the
    // fail-loud zero-bitmap guard in tx_m_broadcast_rts would refuse). Receivers dedup by originator_retry_dedup_ms
    // (no double-inbox) but DO re-broadcast for coverage; LBT is applied by the TX path (enqueue_flood_m -> become_free).
    const bool team = (e.flavor & protocol::channel_flavor_team) != 0;   // §S7 T-A: re-offer coverage keyed on the entry's plane
    uint8_t bm[32] = {};
    flood_set_my_coverage(bm, team);
    enqueue_flood_m(e.channel_id, e.flavor, e.id, e.payload, static_cast<uint8_t>(e.payload_len), bm, protocol::flood_hop_max);
    --rp.retries_left;
    MR_EMIT("channel_reoffer_tx", EF_I("id", static_cast<int64_t>(e.id)), EF_I("retries_left", rp.retries_left));
    const uint32_t jitter = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int32_t>(protocol::channel_reoffer_jitter_ms) + 1));
    (void)_hal.after(protocol::channel_reoffer_delay_ms + jitter, kChannelReofferTimerId + slot);   // re-arm for the next retry
}

// Part 2 CONFIRMATION: the origin OVERHEARD A RELAY of its message (another node transmitting it — a flood RTS-M /
// DATA-M / M-frame) -> a holder formed, it propagated -> cancel the pending re-offer. A DEDICATED signal, NOT seen_by:
// immune to the {neighbours} seed and to digest/pull marks, so the re-offer stops ONLY on real relay activity (and
// keeps trying until then, up to the cap). No-ops on any node with no slot for `id` (every node except the origin).
void Node::channel_reoffer_confirm(uint32_t id) {
    for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
        ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
        if (!(rp.active && rp.id == id)) continue;
        // ★★ §b38 — A HOLDER SLOT REPORTS NOTHING, and this guard is load-bearing, not defensive. A §F-CH-RELAY
        // holder is a RELAY covering its own unmarked downstream, not an origin: it has no `channel_sent` future and
        // no ctr of its own. It also reaches this line routinely — handle_flood_rts's already-buffered branch calls us
        // for ANY id we hold, so a sibling re-broadcast lands here — and before §b38 it was absorbed by the blanket
        // `if (rp.team) return` (a holder slot is always team-flagged). Removing that blanket without this guard would
        // make every relay push a `channel_sent{relayed:true}` for a message it never sent.
        // Its coverage stop is elsewhere and unchanged: channel_mark_seen_by at the same call site, re-checked by
        // channel_reoffer_fire's holder branch.
        if (rp.holder) return;
        // ★★★ REPORT THE OUTCOME ON THE FIRST CONFIRM, ON BOTH PLANES (owner ruling 2026-08-01: *"this is correct to
        // stop after first confirm"*). `relay_seen` latches it so the push is emitted EXACTLY ONCE per origination and
        // channel_reoffer_fire's exhaustion can never contradict it. See emit_channel_sent for what `relayed` means on
        // each plane — the two readings differ, and the difference is the owner's ruling, not an accident.
        if (!rp.relay_seen) {
            rp.relay_seen = true;
            emit_channel_sent(true, rp.ctr);                    // §b40: the FULL 16-bit origination ctr, not `id & 0xff`
        }
        // ★ P-BUDGET (s28 class): a TEAM flood does NOT treat one overheard relay as full coverage — a mixed
        // multi-hop team chain has far members a single near relay never reached. Keep re-offering (all retries)
        // so those far members get independent shots; the retry-exhaustion in channel_reoffer_fire ends it.
        // ⚠ THE TWO RETRY LOOPS ARE SEPARATE AND ONLY ONE OF THEM STOPS: the CONSUMER's retries stop, because they are
        // driven by the value just emitted; the NODE's re-offers below keep running, because they are what the far
        // members depend on. Returning here — WITHOUT clearing rp.active and WITHOUT cancelling the timer — is what
        // keeps them running. A NON-team flood keeps the original relay-confirmed 1-shot semantics (delivery-suite
        // byte-inert): free the slot and cancel.
        if (rp.team) return;
        rp.active = false; _hal.cancel(kChannelReofferTimerId + s);
        return;
    }
}

// =============================================================================
// §clean-team-channel + §clean-join-carriers (2026-07-27) — THE EMIT-CARRIER PURGE. ONE mechanism, TWO axes (U1),
// TWO callers, each passing its own PurgeAxis:
//   • PurgeAxis::team_switch  — Node::set_team_id() (node.cpp), the single entry point for every LIVE team change
//                               (`team new` / `team <id>` / `team 0` / `cfg set team_id`).
//   • PurgeAxis::reprovision  — Node::clear_routing_state() (node.cpp), the `join` / `create` / `leave` verbs.
//
// THE TWO LEAKS IT CLOSES — the SAME disease on the two scope axes, because do_data_tx / the two RTS builders stamp
// the scope fields from the LIVE _cfg at TX time, not at stage time:
//   TEAM axis (node_mac.cpp:1241-1258): a team-flavored M is stamped with the CURRENT _cfg.team_id, so a team-scoped
//     payload that SURVIVES a switch is re-broadcast INTO THE TEAM WE JUST JOINED.
//   LEAF axis (node_mac.cpp:1239 `min.leaf_id = _cfg.leaf_id`, :621 RTS-M, :781 RTS-for-a-DM): a staged/in-flight
//     frame is stamped with the CURRENT _cfg.leaf_id, so after a network reprovision it is broadcast onto the NEW
//     leaf. ★ For a DM this is worse than a scope leak: clear_routing_state has just wiped _rt_count and _id_bind_n,
//     i.e. exactly the state the flight's `next` hop was chosen from, so the RTS names a next-hop the node no longer
//     has any binding for — MIS-DELIVERY, not merely over-broad broadcast.
//     ★★ AND leaf_id is not the only live field re-stamped there — `leaf_id` alone is not the audit. Both RTS builders
//     also write `rin.src = _node_id`, and on this path _node_id is ALREADY 0: provision_apply_live calls
//     reset_join_for_reprovision() — which ends in set_identity(protocol::unjoined_node_id, …) — on the line BEFORE
//     clear_routing_state() (src/firmware_config.cpp:480-481). So a survivor would air on the NEW leaf claiming
//     src = 0, the UNPROVISIONED id, while its inner DATA still carries the OLD network's `origin`: header and payload
//     disagree. The PHY is live too (_cfg.routing_sf / active_bw_hz() / active_cr() are all re-read at TX), so the frame
//     would fly at the new leaf's SF/BW/CR as well. Every one of these is fixed by the same act — dropping the carrier.
//
// ★ FIVE CARRIERS, because the payload is COPIED forward at each stage and each stage can emit on its own. This was
// the previous slice's hardest-won finding (its brief named ONE; the real answer was four) and it holds on both axes:
//   (1) _channel_buffer row     -> CHANNEL_PULL response (enqueue_channel_m) / re-offer re-flood (channel_reoffer_fire)
//   (2) _flood[] state          -> flood_rebroadcast_fire re-floods from fs.body — it does NOT read the buffer, so
//                                  dropping the row does NOT stop it (an armed backoff fires up to flood_backoff_ms later)
//   (3) _tx_queue[] TxItem      -> already staged; the stamp happens at TX time, after the switch/reprovision
//   (4) _pending_tx flight      -> same, one frame from the air
//   (5) _channel_reoffer_pending -> a slot whose row (1) is gone; generic ORPHAN test, so it needs no axis of its own
// A count reset is NOT an option for (1) or (3) on the TEAM axis: both hold still-valid NON-team leaf rows/items (a
// registered team mobile is a full leaf-plane participant), so that axis is a SELECTIVE compaction (the read/write-
// cursor idiom of peer_key_age_out / id_bind_evict_other_hash_holders). The reprovision axis keeps NOTHING, so the
// same compaction degenerates to an empty-out — one code path, two predicates, no second sweep.
//
//   carrier                    | team_switch predicate                        | reprovision predicate
//   ---------------------------|----------------------------------------------|-----------------------
//   _channel_buffer            | team_id != 0 || flavor & channel_flavor_team  | ALL rows
//   _flood[]                   | flavor & team  || fs.team_flood               | ALL active
//   _tx_queue                  | is_channel_m && inner[5] & team               | ALL staged (channel M *and* DM)
//   _pending_tx                | m_broadcast && inner[5] & team                | ANY in-flight frame
//   _channel_reoffer_pending   | orphan (row gone) — generic                   | ALL active (every row just went)
//
// ★ THE WIDER REPROVISION PREDICATE IS DELIBERATE (owner ruling 2026-07-27), and clear_routing_state's own doc comment
// is the justification: "the old network's learned state is stale". A frame BUILT from that state is equally stale, and
// a DM whose `next` came from a just-deleted _id_bind is broken regardless of what it carries. Nothing may survive.
//
// ★ LEAF SCOPE IS AXIS-INTRINSIC, not caller-chosen — that is why this function owns its own layer loop:
//   team_switch -> the ACTIVE leaf ONLY. The whole channel plane is _active-scoped (every function in this file reads
//     _active) and a node that can hold a team channel row is single-layer BY CONSTRUCTION (Principle 11: n_layers==2
//     returns early from ingest / admit / pull / digest, so a dual-layer gateway holds no channel state at all, and
//     MR_N_LAYERS>1 only exists for a gateway). Going through _active is what lets this reuse channel_buffer_find() /
//     flood_state_free() instead of re-implementing them (U1).
//   reprovision -> EVERY leaf. _tx_queue / _pending_tx are NOT channel-only: a dual-layer gateway stages bridged DMs on
//     BOTH layers, and `leave` IS dispatched on the gateway build (only join/create are MR_N_LAYERS<2-gated), so an
//     _active-only sweep would leave _layers[1]'s queue staged against a wiped _id_bind. The pre-existing
//     clear_routing_state lines this call REPLACES were per-layer too, so anything narrower would also REGRESS.
//
// ★ THE TEAM PREDICATE: drop every TEAM-scoped row, with NO comparison against the old team id — none is needed, and
// none is kept anywhere. Both writers of a team row stamp the team that is live AT THAT MOMENT: origination
// (do_send_channel :302, gated on _cfg.team_id != 0) and ingest (ingest_channel_m :192, which admits a team-scoped M
// ONLY when _cfg.team_id == m.team_id and otherwise drops it AND frees its flood state). Since set_team_id is the only
// live writer of _cfg.team_id (node.cpp:403; the boot/NV path writes it pre-on_init, when every table is empty), every
// team row present at a switch was minted or admitted under the team we are LEAVING. Two signals mark such a row and
// EITHER is sufficient: `team_id != 0` (the semantic scope) and `flavor & channel_flavor_team` (the bit do_data_tx
// keys the re-stamp on). They are set together by both writers; the OR is deliberate belt-and-braces so a row can
// never be team-flavored on the wire yet survive as "leaf" here. A genuine leaf row has neither -> it SURVIVES.
// `team 0` (leave) purges too: a left team's message must not be servable, and with team_id==0 the emit could not even
// be scoped — pack_m would put team_id 0 on a team-flavored frame, which parses as a PLAIN LEAF message, leaking the
// team's content to every static node on the leaf (do_data_tx now refuses that outright, C2).
//
// ★ DEPENDENT STATE (TEAM axis) — the full sweep of everything keyed by channel_msg_id. Verdict + WHY, so the next
// reader does not have to re-derive it (and does not "complete" what is deliberately left). ⚠ Every "NOT cleared"
// verdict below is scoped to the TEAM axis: on the REPROVISION axis clear_routing_state clears _per_origin_channel,
// _channel_pull_pending and _channel_pull_recent itself, in its own per-layer loop (they are RECEIVE/SUPPRESS ledgers,
// not emit carriers, which is exactly why they stayed out of this function):
//   • _channel_reoffer_pending  SCRUBBED (the orphan half). channel_reoffer_fire already bails on
//       channel_buffer_find < 0, so a slot for a dropped row can never re-emit — but it stays ACTIVE, holding 1 of
//       cap_channel_reoffer_pending(=4) slots until its timer fires (channel_reoffer_delay_ms 10 s + jitter). Type
//       `team new` then post, and the FIRST post in the NEW team can hit channel_reoffer_table_full and lose its
//       coverage repair. Freed by ORPHAN test (id no longer buffered), which is also exactly right for a slot orphaned
//       earlier by buffer eviction. Behaviour-equivalent to letting it fire (that path emits no channel_sent either).
//   • _channel_pull_pending     NOT scrubbed, harmless BY CONSTRUCTION: a pending pull exists only for an id we do
//       NOT hold (process_channel_digest schedules it on channel_buffer_find < 0; cancel_channel_pull clears it the
//       moment the msg lands), so its ids are DISJOINT from the rows dropped here — there is nothing to scrub. It
//       carries no plane tag, so it could not be scrubbed selectively anyway. Fails in the DROP direction: the pull
//       fires (<= channel_pull_jitter_ms), the old team serves the M, and our own ingest team gate rejects it.
//   • _channel_pull_recent      NOT scrubbed, and deliberately KEPT: it is a pure SUPPRESSION ring, so a purged id
//       still in it prevents an immediate re-pull of the message we just dropped. Clearing it would make the leak's
//       aftermath NOISIER, never safer.
//   • _per_origin_channel       NOT cleared — but ⚠ THE REASON CHANGED under §team-parity T6/B (2026-07-28) and the old
//       one no longer applies, so do not cite it. It USED to be "keyed by a BARE origin id with no plane discriminator
//       (a team local id and a static node_id collide in it), so clearing it would reach into the STATIC plane this
//       switch must not touch." T6/B plane-keyed it — `(plane<<8)|origin` — so a team-only selective clear (erase every
//       key with bit 8 set) is now BOTH possible and safe. ✖ MISSING, deliberately: adding it is a behaviour change on
//       the team-switch axis and this was the plane-keying slice (C1). It stays NOT cleared for the same standing
//       reasons that always also applied — the ledger is TTL-windowed and suppress-direction only, so a stale row can
//       only throttle, never leak, and a `team new` right after a switch is bounded by channel_origin_window_ms.
//       ⚠ ONE knock-on worth knowing: do_send_channel's self-cap counts our own rows IN THE
//       BUFFER, so dropping them RELAXES the cap right after a switch. That is intended (the old team's posts must not
//       budget the new team's), and _last_channel_origin_ms — untouched here — keeps the channel_min_interval_ms
//       burst floor as the backstop.
//   • seen_by[32] on survivors  NOT AFFECTED: per-row and independent; dropping row A invalidates nothing in row B.
//       It also means no id-space mixes — a team row's seen_by indexes TEAM ids, a leaf row's STATIC ids (§18) — and
//       whole-row drops keep it that way.
//   • BCN digest (dirty / bcn_ad_count)  NOT AFFECTED: both live ON the row and go with it.
//       ⚠ V1 §tx-admission TX3: the SELECT/COMMIT pair is no longer "stack-local to one emit_beacon call, no
//       cross-call promise" — a DEFERRED beacon carries its selected ids in its `DeferredLbt` slot and commits when
//       the timer admits the frame, so the pair now SPANS calls. What still holds is the property this bullet needs:
//       commit_channel_digest_advertised re-finds by id and skips a vanished one. An advertisement already
//       TRANSMITTER-ADMITTED leaves a promise we can no longer honour, which fails CLOSED: handle_channel_pull
//       serves only ids it finds, so the puller gets nothing and its own channel_pull_recent stops it re-asking.
//   • _per_sender_originator    NOT AFFECTED: keyed by (sender, kind, ctr_lo) — an airtime observation about a
//       NEIGHBOUR, not about our content.
//   • inbox records (record_channel, which stores team_id)  DELIBERATELY KEPT: durable app-facing history, never a
//       re-emit source (pull_inbox only). The operator's team-A messages must survive leaving team A.
//
// ★★ FLIGHT-STATE COHERENCE — dropping _pending_tx must not leave a flight HALF-ARMED. The full answer, because it is
// the one thing a carrier sweep can get subtly wrong:
//   • awaiting_cts / awaiting_ack / retries_left / retry_attempt / chosen_data_sf / alts_tried  LIVE INSIDE PendingTx,
//       so .reset() takes them with it. There is no separate mirror flag to desync — that is the design that makes this
//       safe, and it is why the sweep can drop a flight with a one-line reset.
//   • every flight TIMER is _pending_tx-GUARDED at its handler, so an already-armed one fires INERT (no cancel needed,
//       and the team axis has relied on exactly this since it landed): kRtsTimeoutTimerId -> rts_timeout_fire (bails on
//       !_pending_tx), kAckTimeoutTimerId -> ack_timeout_fire (same), kRetryBackoffTimerId -> tx_rts_retry (same),
//       kCtsToDataGapTimerId -> do_data_tx (same), kMBcastClearTimerId -> node.cpp's `_pending_tx && m_broadcast` arm,
//       kQueueWakeupTimerId -> become_free (bails on an empty queue). kFloodRebcastTimerId is cancelled outright by
//       flood_state_free; kChannelReofferTimerId by sweep (5).
//   • ★ _nack_wait_pending IS THE ONE EXCEPTION and the ONLY extra state this sweep must touch. Its timer handler is
//       flight_gen-guarded (node.cpp:963) so it cannot re-RTS a dead flight — BUT layer_swap_blocked() (node.cpp:505)
//       reads the BARE flag, so a stale `true` blocks a gateway's leaf swap until the timer fires. Cleared via the
//       existing clear_nack_wait() helper (U1), REPROVISION AXIS ONLY: on the team axis the dropped flight is always an
//       m_broadcast, which never awaits a CTS and so can never be the flight that armed a BUSY_RX wait -> the team
//       caller stays bit-identical.
//
// ★ NAMED RESIDUALS — the ALREADY-PACKED stashes. NOT purged, and the reason is a real distinction, not an omission:
//   they hold FINISHED BYTES whose byte-0 leaf nibble was frozen at pack time, so a late fire carries the OLD leaf and
//   the NEW network's leaf gate REJECTS it (node_mac_rx.cpp:47 RTS, :487 M, node_beacon.cpp:495 BCN, node_join.cpp:212
//   J, node_query.cpp:82 Q, node_hashlocate.cpp:575 H, node_route_discovery.cpp:189 F). That is wasted airtime on the
//   leaf we LEFT — never mis-delivery into the leaf we JOINED, which is the bug this slice exists to close. All are
//   ms-to-seconds bounded, and the two that matter most are inert by an exact guard:
//   • _deferred_lbt[4] (LBT re-fire)   kind==rts is flight_gen-guarded in lbt_complete -> drops. kind==nack/flood/beacon
//       just TX, so those DO fly once on the old leaf. ⚠ .pending also gates layer_swap_blocked() until it fires.
//   • _rts_duty_defer (up to ~1 h!)    flight_gen-guarded in rts_duty_defer_fire -> drops. _flight_gen is monotonic and
//       reset by NOTHING (node.h:1199), so the guard is exact — no aliasing with a post-reprovision flight.
//   • _tx_stash[4] (CTS/DATA/ACK/NACK) retry_stashed() calls _hal.tx UNCONDITIONALLY (only the ack-RE-ARM is
//       flight_gen-guarded), so a busy/duty re-issue DOES re-transmit. A DATA frame carries no leaf nibble at all, but
//       it also needs a peer holding a matching _pending_rx, so it dies unheard.
//   • _h_forward[4] / _rreq_forward[4] (jittered_tx_stash.h)  packed H / F frames; both ARE leaf-gated at RX ->
//       rejected on the new leaf. ⓘ §MH-S2: the packed J-OFFER used to be listed here as the per-leaf
//       `_pending_offer`; it is now the NODE-GLOBAL `_pending_mobile_offers` ring, so it is not LayerRuntime state
//       this audit covers at all — and a node that can host mobiles is single-layer by `can_host_mobiles()`, so it
//       can never reach a leaf swap in the first place.
//   MISSING-AND-WHY (do not "complete" without a ruling): purging them is a strictly separate axis (node-GLOBAL state,
//   not a LayerRuntime carrier) whose failure direction is airtime, not mis-delivery — C1 keeps it out of this slice.
//   • _parked_sends[8] (H-resolution-parked DMs)  NOT purged, and NOT the same class: a parked send re-enters the LIVE
//       build path when its binding arrives, so it can never carry a stale leaf stamp. After a reprovision _id_bind is
//       empty, so the binding never comes and it ages out on send_defer_ttl_ms; park_reflood_fire meanwhile floods an H
//       built with the NEW leaf_id for an OLD-network key hash, which simply resolves to nothing. PRE-EXISTING-symmetric
//       (the team axis does not clear it either) and the exact reason _deferred, which IS cleared by clear_routing_state,
//       is not enough on its own.
//   • _pending_rx / _post_ack  RECEIVE side, not emit carriers. A _post_ack forward re-enters the live build path (fresh
//       leaf stamp) and then finds no route on the wiped table -> defers -> TTL-drops. Both are cleared by
//       clear_learned_state (prep-restart), never by clear_routing_state — pre-existing, unchanged here.
//   • _pending_e2e_acks[8]  DELIBERATELY KEPT: a self-expiring deadline ring, not a frame. A -a DM killed by the
//       reprovision ALSO times out into send_failed{e2e_ack_timeout} later — "delivery was never CONFIRMED". Since the
//       drop now pushes send_failed{reprovisioned} immediately, a -a DM yields TWO pushes on the same (dst, ctr): the
//       truthful immediate one, then the deadline one up to e2e_ack_deadline_ms later. Left as-is DELIBERATELY —
//       scrubbing the ring here would be a second mechanism doing the first one's job, the ring is the ONLY thing that
//       bounds a -a future, and a duplicate completion is idempotent at the app (it matches futures by (dst, ctr) and
//       the first completion wins). ⚠ Flagged so nobody reads the second push as a bug.
//
// ★★ THE APP FUTURES — which dropped carriers owe a send_failed Push, and which owe NOTHING. (2026-07-27 owner ruling
// closing the "MISSING BY RULING-PENDING" note that used to sit here: a reprovision that strands a DM must TELL the
// app, or a companion's future hangs until its own timeout.) REPROVISION AXIS ONLY — see the axis note below.
//   (1) _channel_buffer row        NO push. A stored channel message: either ingested from a peer (never ours) or our
//                                  own POST, whose app future is channel_sent, owned by (5) — not by the row.
//   (2) _flood[] state             NO push. Pure relay/propagation state; it is a copy of a body, not a send.
//   (3) _tx_queue TxItem           PUSH iff carrier_owes_send_failed(is_channel_m, is_forward) — i.e. a DM WE
//                                  originated. A staged channel M and a transit/relay/last-mile/gateway-reinject leg
//                                  have no local future (node_carriers.h states the full rule).
//   (4) _pending_tx flight         PUSH under the same predicate, on (m_broadcast, has_previous_hop).
//   (5) _channel_reoffer_pending   NO push — and this one IS a real app future (channel_sent{relayed}), so the reason
//                                  matters: it is stranded PRE-EXISTING-IDENTICALLY by plain buffer eviction.
//                                  channel_reoffer_fire bails at `channel_buffer_find(rp.id) < 0` with a bare
//                                  `rp.active = false` and NO emit_channel_sent (the `entry evicted` line in
//                                  channel_reoffer_fire), so a row that falls out of the buffer for ANY reason already
//                                  loses its future. Completing it only here would make the purge path diverge from
//                                  the eviction path for one shared bug.
//                                  ⚠ MISSING, NOT DONE, AND WHY: fixing it belongs at that bail (all callers at once),
//                                  which is a different mechanism and a different axis -> C1 keeps it out. Reported open.
//                                  ★ §b38 NARROWED IT, and only for one case: a TEAM post whose first relay was already
//                                  overheard has ALREADY reported `channel_sent{relayed:true}` (the emit moved from
//                                  exhaustion to first confirm), so an eviction/purge after that point strands nothing.
//                                  Still open for every UNCONFIRMED origination and for the whole non-team plane.
//   ALSO PUSHED, one line away in clear_routing_state(): the `_deferred_n = 0` wipe. Same disease, same act, same
//   ruling — see the note at that line (node.cpp).
//   ⚠ HONEST BOUND: _push_ring is cap_push_ring(=32) with DROP-OLDEST (node.cpp enqueue_push). A worst-case gateway
//   reprovision can generate up to 2 leaves x (kTxQueueCap 8 + 1 flight) + 2 x cap_deferred_sends pushes, which can
//   evict EARLIER pushes from the ring. That is the ring's pre-existing policy and it is still strictly better than
//   silence: a dropped push loses one completion, whereas no push at all lost every one of them.
// =============================================================================
void Node::purge_tx_carriers(PurgeAxis axis) {
    // `all` IS the second predicate: each of the five tests below reads `all ||  <team test>`, so the reprovision axis
    // keeps nothing and the team axis is bit-for-bit the test it always was. No branch duplicates a sweep.
    const bool all = (axis == PurgeAxis::reprovision);
    // The leaf span (axis-intrinsic — see the header): team_switch = the ACTIVE leaf alone (lo..lo+1, so the loop body
    // runs exactly once on *_active, identical to the pre-generalization code); reprovision = every provisioned leaf.
    const uint8_t lo = all ? 0 : active_layer_index();
    const uint8_t hi = all ? _n_layers : static_cast<uint8_t>(lo + 1);
    uint16_t rows = 0, kept = 0;
    uint8_t  floods = 0, queued = 0, reoffers = 0;
    bool     flight = false;
    for (uint8_t li = lo; li < hi; ++li) {
        LayerRuntime& L = _layers[li];
        // (1) THE BUFFER — selective compaction (peer_key_age_out's read/write-cursor idiom, node_hashlocate.cpp:372).
        uint16_t w = 0;
        for (uint16_t r = 0; r < L._channel_buffer_n; ++r) {
            const ChannelEntry& e = L._channel_buffer[r];
            if (all || e.team_id != 0 || (e.flavor & protocol::channel_flavor_team)) { ++rows; continue; }   // in-scope -> drop
            L._channel_buffer[w++] = L._channel_buffer[r];                                                  // leaf row -> KEEP (order preserved)
        }
        L._channel_buffer_n = w;
        kept += w;
        // (2) FLOOD states — a mid-backoff flood re-broadcasts from fs.body, independent of the buffer.
        // fs.flavor carries the team bit once the DATA-M has been ingested (the authoritative scope, and the exact
        // predicate flood_rebroadcast_fire itself uses); fs.team_flood is the RTS-M-time signal for a state still
        // awaiting_data, which after the switch can never be admitted anyway (the ingest gate would drop that DATA-M) —
        // free it too rather than leave a dead slot holding 1 of cap_flood_pending(=3). flood_state_free cancels the timer.
        // ★ On the reprovision axis this REPLACES clear_routing_state's old `f = FloodState{}` line, which zeroed the slot
        // but left kFloodRebcastTimerId+s ARMED. Firing it was already inert (flood_rebroadcast_fire bails on !active) and
        // after() is replace-by-id so nothing leaked — cancelling is simply the correct, single definition of "free".
        for (uint8_t s = 0; s < protocol::cap_flood_pending; ++s) {
            const FloodState& fs = L._flood[s];
            if (!fs.active) continue;
            if (all || (fs.flavor & protocol::channel_flavor_team) || fs.team_flood) { ++floods; flood_state_free(li, s); }
        }
        // (3) THE TX QUEUE — a staged frame is stamped at TX time, so it must go BEFORE any become_free() below can pick
        // it. inner[5] is the flavor byte (enqueue_channel_m / enqueue_flood_m); on the team axis a non-channel item is
        // untouched, on the reprovision axis EVERY item goes — a DM staged against a now-wiped _rt/_id_bind is stale too.
        uint8_t qw = 0;
        for (uint8_t r = 0; r < L._tx_queue_n; ++r) {
            const TxItem& it = L._tx_queue[r];
            if (all || (it.is_channel_m && it.inner_len >= 6 && (it.inner[5] & protocol::channel_flavor_team))) {
                ++queued;
                // ★ TELL THE APP (reprovision axis only — see the header's "THE APP FUTURES" block). `all &&` is
                // structural, not an optimisation: on the team axis the dropped item is ALWAYS a team channel M, so
                // carrier_owes_send_failed would be false anyway — but keeping the axis test first is what makes the
                // team caller provably push-free without reasoning about the predicate.
                if (all && carrier_owes_send_failed(it.is_channel_m, it.is_forward))
                    push_send_failed(SendFailReason::reprovisioned, it.dst, it.ctr);
                continue;
            }
            L._tx_queue[qw++] = L._tx_queue[r];
        }
        L._tx_queue_n = qw;
        // (4) THE IN-FLIGHT FRAME — one frame from the air. Dropping an m_broadcast flight is the SAME operation the
        // flight's own completion does (kMBcastClearTimerId: reset + become_free) and the M fail-loud path in do_data_tx;
        // the armed RTS->DATA gap timer finds no _pending_tx and goes inert. An overhearer that retuned simply misses the
        // DATA-M. On the reprovision axis a DM flight goes the same way giveup_flight would drop it, minus the push.
        if (L._pending_tx && (all || (L._pending_tx->m_broadcast && L._pending_tx->inner_len >= 6
                                      && (L._pending_tx->inner[5] & protocol::channel_flavor_team)))) {
            flight = true;
            // ORDER matches giveup_flight (node_cascade.cpp:27): tell the app FIRST, while dst/ctr are still live —
            // push_send_failed takes them BY VALUE, so they are safely copied before the reset below.
            if (all && carrier_owes_send_failed(L._pending_tx->m_broadcast, L._pending_tx->has_previous_hop))
                push_send_failed(SendFailReason::reprovisioned, L._pending_tx->dst, L._pending_tx->ctr);
            L._pending_tx.reset();
        }
        // (5) RE-OFFER slots orphaned by (1) — after the compaction, so the find reflects the purge. `all` short-circuits
        // the find deliberately: channel_buffer_find reads _active, which on a NON-active leaf would consult the wrong
        // buffer (and one not yet swept), so on the reprovision axis the orphan test is answered by construction instead.
        for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
            ChannelReofferPending& rp = L._channel_reoffer_pending[s];
            if (rp.active && (all || channel_buffer_find(rp.id) < 0)) { ++reoffers; rp.active = false; _hal.cancel(kChannelReofferTimerId + s); }
        }
    }
    if (rows || floods || queued || flight || reoffers)
        MR_EMIT(all ? "reprovision_tx_purged" : "team_channel_purged",                     // one emit, axis-named: the team stream is untouched
                EF_I("rows", rows), EF_I("floods", floods), EF_I("queued", queued),
                EF_B("flight", flight), EF_I("reoffers", reoffers), EF_I("kept", kept));
    // Flight coherence (see the header): _nack_wait_pending is the ONE flight flag NOT guarded by _pending_tx at its
    // layer_swap_blocked() reader. Reprovision axis only — a team-axis drop is always an m_broadcast, which never awaits
    // a CTS and so can never have armed a BUSY_RX wait, so the team caller stays bit-identical.
    if (all) clear_nack_wait();
    if (flight) become_free();   // LAST: the queue is already purged, so this can only start a frame we were NOT dropping
                                 // (reprovision axis: provably a no-op — every leaf's queue is now empty).
}

// §S7 T-A — set my bit + my hops==1 neighbour bits, PLANE-KEYED (idempotent: originate-seed on a zeroed bm,
// OR-in on rebroadcast). team=false consults the STATIC plane (_node_id + _rt hops==1 + §S7 T-B the HOSTED
// MOBILES — a home covers its registered leaf mobiles); team=true consults the TEAM plane (_team_local_id +
// _rt_team hops==1). The bitmap indexes ONE id-space per flood instance (§18: a team local-id can numerically
// collide a static id — never mix them). s18-inert: no team (team=false path only) + no hosted mobiles.
void Node::flood_set_my_coverage(uint8_t* bm, bool team) const {
#if MR_FEAT_TEAM
    if (team) {
        seen_set(bm, team_local_id());
        for (uint8_t i = 0; i < _active->_rt_team_count; ++i)
            if (_active->_rt_team[i].n > 0 && _active->_rt_team[i].candidates[0].hops == 1) seen_set(bm, _active->_rt_team[i].dest);
        return;
    }
#else
    (void)team;
#endif
    seen_set(bm, _node_id);
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1) seen_set(bm, _active->_rt[i].dest);
#if MR_FEAT_MOBILE
    // §S7 T-B: a home covers its hosted mobiles so a leaf flood re-broadcasts to reach them.
    // ★★ §MH-S5-FIX [[B172]]/[[B173]] — ONLY the LIVE DIRECT rows (`host_row_live_direct`, node.h). A REDIRECT row's
    // mobile is at another home and cannot hear this leaf's flood; an EXPIRED row's mobile has been silent for 25
    // minutes. Claiming coverage for either is a claim about a node this home cannot reach (§9.1 wants an expired row
    // "absent from … coverage accounting"). ⓘ The predicate is non-mutating, so this reader stays `const`.
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (host_row_live_direct(i)) seen_set(bm, _active->_mobile_reg[i].mobile_local_id);
#endif
}
bool Node::flood_any_unmarked(const uint8_t* bm, bool team) const {
#if MR_FEAT_TEAM
    if (team) {
        for (uint8_t i = 0; i < _active->_rt_team_count; ++i)
            if (_active->_rt_team[i].n > 0 && _active->_rt_team[i].candidates[0].hops == 1 && !seen_test(bm, _active->_rt_team[i].dest)) return true;
        return false;
    }
#else
    (void)team;
#endif
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].hops == 1 && !seen_test(bm, _active->_rt[i].dest)) return true;
#if MR_FEAT_MOBILE
    // §S7 T-B: an un-covered hosted mobile -> the home re-floods to reach it. ★★ §MH-S5-FIX [[B172]]/[[B173]]: only a
    // LIVE DIRECT row may DEMAND that re-flood — a redirect/expired row drove airtime at a mobile that is not here.
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (host_row_live_direct(i) && !seen_test(bm, _active->_mobile_reg[i].mobile_local_id)) return true;
#endif
    return false;
}

// Build + enqueue a FLOOD m-broadcast: a fire-and-forget DATA-M whose RTS-M carries the 43-B FLOOD tail
// (id + 32-B bitmap). A true broadcast (no target); issue_send bypasses route selection (next=0xFF).
void Node::enqueue_flood_m(uint8_t channel_id, uint8_t flavor, uint32_t id, const uint8_t* body, uint8_t body_len,
                           const uint8_t* bitmap32, uint8_t hop_left) {
    if (_active->_tx_queue_n >= kTxQueueCap) return;                       // queue full -> drop (repair covers it)
    TxItem item{};
    stamp_origin(item, m_flavor_plane(flavor), 0xFF); item.dst = 0xFF;   // broadcast; the RTS dst slot carries hop_left
    item.ctr = static_cast<uint16_t>(id & 0xff); item.ctr_lo = static_cast<uint8_t>(id & 0x0F);
    item.is_channel_m = true;
    item.mobile_src = (flavor & protocol::channel_flavor_team) != 0;   // §mobile 6.3: mark a TEAM channel flood -> a static overhearer skips it (no re-flood, keeps team traffic off the static plane). Non-team flood -> 0, byte-identical.
    item.flood = true; item.hop_left = hop_left;
    for (uint8_t i = 0; i < 32; ++i) item.flood_bitmap[i] = bitmap32[i];
    item.inner[0] = static_cast<uint8_t>(id >> 24); item.inner[1] = static_cast<uint8_t>(id >> 16);
    item.inner[2] = static_cast<uint8_t>(id >> 8);  item.inner[3] = static_cast<uint8_t>(id);
    item.inner[4] = channel_id; item.inner[5] = flavor;
    for (uint8_t k = 0; k < body_len; ++k) item.inner[6 + k] = body[k];
    item.inner_len = static_cast<uint8_t>(6 + body_len);
    item.enqueue_time_ms = _hal.now();
    _active->_tx_queue[_active->_tx_queue_n++] = item;
    MR_EMIT("flood_tx", EF_I("id", static_cast<int64_t>(id)), EF_I("hop_left", hop_left));
    become_free();                                                // kick the queue -> issue_m_broadcast (FLOOD RTS-M)
}

// §4.2 — RX of a FLOOD RTS-M (control SF). Returns true iff a FRESH flood-state was created (the caller then
// retunes to catch the DATA-M). The channel_id/flavor/body arrive later with the DATA-M (ingest).
bool Node::handle_flood_rts(const rts_out& r, const uint8_t* in_bm, int16_t snr_q4) {
    const uint32_t id = r.flood_channel_msg_id;
    const int existing = flood_state_find(id);
    if (existing >= 0) {                                          // active state -> overheard duplicate: OR coverage
        FloodState& fs = _active->_flood[existing];
        for (uint8_t i = 0; i < 32; ++i) fs.bitmap[i] |= in_bm[i];
        // §4.5 while-pending: a backoff-phase state now fully covered -> cancel the rebroadcast + free. §S7 T-A: the
        // coverage test is plane-keyed (fs.team_flood = the RTS-M's mobile_src = a TEAM flood -> consult _rt_team).
        if (!fs.awaiting_data && !flood_any_unmarked(fs.bitmap, fs.team_flood)) flood_state_free(static_cast<uint8_t>(existing));
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[40]; snprintf(b, sizeof b, "flood %08lX dup-merge", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        return false;                                            // no new flood, no retune
    }
    if (channel_buffer_find(id) >= 0) {                          // already in the buffer, no state -> already forwarded, drop
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[48]; snprintf(b, sizeof b, "flood %08lX already-buffered", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        // §F-CH-RELAY: a TEAM flood RTS-M we overhear means `src` is (re)transmitting this message -> src HOLDS it. Mark
        // it in seen_by so a HOLDER re-offer's coverage check (flood_any_unmarked on seen_by) sees the sibling covered and
        // stops early (bounds the re-offer + implements "an overheard relay marks progress"). team-gated (r.mobile_src) ->
        // a static/leaf flood's already-buffered path is untouched -> the delivery suite + s18 stay byte-identical.
        if (r.mobile_src) channel_mark_seen_by(id, r.src);
        channel_reoffer_confirm(id);                            // Part 2: a relay of OUR message (its FLOOD RTS-M) was overheard -> stop re-offering
        return false;
    }
    const int slot = flood_state_alloc(id);
    if (slot < 0) {                                              // C3 -> repair
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[40]; snprintf(b, sizeof b, "flood %08lX state-full", (unsigned long)id); _hal.log(b); }   // D (DEBUG)
        MR_EMIT("flood_state_full", EF_I("id", static_cast<int64_t>(id))); return false;
    }
    FloodState& fs = _active->_flood[slot];
    // L7: the RTS `dst` slot carries hop_left off the wire (unauthenticated). A forged dst=255 would give a 255-hop
    // re-flood TTL — clamp to flood_hop_max so a crafted RTS-M can't inflate the flood horizon past the mesh diameter.
    const uint8_t wire_hop_left = r.dst > protocol::flood_hop_max ? protocol::flood_hop_max : r.dst;
    fs.awaiting_data = true; fs.src = r.src; fs.rx_snr_q4 = snr_q4; fs.hop_left = wire_hop_left;  // §3.1: dst slot = hop_left
    fs.team_flood = r.mobile_src;                                // §mobile 6.3: a mobile_src RTS-M is a TEAM flood -> tag it so fast-self-pull DEFERS (team unknown until the DATA-M's team_id)
    for (uint8_t i = 0; i < 32; ++i) fs.bitmap[i] = in_bm[i];
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[56]; snprintf(b, sizeof b, "flood %08lX caught RTS-M from %u, awaiting DATA-M", (unsigned long)id, (unsigned)r.src); _hal.log(b); }   // F (DEBUG): this node will TRY to catch the flood body
    return true;                                                 // fresh -> catch the DATA-M (retune in the caller)
}

// §4.5 — after the DATA-M ingest: do I have an unmarked neighbour? No -> silent. Yes -> arm a SNR-x² backoff.
void Node::flood_forward_decision(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    if (_cfg.is_gateway || _cfg.n_layers == 2) { flood_state_free(slot); return; }     // §7 provider half OFF / Principle 11: a (single- or dual-layer) gateway never rebroadcasts
    FloodState& fs = _active->_flood[slot];
    const bool team = (fs.flavor & protocol::channel_flavor_team) != 0;      // §S7 T-A: the DATA-M's flavor (set at ingest) is the authoritative team-scope
#if MR_FEAT_MOBILE
    // §S7 T-B: a registered mobile INGESTS a leaf/static flood (record+push, done at ingest) but NEVER re-floods it
    // (receiver-only; the never-relay-on-static rule + battery). A team flood (team=true) still re-floods on the team plane.
    if (_cfg.is_mobile && !team) { flood_state_free(slot); return; }
#endif
    if (!flood_any_unmarked(fs.bitmap, team)) {                              // every neighbour covered -> stay silent
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) flood_log_coverage("SILENT", fs.id, fs.bitmap); // A (DEBUG): THE one — why this node went quiet + each hops==1 neighbour's covered state
        flood_state_free(slot); return;
    }
    // SNR-x² gives the backoff WINDOW = T_backoff * snr_norm^2 ; snr_norm = clamp((rx_snr-lo)/(hi-lo),0,1). Then
    // pick a RANDOM slot in [0, window] (rand_range is [lo,hi)). A deterministic delay makes every same-SNR node
    // fire at the SAME instant -> they collide and nobody hears anybody to cancel; worse, a uniformly high-SNR
    // mesh saturates every node to the max window so ALL fire together. The random slot de-collides them: the
    // earliest draw rebroadcasts, the rest hear its RTS-M, OR the coverage, and cancel. far-first holds in
    // expectation (E[delay] = window/2, monotonic in SNR). Standard LBT still gates the actual TX on top.
    const int32_t lo = protocol::flood_snr_lo_q4, hi = protocol::flood_snr_hi_q4;
    int32_t num = static_cast<int32_t>(fs.rx_snr_q4) - lo;
    if (num < 0) num = 0;
    if (num > (hi - lo)) num = (hi - lo);   // clamp num into [0, hi-lo]
    const int64_t span = static_cast<int64_t>(hi) - lo;          // statically > 0 (compile-time constants)
    const uint32_t window  = static_cast<uint32_t>(static_cast<int64_t>(protocol::flood_backoff_ms) * num * num / (span * span));
    const uint32_t backoff = static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(window) + 1));   // random slot in [0, window]
    (void)_hal.after(backoff, kFloodRebcastTimerId + slot);
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[72]; snprintf(b, sizeof b, "flood %08lX relay in %lums slot=%u", (unsigned long)fs.id, (unsigned long)backoff, (unsigned)slot); _hal.log(b); }   // C (DEBUG)
    MR_EMIT("flood_rebroadcast_scheduled", EF_I("id", static_cast<int64_t>(fs.id)), EF_I("backoff_ms", backoff), EF_I("slot", slot));
}

// kFloodRebcastTimerId+slot — re-flood {my unmarked neighbours + me}, hop_left-1 (drop on TTL exhaustion).
void Node::flood_rebroadcast_fire(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    const FloodState fs = _active->_flood[slot];                          // copy: we free the slot before re-enqueue
    const bool team = (fs.flavor & protocol::channel_flavor_team) != 0;   // §S7 T-A: re-flood coverage keyed on the flood's plane
    uint8_t bm[32]; for (uint8_t i = 0; i < 32; ++i) bm[i] = fs.bitmap[i];
    flood_set_my_coverage(bm, team);                             // §4.5 on-fire: {my unmarked neighbours + me} on the flood's plane
    flood_state_free(slot);
    if (fs.hop_left <= 1) {                                      // TTL drop
        // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[44]; snprintf(b, sizeof b, "flood %08lX hop-exhausted", (unsigned long)fs.id); _hal.log(b); }   // E (DEBUG)
        MR_EMIT("flood_hop_exhausted", EF_I("id", static_cast<int64_t>(fs.id))); return;
    }
    if (max_data_sf() == 0) return;                             // non-operational (no data SF) -> no fallback
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[44]; snprintf(b, sizeof b, "flood %08lX RELAY hop=%u", (unsigned long)fs.id, (unsigned)(fs.hop_left - 1)); _hal.log(b); }   // E (DEBUG)
    enqueue_flood_m(fs.channel_id, fs.flavor, fs.id, fs.body, fs.body_len, bm, static_cast<uint8_t>(fs.hop_left - 1));
    // §F-CH-RELAY (2026-07-21, s28 XO5/XH2 carve): a multi-hop TEAM flood's FAR members can be permanently missed. The
    // ORIGIN's re-offer (channel_reoffer_*) re-floods only ITS OWN coverage -> it reaches only the origin's DIRECT
    // neighbours; an intermediate HOLDER that already has the message DEDUPS the re-offer (handle_flood_rts's
    // already-buffered branch) and stays SILENT — relays re-broadcast on FIRST receipt only. So a fragile flood that dies
    // at hop N has NO repair path for the members beyond N (the pull backstop is off for team floods — see node.cpp's
    // kOverhearRetuneTimerId — and a relay's fresh dirty entry isn't beacon-triggered, so the digest is starved too).
    // FIX: extend the origin's coverage-repair discipline to HOLDERS. After THIS relay re-broadcast, if it still has an
    // UNMARKED hops-1 team neighbour in the buffer entry's seen_by (a downstream member not yet confirmed to hold it),
    // arm a coverage-driven holder re-offer so that member gets independent re-injections. Anti-storm: coverage-GATED
    // (only when downstream is unconfirmed, not timer-spam), bounded retries, deterministic per-(id,node,try) jitter (NO
    // shared-RNG draw — BASELINE 2026-07-19d), and an overheard sibling re-broadcast marks seen_by -> next fire stops.
    // TEAM-ONLY (the carve lives on the team plane): a static/leaf flood never enters this branch -> s18 byte-identical.
    if (team) {
        const int bi = channel_buffer_find(fs.id);
        if (bi >= 0 && flood_any_unmarked(_active->_channel_buffer[static_cast<uint16_t>(bi)].seen_by, /*team=*/true))
            channel_holder_reoffer_register(fs.id);
    }
}

// §4.4 — caught the FLOOD RTS-M but missed the DATA-M (overhear window closed, still awaiting_data): pull
// the body immediately from `src` (a confirmed adjacent holder), instead of waiting for a digest.
void Node::flood_fast_self_pull(uint8_t slot) {
    if (slot >= protocol::cap_flood_pending || !_active->_flood[slot].active) return;
    const uint32_t id = _active->_flood[slot].id; const uint8_t src = _active->_flood[slot].src;
    flood_state_free(slot);
    if (channel_buffer_find(id) >= 0) return;                    // arrived meanwhile -> no pull
    if (channel_pull_recently(id)) return;                       // re-pull dedup window
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = src;
    in.opcode = q_opcode::channel_pull; in.mobile = _cfg.is_mobile;
    in.channel_ids = std::span<const uint32_t>(&id, 1);
    uint8_t buf[16];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    // FLOOD-DBG disabled 2026-06-23 (re-enable for bench diag): if (_hal.trace_on()) { char b[60]; snprintf(b, sizeof b, "flood %08lX DATA-M MISSED -> self-pull from %u", (unsigned long)id, (unsigned)src); _hal.log(b); }   // G (DEBUG): live flood body LOST on this link -> pull (the weak-link path)
    MR_EMIT("channel_pull_sent", EF_I("id", static_cast<int64_t>(id)), EF_I("target", src), EF_S("trigger", "flood_fast"));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    channel_pull_mark(id);
}

}  // namespace meshroute
