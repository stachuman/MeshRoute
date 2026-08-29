// MeshRoute — src/device_inbox_seam.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// THE PLATFORM-NEUTRAL DURABLE-INBOX SEAM — the decisions that turn a flash IO adapter into a
// `meshroute::ISegmentStore`, plus the `SegmentStoreOver<IoT>` template that composes them.
//
// ★★ THIS FILE IS A SEAM, NOT A STORE. The ring, the framing, the drop-oldest eviction, the reboot restore,
//    the §10.1 wipe-detect and the §B135 seal-and-roll all live in `lib/core/segmented_inbox_store.h`, which is
//    host-tested (test/test_segmented_inbox_store.cpp). What is here is only what a PLATFORM has to decide on
//    the way to that logic's two injected interfaces — and every one of those decisions is hoisted ABOVE any
//    platform `#if` (the `device_nv.h` idiom), so the native suite drives the SHIPPED code rather than a
//    host lookalike of it.
//
// ⛔ WHY IT IS ITS OWN FILE ([[B260]], 2026-08-29). These decisions were born inside `device_inbox_fs_esp32.h`
//    when ESP32 was the only platform that had them. Retiring the nRF52 hand-maintained twin
//    (`src/device_inbox_store.h`, DELETED by [[B260]]) put a SECOND platform on the same seam, and there were
//    exactly two ways to do that: have the nRF52 adapter include the ESP32 header — which makes one platform's
//    file the other's dependency for no reason — or hoist what both need to a file neither owns. The second is
//    U1's answer and the one taken: `device_inbox_fs_esp32.h` and `device_inbox_fs_nrf52.h` are SIBLINGS, each
//    supplying its own IO adapter, neither including the other.
// ⓘ The move was VERBATIM — no decision changed shape, no comment was rewritten to fit its new home. The
//   [[B134]] mutation battery follows the code (`--target=b134seam` now names THIS file), which is what keeps
//   the eight QG rounds' properties measured across the move rather than merely re-asserted.
#pragma once
#include "inbox.h"                    // meshroute::InboxStore
#include "segmented_inbox_store.h"    // meshroute::ISegmentStore / IMetaStore / SegmentedInboxStore
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mrinboxfs {

// ---- the ONE place the on-flash record-dir names live ------------------------------------------------------
// ★ BOTH platforms use these: nRF52 puts them on the external QSPI chip's LittleFS, ESP32 on its own `spiffs`
//   data partition. Same logical layout on a different medium, so a reader comparing the two platforms is not
//   also decoding two naming schemes.
inline constexpr const char* kDirDm     = "/dm";
inline constexpr const char* kDirCh     = "/ch";

// ============================================================================================================
// THE DECISIONS — hoisted above every platform `#if` (device_nv.h's idiom), so the native suite can drive them.
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
// ⚠ [[B260]] — A BACKEND MAY BE UNABLE TO SEPARATE THE TWO ATTEMPTS, and that is a fact about the vendor library,
//   not a licence to guess. `CustomLFS_QSPIFlash::begin()` mounts and, on failure, formats and remounts INSIDE
//   one one-shot call, reporting one bool for both outcomes — so the nRF52 adapter answers this protocol with a
//   single attempt and `formatted=false` always. The soundness argument for that (a format leaves NO records, so
//   `any_segments` already answers "empty" and `formatted` is only ever CONSULTED where records ARE present)
//   is stated at the nRF52 adapter's `mount()`, where it belongs — not assumed here.
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
//   2. `sync()` SUCCEEDED — ⛔⛔ AND THIS IS THE TERM THE ARDUINO FILE WRAPPERS CANNOT GIVE YOU ON EITHER
//      PLATFORM, WHICH IS WHY NEITHER ADAPTER USES THEM. ESP32: `fs::File::write` is `fwrite` into a 4 KiB stdio
//      buffer (`vfs_api.cpp:283`), `File::flush()` calls `fflush()` and `fsync()` and **DISCARDS BOTH RESULTS**
//      (`vfs_api.cpp:411`), and `File::close()` is `fclose` returning **void**. nRF52: `Adafruit_LittleFS`'s
//      `File::flush()` returns **void** and `File::close()` returns **void**, both discarding `lfs_file_sync`'s
//      and `lfs_file_close`'s error codes (`Adafruit_LittleFS_File.h:69/76`). ⇒ through EITHER wrapper a
//      COMPLETE write with a FAILED SYNC is indistinguishable from success, and terms 1 and 3 would BOTH pass on
//      it. Each adapter therefore drives the layer the wrapper is built on — POSIX on ESP32, raw `lfs_*` on
//      nRF52 — where every one of those returns a result and every one is checked. Sync adds no wear: it is the
//      same commit the close would do.
//   3. the file GREW BY `n` — the medium-side proof, measured AFTER the sync.
//   4. `close()` SUCCEEDED — the close can still fail on the final flush of a file the sync did not fully cover.
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
// seam refuses anything but an EXACT-length read, and a PRESENT blob of the wrong length is `error`, NEVER fresh.
// ⓘ [[B260]] — THE SAME PREDICATE SERVES nRF52, where the hazard arrives by a different door and lands in the
//   same place: an `/mri_*` FILE that is SHORTER than `Meta` reads as a valid PREFIX, and one that is LONGER
//   (the retired twin's 24-byte v6 blob under the shared store's 28-byte v4 `Meta` is the inverse) reads a
//   truncated struct. Exact length or nothing, on both media.
inline bool meta_len_ok(int got, uint16_t want) { return got == static_cast<int>(want); }
// ⛔ [[B134]] QG ROUND 3 — WHAT THIS PREDICATE MEANS CHANGED, THOUGH ITS BODY DID NOT. It used to answer
//    "is this a meta, or are we FRESH?", collapsing two states the store must now tell apart. A key that is
//    PRESENT and the wrong length is CORRUPT metadata, not an absent record — only a true key-absent is fresh.
//    ⇒ its callers map `false` to `MetaLoad::error`, never to `MetaLoad::absent`.

// ---- D6: the record-directory INSPECTION WALK, and every way it can fail to answer ------------------------
// ★★ HOISTED so BOTH the device directory walks and the native fake run THIS logic — the F16 precedent. The three
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
//    exactly the trap the RETIRED `src/device_inbox_store.h` recorded about itself ("a HAND-MAINTAINED TWIN")
//    right up until [[B260]] deleted it.
// IoT duck-type (see D2/D3/D4 for the decisions each call feeds):
//    bool mount(bool format_on_fail);        bool ensure_dir(const char* dir);
//    bool open_append(const char*);          bool open_read(const char*);
//    uint32_t size();                        size_t write(const uint8_t*, uint16_t);
//    bool sync();                            int  read(uint8_t*, uint32_t);
//    bool close();                           bool remove(const char*);
//    bool any_under(const char* dir, bool* ok);
// ⓘ EVERY MUTATING CALL IS FALLIBLE. That is the whole shape of the [[B134]] QG round: a seam whose commit and
//   whose erase cannot fail is a seam that can only ever report success.
// ============================================================================================================

// ★ THE SHARED MOUNT, MADE EXPLICIT. The DM and channel stores sit on ONE filesystem, so exactly one of them may
//   mount it — and BOTH must learn whether that mount had to FORMAT, because a format wipes both dirs and each
//   store owes its companion a §10.1 epoch bump. ⛔ Deliberately a passed-in object and NOT a function-local
//   `static`: a hidden static is untestable (it would leak one case's verdict into the next) and it would make
//   "the two stores share a mount" an invisible property instead of a stated one.
// ⓘ [[B260]] — the RETIRED nRF52 twin used exactly that hidden `static bool s_begun, s_ok` inside its
//   `qspi_mount()`, and the fact that it could not be driven from a test is why the nRF52 mount path had no
//   host coverage at all. It shares this object now.
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
    // that distinction (POSIX `ENOENT` / `LFS_ERR_NOENT` is not a failure). A real removal failure is REPORTED,
    // because a destructive verb that cannot erase must say so and a roll must not write behind stale bytes.
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
