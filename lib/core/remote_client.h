// MeshRoute — bounded remote controller state (R-RA-22/45). Pure; all adapters are call-scoped.
#pragma once
#include "remote_codec.h"
#include "remote_session.h" // shared transcript chunk capacity, pinned to the real response carrier
namespace MESHROUTE_NS {
inline constexpr size_t kRemoteClientSealedBytes = 231;
inline constexpr size_t kRemoteClientChunkBytes = kRadminChunkBytes;
inline constexpr uint16_t kRemoteClientNoChunk = 0xffff;
inline constexpr uint8_t kRemoteClientSelf = 0xff;
struct RemoteClientRoute { uint32_t target_hash; uint8_t hops[3]; uint8_t hop_count; };
struct RemoteClientPendingCore {
    uint64_t discovery_id;                    // fresh bootstrap ID while exact execute bytes stay retained
    uint64_t request_id;                     // §8.2 random u64, clear but authenticated
    uint64_t admin_epoch;                    // §8.6 the epoch this request was sealed under
    uint32_t source_hash;                    // §8.1 the controller's stable SOURCE_HASH (clear, AEAD-bound)
    uint32_t outcome_deadline_ms;            // §13 remote_disruptive_outcome_deadline_ms bookkeeping
    uint32_t next_retry_ms;                  // retry state
    uint16_t sealed_len;                     // the EXACT sealed length; a body cap needs 8 bits + the 231 bound
    uint8_t  target_admin_pub[kRemoteKeyBytes];  // §6.3 the stable target administration identity, in full (§8.4)
    uint8_t  controller_pub[kRemoteKeyBytes];    // §15 "selected local credential", in full — never inferred from UI state
    uint8_t  acl_slot;                       // §8.1 slot 0..9
    uint8_t  local_transport;                // §8.10 USB serial vs secured BLE — the sink the answer is owed to
    uint8_t  carrier;                        // §14 which RemoteCarrier this request flew on
    uint8_t  retries;                        // retry state
    uint8_t  state;                          // sent / awaiting-terminal / complete / undeliverable
    uint8_t  flags;                          // e2e-ACK requested (`-a`), mutating-command, confirmation-token seen
    RemoteClientRoute route;                         // immutable source-validation/carrier hints
    uint8_t credential_slot;                  // self or key0..key9, never inferred from mutable UI
    uint8_t target_book_slot;                 // existing in-use guards
};

struct RemoteClientPending { RemoteClientPendingCore core; uint8_t sealed[kRemoteClientSealedBytes]; };
struct RemoteClientSession {
    uint64_t admin_epoch;                    // §8.6 the epoch the session key was derived under (§7.1)
    uint32_t last_used_ms;
    uint8_t  target_admin_pub[kRemoteKeyBytes];  // half the key
    uint8_t  controller_pub[kRemoteKeyBytes];    // the other half — a cache keyed by one alone would cross credentials
    uint8_t  base_key[kRemoteKeyBytes];            // §7.1 label || shared32 || controller_ed_pub32 || target_admin_ed_pub32
    uint8_t  session_key[kRemoteKeyBytes];         // §7.1 label || base_key32 || admin_epoch_le64
    uint8_t  acl_slot;                       // §8.6 the slot the bootstrap response reported
    uint8_t  valid;
    uint8_t credential_slot;
    uint8_t target_book_slot;
};

struct RemoteClientAssembly {
    uint64_t request_id;                     // §8.7 the response's own id; an unmatched response is refused
    uint32_t started_ms;
    uint32_t bytes_used;
    uint16_t first_chunk;                    // head of this assembly's chunk list; 0xFFFF = none
    uint8_t  next_seq;                       // §8.7 response_seq, contiguous from 0
    uint8_t  terminal_seen;                  // §8.9 the terminal frame closed the series
    uint8_t  result_code;                    // §8.9 completed / scheduled / refused / …
    uint8_t  in_use;
    uint8_t result_domain;                    // terminal vs protocol error vs admission/local outcome
    uint8_t reserved;                        // delivery: low bits 1=failed/2=accepted; bit 2=re-offer
    uint32_t result_detail;                   // scheduled delay / admission count, independent of output
};

struct RemoteClientRetained {
    uint64_t request_id;
    uint32_t completed_ms;
    uint32_t bytes_total;
    uint16_t first_chunk;
    uint8_t  local_transport;                // §8.10 the sink that must accept it
    uint8_t  delivered;                      // local delivery accepted -> the target ACK may now be sent
    uint8_t  reoffer_from_zero;              // §8.10 after a secured-BLE reconnect
    uint8_t  in_use;
    uint8_t result_domain;
    uint8_t result_code;
    uint32_t result_detail;                   // survives assembly release and local replay
};

struct RemoteClientAck {
    uint64_t request_id;                     // §8.5 RESPONSE_ACK reuses the acknowledged request's id
    uint32_t next_retry_ms;
    uint8_t  target_admin_pub[kRemoteKeyBytes];  // which target the debt is owed to
    uint8_t  sealed_ack[kRemoteOverheadSessionControl];   // §8.11: exactly 25 bytes
    uint8_t  acl_slot;
    uint8_t  retries;
    uint8_t  in_use;
    uint32_t source_hash;                     // originating SOURCE_HASH, never rebuilt from mutable state
    RemoteClientRoute route;
    uint8_t credential_slot;
    uint8_t target_book_slot;
    uint8_t carrier;
    uint8_t reserved;
};


struct RemoteClientChunk { uint8_t bytes[kRemoteClientChunkBytes]; uint16_t len; uint16_t next; };
struct RemoteClientCounters {
    uint16_t request_table_pressure, assembly_failure, unmatched_response;
    uint16_t auth_failure, local_result_pressure, radio_enqueue_failure;
};
struct RemoteClientState {
    RemoteClientPending pending[4];
    RemoteClientSession sessions[4];
    RemoteClientAssembly assemblies[2];
    RemoteClientChunk chunks[8];
    RemoteClientRetained retained[2];
    RemoteClientAck ack_debt[8];
    RemoteClientCounters counters;
};
static_assert(sizeof(RemoteClientState) == 4512 && alignof(RemoteClientState) == 8, "R-RA-45 controller allocation");
static_assert(sizeof(RemoteClientPending) == 352 && sizeof(RemoteClientSession) == 144);
static_assert(sizeof(RemoteClientAssembly) == 32 && sizeof(RemoteClientRetained) == 32);
static_assert(sizeof(RemoteClientChunk) == 210 && sizeof(RemoteClientAck) == 88);

enum class RemoteLocalTransport : uint8_t { usb, ble };
enum class RemoteClientPhase : uint8_t {
    empty, bootstrap_ready, bootstrap_wait, request_ready, response_wait,
    compare_ready, compare_wait, rollover_ready, rollover_wait, complete
};
enum class RemoteClientError : uint8_t {
    none, bad_args, authentication_failed, entropy_failed, duplicate_id, request_table_full,
    assembly_full, result_full, ack_debt_full, correlation_full, session_cache_full,
    carrier_unavailable, radio_enqueue_failed, not_found, incomplete, unknown
};
enum class RemoteClientSend : uint8_t { queued, full, unavailable };
struct IRadminCarrier {
    virtual ~IRadminCarrier() = default;
    virtual bool tx_queue_full() const = 0;
    virtual RemoteClientSend submit_request(const RemoteClientRoute&, RemoteSource, const RemoteCarrier&,
                                            std::span<const uint8_t>, bool e2e_ack) = 0;
    virtual RemoteClientSend submit_ack(const RemoteClientRoute&, RemoteSource, const RemoteCarrier&,
                                        std::span<const uint8_t>) = 0;
};
// No callback or sink is stored. Delivery resolves the transport tag on each main-loop call (R-RA-45).
struct IRemoteLocalDelivery {
    virtual ~IRemoteLocalDelivery() = default;
    virtual void begin_result() {} // call-scoped formatting state only; every re-offer starts at sequence zero
    virtual bool connected(RemoteLocalTransport) const = 0;
    virtual bool output(RemoteLocalTransport, uint64_t id, uint16_t& seq, std::span<const uint8_t>) = 0;
    virtual bool terminal(RemoteLocalTransport, uint64_t id, const char* result, uint32_t detail, bool scheduled) = 0;
    virtual void retained(RemoteLocalTransport, uint64_t id) = 0;
};
struct RemoteClientRequest {
    const Identity* identity = nullptr; // guarded caller transient; required except OPEN
    std::span<const uint8_t> target_pub{};
    std::span<const uint8_t> command{};
    RemoteClientRoute route{};
    uint32_t source_hash = 0;
    uint8_t credential_slot = kRemoteClientSelf;
    uint8_t target_book_slot = 0;
    RemoteLocalTransport transport = RemoteLocalTransport::usb;
    RemoteCmdOpcode opcode = RemoteCmdOpcode::auth_execute;
    bool e2e_ack = false;
    uint8_t carrier = 0; // 0 ordinary RPC, 1 typed mobile wrapper; transient input only
    uint8_t correlation_free = 0; // existing delegated-ring availability, reservations counted in pending
};
struct RemoteClientResult { RemoteClientError error; uint64_t request_id; };
[[nodiscard]] RemoteStatus remote_client_base(uint8_t out[32], const Identity&, const uint8_t target_pub[32]);
[[nodiscard]] RemoteCarrier remote_client_carrier(const RemoteClientRoute&, uint8_t type, uint8_t carrier);
[[nodiscard]] RemoteClientResult remote_client_start(RemoteClientState&, const RemoteClientRequest&, uint32_t now,
                                                      RemoteEntropyFn, void*, IRadminCarrier&);
[[nodiscard]] RemoteClientError remote_client_retry(RemoteClientState&, uint64_t id, uint32_t now, RemoteEntropyFn, void*);
void remote_client_receive(RemoteClientState&, uint8_t type, std::span<const uint8_t> body,
                           RemoteSource sender, const RemoteCarrier&, uint32_t now);
void remote_client_service(RemoteClientState&, uint32_t now, RemoteEntropyFn, void*, IRadminCarrier&, IRemoteLocalDelivery&);
[[nodiscard]] RemoteClientError remote_client_show(RemoteClientState&, uint64_t id, RemoteLocalTransport,
                                                   uint32_t now, IRemoteLocalDelivery&);
[[nodiscard]] RemoteClientError remote_client_ack(RemoteClientState&, uint64_t id, uint32_t now);
void remote_client_expire(RemoteClientState&, uint32_t now);
// Remaining time until the single scan's next edge, or UINT32_MAX when nothing expires.
[[nodiscard]] uint32_t remote_client_next_expiry(const RemoteClientState&, uint32_t now);
[[nodiscard]] bool remote_client_busy(const RemoteClientState&);
[[nodiscard]] bool remote_client_key_in_use(const RemoteClientState&, uint8_t slot);
[[nodiscard]] bool remote_client_target_in_use(const RemoteClientState&, uint8_t slot);
[[nodiscard]] uint8_t remote_client_pending_count(const RemoteClientState&);
[[nodiscard]] uint8_t remote_client_retained_count(const RemoteClientState&);
[[nodiscard]] const char* remote_client_error_name(RemoteClientError);
} // namespace MESHROUTE_NS
