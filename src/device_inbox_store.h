// MeshRoute — src/device_inbox_store.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The DEVICE InboxStore (persistent-inbox spec Phase 2) — a segmented append-log over flash, behind the
// platform-neutral meshroute::InboxStore contract (lib/core/inbox.h). Two instances (DM + channel).
//
// LAYOUT (the §10.1 high-water/epoch survival falls out of this split):
//   - META  -> InternalFS (on-chip, the proven device_nv path): {next_seq, read_cursor, epoch, ring head/tail}.
//              SEPARATE from the records, so a records-store wipe can't lose the seq high-water or the epoch.
//   - RECORDS -> the EXTERNAL flash (nRF52: 2 MB QSPI), a fixed RING of N segment files `<dir>/<i>`. Append to
//              the head segment; roll at seg_bytes; drop-oldest = erase the tail segment (coarse, wear-friendly,
//              no rewrite — spec §9). On-flash frame per record: [u16 framed_len][u32 seq][record bytes].
//   - EPOCH (§10.1): begin() bumps meta.epoch when the records store came up EMPTY/formatted while meta.next_seq>1
//              (the records were wiped). next_seq is preserved from the InternalFS meta -> seq never reuses; the
//              companion reads the bumped epoch and re-syncs from 0. See INBOX_SYNC_CONTRACT.md.
//
// REALITY SPLIT (like device_nv): I compile-verify this under both board envs; the on-metal QSPI bring-up +
// flash read/write/wear + format-on-dirty are **BENCH-VERIFIED BY THE USER** — the QSPI primitives are the
// `qspi_*` seam below, marked [BENCH]. The segmented-log + meta + epoch LOGIC is platform-neutral and the
// part to review. ESP32 (Heltec): a LittleFS-data-partition backend is the NEXT slice — for now its qspi_*
// return failure, so begin() fails and Inbox stays disabled on Heltec (record_* inert) until it lands.
#pragma once
#include "inbox.h"
#include <stdint.h>
#include <string.h>

namespace mrinbox {

// ---- meta record (InternalFS; mirrors device_nv's Blob discipline: magic + version, rejected on mismatch) ----
struct Meta {
    uint32_t magic;        // kMagic
    uint16_t version;      // kVersion
    uint16_t head_seg;     // ring head segment index (append target)
    uint16_t tail_seg;     // ring tail segment index (oldest live segment)
    uint16_t seg_count;    // ring size N (= cap/seg + 1); fixed per store, sanity-checked
    uint32_t next_seq;     // §6 high-water (survives a records wipe — that's the whole point of meta-on-InternalFS)
    uint32_t read_cursor;  // UX unread badge
    uint32_t epoch;        // §10.1 storage epoch (bumps on a records-store wipe)
};
constexpr uint32_t kMagic   = 0x4D524958u;  // 'MRIX'
constexpr uint16_t kVersion = 2;            // v2 (2026-06-13): record header gained layer_id (§2/Q13) — old meta rejected, store re-inits

}  // namespace mrinbox

#if defined(ARDUINO)
namespace mrinbox {

// read_since reads a WHOLE segment into this scratch + walks its frames, so a segment must be <= it (begin()
// guards seg_bytes). Tied to protocol::inbox_segment_bytes so the segment size and the scratch are ONE value —
// a larger segment would silently truncate the read (drop every record past the scratch). One static buffer
// (single-threaded); .bss, not the stack (avoids the try_drain stack-overflow class).
static constexpr uint32_t kSegScratchBytes = meshroute::protocol::inbox_segment_bytes;   // 4 KiB
static uint8_t kSegScratch[kSegScratchBytes];

// =============================================================================
// The store. The class logic is platform-neutral; the flash primitives are the
// ifs_* (InternalFS meta) + qspi_* (external-flash records) seams below.
// =============================================================================
class DeviceInboxStore : public meshroute::InboxStore {
public:
    // dir = the records directory on QSPI (e.g. "/dm"); meta_path = the InternalFS meta file (e.g. "/mri_dm").
    DeviceInboxStore(const char* dir, const char* meta_path, uint32_t cap_bytes, uint32_t seg_bytes)
        : _dir(dir), _meta_path(meta_path), _cap(cap_bytes), _seg(seg_bytes) {}

    bool begin() override;
    bool append(uint32_t seq, const uint8_t* rec, uint16_t len) override;
    uint16_t read_since(uint32_t since_seq, ReadCb cb, void* ctx) const override;
    uint32_t persisted_next_seq() const override { return _meta.next_seq; }
    bool     set_next_seq(uint32_t next) override { _meta.next_seq = next; return save_meta(); }
    uint32_t read_cursor() const override { return _meta.read_cursor; }
    bool     set_read_cursor(uint32_t seq) override { _meta.read_cursor = seq; return save_meta(); }
    uint16_t count() const override { return _count; }
    uint32_t storage_epoch() const override { return _meta.epoch; }

private:
    // ---- meta (InternalFS) ----
    bool load_meta();                 // false => missing/mismatched (caller treats as fresh)
    bool save_meta();
    // ---- the ring (computed at begin) ----
    void seg_path(uint16_t idx, char* out, size_t cap) const;   // "<dir>/<idx>"
    uint16_t ring_segs() const { return static_cast<uint16_t>(_cap / _seg + 1); }

    // ---- [BENCH] external-flash record primitives (nRF52 QSPI; ESP32 = next slice) ----
    static bool     qspi_mount(bool* formatted);                            // mount; *formatted=true if a format was needed
    bool            qspi_seg_size(uint16_t idx, uint32_t* size) const;      // file size; false if absent
    bool            qspi_seg_append(uint16_t idx, const uint8_t* b, uint16_t n);
    uint32_t        qspi_seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const;  // read whole segment; bytes read
    void            qspi_seg_erase(uint16_t idx);                           // remove/empty the segment
    bool            qspi_any_segments() const;                              // does the dir hold ANY record bytes?

    const char* _dir;
    const char* _meta_path;
    uint32_t    _cap, _seg;
    Meta        _meta{};
    uint16_t    _count = 0;          // DIAGNOSTIC ONLY: appends THIS boot (0 after a restore — begin() rebuilds bytes,
                                     //   not count; not decremented on drop). The Inbox uses pull, never count().
    uint32_t    _total = 0;          // live bytes across the ring
    bool        _ok = false;         // begin() succeeded (mount + meta consistent)
};

// ---- the segmented-log logic (platform-neutral; uses the seams above) -------------------------------------
inline bool DeviceInboxStore::begin() {
    if (_seg > kSegScratchBytes) return false;                  // a segment can't exceed the read scratch -> fail loud (no silent truncation)
    bool formatted = false;
    if (!qspi_mount(&formatted)) return false;                  // records store unmountable -> fail loud (Inbox stays disabled)

    const bool had_meta = load_meta();
    if (!had_meta) {                                            // fresh / unreadable meta -> initialize
        _meta = Meta{};
        _meta.magic = kMagic; _meta.version = kVersion;
        _meta.seg_count = ring_segs();
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.next_seq = 1; _meta.read_cursor = 0; _meta.epoch = 1;
    }
    // §10.1 wipe detection: the records store came up EMPTY but the (InternalFS) meta says we had records ->
    // the records were wiped (format-on-dirty / OTAFIX QSPI erase). BUMP the epoch, reset the ring; KEEP
    // next_seq (it survived on InternalFS) so seq never reuses. The companion sees the new epoch + re-syncs.
    const bool records_empty = formatted || !qspi_any_segments();
    if (had_meta && records_empty && _meta.next_seq > 1) {
        _meta.epoch += 1;
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        save_meta();
    }
    // Recompute the live count + bytes from the ring (the segment files are the truth for the byte cap).
    _count = 0; _total = 0;
    for (uint16_t i = _meta.tail_seg; ; i = static_cast<uint16_t>((i + 1) % _meta.seg_count)) {
        uint32_t sz = 0;
        if (qspi_seg_size(i, &sz)) _total += sz;               // bytes drive the cap; _count is NOT rebuilt here (diag-only, see count())
        if (i == _meta.head_seg) break;
    }
    _ok = true;
    return true;
}

inline bool DeviceInboxStore::append(uint32_t seq, const uint8_t* rec, uint16_t len) {
    if (!_ok) return false;
    const uint16_t framed = static_cast<uint16_t>(2 + 4 + len);  // [u16 framed_len][u32 seq][rec]
    if (framed > _seg) return false;                            // a single record bigger than a segment (never: header+body << seg)
    // Roll to a new head segment if this record won't fit the current one.
    uint32_t head_sz = 0; qspi_seg_size(_meta.head_seg, &head_sz);
    if (head_sz + framed > _seg) {
        const uint16_t next_head = static_cast<uint16_t>((_meta.head_seg + 1) % _meta.seg_count);
        if (next_head == _meta.tail_seg) {                     // ring full -> drop the oldest segment
            uint32_t tsz = 0; qspi_seg_size(_meta.tail_seg, &tsz);
            qspi_seg_erase(_meta.tail_seg);
            _total -= (tsz <= _total ? tsz : _total);
            _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        }
        qspi_seg_erase(next_head);                             // the new head starts empty (it may hold stale lapped bytes)
        _meta.head_seg = next_head;
        save_meta();                                           // persist the ring move (infrequent; not per-append)
    }
    uint8_t hdr[6] = { static_cast<uint8_t>(framed), static_cast<uint8_t>(framed >> 8),
                       static_cast<uint8_t>(seq), static_cast<uint8_t>(seq >> 8),
                       static_cast<uint8_t>(seq >> 16), static_cast<uint8_t>(seq >> 24) };
    if (!qspi_seg_append(_meta.head_seg, hdr, 6)) return false;
    if (len && !qspi_seg_append(_meta.head_seg, rec, len)) return false;
    _total += framed; _count++;
    // Drop-oldest if the WHOLE store is over the byte cap (a roll already handled the per-segment fill).
    while (_total > _cap && _meta.tail_seg != _meta.head_seg) {
        uint32_t tsz = 0; qspi_seg_size(_meta.tail_seg, &tsz);
        qspi_seg_erase(_meta.tail_seg);
        _total -= (tsz <= _total ? tsz : _total);
        _meta.tail_seg = static_cast<uint16_t>((_meta.tail_seg + 1) % _meta.seg_count);
        save_meta();
    }
    return true;
}

inline uint16_t DeviceInboxStore::read_since(uint32_t since_seq, ReadCb cb, void* ctx) const {
    if (!_ok) return 0;
    uint16_t visited = 0;
    const uint32_t cap = _seg;                                  // begin() guarantees _seg <= kSegScratchBytes (no truncation)
    // Walk tail..head (oldest segment first); within a segment, records are append-order (oldest first).
    for (uint16_t i = _meta.tail_seg; ; i = static_cast<uint16_t>((i + 1) % _meta.seg_count)) {
        const uint32_t n = qspi_seg_read(i, kSegScratch, cap);   // whole segment into the scratch (<= kSegScratchBytes)
        uint32_t off = 0;
        while (off + 6 <= n) {                                   // frame = [u16 framed_len][u32 seq][rec]
            const uint16_t fl = static_cast<uint16_t>(kSegScratch[off] | (kSegScratch[off + 1] << 8));
            if (fl < 6 || off + fl > n) break;                   // torn/truncated tail record -> stop this segment
            const uint32_t s = static_cast<uint32_t>(kSegScratch[off + 2]) | (static_cast<uint32_t>(kSegScratch[off + 3]) << 8)
                             | (static_cast<uint32_t>(kSegScratch[off + 4]) << 16) | (static_cast<uint32_t>(kSegScratch[off + 5]) << 24);
            if (s > since_seq) {
                ++visited;
                if (!cb(ctx, s, kSegScratch + off + 6, static_cast<uint16_t>(fl - 6))) return visited;
            }
            off += fl;
        }
        if (i == _meta.head_seg) break;
    }
    return visited;
}

inline void DeviceInboxStore::seg_path(uint16_t idx, char* out, size_t cap) const {
    // "<dir>/<idx>" — small, no heap.
    size_t p = 0;
    for (const char* d = _dir; *d && p + 1 < cap; ++d) out[p++] = *d;
    if (p + 1 < cap) out[p++] = '/';
    char num[6]; int np = 0; uint16_t v = idx;
    do { num[np++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v && np < 6);
    while (np-- > 0 && p + 1 < cap) out[p++] = num[np];
    out[p < cap ? p : cap - 1] = '\0';
}

}  // namespace mrinbox

// ============================ platform flash backends =======================================================
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262)
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
namespace mrinbox {

// ---- META on InternalFS (REAL — the proven device_nv File API; lives SEPARATE from the records so it
//      survives a QSPI records wipe → §10.1's high-water/epoch survival). ----------------------------------
bool DeviceInboxStore::load_meta() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS); if (!f.open(_meta_path, FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&_meta), sizeof(_meta)); f.close();
    return n == static_cast<int>(sizeof(_meta)) && _meta.magic == kMagic && _meta.version == kVersion;
}
bool DeviceInboxStore::save_meta() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove(_meta_path);
    File f(InternalFS); if (!f.open(_meta_path, FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&_meta), sizeof(_meta)); f.close();
    return n == static_cast<int>(sizeof(_meta));
}

// ---- RECORDS on the external 2 MB QSPI — BENCH-TODO (the records backend) -------------------------------
// PROVEN DEAD-END (don't retry): `adafruit/Adafruit SPIFlash` pulls the `SdFat - Adafruit Fork`, whose
// `SS`/`File` clash breaks Adafruit_TinyUSB on this BSP, and `Adafruit_LittleFS` has NO ctor over an
// `Adafruit_SPIFlash` (it wants a raw `lfs_config`). Bring the XIAO's 2 MB QSPI up the MeshCore way (its
// working external-flash + littlefs instance), then implement the six qspi_* below by MIRRORING the
// load_meta/save_meta File API above onto that external FS object — the segmented-log mechanics are just
// open/seek/read/write/remove on `<dir>/<idx>` files (see seg_path + the framing in append/read_since).
// Until wired, these return false → begin() fails → the inbox is disabled on device (record_* inert), like
// an unprovisioned device_nv. Define MRINBOX_QSPI_READY once the six methods are real.
#ifndef MRINBOX_QSPI_READY
bool     DeviceInboxStore::qspi_mount(bool*) { return false; }                       // [BENCH-TODO] mount the QSPI LittleFS
bool     DeviceInboxStore::qspi_seg_size(uint16_t, uint32_t*) const { return false; }
bool     DeviceInboxStore::qspi_seg_append(uint16_t, const uint8_t*, uint16_t) { return false; }
uint32_t DeviceInboxStore::qspi_seg_read(uint16_t, uint8_t*, uint32_t) const { return 0; }
void     DeviceInboxStore::qspi_seg_erase(uint16_t) {}
bool     DeviceInboxStore::qspi_any_segments() const { return false; }
#endif

}  // namespace mrinbox
#else   // ESP32 (Heltec) + any other board: the records backend is the NEXT slice -> begin() fails (Inbox disabled)
namespace mrinbox {
bool DeviceInboxStore::qspi_mount(bool*) { return false; }
bool DeviceInboxStore::qspi_seg_size(uint16_t, uint32_t*) const { return false; }
bool DeviceInboxStore::qspi_seg_append(uint16_t, const uint8_t*, uint16_t) { return false; }
uint32_t DeviceInboxStore::qspi_seg_read(uint16_t, uint8_t*, uint32_t) const { return 0; }
void DeviceInboxStore::qspi_seg_erase(uint16_t) {}
bool DeviceInboxStore::qspi_any_segments() const { return false; }
bool DeviceInboxStore::load_meta() { return false; }
bool DeviceInboxStore::save_meta() { return false; }
}  // namespace mrinbox
#endif

#endif  // ARDUINO
