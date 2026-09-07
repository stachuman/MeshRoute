// MeshRoute — lib/core/remote_session.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — the target's authenticated session, admission, bootstrap and expiry.
//     The contract, the fence and every "what this deliberately does not do" live in remote_session.h; this
//     file holds the decisions and the arithmetic. See that header FIRST.
#include "remote_session.h"

#include "frame_codec.h"    // DATA_TYPE_REMOTE_CMD / DATA_TYPE_REMOTE_RESP — the real outer type bytes
#include "identity.h"       // Identity + ed_pub_to_x25519 — the EXISTING conversion, never a second one
#include "monocypher.h"     // crypto_wipe — every transient below leaves through it

#include <cstring>          // memcmp/memcpy — the exact tag compare and the ONE owned-copy path

namespace MESHROUTE_NS {

// =========================================================================================================
// §4.2 — THE LAYOUT IS ASSERTED, NOT DESCRIBED. These fire on native, ARM and Xtensa alike, so a field edit
// that forgets a byte cannot silently keep the header's numbers. The offsets are the brief's, verbatim.
// =========================================================================================================
static_assert(sizeof(AdminAclRow) == 34 && alignof(AdminAclRow) == 1, "AdminAclRow: 34/1");
static_assert(offsetof(AdminAclRow, ed_pub) == 0 && offsetof(AdminAclRow, role) == 32
              && offsetof(AdminAclRow, reserved) == 33, "AdminAclRow offsets");

static_assert(sizeof(ReplyRoute) == 8 && alignof(ReplyRoute) == 1, "ReplyRoute: 8/1");
static_assert(offsetof(ReplyRoute, layer_ids) == 0 && offsetof(ReplyRoute, n_layers) == 4
              && offsetof(ReplyRoute, cur) == 5 && offsetof(ReplyRoute, carrier) == 6
              && offsetof(ReplyRoute, origin) == 7, "ReplyRoute offsets");

static_assert(sizeof(SeenRequestRecord) == 48 && alignof(SeenRequestRecord) == 8, "SeenRequestRecord: 48/8");
static_assert(offsetof(SeenRequestRecord, request_id) == 0 && offsetof(SeenRequestRecord, admin_epoch) == 8
              && offsetof(SeenRequestRecord, first_seen_ms) == 16 && offsetof(SeenRequestRecord, source_hash) == 20
              && offsetof(SeenRequestRecord, request_tag) == 24 && offsetof(SeenRequestRecord, controller_slot) == 40
              && offsetof(SeenRequestRecord, result_code) == 41 && offsetof(SeenRequestRecord, state) == 42
              && offsetof(SeenRequestRecord, transcript_slot) == 43 && offsetof(SeenRequestRecord, reserved) == 44,
              "SeenRequestRecord offsets");
// ★ THE 16 BYTES ARE THE POINT: the 0e candidate row was 32 B and omitted the request FINGERPRINT design §10
//   requires. 32 + 16 = 48, and the tag is a member rather than a recomputed value.
static_assert(sizeof(SeenRequestRecord::request_tag) == kRemoteTagBytes,
              "the seen fingerprint IS the received Poly1305 tag — 16 bytes, not a digest of our own");

static_assert(sizeof(SeenEntry) == 56 && alignof(SeenEntry) == 8, "SeenEntry: 56/8");
static_assert(offsetof(SeenEntry, record) == 0 && offsetof(SeenEntry, route) == 48, "SeenEntry offsets");

static_assert(sizeof(IngressOperationHeader) == 40 && alignof(IngressOperationHeader) == 8,
              "IngressOperationHeader: 40/8");
static_assert(offsetof(IngressOperationHeader, request_id) == 0
              && offsetof(IngressOperationHeader, expires_at_ms) == 8
              && offsetof(IngressOperationHeader, source_hash) == 16
              && offsetof(IngressOperationHeader, body_len) == 20
              && offsetof(IngressOperationHeader, seen_index) == 22
              && offsetof(IngressOperationHeader, ctl) == 23
              && offsetof(IngressOperationHeader, route) == 24
              && offsetof(IngressOperationHeader, partition) == 32
              && offsetof(IngressOperationHeader, state) == 33
              && offsetof(IngressOperationHeader, body_slot) == 34
              && offsetof(IngressOperationHeader, reserved) == 35, "IngressOperationHeader offsets");

static_assert(sizeof(IngressBodySlot) == 236 && alignof(IngressBodySlot) == 2, "IngressBodySlot: 236/2");
static_assert(offsetof(IngressBodySlot, bytes) == 0 && offsetof(IngressBodySlot, reserved) == 233
              && offsetof(IngressBodySlot, len) == 234, "IngressBodySlot offsets");

static_assert(sizeof(OpenStagingSlot) == 32 && alignof(OpenStagingSlot) == 8, "OpenStagingSlot: 32/8");
static_assert(offsetof(OpenStagingSlot, request_id) == 0 && offsetof(OpenStagingSlot, expires_at_ms) == 8
              && offsetof(OpenStagingSlot, peer_source_hash) == 16 && offsetof(OpenStagingSlot, route) == 20
              && offsetof(OpenStagingSlot, kind) == 28 && offsetof(OpenStagingSlot, controller_slot) == 29
              && offsetof(OpenStagingSlot, reserved) == 30, "OpenStagingSlot offsets");

// ★★★ THE BLOCK ITSELF — 2 064 bytes, and every one of them attributed:
//       pair 64 + ACL 340 + status 4 + epochs 80 + seen 896 + headers 80 + bodies 472 + staging 128.
//     ⛔ There is NO tail padding (2 064 % 8 == 0), so `sizeof` is exactly the sum and the Node re-pin
//        arithmetic closes without an unexplained remainder.
static_assert(sizeof(RemoteSessionState) == 2064 && alignof(RemoteSessionState) == 8,
              "RemoteSessionState: the 2064-byte / 8-aligned ACCEPT block moved — re-derive the Node re-pin");
static_assert(offsetof(RemoteSessionState, admin_x_secret) == 0
              && offsetof(RemoteSessionState, admin_ed_pub) == 32
              && offsetof(RemoteSessionState, acl) == 64
              && offsetof(RemoteSessionState, readiness) == 404
              && offsetof(RemoteSessionState, acl_occupied) == 405
              && offsetof(RemoteSessionState, root_present) == 406
              && offsetof(RemoteSessionState, reserved0) == 407
              && offsetof(RemoteSessionState, epoch) == 408
              && offsetof(RemoteSessionState, seen) == 488
              && offsetof(RemoteSessionState, ingress) == 1384
              && offsetof(RemoteSessionState, body) == 1464
              && offsetof(RemoteSessionState, staging) == 1936, "RemoteSessionState offsets");
static_assert(sizeof(RemoteSessionState::acl) == 340 && sizeof(RemoteSessionState::epoch) == 80
              && sizeof(RemoteSessionState::seen) == 896 && sizeof(RemoteSessionState::ingress) == 80
              && sizeof(RemoteSessionState::body) == 472 && sizeof(RemoteSessionState::staging) == 128,
              "RemoteSessionState array sizes");
// The partition constants must index the arrays they name, and the bootstrap row must sit OUTSIDE the open range.
static_assert(kRadminIngressGeneral < kRadminIngressSlots && kRadminIngressControl < kRadminIngressSlots
              && kRadminIngressGeneral != kRadminIngressControl, "the two ingress partitions must be distinct rows");
static_assert(kRadminOpenSlots + 1 == kRadminStagingSlots && kRadminBootstrapSlot == kRadminOpenSlots,
              "staging is THREE open + ONE bootstrap — the bootstrap row is never inside the open range");
// The body array must hold the largest RPC body ANY legal carrier can present, so storage can never be the
// thing that refuses a legal request (the cap authority is `remote_body_cap`, never this number).
static_assert(kRadminBodyBytes >= protocol::max_payload_bytes_hard_cap
                                  - protocol::dm_inner_origin_bytes
                                  - protocol::dm_inner_source_hash_bytes
                                  - protocol::dm_inner_dst_hash_bytes,
              "the ingress body array is smaller than the largest carrier-admissible RPC body");

namespace {

// ---------------------------------------------------------------------------------------------------------
// ★ THE ONE ECDH ADAPTER (brief §3). `remote_ecdh_shared` takes an `Identity&`, and `ecdh_shared`
//   (`identity.cpp:47`) reads EXACTLY ONE field of it: `self.x_secret`. This node holds the administration
//   root as 32 RESIDENT bytes rather than as a 196-byte expanded `Identity` (§4.1's residency decision), so
//   the secret is presented to the EXISTING primitive through this transient and wiped on every exit.
// ⛔ IT IS NOT A SECOND ECDH OR KDF IMPLEMENTATION, and ⛔ it is not a resident `Identity`: the object lives
//   for one derivation and its destructor scrubs all 196 bytes.
// ---------------------------------------------------------------------------------------------------------
struct AdminEcdhTransient {
    Identity id{};
    explicit AdminEcdhTransient(const uint8_t x_secret[32]) {
        for (int i = 0; i < 32; ++i) id.x_secret[i] = x_secret[i];
    }
    ~AdminEcdhTransient() { crypto_wipe(&id, sizeof id); }
    AdminEcdhTransient(const AdminEcdhTransient&) = delete;
    AdminEcdhTransient& operator=(const AdminEcdhTransient&) = delete;
};

// Derive the BASE key for one live ACL row. `out_base32` is written only on success.
// ⛔ Bound to BOTH ORDERED FULL identities (controller first, this target's administration root second) —
//    never to a sorted pair and never to a 32-bit routing hash. Every low-order controller point is refused
//    inside `remote_ecdh_shared` BEFORE a key exists.
[[nodiscard]] RemoteStatus derive_base(const RemoteSessionState& s, const uint8_t controller_ed_pub[32],
                                       uint8_t out_base32[32]) {
    uint8_t peer_x[32] = {};
    ed_pub_to_x25519(peer_x, controller_ed_pub);         // the EXISTING conversion (identity.cpp), never a fork
    uint8_t shared[32] = {};
    RemoteStatus st;
    {
        AdminEcdhTransient self(s.admin_x_secret);
        st = remote_ecdh_shared(shared, self.id, peer_x);
    }
    crypto_wipe(peer_x, sizeof peer_x);
    if (st != RemoteStatus::ok) { crypto_wipe(shared, sizeof shared); return st; }
    st = remote_kdf_base(out_base32, shared, controller_ed_pub, s.admin_ed_pub);
    crypto_wipe(shared, sizeof shared);
    return st;
}

[[nodiscard]] bool row_occupied(const AdminAclRow& r) {
    return r.role == kRadminRoleOperator || r.role == kRadminRoleOwner;
}

// ★ Exact 32-byte compare, no early exit — the `admin_buf_all_zero` / `e2e_seal_inner` idiom. A full-key
//   lookup is a TRUST decision, so it must not leak a match position through timing.
[[nodiscard]] bool key_equal32(const uint8_t a[32], const uint8_t b[32]) {
    uint8_t acc = 0;
    for (int i = 0; i < 32; ++i) acc = static_cast<uint8_t>(acc | (a[i] ^ b[i]));
    return acc == 0;
}
[[nodiscard]] bool tag_equal(const uint8_t a[16], const uint8_t b[16]) {
    uint8_t acc = 0;
    for (int i = 0; i < 16; ++i) acc = static_cast<uint8_t>(acc | (a[i] ^ b[i]));
    return acc == 0;
}

// The BOOTSTRAP discovery lookup: exact across the ten LIVE rows. kRadminNoSlot when no row holds that key.
[[nodiscard]] uint8_t acl_find_full_key(const RemoteSessionState& s, const uint8_t ed_pub[32]) {
    uint8_t hit = kRadminNoSlot;
    for (uint8_t i = 0; i < kRadminAclSlots; ++i)
        if (row_occupied(s.acl[i]) && key_equal32(s.acl[i].ed_pub, ed_pub) && hit == kRadminNoSlot) hit = i;
    return hit;
}

[[nodiscard]] uint8_t seen_free_index(const RemoteSessionState& s) {
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i)
        if (s.seen[i].record.state == static_cast<uint8_t>(SeenState::free)) return i;
    return kRadminNoSlot;
}
// ⛔ THE PARTITION IS HARD. A general request never looks at the control row and a control request never
//    looks at the general one — that is what makes both starvation directions impossible.
[[nodiscard]] uint8_t ingress_free_index(const RemoteSessionState& s, uint8_t partition) {
    if (partition >= kRadminIngressSlots) return kRadminNoSlot;
    return s.ingress[partition].state == static_cast<uint8_t>(IngressState::free) ? partition : kRadminNoSlot;
}

void ingress_release(RemoteSessionState& s, uint8_t i) {
    if (i >= kRadminIngressSlots) return;
    const uint8_t slot = s.ingress[i].body_slot;
    if (slot < kRadminIngressSlots) {
        crypto_wipe(s.body[slot].bytes, sizeof s.body[slot].bytes);   // ⛔ plaintext never outlives its row
        s.body[slot] = IngressBodySlot{};
    }
    s.ingress[i] = IngressOperationHeader{};
    s.ingress[i].seen_index = kRadminNoSlot;
    s.ingress[i].body_slot  = kRadminNoSlot;
}

void staging_release(RemoteSessionState& s, uint8_t i) {
    if (i >= kRadminStagingSlots) return;
    s.staging[i] = OpenStagingSlot{};
    s.staging[i].controller_slot = kRadminNoSlot;
}

// ★★ INVALIDATE ONE SLOT — and this is the ONLY thing that may clear a seen row. Time never does it, an ACK
//    never does it, and a full pool never does it (design §10: "session records are not silently evicted
//    while their session key remains valid"). It runs AFTER durability, on the affected slot alone.
void invalidate_slot(RemoteSessionState& s, uint8_t slot) {
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i)
        if (s.seen[i].record.state != static_cast<uint8_t>(SeenState::free)
            && s.seen[i].record.controller_slot == slot) {
            crypto_wipe(&s.seen[i], sizeof s.seen[i]);                // the retained TAG is secret-adjacent
            s.seen[i] = SeenEntry{};
            s.seen[i].record.transcript_slot = kRadminNoTranscript;
        }
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i)
        if (s.ingress[i].state != static_cast<uint8_t>(IngressState::free)
            && static_cast<uint8_t>(s.ingress[i].ctl & 0x0F) == slot) ingress_release(s, i);
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i)
        if (s.staging[i].kind == static_cast<uint8_t>(OpenStagingKind::bootstrap)
            && s.staging[i].controller_slot == slot) staging_release(s, i);
}

void invalidate_everything(RemoteSessionState& s) {
    crypto_wipe(s.seen, sizeof s.seen);
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i) {
        s.seen[i] = SeenEntry{};
        s.seen[i].record.transcript_slot = kRadminNoTranscript;
    }
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i)  ingress_release(s, i);
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i)  staging_release(s, i);
}

// Readiness is a CONJUNCTION recomputed from the installed images — never a stored promise. A node is
// accepting only with a root, at least one occupied row, and a NON-ZERO epoch on every occupied row.
// ⛔ Epoch 0 is never in service (§4.1); a slot holding it is unusable, and if it is the only occupied row
//    the node is `disabled`, not "ready with a dead slot".
void recompute_readiness(RemoteSessionState& s) {
    uint8_t occupied = 0;
    bool    usable   = true;
    for (uint8_t i = 0; i < kRadminAclSlots; ++i) {
        if (!row_occupied(s.acl[i])) continue;
        ++occupied;
        if (s.epoch[i] == 0) usable = false;
    }
    s.acl_occupied = occupied;
    const bool ready = s.root_present != 0 && occupied > 0 && usable;
    s.readiness = static_cast<uint8_t>(ready ? AdminReadiness::ready : AdminReadiness::disabled);
}

}  // namespace

// =========================================================================================================
// Install / teardown
// =========================================================================================================
void remote_session_clear(RemoteSessionState& s) {
    crypto_wipe(&s, sizeof s);                       // the SECRET half first, unconditionally
    s = RemoteSessionState{};
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i)   s.seen[i].record.transcript_slot = kRadminNoTranscript;
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i) {
        s.ingress[i].seen_index = kRadminNoSlot;
        s.ingress[i].body_slot  = kRadminNoSlot;
    }
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i) s.staging[i].controller_slot = kRadminNoSlot;
    s.readiness = static_cast<uint8_t>(AdminReadiness::disabled);
}

void remote_session_mark_entropy_failed(RemoteSessionState& s) {
    remote_session_clear(s);
    s.readiness = static_cast<uint8_t>(AdminReadiness::entropy_failed);
}

void remote_session_install(RemoteSessionState& s, const RemoteSessionInstall& plan) {
    // ⛔ A ROOT CHANGE KILLS EVERY OLD SESSION. It is forced here rather than trusted to the caller's flag:
    //    the base key is bound to this target's administration public key, so every previously derived
    //    session key is dead the instant the pair moves — leaving a seen row keyed to it would be a lie.
    //    The ACL is PRESERVED (design §6.5): the controllers keep their rows and re-pin the new fingerprint.
    const bool root_moved = plan.set_root || plan.clear_root;
    if (plan.clear_root) {
        crypto_wipe(s.admin_x_secret, sizeof s.admin_x_secret);
        for (int i = 0; i < 32; ++i) { s.admin_x_secret[i] = 0; s.admin_ed_pub[i] = 0; }
        s.root_present = 0;
    } else if (plan.set_root) {
        for (int i = 0; i < 32; ++i) { s.admin_x_secret[i] = plan.x_secret[i]; s.admin_ed_pub[i] = plan.ed_pub[i]; }
        s.root_present = 1;
    }
    if (plan.set_acl) {
        for (uint8_t i = 0; i < kRadminAclSlots; ++i) s.acl[i] = plan.acl[i];
        // ⛔ PER SLOT, AND ONLY WHERE THE CALLER PREPARED ONE. An unchanged slot keeps its epoch EXACTLY and
        //    keeps its work; a changed/added occupied slot gets its prepared fresh non-zero epoch; a removed
        //    slot gets 0 and becomes unusable. A same-value mutation prepares nothing and therefore moves nothing.
        for (uint8_t i = 0; i < kRadminAclSlots; ++i) {
            if (!plan.epoch_set[i]) continue;
            s.epoch[i] = plan.epoch[i];
            invalidate_slot(s, i);
        }
    } else {
        for (uint8_t i = 0; i < kRadminAclSlots; ++i)
            if (plan.epoch_set[i]) { s.epoch[i] = plan.epoch[i]; invalidate_slot(s, i); }
    }
    if (root_moved || plan.invalidate_all) invalidate_everything(s);
    recompute_readiness(s);
}

// =========================================================================================================
// Expiry — §4.5's ONE shared earliest-deadline scan. `Node` owns the single wheel id; this owns the rows.
// =========================================================================================================
uint64_t remote_session_deadline(uint64_t now_ms, uint32_t lifetime_ms) {
    const uint64_t add = static_cast<uint64_t>(lifetime_ms);
    // ⛔ CHECKED AT THE EDGE, not hoped for: a saturating add can never produce a deadline in the past.
    return (now_ms > (~uint64_t{0}) - add) ? ~uint64_t{0} : now_ms + add;
}

uint64_t remote_session_earliest_deadline(const RemoteSessionState& s) {
    uint64_t earliest = ~uint64_t{0};
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i)
        if (s.ingress[i].state != static_cast<uint8_t>(IngressState::free)
            && s.ingress[i].expires_at_ms < earliest) earliest = s.ingress[i].expires_at_ms;
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i)
        if (s.staging[i].kind != static_cast<uint8_t>(OpenStagingKind::free)
            && s.staging[i].expires_at_ms < earliest) earliest = s.staging[i].expires_at_ms;
    return earliest;
}

uint8_t remote_session_expire(RemoteSessionState& s, uint64_t now_ms) {
    uint8_t released = 0;
    // ★ `now >= expires_at` — the boundary EXPIRES. Just below it does not.
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i)
        if (s.ingress[i].state != static_cast<uint8_t>(IngressState::free)
            && now_ms >= s.ingress[i].expires_at_ms) { ingress_release(s, i); ++released; }
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i)
        if (s.staging[i].kind != static_cast<uint8_t>(OpenStagingKind::free)
            && now_ms >= s.staging[i].expires_at_ms) { staging_release(s, i); ++released; }
    // ⛔⛔ AND NOTHING ABOVE TOUCHED A SEEN ROW. Expiry releases SCRATCH. The same-epoch fingerprint is the
    //    tombstone §10 case 4 answers from, and only an epoch-invalidating change may clear it.
    return released;
}

void remote_session_staging_release(RemoteSessionState& s, uint8_t index) { staging_release(s, index); }

// =========================================================================================================
// Read-only introspection
// =========================================================================================================
uint8_t remote_session_seen_used(const RemoteSessionState& s) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i)
        if (s.seen[i].record.state != static_cast<uint8_t>(SeenState::free)) ++n;
    return n;
}
uint8_t remote_session_ingress_used(const RemoteSessionState& s) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kRadminIngressSlots; ++i)
        if (s.ingress[i].state != static_cast<uint8_t>(IngressState::free)) ++n;
    return n;
}
uint8_t remote_session_staging_used(const RemoteSessionState& s) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kRadminStagingSlots; ++i)
        if (s.staging[i].kind != static_cast<uint8_t>(OpenStagingKind::free)) ++n;
    return n;
}
uint8_t remote_session_seen_find(const RemoteSessionState& s, uint8_t slot, uint64_t request_id) {
    for (uint8_t i = 0; i < kRadminSeenSlots; ++i) {
        const SeenRequestRecord& r = s.seen[i].record;
        if (r.state == static_cast<uint8_t>(SeenState::free)) continue;
        if (r.controller_slot == slot && r.request_id == request_id) return i;
    }
    return kRadminNoSlot;
}

bool remote_admit_is_silent(RemoteAdmitVerdict v) {
    switch (v) {
        case RemoteAdmitVerdict::silent_not_ready:
        case RemoteAdmitVerdict::silent_no_source:
        case RemoteAdmitVerdict::silent_bad_carrier:
        case RemoteAdmitVerdict::silent_bad_request:
        case RemoteAdmitVerdict::silent_over_cap:
        case RemoteAdmitVerdict::silent_no_acl_row:
        case RemoteAdmitVerdict::silent_bad_key:
        case RemoteAdmitVerdict::silent_auth_failed:
        case RemoteAdmitVerdict::silent_reply_unbuildable:
            return true;
        case RemoteAdmitVerdict::admit:
        case RemoteAdmitVerdict::replay_transcript:
        case RemoteAdmitVerdict::reject_id_reuse:
        case RemoteAdmitVerdict::already_acknowledged:
        case RemoteAdmitVerdict::session_full:
        case RemoteAdmitVerdict::bootstrap_answered:
        case RemoteAdmitVerdict::control_admitted:
        case RemoteAdmitVerdict::ingress_full:
        case RemoteAdmitVerdict::open_staged:
        case RemoteAdmitVerdict::open_staging_full:
        case RemoteAdmitVerdict::bootstrap_staging_busy:
        case RemoteAdmitVerdict::open_peer_bound:
            break;
    }
    return false;
}

const char* remote_admit_name(RemoteAdmitVerdict v) {
    switch (v) {
        case RemoteAdmitVerdict::admit:                   return "admit";
        case RemoteAdmitVerdict::replay_transcript:       return "replay_transcript";
        case RemoteAdmitVerdict::reject_id_reuse:         return "reject_id_reuse";
        case RemoteAdmitVerdict::already_acknowledged:    return "already_acknowledged";
        case RemoteAdmitVerdict::session_full:            return "session_full";
        case RemoteAdmitVerdict::bootstrap_answered:      return "bootstrap_answered";
        case RemoteAdmitVerdict::control_admitted:        return "control_admitted";
        case RemoteAdmitVerdict::ingress_full:            return "ingress_full";
        case RemoteAdmitVerdict::open_staged:             return "open_staged";
        case RemoteAdmitVerdict::open_staging_full:       return "open_staging_full";
        case RemoteAdmitVerdict::bootstrap_staging_busy:  return "bootstrap_staging_busy";
        case RemoteAdmitVerdict::open_peer_bound:         return "open_peer_bound";
        case RemoteAdmitVerdict::silent_not_ready:        return "silent_not_ready";
        case RemoteAdmitVerdict::silent_no_source:        return "silent_no_source";
        case RemoteAdmitVerdict::silent_bad_carrier:      return "silent_bad_carrier";
        case RemoteAdmitVerdict::silent_bad_request:      return "silent_bad_request";
        case RemoteAdmitVerdict::silent_over_cap:         return "silent_over_cap";
        case RemoteAdmitVerdict::silent_no_acl_row:       return "silent_no_acl_row";
        case RemoteAdmitVerdict::silent_bad_key:          return "silent_bad_key";
        case RemoteAdmitVerdict::silent_auth_failed:      return "silent_auth_failed";
        case RemoteAdmitVerdict::silent_reply_unbuildable:return "silent_reply_unbuildable";
    }
    return "unknown";
}

// =========================================================================================================
// THE RECEIVE PATH
// =========================================================================================================
namespace {

// Commit one admitted execute: the seen row and the ingress reservation, TOGETHER. Both indices were found
// free before this is called, so it cannot half-succeed.
void commit_admit(RemoteSessionState& s, const RemoteRxInput& in, const RemoteDecoded& d,
                  uint8_t slot, const uint8_t tag[16], uint8_t si, uint8_t ii, RemoteRxResult& out) {
    SeenEntry& e = s.seen[si];
    e = SeenEntry{};
    e.record.request_id      = d.msg.request_id;
    e.record.admin_epoch     = s.epoch[slot];
    e.record.first_seen_ms   = static_cast<uint32_t>(in.now_ms & 0xFFFFFFFFu);
    e.record.source_hash     = in.source.hash;
    for (int i = 0; i < 16; ++i) e.record.request_tag[i] = tag[i];
    e.record.controller_slot = slot;
    e.record.result_code     = 0;
    e.record.state           = static_cast<uint8_t>(SeenState::admitted);
    e.record.transcript_slot = kRadminNoTranscript;   // ⛔ Slice 7b allocates transcripts; this slice never does
    e.route                  = in.route;              // ★ the FIRST-admitted route, retained past ingress expiry

    IngressOperationHeader& h = s.ingress[ii];
    h = IngressOperationHeader{};
    h.request_id    = d.msg.request_id;
    h.expires_at_ms = remote_session_deadline(in.now_ms, radmin_staging_lifetime_ms);
    h.source_hash   = in.source.hash;
    h.body_len      = static_cast<uint16_t>(d.body.size());
    h.seen_index    = si;
    h.ctl           = in.body[0];
    h.route         = in.route;                       // the ingress's OWN copy for pending work
    h.partition     = ii;
    h.state         = static_cast<uint8_t>(IngressState::reserved);
    h.body_slot     = ii;

    IngressBodySlot& b = s.body[ii];
    b = IngressBodySlot{};
    // ⛔ AN OWNED COPY — never a span into PostAck, into the decode scratch or into mutable RX memory.
    //    The size was admitted by `remote_body_cap` before the decode, so this cannot overrun.
    for (size_t i = 0; i < d.body.size() && i < kRadminBodyBytes; ++i) b.bytes[i] = d.body[i];
    b.len = static_cast<uint16_t>(d.body.size());

    out.seen_index    = si;
    out.ingress_index = ii;
    out.route         = e.route;
    out.verdict       = RemoteAdmitVerdict::admit;
}

}  // namespace

void remote_session_receive(RemoteSessionState& s, const RemoteRxInput& in, RemoteRxResult& out) {
    out = RemoteRxResult{};
    out.source_hash = in.source.hash;

    // ---- the four pre-crypto gates, in order. Each costs ZERO state and reveals NOTHING. ----------------
    if (!remote_session_accepting(s))                { out.verdict = RemoteAdmitVerdict::silent_not_ready;  return; }
    // ★★ R-RA-13, AND IT IS THE FIRST OF SLICE 1b's FOUR DEFERRED OBLIGATIONS DISCHARGED HERE: the v2 arm
    //    REQUIRES a `SOURCE_HASH`. ⛔ It is never aliased to the 8-bit `pa.origin` (which repeats across
    //    leaves), and ⛔ "absent" is never spelled as the value 0 — presence and value are separate fields.
    if (!in.source.present)                          { out.verdict = RemoteAdmitVerdict::silent_no_source;  return; }
    size_t cap = 0;
    if (remote_body_cap(in.request_carrier, cap) != RemoteStatus::ok)
                                                     { out.verdict = RemoteAdmitVerdict::silent_bad_carrier;return; }
    if (in.body.empty())                             { out.verdict = RemoteAdmitVerdict::silent_bad_request;return; }
    // ★★ REFUSE, ⛔ NEVER CLAMP — Slice 1b's third deferred obligation. The old staging did
    //    `if (n > inbox_max_body) n = inbox_max_body`; an over-cap v2 body is rejected BEFORE any copy.
    if (in.body.size() > cap)                        { out.verdict = RemoteAdmitVerdict::silent_over_cap;   return; }

    // ---- ONLY the bounded syntax key selection needs, and nothing more, before authentication -----------
    RemoteLayout L{};
    if (remote_layout(in.outer_type, in.body[0], L) != RemoteStatus::ok)
                                                     { out.verdict = RemoteAdmitVerdict::silent_bad_request;return; }

    switch (L.domain) {
        // ===== §7.2 BOOTSTRAP — READ-ONLY. ⛔ No seen row, ⛔ no epoch change, ⛔ no dispatch. ============
        case RemoteDomainId::cmd_bootstrap: {
            // The bootstrap request is a FIXED layout, so its exact length is checked before the clear
            // controller key is read out of it. That read is the whole of the pre-auth syntax.
            if (in.body.size() != L.fixed_overhead)  { out.verdict = RemoteAdmitVerdict::silent_bad_request;return; }
            const uint8_t* ctrl_pub = in.body.data() + kRemoteCtlBytes + kRemoteRequestIdBytes;
            const uint8_t  slot     = acl_find_full_key(s, ctrl_pub);
            // ⛔ SILENT: §7.2 forbids revealing ACL membership, so an unknown key answers exactly as a bad
            //    tag does — nothing at all.
            if (slot == kRadminNoSlot)               { out.verdict = RemoteAdmitVerdict::silent_no_acl_row; return; }
            if (s.epoch[slot] == 0)                  { out.verdict = RemoteAdmitVerdict::silent_bad_key;    return; }

            uint8_t base[32] = {};
            RemoteStatus st = derive_base(s, s.acl[slot].ed_pub, base);
            if (st != RemoteStatus::ok) {
                crypto_wipe(base, sizeof base);
                out.verdict = RemoteAdmitVerdict::silent_bad_key;                                            return;
            }
            // ⚠ B313: a failed decode does NOT scrub the caller's buffer, so this one is wiped on EVERY exit.
            uint8_t pt[kRadminBodyBytes] = {};
            RemoteDecoded d{};
            const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
            st = remote_body_decode(d, in.outer_type, in.body, keys, in.source, in.request_carrier,
                                    std::span<uint8_t>(pt, sizeof pt));
            crypto_wipe(pt, sizeof pt);
            if (st != RemoteStatus::ok) {
                crypto_wipe(base, sizeof base);
                out.verdict = (st == RemoteStatus::auth_failed) ? RemoteAdmitVerdict::silent_auth_failed
                                                                : RemoteAdmitVerdict::silent_bad_request;    return;
            }
            // ---- AUTHENTICATED from here, and the reservation comes AFTER that fact. -------------------
            out.controller_slot = slot;
            out.request_id      = d.msg.request_id;
            out.route           = in.route;
            OpenStagingSlot& row = s.staging[kRadminBootstrapSlot];
            if (row.kind != static_cast<uint8_t>(OpenStagingKind::free)) {
                crypto_wipe(base, sizeof base);
                out.verdict = RemoteAdmitVerdict::bootstrap_staging_busy;                                     return;
            }
            row = OpenStagingSlot{};
            row.request_id       = d.msg.request_id;
            row.expires_at_ms    = remote_session_deadline(in.now_ms, radmin_staging_lifetime_ms);
            row.peer_source_hash = in.source.hash;
            row.route            = in.route;
            row.kind             = static_cast<uint8_t>(OpenStagingKind::bootstrap);
            row.controller_slot  = slot;
            out.staging_index    = kRadminBootstrapSlot;

            // The ONE thing Slice 5 puts on air. `[ctl(slot)][request_id][admin_epoch][tag]` under the BASE
            // key, at the MATCHED slot and this node's CURRENT epoch. ⛔ The epoch is READ, never advanced.
            // ★ The crypto `RemoteSource` stays the ORIGINAL CONTROLLER's in BOTH directions: it is the
            //   controller-domain identity the AAD and the nonce bind. ⛔ It is NOT the physical response
            //   carrier's own `SOURCE_HASH` (which will be this target's routing identity) — different facts.
            RemoteMessage m{};
            m.outer_type  = DATA_TYPE_REMOTE_RESP;
            m.opcode      = static_cast<uint8_t>(RemoteRespOpcode::bootstrap);
            m.slot        = slot;
            m.request_id  = d.msg.request_id;
            m.admin_epoch = s.epoch[slot];
            size_t rlen = 0;
            st = remote_body_encode(std::span<uint8_t>(out.reply, sizeof out.reply), rlen, m,
                                    std::span<const uint8_t>{}, keys, in.source, in.reply_carrier);
            crypto_wipe(base, sizeof base);
            if (st != RemoteStatus::ok) {
                // The return leg cannot carry the answer (an unusable reversed carrier shape). Release the
                // reservation LOUDLY rather than leaving it held for a response that will never be built.
                staging_release(s, kRadminBootstrapSlot);
                out.staging_index = kRadminNoSlot;
                for (size_t i = 0; i < sizeof out.reply; ++i) out.reply[i] = 0;
                out.verdict = RemoteAdmitVerdict::silent_reply_unbuildable;                                   return;
            }
            out.has_reply = true;
            out.reply_len = static_cast<uint8_t>(rlen);
            out.verdict   = RemoteAdmitVerdict::bootstrap_answered;
            return;
        }

        // ===== §8.3 OPEN EXECUTE — UNAUTHENTICATED. Storage admission only. ================================
        // ⓘ MARK DONE-VS-MISSING IN CODE: what is DONE here is the bounded RESERVATION (three rows, at most
        //   one per source, expiring on the shared timer). What is MISSING BY DESIGN is everything that
        //   would make it useful — ⛔ no verb validation (`status`/`routes` are the only ones policy will
        //   accept), ⛔ no rate policy, ⛔ no dispatcher and ⛔ no response. Those are Slices 6/7b's, and
        //   this narrow admission is deliberately NOT the common command validator.
        case RemoteDomainId::cmd_open_execute: {
            RemoteDecoded d{};
            const RemoteKeys none{};
            uint8_t pt[1] = {0};
            const RemoteStatus st = remote_body_decode(d, in.outer_type, in.body, none, in.source,
                                                       in.request_carrier, std::span<uint8_t>(pt, 0));
            if (st != RemoteStatus::ok)              { out.verdict = RemoteAdmitVerdict::silent_bad_request;  return; }
            out.request_id = d.msg.request_id;
            // ★ AT MOST ONE ACTIVE OPEN ROW PER SOURCE, and a changed request id does not evade it: the bound
            //   is on the SOURCE, not on the operation.
            for (uint8_t i = 0; i < kRadminOpenSlots; ++i)
                if (s.staging[i].kind == static_cast<uint8_t>(OpenStagingKind::open_execute)
                    && s.staging[i].peer_source_hash == in.source.hash)
                                                     { out.verdict = RemoteAdmitVerdict::open_peer_bound;     return; }
            uint8_t idx = kRadminNoSlot;
            for (uint8_t i = 0; i < kRadminOpenSlots; ++i)
                if (s.staging[i].kind == static_cast<uint8_t>(OpenStagingKind::free)) { idx = i; break; }
            // ⛔ THE BOOTSTRAP ROW IS NEVER LENT. Three open rows is three, even with staging[3] free.
            if (idx == kRadminNoSlot)                { out.verdict = RemoteAdmitVerdict::open_staging_full;   return; }
            OpenStagingSlot& row = s.staging[idx];
            row = OpenStagingSlot{};
            row.request_id       = d.msg.request_id;
            row.expires_at_ms    = remote_session_deadline(in.now_ms, radmin_staging_lifetime_ms);
            row.peer_source_hash = in.source.hash;
            row.route            = in.route;
            row.kind             = static_cast<uint8_t>(OpenStagingKind::open_execute);
            row.controller_slot  = kRadminNoSlot;    // ⛔ an OPEN request claims no slot and authenticates none
            out.staging_index    = idx;
            out.route            = in.route;
            out.verdict          = RemoteAdmitVerdict::open_staged;
            return;
        }

        // ===== the AUTHENTICATED session domains ==========================================================
        case RemoteDomainId::cmd_auth_execute:
        case RemoteDomainId::cmd_response_ack:
        case RemoteDomainId::cmd_safe_rollover:
        case RemoteDomainId::cmd_force_rollover:
            break;

        // ⛔ EVERY RESPONSE DOMAIN AND `invalid`: a target never receives one. No fallback, no trial decode.
        default:
            out.verdict = RemoteAdmitVerdict::silent_bad_request;
            return;
    }

    // ---- the selected slot must be a LIVE row holding a usable epoch ------------------------------------
    const uint8_t slot = L.slot;
    if (slot >= kRadminAclSlots)                     { out.verdict = RemoteAdmitVerdict::silent_bad_request;  return; }
    if (!row_occupied(s.acl[slot]))                  { out.verdict = RemoteAdmitVerdict::silent_no_acl_row;   return; }
    if (s.epoch[slot] == 0)                          { out.verdict = RemoteAdmitVerdict::silent_bad_key;      return; }

    // ---- derive base, then THIS SLOT'S CURRENT session key. Both are per-call wiped transients. ---------
    uint8_t base[32] = {};
    uint8_t session[32] = {};
    RemoteStatus st = derive_base(s, s.acl[slot].ed_pub, base);
    if (st == RemoteStatus::ok) st = remote_kdf_session(session, base, s.epoch[slot]);
    crypto_wipe(base, sizeof base);
    if (st != RemoteStatus::ok) {
        crypto_wipe(session, sizeof session);
        out.verdict = RemoteAdmitVerdict::silent_bad_key;
        return;
    }

    // ⚠ B313 again: `remote_body_decode` publishes nothing on a bad tag but does NOT scrub this buffer.
    uint8_t pt[kRadminBodyBytes] = {};
    RemoteDecoded d{};
    const RemoteKeys keys{ {}, std::span<const uint8_t>(session, 32) };
    st = remote_body_decode(d, in.outer_type, in.body, keys, in.source, in.request_carrier,
                            std::span<uint8_t>(pt, sizeof pt));
    crypto_wipe(session, sizeof session);
    if (st != RemoteStatus::ok) {
        crypto_wipe(pt, sizeof pt);
        // ★ AN EPOCH MISMATCH ARRIVES HERE AS `auth_failed`, BY CONSTRUCTION — the session key differs, so
        //   the tag cannot verify. There is deliberately NO epoch comparison to reject a fixture with.
        out.verdict = (st == RemoteStatus::auth_failed) ? RemoteAdmitVerdict::silent_auth_failed
                                                        : RemoteAdmitVerdict::silent_bad_request;
        return;
    }

    // ---- AUTHENTICATED. Only now may a lookup decision affect state. ------------------------------------
    out.controller_slot = slot;
    out.request_id      = d.msg.request_id;
    // ★ THE FINGERPRINT IS THE RECEIVED TAG ITSELF — the last 16 bytes of the authenticated body, exactly as
    //   `remote_body_decode` verified them. ⛔ Not an R-RA-29 display fingerprint, ⛔ not a hash of the
    //   plaintext and ⛔ not a routing hash.
    const uint8_t* tag = in.body.data() + in.body.size() - kRemoteTagBytes;

    // ★★ THE RESERVED CONTROL PARTITION (§4.3). An OWNER execute and every authenticated session-control
    //    request are eligible for it; an operator execute is not, and an UNAUTHENTICATED claim to either
    //    could not have reached this line. ⛔ Neither partition ever borrows the other.
    const bool is_control  = (L.domain != RemoteDomainId::cmd_auth_execute);
    const bool is_owner    = s.acl[slot].role == kRadminRoleOwner;
    const uint8_t partition = (is_control || is_owner) ? kRadminIngressControl : kRadminIngressGeneral;

    if (is_control) {
        // ⛔ CONTROL ADMISSION ALLOCATES NO EXECUTE SEEN ROW — which is precisely why a full seen pool can
        //    never consume the control reservation. ⓘ There is NO ACK/rollover EFFECT and NO response here:
        //    the row is retained until it expires. Slice 7b owns both producers.
        const uint8_t ii = ingress_free_index(s, partition);
        if (ii == kRadminNoSlot) { crypto_wipe(pt, sizeof pt); out.verdict = RemoteAdmitVerdict::ingress_full; return; }
        IngressOperationHeader& h = s.ingress[ii];
        h = IngressOperationHeader{};
        h.request_id    = d.msg.request_id;
        h.expires_at_ms = remote_session_deadline(in.now_ms, radmin_staging_lifetime_ms);
        h.source_hash   = in.source.hash;
        h.body_len      = 0;                         // every control domain is a FIXED layout: no payload
        h.seen_index    = kRadminNoSlot;
        h.ctl           = in.body[0];
        h.route         = in.route;
        h.partition     = ii;
        h.state         = static_cast<uint8_t>(IngressState::reserved);
        h.body_slot     = ii;
        s.body[ii]      = IngressBodySlot{};
        crypto_wipe(pt, sizeof pt);
        out.ingress_index = ii;
        out.route         = in.route;
        out.verdict       = RemoteAdmitVerdict::control_admitted;
        return;
    }

    // ---- design §10, cases 1..5, keyed by (slot, request_id) --------------------------------------------
    const uint8_t si_hit = remote_session_seen_find(s, slot, d.msg.request_id);
    if (si_hit != kRadminNoSlot) {
        const SeenEntry& e = s.seen[si_hit];
        crypto_wipe(pt, sizeof pt);
        out.seen_index = si_hit;
        // ★ THE FIRST-ACCEPTED ROW IS PRESERVED, and it is the first route that is published — ⛔ never the
        //   retry's. A retry can neither replace the admitted source nor its captured return metadata.
        out.route = e.route;
        // Case 3: the same id under the same slot with a DIFFERENT tag (or a different authenticated source)
        // is a credential-sharing ID REUSE. ⛔ NEITHER plaintext is ever dispatched.
        if (!tag_equal(e.record.request_tag, tag) || e.record.source_hash != in.source.hash) {
            out.verdict = RemoteAdmitVerdict::reject_id_reuse;
            return;
        }
        // Case 4 (⚠ SYNTHETIC-ONLY until Slice 7b's ACK producer exists) / case 2.
        out.verdict = (e.record.state == static_cast<uint8_t>(SeenState::acknowledged))
                        ? RemoteAdmitVerdict::already_acknowledged
                        : RemoteAdmitVerdict::replay_transcript;
        // ⛔ AND NOTHING IS DISPATCHED, RESEALED OR OVERWRITTEN on either arm.
        return;
    }

    // Case 1 / case 5. ⛔ RESERVE BOTH OR NEITHER: both indices are found free BEFORE anything is written,
    // so a missing ingress row can never leave a stranded seen row (or the reverse).
    const uint8_t si = seen_free_index(s);
    if (si == kRadminNoSlot) { crypto_wipe(pt, sizeof pt); out.verdict = RemoteAdmitVerdict::session_full; return; }
    const uint8_t ii = ingress_free_index(s, partition);
    if (ii == kRadminNoSlot) { crypto_wipe(pt, sizeof pt); out.verdict = RemoteAdmitVerdict::ingress_full; return; }
    commit_admit(s, in, d, slot, tag, si, ii, out);
    crypto_wipe(pt, sizeof pt);
}

}  // namespace MESHROUTE_NS
