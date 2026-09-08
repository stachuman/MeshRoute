// MeshRoute — lib/core/remote_session.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — THE TARGET'S AUTHENTICATED SESSION, ADMISSION AND BOOTSTRAP STATE.
//     Pure, allocation-free, `now`-driven. The ONE `MR_FEAT_RADMIN_ACCEPT` state block `Node` owns, plus the
//     classifier/crypto/expiry that operate on it. It is the FIRST CONSUMER of `remote_codec.h`.
//
// Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§7.1-7.3, 9, 10,
// 15, 19 item 5; the brief `docs/superpowers/plans/2026-09-06-radmin-slice5-target-session.md` §§4.1-4.7 (the
// Author decisions this file implements verbatim); rulings R-RA-13 (`SOURCE_HASH` mandatory), R-RA-22 (the
// managed profile + the ONE shared expiry timer), R-RA-27 (the owned pre-tail receive), R-RA-31 (bootstrap is
// answered ON AIR and the native/gateway `sizeof(Node)` re-pin is authorized).
//
// ⛔ WHAT THIS MODULE DELIBERATELY DOES **NOT** DO — Slice 5's fence, spelled out so no later reader mistakes a
//    state module for the RPC:
//      * ⛔ NO EXECUTION. A verdict is what Slice 7b acts on; nothing here dispatches a command, runs a
//        whitelist or knows a dispatcher. There is no `CommandContext` (Slice 6) and no controller state (8a).
//      * Slice 7b-1 adds bounded transcripts, OUTPUT/TERMINAL sealing, retry replay and ACK release.
//        MISSING: open replies, rollover and protocol-error producers (7b-2), deferred actions (7b-3).
//        The main-loop firmware executor owns dispatch; this module owns bytes and lifecycle only.
//      * ⛔ NO NV, NO `src/` INCLUDE, NO HEAP, NO TIMER CALL AND NO HAL CALL. Time arrives as an explicit
//        `now_ms`; entropy arrives as an already-drawn, already-checked epoch. `Node` owns the timer and the
//        transmitter; `src/` owns the durable records and the prepare/commit ordering.
//      * ⛔ NO SECOND CRYPTO IMPLEMENTATION. Every key comes from `remote_ecdh_shared` / `remote_kdf_base` /
//        `remote_kdf_session`, every body from `remote_body_decode` / `remote_body_encode`, every capacity from
//        `remote_body_cap` (U1/U2). This file adds ONE thing the codec deliberately lacks: the ADAPTER that
//        presents the resident administration secret to `remote_ecdh_shared`'s `Identity&` parameter, as a
//        wiped transient.
//
// ⚠⚠ WHAT A CAPTURED `ReplyRoute` IS AND IS NOT. It is VALIDATED ROUTING METADATA — the reversed leg an answer
//    must travel. It is ⛔ NOT AEAD-authenticated data: mutable relay/path headers are deliberately outside the
//    cryptographic binding (`remote_aad` binds `outer_type ‖ the clear RPC header ‖ source_hash` and nothing
//    else). Capturing it does not turn it into an authentication claim, and no admission decision may be made
//    from it. The AUTHENTICATED facts are the tag, the slot and the `SOURCE_HASH`.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // faithful two-lib: the gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstddef>
#include <cstdint>
#include <span>

#include "protocol_constants.h"   // gw_env_max_hops (the path width) + e2e_ack_deadline_xl_ms (the lifetime base)
#include "remote_codec.h"        // the codec this module CONSUMES — layouts, caps, KDFs, encode/decode (U1)

namespace MESHROUTE_NS {

// =====================================================================================================
// Capacities. R-RA-22's managed static/gateway profile through 7b-1; deferred actions remain 7b-3.
// =====================================================================================================
// The ACL image mirrors the codec's session-slot handles EXACTLY; `firmware_admin_acl.h` already binds the
// durable record's row count to the same authority, so there are not two independent tens.
inline constexpr uint8_t kRadminAclSlots = kRemoteSlotSessionMax + 1;   // 10

// ★★★ `N` = **16 TOTAL seen entries per target, SHARED across all ten ACL slots**, keyed by (slot, request_id).
//     ⛔ It is NOT 16 per slot (that would be 160 rows / 8 960 bytes). A single busy session can occupy all
//     sixteen; under sharing, the capacity left for any other credential depends on what the others hold. ⇒ the
//     automatic safe-rollover cadence (§7.3) is a SHARED budget and must be reported as one — ⛔ never as a
//     guaranteed sixteen requests for every concurrent controller. No quota and no eviction is introduced to
//     promise a fixed per-slot cadence: §10 forbids evicting a record whose session key is still valid.
inline constexpr uint8_t kRadminSeenSlots = 16;

// The authenticated ingress: TWO header/body pairs, HARD-PARTITIONED (§4.3). ⛔ They are not four operations.
inline constexpr uint8_t kRadminIngressSlots  = 2;
inline constexpr uint8_t kRadminIngressGeneral = 0;   // ordinary / operator execute
inline constexpr uint8_t kRadminIngressControl = 1;   // RESERVED: owner execute + authenticated session control

// The open/bootstrap staging: FOUR rows, partitioned THREE open + ONE bootstrap, with ⛔ no borrowing in either
// direction and ⛔ no borrowing from the authenticated ingress above.
inline constexpr uint8_t kRadminStagingSlots   = 4;
inline constexpr uint8_t kRadminOpenSlots      = 3;   // staging[0..2]
inline constexpr uint8_t kRadminBootstrapSlot  = 3;   // staging[3] — reserved, never lent to an open request

// ★ THE 233-BYTE BODY ARRAY IS **STORAGE, NOT AN ADMISSION CAP** (§4.3). `remote_body_cap` remains the ONE
//   capacity authority (232 same-layer, and the ruled smaller cross-layer sizes, plus each domain's
//   authenticating overhead). A body over ITS carrier's cap is REFUSED before any copy — ⛔ never clamped to
//   this number. The array is sized to the largest RPC body any legal carrier can present so a legal request
//   can never be refused by storage; that is the only relationship between the two numbers.
inline constexpr size_t kRadminBodyBytes = 233;

// =====================================================================================================
// §4.2 — the real state layout. Every offset/size/alignment below is `static_assert`ed in remote_session.cpp
// on native, ARM and Xtensa alike, so a field edit that forgets a byte cannot silently keep the old number.
// ⛔ `reserved` bytes are DECLARED members, not implicit tail padding: implicit padding is INDETERMINATE after
//    `T{}`, which would make any whole-record comparison unsound (the §AB1 `PeerLoc` lesson).
// =====================================================================================================

// One live ACL row — the firmware-installed image of `/mracl`'s durable row. Roles preserve Slice 3's typed
// values byte for byte (`mrnv::kAclRole*`), so the image and the record cannot drift.
inline constexpr uint8_t kRadminRoleEmpty    = 0;
inline constexpr uint8_t kRadminRoleOperator = 1;
inline constexpr uint8_t kRadminRoleOwner    = 2;

struct AdminAclRow {
    uint8_t ed_pub[32];   // @0  — the controller's FULL Ed25519 public key (the trust anchor, never a hash)
    uint8_t role;         // @32 — kRadminRole*
    uint8_t reserved;     // @33 — canonical zero
};

// The captured RETURN metadata of one admitted request. Same-layer zeroes the path bytes, the count and the
// cursor; cross-layer retains every validated received field. `origin` is DIAGNOSTIC ONLY and is ⛔ never a
// reply identity — the reply destination is the authenticated `SOURCE_HASH`.
struct ReplyRoute {
    uint8_t layer_ids[4];   // @0 — the PRESERVED full path, exactly as received (protocol::gw_env_max_hops)
    uint8_t n_layers;       // @4 — 0 = same-layer
    uint8_t cur;            // @5 — the received cursor
    uint8_t carrier;        // @6 — RadminCarrierKind: which leg it arrived on
    uint8_t origin;         // @7 — the DATA inner's 8-bit origin. DIAGNOSTIC. ⛔ Never an identity.
};
static_assert(protocol::gw_env_max_hops == 4,
              "remote_session.h: ReplyRoute::layer_ids mirrors protocol::gw_env_max_hops — re-derive both");

enum class RadminCarrierKind : uint8_t {
    none        = 0,
    same_layer  = 1,
    cross_layer = 2,
};

// §10: at-most-once execution and exact-retry classification. It retains the ORIGINAL AUTHENTICATED REQUEST TAG
// as the exact 128-bit request fingerprint — ⛔ NOT an R-RA-29 display fingerprint, ⛔ not a recomputed
// plaintext hash and ⛔ not a short routing hash — beside the stable logical controller `SOURCE_HASH` that the
// same request authenticated.
enum class SeenState : uint8_t {
    free         = 0,   // ★ ABSENCE IS THIS FLAG. ⛔ NEVER `request_id == 0` or `source_hash == 0`: the codec
                        //   permits numeric zero with presence true (R-RA-5 draws a legitimate all-zero id).
    admitted     = 1,
    executing    = 2,   // transcript reserved; the main-loop executor owns the one dispatch
    completed    = 3,   // immutable transcript and terminal retained until ACK/epoch invalidation
    acknowledged = 4,   // transcript released; same-epoch replay tombstone retained
};
inline constexpr uint8_t kRadminNoTranscript = 0xFF;

struct SeenRequestRecord {
    uint64_t request_id;       // @0
    uint64_t admin_epoch;      // @8  — §13: an old epoch cannot match after a reboot or a rotation
    uint32_t first_seen_ms;    // @16
    uint32_t source_hash;      // @20 — the AEAD-bound controller SOURCE_HASH
    uint8_t  request_tag[16];  // @24 — ★ THE EXACT 128-BIT AUTHENTICATED TAG, as received
    uint8_t  controller_slot;  // @40
    uint8_t  result_code;      // @41 — retained terminal result
    uint8_t  state;            // @42 — SeenState
    uint8_t  transcript_slot;  // @43 — pool index, or kRadminNoTranscript
    uint8_t  reserved[4];      // @44 — canonical zero
};

// The route is retained BESIDE the record and OUTLIVES the ingress row: a retry can never replace the
// first-admitted authenticated source or its captured return metadata.
struct SeenEntry {
    SeenRequestRecord record;   // @0
    ReplyRoute        route;    // @48
};

// One authenticated ingress reservation. `body_slot` pairs it 1:1 with an `IngressBodySlot`.
enum class IngressState : uint8_t {
    free     = 0,
    reserved = 1,   // body owned until execution completes or the pre-execution deadline expires
};

struct IngressOperationHeader {
    uint64_t   request_id;      // @0
    uint64_t   expires_at_ms;   // @8  — ABSOLUTE, checked/saturating at the edge
    uint32_t   source_hash;     // @16
    uint16_t   body_len;        // @20
    uint8_t    seen_index;      // @22 — the row this reservation was allocated WITH (atomically)
    uint8_t    ctl;             // @23 — the transmitted control byte
    ReplyRoute route;           // @24 — the ingress's OWN captured route for pending work
    uint8_t    partition;       // @32 — kRadminIngressGeneral / kRadminIngressControl. ⛔ No borrowing.
    uint8_t    state;           // @33 — IngressState
    uint8_t    body_slot;       // @34
    uint8_t    reserved[5];     // @35 — canonical zero
};

struct IngressBodySlot {
    uint8_t  bytes[kRadminBodyBytes];   // @0   — an OWNED copy; ⛔ never a span into PostAck or RX memory
    uint8_t  reserved;                  // @233 — canonical zero
    uint16_t len;                       // @234
};

enum class OpenStagingKind : uint8_t {
    free         = 0,
    open_execute = 1,   // an UNAUTHENTICATED open request: staged and expired, ⛔ never dispatched here
    bootstrap    = 2,   // R-RA-31: authenticated, answered, released on the checked send outcome
};

struct OpenStagingSlot {
    uint64_t   request_id;        // @0  — §8.3 a correlation id only, ⛔ NOT an authenticator
    uint64_t   expires_at_ms;     // @8
    uint32_t   peer_source_hash;  // @16 — the peer the per-source open bound applies to
    ReplyRoute route;             // @20
    uint8_t    kind;              // @28 — OpenStagingKind
    uint8_t    controller_slot;   // @29 — the AUTHENTICATED slot for a bootstrap; 0xFF for an open request
    uint8_t    reserved[2];       // @30 — canonical zero
};
inline constexpr uint8_t kRadminNoSlot = 0xFF;

// The three readiness values the ACCEPT boot line prints, and nothing else. ⛔ `ready` is a CONJUNCTION
// recomputed on every install, never a stored promise that can outlive its inputs.
enum class AdminReadiness : uint8_t {
    disabled       = 0,   // absent / invalid / io_failed prerequisites, or no usable occupied slot
    ready          = 1,
    entropy_failed = 2,   // ⛔ a cold-boot draw refused: acceptance stays OFF and NOTHING durable was touched
};

// R-RA-22 / R-RA-34: pool storage, not admission caps. The 206 is bound to the live same-layer
// remote_body_cap minus kRemoteOverheadAuthResponse by native tests (the cap is not constexpr).
inline constexpr uint8_t kRadminTranscriptSlots = 4;
inline constexpr uint8_t kRadminChunkSlots = 8;
inline constexpr uint16_t kRadminChunkBytes = 206;
inline constexpr uint16_t kRadminNoChunk = UINT16_MAX;
enum class TranscriptState : uint8_t { free, capturing, ready };
struct TranscriptHeader {
    uint64_t request_id;
    uint32_t bytes_total;          // retained bytes, not attempted writes after truncation
    uint16_t first_chunk;
    uint8_t frames;                // OUTPUT chunks + mandatory TERMINAL
    uint8_t next_seq_to_send;
    uint8_t controller_slot;
    uint8_t seen_index;
    uint8_t terminal;
    uint8_t state;
    uint16_t chunk_bytes;          // computed once at reservation for the actual return carrier
    uint8_t truncated;
    uint8_t order;                // 0..3 allocation order; no uptime-wrap-dependent scheduling
};
struct TranscriptChunk {
    uint8_t bytes[kRadminChunkBytes];
    uint16_t len = kRadminNoChunk; // free marker; an owned empty chunk has len == 0
    uint16_t next = kRadminNoChunk;
};

// =====================================================================================================
// THE ONE ACCEPT-ONLY STATE BLOCK. §4.2's frozen order — pair, ACL, status, epochs, seen, headers, bodies,
// staging — original offsets preserved. 7b-1 appends the 1776-byte pool and three named counters.
// ⛔ NO hidden resident counter, vtable or key cache lives here, and the derived
//    base/session keys are per-call WIPED TRANSIENTS — never members.
// =====================================================================================================
struct RemoteSessionState {
    uint8_t     admin_x_secret[32];              // @0   — SECRET. The ADMINISTRATION root's X25519 scalar.
    uint8_t     admin_ed_pub[32];                // @32  — the administration root's Ed25519 public key.
                                                 //        ⛔ NEVER the messaging identity (`_ed_pub`): they are
                                                 //        separate secrets with separate lifetimes, and
                                                 //        `regen` rotates the messaging one alone.
    AdminAclRow acl[kRadminAclSlots];            // @64  — 340: the LIVE image the firmware installs
    uint8_t     readiness;                       // @404 — AdminReadiness
    uint8_t     acl_occupied;                    // @405 — validated occupied rows (0..10)
    uint8_t     root_present;                    // @406 — 1 iff a non-degenerate administration pair is installed
    uint8_t     reserved0;                       // @407 — canonical zero
    uint64_t    epoch[kRadminAclSlots];          // @408 — 80: the per-slot session epoch. 0 = slot UNUSABLE.
    SeenEntry   seen[kRadminSeenSlots];          // @488 — 896
    IngressOperationHeader ingress[kRadminIngressSlots];  // @1384 — 80
    IngressBodySlot        body[kRadminIngressSlots];     // @1464 — 472
    OpenStagingSlot        staging[kRadminStagingSlots];  // @1936 — 128
    TranscriptHeader      transcripts[kRadminTranscriptSlots];
    TranscriptChunk       chunks[kRadminChunkSlots];
    uint16_t              transcript_exhaustion;
    uint16_t              response_enqueue_failure;
    uint16_t              response_seal_failure;
};

// Borrowed only during the main-loop service call; never retained by firmware or a Print adapter.
struct RadminIngressView {
    uint8_t seen_index = kRadminNoSlot;
    uint8_t slot = kRadminNoSlot;
    uint8_t role = kRadminRoleEmpty;
    uint64_t request_id = 0;
    std::span<const uint8_t> body{};
    ReplyRoute route{};
    RemoteCarrier reply_carrier{};
};
enum class RadminSend : uint8_t { none, queued, parked, refused };

[[nodiscard]] RemoteCarrier remote_reply_carrier(const ReplyRoute& route);
[[nodiscard]] bool remote_next_admitted(const RemoteSessionState& s, RadminIngressView& out);
[[nodiscard]] bool remote_transcript_reserve(RemoteSessionState& s, uint8_t seen_index, uint16_t chunk_bytes);
void remote_transcript_append(RemoteSessionState& s, uint8_t seen_index, const uint8_t* p, size_t n);
void remote_transcript_complete(RemoteSessionState& s, uint8_t seen_index, RemoteTerminal result);
// Returns the oldest transcript with a pending frame, as a SEEN index. No reservation or side effect.
[[nodiscard]] uint8_t remote_transcript_next(const RemoteSessionState& s);
[[nodiscard]] RemoteStatus remote_transcript_encode(const RemoteSessionState& s, uint8_t seen_index,
                                                    std::span<uint8_t> out, size_t& written);
void remote_transcript_sent(RemoteSessionState& s, uint8_t seen_index);

// =====================================================================================================
// §10 as VERDICTS. Slice 5 classifies; Slice 7b acts. Cases 1-5 are the design's; the rest are storage
// admission and the SILENT refusals §7.2 requires ("do not reveal ACL membership").
// =====================================================================================================
enum class RemoteAdmitVerdict : uint8_t {
    // ---- design §10, cases 1..5 ----------------------------------------------------------------------
    admit = 0,              // 1: id absent, capacity available -> seen row + ingress reserved ATOMICALLY
    replay_transcript,      // 2: id present, tag IDENTICAL, unacknowledged -> 7b resends; ⛔ never re-executes
    reject_id_reuse,        // 3: id present, tag DIFFERS -> ⛔ neither plaintext is ever dispatched
    already_acknowledged,   // 4: id present, ACK tombstone retained; the wire error producer belongs to 7b-2
    session_full,           // 5: the shared 16-row pool is full (⚠ REAL and reachable; no wire answer yet)
    // ---- R-RA-31: the ONE thing this slice answers on air --------------------------------------------
    bootstrap_answered,     // authenticated read-only bootstrap: response ENCODED. ⛔ No seen row, ⛔ no epoch change.
    // ---- storage admission that is NOT a §10 classification ------------------------------------------
    control_admitted,       // an authenticated session-control request holding its RESERVED ingress row.
                            // ⛔ It allocates NO execute seen row — seen exhaustion cannot consume its reservation.
    ingress_full,           // the request's OWN partition is full. ⛔ It never borrows the other one.
    open_staged,            // an open request holds one of the three open rows
    open_staging_full,      // the three open rows are taken. ⛔ The bootstrap row is never lent to them.
    bootstrap_staging_busy, // the ONE reserved bootstrap row is held. ⛔ It never borrows an open row either.
    open_peer_bound,        // this source already holds an open row (at most ONE per source; a new id does not evade it)
    // ---- SILENT refusals: no response, no oracle, no reservation, no state change ---------------------
    silent_not_ready,       // no administration root / no usable ACL
    silent_no_source,       // R-RA-13: SOURCE_HASH absent. ⛔ Never aliased to the 8-bit origin, never 0-as-absent.
    silent_bad_carrier,     // the received leg is not a real carrier shape
    silent_bad_request,     // ctl/opcode/slot/length the codec refuses
    silent_over_cap,        // the body exceeds THIS carrier's cap -> REFUSE before copy. ⛔ Never clamp.
    silent_no_acl_row,      // no live row holds that slot / that full key
    silent_bad_key,         // conversion or ECDH refused (every low-order point lands here), or epoch 0
    silent_auth_failed,     // the tag did not verify
    silent_reply_unbuildable,   // authenticated bootstrap whose response cannot be encoded for the return leg
    ack_released,              // authenticated ACK consumed without any ingress reservation
    ack_premature,
    ack_duplicate,
    ack_unknown,
};

// True iff the verdict is one the target must stay SILENT about (§7.2). Used by the caller to decide that
// nothing at all goes back on air.
[[nodiscard]] bool remote_admit_is_silent(RemoteAdmitVerdict v);
[[nodiscard]] const char* remote_admit_name(RemoteAdmitVerdict v);   // MR_EMIT label; sim-only, device-stripped

// What the receive path was handed. ⛔ Every span is CALLER-OWNED and is not retained: whatever this module
// keeps, it COPIES.
struct RemoteRxInput {
    uint64_t                 now_ms        = 0;
    uint8_t                  outer_type    = 0;    // the REAL outer DATA type byte
    std::span<const uint8_t> body{};               // the parsed unicast inner's body (cleartext RPC body)
    RemoteSource             source{};             // the carrier's SOURCE_HASH — presence and value SEPARATE
    RemoteCarrier            request_carrier{};    // the leg it ARRIVED on
    RemoteCarrier            reply_carrier{};      // the leg an answer would LEAVE on (the reversed shape)
    ReplyRoute               route{};              // the captured return metadata
};

// What the receive path decided. `reply` is the ONLY thing Slice 5 may put on air (R-RA-31).
struct RemoteRxResult {
    RemoteAdmitVerdict verdict         = RemoteAdmitVerdict::silent_not_ready;
    uint8_t            controller_slot = kRadminNoSlot;
    uint32_t           source_hash     = 0;
    uint64_t           request_id      = 0;
    uint8_t            seen_index      = kRadminNoSlot;
    uint8_t            ingress_index   = kRadminNoSlot;
    uint8_t            staging_index   = kRadminNoSlot;
    ReplyRoute         route{};        // the FIRST-ADMITTED route, not the retry's
    bool               has_reply       = false;
    uint8_t            reply_len       = 0;
    uint8_t            reply[kRemoteOverheadBootstrapResponse] = {};   // 33: [ctl][id 8][epoch 8][tag 16]
};

// =====================================================================================================
// The LIVE-INSTALL conversion path (§4.1). ⛔ ONE structure, ONE apply function: the firmware never writes a
// field of `RemoteSessionState` directly and never rebuilds the image member by member (U2).
// =====================================================================================================
struct RemoteSessionInstall {
    bool        set_root       = false;             // install a new administration pair (generate/rotate/recover/boot)
    uint8_t     x_secret[32]   = {};                // SECRET — the caller wipes its own copy
    uint8_t     ed_pub[32]     = {};
    bool        clear_root     = false;             // acceptance OFF: no root, no sessions
    bool        set_acl        = false;             // install the ten-row image below
    AdminAclRow acl[kRadminAclSlots] = {};
    uint64_t    epoch[kRadminAclSlots] = {};        // the value to install where `epoch_set[i]`
    bool        epoch_set[kRadminAclSlots] = {};    // ★ per slot: replace the epoch AND invalidate that slot's work.
                                                    //   false = the slot is UNCHANGED and keeps its epoch EXACTLY.
    bool        invalidate_all = false;             // a root change: every old session dies, the ACL survives
};

// Apply one prepared plan. ⛔ NON-FAILING by construction — every fallible step (validation, key derivation,
// the entropy draw) happened in the caller's PREPARE, before the durable save. It recomputes readiness.
void remote_session_install(RemoteSessionState& s, const RemoteSessionInstall& plan);

// ⛔ A COLD-BOOT ENTROPY REFUSAL, made explicit rather than spelled as "disabled". Acceptance stays OFF, the
//    root and every session are cleared, and NOTHING durable is touched by this (the caller owns NV).
void remote_session_mark_entropy_failed(RemoteSessionState& s);

// Wipe the whole block, secret included. Used by the ctor path and by an explicit teardown.
void remote_session_clear(RemoteSessionState& s);

[[nodiscard]] inline bool remote_session_accepting(const RemoteSessionState& s) {
    return s.readiness == static_cast<uint8_t>(AdminReadiness::ready);
}
[[nodiscard]] inline AdminReadiness remote_session_readiness(const RemoteSessionState& s) {
    return static_cast<AdminReadiness>(s.readiness);
}

// =====================================================================================================
// THE RECEIVE PATH (§6). Authentication happens BEFORE any lookup decision affects state.
// =====================================================================================================
void remote_session_receive(RemoteSessionState& s, const RemoteRxInput& in, RemoteRxResult& out);

// =====================================================================================================
// §4.5 — THE ONE SHARED EARLIEST-DEADLINE EXPIRY. ⛔ No per-class timer id; `Node` owns the single wheel slot.
// =====================================================================================================
// The pre-dispatch STAGING LIFETIME, derived from the existing maximum transport horizon rather than minted as
// a new magic literal. ⚠ IT IS A RESOURCE-HOLDING CEILING AND NOTHING ELSE: ⛔ not a seen/session timeout,
// ⛔ not an execution promise and ⛔ not an RPC terminal deadline. Slice 7b owns the active operation,
// transcript and deferred-action lifetimes.
inline constexpr uint32_t radmin_staging_lifetime_ms = protocol::e2e_ack_deadline_xl_ms;   // 300 000

// Checked/saturating absolute deadline. ⛔ No wrap, ever.
[[nodiscard]] uint64_t remote_session_deadline(uint64_t now_ms, uint32_t lifetime_ms);

// The earliest ACTIVE deadline across every expiring class, or `~0ull` when nothing pends.
[[nodiscard]] uint64_t remote_session_earliest_deadline(const RemoteSessionState& s);

// Release every row whose deadline has ELAPSED (`now >= expires_at`). Returns how many were released.
// B369 supersedes Slice 5's unconditional seen retention: expire a matching admitted/no-transcript row
// with its ingress. Executing/completed/acknowledged rows NEVER expire; their replay safety is epoch-bound.
uint8_t remote_session_expire(RemoteSessionState& s, uint64_t now_ms);

// Release ONE staging row explicitly. R-RA-31's bootstrap holds its reserved row across exactly one enqueue
// attempt and releases it on the CHECKED outcome — success or failure alike. ⛔ It is not an expiry shortcut and
// ⛔ it never touches a seen row. Out-of-range / already-free is a no-op.
void remote_session_staging_release(RemoteSessionState& s, uint8_t index);

// ---- introspection the tests and the boot line need (read-only; no allocation) -----------------------------
[[nodiscard]] uint8_t remote_session_seen_used(const RemoteSessionState& s);
[[nodiscard]] uint8_t remote_session_ingress_used(const RemoteSessionState& s);
[[nodiscard]] uint8_t remote_session_staging_used(const RemoteSessionState& s);
// The seen row for (slot, request_id), or kRadminNoSlot. ⛔ Reads only; it makes no admission decision.
[[nodiscard]] uint8_t remote_session_seen_find(const RemoteSessionState& s, uint8_t slot, uint64_t request_id);

}  // namespace MESHROUTE_NS
