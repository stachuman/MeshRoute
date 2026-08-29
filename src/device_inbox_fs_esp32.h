// MeshRoute — src/device_inbox_fs_esp32.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// [[B134]] — THE DURABLE ESP32/Heltec INBOX BACKEND. Until this file, every ESP32 target ran the VOLATILE
// `meshroute::FixedInboxStore` RAM ring: a reboot destroyed the records, their §3.5 tombstones and the whole
// history alike (nothing "returned" because nothing survived — the 2026-08-13 sharpening of the register row).
//
// ★★ THIS FILE IS A **SEAM**, NOT A STORE. The ring, the framing, the drop-oldest eviction, the reboot restore,
//    the §10.1 wipe-detect and the §B135 seal-and-roll all stay in `lib/core/segmented_inbox_store.h`, which is
//    host-tested (test/test_segmented_inbox_store.cpp) and — since [[B260]] retired the twin — is literally the
//    SAME CLASS the nRF52 store runs, not merely the same logic. What is
//    here is only the two INJECTED interfaces that logic already takes (U1):
//      · RECORDS -> `ISegmentStore`  = LittleFS files `<dir>/<i>` on the ESP32's own `spiffs` data partition
//      · META    -> `IMetaStore`     = ONE NVS blob per store, in the `"mr"` Preferences namespace
//    ⇒ the §10.1 split is preserved on a DIFFERENT axis than the nRF52's (there: on-chip InternalFS vs an
//    external QSPI chip; here: the NVS partition vs the LittleFS partition) — two partitions, two erase
//    domains, so a records format cannot take the seq high-water or the epoch with it.
//
// ⛔ WHY NOT `mrinbox::DeviceInboxStore` (the nRF52 file this slice would once have reused): that class was a
//    hand-maintained TWIN of the lib/core logic with the flash primitives as private static/member seams.
//    Wiring ESP32 into it would have added a THIRD copy of the same ring under a second `#if` arm. The
//    injected-interface store exists precisely so a new platform is two small adapters — so that is the one
//    that got reused. ⓘ [[B260]] (2026-08-29) then RETIRED the twin onto this same seam and DELETED
//    `src/device_inbox_store.h`, which is why the shared half of this file now lives in `device_inbox_seam.h`:
//    `device_inbox_fs_nrf52.h` is its SIBLING, not its consumer. Neither platform header includes the other.
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
// REALITY SPLIT (like `device_nv.h` and `device_inbox_fs_nrf52.h`): the DECISIONS are hoisted ABOVE the platform
// `#if` as templates over a duck-typed seam, so `test/test_device_inbox_fs_esp32.cpp` drives every one of them
// on the host. The one arm below is then pure plumbing with no branch of its own. What remains
// BENCH-ONLY is the metal itself: real flash wear, a real power cycle, and a real `esp_littlefs` format
// (bench script Part 19.1 / Part 11.4).
#pragma once
#include "inbox.h"                    // meshroute::InboxStore
#include "segmented_inbox_store.h"    // meshroute::ISegmentStore / IMetaStore / SegmentedInboxStore
// ★ [[B260]] — D1..D6, `MountOnce` and `SegmentStoreOver` moved here from this file, VERBATIM, when the nRF52
//   twin was retired onto the same seam. `kDirDm`/`kDirCh` live there too (both platforms name their record
//   dirs the same). Nothing below decides anything those decisions already decide.
#include "device_inbox_seam.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mrinboxfs {

// ---- the ONE place the ESP32-ONLY on-flash names live ------------------------------------------------------
// ⓘ The record dirs (`kDirDm`/`kDirCh`) are in `device_inbox_seam.h` — shared with nRF52. What is left here is
//   what only an ESP32 target has: a VFS mount point, a partition label and the NVS namespace/keys.
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
// THE ESP32-ONLY DECISION — hoisted above the platform `#if` (device_nv.h's idiom), so the native suite can
// drive it. ⓘ D1..D6, `MountOnce` and `SegmentStoreOver` are in `device_inbox_seam.h` (shared with nRF52 since
// [[B260]]); D7 stays here because an `esp_err_t` is not a fact any other platform has.
// ============================================================================================================

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
