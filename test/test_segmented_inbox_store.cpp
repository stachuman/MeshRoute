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

TEST_CASE("SegmentedInboxStore: §S5 record-format VERSION mismatch -> records wiped + epoch bumped + next_seq preserved") {
    // The §S5 team_id record-header growth bumps the store version. On the next boot the OLD-format records can't be
    // parsed by the new deserializer, so begin() must WIPE them, BUMP the epoch (companion re-pulls from 0), and KEEP
    // next_seq (monotonic — seq never reuses). Meta layout: magic@0, version@4 (a private struct; offset documented there).
    FakeSegmentStore recs; FakeMetaStore meta;
    {
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        put(s, 1, "x"); put(s, 2, "y");
        CHECK(s.set_next_seq(3));                             // high-water persisted to the (separate) meta store
        CHECK(s.storage_epoch() == 1);
    }
    CHECK(recs.any_segments());                              // the old-format records ARE present on "flash"
    meta.poke_u16(4, 99);                                    // simulate an OLD store version (record header layout changed under it)
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
