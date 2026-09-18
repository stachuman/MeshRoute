#pragma once
#include <cstddef>
#include <cstdint>
#include "radmin_0e_candidate_types.h"
namespace s8model {
struct ReplyRoute { uint32_t target_hash; uint8_t hops[3]; uint8_t hop_count; };
using namespace radmin0e;
struct PendingCore {
    uintptr_t usb_sink;                       // supplied USB Print, must outlive request; zero for BLE
    uint64_t discovery_id;                    // fresh bootstrap ID while exact execute bytes stay retained
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
    uint8_t  flags;                          // e2e-ACK requested (`-a`), mutating-command, confirmation-token seen    ReplyRoute route;                         // immutable source-validation/carrier hints
    uint8_t credential_slot;                  // self or key0..key9, never inferred from mutable UI
    uint8_t target_book_slot;                 // existing in-use guards
};

struct PendingInline { PendingCore core; uint8_t sealed[kCandidateSealedRequestBytes]; };
struct SessionEntry {
    uint64_t admin_epoch;                    // §8.6 the epoch the session key was derived under (§7.1)
    uint32_t last_used_ms;
    uint8_t  target_admin_pub[kEdPubBytes];  // half the key
    uint8_t  controller_pub[kEdPubBytes];    // the other half — a cache keyed by one alone would cross credentials
    uint8_t  base_key[kKeyBytes];            // §7.1 label || shared32 || controller_ed_pub32 || target_admin_ed_pub32
    uint8_t  session_key[kKeyBytes];         // §7.1 label || base_key32 || admin_epoch_le64
    uint8_t  acl_slot;                       // §8.6 the slot the bootstrap response reported
    uint8_t  valid;    uint8_t credential_slot;
    uint8_t target_book_slot;
};

struct AssemblyHeader {
    uint64_t request_id;                     // §8.7 the response's own id; an unmatched response is refused
    uint32_t started_ms;
    uint32_t bytes_used;
    uint16_t first_chunk;                    // head of this assembly's chunk list; 0xFFFF = none
    uint8_t  next_seq;                       // §8.7 response_seq, contiguous from 0
    uint8_t  terminal_seen;                  // §8.9 the terminal frame closed the series
    uint8_t  result_code;                    // §8.9 completed / scheduled / refused / …
    uint8_t  in_use;    uint8_t result_domain;                    // terminal vs protocol error vs admission/local outcome
    uint8_t reserved;
    uint32_t result_detail;                   // scheduled delay / admission count, independent of output
};

struct RetainedHeader {
    uint64_t request_id;
    uint32_t completed_ms;
    uint32_t bytes_total;
    uint16_t first_chunk;
    uint8_t  local_transport;                // §8.10 the sink that must accept it
    uint8_t  delivered;                      // local delivery accepted -> the target ACK may now be sent
    uint8_t  reoffer_from_zero;              // §8.10 after a secured-BLE reconnect
    uint8_t  in_use;    uint8_t result_domain;
    uint8_t result_code;
    uint32_t result_detail;                   // survives assembly release and local replay
};

struct AckEntry {
    uint64_t request_id;                     // §8.5 RESPONSE_ACK reuses the acknowledged request's id
    uint32_t next_retry_ms;
    uint8_t  target_admin_pub[kEdPubBytes];  // which target the debt is owed to
    uint8_t  sealed_ack[kCandidateSealedAckBytes];   // §8.11: exactly 25 bytes
    uint8_t  acl_slot;
    uint8_t  retries;
    uint8_t  in_use;    uint32_t source_hash;                     // originating SOURCE_HASH, never rebuilt from mutable state
    ReplyRoute route;
    uint8_t credential_slot;
    uint8_t target_book_slot;
    uint8_t carrier;
    uint8_t reserved;
};

struct Counters { uint16_t request_table_pressure, assembly_failure, unmatched_response,
    auth_failure, local_result_pressure, radio_enqueue_failure; };
struct HistoricalState {
    radmin0e::PendingRequestInline pending[4]; radmin0e::SessionCacheEntry sessions[4];
    radmin0e::ResponseAssemblyHeader assemblies[2]; radmin0e::ResponseChunk chunks[8];
    radmin0e::RetainedResultHeader retained[2]; radmin0e::AckDebtEntry ack_debt[8];
};
struct CountersOnlyState { HistoricalState historical; Counters counters; };
struct ProposedState {
    PendingInline pending[4]; SessionEntry sessions[4]; AssemblyHeader assemblies[2];
    radmin0e::ResponseChunk chunks[8]; RetainedHeader retained[2]; AckEntry ack_debt[8]; Counters counters;
};
}
