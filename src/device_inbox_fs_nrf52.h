// MeshRoute — src/device_inbox_fs_nrf52.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// [[B260]] — THE DURABLE nRF52 INBOX BACKEND, AND THE RETIREMENT OF ITS HAND-MAINTAINED TWIN.
//
// ★★★ WHAT THIS FILE REPLACES, AND WHY IT IS A DELETION AND NOT A PORT. `src/device_inbox_store.h` held a
//     SECOND, Arduino-gated copy of the segmented-log logic — its own `begin`, `append`, `read_since`, ring
//     arithmetic and §10.1 epoch detect — and said so about itself ("a HAND-MAINTAINED TWIN of
//     lib/core/segmented_inbox_store.h … any change here must be mirrored there"). It was not mirrored. Every
//     one of [[B134]]'s eight QG rounds hardened the shared copy and left the twin behind, and the register row
//     [[B260]] catalogued the FIVE resulting defect classes, all of them the same shape — a durable-write
//     result DISCARDED, so the store reported a success it had not achieved:
//       ① EVERY `save_meta()` result ignored. The ROTATION save moved the head, wrote the record and returned
//          TRUE, so an unpersisted head lost an ACKNOWLEDGED record across a reboot — and for a §3.5 TOMBSTONE
//          that means the UI reported the deletion and the message CAME BACK. Same at the eviction save, at
//          `begin()`'s upgrade/§10.1 saves, and at `begin()`'s fresh branch, which persisted NOTHING at all (a
//          reboot inside the first `kSeqPersistBatch` appends restored `next_seq = 1` over a log that already
//          held those seqs — duplicate sequences in one store).
//       ② The two `qspi_seg_erase` calls in `append()`'s roll/eviction unchecked, so a roll could write behind
//          stale lapped bytes that `read_since` then parsed as frames; and `wipe()` left head/tail/`_total`
//          stale — safe only because both callers happened to reboot immediately, which is a dependency on the
//          CALLER, not a property of the store.
//       ③ `begin()`'s `!had_meta` branch re-initialised over EXISTING QSPI records without ever looking (the
//          [[B134]] QG round-2 blocker verbatim): head/tail reset to 0/0 hid every other segment while it was
//          physically present, and `next_seq = 1` reused sequences the companion had already filed.
//       ④ The §10.1 arm carried the EVERY-BOOT epoch ratchet (`records_empty && next_seq > 1`), so after a
//          `prep-restart` an nRF52 node announced a fresh wipe on every single power cycle and the app re-pulled
//          an unchanged empty inbox for ever.
//       ⑤ `set_read_cursor` assigned RAM BEFORE saving and then coalesced on the RAM value, so the retry that
//          should have repaired the medium was the one the wear optimisation ate; `set_next_seq` had the same
//          RAM-first shape and over-reported `persisted_next_seq()`.
//     ⇒ ALL FIVE ARE FIXED BY DELETION, not by a sixth hand-port. `lib/core/segmented_inbox_store.h` — reused
//       UNCHANGED — already carries every one of those fixes, host-tested, with a mutation battery behind them.
//       The twin's existence WAS the root cause (the duplicated-code rule), and the only durable repair is that
//       there is no longer a second copy to drift.
//
// ★★ THIS FILE IS A **SEAM**, NOT A STORE — the sibling of `device_inbox_fs_esp32.h`. The ring, the framing, the
//    drop-oldest eviction, the reboot restore, the §10.1 wipe-detect and the §B135 seal-and-roll all live in
//    `lib/core/segmented_inbox_store.h`; the platform-neutral seam decisions (D1..D6, `MountOnce`,
//    `SegmentStoreOver`) live in `device_inbox_seam.h`. What is here is ONLY what is true of nRF52:
//      · RECORDS -> `ISegmentStore` = LittleFS files `<dir>/<i>` on the EXTERNAL 2 MB QSPI chip (CustomLFS)
//      · META    -> `IMetaStore`    = ONE file per store on the ON-CHIP InternalFS (`/mri_dm`, `/mri_ch`)
//    ⇒ the §10.1 records/meta split is preserved on nRF52's own axis: two different FLASH CHIPS, so a QSPI
//    records wipe cannot take the seq high-water or the epoch with it. (ESP32 gets the same property from two
//    partitions; the axis differs, the guarantee does not.)
// ⛔ IT DOES NOT INCLUDE `device_inbox_fs_esp32.h` AND NOTHING HERE IS COPIED FROM IT. Both platform headers
//    include the shared seam; neither includes the other.
//
// ★★★ THE MIGRATION IS DESTRUCTIVE, DEFINITELY — NOT CONDITIONALLY, AND NOT REINTERPRETED.
//     The retired twin's meta blob (`mrinbox::Meta`, magic 'MRIX', v6) is **24 bytes**. The shared store's
//     `SegmentedInboxStore::Meta` (v4) is **28 bytes** — it gained the persisted `records_state` marker that
//     retired the epoch ratchet, and the trailing `uint8_t` pads the struct to 28. A 24-byte file read into a
//     28-byte destination is a WRONG-LENGTH read, which N1 below classifies as `MetaLoad::error`, which
//     `SegmentedInboxStore::begin()` turns into `SegMountFault::meta_corrupt` and a REFUSED MOUNT.
//     ⇒ THE FIRST BOOT OF THIS BUILD ON A NODE THAT ALREADY HAD AN INBOX REFUSES TO MOUNT IT, on purpose.
//     ⛔ NO AUTOMATIC REINTERPRETATION of the old bytes. Guessing that a short blob is "probably the previous
//        layout" is exactly the guess-at-bytes [[B134]] spent five rounds removing, and it cannot be made safe
//        here: the old blob has no `records_state`, so any value invented for it either fakes an acknowledged
//        wipe (suppressing a real §10.1 bump) or fakes a pending append (announcing one that never happened).
//     ⛔⛔ AND THE RECOVERY IS NOT INBOX-ONLY. The operator's way out is `factory_reset confirm`, whose nRF52 arm
//        is `mrnv::factory_erase()` -> `InternalFS.format()` — a FULL format of the on-chip filesystem. That
//        takes **CONFIG (`/mrcfg`), IDENTITY (`/mrid`), PEERS (`/mrpeers`), TEAM STATE (`/mrteams`), the join
//        profiles (`/mrjoin`), the UI presets (`/mrui`) AND the inbox meta** — the node comes back with a fresh
//        identity and an unconfigured radio and must be re-joined. (`/mrfault` is deliberately preserved.) The
//        inbox RECORDS on the QSPI chip are taken separately, by the command's own `InboxStore::wipe()`.
//     ⇒ accepted ONCE under M3 (MeshRoute is UNSHIPPED — owner test hardware only, so the cost is one
//       re-provisioning on the flash that first carries this build). It is stated here, in the report, in the
//       drafted register text and in the drafted bench step — never left for an operator to discover.
//
// REALITY SPLIT (like `device_nv.h` and `device_inbox_fs_esp32.h`): the DECISIONS are hoisted ABOVE the platform
// `#if` as templates over a duck-typed seam, so `test/test_device_inbox_fs_nrf52.cpp` drives them on the host.
// The arm below is then plumbing with no branch of its own. What stays BENCH-ONLY is the metal: the QSPI
// bring-up, real flash wear, a real power cut mid-write and a real CustomLFS format.
#pragma once
#include "inbox.h"                    // meshroute::InboxStore
#include "segmented_inbox_store.h"    // meshroute::ISegmentStore / IMetaStore / SegmentedInboxStore / MetaLoad
#include "device_inbox_seam.h"        // mrinboxfs::D1..D6, MountOnce, SegmentStoreOver, kDirDm/kDirCh
#include "device_nv.h"                // ★ mrnv::fs_read_slot / SlotIo / kSlotAbsent — the [[B218]]-hardened read
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mrinboxnrf {

// ---- the ONE place the nRF52-ONLY on-flash names live ------------------------------------------------------
// ⓘ The record dirs (`kDirDm`/`kDirCh`) are in `device_inbox_seam.h` — shared with ESP32. What is left here is
//   what only an nRF52 target has: the two InternalFS META paths. ⛔ THEY ARE THE TWIN'S PATHS, UNCHANGED, and
//   that is deliberate: `mrnv::factory_erase()`'s comment already names `/mri_dm`,`/mri_ch` as files its format
//   takes, and renaming them would have orphaned the old blobs on every test node instead of failing loud on
//   them (see the migration note above).
inline constexpr const char* kMetaPathDm = "/mri_dm";
inline constexpr const char* kMetaPathCh = "/mri_ch";

// ============================================================================================================
// THE nRF52-ONLY DECISIONS — hoisted above the platform `#if` (device_nv.h's idiom), so the native suite can
// drive them. ⓘ D1..D6, `MountOnce` and `SegmentStoreOver` are in `device_inbox_seam.h`; only what an
// `lfs_*` / InternalFS backend decides for itself is here.
// ============================================================================================================

// ---- N1: the InternalFS META LOAD's verdict — the ONE place a slot read becomes a MetaLoad -----------------
// ★★ THREE-VALUED, and the third state is the whole point ([[B134]] QG round 3, now binding on nRF52 too).
//    `absent` means the meta file was NEVER WRITTEN — a true first boot, which must mount fresh. `error` means
//    the store could not be read, or read back wrong — a fact about the MEDIUM, and ⛔ never an invitation to
//    start over, because the sequence high-water and the §10.1 epoch live nowhere else. Collapsing them is what
//    routes a live history onto the silent re-initialise path.
// ★ IT CLASSIFIES `mrnv::fs_read_slot`'s OUTPUT RATHER THAN RE-DERIVING ONE (U1). That sequence is already the
//   [[B218]]-hardened one: it checks the MOUNT, it takes the RAW `lfs_stat` rc so that only `LFS_ERR_NOENT` is
//   an absence (Adafruit's `File::open()` answers ONE `false` for four different facts — a metadata error, a
//   genuine absence, an allocation failure and an `lfs_file_open` failure), and it reports an OVER-LENGTH file.
//   A second copy of that dance here is exactly the fork U1 forbids, and is how the four facts get conflated
//   again.
// THE MAPPING, and every arm is a fact `fs_read_slot` actually produces:
//    io.backend_failed          -> error   (mount refused / stat metadata error / open failed on a file that
//                                           EXISTS — the store would not answer, so nothing is known)
//    n == kSlotAbsent           -> absent  ⓘ the ONLY absence, and reachable here only when the backend did NOT
//                                           fail, because that arm is tested FIRST
//    io.oversize                -> error   (the file is LONGER than `Meta`; the read would take a valid PREFIX)
//    n == want                  -> loaded
//    anything else              -> error   (a SHORT file — which is exactly the retired twin's 24-byte v6 blob
//                                           under the shared 28-byte v4 `Meta` — or a negative rc from a
//                                           corrupt CTZ block. Both are corruption, neither is a fresh device.)
inline meshroute::MetaLoad classify_meta_read(int n, const mrnv::SlotIo& io, uint16_t want) {
    if (io.backend_failed) return meshroute::MetaLoad::error;    // ⛔ FIRST: a refusal is never an absence
    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;
    if (io.oversize) return meshroute::MetaLoad::error;          // a valid PREFIX is not a valid record
    return mrinboxfs::meta_len_ok(n, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;
}

// ---- N2: the META SAVE's verdict — COMMITTED-AND-MEASURED, the D3 discipline on a whole-blob write ---------
// ★★ THE META SAVE IS THE MOST LOAD-BEARING WRITE IN THE STORE, because every one of [[B260]]'s ① defects is a
//    `save_meta()` whose result did not reach a decision. The shared store now CHECKS that result everywhere —
//    but a checked call over a seam that cannot fail is still a seam that can only report success, so the
//    verdict has to be real here.
// ⛔⛔ AND THE ARDUINO WRAPPER CANNOT GIVE IT. `Adafruit_LittleFS_Namespace::File::flush()` returns **void** and
//    `File::close()` returns **void** (`Adafruit_LittleFS_File.h:69,76`), so both discard `lfs_file_sync`'s and
//    `lfs_file_close`'s error codes — a COMPLETE write whose commit failed is indistinguishable from success
//    through that API. It is the same defect [[B134]] QG blocker 2 found behind ESP32's `fs::File`, in a
//    different vendor's wrapper, and it is why `mrnv::write_slot`'s `f.write(...); f.close(); return n == len;`
//    is NOT reused for this record: that shape is right for a config blob that falls back to defaults, and
//    wrong for the one write a durable inbox's honesty rests on. ⇒ the adapter drives raw `lfs_*`, where every
//    call returns a result.
// THE FOUR TERMS, and each one is a fault the others pass:
//    1. `w == n`          — a short write.
//    2. `sync()` SUCCEEDED — the commit. The term the wrapper throws away.
//    3. the file IS `n` bytes — the medium-side proof, re-stat AFTER the sync. ⛔ It is `== n` and not
//       `>= n` because this is a TRUNCATING write: a longer file means the truncate did not take, and the next
//       load would read a valid PREFIX of a stale blob (N1's `oversize` arm, one boot later).
//    4. `close()` SUCCEEDED — the close still flushes and can still fail on what the sync did not cover.
// ⛔ `close()` IS CALLED ON EVERY PATH PAST THE OPEN — the verdict is computed from saved terms afterwards,
//    never by returning early, or a failed sync would leak the one static handle for the rest of the boot.
// FsT duck-type: `bool open_trunc(const char*)`, `size_t write(const void*, uint16_t)`, `bool sync()`,
//                `uint32_t size()`, `bool close()`.
template <class FsT>
inline bool write_whole(FsT& fs, const char* path, const void* b, uint16_t n) {
    if (!fs.open_trunc(path)) return false;
    const size_t   w      = fs.write(b, n);
    const bool     synced = fs.sync();
    const uint32_t after  = fs.size();     // re-stat: the COMMITTED length, not the in-RAM one
    const bool     closed = fs.close();    // always — see the note above
    return w == static_cast<size_t>(n) && synced && after == n && closed;
}

// ---- THE META STORE — an `IMetaStore` composed from N1 + N2, TEMPLATED over its two IO seams ---------------
// ★★ IT IS A TEMPLATE FOR THE REASON `mrinboxfs::SegmentStoreOver` IS: so the native suite drives **THIS** class,
//    the one the device ships, rather than a host lookalike that agrees with it only while somebody keeps them
//    agreeing. That is the S1/L9 field-drop shape one level up — and it is the exact trap the RETIRED
//    `src/device_inbox_store.h` recorded about itself before [[B260]] deleted it. A two-line `load()` inside the
//    platform `#if` would have been a two-line decision no gate could reach.
// ⓘ TWO seam parameters because the read and the write genuinely use different vendor entry points: the READ is
//   `mrnv::fs_read_slot`'s adapter (`InternalFsSlot` — reused, not re-derived, U1), the WRITE is a raw-`lfs_*`
//   commit the Adafruit `File` wrapper cannot express (see N2). Neither carries a decision of its own.
// SlotT duck-type:    mrnv::fs_read_slot's (kFoundRc/kAbsentRc/mount/lookup/open/size/read/close)
// WriteIoT duck-type: N2's (open_trunc/write/sync/size/close)
template <class SlotT, class WriteIoT>
class MetaStoreOver : public meshroute::IMetaStore {
public:
    explicit MetaStoreOver(const char* path) : _path(path) {}

    // ⛔ THE SEQUENCE IS `fs_read_slot`'s AND THE CLASSIFICATION IS N1's — this body only runs the two together,
    //    so it holds no branch a battery cannot attack.
    meshroute::MetaLoad load(void* blob, uint16_t len) override {
        SlotT fs;
        mrnv::SlotIo io;
        const int n = mrnv::fs_read_slot(fs, _path, blob, len, &io);
        return classify_meta_read(n, io, len);
    }
    bool save(const void* blob, uint16_t len) override {
        WriteIoT io;
        return write_whole(io, _path, blob, len);
    }

private:
    const char* _path;
};

}  // namespace mrinboxnrf

// ============================================================================================================
// THE nRF52 ARM — plumbing only. Every branch above this line is host-driven; nothing new is decided here.
// ============================================================================================================
#if defined(ARDUINO) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>   // oltaco/CustomLFS — the `QSPIFlash` global (nrfx_qspi + LittleFS, P25Q16H)

// ⛔ DEFINED ONLY WHEN THE ARM ACTUALLY COMPILED — the wiring in fw_context.h / fw_main.cpp keys the
//    durable-vs-RAM choice on this, exactly as it keys the ESP32 choice on MRINBOX_ESP32_LITTLEFS. An nRF52
//    target built without QSPIFLASH must fall back to the visible volatile ring, not to a store whose backends
//    do not exist. ⓘ THE NAME IS UNCHANGED FROM THE RETIRED TWIN ON PURPOSE: `platformio.ini` documents
//    `QSPIFLASH` as gating "the inbox records FS + MRINBOX_QSPI_READY", and only the parenthesised FILE name in
//    that comment went stale with [[B260]].
#define MRINBOX_QSPI_READY 1

// ★ N1's absence sentinel is pinned to the backend's OWN constant here, so a vendor bump that moved
//   `LFS_ERR_NOENT` would fail the BUILD rather than silently reclassify a corrupt store as a fresh one. (The
//   D7 `static_assert` precedent, applied to the adapter this file actually ships.)
static_assert(mrnv::InternalFsSlot::kAbsentRc == LFS_ERR_NOENT, "N1: the slot read's absence rc must be LFS_ERR_NOENT");
static_assert(mrnv::InternalFsSlot::kFoundRc  == LFS_ERR_OK,    "N1: the slot read's found rc must be LFS_ERR_OK");

namespace mrinboxnrf {

// ---- the FS lock, as RAII ------------------------------------------------------------------------------------
// ⛔ PER-CALL, NEVER HELD ACROSS AN OPEN. `Adafruit_LittleFS::_mutex` is `xSemaphoreCreateMutexStatic`
//    (`Adafruit_LittleFS.cpp:49`) — a PLAIN FreeRTOS mutex, ⛔ not a recursive one — and the seam's protocol
//    spans several calls (open, write, sync, size, close), so holding it from open to close would deadlock the
//    moment any path took the lock twice and would wedge the node for ever if a path ever failed to close.
//    Locking each raw call mirrors what the retired twin's `qspi_seg_append` already did, and FS access is
//    loop-task-only today; the lock is defensive.
template <class FsT>
struct FsLock {
    explicit FsLock(FsT& fs) : _fs(&fs) { _fs->_lockFS(); }
    ~FsLock() { _fs->_unlockFS(); }
    FsLock(const FsLock&) = delete;
    FsLock& operator=(const FsLock&) = delete;
private:
    FsT* _fs;
};

// ============================================================================================================
// RECORDS — the QSPI IO seam that `mrinboxfs::SegmentStoreOver` is templated over.
// ============================================================================================================

// ★★ RAW `lfs_*`, NOT `Adafruit_LittleFS_Namespace::File`, FOR **TWO** INDEPENDENT REASONS, both of them
//    load-bearing and both of them already paid for in this project:
//   (1) THE RESULTS. `File::flush()` and `File::close()` both return **void**, so the two calls that decide
//       whether a record is durably on the medium cannot be checked through that API at all — [[B134]] QG
//       blocker 2, arriving through a different vendor's wrapper (see N2).
//   (2) THE HEAP (ADDENDUM 3, 2026-06-29, RE-APPLIED HERE VERBATIM IN INTENT). The Adafruit `File` path
//       `rtos_malloc`s the `lfs_file_t` AND the lfs file cache on EVERY append
//       (`Adafruit_LittleFS_File.cpp:61,87`), churning the heap right next to the radio's heap-`new`'d
//       ArduinoHal — a vtable-word corruption that was the jump-to-0x0 crash (the WATCHPOINT/SPItransferStream
//       fault). The static caller-owned handle + cache remove the allocation entirely. ⛔ Do not "simplify" this
//       back to `File`; it is a fix, not a style.
// ⓘ ONE static handle, and a BUSY LATCH so a second concurrent open FAILS LOUD instead of writing through
//   another operation's handle. `SegmentStoreOver` constructs a fresh `QspiIo` per call and every seam path
//   (D3 `append_at_end`, D4 `read_whole`, `seg_size`) closes before the next one opens, so the latch is a
//   tripwire on a property the seam already has — not a lock, and never a wait.
struct QspiIo {
    // >= the QSPI `prog_size`, which `CustomLFS_QSPIFlash::_configure_lfs()` sets to 256 (the P25Q16H page).
    // `lfs_file_config::buffer` "must be program sized" (lfs.h:172). The guard below is what keeps that a fact
    // rather than a comment: a chip whose page did not fit would REFUSE to open, never overrun.
    static constexpr uint32_t kCacheBytes = 512;

    ~QspiIo() { close(); }   // ⛔ the latch must never survive a path that forgot to close

    bool mount(bool /*format_on_fail*/) {
        // ★★ D2's TWO-ATTEMPT PROTOCOL COLLAPSES TO ONE ATTEMPT HERE, AND THE REASON IS THE VENDOR, NOT A
        //    SHORTCUT. `CustomLFS_QSPIFlash::begin()` mounts and, if that fails, formats and remounts INSIDE
        //    ITSELF (`CustomLFS_QSPIFlash.cpp:368-384`), returning ONE bool for both outcomes; it is also
        //    one-shot (`if (_qspi_initialized) return false`). So this adapter cannot answer "did you have to
        //    format?" and always reports `formatted = false` through `mount_or_format`.
        // ⛔⛔ AND THAT IS SOUND, WHICH IS A PROOF AND NOT AN ASSUMPTION — `formatted` has exactly two consumers
        //     in `SegmentedInboxStore::begin()`:
        //       · `records_empty = formatted || !have_records`. A CustomLFS format leaves NO records, so
        //         `any_segments()` answers false and `records_empty` is true either way. Identical outcome.
        //       · `if (have_records && !formatted) -> meta_lost_over_records`. This arm is reached only when
        //         records ARE present, and a format that just erased them cannot have left any. Unreachable.
        //     ⇒ under-reporting `formatted` here changes NO decision the store makes. Over-reporting it would:
        //       it would bump the epoch on an ordinary first boot. The conservative direction is the correct one.
        // ⓘ It also means a CustomLFS internal format is not directly OBSERVABLE — the §10.1 external-loss
        //   detect still catches it, one step later and by the right authority (the medium came up empty while
        //   the persisted `records_state` marker says `non_empty`), which is the arm that mechanism exists for.
        return QSPIFlash.begin();
    }
    // An EXISTING directory is success — `Adafruit_LittleFS::mkdir` accepts `LFS_ERR_EXIST` for every path
    // component (`Adafruit_LittleFS.cpp:164-181`), which is the state we asked for.
    // ⓘ The retired twin created these dirs inside its mount with the result DISCARDED, and only for the two
    //   hard-coded names its static seam could see. Here a failed create FAILS THE MOUNT (`SegmentStoreOver`),
    //   so a store that could never append says so at boot instead of failing every record silently.
    bool ensure_dir(const char* d) { return QSPIFlash.mkdir(d); }

    bool open_append(const char* p) { return open_raw(p, LFS_O_RDWR | LFS_O_CREAT, /*seek_end=*/true); }
    bool open_read(const char* p)   { return open_raw(p, LFS_O_RDONLY, /*seek_end=*/false); }

    // ★ THE SIZE IS TAKEN FROM THE **DIRECTORY**, NOT FROM THE OPEN HANDLE, and that is what makes D3's growth
    //   term a statement about the medium. `lfs_file_size()` reports lfs's in-RAM view, which already includes
    //   bytes that have only been written and not yet synced — it would pass on exactly the fault term 3 exists
    //   to catch. `lfs_stat` reads the committed directory entry, so before a sync it answers the OLD length and
    //   after a successful sync it answers the new one. (The ESP32 seam gets the same property from `fstat`
    //   after `fsync`.)
    uint32_t size() {
        if (!s_path[0]) return 0;
        struct lfs_info info;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        if (lfs_stat(QSPIFlash._getFS(), s_path, &info) != LFS_ERR_OK) return 0;
        return info.type == LFS_TYPE_REG ? static_cast<uint32_t>(info.size) : 0;
    }
    size_t write(const uint8_t* b, uint16_t n) {
        if (!s_busy) return 0;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        const lfs_ssize_t w = lfs_file_write(QSPIFlash._getFS(), &s_file, b, n);
        return w > 0 ? static_cast<size_t>(w) : 0;
    }
    // ★ THE FALLIBLE COMMIT — the term the Adafruit wrapper's void `flush()` throws away.
    bool sync() {
        if (!s_busy) return false;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        return lfs_file_sync(QSPIFlash._getFS(), &s_file) == LFS_ERR_OK;
    }
    int read(uint8_t* o, uint32_t n) {
        if (!s_busy) return 0;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        return static_cast<int>(lfs_file_read(QSPIFlash._getFS(), &s_file, o, n));
    }
    // ⛔ AND ITS RESULT IS RETURNED — `File::close()` is void, and a close that fails on the final flush is the
    //    same tear one call later. Idempotent: closing an unopened handle is success.
    bool close() {
        if (!s_busy) return true;
        bool ok;
        {
            FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
            ok = (lfs_file_close(QSPIFlash._getFS(), &s_file) == LFS_ERR_OK);
        }
        s_busy = false; s_path[0] = '\0';
        return ok;
    }
    // "empty afterwards" is the contract (`ISegmentStore::seg_erase`), so an ALREADY-ABSENT segment is SUCCESS.
    // ⛔ THE RAW rc IS WHAT MAKES THAT EXACT. `Adafruit_LittleFS::remove()` collapses `lfs_remove` to
    //    `LFS_ERR_OK == err`, so it answers plain `false` for "already gone" AND for a real removal failure —
    //    the retired twin settled that with a follow-up `exists()`, which reintroduces the same conflation
    //    (`exists()` is `lfs_stat == 0`, so a stat ERROR reads as "gone"). Here NOENT is the only absence.
    bool remove(const char* p) {
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        const int rc = lfs_remove(QSPIFlash._getFS(), p);
        return rc == LFS_ERR_OK || rc == LFS_ERR_NOENT;
    }
    // The §10.1 wipe-detector's one pass, supplied to D6's `inspect_any_nonempty` — ⛔ the DECISIONS are there,
    // host-reachable; this is only the raw-lfs plumbing that feeds them.
    bool any_under(const char* dir, bool* ok);

private:
    bool open_raw(const char* p, int flags, bool seek_end) {
        if (s_busy) return false;                       // ⛔ fail loud rather than write through another handle
        lfs_t* fs = QSPIFlash._getFS();
        if (!fs || !fs->cfg || fs->cfg->prog_size > kCacheBytes) return false;   // the "program sized" guard
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        if (lfs_file_opencfg(fs, &s_file, p, flags, &s_cfg) != LFS_ERR_OK) return false;   // absent / error
        // ⛔ THE SEEK RESULT IS CHECKED. An append that silently started at offset 0 would OVERWRITE the segment
        //    from its front and `read_since` would then parse the old tail as frames — the §B135 mis-parse from
        //    a direction seal-and-roll does not cover. The retired twin issued this seek and discarded its rc.
        if (seek_end && lfs_file_seek(fs, &s_file, 0, LFS_SEEK_END) < 0) {
            lfs_file_close(fs, &s_file);
            return false;
        }
        size_t i = 0;
        for (const char* s = p; *s && i + 1 < sizeof s_path; ++s) s_path[i++] = *s;
        s_path[i] = '\0';
        s_busy = true;
        return true;
    }
    // ⓘ STATIC, in .bss: off the heap (reason 2 above) AND off the stack — `begin()` runs inside `setup()` and
    //   `append` runs in `do_post_ack`, the frame that already overflowed the fixed 4 KB nRF52 loop-task stack
    //   once. Single-threaded, one open at a time (the busy latch below is the tripwire).
    // ⛔ THE PATH IS STATIC TOO, DELIBERATELY: it names the file THE STATIC HANDLE has open, so a per-instance
    //    copy would let a second `QspiIo` answer `size()` about a file it is not the one holding.
    inline static char            s_path[40]{};
    inline static lfs_file_t      s_file{};
    inline static uint8_t         s_cache[kCacheBytes]{};
    inline static lfs_file_config s_cfg{ s_cache };
    inline static bool            s_busy = false;
};

// ---- the raw-lfs directory walk D6 drives -------------------------------------------------------------------
// ★★ `lfs_dir_read` REPORTS ITS ERROR IN ITS RETURN VALUE (`> 0` = an entry, `0` = a clean end, `< 0` = an
//    error — lfs.h:441-445), which is exactly the three-way answer D6 needs and the reason no errno dance is
//    involved. ⓘ It also fills `struct lfs_info` with BOTH the type and the size, so — unlike the ESP32 walk,
//    which must `stat` each entry separately — there is no second call that could fail unnoticed.
// ⛔ A DIRECTORY ENTRY IS NOT A RECORD. lfs v1 yields "." and ".." from every directory; both are
//    `LFS_TYPE_DIR`, and reporting size 0 for anything that is not a regular file is what keeps them from
//    counting as records (the same rule the ESP32 walk applies via `S_ISREG`).
// ⓘ The `lfs_info` is `static`: it carries a 256-byte `name[]` and this runs from `begin()` inside `setup()`,
//   so it stays off the stack. Single-threaded, one walk at a time.
struct QspiDirWalk {
    explicit QspiDirWalk(const char* dir) : _dir(dir) {}
    bool open(bool* absent) {
        if (absent) *absent = false;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        const int rc = lfs_dir_open(QSPIFlash._getFS(), &_d, _dir);
        if (rc == LFS_ERR_OK) { _open = true; return true; }
        // ⛔ ONLY NOENT IS A REAL ABSENCE. Every other rc is the store refusing to answer, and mapping that onto
        //    "no records" is what hands `begin()` to the silent re-initialise path over a live history.
        if (absent) *absent = (rc == LFS_ERR_NOENT);
        return false;
    }
    bool next(bool* have, uint32_t* size) {
        *have = false; *size = 0;
        static struct lfs_info info;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        const int rc = lfs_dir_read(QSPIFlash._getFS(), &_d, &info);
        if (rc < 0) return false;                       // ★ the error is REPORTED, never inferred
        if (rc == 0) return true;                       // clean end of directory
        *have = true;
        *size = info.type == LFS_TYPE_REG ? static_cast<uint32_t>(info.size) : 0;   // "."/".." are not records
        return true;
    }
    void close() {
        if (!_open) return;
        FsLock<CustomLFS_QSPIFlash> lk(QSPIFlash);
        lfs_dir_close(QSPIFlash._getFS(), &_d);
        _open = false;
    }
private:
    const char* _dir;
    lfs_dir_t   _d{};
    bool        _open = false;
};

inline bool QspiIo::any_under(const char* dir, bool* ok) {
    QspiDirWalk w(dir);
    return mrinboxfs::inspect_any_nonempty(w, ok);
}

// ---- RECORDS: the host-tested `SegmentStoreOver` template, typed to the QSPI IO ----------------------------
// ⛔ NOT a second class, and ⛔ not a second ring: this is the SAME class the native suite drives and the SAME
//    class ESP32 ships, with `QspiIo` substituted for the fake / for `LfsIo`. That is the whole of [[B260]].
using Nrf52SegmentStore = mrinboxfs::SegmentStoreOver<QspiIo>;

// ============================================================================================================
// META — the host-tested `MetaStoreOver` template, typed to InternalFS.
// ============================================================================================================
// A DIFFERENT FLASH CHIP from the records (on-chip nRF52 flash vs the external P25Q16H) — which is what makes
// §10.1 work here: a QSPI format cannot take the seq high-water or the epoch with it, so `next_seq` never
// reuses a sequence and the companion learns the history was wiped instead of silently re-reading a hole.

// The InternalFS WRITE seam N2 is templated over. Raw `lfs_*` for the SAME two reasons `QspiIo` is: the Adafruit
// wrapper's `flush()`/`close()` are void (so the commit result is unobtainable), and its `File` path
// `rtos_malloc`s the handle + cache on every call. ⛔ `mrnv::write_slot` is deliberately NOT reused for this
// record even though it writes the same filesystem: its `f.write(...); f.close(); return n == len;` is right for
// a config blob that falls back to defaults, and wrong for the one write a durable inbox's honesty rests on.
struct InternalFsWriteIo {
    // >= InternalFS's `prog_size`, which is `LFS_BLOCK_SIZE` = 128 (`InternalFileSystem.cpp:109-110`).
    // `lfs_file_config::buffer` "must be program sized" (lfs.h:172); the guard below keeps that a fact.
    static constexpr uint32_t kCacheBytes = 256;

    ~InternalFsWriteIo() { close(); }   // ⛔ the latch must never survive a path that forgot to close

    // ⛔ `LFS_O_TRUNC`, NOT a `remove()`-then-create (which is what `mrnv::write_slot` does). The meta is a
    //    fixed-size blob that is OVERWRITTEN, and a remove+create leaves a window in which the record does not
    //    exist at all — a power cut there loses the epoch AND the seq high-water outright, which is the one
    //    thing this store exists to keep. Truncate-on-open never deletes the entry.
    bool open_trunc(const char* p) {
        if (_open) return false;
        if (!InternalFS.begin()) return false;      // idempotent: true when already mounted
        lfs_t* fs = InternalFS._getFS();
        if (!fs || !fs->cfg || fs->cfg->prog_size > kCacheBytes) return false;
        FsLock<InternalFileSystem> lk(InternalFS);
        if (lfs_file_opencfg(fs, &_f, p, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &_cfg) != LFS_ERR_OK) return false;
        size_t i = 0;
        for (const char* s = p; *s && i + 1 < sizeof _path; ++s) _path[i++] = *s;
        _path[i] = '\0';
        _open = true;
        return true;
    }
    size_t write(const void* b, uint16_t n) {
        if (!_open) return 0;
        FsLock<InternalFileSystem> lk(InternalFS);
        const lfs_ssize_t w = lfs_file_write(InternalFS._getFS(), &_f, b, n);
        return w > 0 ? static_cast<size_t>(w) : 0;
    }
    // ★ THE FALLIBLE COMMIT — the term `File::flush()`'s void return throws away.
    bool sync() {
        if (!_open) return false;
        FsLock<InternalFileSystem> lk(InternalFS);
        return lfs_file_sync(InternalFS._getFS(), &_f) == LFS_ERR_OK;
    }
    // The committed DIRECTORY entry, exactly as `QspiIo::size()` — never the open handle's in-RAM view.
    uint32_t size() {
        if (!_path[0]) return 0;
        struct lfs_info info;
        FsLock<InternalFileSystem> lk(InternalFS);
        if (lfs_stat(InternalFS._getFS(), _path, &info) != LFS_ERR_OK) return 0;
        return info.type == LFS_TYPE_REG ? static_cast<uint32_t>(info.size) : 0;
    }
    // ⛔ AND ITS RESULT IS RETURNED — `File::close()` is void, and a close that fails on the final flush is the
    //    same lost write one call later. Idempotent: closing an unopened handle is success.
    bool close() {
        if (!_open) return true;
        bool ok;
        {
            FsLock<InternalFileSystem> lk(InternalFS);
            ok = (lfs_file_close(InternalFS._getFS(), &_f) == LFS_ERR_OK);
        }
        _open = false; _path[0] = '\0';
        return ok;
    }
private:
    // ALL STATIC, for the same .bss reason `QspiIo`'s are: `save_meta()` runs inside `append`, i.e. inside
    // `do_post_ack`, the frame that already overflowed the fixed 4 KB nRF52 loop-task stack once. Only one meta
    // write is ever in flight (single-threaded), and `_open` is the tripwire — which it can only BE if it
    // describes the same static handle every instance would use.
    inline static char            _path[24]{};
    inline static bool            _open = false;
    inline static lfs_file_t      _f{};
    inline static uint8_t         _cache[kCacheBytes]{};
    inline static lfs_file_config _cfg{ _cache };
};

// ⛔ NOT a second class: this is the SAME `MetaStoreOver` the native suite drives, with the two real adapters
//    substituted for its fakes.
using InternalFsMetaStore = MetaStoreOver<mrnv::InternalFsSlot, InternalFsWriteIo>;

}  // namespace mrinboxnrf
  #endif  // QSPIFLASH
#endif  // ARDUINO && nRF52
