// MeshRoute — test/radmin_0e_candidate_types.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#pragma once
//
// ★★ REMOTE-ADMIN v2 SLICE 0e-B — CANDIDATE VALUE TYPES FOR MEASUREMENT (R-RA-2). ⛔ NOT PRODUCTION, NOT AN API.
//
// The owner's ruling R-RA-2 is that nothing can be measured until the candidate structs are written:
//     "Nothing exists yet, so nothing can be measured until the candidate structs are written. Proposal: write them
//      as value types in the design, then measure with the existing ABI probe on host, ARM and Xtensa, exactly as
//      B278 S0 measured the 32-byte row. […] RAM on the gateway build is at 83 percent, and the timer wheel is
//      full."
//
// ⛔ THIS HEADER LIVES UNDER `test/` ON PURPOSE. Slice 0e lands NO production type. Nothing here may be included by
//    `lib/` or `src/`, nothing here becomes a `meshroute::Node` member, and no capacity below is a decision. Its two
//    ONLY consumers are `test/test_radmin_characterization_0e.cpp` and the probe TU that
//    `tools/probe_board_abi.py --extra-pins tools/radmin_0e_abi_pins.json` generates.
//
// ---- WHAT A `sizeof` HERE DOES AND DOES NOT SAY ----------------------------------------------------------------
// It says: how many bytes ONE row of this shape costs in this ABI. Aggregate cost is `sizeof` × the capacity the
// OWNER later rules (R-RA-10), and the report multiplies it out at CANDIDATE row counts only.
// It does NOT say: that any object is resident. Residency is decided by the production slices (3/4/5/7b/8a), and
// the honest measurement of residency is the per-board `RAM_used` diff, exactly as `tools/probe_board_abi.py`'s own
// header says of its `T` column. A profile that never admits an authenticated request pays none of this.
//
// ---- WHY EVERY FIELD IS FIXED-WIDTH AND THE STRUCTS ARE POINTER-FREE -------------------------------------------
// Deliberate, and it is a measurable property rather than a style note: a record whose every member is a
// `uintN_t` or an array of them has the SAME layout on x86-64, ARM Cortex-M4 (AAPCS) and Xtensa ESP32-S3, so the
// owner's capacity ruling costs the same bytes on all three and no board can be surprised later. The probe MEASURES
// that claim on all three ABIs rather than this comment asserting it. ⛔ If a production slice later stores a
// pointer or a `size_t` in one of these rows, that invariant is gone and the measurement must be retaken.
//
// ---- FIELD ORDER --------------------------------------------------------------------------------------------
// Widest first (u64 → u32 → u16 → u8 → arrays of u8), so the tail padding is visible and minimal. Any padding the
// probe measures is reported in the slice evidence with its offset explanation.
//
// ---- PROVENANCE OF EVERY WIDTH ---------------------------------------------------------------------------------
// Each field cites the design section that fixes it. ⛔ No field is invented to hit a size, and no two owners are
// merged into one record to make a number look better (0e-B, verbatim).

#include <cstddef>
#include <cstdint>

namespace radmin0e {

// ---------------------------------------------------------------------------------------------------------------
// CANDIDATE CONSTANTS. ⛔ Every one of these is a CANDIDATE, not a decision. The two that are derived rather than
// proposed say so; the rest are the smallest value that can hold what the design already fixes.
// ---------------------------------------------------------------------------------------------------------------

// DERIVED, not chosen: the largest RPC-body cap any v2 carrier offers — the registered-mobile → own-home wrapper's
// 241-byte inner minus its 10 immutable bytes. `test_radmin_characterization_0e.cpp` re-derives it through the real
// packer and static_asserts this constant against that derivation, so the two cannot drift.
inline constexpr uint16_t kCandidateMaxCarrierBodyBytes = 231;

// DERIVED, not chosen: §8.11's sealed `RESPONSE_ACK` body — ctl 1 + request_id 8 + tag 16.
inline constexpr uint16_t kCandidateSealedAckBytes = 25;

// The sealed authenticated execute request a controller must retain verbatim for retry (§15: "exact sealed request
// bytes"). Bounded by the largest carrier, because a request that does not fit its carrier was never sent.
inline constexpr uint16_t kCandidateSealedRequestBytes = kCandidateMaxCarrierBodyBytes;

// One response frame's worth of authenticated ciphertext (§8.7: envelope 26, so the payload is cap − 26).
inline constexpr uint16_t kCandidateResponseChunkBytes =
    static_cast<uint16_t>(kCandidateMaxCarrierBodyBytes - 26);

// One inbound RPC body as it arrives, before the target decides anything (§15 bounded ingress operation).
inline constexpr uint16_t kCandidateIngressBodyBytes = kCandidateMaxCarrierBodyBytes;

// §6.1/§8.4: the FULL Ed25519 public key. The short hash is never authorization (§8.4, verbatim).
inline constexpr uint8_t kEdPubBytes = 32;
// §7.1: BLAKE2b-512 truncated to 32 for keys.
inline constexpr uint8_t kKeyBytes = 32;

// ===============================================================================================================
// CONTROLLER-SIDE CANDIDATES (design §15, controller bullet list)
// ===============================================================================================================

// §15: "a bounded pending-request table binding local transport, target-administration full key, stable local
// SOURCE_HASH, selected local credential, target ACL slot, epoch, request ID, exact sealed request bytes, and retry
// state." ⛔ The SEALED BYTES ARE DELIBERATELY NOT IN THIS STRUCT — §15 lists one obligation, but "inline" and
// "reference into a pool" are two different resource shapes with very different aggregate costs, and 0e's job is to
// price both rather than to pick. The two wrappers below are those shapes; this is the part they share.
struct PendingRequestCore {
    uint64_t request_id;                     // §8.2 random u64, clear but authenticated
    uint64_t admin_epoch;                    // §8.6 the epoch this request was sealed under
    uint32_t source_hash;                    // §8.1 the controller's stable SOURCE_HASH (clear, AEAD-bound)
    uint32_t outcome_deadline_ms;            // §13 remote_disruptive_outcome_deadline_ms bookkeeping
    uint32_t next_retry_ms;                  // retry state
    uint16_t sealed_len;                     // the EXACT sealed length; a body cap needs 8 bits + the 231 bound
    uint8_t  target_admin_pub[kEdPubBytes];  // §6.3 the stable target administration identity, in full (§8.4)
    uint8_t  controller_pub[kEdPubBytes];    // §15 "selected local credential", in full — never inferred from UI state
    uint8_t  acl_slot;                       // §8.1 slot 0..9
    uint8_t  local_transport;                // §8.10 USB serial vs secured BLE — the sink the answer is owed to
    uint8_t  carrier;                        // §14 which RemoteCarrier this request flew on
    uint8_t  retries;                        // retry state
    uint8_t  state;                          // sent / awaiting-terminal / complete / undeliverable
    uint8_t  flags;                          // e2e-ACK requested (`-a`), mutating-command, confirmation-token seen
};

// SHAPE A: the sealed request stored INLINE in the row. Simplest; the aggregate is capacity × this size.
struct PendingRequestInline {
    PendingRequestCore core;
    uint8_t sealed[kCandidateSealedRequestBytes];
};

// SHAPE B: the sealed request stored in a SEPARATE pool, referenced by index. The row is small; the pool is sized
// independently, so a table deeper than the pool is possible (and must then refuse, never evict — §15).
struct PendingRequestRef {
    PendingRequestCore core;
    uint16_t sealed_slot;                    // index into a SealedRequestSlot pool; 0xFFFF = none
};

struct SealedRequestSlot {
    uint8_t  bytes[kCandidateSealedRequestBytes];
    uint16_t len;
    uint8_t  in_use;
};

// §15: "a bounded session/discovery cache keyed by both full target and full selected-controller public keys."
// ⛔ Both keys in full, because §8.4 makes the target ACL lookup exact and a short hash is never authorization.
struct SessionCacheEntry {
    uint64_t admin_epoch;                    // §8.6 the epoch the session key was derived under (§7.1)
    uint32_t last_used_ms;
    uint8_t  target_admin_pub[kEdPubBytes];  // half the key
    uint8_t  controller_pub[kEdPubBytes];    // the other half — a cache keyed by one alone would cross credentials
    uint8_t  base_key[kKeyBytes];            // §7.1 label || shared32 || controller_ed_pub32 || target_admin_ed_pub32
    uint8_t  session_key[kKeyBytes];         // §7.1 label || base_key32 || admin_epoch_le64
    uint8_t  acl_slot;                       // §8.6 the slot the bootstrap response reported
    uint8_t  valid;
};

// §15: "bounded authenticated response assembly". The HEADER only — the received bytes live in chunks, so the
// owner can price "one big buffer" and "N chunks" separately instead of inheriting one hidden aggregate.
struct ResponseAssemblyHeader {
    uint64_t request_id;                     // §8.7 the response's own id; an unmatched response is refused
    uint32_t started_ms;
    uint32_t bytes_used;
    uint16_t first_chunk;                    // head of this assembly's chunk list; 0xFFFF = none
    uint8_t  next_seq;                       // §8.7 response_seq, contiguous from 0
    uint8_t  terminal_seen;                  // §8.9 the terminal frame closed the series
    uint8_t  result_code;                    // §8.9 completed / scheduled / refused / …
    uint8_t  in_use;
};

struct ResponseChunk {
    uint8_t  bytes[kCandidateResponseChunkBytes];
    uint16_t len;
    uint16_t next;                           // 0xFFFF = end of list
};

// §8.10/§15: "retained-BLE-result capacity". The complete plaintext transcript is retained until the COMPANION
// acknowledges it; a disconnect is not an acknowledgement. Header only, same reason as the assembly.
struct RetainedResultHeader {
    uint64_t request_id;
    uint32_t completed_ms;
    uint32_t bytes_total;
    uint16_t first_chunk;
    uint8_t  local_transport;                // §8.10 the sink that must accept it
    uint8_t  delivered;                      // local delivery accepted -> the target ACK may now be sent
    uint8_t  reoffer_from_zero;              // §8.10 after a secured-BLE reconnect
    uint8_t  in_use;
};

// §8.10/§15: "compact response-ACK debt". The EXACT sealed ACK is retained and retried opportunistically; the
// controller must not infer target receipt from having transmitted one.
struct AckDebtEntry {
    uint64_t request_id;                     // §8.5 RESPONSE_ACK reuses the acknowledged request's id
    uint32_t next_retry_ms;
    uint8_t  target_admin_pub[kEdPubBytes];  // which target the debt is owed to
    uint8_t  sealed_ack[kCandidateSealedAckBytes];   // §8.11: exactly 25 bytes
    uint8_t  acl_slot;
    uint8_t  retries;
    uint8_t  in_use;
};

// ===============================================================================================================
// TARGET-SIDE CANDIDATES (design §15, target bullet list)
// ===============================================================================================================

// §10/§15: the seen-request / fingerprint record that makes execution at-most-once and lets an exact retry replay
// the transcript instead of re-executing.
struct SeenRequestRecord {
    uint64_t request_id;                     // §9: the identity of the logical operation
    uint64_t admin_epoch;                    // §13: an old epoch cannot match after a reboot
    uint32_t first_seen_ms;
    uint32_t source_hash;                    // §8.1 the controller's stable SOURCE_HASH, AEAD-bound
    uint8_t  controller_slot;                // §8.1 slot 0..9
    uint8_t  result_code;                    // §8.9 the terminal result already returned
    uint8_t  state;                          // admitted / executing / completed / acknowledged
    uint8_t  transcript_slot;                // which transcript holds the answer; 0xFF = released after ACK
};

// §15: "a bounded authenticated response transcript pool". Header + chunk, split for the same reason as the
// controller's assembly: the pool depth and the per-response byte budget are two separate owner choices.
struct TranscriptHeader {
    uint64_t request_id;
    uint32_t completed_ms;
    uint32_t bytes_total;
    uint16_t first_chunk;
    uint8_t  frames;                         // §8.9 at most 256 (255 output + the terminal)
    uint8_t  terminal_seq;
    uint8_t  controller_slot;
    uint8_t  acknowledged;                   // §8.10 the target may release the ciphertext once ACKed
    uint8_t  result_code;
    uint8_t  in_use;
};

struct TranscriptChunk {
    uint8_t  bytes[kCandidateResponseChunkBytes];
    uint16_t len;
    uint16_t next;
};

// §15: "a bounded inbound operation queue or an explicit one-at-a-time admission contract". Header + body slot.
struct IngressOperationHeader {
    uint64_t request_id;
    uint32_t received_ms;
    uint32_t source_hash;
    uint16_t body_slot;                      // 0xFFFF = none
    uint16_t body_len;
    uint8_t  ctl;                            // §8.1 the transmitted control byte: opcode nibble + slot nibble
    uint8_t  origin;                         // the DATA inner's origin id
    uint8_t  carrier;                        // §14 which carrier it arrived on (the answer must fit the return leg)
    uint8_t  partition;                      // §15 authenticated / open / bootstrap — the fixed resource partition
    uint8_t  state;
    uint8_t  in_use;
};

struct IngressBodySlot {
    uint8_t  bytes[kCandidateIngressBodyBytes];
    uint16_t len;
    uint8_t  in_use;
};

// §15: "bounded staging for an open multi-frame response" + the peer-local open admission element ("provisionally
// no more than one open response in flight per peer"). One element is both, because the bound IS per peer.
struct OpenStagingSlot {
    uint64_t request_id;                     // §8.3 a correlation id only — NOT an authenticator
    uint32_t peer_source_hash;               // the peer the per-peer admission bound applies to
    uint32_t opened_ms;
    uint16_t bytes_sent;
    uint8_t  next_seq;                       // §8.8 open response_seq
    uint8_t  verb;                           // §8.3 policy accepts exactly `status` and `routes`
    uint8_t  in_use;
};

// §13: the deferred disruptive-action record. Reserved BEFORE execution, so a command can never activate its
// effect before a truthful `scheduled` terminal has entered the reply path.
struct DeferredActionRecord {
    uint64_t request_id;                     // the operation that was promised
    uint32_t activate_at_ms;                 // now + cfg.remote_action_activation_ms, inside [floor, ceiling]
    uint8_t  action;                         // reboot / prep-restart / factory_reset / OTA entry / regen / retune
    uint8_t  controller_slot;
    uint8_t  armed;                          // the `scheduled` terminal was accepted into the response path
    uint8_t  ack_seen;                       // a valid RESPONSE_ACK may activate EARLIER, never later (§13)
    uint8_t  in_use;
};

// ===============================================================================================================
// TIMER-STRATEGY PRICING (0e-B, R-RA-2's second constraint). ⛔ NEITHER OPTION IS CHOSEN HERE.
//
// R-RA-2, verbatim: "the timer wheel is full. `timer_wheel.h` caps ids at 91 and the constants file records the last
// free id as consumed. Every deferred action, session expiry or seen-table sweep needs a timer, so the design must
// budget a cap increase or a shared scan timer up front."
//
// ⛔⛔ THERE IS NO FREE ID AND NO 92nd SLOT MAY BE SPENT. `lib/hal/timer_wheel.h:25` sets `kCap = 91`;
//     `lib/core/protocol_constants.h:403` states outright "(kCap 91, all consumed)"; `node.h:1528`'s
//     `kE2eAckDeadlineTimerId = 90` is the last one taken. ⇒ BOTH strategies below need `kCap` to GROW. The shared
//     scan does not avoid the increase — it makes it ONE id instead of one per expiring record class, which is the
//     actual difference the owner is ruling on.
//
// The wheel is `bool _active[kCap]` + `uint64_t _due[kCap]`, so one extra id costs one bool and one u64 plus
// whatever re-alignment the two arrays force. That is measured rather than argued: the mirror below is compiled on
// host, ARM and Xtensa beside the real `meshroute::TimerWheel`, and `test_radmin_characterization_0e.cpp` asserts
// the mirror at `kCap` reproduces `sizeof(meshroute::TimerWheel)` EXACTLY — so a drifted mirror reddens instead of
// quietly pricing a struct that is not the one in the image.
// ⓘ The other half of option 1 is the SCAN: `pop_due()` and `earliest_due()` are documented O(kCap) per pump
//   (`timer_wheel.h:33`/`:44`), so every added id also lengthens the per-loop scan. 0e reports that; it does not
//   price a loop it has not measured on metal.
// ---------------------------------------------------------------------------------------------------------------
template <uint32_t N>
struct TimerWheelStorageMirror {
    bool     active[N];
    uint64_t due[N];
};
using TimerWheelStorageAtCap    = TimerWheelStorageMirror<91>;   // today
using TimerWheelStorageAtCapP1  = TimerWheelStorageMirror<92>;   // option 2: ONE shared remote-admin expiry scan
using TimerWheelStorageAtCapP8  = TimerWheelStorageMirror<99>;   // option 1 sketch: eight per-class expiry ids

// ---------------------------------------------------------------------------------------------------------------
// A single struct whose layout DIFFERS across the three ABIs, kept beside the candidates on purpose.
// ⛔ It is NOT a candidate and nothing may ever store it: it is the control that proves the extra-pins measurement
//    can SEE an ABI difference at all. If every extra pin measured the same on all three targets and this one did
//    too, the measurement would be indistinguishable from a broken probe.
// ---------------------------------------------------------------------------------------------------------------
struct AbiWitnessNotACandidate {
    void*         p;      // 8 on the host, 4 on both boards
    unsigned long l;      // 8 on the host, 4 on both boards
    uint8_t       tag;
};

}  // namespace radmin0e
