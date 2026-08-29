// MeshRoute — test_device_inbox_fs_esp32.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// [[B134]] — native cases for the ESP32 DURABLE-INBOX SEAM (src/device_inbox_fs_esp32.h). The ring, the
// framing, the eviction, the §10.1 epoch and the §B135 seal all belong to `lib/core/segmented_inbox_store.h`
// and are pinned by test_segmented_inbox_store.cpp; what is pinned HERE is the five decisions the seam adds
// (D1 the segment path · D2 the mount policy · D3 the append verdict · D4 the whole-segment read · D5 the meta
// length) and, at the end, the WHOLE B134 contract driven end-to-end through the real store over a fake flash.
//
// ★★ THE CLASS UNDER TEST IS THE SHIPPED ONE. `SegmentStoreOver<IoT>` is a template precisely so this file can
//    substitute a fake IO for `LfsIo` and still exercise the production `ISegmentStore` — not a host lookalike
//    that agrees with it only while somebody keeps them agreeing (the twin trap `src/device_inbox_store.h`
//    recorded about itself before [[B260]] DELETED it). Only `LfsIo` — nine one-line forwards to
//    `LittleFS`/`Preferences` — stays bench-only, which is where the reality split belongs.
// ⓘ [[B260]] 2026-08-29: D1..D6, `MountOnce` and `SegmentStoreOver` moved VERBATIM to `device_inbox_seam.h`
//   (shared with the nRF52 sibling seam) and are pinned from here unchanged; D7 below stayed ESP32-only.
// NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "device_inbox_fs_esp32.h"   // the seam (its ESP32 arm compiles out off-Arduino; the decisions do not)
#include "fake_inbox_storage.h"      // FakeMetaStore — the meta half is already faked for the segmented store

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mrinboxfs;

namespace {

// ============================================================================================================
// THE FAKE FLASH. A file-scope singleton because `SegmentStoreOver` constructs a FRESH `IoT` per call — exactly
// as the device does, where every call reaches the one global `LittleFS`. Each case resets it.
// ============================================================================================================
struct FakeFs {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string>                       dirs;
    bool mounted        = false;
    int  mount_calls    = 0;
    bool clean_mount_ok = true;    // knob: does the FIRST, non-formatting mount succeed?
    bool format_ok      = true;    // knob: does the format + remount succeed?
    // ★ D3's fault injector, and it models the fault the naive guard CANNOT see: `write()` may REPORT one
    //   length while a different (smaller) number of bytes actually COMMITS — a buffered write whose flush
    //   only partly reached the medium. `report == n && commit < n` is the shape that makes a torn frame look
    //   like a clean append. A fault injector that cannot produce the fault is not a fault test.
    int      fail_at    = -1;      // which write() call (counted from arming) misbehaves; -1 = disarmed
    int      writes_seen = 0;
    uint16_t report     = 0;       // what that write() RETURNS
    uint16_t commit     = 0;       // how many of its bytes actually LAND
    // ★★ [[B134]] QG BLOCKER 2's INJECTOR, AND IT IS A DIFFERENT FAULT FROM EVERY ONE ABOVE: the write lands IN
    //    FULL — correct apparent length, nothing torn — and only the SYNC fails. Terms 1 and 3 of D3 both PASS on
    //    it. That is exactly the fault `fs::File` cannot report (`flush()` discards `fflush`/`fsync`, `close()`
    //    returns void), so before the POSIX adapter it was indistinguishable from success.
    bool     sync_fails  = false;
    bool     close_fails = false;
    bool     unlink_fails = false;
    bool     mkdir_fails  = false;
    bool     opendir_fails = false;
    bool     dir_absent    = false;   // the directory genuinely is not there (a real 'no records')
    int      readdir_fails_at = -1;   // iteration dies at the Nth matching entry
    int      stat_fails_at    = -1;   // the Nth matching entry's size cannot be read
    void reset() { *this = FakeFs{}; }
    void arm(int nth, uint16_t report_n, uint16_t commit_n) { fail_at = nth; report = report_n; commit = commit_n; writes_seen = 0; }
    bool armed() const { return fail_at >= 0; }
};
FakeFs g_fs;

// ★★ THE FAKE FILE MODELS STDIO BUFFERING, AND THAT IS LOAD-BEARING. On the device `write()` is `fwrite` into a
//    4 KiB buffer (`vfs_api.cpp:283`) and the bytes reach littlefs only at `fflush`/`fclose` — so a fake whose
//    `write()` commits immediately would make D3's `flush()` term untestable, and dropping that term would look
//    like a harmless simplification. Here `write()` STAGES and `flush()`/`close()` COMMIT, exactly as newlib
//    does, so `size()` before a flush answers about the medium and not about RAM.
// The fake directory walk, with the three faults the real one can suffer. ⚠ `readdir_fails_at` and
// `stat_fails_at` are the two QG round 3 named: an iteration that dies PARTWAY (which must not read as a clean
// end of directory) and an entry whose size cannot be read (which must not read as an entry of size zero).
struct FakeDirWalk {
    explicit FakeDirWalk(const char* dir) : _pre(std::string(dir) + "/") {}
    bool open(bool* absent) {
        if (absent) *absent = g_fs.dir_absent;
        return !g_fs.opendir_fails && !g_fs.dir_absent;
    }
    bool next(bool* have, uint32_t* size) {
        *have = false; *size = 0;
        for (; _it != g_fs.files.end(); ++_it) {
            if (_it->first.compare(0, _pre.size(), _pre) != 0) continue;
            if (g_fs.readdir_fails_at >= 0 && _seen == g_fs.readdir_fails_at) return false;   // iteration error
            if (g_fs.stat_fails_at >= 0 && _seen == g_fs.stat_fails_at) return false;         // unreadable size
            ++_seen;
            *have = true; *size = static_cast<uint32_t>(_it->second.size());
            ++_it;
            return true;
        }
        return true;                                   // clean end of directory
    }
    void close() {}
private:
    std::string _pre;
    std::map<std::string, std::vector<uint8_t>>::const_iterator _it = g_fs.files.begin();
    int _seen = 0;
};

struct FakeFsIo {
    std::string          path;
    bool                 open_ok = false;
    std::vector<uint8_t> pending;      // the stdio buffer: staged, not yet on the medium

    bool mount(bool format_on_fail) {
        ++g_fs.mount_calls;
        if (!format_on_fail) { g_fs.mounted = g_fs.clean_mount_ok; return g_fs.mounted; }
        if (!g_fs.format_ok) return false;
        g_fs.files.clear(); g_fs.dirs.clear();      // a FORMAT erases every record
        g_fs.mounted = true;
        return true;
    }
    bool ensure_dir(const char* d) { if (!g_fs.mounted || g_fs.mkdir_fails) return false; g_fs.dirs.insert(d); return true; }

    // `create=false` semantics: `fopen(path, "a")` creates the FILE but never its FOLDER.
    bool open_append(const char* p) {
        if (!g_fs.mounted) return false;
        const std::string s(p);
        const size_t slash = s.rfind('/');
        if (slash == std::string::npos || !g_fs.dirs.count(s.substr(0, slash))) return false;
        path = s; open_ok = true; g_fs.files[path];             // creates an empty file if absent
        return true;
    }
    bool open_read(const char* p) {
        if (!g_fs.mounted) return false;
        if (!g_fs.files.count(p)) return false;                 // absent
        path = p; open_ok = true;
        return true;
    }
    uint32_t size() { return open_ok ? static_cast<uint32_t>(g_fs.files[path].size()) : 0; }   // COMMITTED bytes only
    size_t   write(const uint8_t* b, uint16_t n) {
        if (!open_ok) return 0;
        const bool faulty = (g_fs.fail_at >= 0 && g_fs.writes_seen == g_fs.fail_at);
        ++g_fs.writes_seen;
        uint16_t land = n, ret = n;
        if (faulty) { land = g_fs.commit < n ? g_fs.commit : n; ret = g_fs.report; g_fs.fail_at = -1; }
        pending.insert(pending.end(), b, b + land);   // staged — the medium has not seen it yet
        return ret;
    }
    // ★ THE FALLIBLE COMMIT. ⚠ The bytes are committed EVEN WHEN THE SYNC IS REPORTED FAILED, and that is the
    //   whole point of this injector: the file then has the correct apparent length, so D3's growth term passes
    //   and ONLY the sync result can tell the store that the data is not durably there.
    bool sync() {
        if (!open_ok) return false;
        auto& f = g_fs.files[path];
        f.insert(f.end(), pending.begin(), pending.end());
        pending.clear();
        return !g_fs.sync_fails;
    }
    int  read(uint8_t* o, uint32_t n) {
        if (!open_ok) return 0;
        auto& f = g_fs.files[path];
        const uint32_t k = n < f.size() ? n : static_cast<uint32_t>(f.size());
        std::memcpy(o, f.data(), k);
        return static_cast<int>(k);
    }
    bool close() { if (!open_ok) return true; sync(); open_ok = false; return !g_fs.close_fails; }   // fclose flushes and CAN fail
    bool remove(const char* p) { if (g_fs.unlink_fails) return false; g_fs.files.erase(p); return true; }
    // ★★ THE FAKE DRIVES **D6**, THE SAME `inspect_any_nonempty` THE DEVICE DOES (the F16 precedent). Only the
    //    POSIX plumbing differs, so every decision — absent-vs-error, iteration-error-vs-end, unreadable-size —
    //    is exercised and mutatable on the host.
    bool any_under(const char* dir, bool* ok) {
        FakeDirWalk w(dir);
        return inspect_any_nonempty(w, ok);
    }
};

using FakeSegStore = SegmentStoreOver<FakeFsIo>;

std::string path_of(const char* dir, uint16_t idx, size_t cap = 40) {
    std::vector<char> buf(cap, '\xEE');
    seg_path(dir, idx, buf.data(), cap);
    return std::string(buf.data());
}

struct PullSeqs { std::vector<uint32_t> seqs; std::vector<std::string> bodies; };
bool pull_cb(void* ctx, const meshroute::InboxEntry& e) {
    auto* p = static_cast<PullSeqs*>(ctx);
    p->seqs.push_back(e.seq);
    p->bodies.emplace_back(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len);
    return true;
}

}  // namespace

// ============================================================================================================
// D1 — the segment path
// ============================================================================================================
TEST_CASE("B134/D1: seg_path builds \"<dir>/<idx>\" decimal, and NEVER overruns or leaves it unterminated") {
    CHECK(path_of("/dm", 0)   == "/dm/0");
    CHECK(path_of("/ch", 7)   == "/ch/7");
    CHECK(path_of("/dm", 129) == "/dm/129");     // the DM ring is 512K/4K+1 = 129 segments — the real top index
    CHECK(path_of("/dm", 65535) == "/dm/65535");

    // ★ A CLIPPED PATH ADDRESSES A DIFFERENT SEGMENT ("/dm/12" -> "/dm/1"), which is data loss, not cosmetics.
    //   The formatter must stop at the buffer and still terminate; the CALLER's 40-byte buffers make this
    //   unreachable in the store, and this case is what keeps that true if a dir name ever grows.
    char tiny[5]; seg_path("/dm", 42, tiny, sizeof tiny);
    CHECK(std::strlen(tiny) < sizeof tiny);       // always NUL-terminated, never a buffer overrun
    char one[1]; one[0] = 'Z'; seg_path("/dm", 1, one, sizeof one);
    CHECK(one[0] == '\0');
    // cap == 0 must write NOTHING at all (there is no byte to terminate in).
    char guard = 'Q'; seg_path("/dm", 1, &guard, 0);
    CHECK(guard == 'Q');
}

// ============================================================================================================
// D2 — the mount policy
// ============================================================================================================
TEST_CASE("B134/D2: a clean mount NEVER formats, and reports formatted=false (the epoch must not bump)") {
    g_fs.reset();
    FakeFsIo io;
    const MountOutcome m = mount_or_format(io);
    CHECK(m.mounted);
    CHECK_FALSE(m.formatted);
    // ★ THE VACUITY CONTROL. A single `begin(/*formatOnFail=*/true)` would ALSO report mounted here, and every
    //   other assertion in this file would still pass — while every reboot silently reformatted a recoverable
    //   filesystem. Exactly ONE mount attempt is what proves the non-formatting probe came first.
    CHECK(g_fs.mount_calls == 1);
}

TEST_CASE("B134/D2: a corrupt FS is formatted, remounted, and the format is REPORTED (fail-loud, then recover)") {
    g_fs.reset();
    g_fs.clean_mount_ok = false;                  // the plain mount fails: corrupt or never formatted
    g_fs.files["/dm/0"] = {1, 2, 3};              // ...with records nominally present
    FakeFsIo io;
    const MountOutcome m = mount_or_format(io);
    CHECK(m.mounted);
    CHECK(m.formatted);                           // ★ REPORTED — this is what makes §10.1 bump the epoch
    CHECK(g_fs.mount_calls == 2);                 // probe, then format+remount
    CHECK(g_fs.files.empty());                    // the format really did erase the records
}

TEST_CASE("B134/D2: an UNMOUNTABLE FS fails loud — never a silent degrade, and never 'formatted'") {
    g_fs.reset();
    g_fs.clean_mount_ok = false; g_fs.format_ok = false;
    FakeFsIo io;
    const MountOutcome m = mount_or_format(io);
    CHECK_FALSE(m.mounted);
    CHECK_FALSE(m.formatted);                     // ⛔ a FAILED format wiped nothing; claiming it did would bump
                                                  //    the epoch and make the companion re-pull for no reason
    // And the store built on it refuses to begin() -> Inbox stays DISABLED (record_* inert, visible at boot).
    MountOnce once; FakeSegStore recs(once, kDirDm); meshroute::FakeMetaStore meta;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK_FALSE(s.begin());
}

TEST_CASE("B134/D2: the DM and channel stores share ONE mount, and BOTH learn it had to format") {
    g_fs.reset();
    g_fs.clean_mount_ok = false;                  // -> the first store's mount formats
    MountOnce once;
    FakeSegStore dm(once, kDirDm), ch(once, kDirCh);
    bool f_dm = false, f_ch = false;
    CHECK(dm.mount(&f_dm));
    CHECK(ch.mount(&f_ch));
    CHECK(g_fs.mount_calls == 2);                 // ★ ONE mount sequence total, not one per store
    CHECK(f_dm);
    CHECK(f_ch);                                  // ★ the SECOND store must hear about the format too: it wiped
                                                  //   both dirs, so both epochs owe the companion a bump
    CHECK(g_fs.dirs.count(kDirDm) == 1);          // and each store created its OWN record dir after the mount
    CHECK(g_fs.dirs.count(kDirCh) == 1);
}

// ============================================================================================================
// D3 — the append verdict
// ============================================================================================================
TEST_CASE("B134/D3: a clean append reports true and the bytes are on the medium") {
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    FakeFsIo io;
    const uint8_t b[4] = {1, 2, 3, 4};
    CHECK(append_at_end(io, "/dm/0", b, 4));
    CHECK(g_fs.files["/dm/0"].size() == 4);
    FakeFsIo io2;
    CHECK(append_at_end(io2, "/dm/0", b, 4));     // APPEND, never truncate
    CHECK(g_fs.files["/dm/0"].size() == 8);
}

TEST_CASE("B134/D3: a SHORT write is a tear — reported false") {
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.arm(/*nth*/0, /*report*/2, /*commit*/2);
    FakeFsIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(append_at_end(io, "/dm/0", b, 8));
    CHECK_FALSE(g_fs.armed());                    // ★ the injector really FIRED (never assume a fault test faulted)
    CHECK(g_fs.files["/dm/0"].size() == 2);       // the landed bytes are real — the store must charge + seal them
}

TEST_CASE("B134/D3 ★★ a write that RETURNS n while only part of it COMMITS is still a tear") {
    // ★★ THE CASE THIS SEAM EXISTS FOR. `fs::File::write` is `fwrite` into a 4 KiB stdio buffer and
    // `fs::File::close` is `fclose` returning **void**, so a flush that only partly reached the medium is
    // invisible to `w == n`. Left undetected it is precisely the §B135 defect: an unsealed torn header, the
    // next append consumed as its body, every later record present-but-unreachable. The growth check is the
    // only medium-side term available, and this is the fault that proves it is doing work.
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.arm(/*nth*/0, /*report*/8, /*commit*/3);   // "wrote 8" — 3 landed
    FakeFsIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(append_at_end(io, "/dm/0", b, 8));
    CHECK_FALSE(g_fs.armed());
    CHECK(g_fs.files["/dm/0"].size() == 3);
}

TEST_CASE("B134/D3 ★★★ a COMPLETE write whose SYNC FAILED is a tear — the fault `fs::File` cannot report") {
    // ⛔⛔ QG BLOCKER 2. The bytes land in full, so `w == n` passes AND the file grew by exactly `n` — terms 1 and
    //    3 of the guard are BOTH satisfied. Only the sync result distinguishes this from a durable append, and
    //    `fs::File::flush()` calls `fflush`/`fsync` and DISCARDS BOTH (`vfs_api.cpp:411`) while `File::close()`
    //    returns `void`. ⇒ through the Arduino wrapper this is indistinguishable from success, which is why the
    //    adapter drives POSIX. This case is the one that fails if the seam ever goes back.
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.sync_fails = true;
    FakeFsIo io;
    const uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_FALSE(append_at_end(io, "/dm/0", b, 8));
    CHECK(g_fs.files["/dm/0"].size() == 8);       // ★ the apparent length is PERFECT — that is the trap
}

TEST_CASE("B134/D3 ★ a failed CLOSE is a tear too (fclose still flushes, and can still fail)") {
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.close_fails = true;
    FakeFsIo io;
    const uint8_t b[4] = {1, 2, 3, 4};
    CHECK_FALSE(append_at_end(io, "/dm/0", b, 4));
}

TEST_CASE("B134 ★★ a failed SYNC is SEALED end-to-end: the retry stays reachable, nothing is lost") {
    // The blocker-2 fault carried all the way through the real store: the seam must turn a failed sync into the
    // §B135 seal-and-roll, or the next append lands behind bytes the medium may not actually hold.
    g_fs.reset();
    MountOnce once; FakeSegStore recs(once, kDirDm); meshroute::FakeMetaStore meta;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    s.append(1, reinterpret_cast<const uint8_t*>("a"), 1);
    s.append(2, reinterpret_cast<const uint8_t*>("b"), 1);
    g_fs.sync_fails = true;
    CHECK_FALSE(s.append(3, reinterpret_cast<const uint8_t*>("c"), 1));
    g_fs.sync_fails = false;                                  // the medium recovers
    s.append(4, reinterpret_cast<const uint8_t*>("d"), 1);
    struct Got { std::vector<uint32_t> seqs; } g;
    s.read_since(0, [](void* c, uint32_t sq, const uint8_t*, uint16_t) {
        static_cast<Got*>(c)->seqs.push_back(sq); return true; }, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2, 4});          // 1 and 2 survive, 4 is reachable, 3 is not invented
}

TEST_CASE("B134/D2 ★★ a failed record-folder create FAILS THE MOUNT (never a store whose every append will fail)") {
    // A mounted filesystem with no record folder is the worst shape available: every append fails at `fopen` for
    // a reason nothing at boot names, and `enabled=1` would still be printed. ⇒ the mount reports the folder.
    g_fs.reset();
    {   // the POSITIVE arm first, so the failure arm below cannot pass by accident
        MountOnce once; FakeSegStore recs(once, kDirDm);
        bool fmt = false;
        CHECK(recs.mount(&fmt));
        CHECK(g_fs.dirs.count(kDirDm) == 1);
    }
    // ★ THE FAILURE ARM: the FS mounts, the folder does not appear -> the mount is FALSE and begin() refuses,
    //   which leaves the Inbox disabled and visible at boot rather than silently unable to store anything.
    g_fs.reset(); g_fs.mkdir_fails = true;
    MountOnce once2; FakeSegStore recs2(once2, kDirDm);
    bool fmt2 = false;
    CHECK_FALSE(recs2.mount(&fmt2));
    meshroute::FakeMetaStore meta;
    meshroute::SegmentedInboxStore s(recs2, meta, 4096, 256);
    CHECK_FALSE(s.begin());
    // ...and an UNMOUNTABLE FS never claims the dir exists either.
    g_fs.reset(); g_fs.clean_mount_ok = false; g_fs.format_ok = false;
    MountOnce once3; FakeSegStore recs3(once3, kDirDm);
    CHECK_FALSE(recs3.mount(&fmt2));
    CHECK(g_fs.dirs.empty());
}

TEST_CASE("B134 ★★★ an unreadable record directory is 'could not answer', NEVER 'empty' — and it fails the mount") {
    // ⛔⛔ QG ROUND 2, POINT 2. `any_under` used to map EVERY `opendir` failure onto `return false`, and one level
    //    up that reads as "this store holds no records" — which hands `begin()` straight to the fresh-init path
    //    over a live history: records hidden, sequences reused. Only ENOENT is a real absence.
    g_fs.reset();
    MountOnce once; FakeSegStore recs(once, kDirDm);
    meshroute::FakeMetaStore meta;
    {
        meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        s.append(1, reinterpret_cast<const uint8_t*>("live"), 4);
        CHECK(s.set_next_seq(2));
    }
    // The metadata is lost AND the directory will not open: the mount must name the INSPECTION failure.
    meta.wipe();
    g_fs.opendir_fails = true;
    MountOnce once2; FakeSegStore recs2(once2, kDirDm);
    meshroute::SegmentedInboxStore s2(recs2, meta, 4096, 256);
    CHECK_FALSE(s2.begin());
    CHECK(s2.mount_fault() == meshroute::SegMountFault::records_uninspectable);
    // ★ THE CONTROL: with the directory readable the same medium answers meta_lost_over_records — so this case is
    //   about the inspection result and not about a fixture that refuses everything.
    g_fs.opendir_fails = false;
    MountOnce once3; FakeSegStore recs3(once3, kDirDm);
    meshroute::SegmentedInboxStore s3(recs3, meta, 4096, 256);
    CHECK_FALSE(s3.begin());
    CHECK(s3.mount_fault() == meshroute::SegMountFault::meta_lost_over_records);
    // ★ AND THE ORDINARY EMPTY CASE STILL READS AS EMPTY (an absent dir is a real "no records"), or the guard
    //   above would have made every first boot uninspectable.
    g_fs.reset();
    FakeFsIo io; io.mount(false); io.ensure_dir(kDirDm);
    bool ok = false;
    CHECK_FALSE(io.any_under(kDirDm, &ok));
    CHECK(ok);
}

TEST_CASE("B134/D6 ★★★ an ITERATION error and an UNREADABLE ENTRY are both 'could not answer', never 'empty'") {
    // ⛔⛔ QG ROUND 3, BLOCKER 2. `opendir` was already handled, but two more paths could still produce
    //    `ok=true, records=false` over a live history: a `readdir` that fails PARTWAY (indistinguishable from a
    //    clean end of directory unless the error is reported) and an entry whose `stat` fails (which was silently
    //    skipped, i.e. read as an entry of size zero). Either one hands `begin()` to the silent re-init path.
    // ⓘ Both are driven through D6 — the SAME `inspect_any_nonempty` the device walk uses — so these are the
    //   real decisions, not a host restatement of them.
    g_fs.reset(); { FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm); }
    // ⚠ THE FIRST SEGMENT IS DELIBERATELY EMPTY. D6 stops at the first entry it finds bytes in, so a fault armed
    //   BEHIND a non-empty entry would never be reached and the arms below would pass without measuring anything.
    //   An empty leading segment is also the realistic shape: a rolled ring is full of them.
    g_fs.files["/dm/0"] = {};
    g_fs.files["/dm/1"] = {4, 5, 6};

    {   // ★ the healthy control FIRST, so the two failure arms cannot pass by accident
        FakeFsIo io; bool ok = false;
        CHECK(io.any_under(kDirDm, &ok));
        CHECK(ok);
    }
    {   // (a) the iteration dies at the SECOND entry — ⛔ not an end of directory
        g_fs.readdir_fails_at = 1;
        FakeFsIo io; bool ok = true;
        CHECK_FALSE(io.any_under(kDirDm, &ok));
        CHECK_FALSE(ok);
        g_fs.readdir_fails_at = -1;
    }
    {   // (b) an entry's size cannot be read — ⛔ not a zero-length entry
        g_fs.stat_fails_at = 0;
        FakeFsIo io; bool ok = true;
        CHECK_FALSE(io.any_under(kDirDm, &ok));
        CHECK_FALSE(ok);
        g_fs.stat_fails_at = -1;
    }
    {   // (c) ...and a genuinely ABSENT directory is still a real "no records", or first boot would be a fault
        g_fs.dir_absent = true;
        FakeFsIo io; bool ok = false;
        CHECK_FALSE(io.any_under(kDirDm, &ok));
        CHECK(ok);
        g_fs.dir_absent = false;
    }
    // ★★ AND THE CONSEQUENCE, which is the whole reason the distinction matters: an unanswerable inspection over
    //    a LIVE store must refuse the mount rather than re-initialise it.
    meshroute::FakeMetaStore meta;
    g_fs.readdir_fails_at = 1;
    MountOnce once; FakeSegStore recs(once, kDirDm);
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK_FALSE(s.begin());
    CHECK(s.mount_fault() == meshroute::SegMountFault::records_uninspectable);
    g_fs.readdir_fails_at = -1;
}

TEST_CASE("B134 ★★★ a segment the filesystem will not remove makes wipe() report FAILURE through the seam") {
    // ⛔ QG BLOCKER 3, END TO END ACROSS THE SEAM. The store's `wipe()` can only be as honest as the erase beneath
    //    it: a seam that swallowed the removal result would put a destructive verb back to claiming a cleared
    //    inbox over records still on the partition. ⓘ The VERB's own line (`prep-restart` / `factory_reset`) lives
    //    in a TU no host gate compiles (§B115) and is owed a bench step; THIS is the reddenable half.
    g_fs.reset();
    MountOnce once; FakeSegStore recs(once, kDirDm);
    meshroute::FakeMetaStore meta;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    s.append(1, reinterpret_cast<const uint8_t*>("keepme"), 6);
    CHECK(s.wipe());                                          // the healthy arm — so the failure arm can fail
    s.append(2, reinterpret_cast<const uint8_t*>("keepme"), 6);
    g_fs.unlink_fails = true;
    CHECK_FALSE(s.wipe());                                    // ★ the removal failed and the verdict says so
    g_fs.unlink_fails = false;
    CHECK(g_fs.files.count("/dm/0") == 1);                    // ★ and the record really is still on the medium
}

TEST_CASE("B134/D3: a write that commits NOTHING, and an open that fails, are both false") {
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.arm(0, /*report*/8, /*commit*/0);
    FakeFsIo io;
    const uint8_t b[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    CHECK_FALSE(append_at_end(io, "/dm/0", b, 8));
    CHECK(g_fs.files["/dm/0"].empty());

    // ⛔ A MISSING RECORD FOLDER IS A FAILURE, NOT A CREATE. `create=false` is what keeps `VFSImpl::open` off
    //    its leaky per-open `mkdir` walk; the folders come from `ensure_dir` at mount. If one is absent the
    //    append must FAIL so the store seals — never write into a path nobody made.
    FakeFsIo io2;
    CHECK_FALSE(append_at_end(io2, "/ch/0", b, 8));
    CHECK(g_fs.files.count("/ch/0") == 0);
}

// ============================================================================================================
// D4 — the whole-segment read
// ============================================================================================================
TEST_CASE("B134/D4: read_whole returns the bytes read, clamps to cap, and answers 0 for an absent segment") {
    g_fs.reset(); FakeFsIo m; m.mount(false); m.ensure_dir(kDirDm);
    g_fs.files["/dm/0"] = {1, 2, 3, 4, 5};
    uint8_t out[8] = {};
    FakeFsIo a; CHECK(read_whole(a, "/dm/0", out, sizeof out) == 5);
    CHECK(out[4] == 5);
    FakeFsIo b; CHECK(read_whole(b, "/dm/0", out, 3) == 3);         // clamped to cap — never past the scratch
    FakeFsIo c; CHECK(read_whole(c, "/dm/9", out, sizeof out) == 0); // absent is 0, not an error
    FakeFsIo d; CHECK(read_whole(d, "/dm/0", out, 0) == 0);
    FakeFsIo e; CHECK(read_whole(e, "/dm/0", nullptr, sizeof out) == 0);
}

// ============================================================================================================
// D5 — the meta length verdict
// ============================================================================================================
TEST_CASE("B134/D5: only an EXACT-length meta read is a meta; a short or over-long one is 'fresh'") {
    // `Preferences::getBytes` answers 0 when the stored blob is LONGER than the destination and the SHORT
    // length when it is smaller. Either way the destination holds a HALF-POPULATED `Meta`, whose seg_count
    // then divides and whose head_seg bounds a ring walk. Refusing anything but the exact length is what
    // routes those to begin()'s fresh-init path instead of into the arithmetic.
    CHECK(meta_len_ok(26, 26));
    CHECK_FALSE(meta_len_ok(0, 26));        // stored blob longer than the destination
    CHECK_FALSE(meta_len_ok(25, 26));       // truncated record
    CHECK_FALSE(meta_len_ok(-1, 26));       // backend error
}

// ============================================================================================================
// D7 — the NVS blob lookup's verdict
// ============================================================================================================
TEST_CASE("B134/D7 ★★★ an NVS lookup ERROR is never 'absent' — only ESP_ERR_NVS_NOT_FOUND is") {
    // ⛔⛔ QG ROUND 4. The last door into the fresh path. `Preferences::isKey()` is `getType()`, which tries ten
    //    typed reads and falls through to `PT_INVALID` for `ESP_ERR_NVS_NOT_FOUND` AND every other NVS error
    //    alike (`Preferences.cpp:302-350`, pinned core) — so corrupt storage arrived as "this key was never
    //    written" and the store started over. `nvs_get_blob` reports the two separately (`nvs.h:485-492`).
    // ⓘ The codes are D7's own constants, `static_assert`ed against the SDK in the ESP32 arm — so this case
    //   drives the SHIPPED mapping, and a vendor drift breaks the BUILD rather than this test's relevance.
    constexpr uint16_t want = 24;

    // ★ THE ONE ABSENCE. nvs.h:31 documents that a NVS_READONLY open of a namespace that has never been written
    //   also answers NOT_FOUND — a true first boot, which must still mount fresh.
    CHECK(classify_blob_lookup(kNvsNotFound, 0, want) == meshroute::MetaLoad::absent);

    // ★ THE HAPPY PATH, exact length only.
    CHECK(classify_blob_lookup(kNvsOk, want, want) == meshroute::MetaLoad::loaded);

    // ★★ EVERY OTHER DOCUMENTED CODE IS AN ERROR — ⛔ never absent. These are nvs.h's own set:
    CHECK(classify_blob_lookup(-1,     0, want) == meshroute::MetaLoad::error);   // ESP_FAIL: "corrupted NVS partition"
    CHECK(classify_blob_lookup(0x1103, 0, want) == meshroute::MetaLoad::error);   // ESP_ERR_NVS_..._INVALID_HANDLE-class
    CHECK(classify_blob_lookup(0x1104, 0, want) == meshroute::MetaLoad::error);   // ..._INVALID_NAME-class
    CHECK(classify_blob_lookup(0x1105, 0, want) == meshroute::MetaLoad::error);   // ..._INVALID_LENGTH-class
    CHECK(classify_blob_lookup(0x1101, 0, want) == meshroute::MetaLoad::error);   // ..._NOT_INITIALIZED
    CHECK(classify_blob_lookup(0x1102 + 1, 0, want) == meshroute::MetaLoad::error);  // the neighbour of NOT_FOUND

    // ★★ AND A KEY THAT IS PRESENT BUT THE WRONG SIZE IS CORRUPTION, NOT ABSENCE (D5's rule, reached through D7).
    CHECK(classify_blob_lookup(kNvsOk, want - 1, want) == meshroute::MetaLoad::error);   // truncated
    CHECK(classify_blob_lookup(kNvsOk, want + 1, want) == meshroute::MetaLoad::error);   // over-long
    CHECK(classify_blob_lookup(kNvsOk, 0,        want) == meshroute::MetaLoad::error);   // getBytes' "too long" 0
}

// ============================================================================================================
// END-TO-END — the whole [[B134]] contract, through the REAL store over the fake flash
// ============================================================================================================
TEST_CASE("B134 ★★ records AND their §3.5 tombstones survive a reboot, and the epoch does NOT change") {
    // ⛔ THIS IS THE CASE THE REGISTER ROW IS ABOUT. On the old ESP32 backend a reboot destroyed the record,
    //    its tombstone and the whole history alike, and `fw_main` drew a FRESH RANDOM epoch every boot so the
    //    companion re-pulled from zero for ever. All three of those must now be false.
    g_fs.reset();
    MountOnce once;
    FakeSegStore drecs(once, kDirDm), crecs(once, kDirCh);
    meshroute::FakeMetaStore dmeta, cmeta;
    uint32_t epoch_before = 0;
    {
        meshroute::SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
        meshroute::Inbox ib; ib.on_init(&dm, &ch);
        CHECK(ib.enabled());
        const uint32_t s1 = ib.record_dm(2, 0xABCD, 7, 0, reinterpret_cast<const uint8_t*>("keep"),   4, 1000);
        const uint32_t s2 = ib.record_dm(2, 0xABCD, 8, 0, reinterpret_cast<const uint8_t*>("delete"), 6, 1001);
        const uint32_t s3 = ib.record_dm(2, 0xABCD, 9, 0, reinterpret_cast<const uint8_t*>("later"),  5, 1002);
        CHECK(s1 == 1); CHECK(s2 == 2); CHECK(s3 == 3);
        CHECK(ib.erase(meshroute::InboxKind::dm, s2) == meshroute::InboxEraseResult::erased);
        ib.flush();                                   // the §6 high-water reaches the meta store
        epoch_before = ib.storage_epoch();
        CHECK(epoch_before != 0);
        PullSeqs p; ib.pull(0, 0, pull_cb, &p);
        CHECK(p.seqs == std::vector<uint32_t>{1, 3});
    }
    // ---- POWER CUT: the store objects die; the fake flash and the meta blobs persist ----
    meshroute::SegmentedInboxStore dm2(drecs, dmeta, 4096, 256), ch2(crecs, cmeta, 4096, 256);
    meshroute::Inbox ib2; ib2.on_init(&dm2, &ch2);
    PullSeqs p2; ib2.pull(0, 0, pull_cb, &p2);
    CHECK(p2.seqs == std::vector<uint32_t>{1, 3});    // ★ the records SURVIVED...
    CHECK(p2.bodies.size() == 2);
    if (p2.bodies.size() == 2) { CHECK(p2.bodies[0] == "keep"); CHECK(p2.bodies[1] == "later"); }
    CHECK(ib2.storage_epoch() == epoch_before);       // ★ ...and the epoch did NOT change: a reboot is not a wipe
    // ★ seq never reuses, and the count is 5 not 4 BY DESIGN: the tombstone is an ordinary record that takes a
    //   seq of its own off the same monotonic counter (inbox.h — "deleting consumes a seq of its own; history
    //   keeps a HOLE"). Three messages + one marker = the next free seq is 5, and 2 is permanently a hole.
    CHECK(dm2.persisted_next_seq() == 5);
    // ★ THE DELETE IS STILL A DELETE, and asserted by ABSENCE FROM A REAL pull(), never by a return code.
    CHECK(ib2.erase(meshroute::InboxKind::dm, 2) == meshroute::InboxEraseResult::not_found);
}

TEST_CASE("B134 ★ a partly-committed append is SEALED, so the retry stays reachable across the seam") {
    // The seam's D3 verdict feeding §B135's seal-and-roll, end to end: a buffered write that reports success
    // while only some of it commits must still make the store roll away from the tear. Without D3's growth
    // term the next record would land behind a header that measures long enough to swallow it.
    g_fs.reset();
    MountOnce once;
    FakeSegStore recs(once, kDirDm);
    meshroute::FakeMetaStore meta;
    meshroute::SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    s.append(1, reinterpret_cast<const uint8_t*>("a"), 1);
    s.append(2, reinterpret_cast<const uint8_t*>("b"), 1);
    // append() writes the 6-B header (call #0) then the body (call #1). Tear the BODY: report 8, commit 3.
    g_fs.arm(/*nth*/1, /*report*/8, /*commit*/3);
    CHECK_FALSE(s.append(3, reinterpret_cast<const uint8_t*>("cccccccc"), 8));
    CHECK_FALSE(g_fs.armed());
    s.append(4, reinterpret_cast<const uint8_t*>("d"), 1);          // the retry must NOT land behind the tear
    struct Got { std::vector<uint32_t> seqs; std::vector<std::string> bodies; } g;
    s.read_since(0, [](void* c, uint32_t sq, const uint8_t* r, uint16_t n) {
        auto* gg = static_cast<Got*>(c); gg->seqs.push_back(sq);
        gg->bodies.emplace_back(reinterpret_cast<const char*>(r), n); return true; }, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2, 4});
    CHECK(g.bodies.size() == 3);
    if (g.bodies.size() == 3) CHECK(g.bodies[2] == "d");            // its OWN body, not the next frame's bytes
}

TEST_CASE("B134 ★ a records FORMAT bumps the epoch and preserves next_seq (§10.1 across the ESP32 seam)") {
    // The whole reason the meta lives in NVS and the records in LittleFS: two partitions, two erase domains.
    // A LittleFS format takes the records and NOTHING else, so `next_seq` survives (no seq is ever reused) and
    // the companion sees a CHANGED epoch and re-syncs from 0 instead of reading against a history that is gone.
    g_fs.reset();
    MountOnce once1;
    FakeSegStore recs1(once1, kDirDm);
    meshroute::FakeMetaStore meta;                                  // the "NVS" half — survives the format
    {
        meshroute::SegmentedInboxStore s(recs1, meta, 4096, 256);
        CHECK(s.begin());
        s.append(1, reinterpret_cast<const uint8_t*>("x"), 1);
        CHECK(s.set_next_seq(2));
        CHECK(s.storage_epoch() == 1);
    }
    // ---- reboot onto a CORRUPT LittleFS: D2 formats it, D2 reports it, §10.1 acts on it ----
    g_fs.clean_mount_ok = false;
    MountOnce once2;
    FakeSegStore recs2(once2, kDirDm);
    meshroute::SegmentedInboxStore s2(recs2, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 2);                                 // ★ BUMPED -> the companion re-pulls
    CHECK(s2.persisted_next_seq() == 2);                            // ★ PRESERVED -> seq never reuses
    struct C { int n = 0; } c;
    s2.read_since(0, [](void* p, uint32_t, const uint8_t*, uint16_t) { static_cast<C*>(p)->n++; return true; }, &c);
    CHECK(c.n == 0);                                                // the records really did go
}
