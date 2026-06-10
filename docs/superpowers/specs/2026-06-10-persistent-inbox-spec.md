# Persistent Inbox (DM + Channels) — Implementation Spec

**Status:** IMPLEMENTATION SPEC (2026-06-10) — ready for a coding agent.
**Context:** the firmware-side durable inbox from `2026-06-10-ble-companion-ota-inbox-design.md`
Part C, made concrete. Boards: XIAO nRF52840 (QSPI flash) + Heltec V3 ESP32-S3 (LittleFS data
partition). The companion *pull* wiring depends on the BLE companion (that spec's Phase 2); **the
store + the `lib/core` logic + their native tests do NOT** — build and verify those first.

## 1. Goal

Hold received **DMs** and **channel messages** in flash so a node retains them between companion
connections (the RAM push ring is only 16, drop-oldest, and not durable). DMs are large + durable;
channels are persisted but more freely evicted. The phone is the long-term archive; the node holds a
**bounded rolling history** and serves it incrementally to a connecting client.

## 2. Architecture (one core, a storage HAL — mirrors the `Hal` discipline)

```
lib/core/inbox.h/.cpp
   class InboxStore  — ABSTRACT interface (bounded append-log; lib/core depends only on this)
   class Inbox       — the logic: two stores (DM + channel), record-on-delivery, cursor-based pull
src/device_inbox_store.*  — the LittleFS-backed InboxStore (QSPI on nRF52 / data partition on ESP32)
test/  — a RAM-backed fake InboxStore (so all Inbox logic is unit-tested in `native`)
```

`Node` owns an `Inbox`; on a delivered DM / new channel message it appends a record; a companion
`pull_inbox` command drives `Inbox::pull`. `lib/core` never touches LittleFS/QSPI — only `InboxStore`.

## 3. Record format

```cpp
// One inbox entry. Variable length (body). `seq` is the monotonic per-store sync cursor.
struct InboxRecord {
    uint32_t seq;          // per-store monotonic id (survives reboot — see §6); the pull cursor
    uint8_t  kind;         // 0 = DM, 1 = channel
    uint8_t  origin;       // sender node_id (DM) / minter node_id (channel)
    uint8_t  channel_id;   // channel only (0 for DM)
    uint16_t ctr;          // the message ctr (with origin: identity / app-side dedup)
    uint64_t rx_time_ms;   // node uptime ms at receive (absolute time deferred — §15)
    uint8_t  body_len;     // 0..inbox_max_body
    // uint8_t body[body_len] follows
};
```
On flash each record is **length-framed**: `[u16 total_len][InboxRecord header][body]`. No read/unread
flag in the record — read state is a single per-store cursor (§5), so records are never mutated
(append-only, wear-friendly).

## 4. Two stores + eviction

| Store | Cap (proposed, tunable) | Eviction |
|---|---|---|
| **DM** | `inbox_dm_store_bytes` = 512 KiB (≈ thousands of short DMs) | drop-oldest at the byte cap |
| **Channel** | `inbox_chan_store_bytes` = 128 KiB | drop-oldest at the byte cap (freer) |

Separate stores so the policies/caps are independent. `inbox_max_body` = `max_payload_bytes_hard_cap`
(241). Constants go in `protocol_constants.h` (device sizes can be smaller on ESP if the partition is
tighter — see §10).

## 5. `InboxStore` interface (the HAL contract)

```cpp
// A bounded, crash-safe, append + iterate + drop-oldest record log over flash. One instance per store.
class InboxStore {
public:
    virtual ~InboxStore() = default;
    virtual bool     begin() = 0;                                  // mount/init; load the persisted next-seq + read-cursor
    virtual bool     append(uint32_t seq, const uint8_t* rec, uint16_t len) = 0;  // append; drop-oldest if over cap
    // visit records with seq > since, OLDEST-first; cb(seq, bytes, len)->bool (false = stop). returns # visited.
    virtual uint16_t read_since(uint32_t since_seq, const ReadCb& cb) const = 0;
    virtual uint32_t persisted_next_seq() const = 0;               // for monotonic seq across reboot (§6)
    virtual bool     set_next_seq(uint32_t next) = 0;              // persist the counter (batched ok)
    virtual uint32_t read_cursor() const = 0;                     // highest seq marked read (UX only)
    virtual bool     set_read_cursor(uint32_t seq) = 0;
    virtual uint16_t count() const = 0;                           // live record count (diag)
};
```
The store is "dumb bytes + bookkeeping"; `Inbox` owns the record (de)serialization + policy.

## 6. `Inbox` logic (`lib/core`)

```cpp
class Inbox {
public:
    void on_init(InboxStore* dm, InboxStore* chan);               // begin() both; restore the seq counters
    void record_dm(uint8_t origin, uint16_t ctr, const uint8_t* body, uint8_t len, uint64_t now_ms);
    void record_channel(uint8_t ch, uint8_t origin, uint16_t ctr, const uint8_t* body, uint8_t len, uint64_t now_ms);
    // companion pull: stream records with seq > the client's cursors (DM + channel), oldest-first, via cb.
    uint16_t pull(uint32_t dm_since, uint32_t chan_since, const PullCb& cb) const;
    uint32_t dm_newest_seq()   const;  uint32_t chan_newest_seq() const;
    void     mark_read(bool dm, uint32_t seq);                    // UX unread-count (optional to wire)
};
```
- **Monotonic seq across reboot (the subtle part — must never regress):** at `on_init`, each store's
  next-seq = `max(persisted_next_seq(), highest_stored_record_seq + 1)`. The **stored records are
  authoritative** for the high-water mark, so taking their max+1 means a *stale* persisted counter (a
  crash between batched persists) can never make seq **regress and reuse a value** — which would be a
  real bug: a client whose cursor is already past that value would silently **miss** the new messages.
  The persisted counter covers the complementary case: once drop-oldest has **evicted** the high-seq
  records, it remembers the high-water mark so seq still never reuses. Each `record_*` then assigns
  `seq = next++`, appends, and persists `next` **batched** (every K appends / on a timer) to limit
  wear — a crash then only *skips forward* a few seq values, which is harmless (seq must be monotonic,
  not gapless; `pull` is `> cursor`).
- **No append-time dedup needed:** the DATA dedup (`_seen_origins`) and the channel ingest
  (channel_msg_id) already fire `msg_recv`/`channel_recv` exactly once per *new* message. (App-side
  dedup by `(origin, ctr)` is its own safety net.)
- **DM vs channel** are physically separate stores with separate seq spaces; `pull` takes both
  cursors and interleaves the output oldest-first (or emits DM then channel — agent's choice, document it).

## 7. Integration points (where it hooks into the node)

- **DM delivered** — in `node_mac_rx.cpp` `do_post_ack`, right where it builds the `msg_recv` Push and
  `enqueue_push(pu)`: also call `_inbox.record_dm(pa.origin, pa.ctr, body, blen, _hal.now())`. The
  push (live notify) and the inbox (durable) are written together.
- **Channel message received** — in `node_channel.cpp` `ingest_channel_m`, where the `channel_recv`
  Push is enqueued: also `_inbox.record_channel(channel_id, origin, ctr, body, len, _hal.now())`.
- **Pull** — a new `CmdKind::pull_inbox` (or a console/companion command) → `Node::on_command` →
  `_inbox.pull(dm_since, chan_since, cb)` → each record emitted to the client (a `PushKind::inbox_entry`
  or streamed over the companion). Wiring the transport is companion-Phase-2; the `pull` itself is
  testable now via `on_command` + a callback.

## 8. Sync protocol (companion)

- **Catch-up:** `pull_inbox <dm_since> <chan_since>` → stream `inbox_entry` records (seq > cursor),
  oldest-first, chunked to the BLE MTU. The app advances its cursors to the last seq it received.
- **Live:** while connected, the existing `msg_recv`/`channel_recv` pushes deliver new messages in
  real time (no need to poll). `pull_inbox` is only the on-connect / been-away catch-up.
- **Read state (optional, UX):** `mark_read <dm|chan> <seq>` → `set_read_cursor`. Drives unread badges.
- **Deletion is NOT in v1** — the node self-manages size via drop-oldest. (The phone is the archive;
  the node is a bounded rolling buffer.) `mark_read`/delete-driven freeing is §15.

## 9. Recommended storage implementation (segmented LittleFS log)

For the device `InboxStore`, a **segmented append-log** on LittleFS (avoids whole-file compaction):
- A store = a ring of fixed-size **segment files** (e.g. `dm/000`, `dm/001`, …), each ≤
  `inbox_segment_bytes` (e.g. 32 KiB DM / 16 KiB channel), holding length-framed records.
- **Append** → write to the head segment; roll to a new segment when the next record won't fit.
- **Drop-oldest** → when total bytes exceed the cap, **delete the oldest segment file** (coarse: drops
  a segment-worth of the oldest records at once — fine for an inbox). Cheap (one unlink), no rewrite,
  wear-friendly; LittleFS wear-levels underneath.
- **Meta** (next-seq, read-cursor, the segment ring head/tail) in a tiny `dm/meta` file, written
  batched. On `begin()`, scan segments to rebuild `count`/oldest-seq + load meta.
- Crash-consistency: a torn append at the tail is detected on scan (bad length/truncation) and
  ignored; seq monotonicity tolerates skips.

The agent may choose another bounded+wear-aware+crash-safe scheme, but it must satisfy the
`InboxStore` contract and the byte caps. The **RAM fake** for tests is a `std::deque<vector<uint8_t>>`
honoring the same cap/eviction.

## 10. Platform stores

- **nRF52 (XIAO):** bring up the **2 MB QSPI** (currently unused — the variant only deselects its CS):
  `Adafruit_SPIFlash` + an `Adafruit_LittleFS` instance on it. Stores live there; `/mrcfg` + `/mrid`
  stay on InternalFS. **Note the OTAFIX bootloader-install can disturb QSPI** (the MeshCore
  ExtraFS-erase gotcha) — `begin()` must handle an unformatted/dirty QSPI (format-on-fail).
- **ESP32-S3 (Heltec):** a **LittleFS data partition** (from the OTA-capable partition table — that's
  in the OTA work; if OTA partitions aren't in yet, add a `littlefs`/`spiffs` data partition). Same
  segmented-log `InboxStore` on it.
- **native (tests):** the RAM fake.

## 11. Config / constants (`protocol_constants.h`)

`inbox_dm_store_bytes` (512 KiB), `inbox_chan_store_bytes` (128 KiB), `inbox_segment_bytes_dm`
(32 KiB), `inbox_segment_bytes_chan` (16 KiB), `inbox_max_body` (= `max_payload_bytes_hard_cap`).
Sizes are the design knobs; document the resulting approximate message counts.

## 12. Test plan

- **native (the bulk — drive `Inbox` + the RAM fake):**
  - `record_dm`/`record_channel` then `pull(0,0)` returns all records, oldest-first, fields intact.
  - `pull(since)` returns only seq > since (the cursor).
  - drop-oldest: fill past the byte cap → oldest records evicted, newest retained, `count` bounded,
    seq still monotonic + increasing.
  - DM vs channel isolation (separate caps/seq; a channel flood doesn't evict DMs).
  - seq monotonic across a simulated reboot (re-`on_init` with the persisted next-seq).
  - empty/min/`inbox_max_body` bodies; a record larger than a segment is rejected or split (define).
  - integration: a delivered DM (drive `on_recv` end-to-end) lands in the DM store AND the push ring.
- **device (bench, user-verified — like `device_nv`):** QSPI/LittleFS bring-up, persistence across a
  real reboot, format-on-dirty, wear sanity; ESP partition the same.
- both boards build green.

## 13. Phasing (within the inbox)

1. **`InboxStore` interface + `Inbox` logic + the RAM fake + all native tests** (no hardware; full
   logic coverage). Wire `record_dm`/`record_channel` into the delivery paths.
2. **Device stores** — QSPI (nRF52) + LittleFS partition (ESP32) `InboxStore`s; bench-verify.
3. **Companion pull wiring** — `pull_inbox` command + `inbox_entry` push (rides the BLE companion;
   gated on that spec's Phase 2). Until then, `pull` is exercised by the native tests via `on_command`.

## 14. Non-goals / deferred (v1)

- No node-side deletion / drop-after-ack (drop-oldest only); no multi-device per-client cursors.
- No absolute wall-clock time (only node-uptime `rx_time_ms`); the phone stamps absolute receive time
  on pull. Revisit if a node gets an RTC / time sync.
- No encryption of the inbox at rest (the link is paired/encrypted; at-rest crypto is a later call).
