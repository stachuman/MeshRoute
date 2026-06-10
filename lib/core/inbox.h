// MeshRoute — lib/core/inbox.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persistent inbox (DM + channel durable history). A platform-neutral CORE — the Inbox logic + two
// append-log STORES behind an abstract InboxStore HAL — exactly the Hal / device_nv split, so all the
// logic is unit-testable in `native` against a RAM-backed fake (no flash). lib/core depends ONLY on
// the InboxStore interface; the LittleFS/QSPI-backed stores live in src/ (device, spec Phase 2). The
// node holds a bounded rolling history; the phone is the long-term archive (pulls incrementally).
// See docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md.
#pragma once
#include "protocol_constants.h"
#include <cstdint>

namespace meshroute {

// One inbox entry, DECODED (what pull / a store dump yields). kind: DM or channel; channel_id is 0 for
// a DM. (origin, ctr) is the message identity. rx_time_ms = node uptime at receive (absolute wall-clock
// is deferred — the companion stamps it on pull). `body` points into the store's record bytes — valid
// for the duration of the visiting callback only.
enum class InboxKind : uint8_t { dm = 0, channel = 1 };
struct InboxEntry {
    uint32_t       seq;
    InboxKind      kind;
    uint8_t        origin;
    uint8_t        channel_id;
    uint16_t       ctr;
    uint64_t       rx_time_ms;
    const uint8_t* body;
    uint8_t        body_len;
};

// Serialized record = [seq u32][kind u8][origin u8][channel_id u8][ctr u16][rx_time_ms u64][body_len u8]
// [body], all LITTLE-endian. Fixed 18-B header + body. The STORE adds the on-flash framing ([u16
// total_len] …); Inbox owns this record (de)serialization.
inline constexpr uint16_t inbox_record_header_bytes = 4 + 1 + 1 + 1 + 2 + 8 + 1;            // = 18
inline constexpr uint16_t inbox_record_max_bytes    = inbox_record_header_bytes + protocol::inbox_max_body;  // 259

// ---- the storage HAL: a bounded, crash-safe append + iterate + drop-oldest record log -------------
// One instance per store (DM, channel). "Dumb bytes + bookkeeping": the store owns the bytes, the
// byte-cap drop-oldest eviction, and the two persisted counters (next-seq, read-cursor); Inbox owns the
// record format + policy. Implementations: a RAM fake (tests) + a segmented-LittleFS log (device). NO
// heap / exceptions in the contract (matches Hal).
class InboxStore {
public:
    // Visit each record with seq > `since`, OLDEST-first; `rec`/`len` are the raw record bytes. Return
    // false from cb to stop early. A function-pointer + ctx (NOT std::function) — no heap, device-safe.
    using ReadCb = bool (*)(void* ctx, uint32_t seq, const uint8_t* rec, uint16_t len);

    virtual ~InboxStore() = default;
    virtual bool     begin() = 0;                                                  // mount/init; load the persisted counters
    virtual bool     append(uint32_t seq, const uint8_t* rec, uint16_t len) = 0;   // append; drop-oldest if over the byte cap
    virtual uint16_t read_since(uint32_t since_seq, ReadCb cb, void* ctx) const = 0;  // returns # visited
    virtual uint32_t persisted_next_seq() const = 0;                               // batched next-seq, for monotonicity across reboot (§6)
    virtual bool     set_next_seq(uint32_t next) = 0;                              // persist the counter (batched)
    virtual uint32_t read_cursor() const = 0;                                      // highest seq marked read (UX only)
    virtual bool     set_read_cursor(uint32_t seq) = 0;
    virtual uint16_t count() const = 0;                                            // live record count (diag)
};

// ---- the inbox logic (lib/core; platform-neutral) -------------------------------------------------
class Inbox {
public:
    using PullCb = bool (*)(void* ctx, const InboxEntry& e);   // false = stop

    // Install the two stores + restore the seq counters (begin() both; recompute next-seq, §6). NULL
    // stores leave the inbox DISABLED — an optional companion feature: record_* / pull are inert until a
    // backend wires durable storage. NOT a hidden fallback masking a misconfig: an unconfigured inbox
    // does nothing, explicitly distinct (enabled()) from a wired one; a node without a companion needs none.
    void on_init(InboxStore* dm, InboxStore* chan);
    bool enabled() const { return _dm != nullptr && _chan != nullptr; }

    // Record-on-delivery (called from the node's DM / channel deliver paths). No-op if disabled.
    void record_dm(uint8_t origin, uint16_t ctr, const uint8_t* body, uint8_t len, uint64_t now_ms);
    void record_channel(uint8_t channel_id, uint8_t origin, uint16_t ctr,
                        const uint8_t* body, uint8_t len, uint64_t now_ms);

    // Companion pull: stream DM records (seq > dm_since), THEN channel records (seq > chan_since), each
    // oldest-first, via cb. Returns the total entries visited. DM-block-then-channel-block (the two seq
    // spaces are independent; there is no shared clock to interleave on — the app advances each cursor).
    uint16_t pull(uint32_t dm_since, uint32_t chan_since, PullCb cb, void* ctx) const;

    uint32_t dm_newest_seq()   const { return _dm_next   > 1 ? _dm_next   - 1 : 0; }
    uint32_t chan_newest_seq() const { return _chan_next > 1 ? _chan_next - 1 : 0; }
    void     mark_read(InboxKind kind, uint32_t seq);

    // Force-persist both next-seq counters NOW (the "/ on a timer" half of §6's batched persist). The backend
    // should call this on a periodic timer and/or before a planned reboot, to bound how far the persisted
    // high-water can lag the records (it shrinks the seq-reuse window if a device store later loses records).
    void     flush();

private:
    void record(InboxStore* store, uint32_t& next, uint8_t& unpersisted, InboxKind kind, uint8_t origin,
                uint8_t channel_id, uint16_t ctr, const uint8_t* body, uint8_t len, uint64_t now_ms);

    InboxStore* _dm   = nullptr;
    InboxStore* _chan = nullptr;
    uint32_t    _dm_next   = 1;     // next seq to assign (1-based; seq 0 is the "before everything" pull cursor)
    uint32_t    _chan_next = 1;
    uint8_t     _dm_unpersisted   = 0;   // appends since the last set_next_seq (batched persist, §6)
    uint8_t     _chan_unpersisted = 0;
};

}  // namespace meshroute
