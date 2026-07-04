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
//   - EPOCH (§10.1): begin() bumps meta.epoch when the records came up EMPTY/formatted while meta.next_seq>1
//                (records wiped). next_seq is preserved from meta -> seq never reuses; the companion sees the
//                bumped epoch and re-syncs from 0 (INBOX_SYNC_CONTRACT.md).
//
// This supersedes the Arduino-gated logic in src/device_inbox_store.h: that same begin/append/read_since,
// extracted so it runs on the host. The device QSPI/InternalFS backends (the two interfaces) are the thin HAL.
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
    virtual void     seg_erase(uint16_t idx) = 0;
    virtual bool     any_segments() const = 0;                               // does ANY segment hold bytes? (wipe detector)
};

// The META blob HAL — a tiny fixed blob that MUST survive a records wipe (device: InternalFS, off the QSPI).
struct IMetaStore {
    virtual ~IMetaStore() = default;
    virtual bool load(void* blob, uint16_t len) = 0;        // false => missing/unreadable (caller treats as fresh)
    virtual bool save(const void* blob, uint16_t len) = 0;
};

class SegmentedInboxStore : public InboxStore {
public:
    SegmentedInboxStore(ISegmentStore& records, IMetaStore& meta, uint32_t cap_bytes, uint32_t seg_bytes)
        : _records(&records), _meta_io(&meta), _cap(cap_bytes), _seg(seg_bytes) {}

    bool     begin() override;
    bool     append(uint32_t seq, const uint8_t* rec, uint16_t len) override;
    uint16_t read_since(uint32_t since_seq, ReadCb cb, void* ctx) const override;
    uint32_t persisted_next_seq() const override { return _meta.next_seq; }
    bool     set_next_seq(uint32_t next) override { _meta.next_seq = next; return save_meta(); }
    uint32_t read_cursor() const override { return _meta.read_cursor; }
    bool     set_read_cursor(uint32_t seq) override { _meta.read_cursor = seq; return save_meta(); }
    uint16_t count() const override { return _count; }
    uint32_t storage_epoch() const override { return _meta.epoch; }

private:
    struct Meta {
        uint32_t magic; uint16_t version;
        uint16_t head_seg; uint16_t tail_seg; uint16_t seg_count;
        uint32_t next_seq;            // §6 high-water (survives a records wipe — that's the point of meta-off-QSPI)
        uint32_t read_cursor;         // UX unread badge
        uint32_t epoch;               // §10.1 storage epoch (bumps on a records-store wipe)
    };
    static constexpr uint32_t kMagic       = 0x4D524958u;  // 'MRIX'
    static constexpr uint16_t kVersion     = 1;
    // read_since loads a WHOLE segment into this scratch, so a segment must be <= it (begin() guards _seg).
    // Tied to protocol::inbox_segment_bytes so the segment size and the scratch are ONE value — a larger
    // segment would silently truncate the read (drop every record past the scratch). .bss, single-threaded.
    static constexpr uint32_t kScratchBytes = protocol::inbox_segment_bytes;   // 4 KiB
    static_assert(kScratchBytes >= inbox_record_max_bytes, "a single inbox record must fit the read scratch");

    uint16_t ring_segs() const { return static_cast<uint16_t>(_cap / _seg + 1); }
    bool save_meta() { return _meta_io->save(&_meta, sizeof _meta); }
    // S2 flash-validation rule: range-check a flash-loaded struct BEFORE its fields index / divide / bound a loop.
    // A torn /mri_* meta with seg_count==0 hard-faults the `% seg_count` below (DBZ), and head_seg>=seg_count makes
    // the `i == head_seg` ring walk never terminate (infinite boot loop). Reject those -> begin() re-inits fresh meta.
    bool load_meta() {
        return _meta_io->load(&_meta, sizeof _meta) && _meta.magic == kMagic && _meta.version == kVersion
            && _meta.seg_count == ring_segs() && _meta.head_seg < _meta.seg_count && _meta.tail_seg < _meta.seg_count;
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
    inline static uint8_t s_scratch[kScratchBytes];
};

// ---- the segmented-log logic (ported verbatim from device_inbox_store.h; the seams are now the interfaces) ----
inline bool SegmentedInboxStore::begin() {
    // A segment can't exceed the read scratch (read_since loads a whole segment into it). Fail LOUD rather than
    // silently truncate reads — the inbox stays disabled, visible at boot, instead of dropping records past 4 KB.
    if (_seg > kScratchBytes) return false;
    bool formatted = false;
    if (!_records->mount(&formatted)) return false;             // records store unmountable -> fail loud (Inbox stays disabled)

    const bool had_meta = load_meta();
    if (!had_meta) {                                            // fresh / unreadable meta -> initialize
        _meta = Meta{};
        _meta.magic = kMagic; _meta.version = kVersion;
        _meta.seg_count = ring_segs();
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.next_seq = 1; _meta.read_cursor = 0; _meta.epoch = 1;
    }
    // §10.1 wipe detection: the records store came up EMPTY but the (separate) meta says we had records -> the
    // records were wiped (format-on-dirty / OTAFIX QSPI erase). BUMP the epoch, reset the ring; KEEP next_seq
    // (it survived on the meta store) so seq never reuses. The companion sees the new epoch + re-syncs from 0.
    const bool records_empty = formatted || !_records->any_segments();
    if (had_meta && records_empty && _meta.next_seq > 1) {
        _meta.epoch += 1;
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        save_meta();
    }
    // Recompute the live count + bytes from the ring (the segment sizes are the truth for the byte cap).
    _count = 0; _total = 0;
    for (uint16_t i = _meta.tail_seg; ; i = static_cast<uint16_t>((i + 1) % _meta.seg_count)) {
        uint32_t sz = 0;
        if (_records->seg_size(i, &sz)) _total += sz;          // bytes drive the cap; _count is NOT rebuilt here (it's diag-only, see count())
        if (i == _meta.head_seg) break;
    }
    _ok = true;
    return true;
}

inline bool SegmentedInboxStore::append(uint32_t seq, const uint8_t* rec, uint16_t len) {
    if (!_ok) return false;
    const uint16_t framed = static_cast<uint16_t>(2 + 4 + len);  // [u16 framed_len][u32 seq][rec]
    if (framed > _seg) return false;                            // a single record bigger than a segment (never: header+body << seg)
    // Roll to a new head segment if this record won't fit the current one.
    uint32_t head_sz = 0; _records->seg_size(_meta.head_seg, &head_sz);
    if (head_sz + framed > _seg) {
        const uint16_t next_head = static_cast<uint16_t>((_meta.head_seg + 1) % _meta.seg_count);
        if (next_head == _meta.tail_seg) {                     // ring full -> drop the oldest segment
            uint32_t tsz = 0; _records->seg_size(_meta.tail_seg, &tsz);
            _records->seg_erase(_meta.tail_seg);
            _total -= (tsz <= _total ? tsz : _total);
            _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        }
        _records->seg_erase(next_head);                        // the new head starts empty (it may hold stale lapped bytes)
        _meta.head_seg = next_head;
        save_meta();                                           // persist the ring move (infrequent; not per-append)
    }
    const uint8_t hdr[6] = { static_cast<uint8_t>(framed), static_cast<uint8_t>(framed >> 8),
                             static_cast<uint8_t>(seq), static_cast<uint8_t>(seq >> 8),
                             static_cast<uint8_t>(seq >> 16), static_cast<uint8_t>(seq >> 24) };
    if (!_records->seg_append(_meta.head_seg, hdr, 6)) return false;
    if (len && !_records->seg_append(_meta.head_seg, rec, len)) return false;
    _total += framed; _count++;
    // Drop-oldest if the WHOLE store is over the byte cap (a roll already handled the per-segment fill).
    while (_total > _cap && _meta.tail_seg != _meta.head_seg) {
        uint32_t tsz = 0; _records->seg_size(_meta.tail_seg, &tsz);
        _records->seg_erase(_meta.tail_seg);
        _total -= (tsz <= _total ? tsz : _total);
        _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        save_meta();
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
