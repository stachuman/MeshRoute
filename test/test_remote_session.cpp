// MeshRoute — test_remote_session.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — the PURE suite for `lib/core/remote_session.{h,cpp}`.
//
// ⛔⛔ EVERY REQUEST BELOW IS BUILT BY THE PRODUCTION CODEC (`remote_body_encode`) FROM A CONTROLLER-SIDE KEY
//     DERIVED INDEPENDENTLY OF THE TARGET'S — there is no controller implementation in the tree yet (Slice 8a),
//     so the fixture derives the base/session keys itself from the two identities and the epoch, exactly as a
//     controller will. ⇒ a mutation that breaks the target's key selection, its nonce input, its AAD or its
//     slot/epoch binding turns these cases red, because the two sides would no longer agree.
//     ⓘ The codec's own correctness is NOT re-proved here: `test_remote_codec.cpp` pins it against an
//       INDEPENDENT Python reference. This file proves what the SESSION does with it.
//
// ⚠⚠ SYNTHETIC-ARM LABELS, STATED UP FRONT so no reader promotes a fixture into an observation:
//   · `already_acknowledged` (design §10 case 4) is driven by an EXPLICIT VALUE FIXTURE — the seen row's `state`
//     is written to `acknowledged` directly. Slice 7b owns the ACK producer; ⛔ nothing in this slice can put a
//     row into that state over the air, and this case is ⛔ NOT evidence that an ACK path exists.
//   · `session_full` (case 5) is REAL and fully reachable: sixteen distinct authenticated requests fill the
//     shared pool. Its wire ANSWER, however, does not exist yet — the verdict is what Slice 7b will act on.
//   · `executing` / `completed` are declared states with ⛔ no producer in this slice, and no case claims one.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "frame_codec.h"
#include "identity.h"
#include "monocypher.h"
#include "protocol_constants.h"
#include "remote_codec.h"
#include "remote_session.h"

using namespace meshroute;

namespace {

// ---- identities -------------------------------------------------------------------------------------------
Identity make_identity(uint8_t tag) {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(tag * 7 + i * 13 + 1);
    Identity id{};
    identity_from_seed(id, seed);
    return id;
}

// ★ THE CONTROLLER'S OWN DERIVATION, written out rather than borrowed from the target's code path: the two sides
//   must agree because they compute the SAME function of the SAME ordered inputs, not because they share a call.
bool controller_base_key(const Identity& controller, const uint8_t target_admin_ed_pub[32], uint8_t out[32]) {
    uint8_t target_x[32];
    ed_pub_to_x25519(target_x, target_admin_ed_pub);
    uint8_t shared[32];
    if (remote_ecdh_shared(shared, controller, target_x) != RemoteStatus::ok) return false;
    const bool ok = remote_kdf_base(out, shared, controller.ed_pub, target_admin_ed_pub) == RemoteStatus::ok;
    crypto_wipe(shared, sizeof shared);
    return ok;
}

// ---- carriers ---------------------------------------------------------------------------------------------
RemoteCarrier same_layer_cmd() {
    RemoteCarrier c{};
    c.outer_data_type = DATA_TYPE_REMOTE_CMD;
    c.dst_hash_on_wire = true;
    c.source_hash_on_wire = true;
    return c;
}
RemoteCarrier same_layer_resp() {
    RemoteCarrier c = same_layer_cmd();
    c.outer_data_type = DATA_TYPE_REMOTE_RESP;
    return c;
}
RemoteCarrier cross_layer_cmd(uint8_t depth, uint8_t cursor) {
    RemoteCarrier c = same_layer_cmd();
    c.cross_layer = true;
    c.path_depth  = depth;
    c.path_cursor = cursor;
    return c;
}

// ---- the fixture ------------------------------------------------------------------------------------------
struct Target {
    RemoteSessionState st{};
    Identity           root = make_identity(1);
    std::vector<Identity> ctrl;

    std::vector<uint8_t> open_request(uint64_t id, std::span<const uint8_t> line) {
        RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD;
        m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::open_execute); m.slot = kRemoteSlotSentinel; m.request_id = id;
        std::vector<uint8_t> bytes(kRadminBodyBytes); size_t n = 0;
        CHECK(remote_body_encode(bytes, n, m, line, {}, {true, 0}, same_layer_cmd()) == RemoteStatus::ok);
        bytes.resize(n); return bytes;
    }

    RemoteStatus decode_result(const RemoteRxResult& r, RemoteDecoded& d, std::span<uint8_t> pt) {
        uint8_t base[32] = {}, session[32] = {};
        CHECK(controller_base_key(ctrl[r.controller_slot], root.ed_pub, base));
        CHECK(remote_kdf_session(session, base, st.epoch[r.controller_slot]) == RemoteStatus::ok);
        const auto result = remote_body_decode(d, DATA_TYPE_REMOTE_RESP, {r.reply, r.reply_len}, {base, session},
                                               {true, r.source_hash}, remote_reply_carrier(r.route), pt);
        crypto_wipe(base, sizeof base); crypto_wipe(session, sizeof session);
        return result;
    }

    // B369: old capacity fixtures expired never-executed bodies and retained their fingerprints.
    // That claim is withdrawn. Complete and ACK real transcripts instead, retaining executed tombstones.
    void complete_and_ack(const RemoteRxResult& r, uint32_t src) {
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        if (r.verdict != RemoteAdmitVerdict::admit) return;
        const bool reserved = remote_transcript_reserve(st, r.seen_index, kRadminChunkBytes);
        CHECK(reserved); if (!reserved) return;
        remote_transcript_complete(st, r.seen_index, RemoteTerminal::completed);
        const auto& row = st.seen[r.seen_index].record;
        const auto ack = request(row.controller_slot, static_cast<uint8_t>(RemoteCmdOpcode::response_ack),
                                 row.request_id, {}, src, same_layer_cmd());
        CHECK(deliver(ack, src).verdict == RemoteAdmitVerdict::ack_released);
    }

    // n occupied slots, all OWNER unless `operators` says otherwise.
    void provision(uint8_t n, bool owner = true) {
        remote_session_clear(st);
        RemoteSessionInstall p{};
        p.set_root = true;
        memcpy(p.x_secret, root.x_secret, 32);
        memcpy(p.ed_pub,   root.ed_pub,   32);
        p.set_acl = true;
        ctrl.clear();
        for (uint8_t i = 0; i < n; ++i) {
            ctrl.push_back(make_identity(static_cast<uint8_t>(10 + i)));
            memcpy(p.acl[i].ed_pub, ctrl.back().ed_pub, 32);
            p.acl[i].role = owner ? kRadminRoleOwner : kRadminRoleOperator;
            p.epoch[i]     = 0x1000u + i;      // deterministic, NON-ZERO
            p.epoch_set[i] = true;
        }
        remote_session_install(st, p);
    }

    // Build one sealed request from controller `slot`, through the PRODUCTION encoder.
    std::vector<uint8_t> request(uint8_t slot, uint8_t opcode, uint64_t rid,
                                 std::span<const uint8_t> body, uint32_t src_hash,
                                 const RemoteCarrier& carrier) {
        uint8_t base[32] = {};
        CHECK(controller_base_key(ctrl[slot], root.ed_pub, base));
        uint8_t session[32] = {};
        CHECK(remote_kdf_session(session, base, st.epoch[slot]) == RemoteStatus::ok);
        RemoteMessage m{};
        m.outer_type = DATA_TYPE_REMOTE_CMD;
        m.opcode     = opcode;
        m.slot       = slot;
        m.request_id = rid;
        const RemoteKeys keys{ std::span<const uint8_t>(base, 32), std::span<const uint8_t>(session, 32) };
        std::vector<uint8_t> out(260);
        size_t n = 0;
        const RemoteStatus s = remote_body_encode(std::span<uint8_t>(out.data(), out.size()), n, m, body,
                                                  keys, RemoteSource{true, src_hash}, carrier);
        crypto_wipe(base, sizeof base); crypto_wipe(session, sizeof session);
        CHECK(s == RemoteStatus::ok);
        out.resize(n);
        return out;
    }

    // A BOOTSTRAP request from an arbitrary identity (which may or may not be in the ACL).
    std::vector<uint8_t> bootstrap(const Identity& who, uint64_t rid, uint32_t src_hash,
                                    const RemoteCarrier& carrier, bool* ok = nullptr) {
        uint8_t base[32] = {};
        const bool derived = controller_base_key(who, root.ed_pub, base);
        if (ok) *ok = derived;
        if (!derived) { crypto_wipe(base, sizeof base); return {}; }
        RemoteMessage m{};
        m.outer_type      = DATA_TYPE_REMOTE_CMD;
        m.opcode          = static_cast<uint8_t>(RemoteCmdOpcode::bootstrap);
        m.slot            = kRemoteSlotSentinel;
        m.request_id      = rid;
        m.controller_pub  = std::span<const uint8_t>(who.ed_pub, 32);
        const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
        std::vector<uint8_t> out(260);
        size_t n = 0;
        const RemoteStatus s = remote_body_encode(std::span<uint8_t>(out.data(), out.size()), n, m,
                                                  std::span<const uint8_t>{}, keys,
                                                  RemoteSource{true, src_hash}, carrier);
        crypto_wipe(base, sizeof base);
        CHECK(s == RemoteStatus::ok);
        out.resize(n);
        return out;
    }

    RemoteRxResult deliver(std::span<const uint8_t> body, uint32_t src, uint64_t now = 1000,
                           const RemoteCarrier* req = nullptr, const RemoteCarrier* rep = nullptr,
                           const ReplyRoute* route = nullptr) {
        RemoteRxInput in{};
        in.now_ms          = now;
        in.outer_type      = DATA_TYPE_REMOTE_CMD;
        in.body            = body;
        in.source          = RemoteSource{true, src};
        in.request_carrier = req ? *req : same_layer_cmd();
        in.reply_carrier   = rep ? *rep : same_layer_resp();
        in.route           = route ? *route : ReplyRoute{};
        if (!route) in.route.carrier = static_cast<uint8_t>(RadminCarrierKind::same_layer);
        RemoteRxResult out{};
        remote_session_receive(st, in, out);
        return out;
    }
};

}  // namespace

// =============================================================================================================
// §4.2 — the layout, N, and the resource arithmetic
// =============================================================================================================
TEST_CASE("§radmin-5/L1 the original 2064-byte prefix and appended 7b-1 pool have pinned offsets") {
    CHECK(sizeof(AdminAclRow) == 34);
    CHECK(sizeof(ReplyRoute) == 8);
    CHECK(sizeof(SeenRequestRecord) == 48);
    CHECK(sizeof(SeenEntry) == 56);
    CHECK(sizeof(IngressOperationHeader) == 40);
    CHECK(sizeof(IngressBodySlot) == 236);
    CHECK(sizeof(OpenStagingSlot) == 32);
    CHECK(sizeof(RemoteSessionState) == 8904);
    CHECK(alignof(RemoteSessionState) == 8);
    // ★ THE ARITHMETIC CLOSES WITH NO REMAINDER — that is what makes the Node re-pin attributable.
    // Previously the eight prefix terms alone were the whole 2064-byte state.
    // 7b-1's 3848 included two tail bytes. Three 1658-byte captures + two counters reuse them.
    CHECK(64u + 340u + 4u + 80u + 896u + 80u + 472u + 128u + 1776u + 6u + 3u * 1658u + 4u + 32u + 40u + 2u + 6u == sizeof(RemoteSessionState));
    CHECK(offsetof(RemoteSessionState, acl)     == 64);
    CHECK(offsetof(RemoteSessionState, epoch)   == 408);
    CHECK(offsetof(RemoteSessionState, seen)    == 488);
    CHECK(offsetof(RemoteSessionState, ingress) == 1384);
    CHECK(offsetof(RemoteSessionState, body)    == 1464);
    CHECK(offsetof(RemoteSessionState, staging) == 1936);
    CHECK(offsetof(RemoteSessionState, transcript_exhaustion) == 3872);
    CHECK(offsetof(RemoteSessionState, response_enqueue_failure) == 3874);
    CHECK(offsetof(RemoteSessionState, response_seal_failure) == 3876);
}

TEST_CASE("§radmin-5/L2 N is 16 TOTAL seen entries SHARED across the ten slots — not 16 per slot") {
    // ★★ THE MEASURED N, AND THE CAVEAT THAT COMES WITH IT. `kRadminSeenSlots` is the WHOLE pool: ten slots
    //    share it, so one busy credential can occupy all sixteen and the capacity left for any other depends on
    //    what the others hold. ⛔ 16 per slot would be 160 rows / 8 960 bytes, which is not what is allocated.
    CHECK(kRadminSeenSlots == 16);
    CHECK(sizeof(RemoteSessionState::seen) == 16u * 56u);
    CHECK(sizeof(RemoteSessionState::seen) != kRadminAclSlots * 16u * sizeof(SeenEntry));
    // The partitions, asserted as VALUES so a silent widening cannot pass.
    CHECK(kRadminIngressSlots == 2);
    CHECK(kRadminStagingSlots == 4);
    CHECK(kRadminOpenSlots == 3);
    CHECK(kRadminBootstrapSlot == 3);
    CHECK(kRadminAclSlots == kRemoteSlotSessionMax + 1);
    // Historical ceiling 2064 + 1824 covered only 7b-1. R-RA-35 adds exactly the independent open captures.
    // R-RA-40 adds 80 bytes for 7b-3; no unapproved resident work buffer.
    CHECK(sizeof(RemoteSessionState) == 3848u + 4976u + 80u);
}

// =============================================================================================================
// §4.1 — installation, readiness and the epoch rules
// =============================================================================================================
TEST_CASE("§radmin-5/I1 an unprovisioned block accepts nothing and holds nothing") {
    RemoteSessionState st{};
    remote_session_clear(st);
    CHECK(remote_session_readiness(st) == AdminReadiness::disabled);
    CHECK_FALSE(remote_session_accepting(st));
    CHECK(st.acl_occupied == 0);
    CHECK(st.root_present == 0);
    CHECK(remote_session_seen_used(st) == 0);
    CHECK(remote_session_ingress_used(st) == 0);
    CHECK(remote_session_staging_used(st) == 0);
    CHECK(remote_session_earliest_deadline(st) == ~uint64_t{0});
}

TEST_CASE("§radmin-5/I2 readiness is a CONJUNCTION: root + an occupied row + a NON-ZERO epoch on every occupied row") {
    Target t;
    t.provision(2);
    CHECK(remote_session_accepting(t.st));
    CHECK(t.st.acl_occupied == 2);

    // ⛔ EPOCH 0 IS NEVER IN SERVICE: one occupied row holding it disables the whole node rather than running it.
    RemoteSessionInstall zero{};
    zero.epoch_set[1] = true;
    zero.epoch[1]     = 0;
    remote_session_install(t.st, zero);
    CHECK(remote_session_readiness(t.st) == AdminReadiness::disabled);

    // …and restoring a non-zero epoch re-enables it, so the refusal was the epoch and not something else.
    RemoteSessionInstall back{};
    back.epoch_set[1] = true;
    back.epoch[1]     = 0x77;
    remote_session_install(t.st, back);
    CHECK(remote_session_accepting(t.st));

    // ⛔ NO ROOT -> disabled, ACL intact.
    RemoteSessionInstall off{};
    off.clear_root = true;
    remote_session_install(t.st, off);
    CHECK_FALSE(remote_session_accepting(t.st));
    CHECK(t.st.acl_occupied == 2);          // ★ the ACL SURVIVES a root removal (design §6.5)
}

TEST_CASE("§radmin-5/I3 a cold-boot entropy refusal is its OWN state, and it wipes rather than half-installs") {
    Target t;
    t.provision(3);
    CHECK(remote_session_accepting(t.st));
    remote_session_mark_entropy_failed(t.st);
    CHECK(remote_session_readiness(t.st) == AdminReadiness::entropy_failed);
    CHECK_FALSE(remote_session_accepting(t.st));
    CHECK(t.st.root_present == 0);
    CHECK(t.st.acl_occupied == 0);
    // ⛔ …and it is DISTINGUISHABLE from `disabled`, which is the whole reason it is a third value.
    CHECK(remote_session_readiness(t.st) != AdminReadiness::disabled);
}

TEST_CASE("§radmin-5/I4 a root change invalidates every session; an ACL change invalidates ONLY the slots that moved") {
    Target t;
    t.provision(3);
    const uint8_t body[3] = { 'a','b','c' };
    for (uint8_t s = 0; s < 3; ++s) {
        const uint64_t now = 1000 + s * 10;
        const auto req = t.request(s, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0x100 + s,
                                   std::span<const uint8_t>(body, 3), 0xAAAA0000u + s, same_layer_cmd());
        const auto r = t.deliver(req, 0xAAAA0000u + s, now);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        CHECK(r.ingress_index != kRadminNoSlot);
        // Old fixture freed SCRATCH by expiry while keeping unexecuted seen rows (withdrawn, B369).
        t.complete_and_ack(r, 0xAAAA0000u + s);
    }
    CHECK(remote_session_seen_used(t.st) == 3);
    const uint64_t e0 = t.st.epoch[0], e2 = t.st.epoch[2];

    // ---- an ACL change on slot 1 alone -------------------------------------------------------------------
    RemoteSessionInstall p{};
    p.set_acl = true;
    for (uint8_t i = 0; i < kRadminAclSlots; ++i) p.acl[i] = t.st.acl[i];
    p.acl[1].role     = kRadminRoleOperator;      // a ROLE change IS a change
    p.epoch_set[1]    = true;
    p.epoch[1]        = 0x999;
    remote_session_install(t.st, p);
    CHECK(t.st.epoch[0] == e0);                   // ⛔ untouched slots keep their epochs EXACTLY
    CHECK(t.st.epoch[2] == e2);
    CHECK(t.st.epoch[1] == 0x999);
    CHECK(remote_session_seen_find(t.st, 1, 0x101) == kRadminNoSlot);   // slot 1's work is gone
    CHECK(remote_session_seen_find(t.st, 0, 0x100) != kRadminNoSlot);   // ★ slot 0's is NOT
    CHECK(remote_session_seen_find(t.st, 2, 0x102) != kRadminNoSlot);

    // ---- a ROOT change wipes every session, keeps the ACL --------------------------------------------------
    Identity nr = make_identity(99);
    RemoteSessionInstall rp{};
    rp.set_root = true;
    memcpy(rp.x_secret, nr.x_secret, 32);
    memcpy(rp.ed_pub,   nr.ed_pub,   32);
    for (uint8_t i = 0; i < kRadminAclSlots; ++i) { rp.epoch_set[i] = true; rp.epoch[i] = 0x2000u + i; }
    remote_session_install(t.st, rp);
    t.root = nr;                                  // the fixture's controller-side derivation follows the INSTALLED root
    CHECK(remote_session_seen_used(t.st) == 0);
    CHECK(remote_session_ingress_used(t.st) == 0);
    CHECK(t.st.acl_occupied == 3);                // ⛔ the ACL survived
    CHECK(remote_session_accepting(t.st));        // ⛔ and NO slot is running epoch 0
    for (uint8_t i = 0; i < 3; ++i) CHECK(t.st.epoch[i] != 0);

    // ★★ AND THE INVALIDATION IS THE **ROOT MOVE'S OWN**, not a side effect of replacing the epochs. Re-admit a
    //    request, then install a pair change carrying ⛔ NO `epoch_set` and ⛔ NO `invalidate_all` at all: the
    //    sessions must die anyway, because this target's half of every base key just moved and a retained
    //    fingerprint would then describe a key the node no longer holds.
    const uint8_t body2[2] = { 'r','t' };
    const auto again = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0x900, body2, 0xBBBB, same_layer_cmd());
    CHECK(t.deliver(again, 0xBBBB).verdict == RemoteAdmitVerdict::admit);
    CHECK(remote_session_seen_used(t.st) == 1);
    Identity nr2 = make_identity(123);
    RemoteSessionInstall bare{};
    bare.set_root = true;
    memcpy(bare.x_secret, nr2.x_secret, 32);
    memcpy(bare.ed_pub,   nr2.ed_pub,   32);
    remote_session_install(t.st, bare);
    t.root = nr2;
    CHECK(remote_session_seen_used(t.st) == 0);      // ⛔ the root move ALONE cleared them
    CHECK(remote_session_ingress_used(t.st) == 0);
    CHECK(t.st.acl_occupied == 3);                   // ⛔ …and the ACL still survived
    for (uint8_t i = 0; i < 3; ++i) CHECK(t.st.epoch[i] != 0);   // ⛔ …with no slot dropped to epoch 0
}

// =============================================================================================================
// §10 — the five classification cases
// =============================================================================================================
TEST_CASE("§radmin-5/C1 case 1 ADMIT: the seen row and the ingress reservation are taken TOGETHER") {
    Target t;
    t.provision(1);
    const uint8_t body[5] = { 'r','o','u','t','e' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0xDEAD, body, 0x1234, same_layer_cmd());
    const auto r = t.deliver(req, 0x1234, 5000);
    CHECK(r.verdict == RemoteAdmitVerdict::admit);
    CHECK(r.controller_slot == 0);
    CHECK(r.request_id == 0xDEAD);
    CHECK(r.source_hash == 0x1234);
    CHECK(r.seen_index != kRadminNoSlot);
    CHECK(r.ingress_index != kRadminNoSlot);
    // The AUTHENTICATED facts are stored, and the BODY is an OWNED COPY of the decoded plaintext.
    const SeenRequestRecord& rec = t.st.seen[r.seen_index].record;
    CHECK(rec.state == static_cast<uint8_t>(SeenState::admitted));
    CHECK(rec.request_id == 0xDEAD);
    CHECK(rec.admin_epoch == t.st.epoch[0]);
    CHECK(rec.source_hash == 0x1234);
    CHECK(rec.transcript_slot == kRadminNoTranscript);          // ⛔ no transcript is allocated in this slice
    // ★ THE FINGERPRINT IS THE RECEIVED TAG — the last 16 bytes of the authenticated body, byte for byte.
    CHECK(std::memcmp(rec.request_tag, req.data() + req.size() - 16, 16) == 0);
    const IngressBodySlot& b = t.st.body[r.ingress_index];
    CHECK(b.len == 5);
    CHECK(std::memcmp(b.bytes, body, 5) == 0);
}

TEST_CASE("§radmin-5/C2 case 2 REPLAY: an identical retry re-classifies and dispatches/reseals/overwrites NOTHING") {
    Target t;
    t.provision(1);
    const uint8_t body[4] = { 'l','i','s','t' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 7, body, 0x55, same_layer_cmd());
    const auto a = t.deliver(req, 0x55, 1000);
    CHECK(a.verdict == RemoteAdmitVerdict::admit);
    const uint8_t used_seen = remote_session_seen_used(t.st);
    const uint8_t used_ing  = remote_session_ingress_used(t.st);

    const auto b = t.deliver(req, 0x55, 9000);
    CHECK(b.verdict == RemoteAdmitVerdict::replay_transcript);
    CHECK(b.seen_index == a.seen_index);                 // the SAME row
    CHECK(remote_session_seen_used(t.st) == used_seen);  // ⛔ nothing new was reserved
    CHECK(remote_session_ingress_used(t.st) == used_ing);
    CHECK(t.st.seen[a.seen_index].record.first_seen_ms == 1000u);   // ⛔ the FIRST timestamp survives the retry
}

TEST_CASE("§radmin-5/C3 case 3 ID REUSE: the same id under the same slot with a DIFFERENT tag is refused") {
    Target t;
    t.provision(1);
    const uint8_t b1[3] = { 'o','n','e' };
    const uint8_t b2[3] = { 't','w','o' };
    const auto r1 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 42, b1, 0x99, same_layer_cmd());
    const auto r2 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 42, b2, 0x99, same_layer_cmd());
    CHECK(std::memcmp(r1.data() + r1.size() - 16, r2.data() + r2.size() - 16, 16) != 0);   // genuinely different tags
    const auto a = t.deliver(r1, 0x99);
    CHECK(a.verdict == RemoteAdmitVerdict::admit);
    const auto c = t.deliver(r2, 0x99);
    CHECK(c.verdict == RemoteAdmitVerdict::reject_id_reuse);
    // ⛔ THE FIRST ACCEPTED ROW IS PRESERVED — the reuse attempt replaces nothing.
    CHECK(std::memcmp(t.st.seen[a.seen_index].record.request_tag, r1.data() + r1.size() - 16, 16) == 0);
    CHECK(t.st.body[a.ingress_index].len == 3);
    CHECK(std::memcmp(t.st.body[a.ingress_index].bytes, b1, 3) == 0);   // ⛔ still the FIRST plaintext

    // ⚠⚠ THE SECOND BELT, DRIVEN BY AN EXPLICIT VALUE FIXTURE because the wire cannot reach it. The stored row's
    //    `source_hash` is rewritten to a DIFFERENT controller, leaving the tag intact. Over the air that state is
    //    unreachable — the source is AEAD-bound, so a request from another source could not have produced this
    //    tag — which is exactly why the comparison is DEFENCE IN DEPTH and why proving it needs a synthetic
    //    fixture. ⛔ This is NOT executed RF behaviour, and it is not evidence that a source can be swapped.
    Target u;
    u.provision(1);
    const auto q = u.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 99, b1, 0x1234, same_layer_cmd());
    const auto ua = u.deliver(q, 0x1234);
    CHECK(ua.verdict == RemoteAdmitVerdict::admit);
    u.st.seen[ua.seen_index].record.source_hash = 0xDEADBEEF;          // ⚠ SYNTHETIC: not reachable from the wire
    const auto ub = u.deliver(q, 0x1234);
    CHECK(ub.verdict == RemoteAdmitVerdict::reject_id_reuse);          // ★ the SOURCE half refuses on its own
}

TEST_CASE("§radmin-5/C4 case 4 ALREADY ACKNOWLEDGED — ⚠ SYNTHETIC value fixture; no ACK producer exists yet") {
    Target t;
    t.provision(1);
    const uint8_t body[2] = { 'h','i' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 5, body, 0x11, same_layer_cmd());
    const auto a = t.deliver(req, 0x11);
    CHECK(a.verdict == RemoteAdmitVerdict::admit);
    // ⚠⚠ THE SYNTHETIC STEP, AND IT IS THE ONLY ONE IN THIS FILE: Slice 7b owns the ACK consumer, so the row is
    //    moved into `acknowledged` by an explicit write. ⛔ This is NOT executed RF behaviour and must never be
    //    reported as one.
    t.st.seen[a.seen_index].record.state = static_cast<uint8_t>(SeenState::acknowledged);
    const auto b = t.deliver(req, 0x11);
    CHECK(b.verdict == RemoteAdmitVerdict::already_acknowledged);
    // ⛔ AND THE TOMBSTONE SURVIVES THE ANSWER: the row is still there to answer the NEXT retry the same way.
    CHECK(t.st.seen[a.seen_index].record.state == static_cast<uint8_t>(SeenState::acknowledged));
    const auto c = t.deliver(req, 0x11);
    CHECK(c.verdict == RemoteAdmitVerdict::already_acknowledged);
}

TEST_CASE("§radmin-5/C5 case 5 SESSION FULL: sixteen ids fill the SHARED pool and the seventeenth is refused") {
    Target t;
    t.provision(2);
    const uint8_t body[1] = { 'x' };
    // ★ TWO CONCURRENT CREDENTIALS SHARE ONE POOL — eight rows each here, and the seventeenth request refuses
    //   whichever slot it comes from. That is the shared-pool cost the design asks to be REPORTED.
    for (int i = 0; i < 16; ++i) {
        const uint8_t slot = static_cast<uint8_t>(i & 1);
        const auto req = t.request(slot, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute),
                                   0x500 + i, body, 0x2000u + i, same_layer_cmd());
        const auto r = t.deliver(req, 0x2000u + i);
        // Old fixture allowed ingress_full then expired never-executed rows (withdrawn, B369).
        // Every one now really admits, completes and ACKs; all sixteen tombstones must remain.
        t.complete_and_ack(r, 0x2000u + i);
    }
    CHECK(remote_session_seen_used(t.st) == kRadminSeenSlots);
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0xFEED, body, 0x3333, same_layer_cmd());
    const auto r = t.deliver(req, 0x3333);
    CHECK(r.verdict == RemoteAdmitVerdict::session_full);
    CHECK(r.seen_index == kRadminNoSlot);
    CHECK(remote_session_seen_used(t.st) == kRadminSeenSlots);   // ⛔ nothing was evicted to make room
}

// =============================================================================================================
// §4.3 — the partitions
// =============================================================================================================
TEST_CASE("§radmin-5/P1 partition STARVATION, both directions: neither class can consume the other's row") {
    // ---- direction 1: a full GENERAL row does not block an authenticated CONTROL request -------------------
    {
        Target t;
        t.provision(1, /*owner=*/false);                 // an OPERATOR -> general partition
        const uint8_t body[2] = { 'g','o' };
        const auto q = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x1, same_layer_cmd());
        CHECK(t.deliver(q, 0x1).verdict == RemoteAdmitVerdict::admit);
        const auto q2 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 2, body, 0x1, same_layer_cmd());
        CHECK(t.deliver(q2, 0x1).verdict == RemoteAdmitVerdict::ingress_full);   // ⛔ it does NOT take the control row
        // …and the control class is still free.
        // ACK no longer holds ingress; SAFE_ROLLOVER preserves this partition proof (7b-2 producer pending).
        const auto ack = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 3,
                                   std::span<const uint8_t>{}, 0x1, same_layer_cmd());
        const auto ar = t.deliver(ack, 0x1);
        CHECK(ar.verdict == RemoteAdmitVerdict::control_admitted);
        CHECK(ar.ingress_index == kRadminIngressControl);
        // ⛔ CONTROL ADMISSION ALLOCATES NO EXECUTE SEEN ROW.
        CHECK(ar.seen_index == kRadminNoSlot);
    }
    // ---- direction 2: a full CONTROL row does not block an ordinary/operator execute ----------------------
    {
        Target t;
        t.provision(1, /*owner=*/false);
        const auto ack = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 9,
                                   std::span<const uint8_t>{}, 0x2, same_layer_cmd());
        CHECK(t.deliver(ack, 0x2).verdict == RemoteAdmitVerdict::control_admitted);
        const uint8_t body[2] = { 'o','k' };
        const auto q = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 10, body, 0x2, same_layer_cmd());
        const auto r = t.deliver(q, 0x2);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        CHECK(r.ingress_index == kRadminIngressGeneral);
    }
}

TEST_CASE("§radmin-5/P2 an OWNER execute is eligible for the reserved class; an OPERATOR execute is not") {
    {
        Target t; t.provision(1, /*owner=*/true);
        const uint8_t body[1] = { 'z' };
        const auto q = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x7, same_layer_cmd());
        const auto r = t.deliver(q, 0x7);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        CHECK(r.ingress_index == kRadminIngressControl);
        CHECK(r.seen_index != kRadminNoSlot);            // ★ an owner EXECUTE still takes a seen row
    }
    {
        Target t; t.provision(1, /*owner=*/false);
        const uint8_t body[1] = { 'z' };
        const auto q = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x7, same_layer_cmd());
        const auto r = t.deliver(q, 0x7);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        CHECK(r.ingress_index == kRadminIngressGeneral);
    }
}

TEST_CASE("§radmin-5/P3 seen exhaustion cannot consume the CONTROL reservation") {
    Target t;
    t.provision(1);
    const uint8_t body[1] = { 'x' };
    for (int i = 0; i < 16; ++i) {
        const auto q = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0x800 + i, body,
                                 0x4000u + i, same_layer_cmd());
        // B369 supersedes the old expire-and-retain unexecuted fixture.
        t.complete_and_ack(t.deliver(q, 0x4000u + i), 0x4000u + i);
    }
    CHECK(remote_session_seen_used(t.st) == kRadminSeenSlots);
    const auto ack = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 0xABC,
                               std::span<const uint8_t>{}, 0x5555, same_layer_cmd());
    const auto r = t.deliver(ack, 0x5555);
    CHECK(r.verdict == RemoteAdmitVerdict::control_admitted);   // ★ the full pool did NOT starve it
}

TEST_CASE("§radmin-5/P4 an ATOMIC reservation: a refused ingress leaves NO stranded seen row") {
    Target t;
    t.provision(1, /*owner=*/false);
    const uint8_t body[1] = { 'q' };
    const auto a = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x1, same_layer_cmd());
    CHECK(t.deliver(a, 0x1).verdict == RemoteAdmitVerdict::admit);
    const uint8_t before = remote_session_seen_used(t.st);
    const auto b = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 2, body, 0x1, same_layer_cmd());
    const auto r = t.deliver(b, 0x1);
    CHECK(r.verdict == RemoteAdmitVerdict::ingress_full);
    CHECK(remote_session_seen_used(t.st) == before);            // ⛔ CHANGED NEITHER
    CHECK(remote_session_seen_find(t.st, 0, 2) == kRadminNoSlot);
}

// =============================================================================================================
// §7.2 — bootstrap
// =============================================================================================================
TEST_CASE("§radmin-5/B1 a valid bootstrap answers on EVERY one of the ten slots, and never touches seen or epoch") {
    Target t;
    t.provision(10);
    for (uint8_t s = 0; s < 10; ++s) {
        const uint64_t before_epoch = t.st.epoch[s];
        const auto req = t.bootstrap(t.ctrl[s], 0x900 + s, 0x60000u + s, same_layer_cmd());
        const auto r = t.deliver(req, 0x60000u + s);
        CHECK(r.verdict == RemoteAdmitVerdict::bootstrap_answered);
        CHECK(r.controller_slot == s);                       // ★ the FULL-KEY lookup found the right row
        CHECK(r.has_reply);
        CHECK(r.reply_len == kRemoteOverheadBootstrapResponse);   // 33 B exactly
        CHECK(r.request_id == 0x900u + s);
        CHECK(t.st.epoch[s] == before_epoch);                // ⛔ READ-ONLY: the epoch never moved
        CHECK(remote_session_seen_used(t.st) == 0);          // ⛔ and no seen row was consumed
        // The reply is a decodable REMOTE_RESP bootstrap under the BASE key, at the matched slot and this epoch.
        uint8_t base[32] = {};
        CHECK(controller_base_key(t.ctrl[s], t.root.ed_pub, base));
        RemoteDecoded d{};
        uint8_t pt[8] = {};
        const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
        const RemoteStatus st = remote_body_decode(d, DATA_TYPE_REMOTE_RESP,
                                                   std::span<const uint8_t>(r.reply, r.reply_len), keys,
                                                   RemoteSource{true, 0x60000u + s}, same_layer_resp(),
                                                   std::span<uint8_t>(pt, sizeof pt));
        crypto_wipe(base, sizeof base);
        CHECK(st == RemoteStatus::ok);
        CHECK(d.authenticated);
        CHECK(d.msg.slot == s);
        CHECK(d.msg.request_id == 0x900u + s);
        CHECK(d.msg.admin_epoch == before_epoch);
        // ⛔ THE STAGING ROW IS THE RESERVED ONE and it is still HELD — Node releases it on the checked send.
        CHECK(r.staging_index == kRadminBootstrapSlot);
        remote_session_staging_release(t.st, r.staging_index);
        CHECK(remote_session_staging_used(t.st) == 0);
    }
}

TEST_CASE("§radmin-5/B2 bootstrap SILENCE: an absent key, a wrong target and a bad tag are indistinguishable") {
    Target t;
    t.provision(2);
    // (a) a well-formed bootstrap from an identity that holds NO row -> silent, ⛔ no membership oracle.
    Identity stranger = make_identity(200);
    const auto req = t.bootstrap(stranger, 1, 0x10, same_layer_cmd());
    const auto r = t.deliver(req, 0x10);
    CHECK(r.verdict == RemoteAdmitVerdict::silent_no_acl_row);
    CHECK(remote_admit_is_silent(r.verdict));
    CHECK_FALSE(r.has_reply);
    CHECK(remote_session_staging_used(t.st) == 0);

    // (b) the WRONG TARGET: a bootstrap sealed for a different node's administration root.
    Target other;
    other.root = make_identity(77);
    other.provision(2);
    other.ctrl = t.ctrl;                                   // same controllers, different target root
    const auto wrong = other.bootstrap(t.ctrl[0], 2, 0x11, same_layer_cmd());
    const auto rw = t.deliver(wrong, 0x11);
    CHECK(rw.verdict == RemoteAdmitVerdict::silent_auth_failed);
    CHECK_FALSE(rw.has_reply);

    // (c) a CORRUPTED TAG on an otherwise perfect request.
    auto bad = t.bootstrap(t.ctrl[1], 3, 0x12, same_layer_cmd());
    bad[bad.size() - 1] ^= 0x01;
    const auto rb = t.deliver(bad, 0x12);
    CHECK(rb.verdict == RemoteAdmitVerdict::silent_auth_failed);

    // (d) a DIFFERENT SOURCE HASH on unchanged sealed bytes: the source is AEAD-bound, so it fails auth.
    const auto good = t.bootstrap(t.ctrl[0], 4, 0x13, same_layer_cmd());
    CHECK(t.deliver(good, 0x99).verdict == RemoteAdmitVerdict::silent_auth_failed);
    CHECK(t.deliver(good, 0x13).verdict == RemoteAdmitVerdict::bootstrap_answered);   // …and it works with the right one
}

TEST_CASE("§radmin-5/B3 a LOW-ORDER controller key is refused before any key exists") {
    Target t;
    t.provision(1);
    // Install a row whose Ed25519 public key converts to a low-order X25519 point. `remote_ecdh_shared` refuses
    // the degenerate shared point, so derivation cannot produce a key at all.
    RemoteSessionInstall p{};
    p.set_acl = true;
    for (uint8_t i = 0; i < kRadminAclSlots; ++i) p.acl[i] = t.st.acl[i];
    std::memset(p.acl[0].ed_pub, 0, 32);                   // the all-zero point: `crypto_eddsa_to_x25519` -> zero
    p.acl[0].role = kRadminRoleOwner;
    p.epoch_set[0] = true; p.epoch[0] = 0x4242;
    remote_session_install(t.st, p);
    CHECK(remote_session_accepting(t.st));
    // A bootstrap naming that exact (all-zero) key finds the row and then fails the ECDH check.
    std::vector<uint8_t> body(kRemoteOverheadBootstrapRequest, 0);
    body[0] = remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::bootstrap), kRemoteSlotSentinel);
    const auto r = t.deliver(body, 0x21);
    CHECK(r.verdict == RemoteAdmitVerdict::silent_bad_key);
    CHECK_FALSE(r.has_reply);
}

TEST_CASE("§radmin-5/B4 the ONE reserved bootstrap row is never lent to an open request, and vice versa") {
    Target t;
    t.provision(1);
    // Fill all THREE open rows from three distinct sources.
    for (uint8_t i = 0; i < 3; ++i) {
        std::vector<uint8_t> open(kRemoteOverheadOpenExecute + 2, 0);
        open[0] = remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::open_execute), kRemoteSlotSentinel);
        open[1] = static_cast<uint8_t>(i);
        const auto r = t.deliver(open, 0x7000u + i);
        CHECK(r.verdict == RemoteAdmitVerdict::open_staged);
    }
    CHECK(remote_session_staging_used(t.st) == kRadminOpenSlots);
    // ⛔ A FOURTH OPEN REQUEST DOES NOT BORROW THE BOOTSTRAP ROW.
    std::vector<uint8_t> open4(kRemoteOverheadOpenExecute + 2, 0);
    open4[0] = remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::open_execute), kRemoteSlotSentinel);
    open4[1] = 0xEE;
    CHECK(t.deliver(open4, 0x7100).verdict == RemoteAdmitVerdict::open_staging_full);
    // ★ …and the bootstrap row IS still available, which is what the reservation is for.
    const auto boot = t.bootstrap(t.ctrl[0], 0x77, 0x7200, same_layer_cmd());
    const auto rb = t.deliver(boot, 0x7200);
    CHECK(rb.verdict == RemoteAdmitVerdict::bootstrap_answered);
    CHECK(rb.staging_index == kRadminBootstrapSlot);
    CHECK(remote_session_staging_used(t.st) == kRadminStagingSlots);
}

TEST_CASE("§radmin-5/B5 at most ONE open row per source, and a fresh request id does not evade it") {
    Target t;
    t.provision(1);
    std::vector<uint8_t> a(kRemoteOverheadOpenExecute + 1, 0);
    a[0] = remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::open_execute), kRemoteSlotSentinel);
    CHECK(t.deliver(a, 0x8000).verdict == RemoteAdmitVerdict::open_staged);
    std::vector<uint8_t> b(kRemoteOverheadOpenExecute + 1, 0);
    b[0] = a[0];
    b[1] = 0xAB;                                     // ★ a DIFFERENT request id, same source
    CHECK(t.deliver(b, 0x8000).verdict == RemoteAdmitVerdict::open_peer_bound);
    CHECK(t.deliver(b, 0x8001).verdict == RemoteAdmitVerdict::open_staged);   // a DIFFERENT source is fine
}

// =============================================================================================================
// §6 — the pre-crypto gates
// =============================================================================================================
TEST_CASE("§radmin-5/G1 SOURCE_HASH is MANDATORY, and 'absent' is never spelled as the value 0") {
    Target t;
    t.provision(1);
    const auto req = t.bootstrap(t.ctrl[0], 1, 0, same_layer_cmd());   // a source hash whose VALUE is 0
    RemoteRxInput in{};
    in.now_ms          = 1;
    in.outer_type      = DATA_TYPE_REMOTE_CMD;
    in.body            = req;
    in.source          = RemoteSource{ /*present=*/false, 0 };
    in.request_carrier = same_layer_cmd();
    in.reply_carrier   = same_layer_resp();
    RemoteRxResult out{};
    remote_session_receive(t.st, in, out);
    CHECK(out.verdict == RemoteAdmitVerdict::silent_no_source);
    // ★ …and the very same bytes, with PRESENCE true and the value still 0, are accepted: a hash of 0 is a
    //   legitimate value and only the presence flag expresses absence.
    in.source = RemoteSource{ /*present=*/true, 0 };
    remote_session_receive(t.st, in, out);
    CHECK(out.verdict == RemoteAdmitVerdict::bootstrap_answered);
}

TEST_CASE("§radmin-5/G2 an OVER-CAP body is REFUSED before any copy — ⛔ never clamped") {
    Target t;
    t.provision(1);
    // A cross-layer carrier at full depth caps the body well below the same-layer 232; a body sized for the
    // same-layer leg therefore over-runs it, and must be refused rather than truncated to fit.
    const RemoteCarrier narrow = cross_layer_cmd(4, 3);
    size_t cap_wide = 0, cap_narrow = 0;
    CHECK(remote_body_cap(same_layer_cmd(), cap_wide) == RemoteStatus::ok);
    CHECK(remote_body_cap(narrow, cap_narrow) == RemoteStatus::ok);
    CHECK(cap_narrow < cap_wide);
    std::vector<uint8_t> body(cap_wide - kRemoteOverheadAuthExecute, 0x5A);
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x31, same_layer_cmd());
    CHECK(req.size() == cap_wide);
    const auto r = t.deliver(req, 0x31, 1000, &narrow, nullptr, nullptr);
    CHECK(r.verdict == RemoteAdmitVerdict::silent_over_cap);
    CHECK(remote_session_seen_used(t.st) == 0);       // ⛔ nothing reserved
    CHECK(remote_session_ingress_used(t.st) == 0);
    // ★ …and the ONE storage array never became the authority: the same body on its OWN carrier is admitted.
    CHECK(t.deliver(req, 0x31).verdict == RemoteAdmitVerdict::admit);
}

TEST_CASE("§radmin-5/G3 an invalid packet reserves NOTHING, whatever kind of invalid it is") {
    Target t;
    t.provision(1);
    struct { const char* what; std::vector<uint8_t> body; } cases[] = {
        { "empty",            {} },
        { "reserved slot",    { remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 0x0B) } },
        { "reserved opcode",  { remote_ctl(0x7, 0) } },
        { "short bootstrap",  { remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::bootstrap), kRemoteSlotSentinel), 1, 2 } },
        { "a RESPONSE type",  { remote_ctl(static_cast<uint8_t>(RemoteRespOpcode::bootstrap), 0) } },
    };
    for (auto& c : cases) {
        const auto r = t.deliver(c.body, 0x41);
        CHECK(remote_admit_is_silent(r.verdict));
        CHECK_FALSE(r.has_reply);
        CHECK(remote_session_seen_used(t.st) == 0);
        CHECK(remote_session_ingress_used(t.st) == 0);
        CHECK(remote_session_staging_used(t.st) == 0);
    }
    // A structurally impossible CARRIER is refused too — before the body is even looked at.
    RemoteCarrier bad{};
    bad.outer_data_type = DATA_TYPE_REMOTE_CMD;
    bad.source_hash_on_wire = false;                 // R-RA-13 makes this an invalid descriptor
    const auto req = t.bootstrap(t.ctrl[0], 1, 0x42, same_layer_cmd());
    CHECK(t.deliver(req, 0x42, 1000, &bad).verdict == RemoteAdmitVerdict::silent_bad_carrier);
}

TEST_CASE("§radmin-5/G4 an EPOCH mismatch fails by CONSTRUCTION, and there is no epoch comparison to fool") {
    Target t;
    t.provision(1);
    const uint8_t body[2] = { 'e','p' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x51, same_layer_cmd());
    // Roll the slot's epoch — as a rotation or a reboot would — WITHOUT touching the sealed bytes.
    RemoteSessionInstall p{};
    p.epoch_set[0] = true;
    p.epoch[0]     = 0xBEEF;
    remote_session_install(t.st, p);
    const auto r = t.deliver(req, 0x51);
    CHECK(r.verdict == RemoteAdmitVerdict::silent_auth_failed);   // ★ the TAG failed; no counter was compared
    CHECK(remote_session_seen_used(t.st) == 0);
}

TEST_CASE("§radmin-5/G5 nothing at all is admitted while the node is not ready") {
    RemoteSessionState st{};
    remote_session_clear(st);
    Target t;
    t.provision(1);
    const auto req = t.bootstrap(t.ctrl[0], 1, 0x61, same_layer_cmd());
    RemoteRxInput in{};
    in.now_ms = 1; in.outer_type = DATA_TYPE_REMOTE_CMD; in.body = req;
    in.source = RemoteSource{true, 0x61};
    in.request_carrier = same_layer_cmd(); in.reply_carrier = same_layer_resp();
    RemoteRxResult out{};
    remote_session_receive(st, in, out);
    CHECK(out.verdict == RemoteAdmitVerdict::silent_not_ready);
    CHECK_FALSE(out.has_reply);
}

// =============================================================================================================
// §4.5 — the ONE shared expiry
// =============================================================================================================
TEST_CASE("§radmin-5/E1 the deadline is the named transport horizon, and the addition SATURATES") {
    CHECK(radmin_staging_lifetime_ms == protocol::e2e_ack_deadline_xl_ms);
    CHECK(radmin_staging_lifetime_ms == 300000u);
    CHECK(remote_session_deadline(0, 1000) == 1000u);
    // ⛔ NO WRAP, EVER: a `now` near the top of the range saturates instead of producing a deadline in the past.
    CHECK(remote_session_deadline(~uint64_t{0} - 5, 1000) == ~uint64_t{0});
    CHECK(remote_session_deadline(~uint64_t{0}, radmin_staging_lifetime_ms) == ~uint64_t{0});
}

TEST_CASE("§radmin-5/E2 expiry is at now >= expires_at: just below holds, exactly at releases") {
    Target t;
    t.provision(1);
    const uint8_t body[1] = { 'a' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x71, same_layer_cmd());
    const auto r = t.deliver(req, 0x71, /*now=*/1000);
    CHECK(r.verdict == RemoteAdmitVerdict::admit);
    const uint64_t due = 1000 + radmin_staging_lifetime_ms;
    CHECK(remote_session_earliest_deadline(t.st) == due);
    CHECK(remote_session_expire(t.st, due - 1) == 0);            // just BELOW: nothing moves
    CHECK(remote_session_ingress_used(t.st) == 1);
    CHECK(remote_session_expire(t.st, due) == 1);                // EXACTLY at: released
    CHECK(remote_session_ingress_used(t.st) == 0);
    // Old claim "expiry releases scratch, never the fingerprint; identical retry is a replay"
    // is withdrawn for NEVER EXECUTED rows (B369). Executed tombstones remain protected.
    CHECK(remote_session_seen_used(t.st) == 0);
    CHECK(remote_session_seen_find(t.st, 0, 1) == kRadminNoSlot);
    CHECK(t.st.seen[r.seen_index].record.state == static_cast<uint8_t>(SeenState::free));
    CHECK(t.deliver(req, 0x71, due + 1).verdict == RemoteAdmitVerdict::admit);
}

TEST_CASE("§radmin-5/E3 many rows, one scan: equal deadlines all go, and the next arm is the true remaining minimum") {
    Target t;
    t.provision(1, /*owner=*/false);          // an OPERATOR: its execute takes the GENERAL row, leaving CONTROL free
    const uint8_t body[1] = { 'a' };
    // Two ingress rows at t=1000 and two staging rows at t=2000.
    const auto q1 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x81, same_layer_cmd());
    CHECK(t.deliver(q1, 0x81, 1000).verdict == RemoteAdmitVerdict::admit);
    const auto q2 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 2,
                              std::span<const uint8_t>{}, 0x81, same_layer_cmd());
    CHECK(t.deliver(q2, 0x81, 1000).verdict == RemoteAdmitVerdict::control_admitted);
    for (uint8_t i = 0; i < 2; ++i) {
        std::vector<uint8_t> open(kRemoteOverheadOpenExecute + 1, 0);
        open[0] = remote_ctl(static_cast<uint8_t>(RemoteCmdOpcode::open_execute), kRemoteSlotSentinel);
        CHECK(t.deliver(open, 0x9000u + i, 2000).verdict == RemoteAdmitVerdict::open_staged);
    }
    CHECK(remote_session_earliest_deadline(t.st) == 1000 + radmin_staging_lifetime_ms);
    // ★ BOTH EQUAL-DEADLINE ROWS GO IN ONE PASS — one `now`, so they cannot fall on opposite sides of it.
    CHECK(remote_session_expire(t.st, 1000 + radmin_staging_lifetime_ms) == 2);
    CHECK(remote_session_ingress_used(t.st) == 0);
    CHECK(remote_session_staging_used(t.st) == 2);
    // …and the next arm is the TRUE remaining minimum, not the one that just fired.
    CHECK(remote_session_earliest_deadline(t.st) == 2000 + radmin_staging_lifetime_ms);
    CHECK(remote_session_expire(t.st, 2000 + radmin_staging_lifetime_ms) == 2);
    CHECK(remote_session_earliest_deadline(t.st) == ~uint64_t{0});     // nothing pends -> the caller CANCELS
}

TEST_CASE("§radmin-5/E4 an INVALIDATION releases rows, and the scan re-arms against what remains") {
    Target t;
    t.provision(2, /*owner=*/false);
    {   // slot 1 becomes an OWNER so the two executes land in DIFFERENT partitions and can coexist.
        // ⛔ `epoch_set` stays false everywhere: this is fixture setup, not a mutation, and it must invalidate
        //    nothing (which is itself the no-op rule §radmin-5/I4 pins).
        RemoteSessionInstall setup{};
        setup.set_acl = true;
        for (uint8_t i = 0; i < kRadminAclSlots; ++i) setup.acl[i] = t.st.acl[i];
        setup.acl[1].role = kRadminRoleOwner;
        remote_session_install(t.st, setup);
        CHECK(remote_session_accepting(t.st));
    }
    const uint8_t body[1] = { 'a' };
    const auto q0 = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0x91, same_layer_cmd());
    CHECK(t.deliver(q0, 0x91, 1000).verdict == RemoteAdmitVerdict::admit);
    const auto q1 = t.request(1, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 2, body, 0x92, same_layer_cmd());
    CHECK(t.deliver(q1, 0x92, 3000).verdict == RemoteAdmitVerdict::admit);
    CHECK(remote_session_earliest_deadline(t.st) == 1000 + radmin_staging_lifetime_ms);
    // Remove slot 0 -> its work goes; slot 1's stays, and the earliest deadline becomes slot 1's.
    RemoteSessionInstall p{};
    p.set_acl = true;
    for (uint8_t i = 0; i < kRadminAclSlots; ++i) p.acl[i] = t.st.acl[i];
    p.acl[0] = AdminAclRow{};
    p.epoch_set[0] = true; p.epoch[0] = 0;
    remote_session_install(t.st, p);
    CHECK(remote_session_seen_find(t.st, 0, 1) == kRadminNoSlot);
    CHECK(remote_session_seen_find(t.st, 1, 2) != kRadminNoSlot);
    CHECK(remote_session_earliest_deadline(t.st) == 3000 + radmin_staging_lifetime_ms);
}

TEST_CASE("§radmin-5/E5 an expired never-executed body AND route are wiped; retry re-admits fresh") {
    Target t;
    t.provision(1);
    const uint8_t body[6] = { 'S','E','C','R','E','T' };
    ReplyRoute route{};
    route.carrier = static_cast<uint8_t>(RadminCarrierKind::cross_layer);
    route.n_layers = 3; route.cur = 2; route.origin = 9;
    route.layer_ids[0] = 0x11; route.layer_ids[1] = 0x22; route.layer_ids[2] = 0x33;
    const RemoteCarrier xreq = cross_layer_cmd(3, 2);
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0xA1, xreq);
    const auto r = t.deliver(req, 0xA1, 1000, &xreq, nullptr, &route);
    CHECK(r.verdict == RemoteAdmitVerdict::admit);
    CHECK(t.st.ingress[r.ingress_index].route.n_layers == 3);
    CHECK(t.st.seen[r.seen_index].route.n_layers == 3);
    CHECK(t.st.seen[r.seen_index].route.layer_ids[2] == 0x33);
    CHECK(t.st.seen[r.seen_index].route.origin == 9);
    CHECK(std::memcmp(t.st.body[r.ingress_index].bytes, body, 6) == 0);
    // B369 changes expiry, not a LIVE retry's route ownership. The earlier rewrite lost this
    // independent obligation; retain it before expiry, with different return metadata on the retry.
    ReplyRoute later_route = route;
    later_route.origin = 8;
    later_route.layer_ids[1] = 0x44;
    const auto live_retry = t.deliver(req, 0xA1, 1001, &xreq, nullptr, &later_route);
    CHECK(live_retry.verdict == RemoteAdmitVerdict::replay_transcript);
    CHECK(live_retry.seen_index == r.seen_index);
    CHECK(std::memcmp(&live_retry.route, &route, sizeof route) == 0);
    CHECK(std::memcmp(&t.st.seen[r.seen_index].route, &route, sizeof route) == 0);
    CHECK(std::memcmp(&t.st.ingress[r.ingress_index].route, &route, sizeof route) == 0);
    (void)remote_session_expire(t.st, 1000 + radmin_staging_lifetime_ms);
    // ⛔ THE PLAINTEXT IS GONE…
    bool any = false;
    for (size_t i = 0; i < kRadminBodyBytes; ++i) if (t.st.body[r.ingress_index].bytes[i] != 0) any = true;
    CHECK_FALSE(any);
    CHECK(t.st.body[r.ingress_index].len == 0);
    // Old claim "return metadata survives; retry finds the first route" is superseded for
    // NEVER EXECUTED work (B369). Both the body and its now-unowned route are wiped.
    CHECK(t.st.seen[r.seen_index].route.n_layers == 0);
    CHECK(t.st.seen[r.seen_index].route.layer_ids[0] == 0);
    const auto retry = t.deliver(req, 0xA1, 1000 + radmin_staging_lifetime_ms + 1, &xreq, nullptr, nullptr);
    CHECK(retry.verdict == RemoteAdmitVerdict::admit);
    CHECK(retry.route.n_layers == 0);
    CHECK(retry.route.layer_ids[1] == 0);
}

TEST_CASE("§radmin-5/E6 the same-layer route zeroes its path bytes; only cross-layer retains them") {
    Target t;
    t.provision(1);
    const uint8_t body[1] = { 'a' };
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, body, 0xB1, same_layer_cmd());
    const auto r = t.deliver(req, 0xB1);
    CHECK(r.verdict == RemoteAdmitVerdict::admit);
    CHECK(r.route.carrier == static_cast<uint8_t>(RadminCarrierKind::same_layer));
    CHECK(r.route.n_layers == 0);
    CHECK(r.route.cur == 0);
    for (int i = 0; i < 4; ++i) CHECK(r.route.layer_ids[i] == 0);
}

TEST_CASE("§radmin-5/V1 every verdict has a distinct name, and the silent set is exactly the design's") {
    // A name table that aliased two verdicts would make an MR_EMIT report the wrong refusal.
    const RemoteAdmitVerdict all[] = {
        RemoteAdmitVerdict::admit, RemoteAdmitVerdict::replay_transcript, RemoteAdmitVerdict::reject_id_reuse,
        RemoteAdmitVerdict::already_acknowledged, RemoteAdmitVerdict::session_full,
        RemoteAdmitVerdict::bootstrap_answered, RemoteAdmitVerdict::control_admitted,
        RemoteAdmitVerdict::ingress_full, RemoteAdmitVerdict::open_staged, RemoteAdmitVerdict::open_staging_full,
        RemoteAdmitVerdict::bootstrap_staging_busy, RemoteAdmitVerdict::open_peer_bound,
        RemoteAdmitVerdict::silent_not_ready, RemoteAdmitVerdict::silent_no_source,
        RemoteAdmitVerdict::silent_bad_carrier, RemoteAdmitVerdict::silent_bad_request,
        RemoteAdmitVerdict::silent_over_cap, RemoteAdmitVerdict::silent_no_acl_row,
        RemoteAdmitVerdict::silent_bad_key, RemoteAdmitVerdict::silent_auth_failed,
        RemoteAdmitVerdict::silent_reply_unbuildable,
    };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; ++i) {
        CHECK(std::strcmp(remote_admit_name(all[i]), "unknown") != 0);
        for (size_t j = i + 1; j < sizeof all / sizeof all[0]; ++j)
            CHECK(std::strcmp(remote_admit_name(all[i]), remote_admit_name(all[j])) != 0);
    }
    // ⛔ THE FIVE §10 CASES ARE NOT SILENT — Slice 7b has to be able to answer them.
    CHECK_FALSE(remote_admit_is_silent(RemoteAdmitVerdict::admit));
    CHECK_FALSE(remote_admit_is_silent(RemoteAdmitVerdict::replay_transcript));
    CHECK_FALSE(remote_admit_is_silent(RemoteAdmitVerdict::reject_id_reuse));
    CHECK_FALSE(remote_admit_is_silent(RemoteAdmitVerdict::already_acknowledged));
    CHECK_FALSE(remote_admit_is_silent(RemoteAdmitVerdict::session_full));
    // ★ …and every authentication/membership refusal IS, per §7.2 ("do not reveal ACL membership").
    CHECK(remote_admit_is_silent(RemoteAdmitVerdict::silent_no_acl_row));
    CHECK(remote_admit_is_silent(RemoteAdmitVerdict::silent_auth_failed));
    CHECK(remote_admit_is_silent(RemoteAdmitVerdict::silent_bad_key));
}

TEST_CASE("§radmin-7b2/open owns raw bytes and shares three rolling admissions across every source") {
    Target t; t.provision(2);
    const uint8_t line[] = {'s','t','a','t','u','s'};
    const auto admit = [&](uint32_t peer, uint64_t at, uint64_t id) {
        return t.deliver(t.open_request(id, line), peer, at);
    };
    auto wire = t.open_request(1, line);
    const auto a = t.deliver(wire, 101, 1000);
    CHECK(a.verdict == RemoteAdmitVerdict::open_staged);
    std::fill(wire.begin(), wire.end(), 0xCC);
    RadminOpenView view{}; CHECK(remote_open_next_admitted(t.st, view));
    CHECK(view.index == a.staging_index); CHECK(view.request_id == 1);
    CHECK(std::vector<uint8_t>(view.body.begin(), view.body.end()) == std::vector<uint8_t>(line, line + sizeof line));
    auto expected = t.st;
    CHECK(admit(101, 2000, 99).verdict == RemoteAdmitVerdict::open_peer_bound);
    ++expected.inbound_refusal;
    CHECK(std::memcmp(&t.st, &expected, sizeof expected) == 0);
    CHECK(remote_open_reserve(t.st, 0, 222));
    remote_open_complete(t.st, 0, RemoteTerminal::completed);
    remote_open_sent(t.st, 0); // pure lifecycle: the Node sender separately proves ownership before this call
    CHECK(t.st.staging[0].kind == static_cast<uint8_t>(OpenStagingKind::open_cooldown));
    CHECK(t.st.staging[0].expires_at_ms == 301000); CHECK(t.st.staging[0].request_id == 0);
    const OpenCapture empty{}; const ReplyRoute empty_route{};
    CHECK(std::memcmp(&t.st.open[0], &empty, sizeof empty) == 0);
    CHECK(std::memcmp(&t.st.staging[0].route, &empty_route, sizeof empty_route) == 0);
    CHECK(admit(101, 1500, 88).verdict == RemoteAdmitVerdict::open_rate_refused); // even with two FREE pairs
    CHECK(admit(102, 2000, 2).verdict == RemoteAdmitVerdict::open_staged);
    CHECK(admit(103, 3000, 3).verdict == RemoteAdmitVerdict::open_staged);
    expected = t.st;
    CHECK(admit(104, 300999, 4).verdict == RemoteAdmitVerdict::open_rate_refused);
    CHECK(admit(101, 300999, 5).verdict == RemoteAdmitVerdict::open_rate_refused);
    CHECK(admit(102, 300999, 6).verdict == RemoteAdmitVerdict::open_peer_bound);
    expected.open_rate_refusal += 2; ++expected.inbound_refusal;
    CHECK(std::memcmp(&t.st, &expected, sizeof expected) == 0);
    CHECK(admit(104, 301000, 7).verdict == RemoteAdmitVerdict::open_staged);
    CHECK(t.st.staging[0].expires_at_ms == 601000);
    CHECK(admit(105, 301999, 8).verdict == RemoteAdmitVerdict::open_staging_full);
    CHECK(admit(105, 302000, 9).verdict == RemoteAdmitVerdict::open_staged);
    CHECK(admit(106, 303000, 10).verdict == RemoteAdmitVerdict::open_staged);
    CHECK(remote_session_seen_used(t.st) == 0); CHECK(remote_session_ingress_used(t.st) == 0);
    CHECK(t.st.staging[kRadminBootstrapSlot].kind == static_cast<uint8_t>(OpenStagingKind::free));
}

TEST_CASE("§radmin-7b2/open clear framing exact bounds truncation freeze and expiry on every return depth") {
    for (const uint8_t depth : {uint8_t(0), uint8_t(2), uint8_t(3), uint8_t(4)}) {
        for (const size_t count : {size_t(0), size_t(222), size_t(1648), size_t(1649)}) {
            Target t; t.provision(1);
            const uint8_t line[] = {'r','o','u','t','e','s'};
            const auto admitted = t.deliver(t.open_request(89, line), 303, 1000);
            CHECK(admitted.verdict == RemoteAdmitVerdict::open_staged);
            auto& route = t.st.staging[0].route; // labelled carrier fixture; real reversed flights are Node cases
            if (depth) { route.carrier = static_cast<uint8_t>(RadminCarrierKind::cross_layer);
                route.n_layers = depth; route.cur = depth - 1; for (uint8_t i = 0; i < depth; ++i) route.layer_ids[i] = i + 1; }
            const auto carrier = remote_reply_carrier(route); size_t cap = 0;
            CHECK(remote_body_cap(carrier, cap) == RemoteStatus::ok);
            CHECK_FALSE(remote_open_reserve(t.st, 0, static_cast<uint16_t>(cap - kRemoteOverheadAuthResponse)));
            CHECK(remote_open_reserve(t.st, 0, static_cast<uint16_t>(cap - kRemoteOverheadOpenResponse)));
            std::vector<uint8_t> content(count); for (size_t i = 0; i < count; ++i) content[i] = static_cast<uint8_t>(i);
            remote_open_append(t.st, 0, content.data(), count / 2);
            if (count) remote_open_append(t.st, 0, content.data() + count / 2, count - count / 2);
            remote_open_complete(t.st, 0, RemoteTerminal::completed);
            const auto frozen = t.st.open[0];
            remote_open_append(t.st, 0, line, sizeof line); remote_open_complete(t.st, 0, RemoteTerminal::internal_error);
            CHECK(std::memcmp(&frozen, &t.st.open[0], sizeof frozen) == 0);
            std::vector<uint8_t> assembled; uint8_t seq = 0; bool terminal = false;
            while (remote_open_next(t.st) != kRadminNoSlot && seq < 20) {
                uint8_t bytes[kRadminBodyBytes] = {}, repeat[kRadminBodyBytes] = {}, pt[kRadminBodyBytes] = {};
                size_t n = 0, n2 = 0;
                CHECK(remote_open_encode(t.st, 0, bytes, n) == RemoteStatus::ok);
                CHECK(remote_open_encode(t.st, 0, repeat, n2) == RemoteStatus::ok);
                CHECK(n == n2); CHECK(std::memcmp(bytes, repeat, n) == 0); CHECK(n <= cap);
                RemoteDecoded decoded{};
                CHECK(remote_body_decode(decoded, DATA_TYPE_REMOTE_RESP, {bytes, n}, {}, {true, 303}, carrier, pt)
                      == RemoteStatus::ok);
                CHECK(decoded.msg.slot == kRemoteSlotSentinel); CHECK(decoded.msg.request_id == 89);
                CHECK(decoded.msg.response_seq == seq++);
                terminal = decoded.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::terminal);
                if (terminal) { CHECK(decoded.body.size() == 1);
                    CHECK(decoded.body[0] == static_cast<uint8_t>(count > kRadminOpenBytes
                        ? RemoteTerminal::output_truncated : RemoteTerminal::completed)); }
                else { CHECK(n == decoded.body.size() + kRemoteOverheadOpenResponse);
                    assembled.insert(assembled.end(), decoded.body.begin(), decoded.body.end()); }
                remote_open_sent(t.st, 0);
            }
            CHECK(terminal); content.resize(count > kRadminOpenBytes ? kRadminOpenBytes : count); CHECK(assembled == content);
            CHECK(t.st.staging[0].expires_at_ms == 301000);
            CHECK(remote_session_expire(t.st, 300999) == 0); CHECK(remote_session_expire(t.st, 301000) == 1);
            CHECK(remote_session_staging_used(t.st) == 0);
            CHECK(remote_session_earliest_deadline(t.st) == UINT64_MAX);
        }
    }
}

TEST_CASE("§radmin-7b2/control prepare is immutable and busy notices bind mutable details before force install") {
    Target t; t.provision(3);
    const uint8_t line[] = {'s','t','a','t','u','s'};
    const auto execute = [&](uint8_t slot, uint64_t id, uint32_t src) {
        const auto r = t.deliver(t.request(slot, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), id, line, src,
                                            same_layer_cmd()), src);
        CHECK(r.verdict == RemoteAdmitVerdict::admit);
        CHECK(remote_transcript_reserve(t.st, r.seen_index, kRadminChunkBytes));
        remote_transcript_append(t.st, r.seen_index, line, sizeof line);
        remote_transcript_complete(t.st, r.seen_index, RemoteTerminal::completed); return r;
    };
    const auto own1 = execute(0, 1, 11); const auto own2 = execute(0, 2, 11);
    const auto other1 = execute(1, 3, 12); const auto other2 = execute(2, 4, 13);
    remote_transcript_sent(t.st, other1.seen_index); // nonzero surviving cursor is load-bearing (B386)
    const auto control_wire = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 10, {}, 11, same_layer_cmd());
    CHECK(t.deliver(control_wire, 11).verdict == RemoteAdmitVerdict::control_admitted);
    RadminControlView view{}; CHECK(remote_control_next(t.st, view));
    uint8_t completed = 0; CHECK(remote_control_check(t.st, view, completed) == RadminControlDecision::busy);
    CHECK(completed == 2);
    RemoteSessionInstall plan{}; bool install = true; uint8_t bytes[34] = {}; size_t n = 0;
    auto before = t.st;
    CHECK(remote_control_prepare(t.st, view, 0, bytes, n, plan, install) == RemoteStatus::ok);
    CHECK_FALSE(install); CHECK(n == kRemoteOverheadAdmissionResult); CHECK(bytes[11] == 2);
    CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
    const std::vector<uint8_t> busy2(bytes, bytes + n);
    remote_control_release(t.st, view);
    CHECK(t.deliver(t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::response_ack), 1, {}, 11, same_layer_cmd()), 11).verdict
          == RemoteAdmitVerdict::ack_released);
    CHECK(t.deliver(control_wire, 11).verdict == RemoteAdmitVerdict::control_admitted);
    CHECK(remote_control_next(t.st, view));
    CHECK(remote_control_prepare(t.st, view, 0, bytes, n, plan, install) == RemoteStatus::ok);
    CHECK(bytes[11] == 1); CHECK(std::vector<uint8_t>(bytes, bytes + n) != busy2);
    RemoteRxResult wire{}; wire.controller_slot = 0; wire.source_hash = 11; wire.route = view.route;
    wire.reply_len = n; std::memcpy(wire.reply, bytes, n); uint8_t pt[32] = {}; RemoteDecoded decoded{};
    CHECK(t.decode_result(wire, decoded, pt) == RemoteStatus::ok);
    CHECK(decoded.msg.admission_code == RemoteAdmission::session_busy); CHECK(decoded.msg.admission_detail == 1);
    for (const uint8_t at : {uint8_t(9), uint8_t(10), uint8_t(11)}) {
        wire.reply[at] ^= 1;
        CHECK(t.decode_result(wire, decoded, pt) != RemoteStatus::ok);
        wire.reply[at] ^= 1;
    }
    remote_control_release(t.st, view);
    CHECK(t.deliver(t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::force_rollover), 11, {}, 11, same_layer_cmd()), 11).verdict
          == RemoteAdmitVerdict::control_admitted);
    CHECK(remote_control_next(t.st, view));
    before = t.st;
    uint8_t other_wire[2][kRadminBodyBytes] = {}; size_t other_n[2] = {};
    CHECK(remote_transcript_encode(t.st, other1.seen_index, other_wire[0], other_n[0]) == RemoteStatus::ok);
    CHECK(remote_transcript_encode(t.st, other2.seen_index, other_wire[1], other_n[1]) == RemoteStatus::ok);
    CHECK(remote_control_prepare(t.st, view, 999, bytes, n, plan, install) == RemoteStatus::ok);
    CHECK(install); CHECK(n == kRemoteOverheadRolloverResult); CHECK(plan.epoch[0] == 999);
    CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
    wire.reply_len = n; std::memcpy(wire.reply, bytes, n);
    CHECK(t.decode_result(wire, decoded, pt) == RemoteStatus::ok);
    CHECK(decoded.msg.admin_epoch == 999); CHECK(decoded.msg.abandoned_count == 1);
    remote_session_install(t.st, plan);
    CHECK(t.st.epoch[0] == 999); CHECK(t.st.epoch[1] == before.epoch[1]); CHECK(t.st.epoch[2] == before.epoch[2]);
    CHECK(remote_session_seen_find(t.st, 0, 1) == kRadminNoSlot); CHECK(remote_session_seen_find(t.st, 0, 2) == kRadminNoSlot);
    uint8_t which = 0;
    for (const auto& r : {other1, other2}) {
        const auto ti = t.st.seen[r.seen_index].record.transcript_slot;
        auto expected = before.transcripts[ti]; --expected.order;
        CHECK(std::memcmp(&t.st.transcripts[ti], &expected, sizeof expected) == 0);
        CHECK(std::memcmp(&t.st.seen[r.seen_index], &before.seen[r.seen_index], sizeof(SeenEntry)) == 0);
        for (uint16_t ci = expected.first_chunk; ci < kRadminChunkSlots; ci = t.st.chunks[ci].next)
            CHECK(std::memcmp(&t.st.chunks[ci], &before.chunks[ci], sizeof(TranscriptChunk)) == 0);
        uint8_t after[kRadminBodyBytes] = {}; size_t an = 0;
        CHECK(remote_transcript_encode(t.st, r.seen_index, after, an) == RemoteStatus::ok);
        CHECK(an == other_n[which]); CHECK(std::memcmp(after, other_wire[which], an) == 0); ++which;
    }
    CHECK(remote_transcript_next(t.st) == other1.seen_index);
    CHECK(t.deliver(control_wire, 11).verdict == RemoteAdmitVerdict::silent_auth_failed);
    (void)own1; (void)own2;
}

TEST_CASE("§radmin-7b2/counters saturation preserves all state except the named scalar and invalidation preserves totals") {
    Target t; t.provision(1);
    for (const auto counter : {RadminCounter::inbound_refusal, RadminCounter::open_rate_refusal,
                              RadminCounter::transcript_exhaustion, RadminCounter::response_enqueue_failure,
                              RadminCounter::response_seal_failure}) {
        for (unsigned i = 0; i < 65537; ++i) remote_counter_increment(t.st, counter);
    }
    auto before = t.st;
    CHECK(t.deliver({}, 0).verdict == RemoteAdmitVerdict::silent_bad_request);
    CHECK(std::memcmp(&t.st, &before, sizeof before) == 0);
    const auto counters = remote_counters(t.st);
    CHECK(counters.inbound_refusal == UINT16_MAX); CHECK(counters.open_rate_refusal == UINT16_MAX);
    CHECK(counters.transcript_exhaustion == UINT16_MAX); CHECK(counters.response_enqueue_failure == UINT16_MAX);
    CHECK(counters.response_seal_failure == UINT16_MAX);
    RemoteSessionInstall plan{}; plan.epoch_set[0] = true; plan.epoch[0] = 123;
    remote_session_install(t.st, plan);
    auto after = remote_counters(t.st); CHECK(std::memcmp(&after, &counters, sizeof after) == 0);
    plan = {}; plan.clear_root = true; remote_session_install(t.st, plan);
    after = remote_counters(t.st); CHECK(std::memcmp(&after, &counters, sizeof after) == 0);
    remote_session_clear(t.st); after = remote_counters(t.st); const RadminCounters empty{};
    CHECK(std::memcmp(&after, &empty, sizeof after) == 0);
}

TEST_CASE("§radmin-7b2/control prepare rejects short output stale views and equal epochs without touching admitted work") {
    Target t; t.provision(2, false);
    const uint8_t line[] = {'s','t','a','t','u','s'};
    const auto admitted = t.deliver(t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, line, 11,
                                              same_layer_cmd()), 11);
    CHECK(admitted.verdict == RemoteAdmitVerdict::admit); CHECK(admitted.ingress_index == kRadminIngressGeneral);
    const auto control = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover), 2, {}, 11, same_layer_cmd());
    CHECK(t.deliver(control, 11).verdict == RemoteAdmitVerdict::control_admitted);
    RadminControlView view{}; CHECK(remote_control_next(t.st, view));
    const auto before = t.st; uint8_t bytes[34]; size_t n = 99; bool install = true; RemoteSessionInstall plan{};
    CHECK(remote_control_prepare(t.st, view, 999, {bytes, 33}, n, plan, install) != RemoteStatus::ok);
    CHECK_FALSE(install); CHECK(n == 0); CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
    for (const uint64_t draw : {uint64_t(0), view.epoch}) {
        CHECK(remote_control_prepare(t.st, view, draw, bytes, n, plan, install) == RemoteStatus::ok);
        CHECK_FALSE(install); CHECK(n == kRemoteOverheadAdmissionResult);
        CHECK(bytes[10] == static_cast<uint8_t>(RemoteAdmission::preparation_failed));
        CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
    }
    for (uint8_t which = 0; which < 6; ++which) {
        auto stale = view;
        switch (which) {
            case 0: stale.request_ctl ^= 0x10; break;
            case 1: ++stale.request_id; break;
            case 2: ++stale.epoch; break;
            case 3: ++stale.source_hash; break;
            case 4: ++stale.route.origin; break;
            case 5: ++stale.slot; break;
        }
        CHECK(remote_control_prepare(t.st, stale, 999, bytes, n, plan, install) == RemoteStatus::bad_pairing);
        CHECK_FALSE(install); CHECK(n == 0); remote_control_release(t.st, stale);
        CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
    }
    CHECK(remote_control_prepare(t.st, view, 999, bytes, n, plan, install) == RemoteStatus::ok);
    CHECK(install); CHECK(n == 34); CHECK(bytes[17] == 0); // cancelled admission is NOT an abandoned transcript
    remote_session_install(t.st, plan);
    CHECK(remote_session_seen_used(t.st) == 0); CHECK(remote_session_ingress_used(t.st) == 0);
    CHECK(t.st.epoch[0] == 999); CHECK(t.st.epoch[1] == before.epoch[1]);
}

TEST_CASE("§radmin-7b2/control target-wide executing guard includes a labelled paused open capture") {
    Target t; t.provision(1); const uint8_t line[] = {'s','t','a','t','u','s'};
    CHECK(t.deliver(t.open_request(1, line), 22).verdict == RemoteAdmitVerdict::open_staged);
    CHECK(remote_open_reserve(t.st, 0, 222)); // labelled paused-open-executor fixture; no natural RX interleaving claimed
    for (const auto opcode : {RemoteCmdOpcode::safe_rollover, RemoteCmdOpcode::force_rollover}) {
        CHECK(t.deliver(t.request(0, static_cast<uint8_t>(opcode), 2, {}, 11, same_layer_cmd()), 11).verdict
              == RemoteAdmitVerdict::control_admitted);
        RadminControlView view{}; CHECK(remote_control_next(t.st, view)); const auto before = t.st;
        uint8_t count = 99; CHECK(remote_control_check(t.st, view, count) == RadminControlDecision::executing);
        uint8_t bytes[34]; size_t n = 0; bool install = true; RemoteSessionInstall plan{};
        CHECK(remote_control_prepare(t.st, view, 999, bytes, n, plan, install) == RemoteStatus::ok);
        CHECK_FALSE(install); CHECK(n == 28); CHECK(bytes[10] == static_cast<uint8_t>(RemoteAdmission::executing));
        CHECK(bytes[11] == 0); CHECK(std::memcmp(&before, &t.st, sizeof before) == 0);
        remote_control_release(t.st, view); CHECK(t.st.open[0].phase == static_cast<uint8_t>(OpenPhase::capturing));
    }
}

TEST_CASE("§radmin-7b2/refusal accounting changes exactly the named scalar and fixed notices own no rows") {
    Target t; t.provision(1); const uint8_t line[] = {'s','t','a','t','u','s'};
    const auto req = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 1, line, 11, same_layer_cmd());
    CHECK(t.deliver(req, 11).verdict == RemoteAdmitVerdict::admit);
    const auto new_id = t.request(0, static_cast<uint8_t>(RemoteCmdOpcode::auth_execute), 2, line, 11, same_layer_cmd());
    auto expected = t.st; ++expected.inbound_refusal;
    const auto notice = t.deliver(new_id, 11);
    CHECK(notice.verdict == RemoteAdmitVerdict::ingress_full); CHECK(notice.has_reply); CHECK(notice.reply_len == 28);
    CHECK(std::memcmp(&expected, &t.st, sizeof expected) == 0);
    RemoteDecoded d{}; uint8_t pt[32] = {}; CHECK(t.decode_result(notice, d, pt) == RemoteStatus::ok);
    CHECK(d.msg.admission_code == RemoteAdmission::ingress_full); CHECK(d.msg.request_ctl == new_id[0]);
    RemoteRxInput in{}; in.now_ms = 1000; in.outer_type = DATA_TYPE_REMOTE_CMD; in.body = new_id;
    in.source = {true, 11}; in.request_carrier = same_layer_cmd(); in.reply_carrier = same_layer_resp();
    in.route.carrier = static_cast<uint8_t>(RadminCarrierKind::same_layer); in.reply_permitted = false;
    RemoteRxResult out{}; ++expected.inbound_refusal;
    remote_session_receive(t.st, in, out); CHECK(out.verdict == RemoteAdmitVerdict::ingress_full); CHECK_FALSE(out.has_reply);
    CHECK(std::memcmp(&expected, &t.st, sizeof expected) == 0);
    in.source.present = false; ++expected.inbound_refusal; remote_session_receive(t.st, in, out);
    CHECK(out.verdict == RemoteAdmitVerdict::silent_no_source); CHECK_FALSE(out.has_reply);
    CHECK(std::memcmp(&expected, &t.st, sizeof expected) == 0);
    auto tampered = req; tampered.back() ^= 1; in.body = tampered; in.source.present = true;
    ++expected.inbound_refusal; remote_session_receive(t.st, in, out);
    CHECK(out.verdict == RemoteAdmitVerdict::silent_auth_failed); CHECK_FALSE(out.has_reply);
    CHECK(std::memcmp(&expected, &t.st, sizeof expected) == 0);
}

TEST_CASE("§radmin-73/control armed or preparing promise is target-wide executing; unowned force remains same-slot") {
    for(const auto phase:{RemoteActionPhase::preparing,RemoteActionPhase::prepared,RemoteActionPhase::armed,RemoteActionPhase::due})
    for(uint8_t slot:{uint8_t(0),uint8_t(1)})
    for(const auto opcode:{RemoteCmdOpcode::safe_rollover,RemoteCmdOpcode::force_rollover}) {
        // A paused operator occupies general ingress, leaving control ingress available.
        Target t;t.provision(2,phase!=RemoteActionPhase::preparing);const uint8_t line[]={'s','t','a','t','u','s'};
        const auto request=t.request(0,static_cast<uint8_t>(RemoteCmdOpcode::auth_execute),1,line,11,same_layer_cmd());
        CHECK(t.deliver(request,11).verdict==RemoteAdmitVerdict::admit);
        const auto si=remote_session_seen_find(t.st,0,1);CHECK(si<kRadminSeenSlots);if(si>=kRadminSeenSlots)return;
        CHECK(remote_transcript_reserve(t.st,si,206));CHECK(remote_action_reserve(t.st,0,1,2,0,7006)==RemoteTerminal::scheduled);
        if(phase!=RemoteActionPhase::preparing)remote_transcript_complete(t.st,si,RemoteTerminal::scheduled);
        if(phase==RemoteActionPhase::armed||phase==RemoteActionPhase::due)remote_action_owned(t.st,si,100);
        if(phase==RemoteActionPhase::due)remote_action_expire(t.st,7106);
        CHECK(t.st.action.phase==phase);
        CHECK(t.deliver(t.request(slot,static_cast<uint8_t>(opcode),2,{},11,same_layer_cmd()),11).verdict==RemoteAdmitVerdict::control_admitted);
        RadminControlView view{};CHECK(remote_control_next(t.st,view));uint8_t count=99;
        const auto decision=remote_control_check(t.st,view,count);
        if(phase!=RemoteActionPhase::prepared)CHECK(decision==RadminControlDecision::executing);
        else if(slot==0&&opcode==RemoteCmdOpcode::safe_rollover)CHECK(decision==RadminControlDecision::busy);
        else CHECK(decision==RadminControlDecision::ready);
        const auto before=t.st;uint8_t bytes[34];size_t n=0;bool install=false;RemoteSessionInstall plan{};
        CHECK(remote_control_prepare(t.st,view,999,bytes,n,plan,install)==RemoteStatus::ok);
        CHECK(memcmp(&before,&t.st,sizeof before)==0);
        if(install) {remote_session_install(t.st,plan);CHECK(t.st.action.phase==(slot==0?RemoteActionPhase::none:phase));}
        else CHECK(t.st.action.phase==phase);
    }
}
