// MeshRoute — lib/core/segmented_inbox_store.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DURABLE InboxStore LOGIC — a segmented append-log with drop-oldest eviction + a §10.1 storage epoch,
// behind the platform-neutral meshroute::InboxStore contract (inbox.h). PLATFORM-NEUTRAL + native-testable:
// the flash is two INJECTED interfaces (records + meta), so the hard part (the ring, the framing, the
// reboot-restore, the wipe-detect) is verified host-side against a fake; the device wires real backends.
//
// LAYOUT (the §10.1 high-water/epoch survival falls out of the records/meta SPLIT):
//   - META    -> IMetaStore: a tiny fixed blob {next_seq, read_cursor, epoch, ring head/tail}. On device this is
//                on-chip InternalFS — SEPARATE from the records, so a records wipe can't lose the seq high-water.
//   - RECORDS -> ISegmentStore: a fixed RING of N segment "files" by index `<i>`. Append to the head segment,
//                roll at seg_bytes, drop-oldest = erase the tail segment (coarse, wear-friendly — spec §9). The
//                on-flash frame per record is [u16 framed_len][u32 seq][record bytes].
//   - EPOCH (§10.1): meta.epoch advances ONCE PER REAL TRANSITION, never per boot. ⛔ CORRECTED 2026-08-29
//                ([[B134]] QG rounds 5-6): this line used to read *"begin() bumps meta.epoch when the records
//                came up EMPTY/formatted while meta.next_seq>1"*, which was the RATCHET — `next_seq>1` only says
//                "this store once had traffic" and stays true for ever, so every reboot of an empty store bumped
//                again. The authority is now the persisted `records_state` marker: a deliberate `wipe()` bumps
//                at the wipe; a records loss detected at mount (marked non_empty, medium empty) bumps once and
//                records the acknowledgement; `append_pending` resolves against the medium and bumps for
//                neither. next_seq is preserved from meta -> seq never reuses; the companion sees the bumped
//                epoch and re-syncs from 0 (INBOX_SYNC_CONTRACT.md).
//
// This SUPERSEDED the Arduino-gated logic in `src/device_inbox_store.h` — that same begin/append/read_since,
// extracted so it runs on the host. ⛔ [[B260]] 2026-08-29 DELETED that file; nRF52 now runs THIS class.
//
// ★★★ MISSING/INVALID METADATA OVER EXISTING RECORDS = FAIL LOUD, NOT RECONSTRUCTION — the ruled choice
//     ([[B134]] QG round 2), recorded here because the alternative is the obvious one and somebody will propose it.
//     Reconstruction (walk the segments, rebuild head/tail, resume next_seq above the highest frame seen, bump the
//     epoch) is ALLOWED ONLY against a proof that it never resurrects a tombstoned message and never reuses a seq.
//     I could not produce that proof, and the reasons are structural rather than a lack of effort:
//       · HEAD/TAIL ARE NOT DERIVABLE FROM THE BYTES ALONE once the ring has LAPPED. The ordering has to be
//         inferred from seq ranges, but the newest segment can hold a §B135 TORN tail whose frame chain stops
//         early, so the "highest seq present" is not reliably the highest seq WRITTEN — and guessing the head one
//         segment wrong silently hides everything past it, which is the very defect being fixed.
//       · `read_cursor` IS UNRECOVERABLE BY CONSTRUCTION. It lives only in the meta, so a reconstruction must
//         invent one. Choosing 0 marks the whole history unread; choosing the max marks it all read. Neither is
//         a recovery, and the second can hide messages the operator never saw.
//     ⇒ the mount is REFUSED, `mount_fault()` says why, the node still boots and operates with the inbox inert,
//       and the only way out is an explicit `factory_reset confirm` — a destruction the OPERATOR chooses, rather
//       than one the store performs on their behalf while reporting success. That is C2's default and it is the
//       honest side to be wrong on: refusing to read a history costs a support call, silently re-initialising over
//       one costs the history AND reuses its sequence numbers.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "inbox.h"      // meshroute::InboxStore
#include <stdint.h>
#include <string.h>

namespace MESHROUTE_NS {

// The RECORDS flash HAL — a ring of fixed-cap segment "files" addressed by index. Append-only within a
// segment; whole-segment erase for drop-oldest. Device: a LittleFS dir of `<i>` files on QSPI. Native: a fake.
struct ISegmentStore {
    virtual ~ISegmentStore() = default;
    virtual bool     mount(bool* formatted) = 0;                              // *formatted=true if a fresh format was needed
    virtual bool     seg_size(uint16_t idx, uint32_t* size) const = 0;        // false if the segment is absent
    virtual bool     seg_append(uint16_t idx, const uint8_t* b, uint16_t n) = 0;
    virtual uint32_t seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const = 0;  // whole segment -> out; bytes read
    // ⛔ RETURNS bool SINCE 2026-08-28 ([[B134]] QG blocker 3): a discarded erase result is how a destructive verb
    //    comes to report success over records that are still on the medium, and how a ROLL comes to write behind
    //    stale lapped bytes that the reader then parses as frames.
    // ★ THE CONTRACT IS "EMPTY AFTERWARDS", NOT "A REMOVAL HAPPENED": an ALREADY-ABSENT segment is SUCCESS. That
    //   distinction is load-bearing, because the backends disagree about absence — POSIX `unlink` sets `ENOENT`
    //   and Adafruit's `remove()` answers plain `false` — and a store that treated "already gone" as a failure
    //   would fail its own version-upgrade wipe on a half-populated ring.
    virtual bool     seg_erase(uint16_t idx) = 0;
    // Does ANY segment hold bytes? (the §10.1 wipe detector, and — since [[B134]] QG round 2 — the
    // FRESH-vs-CORRUPTED discriminator, which is why it may no longer answer a plain bool.)
    // ⛔ THREE-VALUED: `*ok = false` means THE QUESTION COULD NOT BE ANSWERED (a transient FS error, an
    //    unreadable directory), which is NOT the same as "no records". Mapping an inspection failure onto
    //    "empty" is exactly what routes a live store into the silent re-initialise path — records hidden and
    //    sequences reused. An ABSENT record directory is a real "no records" and answers `false` with `*ok`
    //    true; anything the backend cannot classify must clear `*ok` and the mount then fails loud.
    virtual bool     any_segments(bool* ok) const = 0;
};

// ★★★ THE META LOAD IS THREE-VALUED ([[B134]] QG round 3), AND THE THIRD STATE IS THE WHOLE POINT.
//     `absent` and `error` used to be ONE `false`, and the caller could only read it as "fresh" — so CORRUPT
//     metadata over an EMPTY record store was treated as first boot. That is not a harmless case: after a
//     `prep-restart` the records are empty BY DESIGN while the meta still carries the sequence high-water, the
//     epoch and the read cursor. Losing it there reuses sequences AND conceals the wipe from the companion —
//     the §10.1 epoch bump that bench 19.8 depends on is exactly what a silent re-init resets.
// ⇒ `absent` means the record was never written (a true key-absent); `error` means it could not be read or came
//   back wrong — a fact about the MEDIUM, never an invitation to start over.
enum class MetaLoad : uint8_t { absent = 0, loaded = 1, error = 2 };

// The META blob HAL — a tiny fixed blob that MUST survive a records wipe (device: InternalFS, off the QSPI).
struct IMetaStore {
    virtual ~IMetaStore() = default;
    virtual MetaLoad load(void* blob, uint16_t len) = 0;    // see MetaLoad — ⛔ `error` is NOT `absent`
    virtual bool save(const void* blob, uint16_t len) = 0;
};

// ★ WHY begin() FAILED — the "§10.1-style detect reporting what happened" ([[B134]] QG round 2). A refused mount
//   leaves the Inbox disabled and VISIBLE at boot, but `enabled=0` alone cannot tell a corrupted partition from a
//   corrupted meta over a LIVE history, and those want completely different operator responses (reflash vs. an
//   explicit `factory_reset confirm` that the operator must choose knowing history is being destroyed).
enum class SegMountFault : uint8_t {
    none = 0,
    seg_too_big,            // seg_bytes > the read scratch — a build/config error, never a medium fault
    records_unmountable,    // the records backend would not mount even after a format
    records_uninspectable,  // ⛔ the records store could not say whether it holds anything (see any_segments)
    meta_unwritable,        // the metadata store refused a load-bearing save
    meta_lost_over_records, // ⛔⛔ the meta is ABSENT and records ARE present — the history cannot be addressed
    // ⛔⛔ ...AND ITS SIBLING, DELIBERATELY DISTINCT because the operator response differs. `meta_corrupt` means
    //     the metadata was READ and is wrong (unreadable, wrong length, bad magic, structurally impossible). It
    //     fails REGARDLESS of record count — a corrupt high-water cannot be trusted even over an empty store,
    //     because the sequence space and the epoch live nowhere else. ⓘ With NO records this is recoverable by
    //     `factory_reset confirm` WITHOUT data loss; `meta_lost_over_records` is not. Two codes, two answers.
    meta_corrupt,
};

class SegmentedInboxStore : public InboxStore {
public:
    SegmentedInboxStore(ISegmentStore& records, IMetaStore& meta, uint32_t cap_bytes, uint32_t seg_bytes)
        : _records(&records), _meta_io(&meta), _cap(cap_bytes), _seg(seg_bytes) {}

    bool     begin() override;
    bool     append(uint32_t seq, const uint8_t* rec, uint16_t len) override;
    uint16_t read_since(uint32_t since_seq, ReadCb cb, void* ctx) const override;
    uint32_t persisted_next_seq() const override { return _meta.next_seq; }
    // ⓘ THE SAME SWEEP ([[B134]] QG round 7): this path had the RAM-first shape too. It has NO change-detect in
    //   front of it, so its retry was never eaten — but a failed save still left `persisted_next_seq()` reporting
    //   a high-water that is not on the medium, which is the same over-claim one field along. Rolled back and
    //   latched for the same reason. ⓘ `Inbox::record` already keeps its batch counter high on a false return,
    //   so the retry arrives on its own.
    bool     set_next_seq(uint32_t next) override {
        const uint32_t prev = _meta.next_seq;
        _meta.next_seq = next;
        if (save_meta()) { _meta_dirty = false; return true; }
        _meta.next_seq = prev;
        _meta_dirty = true;
        return false;
    }
    uint32_t read_cursor() const override { return _meta.read_cursor; }
    // ★ CHANGE-DETECT (InternalFS self-heal Part 3, which the nRF52 twin — DELETED by [[B260]] — always had
    // too): a `mark_read` to the SAME cursor must not rewrite the meta store. An app/companion fires mark_read at
    // its OWN cadence during a pull session, so a no-op rewrite is pure write churn — flash wear plus a widened
    // reset-during-write window, on a tree this project has already been BRICKED by once. A REAL advance still
    // persists immediately (it is user/app-commanded). ⓘ Added 2026-08-28 with [[B134]] because this is now a
    // DEVICE store on ESP32, not only the host-tested logic; without it the ESP32 backend would have been born
    // wearing its flash harder than the nRF52 one it reuses.
    // ⛔⛔ ROLLBACK + DIRTY-AWARE COALESCING ([[B134]] QG round 7). The one-line form this replaces was a live
    //    "success that isn't", and the wear-coalescing was what made it unrepairable: it set the RAM cursor
    //    BEFORE saving, so a failed save left the NEW value in RAM — and the caller's retry with the SAME value
    //    then hit `seq == _meta.read_cursor` and returned `true` WITHOUT attempting a save. The one operation
    //    that could have repaired the medium was the one the optimisation ate. QG reproduced: first call false,
    //    repeat true, RAM 7, reboot cursor 0 — the companion told `inbox_marked` over a cursor that never moved.
    // ⇒ (1) the coalescing now requires the medium to AGREE (`!_meta_dirty`), so a retry always gets through;
    //   (2) a failed save ROLLS THE RAM VALUE BACK, so the cursor never LOOKS persisted when it is not; and
    //   (3) the failure latches the store's existing `_meta_dirty` retry (U1 — the same latch blocker-1 added,
    //       and it covers the whole blob, so the next append's retry re-persists this too).
    bool     set_read_cursor(uint32_t seq) override {
        if (seq == _meta.read_cursor && !_meta_dirty) return true;   // genuinely nothing to write
        const uint32_t prev = _meta.read_cursor;
        _meta.read_cursor = seq;
        if (save_meta()) { _meta_dirty = false; return true; }
        _meta.read_cursor = prev;                                    // RAM must never out-run the medium
        _meta_dirty = true;
        return false;
    }
    uint16_t count() const override { return _count; }
    uint32_t storage_epoch() const override { return _meta.epoch; }
    // Why the last begin() refused — `none` after a successful mount. See SegMountFault.
    SegMountFault mount_fault() const { return _fault; }
    // factory_reset (§5) / `prep-restart`: drop EVERY record segment. ⓘ Added 2026-08-28 with [[B134]]: the base
    // `InboxStore::wipe()` is a no-op, which was CORRECT while this logic had no device instance (a RAM store is
    // cleared by the reboot that follows) and is WRONG the moment ESP32 mounts it on real flash — `factory_reset
    // confirm` and `prep-restart` would both have left the whole history on the medium.
    // ⛔ AND IT RESETS THE BOOKKEEPING, WHICH THE RETIRED nRF52 TWIN DID NOT (it erased the segments and cleared
    // the seal only). That twin got away with it because BOTH callers rebooted immediately — an unstated
    // dependency on the caller, not a property of the store (C2). ⓘ [[B260]] deleted it. Here the ring head/tail,
    // the live-byte total and the seal are all put back to the empty state they now describe, so a wiped store is
    // immediately usable. ⛔ CORRECTED 2026-08-29 ([[B134]] QG round 7): this used to end *"and the NEXT boot's
    // §10.1 detect (records empty + next_seq > 1) bumps the epoch exactly once"*. That is the RETIRED ratchet —
    // the bump now happens INSIDE this function, at the transition, and the next boot is stable because the
    // `records_state` marker records the acknowledgement. next_seq is deliberately NOT reset — it lives on the
    // meta store and a wipe must never reuse a seq.
    // ⛔ EVERY erase AND the metadata save are CHECKED ([[B134]] QG blocker 3). ★ And the loop does NOT stop at the
    //    first failure: a destructive verb must erase as much as it can AND still report that it did not finish.
    //    Stopping early would leave MORE recoverable history behind for the same `false`.
    bool wipe() override {
        bool ok = true;
        const uint32_t _total_before_erase = _total;   // captured BEFORE the erases zero it (see had_history)
        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) ok = false;
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        _total = 0; _count = 0; _head_sealed = false;
        // ★★ THE DELIBERATE-WIPE EPOCH BUMP LIVES HERE NOW ([[B134]] QG round 5), and it happens ONCE — at the
        //    transition, not on every subsequent boot. It used to be left to the next boot's §10.1 detect, which
        //    had no way to know it had already fired and so bumped again, and again. ⓘ Only a store that HELD
        //    something bumps: wiping an already-empty store destroyed no history and owes the companion nothing.
        // ⓘ `append_pending` is resolved by the SAME rule the mount uses, from the store's own live-byte count:
        //   pending with bytes present is real history (bump), pending with none is an append that never landed
        //   (nothing to announce). After a mount pending cannot survive — begin() resolves it — so this arm is
        //   reachable only when a finalizing save failed earlier in THIS runtime, where `_total` is exact.
        const bool had_history = (_meta.records_state == kRecordsNonEmpty)
                              || (_meta.records_state == kRecordsAppendPending && _total_before_erase > 0);
        if (had_history) _meta.epoch += 1;
        _meta.records_state = kRecordsEmpty;
        // The topology must say "empty" on the MEDIUM too: an unpersisted reset leaves the next boot walking a ring
        // that describes records this call just erased. A failed save is a failed wipe, and it arms the retry.
        // ⛔ AND THE MARKER RIDES THE SAME SAVE, deliberately: an acknowledged wipe whose marker did not persist
        //    would come back as an external-loss detect on the next boot and bump a second time.
        if (!save_meta()) { _meta_dirty = true; ok = false; }
        return ok;
    }

private:
    struct Meta {
        uint32_t magic; uint16_t version;
        uint16_t head_seg; uint16_t tail_seg; uint16_t seg_count;
        uint32_t next_seq;            // §6 high-water (survives a records wipe — that's the point of meta-off-QSPI)
        uint32_t read_cursor;         // UX unread badge
        uint32_t epoch;               // §10.1 storage epoch (bumps on a records-store wipe)
        // ★★★ [[B134]] QG ROUND 5 — THE ACKNOWLEDGED-EMPTY MARKER, and it exists because `next_seq > 1` CANNOT
        //     carry this fact. The §10.1 detect used to read "records empty AND we once had some" and bump, with
        //     nothing recording that the CURRENT empty state had already been accounted for — so after a
        //     `prep-restart` with no new traffic, EVERY reboot bumped again (QG measured epoch 2, 3, 4 with
        //     next_seq stuck at 2). The companion then treats an unchanged empty inbox as newly wiped, for ever.
        // ⇒ one explicit, persisted state. `non_empty` is set by the first append after an empty state; `empty`
        //   is set by a wipe/format transition, which is also the ONE place the epoch bumps for a deliberate
        //   wipe. A boot that finds records empty while the marker still says `non_empty` is the GENUINE §10.1
        //   external-loss detect — the arm the whole mechanism exists for — and it bumps exactly once, then
        //   records the acknowledgement so the next boot is stable.
        uint8_t  records_state;       // kRecords* below
    };
    // ★★★ THREE STATES, AND THE THIRD ONE CLOSES A FALSE-WIPE HOLE ([[B134]] QG round 6). Marking `non_empty`
    //     before writing was safe against a MARKER failure but not against a RECORD failure: if the first
    //     `seg_append` then failed having written NOTHING, the append correctly returned false while the medium
    //     stayed empty under a persisted `non_empty` — so the next boot read it as an external records loss and
    //     advanced the epoch for a message that never existed (and a `wipe()` before that reboot bumped too).
    //   `append_pending` says exactly what is true at that moment: "an append was attempted; whether any bytes
    //   landed is a question only the MEDIUM can answer." It is resolved by looking, never by assuming:
    //     · pending + no bytes  -> empty      (the append never landed — no history was lost, so NO bump)
    //     · pending + any bytes -> non_empty  (it landed; a finalizing save may have died — resolve FORWARD)
    //   ⇒ only a genuine `non_empty -> empty` is history loss, and only that advances the epoch.
    enum : uint8_t { kRecordsEmpty = 0, kRecordsNonEmpty = 1, kRecordsAppendPending = 2 };
    static constexpr uint8_t kRecordsStateMax = kRecordsAppendPending;   // the exact valid set is 0..this
    // ⓘ NO kVersion BUMP FOR THIS, and the argument is stated rather than assumed: a v4 blob written before this
    //   change carries {0,1} ONLY, both of which keep their exact meaning and interpretation here — 2 is a NEW
    //   value, not a reinterpretation of an old one — and the struct LENGTH is unchanged (the field was already a
    //   `uint8_t`). So no v4 blob can be misread, and a bump would cost every test node its history for nothing.
    static constexpr uint32_t kMagic       = 0x4D524958u;  // 'MRIX'
    // ★ v4 (2026-08-29, [[B134]] QG round 5): the Meta blob gained `records_state`, so the STRUCT GREW.
    // ⛔ AN OLD (v3) BLOB IS THEREFORE THE WRONG LENGTH AND READS AS `MetaLoad::error` -> `meta_corrupt` -> a
    //    REFUSED MOUNT, and that is a DELIBERATE, TESTED outcome rather than an accident of the length rule
    //    (see the §B134e/5 case). The alternative — reinterpreting a short blob as "probably the previous
    //    layout" — is exactly the guess-at-bytes this arc has spent five rounds removing, and M3 makes the
    //    honest option affordable: MeshRoute is unshipped, so the cost is ONE `factory_reset confirm` on the
    //    flash that first carries this build. That is recorded in the report and the bench script, not left for
    //    an operator to discover. ⓘ The `!version_ok` branch below still serves a SAME-SIZE version change (a
    //    record-format bump that does not grow Meta), which is what it was written for.
    // ★★ v5 (2026-08-29, §CUSTODY-A) — A **SEMANTIC** BUMP AT AN UNCHANGED LAYOUT, which is exactly the case the
    //    `!version_ok` branch below was written for, so it rides the LANDED upgrade path with no new code:
    //    every serialized field keeps its offset and width; what changed is what the `type` BYTE MEANS. A v4
    //    store's E2E-ACK receipts carry numeric type 3, and after the DATA-namespace transition 3 is
    //    `DATA_TYPE_SEALED_RELAY` — an application record. ⛔ Reading those bytes forward would silently
    //    reclassify every stored receipt as a sealed user message (and, one layer up, the companion's
    //    `"type":"e2e_ack"` row would become `"type":3`). The wipe is what makes that IMPOSSIBLE rather than
    //    merely unlikely: no v4 record survives to be reinterpreted. Cost is one inbox history on the flash
    //    that first carries this build — M3 (unshipped) makes that affordable, and it is in the bench script.
    //    ⓘ NOT a Meta LENGTH change (contrast v4, which grew the struct): a v4 blob still loads, so `_meta`
    //      carries a TRUSTWORTHY next_seq/epoch across the wipe and nothing is guessed.
    static constexpr uint16_t kVersion     = 5;            // v4 (2026-08-29 [[B134]]): Meta gained records_state (struct GREW). v3 (2026-07-19 §GapA-durable): record header gained origin_layer. v2 (§S5): +team_id. Old-format records unparseable -> begin() wipes + bumps epoch on a version mismatch.
    // read_since loads a WHOLE segment into this scratch, so a segment must be <= it (begin() guards _seg).
    // Tied to protocol::inbox_segment_bytes so the segment size and the scratch are ONE value — a larger
    // segment would silently truncate the read (drop every record past the scratch). .bss, single-threaded.
    static constexpr uint32_t kScratchBytes = protocol::inbox_segment_bytes;   // 4 KiB
    static_assert(kScratchBytes >= inbox_record_max_bytes, "a single inbox record must fit the read scratch");

    uint16_t ring_segs() const { return static_cast<uint16_t>(_cap / _seg + 1); }
    bool save_meta() { return _meta_io->save(&_meta, sizeof _meta); }
    // ---- §B135 torn-tail recovery (see the block comment above append()) ----
    bool head_tail_torn() const;                 // does the HEAD segment's frame chain end exactly at its last byte?
    bool note_torn_append(uint32_t head_sz_before);   // always returns false; seals + fixes _total IFF bytes landed
    // S2 flash-validation rule: range-check a flash-loaded struct BEFORE its fields index / divide / bound a loop.
    // A torn /mri_* meta with seg_count==0 hard-faults the `% seg_count` below (DBZ), and head_seg>=seg_count makes
    // the `i == head_seg` ring walk never terminate (infinite boot loop). Reject those -> begin() re-inits fresh meta.
    // MAGIC + STRUCTURE only (NOT version): begin() needs the prior _meta (incl. version) to detect a record-format
    // UPGRADE and wipe+re-epoch the records while preserving next_seq (§S5; the retired nRF52 twin mirrored it). The
    // structural checks stay — a torn meta (seg_count==0 / head|tail out of range) must still be rejected as fresh.
    // ⛔ THREE-VALUED since [[B134]] QG round 3. A structurally impossible record is `error`, NOT `absent`: the
    //    blob WAS there, so something wrote or corrupted it, and "start over" is never the right reading of that.
    //    MAGIC + STRUCTURE only (NOT version): begin() needs the prior _meta (incl. version) to detect a record-
    //    format UPGRADE and wipe+re-epoch the records while preserving next_seq (§S5). The structural checks stay —
    //    a torn meta (seg_count==0 -> `% seg_count` DBZ; head|tail out of range -> a ring walk that never
    //    terminates) must be rejected BEFORE its fields divide or bound a loop.
    MetaLoad load_meta() {
        const MetaLoad r = _meta_io->load(&_meta, sizeof _meta);
        if (r != MetaLoad::loaded) return r;                    // absent / error pass through unchanged
        if (_meta.magic != kMagic || _meta.seg_count != ring_segs()
            || _meta.head_seg >= _meta.seg_count || _meta.tail_seg >= _meta.seg_count) return MetaLoad::error;
        // ⛔ THE MARKER IS RANGE-CHECKED TOO ([[B134]] QG round 6) — the v4 fail-loud policy applied to the field
        //    v4 itself added. An out-of-range value is not a state this store has ever written, so it is corrupt
        //    metadata; accepting it silently bypassed external-loss detection entirely (a value of 2 under the
        //    two-state scheme was neither `empty` nor `non_empty`, so every arm that tested the marker fell
        //    through and the store mounted with fault=0 over a partition it could not classify).
        if (_meta.records_state > kRecordsStateMax) return MetaLoad::error;
        return MetaLoad::loaded;
    }

    ISegmentStore* _records;
    IMetaStore*    _meta_io;
    uint32_t       _cap, _seg;
    Meta           _meta{};
    uint16_t       _count = 0;        // DIAGNOSTIC ONLY: appends THIS boot — 0 after a restore (begin() rebuilds
                                      //   _total/bytes for the cap, not _count), and NOT decremented on drop-oldest
                                      //   (an upper bound). The Inbox uses pull/read_since for logic, never count().
    uint32_t       _total = 0;        // live bytes across the ring
    bool           _ok = false;
    // ★★ [[B134]] QG BLOCKER 1 — THE UNPERSISTED-TOPOLOGY LATCH. A `save_meta()` that FAILED leaves the ring in RAM
    //    describing a topology the medium does not have, and the defect that follows is the worst class this project
    //    knows: a rotation persisted nothing, `append` returned TRUE, and after a reboot the record was GONE — so a
    //    TOMBSTONE would report `erased` and the deleted message would COME BACK. That is [[B134]] itself,
    //    resurrected through its own fix's seam. ⇒ the failure is LATCHED here and the next append RETRIES the save
    //    before writing anything; if it still will not persist, the append is REFUSED (C2 — no silent degrade, and
    //    `Inbox::erase` turns the refusal into `io_error` = a loud DELETE FAILED rather than a false success).
    bool           _meta_dirty  = false;
    SegMountFault  _fault       = SegMountFault::none;   // why the LAST begin() refused (mount_fault())
    bool           _head_sealed = false;   // §B135: the head segment ends in a TORN frame -> the next append MUST roll
                                           //   away from it first. Re-derived at begin() (see head_tail_torn()) because
                                           //   the power cut that tears a frame also loses this RAM flag.
    inline static uint8_t s_scratch[kScratchBytes];
};

// ---- the segmented-log logic (ported verbatim from the retired twin; the seams are now the interfaces) -------
inline bool SegmentedInboxStore::begin() {
    // A segment can't exceed the read scratch (read_since loads a whole segment into it). Fail LOUD rather than
    // silently truncate reads — the inbox stays disabled, visible at boot, instead of dropping records past 4 KB.
    _fault = SegMountFault::none;
    if (_seg > kScratchBytes) { _fault = SegMountFault::seg_too_big; return false; }
    bool formatted = false;
    if (!_records->mount(&formatted)) { _fault = SegMountFault::records_unmountable; return false; }   // fail loud (Inbox stays disabled)

    // ★★★ [[B134]] QG ROUND 2 — THE RECORDS ARE INSPECTED **BEFORE** THE META IS INTERPRETED, and the order is the
    //     whole fix. "No metadata" is not a fact about the store; it is a QUESTION whose answer depends entirely on
    //     whether records exist, and the old code answered it without ever looking.
    bool insp_ok = true;
    const bool have_records = _records->any_segments(&insp_ok);
    // ⛔ AN UNANSWERABLE INSPECTION IS NOT "EMPTY". Routing a transient FS error into the fresh-init path is the
    //    same silent re-initialise by another door, so it fails the mount instead.
    if (!insp_ok) { _fault = SegMountFault::records_uninspectable; return false; }

    // ★★★ [[B134]] QG ROUND 3 — CORRUPT IS NOT ABSENT. Read the metadata's THREE states and dispatch on all
    //     three. An `error` fails loud REGARDLESS of record count: the sequence high-water and the epoch live
    //     nowhere else, so a meta that came back wrong cannot be replaced with a guess even when the record store
    //     is empty (which, after a `prep-restart`, is precisely the normal state).
    const MetaLoad ml = load_meta();
    if (ml == MetaLoad::error) { _fault = SegMountFault::meta_corrupt; return false; }
    const bool had_meta   = (ml == MetaLoad::loaded);           // magic + structure matched -> _meta holds the PRIOR values (incl. version)
    const bool version_ok = had_meta && _meta.version == kVersion;
    // ★★ [[B134]] QG BLOCKER 1 — EVERY SAVE IN begin() IS LOAD-BEARING AND IS NOW CHECKED, and a failure FAILS THE
    //    MOUNT (⇒ `Inbox` stays DISABLED, loudly, at boot) rather than running non-durably. A store whose baseline
    //    topology / epoch / high-water cannot reach the medium is not a durable store, and pretending otherwise is
    //    the same lie one layer up.
    if (!had_meta) {                                            // ⓘ reached ONLY for a TRUE key-absent (see above)
        // ★★★ THE QG ROUND-2 BLOCKER, AND THE DISCRIMINATOR THAT CLOSES IT. An ABSENT meta means one of two
        //     completely different things, and the old code assumed the harmless one:
        //       · NO RECORDS EITHER  -> a genuinely FRESH store (first boot, or the boot after a format). Init.
        //       · RECORDS PRESENT    -> the metadata was LOST OR CORRUPTED OVER A LIVE HISTORY. ⛔ Initialising
        //         here resets head/tail to 0/0, so the tail..head walk sees ONE segment and every other segment's
        //         records become invisible while physically present; and it resets next_seq to 1, so the very next
        //         append REUSES sequences the companion has already filed. QG reproduced exactly that: 6 segments,
        //         1 record visible, `after = 1,1`. Both halves of the corruption contract violated at once.
        // ⇒ FAIL LOUD (C2's default, and the choice is deliberate — see the header note on why not reconstruction).
        //   The node still boots and operates; only the inbox is inert, and `mount_fault()` names why so the
        //   operator can choose `factory_reset confirm` KNOWING it destroys history, rather than having the store
        //   destroy it silently on their behalf.
        // ⓘ `formatted` is part of the test because a just-formatted partition cannot hold records: it keeps a
        //   format-on-corrupt recovery on the fresh path instead of deadlocking it against its own erased records.
        if (have_records && !formatted) { _fault = SegMountFault::meta_lost_over_records; return false; }
        _meta = Meta{};                                         // genuinely fresh -> initialize
        _meta.magic = kMagic; _meta.version = kVersion;
        _meta.seg_count = ring_segs();
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.next_seq = 1; _meta.read_cursor = 0; _meta.epoch = 1;
        _meta.records_state = kRecordsEmpty;                // a fresh store IS empty, and that is ACKNOWLEDGED
        // ⛔ THE FRESH BRANCH USED TO PERSIST NOTHING AT ALL, and that was its own seq-reuse hole: up to
        //    kSeqPersistBatch records could be written before the first `set_next_seq`, and a reboot in that window
        //    restored next_seq = 1 over a log that already held seqs 1..N — duplicate sequences in one store.
        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }
    } else if (!version_ok) {                                   // §S5 UPGRADE: the record header layout changed (+team_id) -> the old
        //  records can't be parsed by the new deserializer -> WIPE them. ⛔ A failed erase leaves OLD-FORMAT bytes
        //  that the new deserializer would parse as garbage records, so it fails the mount rather than proceeding.
        // ⓘ This branch is SAFE where the one above is not, and the difference is the meta: the version came from a
        //   VALID record, so next_seq/epoch are trustworthy and are carried across the wipe. Nothing is guessed.
        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) { _fault = SegMountFault::records_unmountable; return false; }
        _meta.version   = kVersion;                            //  KEEP next_seq (survives on the meta store) so seq never reuses, and BUMP
        _meta.epoch    += 1;                                   //  the epoch so the companion sees the wipe + re-pulls cleanly (the retired twin mirrored it).
        _meta.head_seg  = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        _meta.read_cursor = 0;
        _meta.records_state = kRecordsEmpty;               // the wipe above IS the transition; the epoch bumped with it
        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }
        formatted = true;                                      // records are now empty -> the §10.1 detect below is a no-op (don't double-bump)
    }
    // ★★★ §10.1 EXTERNAL-LOSS DETECTION — THE GENUINE ARM, and the one the whole mechanism exists for: the
    // records store came up EMPTY while the marker still says we HAVE records, so something outside this store
    // destroyed them (a format-on-dirty, an OTAFIX erase, a partition rewrite). Bump the epoch ONCE, reset the
    // ring, and ⛔ RECORD THE ACKNOWLEDGEMENT so the next boot is stable. KEEP next_seq (it survived on the meta
    // store) so seq never reuses; the companion sees the new epoch and re-syncs from 0.
    // ⛔⛔ THE GUARD IS THE MARKER, NOT `next_seq > 1` ([[B134]] QG round 5). `next_seq` only says "this store
    //     once had traffic", which stays true for ever — so it re-fired on EVERY reboot of an empty store and the
    //     epoch climbed 2, 3, 4… with nothing having changed. A deliberate `wipe()` now does its own single bump
    //     and marks the store empty, so it does NOT come through here at all.
    // ⓘ ONLY the version-OK path — the upgrade branch above already wiped, bumped and marked.
    const bool records_empty = formatted || !have_records;   // the ONE inspection, taken above (no second walk)
    // ★★★ RESOLVE `append_pending` FIRST, AND RESOLVE IT BY LOOKING. A pending marker means an append was
    //     attempted and nobody recorded whether its bytes landed; the medium is the only authority. Resolving it
    //     BEFORE the §10.1 arm is what keeps a failed first append from ever reaching the external-loss test.
    //     ⛔ NEITHER RESOLUTION BUMPS THE EPOCH: `pending -> empty` means the append never landed, so no history
    //     was lost; `pending -> non_empty` means it did land, so nothing was lost either.
    if (version_ok && _meta.records_state == kRecordsAppendPending) {
        _meta.records_state = records_empty ? kRecordsEmpty : kRecordsNonEmpty;
        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }
    }
    if (version_ok && records_empty && _meta.records_state == kRecordsNonEmpty) {
        _meta.epoch += 1;
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        _meta.records_state = kRecordsEmpty;               // ★ acknowledged — the next boot must NOT bump again
        // ⛔ [[B134]] QG blocker 1: an UNPERSISTED epoch bump is a wipe the companion never learns about — it keeps
        //    its cursors against a history that no longer exists and silently never re-pulls. Fail the mount.
        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }
    }
    // Recompute the live count + bytes from the ring (the segment sizes are the truth for the byte cap).
    _count = 0; _total = 0;
    for (uint16_t i = _meta.tail_seg; ; i = static_cast<uint16_t>((i + 1) % _meta.seg_count)) {
        uint32_t sz = 0;
        if (_records->seg_size(i, &sz)) _total += sz;          // bytes drive the cap; _count is NOT rebuilt here (it's diag-only, see count())
        if (i == _meta.head_seg) break;
    }
    // ★ §B135 REBOOT ARM. The in-RAM seal below is lost by the very power cut that tears a frame, so the torn tail
    // must be re-detected here — otherwise the FIRST append after the reboot lands behind the torn frame and the
    // reader mis-parses from there on (a record that is physically present becomes unreachable). One segment read.
    _head_sealed = head_tail_torn();
    _ok = true;
    return true;
}

// Walk the HEAD segment's frame chain exactly as read_since does. The chain must consume the segment EXACTLY:
// anything left over is a torn frame (a body that never arrived, a header that did not finish, a length that runs
// past the data). Only the head can acquire a fresh tear — appends go nowhere else — and a segment that was sealed
// while it was head is never appended to again, so its tear stays permanently at its end where the reader stops.
inline bool SegmentedInboxStore::head_tail_torn() const {
    const uint32_t n = _records->seg_read(_meta.head_seg, s_scratch, _seg);
    uint32_t off = 0;
    while (off + 6 <= n) {
        const uint16_t fl = static_cast<uint16_t>(s_scratch[off] | (s_scratch[off + 1] << 8));
        if (fl < 6 || off + fl > n) return true;               // a frame that runs past the bytes present = torn
        off += fl;
    }
    return off != n;                                            // <6 trailing bytes = a torn HEADER
}

// A failed append may have left a PARTIAL frame on the medium. Detect that by size (the only medium-neutral way —
// seg_append is not required to be all-or-nothing) and, if bytes landed: charge them to _total (they occupy the
// ring, so the byte cap must see them) and SEAL the segment. If nothing landed there is nothing to recover from,
// and sealing would burn the rest of a 4 KB segment for free — so it is deliberately conditional.
inline bool SegmentedInboxStore::note_torn_append(uint32_t head_sz_before) {
    uint32_t now = 0;
    if (!_records->seg_size(_meta.head_seg, &now)) now = 0;
    if (now > head_sz_before) { _total += (now - head_sz_before); _head_sealed = true; }
    return false;
}

// ★★ §B135 — TORN-FRAME RECOVERY (pre-existing hole, found by QA on the [[B133]] delete slice 2026-08-06).
// The header and the body are TWO seg_append calls, so a power cut / write failure between them leaves a header
// claiming `framed_len` bytes with fewer present. read_since already stops at such a frame (`off + fl > n`), so a
// torn tail alone is harmless. THE DEFECT IS THE NEXT APPEND: it lands its bytes immediately behind the torn header,
// which now measures long enough to "contain" them, so the reader consumes the new frame AS the torn one's body and
// then resumes at a bogus offset — every record after the tear becomes unreachable while physically present.
// ⇒ ordinary inbox messages could silently vanish, and a [[B133]] tombstone written as a RETRY after a torn write
// could report `erased` while the target stays visible ("a success that isn't", the fifth instance in this project).
// FIX = SEAL-AND-ROLL: a torn segment is never appended to again; the next append rolls to a fresh head, so the
// tear stays permanently at a segment's end where the reader's existing stop is correct. Chosen over TRUNCATION
// because ISegmentStore has no truncate and adding one is a new virtual on a HAL with a device implementation
// (the hazard [[B133]] deliberately avoided), and because append-only media cannot un-write bytes at all.
// ⛔ WHAT SEAL-AND-ROLL DOES NOT COVER — stated, not implied:
//   (a) NO INTEGRITY CHECK. There is no per-record CRC, so a tear that leaves a PLAUSIBLE header plus plausible
//       bytes (corrupt rather than short) still parses. Sealing detects SHORT frames, not corrupt ones.
//   (b) Damage to records EARLIER in the segment (a flash page rewritten under a brown-out) is not detected.
//   (c) The torn frame's bytes and the rest of its segment (up to seg_bytes) are LOST/wasted until the ring laps.
//   (d) ⛔ RETIRED 2026-08-28 ([[B134]] QG blocker 1) — THIS ENTRY USED TO READ *"a `save_meta()` failure is still
//       ignored (pre-existing, unchanged here)"*, AND IT WAS NOT A GAP TO CARVE OUT, IT WAS THE DEFECT. Ignoring
//       the ROTATION save let `append` return TRUE with the head move unpersisted, so a reboot dropped an
//       ACKNOWLEDGED record — and a tombstone written that way reported `erased` while the message CAME BACK.
//       Every load-bearing save is now checked; a failure LATCHES `_meta_dirty`, the next append retries it, and
//       an append that cannot persist its topology is REFUSED. See the latch's own note at `_meta_dirty`.
//   (e) ★ ROTATION IS NOT TRANSACTIONAL: if this append had to roll, the roll may already have erased the OLDEST
//       segment before the write failed. Obtaining a free segment in a full ring IS the eviction, so it cannot be
//       undone — but it is the ring's ordinary drop-oldest and it would have happened on the next successful
//       append anyway. This is why the contract says "no previously readable record is corrupted or misparsed"
//       and NOT "nothing else is mutated" (inbox.h's erase() note was corrected to match).
inline bool SegmentedInboxStore::append(uint32_t seq, const uint8_t* rec, uint16_t len) {
    if (!_ok) return false;
    const uint16_t framed = static_cast<uint16_t>(2 + 4 + len);  // [u16 framed_len][u32 seq][rec]
    if (framed > _seg) return false;                            // a single record bigger than a segment (never: header+body << seg)
    // ★★★ [[B134]] QG blocker 1 — RETRY-BEFORE-WRITE. A previous save_meta() failed, so the topology on the medium
    //     does not describe this ring. Retry it; if it STILL will not persist, REFUSE rather than write a record
    //     (or a tombstone) that a reboot would lose or resurrect. This is also the RECOVERY path: once the medium
    //     accepts the save again, the latch clears and the store resumes normal operation with nothing lost.
    if (_meta_dirty) { if (!save_meta()) return false; _meta_dirty = false; }
    // Roll to a new head segment if this record won't fit the current one — or if the head is SEALED (§B135).
    uint32_t head_sz = 0; _records->seg_size(_meta.head_seg, &head_sz);
    if (_head_sealed || head_sz + framed > _seg) {
        const uint16_t next_head = static_cast<uint16_t>((_meta.head_seg + 1) % _meta.seg_count);
        if (next_head == _meta.tail_seg) {                     // ring full -> drop the oldest segment
            uint32_t tsz = 0; _records->seg_size(_meta.tail_seg, &tsz);
            // ⛔ Charge the bytes back ONLY if they actually went: a failed erase leaves them occupying the ring,
            //    and a `_total` that under-counts the medium is a cap that stops capping.
            if (_records->seg_erase(_meta.tail_seg)) _total -= (tsz <= _total ? tsz : _total);
            _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        }
        // ⛔ [[B134]] QG blocker 1/3: the new head MUST come up empty. If stale lapped bytes survive the erase, this
        //    append lands behind them and `read_since` parses them as frames — the §B135 mis-parse from the other
        //    end. The tail may already have moved, so latch the topology for the next attempt and refuse.
        if (!_records->seg_erase(next_head)) { _meta_dirty = true; return false; }
        _meta.head_seg = next_head;
        _head_sealed = false;                                  // §B135: the tear (if any) is behind us, in a segment we never append to again
        head_sz = 0;                                           //   and the fresh head is empty — the torn-detect below measures from 0
        // ★★★ THE BLOCKER-1 CHECK ITSELF. This save is the ONLY thing that tells the next boot where the head is.
        //     Ignoring it (as this line did until 2026-08-28) meant: rotation moves the head, the record is written
        //     into a segment the persisted meta does not include in its tail..head walk, `append` returns TRUE — and
        //     after a reboot the record is GONE while the caller was told it landed. QG reproduced exactly that.
        if (!save_meta()) { _meta_dirty = true; return false; }
    }
    // ★★★ THE EMPTY -> APPEND_PENDING TRANSITION, PERSISTED **BEFORE** THE RECORD IS WRITTEN.
    //     Order is the contract: if this marker save fails the append is REFUSED, so a record can never be
    //     acknowledged under a marker that still says "empty" (the blocker-1 discipline, applied to this field).
    //     ⛔ AND IT WRITES `append_pending`, NOT `non_empty` ([[B134]] QG round 6): `non_empty` would be a claim
    //     about bytes that have not been written yet, and if the very next `seg_append` failed having written
    //     NOTHING, that claim would outlive the failed append and cost a false external-loss bump on the next
    //     boot. `pending` claims only what is true — an attempt was made — and the mount resolves it by LOOKING.
    // ⓘ ONLY from `empty`. From `pending` there is nothing to persist (the medium already says pending, which is
    //   still exactly right), and from `non_empty` the marker is already true — so an ordinary append into a
    //   populated store does no meta write at all. The dance costs two meta writes per empty->non_empty
    //   transition, i.e. once per boot-after-a-wipe, never per record.
    if (_meta.records_state == kRecordsEmpty) {
        _meta.records_state = kRecordsAppendPending;
        if (!save_meta()) { _meta.records_state = kRecordsEmpty; _meta_dirty = true; return false; }
    }
    const uint8_t hdr[6] = { static_cast<uint8_t>(framed), static_cast<uint8_t>(framed >> 8),
                             static_cast<uint8_t>(seq), static_cast<uint8_t>(seq >> 8),
                             static_cast<uint8_t>(seq >> 16), static_cast<uint8_t>(seq >> 24) };
    if (!_records->seg_append(_meta.head_seg, hdr, 6))          return note_torn_append(head_sz);   // §B135
    if (len && !_records->seg_append(_meta.head_seg, rec, len)) return note_torn_append(head_sz);   // §B135 — the dangerous half
    _total += framed; _count++;
    // ★ RESOLVE FORWARD: the bytes are on the medium, so the store really is non-empty. ⛔ A FAILURE HERE IS NOT A
    //   REFUSAL, and that asymmetry is deliberate: the record IS durable, and a mount that finds `pending` with
    //   bytes present resolves to `non_empty` anyway — so the honest verdict is success, with the retry latched.
    //   The RAM state is rolled back to `pending` on failure so it never disagrees with the medium.
    if (_meta.records_state != kRecordsNonEmpty) {
        _meta.records_state = kRecordsNonEmpty;
        if (!save_meta()) { _meta.records_state = kRecordsAppendPending; _meta_dirty = true; }
    }
    // Drop-oldest if the WHOLE store is over the byte cap (a roll already handled the per-segment fill).
    // ★ [[B134]] QG blocker 1 — THIS SAVE IS CHECKED TOO, BUT IT IS **NOT** LOAD-BEARING FOR THE RECORD JUST
    //   WRITTEN, and the difference is stated rather than assumed: the record is at the HEAD, whose position is
    //   already persisted (the roll above saved it, or it never moved). An unpersisted TAIL advance only means the
    //   next boot starts its walk one segment earlier — at a segment that is now empty, reads 0 bytes, and is
    //   walked straight past. ⇒ nothing previously readable is lost and the new record is still reached, so the
    //   append stays honestly acknowledged. What a failure DOES mean is that the topology is stale, so it latches
    //   the retry and stops evicting (further evictions would only widen the gap for the same failure).
    while (_total > _cap && _meta.tail_seg != _meta.head_seg) {
        uint32_t tsz = 0; _records->seg_size(_meta.tail_seg, &tsz);
        if (!_records->seg_erase(_meta.tail_seg)) { _meta_dirty = true; break; }
        _total -= (tsz <= _total ? tsz : _total);
        _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        if (!save_meta()) { _meta_dirty = true; break; }
    }
    return true;
}

inline uint16_t SegmentedInboxStore::read_since(uint32_t since_seq, ReadCb cb, void* ctx) const {
    if (!_ok) return 0;
    uint16_t visited = 0;
    const uint32_t cap = _seg;                                  // begin() guarantees _seg <= kScratchBytes (no truncation)
    // Walk tail..head (oldest segment first); within a segment, records are append-order (oldest first).
    for (uint16_t i = _meta.tail_seg; ; i = static_cast<uint16_t>((i + 1) % _meta.seg_count)) {
        const uint32_t n = _records->seg_read(i, s_scratch, cap);   // whole segment into the scratch (<= kScratchBytes)
        uint32_t off = 0;
        while (off + 6 <= n) {                                      // frame = [u16 framed_len][u32 seq][rec]
            const uint16_t fl = static_cast<uint16_t>(s_scratch[off] | (s_scratch[off + 1] << 8));
            if (fl < 6 || off + fl > n) break;                      // torn/truncated tail record -> stop this segment
            const uint32_t s = static_cast<uint32_t>(s_scratch[off + 2]) | (static_cast<uint32_t>(s_scratch[off + 3]) << 8)
                             | (static_cast<uint32_t>(s_scratch[off + 4]) << 16) | (static_cast<uint32_t>(s_scratch[off + 5]) << 24);
            if (s > since_seq) {
                ++visited;
                if (!cb(ctx, s, s_scratch + off + 6, static_cast<uint16_t>(fl - 6))) return visited;
            }
            off += fl;
        }
        if (i == _meta.head_seg) break;
    }
    return visited;
}

}  // namespace meshroute
