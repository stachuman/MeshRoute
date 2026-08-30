// MeshRoute — lib/core/node.cpp  (Node spine: construction, lifecycle, dispatch)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The Node's glue: construction, on_init lifecycle, the on_timer / on_recv
// dispatchers that route to the subsystem handlers, the typed command seam, and
// the async push ring. The subsystem implementations live in sibling TUs of the
// same Node class (declared in node.h):
//   node_beacon.cpp   — §10 BCN emit/ingest + discovery
//   node_routing.cpp  — DV route table + R2 aging/prune
//   node_mac.cpp       — R3 RTS-CTS-DATA-ACK-NACK data plane
//   node_cascade.cpp   — cascade-to-alt walk + no-route defer + timeout fires
// Behaviour mirrors dv_dual_sf.lua; the wire is C5 cmd-nibble. See
// docs/specs/2026-05-29-r1-beacon-emit-design.md + 2026-05-30-r2-route-hardening-design.md.
#include "node.h"

#include "airtime.h"   // airtime_ms — Slice 3a SF-weighted window derivation
#include "frame_codec.h"  // DATA_HDR_LEN — §3e exchange_airtime_ms DATA-leg sizing
#include "identity.h"  // §P2-6: key_hash32_of (LE(ed_pub[:4]) derivation); §team-ch-key: team_channel_key_derive
#include "monocypher.h"   // §team-ch-key: crypto_wipe — scrub the scalar/priv scratch on the mint/adopt paths
#include "node_role.h"    // ★ B28/R2: role_enforce + kRoleHasMobilePlane — the role model's ONE definition (set_team_id below)
#include "meshroute_wire.h"

#include <cstdlib>     // atol — parse_gateway_cmd
#include <cstring>     // strncmp — parse_gateway_cmd

namespace MESHROUTE_NS {

// ---- construction & lifecycle ----------------------------------------------

Node::Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name)
    : _hal(hal), _node_id(node_id), _key_hash32(key_hash32) {
    if (name) { uint8_t l = 0; while (name[l] && l < sizeof _name) ++l; set_name(name, l); }   // §1.3: keep the ctor/sim name (was discarded) so effective_name + the pubkey exchange can carry it
    // 0xFF is RESERVED — never a valid node id. It is the "unknown PHY source"
    // sentinel: RxMeta.src_hint=-1 casts to 0xFF, and real LoRa carries no link
    // source. The console `cfg id` already caps at 254; this guards the ctor too.
    if (node_id == 0xFF) _hal.panic("node_id 0xFF is reserved (invalid)");
}

// §1.3: the node's human name for display + the pubkey exchange. An empty name defaults to "MeshRoute node: 0x<hash>" —
// the key_hash32 is STABLE (the node_id can change via join/lease), so the default is a persistent identity.
uint8_t Node::effective_name(char* out, uint8_t cap) const {
    if (cap == 0) return 0;
    if (_name_len > 0) {                                          // an explicit name
        const uint8_t n = _name_len < cap ? _name_len : cap;
        for (uint8_t i = 0; i < n; ++i) out[i] = _name[i];
        return n;
    }
    static const char pfx[] = "MeshRoute node: 0x";
    const char hex[] = "0123456789ABCDEF";
    uint8_t k = 0;
    for (uint8_t i = 0; pfx[i] && k < cap; ++i) out[k++] = pfx[i];
    for (int sh = 28; sh >= 0 && k < cap; sh -= 4) out[k++] = hex[(_key_hash32 >> sh) & 0xF];
    return k;
}

// Reassign identity post-construct: the device boots id=0 then loads it from NV; the join runtime sets
// it too. 0 stays unprovisioned (do_send refused). 0xFF is reserved -> ignored.
void Node::set_identity(uint8_t node_id, uint32_t key_hash32) {
    if (node_id == 0xFF) return;
    _node_id    = node_id;
    _key_hash32 = key_hash32;
    _hal.set_protocol_id(node_id);   // keep the Hal short-id in sync (addressing / join) — ALWAYS, incl. 0 (going unprovisioned must un-address us)
    // ★ id 0 = UNPROVISIONED -> NO self-binding (owner ruling 2026-07-27). This is the SAME id_bind_set(self,
    // authoritative) call as on_init (:341) and activate_layer (:616) and BOTH of those already guard it on
    // `!= 0` ("node_id 0 is unprovisioned (no identity yet)"). This site was the only unguarded one of the three,
    // so every set_identity(protocol::unjoined_node_id, …) wrote an AUTHORITATIVE {node_id:0, our key_hash32} row.
    // Where that actually persisted (no following wipe): forced_rejoin() -> reset_join_for_reprovision()
    // (node_join.cpp:410; node.h:272 says the heal DELIBERATELY keeps its routes), mobile_reset_registration()
    // (node_mobile.cpp:250), and the DEVICE BOOT of an unprovisioned node (src/fw_main.cpp:659 runs BEFORE on_init,
    // so :341's guard was defeated on the very path it was written for). The reprovision verb path was harmless only
    // by ORDER — clear_routing_state() zeroes _id_bind_n on the next line.
    // Consumers were swept: nothing depends on the row. Every reader either scans ids >= normal_node_id_min
    // (join_choose_candidate_id / find_free_mobile_id), rejects the hit with `!= _node_id` (which IS 0 here:
    // node_join.cpp:454, node_hashlocate.cpp:1346) or `> 0` (node_mac_rx.cpp:1184), or short-circuits our own hash
    // on _node_id BEFORE consulting the table (handle_h_query :607, request_resolve :1398 — both of which therefore
    // still answer 0 while unprovisioned; that is the SEPARATE, still-open responder-id-0 family, NOT fixed here).
    if (node_id != 0) id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);   // re-seed our own binding (authoritative) under the new identity
}

void Node::set_crypto_identity(const uint8_t x_secret[32], const uint8_t ed_pub[32]) {
    for (int i = 0; i < 32; ++i) { _x_secret[i] = x_secret[i]; _ed_pub[i] = ed_pub[i]; }
    _crypto_ready = true;        // DP1: seal/open are now permitted (until set, they FAIL LOUD — never cleartext)
}

#if MR_FEAT_TEAM
// §team-ch-key (T-K1, spec 2026-07-26 §2.1) — the TEAM CHANNEL content keypair. Three entry points, ONE
// derivation path (team_channel_key_derive, identity.cpp), so a minted key and an adopted one are
// byte-identically canonical. NOTE the asymmetry with the node identity: that one is DERIVED from a persisted
// master seed, so it can always be recomputed; this one is a standalone secret whose ONLY copies are NV and
// whatever the operator distributed (T-K3 grant / T-K4 QR). Losing it = re-key the team (spec §0/Q5).
//
// MINT (`team new`, unconditional per the owner ruling 2026-07-29). Entropy comes from the HAL CSPRNG — the
// device HW-RNG, or the simulator's per-node deterministic rand_bytes stream. C2: a dead RNG (all-zero draw)
// REFUSES; we do not fall back to a weaker source and we do not leave a keyless-but-flagged team behind.
bool Node::team_channel_key_mint() {
    uint8_t scalar[32];
    _hal.rand_bytes(scalar, sizeof scalar);
    uint8_t pub[32], priv[32];
    const bool ok = team_channel_key_derive(pub, priv, scalar);
    crypto_wipe(scalar, sizeof scalar);
    if (!ok) return false;                                  // nothing written -> the caller reports and mints no team key
    team_channel_key_load(pub, priv, /*present=*/true);
    crypto_wipe(priv, sizeof priv);
    return true;
}

// ADOPT an externally-supplied pair (`team new tkpub=…/tkpriv=…`; the same path T-K3's grant and T-K4's QR
// will take). Beyond the derive-level refusals this ALSO cross-checks the supplied public half against the
// one the private half actually derives, and refuses on mismatch: a keypair whose two halves disagree can
// only come from corruption (a mis-scanned QR, a truncated grant) or from a foreign scalar convention, and
// silently keeping either half would produce posts nobody can read. Fail loud instead (C2).
bool Node::team_channel_key_adopt(const uint8_t pub[32], const uint8_t priv[32]) {
    uint8_t derived_pub[32], canon_priv[32];
    if (!team_channel_key_derive(derived_pub, canon_priv, priv)) return false;   // all-zero / degenerate -> refuse
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= static_cast<uint8_t>(derived_pub[i] ^ pub[i]);
    if (diff != 0) { crypto_wipe(canon_priv, sizeof canon_priv); return false; }  // pub does not belong to priv -> refuse
    team_channel_key_load(derived_pub, canon_priv, /*present=*/true);
    crypto_wipe(canon_priv, sizeof canon_priv);
    return true;
}

// ADOPT from the PRIVATE HALF ALONE — §T-K3's sealed grant, whose body deliberately carries only tkpriv (see the
// DATA_TYPE_TEAM_KEY_GRANT note in frame_codec.h for why). The FOURTH entry point over the SAME single derivation
// path, not a fork (U2): team_channel_key_derive canonicalises the scalar and derives the public half, exactly as
// mint and adopt do. There is no pub to cross-check here and that is the POINT — a derived public half cannot
// disagree with the private one, so the mismatch adopt() must detect cannot arise on this path at all.
bool Node::team_channel_key_adopt_priv(const uint8_t priv[32]) {
    uint8_t derived_pub[32], canon_priv[32];
    if (!team_channel_key_derive(derived_pub, canon_priv, priv)) return false;   // all-zero / degenerate -> refuse
    team_channel_key_load(derived_pub, canon_priv, /*present=*/true);
    crypto_wipe(canon_priv, sizeof canon_priv);
    return true;
}

// Boot restore from the NV Blob (v22) — VERBATIM, no re-derivation, mirroring admin_load. `present=false`
// (a fresh chip, or a version-rejected blob, which fw_main passes as a ZERO-INITIALISED Blob) leaves the
// node keyless with the buffers zeroed: a stale/absent blob can never fabricate a key.
void Node::team_channel_key_load(const uint8_t pub[32], const uint8_t priv[32], bool present) {
    for (int i = 0; i < 32; ++i) { _team_ch_pub[i] = pub[i]; _team_ch_priv[i] = priv[i]; }
    _team_ch_key_present = present;
}

// §o3-key-lifetime (owner ruling 2026-07-31) — the DESTROY half, called by set_team_id alone. A team CONTENT key is
// scoped to the team it was granted for: carrying it across a switch would let a member seal a post for its NEW team
// under the OLD team's key the moment CL2a makes posts seal (inert until then — that is why this landed as its own
// slice). `team 0` clears too: with no team the key applies to nothing.
// ★ crypto_wipe, NOT team_channel_key_load(zeros): this is the destruction of a SECRET, and a plain copy loop is
// exactly what a compiler is entitled to elide on a buffer nothing reads afterwards. The four ESTABLISH paths still
// share the one write path above (U2) — only the un-doer is separate, and deliberately so.
// ⚠ The pair is UNRECOVERABLE (no seed derives it, node_role.h:89), so the recovery after a switch is a re-grant
// (T-K3 `team grantkey`) or a T-K4 QR — never a local regeneration.
void Node::team_channel_key_clear() {
    crypto_wipe(_team_ch_pub,  sizeof _team_ch_pub);
    crypto_wipe(_team_ch_priv, sizeof _team_ch_priv);
    _team_ch_key_present = false;   // the accessors now return nullptr -> blob_take_team_channel_key persists present=0 + all-zero (the NV mirror, free by construction)
}
#endif   // MR_FEAT_TEAM (§team-ch-key)

// ===================== §team-ch-key T-K3 — the SEALED team key-grant DM (DATA_TYPE_TEAM_KEY_GRANT = 0xA2) =====================
// Spec 2026-07-26 §2.3 + the owner's five requirements (2026-07-29): sealed-only · carries team_id · its own console
// verb that FAILS if there is no pubkey or no receiver · format free to choose · an optional name parameter.
//
// Body layout (ONE definition, shared by both halves below — the wire note lives at DATA_TYPE_TEAM_KEY_GRANT):
//     [team_id u32 LE][name_len u8][team_name name_len][tkpriv 32]        37 .. 69 bytes
// tkpub is NOT carried. See frame_codec.h for the full argument; the short version is that re-deriving it makes a
// half-mismatch structurally impossible rather than something the receiver must detect, and it costs 32 fewer bytes.
namespace {
constexpr uint8_t kGrantTeamIdLen = 4;
constexpr uint8_t kGrantPrivLen   = 32;
constexpr uint8_t kGrantNameMax   = 32;                                                   // the codebase-wide name cap (PeerKey::name, INTRO, H)
constexpr uint8_t kGrantMinLen    = kGrantTeamIdLen + 1 + kGrantPrivLen;                   // 37 = no name
}  // namespace

// ORIGINATION. Pre-flights the refusals an operator/app can act on, then hands the sealed send to the ORDINARY
// send-by-hash machinery (U1 — no parallel send path). CryptIntent::on is forced, never `def`: a node with e2e_dm
// OFF must still not be able to air this in the clear, and enqueue_data's TEAM_KEY_GRANT guard makes that structural.
Node::TeamKeyGrantTx Node::team_key_grant_send(uint32_t target_hash, const char* name, uint8_t name_len, Plane plane, uint16_t* out_ctr, uint8_t* out_dst) {
    if (out_ctr) *out_ctr = 0;
    if (out_dst) *out_dst = 0;
    if (_cfg.team_id == 0)              return TeamKeyGrantTx::no_team;
    const uint8_t* priv = team_channel_priv();                                            // nullptr while keyless (node.h) — never a zero buffer
    if (!priv)                          return TeamKeyGrantTx::no_key;
    if (!_crypto_ready)                 return TeamKeyGrantTx::no_identity;               // e2e_seal_inner would refuse too; this names it for the operator
    if (target_hash == 0 || target_hash == _key_hash32) return TeamKeyGrantTx::self;
    // ★ THE BAR IS e2e_seal_inner's OWN BAR, deliberately not lower (node_hashlocate.cpp:387 — authoritative OR
    // pinned). Shipping a PRIVATE key to an `overheard` key would be shipping it to whoever last spoofed a beacon.
    // ⚠ AND WE DO **NOT** AUTO-ISSUE A WANT_PUBKEY LOCATE HERE — §no-auto-reqpubkey, OWNER-RATIFIED 2026-07-29; the
    // full rule and its reasoning live ONCE, at Node::send_by_hash's header (node_hashlocate.cpp, tagged
    // `§no-auto-reqpubkey`), and all four locate sites cite it. Restated here only because THIS verb is the worst
    // possible place to break it: auto-escalating would silently prefer the on-air TOFU path over the MITM-resistant
    // QR ceremony for the one operation that ships a PRIVATE key. Fail loud and let the operator type
    // `reqpubkey <hash>` (or scan the QR — one call keys both sides, it is mutual); the console spells that remedy out
    // verbatim. A future slice that "fixes" this by auto-resolving is reversing a ruling.
    { uint8_t ed[32]; PeerKeyConf conf = PeerKeyConf::overheard;
      if (!peer_key_find(target_hash, ed, &conf) || static_cast<uint8_t>(conf) < static_cast<uint8_t>(PeerKeyConf::authoritative))
          return TeamKeyGrantTx::no_pubkey; }
    uint8_t nlen = name_len;
    if (name == nullptr) nlen = 0;
    if (nlen > kGrantNameMax)           return TeamKeyGrantTx::too_large;                  // C2: refuse, never silently truncate the label
#if MR_FEAT_MOBILE
    // PREDICT send_by_hash's delegate branch (node_hashlocate.cpp:1032) rather than discover it after the fact: a
    // REGISTERED mobile with neither an authoritative id_bind nor a team-cache hit would wrap the sealed body under a
    // MOBILE_SEND whose enclosed-type slot is ALREADY DATA_TYPE_SEALED_RELAY — the grant would be dropped and the peer
    // would receive 37 raw key bytes as inbox text. The structural refusal in send_by_hash catches it regardless
    // (belt-and-suspenders, the node_mac_rx.cpp:1196 idiom); this pre-check exists so the OPERATOR gets told which of
    // the two things to do (grant from the home's layer, or `-t` over the team plane) instead of a bare send_failed.
    if (_cfg.is_mobile && mobile_registered() && plane != Plane::TEAM) {
        IdBindConf conf = IdBindConf::claimed;
        const int    id      = id_bind_find_by_hash(target_hash, &conf);
        uint8_t      tid     = 0;
        const bool   team_ok = team_id_of_key(target_hash, tid);                            // stubs false on MR_FEAT_TEAM 0
        bool hosted = false;                                                               // a mobile WE host is a direct last-mile, which DOES keep the type
        // ★★★ §MH-S5-FIX2 (owner-ruled 2026-08-10, ledger §1.14): `host_row_live_direct(i)`, not a bare
        // `redirect_home_id == 0`. This pre-check exists to PREDICT `send_by_hash`'s direct-last-mile arm, and that arm
        // now refuses an EXPIRED row too — so leaving the bare test here would have made the prediction wrong in
        // exactly the case the operator is warned about, sending a private key half into a delegated wrapper whose
        // enclosed-type slot is already taken. ⇒ the two must ask the same question, and this is the same question.
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == target_hash && host_row_live_direct(i)) hosted = true;
        if (!(id >= 0 && conf == IdBindConf::authoritative) && !team_ok && !hosted)
            return TeamKeyGrantTx::delegated;
    }
#endif
    uint8_t body[kGrantTeamIdLen + 1 + kGrantNameMax + kGrantPrivLen];
    uint8_t n = 0;
    body[n++] = static_cast<uint8_t>(_cfg.team_id);          body[n++] = static_cast<uint8_t>(_cfg.team_id >> 8);
    body[n++] = static_cast<uint8_t>(_cfg.team_id >> 16);    body[n++] = static_cast<uint8_t>(_cfg.team_id >> 24);
    body[n++] = nlen;
    for (uint8_t i = 0; i < nlen; ++i) body[n++] = static_cast<uint8_t>(name[i]);
    for (uint8_t i = 0; i < kGrantPrivLen; ++i) body[n++] = priv[i];
    SendDispatch dsp{};                                      // §UI-16 N6b: the dispatch's own facts, no longer discarded
    // ⚠ `[[maybe_unused]]` IS LOAD-BEARING AND IS [[B169]]'s SHAPE, ⛔ not decoration: after N6b the ONLY remaining
    //   reader of this local is the `MR_EMIT` below, and `MR_TELEMETRY` strips that ENTIRELY on every board env
    //   (`MESHROUTE_NO_TELEMETRY`) ⇒ without it the ten board builds would fail `-Wunused-variable` while native and
    //   the corpus stayed green. ⛔ The emit itself keeps publishing `send_by_hash`'s OWN return (⛔ never `dsp.ctr`),
    //   because changing an emitted value is a corpus re-anchor and this slice must be inert.
    [[maybe_unused]] const uint16_t ctr =
        send_by_hash(target_hash, body, n, /*flags=*/0, CryptIntent::on, /*reply_to_hash=*/0,
                     /*mobile_ctr=*/0, plane, DATA_TYPE_TEAM_KEY_GRANT, /*suppress_intro=*/false, &dsp);
    crypto_wipe(body, sizeof body);                          // the private half was in this frame — scrub it
    MR_EMIT("team_key_grant_tx", EF_I("hash", static_cast<int64_t>(target_hash)),
            EF_I("team", static_cast<int64_t>(_cfg.team_id)), EF_I("ctr", ctr), EF_I("nlen", nlen));
    // ★★★★ §UI-16 N6b (2026-08-24, QG-ruled) — THE DISPATCH IS NOW **REPORTED**, NOT INFERRED. This function used to
    //      `return TeamKeyGrantTx::queued` unconditionally on reaching this line, so THREE genuinely different
    //      outcomes wore one word and the screen split them on the COUNTER — which is not an admission signal:
    //        · a FULL TX QUEUE dropped the frame and still returned a non-zero `ctr`  ⇒ `queued` was FALSE;
    //        · a FULL PARKED RING stored nothing and returned 0                        ⇒ "parked" was FALSE;
    //        · a loud pre-admission refusal (the joining gate, a seal failure, the TEAM_KEY_GRANT delegate/cross-layer
    //          structural refusals, a TEAM plane with no routable team origin) also returned 0 ⇒ both were FALSE.
    //      Each of those facts was ALREADY computed one or two frames down; all this does is carry it up (C1: no
    //      branch moved, no drop became a retry, nothing new is emitted — the `team_key_grant_tx` emit above is
    //      unchanged and still fires for every arm, which is what keeps the corpus inert).
    // ⛔ THE TWO CORRELATION TERMS ARE WRITTEN ONLY FOR AN ADMITTED FLIGHT, and `dsp.dst` is the SEND-TIME resolved
    //    id (`send_by_hash` re-resolves the hash against the CURRENT binding) — ⛔ never an id the caller froze.
    switch (dsp.admit) {
        case SendDispatch::Admit::queued:
            if (out_ctr) *out_ctr = dsp.ctr;
            if (out_dst) *out_dst = dsp.dst;
            return TeamKeyGrantTx::queued;
        case SendDispatch::Admit::parked:  return TeamKeyGrantTx::parked;
        case SendDispatch::Admit::refused: return TeamKeyGrantTx::queue_full;
        case SendDispatch::Admit::none:    return TeamKeyGrantTx::send_failed;
    }
    return TeamKeyGrantTx::send_failed;   // ⛔ unreachable (-Wswitch guards the four arms); a REFUSAL, never a claim
}

// RECEIPT. Called ONLY from the delivery path, ONLY after a successful open (node_mac_rx.cpp) — `body` is already the
// decrypted plaintext. Consumes the DM: a grant is control traffic like MOBILE_KEY_FORWARD, never inbox'd and never
// surfaced as a message, so the key bytes cannot reach the app as text on ANY outcome, including every refusal.
Node::TeamKeyGrantRx Node::team_key_grant_receive(const uint8_t* body, uint8_t body_len, uint32_t granter_hash, uint8_t granter_node) {
    auto reject = [&](TeamKeyGrantRx r) {
        MR_EMIT("team_key_grant_reject", EF_I("reason", static_cast<int>(r)), EF_I("from", granter_node),
                EF_I("hash", static_cast<int64_t>(granter_hash)), EF_I("len", body_len));
        return r;
    };
    if (!body || body_len < kGrantMinLen)                    return reject(TeamKeyGrantRx::bad_len);
    const uint32_t their_team = static_cast<uint32_t>(body[0]) | (static_cast<uint32_t>(body[1]) << 8)
                              | (static_cast<uint32_t>(body[2]) << 16) | (static_cast<uint32_t>(body[3]) << 24);
    const uint8_t  nlen = body[4];
    if (nlen > kGrantNameMax)                                return reject(TeamKeyGrantRx::long_name);
    // EXACT length, not "at least": the body is fully self-describing, so a trailing byte means a malformed or
    // re-framed grant and the 32 bytes we would slice as tkpriv might not be the 32 the granter meant (C2).
    if (body_len != static_cast<uint8_t>(kGrantTeamIdLen + 1 + nlen + kGrantPrivLen)) return reject(TeamKeyGrantRx::bad_len);
    if (_cfg.team_id == 0)                                   return reject(TeamKeyGrantRx::no_team);
    if (their_team != _cfg.team_id)                          return reject(TeamKeyGrantRx::team_mismatch);   // spec §2.3 Q2: carried defensively, refused loud
    if (!team_channel_key_adopt_priv(body + kGrantTeamIdLen + 1 + nlen)) return reject(TeamKeyGrantRx::bad_key);
    MR_EMIT("team_key_grant_rx", EF_I("from", granter_node), EF_I("hash", static_cast<int64_t>(granter_hash)),
            EF_I("team", static_cast<int64_t>(their_team)), EF_I("nlen", nlen));
    // The app's surface. ★ The NAME IS NOT PERSISTED — it rides the push and stops there. Storing it would need a new
    // NV field and an NV kVersion bump (a fleet reprovision) for a human label the companion already holds against the
    // team it just onboarded; if the owner wants it durable it is its own slice.
    Push pu{}; pu.kind = PushKind::team_key_received;
    pu.team_id = their_team; pu.sender_hash = granter_hash; pu.origin = granter_node;
    pu.body_len = nlen; for (uint8_t i = 0; i < nlen; ++i) pu.body[i] = body[kGrantTeamIdLen + 1 + i];
    enqueue_push(pu);
    return TeamKeyGrantRx::adopted;
}

// The shared §3.2 dual-layer gate (see node.h). Extracted verbatim from on_init's former inline block so on_init
// and the `gateway` console command validate IDENTICALLY. Mutates l0/l1 window fields (the SF/BW-weighted derive —
// CR is deliberately excluded from the weighting; the rule and its rationale are at the derive itself, below).
// v17 per-layer PHY: the legal SX1262 LoRa bandwidths (Hz). A per-layer bw_hz must be one of these (0 = inherit).
static bool is_valid_bw_hz(uint32_t bw) {
    switch (bw) {
        case 7800: case 10400: case 15600: case 20800: case 31250:
        case 41700: case 62500: case 125000: case 250000: case 500000: return true;
        default: return false;
    }
}

GwValErr validate_gateway_layers(LayerConfig& a, LayerConfig& b, uint32_t radio_bw_hz, uint8_t radio_cr,
                                 double radio_freq_mhz) {
    for (LayerConfig* L : {&a, &b}) {                                            // per-layer required fields (loop order matches the original)
        if (L->layer_id == 0)                       return GwValErr::bad_leaf;     // REQUIRED: full 8-bit id (1..255)
        if (L->routing_sf < 5 || L->routing_sf > 12) return GwValErr::bad_ctrl_sf; // REQUIRED: a valid routing SF
        if (L->allowed_sf_bitmap == 0)              return GwValErr::no_data_sf;   // REQUIRED: a gateway must route data
        if (L->cr != 0 && (L->cr < 5 || L->cr > 8)) return GwValErr::bad_cr;       // v17 per-layer CR: 0=inherit, else 5..8
        if (L->bw_hz != 0 && !is_valid_bw_hz(L->bw_hz)) return GwValErr::bad_bw;   // v17 per-layer BW: 0=inherit, else a legal SX1262 BW
    }
    // §0.8: the two layers MUST differ in their leaf nibble (layer_id & 0x0F) — the coarse byte-0 wire filter;
    // same-nibble co-channel layers would ALIAS (frames cross).
    if ((a.layer_id & 0x0F) == (b.layer_id & 0x0F)) return GwValErr::leaf_nibble_clash;
    // §layer-freq (2026-07-27) C2 fail-loud. A layer with freq_mhz==0 INHERITS radio_freq_mhz, and
    // activate_layer pushes that EFFECTIVE carrier on every window switch precisely so an inherit-leaf is
    // reset off the other leaf's override. With no global there is nothing to reset TO — the Hal drops
    // mhz<=0 — so the inherit-leaf would run the override-leaf's carrier, silently, RX and TX. That is the
    // metal bug this slice fixes; refuse the config instead of half-fixing it. NB the two legal shapes are
    // untouched: BOTH inherit + no global = the legacy single-carrier world (the radio never moves), and
    // BOTH override needs no global at all.
    if (((a.freq_mhz > 0.0) != (b.freq_mhz > 0.0)) && !(radio_freq_mhz > 0.0)) return GwValErr::freq_inherit_no_global;
    if (a.window_period_ms != b.window_period_ms)   return GwValErr::period_mismatch;  // the ONE shared cycle must agree
    if (a.window_period_ms == 0)                    return GwValErr::period_zero;       // validate-not-clamp: refuse a 0 cycle
    if (a.window_ms && b.window_ms && a.window_ms + b.window_ms > a.window_period_ms) return GwValErr::window_overlap;
    // SF/BW-weighted anti-phase window derive (window_i = period * per_byte_air(L_i) / sum; offset[1] = window[0]).
    const uint32_t period = a.window_period_ms;
    // v17 + §cr-out-of-split (owner ruling 2026-07-27): weight each layer's window by ITS OWN effective SF and BW —
    // a slower SF / narrower BW = more airtime/byte = a longer window. eff_bw = the per-layer override, else the
    // global fallback (mirrors active_bw_hz()).
    // ★ CR IS DELIBERATELY EXCLUDED FROM THE WEIGHTING — a rule, not an oversight. Do NOT "fix" it back.
    //   WHY: SF and BW are properties every member of a layer MUST share to demodulate each other at all, so they
    //   are genuine properties OF THE LAYER. CR rides in the LoRa EXPLICIT PHY header and is auto-detected per
    //   frame, so nodes running different CRs still interoperate — CR is a property of a TRANSMISSION, not of the
    //   channel. And this derive REDISTRIBUTES ONE FIXED period, so weighting by CR would let a layer's private CR
    //   choice take window time away from its PEER layer. Hence both layers weigh at the node's global radio_cr.
    //   ⚠ DELIBERATELY NOT DONE — ignoring CR does not make a heavy CR free. A CR4/8 layer genuinely occupies more
    //   airtime and can still overrun a window that no longer grows for it; nothing here compensates for that. The
    //   trade is that it overruns ITS OWN window instead of stealing its peer's. Scoped to the SPLIT only: real TX
    //   airtime still uses the per-layer effective CR everywhere else (active_cr(), activate_layer's set_rx_cr).
    //   ⚠ NOT cr-invariant in the ABSOLUTE sense (measured 2026-07-27): airtime_ms floors twice, so per_byte_air is
    //   only ~linear in cr (±1 ms) and the ratio still shifts by up to ~93 ms of a 15 s period as the GLOBAL cr
    //   varies 5..8. That residual is PRE-EXISTING and unchanged by this rule (an inherit-both config already fed
    //   radio_cr to both sides), and it is symmetric, so it can never favour one layer. What the rule removes is
    //   the PER-LAYER CR differential, which was neither symmetric nor small (s32: 3188/8812 -> 4395/7605 ms).
    auto eff_bw = [&](const LayerConfig& L) -> uint32_t { return L.bw_hz > 0 ? L.bw_hz : radio_bw_hz; };
    auto per_byte_air = [&](const LayerConfig& L) -> uint32_t {     // marginal payload airtime for 120 B (preamble cancels)
        return airtime_ms(L.routing_sf, eff_bw(L), radio_cr, protocol::preamble_sym, 240)
             - airtime_ms(L.routing_sf, eff_bw(L), radio_cr, protocol::preamble_sym, 120);
    };
    const uint32_t w0 = per_byte_air(a), w1 = per_byte_air(b);
    if (w0 + w1 == 0) return GwValErr::window_degenerate;                          // guard; SFs are 5..12
    if (a.window_ms == 0 && b.window_ms == 0) {                                    // both DERIVE: SF/BW-weighted, fill the period
        a.window_ms = static_cast<uint32_t>(static_cast<uint64_t>(period) * w0 / (w0 + w1));
        b.window_ms = period - a.window_ms;
    } else if (a.window_ms == 0) {                                                 // one explicit -> the other fills the rest
        a.window_ms = (period > b.window_ms) ? period - b.window_ms : 0;
    } else if (b.window_ms == 0) {
        b.window_ms = (period > a.window_ms) ? period - a.window_ms : 0;
    }
    if (b.window_offset_ms == 0) b.window_offset_ms = a.window_ms;                 // anti-phase (layer0 offset stays 0)
    if (a.window_ms == 0 || b.window_ms == 0) return GwValErr::window_zero;        // concrete-schedule validation (fail loud)
    if (a.window_offset_ms + a.window_ms > period) return GwValErr::window_exceeds_period;
    if (b.window_offset_ms + b.window_ms > period) return GwValErr::window_exceeds_period;
    if (a.window_offset_ms < b.window_offset_ms + b.window_ms &&
        b.window_offset_ms < a.window_offset_ms + a.window_ms) return GwValErr::window_overlap;   // intervals overlap
    if (a.window_ms > protocol::gateway_schedule_window_max_ms ||                   // §3e F-C: fit the 8-bit ×100ms wire field (25.5 s)
        b.window_ms > protocol::gateway_schedule_window_max_ms) return GwValErr::window_too_long;
    return GwValErr::ok;
}

// Parse the `gateway l0=… l1=… [opts]` console line (see node.h). Pure C-string parsing into `out`; per-field
// ranges only — the cross-layer/nibble/window gate is validate_gateway_layers, run by the caller. node ∈ 1..254
// (the 1..16 gateway reservation is a Join-time convention, NOT enforced here).
GwParseErr parse_gateway_cmd(const char* args, GatewayProvision& out) {
    out = GatewayProvision{};                                   // defaults: window_period_ms 15000, window_ms 0, beacon 900000
    if (!args) return GwParseErr::missing_l0;
    char buf[192]; size_t blen = 0;
    for (; args[blen] && blen < sizeof(buf) - 1; ++blen) buf[blen] = args[blen];
    buf[blen] = '\0';

    auto parse_data_sfs = [](const char* s, uint16_t& bm) -> bool {     // "7,9,10" -> bitmap; false if empty/out-of-range/junk
        bm = 0;
        if (!s || !*s) return false;
        const char* d = s;
        while (*d) {
            if (*d < '0' || *d > '9') return false;
            long v = 0; while (*d >= '0' && *d <= '9') { v = v * 10 + (*d - '0'); ++d; }
            if (v < 5 || v > 12) return false;
            bm |= static_cast<uint16_t>(1u << v);
            if (*d == ',') ++d; else if (*d) return false;             // a number must be followed by ',' or end
        }
        return bm != 0;
    };
    auto parse_leaf = [&](char* s, LayerConfig& L) -> GwParseErr {      // "level:node:ctrl_sf:data_sfs" (level = the 1..255 layer id; leaf nibble = level & 0x0F)
        char* part[4] = { s, nullptr, nullptr, nullptr }; int np = 1;
        for (char* p = s; *p; ++p) if (*p == ':') { if (np >= 4) return GwParseErr::bad_l0; *p = '\0'; part[np++] = p + 1; }
        if (np != 4) return GwParseErr::bad_l0;
        for (int i = 0; i < 3; ++i) {                                  // leaf/node/ctrl_sf are plain ints
            if (!part[i][0]) return GwParseErr::bad_l0;
            for (char* q = part[i]; *q; ++q) if (*q < '0' || *q > '9') return GwParseErr::bad_l0;
        }
        const long leaf = atol(part[0]), node = atol(part[1]), sf = atol(part[2]);
        if (leaf < 1 || leaf > 255) return GwParseErr::bad_leaf;       // valid 8-bit id (validate also rejects 0 — parity)
        if (node < 1 || node > 254) return GwParseErr::bad_node;       // 1..254 — the 1..16 reservation is NOT enforced here
        if (sf < 5 || sf > 12)      return GwParseErr::bad_ctrl_sf;
        uint16_t bm = 0; if (!parse_data_sfs(part[3], bm)) return GwParseErr::bad_data_sf;
        L.layer_id = static_cast<uint8_t>(leaf); L.node_id = static_cast<uint8_t>(node);
        L.routing_sf = static_cast<uint8_t>(sf); L.allowed_sf_bitmap = bm;
        return GwParseErr::ok;
    };

    bool have_l0 = false, have_l1 = false;
    char* p = buf;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char* tok = p;
        while (*p && *p != ' ') ++p;
        if (*p == ' ') { *p = '\0'; ++p; }
        if      (!strncmp(tok, "l0=", 3)) { GwParseErr e = parse_leaf(tok + 3, out.l0); if (e != GwParseErr::ok) return e; have_l0 = true; }
        else if (!strncmp(tok, "l1=", 3)) { GwParseErr e = parse_leaf(tok + 3, out.l1); if (e != GwParseErr::ok) return (e == GwParseErr::bad_l0) ? GwParseErr::bad_l1 : e; have_l1 = true; }
        else if (!strncmp(tok, "period=", 7)) { const long v = atol(tok + 7); if (v < 1) return GwParseErr::bad_period; out.l0.window_period_ms = out.l1.window_period_ms = static_cast<uint32_t>(v); }
        else if (!strncmp(tok, "beacon=", 7)) { const long v = atol(tok + 7); if (v < 1) return GwParseErr::bad_beacon; out.beacon_ms = static_cast<uint32_t>(v); }
        else if (!strncmp(tok, "freq0=", 6)) { const double f = atof(tok + 6); if (f <= 0.0) return GwParseErr::bad_freq; out.l0.freq_mhz = f; }
        else if (!strncmp(tok, "freq1=", 6)) { const double f = atof(tok + 6); if (f <= 0.0) return GwParseErr::bad_freq; out.l1.freq_mhz = f; }
        else if (!strncmp(tok, "bw0=", 4)) { out.l0.bw_hz = protocol::khz_to_hz(atof(tok + 4)); }   // v17 per-layer BW in kHz (fractional ok, e.g. 62.5) -> Hz, matching create/join; 0=inherit; validate gates it
        else if (!strncmp(tok, "bw1=", 4)) { out.l1.bw_hz = protocol::khz_to_hz(atof(tok + 4)); }
        else if (!strncmp(tok, "cr0=", 4)) { out.l0.cr    = static_cast<uint8_t>(atoi(tok + 4)); }    // v17 per-layer CR (0 = inherit)
        else if (!strncmp(tok, "cr1=", 4)) { out.l1.cr    = static_cast<uint8_t>(atoi(tok + 4)); }
        else if (!strncmp(tok, "win0=", 5) || !strncmp(tok, "win1=", 5)) {
            LayerConfig& L = (tok[3] == '0') ? out.l0 : out.l1;       // "ms:off"
            char* c = tok + 5; char* colon = nullptr;
            for (char* q = c; *q; ++q) if (*q == ':') { colon = q; break; }
            if (!colon) return GwParseErr::bad_window;
            *colon = '\0';
            const long ms = atol(c), off = atol(colon + 1);
            if (ms < 0 || off < 0) return GwParseErr::bad_window;
            L.window_ms = static_cast<uint32_t>(ms); L.window_offset_ms = static_cast<uint32_t>(off);
        }
        else if (!strncmp(tok, "gateway_only=", 13)) { out.gateway_only = (atol(tok + 13) != 0); }
        else return GwParseErr::unknown_opt;
    }
    if (!have_l0) return GwParseErr::missing_l0;
    if (!have_l1) return GwParseErr::missing_l1;
    return GwParseErr::ok;
}

bool Node::on_init(const NodeConfig& cfg) {
#if defined(MR_GATEWAY_BUILD)
    // F1 (RAM-safety guard): the DEDICATED gateway firmware cuts cap_channel_buffer to 8 at COMPILE time, which is
    // only safe because a dual-layer gateway SKIPS the channel plane at RUNTIME (n_layers==2). A single-layer config
    // on a gateway build (e.g. fresh NV → n_layers=1) would run the FULL plane into the 8-entry buffer = silent lossy
    // gossip. REFUSE it (fail-loud): a gateway build is dedicated to gateways. (The node stays up for re-provisioning.)
    if (cfg.n_layers < 2) return false;
#endif
    // Defend the fixed _layers[MR_N_LAYERS] bound (fail-loud, §3.2): refuse an out-of-range layer count from ANY
    // source (corrupt NV, a direct caller, a mis-cast host config). The normalization/validation below only
    // handles n_layers in {1, 2}, so n_layers > MR_N_LAYERS would otherwise set _n_layers past the array end.
    if (cfg.n_layers > MR_N_LAYERS) return false;
    // Dual-layer validation gate (§3.2) — a GATEWAY (n_layers==2) must have both layers' REQUIRED fields set and
    // non-overlapping explicit windows. Fail LOUD (refuse, the node stays down) — no silent inherit / auto-adjust.
#if MR_N_LAYERS < 2
    if (cfg.n_layers == 2) return false;   // a gateway config on a single-layer build — REFUSE (no _layers[1]). Fail
                                           // loud, never silently single-layer. Build [env:gateway] (-DMR_N_LAYERS=2).
#endif
    _cfg = cfg;
    // §mobile Option A: a MOBILE is NEVER a leaf-config-plane member (it adopts only the host's PHY, never lineage/config —
    // node_beacon.cpp membership exemption). Normalize UNMANAGED at init so (a) a mobile that adopted a lineage before this
    // rule self-heals, and (b) a stale managed epoch can never leave it un-synced -> leaf_config_synced() true -> it can
    // originate DMs. A mobile advertises lineage 0 (unmanaged) in its beacon. Inert for a static node (is_mobile false).
    if (_cfg.is_mobile) { _cfg.lineage_id = 0; _cfg.config_epoch = 0; }
    // Slice 0: a single-layer node mirrors its legacy scalars into layers[0] (backward-compat until Slice 2a migrates
    // the readers). A GATEWAY (n_layers==2) supplies layers[0..1] explicitly, validated by the SHARED §3.2 gate —
    // the SAME predicate the `gateway` console command runs, so the command can never accept a config on_init refuses.
    if (_cfg.n_layers <= 1) {
        _cfg.n_layers = 1;
        _cfg.layers[0].layer_id          = _cfg.leaf_id;          // single-layer: layer_id == leaf_id (may be 0 for R1)
        _cfg.layers[0].node_id           = _node_id;              // single-layer: the one node_id (Slice 3a)
        _cfg.layers[0].routing_sf        = _cfg.routing_sf;
        _cfg.layers[0].allowed_sf_bitmap = _cfg.allowed_sf_bitmap;
        _cfg.layers[0].beacon_period_ms  = _cfg.beacon_period_ms;
        _cfg.layers[0].window_ms         = _cfg.layers[0].window_period_ms;   // no split: one window == whole period (always-on)
    } else {
        // §3.2 dual-layer gate (shared with parse_gateway_cmd): required fields + leaf-nibble + the SF-weighted
        // anti-phase window derive/validate (mutates _cfg.layers' window fields in place). Fail LOUD — node stays down.
        if (validate_gateway_layers(_cfg.layers[0], _cfg.layers[1], _cfg.radio_bw_hz, _cfg.radio_cr,
                                    _cfg.radio_freq_mhz) != GwValErr::ok)
            return false;
    }
    _n_layers = _cfg.n_layers;   // Slice 3c: mirror the (normalized) layer count to the runtime member activate_layer guards on
    // is_gateway is DERIVED, NOT configurable: a gateway IS a dual-layer node (n_layers==2). Single authoritative point
    // (overrides any cfg/NV value) — so self_gateway / J gateway_capable reliably mean "dual-layer gateway".
    _cfg.is_gateway = (_cfg.n_layers == 2);
    // ★ §B132 (C3 — a gateway build comes out INERT, not merely refusing): a gateway must never be a mobile HOME
    // (it time-multiplexes one radio across two leaves, so it can only serve a home's continuous obligations —
    // last-mile delivery, presence, hash proxying, liveness — for half of every schedule period). `host_mobiles`
    // DEFAULTS TRUE and is persisted, so a node reprovisioned into a gateway would otherwise keep carrying an
    // effective "yes". Force the EFFECTIVE value off at the single point where `is_gateway` becomes known, so
    // `config()`/`cfg` REPORT the truth rather than a stale yes, and clear any hosted-mobile runtime state a
    // pre-gateway life may have accumulated. ⚠ Deliberately AFTER the derivation above and NOT a substitute for
    // can_host_mobiles() at the decision sites: this cannot cover the REFUSED on_init path (which returns before
    // this line with `_cfg.n_layers == 2` intact) — that path is exactly why the invariant is re-checked per site.
    if (_cfg.is_gateway) {
        _cfg.host_mobiles = false;
        for (uint8_t li = 0; li < MR_N_LAYERS; ++li) {                // every leaf, not just _active — a gateway owns two
            _layers[li]._mobile_reg_n    = 0;
            _layers[li]._notify_pending_n = 0;
            for (uint8_t k = 0; k < protocol::cap_host_mobiles; ++k) _layers[li]._mobile_snr_q4[k] = 0;
        }
        // ★★ §B132b: the REGISTRY clear above is not the whole state — a mobile-host transmission can be STAGED and
        // PENDING (the jittered J OFFER stash + the roster coalesce window). Drop those and cancel their timers on
        // every leaf too, or a frame committed one moment before this init is still fired afterwards, BY THE GATEWAY.
        mobile_host_pending_clear();
    }
    // Lua: (SF_DEMOD_THRESHOLD[routing_sf] or -240) + sf_margin_q4 (dv_dual_sf.lua:8386).
    // The out-of-range fallback is the literal -240 (SF10), NOT table[12].
    const int16_t demod = (_cfg.routing_sf >= 5 && _cfg.routing_sf <= 12)
                          ? protocol::sf_demod_threshold_q4_table[_cfg.routing_sf]
                          : static_cast<int16_t>(-240);
    _routing_snr_floor_q4 = static_cast<int16_t>(demod + protocol::sf_margin_q4);
    _hal.set_rx_sf(_cfg.routing_sf);                       // listen on routing SF

    // R4.0 duty-cycle budget = floor(duty_cycle * window) (Lua dv:8497). 0 => disabled (HEALTHY).
    _duty_cycle_budget_ms = (_cfg.duty_cycle > 0.0)
        ? static_cast<uint64_t>(_cfg.duty_cycle * _cfg.duty_cycle_window_ms)
        : 0;

    // R4.5 LBT delays (Lua dv:8628-8632). 0-config => derive: backoff = max(1, retry_jitter/2); the flood
    // max-defer = one full-size beacon's airtime. (retry_jitter_ms() is the same RTS_LEN=8 timing constant.)
    _lbt_backoff_ms = (_cfg.lbt_backoff_ms > 0) ? _cfg.lbt_backoff_ms
                      : (retry_jitter_ms() / 2 > 1 ? retry_jitter_ms() / 2 : 1);
    _flood_lbt_max_defer_ms = (_cfg.flood_lbt_max_defer_ms > 0) ? _cfg.flood_lbt_max_defer_ms
                              : airtime_routing_ms(protocol::beacon_max_bytes);

    // Slice 3c: a GATEWAY boots on leaf 0 — activate_layer(0) overrides the active-layer scalars (routing_sf /
    // leaf_id / beacon_period_ms / node_id), the SNR floor + LBT timing, and retunes the radio to leaf 0's SF.
    // (Runs AFTER the legacy-scalar setup above, which it supersedes; the discovery/beacon arming below then
    // reads leaf 0's values.) A single-layer node never activates — its scalars stay as set above (no-op path).
    if (_cfg.n_layers == 2) {
        _window_epoch_ms = _hal.now();                                           // Slice 3d GRID: anchor the absolute window grid at boot
        activate_layer(0);                                                       // boot on leaf 0
        set_window_anchors(0);                                                   // Slice 3e: seed the countdown anchors
        (void)_hal.after(_cfg.layers[0].window_ms, kLayerWindowTimerId);         // Slice 3d: arm the first window-switch (leaf0 -> leaf1)
    }

    // Discovery window: boot in fast-cadence / full-page mode until we have heard
    // enough of the mesh or a bounded timeout expires (dv_dual_sf.lua:8399-8401).
    const uint64_t now_disc = _hal.now();
    for (uint8_t i = 0; i < _n_layers; ++i) {                 // §per-layer discovery: n_layers==1 (normal node) => leaf 0 only = UNCHANGED;
        _layers[i]._discovery_started_ms   = now_disc;        // a gateway arms BOTH leaves so leaf 1 runs its own fast cadence in its own windows
        _layers[i]._discovery_mode         = (protocol::discovery_ms > 0);
        _layers[i]._discovery_until_ms     = now_disc + protocol::discovery_ms;
        _layers[i]._discovery_bcn_rx_count = 0;
    }

    // §P2 freshness-coupling sanity (LOUD, not a refuse — the node still runs): a neighbour must beacon faster than the
    // freshness window (next_hop_live_ttl_ms) or its route is wrongly demoted as a stale next-hop. The defaults hold
    // (beacon 15min/discovery faster < 20min ttl); flag a raised beacon_period that breaks the coupling.
    if (_cfg.beacon_period_ms >= protocol::next_hop_live_ttl_ms)
        _hal.log("WARN: beacon_period_ms >= next_hop_live_ttl_ms (P2) — quiet-but-alive neighbours will be demoted as stale");

    // Arm the first beacon spread across the (phase-dependent) period to avoid a mass-boot burst (dv:9027-9035).
    // A GATEWAY does NOT use the shared kBeaconTimerId — its single deadline would HALVE the per-leaf cadence (one
    // beacon/period landing on whichever leaf is active then). Instead it beacons each leaf at WINDOW-ACTIVATION on
    // that leaf's own cadence (maybe_emit_gateway_beacon); seed leaf 0 now (after discovery state is set above).
    if (_cfg.n_layers == 2) {
        maybe_emit_gateway_beacon();
    } else {
        const int first_period = static_cast<int>(in_discovery() ? protocol::discovery_beacon_period_ms
                                                                 : steady_beacon_period_ms());
        (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, first_period)), kBeaconTimerId);
    }
    // ★★★ §MH-S3 §5.2 — THE AUTOMATIC BOOT ATTEMPT DRAWS A STARTUP JITTER; A MANUAL ONE STAYS IMMEDIATE.
    // This kick used to be `after(0, ...)` unconditionally, so a fleet powered from one switch put every
    // mobile's first DISCOVER on the air in the same millisecond — the alignment §5.2 exists to break, and
    // the one LBT cannot help with (they all sense a clear channel at the same instant).
    // ★ THE DRAW IS GATED ON `mobile_autoregister`, NOT on the kick, and the two are NOT the same condition:
    //   a team-only mobile (autoregister OFF, team_id set) is kicked purely so `team_dad_fire` runs, and
    //   `registration_armed()` then returns before any DISCOVER — there is no automatic attempt to stagger,
    //   so it draws NOTHING and keeps its `after(0, ...)`. Neither is `mobile_register_*` (node.h), which is
    //   §5.2's "the manual first attempt is immediate" and is deliberately left at zero.
    // ★ §MH-S4 §4.1/§4.2 — THE BOOT STATE OF THE ATTACHMENT PLANE, stated rather than left at the member's
    // default. `mobile_autoregister` ON means an automatic session starts now, so the plane is `seeking` from
    // this instant — before the jittered first DISCOVER fires, which is exactly the window in which a companion
    // app connects and asks. OFF means `dormant` (§4.2: "boot remains dormant; the user/app/Heltec starts
    // attachment with `mobile register`") — INCLUDING for a team-only mobile, whose kick below exists solely to
    // run team-DAD and which emits no DISCOVER at all.
    // ⛔ The HOME-LINK plane stays `unknown` and is not named here: nothing has been measured yet, which is its
    //    definition. A non-mobile node leaves both members at their `dormant`/`unknown` defaults.
    // ★★★★ §MH-S4b §4.2 — AND THE SESSION STATE IS SEEDED FROM THE FLAG **HERE, ONCE**. `mobile_autoregister` is
    // the BOOT POLICY ("`true`: boot enters `seeking` automatically"); `_mobile_home_desired` is the live session,
    // and it is now the WHOLE of `registration_armed()`/`mobile_service_desired()` (node.h documents why). Seeding
    // it here is what makes `mobile unregister` mean something on an `autoregister=1` device: before this, the verb
    // cleared a member that the predicates OR-ed away, so the node reported `dormant` while still being armed.
    // ⛔ Read ONCE and never re-read: §4.2 rules that an explicitly-started session continues independently of the
    //    initial-auto flag, so a later `cfg set mobile_autoregister 0` must not end a live session.
    // ⓘ CORPUS-INERT: the seeded value IS `_cfg.mobile_autoregister`, and no scenario can reach the console verbs
    //   that later change it ⇒ both predicates hold exactly the value they held before this slice, for the whole run.
#if MR_FEAT_MOBILE
    if (_cfg.is_mobile) {
        _mobile_home_desired = _cfg.mobile_autoregister;
        _mobile_attach_state = _cfg.mobile_autoregister ? MobileAttachState::seeking : MobileAttachState::dormant;
    }
#endif
    if (_cfg.is_mobile && (_cfg.mobile_autoregister || _cfg.team_id != 0))
        (void)_hal.after(_cfg.mobile_autoregister
                             ? static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::mobile_boot_jitter_ms) + 1))
                             : 0u,
                         kMobileDiscoverTimerId);   // §mobile 2b/console: kick the FSM. §6.4: a TEAM member ALSO kicks it regardless of the toggle so team-DAD runs. §autoregister ruling (2026-07-21): the kick still fires, but the FSM's DISCOVER/registration half is now gated on registration_armed() (autoregister ON, or a manual `mobile register` arm) — a team member with autoregister=false thus runs team-DAD but emits ZERO DISCOVERs; a persisted _team_local_id makes team-DAD a no-op.
    // Periodic route-aging sweep (dv_dual_sf.lua:9080-9086).
    (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
    // REQ_SYNC bootstrap (dv_dual_sf.lua:9166-9175): after a listen window, broadcast a REQ_SYNC Q
    // while still in discovery + route-starved, so a sparse joiner pulls neighbours' tables instead
    // of waiting out the slow periodic-beacon rotation. The loop (kReqSyncTimerId) re-arms itself.
    if (_cfg.req_sync_on_boot && in_discovery())
        (void)_hal.after(protocol::req_sync_listen_ms, kReqSyncTimerId);
    // Hash-locate A0: seed our OWN binding (authenticated) so we resolve self-directed H queries (Lua
    // dv:9072). node_id 0 is unprovisioned (no identity yet) — set_identity re-seeds after a join/cfg.
    if (_node_id != 0) id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);
    return true;
}

#if MR_FEAT_TEAM
// §clean-team (2026-07-27): THE team plane's "old network's learned state is stale" clear — ONE implementation, TWO
// callers (U1): clear_routing_state() below (the static join/create/leave reprovision) and set_team_id() (a `team new` /
// `team <id>` / `team 0` / `cfg set team_id` switch). A team switch calls ONLY this, deliberately: the static plane
// (_rt / _id_bind / gateway schedules / bridged layers / hosted-mobile registry / channel buffer / our own home
// registration) belongs to a network that did NOT change, so wiping it would strand a HOMED team mobile that merely
// changed teams. Count-reset only where the table is count-bounded (stale bytes are never read past _n) — the codebase's
// clear idiom; the bitset is the one that must be scrubbed byte-wise because it is set-only, never counted.
//
// ★ DONE — every #if MR_FEAT_TEAM-gated learned table in node.h is covered here; that is the whole team plane:
//   _rt_team/_rt_team_count · _team_peer · _team_liveness(_n) · _team_keys(_n) · _rreq_seen_team(_n)/_rreq_last_team(_n).
//   (_team_local_id / _team_dad_pending are Node-global identity, not a table — set_team_id drops them via
//   set_team_local_id(0), which also clears the pending DAD-guard flag so an in-flight guard timer fires inert.)
// ★ NOT cleared here, deliberately, each with the reason (do not "complete" these without reading the reason):
//   • the CHANNEL plane's team-scoped state (buffer rows, flood states, staged/in-flight M frames, re-offer slots).
//     ✔ CLOSED 2026-07-27 by §clean-team-channel — but in its OWN function, purge_tx_carriers() (node_channel.cpp),
//     which set_team_id() calls right after this one, NOT here: it is a SELECTIVE compaction (the buffer + tx queue also
//     hold still-valid NON-team leaf rows, so a count reset would be over-broad). ★ 2026-07-27 §clean-join-carriers:
//     that function is now axis-parameterised and clear_routing_state below calls it too (PurgeAxis::reprovision) — so
//     the two axes share ONE sweep instead of the reprovision path re-clearing the plane its own way. The full
//     dependent-state audit (what is scrubbed, what is harmless, and why) lives at that definition.
//   • parked (_parked_sends) / deferred (_deferred) sends whose TxItem carries Plane::TEAM: bounded by
//     send_defer_ttl_ms and they simply fail to find a route on the fresh plane. Also PRE-EXISTING-symmetric — the
//     static reprovision does not clear _parked_sends either.
//   • the shared ledgers (_seen_origins / _per_origin_channel / _hash_query_seen / _mediated_recent / _blind_until): all
//     TTL-windowed, and all fail in the SUPPRESS direction (a dropped duplicate / a withheld DENY), never a mis-address.
//     ⚠ 2026-07-28 §team-parity T6/B: the FIRST THREE ARE NO LONGER PLANE-BLIND (see node.h's §P2-7 audit, now updated),
//     and `_mediated_recent` was audited and found unable to alias at all. So "clearing them from here would reach into
//     the static plane" is now only true of `_blind_until`. The other four still stay UNCLEARED — ✖ MISSING,
//     deliberately: a selective team-only clear is a behaviour change on the team-switch axis and T6/B was the
//     plane-keying slice (C1). The suppress-only + TTL argument above is what makes leaving them safe meanwhile.
void Node::clear_team_routing_state() {
    for (uint8_t i = 0; i < _n_layers; ++i) {
        LayerRuntime& L = _layers[i];
        L._rt_team_count = 0;                // §mobile 6.2: the TEAM DV plane — a stale _rt_team + _team_peer bit would SHADOW the fresh plane (rt_find dispatches on is_team_peer with no _rt fallback).
        for (auto& v : L._team_peer) v = 0;  // §6.2: scrub the set-only team-peer bitset (mirror the _mobile_peer clear in clear_learned_state)
        L._team_liveness_n = 0;              // §clean-join R3: the team-plane liveness mirror (2c) is old-team state too — a stale dead/silent tier would misrank the fresh team's _rt_team. Same disease as the mobile registry.
        // ★ ADDED 2026-07-27 (§clean-team audit): the two team tables that were cleared by NOTHING — not by this
        // reprovision path, not by clear_learned_state. So the pre-existing gap was WIDER than the team switch:
        // `join`/`create`/`leave` left them stale too. Both are now fixed by extension, in this one place.
        L._team_keys_n = 0;                  // ★ THE MIS-ADDRESSING ONE, worse than a stale route: _team_keys is the team-SCOPED id<->key_hash32 cache (NOT _id_bind). Stale entries make team_key_of_id hand a send the WRONG DST_HASH (node_mac.cpp:94) and team_id_of_key reverse-resolve a hash to the WRONG teammate (node_hashlocate.cpp:988/1019). The _team_peer bit is NOT sufficient cover: node_beacon.cpp:848 sets the bit from a multi-hop DV entry with no key at all, and at :758 the bit is set BEFORE the :766 L2a mediation reads the cache — so a new teammate reusing an old team id would draw a SPURIOUS mediated DENY on its very first beacon.
        L._rreq_seen_team_n = 0; L._rreq_last_team_n = 0;   // team-plane F RREQ dedup + rate-limit, keyed by (origin,dst) TEAM ids that the new team recycles -> a stale hit suppresses/throttles a legitimate fresh team route discovery for the TTL window. The static siblings (_rreq_seen_n/_rreq_last_n) are cleared by clear_learned_state; these had no clear at all.
    }
}
#endif   // MR_FEAT_TEAM (clear_team_routing_state)

// §clean-team (2026-07-27, owner bench report): THE team-switch entry point. Every LIVE writer of _cfg.team_id goes
// through here (`team new` mint / `team <id>` join / `team 0` leave — ★ and `cfg set team_id` is no longer a fourth
// spelling: it was REMOVED 2026-07-31 as a forked, unguarded duplicate, see firmware_config.cpp:279) so a switch is coherent in ONE
// place: drop the OLD team's learned plane, the OLD team-DAD id and the OLD team's CHANNEL CONTENT KEY (§o3-key-lifetime)
// BEFORE _cfg.team_id names the new team. Without the
// clear, a stale _rt_team + _team_peer bit SHADOWS the fresh plane and a stale _team_keys row MIS-ADDRESSES a send.
// Returns true iff the team ACTUALLY changed -> the caller runs its re-DAD (team_dad_fire) only then; a same-team no-op
// (`team <current_id>`) clears NOTHING, because nothing is stale — ★ INCLUDING the content key, which a same-team call
// must never destroy (it is UNRECOVERABLE; only a teammate's re-grant brings it back).
// ⚠ DELIBERATELY NOT USED BY THE BOOT/NV PATH (src/fw_main.cpp assigns cfg.team_id directly, pre-on_init): at boot every
// team table is already empty so there is nothing to clear, and set_team_local_id(0) would destroy the PERSISTED
// team-DAD id that fw_main loads immediately afterwards (a needless re-DAD + a lost defended id) — ★ and, since
// §o3-key-lifetime, the PERSISTED team channel key that fw_main.cpp:715 restores on the very next line.
// NOT #if-forked: all three callees inline-stub to no-ops on a !MR_FEAT_TEAM build (node.h), so ONE implementation serves
// both profiles and the gateway build keeps identical behaviour (bare _cfg.team_id assignment) by construction.
bool Node::set_team_id(uint32_t team_id) {
    if (_cfg.team_id == team_id) return false;   // no-op: same team -> nothing is stale (C2: no silent side-effects)
    // ★ B28/R2's "unless it is IMPOSSIBLE (firmware without teams handling)" clause, at the live switch: a build with
    // MR_FEAT_MOBILE 0 has no roaming-endpoint plane for a team member to be reachable ON, so a non-zero team is
    // REFUSED here rather than adopted — nothing moves, and the caller sees the same `false` it gets for a same-team
    // no-op. Deliberately NOT folded into role_enforce() below: that helper RESTORES the invariant on a config it is
    // handed (the NV boot path has no way to refuse a provisioned blob, so there it drops the team instead), whereas a
    // LIVE verb can refuse before any state moves — and refusing beats adopting-then-stripping, which would have this
    // function return true while team_id stayed 0 and make its caller announce a team it did not join.
    // ⓘ Fully DEAD CODE on every profile that HAS the plane (kRoleHasMobilePlane is constexpr) — and on the profiles
    // that do not, `handle_team` is itself compiled out (`#if MR_N_LAYERS < 2`), so this is the backstop for a direct
    // core caller, not a reachable console path. See role_enforce's twin arm for the path that IS reachable there (NV).
    if (team_id != 0 && !kRoleHasMobilePlane) {
        _hal.log("set_team_id refused: this build has no mobile plane (MR_FEAT_MOBILE 0) for a team member to live on");
        return false;
    }
    clear_team_routing_state();                  // the OLD team's routes / peer set / liveness / key cache / RREQ ledgers
    team_channel_key_clear();                    // §o3-key-lifetime (owner ruling 2026-07-31): the OLD team's CONTENT key. Distinct from the _team_keys cache cleared just above — that one maps teammate ids to key HASHES, this is the shared secret a post is sealed UNDER. ⚠ NOT in clear_team_routing_state: its OTHER caller is clear_routing_state (create/join/leave), which MUST preserve the key (firmware_config.cpp's blob_take_team_channel_key exists precisely to carry it through a reprovision). ⓘ The one exception to "a switch takes no key with it" lives at the CALLER: handle_team re-applies a pair minted/adopted FOR THE TEAM BEING JOINED across this call — see its §o3-key-lifetime note.
    purge_tx_carriers(PurgeAxis::team_switch);    // §clean-team-channel: the OLD team's channel CONTENT + every carrier that would re-emit it (buffer rows / flood states / staged + in-flight M frames / re-offer slots). Team-scoped rows only — the leaf channel rows survive. Compiled on EVERY profile now (it also serves the reprovision axis), but the team_switch predicate can never match on a !MR_FEAT_TEAM build: no writer sets ChannelEntry::team_id / FloodState::team_flood / the channel_flavor_team bit there.
    set_team_local_id(0);                        // §6.4: drop the stale team-DAD id (0 = left; a re-DAD picks a fresh one for the new team) + clear _team_dad_pending
    _cfg.team_id = team_id;                      // LIVE (team_dad_fire / same_team / team_addr_for_us all read _cfg.team_id)
    // ★★ B28/R2 (owner ruling 2026-07-31: *"setting mobile if creating or joining a team"*): a team member IS a mobile
    // — `team_local_id` is a reachable-through-someone-else identity — so ADOPTING a team promotes this node to MOBILE.
    // THE single LIVE enforcement point, and it covers BOTH verb spellings (`team new` mint and `team <id>` join)
    // because both route through here (U1); the other two points are the NV boot restore (src/fw_main.cpp) and the
    // `cfg set mobile 0` refusal (src/firmware_config.cpp), because NV carries the two fields independently.
    // ⚠ ONE-DIRECTIONAL (R3): `team 0` returns RoleFix::none and leaves is_mobile ALONE — a mobile with no team is a
    // legitimate, reachable configuration (a homed roaming endpoint), so leaving a team must not un-mobile the node.
    // ★ SELF-ANNOUNCING, so R5 needs no explicit re-beacon here: is_mobile rides the BEACON (bit 0x20) and the J frame
    // (bit 0x40) packed straight off _cfg at frame-build time (node_beacon.cpp / node_join.cpp), so the next beacon
    // already carries the new role — and handle_team's existing `c.is_mobile && t != 0 && team_switched` team_dad_fire()
    // now fires for a promoted node too, which is exactly the team-plane bootstrap an already-mobile joiner gets.
    // ⓘ The RoleFix is deliberately NOT plumbed through this function's bool, which means "the team ACTUALLY changed"
    // and nothing else. The caller detects the promotion by comparing config().is_mobile across the call — which it
    // must do anyway, to REPORT it (B28 constraint 3: never flip the role silently) and to PERSIST it to NV.
    (void)role_enforce(_cfg);
    return true;
}

// Reprovision (join/create/leave verbs): the old network's learned state is stale -> wipe routes / id-bindings /
// deferred sends (per layer) + the node-global gateway schedules + multi-hop bridge map. The DAD listen window then
// re-learns the NEW network's neighbours; restart_discovery() (at id-adopt) drives the rebuild. NOT for the heal.
void Node::clear_routing_state() {
    clear_team_routing_state();              // §clean-team: the TEAM plane's half (inert stub on a !MR_FEAT_TEAM build)
    // §clean-join-carriers (2026-07-27): drop every carrier that could still EMIT — the channel buffer + flood states
    // this used to clear inline, AND the three it did not: _tx_queue / _pending_tx / _channel_reoffer_pending. That gap
    // was the LEAF-axis twin of the team leak: do_data_tx and both RTS builders stamp leaf_id from the LIVE _cfg at TX
    // time (node_mac.cpp:1239 / :621 / :781), so a staged channel M was re-broadcast onto the NEW leaf — and worse, a
    // staged/in-flight DM was re-stamped and sent to a `next` chosen from the _rt/_id_bind wiped just below, i.e.
    // MIS-DELIVERY. _channel_reoffer_pending was cleared by NOTHING at all (not here, not clear_learned_state).
    // Placed BEFORE the loop so the queue is already empty when the sweep's trailing become_free() runs — that is what
    // makes it a provable no-op here (it early-returns on an empty queue) instead of something that could re-enter the
    // pipeline halfway through the wipe. Order is otherwise immaterial: the reprovision axis keeps nothing either way.
    // Sweeps EVERY leaf (a gateway stages bridged DMs on both, and `leave` IS dispatched on the gateway build).
    purge_tx_carriers(PurgeAxis::reprovision);
    for (uint8_t i = 0; i < _n_layers; ++i) {
        _layers[i]._rt_count   = 0;          // routes
        _layers[i]._id_bind_n  = 0;          // id -> key bindings (old neighbours)
        // ★ THE SAME APP-FUTURE RULING as purge_tx_carriers' queue/flight sweeps, applied to the THIRD staged-DM
        // carrier (owner ruling 2026-07-27). A deferred send is an ORIGINATION waiting for a route; every other exit
        // from _deferred already completes the app's future — defer_send's redrain giveup and its cap refusal push
        // (node_cascade.cpp:245/251), and the TTL giveup in the drain (:285). Wiping the queue here was the ONE exit
        // that stayed silent, so a `join`/`leave` with parked sends hung exactly the futures this ruling is about.
        // Same reason code, same predicate (carrier_owes_send_failed, node_carriers.h) — a forwarder never defers
        // (node_join.cpp:490 drops a no-route transit DM outright), so the guard is defence-in-depth, not dead code.
        for (uint8_t d = 0; d < _layers[i]._deferred_n; ++d) {
            const TxItem& it = _layers[i]._deferred[d].item;
            // §CUSTODY-B §6.2(5) + [[B268]] blocker-1. `carrier_owes_send_failed` keeps its own narrow question
            // (channel-M / forwarded) — see its banner in node_carriers.h, which warns against overloading it —
            // and is passed IN as this site's `generic_owed`, unchanged. The trait decision and the grant's
            // replacement live in the shared helper.
            terminal_carrier_outcome(it.type, !it.is_forward,
                                     carrier_owes_send_failed(it.is_channel_m, it.is_forward),
                                     SendFailReason::reprovisioned, it.dst, it.ctr);
        }
        _layers[i]._deferred_n = 0;          // parked no-route sends
        _layers[i]._drain_armed = false;
        _layers[i]._mobile_reg_n = 0;        // §clean-join: the hosted-mobile registry is old-network state — the mobiles registered to us on the PREVIOUS network are void. UNGUARDED: _mobile_reg is compiled into every build (node.h:1285). Count-reset only (stale bytes never read past _mobile_reg_n), matching the codebase's count-based clear idiom.
        // §clean-join R4: the CHANNEL plane is old-network state too — buffered channel messages would flood into the NEW
        // network (a beacon digest advertises their ids + a pull re-broadcasts them, both stamped with the CURRENT leaf; a
        // shared-key network then ingests old content), and the per-origin anti-spam ledgers would mis-account recycled ids.
        // MOVED here from clear_learned_state (which calls us) so the reprovision verbs wipe it too; prep-restart unchanged.
        // ★ The two EMIT carriers this block used to clear inline — `_channel_buffer_n = 0` and the `_flood` loop — moved
        // into purge_tx_carriers(reprovision) above, so ONE place now defines what dropping a channel row means (and the
        // flood free finally cancels its rebroadcast timer). What stays here is the RECEIVE/SUPPRESS half: these three
        // never emit our content, so they are not carriers.
        _layers[i]._per_origin_channel.clear();                                             // per-origin anti-spam ledgers
        for (auto& p : _layers[i]._channel_pull_pending) p = ChannelPullPending{};          // digest/pull pending
        _layers[i]._channel_pull_recent_n = 0;
    }
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i) _gw_schedules[i].valid = false;
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)             _bridged_layers[i].valid = false;
#if MR_FEAT_MOBILE
    // §clean-join R2: drop OUR registration to a home too (this node may itself be a mobile). Push the deregistration
    // FIRST (active-guarded → registered:false shape, S2) so a `leave`/re-`join` doesn't leave the companion chip's
    // registration state stale forever; then reset. Plain struct reset — NOT mobile_reset_registration() (whose
    // re-DISCOVER/set_identity side-effects don't belong on a verb reprovision; the join's own re-DAD drives rediscovery).
    // ★ §MH-S4 §4.1 — GATED ON THE ATTACHMENT PLANE, NOT ON `active`, for the same symmetry reason as
    // `mobile_reset_registration`: with the `registered:true` push moved to roster confirmation, a `registered:false`
    // for an attachment the app was never told about would be an event with no counterpart.
    if (_mobile_attach_state == MobileAttachState::attached) { Push pu{}; pu.kind = PushKind::mobile_reg; enqueue_push(pu); }   // registered:false (relayed defaults false)
    _my_mobile_reg = MyMobileReg{};
    // A verb reprovision drops the attachment session with everything else it drops: both planes return to their
    // boot state and the volatile home-service request is cleared (the join's own re-DAD drives rediscovery).
    _mobile_attach_state      = MobileAttachState::dormant;
    _mobile_home_link         = MobileHomeLink::unknown;
    // ★★ §MH-S4b — RE-SEEDED FROM THE BOOT POLICY, **not** cleared, and that is BEHAVIOUR-PRESERVING at this site
    // rather than a new decision. §MH-S4 wrote `= false` here, which was harmless only because both predicates
    // still OR-ed in `_cfg.mobile_autoregister`; now that `_mobile_home_desired` IS the whole predicate (node.h),
    // a bare clear would leave an `autoregister=1` mobile PERMANENTLY unable to re-register after a `join`/`create`/
    // `leave` — an autonomy regression this line exists to not have. A verb reprovision is a fresh start on a new
    // network, so it re-applies the same rule `on_init` applies: the flag is the boot policy.
    // ⛔ It is NOT a general licence to re-read the flag: §4.2 forbids that mid-session (see node.h). Only the two
    //    fresh-start sites — `on_init` and this reprovision — evaluate it.
    _mobile_home_desired      = _cfg.mobile_autoregister;
    _mobile_home_confirmed_ms = 0;
    _presence_reg_confirmed   = false;
    _mobile_claim_retries     = 0;
#endif
}

// prep-restart (2026-06-24): the middle-tier reset — drop EVERY volatile/learned table to a fresh-but-PROVISIONED
// state, KEEPING _cfg (node_id/layer/sf_list/lineage/config_epoch), the crypto identity, and the DAD join.
// IN-PLACE clears (NOT `_layers[i] = LayerRuntime{}` — that ~20 KB stack temporary would overflow the device stack):
// reset the count/flag fields (stale array bytes are never read past the count), .clear() the maps, .reset() the
// optionals, default-assign the SMALL per-slot rings. Leaves the node operational (it re-learns on the next beacons).
void Node::clear_learned_state() {
    clear_routing_state();                                  // routes + id_bind + deferred + gw_schedules + bridged_layers
    for (uint8_t i = 0; i < _n_layers; ++i) {
        LayerRuntime& L = _layers[i];
        // data plane / in-flight (clear_routing_state already did _rt_count / _id_bind_n / _deferred_n / _drain_armed)
        L._tx_queue_n = 0; L._pending_tx.reset(); L._pending_rx.reset(); L._post_ack = PostAck{};
        L._rreq_seen_n = 0; L._rreq_last_n = 0;             // F route-discovery dedup
        L._peer_keys_n = 0; L._hash_query_seen_n = 0;       // peer-key cache (reloads /mrpeers on the power-cycle) + hash-locate dedup
        L._peer_liveness_n = 0;                             // liveness tiers
        for (auto& v : L._dest_seen_ms) v = 0;              // freshness map (256 × u64)
        for (auto& v : L._mobile_peer)  v = 0;              // mobile-peer set (32 B)
        // (§clean-join R4: the channel-plane cluster — buffer/per-origin ledger/pull-pending/pull-recent/flood — MOVED
        //  to clear_routing_state so the reprovision verbs wipe it too; clear_routing_state runs first here, so
        //  prep-restart still clears it.)
        L._peer_send_counter.clear();   // §B153: _last_acked_from is gone (see node.h)
        L._seen_origins.clear(); L._seen_origin_from.clear(); L._blind_until.clear();
        L._neighbor_budget_tier.clear(); L._neighbor_budget_tier_set_at.clear();
        L._per_sender_originator.clear();
        L._q_responded_n = 0;
        for (auto& s : L._sync_pending) s = SyncPending{};  // REQ_SYNC reply ring
        L._last_beacon_ms = 0; L._next_open_ms = 0;         // beacon/window timing
    }
    // node-global learned/pending tables beyond clear_routing_state
    for (uint8_t i = 0; i < protocol::cap_gateway_handoffs; ++i) _xl_handoffs[i].valid = false;
    _parked_sends_n = 0; _l2c_redirect_n = 0; _mediated_recent_n = 0;
    _ack_warn_until = 0; _last_dm_origin_ms = 0;
    _nack_wait_pending = false; _nack_wait_flight_gen = 0;
    // KEEP: _cfg, _node_id, _key_hash32, _x_secret/_ed_pub/_crypto_ready, _joined, _claim_epoch, channel_ctr (NV).
    //       The push ring is the app-notification channel (not learned topology) -> left intact.
}

// Re-enter discovery so a reprovisioned node aggressively rebuilds its table under the NEW (just-adopted) id: fast
// beacon cadence + the REQ_SYNC route-bootstrap pull. Mirrors on_init's discovery setup; called from join_adopt when
// a verb reprovision is pending (id is now stable). after() is replace-by-id, so re-arming kBeaconTimerId is safe.
void Node::restart_discovery() {
    const uint64_t now = _hal.now();
    _active->_discovery_started_ms   = now;
    _active->_discovery_mode         = (protocol::discovery_ms > 0);
    _active->_discovery_until_ms     = now + protocol::discovery_ms;
    _active->_discovery_bcn_rx_count = 0;
    _last_req_sync_tx_ms    = 0;             // clear the rate-limit -> the bootstrap REQ_SYNC can fire immediately
    if (_cfg.n_layers != 2)                  // single-layer: a fast-cadence beacon soon (replaces the slow pending re-arm)
        (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(protocol::discovery_beacon_period_ms))), kBeaconTimerId);
    if (_cfg.req_sync_on_boot)               // re-arm the REQ_SYNC bootstrap loop (pull neighbours' full tables)
        (void)_hal.after(protocol::req_sync_listen_ms, kReqSyncTimerId);
}

// ---- Slice 3c: dual-layer leaf activation -------------------------------------------------------------------
// SF_DEMOD_THRESHOLD[sf] + sf_margin (Lua dv:8386); the -240 out-of-range fallback is the literal (SF10), not table[12].
int16_t Node::routing_snr_floor_for(uint8_t routing_sf) const {
    const int16_t demod = (routing_sf >= 5 && routing_sf <= 12)
                          ? protocol::sf_demod_threshold_q4_table[routing_sf] : static_cast<int16_t>(-240);
    return static_cast<int16_t>(demod + protocol::sf_margin_q4);
}

// §4 busy-guard: NEVER switch leaves mid-exchange. An in-flight RTS/CTS/DATA/ACK (_pending_tx / _pending_rx), the
// post-ACK deliver/forward that straddles the ACK (_post_ack.pending — must finish on its own leaf, code-verified
// 2026-06-12), or a BUSY_RX same-hop re-RTS wait (_nack_wait_pending — a paused flight) must complete first. The
// scheduler (3d) re-arms the switch after gateway_layer_busy_retry_ms when this returns true.
bool Node::layer_swap_blocked() const {
    if (_active->_pending_tx.has_value() || _active->_pending_rx.has_value()
        || _active->_post_ack.pending || _nack_wait_pending) return true;
    // Node-GLOBAL transient stash holding a frame packed for the LEAVING leaf's SF/id (the id-classification's
    // guard_defer set): the LBT-deferred ring + the on-radio-busy / duty-defer re-issue stash. They clear in ms
    // (one LBT backoff), so deferring the swap until then is a bounded wait — and stops a wrong-leaf-SF re-TX.
    for (uint8_t s = 0; s < kLbtSlots;   ++s) if (_deferred_lbt[s].pending)         return true;
    // Gate on reissue_pending (a busy/duty re-issue timer is ARMED), NOT bare `valid`: a cleanly-sent CTS/ACK/NACK
    // leaves its stash `valid` (the buffer is only cleared by a newer same-tag TX or an on_radio_busy giveup), so a
    // bare-`valid` gate left a gateway's first ACK blocking the layer swap FOREVER (the bridged DM never transmits).
    for (uint8_t s = 0; s < kRetrySlots; ++s) if (_tx_stash[s].valid && _tx_stash[s].reissue_pending) return true;
    return false;
}

// Switch the active leaf. The window scheduler (3d) drives this on timers, GATED by layer_swap_blocked(). Steps:
//  (1) drain the LEAVING leaf's sync-response ring — its timer ids (kSyncResponseTimerId+slot) are SHARED across
//      leaves, so a stale fire would hit the wrong leaf / leak a slot. (The single-flight MAC timers are covered by
//      layer_swap_blocked() — none in flight here; the channel rings are gateway-skipped, Principle 11.)
//  (2) make the active-layer scalars + SF-derived timing (the Lua active_*) reflect leaf i;  (3) swap _active;
//  (4) retune the radio + the Hal short-id;  (5) re-seed the now-active leaf's own id_bind binding.
// §mobile 5a: a mobile adopts the host's PHY (single-leaf; NO layer swap). Sets the active-layer scalars the shared MAC
// reads + retunes the radio to (freq, SF, BW, CR). For the single-entry default (phy == layers[0]) this is a no-op -> 2b.
#if MR_FEAT_MOBILE
void Node::adopt_mobile_phy(const LayerConfig& phy, bool retune_radio) {
    _cfg.layers[0].layer_id = phy.layer_id; _cfg.layers[0].routing_sf = phy.routing_sf;
    _cfg.layers[0].allowed_sf_bitmap = phy.allowed_sf_bitmap; _cfg.layers[0].freq_mhz = phy.freq_mhz;
    _cfg.layers[0].bw_hz = phy.bw_hz; _cfg.layers[0].cr = phy.cr;
    _cfg.routing_sf        = phy.routing_sf;                      // active-layer scalars (the shared MAC reads these) — ADOPTED ALWAYS
    _cfg.allowed_sf_bitmap = phy.allowed_sf_bitmap;              // §mobile: the host's sf_list (so last-mile DATA-SF negotiation works)
    _cfg.leaf_id           = static_cast<uint8_t>(phy.layer_id & 0x0F);   // the byte-0 wire leaf filter — the host's leaf
    _routing_snr_floor_q4  = routing_snr_floor_for(phy.routing_sf);
    if (retune_radio) {                                          // §mobile: single-PHY is already tuned; retuning would arm a spurious blind window
        _hal.set_rx_sf(phy.routing_sf);
        // §layer-freq (2026-07-27) — NOT DONE HERE, deliberately. This is the SAME conditional-freq shape that
        // was a real bug in activate_layer (fixed below): BW/CR fall back to the global, freq does not, so an
        // adopt of a host PHY carrying freq_mhz==0 cannot reset the carrier a PREVIOUS adopt moved. It is not
        // the metal bug that was fixed — a mobile is single-leaf and never calls activate_layer, so there is no
        // window-swap that flips carriers underneath it — and converting it here would change mobile-plane
        // behaviour (s21/s22/s27 …), which is a different slice with a different gate. Left as-is on purpose;
        // the fix, when it comes, is `_hal.set_rx_freq(phy.freq_mhz > 0.0 ? phy.freq_mhz : _cfg.radio_freq_mhz)`.
        if (phy.freq_mhz > 0.0) _hal.set_rx_freq(phy.freq_mhz);
        _hal.set_rx_bw(phy.bw_hz ? phy.bw_hz : _cfg.radio_bw_hz);
        _hal.set_rx_cr(phy.cr ? phy.cr : _cfg.radio_cr);
    }
}
#endif

void Node::activate_layer(uint8_t i) {
    if (i >= _n_layers) return;                                  // defensive: n_layers==2 only; single-layer never swaps
    for (uint8_t s = 0; s < protocol::cap_sync_response_pending; ++s)
        if (_active->_sync_pending[s].active) { _hal.cancel(kSyncResponseTimerId + s); _active->_sync_pending[s].active = false; }
    // Re-home (LEAVE): cancel the leaving leaf's per-leaf queue/drain timers (shared wheel ids — the id-classification
    // rehome set). The STATE lives per-leaf in LayerRuntime (preserved); only the wheel slots re-home, on ENTER below.
    // (The periodic BEACON plane — kBeaconTimerId/kTriggeredBeaconTimerId/kBeaconJitterTimerId + kReqSyncTimerId — is a
    // PER-LEAF-TIMER-id job for Slice 3d: a cancel+rearm here would RESET its long cadence on every window.)
    _hal.cancel(kDeferredDrainTimerId);
    _hal.cancel(kQueueWakeupTimerId);
    _hal.cancel(kCascadeRequeueTimerId);

    const LayerConfig& L = _cfg.layers[i];
    _cfg.routing_sf        = L.routing_sf;                       // active-layer scalars the active-layer-shared MAC reads
    _cfg.allowed_sf_bitmap = L.allowed_sf_bitmap;
    _cfg.leaf_id           = static_cast<uint8_t>(L.layer_id & 0x0F);   // the byte-0 wire leaf filter
    _cfg.beacon_period_ms  = L.beacon_period_ms;
    _node_id               = L.node_id;                          // the leaf's own 8-bit address (static; GATEWAY per-leaf DAD deferred — single-layer node_id DAD is built)
    _routing_snr_floor_q4  = routing_snr_floor_for(L.routing_sf);
    _active = &_layers[i];                                       // THE SWAP — the MAC pump now operates on leaf i
    // ★ per-layer-bw: these SF-derived timings feed airtime_routing_ms() -> active_bw_hz()/active_cr(), which read the
    // ACTIVE leaf via _active — so they MUST be computed AFTER the swap. Else a mixed-BW gateway pairs the ENTERING
    // leaf's SF with the DEPARTING leaf's BW/CR (spec §7: airtime must compute at a swap-time-correct _active). Nothing
    // between the swap and here consumes them, so the move is side-effect-free.
    _lbt_backoff_ms        = (_cfg.lbt_backoff_ms > 0) ? _cfg.lbt_backoff_ms     // SF-derived timing for the new leaf
                             : (retry_jitter_ms() / 2 > 1 ? retry_jitter_ms() / 2 : 1);
    _flood_lbt_max_defer_ms = (_cfg.flood_lbt_max_defer_ms > 0) ? _cfg.flood_lbt_max_defer_ms
                              : airtime_routing_ms(protocol::beacon_max_bytes);
    _hal.set_rx_sf(L.routing_sf);                               // retune RX (SF latches in standby)
    // ★ ONE RULE for all three remaining PHY knobs: ALWAYS push the ACTIVE leaf's EFFECTIVE value (its
    // override OR the global) — NEVER `if (L.<knob> > 0)`. An inherit-leaf entered AFTER an override-leaf must
    // RESET the HAL back to the global, else RX *and* TX keep flying on the prior leaf's stale value
    // (charge != transmit).
    // §layer-freq (2026-07-27) — DONE: freq used to be the odd one out (`if (L.freq_mhz > 0.0)`), i.e. it had
    // exactly the defect shape the BW/CR lines' comment warns against. It could not be fixed before because
    // there was no NodeConfig::radio_freq_mhz to fall back to; there is now, and active_freq_mhz() mirrors
    // active_bw_hz()/active_cr() exactly. A 0.0 here means no carrier is configured ANYWHERE (no per-layer
    // override, no global) and is a documented Hal no-op — validate_gateway_layers refuses the only mixed
    // shape where a 0.0 could hide a stale carrier.
    // ⚠ HOW REACHABLE, measured (do NOT repeat the stronger claim in BASELINE 26u): a board provisioned via
    // `gateway`/`cfg set` never gets here with L.freq_mhz == 0, because fw_main's NV restore already
    // pre-resolves the inherit into BOTH layers. The inherit-with-a-global path is live in the SIMULATOR
    // (map_layers defaults freq_mhz to 0), in native tests, and for any future config path that leaves it 0.
    // MISSING (deliberate, out of this slice): the SAME conditional still stands in adopt_mobile_phy above
    // (`if (phy.freq_mhz > 0.0)`) and in node_mobile.cpp's scan retune — a mobile is single-leaf and never
    // calls activate_layer, so it cannot hit the leaf-swap bug, but an adopt of a host PHY that carries no
    // freq likewise cannot reset a previous adopt's carrier. Left for a mobile-plane slice, see the note there.
    _hal.set_rx_freq(active_freq_mhz());
    _hal.set_rx_bw(active_bw_hz());
    _hal.set_rx_cr(active_cr());
    _hal.set_protocol_id(L.node_id);                           // Hal short-id = the active leaf's node_id
    if (L.node_id != 0)                                          // seed leaf i's OWN id_bind binding (per-leaf table)
        id_bind_set(L.node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative);
    // Re-home (ENTER): re-derive the entering leaf's queue/drain drivers from its preserved LayerRuntime state.
    // Slice 4c.1: drain any cross-layer handoffs targeting THIS leaf into its tx_queue FIRST (now that _active is it),
    // so become_free() carries the bridged relay legs in this window. become_free() re-services the tx_queue (covers
    // the self-safe queue-wakeup / cascade-requeue ids); the deferred TTL-drain re-arms iff this leaf still has no-route
    // sends parked (1s period << a window — no cadence skew).
    drain_xl_handoffs_for_leaf(i);
    become_free();
    if (_active->_deferred_n > 0) { _active->_drain_armed = true; (void)_hal.after(protocol::send_defer_drain_period_ms, kDeferredDrainTimerId); }
}

// Slice 3d: the gateway WINDOW SCHEDULER (kLayerWindowTimerId). The two leaves' windows are anti-phase + back-to-back
// (§4): leaf 0 is active for window_ms[0], then leaf 1 for window_ms[1], summing to the period — so the "close" of one
// leaf IS the "open" of the other, ONE recurring switch event. Boot arms the first switch (after leaf-0's window);
// each fire alternates the active leaf + arms the next switch after the NOW-active leaf's window. BUSY-GUARD (Lua
// gate order): if mid-exchange, HOLD the current leaf + re-evaluate after gateway_layer_busy_retry_ms (the window
// slips, never yanks a flight) — NO separate close-retry, matching the Lua (the next switch re-checks).
void Node::window_switch_fire() {
    if (_n_layers != 2) return;                                    // gateways only (defensive; never armed single-layer)
    if (layer_swap_blocked()) {                                    // mid-exchange -> hold, re-evaluate after the busy-retry
        // The window slips to protect the in-flight exchange (Lua §4 "never yanks a flight"). With the ABSOLUTE grid
        // below, a slip does NOT ratchet the schedule: the next successful fire snaps to the grid's current leaf +
        // boundary, so the phase drift is bounded to <= one window (the slipped leaf simply loses that much of its
        // window). STARVATION (a leaf reliably busy at switch time) is still observable here; a max-hold is an open item.
        [[maybe_unused]] const uint8_t next = (_active == &_layers[0]) ? 1 : 0;   // telemetry-only (stripped on device) — the MR_EMIT below is its ONLY consumer
        MR_EMIT("gateway_layer_window_deferred", EF_I("held_leaf", (next == 0) ? 1 : 0), EF_I("next_leaf", next));
        (void)_hal.after(protocol::gateway_layer_busy_retry_ms, kLayerWindowTimerId);
        return;
    }
    // ABSOLUTE GRID (Slice 3d): the active leaf + the next switch are derived from the fixed epoch grid, NOT from a
    // running "now + window_ms" (which ratchets on every slip). So even after a busy slip the schedule snaps back.
    uint8_t target; uint32_t to_boundary;
    window_grid_now(&target, &to_boundary);
    if (&_layers[target] != _active) activate_layer(target);              // snap to the grid's current leaf (no-op if already there)
    set_window_anchors(target);                                          // Slice 3e: refresh the countdown anchors BEFORE the beacon advertises them
    maybe_emit_gateway_beacon();                                          // beacon the entering leaf iff its cadence is due
    (void)_hal.after(to_boundary, kLayerWindowTimerId);                   // arm to the ABSOLUTE grid boundary (no ratchet)
}

// Slice 3d GRID: which leaf the absolute grid says is active NOW, and the ms until that leaf's window closes (the next
// switch). Grid: leaf 0 owns [k·period, k·period+window0), leaf 1 owns [k·period+window0, (k+1)·period), anchored at
// _window_epoch_ms. validate_gateway_layers guarantees 0 < window0 < period, so to_boundary is always >= 1.
void Node::window_grid_now(uint8_t* active_leaf, uint32_t* ms_to_boundary) const {
    const uint32_t period = _cfg.layers[0].window_period_ms;
    const uint32_t w0     = _cfg.layers[0].window_ms;
    const uint32_t phase  = static_cast<uint32_t>((_hal.now() - _window_epoch_ms) % period);
    if (phase < w0) { *active_leaf = 0; *ms_to_boundary = w0 - phase; }
    else            { *active_leaf = 1; *ms_to_boundary = period - phase; }
}

// Slice 3e: refresh each leaf's _next_open_ms — the anchor for the receiver-anchored schedule countdown. The just-
// activated leaf re-opens in a FULL cycle (now+period; its countdown will %period back to 0 = "open now"); the other
// leaf opens when this leaf's window closes (now + this leaf's window_ms). Robust to busy-defer slip (re-derived each switch).
void Node::set_window_anchors(uint8_t active_leaf) {
    const uint64_t now    = _hal.now();
    const uint32_t period = _cfg.layers[0].window_period_ms;
    uint8_t gl; uint32_t to_boundary;
    window_grid_now(&gl, &to_boundary);                            // GRID: ms until the active leaf closes (= the other opens)
    _layers[active_leaf]._next_open_ms     = now + period;         // active is open now (countdown %period -> ~0); next full open +period
    _layers[1 - active_leaf]._next_open_ms = now + to_boundary;    // the other opens at the ABSOLUTE grid boundary (slip-robust)
}

// ---- Slice 3e.2: learned gateway schedules (RX consume + the sender-defer) ----------------------------------
const GatewaySchedule* Node::find_gw_schedule(uint8_t gw_node_id) const {
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i)
        if (_gw_schedules[i].valid && _gw_schedules[i].gw_node_id == gw_node_id) return &_gw_schedules[i];
    return nullptr;
}

void Node::store_gateway_schedule(const GatewaySchedule& gs) {
    uint8_t slot = 0xFF, oldest = 0;                               // refresh-by-id, else a free slot, else evict-oldest
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i) {
        if (_gw_schedules[i].valid && _gw_schedules[i].gw_node_id == gs.gw_node_id) { slot = i; break; }
        if (!_gw_schedules[i].valid && slot == 0xFF) slot = i;
        if (_gw_schedules[i].heard_ms < _gw_schedules[oldest].heard_ms) oldest = i;
    }
    _gw_schedules[(slot == 0xFF) ? oldest : slot] = gs;
}

// Multi-hop gateway discovery: record "gw_id bridges TO dest_leaf" (last-write-wins, one row per gw_id — Lua dv:4936).
// §6 OUT-OF-SCOPE (documented, not silently mis-routed): a SINGLE gateway bridging 3+ layers via PROPAGATION loses all
// but the last dest_leaf here (one row/gw_id). Direct neighbours still know every served leaf via _gw_schedules; full
// multi-bridge propagation belongs with the 3-layer work. Today's 2-layer gateways advertise exactly one other leaf.
void Node::ingest_bridged_layer(uint8_t gw_id, uint8_t dest_leaf) {
    uint8_t slot = 0xFF, oldest = 0;                               // refresh-by-id, else a free slot, else evict-oldest
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i) {
        if (_bridged_layers[i].valid && _bridged_layers[i].gw_id == gw_id) { slot = i; break; }
        if (!_bridged_layers[i].valid && slot == 0xFF) slot = i;
        if (_bridged_layers[i].last_seen_ms < _bridged_layers[oldest].last_seen_ms) oldest = i;
    }
    BridgedLayer& b = _bridged_layers[(slot == 0xFF) ? oldest : slot];
    b.valid = true; b.gw_id = gw_id; b.dest_leaf = dest_leaf; b.last_seen_ms = _hal.now();
}

void Node::prune_aged_bridged_layers(uint64_t now) {
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)
        if (_bridged_layers[i].valid && now > _bridged_layers[i].last_seen_ms + protocol::bridged_layers_ttl_ms)
            _bridged_layers[i].valid = false;
}

// PURE (no RNG draw): the base defer to land an RTS during the gateway's window on OUR leaf (Lua gateway_schedule_defer_ms
// dv:5013) + the capped herd-jitter range *out_jmax (the SEND path draws over it; the routes-dump reads base only). For
// each record: visit_start = heard_ms + offset (receiver-anchored, NO shared clock); phase = (now-visit_start) mod period.
// FOREIGN-leaf record OPEN -> gateway deaf to us, wait it out. OUR-leaf record AWAY -> wait for our window. Max defer; 0 = now.
uint32_t Node::gateway_schedule_base_defer_ms(uint8_t gw_node_id, uint32_t* out_jmax) const {
    if (out_jmax) *out_jmax = 0;
    const GatewaySchedule* s = find_gw_schedule(gw_node_id);
    if (!s || !s->valid || s->period_ms == 0) return 0;           // unknown / no schedule -> send now
    const uint64_t now     = _hal.now();
    const uint8_t  my_leaf = _cfg.leaf_id;
    // Adaptive guard (Lua dv:5029): a SPARSE herd (nibble 0 = herd-jitter inactive) biases the send deeper into the
    // window for settle-edge margin; a DENSE herd (nibble>0) keeps the base guard (the jitter already disperses it).
    uint32_t guard = protocol::gateway_schedule_guard_ms;
    if (s->spread_nibble == 0) guard += protocol::gateway_schedule_guard_sparse_bonus_ms;
    uint32_t best = 0, best_window = 0;                           // best_window = the window we'll be reachable in (jitter sizing)
    for (uint8_t i = 0; i < s->n_rec; ++i) {
        const GatewaySchedule::Rec& r = s->rec[i];
        const uint64_t visit_start = s->heard_ms + r.offset_ms;
        const int64_t  raw   = static_cast<int64_t>(now) - static_cast<int64_t>(visit_start);
        const uint32_t phase = static_cast<uint32_t>(((raw % s->period_ms) + s->period_ms) % s->period_ms);
        if (r.leaf_id != my_leaf) {                               // foreign visit currently open -> deaf to us, wait it out
            if (phase < r.window_ms) { const uint32_t d = r.window_ms - phase + guard; if (d > best) { best = d; best_window = s->period_ms - r.window_ms; } }
        } else {                                                  // our visit not open yet -> wait until it comes around
            if (phase >= r.window_ms) { const uint32_t d = s->period_ms - phase + guard; if (d > best) { best = d; best_window = r.window_ms; } }
        }
    }
    // Herd-jitter range (Lua dv:5072): spread our arrival over (nibble/15 × window) so the herd doesn't re-collide at
    // window-open. Two caps leave the window tail for the chosen sender's exchange + the gateway's own forwards: an
    // absolute couple-BARE-exchange headroom AND a fraction of the window. nibble 0 (sparse) -> no jitter. (The DRAW
    // itself happens in the non-const wrapper below — keeping THIS query pure so the routes-dump never mutates the RNG.)
    if (best > 0 && s->spread_nibble > 0 && best_window > 0 && out_jmax) {
        uint32_t jmax = static_cast<uint32_t>((static_cast<uint64_t>(s->spread_nibble) * best_window) / 15);
        const uint32_t headroom = 2u * exchange_airtime_ms();     // window-tail reserve = a couple BARE exchanges (NOT × slack)
        const uint32_t cap_frac = static_cast<uint32_t>((static_cast<uint64_t>(best_window) * protocol::gateway_herd_jitter_max_pct) / 100);
        uint32_t cap = (best_window > headroom) ? (best_window - headroom) : 0;
        if (cap_frac < cap) cap = cap_frac;
        if (jmax > cap) jmax = cap;
        *out_jmax = jmax;
    }
    return best;
}

// SEND path: base defer + a uniform herd-jitter draw over [0, jmax) (Lua dv:5083 `self:rand(0, jmax)` — half-open, NO +1,
// unlike the doorstep-retry sibling which adds +1). NON-const: it draws the shared RNG, so it must NOT be a const query.
uint32_t Node::gateway_schedule_defer_ms(uint8_t gw_node_id) {
    uint32_t jmax = 0;
    uint32_t best = gateway_schedule_base_defer_ms(gw_node_id, &jmax);
    if (jmax > 0) best += static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(jmax)));   // rand(0,jmax) — Lua half-open
    return best;
}

// Gateway-window broadcast sync (2026-06-20 side-task): bias the PERIODIC beacon to land at a gateway-neighbour's
// window-open, so the gateway actually HEARS the route advertisement the node was going to send anyway. The unicast
// path already does this (gateway_schedule_defer_ms); this is the broadcast half. CORE INVARIANT = ZERO extra beacons:
// the nominal cadence is preserved — we only push the fire time forward to the FIRST window-open at/after it (the added
// delay before the chosen window is < one window-period), then disperse within the window with the existing herd-jitter.
// Returns nominal_ms unchanged when: in discovery (protect same-layer latency), no gateway neighbour serves our active
// leaf, or the soonest such gw is already in-window now (defer==0 — spec §9.2: skip the bias). Multi-gateway: aligns to
// the SOONEST window on the active leaf; disjoint windows are covered across successive periods (cadence drift + jitter).
uint32_t Node::gateway_window_align_beacon(uint32_t nominal_ms) {
    if (in_discovery()) return nominal_ms;                            // discovery beacons stay fast (same-layer bootstrap)
    uint32_t best_defer = 0xFFFFFFFFu, best_jmax = 0, best_period = 0;
    for (uint8_t i = 0; i < protocol::cap_gateway_neighbor_schedules; ++i) {
        const GatewaySchedule& s = _gw_schedules[i];
        if (!s.valid || s.period_ms == 0) continue;
        bool serves_us = false;                                       // only align to a gw that visits OUR active leaf
        for (uint8_t k = 0; k < s.n_rec; ++k) if (s.rec[k].leaf_id == _cfg.leaf_id) { serves_us = true; break; }
        if (!serves_us) continue;
        uint32_t jmax = 0;
        const uint32_t defer = gateway_schedule_base_defer_ms(s.gw_node_id, &jmax);
        if (defer == 0) continue;                                     // in-window now (or no schedule) -> no bias for this gw
        if (defer < best_defer) { best_defer = defer; best_jmax = jmax; best_period = s.period_ms; }
    }
    if (best_period == 0) return nominal_ms;                          // no alignable gateway neighbour -> plain cadence
    uint32_t target = best_defer;                                     // reachable windows recur at best_defer + k*period
    if (target < nominal_ms) {                                        // step to the first window-open AT/AFTER the cadence instant
        const uint32_t gap = nominal_ms - target;
        const uint32_t k   = (gap + best_period - 1) / best_period;   // ceil -> added delay < one window-period
        target += k * best_period;
    }
    if (best_jmax > 0) target += static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(best_jmax)));  // herd-jitter WITHIN the window
    return target;
}

// §3e herd sizing (Lua count_direct_neighbors dv:1677): rt entries whose PRIMARY candidate is a 1-hop neighbour.
uint8_t Node::count_direct_neighbors() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _active->_rt_count; ++i)
        if (_active->_rt[i].candidates[0].hops == 1) ++n;
    return n;
}

// §3e: a single RTS+CTS+DATA+ACK exchange's airtime, computed from airtime_ms (improves on the Lua's fixed 600ms).
// RTS/CTS/ACK fly on the routing SF (lengths 8/4/4, Lua timing); DATA flies on the most-robust data SF (max_data_sf)
// and includes the cts_to_data_gap (the SF-retune delay between CTS and DATA). DATA payload = the rolling mean of what
// we pass (_dm_payload_mean), bootstrapped to gateway_herd_assumed_payload_bytes until the first DATA sample lands.
//
// ✖ KNOWN UNDER-ESTIMATE, DELIBERATELY LEFT — [[B158]] site 2, MEASURED 2026-08-08 (§B158-EXCHANGE-ARM):
// the RTS term below prices a PHANTOM 8-byte RTS against a live 10-B plaintext / 11-B crypted unicast wire
// (§HYBRID-RTS-S1). It is NOT fixed here, and the reason is measurement, not oversight:
//   · Only ONE consumer can ever act on it — the herd-jitter cap in gateway_schedule_base_defer_ms(), which is
//     jmax = best_window - 2*exchange_airtime_ms() whenever that binds before gateway_herd_jitter_max_pct.
//     The OTHER consumer, gateway_spread_nibble() below, is SATURATED at 15 in every corpus scenario that has a
//     herd at all (measured off the beacon wire), so a larger estimate cannot move it.
//   · ⇒ the sign is INVERTED from intuition: a LARGER (more accurate) estimate SHRINKS the jitter cap and packs
//     the herd MORE tightly, it does not spread it wider.
//   · 35 of the 36 corpus scenarios are BYTE-IDENTICAL for every RTS length from 7 to 16; the sole sensitive row
//     is s16_dense_gateway, and there the response is CHAOTIC and NON-MONOTONE (deliveries 56/56/60/56/70/55 at
//     lengths 7/8/10/11/13/15). No pricing is attributable, so none was adopted (C2's sibling: do not "fix" a
//     constant when the measurement cannot tell the fix from the noise).
// ⇒ if this is ever retuned, it must be retuned as a herd-spread DESIGN decision with s16 as the instrument, not
// as a byte-count correction. Full evidence: simulation/BASELINE.md §B158-EXCHANGE-ARM.
uint32_t Node::exchange_airtime_ms() const {
    const uint8_t  dsf     = max_data_sf() ? max_data_sf() : _cfg.routing_sf;   // no data SF -> routing as a fallback
    const uint16_t payload = _dm_payload_mean ? _dm_payload_mean : protocol::gateway_herd_assumed_payload_bytes;
    const uint32_t data_air = airtime_ms(dsf, active_bw_hz(), active_cr(), protocol::preamble_sym,
                                         static_cast<uint16_t>(DATA_HDR_LEN + payload));
    return airtime_routing_ms(8) + airtime_routing_ms(4)        // RTS + CTS (routing SF)
         + protocol::cts_to_data_gap_ms                         // CTS->DATA SF-retune gap
         + data_air                                             // DATA (data SF)
         + airtime_routing_ms(4);                               // ACK (routing SF)
}

// §3e (Lua gateway_spread_nibble dv:1692): this gateway's 0..15 herd-spread hint. frac = herd·exchange / window, capped
// to 1; nibble = round(frac·15). A herd < gateway_herd_min advertises 0 (≤2 has nothing to de-conflict). window = the
// ACTIVE leaf's window (the contention window the herd shares on this leaf). exchange = the computed per-exchange airtime.
uint8_t Node::gateway_spread_nibble() const {
    if (_cfg.n_layers != 2) return 0;                            // only a gateway advertises a schedule/spread
    const uint32_t window = _cfg.layers[static_cast<size_t>(_active - &_layers[0])].window_ms;
    const uint8_t  herd   = count_direct_neighbors();
    if (window == 0 || herd < protocol::gateway_herd_min) return 0;
    const uint32_t slack  = _cfg.gw_herd_slack ? _cfg.gw_herd_slack : 1;       // 0 -> 1 (no negative/zero spread)
    uint64_t frac_num = static_cast<uint64_t>(herd) * exchange_airtime_ms() * slack;   // frac = num/window, capped to 1
    if (frac_num > window) frac_num = window;
    const uint32_t nib = static_cast<uint32_t>((frac_num * 15 + window / 2) / window);       // round(frac·15)
    return static_cast<uint8_t>(nib > 15 ? 15 : nib);
}

// Slice 3d: per-leaf beacon — at WINDOW-ACTIVATION, beacon the now-active leaf iff its OWN cadence is due. The window
// schedule provides the emit opportunity (correct SF + the leaf is idle post-swap, so emit_beacon won't busy-skip); the
// per-leaf _last_beacon_ms gates it to the period (visit-quantized: at the first visit past due, ±one window). Replaces
// the shared kBeaconTimerId for gateways. Also drives the gateway's discovery-exit (no shared timer calls it otherwise).
void Node::maybe_emit_gateway_beacon() {
    maybe_exit_discovery("gateway_window");
    const uint64_t now = _hal.now();
    // A gateway is REACTIVE-ONLY in steady state — re-announcing its STATIC schedule on a timer is pure airtime waste
    // (a neighbour computes every future window from one hearing; cold neighbours pull via REQ_SYNC), and that waste
    // kills the duty budget the gateway needs for bridging. Three emit triggers, highest-priority first:
    bool dirty = false;
    for (uint8_t i = 0; i < _active->_rt_count; ++i) if (_active->_rt[i].dirty) { dirty = true; break; }

    bool emit = false;
    if (dirty) {
        emit = true;                                                      // (1) reactive: real NEW info to propagate now
    } else if (in_discovery()) {
        // (2) NEW-gateway announcement on the fast discovery cadence — a fresh two-layer link-up MUST be discoverable.
        if (now - _active->_last_beacon_ms >= protocol::discovery_beacon_period_ms) emit = true;
    } else if (_cfg.gw_schedule_readvert_ms != 0 && now - _active->_last_beacon_ms >= _cfg.gw_schedule_readvert_ms
               && gateway_announce_has_headroom()) {
        // (3) Wave-4 antidote: periodic SCHEDULE re-advertisement every gw_schedule_readvert_ms (duty-gated). The
        // "compute every future window from one hearing" premise (below) FAILS when that one hearing was a
        // boundary-degenerate snapshot (a discovery/dirty beacon emitted mid-window advertises collapsed anti-phase
        // offsets) OR simply went stale — the sender then phase-locks into a never-opening window and re-defers forever
        // (the s15 cross-layer livelock). A gateway beacon fires at WINDOW-ACTIVATION, so set_window_anchors has just
        // stamped ACCURATE offsets → the re-advert reliably re-anchors every listening neighbour (RX is always on,
        // unlike the phase-gated TX). Bounded to gw_schedule_readvert_ms + duty headroom = a handful of beacons/run.
        emit = true;
    } else if (now - _active->_last_beacon_ms >= _cfg.gw_announce_min_interval_ms
               && gateway_announce_has_headroom()) {
        emit = true;                                                      // (4) slow safety-net heartbeat: gated on duty headroom
    }
    if (emit) {
        emit_beacon("gateway_window");                                    // also guarded (busy / critical-budget skip)
        _active->_last_beacon_ms = now;
    }
}

// True when the rolling airtime usage is below gw_announce_duty_pct % of the duty budget — i.e. there is enough
// headroom to spend a beacon on an unsolicited heartbeat. Duty disabled (budget 0) => unconstrained => always true.
bool Node::gateway_announce_has_headroom() const {
    if (_duty_cycle_budget_ms == 0) return true;
    const uint64_t used = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);
    const uint64_t cap  = (_duty_cycle_budget_ms * _cfg.gw_announce_duty_pct) / 100;
    return used < cap;
}

// §3-B.5 — the Node-side half of jittered_tx_stash.h, shared by all three de-storm stashes (§F-XL-1
// H-forward ring, §F-XL-2 RREQ-forward ring, §S6/QA-3b mobile-OFFER slot). Every stashed frame is
// self-contained (its leaf_id / team scope is packed in), so it tx's at routing_sf as a flood regardless
// of the currently-active layer. `len` is the slot's "armed" flag — cleared AFTER the tx, as every hand-
// rolled copy did, so a re-entrant fire on the same slot is a no-op rather than a duplicate transmission.
// §MH-S1 §6.2: it RETURNS the transmitter admission now (see the node.h contract). `len == 0` (nothing
// armed / already fired) is `false` too — there is no frame, so there is nothing to call admitted; the one
// caller that reads the answer checks `len` first so it can never confuse the two.
// §MH-S1b: `kind` selects the LBT kind (default `flood` = the two §F-XL rings, unchanged). The mobile-OFFER
// slot passes `mobile_offer` so the frame is attributable at BOTH ends of a defer — `lbt_complete` raises the
// honest `mobile_offer_tx` on handoff, and node.cpp's deferred-loss arm raises `mobile_offer_dropped` on a
// late HAL refusal instead of only the generic `tx_deferred_lost`.
bool Node::jtx_fire(uint8_t* buf, uint8_t& len, LbtKind kind, TxAdmission* out) {
    if (len == 0) return false;
    // [[B142]]: `completion_gen` = 0 — the two §F-XL flood rings and the mobile-OFFER slot carry NO transaction
    // identity and are never staleness-tested (node.h's contract). Giving the OFFER slot one is [[B137]]/S2's.
    const bool admitted = tx_initiating(buf, len, static_cast<int16_t>(_cfg.routing_sf), kind, 0, out);
    len = 0;
    return admitted;
}

// ---- dispatch (timer ids -> subsystem handlers; RX cmd-nibble -> handlers) --

void Node::on_timer(uint32_t timer_id) {
    switch (timer_id) {
    case kBeaconTimerId: {
        periodic_beacon_fire();       // R4.3 throttle body (may emit now, skip, or defer to kBeaconJitterTimerId)
        maybe_exit_discovery("timer");// UNCONDITIONAL before the re-arm (dv:7858) so the period reflects the state
        // Re-arm ±20% jitter [0.8P, 1.2P] inclusive (dv_dual_sf.lua:7858-7864).
        // Period reflects the (possibly just-exited) discovery state. Integer
        // floor division; +1 makes hi inclusive (rand_range is [lo,hi)).
        const uint32_t P  = in_discovery() ? protocol::discovery_beacon_period_ms
                                           : steady_beacon_period_ms();
        const int      lo = static_cast<int>(P * 4 / 5);
        const int      hi = static_cast<int>(P * 6 / 5);
        const uint32_t nominal = static_cast<uint32_t>(_hal.rand_range(lo, hi + 1));
        // Gateway-window broadcast sync: nudge this already-scheduled periodic beacon to a gateway-neighbour's
        // window-open so the gateway hears it (ZERO extra beacons — cadence/count preserved; see the helper). A
        // no-op (returns nominal) when there's no gateway neighbour / in discovery — i.e. the entire existing suite.
        (void)_hal.after(gateway_window_align_beacon(nominal), kBeaconTimerId);
        break;
    }
    case kAgingTimerId:
        age_out_stale_routes();
        id_bind_age_out();            // hash-locate A0: drop expired bindings on the same periodic sweep
        mobile_home_age_out();        // §mobile 3c: TTL-drop the sender-side mobile_hash->home cache on the same sweep
        mobile_reg_age_out();         // ★ §MH-S5 §9.1/§9.2: physically expire HOSTED-mobile rows (direct AND redirect) at mobile_liveness_ms — the deadline scan §9.3 requires "from the normal aging timer". ⛔ NO new timer id (kCap stays 91)
        age_out_parked_sends();       // hash-locate D: give up on DMs whose hash never resolved
        age_out_pending_id_pubkey();  // §id-hash S4b (spec §5 step 5): give up on by-id reqpubkeys whose id never resolved
        age_out_denied_ids();         // node_id DAD: a denied slot becomes reusable after dad_denied_id_ttl_ms
        age_out_mediated();           // L2a: drop mediation-suppression records past the window
        age_out_rreq_last();          // F route-discovery: free spent per-dst RREQ rate-limit slots (BOTH planes) — the table is refuse-when-full, so with no age-out `cap` distinct dsts kill discovery for good
        (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
        break;
    case kTriggeredBeaconTimerId:
        _triggered_beacon_pending = false;   // clear BEFORE emit so a re-trigger can re-arm
        emit_beacon("triggered");
        break;
    // ---- R3 data-plane timers ----
    case kRtsTimeoutTimerId:      rts_timeout_fire();      break;
    case kAckTimeoutTimerId:      ack_timeout_fire();      break;
    case kPendingRxExpiryTimerId: pending_rx_expiry_fire();break;
    case kCtsToDataGapTimerId:    do_data_tx();            break;
    case kQueueWakeupTimerId:     become_free();           break;
    case kPostAckTimerId:         do_post_ack();           break;
    case kRetryBackoffTimerId:    tx_rts_retry();          break;
    case kDeferredDrainTimerId:   try_drain_deferred();    break;   // periodic no-route drain / TTL giveup
    case kParkRefloodTimerId:     park_reflood_fire();     break;   // §F-SL-1: bounded jittered H re-flood for still-parked sends
    case kE2eAckDeadlineTimerId:  e2e_ack_deadline_fire(); break;   // shelf item (i): -a sends whose DATA_TYPE_E2E_ACK never returned -> send_failed{e2e_ack_timeout}
    case kReqSyncTimerId:         req_sync_loop_fire();    break;   // REQ_SYNC boot loop: send + re-arm while starved
#if MR_FEAT_MOBILE
    case kMobileDiscoverTimerId:  mobile_discover_fire();  break;   // §mobile 2b: registration FSM (armed only for a mobile)
    case kMobileClaimGuardTimerId: mobile_claim_guard_fire(); break;
    case kMobileLayerQueryTimerId: mobile_layer_query_fire(); break;   // §mobile 5a: pull the layer directory from a gateway
    case kPresenceProbeTimerId:   presence_probe_fire();  break;   // §S6: mobile presence check/probe/retry (REPLACES the re-CLAIM tick)
#endif
    case kPresenceRosterTimerId:  presence_roster_fire(); break;   // §S6: home coalesced-roster emit (always compiled — a home is a static)
    case kMobileOfferBackoffTimerId:                              // §S6/QA-3b + ★ §MH-S2 §5.3.3: the pending-OFFER ring's DEADLINE SCAN
        // ★★ §MH-S2 — ONE TIMER, MANY ENTRIES. This was a single-slot fire; it is now a scan that expires elapsed
        // [[B137]] id reservations, transmits AT MOST ONE due OFFER and re-arms for the next earliest deadline. The
        // whole body (including the §B132b eligibility re-check at the transmission boundary and the §MH-S1 §6.2
        // admission-result handling) lives in `mobile_offer_fire` (node_join.cpp), beside the ring it serves —
        // ⛔ `kCap` is UNCHANGED at 91: no timer id was allocated for this, by design.
        mobile_offer_fire();
        break;
    case kTeamDadGuardTimerId:     team_dad_guard_fire();     break;   // §mobile 6.4: team-DAD guard window close -> confirm _team_local_id
    case kMBcastClearTimerId:                                       // M-broadcast fire-and-forget: clear the flight (no ACK)
        if (_active->_pending_tx && _active->_pending_tx->m_broadcast) { _active->_pending_tx.reset(); become_free(); }
        break;
    case kOverhearRetuneTimerId:                                            // overhear ARM: retune RX back to routing_sf
        _hal.set_rx_sf(_cfg.routing_sf);
        // §4.4: a FLOOD flood-state still awaiting its DATA-M (caught the RTS-M, missed the body) -> fast-self-pull
        // from its src now. Single-radio + SF-gating normally means at most ONE awaiting_data state (§4.2), but
        // resolve ALL of them — never strand an awaiting_data slot if that invariant is ever broken (2nd radio /
        // a retune-logic change), which would otherwise leak the slot until reboot. (Defense-in-depth.)
        for (uint8_t i = 0; i < protocol::cap_flood_pending; ++i)
            if (_active->_flood[i].active && _active->_flood[i].awaiting_data) {
                if (_active->_flood[i].team_flood) flood_state_free(i);   // §mobile 6.3: never fast-pull a TEAM flood (team unknown until the DATA-M) -> no CHANNEL_PULL for a possibly-foreign team. §F-CH-RELAY: but DON'T strand the slot either — free it so a HOLDER re-offer's fresh RTS-M (~10 s later) re-allocates a state + re-arms the overhear window to catch the re-injected DATA-M (a stranded awaiting_data slot would dup-merge the re-offer RTS-M and never retune, defeating the repair). Pre-F-CH-RELAY this slot leaked until reboot.
                else flood_fast_self_pull(i);                            // non-team: the weak-link fast-self-pull (§4.4)
            }
        break;
    case kJoinClaimGuardTimerId:  join_claim_guard_fire();         break;   // node_id DAD: guard elapsed -> adopt-or-deny
    case kJoinRetryTimerId:       join_start_claim("retry");       break;   // node_id DAD: re-claim after a lost claim/heal
    case kJoinListenTimerId:      _join_listen_pending = false; join_start_claim("listen_done"); break;   // L1: listen window done -> claim
    case kCascadeRequeueTimerId:  become_free();           break;   // backoff elapsed -> drain the requeued flight
    case kRtsDutyDeferTimerId:    rts_duty_defer_fire();   break;   // #A redo: over-budget RTS duty-defer re-check/hand
    case kNackWaitTimerId:                                          // BUSY_RX wait elapsed -> re-RTS SAME hop
        if (_nack_wait_pending) {
            _nack_wait_pending = false;
            if (_active->_pending_tx && _active->_pending_tx->flight_gen == _nack_wait_flight_gen) tx_rts_retry();   // L9: exact flight match (was the 4-bit ctr_lo)
        }
        break;
    case kLayerWindowTimerId:     window_switch_fire();    break;   // Slice 3d: gateway window scheduler — alternate the active leaf
    default:
        // R4.5 LBT deferred-TX slots occupy the id range [kLbtDeferTimerId, +kLbtSlots) — each fires its own slot.
        if (timer_id >= kLbtDeferTimerId && timer_id < kLbtDeferTimerId + kLbtSlots) {
            DeferredLbt& d = _deferred_lbt[timer_id - kLbtDeferTimerId];
            if (d.pending) { d.pending = false;
                // ★★★ §id-hash S1d (2026-08-01) — THE DEFERRED RESIDUAL, and the binding requirement is NO SILENT
                // LOSS. `tx_initiating` answered "accepted" when this frame entered the defer ring, and the
                // synchronous ack has long since reached the operator. If the HAL queue has filled during the wait,
                // the frame dies HERE — and unlike a DATA frame there is no MAC timeout behind an H query to
                // recover it, so without this the operator waits forever on an answer that can never come.
                // ⇒ REPORTED, LOUDLY AND ON THE DEVICE. ⚠ CORRECTED 2026-08-01 (QA P2): the first version of this
                //   said `_hal.log` "reaches the console on metal" — IT DID NOT. `fw_main`'s sink gated every log
                //   line on `g_mr_trace_on`, so under normal `debug off` operation this report was completely
                //   silent on hardware and the no-silent-loss claim was FALSE. The `!!` prefix marks it
                //   operator-critical and `fw_main`'s sink now prints those regardless of trace (see it there).
                //   The MR_EMIT beside it is telemetry only — device-stripped — and exists for the simulator.
                // ⚠ A BOUNDED RE-DEFER WAS CONSIDERED AND REFUSED, and the reason is specific to what this carries:
                //   re-deferring would put an H query on the air at an unbounded later time, after the operator has
                //   been told and has plausibly retried by hand — duplicate airtime for a question that is already
                //   stale. It also needs retry state in the ring plus a second timer path, for a case no automated
                //   gate can reach. Reporting the death is the honest product; re-sending a stale query is not.
                // ⓘ NOT a per-command push: correlating this back to the `reqpubkey` that queued it needs a handle
                //   this frame does not carry, and a `send_failed{ctr:0}` is exactly the uncorrelated shape B39
                //   exists to fix. Owed to B39's discriminated result (C1) — recorded, not faked here.
                // §TX3: for a beacon, `lbt_complete` true ⇔ DeviceHal answered ok (a slot<0 frame cannot duty-defer),
                // which IS the owner's admission boundary — so THIS is where a deferred beacon's digest commits.
                // A rejection leaves the ad_count and the dirty flag untouched.
                const bool admitted = lbt_complete(d.buf, d.len, d.sf, static_cast<LbtKind>(d.kind), d.completion_gen);
                if (admitted) commit_channel_digest_advertised(d.digest_ids, kDeferDigestIds);
                if (!admitted) {
                    MR_EMIT("tx_deferred_lost", EF_I("kind", d.kind), EF_I("len", d.len));
                    _hal.log("!! deferred TX dropped at the radio queue — a request reported as accepted never aired");
#if MR_FEAT_MOBILE
                    // ★ §MH-S1 §6.1 — THE DEFERRED DISCOVER'S OWN RESIDUAL. A DISCOVER accepted into this ring
                    // reported `true` to `mobile_discover_fire`, so nothing was scheduled for it; if the HAL has
                    // filled up during the wait it dies right here, `lbt_complete` never arms the guard, and the
                    // mobile would sit with NO window, NO retry and NO recorded reason. Gate 6 requires a bounded
                    // retry for a rejected DISCOVER on EVERY path, not just the synchronous one.
                    if (static_cast<LbtKind>(d.kind) == LbtKind::mobile_discover)
                        mobile_admission_rejected(TxAdmission::tx_rejected, "discover_deferred");
                    // ★★ §MH-S1b §6.3 — AND THE DEFERRED CLAIM'S, WHICH ROUND 1 MISSED AND QA FOUND. This arm
                    // recognised ONLY `mobile_discover`, so a CLAIM that died here was anonymous — while the
                    // mobile had ALREADY adopted on `tx_initiating`'s `true` (the defer path returns true), i.e.
                    // it sat FALSELY REGISTERED at a home that was never sent anything, with no retry. The adopt
                    // now happens in `lbt_complete` (which is NOT reached on this branch), so reaching here means
                    // no registration was created; all that is owed is the same bounded local retry as DISCOVER.
                    // ⚠ Clear the stage too: without it a later `mobile_claim_adopt` could still consume it.
                    // ★★★ [[B142]] 2026-08-07 — AND THAT CLEAR IS ONLY SAFE BECAUSE THIS ARM IS NOW
                    // TRANSACTION-SCOPED (the same holds for the `mobile_discover` arm directly above, whose
                    // `mobile_admission_rejected` would otherwise report a superseded attempt's death as the
                    // CURRENT one's). A STALE deferred DISCOVER/CLAIM never reaches here at all: `lbt_complete`
                    // cancels it on `completion_gen != _mobile_attach_gen` and answers TRUE, so `admitted` is
                    // true and this whole block is skipped. Before the token, a stale slot firing after the
                    // operator re-registered ran these two lines against the CURRENT transaction — clearing a
                    // stage it did not own and arming a retry for an attempt that no longer existed, which is
                    // why "the rejected path is as destructive as the accepted one" is the defect's shape.
                    // ⇒ reaching here means the CLAIM was OURS and really died: clearing our own stage is right.
                    // ★★★★ §MH-S4b — AND A **RE-CLAIM** THAT DIES HERE IS A DIFFERENT FRAME WITH A DIFFERENT OWED
                    // ACTION, so it is identified BEFORE either branch acts ([[B147]]'s ordering rule). §7.1's
                    // re-CLAIM is sent by an already provisionally-attached `claiming` node; it counted against the
                    // bounded budget the moment `tx_initiating` answered true for the defer (which is correct — a
                    // deferred frame IS in flight), and now it turns out it never reached the air ⇒ **REFUND**.
                    // ⛔ It must NOT reach `mobile_admission_rejected`: that is the PRE-attachment FSM's backoff +
                    //    retry-DISCOVER, and a `claiming` node already has a home, a local id and an armed
                    //    confirmation deadline that will try again. Routing it there would throw the attachment away
                    //    for a local radio hiccup — [[B139]]'s defect, one plane over.
                    // ★ §B186a 2026-08-12 — BOTH claim kinds are named, so this arm behaves EXACTLY as it did: a
                    // re-CLAIM used to arrive here as `mobile_claim` and omitting the new kind would silently stop
                    // routing re-CLAIM deaths into the refund. ⓘ THE ORDER IS UNCHANGED AND SO IS THE DECISION:
                    // `mobile_reclaim_deferred_rejected()` still identifies the re-CLAIM from FSM state
                    // (`_mobile_claim_pending` / `active` / `claiming`). ⛔ NOT rewired to `d.kind` in this slice —
                    // that would change a BEHAVIOURAL decision, which is [[B186b]]'s, not observability's. The
                    // captured kind is now available here for that work, and the two agreeing is testable.
                    if ((static_cast<LbtKind>(d.kind) == LbtKind::mobile_claim
                         || static_cast<LbtKind>(d.kind) == LbtKind::mobile_reclaim)
                        && !mobile_reclaim_deferred_rejected()) {
                        _mobile_claim_pending = false;
                        mobile_admission_rejected(TxAdmission::tx_rejected, "claim_deferred");
                    }
#endif
                    // ★ §MH-S1b §6.2 — the HOST half. A staged OFFER accepted into this ring and then refused by
                    // the HAL was likewise anonymous; `mobile_offer` makes it attributable, so the source mobile
                    // sees an explicit `mobile_offer_dropped` (its own retry is the backstop) rather than only
                    // the generic line above. NOT `#if MR_FEAT_MOBILE`: a home is a STATIC node.
                    if (static_cast<LbtKind>(d.kind) == LbtKind::mobile_offer)
                        mobile_offer_admission_rejected(TxAdmission::tx_rejected);
                } }
        } else if (timer_id >= kRadioBusyRetryTimerId && timer_id < kRadioBusyRetryTimerId + kRetrySlots) {
            retry_stashed(static_cast<uint8_t>(timer_id - kRadioBusyRetryTimerId));   // R4.5b stash re-issue
        } else if (timer_id >= kDutyDeferTimerId && timer_id < kDutyDeferTimerId + kRetrySlots) {
            duty_defer_fire(static_cast<uint8_t>(timer_id - kDutyDeferTimerId));      // #2 duty-defer re-run
        } else if (timer_id >= kBeaconJitterTimerId && timer_id < kBeaconJitterTimerId + kBeaconJitterSlots) {
            deferred_beacon_jitter_fire(static_cast<uint8_t>(timer_id - kBeaconJitterTimerId));   // #D ring slot
        } else if (timer_id >= kSyncResponseTimerId && timer_id < kSyncResponseTimerId + kSyncRespSlots) {
            sync_response_fire(static_cast<uint8_t>(timer_id - kSyncResponseTimerId));            // REQ_SYNC jittered reply ring slot
        } else if (timer_id >= kChannelPullTimerId && timer_id < kChannelPullTimerId + kChannelPullSlots) {
            channel_pull_fire(static_cast<uint8_t>(timer_id - kChannelPullTimerId));             // channel CHANNEL_PULL jittered fire
        } else if (timer_id >= kFloodRebcastTimerId && timer_id < kFloodRebcastTimerId + protocol::cap_flood_pending) {
            flood_rebroadcast_fire(static_cast<uint8_t>(timer_id - kFloodRebcastTimerId));       // channel FLOOD rebroadcast ring slot
        } else if (timer_id >= kChannelReofferTimerId && timer_id < kChannelReofferTimerId + kChannelReofferSlots) {
            channel_reoffer_fire(static_cast<uint8_t>(timer_id - kChannelReofferTimerId));       // Part 2: channel origin re-offer ring slot
        } else if (timer_id >= kHForwardTimerId && timer_id < kHForwardTimerId + kHForwardSlots) {
            h_forward_fire(static_cast<uint8_t>(timer_id - kHForwardTimerId));                    // §F-XL-1: jittered h_forward de-storm ring slot
        } else if (timer_id >= kRreqForwardTimerId && timer_id < kRreqForwardTimerId + kRreqForwardSlots) {
            rreq_forward_fire(static_cast<uint8_t>(timer_id - kRreqForwardTimerId));              // §F-XL-2: jittered rreq_forward de-storm ring slot
        }
        break;
    }
}

void Node::on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    if (len < 1) return;
    // R4.3 channel-busy witness: ANY successful decode means the channel was busy now (broadcast OR
    // unicast, beacon OR data) — the throttle reads this to suppress the next beacon (dv:9164). No rand.
    _last_rx_routing_sf_ms = _hal.now();
    switch (wire::cmd_of(bytes[0])) {
        case wire::Cmd::B: ingest_beacon(bytes, len, meta); break;   // R1/R2 beacon (+max-idle witness set INSIDE, post-guards)
        case wire::Cmd::R: handle_rts (bytes, len, meta); break;     // R3 RTS  -> CTS
        case wire::Cmd::C: handle_cts (bytes, len, meta); break;     // R3 CTS  -> DATA
        case wire::Cmd::D: handle_data(bytes, len, meta); break;     // R3 DATA -> deliver/forward + ACK
        case wire::Cmd::K: handle_ack (bytes, len, meta); break;     // R3 ACK  -> done
        case wire::Cmd::N: handle_nack(bytes, len, meta); break;     // NACK -> blind+wait / cascade
        case wire::Cmd::F: handle_f  (bytes, len, meta); break;     // F route-find RREQ/RREP flood
        case wire::Cmd::Q: handle_q  (bytes, len, meta); break;     // Q REQ_SYNC route-bootstrap (-> jittered sync beacon)
        case wire::Cmd::H: handle_h  (bytes, len, meta); break;     // H hash-locate flood (key_hash32 -> node_id)
        case wire::Cmd::J: handle_j  (bytes, len, meta); break;     // J node_id DAD (CLAIM/DENY -> claim/heal)
        case wire::Cmd::M: handle_channel_data(bytes, len, meta); break;  // M lean channel-message frame (cmd 0xA) -> leaf gate + ingest
        case wire::Cmd::CFG: handle_c(bytes, len, meta); break;     // C leaf-config frame (cmd 0xB) -> control-plane CONFIG_PULL answer -> adopt
        case wire::Cmd::P: {                                         // §S6 presence plane — LEAF-FREE (byte-0 low nibble is FLAGS, not a leaf gate)
            // Routed HERE, before any per-handler leaf filter. Type-gated: a non-hosting static drops cheaply inside the
            // ingest (no _mobile_reg + not is_mobile). Roster (dir=1) vs probe (dir=0) split on the byte-0 dir bit.
            if (wire::flags_of(bytes[0]) & P_DIR_ROSTER) {
#if MR_FEAT_MOBILE
                if (_cfg.is_mobile) presence_ingest_roster(bytes, len, meta);   // a mobile consumes rosters (its own home + candidates)
#endif
            } else {
                presence_ingest_probe(bytes, len, meta);            // a home (host) answers probes; a non-host drops on the empty registry
            }
            break;
        }
        // §w4-switchenum: EXT (0xF) is an EXPLICIT no-op, not a silently-dropped frame type — VERIFIED: nothing in
        // the tree ever builds one (zero cmd_byte(Cmd::EXT, …) call sites) and it is absent from docs/frames.md's
        // command-nibble map (0x0..0xC). It is a RESERVED extension nibble, so an EXT frame on the air today is
        // foreign/corrupt and dropping it is correct. Listing it keeps -Wswitch-enum clean here.
        case wire::Cmd::EXT: break;
        // ★ NO `default:` — DELIBERATE, and the reason is the tripwire, not the dispatch. The subject is
        // `cmd_of(bytes[0])` = a raw wire nibble, so 0xD/0xE ARE representable `Cmd` values with no enumerator
        // (`enum class Cmd : uint8_t`); with every enumerator cased they fall out of the switch and are ignored,
        // which is behaviour-IDENTICAL to the `default: break;   // 0xD/0xE unassigned` that stood here.
        // What the label COST: `-Wswitch` is blind behind any `default:`, so a future `Cmd` enumerator added
        // without a case here would dispatch NOWHERE and warn NOTHING. Without it the build reports
        // "enumeration value ... not handled in switch" — and this is the frame-dispatch switch, the one place a
        // whole frame type going unhandled is worst. Same idiom as node_mac.cpp's label_of_frame / retry_slot_of
        // (§w4-switchenum). Never re-add the label; add the case.
    }
}

// ---- the typed command seam (the app<->firmware entrypoint) -----------------

// Pack a send_layer destination path into the CmdResult.layer_path correlation token: hops MSB-first, hops[0] in
// the highest used byte ([2,3] -> (2<<8)|3 = 0x0203). Layer ids are >=1, so no leading-zero hop (unambiguous).
static uint32_t pack_layer_path(const uint8_t* hops, uint8_t hop_count) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < hop_count; ++i) v = (v << 8) | hops[i];
    return v;
}

CmdResult Node::on_command(const Command& c) {
    switch (c.kind) {
        case CmdKind::send: {
            if (_node_id == 0)                                    // unprovisioned: must join / cfg set node_id
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                      // no data SF (empty sf_list): refuse — no silent fallback
                return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            // ★★ §B20/B21 (2026-08-28) — **WHICH CHECK OWNS WHAT.** This one owns MEMORY SAFETY and nothing else:
            // `dm_max_body_bytes` (239) is `TxItem.inner[]`'s size minus a conservative prefix, so a body past it
            // would overrun the carrier. It is deliberately SHAPE-BLIND — it cannot know whether the DM will be
            // sealed, typed, or carry a location, and those decide the real carrier bound. It is therefore the
            // LAXER of the two bounds for a CRYPTED DM (239 vs a true 214) and it must NOT be tightened here:
            // making it shape-aware would fork a second copy of enqueue_data's flag decisions (U1).
            // The CARRIER bound is owned by the packers — `data_inner_cap()`/`data_frame_len()` in frame_codec.h —
            // and enforced where the shape is finally known (enqueue_data's seal cap, and pack_data itself).
            // ⇒ the laxer check can still ADMIT a body the carrier will not take, but it can no longer let one
            // DISAPPEAR: the carrier's refusal is loud (`send_failed{too_large}`) and fires before anything airs.
            // That split is the reconciliation, and it is why this line is unchanged.
            if (c.body_len > protocol::dm_max_body_bytes)         // body + the 2-B inner prefix must fit inner[] (no OOB)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            const Plane plane = static_cast<Plane>(c.u.send.plane);   // Wave 2 HARD SPLIT: 0=AUTO (companion/sim) / 1=TEAM (`-t`) / 2=GLOBAL (plain `send`)
            if ((c.u.send.flags & DATA_FLAG_E2E_ACK_REQ) && e2e_ack_ring_full())   // ★ shelf item (i): REFUSE a new -a send LOUD when the pending-ack ring is saturated (never evict-oldest -> the silent class)
                return CmdResult{ CmdCode::err_ack_ring_full, 0, _active->_tx_queue_n };
            if (c.u.send.dst_hash != 0) {                         // address-by-hash (hash-locate): resolve, then send
                const uint16_t ctr = send_by_hash(c.u.send.dst_hash, c.body, c.body_len, c.u.send.flags, c.crypt, /*reply_to_hash=*/0, /*mobile_ctr=*/0, plane, /*type=*/0, /*suppress_intro=*/c.no_intro);   // §D1 `-K`
                return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n, c.u.send.dst_hash, /*layer_path*/ 0 };
            }
            // §team-parity T1 (spec 2026-07-27 §3/T1): `send -t <id>` is refused on CONFIGURATION ONLY — never on
            // "we have not heard that teammate yet". The pre-T1 guard was `!is_team_peer(dst)`, a chicken-and-egg
            // deadlock: EVERY team-RREQ entry point (defer_send / try_drain_deferred / cascade_to_alt) sits
            // DOWNSTREAM of do_send, so the guard foreclosed the very discovery that would have satisfied it — a team
            // id could only be discovered once it had already been discovered. The 2026-07-27 bench failure (member
            // 213 could not `send 174 "…" -t` over the relay 234) returned here as `err ctr=0 depth=0`, i.e. refused
            // before a counter was minted. An unknown teammate id is now the NORMAL input to discovery.
            // ★ WHY THE OLD COMMENT'S "don't storm the static plane" RATIONALE DOES NOT SURVIVE (verified, not
            // assumed — this is the single assumption T1 rests on): a team RREQ cannot reach the static plane at all.
            // emit_route_request's team arm (node_route_discovery.cpp:116) needs team_local_id()!=0, uses the
            // team-PRIVATE rate/dedup ledgers and stamps team_scoped=true; handle_f (:183) drops a team_scoped F with
            // the `return` placed OUTSIDE the `#if MR_FEAT_TEAM`, so even a MR_FEAT_TEAM 0 gateway build drops it
            // rather than falling into the static F body; handle_f_team (:314) then requires is_mobile + same_team +
            // a DAD'd id. Static, wrong-team and not-yet-DAD'd nodes all bail before touching state.
            // Cost of an id that does not exist: ~3 team-scoped floods over send_defer_ttl_ms (ttl=1 probe at
            // defer_send, then requeries at team_hop_cap gated by route_request_seen_ttl_ms), then a loud
            // send_failed{no_route}. Bounded and self-limiting — strictly better than an instant refusal that is
            // loud but WRONG for a teammate that genuinely exists.
            // Membership test: the SAME predicate the sibling `send_channel -t` verb uses below (:1130,
            // `_cfg.is_mobile && _cfg.team_id != 0`) — U1, one definition of "this node is on the team plane" — PLUS
            // team_local_id()!=0, because without a DAD'd id emit_route_request's team arm returns silently and the
            // send would only fail 30 s later by TTL. Refuse loud now instead (C2).
            // Static reduction: `plane != Plane::TEAM` on every static / AUTO / GLOBAL send, so this guard is
            // unreachable there — exactly as the pre-T1 expression was. Build profiles: on the three gateway_* envs
            // (MR_FEAT_TEAM 0) team_local_id() stubs to 0 (node.h:197) and is_team_peer() to false, so a TEAM send is
            // refused unconditionally on both the pre- and post-T1 form — the feature is absent and refusing is its
            // correct inert shape.
            if (plane == Plane::TEAM && !(_cfg.is_mobile && _cfg.team_id != 0 && team_local_id() != 0))
                return CmdResult{ CmdCode::err_no_binding, 0, _active->_tx_queue_n };
            const uint16_t ctr = do_send(c.u.send.dst_id, c.body, c.body_len, c.u.send.flags, c.crypt, /*override_dst_hash=*/0, /*type=*/0, /*override_source_hash=*/0, plane);   // §8b: per-message crypt + Wave 2 plane
            return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };   // id-addressed: dst_hash/layer_path = 0
        }
        case CmdKind::send_channel: {                         // ROADMAP §3 channel gossip (single-layer) + §S7 T-B plane select
            if (_node_id == 0)                                // unprovisioned: must join / cfg set node_id
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                  // channel gossip rides a data SF: refuse if none configured
                return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            if (c.body_len > protocol::channel_msg_max_payload_bytes)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            // §S7 T-B DM-symmetric plane select: plain => GLOBAL; `-t` => TEAM only; `-t -g` => BOTH.
            const bool want_team   = c.u.channel.team;
            const bool want_global = c.u.channel.global || !c.u.channel.team;
            // ★★ §chan-crypt CL1 (spec 2026-07-30 §2.2): the console `-e` on a channel post. The intent rides
            // Command::crypt — the SAME field `send` and `send_layer` carry their per-message crypt intent in (U1: one
            // representation, and SendChannelCmd does not grow a parallel bool). `def` therefore means "no `-e`", and
            // NEITHER of the two hand-built producers sets it — `testch` (src/fw_main.cpp, which only assigns `crypt` on
            // its DM arm) nor the simulator's NodeRuntimeWrapper send_channel arm — so both stay on today's behaviour by
            // construction. That is also why accepting `-e` moved 0 of 36 corpus streams.
            // ⚠ Only `on` counts. `CryptIntent::off` would be an explicit "air this one in the clear", and for a channel
            // post that is today's behaviour anyway; O2 rules the opt-out a CONFIG toggle (`cfg set team_channel_crypt 0`),
            // not a per-send flag, so `off` and `def` are the same thing here.
            // ★★ §chan-crypt CL2a — `team_channel_crypt` DEFAULT-ON (T-K2 §2.5) is the second term. A node that HOLDS
            // the team content key seals a `-t` post without being asked; `-e` then exists to be EXPLICIT and to fail
            // loud when sealing is impossible. `team_channel_priv() != nullptr` IS the key-held test (node.h returns
            // nullptr while keyless, never a zero buffer), and since §o3-key-lifetime clears the pair on every
            // `team_id` change it genuinely means "a key for THIS team".
            // ⚠⚠ THE IMPLICIT TERM IS SCOPED TO A TEAM-***ONLY*** POST, and that scoping is load-bearing — the
            // dispatch's unqualified `(c.crypt == on) || (crypt_cfg && key_held)` is WRONG and would REGRESS two of
            // the spec's own "unchanged" rows the moment a node acquires a key:
            //   · plain `send_channel <ch> "…"` (GLOBAL) would get want_crypt=true with want_team=false and be
            //     REFUSED `no_team` — a keyholder could no longer post to a global channel at all;
            //   · `-t -g` would get want_crypt=true with want_global=true and be REFUSED `global_clear_copy` — an
            //     invocation that works today would start failing purely because a key arrived.
            // Scoping it to `want_team && !want_global` seals exactly where sealing is the right default, leaves both
            // documented rows untouched, and — the part worth noticing — keeps BOTH permanent refusals below firing
            // for EXPLICIT `-e` ONLY. Their reason strings therefore did NOT need widening; the dispatch's prediction
            // that they would was a consequence of the unqualified formula, not of the feature.
            const bool key_held    = (team_channel_priv() != nullptr);
            const bool want_crypt  = (c.crypt == CryptIntent::on)
                                  || (want_team && !want_global && _cfg.team_channel_crypt && key_held);
            const bool want_loc    = c.u.channel.loc;             // §chan-crypt CL2b `-l` — gated BELOW, after want_crypt is known
            // A keyholder who types `-t -g` gets TWO CLEAR copies, deliberately (the global copy cannot be sealed, and
            // sealing only the team copy is the self-cancelling combination the `-e` arm below refuses outright). That
            // is a SILENT downgrade of this node's default, so say so — telemetry only, no push: the operator asked
            // for the global copy explicitly, so this is information, not a failure. Corpus-inert (no sim node can
            // hold a key), and inert on any node without one.
            if (!want_crypt && want_team && want_global && _cfg.team_channel_crypt && key_held)
                MR_EMIT("channel_crypt_skipped", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "global_clear_copy"));
#if MR_FEAT_TEAM
            const bool team_member = _cfg.is_mobile && _cfg.team_id != 0;
#else
            const bool team_member = false;
#endif
            if (want_team && !team_member)                    // §S7: `-t` on a static / non-team node -> fail loud (unchanged: static plain=leaf, `-t` refused as today)
                return CmdResult{ CmdCode::err_no_binding, 0, _active->_tx_queue_n };
            // ★★★ THE `-e` MATRIX (spec §2.2) — four cases, and TWO OF THEM MUST REFUSE:
            //   plain         -> GLOBAL, plaintext   UNCHANGED (want_crypt false ⇒ this whole block is skipped)
            //   `-t`          -> TEAM,   plaintext on a KEYLESS member (unchanged, and deliberately so: a member
            //                    without the key must still be able to post, and plaintext is always openable) —
            //                    ✅ SEALED on a keyholder, by the `team_channel_crypt` default above (T-K2 §2.5)
            //   `-t -e`       -> ✅ BUILT (§chan-crypt CL2a) — TEAM, sealed under the team CONTENT key. The pre-flights
            //                    below (`no_key`, `no_fix`, `empty`, `too_large`) are the only things that stop it.
            //   `-t -l -e`    -> ✅ BUILT (§chan-crypt CL2b) — ★ THE OWNER'S TARGET: text AND this node's position in
            //                    ONE sealed post. The position rides the SEALED INNER's flags byte (bit1), so it is
            //                    never on the wire in clear; see the location gate above for the full O6 matrix.
            //   `-e` (no -t)  -> ❌ REFUSE. A GLOBAL channel has NO KEY. The only content key in the system is the TEAM
            //                    key (`team_ch_pub`, §team-ch-key T-K1) and a global channel has no team to own one, so
            //                    "sealed" is not a state this post can be reached in. Refusing beats both alternatives
            //                    C2 forbids: silently downgrading to clear (the app believes it encrypted) or inventing
            //                    a key. T-K2 §2.2 independently requires rejecting `crypted && !team` at origination.
            //   `-t -g -e`    -> ❌ REFUSE, and THE REASON IS THE WHOLE POINT, not a flag-conflict technicality: `-t -g`
            //                    means BOTH planes — one TEAM copy and one GLOBAL copy of the SAME body — and per the
            //                    row above the global copy cannot be sealed. Sealing the team copy while airing
            //                    byte-identical content in the clear defeats the encryption ENTIRELY: an eavesdropper
            //                    just reads the global copy. The combination is SELF-CANCELLING, so it is refused
            //                    rather than half-honoured. Remedy: drop `-g` (or drop `-e` and accept a clear post).
            // ★ WHY THIS LIVES HERE AND NOT IN console_parse: `CmdKind::send_channel` has THREE producers, and only one
            // of them is the text parser — `src/fw_main.cpp`'s `testch` scheduled-send workload builds the Command by
            // hand, and so does the simulator's NodeRuntimeWrapper. `on_command` is the one seam all three pass. A
            // refusal spelled as a `ParseErr` would exist for typed console lines only and be MISSING for the other two
            // — the exact sim-vs-metal asymmetry §b22 had just finished closing in the other direction.
            // ⚠ V1, corrected while writing this: it is NOT true that "the companion's binary transport builds a Command
            // directly". `lib/console/console_binary.*` is status/config/limits TLV only and carries no send verb at all;
            // the app sends by writing a console LINE over BLE-NUS into the same `dispatch()` the serial port uses
            // (§command-sink-consolidation). So the app DOES pass the parser — the argument rests on `testch` and the
            // sim, which is weaker than it first looked but still decisive.
            // The three UNSEALABLE arms (`no_team`, `global_clear_copy`, and CL2a's `no_key`) refuse with
            // `SendFailReason::unsealable` (U1 — NO new enumerator: the enum already means "this content may travel
            // ONLY sealed and this transport cannot carry it sealed, so it was REFUSED rather than downgraded", which
            // is precisely each case) plus `CmdCode::err_unsupported` (the code the sibling `send_layer -l` refusal
            // returns). CL2a's fourth arm is a SIZE refusal, not an unsealable one, so it reuses `too_large` /
            // `err_too_large` — telling an operator whose 190-B post will not fit that he needs a key would send him
            // after the wrong remedy, the exact confusion `no_location` was appended to avoid.
            // The operator-facing WHY rides the push: src/fw_main.cpp's `unsealable` arm names the sealable form and
            // why the others are not.
            // ★★★ §chan-crypt CL2b (spec §2.2.1, OWNER-RULED O6) — THE LOCATION GATE, and its POSITION IN THIS FILE IS
            // THE WHOLE POINT: it is tested AFTER `want_crypt` above, never before. The rule being enforced is
            // "a position never travels in clear", which is a property of WHAT HAPPENS ON THE WIRE, not of which
            // letters the operator typed — so a `-t -l` that WILL be sealed by the `team_channel_crypt` default must
            // succeed, exactly as `send -l` succeeds under `e2e_dm` with no `-e`. Deciding it before the effective-crypt
            // expression is known is precisely how register-B0 became a live leak on the DM plane (node_mac.cpp had to
            // HOIST `want_crypt` above its own location gate for the same reason — U1, that hoist is this one's twin).
            //   -t -l    + key held, crypt on   ->  OK      (sealed by the node default; want_crypt already true)
            //   -t -l    + no team key          ->  REFUSE  unsealable  (want_crypt false — the implicit term needs a key)
            //   -t -l    + team_channel_crypt 0 ->  REFUSE  unsealable  (want_crypt false — the operator opted out)
            //   -t -l -e                        ->  OK      (explicit; a missing key then refuses `no_key` below)
            //   -l       (no -t)                ->  REFUSE  unsealable  (no team ⇒ no content key, ever)
            //   -t -g -l -e                     ->  REFUSE  global_clear_copy (below) — the GLOBAL copy would air COORDINATES
            //   -t -l    + no fix (0,0)         ->  REFUSE  no_location (below, after the key checks)
            // ⚠ Only the FIRST row of the block below is decided here; the rest fall out of the `-e` matrix that
            // follows, which is the point of computing `want_crypt` once (U1) instead of re-deriving "will this be
            // sealed?" per flag. `unsealable` is REUSED verbatim (no new enumerator): its documented meaning — "this
            // content may travel ONLY sealed and this transport cannot carry it sealed, so it was REFUSED rather than
            // downgraded" — is exactly this, and `send -l` already reuses it for the identical DM case.
            if (want_loc && !want_crypt) {
                MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "loc_unsealed"));
                push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
                return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
            }
            if (want_crypt) {
                if (!want_team) {                             // `-e` with no `-t` — no team plane ⇒ no content key
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "no_team"));
                    push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                if (want_global) {                            // `-t -g -e` — the self-cancelling combination
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "global_clear_copy"));
                    push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                // ★★ §chan-crypt CL2a — CL1's `not_implemented` stub is GONE from here; the command now falls through
                // to the origination below with `want_crypt` true and do_send_channel SEALS. What remains are the two
                // PRE-FLIGHTS: conditions an operator can act on, refused HERE with a specific reason rather than
                // inside the seal, where they would surface as a generic failure after a ctr had already been minted.
                if (!key_held) {              // explicit `-e` on a node holding no team CONTENT key
                    // ⚠ NOT reachable via the implicit `team_channel_crypt` term — that term REQUIRES key_held — so
                    // this is exactly "the operator asked for encryption and we cannot provide it". Membership is not
                    // readership (the whole premise of T-K2): a member without the key posts plaintext fine, so the
                    // remedy is `team grantkey` from a keyholder, or the T-K4 QR — NOT joining the team again.
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "no_key"));
                    push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                // ★★ §chan-crypt CL2c — A LOCATED POST MUST NAME ITS SENDER, so a node with NO STABLE IDENTITY
                // (`_key_hash32 == 0`) REFUSES to originate one rather than airing bit2 with a zero hash (C2).
                // ⚠ WHY IT SITS AHEAD OF THE FIX CHECK: identity is the deeper lack. A node with neither will be told
                // to provision, which is the only order in which the remedies compose — telling it to acquire a GPS
                // fix first would send the operator after a step that cannot make the post legal.
                // `no_identity` is REUSED verbatim (U1, no new enumerator): it already means exactly this and
                // src/fw_main.cpp already renders it "(no crypto identity)".
                // ⓘ NARROW BY CONSTRUCTION, not dead: src/fw_main.cpp constructs `g_node` with key_hash32 0 and only
                // `setup()` replaces it from /mrid (identity_from_seed -> ed_pub[:4]), so this fires on a node whose
                // identity never came up. Natively reachable and tested; on metal it is the fail-loud backstop.
                if (want_loc && _key_hash32 == 0) {
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "no_identity"));
                    push_send_failed(SendFailReason::no_identity, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                // ★ §chan-crypt CL2b: no fix -> REFUSE `no_location`. Verbatim the DM rule (node_mac.cpp's `-l` gate,
                // U1) including its ORDER: the CONFIDENTIALITY refusals above win, so a `-l` post that is both
                // unsealable and fix-less reports the leak it would have caused, not the missing fix. `no_location` is
                // DISTINCT from `unsealable` on purpose — conflating them sends the operator after the wrong remedy
                // (encryption vs a GPS fix), which is the confusion the enumerator was appended to prevent.
                if (want_loc && _cfg.lat_e7 == 0 && _cfg.lon_e7 == 0) {
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "no_fix"));
                    push_send_failed(SendFailReason::no_location, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                // ★ §chan-crypt CL2b — `flags == 0` IS REFUSED (spec §2.2.1: "an empty post is a bug, not a feature").
                // The sealed inner's flags byte would be 0: no text AND no position, i.e. 26 bytes of seal wrapped
                // around nothing, which no reader can render and no sender can have meant.
                // ⚠ SCOPED TO THE SEALED PATH, deliberately: `send_channel <ch> ""` PLAINTEXT is accepted today and
                // stays accepted (C1 — changing it is a different slice, and the flags byte does not exist there).
                // ⚠⚠ NO `send_failed` PUSH HERE, and that is a REPORTED GAP rather than a silent choice: none of the
                // three reusable enumerators is true (`unsealable` would send the operator after a key, `too_large` is
                // the opposite complaint, `no_location` names a fix this post never asked for) and appending an
                // enumerator is out of this slice's scope. The SYNCHRONOUS `err_unsupported` is the app-facing answer —
                // `send_channel` is a console line whose CmdResult the app reads directly — plus the telemetry below.
                if (!want_loc && c.body_len == 0) {
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "empty"));
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
                }
                // The seal costs channel_seal_overhead_bytes (26) on the wire — [seal_ctr 2][seed8 8][tag 16] — and the
                // sealed blob must still fit the 200-B channel payload carriers. The plain-post check at the top of
                // this arm admits up to 200 B, so refuse HERE with the sealed cap rather than truncate a message the
                // operator typed (C2). Distinct code from the plain one only in the limit.
                // ★ §chan-crypt CL2b/CL2c: the cap bounds the whole INNER, `[flags 1][source_hash 4?][loc 6?][text]`,
                // so the text a sealed post can carry is 173 B (174 − 1) and **163 B** with `-l` (174 − 1 − 4 − 6).
                // ⚠ 163, NOT CL2b's 167: `-l` now costs 10 header bytes, not 6. ONE definition of the overhead AND of
                // the flags that drive it (protocol::channel_inner_flags / channel_inner_overhead) is shared with the
                // assembly in do_send_channel — a second copy here is exactly how a size gate and its writer drift
                // apart, and with two optional fields the drift would be silent truncation rather than a refusal.
                if (protocol::channel_inner_overhead(protocol::channel_inner_flags(c.body_len != 0, want_loc))
                        + c.body_len > protocol::channel_seal_max_plaintext_bytes) {
                    MR_EMIT("channel_crypt_refused", EF_I("channel_id", c.u.channel.channel_id), EF_S("reason", "too_large"));
                    push_send_failed(SendFailReason::too_large, /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
                }
            }
            uint16_t ctr = 0;
            if (want_team) ctr = do_send_channel(c.u.channel.channel_id, c.body, c.body_len, want_crypt, want_loc);   // TEAM: the mobile self-originates the team-scoped flood (do_send_channel team-scopes on is_mobile+team_id). §chan-crypt CL2a: want_crypt is true ONLY for a team-ONLY post, so the GLOBAL arms below keep the default (plaintext) by construction. CL2b: want_loc reaches here ONLY with want_crypt true (the gate above) — the GLOBAL arms take the defaulted false and never carry a position
            if (want_global) {
#if MR_FEAT_MOBILE
                if (_cfg.is_mobile) {                         // a mobile DELEGATES a GLOBAL post to its home (the home mints under its own origin). Off-grid (no home) -> fail loud.
                    // ★ §b39 PRODUCER (3): `true` here means a MOBILE_SEND DM really flew, but the HOME mints the
                    // channel ctr, so `ctr` stays 0 and this SUCCESS is reported as `queued ctr=0` — see the contract
                    // on the return below. Deliberate (C1): a local handle for a remote mint is B39's real fix.
                    if (!do_send_channel_delegated(c.u.channel.channel_id, c.body, c.body_len)) {
                        MR_EMIT("send_failed", EF_S("reason", "channel_no_home"));
                        push_send_failed(SendFailReason::mobile_no_home, /*dst=*/0, /*ctr=*/0);
                        if (!want_team) return CmdResult{ CmdCode::err_no_binding, 0, _active->_tx_queue_n };
                    }
                } else
#endif
                {                                             // a STATIC node: GLOBAL == the leaf post (byte-identical to the pre-S7 send_channel)
                    const uint16_t gctr = do_send_channel(c.u.channel.channel_id, c.body, c.body_len);
                    if (!want_team) ctr = gctr;
                }
            }
            // ★★★ §b39 (register B39) — `CmdCode::queued` HERE MEANS "THE COMMAND WAS ACCEPTED", NEVER "A POST WENT
            // OUT", AND `ctr == 0` IS THE SENTINEL THAT SAYS SO. Every path above that fails to mint answers 0 and
            // that 0 is handed through UNCHANGED, so a caller which spends an attempt/retry budget on
            // `code == queued` miscounts. ⇒ **TEST `ctr != 0`, NOT `code == queued`.** (src/fw_main.cpp's
            // scheduled-send `testch` counts `fired` on the code alone — see the note at its on_command call.)
            // ⓘ WHY 0 IS A SOUND SENTINEL AND NOT MERELY A PLAUSIBLE ONE: next_ctr (node_mac.cpp:20) floors below the
            // persisted per-peer high-water and then wraps 65535 -> 1, so its range is 1..65535 and it can NEVER mint
            // 0. That invariant is PINNED natively by "§b39 — next_ctr NEVER mints 0" (test/test_node_r3.cpp), so a
            // future `c + 1` "simplification" — which WOULD yield 0 — fails a test that names this contract instead of
            // only tripping a channel-push width assertion by luck.
            // ★★ THREE THINGS PRODUCE THE ZERO, AND — CORRECTING THE REGISTER ENTRY, WHICH NAMES ONLY THE FIRST TWO —
            // THE THIRD IS A **SUCCESS**. So `ctr == 0` means "THIS NODE MINTED NO CHANNEL CTR", which is NOT a synonym
            // for "nothing was sent":
            //   (1) node_channel.cpp:~645  the pre-TX SELF-GATE (min_interval / cap). NOT SENT: nothing minted, nothing
            //       buffered, nothing flooded. Reason + retry-after ride the `send_blocked` push — which carries NO ctr
            //       at all (emit_send_blocked, node_channel.cpp:~809), so it cannot be correlated either.
            //   (2) node_channel.cpp:~734  a SEAL failure AFTER the mint. NOT SENT, and worse: the ctr was burned and
            //       the `send_failed` push names THAT burned value — a handle this caller was never given. The reason
            //       arrives asynchronously and correlates with nothing.
            //   (3) ★ the mobile DELEGATED-GLOBAL arm just above: do_send_channel_delegated returns TRUE after a real
            //       MOBILE_SEND DM flew, but it discards that DM's ctr `(void)do_send(...)` and `ctr` is never assigned
            //       on that path ⇒ a registered mobile's plain (GLOBAL) `send_channel` answers `queued ctr=0` ON
            //       SUCCESS. Nothing distinguishes it from (1)/(2) synchronously, and it emits no CHANNEL-level push at
            //       all — only the wrapper DM's own send_acked/send_failed, under a ctr this caller never saw.
            //       Named, not fixed (C1): giving it a handle means minting/plumbing one, i.e. B39's real fix.
            // ⓘ NARROWER THAN THE ENTRY IMPLIES, and deliberately so after §err-reason/B32: a REFUSAL now names its own
            // code and the console prints it (`> err_unsupported ctr=0 depth=0`), so every refusal above is
            // self-describing. The residual ambiguity is EXACTLY this — `queued` + `ctr == 0`, i.e. "accepted, no local
            // handle" — and it is one bucket, not the three-way accepted/blocked/failed the entry was written against.
            // ⚠ SCOPE, twice over:
            //   (a) with `-t` present `ctr` is the TEAM copy's handle and NOTHING else — in BOTH global arms: on a
            //       mobile the global copy is delegated (producer 3, which has no handle to give) and on the static arm
            //       `gctr` is DISCARDED by its `if (!want_team)` guard. ⇒ 0 on a `-t -g` post reports the TEAM copy and
            //       says NOTHING about the global one.
            //   (b) this is NOT a statement about `queued` generally. `join`/`resolve`/`reqpubkey`/`peername` return
            //       `queued, 0` because they mint no ctr at all, and on the HASH-addressed `send` arm 0 can mean
            //       "parked behind an H resolve" — sent LATER, not refused (send_by_hash's documented contract, "the
            //       ctr if sent immediately, else 0", node_hashlocate.cpp:~1450; restated at node.h:201). There is no
            //       parking on THIS arm.
            // Deliberately NOT fixed here (C1): B39's fix shape is a discriminated result (accepted / blocked /
            // refused + SendFailReason), which changes what this function RETURNS, every caller, and the console
            // format. The `send_layer` arm below already carries the cheap half of that shape
            // (`ctr ? CmdCode::queued : CmdCode::err_no_gateway`) — adopting it here is a behaviour change, hence its
            // own slice.
            return CmdResult{ CmdCode::queued, ctr, _active->_tx_queue_n };   // buffered dirty -> advertised next BCN -> pulled
        }
        case CmdKind::join: {        // node_id DAD. Idempotent once joined. CLAIM-AFTER-LISTEN (L1): hear the
                                     // leaf's beacons first (populate _active->_rt/_active->_id_bind so the picker sees existing
                                     // ids), THEN claim — armed here, fired on kJoinListenTimerId.
            if (_joined) return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
            if (!_join_claim.active && !_join_listen_pending) {
                _join_listen_pending = true;
                (void)_hal.after(protocol::join_listen_ms, kJoinListenTimerId);
            }
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
        }
        case CmdKind::resolve: {     // diagnostic hash-locate (no DM) — the answer rides the hash_resolved push
            if (_node_id == 0)       // unprovisioned: the H flood needs a valid origin
                return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            request_resolve(c.u.resolve.dst_hash, c.u.resolve.hard);
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
        }
        case CmdKind::reqpubkey: {   // §6: user-triggered on-air pubkey request — the ONLY auto-source of WANT_PUBKEY now
            if (_node_id == 0) return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            uint32_t h     = c.u.resolve.dst_hash;
            uint8_t  plane = c.u.resolve.plane;                         // 0=AUTO / 1=TEAM (`-t`) / 2=GLOBAL i.e. static (`-s`)
            const uint8_t qid = c.u.resolve.dst_id;                     // §S4a: the queried id (0 = a hash-form request)
            bool by_id = false;                                         // §S4a: stage 1 — we could not resolve the id, so ASK who owns it
            if (h == 0 && qid != 0) {
                // ★★ §id-hash S1 (spec 2026-08-01 §1-A / §3-D1 / §3-D9) — THE DEFECT REPLACED HERE, bench-proven
                // 2026-08-01: this arm resolved through `team_key_of_id` ALONE, and that function's first line is
                // `if (_cfg.team_id == 0 || !is_team_peer(id)) return false` (node_routing.cpp:842) ⇒ `reqpubkey
                // <bare id>` was TEAM-ONLY BY CONSTRUCTION and on a STATIC node could not succeed for ANY id. The
                // bench pair: `hashof 186` answered 0x61CD83EA out of _id_bind while `reqpubkey 186` refused
                // err_no_binding out of _team_keys. §AB3's "each verb was correct about its own table; neither
                // answered the question" fix landed on `hashof` (firmware_commands.cpp:527) and never here.
                // ⇒ resolve through peer_book_by_id, THE dual-plane resolver — not a second hand-rolled two-table
                // scan (U1). It returns a MASK rather than a winner on purpose: the same 8-bit number legitimately
                // names different peers in the two planes (§18), so the choice is the CALLER's and must be explicit.
                PeerBookRow st{}, tm{};
                const uint8_t mask = peer_book_by_id(qid, st, tm);
                const bool has_static = (mask & kPeerBookStatic) != 0;
                const bool has_team   = (mask & kPeerBookTeam)   != 0;
                // §3-D9 — plane selection is EXPLICIT at the airtime/mutation boundary, where picking the wrong row
                // costs more than a display error. `-s`/`-t` force (and then must match, or we refuse); a bare id
                // picks only when exactly one plane holds it.
                if (plane == static_cast<uint8_t>(Plane::AUTO)) {
                    if (has_static && has_team)                         // the §18 collision, live -> name the flag, never guess
                        return CmdResult{ CmdCode::err_ambiguous_plane, 0, _active->_tx_queue_n };
                    if (has_static || has_team) plane = static_cast<uint8_t>(has_team ? Plane::TEAM : Plane::GLOBAL);
                    else {
                        // ★★ §id-hash S4a — §3-D9 BULLET 4 IS NOW LIVE, and S1's own note said it would be: an
                        // UNRESOLVED id no longer refuses, it FLIES A BY-ID QUERY, so a plane must be chosen for it.
                        // The rule is D9's last two bullets, read off CONFIGURATION rather than off a binding we do
                        // not have: static-only defaults static, team-only/off-grid defaults team, and a node that
                        // genuinely lives on BOTH planes must say which — because the same 8-bit number names two
                        // different peers there (§18) and this decision now spends AIRTIME.
                        // ⓘ `mobile_registered()` is the dual test: an off-grid team member has no static return
                        //   path for a GLOBAL answer (emit_hash_query would refuse it), whereas a HOMED team mobile
                        //   stamps origin=home_id and is genuinely reachable on both.
                        const bool team_capable   = (_cfg.team_id != 0);
                        const bool static_capable = (!_cfg.is_mobile || mobile_registered());   // mirrors emit_hash_query's return-route guard exactly (U1)
                        if (team_capable && static_capable)                 // genuinely dual -> the operator must say which
                            return CmdResult{ CmdCode::err_ambiguous_plane, 0, _active->_tx_queue_n };
                        plane = static_cast<uint8_t>(team_capable ? Plane::TEAM : Plane::GLOBAL);
                    }
                }
                const bool resolved = (plane == static_cast<uint8_t>(Plane::TEAM)) ? has_team : has_static;
                h = resolved ? ((plane == static_cast<uint8_t>(Plane::TEAM)) ? tm.hash : st.hash) : 0u;
                // ★★★ §id-hash S4a (spec §5 stage 1 + §6's B fix) — THE ORIGINATOR. Where S1 refused
                // `err_no_binding`, we now ask the mesh "who owns id N?" on the selected plane. This is the ONLY
                // producer of a BY_ID query in the tree, and it is what makes register B43 fixable at all: a node we
                // ROUTE to but never heard has no hash on either plane, so no by-hash question can be asked about it.
                // ⚠ `want_pubkey = false` (spec §5 stage 1): the answer that comes back is an id->hash binding, not a
                //   key. §5's one-round form (hash + pubkey together) is explicitly NOT DESIGNED and pack_h refuses
                //   the combination.
                // ✅ §id-hash S4b LANDED SPEC §5's STAGES 3-5 (2026-08-02): the bounded `resolve-id-for-pubkey`
                //   intent armed below consumes the answer, re-asks BY HASH and times out loudly, so the operator no
                //   longer runs the verb twice. S4a's "until it lands, run it by hand" note is retired in place.
                // ⓘ `h == 0` on a "resolved" row is folded in here rather than refused separately: a row carrying no
                //   hash answers the same question as no row at all, and the by-id query is the better answer to it.
                // ⚠ …BUT ONLY ONTO A PLANE THIS NODE ACTUALLY HAS. An explicit `-t` on a node with `team_id == 0`
                //   selects a plane that does not exist here: `emit_hash_query` would leave `team_scoped` UNSET (it
                //   needs a team_id to stamp) and the frame would fly as a STATIC by-id query wearing the operator's
                //   `-t`. Keep S1's `err_no_binding` refusal for that, echoing the plane searched (C2 — refuse, do
                //   not silently answer a different question).
                const bool plane_usable = (plane != static_cast<uint8_t>(Plane::TEAM)) || (_cfg.team_id != 0);
                if (h == 0 && !plane_usable)
                    return CmdResult{ CmdCode::err_no_binding, 0, _active->_tx_queue_n, 0, 0, plane };
                if (h == 0) by_id = true;
            }
            // ★★★ §id-hash S4b (spec §5 stages 1 + 5) — THE TWO PRE-FLIGHTS THE STAGE-1 ACK *CAN* MAKE, and both exist
            // because the ack must not defer a failure it is already able to see. Ordered BEFORE emit_hash_query so a
            // refusal costs no airtime (D9's discipline), and applied ONLY to the by-id form — the by-hash form has no
            // second stage to fail.
            if (by_id) {
                // (1) ★★ THE STAGE-1 PREFLIGHT IS WEAKER THAN THE STAGE-2 REQUIREMENT, and S4a shipped that gap:
                //     `emit_hash_query` gates `_crypto_ready` on `want_pubkey`, and stage 1 passes `want_pubkey =
                //     false`. So an identity-less node ACCEPTED the by-id query, flooded it, waited for the answer —
                //     and only then discovered it can never issue the mutual WANT_PUBKEY request. ⇒ ask the question
                //     now. `err_no_identity` already exists with exactly this remedy (`regen`), and reporting it
                //     synchronously is strictly better than an asynchronous giveup ~25 s later for a fact known here.
                //     ⓘ This CHANGES S4a's behaviour on that node (queued+accepted -> err_no_identity). Deliberate:
                //       the verb's contract is a pubkey, and we can prove we will not deliver one.
                if (!_crypto_ready)
                    return CmdResult{ CmdCode::err_no_identity, 0, _active->_tx_queue_n, 0, 0, plane, /*accepted=*/false };
                // (2) The bounded intent (spec §5 step 1). ARMED BEFORE THE EMIT so a full ring refuses ahead of the
                //     airtime; cleared below if the TX path then rejects the frame. A query whose answer we could not
                //     remember wanting is a flood spent on nothing.
                if (!id_pubkey_intent_arm(qid, plane))
                    return CmdResult{ CmdCode::err_resolve_pending_full, 0, _active->_tx_queue_n, 0, 0, plane, /*accepted=*/false };
            }
#if MR_FEAT_MOBILE
            // §mobile Part 2: a mobile WE HOST already pushed us its pubkey -> cache it LOCALLY, no flood (the home is the key
            // authority for its hosted mobiles). Lets the home send ENCRYPTED to its own hosted mobile. Mirrors the send-side _mobile_reg last-mile.
            // ⚠ §id-hash S4a: `!by_id` is load-bearing, not defensive — on the by-id path `h` is 0 and the ID is the
            // query key, so passing either to a HASH-keyed lookup asks a different question of the wrong table.
            if (const uint8_t* mk = by_id ? nullptr : host_mobile_ed_pub(h)) {
                peer_key_set(h, mk, PeerKeyConf::authoritative);
                MR_EMIT("peer_key_cached", EF_I("hash", static_cast<int64_t>(h)), EF_I("node", 0));   // mirror the handle_h path (telemetry + push)
                push_peer_key_cached(h);   // §S6: + the cached name
                // ★ §id-hash S1b (QA P1c): a GENUINE success that airs NOTHING — the key came out of the local cache
                // and the app learns it from the peer_key_cached push above. `accepted` stays FALSE so the BLE
                // transport does not additionally claim `reqpubkey_sent` — which means "the TX path accepted a
                // frame" (owner ruling 2026-08-01), and this path hands the TX path nothing at all.
                return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n, h, 0, plane, /*accepted=*/false };
            }
#endif
            // ★★ §id-hash S1b (QA finding P1c): emit_hash_query has FOUR silent early-outs and this arm used to
            // answer `queued` through all of them, which fw_main then rendered as `{"ev":"reqpubkey_sent"}` — a
            // contract event that asserts a flood happened. Worst case: a node with NO crypto identity reported
            // every reqpubkey as sent, and the in-source comment + the companion contract both claimed it "keeps its
            // existing error ack" — there was no such ack. The originator now reports and we map it honestly.
            // The mapping reuses existing codes wherever one is already the RIGHT answer (U1/U3), and adds exactly
            // one where none was:
            //   · degenerate      -> err_unsupported  (verbatim the precedent two arms below: send_layer refuses a
            //                                          0 dst_hash with err_unsupported — "this target is not a thing
            //                                          you can address", which also covers "the target is us")
            //   · no_identity     -> err_no_identity  (NEW — §err-reason/B32: its remedy is specific, `regen`)
            //   · no_return_route -> err_no_gateway   (verbatim the precedent below: a mobile with no home cannot
            //                                          delegate, `if (!_my_mobile_reg.active) return err_no_gateway`)
            //   · encode_failed   -> err_too_large    (pack_h refused: the frame did not fit its buffer)
            //   · tx_dropped      -> err_tx_queue_full (NEW — the LBT defer ring was full; the only TRANSIENT one,
            //                                          and U1-checked against err_ack_ring_full, a different ring)
            // The refusal still echoes `dst_hash` + `plane` so the app knows WHICH target on WHICH plane failed.
            // §id-hash S4a: on the by-id stage the QUERY KEY is the id and `want_pubkey` is false (spec §5 stage 1);
            // the by-hash form is unchanged. `degenerate` now also covers "id 0/255" and "that id is us".
            const HQueryOutcome oc = emit_hash_query(by_id ? static_cast<uint32_t>(qid) : h, /*hard=*/true, /*want_pubkey=*/!by_id, static_cast<Plane>(plane), by_id);   // §6.4: -t=TEAM (team_scoped, origin=team_local_id); else GLOBAL
            if (oc != HQueryOutcome::sent) {
                CmdCode code = CmdCode::err_unsupported;
                switch (oc) {                                   // -Wswitch: a new outcome must be mapped here
                    case HQueryOutcome::sent:            break;                              // unreachable (guarded above)
                    case HQueryOutcome::degenerate:      code = CmdCode::err_unsupported; break;
                    case HQueryOutcome::no_identity:     code = CmdCode::err_no_identity;  break;
                    case HQueryOutcome::no_return_route: code = CmdCode::err_no_gateway;   break;
                    case HQueryOutcome::encode_failed:   code = CmdCode::err_too_large;    break;
                    case HQueryOutcome::tx_dropped:      code = CmdCode::err_tx_queue_full; break;   // §S1c: transient — retry
                }
                // ★ §id-hash S4b: the stage-1 frame was NOT accepted, so the intent armed for it must go. Leaving it
                // would burn a ring slot for a full TTL and then report a timeout for a query that never flew —
                // a manufactured failure on top of a real one.
                if (by_id) id_pubkey_intent_clear(qid, plane);
                return CmdResult{ code, 0, _active->_tx_queue_n, h, 0, plane, /*accepted=*/false };
            }
            // §id-hash S1: echo the hash the query ACTUALLY flew for (the by-id form's RESOLVED hash) + the plane it
            // flew on, so no transport re-runs the lookup to build its own answer — that duplicate is exactly how
            // fw_main's BLE `reqpubkey_sent` echo kept the one-table bug alive after this arm was first fixed (U1).
            // ★ §id-hash S4a: on the BY-ID stage `h` is 0 and that is the HONEST echo — the hash is precisely what we
            // just went to ask for. It is also the app-visible discriminator between the two stages: a
            // `reqpubkey_sent` with `hash != 0` means the PUBKEY request flew, `hash == 0` means the id->hash query did.
            // ★★★ §id-hash S4b — WHAT THIS ACK CLAIMS, AND WHY IT IS NOT STRENGTHENED. S4b makes the node complete the
            // workflow, so the tempting wording is "a pubkey is on its way". **It cannot be.** Synchronously we know
            // one fact: the TX path accepted the STAGE-1 frame. Whether anyone owns that id, whether the answer routes
            // back, whether stage 2's frame is then accepted — all lie in the future, exactly as "the frame aired" lay
            // in the future for the ack this arc already had to weaken (owner ruling 2026-08-01, two review rounds).
            // ⇒ the RESULT is unchanged; only the app's INSTRUCTION changes (`hash == 0` no longer means "re-issue"),
            //   and that lives in write_reqpubkey_sent's contract note.
            // ⇒ B55's `hash == 0` case therefore DOES NOT DISAPPEAR — it is the honest report of a real stage-1
            //   acceptance. What disappears is the operator's second command.
            // ⚠ OWED to `ios-companion/INBOX_SYNC_CONTRACT.md` (QA-owned — reported, not written).
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n, h, 0, plane, /*accepted=*/true };
        }
        case CmdKind::peerkey: {     // §3: QR import — install the scanned full pubkey as a PINNED (verified) key.
            const uint8_t* ep = c.u.peerkey.ed_pub;             // key_hash32 = ed_pub[:4] (derived, never trusted from the wire)
            const uint32_t kh = key_hash32_of(ep);   // §P2-6: identity.h owns the LE(ed_pub[:4]) derivation
            // §AB2 (spec §2.3): the OPTIONAL one-shot name — `peerkey <hex64> "<label>"`, for the QR-import flow where
            // the key and the label arrive together. It rides Command::body and lands in the SAME peer_key_set name
            // parameter `peername` uses, so there is one write path (U1/U2). REFUSED past the cap rather than clamped:
            // the on-air callers may truncate a foreign advertisement, but an operator label the user typed must not be
            // silently shortened (C2 — the same rule and the same `too_long` outcome as `team grantkey name=`).
            if (c.body_len > protocol::peer_name_max)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            if (!peer_key_set(kh, ep, PeerKeyConf::pinned, reinterpret_cast<const char*>(c.body), c.body_len))    // false only when the cache is full of pinned keys (peer_key_full)
                return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n };
        }
        // ★ §AB2 (spec 2026-07-29 §2.3): `peername 0x<hash> "<text>"` — rename a CACHED peer. Purely local table state:
        // no frame, no airtime, no push, and the result is known NOW, which is why §2.6(b) ruled the ack SYNCHRONOUS
        // (a push would have needed a new PushKind, and the sim bridges that enum on its raw uint8_t).
        // Refusals reuse EXISTING CmdCodes, so no contract enum is extended (C4):
        //   · name > cap      -> err_too_large   (the app sees "too_long");
        //   · hash not cached -> err_unknown_dst (the app sees "unknown_hash"). ⚠ NOT unprovisioned- or route-gated:
        //                        renaming a contact is a local edit and must work on a node that has joined nothing.
        //   · body_len == 0   -> also err_too_large, as an API-level backstop only. The CONSOLE never gets here with an
        //                        empty name (console_parse.cpp refuses `peername 0x… ""` as bad_args, because "too_long"
        //                        would be an actively misleading reason for it) — this guard exists for a direct
        //                        on_command caller (a test, or the sim) so peer_name_set is never handed a 0-length name.
        case CmdKind::peername: {
            if (c.body_len == 0 || c.body_len > protocol::peer_name_max)
                return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            if (!peer_name_set(c.u.peername.key_hash32, reinterpret_cast<const char*>(c.body), c.body_len))
                return CmdResult{ CmdCode::err_unknown_dst, 0, _active->_tx_queue_n, c.u.peername.key_hash32, /*layer_path*/ 0 };
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n, c.u.peername.key_hash32, /*layer_path*/ 0 };
        }
        case CmdKind::send_layer: {                          // Slice 4d: cross-layer DM origination (§5)
            if (_node_id == 0)                               return CmdResult{ CmdCode::err_unprovisioned, 0, _active->_tx_queue_n };
            if (_cfg.allowed_sf_bitmap == 0)                 return CmdResult{ CmdCode::err_no_data_sf, 0, _active->_tx_queue_n };
            if (c.body_len > protocol::dm_max_body_bytes)    return CmdResult{ CmdCode::err_too_large, 0, _active->_tx_queue_n };
            // Every send_layer return echoes the dst_hash (and, once known, the layer_path) so the app holds the
            // full "send handle" (CmdResult.dst_hash + layer_path); async pushes then correlate by CmdResult.ctr.
            if (c.u.layer.dst_hash == 0)                     return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };  // a layer send needs a stable dst key
            if ((c.u.layer.flags & DATA_FLAG_E2E_ACK_REQ) && e2e_ack_ring_full())   // ★ shelf item (i): refuse a new -a cross-layer send LOUD when the pending-ack ring is full
                return CmdResult{ CmdCode::err_ack_ring_full, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };
            // ★★ §loc-per-send (2026-07-31, register B0): `send_layer -l` is REFUSED, synchronously, before a seal_ctr or
            // a MAC ctr is burned. `send_layer` is ALWAYS a cross-layer flight (the console parser requires >=1 hop), and
            // NEITHER cross-layer builder can carry a position: enqueue_cross_layer masks the flag off and packs
            // lat/lon = 0, and the sealed substitute (DATA_TYPE_SEALED_RELAY via build_sealed_relay_body) has no flags
            // word on the wire for the receiver to read one from. ⇒ the honest answer is a refusal, not a silent strip —
            // the owner ruled out "the app believes it shared a position it did not" explicitly.
            // ★ WHY HERE AND NOT ONLY AT THE CHOKE POINT: this verb forks below into the static path
            // (originate_layer_path -> enqueue_cross_layer, which has the structural guard) AND the mobile path
            // (delegate_send_layer, which builds its own TxItem and never reaches that guard). Refusing at the verb
            // covers both forks in one place, before the seal, and gives the operator a synchronous err_unsupported
            // instead of only an async push. `-l` is deliberately still ACCEPTED BY THE PARSER on this verb (rather than
            // rejected as `-t` is) so the flag letter keeps ONE meaning everywhere and the refusal can explain itself.
            if (c.u.layer.flags & DATA_FLAG_LOCATION) {
                MR_EMIT("location_refused", EF_I("dst_hash", static_cast<int64_t>(c.u.layer.dst_hash)), EF_S("reason", "send_layer"));
                push_send_failed(SendFailReason::unsealable, /*dst=*/0, /*ctr=*/0);
                return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };
            }
            // §S4: a CRYPTED cross-layer send (e2e_dm ON, or per-message `-e`) SEALS the body to the target HERE and rides
            // a DATA_TYPE_SEALED_RELAY plaintext frame [seal_ctr][seed8][ct‖tag] — there is NO CRYPTED-flagged XL frame
            // (the crypto core stays same-layer; XL confidentiality layers on top via the relay type). This LIFTS the old
            // v1 e2e_dm-ON refusal: an e2e_dm node now seals cross-layer (never a cleartext downgrade — want_crypt below
            // always steers to the seal path). The seal ctr is CARRIED (the frame ctr stays the originator/home's for MAC
            // dedup). FAIL LOUD on seal failure (no pubkey / identity / too large), never cleartext.
            const bool want_crypt = (c.crypt == CryptIntent::on)  ? true
                                  : (c.crypt == CryptIntent::off) ? false
                                                                  : _cfg.e2e_dm;
            const uint8_t* dbody = c.body; uint8_t dblen = c.body_len; uint8_t etype = 0;
            uint8_t rbuf[protocol::max_payload_bytes_hard_cap];
            if (want_crypt) {
                if (c.u.layer.hop_count == 0)                return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };  // sealed XL needs an explicit path (no park+H-flood)
                SealOutcome oc = SealOutcome::ok;
                const uint8_t rn = build_sealed_relay_body(c.u.layer.dst_hash, c.body, c.body_len, rbuf, sizeof rbuf, oc);
                if (rn == 0) {                               // fail loud (no_pubkey/no_identity/too_large) — NEVER cleartext
                    MR_EMIT("e2e_xl_seal_failed", EF_I("dst_hash", static_cast<int64_t>(c.u.layer.dst_hash)), EF_I("oc", static_cast<int>(oc)));
                    push_send_failed((oc == SealOutcome::no_pubkey)   ? SendFailReason::no_pubkey
                                   : (oc == SealOutcome::no_identity) ? SendFailReason::no_identity
                                                                      : SendFailReason::too_large,
                                     /*dst=*/0, /*ctr=*/0);
                    return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };
                }
                dbody = rbuf; dblen = rn; etype = DATA_TYPE_SEALED_RELAY;
            }
#if MR_FEAT_MOBILE
            // §S1/§S4: a REGISTERED mobile must NOT originate a cross-layer DM on the static plane (origin = local id ->
            // the answer can't route back -> RREQ storm). WRAP it to the HOME (DATA_TYPE_MOBILE_SEND + the path); the home
            // prepends its own layer, re-validates, and re-originates with the enclosed TYPE. Requires an explicit path.
            if (_cfg.is_mobile) {
                if (!_my_mobile_reg.active)  return CmdResult{ CmdCode::err_no_gateway, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };   // no home -> can't delegate (fail loud)
                if (c.u.layer.hop_count == 0) return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };  // no park-resolve on a mobile
                // §S2: auto-attach INTRO on a first-contact PLAINTEXT XL send (not for a sealed relay — a sealed body must
                // never carry a cleartext key prefix). Ride enclosed_type=INTRO through the wrapper (§1b-4).
                if (!want_crypt) {
                    uint8_t ibody[protocol::max_payload_bytes_hard_cap]; uint8_t pfx[33 + 32];
                    const uint8_t pn = c.no_intro ? 0 : intro_attach_prefix(c.u.layer.dst_hash, CryptIntent::off, c.body_len, pfx, sizeof pfx);   // §D1 `-K`: suppress the attach for this send
                    if (pn && static_cast<size_t>(pn) + c.body_len <= sizeof ibody) {
                        for (uint8_t i = 0; i < pn; ++i)         ibody[i]      = pfx[i];
                        for (uint8_t i = 0; i < c.body_len; ++i) ibody[pn + i] = c.body[i];
                        dbody = ibody; dblen = static_cast<uint8_t>(pn + c.body_len); etype = DATA_TYPE_INTRO;
                    }
                    const uint16_t ctr = delegate_send_layer(c.u.layer.dst_hash, c.u.layer.hops, c.u.layer.hop_count,
                                                             /*enclosed_type=*/etype, dbody, dblen, c.u.layer.flags);
                    const uint32_t lp = pack_layer_path(c.u.layer.hops, c.u.layer.hop_count);
                    return CmdResult{ ctr ? CmdCode::queued : CmdCode::err_no_gateway, ctr, _active->_tx_queue_n, c.u.layer.dst_hash, lp };
                }
                // §S4: the SEALED relay (dbody/dblen/etype already built above) — the home re-originates DATA_TYPE_SEALED_RELAY
                // WITHOUT re-sealing (it can't; only the mobile holds the target's ECDH pair). Wrapper stays PLAINTEXT.
                const uint16_t ctr = delegate_send_layer(c.u.layer.dst_hash, c.u.layer.hops, c.u.layer.hop_count,
                                                         /*enclosed_type=*/etype, dbody, dblen, c.u.layer.flags);
                const uint32_t lp = pack_layer_path(c.u.layer.hops, c.u.layer.hop_count);
                return CmdResult{ ctr ? CmdCode::queued : CmdCode::err_no_gateway, ctr, _active->_tx_queue_n, c.u.layer.dst_hash, lp };
            }
#endif
            if (c.u.layer.hop_count > 0) {
                // EXPLICIT-PATH origination (the user supplied the destination layer path) — route by it, no H-query.
                // Validate the path fail-loud (§5, no silent fix): the full path (1 + hop_count, after prepending our
                // own layer) must fit gw_env_max_hops; every layer id >= 1; and hops[0] must not be our OWN layer (a
                // cross-layer send to your own layer is a misconfig).
                const uint8_t hc = c.u.layer.hop_count;
                const uint32_t lp = pack_layer_path(c.u.layer.hops, hc);
                if (hc > protocol::gw_env_max_hops - 1)      return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, lp };  // path too long
                for (uint8_t i = 0; i < hc; ++i)
                    if (c.u.layer.hops[i] == 0)              return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, 0 };  // layer id 0 is unset (path invalid -> layer_path omitted)
                if (c.u.layer.hops[0] == active_layer_id())  return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n, c.u.layer.dst_hash, lp };  // self-layer = misconfig
                // Synchronous: queued (+ ctr to correlate) / err_no_gateway / err_too_large. NO orphan push.
                // §S4: dbody/dblen/etype = the SEALED relay body + DATA_TYPE_SEALED_RELAY when want_crypt (a static
                // originating a sealed XL DM directly); else c.body/0 = plaintext, byte-identical to before.
                uint16_t ctr = 0;
                const CmdCode code = originate_layer_path(c.u.layer.dst_hash, c.u.layer.hops, hc, dbody, dblen, c.u.layer.flags, ctr, /*type=*/etype);
                return CmdResult{ code, ctr, _active->_tx_queue_n, c.u.layer.dst_hash, lp };
            }
            // hop_count == 0: park-first (§5 / user 2026-06-13): resolve the dst's (node_id, target_layer) via an H
            // query; the drain decides same-layer-vs-cross-layer from the answer's target_layer (layer-in-id_bind cache
            // deferred). The target layer is resolved later, so layer_path is unknown here (0) — the app still has dst_hash.
            park_send_layer(c.u.layer.dst_hash, c.body, c.body_len, c.u.layer.flags);
            emit_hash_query(c.u.layer.dst_hash, /*hard=*/false);
            return CmdResult{ CmdCode::queued, 0, _active->_tx_queue_n, c.u.layer.dst_hash, /*layer_path*/ 0 };
        }
        default:
            return CmdResult{ CmdCode::err_unsupported, 0, _active->_tx_queue_n };
    }
}

void Node::enqueue_push(const Push& p) {
    if (_push_count >= protocol::cap_push_ring) {        // full -> drop-oldest (MeshCore offline queue)
        _push_head = static_cast<uint8_t>((_push_head + 1) % protocol::cap_push_ring);
        --_push_count;
    }
    const uint8_t tail = static_cast<uint8_t>((_push_head + _push_count) % protocol::cap_push_ring);
    _push_ring[tail] = p;
    ++_push_count;
}

// ★★★★ §CUSTODY-B / [[B268]] blocker-1 (QG 2026-08-30) — **THE ONE POST-ADMISSION TERMINAL OUTCOME.**
//
// ⛔⛔ THE DEFECT IT CLOSES, stated first because it is subtle: §6.2(5) removes the generic `send_failed` from
//    every protocol-internal type, and [[B268]] gave the team-key grant a replacement — but only at
//    `giveup_flight`. An ADMITTED grant can die at SEVEN other places, and at each of them the generic push was
//    correctly suppressed and nothing replaced it ⇒ the §UI-16 panel sat at `GRANT QUEUED` FOR EVER. A
//    per-site fix would have re-created the duplication this whole slice exists to remove, so there is exactly
//    one helper and every carrier death routes through it.
//
// ⛔⛔ `generic_owed` IS THE CALLER'S OWN, UNCHANGED, SITE-SPECIFIC ANSWER AND IS PASSED **IN**, NEVER RECOMPUTED
//    HERE. That is the single most important line in this function, and it is what stops the helper becoming a
//    stealth [[B263]] fix: the sites genuinely differ — `giveup_flight` owes a generic failure UNCONDITIONALLY
//    (transit or own, which IS B263 and stays exactly as defective as it is until Slice E), the reprovision
//    purges first ask `carrier_owes_send_failed`, and three sites owe NOTHING to anybody and must stay silent.
//    If this helper decided that question, every one of those would move. It decides only the TRAIT question,
//    which is this slice's, and which each site already asked identically.
//
// ⛔ THE TWO ARMS ARE MUTUALLY EXCLUSIVE BY CONSTRUCTION, not by an `else`: `generic_send_lifecycle` is false for
//    every internal type and the grant is one, so no carrier can take both. They are written as two independent
//    `if`s so each stays separately attackable — §18.2 mutates them one at a time, per call site.
//
// ★ SINGLE EMISSION IS GUARANTEED BY DESTRUCTION, not by a flag: every caller DESTROYS the carrier at the site
//   that reports it (a `_pending_tx.reset()`, a `continue` that drops the deferred entry, or a `_deferred_n = 0`
//   after the loop). A carrier that MOVES between carriers — flight -> requeue -> deferred -> purge — is reported
//   only where it finally dies, because the moves are not deaths. ⇒ zero or one, never two.
void Node::terminal_carrier_outcome(uint8_t type, bool own_origination, bool generic_owed,
                                    SendFailReason reason, uint8_t dst, uint16_t ctr) {
    if (generic_owed && data_type_traits(type).generic_send_lifecycle) push_send_failed(reason, dst, ctr);
    // [[B268]]: the grant's protocol-specific terminal outcome (§6.2(6)). ⛔ Own originations only — a relayed
    // grant in transit is not ours to report, the same fence the generic path keeps.
    if (own_origination && type == DATA_TYPE_TEAM_KEY_GRANT) {
        Push g{}; g.kind = PushKind::team_key_grant_failed; g.reason = reason; g.dst = dst; g.ctr = ctr;
        enqueue_push(g);
    }
}

// §3-B.2: the one send-failure fill (see node.h). Every other Push field stays at its `Push{}` default — notably
// `body`/`body_len` empty and `origin` 0, which is what all 24 former hand-rolled sites did.
void Node::push_send_failed(SendFailReason reason, uint8_t dst, uint16_t ctr) {
    Push pu{};
    pu.kind   = PushKind::send_failed;
    pu.reason = reason;
    pu.dst    = dst;
    pu.ctr    = ctr;
    enqueue_push(pu);
}

// §3-B.9: the wire_version refusal ritual, verbatim-duplicated at the two version walls (see node.h). Both are
// rate-limited on the SHARED `_last_join_refused_ms` window — one refusal push per join_refused_retry_ms across every
// flavour — so a foreign-version neighbour's every beacon/roster can't spam the app. The MR_EMIT stays INSIDE the
// window with the Push: it is device-stripped, which is precisely why the Push exists (R6.3 §7c).
void Node::push_join_refused_wire(uint8_t their_ver) {
    const uint64_t now = _hal.now();
    if (_last_join_refused_ms == 0 || now - _last_join_refused_ms >= protocol::join_refused_retry_ms) {
        _last_join_refused_ms = now;
        Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::wire_version;
        pu.origin = their_ver; pu.dst = protocol::wire_version; enqueue_push(pu);
        MR_EMIT("join_refused", EF_S("reason", "wire_version"), EF_I("their_ver", their_ver), EF_I("my_ver", protocol::wire_version));
    }
}

// ★★★★ §T3 2026-08-14 — THE ORIGIN-OWNERSHIP RULE FOR `send_aired`, and it is the whole app half of [[B164]].
//
// ⛔⛔ THIS FUNCTION IS LOAD-BEARING AND MUST NEVER BE WRAPPED IN `MR_TELEMETRY(...)` — nor may its call site.
//    `MESHROUTE_NO_TELEMETRY` is set on every board env, so a wrapped push would compile the entire fix OUT on metal
//    while native and all 36 corpus streams stayed green. That is [[B169]]'s mirror image and it earns this comment.
//
// The rule, in full: enqueue exactly one `send_aired` iff the completion is a DATA (`frame_tag_of` masks §B186a's
// mobile-op high byte), a flight is live, the completion's identity is EXACTLY this flight's (`o.seq ==
// pt.flight_gen`, never an approximation — a stale retry completing after the flight was replaced would otherwise
// falsely confirm the NEW one), and the carrier matches exactly ONE of two ownership rows:
//
//   · ORDINARY LOCAL DM      `!pt.m_broadcast && !pt.has_previous_hop`     -> dst = pt.dst, ctr = pt.ctr
//   · LOCAL CHANNEL POST     `pt.m_broadcast && pt.flood && inner_len>=6`  -> dst = 0,      ctr = entry.ctr
//     ...plus an ACTIVE `_channel_reoffer_pending` entry whose `id` matches and whose `holder` is FALSE.
//
// ★★ WHY `pt.flood` IS A REQUIRED CLAUSE AND NOT DECORATION. `enqueue_channel_m` (the CHANNEL_PULL responder) writes
//    the SAME 4-byte BE message id into `inner[0..3]` and sets `is_channel_m`, but it does NOT set `flood`; only the
//    flood path does (node_channel.cpp's re-flood/origination). ⇒ without this clause a PULL RESPONSE airing while
//    the origin's own re-offer slot is still active would be reported as THE ORIGINAL POST AIRING — a false
//    confirmation on precisely the surface this arc exists to fix. With it, the DM and channel rows are structurally
//    disjoint rather than merely non-overlapping in practice.
// ⚠ `entry.holder == false` is equally load-bearing: a HOLDER slot (a relay covering its downstream) owns no
//    origination and its `ctr` is 0, so a holder match would push a correlation handle that means nothing.
// ★ The id decode is the EXISTING shared helper `m_inner_id` (U1) — never hand-rolled here; `inner_len >= 6` is
//    checked first, exactly as `do_data_tx` does, so this read can never be the one that underflows.
// ⛔ `carrier_owes_send_failed` is NOT the predicate here and must not be reused for it: it answers *"does dropping
//    this carrier owe a `send_failed`?"* and excludes `channel_m` DELIBERATELY, because a channel post's app future
//    is the `channel_sent` push owned by the re-offer slot. Reusing it made `ChanState::aired` unreachable once.
// ⛔ `ChannelReofferPending` is READ here and never grown — node.h's `sizeof == 12` static_assert is the tripwire.
// ⓘ NOTHING IS STORED: no de-duplication bit exists anywhere, because the consumer's monotonic rank makes a repeated
//    `send_aired` idempotent. `sizeof(Node)` is therefore unmoved by this whole slice.
void Node::push_send_aired_if_owned(const TxOutcome& info) {
    if (frame_tag_of(info.tag) != FrameTag::data) return;       // beacon / RTS / CTS / ACK / NACK: telemetry only
    if (!_active->_pending_tx) return;                          // the flight is gone -> nothing to attribute this to
    const PendingTx& pt = *_active->_pending_tx;
    if (info.seq != pt.flight_gen) return;                      // exact flight identity (flight_gen is never 0 on a live flight)
    // §CUSTODY-B §6.2(5): `send_aired` is a GENERIC user-send outcome, so a protocol-internal flight raises none.
    // ⓘ This sits AFTER the identity check on purpose — the type is only meaningful once the outcome is proven to
    //   belong to THIS flight; asking it earlier would read a type off a flight the outcome does not describe.
    // ⛔ The `has_previous_hop` ownership term below is NOT touched (it is the transit axis, [[B263]]/Slice E).
    if (!data_type_traits(pt.type).generic_send_lifecycle) {
        // ★★★★ [[B268]] (owner ruling 2026-08-30) — THE TEAM-KEY GRANT'S OWN AIRING OUTCOME. §6.2(6): an internal
        //   type keeps its PROTOCOL-SPECIFIC result, and for the grant that result is this push. Without it §UI-16's
        //   panel sits at `GRANT QUEUED` for ever, waiting on a generic `send_aired` that can no longer be minted —
        //   which is [[B268]] verbatim, and is why the suppression above must not simply `return` for every type.
        // ⛔ THE OWNERSHIP GUARD I RELY ON IS `has_previous_hop`, ASSERTED HERE RATHER THAN ASSUMED: a grant is an
        //   OWN origination at the node that mints it (`team_key_grant_send` -> `send_by_hash` -> `do_send`), and a
        //   RELAYED grant passing through carries `has_previous_hop` and must raise nothing. That term already sits
        //   below for the generic path; this arm re-uses it rather than forking a second ownership rule, so the
        //   [[B263]] transit-vs-own fence is respected identically on both arms and Slice E moves them together.
        // ⛔ `m_broadcast` cannot be a grant (a grant is a unicast DM), so the channel arm below is not duplicated.
        if (pt.type == DATA_TYPE_TEAM_KEY_GRANT && !pt.m_broadcast && !pt.has_previous_hop) {
            Push g{}; g.kind = PushKind::team_key_grant_aired; g.dst = pt.dst; g.ctr = pt.ctr; enqueue_push(g);
        }
        return;
    }
    Push pu{};
    pu.kind = PushKind::send_aired;
    if (!pt.m_broadcast) {
        if (pt.has_previous_hop) return;                        // a FORWARDED DM — we made no send, so we own no future
        pu.dst = pt.dst;
        pu.ctr = pt.ctr;
    } else {
        if (!pt.flood) return;                                  // a CHANNEL_PULL response carries the same id — see above
        // ⚠ BELT-AND-BRACES, AND MEASURED AS SUCH RATHER THAN CLAIMED AS TESTED: both production paths that can put
        //   an m_broadcast flight in the air REFUSE a short inner first (`do_data_tx` and `tx_m_broadcast_rts` both
        //   log and reset `_pending_tx`), so no live flight can reach here with `inner_len < 6`. ⇒ deleting this line
        //   leaves the native suite GREEN — verified by mutation, and recorded instead of pretending otherwise. It
        //   stays because this read must never be the one that underflows if a future path changes that.
        if (pt.inner_len < 6) return;
        const uint32_t id = m_inner_id(pt.inner);
        const ChannelReofferPending* owned = nullptr;
        for (uint8_t s = 0; s < protocol::cap_channel_reoffer_pending; ++s) {
            const ChannelReofferPending& rp = _active->_channel_reoffer_pending[s];
            if (rp.active && rp.id == id && !rp.holder) { owned = &rp; break; }
        }
        if (!owned) return;                                     // a relay/holder re-flood of someone else's post
        pu.dst = 0;
        pu.ctr = owned->ctr;                                    // §b40: the FULL 16-bit handle, never pt.ctr's low byte
    }
    enqueue_push(pu);
}

bool Node::next_push(Push& out) {
    if (_push_count == 0) return false;
    out = _push_ring[_push_head];
    _push_head = static_cast<uint8_t>((_push_head + 1) % protocol::cap_push_ring);
    --_push_count;
    return true;
}

// ---- callbacks deferred to later R-iterations -------------------------------
// ★★★ §T1 2026-08-14 — THE ADAPTER, AND IT IS ALL THAT IS LEFT OF THIS FUNCTION. The simulator's asynchronous
// refusal is ONE `TxOutcome` of kind `refused`; the entire previous body of `on_radio_busy` now lives in
// `Node::on_tx_complete` below, moved VERBATIM (its parameter is still named `info` for exactly that reason).
// ⛔ NOTHING ELSE CHANGES: the four `BusyInfo` members map one-to-one, `error` is unread on this arm
// (`TxResult::ok` is the placeholder), and `seq` is **0 DELIBERATELY** — the simulator's refusal carries no flight
// identity, and reconstructing one here from `_pending_tx` is the false-confirmation defect this arc is about.
// ⛔ THE ADAPTER IS KEPT, NOT REMOVED: the simulator's wrapper and ~35 native cases call this directly, and
//    re-pointing them at `on_tx_complete` is a separate pure refactor (C1) — not this slice.
void Node::on_radio_busy(const BusyInfo& info) {
    on_tx_complete(TxOutcome{ TxOutcomeKind::refused, info.reason, TxResult::ok,
                              info.tag, /*seq=*/0u, info.sf, info.busy_until_ms });
}

// R4.5b: the sim's LBT/half-duplex safety-net fires this when a handed TX hits a busy channel (the firmware
// LBT defers the INITIATING TXs first, so this catches the residual + the non-LBT responses). info.tag is the
// frame-type the firmware tagged the TX with. RTS -> the already-armed rts_timeout re-RTSes (we must NOT clear
// awaiting_cts here — see below); DATA -> clear awaiting_ack + cancel the ack-timeout; then re-issue the stashed
// retry-eligible frame (CTS/DATA/ACK/NACK) up to TX_DEFER_MAX_RETRIES. Lua dv:12081-12215. NEVER fires in the gates
// (lbt_enabled=false + healthy duty) -> inert.
// ★ §T2 — DeviceHal's `failed` / `unknown` attempts are reported here to telemetry and return. They do NOT enter the
//   app push ring, mutate protocol state, arm a timer or consume the refusal stash: an attempt outcome is not a
//   terminal send outcome, and a failed/unknown attempt may still be followed by a successful MAC retry.
//   The `refused` arm below remains the simulator's existing recovery path, reached through on_radio_busy's adapter.
// ★★ §T3 — `aired` GAINS ITS ONE APP CONSUMER and is now the ONLY attempt outcome that may. It can only ever be an
//   UPGRADE (`queued -> sent`) and no later attempt can contradict it, which is exactly what `failed`/`unknown`
//   cannot claim. Everything else about this function is unchanged: no protocol state, no timer, no stash.
void Node::on_tx_complete(const TxOutcome& info) {
    switch (info.kind) {
        case TxOutcomeKind::aired:
            MR_EMIT("tx_aired", EF_I("tag", info.tag), EF_I("seq", info.seq), EF_I("sf", info.sf));
            push_send_aired_if_owned(info);   // ⛔ NOT telemetry, NEVER inside MR_TELEMETRY — see the definition
            return;
        case TxOutcomeKind::failed:
            MR_EMIT("tx_failed", EF_I("tag", info.tag), EF_I("seq", info.seq), EF_I("sf", info.sf),
                    EF_I("result", static_cast<uint8_t>(info.error)));
            return;
        case TxOutcomeKind::unknown:
            MR_EMIT("tx_unknown", EF_I("tag", info.tag), EF_I("seq", info.seq), EF_I("sf", info.sf));
            return;
        // ★★★ [[B159]] — THE PHYSICAL-START EXPIRY, AND IT IS THE ONE ATTEMPT OUTCOME THAT IS **TERMINAL**.
        // The HAL refused to start this frame because its `TxParams::deadline_ms` had passed (device_hal.cpp's
        // pump_tx); nothing aired. Unlike `failed`/`unknown` — which mean "this attempt died, a MAC retry may still
        // follow" and deliberately touch no protocol state — an expiry means the flight's gateway patience is SPENT,
        // so re-entering retry would be exactly the silent-late-delivery behaviour [[B159]] exists to forbid.
        // ⇒ it maps to the SAME loud give-up an age-expired doorstep hold produces (U1: `giveup_flight` +
        //   `send_giveup{gateway_unreachable_timeout}`), producing EXACTLY ONE terminal failure.
        // ⛔ CORRELATED BY `seq`, WHICH IS THE SENDING SITE'S OWN STAMP AND IS NEVER RE-DERIVED (see TxParams::seq):
        //    a non-matching seq is a SUPERSEDED flight whose RTS expired after the flight moved on — it is reported
        //    to telemetry and does NOTHING else, so it can neither fail the CURRENT flight (a wrong-flight failure)
        //    nor add a second failure for one already given up. Zero or one, never two.
        case TxOutcomeKind::expired: {
            MR_EMIT("tx_expired", EF_I("tag", info.tag), EF_I("seq", info.seq), EF_I("sf", info.sf));
            if (info.seq == 0 || !_active->_pending_tx
                || _active->_pending_tx->flight_gen != info.seq) return;   // superseded/unowned: report only
            const uint8_t  dst = _active->_pending_tx->dst;
            const uint16_t ctr = _active->_pending_tx->ctr;
            MR_EMIT("send_giveup", EF_I("origin", _active->_pending_tx->origin), EF_I("dst", dst), EF_I("ctr", ctr),
                    EF_S("reason", "gateway_unreachable_timeout"), EF_I("age_ms", 0));
            giveup_flight(SendFailReason::gateway_unreachable, dst, ctr);
            return;
        }
        case TxOutcomeKind::refused:
            break;
    }
    const FrameTag tag = frame_tag_of(info.tag);   // §B186a: mask the mobile-op high byte — the low byte is verbatim what this line used to cast
    MR_EMIT("radio_busy",EF_I("reason",info.reason),EF_I("busy_until_ms",info.busy_until_ms));
    // ★★★★ §B186a 2026-08-12 — THE ASYNCHRONOUS REFUSAL REPORT, and it is the ONE gap this slice closes.
    // `radio_busy` above carries neither a frame identity nor an SF, and for a `FrameTag::beacon` frame the function
    // RETURNS a few lines below with no retry and no further emit — so a mobile DISCOVER/OFFER/CLAIM/re-CLAIM that
    // the radio accepted and then refused was UNATTRIBUTABLE from firmware telemetry ([[B183]] §7: seven counters
    // read 0 while 71 J frames died). The four facts `BusyInfo` already carries are now reported together:
    // the OPERATION (from the tag the sending site stamped), the REASON, the SF and `busy_until_ms`.
    // ⛔ THIS IS OBSERVABILITY, NOT RECOVERY: nothing below changes, no retry is armed, no beacon is re-sent
    //    ([[B186b]] is not implemented here). ⛔ And the operation is READ, never DERIVED FROM FSM STATE: by now the
    //    attachment FSM may have moved on, so a reconstruction would be a false attribution wearing a diagnostic's
    //    name — the defect class this arc keeps paying for.
    // ⚠ THE WHOLE BLOCK IS INSIDE `MR_TELEMETRY`, DELIBERATELY, AND [[B169]] IS WHY: `op` and the two name lookups
    //   exist ONLY to fill the event, so on a `MESHROUTE_NO_TELEMETRY` board build they must vanish WITH it —
    //   otherwise `op` is a set-but-unused variable on all ten board envs, invisible to native and to all 36 corpus
    //   streams by construction. (Same idiom, same reason, as the `parse_j` read-back at node_mac.cpp's
    //   `mobile_offer_tx`.) ⓘ No length field is reported: `BusyInfo` does not carry one and none was added.
    MR_TELEMETRY( const MobileTxOp op = mobile_op_of_tag(info.tag);
                  if (op != MobileTxOp::none)
                      MR_EMIT("mobile_tx_refused", EF_S("op", mobile_tx_op_name(op)),
                              EF_I("reason", static_cast<int64_t>(info.reason)),
                              EF_S("reason_name", busy_reason_name(info.reason)),
                              EF_I("sf", info.sf),
                              EF_I("busy_until_ms", static_cast<int64_t>(info.busy_until_ms))); );

    if (tag == FrameTag::rts && _active->_pending_tx) {                      // RTS blocked: rts_timeout retries (dv:12089)
        // PORT DIVERGENCE (deliberate): Lua dv:12091 clears awaiting_cts here, but Lua's rts_timeout_fire does NOT
        // gate on it (it captures ctr_lo in the timer closure). OUR rts_timeout_fire uses awaiting_cts AS the
        // staleness key (the fixed timer id can't carry ctr_lo), so clearing it makes the already-armed timeout bail
        // -> the blocked RTS would never retry (carol stranded on r7_lbt_busy_diff). The RTS never hit the air, so
        // the node legitimately still awaits a CTS that won't come; leaving awaiting_cts=true lets the armed
        // rts_timeout fire + re-RTS, matching Lua's NET behaviour. Every other awaiting_cts=false transition cancels
        // kRtsTimeoutTimerId first (handle_cts:173, handle_nack:389), so the guard stays sound for those paths.
        MR_EMIT("rts_tx_blocked",EF_I("next",_active->_pending_tx->next),EF_I("ctr",_active->_pending_tx->ctr));
    }
    if (tag == FrameTag::data && _active->_pending_tx) {                     // DATA blocked: stash retry re-issues (dv:12109)
        _active->_pending_tx->awaiting_ack = false;
        _hal.cancel(kAckTimeoutTimerId);
        MR_EMIT("data_tx_blocked",EF_I("next",_active->_pending_tx->next),EF_I("ctr",_active->_pending_tx->ctr));
    }
    const int slot = retry_slot_of(tag);
    if (slot < 0) return;                                          // RTS/beacon: not stash-retried
    TxStashSlot& s = _tx_stash[slot];
    if (!s.valid) return;                                          // stash cleared by a newer same-tag TX
    if (s.retries_left == 0) {                                     // exhausted -> give up (dv:12190)
        MR_EMIT("tx_giveup", EF_I("tag", info.tag));
        s.valid = false;
        // SHARED-BUG FIX (#1, both engines): a DATA giveup STRANDS the flight — the DATA branch above cleared
        // awaiting_ack + cancelled the ack-timeout (and rts_timeout is moot), so _active->_pending_tx would sit forever with
        // no recovery timer and become_free() is blocked behind it -> the whole TX queue stalls. Release the flight
        // (mirror the DATA-M giveup, dv:12151) so the queue drains. Only DATA: a CTS/ACK/NACK giveup is a
        // receiver-side response whose pending_rx is freed by pending_rx_expiry; _active->_pending_tx may be unrelated.
        if (tag == FrameTag::data && _active->_pending_tx && _active->_pending_tx->flight_gen == s.flight_gen) {   // L9: exact flight match (was the 4-bit ctr_lo)
            // ★★★ [[B268]] blocker-1, A **SIXTH SITE** THIS SWEEP FOUND: this releases a stranded DATA flight and
            //     reports NOTHING to anybody. ⛔ `generic_owed = false` PRESERVES that silence byte-for-byte —
            //     inventing a generic failure here is a behaviour change outside this slice. The grant's own arm
            //     fires, so a grant stranded by an LBT-stash give-up no longer freezes the panel.
            terminal_carrier_outcome(_active->_pending_tx->type, !_active->_pending_tx->has_previous_hop,
                                     /*generic_owed=*/false, SendFailReason::none,
                                     _active->_pending_tx->dst, _active->_pending_tx->ctr);
            _active->_pending_tx.reset();
            become_free();
        }
        return;
    }
    --s.retries_left;
    s.reissue_pending = true;                                         // a busy re-issue timer is now armed (gates the gateway layer swap)
    const uint64_t now  = _hal.now();
    const uint64_t wait = (info.busy_until_ms > now) ? (info.busy_until_ms - now) : 0;
    const uint32_t delay = static_cast<uint32_t>(wait) + 2 +                                  // +2 guard (dv:12204)
                           static_cast<uint32_t>(_hal.rand_range(0, static_cast<int>(_lbt_backoff_ms) + 1));   // DRAW
    (void)_hal.after(delay, kRadioBusyRetryTimerId + slot);
}
// SX1262 PreambleDetected IRQ: the channel is busy with someone at our SF NOW, even if the packet
// won't decode. Feeds the throttle's channel-busy witness so beacon_fire's quiet check sees real
// activity, not the decode-success-biased view (dv:12219-12232). Pure timestamp, no rand.
void Node::on_preamble_detected(uint64_t time_ms)  { _last_rx_routing_sf_ms = time_ms; }

}  // namespace meshroute
