// MeshRoute — test_segmented_inbox_store.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the DURABLE inbox store LOGIC (lib/core/segmented_inbox_store.h) against the RAM fakes
// (test/fake_inbox_storage.h). This is the FIRST host verification of the segmented-log / framing / restore /
// §10.1 wipe-detect logic (it lived Arduino-gated in src/device_inbox_store.h and never ran on the host).
// Covers: append + read_since (oldest-first + since-filter), reboot-restore (§6), drop-oldest eviction,
// the §10.1 records-wipe epoch bump, a full wipe, segment rolls, and torn-record crash-safety (§14).
// The store is an OPAQUE seq'd byte log (Inbox owns the 24-B record format) — so the test payloads are plain
// bytes. NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "fake_inbox_storage.h"   // pulls in segmented_inbox_store.h

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {
struct Got { std::vector<uint32_t> seqs; std::vector<std::string> bodies; };
bool got_cb(void* ctx, uint32_t seq, const uint8_t* rec, uint16_t len) {
    auto* g = static_cast<Got*>(ctx);
    g->seqs.push_back(seq);
    g->bodies.emplace_back(reinterpret_cast<const char*>(rec), len);
    return true;
}
void put(SegmentedInboxStore& s, uint32_t seq, const char* body) {
    s.append(seq, reinterpret_cast<const uint8_t*>(body), static_cast<uint16_t>(std::strlen(body)));
}
// §B135/6 collector — a REAL Inbox::pull over two durable stores (the store-level got_cb above sees raw records,
// which cannot tell a filtered tombstone from a visible message).
struct PullSeqs { std::vector<uint32_t> seqs; std::vector<std::string> bodies; };
bool pull_seq_cb(void* ctx, const InboxEntry& e) {
    auto* p = static_cast<PullSeqs*>(ctx);
    p->seqs.push_back(e.seq);
    p->bodies.emplace_back(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len);
    return true;
}
}  // namespace

TEST_CASE("SegmentedInboxStore: append + read_since oldest-first + since-filter; bodies intact") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/256);
    CHECK(s.begin());
    CHECK(s.storage_epoch() == 1);
    put(s, 1, "a"); put(s, 2, "bb"); put(s, 3, "ccc");
    CHECK(s.count() == 3);

    Got g; CHECK(s.read_since(0, got_cb, &g) == 3);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2, 3});            // oldest-first
    CHECK(g.bodies[0] == "a"); CHECK(g.bodies[2] == "ccc");     // payloads round-trip

    Got g2; CHECK(s.read_since(2, got_cb, &g2) == 1);           // only seq > since
    CHECK(g2.seqs == std::vector<uint32_t>{3});
}

TEST_CASE("SegmentedInboxStore: survives a reboot — records + next_seq + read_cursor restored, epoch unchanged") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); put(s, 2, "y");
        CHECK(s.set_next_seq(3));                              // §6: persist the high-water
        CHECK(s.set_read_cursor(1));
    }
    // "Reboot": a fresh store object over the SAME backends (the flash persists across the object's life).
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.persisted_next_seq() == 3);                      // restored from meta
    CHECK(s2.read_cursor() == 1);
    CHECK(s2.storage_epoch() == 1);                           // NOT bumped — the records survived
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 2);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2});
}

TEST_CASE("SegmentedInboxStore: drop-oldest eviction keeps the newest; survivors are a contiguous suffix, in order") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/96, /*seg*/32);
    CHECK(s.begin());
    for (uint32_t i = 1; i <= 30; ++i) { char b[8]; std::snprintf(b, sizeof b, "m%u", i); put(s, i, b); }

    Got g; const uint16_t n = s.read_since(0, got_cb, &g);
    CHECK(n > 0);
    CHECK(n < 30);                                            // some were evicted (over the byte cap)
    for (size_t i = 1; i < g.seqs.size(); ++i) CHECK(g.seqs[i] == g.seqs[i - 1] + 1);   // contiguous + increasing
    CHECK(g.seqs.back() == 30);                               // newest retained
    CHECK(g.seqs.front() > 1);                                // oldest evicted
    CHECK(s.count() >= n);     // count() is incremented on append, NOT decremented on drop -> an upper bound (diag only, per the contract)
}

TEST_CASE("SegmentedInboxStore: §10.1 records-wipe bumps the epoch + PRESERVES next_seq (seq never reuses)") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); put(s, 2, "y");
        CHECK(s.set_next_seq(3));                             // high-water persisted to the (separate) meta store
        CHECK(s.storage_epoch() == 1);
    }
    recs.wipe();                                              // records-store format/wipe — the meta is a SEPARATE object, survives
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 2);                          // §10.1: BUMPED (records came up empty, meta said next_seq>1)
    CHECK(s2.persisted_next_seq() == 3);                     // next_seq PRESERVED -> seq never reuses
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 0);         // the records are gone
}

TEST_CASE("SegmentedInboxStore: FULL wipe (records + meta) -> fresh epoch + seq restart at 1") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); CHECK(s.set_next_seq(2));
    }
    recs.wipe(); meta.wipe();                                // EVERYTHING lost (e.g. a bootloader re-flash erasing both)
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.persisted_next_seq() == 1);                    // fresh -> seq restarts at 1
    CHECK(s2.storage_epoch() == 1);                         // fresh epoch — a CHANGE from the app's last-seen (2) -> it re-pulls
}

TEST_CASE("SegmentedInboxStore: §GapA-durable record-format VERSION mismatch (v2->v3) -> records wiped + epoch bumped + next_seq preserved") {
    // The §GapA-durable origin_layer record-header growth bumps the store version (v2 -> v3, after §S5's team_id v2). On the
    // next boot the OLD-format records can't be parsed by the new deserializer, so begin() must WIPE them, BUMP the epoch
    // (companion re-pulls from 0), and KEEP next_seq (monotonic — seq never reuses). Meta layout: magic@0, version@4.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); put(s, 2, "y");
        CHECK(s.set_next_seq(3));                             // high-water persisted to the (separate) meta store
        CHECK(s.storage_epoch() == 1);
    }
    CHECK(recs.any_segments(nullptr));                              // the old-format records ARE present on "flash"
    meta.poke_u16(4, 2);                                     // simulate the PRIOR store version v2 (§S5 team_id; record header grew under it -> +origin_layer v3)
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 2);                          // BUMPED -> the companion sees the wipe + re-syncs
    CHECK(s2.persisted_next_seq() == 3);                     // next_seq PRESERVED (never reuses a seq)
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 0);         // the unparseable old records are gone
    CHECK_FALSE(recs.any_segments(nullptr));                        // records store wiped
    // A SUBSEQUENT reboot at the new version is stable (no repeated wipe/bump).
    put(s2, 3, "z"); CHECK(s2.set_next_seq(4));
    SegmentedInboxStore s3(recs, meta, 4096, 256);
    CHECK(s3.begin());
    CHECK(s3.storage_epoch() == 2);                          // NOT re-bumped (version now matches)
    Got g3; CHECK(s3.read_since(0, got_cb, &g3) == 1);
}

TEST_CASE("SegmentedInboxStore: records span multiple segments (roll), all readable oldest-first") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/24);   // tiny segs -> forced rolls; big cap -> no eviction
    CHECK(s.begin());
    for (uint32_t i = 1; i <= 12; ++i) { char b[8]; std::snprintf(b, sizeof b, "r%u", i); put(s, i, b); }

    CHECK(recs.live_segments() > 1);                         // it actually rolled across segments
    Got g; CHECK(s.read_since(0, got_cb, &g) == 12);         // every record readable across segments
    for (uint32_t i = 0; i < 12; ++i) CHECK(g.seqs[i] == i + 1);   // in order across the ring
}

TEST_CASE("SegmentedInboxStore: a torn record at a segment tail is skipped (crash-safety §14)") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 256);            // big seg -> records 1,2 both land in segment 0 (the head)
    CHECK(s.begin());
    put(s, 1, "a"); put(s, 2, "b");
    // Simulate a torn append: a full 6-byte frame header claiming a 100-byte frame, with no body following.
    const uint8_t torn[6] = { 100, 0,  9, 0, 0, 0 };        // framed_len=100, seq=9 — but the body never arrived
    recs.seg_append(0, torn, 6);
    Got g; CHECK(s.read_since(0, got_cb, &g) == 2);          // the 2 valid records; the torn seq=9 is NOT visited
    CHECK(g.seqs == std::vector<uint32_t>{1, 2});
}

TEST_CASE("SegmentedInboxStore: M2 — a torn meta (seg_count==0 / head_seg>=seg_count) is REFUSED over live records, no DBZ/hang") {
    // S2 flash-validation regression: load_meta() must reject a torn meta BEFORE its fields divide (% seg_count ->
    // DBZ when seg_count==0) or bound the ring walk (i == head_seg never true when head_seg >= seg_count ->
    // infinite boot loop). Meta layout offsets (private struct, documented there): head_seg@6, tail_seg@8, seg_count@10.
    //
    // ⛔⛔ CORRECTED TWICE, AND BOTH CORRECTIONS ARE RECORDED BECAUSE THE SECOND ONE CAUGHT THE FIRST BEING
    //    HALF-RIGHT.
    //    (1) 2026-08-29, [[B134]] QG round 2 — THIS CASE USED TO PIN THE DEFECT. Its name ended *"-> fresh"* and
    //        it asserted `persisted_next_seq() == 1` / `storage_epoch() == 1` after corrupting the meta of a store
    //        THAT HAD JUST WRITTEN A RECORD: a re-init over live records resets head/tail to 0/0 (every segment
    //        past the first becomes invisible while physically present) and resets next_seq to 1 (the next append
    //        REUSES sequences the companion has filed). Arms (a)-(c) became refusals.
    //    (2) 2026-08-29, QG round 3 — the refusal was right but the CODE was wrong, and a fourth arm added by (1)
    //        still pinned an unsafe result. A `poke_u16` meta is READ and STRUCTURALLY WRONG, which is
    //        `meta_corrupt`, ⛔ not `meta_lost_over_records` (that is for a meta that is ABSENT while records
    //        exist) — the two carry different operator responses. And arm (d) asserted that a corrupt meta over
    //        an EMPTY store still mounts fresh; it does not, and must not: after a `prep-restart` the records are
    //        empty BY DESIGN while the meta still holds the sequence high-water and the epoch.
    //    ⇒ (a)-(c) assert `meta_corrupt` over live records, (d) asserts it over an EMPTY store, and (e) is new —
    //      a genuinely ABSENT meta over live records, so both codes are pinned by this case rather than one.
    //    What the case was always REALLY for — that a torn meta is rejected BEFORE its fields divide or bound a
    //    loop — is unchanged, and is still proved by these calls returning at all: a DBZ or an infinite ring walk
    //    never gets here.

    // (a) seg_count == 0 would divide-by-zero in the `% seg_count` ring walk.
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "x"); CHECK(s.set_next_seq(9)); }
        CHECK(meta.saved());
        meta.poke_u16(10, 0);                                    // corrupt seg_count -> 0
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK_FALSE(s2.begin());                                 // must NOT hard-fault (DBZ) — and must NOT re-init
        CHECK(s2.mount_fault() == SegMountFault::meta_corrupt);   // READ and wrong, not absent
        CHECK(recs.any_segments(nullptr));                       // ★ the records were NOT touched by the refusal
    }
    // (b) head_seg >= seg_count would make the `i == head_seg` ring-walk terminator unreachable (infinite loop).
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "y"); CHECK(s.set_next_seq(9)); }
        const uint16_t sc = meta.peek_u16(10);                   // the valid seg_count == ring_segs()
        CHECK(sc > 0);
        meta.poke_u16(6, sc);                                    // head_seg = seg_count (out of range)
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK_FALSE(s2.begin());                                 // must RETURN (no infinite loop) — and must NOT re-init
        CHECK(s2.mount_fault() == SegMountFault::meta_corrupt);
    }
    // (c) tail_seg >= seg_count is likewise rejected.
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "z"); CHECK(s.set_next_seq(9)); }
        const uint16_t sc = meta.peek_u16(10);
        meta.poke_u16(8, static_cast<uint16_t>(sc + 5));         // tail_seg out of range
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK_FALSE(s2.begin());
        CHECK(s2.mount_fault() == SegMountFault::meta_corrupt);
    }
    // (d) ★★★ CORRUPT META OVER AN **EMPTY** RECORD STORE ALSO REFUSES — the arm QG round 3 caught pinning the
    //     unsafe result. An empty record store is the NORMAL state after `prep-restart`, and the meta is exactly
    //     what still holds the sequence high-water, the epoch and the read cursor there. Re-initialising would
    //     reuse sequences AND reset the very epoch bump the companion uses to learn the wipe happened.
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); CHECK(s.set_next_seq(9)); }
        CHECK_FALSE(recs.any_segments(nullptr));                 // no records — and it STILL must not start over
        meta.poke_u16(10, 0);                                    // same corruption as arm (a)
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK_FALSE(s2.begin());
        CHECK(s2.mount_fault() == SegMountFault::meta_corrupt);
    }
    // (e) ★★ ...AND THE OTHER CODE, so this case pins BOTH: a meta that is genuinely ABSENT while records exist
    //     is `meta_lost_over_records` — a different fault with a different remedy (that one costs the history).
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "x"); CHECK(s.set_next_seq(9)); }
        meta.wipe();                                             // ABSENT, not corrupt
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK_FALSE(s2.begin());
        CHECK(s2.mount_fault() == SegMountFault::meta_lost_over_records);
    }
}

// ============================================================================================================
// ★★ §B135 — TORN-FRAME RECOVERY. The pre-existing hole QA found on the [[B133]] delete slice: the frame header
// and the body are two seg_append calls, so a failure between them leaves a header claiming more bytes than are
// present. A torn tail ALONE is harmless (read_since stops at it) — the defect is THE NEXT APPEND, which lands
// behind the tear and is then consumed AS the torn frame's body, making a physically-present record unreachable.
// ⚠ WHY THE OLD SUITE COULD NOT SEE IT, recorded so the instrument is never trusted again: the case above
// ("a torn record at a segment tail is skipped") hand-writes a torn tail and NEVER RETRIES AFTER IT, and
// RamInboxStore's `fail_append` fails before writing anything, so it cannot produce a tear at all.
// Every case below asserts the OBSERVABLE side effect — what a later read/pull can still reach — never a
// return code alone.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: §B135/1 — header written, body FAILED -> the tear is sealed, the retry stays reachable") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/256);
    CHECK(s.begin());
    put(s, 1, "a"); put(s, 2, "b");
    recs.fail_mid_frame(/*nth*/1, /*partial*/0);              // #0 = the 6-B header lands; #1 = the body fails outright
    CHECK_FALSE(s.append(3, reinterpret_cast<const uint8_t*>("c"), 1));   // the tear
    CHECK_FALSE(recs.fault_armed());                          // ★ the injector really FIRED (never assume a fault test faulted)
    // ⑤ THE RETRY. Without the seal this record lands behind the torn header and becomes unreachable.
    put(s, 4, "d");
    Got g; const uint16_t n = s.read_since(0, got_cb, &g);
    CHECK(n == 3);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2, 4});          // ★ 1 and 2 survive, 4 is READABLE, the torn 3 is not invented
    CHECK(g.bodies[2] == "d");                                // and its body is its OWN, not the next frame's header bytes
}

TEST_CASE("SegmentedInboxStore: §B135/2 — a PARTIAL body is sealed too (a short frame is still a torn frame)") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    put(s, 1, "a");
    recs.fail_mid_frame(/*nth*/1, /*partial*/3);              // 3 of the 8 body bytes land, then the write dies
    CHECK_FALSE(s.append(2, reinterpret_cast<const uint8_t*>("bbbbbbbb"), 8));
    CHECK_FALSE(recs.fault_armed());
    put(s, 3, "c");
    Got g; CHECK(s.read_since(0, got_cb, &g) == 2);
    CHECK(g.seqs == std::vector<uint32_t>{1, 3});
    CHECK(g.bodies[1] == "c");
}

TEST_CASE("SegmentedInboxStore: §B135/3 — a failure IMMEDIATELY AFTER a rotation leaves the older segments readable") {
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/24);   // tiny segments -> every couple of records rolls
    CHECK(s.begin());
    for (uint32_t i = 1; i <= 6; ++i) { char b[8]; std::snprintf(b, sizeof b, "r%u", i); put(s, i, b); }
    Got before; const uint16_t n_before = s.read_since(0, got_cb, &before);
    CHECK(n_before == 6);
    recs.fail_mid_frame(/*nth*/0, /*partial*/2);              // the roll happens, then the HEADER itself tears in the fresh head
    CHECK_FALSE(s.append(7, reinterpret_cast<const uint8_t*>("r7"), 2));
    CHECK_FALSE(recs.fault_armed());
    Got after; CHECK(s.read_since(0, got_cb, &after) == 6);   // ★ nothing previously readable was lost or reordered
    CHECK(after.seqs == before.seqs);
    put(s, 8, "r8");                                          // and the store keeps working
    Got g; CHECK(s.read_since(7, got_cb, &g) == 1);
    CHECK(g.seqs == std::vector<uint32_t>{8});
}

TEST_CASE("SegmentedInboxStore: §B135/4 — a REBOOT between the two writes re-arms the seal (the RAM flag is not the truth)") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "a"); put(s, 2, "b");
        recs.fail_mid_frame(1, 0);
        CHECK_FALSE(s.append(3, reinterpret_cast<const uint8_t*>("c"), 1));
        CHECK(s.set_next_seq(4));
        CHECK_FALSE(recs.fault_armed());
    }                                                          // POWER CUT — `_head_sealed` dies with the object
    SegmentedInboxStore s2(recs, meta, 4096, 256);             // the same "flash", a fresh store
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 1);                            // a tear is NOT a wipe: no epoch bump, no re-pull storm
    CHECK(s2.persisted_next_seq() == 4);
    put(s2, 4, "d");                                           // ★ the first post-reboot append MUST NOT land behind the tear
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 3);
    CHECK(g.seqs == std::vector<uint32_t>{1, 2, 4});
    CHECK(g.bodies[2] == "d");
}

TEST_CASE("SegmentedInboxStore: §B135/5 — a CLEAN store is never sealed (the recovery must not cost a segment per boot)") {
    // The vacuity control for the two arms above: if `head_tail_torn()` reported true on a healthy log, every
    // case here would still pass (records stay readable) while the ring burned a segment on every single boot.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "a"); put(s, 2, "b");
    }
    const size_t bytes_before = recs.seg_bytes(0);
    CHECK(bytes_before > 0);
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    put(s2, 3, "c");
    CHECK(recs.live_segments() == 1);                          // ★ still ONE segment — the clean reboot did not roll
    CHECK(recs.seg_bytes(0) > bytes_before);                   // and record 3 went into it
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 3);
}

TEST_CASE("SegmentedInboxStore: §B135/6 ★ a tombstone reports `erased` ONLY when a real pull() hides the target") {
    // ★★ THE CASE THAT CLOSES THE CLASS. [[B133]]'s `erase()` is composed from read_since + append, so a torn
    // append is a torn DELETE: the failure mode is `erased` returned while the message is still readable — the
    // fifth "contract event asserting a physical act" in this project. Asserted here on the DURABLE store and by
    // OBSERVABLE ABSENCE from a real pull(), never by the return code alone.
    FakeSegmentStore drecs, crecs; FakeMetaStore dmeta, cmeta;
    SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.enabled());
    const uint32_t s1 = ib.record_dm(2, 0xABCD, 7, 0, reinterpret_cast<const uint8_t*>("keep"),   4, 1000);
    const uint32_t s2 = ib.record_dm(2, 0xABCD, 8, 0, reinterpret_cast<const uint8_t*>("delete"), 6, 1001);
    CHECK(s1 == 1); CHECK(s2 == 2);

    // ARM 1 — the tombstone's own write TEARS. The verdict must be io_error AND the target must still be there.
    drecs.fail_mid_frame(/*nth*/1, /*partial*/0);
    CHECK(ib.erase(InboxKind::dm, s2) == InboxEraseResult::io_error);
    CHECK_FALSE(drecs.fault_armed());
    PullSeqs p1; ib.pull(0, 0, pull_seq_cb, &p1);
    CHECK(p1.seqs == std::vector<uint32_t>{1, 2});             // ★ a FAILED delete hides NOTHING

    // ARM 2 — the retry, over the sealed tail. Now `erased` must mean the record is really gone from pull().
    CHECK(ib.erase(InboxKind::dm, s2) == InboxEraseResult::erased);
    PullSeqs p2; ib.pull(0, 0, pull_seq_cb, &p2);
    CHECK(p2.seqs == std::vector<uint32_t>{1});                // ★ target gone, the OTHER record untouched
    CHECK(p2.bodies.size() == 1);
    if (p2.bodies.size() == 1) CHECK(p2.bodies[0] == "keep");

    // ARM 3 — and it stays gone across a reboot (a tombstone that only lives in RAM would pass arm 2 and fail here).
    Inbox ib2; ib2.on_init(&dm, &ch);
    PullSeqs p3; ib2.pull(0, 0, pull_seq_cb, &p3);
    CHECK(p3.seqs == std::vector<uint32_t>{1});
}

// ============================================================================================================
// [[B134]] — the two behaviours the durable ESP32 backend needed from this store, and that only its nRF52 TWIN
// had. Both are asserted here, where the logic lives, rather than in the ESP32 seam's own file.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: [[B134]] wipe() drops every record AND resets the ring bookkeeping it just emptied") {
    // ⛔ The base `InboxStore::wipe()` is a no-op, which was right while this logic had no device instance (a RAM
    //    store is cleared by the reboot that follows) and is WRONG the moment ESP32 mounts it on real flash:
    //    `factory_reset confirm` and `prep-restart` both call wipe() and would otherwise have left the entire
    //    history on the medium. ⚠ Its nRF52 twin (src/device_inbox_store.h:80) erases the segments and clears the
    //    seal only, getting away with the stale head/tail/_total because BOTH callers reboot immediately — an
    //    unstated dependency on the caller (C2), which is why this one puts the bookkeeping back too.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/32);
    CHECK(s.begin());
    for (uint32_t i = 1; i <= 10; ++i) { char b[8]; std::snprintf(b, sizeof b, "w%u", i); put(s, i, b); }
    CHECK(recs.live_segments() > 1);                          // it really rolled, so head != tail before the wipe
    CHECK(s.set_next_seq(11));

    s.wipe();
    CHECK_FALSE(recs.any_segments(nullptr));                         // ★ every segment gone
    Got g; CHECK(s.read_since(0, got_cb, &g) == 0);
    CHECK(s.persisted_next_seq() == 11);                      // ★ next_seq is NOT reset — a wipe must never reuse a seq
    // ★ AND THE STORE IS IMMEDIATELY USABLE FROM THE TOP OF THE RING, which is what the bookkeeping reset buys.
    //   A stale head/tail would resume writing at whatever segment the ring had rolled to — leaving the store
    //   describing bytes that no longer exist and evicting against them — so assert the POSITION, not merely
    //   that the record can be read back (which a stale head would also satisfy).
    put(s, 11, "after");
    CHECK(recs.live_segments() == 1);                         // ★ exactly one segment holds bytes again...
    CHECK(recs.seg_bytes(0) > 0);                             // ★ ...and it is segment 0: head/tail really reset
    Got g2; CHECK(s.read_since(0, got_cb, &g2) == 1);
    CHECK(g2.seqs == std::vector<uint32_t>{11});
    CHECK(g2.bodies[0] == "after");
    // ⛔ RE-DERIVED 2026-08-29 ([[B134]] QG round 5) — THE EXPECTED EPOCH IS 3, NOT 2, AND THE ARITHMETIC IS THE
    //   POINT. The bump for a DELIBERATE wipe now happens inside `wipe()` itself (1 -> 2), exactly once, instead
    //   of being left to the next boot's §10.1 detect — which had no way to know it had already fired and so
    //   re-bumped on every reboot. This case then RE-POPULATES the store (the `put` above marks it non-empty
    //   again), so the `recs.wipe()` below is a SECOND and genuinely EXTERNAL loss, which the §10.1 arm bumps
    //   for on its own account: 2 -> 3. Two real transitions, two increments — never a per-boot ratchet.
    const uint32_t epoch_after_wipe = s.storage_epoch();
    CHECK(epoch_after_wipe == 2);                             // ★ the deliberate wipe bumped, at the wipe
    recs.wipe(); /* an EXTERNAL loss of the records written since */
    SegmentedInboxStore s2(recs, meta, 4096, 32);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 3);                           // ★ the external loss bumped, once
    CHECK(s2.persisted_next_seq() >= 11);
    // ★★ AND THE STABILITY PROPERTY THIS ROUND EXISTS FOR: booting the same empty store again changes NOTHING.
    SegmentedInboxStore s3(recs, meta, 4096, 32);
    CHECK(s3.begin());
    CHECK(s3.storage_epoch() == 3);
}

TEST_CASE("SegmentedInboxStore: [[B134]] set_read_cursor to the SAME value writes NOTHING (wear), a real advance persists") {
    // InternalFS self-heal Part 3, and the twin at src/device_inbox_store.h has always had it: a companion fires
    // mark_read at its own cadence during a pull session, so a no-op rewrite is pure flash wear plus a widened
    // reset-during-write window — on a tree that has already been bricked by NV corruption once.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    CHECK(s.set_read_cursor(5));
    const int after_first = meta.saves();
    CHECK(s.read_cursor() == 5);

    CHECK(s.set_read_cursor(5));                              // ★ the same value again
    CHECK(meta.saves() == after_first);                       // ★ ...wrote nothing
    CHECK(s.read_cursor() == 5);

    CHECK(s.set_read_cursor(9));                              // a REAL advance is user/app-commanded -> persist now
    CHECK(meta.saves() == after_first + 1);
    CHECK(s.read_cursor() == 9);
}

// ============================================================================================================
// ★★★ [[B134]] QG BLOCKER 1 — THE UNPERSISTED TOPOLOGY. Every case below asserts what a REBOOT can still reach,
// never a return code alone: the defect class is "append said yes and the record was gone", which no return
// value can be trusted to describe.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: §B134b/1 ★★★ a ROTATION whose meta save fails REFUSES the append (it is not acknowledged)") {
    // ⛔ THE REPRODUCTION. Before this fix the rotation moved the head, wrote the record and returned TRUE while
    //    the persisted meta still pointed at the OLD head — so the next boot's tail..head walk excluded the new
    //    segment and an ACKNOWLEDGED record was GONE. For a §3.5 tombstone that is the deleted message COMING
    //    BACK: [[B134]] itself, resurrected through its own fix's seam.
    FakeSegmentStore recs; FakeMetaStore meta;
    // ⚠ seg=16 is chosen so the SECOND record genuinely ROLLS: framed("first")=11 fits, +framed("second")=12
    //   does not. A size where both fit would make this case pass with no rotation at all — vacuous.
    SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/16);
    CHECK(s.begin());
    put(s, 1, "first");
    meta.fail_saves(true);                                    // the medium stops accepting metadata
    CHECK_FALSE(s.append(2, reinterpret_cast<const uint8_t*>("second"), 6));   // ★ REFUSED, not acknowledged
    meta.fail_saves(false);

    // ★ AND THE REBOOT AGREES WITH THE VERDICT: record 1 survives, record 2 was never claimed.
    SegmentedInboxStore s2(recs, meta, 4096, 16);
    CHECK(s2.begin());
    Got g; s2.read_since(0, got_cb, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1});
    CHECK(g.bodies[0] == "first");
}

TEST_CASE("SegmentedInboxStore: §B134b/2 ★★ a refused rotation RECOVERS — a later successful save resumes normal service") {
    // A latch that never clears would turn one transient flash failure into a permanently dead inbox. The retry
    // lives at the TOP of append(), so the very next attempt after the medium recovers re-persists the topology.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 16);              // seg=16 -> the 2nd record must roll (see §B134b/1)
    CHECK(s.begin());
    put(s, 1, "first");
    meta.fail_saves(true);
    CHECK_FALSE(s.append(2, reinterpret_cast<const uint8_t*>("second"), 6));
    meta.fail_saves(false);                                   // the medium recovers
    CHECK(s.append(3, reinterpret_cast<const uint8_t*>("third"), 5));   // ★ accepted again

    SegmentedInboxStore s2(recs, meta, 4096, 16);             // and the reboot can reach BOTH survivors
    CHECK(s2.begin());
    Got g; s2.read_since(0, got_cb, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1, 3});
}

TEST_CASE("SegmentedInboxStore: §B134b/3 ★★★ a TOMBSTONE under a failed meta save reports io_error — the message must NOT come back") {
    // ★ The blocker stated in the terms §3.5 actually renders. `erase()` is composed from read_since + append, so
    //   a refused append MUST surface as `io_error` (a loud DELETE FAILED) and the record must still be in a real
    //   pull() — both before AND after a reboot. A `erased` here is the deletion that un-deletes itself.
    FakeSegmentStore drecs, crecs; FakeMetaStore dmeta, cmeta;
    // ⚠ seg=64, not 32: an Inbox record is a 32-B header + body, so framed("target")=44 would not even FIT a
    //   32-byte segment (append refuses outright and the case would pass for the wrong reason). At 64 the record
    //   fits and the 38-byte tombstone frame then forces the ROLL this case is about.
    SegmentedInboxStore dm(drecs, dmeta, 4096, 64), ch(crecs, cmeta, 4096, 64);
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.enabled());
    const uint32_t s1 = ib.record_dm(2, 0xABCD, 7, 0, reinterpret_cast<const uint8_t*>("target"), 6, 1000);
    CHECK(s1 == 1);
    dmeta.fail_saves(true);                                   // the tombstone's append will have to roll, and cannot persist
    CHECK(ib.erase(InboxKind::dm, s1) == InboxEraseResult::io_error);
    dmeta.fail_saves(false);
    PullSeqs p1; ib.pull(0, 0, pull_seq_cb, &p1);
    CHECK(p1.seqs == std::vector<uint32_t>{1});               // ★ a FAILED delete hides nothing...

    SegmentedInboxStore dm2(drecs, dmeta, 4096, 64), ch2(crecs, cmeta, 4096, 64);
    Inbox ib2; ib2.on_init(&dm2, &ch2);
    PullSeqs p2; ib2.pull(0, 0, pull_seq_cb, &p2);
    CHECK(p2.seqs == std::vector<uint32_t>{1});               // ★ ...and still hides nothing after a reboot
}

TEST_CASE("SegmentedInboxStore: §B134b/2b ★★ a retry that ALSO fails refuses again — 'we tried, carry on' is not recovery") {
    // ★ The companion control to §B134b/2, and it attacks a DIFFERENT line: /2 proves the latch CLEARS when the
    //   medium recovers, this proves the retry's own RESULT is honoured. A retry that is attempted and then
    //   ignored puts the store straight back under the unpersisted topology it just refused to write beneath.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 16);              // seg=16 -> the 2nd record must roll (see §B134b/1)
    CHECK(s.begin());
    put(s, 1, "first");
    meta.fail_saves(true);
    CHECK_FALSE(s.append(2, reinterpret_cast<const uint8_t*>("second"), 6));   // the rotation is refused + latched
    // ⛔ STILL FAILING. This append needs no roll of its own, so the ONLY thing that may stop it is the latched
    //    retry — and the retry still cannot persist, so the append must be refused too.
    CHECK_FALSE(s.append(3, reinterpret_cast<const uint8_t*>("third"), 5));
    meta.fail_saves(false);
    CHECK(s.append(4, reinterpret_cast<const uint8_t*>("fourth"), 6));         // and only NOW does it resume

    SegmentedInboxStore s2(recs, meta, 4096, 16);
    CHECK(s2.begin());
    Got g; s2.read_since(0, got_cb, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1, 4});             // neither refused record was kept or acknowledged
}

TEST_CASE("SegmentedInboxStore: §B134b/4b ★★ a §10.1 epoch bump that will not persist FAILS THE MOUNT") {
    // The records were wiped and the epoch must tell the companion so. If that bump never reaches the medium, the
    // app keeps its cursors against a history that is gone and silently never re-pulls — a wipe nobody learns
    // about. ⇒ the mount fails rather than running with an epoch that only exists in RAM.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); put(s, 2, "y");
        CHECK(s.set_next_seq(3));
    }
    recs.wipe();                                              // the records went; the (separate) meta survives
    meta.fail_saves(true);
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK_FALSE(s2.begin());                                  // ★ the §10.1 bump could not persist -> fail loud
    // ★ THE CONTROL: with the medium healthy the very same reboot mounts and bumps exactly once.
    meta.fail_saves(false);
    SegmentedInboxStore s3(recs, meta, 4096, 256);
    CHECK(s3.begin());
    CHECK(s3.storage_epoch() == 2);
    CHECK(s3.persisted_next_seq() == 3);
}

TEST_CASE("SegmentedInboxStore: §B134b/4c ★ a cap-eviction whose erase FAILS does not walk the tail past live records") {
    // The byte-cap drop-oldest loop. If the erase fails and the tail advances anyway, the segment's records are
    // still physically on the medium but are no longer inside the tail..head walk — present and unreachable, the
    // §B135 outcome reached through the eviction path — and `_total` under-counts the medium so the cap stops
    // capping. ⇒ a failed erase STOPS the eviction and leaves the tail where it is.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, /*cap*/96, /*seg*/32);  // 4-segment ring; the cap bites before the ring laps
    CHECK(s.begin());
    recs.fail_erase_of(0);                                    // the tail segment refuses erasure
    // ⚠ EXACTLY 12 RECORDS, AND THE COUNT IS LOAD-BEARING. ~3 records fill a 32-B segment, so the 12th pushes
    //   `_total` past the 96-B cap and fires the EVICTION loop on tail 0 — while the ring (4 segments) is still
    //   NOT full, so the ROLL's own tail-drop has not run yet. More records and the ring laps, the roll evicts
    //   segment 0 through a different line, and this case would stop being about the eviction path at all.
    for (uint32_t i = 1; i <= 12; ++i) { char b[8]; std::snprintf(b, sizeof b, "e%u", i); put(s, i, b); }
    Got g; s.read_since(0, got_cb, &g);
    CHECK(g.seqs.size() > 0);
    CHECK(g.seqs.front() == 1);                               // ★ the OLDEST record is still reachable: the tail
                                                              //   never walked past the segment it could not erase
    recs.fail_erase_of(-1);
}

// ============================================================================================================
// ★★★ [[B134]] QG ROUND 2 — INVALID/MISSING METADATA OVER EXISTING RECORDS. QG reproduced: 6 persisted segments
// + failed meta -> begin() succeeded as "fresh" (next=1, epoch=1), ONE record visible, and the next append
// reused seq 1. Both halves of the corruption contract violated at once — records hidden AND sequence reuse.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: §B134c/1 ★★★ meta LOST over a MULTI-SEGMENT log refuses the mount — no hiding, no seq reuse") {
    FakeSegmentStore recs; FakeMetaStore meta;
    std::vector<uint32_t> before;
    {
        SegmentedInboxStore s(recs, meta, /*cap*/4096, /*seg*/24);   // tiny segs -> many segments, big cap -> no eviction
        CHECK(s.begin());
        for (uint32_t i = 1; i <= 12; ++i) { char b[8]; std::snprintf(b, sizeof b, "r%u", i); put(s, i, b); }
        CHECK(s.set_next_seq(13));
        CHECK(recs.live_segments() > 1);                      // ★ the log really does span segments (the repro's shape)
        Got g; s.read_since(0, got_cb, &g); before = g.seqs;
        CHECK(before.size() == 12);
    }
    meta.wipe();                                              // the metadata is GONE; every record is still on the medium
    SegmentedInboxStore s2(recs, meta, 4096, 24);
    CHECK_FALSE(s2.begin());                                  // ★ REFUSED — never silently re-initialised
    CHECK(s2.mount_fault() == SegMountFault::meta_lost_over_records);

    // ★★ AND THE TWO CONTRACT VIOLATIONS ARE ASSERTED DIRECTLY, not merely implied by the return code.
    // (a) NOTHING IS HIDDEN: the refusal did not touch the medium, so a store that CAN read it still sees all 12.
    //     (The old behaviour reset head/tail to 0/0 and showed only the first segment's records.)
    CHECK(recs.any_segments(nullptr));
    // (b) NO SEQUENCE REUSE: the refused store is inert, so nothing can be appended over the live log at all.
    CHECK_FALSE(s2.append(1, reinterpret_cast<const uint8_t*>("clash"), 5));
    Got after; s2.read_since(0, got_cb, &after);
    CHECK(after.seqs.empty());                                // a refused store reads nothing and writes nothing
    // ★ THE RECOVERY IS THE OPERATOR'S, EXPLICITLY: a wipe (factory_reset) makes the store mountable again, and
    //   only then — after the history was deliberately destroyed — does seq restart.
    for (uint16_t i = 0; i < 200; ++i) recs.seg_erase(i);
    SegmentedInboxStore s3(recs, meta, 4096, 24);
    CHECK(s3.begin());
    CHECK(s3.mount_fault() == SegMountFault::none);
}

TEST_CASE("SegmentedInboxStore: §B134c/2 ★★ a GENUINELY fresh store still mounts (the 19.7 reflash-wipes-once side)") {
    // ⛔ THE DISCRIMINATOR MUST NOT TURN FIRST BOOT INTO A REFUSED MOUNT. No meta AND no records is the ordinary
    //    first-boot / post-format state, and it has to come up clean — otherwise every freshly flashed node would
    //    boot with a dead inbox and the bench's "reflash wipes once" step would fail on a working device.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    CHECK(s.mount_fault() == SegMountFault::none);
    CHECK(s.persisted_next_seq() == 1);
    CHECK(s.storage_epoch() == 1);
    put(s, 1, "hello");
    Got g; CHECK(s.read_since(0, got_cb, &g) == 1);
    // ★ AND THE FORMAT-ON-CORRUPT PATH STAYS OPEN TOO: a mount that had to FORMAT reports records present as
    //   impossible, so `formatted` keeps the fresh branch reachable rather than deadlocking it against the
    //   very records the format just erased.
    FakeSegmentStore recs2; FakeMetaStore meta2;
    recs2.wipe(/*report_formatted=*/true);                    // the next mount() reports "I had to format"
    SegmentedInboxStore s2(recs2, meta2, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.mount_fault() == SegMountFault::none);
}

TEST_CASE("SegmentedInboxStore: §B134c/3 ★★★ a records store that cannot ANSWER fails the mount (not 'empty')") {
    // ⛔ The other door into the silent re-initialise: if the inspection itself fails and is reported as "no
    //    records", a live store with a transient FS error is re-initialised exactly as if it were fresh.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "live"); CHECK(s.set_next_seq(2));
    }
    meta.wipe();
    recs.fail_inspection(true);                               // the store cannot say whether it holds anything
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK_FALSE(s2.begin());
    CHECK(s2.mount_fault() == SegMountFault::records_uninspectable);   // ★ named as the inspection failure it is,
                                                                      //   ⛔ never mistaken for meta_lost/fresh
    // ★ THE CONTROL: with the inspection working, the SAME medium gives the meta_lost verdict — so the case above
    //   is about the inspection and not merely about a broken fixture.
    recs.fail_inspection(false);
    SegmentedInboxStore s3(recs, meta, 4096, 256);
    CHECK_FALSE(s3.begin());
    CHECK(s3.mount_fault() == SegMountFault::meta_lost_over_records);
}

TEST_CASE("SegmentedInboxStore: §B134d/1 ★★★ THE prep-restart SCENARIO — corrupt meta over the empty store it leaves") {
    // ⛔ DRIVEN THROUGH THE REAL SEQUENCE, not a hand-built state, because that is the point: `prep-restart` wipes
    //    the RECORDS and deliberately keeps the META (next_seq, epoch, read cursor). An empty record store is
    //    therefore NORMAL here, and treating a corrupt meta as "first boot" in that state reuses sequences AND
    //    resets the epoch bump bench 19.8 relies on to see the wipe at all.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "a"); put(s, 2, "b"); put(s, 3, "c");
        CHECK(s.set_next_seq(4));
        CHECK(s.set_read_cursor(2));
        CHECK(s.wipe());                                      // ← the real prep-restart step
    }
    CHECK_FALSE(recs.any_segments(nullptr));                  // records gone by design; meta deliberately kept
    // ★ THE CONTROL FIRST: with the meta intact this is the ordinary post-prep-restart boot — it mounts, the
    //   high-water survives, and §10.1 bumps the epoch exactly once so the companion learns the history went.
    {
        SegmentedInboxStore ok_boot(recs, meta, 4096, 256);
        CHECK(ok_boot.begin());
        CHECK(ok_boot.persisted_next_seq() == 4);             // ★ sequences do NOT restart
        CHECK(ok_boot.storage_epoch() == 2);                  // ★ the wipe is announced, exactly once
    }
    // ★★ NOW CORRUPT THE META IN THAT SAME STATE. The old code called this "fresh" — next_seq back to 1 over a
    //    sequence space the companion has already filed, and the epoch reset so the wipe is concealed.
    const uint16_t sc_before = meta.peek_u16(10);              // keep the real ring size so the repair is exact
    meta.poke_u16(10, 0);
    SegmentedInboxStore bad(recs, meta, 4096, 256);
    CHECK_FALSE(bad.begin());
    CHECK(bad.mount_fault() == SegMountFault::meta_corrupt);
    // ⛔ CORRECTED 2026-08-29 ([[B134]] QG round 5) — THE ROUND-4 "MEDIUM-SIDE REPAIR" PROVED NOTHING. It built a
    //    BRAND-NEW `FakeMetaStore` + `FakeSegmentStore`, wrote 4 into them and read 4 back — a statement about two
    //    objects that had never been near the corruption. ⇒ repair THIS meta, remount THIS pair, and read THIS
    //    store's high-water and epoch. That is the only version of the claim that can fail.
    meta.poke_u16(10, sc_before);                             // undo the exact corruption applied above
    SegmentedInboxStore repaired(recs, meta, 4096, 256);
    CHECK(repaired.begin());                                  // ★ the SAME pair mounts again once the meta is sound
    CHECK(repaired.mount_fault() == SegMountFault::none);
    CHECK(repaired.persisted_next_seq() == 4);                // ★ the high-water the refusal did not touch
    // ★★ AND THE EPOCH IS THE ONE THE WIPE LEFT — ⛔ NOT one more. A refused mount must not bump, and neither may
    //    the successful mount that follows it: the store was already marked empty by `wipe()`, so this boot is
    //    stable. (Before the acknowledged-empty marker this arm read 3, then 4, then 5 on repeated boots.)
    CHECK(repaired.storage_epoch() == 2);
    SegmentedInboxStore again(recs, meta, 4096, 256);
    CHECK(again.begin());
    CHECK(again.storage_epoch() == 2);
}

// ============================================================================================================
// ★★★ [[B134]] QG ROUND 5 — THE ACKNOWLEDGED-EMPTY MARKER. QG reproduced: after a wipe with no new traffic,
// boot1 epoch=2, boot2 epoch=3, boot3 epoch=4 with next_seq stuck at 2 — the companion treating an unchanged
// empty inbox as newly wiped, for ever. `next_seq > 1` says "this store once had traffic", which stays true.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: §B134e/1 ★★★ record -> wipe -> THREE boots with no new traffic = ONE increment") {
    FakeSegmentStore recs; FakeMetaStore meta;
    uint32_t after_wipe = 0;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        CHECK(s.storage_epoch() == 1);
        put(s, 1, "a"); put(s, 2, "b");
        CHECK(s.set_next_seq(3));                             // the high-water a real Inbox would have batched
        CHECK(s.wipe());
        after_wipe = s.storage_epoch();
        CHECK(after_wipe == 2);                               // the deliberate wipe: exactly one bump, at the wipe
        // ★★ AND WIPING AN ALREADY-EMPTY STORE BUMPS NOTHING. It destroyed no history, so it owes the companion
        //    no re-pull — an unconditional bump here would make a repeated `prep-restart` (or a `factory_reset`
        //    on a clean node) cost a full re-sync for an inbox that never changed.
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == after_wipe);
    }
    // ★★ THREE CONSECUTIVE BOOTS, NO NEW MESSAGES. Every one must report the SAME epoch.
    for (int boot = 0; boot < 3; ++boot) {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        CHECK(s.storage_epoch() == after_wipe);               // ⛔ 2, 2, 2 — never 2, 3, 4
        CHECK(s.persisted_next_seq() == 3);                   // and the high-water is stable too
    }
}

TEST_CASE("SegmentedInboxStore: §B134e/2 ★★ the first append after empty marks the store NON-EMPTY, and it persists") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "first");                                   // the empty -> non_empty transition
    }
    // ★ PROVED ACROSS A REMOUNT, which is the only way to show the marker reached the MEDIUM: with the records
    //   still present the boot is ordinary and must not bump anything.
    {
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 1);                       // records present + marked non-empty -> no detect
        Got g; CHECK(s2.read_since(0, got_cb, &g) == 1);
    }
}

TEST_CASE("SegmentedInboxStore: §B134e/3 ★★★ an EXTERNAL record loss after that append bumps EXACTLY once") {
    // ⛔ THE ARM THE WHOLE MECHANISM EXISTS FOR — the genuine §10.1 detect. It must survive the fix that stopped
    //    the per-boot ratchet, and it must itself be a once-only event.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "live"); CHECK(s.set_next_seq(2));
    }
    recs.wipe();                                              // something OUTSIDE this store destroyed the records
    {
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 2);                       // ★ detected, bumped
        CHECK(s2.persisted_next_seq() == 2);                  // ★ next_seq preserved — seq never reuses
    }
    // ★★ ...AND THE NEXT TWO BOOTS ARE STABLE. Before the marker this is where it climbed to 3 and 4.
    for (int boot = 0; boot < 2; ++boot) {
        SegmentedInboxStore s3(recs, meta, 4096, 256);
        CHECK(s3.begin());
        CHECK(s3.storage_epoch() == 2);
    }
}

TEST_CASE("SegmentedInboxStore: §B134e/4 ★★★ a failed save on EITHER transition acknowledges nothing") {
    // The blocker-1 discipline applied verbatim to the new field: a marker that did not reach the medium must not
    // leave behind an acknowledged record or an acknowledged wipe.
    {   // ---- the empty -> non_empty transition ----
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        meta.fail_saves(true);
        CHECK_FALSE(s.append(1, reinterpret_cast<const uint8_t*>("x"), 1));   // ★ REFUSED, not acknowledged
        meta.fail_saves(false);
        CHECK_FALSE(recs.any_segments(nullptr));              // ★ and no record bytes were written
        // ★ AND THE MARKER DID NOT MOVE EITHER: a later external-loss boot must not fire for a record that was
        //   never accepted, or the refusal would cost a spurious epoch bump.
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 1);
    }
    {   // ---- the wipe (non_empty -> empty) transition ----
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "a");
        meta.fail_saves(true);
        CHECK_FALSE(s.wipe());                                // ★ the wipe reports FAILURE...
        meta.fail_saves(false);
        // ...and the marker never persisted, so the next boot sees the records gone while still marked non-empty
        // and reports it as the external loss it now genuinely is — ONE bump, not zero and not two.
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 2);
        SegmentedInboxStore s3(recs, meta, 4096, 256);
        CHECK(s3.begin());
        CHECK(s3.storage_epoch() == 2);                       // stable afterwards
    }
}

// ============================================================================================================
// ★★★ [[B134]] QG ROUND 6 — `append_pending`. Marking `non_empty` before writing was safe against a MARKER
// failure but not against a RECORD failure: QG reproduced append=0, any_segments=0, and a boot that then
// reported an external records loss and advanced the epoch for a message that never existed.
// ⓘ Meta layout offset for the marker: magic@0 version@4 head@6 tail@8 segs@10 next@12 cursor@16 epoch@20,
//   so `records_state` is at 24.
// ============================================================================================================

TEST_CASE("SegmentedInboxStore: §B134g/1 ★★★ a failed mark_read never reports success, and the RETRY repairs it") {
    // ⛔⛔ QG ROUND 7, THE SEQUENCE VERBATIM. The wear-coalescing was what made this unrepairable: the old
    //    one-liner set the RAM cursor before saving, so a failed save left the NEW value in RAM and the caller's
    //    retry with the SAME value hit `seq == _meta.read_cursor` and returned true WITHOUT saving. The one
    //    operation that could have repaired the medium was the one the optimisation ate.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        // (1) FAIL THE SAVE -> no success answer.
        meta.fail_saves(true);
        CHECK_FALSE(s.set_read_cursor(7));
        // ★ AND THE RAM VALUE DID NOT MOVE: a cursor that looks persisted when it is not is the same lie, and it
        //   is also exactly what re-armed the coalescing.
        CHECK(s.read_cursor() == 0);
        // (2) THE RETRY WHILE STILL BROKEN is still refused — ⛔ never swallowed by the coalescing.
        CHECK_FALSE(s.set_read_cursor(7));
        // (3) RECOVER THE MEDIUM, (4) REPEAT THE SAME CURSOR -> a save really OCCURS.
        meta.fail_saves(false);
        const int saves_before = meta.saves();
        CHECK(s.set_read_cursor(7));
        CHECK(meta.saves() > saves_before);                   // ★ it wrote — the retry was not coalesced away
        CHECK(s.read_cursor() == 7);
    }
    // (5) REBOOT RESTORES THAT CURSOR.
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.read_cursor() == 7);
    // ★★ AND THE COALESCING STILL COALESCES when the medium genuinely agrees — the wear guard must survive its
    //    own fix, or every mark_read in a pull session rewrites the meta again.
    const int saves_settled = meta.saves();
    CHECK(s2.set_read_cursor(7));
    CHECK(meta.saves() == saves_settled);
    // ★★ AND THE DIRTY LATCH OVERRIDES THE COALESCING, which is the term that makes a repair possible at all.
    //    `_meta_dirty` can be set by ANY failed save — a rotation, a marker transition — and a later mark_read to
    //    the cursor the store ALREADY holds is then the opportunity to re-persist the whole blob. Without the
    //    `!_meta_dirty` term that call short-circuits to success and the metadata stays stale for ever.
    meta.fail_saves(true);
    CHECK_FALSE(s2.set_read_cursor(11));                      // fails -> rolls back to 7 AND latches dirty
    CHECK(s2.read_cursor() == 7);
    meta.fail_saves(false);
    const int saves_dirty = meta.saves();
    CHECK(s2.set_read_cursor(7));                             // the SAME value the store already holds...
    CHECK(meta.saves() > saves_dirty);                        // ★ ...still writes, because the medium is stale
}

TEST_CASE("SegmentedInboxStore: §B134g/2 ★★ a failed set_next_seq does not over-report the persisted high-water") {
    // The same RAM-first shape one field along. It has no change-detect in front of it, so its retry was never
    // eaten — but `persisted_next_seq()` still reported a high-water that is not on the medium.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK(s.begin());
    CHECK(s.set_next_seq(5));
    meta.fail_saves(true);
    CHECK_FALSE(s.set_next_seq(9));
    CHECK(s.persisted_next_seq() == 5);                       // ★ NOT 9 — it reports the medium, not the wish
    meta.fail_saves(false);
    CHECK(s.set_next_seq(9));
    CHECK(s.persisted_next_seq() == 9);
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.persisted_next_seq() == 9);
}

TEST_CASE("Inbox: §B134g/3 ★★★ mark_read RELAYS the store's verdict — an unwired inbox never reports success") {
    FakeSegmentStore drecs, crecs; FakeMetaStore dmeta, cmeta;
    SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.enabled());
    CHECK(ib.mark_read(InboxKind::dm, 3));                    // the healthy arm
    dmeta.fail_saves(true);
    CHECK_FALSE(ib.mark_read(InboxKind::dm, 9));              // ★ the failure is RELAYED, not discarded
    dmeta.fail_saves(false);
    CHECK(ib.mark_read(InboxKind::channel, 4));               // the other store is unaffected
    Inbox off;                                                // ⛔ and an unwired inbox is false, never silent success
    CHECK_FALSE(off.mark_read(InboxKind::dm, 1));
}

TEST_CASE("SegmentedInboxStore: §B134f/1 ★★★ a first append that writes ZERO bytes causes NO epoch bump, ever") {
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        CHECK(s.storage_epoch() == 1);
        // ⛔ THE EXACT SHAPE: the very first record's HEADER write fails having written NOTHING. The marker save
        //    that precedes it SUCCEEDS, which is what made this reachable — the old code had already persisted
        //    `non_empty` over a medium that stayed empty.
        recs.fail_mid_frame(/*nth*/0, /*partial*/0);
        CHECK_FALSE(s.append(1, reinterpret_cast<const uint8_t*>("x"), 1));
        CHECK_FALSE(recs.fault_armed());                      // ★ the injector really FIRED
        CHECK_FALSE(recs.any_segments(nullptr));              // ★ and the medium really is empty
    }
    // ★★ THE REBOOT. `pending` + no bytes resolves to `empty` — the append never landed, so no history was lost
    //    and there is nothing to announce.
    {
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 1);                       // ⛔ NOT 2 — this is the whole case
    }
    // ★ ...and it stays resolved: a second boot is stable too (the resolution was persisted).
    {
        SegmentedInboxStore s3(recs, meta, 4096, 256);
        CHECK(s3.begin());
        CHECK(s3.storage_epoch() == 1);
    }
}

TEST_CASE("SegmentedInboxStore: §B134f/2 ★★★ bytes landed but the FINALIZE save died — resolves to non_empty, no bump") {
    // The other half of the resolve rule. The append is honestly ACKNOWLEDGED (its bytes are durable), the marker
    // is left saying `pending`, and the mount resolves it FORWARD by looking at the medium.
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        meta.fail_save_at(1);                                 // save #0 = the pending marker; #1 = the finalize
        CHECK(s.append(1, reinterpret_cast<const uint8_t*>("kept"), 4));   // ★ SUCCESS — the bytes are durable
        CHECK_FALSE(meta.save_fault_armed());                 // ★ the injector really FIRED
    }
    CHECK(recs.any_segments(nullptr));
    {
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.storage_epoch() == 1);                       // ⛔ no bump: nothing was lost
        Got g; CHECK(s2.read_since(0, got_cb, &g) == 1);      // ★ and the record is readable
        CHECK(g.bodies[0] == "kept");
    }
    // ★★ AND THE RESOLUTION STUCK AS `non_empty`, which the external-loss arm must still honour: destroy the
    //    records now and the next boot DOES bump. Resolving forward must not disarm §10.1.
    recs.wipe();
    SegmentedInboxStore s3(recs, meta, 4096, 256);
    CHECK(s3.begin());
    CHECK(s3.storage_epoch() == 2);
}

TEST_CASE("SegmentedInboxStore: §B134f/3 ★★ wipe() while append_pending follows the SAME resolve rule") {
    {   // (a) pending with NO bytes — the append never landed, so the wipe destroys nothing and announces nothing
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        recs.fail_mid_frame(0, 0);
        CHECK_FALSE(s.append(1, reinterpret_cast<const uint8_t*>("x"), 1));
        CHECK_FALSE(recs.any_segments(nullptr));
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == 1);                        // ⛔ no bump — there was no history to lose
    }
    {   // (b) pending WITH bytes — the append landed, so this wipe really does destroy history: bump once
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        meta.fail_save_at(1);                                 // the finalize dies -> the marker stays `pending`
        CHECK(s.append(1, reinterpret_cast<const uint8_t*>("real"), 4));
        CHECK(recs.any_segments(nullptr));
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == 2);                        // ★ bumped, because history genuinely went
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == 2);                        // ...and only once
    }
}

TEST_CASE("SegmentedInboxStore: §B134f/4 ★★★ an OUT-OF-RANGE records_state is corrupt metadata, both directions") {
    // ⛔ QG round 6 gap 2: the marker was never range-checked, so a persisted 2 (under the two-state scheme) fell
    //    through every arm that tested it and the store mounted with fault=0 — external-loss detection bypassed
    //    on an empty partition. The v4 fail-loud policy has to cover the field v4 added.
    FakeSegmentStore recs; FakeMetaStore meta;
    { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "x"); CHECK(s.set_next_seq(2)); }
    const uint8_t good = meta.peek_u8(24);
    CHECK(good == 1);                                         // non_empty — the layout offset is what we think

    meta.poke_u8(24, 3);                                      // one past the valid set {0,1,2}
    { SegmentedInboxStore s2(recs, meta, 4096, 256);
      CHECK_FALSE(s2.begin());
      CHECK(s2.mount_fault() == SegMountFault::meta_corrupt); }
    meta.poke_u8(24, 0xFF);                                   // and the erased-flash byte
    { SegmentedInboxStore s3(recs, meta, 4096, 256);
      CHECK_FALSE(s3.begin());
      CHECK(s3.mount_fault() == SegMountFault::meta_corrupt); }

    // ★★ THE OTHER DIRECTION — every VALID value must still mount, or the check would break first boot and every
    //    in-flight append (the G20/F23 pattern: a guard that refuses too much is as broken as one that refuses
    //    too little).
    for (uint8_t v = 0; v <= 2; ++v) {
        meta.poke_u8(24, v);
        SegmentedInboxStore ok_store(recs, meta, 4096, 256);
        CHECK(ok_store.begin());
        CHECK(ok_store.mount_fault() == SegMountFault::none);
    }
}

TEST_CASE("SegmentedInboxStore: §B134e/5 ★★ an OLD-FORMAT meta blob is REFUSED, explicitly (the v3->v4 upgrade)") {
    // ⛔ THE META STRUCT GREW (it gained `records_state`), so a v3 blob is the WRONG LENGTH and the exact-length
    //    rule classifies it as `error` -> `meta_corrupt` -> a refused mount. That is DELIBERATE and it is tested
    //    here so it is not an accident of the length rule: the alternative is reinterpreting a short blob as
    //    "probably the previous layout", which is the guess-at-bytes this arc has spent five rounds removing.
    //    M3 makes the honest option affordable — MeshRoute is unshipped, so the cost is ONE `factory_reset
    //    confirm` on the flash that first carries this build, which the report and the bench script both record.
    // ⓘ The fake stores whatever length it was handed, so a SHORT blob models a v3 record exactly.
    FakeSegmentStore recs; FakeMetaStore meta;
    struct OldMeta { uint32_t magic; uint16_t version; uint16_t head, tail, segs; uint32_t next_seq, cursor, epoch; };
    OldMeta v3{ 0x4D524958u, 3, 0, 0, 17, 9, 0, 4 };
    CHECK(meta.save(&v3, sizeof v3));
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK_FALSE(s.begin());
    CHECK(s.mount_fault() == SegMountFault::meta_corrupt);
    // ★ AND RECOVERY IS THE DOCUMENTED ONE: clearing the meta (what factory_reset does) mounts fresh.
    meta.wipe();
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 1);
    CHECK(s2.persisted_next_seq() == 1);
}

TEST_CASE("SegmentedInboxStore: §B134d/2 ★★ a meta store that cannot be READ is an error, never a fresh start") {
    // The third of the three states. `absent` is "never written"; this is "the medium would not answer", and it
    // fails loud whether or not records exist — the high-water and the epoch live nowhere else.
    // ⛔ CORRECTED 2026-08-29 ([[B134]] QG round 4) — THE "WITH RECORDS" ARM HAD NO RECORDS. It CLAIMED to drive
    //    the read-error verdict over a live log; it ACTUALLY called `put()` on a store whose `begin()` had already
    //    FAILED, so `_ok` was false, the append was inert, and both arms tested an EMPTY record store. ⇒ the
    //    records are now seeded BEFORE the fault is armed, and their existence is asserted against the MEDIUM
    //    (`any_segments`), never against a `put()` return that a refused store cannot honour anyway.
    FakeSegmentStore recs; FakeMetaStore meta;
    { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); CHECK(s.set_next_seq(7)); }
    CHECK_FALSE(recs.any_segments(nullptr));                  // arm 1's precondition, asserted not assumed
    meta.fail_loads(true);
    SegmentedInboxStore s2(recs, meta, 4096, 256);            // EMPTY record store — still refuses
    CHECK_FALSE(s2.begin());
    CHECK(s2.mount_fault() == SegMountFault::meta_corrupt);
    // ---- arm 2: the SAME fault over a genuinely LIVE log ----
    meta.fail_loads(false);                                   // seed through a store that can actually mount
    { SegmentedInboxStore seed(recs, meta, 4096, 256); CHECK(seed.begin()); put(seed, 7, "x"); }
    CHECK(recs.any_segments(nullptr));                        // ★ the records are REALLY there this time
    meta.fail_loads(true);
    SegmentedInboxStore s3(recs, meta, 4096, 256);
    CHECK_FALSE(s3.begin());
    CHECK(s3.mount_fault() == SegMountFault::meta_corrupt);
    // ★ THE CONTROL: readable again -> mounts, high-water intact.
    meta.fail_loads(false);
    SegmentedInboxStore s4(recs, meta, 4096, 256);
    CHECK(s4.begin());
    CHECK(s4.persisted_next_seq() == 7);
}

TEST_CASE("SegmentedInboxStore: §B134d/3 ★★★ BOTH stores mount before the verdict — mount_fault is never under-stated") {
    // ⛔ QG round 3 blocker 3: `!_dm->begin() || !_chan->begin()` short-circuited, so a DM failure meant the
    //    CHANNEL store never attempted its mount — its fault stayed `none` and two corrupted keys reported 5/0.
    //    The diagnostic then UNDER-STATES the damage and an operator reflashes expecting to keep channel history.
    FakeSegmentStore drecs, crecs; FakeMetaStore dmeta, cmeta;
    // Seed both with a real history, then lose BOTH metas.
    { SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
      CHECK(dm.begin()); CHECK(ch.begin()); put(dm, 1, "d"); put(ch, 1, "c"); }
    dmeta.wipe(); cmeta.wipe();
    {   // ---- both fail -> 5/5, and the inbox is fully disabled ----
        SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
        Inbox ib; ib.on_init(&dm, &ch);
        CHECK_FALSE(ib.enabled());
        CHECK(dm.mount_fault() == SegMountFault::meta_lost_over_records);
        CHECK(ch.mount_fault() == SegMountFault::meta_lost_over_records);   // ★ the one the short-circuit hid
    }
    {   // ---- only the DM side fails -> 5/0, still fully disabled ----
        // ⛔ CORRECTED 2026-08-29 ([[B134]] QG round 4) — THIS ARM WAS BOTH-FAIL WEARING A DM-ONLY LABEL, and it
        //    said so in its own assertion. It gave the channel `crecs` (which the seed above filled) plus a FRESH
        //    `FakeMetaStore` — i.e. records present and metadata ABSENT, which correctly fails as
        //    `meta_lost_over_records`. So the mixed verdict was never tested and the arm duplicated the one above
        //    it. ⇒ the channel now gets a genuinely HEALTHY pair: its own record store AND a meta that matches it.
        FakeSegmentStore chealthy; FakeMetaStore cmeta_ok;
        { SegmentedInboxStore ch0(chealthy, cmeta_ok, 4096, 256); CHECK(ch0.begin()); put(ch0, 1, "c"); }
        SegmentedInboxStore dm(drecs, dmeta, 4096, 256), ch(chealthy, cmeta_ok, 4096, 256);
        Inbox ib; ib.on_init(&dm, &ch);
        CHECK_FALSE(ib.enabled());                            // ⛔ all-or-nothing: never half an inbox
        CHECK(dm.mount_fault() == SegMountFault::meta_lost_over_records);   // 5 ...
        CHECK(ch.mount_fault() == SegMountFault::none);                     // ... / 0 — the MIXED verdict, at last
    }
    {   // ---- only the CHANNEL side fails -> 0/5: the DM store mounts cleanly and the channel fault is still seen ----
        FakeSegmentStore ddrecs; FakeMetaStore ddmeta;
        SegmentedInboxStore dm(ddrecs, ddmeta, 4096, 256), ch(crecs, cmeta, 4096, 256);
        Inbox ib; ib.on_init(&dm, &ch);
        CHECK_FALSE(ib.enabled());
        CHECK(dm.mount_fault() == SegMountFault::none);       // ★ a genuinely fresh store mounted...
        CHECK(ch.mount_fault() == SegMountFault::meta_lost_over_records);   // ...and the OTHER side's fault is reported
    }
}

TEST_CASE("SegmentedInboxStore: §B134b/4 ★★ begin() FAILS LOUD when the baseline meta will not persist") {
    // A store whose topology/epoch/high-water cannot reach the medium is not durable. Refusing the mount leaves
    // the Inbox DISABLED and visible at boot; running anyway would be non-durability wearing durability's name.
    // ⓘ It also closes the fresh-branch seq-reuse hole: up to kSeqPersistBatch records could be written before the
    //   first set_next_seq, and a reboot in that window restored next_seq=1 over a log already holding those seqs.
    FakeSegmentStore recs; FakeMetaStore meta;
    meta.fail_saves(true);
    SegmentedInboxStore s(recs, meta, 4096, 256);
    CHECK_FALSE(s.begin());
    // ★ AND THE FAIL-LOUD CHAIN RUNS ALL THE WAY UP: `Inbox::on_init` drops BOTH store pointers when a begin()
    //   fails, so the inbox reports DISABLED and every record_* is inert — never a half-wired store that accepts
    //   messages it cannot keep.
    Inbox ib; ib.on_init(&s, &s);
    CHECK_FALSE(ib.enabled());
    CHECK(ib.record_dm(2, 0, 1, 0, reinterpret_cast<const uint8_t*>("x"), 1, 1) == 0);
    CHECK(ib.erase(InboxKind::dm, 1) == InboxEraseResult::io_error);   // an unwired inbox NEVER reports success
    Got g; CHECK(s.read_since(0, got_cb, &g) == 0);           // and nothing was written
    // ★ And the fresh branch really does persist when it can — the control that keeps the case above non-vacuous.
    FakeSegmentStore recs2; FakeMetaStore meta2;
    SegmentedInboxStore s2(recs2, meta2, 4096, 256);
    CHECK(s2.begin());
    CHECK(meta2.saved());
}

TEST_CASE("SegmentedInboxStore: §B134b/5 ★★ a ROLL onto a segment that will not erase is REFUSED (no writing behind stale bytes)") {
    // The new head must come up EMPTY. Stale lapped bytes surviving the erase would be parsed as frames by
    // read_since, which is the §B135 mis-parse arriving from the other end.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 16);              // seg=16 -> the 2nd record must roll (see §B134b/1)
    CHECK(s.begin());
    put(s, 1, "first");
    recs.fail_erase_of(1);                                    // segment 1 is the roll target and refuses erasure
    CHECK_FALSE(s.append(2, reinterpret_cast<const uint8_t*>("second"), 6));
    recs.fail_erase_of(-1);
    CHECK(s.append(3, reinterpret_cast<const uint8_t*>("third"), 5));   // recovers once the medium does
    Got g; s.read_since(0, got_cb, &g);
    CHECK(g.seqs == std::vector<uint32_t>{1, 3});
}

TEST_CASE("SegmentedInboxStore: §B134b/6 ★★★ wipe() REPORTS failure — a destructive verb may not claim an erase it did not do") {
    // ⛔ QG BLOCKER 3. `wipe()` returned `void`, so `prep-restart` printed "inbox cleared" and `factory_reset`
    //    rebooted claiming a factory state while records stayed RECOVERABLE on flash. For a destructive verb that
    //    is the worst direction to be wrong in.
    FakeSegmentStore recs; FakeMetaStore meta;
    SegmentedInboxStore s(recs, meta, 4096, 32);
    CHECK(s.begin());
    for (uint32_t i = 1; i <= 10; ++i) { char b[8]; std::snprintf(b, sizeof b, "w%u", i); put(s, i, b); }
    CHECK(s.wipe());                                          // the healthy arm, so the failure arms below can fail
    CHECK_FALSE(recs.any_segments(nullptr));

    // (a) a segment that will not erase -> FALSE, and the surviving record proves the report is about reality.
    for (uint32_t i = 11; i <= 20; ++i) { char b[8]; std::snprintf(b, sizeof b, "x%u", i); put(s, i, b); }
    recs.fail_erase_of(0);
    CHECK_FALSE(s.wipe());
    CHECK(recs.seg_bytes(0) > 0);                             // ★ records really do remain — the `false` is earned
    recs.fail_erase_of(-1);

    // ★ AND IT DID NOT STOP AT THE FIRST FAILURE: every OTHER segment was still erased. A destructive verb must
    //   erase as much as it can and still report that it did not finish.
    CHECK(recs.live_segments() == 1);

    // (b) the metadata half: erases succeed but the topology will not persist -> still FALSE.
    CHECK(s.wipe());
    for (uint32_t i = 21; i <= 30; ++i) { char b[8]; std::snprintf(b, sizeof b, "y%u", i); put(s, i, b); }
    meta.fail_saves(true);
    CHECK_FALSE(s.wipe());
    meta.fail_saves(false);
}

TEST_CASE("SegmentedInboxStore: begin() FAILS LOUD if seg_bytes exceeds the read scratch (no silent truncation)") {
    FakeSegmentStore recs; FakeMetaStore meta;
    // A segment larger than the 4 KiB read scratch would make read_since drop every record past 4 KB -> begin
    // refuses (the inbox stays disabled, visible at boot) instead of silently losing records.
    SegmentedInboxStore too_big(recs, meta, /*cap*/1u << 20, /*seg*/8192);
    CHECK_FALSE(too_big.begin());
    FakeSegmentStore recs2; FakeMetaStore meta2;
    SegmentedInboxStore okp(recs2, meta2, /*cap*/1u << 20, /*seg*/4096);   // == the scratch -> allowed
    CHECK(okp.begin());
}
