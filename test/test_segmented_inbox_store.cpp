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
    CHECK(recs.any_segments());                              // the old-format records ARE present on "flash"
    meta.poke_u16(4, 2);                                     // simulate the PRIOR store version v2 (§S5 team_id; record header grew under it -> +origin_layer v3)
    SegmentedInboxStore s2(recs, meta, 4096, 256);
    CHECK(s2.begin());
    CHECK(s2.storage_epoch() == 2);                          // BUMPED -> the companion sees the wipe + re-syncs
    CHECK(s2.persisted_next_seq() == 3);                     // next_seq PRESERVED (never reuses a seq)
    Got g; CHECK(s2.read_since(0, got_cb, &g) == 0);         // the unparseable old records are gone
    CHECK_FALSE(recs.any_segments());                        // records store wiped
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

TEST_CASE("SegmentedInboxStore: M2 — a torn meta (seg_count==0 / head_seg>=seg_count) is rejected -> fresh, no DBZ/hang") {
    // S2 flash-validation regression: load_meta() must reject a torn meta BEFORE its fields divide (% seg_count ->
    // DBZ when seg_count==0) or bound the ring walk (i == head_seg never true when head_seg >= seg_count ->
    // infinite boot loop). A rejected meta is treated as FRESH: begin() re-inits (next_seq back to 1, epoch 1).
    // Meta layout offsets (private struct, documented there): head_seg@6, tail_seg@8, seg_count@10.

    // (a) seg_count == 0 would divide-by-zero in the `% seg_count` ring walk.
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "x"); CHECK(s.set_next_seq(9)); }
        CHECK(meta.saved());
        meta.poke_u16(10, 0);                                    // corrupt seg_count -> 0
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());                                       // must NOT hard-fault (DBZ) — rejected -> fresh
        CHECK(s2.persisted_next_seq() == 1);                     // re-inited fresh (torn meta discarded)
        CHECK(s2.storage_epoch() == 1);
    }
    // (b) head_seg >= seg_count would make the `i == head_seg` ring-walk terminator unreachable (infinite loop).
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "y"); CHECK(s.set_next_seq(9)); }
        const uint16_t sc = meta.peek_u16(10);                   // the valid seg_count == ring_segs()
        CHECK(sc > 0);
        meta.poke_u16(6, sc);                                    // head_seg = seg_count (out of range)
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());                                       // must RETURN (no infinite loop) — rejected -> fresh
        CHECK(s2.persisted_next_seq() == 1);
    }
    // (c) tail_seg >= seg_count is likewise rejected.
    {
        FakeSegmentStore recs; FakeMetaStore meta;
        { SegmentedInboxStore s(recs, meta, 4096, 256); CHECK(s.begin()); put(s, 1, "z"); CHECK(s.set_next_seq(9)); }
        const uint16_t sc = meta.peek_u16(10);
        meta.poke_u16(8, static_cast<uint16_t>(sc + 5));         // tail_seg out of range
        SegmentedInboxStore s2(recs, meta, 4096, 256);
        CHECK(s2.begin());
        CHECK(s2.persisted_next_seq() == 1);
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
