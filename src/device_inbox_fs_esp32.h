// MeshRoute — src/device_inbox_fs_esp32.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// [[B134]] — THE DURABLE ESP32/Heltec INBOX BACKEND. Until this file, every ESP32 target ran the VOLATILE
// `meshroute::FixedInboxStore` RAM ring: a reboot destroyed the records, their §3.5 tombstones and the whole
// history alike (nothing "returned" because nothing survived — the 2026-08-13 sharpening of the register row).
//
// ★★ THIS FILE IS A **SEAM**, NOT A STORE. The ring, the framing, the drop-oldest eviction, the reboot restore,
//    the §10.1 wipe-detect and the §B135 seal-and-roll all stay in `lib/core/segmented_inbox_store.h`, which is
//    host-tested (test/test_segmented_inbox_store.cpp) and is the SAME logic the nRF52 store discharges. What is
//    here is only the two INJECTED interfaces that logic already takes (U1):
//      · RECORDS -> `ISegmentStore`  = LittleFS files `<dir>/<i>` on the ESP32's own `spiffs` data partition
//      · META    -> `IMetaStore`     = ONE NVS blob per store, in the `"mr"` Preferences namespace
//    ⇒ the §10.1 split is preserved on a DIFFERENT axis than the nRF52's (there: on-chip InternalFS vs an
//    external QSPI chip; here: the NVS partition vs the LittleFS partition) — two partitions, two erase
//    domains, so a records format cannot take the seq high-water or the epoch with it.
//
// ⛔ WHY NOT `mrinbox::DeviceInboxStore` (the nRF52 file): that class is a hand-maintained TWIN of the lib/core
//    logic with the flash primitives as private static/member seams. Wiring ESP32 into it would have added a
//    THIRD copy of the same ring under a second `#if` arm. The injected-interface store exists precisely so a
//    new platform is two small adapters — so this is the one that gets reused.
//
// ⛔ NO NEW LIBRARY AND NO `platformio.ini` CHANGE, AND THAT IS MEASURED, NOT ASSUMED:
//    · `LittleFS` ships INSIDE the pinned arduino-esp32 3.1.3 core (`libraries/LittleFS`), so it is a framework
//      built-in, resolved by the LDF from the `#include <LittleFS.h>` below — the same way `Preferences` and
//      `nvs.h` already reach `device_nv.h`.
//    · both ESP32 boards in this tree (`heltec_wifi_lora_32_V3`, `seeed_xiao_esp32s3`) select
//      `default_8MB.csv`, which carries `spiffs, data, spiffs, …, 0x180000` = a 1.5 MB data partition that
//      NOTHING else in this firmware uses. The DM + channel caps are 512 KiB + 128 KiB = 640 KiB, so the ring
//      fits with room to spare. `partitionLabel = "spiffs"` is `LittleFSFS::begin`'s own default — the
//      subtype is what is matched, not the filesystem that once lived there.
//
// REALITY SPLIT (like `device_nv.h` and `device_inbox_store.h`): the DECISIONS are hoisted ABOVE the platform
// `#if` as templates over a duck-typed seam, so `test/test_device_inbox_fs_esp32.cpp` drives every one of them
// on the host. The two arms below are then pure plumbing with no branch of their own. What remains
// BENCH-ONLY is the metal itself: real flash wear, a real power cycle, and a real `esp_littlefs` format
// (bench script Part 19.1 / Part 11.4).
#pragma once
#include "inbox.h"                    // meshroute::InboxStore
#include "segmented_inbox_store.h"    // meshroute::ISegmentStore / IMetaStore / SegmentedInboxStore
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mrinboxfs {

// ---- the ONE place the on-flash names live -----------------------------------------------------------------
// The record dirs deliberately MATCH the nRF52 store's ("/dm", "/ch") — same logical layout on a different
// medium, so a reader comparing the two platforms is not also decoding two naming schemes.
inline constexpr const char* kDirDm     = "/dm";
inline constexpr const char* kDirCh     = "/ch";
inline constexpr const char* kBasePath  = "/mri";     // the VFS mount point (must not collide with any other FS)
inline constexpr const char* kPartLabel = "spiffs";   // the data partition SUBTYPE in default_8MB.csv
// ★ The meta blobs live in the SAME `"mr"` NVS namespace as `/mrcfg`, `/mrid`, `/mrpeers`, `/mrui`. That is
//   deliberate and it is what makes `factory_reset confirm` correct WITHOUT touching `mrnv::factory_erase()`:
//   its one `Preferences::clear()` already takes the whole namespace, so the inbox META goes with everything
//   else, while the RECORDS are taken by `InboxStore::wipe()` in the command itself (firmware_commands.cpp).
inline constexpr const char* kMetaNs    = "mr";
inline constexpr const char* kMetaKeyDm = "ibm_dm";
inline constexpr const char* kMetaKeyCh = "ibm_ch";

// ============================================================================================================
// THE DECISIONS — hoisted above the platform `#if` (device_nv.h's idiom), so the native suite can drive them.
// ============================================================================================================

// ---- D1: the segment path, "<dir>/<idx>" -------------------------------------------------------------------
// Decimal, no heap, ALWAYS NUL-terminated, and never writes past `cap`. A truncated path would silently address
// a DIFFERENT segment (e.g. "/dm/12" clipped to "/dm/1"), which is a data-loss bug rather than a cosmetic one —
// so the caller sizes the buffer and this refuses to overrun it.
inline void seg_path(const char* dir, uint16_t idx, char* out, size_t cap) {
    if (!out || cap == 0) return;
    size_t p = 0;
    for (const char* d = dir; d && *d && p + 1 < cap; ++d) out[p++] = *d;
    if (p + 1 < cap) out[p++] = '/';
    char num[6]; int np = 0; uint16_t v = idx;
    do { num[np++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v && np < 6);
    while (np-- > 0 && p + 1 < cap) out[p++] = num[np];
    out[p] = '\0';
}

// ---- D2: the MOUNT POLICY — detect-on-mount, format-on-corrupt, never boot-brick ---------------------------
// ★★ TWO ATTEMPTS, AND THE ORDER IS THE WHOLE POINT (the InternalFS self-heal lesson, 2026-06-24). A single
// `begin(/*formatOnFail=*/true)` would recover a corrupt filesystem and then LIE about it: the caller could not
// tell a clean mount from one that just erased every record, so §10.1's epoch would not bump and the companion
// would keep its cursors against a history that no longer exists — the app's "a success that isn't".
// ⇒ mount WITHOUT formatting first; only if that fails, format and remount, and REPORT `formatted`.
// Fail-loud on the second failure: `mounted=false` makes `SegmentedInboxStore::begin()` return false, which
// leaves the Inbox DISABLED and visible at boot — never a silent degrade to a volatile ring.
// FsT duck-type: `bool mount(bool format_on_fail)`.
struct MountOutcome { bool mounted = false; bool formatted = false; };
template <class FsT>
inline MountOutcome mount_or_format(FsT& fs) {
    MountOutcome r;
    if (fs.mount(/*format_on_fail=*/false)) { r.mounted = true; return r; }   // clean mount -> nothing was erased
    r.mounted   = fs.mount(/*format_on_fail=*/true);                          // corrupt/blank -> format + remount
    r.formatted = r.mounted;                                                  // ONLY a successful reformat wiped records
    return r;
}

// ---- D3: the APPEND verdict — COMMITTED-AND-MEASURED, not merely "the call returned n" ----------------------
// ★★ THIS IS THE §B135 TORN-WRITE GUARD SEEN FROM THE SEAM, and it needs THREE terms, not one.
// `SegmentedInboxStore::append` issues TWO calls per record (the 6-B frame header, then the body) and seals +
// rolls the head segment the moment either reports false. A seam that over-reports success defeats that
// completely: the tear goes unsealed, the next append lands behind the torn header, the reader consumes it AS
// that header's body, and every record after the tear is physically present and unreachable.
//   1. `w == n` — a SHORT write is a tear. Never `w > 0`.
//   2. `sync()` SUCCEEDED — ⛔⛔ AND THIS IS THE TERM `fs::File` CANNOT GIVE YOU, WHICH IS WHY THIS ADAPTER DOES
//      NOT USE IT. `fs::File::write` is `fwrite` into a 4 KiB stdio buffer (`vfs_api.cpp:283`), `File::flush()`
//      calls `fflush()` and `fsync()` and **DISCARDS BOTH RESULTS** (`vfs_api.cpp:411`), and `File::close()` is
//      `fclose` returning **void**. ⇒ through the Arduino API a COMPLETE write with a FAILED SYNC is
//      indistinguishable from success, and terms 1 and 3 would BOTH pass on it. The seam therefore drives the
//      POSIX layer the VFS is built on (`fopen`/`fwrite`/`fflush`/`fsync`/`fclose`), where every one of those
//      returns a result and every one is checked. Sync adds no wear — it is the same commit `fclose` would do.
//   3. the file GREW BY `n` — the medium-side proof, via `fstat` AFTER the sync.
//   4. `close()` SUCCEEDED — `fclose` can still fail on the final flush of a file the sync did not fully cover.
// ⇒ a partial commit, a failed sync, or a failed close (a worn sector, a full partition) is reported as the TEAR
//   it is; `note_torn_append` charges the landed bytes and seals, and the next append rolls to a fresh segment.
// ⛔ And the open mode is APPEND, never write-truncate: the log is append-only and a truncating open would
//    silently discard a whole segment on every single record.
// ⛔ `close()` IS CALLED ON EVERY PATH past the open — the verdict is computed from saved terms afterwards, never
//    by returning early, or a failed sync would leak the handle.
// FsT duck-type: `bool open_append(const char*)`, `uint32_t size()`, `size_t write(const uint8_t*, uint16_t)`,
//                `bool sync()`, `bool close()`.
template <class FsT>
inline bool append_at_end(FsT& fs, const char* path, const uint8_t* b, uint16_t n) {
    if (!fs.open_append(path)) return false;
    const uint32_t before = fs.size();                 // the open's own stat — free
    const size_t   w      = fs.write(b, n);
    const bool     synced = fs.sync();                 // fflush + fsync, BOTH results checked
    const uint32_t after  = fs.size();                 // re-stat: the COMMITTED length, not the buffered one
    const bool     closed = fs.close();                // always — see the note above
    return w == static_cast<size_t>(n) && synced && after == before + n && closed;
}

// ---- D4: the whole-segment read ----------------------------------------------------------------------------
// Clamp to `cap` (the store guarantees `cap == seg_bytes <= the 4 KiB scratch`) and report the bytes ACTUALLY
// read: a negative/short read is not an error to hide, it is simply where the frame chain stops — which is the
// existing torn-tail stop in `read_since`. An absent segment reads 0.
// FsT duck-type: `bool open_read(const char*)`, `uint32_t size()`, `int read(uint8_t*, uint32_t)`, `void close()`.
template <class FsT>
inline uint32_t read_whole(FsT& fs, const char* path, uint8_t* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    if (!fs.open_read(path)) return 0;                       // absent is not an error
    uint32_t sz = fs.size();
    if (sz > cap) sz = cap;
    const int r = sz ? fs.read(out, sz) : 0;
    fs.close();
    return r > 0 ? static_cast<uint32_t>(r) : 0;
}

// ---- D5: the META blob verdict — EXACT length or nothing ---------------------------------------------------
// ★ `Preferences::getBytes` answers 0 when the stored blob is LONGER than the destination, and the SHORT length
// when it is smaller (`device_nv.h`'s `nvs_read_slot` documents both). Either way the destination holds a
// half-populated `Meta`, whose `seg_count`/`head_seg` then divide and bound a ring walk. The store's own
// `load_meta()` range-checks magic + structure, but it can only do that on a struct that was fully read — so the
// seam refuses anything but an EXACT-length read and reports it as "no meta", which `begin()` treats as fresh.
inline bool meta_len_ok(int got, uint16_t want) { return got == static_cast<int>(want); }
// ⛔ [[B134]] QG ROUND 3 — WHAT THIS PREDICATE MEANS CHANGED, THOUGH ITS BODY DID NOT. It used to answer
//    "is this a meta, or are we FRESH?", collapsing two states the store must now tell apart. A key that is
//    PRESENT and the wrong length is CORRUPT metadata, not an absent record — only a true key-absent is fresh.
//    ⇒ its one caller maps `false` to `MetaLoad::error`, never to `MetaLoad::absent`.

// ---- D7: the NVS BLOB LOOKUP's verdict — the ONE place an esp_err_t becomes a MetaLoad -----------------------
// ★★ HOISTED, and the two codes are CONSTANTS here rather than SDK macros, so the whole mapping is host-driven
//    (the D5/D6 precedent). The ESP32 arm `static_assert`s them against the SDK's own values, so a vendor drift
//    is a BUILD FAILURE rather than a silent misclassification — the classifier cannot quietly go wrong.
// ⛔ WHY THIS EXISTS AT ALL: `Preferences::isKey()` answers ONE `false` for "never written" and for every NVS
//    error, because it is `getType()` falling through ten typed reads to `PT_INVALID` (`Preferences.cpp:302-350`
//    in the pinned core). Routing corrupt storage through that door put it straight back on the fresh path —
//    the QG round 3 blocker arriving through the one classifier that had not been fixed.
// THE MAPPING, and every arm is `nvs.h:485-492`'s own documented set:
//    ESP_ERR_NVS_NOT_FOUND  -> absent  ⓘ the ONLY absence; nvs.h:31 says this also covers a namespace that has
//                                        never been written when opened NVS_READONLY, which is a true first boot
//    ESP_OK + exact length  -> loaded
//    ESP_OK + wrong length  -> error   (the key IS there and is the wrong size = corruption, per D5's note)
//    anything else          -> error   (ESP_FAIL is documented as "most likely a corrupted NVS partition";
//                                       INVALID_HANDLE / INVALID_NAME / INVALID_LENGTH are all medium faults)
inline constexpr int kNvsOk       = 0;        // ESP_OK
inline constexpr int kNvsNotFound = 0x1102;   // ESP_ERR_NVS_NOT_FOUND == ESP_ERR_NVS_BASE(0x1100) + 0x02
inline meshroute::MetaLoad classify_blob_lookup(int err, int got, uint16_t want) {
    if (err == kNvsNotFound) return meshroute::MetaLoad::absent;
    if (err != kNvsOk)       return meshroute::MetaLoad::error;
    return meta_len_ok(got, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;
}

// ---- D6: the record-directory INSPECTION WALK, and every way it can fail to answer ------------------------
// ★★ HOISTED so BOTH the ESP32 directory walk and the native fake run THIS logic — the F16 precedent. The three
//    decisions here (an absent directory is a real "no records"; an iteration error is not an end-of-directory;
//    an entry whose size cannot be read is not an entry of size zero) are the ones that must be RED-testable,
//    because each of them can otherwise produce `ok=true, records=false` over a live history and hand `begin()`
//    to the silent re-initialise path.
// WalkT duck-type:
//    bool open(bool* absent);            // false + *absent -> a genuine "no directory"; false + !*absent -> ERROR
//    bool next(bool* have, uint32_t* size);  // false -> ERROR (iteration or size lookup); *have=false -> clean end
//    void close();
template <class WalkT>
inline bool inspect_any_nonempty(WalkT& w, bool* ok) {
    if (ok) *ok = true;
    bool absent = false;
    if (!w.open(&absent)) { if (ok) *ok = absent; return false; }   // ⛔ only a real absence keeps `ok` true
    bool found = false, err = false;
    for (;;) {
        bool have = false; uint32_t size = 0;
        if (!w.next(&have, &size)) { err = true; break; }           // ⛔ an ERROR is never a clean end
        if (!have) break;                                          // end of directory
        if (size > 0) { found = true; break; }
    }
    w.close();
    if (err) { if (ok) *ok = false; return false; }
    return found;
}

// ============================================================================================================
// THE RECORDS STORE — an `ISegmentStore` composed from D1..D4, TEMPLATED over the IO seam.
// ★★ IT IS A TEMPLATE SO THE NATIVE SUITE DRIVES **THIS** CLASS, not a host lookalike of it. A second
//    hand-written copy in test/ would be the S1/L9 field-drop shape one level up: the thing measured and the
//    thing shipped would be two files that agree only as long as somebody keeps them agreeing — which is
//    exactly the trap `src/device_inbox_store.h` already records about itself ("a HAND-MAINTAINED TWIN").
// IoT duck-type (see D2/D3/D4 for the decisions each call feeds):
//    bool mount(bool format_on_fail);        bool ensure_dir(const char* dir);
//    bool open_append(const char*);          bool open_read(const char*);
//    uint32_t size();                        size_t write(const uint8_t*, uint16_t);
//    bool sync();                            int  read(uint8_t*, uint32_t);
//    bool close();                           bool remove(const char*);
//    bool any_under(const char* dir);
// ⓘ EVERY MUTATING CALL IS FALLIBLE. That is the whole shape of the [[B134]] QG round: a seam whose commit and
//   whose erase cannot fail is a seam that can only ever report success.
// ============================================================================================================

// ★ THE SHARED MOUNT, MADE EXPLICIT. The DM and channel stores sit on ONE filesystem, so exactly one of them may
//   mount it — and BOTH must learn whether that mount had to FORMAT, because a format wipes both dirs and each
//   store owes its companion a §10.1 epoch bump. ⛔ Deliberately a passed-in object and NOT a function-local
//   `static`: a hidden static is untestable (it would leak one case's verdict into the next) and it would make
//   "the two stores share a mount" an invisible property instead of a stated one.
struct MountOnce { bool done = false; bool ok = false; bool formatted = false; };

template <class IoT>
class SegmentStoreOver : public meshroute::ISegmentStore {
public:
    SegmentStoreOver(MountOnce& once, const char* dir) : _once(&once), _dir(dir) {}

    bool mount(bool* formatted) override {
        if (!_once->done) {
            _once->done = true;
            IoT io;
            const MountOutcome m = mount_or_format(io);
            _once->ok = m.mounted; _once->formatted = m.formatted;
        }
        if (formatted) *formatted = _once->formatted;
        if (!_once->ok) return false;
        // Per-store: the FS will not create a parent on open(). ⛔ A failed `mkdir` is a FAILED MOUNT, not a
        // warning — every subsequent append would fail for a reason nothing at boot would name.
        IoT io;
        return io.ensure_dir(_dir);
    }

    bool seg_size(uint16_t idx, uint32_t* size) const override {
        char p[40]; seg_path(_dir, idx, p, sizeof p);
        IoT io; if (!io.open_read(p)) return false;       // absent is not an error (never written yet)
        if (size) *size = io.size();
        io.close();
        return true;
    }
    bool seg_append(uint16_t idx, const uint8_t* b, uint16_t n) override {
        char p[40]; seg_path(_dir, idx, p, sizeof p);
        IoT io; return append_at_end(io, p, b, n);
    }
    uint32_t seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const override {
        char p[40]; seg_path(_dir, idx, p, sizeof p);
        IoT io; return read_whole(io, p, out, cap);
    }
    // "empty afterwards" is the contract, so an ALREADY-ABSENT segment is success — the adapter's `remove` owns
    // that distinction (POSIX `ENOENT` is not a failure). A real removal failure is REPORTED, because a destructive
    // verb that cannot erase must say so and a roll must not write behind stale bytes.
    bool seg_erase(uint16_t idx) override {
        char p[40]; seg_path(_dir, idx, p, sizeof p);
        IoT io; return io.remove(p);
    }
    // §10.1 wipe-detect. ⛔ A DIRECTORY WALK, not `ring_segs()` probes: the ring size is the STORE's private
    // arithmetic that this interface is never told, and a walk answers in one pass instead of up to 129 opens.
    bool any_segments(bool* ok) const override { IoT io; return io.any_under(_dir, ok); }

private:
    MountOnce*  _once;
    const char* _dir;
};

}  // namespace mrinboxfs

// ============================================================================================================
// THE ESP32 ARM — plumbing only. Every branch above this line is host-driven; nothing new is decided here.
// ============================================================================================================
#if defined(ARDUINO) && (defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3))
  #include <LittleFS.h>       // arduino-esp32 3.1.3 built-in (framework `libraries/LittleFS`) — no lib_deps
  #include "device_nv.h"      // ★ mrnv::PreferencesSlot — the NAMESPACE-ABSENCE classifier, reused not re-derived (U1)
  #include <Preferences.h>    // the same NVS backend `device_nv.h` already uses
  // ⛔ THE POSIX LAYER, AND IT IS NOT AN EXTRA DEPENDENCY: `fs::LittleFSFS` IS a `VFSImpl` over exactly these
  //    calls (`vfs_api.cpp` is `fopen`/`fwrite`/`fflush`/`fsync`/`fclose`/`unlink` throughout). Using them
  //    directly changes nothing about what runs — it only stops the wrapper from THROWING AWAY the results, which
  //    is [[B134]] QG blocker 2. Everything here ships inside the pinned arduino-esp32 3.1.3 newlib/VFS.
  #include <stdio.h>
  #include <unistd.h>         // fsync
  #include <errno.h>
  #include <dirent.h>
  #include <sys/stat.h>       // fstat, mkdir

// ⛔ DEFINED ONLY WHEN THE ARM ACTUALLY COMPILED — the wiring in fw_context.h / fw_main.cpp keys the durable-vs-
//    RAM choice on this, exactly as it keys the nRF52 choice on MRINBOX_QSPI_READY. An ESP32 target whose FS did
//    not compile must fall back to the visible volatile ring, not to a store whose backends do not exist.
#define MRINBOX_ESP32_LITTLEFS 1

// ★ D7's constants are pinned to the SDK's own values HERE, so a vendor bump that moved them would fail the
//   BUILD rather than silently reclassify corrupt storage as a fresh store.
static_assert(mrinboxfs::kNvsOk == ESP_OK, "D7: kNvsOk must equal ESP_OK");
static_assert(mrinboxfs::kNvsNotFound == ESP_ERR_NVS_NOT_FOUND, "D7: kNvsNotFound must equal ESP_ERR_NVS_NOT_FOUND");

namespace mrinboxfs {

// ---- the LittleFS file seam that D2/D3/D4 are templated over ------------------------------------------------
// ⛔⛔ POSIX, NOT `fs::File`, AND THAT IS [[B134]] QG BLOCKER 2 — NOT A STYLE CHOICE. `fs::LittleFSFS` is a
//     `VFSImpl` whose every operation is one of these calls, but the wrapper DISCARDS the results that matter:
//     `File::flush()` calls `fflush()` and `fsync()` and keeps neither (`vfs_api.cpp:411`), `File::close()`
//     returns `void`, and `FS::remove()` is the only one that reports at all. ⇒ through that API a complete
//     write whose SYNC FAILED is indistinguishable from success, and a wipe cannot tell whether it wiped.
//     Driving the same POSIX layer directly changes nothing about what executes; it only lets the seam SEE.
// ⚠ Still ONE handle, opened and closed per call — deliberately NOT a cached open: a held-open file has
//   unflushed bytes at the moment of a power cut, which is precisely the tear §B135 exists to survive, and the
//   store's seal-and-roll assumes a failed/short write is the only tear shape.
// ⓘ `LittleFS.begin()` is still the MOUNT (it is the partition/VFS registration API and it reports its result
//   honestly); only the per-file operations go around the wrapper.
// The VFS mount point is `kBasePath`, so a store path ("/dm/7") must be prefixed to reach POSIX ("/mri/dm/7").
// ⓘ Its own struct because BOTH the file IO and the directory walk need it, and a second copy would be the
//   duplicated-path-formatter shape U1 forbids.
struct LfsPath {
    static void full(const char* p, char* out, size_t cap) {
        if (!out || cap == 0) return;
        size_t i = 0;
        for (const char* s = kBasePath; *s && i + 1 < cap; ++s) out[i++] = *s;
        for (const char* s = p; s && *s && i + 1 < cap; ++s) out[i++] = *s;
        out[i] = '\0';
    }
};

// ---- the POSIX directory walk D6 drives ---------------------------------------------------------------------
// ★★ `readdir_r`, NOT `readdir` + the errno dance — AND THE CHOICE IS EVIDENCE-BASED, not taste. Distinguishing
//    an iteration ERROR from END-OF-DIRECTORY with `readdir` requires `errno = 0` before the call and a non-zero
//    `errno` after, which is only sound if the FS driver actually SETS errno on an internal failure. ⛔ I could
//    not verify that for this build: the pinned arduino-esp32 3.1.3 ships esp_vfs/esp_littlefs as PRECOMPILED
//    ARCHIVES plus headers — no `vfs.c`, no `esp_littlefs.c` — so the driver's errno behaviour is unreadable
//    here and would have been an assumption wearing a measurement's clothes.
//    `readdir_r` reports the error IN ITS RETURN VALUE (0 = ok, `*out == NULL` = clean end; non-zero = error), a
//    contract that needs nothing from the driver beyond what `esp_vfs.h:163` already declares as a VFS entry
//    point. ⇒ verified from the header that ships; the errno dance was not verifiable at all.
// ⓘ The `dirent` is `static`: `struct dirent` carries a 256-byte `d_name` and this runs from `begin()` inside
//   `setup()`, so it stays off the stack (the do_post_ack overflow lesson). Single-threaded, one walk at a time.
struct PosixDirWalk {
    explicit PosixDirWalk(const char* dir) { LfsPath::full(dir, _dir, sizeof _dir); }
    bool open(bool* absent) {
        if (absent) *absent = false;
        errno = 0;
        _d = ::opendir(_dir);
        if (_d) return true;
        if (absent) *absent = (errno == ENOENT);   // the directory genuinely is not there = a real "no records"
        return false;
    }
    bool next(bool* have, uint32_t* size) {
        *have = false; *size = 0;
        static struct dirent ent;
        struct dirent* out = nullptr;
        if (::readdir_r(_d, &ent, &out) != 0) return false;   // ★ the error is REPORTED, not inferred
        if (!out) return true;                                // clean end of directory
        char fp[96]; size_t i = 0;
        for (const char* s = _dir; *s && i + 1 < sizeof fp; ++s) fp[i++] = *s;
        if (i + 1 < sizeof fp) fp[i++] = '/';
        for (const char* s = out->d_name; *s && i + 1 < sizeof fp; ++s) fp[i++] = *s;
        fp[i < sizeof fp ? i : sizeof fp - 1] = '\0';
        struct stat st{};
        // ⛔ A SIZE WE CANNOT READ IS NOT A SIZE OF ZERO. Swallowing this made an unreadable entry look like an
        //    empty one, which is the same "could not answer" laundered into "no records".
        if (::stat(fp, &st) != 0) return false;
        *have = true;
        *size = S_ISREG(st.st_mode) ? static_cast<uint32_t>(st.st_size) : 0;   // a subdirectory is not a record
        return true;
    }
    void close() { if (_d) { ::closedir(_d); _d = nullptr; } }
private:
    DIR* _d = nullptr;
    char _dir[48];
};

struct LfsIo {
    FILE* f = nullptr;

    static void full(const char* p, char* out, size_t cap) { LfsPath::full(p, out, cap); }   // one formatter (U1)

    bool mount(bool format_on_fail) {
        return LittleFS.begin(format_on_fail, kBasePath, /*maxOpenFiles=*/5, kPartLabel);
    }
    // An EXISTING directory is success — `mkdir` answers `EEXIST`, which is the state we asked for.
    bool ensure_dir(const char* d) {
        char q[48]; full(d, q, sizeof q);
        return ::mkdir(q, 0777) == 0 || errno == EEXIST;
    }
    // "ab" = append, never truncate; it creates the FILE (the folder comes from ensure_dir at mount).
    bool open_append(const char* p) { char q[48]; full(p, q, sizeof q); f = ::fopen(q, "ab"); return f != nullptr; }
    bool open_read(const char* p)   { char q[48]; full(p, q, sizeof q); f = ::fopen(q, "rb"); return f != nullptr; }
    // `fstat` on the OPEN handle — before any write it is the pre-write length, after `sync()` it is the
    // COMMITTED length. That is what makes D3's growth term a statement about the medium.
    uint32_t size() {
        struct stat st{};
        if (!f || ::fstat(::fileno(f), &st) != 0) return 0;
        return static_cast<uint32_t>(st.st_size);
    }
    size_t write(const uint8_t* b, uint16_t n) { return f ? ::fwrite(b, 1, n, f) : 0; }
    // ★ THE FALLIBLE COMMIT. BOTH results checked — `fflush` empties the stdio buffer into the VFS, `fsync`
    //   drives littlefs to the medium. Either failing means the bytes are not durably there.
    bool sync() { return f && ::fflush(f) == 0 && ::fsync(::fileno(f)) == 0; }
    int  read(uint8_t* o, uint32_t n) { return f ? static_cast<int>(::fread(o, 1, n, f)) : 0; }
    bool close() { if (!f) return true; const bool ok = (::fclose(f) == 0); f = nullptr; return ok; }
    // "empty afterwards" is the contract: ENOENT means the segment is already gone, which is what was asked for.
    bool remove(const char* p) {
        char q[48]; full(p, q, sizeof q);
        errno = 0;
        return ::unlink(q) == 0 || errno == ENOENT;
    }
    // The §10.1 wipe-detector's one pass, supplied to D6's `inspect_any_nonempty` — ⛔ the DECISIONS are there,
    // host-reachable; this is only the POSIX plumbing that feeds them.
    bool any_under(const char* dir, bool* ok) {
        PosixDirWalk w(dir);
        return inspect_any_nonempty(w, ok);
    }
};

// ---- RECORDS: the host-tested `SegmentStoreOver` template, typed to the LittleFS IO ------------------------
// ⛔ NOT a second class: this is the SAME class the native suite drives, with `LfsIo` substituted for the fake.
using Esp32SegmentStore = SegmentStoreOver<LfsIo>;

// ---- META: `IMetaStore` over ONE NVS blob --------------------------------------------------------------------
// A DIFFERENT PARTITION from the records (nvs @0x9000 vs spiffs @0x670000) — which is what makes §10.1 work:
// a LittleFS format cannot take the seq high-water or the epoch with it, so `next_seq` never reuses a sequence
// and the companion learns the history was wiped instead of silently re-reading a hole.
class Esp32NvsMetaStore : public meshroute::IMetaStore {
public:
    explicit Esp32NvsMetaStore(const char* key) : _key(key) {}

    // ⛔ THREE-VALUED ([[B134]] QG round 3). ★★ `Preferences::begin(ns, readOnly=true)` ANSWERS ONE `false` FOR
    //    TWO OPPOSITE FACTS — "this namespace has never been written" and "it would not open" — which is exactly
    //    the hazard `device_nv.h` already solved. ⇒ its `PreferencesSlot::ns_absent` CLASSIFIER is reused rather
    //    than re-derived (U1); a second copy of that `nvs_open` dance is how the two facts get conflated again.
    // ⛔ A THIN FORWARD, AND DELIBERATELY NOTHING MORE. Every decision is D7's, above the platform `#if`, where
    //    the native suite drives it. This body's only job is to make ONE raw `nvs_get_blob` call — through the
    //    SHARED `mrnv::PreferencesSlot` adapter, so both consumers classify NVS the same way (U1) — and hand its
    //    untouched `esp_err_t` to the classifier. ⚠ The raw call itself is the ESP32-only residue (the F12/F16/F18
    //    discipline): it is covered by bench 19.11/19.13, not by a host battery.
    meshroute::MetaLoad load(void* blob, uint16_t len) override {
        mrnv::PreferencesSlot nvs;
        size_t got = 0;
        const int err = nvs.get_blob_raw(kMetaNs, _key, blob, len, &got);
        return classify_blob_lookup(err, static_cast<int>(got), len);
    }
    bool save(const void* blob, uint16_t len) override {
        Preferences p;
        if (!p.begin(kMetaNs, /*readOnly=*/false)) return false;
        const size_t n = p.putBytes(_key, blob, len);
        p.end();
        return n == len;
    }

private:
    const char* _key;
};

}  // namespace mrinboxfs
#endif  // ARDUINO && ESP32
