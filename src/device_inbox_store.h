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
constexpr uint16_t kVersion = 4;            // v4 (2026-06-23): record header gained `type` (E2E-ack receipts). v3: +enc (§8b). v2: +layer_id (§2/Q13).
                                            // A version mismatch on an upgrade flash WIPES the QSPI records (old header layout) + BUMPS the epoch (companion re-pulls); next_seq is preserved (monotonic).

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
    // Change-detect (InternalFS self-heal Part 3): a `mark_read` to the SAME cursor must not rewrite the InternalFS
    // meta — an app/companion can fire mark_read at its own cadence during a pull session, and a no-op rewrite is
    // pure write churn (the corruption window). A real cursor advance still persists immediately (user/app-commanded).
    bool     set_read_cursor(uint32_t seq) override { if (seq == _meta.read_cursor) return true; _meta.read_cursor = seq; return save_meta(); }
    uint16_t count() const override { return _count; }
    uint32_t storage_epoch() const override { return _meta.epoch; }
    // factory_reset (§5): drop EVERY record segment (the QSPI records). The InternalFS meta is removed by
    // mrnv::factory_erase(); together they leave the inbox truly empty. Best-effort (idempotent erase).
    void wipe() override { for (uint16_t i = 0; i < ring_segs(); ++i) qspi_seg_erase(i); }

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

    const bool had_meta   = load_meta();                       // magic matched -> _meta holds the PRIOR values (incl. version)
    const bool version_ok = had_meta && _meta.version == kVersion;
    if (!had_meta) {                                            // fresh / unreadable meta -> initialize
        _meta = Meta{};
        _meta.magic = kMagic; _meta.version = kVersion;
        _meta.seg_count = ring_segs();
        _meta.head_seg = _meta.tail_seg = 0;
        _meta.next_seq = 1; _meta.read_cursor = 0; _meta.epoch = 1;
    } else if (!version_ok) {                                  // UPGRADE: the record header layout changed (e.g. +type 2026-06-23) ->
        for (uint16_t i = 0; i < ring_segs(); ++i) qspi_seg_erase(i);  //  the old QSPI records can't be parsed by the new deserializer ->
        _meta.version   = kVersion;                            //  WIPE them. KEEP next_seq (survives on InternalFS) so seq never reuses,
        _meta.epoch    += 1;                                   //  and BUMP the epoch so the companion sees the wipe + re-pulls cleanly.
        _meta.head_seg  = _meta.tail_seg = 0;
        _meta.seg_count = ring_segs();
        _meta.read_cursor = 0;
        save_meta();
        formatted = true;                                      // records are now empty (we just wiped) -> the §10.1 detect below is a no-op
    }
    // §10.1 wipe detection: the records store came up EMPTY but the (InternalFS) meta says we had records ->
    // the records were wiped (format-on-dirty / OTAFIX QSPI erase). BUMP the epoch, reset the ring; KEEP
    // next_seq (it survived on InternalFS) so seq never reuses. The companion sees the new epoch + re-syncs.
    // ONLY for the version-OK path — the upgrade branch above already wiped + bumped (don't double-bump).
    const bool records_empty = formatted || !qspi_any_segments();
    if (version_ok && records_empty && _meta.next_seq > 1) {
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
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>   // oltaco/CustomLFS — the `QSPIFlash` global (nrfx_qspi + LittleFS, P25Q16H)
    #define MRINBOX_QSPI_READY         // -> the real qspi_* below (drops the disabled-stub block)
  #endif
namespace mrinbox {

// ---- META on InternalFS (REAL — the proven device_nv File API; lives SEPARATE from the records so it
//      survives a QSPI records wipe → §10.1's high-water/epoch survival). ----------------------------------
bool DeviceInboxStore::load_meta() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS); if (!f.open(_meta_path, FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&_meta), sizeof(_meta)); f.close();
    // MAGIC-only here (NOT version): begin() needs the prior _meta (next_seq/epoch/version) to detect a version
    // UPGRADE and wipe+re-epoch the records while preserving next_seq. A bad magic = no/garbage meta = a fresh init.
    return n == static_cast<int>(sizeof(_meta)) && _meta.magic == kMagic;
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
bool     DeviceInboxStore::qspi_mount(bool*) { return false; }                       // QSPIFLASH not set -> inbox disabled
bool     DeviceInboxStore::qspi_seg_size(uint16_t, uint32_t*) const { return false; }
bool     DeviceInboxStore::qspi_seg_append(uint16_t, const uint8_t*, uint16_t) { return false; }
uint32_t DeviceInboxStore::qspi_seg_read(uint16_t, uint8_t*, uint32_t) const { return 0; }
void     DeviceInboxStore::qspi_seg_erase(uint16_t) {}
bool     DeviceInboxStore::qspi_any_segments() const { return false; }
#else
// ---- REAL QSPI records backend: File ops on the `QSPIFlash` (CustomLFS) instance, MIRRORING load_meta/save_meta
//      on InternalFS. The store's ring/seq/cap math is platform-neutral + host-tested; these are pure file ops.
//      ★ The on-metal flash behaviour is USER BENCH-VERIFIED (the reality-split; the host can't drive QSPI). ----
bool DeviceInboxStore::qspi_mount(bool* formatted) {
    if (formatted) *formatted = false;   // begin() format-on-fail is internal; the §10.1 wipe-detect uses qspi_any_segments()
    // begin() is ONE-SHOT (CustomLFS_QSPIFlash::begin rejects re-entry with `_qspi_initialized`), but BOTH stores
    // (DM + channel) call qspi_mount -> guard so begin() runs exactly once; the 2nd store reuses the result.
    static bool s_begun = false, s_ok = false;
    if (!s_begun) {
        s_begun = true;
        s_ok = QSPIFlash.begin();                                 // mounts + format-on-mount-fail (CustomLFS base)
        if (s_ok) {                                              // create the inbox record dirs (LittleFS needs the parent;
            if (!QSPIFlash.exists("/dm")) QSPIFlash.mkdir("/dm");//   static fn -> hardcode the two known inbox dirs, can't use _dir)
            if (!QSPIFlash.exists("/ch")) QSPIFlash.mkdir("/ch");
        }
    }
    return s_ok;
}
bool DeviceInboxStore::qspi_seg_size(uint16_t idx, uint32_t* size) const {
    using namespace Adafruit_LittleFS_Namespace;
    char p[40]; seg_path(idx, p, sizeof p);
    File f(QSPIFlash); if (!f.open(p, FILE_O_READ)) return false;   // absent (not an error)
    if (size) *size = static_cast<uint32_t>(f.size());
    f.close(); return true;
}
bool DeviceInboxStore::qspi_seg_append(uint16_t idx, const uint8_t* b, uint16_t n) {
    using namespace Adafruit_LittleFS_Namespace;
    char p[40]; seg_path(idx, p, sizeof p);
    // ADDENDUM 3 RE-APPLIED (2026-06-29): keep the hot do_post_ack append OFF THE HEAP. The Adafruit `File` path
    // malloc's the lfs file cache (prog_size) + the handle on EVERY append, churning the heap right next to the radio's
    // heap-`new`'d ArduinoHal -> a vtable-word corruption = the jump-to-0x0 (the WATCHPOINT/SPItransferStream crash).
    // ADDENDUM 4's STACK-OVERFLOW theory was REFUTED on metal (`status` reported stackhw=1540 B free on the current
    // build, AND the static-buffer do_post_ack fix was flashed, yet it STILL crashed at 32m50s) -> so the heap-churn
    // hypothesis is re-promoted, and the canary's "do_post_ack/timer-9" finding (its only device-only branch is this
    // inbox store) points right here. Raw lfs with a STATIC caller-owned cache removes the allocation entirely. The
    // static handle/cache are safe: qspi_seg_append runs only in the single loop task (record_dm/record_channel), never
    // reentrant. (SOAK to confirm: if the crash stops past the ~33-min MTBF, the inbox heap churn was the corruptor.)
    static uint8_t         s_cache[512];                            // >= prog_size (LFS_DEFAULT_BLOCK_SIZE=128, QSPI page <=256); guarded below
    static lfs_file_t      s_file;
    static lfs_file_config s_cfg = { s_cache };
    lfs_t* fs = QSPIFlash._getFS();
    if (!fs || fs->cfg->prog_size > sizeof(s_cache)) {              // fail-safe: cache too small -> the (heap) File path, never an overrun
        File f(QSPIFlash); if (!f.open(p, FILE_O_WRITE)) return false;
        f.seek(f.size()); const size_t w = f.write(b, n); f.close(); return w == n;
    }
    // Take the same FS mutex the Adafruit File path holds — so the ONLY variable vs the File path is the heap alloc
    // (clean soak test of the heap-churn hypothesis), not the locking. FS access is loop-task-only today; defensive.
    QSPIFlash._lockFS();
    bool ok = false;
    if (lfs_file_opencfg(fs, &s_file, p, LFS_O_RDWR | LFS_O_CREAT, &s_cfg) >= 0) {   // FILE_O_WRITE == RDWR|CREAT (no truncate)
        lfs_file_seek(fs, &s_file, 0, LFS_SEEK_END);               // -> APPEND at the segment end
        const lfs_ssize_t w = lfs_file_write(fs, &s_file, b, n);
        lfs_file_close(fs, &s_file);
        ok = (w == static_cast<lfs_ssize_t>(n));
    }
    QSPIFlash._unlockFS();
    return ok;
}
uint32_t DeviceInboxStore::qspi_seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const {
    using namespace Adafruit_LittleFS_Namespace;
    char p[40]; seg_path(idx, p, sizeof p);
    File f(QSPIFlash); if (!f.open(p, FILE_O_READ)) return 0;       // absent
    uint32_t sz = static_cast<uint32_t>(f.size()); if (sz > cap) sz = cap;
    const int r = f.read(out, sz);
    f.close(); return r > 0 ? static_cast<uint32_t>(r) : 0;
}
void DeviceInboxStore::qspi_seg_erase(uint16_t idx) {
    char p[40]; seg_path(idx, p, sizeof p);
    QSPIFlash.remove(p);                                            // remove = empty (the next append re-creates it)
}
bool DeviceInboxStore::qspi_any_segments() const {
    for (uint16_t i = 0; i < ring_segs(); ++i) {                    // any ring segment with bytes? (§10.1 wipe-detect)
        uint32_t sz = 0;
        if (qspi_seg_size(i, &sz) && sz > 0) return true;
    }
    return false;
}
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
