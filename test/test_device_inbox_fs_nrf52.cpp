// MeshRoute — test_device_inbox_fs_nrf52.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// [[B260]] — native cases for the nRF52 DURABLE-INBOX SEAM (src/device_inbox_fs_nrf52.h). The ring, the
// framing, the eviction, the §10.1 epoch and the §B135 seal all belong to `lib/core/segmented_inbox_store.h`
// and are pinned by test_segmented_inbox_store.cpp; the platform-neutral seam decisions (D1..D6,
// `SegmentStoreOver`) belong to `src/device_inbox_seam.h` and are pinned by test_device_inbox_fs_esp32.cpp.
// What is pinned HERE is only what is true of nRF52: N1 (the InternalFS meta load's three-valued verdict),
// N2 (the checked meta commit) and the `MetaStoreOver` that composes them — plus, at the end, the DESTRUCTIVE
// [[B260]] MIGRATION and its recovery, driven end to end through the real store.
//
// ★★ THE CLASS UNDER TEST IS THE SHIPPED ONE. `MetaStoreOver<SlotT, WriteIoT>` is a template precisely so this
//    file can substitute fakes for `mrnv::InternalFsSlot` / `InternalFsWriteIo` and still exercise the
//    production `IMetaStore` — not a host lookalike that agrees with it only while somebody keeps them
//    agreeing. That is the twin trap the RETIRED `src/device_inbox_store.h` recorded about itself, and the one
//    [[B260]] exists to close; re-opening it inside the test that proves the closure would be absurd.
//
// ⛔ THE HONEST RESIDUE, STATED RATHER THAN IMPLIED. `QspiIo`, `QspiDirWalk` and `InternalFsWriteIo`'s bodies
//    sit inside `#if defined(ARDUINO) && nRF52 && QSPIFLASH`, so NO host gate compiles them — the same reality
//    split `device_nv.h`'s platform arms and the ESP32 seam's `LfsIo` have always had. A mutation there comes
//    back "the suite still passes", which would be an instrument reporting on code it never ran. What IS
//    driven here is every DECISION those adapters feed; that they really call `lfs_file_sync`/`lfs_dir_read` on
//    real QSPI is M2 bench residue (the drafted nRF52 steps).
// NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "device_inbox_fs_nrf52.h"   // the seam (its nRF52 arm compiles out off-Arduino; the decisions do not)
#include "fake_inbox_storage.h"      // FakeSegmentStore — the records half is already faked for the shared store

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mrinboxnrf;

namespace {

// ============================================================================================================
// THE FAKE FLASH. A file-scope singleton because `MetaStoreOver` constructs a FRESH `SlotT`/`WriteIoT` per call
// — exactly as the device does, where every call reaches the one global `InternalFS`. Each case resets it.
// ============================================================================================================
struct FakeFlash {
    std::map<std::string, std::vector<uint8_t>> files;

    // ---- read-side knobs (the four facts Adafruit's `File::open()` collapses into one `false`) ----
    bool mount_ok          = true;    // the FS would not mount at all
    bool lookup_meta_error = false;   // `lfs_stat` answered a METADATA error (⛔ not NOENT)
    bool open_fails        = false;   // the lookup said PRESENT, yet the file would not open
    bool read_rc_set       = false;   // override what read() returns...
    int  read_rc           = 0;       // ...e.g. a NEGATIVE rc from a corrupt CTZ block
    uint16_t last_read_len = 0;       // ★ what the STORE asked for = sizeof(SegmentedInboxStore::Meta)

    // ---- write-side knobs ----
    bool open_trunc_fails  = false;
    bool sync_fails        = false;   // ★★ the fault a void `flush()`/`close()` cannot report
    bool close_fails       = false;
    // ★★ THE TRUNCATE THAT DID NOT TAKE: `open_trunc` leaves the PREVIOUS blob in place. Together with a write
    //    that commits nothing this is the one fault the medium-side LENGTH term cannot see — the file is still
    //    exactly `n` bytes, of stale data — and it is why the verdict also needs `w == n`.
    bool trunc_noop        = false;
    int  write_report_by   = 0;       // write() RETURNS n + this...
    int  write_commit_by   = 0;       // ...while this many FEWER bytes actually land
    int  size_bonus        = 0;       // the medium reports extra bytes (a truncate that did not take)

    void reset() { *this = FakeFlash{}; }
};
FakeFlash g_fs;

// The `SlotT` duck-type `mrnv::fs_read_slot` requires — the same shape `test_device_nv.cpp`'s FakeFs has, so
// the [[B218]] branch order is exercised by the SAME template the device runs.
struct FakeSlot {
    static constexpr int kFoundRc  = 0;    // == LFS_ERR_OK on the live adapter
    static constexpr int kAbsentRc = -2;   // == LFS_ERR_NOENT — ⛔ the ONLY rc that means "fresh device"
    std::string path;

    bool mount() { return g_fs.mount_ok; }
    int  lookup(const char* p) {
        path = p;
        if (g_fs.lookup_meta_error) return -5;                 // LFS_ERR_IO — a metadata error, never absence
        return g_fs.files.count(p) ? kFoundRc : kAbsentRc;
    }
    bool open(const char* p) {
        path = p;
        if (g_fs.open_fails) return false;                     // present, but the open failed
        return g_fs.files.count(p) > 0;
    }
    uint32_t size() {
        auto it = g_fs.files.find(path);
        return it == g_fs.files.end() ? 0u : static_cast<uint32_t>(it->second.size());
    }
    int read(void* dst, size_t len) {
        g_fs.last_read_len = static_cast<uint16_t>(len);
        if (g_fs.read_rc_set) return g_fs.read_rc;
        auto it = g_fs.files.find(path);
        if (it == g_fs.files.end()) return 0;
        const size_t k = it->second.size() < len ? it->second.size() : len;
        std::memcpy(dst, it->second.data(), k);
        return static_cast<int>(k);
    }
    void close() {}
};

// The `WriteIoT` duck-type N2 requires. ★ `write()` STAGES and `sync()`/`close()` COMMIT, so `size()` before a
// sync answers about the MEDIUM and not about RAM — without that, N2's growth term would be untestable and
// dropping it would look like a harmless simplification.
struct FakeWriteIo {
    std::string          path;
    bool                 open_ = false;
    std::vector<uint8_t> staged;

    ~FakeWriteIo() { if (open_) { commit(); open_ = false; } }

    bool open_trunc(const char* p) {
        if (g_fs.open_trunc_fails) return false;
        path = p; open_ = true; staged.clear();
        if (!g_fs.trunc_noop) g_fs.files[path].clear();   // ★ TRUNCATE — emptied at open, never removed
        return true;
    }
    size_t write(const void* b, uint16_t n) {
        if (!open_) return 0;
        const int land_i = static_cast<int>(n) - g_fs.write_commit_by;
        const size_t land = land_i > 0 ? static_cast<size_t>(land_i) : 0;
        const auto* p = static_cast<const uint8_t*>(b);
        staged.insert(staged.end(), p, p + land);
        const int rep = static_cast<int>(n) + g_fs.write_report_by;
        return rep > 0 ? static_cast<size_t>(rep) : 0;
    }
    // ★ THE FALLIBLE COMMIT. ⚠ The bytes are committed EVEN WHEN THE SYNC IS REPORTED FAILED, and that is the
    //   whole point of this injector: the file then has the correct apparent length, so N2's growth term passes
    //   and ONLY the sync result can tell the store the data is not durably there.
    bool sync() { if (!open_) return false; commit(); return !g_fs.sync_fails; }
    uint32_t size() {
        auto it = g_fs.files.find(path);
        const int n = (it == g_fs.files.end() ? 0 : static_cast<int>(it->second.size())) + g_fs.size_bonus;
        return n > 0 ? static_cast<uint32_t>(n) : 0u;
    }
    bool close() { if (!open_) return true; commit(); open_ = false; return !g_fs.close_fails; }

private:
    void commit() {
        auto& f = g_fs.files[path];
        f.insert(f.end(), staged.begin(), staged.end());
        staged.clear();
    }
};

// ⛔ THE SHIPPED CLASS, with the fakes substituted — not a copy of it.
using FakeMetaStore = mrinboxnrf::MetaStoreOver<FakeSlot, FakeWriteIo>;

constexpr const char* kMetaDm = "/mri_dm";

mrnv::SlotIo io_ok()        { return mrnv::SlotIo{}; }
mrnv::SlotIo io_backend()   { mrnv::SlotIo io; io.backend_failed = true; return io; }
mrnv::SlotIo io_oversize()  { mrnv::SlotIo io; io.oversize = true; return io; }

}  // namespace

// ============================================================================================================
// N1 — the InternalFS meta load's three-valued verdict
// ============================================================================================================
TEST_CASE("B260/N1 ★★★ a backend REFUSAL is never 'absent' — the store would not answer, so nothing is known") {
    // ⛔⛔ THE ORDER IS THE FIX. `mrnv::fs_read_slot` returns `kSlotAbsent` for a mount failure, a metadata
    //    error and a failed open ALIKE, and only `SlotIo::backend_failed` separates them from a genuine
    //    first boot. Testing the sentinel first would classify every one of those as a fresh device — which is
    //    the [[B134]] QG round-2/3 blocker arriving through the nRF52 door, and the reason ③ in the [[B260]]
    //    row (`!had_meta` re-initialising over existing records) was reachable at all.
    constexpr uint16_t want = 28;
    CHECK(classify_meta_read(mrnv::kSlotAbsent, io_backend(), want) == meshroute::MetaLoad::error);
    // ★ AND EVEN WITH A PERFECT-LOOKING LENGTH: a backend that failed cannot have produced a valid read.
    CHECK(classify_meta_read(want, io_backend(), want) == meshroute::MetaLoad::error);
}

TEST_CASE("B260/N1: the ONE absence — no record, and the backend answered fine — is a real first boot") {
    constexpr uint16_t want = 28;
    CHECK(classify_meta_read(mrnv::kSlotAbsent, io_ok(), want) == meshroute::MetaLoad::absent);
}

TEST_CASE("B260/N1: an EXACT-length read is the only 'loaded'") {
    constexpr uint16_t want = 28;
    CHECK(classify_meta_read(want, io_ok(), want) == meshroute::MetaLoad::loaded);
}

TEST_CASE("B260/N1 ★★★ THE MIGRATION VERDICT: the retired twin's 24-byte meta is CORRUPT, never absent") {
    // ⛔⛔ THE [[B260]] MIGRATION, AT ITS SOURCE. `mrinbox::Meta` (the DELETED twin's blob, v6) was 24 bytes;
    //    `SegmentedInboxStore::Meta` (v4) is 28 — it gained the persisted `records_state` marker that retired
    //    the every-boot epoch ratchet (defect ④). A short read is CORRUPTION, and calling it `absent` would
    //    re-initialise over a live QSPI history: head/tail to 0/0 (records hidden while physically present)
    //    and next_seq to 1 (sequences the companion has already filed, reused). Both halves of the corruption
    //    contract, violated at once — which is exactly defect ③.
    CHECK(classify_meta_read(24, io_ok(), 28) == meshroute::MetaLoad::error);
    CHECK(classify_meta_read(27, io_ok(), 28) == meshroute::MetaLoad::error);
    CHECK(classify_meta_read(0,  io_ok(), 28) == meshroute::MetaLoad::error);
}

TEST_CASE("B260/N1 ★★ an OVER-LENGTH file is corrupt too — a valid PREFIX is not a valid record") {
    // `fs_read_slot` reads only `len` bytes, so a LONGER file returns exactly `sizeof(Meta)` and its prefix
    // would be accepted as the whole record. The length alone can never show this; only `SlotIo::oversize` can.
    CHECK(classify_meta_read(28, io_oversize(), 28) == meshroute::MetaLoad::error);
}

TEST_CASE("B260/N1 ★★ a NEGATIVE read rc (a corrupt CTZ block) is an error — ⛔ not absence, and not zero") {
    // nRF52's own corruption mode: `File::read` returns an LFS_ERR_* on a corrupt skip-list head, which is the
    // signal `mrnv::mount_or_repair()` keys its self-heal on. It must never be laundered into "fresh device".
    CHECK(classify_meta_read(-84, io_ok(), 28) == meshroute::MetaLoad::error);
    CHECK(classify_meta_read(-5,  io_ok(), 28) == meshroute::MetaLoad::error);
    // ⚠ ...and ⛔ NOT via a `< 0` test that would swallow the absent sentinel: -1 IS `kSlotAbsent`.
    CHECK(classify_meta_read(mrnv::kSlotAbsent, io_ok(), 28) == meshroute::MetaLoad::absent);
}

// ============================================================================================================
// N1 COMPOSED WITH `mrnv::fs_read_slot` — the four facts Adafruit's `File::open()` collapses, end to end
// ============================================================================================================
TEST_CASE("B260 ★★★ every way the InternalFS read can fail lands on `error`; only a true NOENT is `absent`") {
    // ⓘ Driven through the SHIPPED `MetaStoreOver::load`, so this is the real composition — the [[B218]]
    //   branch order AND N1's classification — not two things tested apart and assumed to compose.
    uint8_t blob[28] = {};
    FakeMetaStore meta(kMetaDm);

    {   // (a) the healthy control FIRST, so the failure arms cannot pass by accident
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(28, 0xAB);
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::loaded);
        CHECK(blob[0] == 0xAB);
    }
    {   // (b) a genuine first boot: no file, backend healthy
        g_fs.reset();
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::absent);
    }
    {   // (c) the filesystem would not mount — ⛔ the store refused to answer
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(28, 0); g_fs.mount_ok = false;
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::error);
    }
    {   // (d) `lfs_stat` answered a METADATA error — ⛔ not NOENT, so not a fresh device
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(28, 0); g_fs.lookup_meta_error = true;
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::error);
    }
    {   // (e) the lookup said PRESENT and the open still failed (allocation / lfs_file_open)
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(28, 0); g_fs.open_fails = true;
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::error);
    }
    {   // (f) ★ THE MIGRATION, THROUGH THE REAL READ PATH: the twin's 24-byte blob is present and short
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(24, 0x5A);
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::error);
    }
    {   // (g) a LONGER file — the prefix hazard `SlotIo::oversize` exists for
        g_fs.reset(); g_fs.files[kMetaDm] = std::vector<uint8_t>(40, 0x11);
        CHECK(meta.load(blob, sizeof blob) == meshroute::MetaLoad::error);
    }
}

// ============================================================================================================
// N2 — the meta commit's verdict
// ============================================================================================================
TEST_CASE("B260/N2: a clean save reports true and the bytes are on the medium") {
    g_fs.reset();
    FakeWriteIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(write_whole(io, kMetaDm, b, 8));
    CHECK(g_fs.files[kMetaDm].size() == 8);
    CHECK(g_fs.files[kMetaDm][7] == 8);
}

TEST_CASE("B260/N2 ★★★ a COMPLETE write whose SYNC FAILED is a FAILED SAVE — the fault a void flush() hides") {
    // ⛔⛔ THE BLOCKER-2 SHAPE IN A SECOND VENDOR'S WRAPPER. `Adafruit_LittleFS_Namespace::File::flush()` and
    //    `File::close()` BOTH return void, discarding `lfs_file_sync`'s and `lfs_file_close`'s error codes — so
    //    through that API a complete write whose commit failed is indistinguishable from success. The bytes
    //    land in full here, so terms 1, 3 and 4 ALL pass; only the sync result can tell. This is the case that
    //    fails if the seam ever goes back to `File`.
    // ⓘ And it is the case defect ① turns on: every `save_meta()` the shared store now CHECKS is only as
    //   honest as this verdict. A seam that cannot fail makes a checked call a checked lie.
    g_fs.reset(); g_fs.sync_fails = true;
    FakeWriteIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(write_whole(io, kMetaDm, b, 8));
    CHECK(g_fs.files[kMetaDm].size() == 8);      // ★ the apparent length is PERFECT — that is the trap
}

TEST_CASE("B260/N2 ★★ a SHORT write, and a write that REPORTS n while committing less, are both failures") {
    {   // the write itself came up short
        g_fs.reset(); g_fs.write_commit_by = 3; g_fs.write_report_by = -3;
        FakeWriteIo io;
        const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK_FALSE(write_whole(io, kMetaDm, b, 8));
    }
    {   // ★ it RETURNED 8 and only 5 landed — term 1 passes, and only the medium-side length term catches it
        g_fs.reset(); g_fs.write_commit_by = 3;
        FakeWriteIo io;
        const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK_FALSE(write_whole(io, kMetaDm, b, 8));
        CHECK(g_fs.files[kMetaDm].size() == 5);
    }
}

TEST_CASE("B260/N2 ★★★ a STALE blob of the right LENGTH is a failed save — the term only `w == n` can see") {
    // ⛔⛔ THE FAULT THE MEDIUM-SIDE LENGTH TERM IS BLIND TO, and the reason the verdict is four terms and not
    //    three. The file already holds a previous 8-byte Meta; `open_trunc` does NOT actually truncate (the
    //    LFS_O_TRUNC flag not honoured, a backend bug), and the write then commits NOTHING. The result: the file
    //    is exactly `n` bytes — so `after == n` PASSES — of STALE metadata. Reporting success there is a
    //    `save_meta()` that persisted the OLD epoch, the OLD ring head and the OLD seq high-water while telling
    //    the store the new ones landed, which is defect ① in its purest form.
    // ⓘ This case exists because the battery said so: without it, weakening `w == n` to `w > 0` came back
    //   "the suite still passes; nothing measures this". An entry that cannot go RED is a property nothing
    //   was measuring, and the answer is a case, not a deleted entry.
    g_fs.reset();
    g_fs.files[kMetaDm] = std::vector<uint8_t>(8, 0xEE);   // the PREVIOUS blob, same length as the new one
    g_fs.trunc_noop = true;
    g_fs.write_commit_by = 8;      // nothing lands...
    g_fs.write_report_by = -5;     // ...and the call under-reports (3) rather than answering 0
    FakeWriteIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(write_whole(io, kMetaDm, b, 8));
    CHECK(g_fs.files[kMetaDm].size() == 8);        // ★ the length is PERFECT — that is the trap
    CHECK(g_fs.files[kMetaDm][0] == 0xEE);         // ★ ...and every byte of it is the STALE blob
}

TEST_CASE("B260/N2 ★★ a file LONGER than the blob is a failed save — the truncate did not take") {
    // ⛔ `after == n`, not `after >= n`, and this is why: an overwrite that appended instead of truncating
    //    leaves a longer file, and the NEXT boot reads it as N1's `oversize` — a valid PREFIX of a stale blob
    //    presented as the current one. Reporting success here would defer that corruption to a reboot.
    g_fs.reset(); g_fs.size_bonus = 4;
    FakeWriteIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(write_whole(io, kMetaDm, b, 8));
}

TEST_CASE("B260/N2 ★ a failed CLOSE is a failed save too (the close still flushes, and can still fail)") {
    g_fs.reset(); g_fs.close_fails = true;
    FakeWriteIo io;
    const uint8_t b[4] = {1, 2, 3, 4};
    CHECK_FALSE(write_whole(io, kMetaDm, b, 4));
}

TEST_CASE("B260/N2: an open that fails is a failed save, and writes nothing") {
    g_fs.reset(); g_fs.open_trunc_fails = true;
    FakeWriteIo io;
    const uint8_t b[4] = {1, 2, 3, 4};
    CHECK_FALSE(write_whole(io, kMetaDm, b, 4));
    CHECK(g_fs.files.count(kMetaDm) == 0);
}

TEST_CASE("B260/N2 ★ the save TRUNCATES — a second save of the same blob does not grow the file") {
    // The meta is OVERWRITTEN, never appended. Without truncate-on-open every `save_meta()` would extend the
    // file, and the boot after the first one would read a stale 28-byte prefix as the live metadata.
    g_fs.reset();
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    { FakeWriteIo io; CHECK(write_whole(io, kMetaDm, b, 8)); }
    { FakeWriteIo io; CHECK(write_whole(io, kMetaDm, b, 8)); }
    CHECK(g_fs.files[kMetaDm].size() == 8);
}

// ============================================================================================================
// END-TO-END — the [[B260]] migration and its recovery, through the REAL store over the fake flash
// ============================================================================================================
TEST_CASE("B260 ★★★ THE MIGRATION: an nRF52 node carrying the retired twin's meta REFUSES to mount, loudly") {
    // ⛔⛔ THE DELIBERATE, DESTRUCTIVE OUTCOME, MEASURED RATHER THAN ASSERTED IN A COMMENT. On the FIRST boot of
    //    this build, a node whose `/mri_dm` still holds the deleted `mrinbox::Meta` (24 B, v6) reads a
    //    WRONG-LENGTH blob -> `MetaLoad::error` -> `SegMountFault::meta_corrupt` -> begin() false. The node
    //    still boots and operates; only the inbox is inert, and the boot line names the fault (fw_main.cpp).
    // ⚠ THE OPERATOR'S WAY OUT IS `factory_reset confirm`, WHICH IS **NOT INBOX-ONLY**: its nRF52 arm is
    //   `InternalFS.format()`, so CONFIG, IDENTITY, PEERS, TEAM STATE and the join/UI stores go with the inbox
    //   meta and the node must be re-provisioned. Accepted once under M3 (MeshRoute is unshipped).
    g_fs.reset();
    g_fs.files[kMetaDm] = std::vector<uint8_t>(24, 0x5A);      // the twin's blob, byte-count exact
    FakeMetaStore meta(kMetaDm);
    meshroute::FakeSegmentStore recs;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);

    CHECK_FALSE(s.begin());
    CHECK(s.mount_fault() == meshroute::SegMountFault::meta_corrupt);
    // ★ THE MEASUREMENT THAT MAKES THE REFUSAL DETERMINISTIC rather than incidental: the store asks for
    //   EXACTLY 28 bytes. 24 != 28 on every build, so no node can silently slip through with a stale blob.
    //   (The 24 is the DELETED struct's size and cannot be `sizeof`'d here — it is the constant above.)
    CHECK(g_fs.last_read_len == 28);
    // ⛔ AND IT IS `meta_corrupt`, NOT `meta_lost_over_records` — two codes, two operator answers. This one is
    //    recoverable by `factory_reset confirm`; over a live record store the other one is not.
    CHECK(s.mount_fault() != meshroute::SegMountFault::meta_lost_over_records);
}

TEST_CASE("B260 ★★ THE RECOVERY: once the stale meta is gone the store mounts FRESH, and stays mounted") {
    // The second half of the migration contract. `factory_reset confirm` removes the meta (the format takes it)
    // and wipes the QSPI records, so the next boot is an ordinary first boot: epoch 1, next_seq 1, no fault.
    g_fs.reset();
    FakeMetaStore meta(kMetaDm);
    meshroute::FakeSegmentStore recs;
    {
        meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        CHECK(s.mount_fault() == meshroute::SegMountFault::none);
        CHECK(s.storage_epoch() == 1);
        CHECK(s.persisted_next_seq() == 1);
        // ⛔ THE FRESH BRANCH MUST HAVE PERSISTED — defect ①'s last limb. The twin's fresh branch wrote NOTHING,
        //    so a reboot inside the first batch of appends restored next_seq = 1 over a log that already held
        //    those seqs. Here the baseline is on the medium before begin() returns.
        CHECK(g_fs.files.count(kMetaDm) == 1);
        CHECK(g_fs.files[kMetaDm].size() == 28);
        s.append(1, reinterpret_cast<const uint8_t*>("hi"), 2);
        CHECK(s.set_next_seq(2));
    }
    // ---- POWER CUT: the store object dies; the fake flash persists ----
    meshroute::SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.mount_fault() == meshroute::SegMountFault::none);
    CHECK(s2.persisted_next_seq() == 2);          // ★ the high-water survived
    CHECK(s2.storage_epoch() == 1);               // ★ ...and a reboot is not a wipe
    struct Got { std::vector<uint32_t> seqs; } got;
    s2.read_since(0, [](void* c, uint32_t sq, const uint8_t*, uint16_t) {
        static_cast<Got*>(c)->seqs.push_back(sq); return true; }, &got);
    CHECK(got.seqs == std::vector<uint32_t>{1});
}

TEST_CASE("B260 ★★★ a meta save the InternalFS will not commit REFUSES the append — defect ① through the seam") {
    // ⛔⛔ THE REGISTER ROW'S ①, DRIVEN THROUGH THE REAL nRF52 SEAM. The retired twin's rotation save moved the
    //    head, wrote the record and returned TRUE; a reboot then dropped an ACKNOWLEDGED record, and a §3.5
    //    tombstone written that way let the deleted message COME BACK. The shared store checks that result —
    //    but only a seam whose commit can FAIL makes the check mean anything, which is what N2 supplies here.
    g_fs.reset();
    FakeMetaStore meta(kMetaDm);
    meshroute::FakeSegmentStore recs;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 64);   // small segments -> rotation happens quickly
    CHECK(s.begin());
    CHECK(s.append(1, reinterpret_cast<const uint8_t*>("aaaaaaaa"), 8));

    // ★★ THE FAULT: every subsequent commit fails its SYNC — the bytes look right, the medium is not durable.
    g_fs.sync_fails = true;
    bool refused = false;
    for (uint32_t seq = 2; seq <= 12 && !refused; ++seq)
        if (!s.append(seq, reinterpret_cast<const uint8_t*>("aaaaaaaa"), 8)) refused = true;
    CHECK(refused);                                          // ⛔ never a silent TRUE over an unpersisted topology

    // ★ AND IT RECOVERS: once the medium accepts commits again the latch clears and appends resume.
    g_fs.sync_fails = false;
    CHECK(s.append(99, reinterpret_cast<const uint8_t*>("bbbb"), 4));
}

TEST_CASE("B260 ★★ set_read_cursor's failure is REPORTED and REPAIRABLE across the nRF52 seam — defect ⑤") {
    // ⛔ The twin assigned `_meta.read_cursor` BEFORE saving and then coalesced on the RAM value, so a failed
    //    save left the NEW value in RAM and the caller's retry hit `seq == _meta.read_cursor` and returned TRUE
    //    without attempting a save — the one operation that could repair the medium was the one the wear
    //    optimisation ate. The shared store rolls back and requires the medium to AGREE before coalescing.
    g_fs.reset();
    FakeMetaStore meta(kMetaDm);
    meshroute::FakeSegmentStore recs;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    CHECK(s.set_read_cursor(7));
    CHECK(s.read_cursor() == 7);

    g_fs.sync_fails = true;
    CHECK_FALSE(s.set_read_cursor(9));
    CHECK(s.read_cursor() == 7);            // ★ ROLLED BACK — RAM must never out-run the medium
    CHECK_FALSE(s.set_read_cursor(9));      // ★ the retry is NOT eaten by the change-detect
    g_fs.sync_fails = false;
    CHECK(s.set_read_cursor(9));            // ★ ...and it repairs once the medium accepts
    CHECK(s.read_cursor() == 9);
}
