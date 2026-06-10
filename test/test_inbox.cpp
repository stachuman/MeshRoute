// MeshRoute — test_inbox.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the persistent inbox (lib/core/inbox.{h,cpp}): record_dm/record_channel, cursor-based
// pull, drop-oldest at the byte cap, DM/channel store isolation, and seq-monotonic-across-reboot (§6) —
// all driven against a RAM-backed fake InboxStore (a std::deque honoring the same cap/eviction, spec §9).
// The device QSPI/LittleFS stores are Phase 2. NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp
// provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "inbox.h"
#include "ram_inbox_store.h"

#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// pull collector: copy the decoded entry (body points into transient store bytes -> copy to a std::string).
struct Collected { uint32_t seq; InboxKind kind; uint8_t origin; uint8_t channel_id; uint16_t ctr; uint64_t rx; std::string body; };
struct Collector { std::vector<Collected> items; };
bool collect_cb(void* ctx, const InboxEntry& e) {
    auto* c = static_cast<Collector*>(ctx);
    c->items.push_back({ e.seq, e.kind, e.origin, e.channel_id, e.ctr, e.rx_time_ms,
                         std::string(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len) });
    return true;
}

void rec_dm(Inbox& ib, uint8_t origin, uint16_t ctr, const char* s, uint64_t t) {
    ib.record_dm(origin, ctr, reinterpret_cast<const uint8_t*>(s), static_cast<uint8_t>(std::strlen(s)), t);
}
void rec_ch(Inbox& ib, uint8_t ch, uint8_t origin, uint16_t ctr, const char* s, uint64_t t) {
    ib.record_channel(ch, origin, ctr, reinterpret_cast<const uint8_t*>(s), static_cast<uint8_t>(std::strlen(s)), t);
}

}  // namespace

TEST_CASE("inbox: disabled until stores are installed (record_* / pull inert)") {
    Inbox ib;                                                     // no on_init
    CHECK_FALSE(ib.enabled());
    rec_dm(ib, 5, 1, "ignored", 0);                              // no store -> no-op, no crash
    Collector c; CHECK(ib.pull(0, 0, collect_cb, &c) == 0);
    CHECK(c.items.empty());
}

TEST_CASE("inbox: record DM + channel, pull(0,0) returns all oldest-first, fields intact") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.enabled());
    rec_dm(ib, 5, 100, "hi-bob", 1000);
    rec_dm(ib, 7, 101, "yo", 1001);
    rec_ch(ib, 2, 9, 0x42, "hello-chan", 1002);

    Collector c; const uint16_t n = ib.pull(0, 0, collect_cb, &c);
    CHECK(n == 3); CHECK(c.items.size() == 3);
    // DM block (oldest-first) THEN channel block
    CHECK(c.items[0].kind == InboxKind::dm); CHECK(c.items[0].origin == 5); CHECK(c.items[0].ctr == 100);
    CHECK(c.items[0].body == "hi-bob"); CHECK(c.items[0].rx == 1000); CHECK(c.items[0].seq == 1);
    CHECK(c.items[1].kind == InboxKind::dm); CHECK(c.items[1].origin == 7); CHECK(c.items[1].body == "yo");
    CHECK(c.items[1].seq == 2);
    CHECK(c.items[2].kind == InboxKind::channel); CHECK(c.items[2].channel_id == 2); CHECK(c.items[2].origin == 9);
    CHECK(c.items[2].ctr == 0x42); CHECK(c.items[2].body == "hello-chan"); CHECK(c.items[2].seq == 1);   // independent seq space
    CHECK(ib.dm_newest_seq() == 2); CHECK(ib.chan_newest_seq() == 1);
}

TEST_CASE("inbox: pull(since) returns only seq > since (the cursor)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    for (uint16_t i = 0; i < 5; ++i) rec_dm(ib, 1, static_cast<uint16_t>(200 + i), "m", 0);   // seq 1..5
    Collector c; ib.pull(/*dm_since=*/3, /*chan_since=*/0, collect_cb, &c);
    CHECK(c.items.size() == 2);
    CHECK(c.items[0].seq == 4); CHECK(c.items[1].seq == 5);
}

TEST_CASE("inbox: drop-oldest at the byte cap — oldest evicted, count bounded, seq monotonic + newest kept") {
    RamInboxStore dm(80), ch(protocol::inbox_chan_store_bytes);   // ~3 records fit (18 hdr + 1 body + 2 frame = 21 B)
    Inbox ib; ib.on_init(&dm, &ch);
    for (uint16_t i = 1; i <= 10; ++i) rec_dm(ib, 1, i, "x", i);
    CHECK(dm.count() >= 1); CHECK(dm.count() <= 3);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == dm.count());
    for (size_t i = 1; i < c.items.size(); ++i) CHECK(c.items[i].seq > c.items[i - 1].seq);   // monotonic, oldest-first
    CHECK(c.items.back().seq == 10);                              // the newest is retained
    CHECK(c.items.back().ctr == 10);
}

TEST_CASE("inbox: DM and channel stores are isolated (a channel flood does NOT evict DMs)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(80);     // tiny channel cap
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 1, "keep-me", 0);
    for (uint16_t i = 1; i <= 20; ++i) rec_ch(ib, 0, 9, i, "c", i);   // flood the channel store
    Collector c; ib.pull(0, 0, collect_cb, &c);
    int dms = 0, chs = 0;
    for (const auto& x : c.items) { if (x.kind == InboxKind::dm) ++dms; else ++chs; }
    CHECK(dms == 1);                                              // the DM is untouched by the channel flood
    CHECK(c.items[0].body == "keep-me");
    CHECK(chs == ch.count());
    CHECK(chs < 20);                                             // channel store DID evict
}

TEST_CASE("inbox: seq never regresses across a reboot (re-on_init restores next-seq); batched persist fires") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    {   // first boot: 10 DMs -> seq 1..10; the batched set_next_seq fires (every 8 appends)
        Inbox ib; ib.on_init(&dm, &ch);
        for (uint16_t i = 1; i <= 10; ++i) rec_dm(ib, 1, i, "m", i);
        CHECK(ib.dm_newest_seq() == 10);
    }
    CHECK(dm.persisted_next_seq() >= 9);                          // §6: counter persisted at the 8th append (next=9)
    Inbox ib2; ib2.on_init(&dm, &ch);                            // "reboot" on the persisted store
    CHECK(ib2.dm_newest_seq() == 10);                            // restored from the stored records' high-water
    rec_dm(ib2, 2, 99, "after-reboot", 100);                     // MUST be seq 11, never reuse <= 10
    Collector c; ib2.pull(/*dm_since=*/10, 0, collect_cb, &c);   // since=10 -> only the post-reboot record
    CHECK(c.items.size() == 1);
    CHECK(c.items[0].seq == 11);
    CHECK(c.items[0].body == "after-reboot");
}

TEST_CASE("inbox: empty / max-body records round-trip; an over-cap single record is rejected") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 1, 1, "", 0);                                      // empty body
    std::string big(protocol::inbox_max_body, 'A');              // max body
    ib.record_dm(2, 2, reinterpret_cast<const uint8_t*>(big.data()), protocol::inbox_max_body, 0);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 2);
    CHECK(c.items[0].body.empty());
    CHECK(c.items[1].body.size() == protocol::inbox_max_body);
    CHECK(c.items[1].body == big);

    // a single record larger than the whole store -> append rejects (never reached given inbox_max_body, but the guard holds)
    RamInboxStore tiny(20), ch2(protocol::inbox_chan_store_bytes);   // < one 21-B record
    Inbox ib2; ib2.on_init(&tiny, &ch2);
    rec_dm(ib2, 1, 1, "x", 0);
    CHECK(tiny.count() == 0);                                     // not stored (and seq still advanced — monotonic, not gapless)
}

TEST_CASE("inbox: flush() force-persists the next-seq counters (the on-a-timer half of §6)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 1, 1, "m", 0);  rec_dm(ib, 1, 2, "m", 0);  rec_dm(ib, 1, 3, "m", 0);   // 3 < batch(8) -> not yet persisted
    CHECK(dm.persisted_next_seq() == 0);
    rec_ch(ib, 0, 9, 1, "c", 0);
    ib.flush();
    CHECK(dm.persisted_next_seq()   == 4);                        // DM next-seq (3 records -> next 4) now durable
    CHECK(ch.persisted_next_seq()   == 2);                        // channel next-seq (1 record -> next 2)
}

TEST_CASE("inbox: a FAILED set_next_seq keeps the batch so the next append retries (does not swallow it)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    dm.fail_set_next = true;
    for (uint16_t i = 1; i <= 8; ++i) rec_dm(ib, 1, i, "m", 0);   // the 8th append hits the batch boundary -> set_next_seq fails
    CHECK(dm.failed_set_next_calls >= 1);                         // it WAS attempted
    CHECK(dm.persisted_next_seq() == 0);                          // ...and not advanced (write failed)
    dm.fail_set_next = false;                                     // flash recovers
    rec_dm(ib, 1, 9, "m", 0);                                     // next append RETRIES (batch was kept, not reset)
    CHECK(dm.persisted_next_seq() == 10);                         // 9 records -> next 10, now persisted
}
