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
#include "frame_codec.h"   // DATA_TYPE_E2E_ACK (the receipt `type` value)

#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// pull collector: copy the decoded entry (body points into transient store bytes -> copy to a std::string).
struct Collected { uint32_t seq; InboxKind kind; uint8_t origin; uint8_t channel_id; uint32_t msg_id;
                   uint32_t sender_hash; uint8_t layer_id; uint8_t enc; uint8_t type; uint8_t origin_layer; uint64_t rx; std::string body; };
struct Collector { std::vector<Collected> items; };
bool collect_cb(void* ctx, const InboxEntry& e) {
    auto* c = static_cast<Collector*>(ctx);
    c->items.push_back({ e.seq, e.kind, e.origin, e.channel_id, e.msg_id, e.sender_hash, e.layer_id, e.enc, e.type, e.origin_layer, e.rx_time_ms,
                         std::string(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len) });
    return true;
}

// A synthetic channel_msg_id (origin<<24 | key_hash16<<8 | ctr8) — the full 32-bit identity the inbox stores.
uint32_t mk_chan_id(uint8_t origin, uint8_t ctr8) { return (uint32_t(origin) << 24) | (uint32_t(0x1234) << 8) | ctr8; }

void rec_dm(Inbox& ib, uint8_t origin, uint16_t ctr, const char* s, uint64_t t, uint32_t sender_hash = 0, uint8_t layer_id = 0, uint8_t enc = 0) {
    ib.record_dm(origin, sender_hash, ctr, layer_id, reinterpret_cast<const uint8_t*>(s), static_cast<uint8_t>(std::strlen(s)), t, enc);
}
void rec_ch(Inbox& ib, uint8_t ch, uint8_t origin, uint8_t ctr8, const char* s, uint64_t t, uint8_t layer_id = 0) {
    ib.record_channel(ch, mk_chan_id(origin, ctr8), layer_id, reinterpret_cast<const uint8_t*>(s), static_cast<uint8_t>(std::strlen(s)), t);
}

}  // namespace

// §8b: the `enc` (sealed-delivery) flag round-trips through serialize -> store -> pull. DMs carry their crypted-on-delivery
// state; channels are always cleartext (enc=0).
TEST_CASE("inbox: §8b enc flag survives the record round-trip (DM enc 0/1; channel always 0)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 7, "sealed",  1000, /*sender_hash*/ 0xABCD, /*layer_id*/ 0, /*enc*/ 1);
    rec_dm(ib, 2, 8, "plain",   1001, /*sender_hash*/ 0xABCD, /*layer_id*/ 0, /*enc*/ 0);
    rec_ch(ib, 5, 9, 1, "chan", 1002);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 3);
    if (c.items.size() == 3) {
        CHECK(c.items[0].body == "sealed"); CHECK(c.items[0].enc == 1);   // CRYPTED DM
        CHECK(c.items[1].body == "plain");  CHECK(c.items[1].enc == 0);   // plaintext DM
        CHECK(c.items[2].kind == InboxKind::channel); CHECK(c.items[2].enc == 0);  // channel (cleartext)
    }
}

// §GapA-durable (B3): the XL sender's origin_layer round-trips serialize -> store -> pull (0 = same-layer / non-XL).
TEST_CASE("inbox: §GapA-durable origin_layer survives the record round-trip (XL sender's layer; 0 for same-layer)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    ib.record_dm(101, /*sender_hash*/0x2716EFCDu, /*ctr*/7, /*layer_id*/23, reinterpret_cast<const uint8_t*>("xl"), 2, /*now*/1000, /*enc*/1, /*origin_layer*/4);
    ib.record_dm(102, /*sender_hash*/0x3A3E77A3u, /*ctr*/8, /*layer_id*/23, reinterpret_cast<const uint8_t*>("same"), 4, /*now*/1001, /*enc*/0, /*origin_layer*/0);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 2);
    if (c.items.size() == 2) {
        CHECK(c.items[0].body == "xl");   CHECK(c.items[0].origin_layer == 4);   // ★ the cross-layer sender's layer preserved durably
        CHECK(c.items[1].body == "same"); CHECK(c.items[1].origin_layer == 0);   // same-layer -> 0
    }
}

// InternalFS self-heal Part 3 (2026-06-24): flush() must NOT write a store with nothing un-persisted (the old
// unconditional 30 s set_next_seq was a top InternalFS write-churn source). It writes only the dirty store(s).
TEST_CASE("inbox: flush() is a no-op when nothing un-persisted; writes only a dirty store") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    ib.flush();                                          // fresh inbox, no appends -> NO writes
    CHECK(dm.set_next_calls == 0);
    CHECK(ch.set_next_calls == 0);
    rec_dm(ib, 2, 7, "hi", 1000);                        // one DM append (< the 8-batch) -> DM store dirty, channel quiet
    ib.flush();
    CHECK(dm.set_next_calls == 1);                       // the dirty DM store persisted
    CHECK(ch.set_next_calls == 0);                       // the quiet channel store NOT touched
    ib.flush();                                          // nothing new -> still no further writes
    CHECK(dm.set_next_calls == 1);
    CHECK(ch.set_next_calls == 0);
    rec_ch(ib, 5, 9, 1, "c", 1001);                      // now a channel append
    ib.flush();
    CHECK(dm.set_next_calls == 1);                       // DM still untouched (no new DM appends)
    CHECK(ch.set_next_calls == 1);                       // channel persisted
}

// E2E-ack durable receipt (2026-06-23): record_ack writes a DM-store entry with type=DATA_TYPE_E2E_ACK, no body,
// origin = the dest that confirmed, msg_id = the acked ctr. The `type` byte round-trips serialize -> store -> pull;
// a normal record_dm stays type=0. (spec native unit (a)/(c)/(d).)
TEST_CASE("inbox: record_ack stores an E2E-ack RECEIPT (type=E2E_ACK, no body) on the DM seq-space; normal DM type=0") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 7, 100, "hello", 1000);                                   // a normal DM (seq 1, type 0)
    const uint32_t ack_seq = ib.record_ack(/*from_origin=*/3, /*acked_ctr=*/55, /*layer_id=*/9, /*now=*/2000, /*acker_hash=*/0xC0FFEEu);
    CHECK(ack_seq == 2);                                                 // rides the DM seq-space (no third cursor)
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 2);
    if (c.items.size() == 2) {
        CHECK(c.items[0].type == 0);                                     // the normal DM is type 0
        CHECK(c.items[0].body == "hello");
        const auto& r = c.items[1];                                      // the receipt
        CHECK(r.kind == InboxKind::dm);
        CHECK(r.type == DATA_TYPE_E2E_ACK);
        CHECK(r.origin == 3);                                            // the dest that confirmed delivery
        CHECK(r.msg_id == 55);                                           // = the acked ctr
        CHECK(r.sender_hash == 0xC0FFEEu);                               // cross-layer acker hash (the stable match key)
        CHECK(r.layer_id == 9);
        CHECK(r.body.empty());                                           // a receipt has no body
        CHECK(r.rx == 2000);
    }
    CHECK(ib.dm_newest_seq() == 2); CHECK(ib.chan_newest_seq() == 0);    // receipt on the DM space; channel space untouched
}

TEST_CASE("inbox: disabled until stores are installed (record_* / pull inert)") {
    Inbox ib;                                                     // no on_init
    CHECK_FALSE(ib.enabled());
    rec_dm(ib, 5, 1, "ignored", 0);                              // no store -> no-op, no crash
    Collector c; CHECK(ib.pull(0, 0, collect_cb, &c) == 0);
    CHECK(c.items.empty());
}

TEST_CASE("inbox: record_dm/record_channel RETURN the assigned seq (model-B live-push stamp); 0 when disabled") {
    // Disabled -> 0: the live push then omits seq -> the app treats it as best-effort live only (no gap-pull).
    Inbox off;
    CHECK(off.record_dm(5, 0, 1, /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("x"), 1, 0) == 0);
    CHECK(off.record_channel(2, mk_chan_id(9, 1), /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("x"), 1, 0) == 0);
    // Enabled -> the assigned per-store seq, monotonic, with INDEPENDENT DM / channel spaces. This is the
    // exact value stamped into the live Push, so live + pulled dedup/order on the same seq.
    RamInboxStore rdm(protocol::inbox_dm_store_bytes), rch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&rdm, &rch);
    CHECK(ib.record_dm(5, 0, 100, /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("a"), 1, 0) == 1);
    CHECK(ib.record_dm(7, 0, 101, /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("b"), 1, 0) == 2);
    CHECK(ib.record_channel(2, mk_chan_id(9, 0x42), /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("c"), 1, 0) == 1);  // channel space starts at 1
    CHECK(ib.record_dm(8, 0, 102, /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("d"), 1, 0) == 3);
    CHECK(ib.record_channel(2, mk_chan_id(9, 0x43), /*layer_id*/ 0, reinterpret_cast<const uint8_t*>("e"), 1, 0) == 2);
    CHECK(ib.dm_newest_seq() == 3); CHECK(ib.chan_newest_seq() == 2);
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
    CHECK(c.items[0].kind == InboxKind::dm); CHECK(c.items[0].origin == 5); CHECK(c.items[0].msg_id == 100);   // DM msg_id = ctr
    CHECK(c.items[0].body == "hi-bob"); CHECK(c.items[0].rx == 1000); CHECK(c.items[0].seq == 1);
    CHECK(c.items[1].kind == InboxKind::dm); CHECK(c.items[1].origin == 7); CHECK(c.items[1].body == "yo");
    CHECK(c.items[1].seq == 2);
    CHECK(c.items[2].kind == InboxKind::channel); CHECK(c.items[2].channel_id == 2); CHECK(c.items[2].origin == 9);
    CHECK(c.items[2].msg_id == mk_chan_id(9, 0x42));          // FULL 32-bit channel_msg_id (origin in the high byte)
    CHECK(c.items[2].body == "hello-chan"); CHECK(c.items[2].seq == 1);   // independent seq space
    CHECK(ib.dm_newest_seq() == 2); CHECK(ib.chan_newest_seq() == 1);
}

TEST_CASE("inbox: the receiving layer_id round-trips through serialize -> store -> pull (Slice 4a' / §2/Q13)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "on-23", 1000, /*sender_hash*/ 0xABCDu, /*layer_id*/ 23);   // a DM heard on layer 23
    rec_dm(ib, 5, 101, "on-39", 1001, /*sender_hash*/ 0xABCDu, /*layer_id*/ 39);   // SAME origin/sender, different layer
    rec_ch(ib, 2, 9, 0x42, "ch-on-7", 1002, /*layer_id*/ 7);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 3);
    CHECK(c.items[0].layer_id == 23);   // the receiving layer survived the durable record (24->25 B header)
    CHECK(c.items[1].layer_id == 39);   // same (origin 5, ctr-pair) but a distinct layer — the disambiguation §2/Q13 demands
    CHECK(c.items[2].layer_id == 7);    // channels carry it too
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
    CHECK(c.items.back().msg_id == 10);                           // DM msg_id = ctr (the 10th)
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
    ib.record_dm(2, /*sender_hash*/ 0, 2, /*layer_id*/ 0, reinterpret_cast<const uint8_t*>(big.data()), protocol::inbox_max_body, 0);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 2);
    CHECK(c.items[0].body.empty());
    CHECK(c.items[1].body.size() == protocol::inbox_max_body);
    CHECK(c.items[1].body == big);

    // a single record larger than the whole store -> append rejects (never reached given inbox_max_body, but the guard holds)
    RamInboxStore tiny(20), ch2(protocol::inbox_chan_store_bytes);   // < one record (24-B header + body + 2-B frame)
    Inbox ib2; ib2.on_init(&tiny, &ch2);
    rec_dm(ib2, 1, 1, "x", 0);
    CHECK(tiny.count() == 0);                                     // not stored (and seq still advanced — monotonic, not gapless)
}

TEST_CASE("inbox: DM sender_hash (the stable identity, SOURCE_HASH) round-trips; absent/channel = 0") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, /*origin*/ 5, /*ctr*/ 42, "hi", 0, /*sender_hash*/ 0xDEADBEEFu);   // an E2E/SOURCE_HASH DM
    rec_dm(ib, /*origin*/ 6, /*ctr*/ 7,  "plain", 0);                              // sender_hash absent -> 0
    rec_ch(ib, /*ch*/ 2, /*origin*/ 9, /*ctr8*/ 1, "c", 0);
    Collector c; ib.pull(0, 0, collect_cb, &c);
    CHECK(c.items.size() == 3);
    CHECK(c.items[0].kind == InboxKind::dm);      CHECK(c.items[0].sender_hash == 0xDEADBEEFu);  // the stable DM identity (sender_hash, ctr)
    CHECK(c.items[0].msg_id == 42);
    CHECK(c.items[1].kind == InboxKind::dm);      CHECK(c.items[1].sender_hash == 0);             // absent -> (origin, ctr)
    CHECK(c.items[2].kind == InboxKind::channel); CHECK(c.items[2].sender_hash == 0);             // channels identify by channel_msg_id
}

TEST_CASE("inbox: storage_epoch is surfaced from the DM store (the companion's wipe-detector, §10.1)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    dm.epoch = 7;                                                 // device bumped it on a wipe
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.storage_epoch() == 7);
    Inbox off;                                                    // disabled inbox -> 0 (no durable epoch)
    CHECK(off.storage_epoch() == 0);
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

// ===================================================================================================
// §3.5 / §6.2 — DURABLE SINGLE-RECORD DELETE (owner ruling 2026-08-06: an appended TOMBSTONE; no rewrite,
// no segment erase). ★ Every case asserts the SIDE EFFECT — the record's ABSENCE from a real pull sweep —
// never the return code alone. A delete that returns `erased` and leaves the record readable is exactly the
// "a contract event asserting a physical act" defect this arc has now hit four times.
// ===================================================================================================
namespace {

// Does a full pull still yield a record with this (kind, seq)?
bool pull_has(Inbox& ib, InboxKind kind, uint32_t seq) {
    Collector c; ib.pull(0, 0, collect_cb, &c);
    for (const auto& it : c.items) if (it.kind == kind && it.seq == seq) return true;
    return false;
}
// Bodies of the DM block, in pull order (proves the survivors keep their original order).
std::vector<std::string> pull_bodies(Inbox& ib, InboxKind kind) {
    Collector c; ib.pull(0, 0, collect_cb, &c);
    std::vector<std::string> v;
    for (const auto& it : c.items) if (it.kind == kind) v.push_back(it.body);
    return v;
}
// RAW store sweep — bypasses Inbox entirely, so it sees the tombstone records the pull filter hides.
// `type` sits at byte 25 of the record header (4+1+1+1+4+4+8+1+1); the target seq is the msg_id at byte 7.
struct RawScan { uint16_t records = 0; uint16_t tombs = 0; uint32_t last_tomb_target = 0; bool tomb_was_last = false; };
bool raw_scan_cb(void* ctx, uint32_t, const uint8_t* rec, uint16_t len) {
    auto* r = static_cast<RawScan*>(ctx);
    ++r->records;
    const bool is_tomb = (len > 25 && rec[25] == inbox_rec_type_tombstone);
    r->tomb_was_last = is_tomb;                                   // overwritten each record -> true iff the LAST one is a tombstone
    if (is_tomb) { ++r->tombs; r->last_tomb_target = uint32_t(rec[7]) | (uint32_t(rec[8]) << 8) | (uint32_t(rec[9]) << 16) | (uint32_t(rec[10]) << 24); }
    return true;
}
RawScan raw_scan(const InboxStore& s) { RawScan r; s.read_since(0, raw_scan_cb, &r); return r; }

}  // namespace

// §3.5/1 — the happy path, asserted as a SIDE EFFECT: the record vanishes from pull, the survivors keep their
// order, and the tombstone record itself is never emitted as a message.
TEST_CASE("inbox §3.5/1: erase(dm, seq) removes THAT record from pull; survivors keep order; no tombstone leaks") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 1, "alpha", 100);  rec_dm(ib, 2, 2, "bravo", 200);  rec_dm(ib, 2, 3, "charlie", 300);
    CHECK(pull_has(ib, InboxKind::dm, 2));                        // premise: bravo IS readable before the delete
    CHECK(ib.erase(InboxKind::dm, 2) == InboxEraseResult::erased);
    CHECK(!pull_has(ib, InboxKind::dm, 2));                       // ★ THE SIDE EFFECT, not the return code
    const std::vector<std::string> b = pull_bodies(ib, InboxKind::dm);
    CHECK(b.size() == 2);
    if (b.size() == 2) { CHECK(b[0] == "alpha"); CHECK(b[1] == "charlie"); }   // order preserved, nothing else touched
    Collector c; const uint16_t visited = ib.pull(0, 0, collect_cb, &c);
    CHECK(visited == 2);                                          // the count excludes BOTH the deleted record and the marker
    for (const auto& it : c.items) CHECK(it.type != inbox_rec_type_tombstone);
    // ...and the marker really is in the store: the pull filter is what hides it, not its absence.
    const RawScan r = raw_scan(dm);
    CHECK(r.records == 4);                                        // 3 messages + 1 tombstone
    CHECK(r.tombs == 1);
    CHECK(r.last_tomb_target == 2);
}

// §3.5/2 — ★★ HAZARD 1, THE ORDERING TRAP, PINNED DIRECTLY. The tombstone is appended AFTER its target, so a
// single-pass streaming reader cannot filter a marker it has not seen yet. This case proves the marker is
// physically LAST in the log and the target is STILL not emitted.
TEST_CASE("inbox §3.5/2: the tombstone is the LAST record in the log and the target is still filtered (ordering)") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 1, "keep-me", 100);
    rec_dm(ib, 2, 2, "delete-me", 200);
    CHECK(ib.erase(InboxKind::dm, 2) == InboxEraseResult::erased);
    const RawScan r = raw_scan(dm);
    CHECK(r.tomb_was_last);                                       // premise: the marker follows its target in the log
    CHECK(r.last_tomb_target == 2);
    CHECK(!pull_has(ib, InboxKind::dm, 2));                       // ...and the pre-pass still caught it
    CHECK(pull_has(ib, InboxKind::dm, 1));
    // Same guarantee when the cursor sits just below the deleted record (the pre-pass uses the SAME `since`).
    Collector c; ib.pull(/*dm_since*/ 1, 0, collect_cb, &c);
    CHECK(c.items.empty());
}

// §3.5/3 — ★★ IDENTITY IS THE PAIR. Both stores hold a seq 2; erasing the DM one must not touch the channel one.
// ⚠ This is the case that must go RED if (kind, seq) is ever collapsed to seq alone.
TEST_CASE("inbox §3.5/3: identity is (kind, seq) — the two seq spaces are independent") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 1, "dm-one", 100);   rec_dm(ib, 2, 2, "dm-two", 200);       // DM   seq 1, 2
    rec_ch(ib, 5, 9, 1, "ch-one", 300); rec_ch(ib, 5, 9, 2, "ch-two", 400);   // CHAN seq 1, 2 (independent space)
    CHECK(pull_has(ib, InboxKind::dm, 2));  CHECK(pull_has(ib, InboxKind::channel, 2));
    CHECK(ib.erase(InboxKind::dm, 2) == InboxEraseResult::erased);
    CHECK(!pull_has(ib, InboxKind::dm, 2));                       // the DM went
    CHECK(pull_has(ib, InboxKind::channel, 2));                   // ★ the SAME-numbered channel record did NOT
    CHECK(raw_scan(ch).tombs == 0);                               // and no marker was written into the wrong store
    CHECK(ib.erase(InboxKind::channel, 2) == InboxEraseResult::erased);
    CHECK(!pull_has(ib, InboxKind::channel, 2));
    CHECK(pull_has(ib, InboxKind::channel, 1));                   // its neighbour survived
    CHECK(pull_has(ib, InboxKind::dm, 1));
}

// §3.5/4 — ★★ THE DELETE SURVIVES A REBOOT (re-on_init over the SAME store objects = the persisted state), and
// the seq high-water does not regress: the next message gets a NEW seq, never a reused one.
TEST_CASE("inbox §3.5/4: a delete survives a reboot; the record never reappears; no seq is reused") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    {
        Inbox ib; ib.on_init(&dm, &ch);
        rec_dm(ib, 2, 1, "alpha", 100);  rec_dm(ib, 2, 2, "bravo", 200);
        CHECK(ib.erase(InboxKind::dm, 2) == InboxEraseResult::erased);
        CHECK(ib.dm_newest_seq() == 3);                           // the tombstone consumed seq 3 (a HOLE, not a rewind)
    }
    Inbox ib2; ib2.on_init(&dm, &ch);                             // ---- reboot ----
    CHECK(!pull_has(ib2, InboxKind::dm, 2));                      // ★ still gone after the restore
    CHECK(pull_has(ib2, InboxKind::dm, 1));
    rec_dm(ib2, 2, 9, "post-reboot", 500);
    CHECK(ib2.dm_newest_seq() == 4);                              // 4, never a reuse of 2 or 3
    CHECK(pull_has(ib2, InboxKind::dm, 4));
    CHECK(!pull_has(ib2, InboxKind::dm, 2));                      // and the new record did not resurrect the old one
    CHECK(ib2.erase(InboxKind::dm, 2) == InboxEraseResult::not_found);   // a second delete after reboot: GONE, not failed
}

// §3.5/5 — `not_found` is its own outcome (MESSAGE GONE), distinct from success and from failure, in all three
// ways it arises: never existed, evicted by the bounded ring, already deleted. None of them removes anything.
TEST_CASE("inbox §3.5/5: not_found — never existed / evicted / already deleted, and nothing else is touched") {
    // A 40-B body -> a 72-B record, 74 framed; a 300-B cap therefore holds FOUR, so six appends evict seq 1 and 2.
    RamInboxStore dm(/*cap*/ 300), ch(protocol::inbox_chan_store_bytes);   // tiny cap -> drop-oldest evicts
    Inbox ib; ib.on_init(&dm, &ch);
    CHECK(ib.erase(InboxKind::dm, 7) == InboxEraseResult::not_found);      // empty store, no such seq
    CHECK(ib.erase(InboxKind::dm, 0) == InboxEraseResult::not_found);      // seq 0 is a cursor, never a record
    for (uint16_t i = 1; i <= 6; ++i) rec_dm(ib, 2, i, "0123456789012345678901234567890123456789", 100 + i);
    CHECK(!pull_has(ib, InboxKind::dm, 1));                                // premise: seq 1 was EVICTED by the cap
    CHECK(ib.erase(InboxKind::dm, 1) == InboxEraseResult::not_found);      // ...so deleting it is GONE, not a failure
    CHECK(raw_scan(dm).tombs == 0);                                        // ★ and no marker was written for it
    const uint32_t newest = ib.dm_newest_seq();
    CHECK(pull_has(ib, InboxKind::dm, newest));
    CHECK(ib.erase(InboxKind::dm, newest) == InboxEraseResult::erased);
    CHECK(!pull_has(ib, InboxKind::dm, newest));
    CHECK(ib.erase(InboxKind::dm, newest) == InboxEraseResult::not_found); // a REPEAT delete: gone, not an error
    CHECK(raw_scan(dm).tombs == 1);                                        // ...and it did NOT write a second marker
}

// §3.5/6 — a storage failure is `io_error` and DELETES NOTHING. ⛔ "a visual disappearance without durable
// success is forbidden" (§3.5) — so the record must still be readable after a failed erase.
TEST_CASE("inbox §3.5/6: io_error on a failed append — a failed delete omits nothing") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 1, "survivor", 100);
    dm.fail_append = true;                                        // flash write dies
    CHECK(ib.erase(InboxKind::dm, 1) == InboxEraseResult::io_error);
    CHECK(dm.failed_append_calls == 1);                           // the append WAS attempted (not short-circuited)
    dm.fail_append = false;
    CHECK(pull_has(ib, InboxKind::dm, 1));                        // ★ STILL THERE — no visual disappearance
    CHECK(raw_scan(dm).tombs == 0);
    CHECK(ib.erase(InboxKind::dm, 1) == InboxEraseResult::erased); // and it can be retried once flash recovers
    CHECK(!pull_has(ib, InboxKind::dm, 1));
}

// §3.5/7 — an UNWIRED inbox reports io_error, never success (§6.2, verbatim).
TEST_CASE("inbox §3.5/7: a disabled inbox reports io_error, never erased") {
    Inbox off;
    CHECK(off.erase(InboxKind::dm, 1) == InboxEraseResult::io_error);
    CHECK(off.erase(InboxKind::channel, 1) == InboxEraseResult::io_error);
}

// §3.5/8 — ★★ HAZARD 2, THE TOMBSTONE BOUND. The read pre-pass holds inbox_max_tombstones targets; erase refuses
// the one that would overflow it. The refusal is io_error (a loud DELETE FAILED), and — the point of the case —
// every one of the capped deletes is STILL filtered, so nothing resurrects for want of array space.
TEST_CASE("inbox §3.5/8: the tombstone cap is enforced at the WRITER, and no capped delete ever resurrects") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    const uint16_t n = protocol::inbox_max_tombstones;
    std::vector<uint32_t> victims;
    for (uint16_t i = 0; i < n + 1; ++i) victims.push_back(ib.record_dm(2, 0, i, 0, reinterpret_cast<const uint8_t*>("x"), 1, 1000 + i));
    for (uint16_t i = 0; i < n; ++i) CHECK(ib.erase(InboxKind::dm, victims[i]) == InboxEraseResult::erased);
    CHECK(raw_scan(dm).tombs == n);
    const InboxEraseResult over = ib.erase(InboxKind::dm, victims[n]);
    CHECK(over == InboxEraseResult::io_error);                    // the (n+1)-th: refused LOUD
    // ★ The outcome and what the reader SEES must agree, whatever the outcome is. Drop the writer's cap and this
    // is the assert that fires: erase reports `erased` while the pre-pass array has no room for the (n+1)-th
    // marker, so the record is still emitted — a deleted message resurrected for want of space.
    CHECK(pull_has(ib, InboxKind::dm, victims[n]) == (over != InboxEraseResult::erased));
    for (uint16_t i = 0; i < n; ++i) CHECK(!pull_has(ib, InboxKind::dm, victims[i]));   // ★ all n stay deleted
    Collector c; CHECK(ib.pull(0, 0, collect_cb, &c) == 1);       // exactly the one that refused to delete
}

// §3.5/9 — a one-record delete is NOT a store wipe: next_seq, the read cursor and the storage epoch keep their
// meanings, so the companion must not be pushed into resetting both cursors (§6.2).
TEST_CASE("inbox §3.5/9: a delete is not a wipe — read cursor and storage epoch are untouched") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    dm.epoch = 5;
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 2, 1, "alpha", 100);  rec_dm(ib, 2, 2, "bravo", 200);
    ib.mark_read(InboxKind::dm, 2);
    CHECK(ib.erase(InboxKind::dm, 1) == InboxEraseResult::erased);
    CHECK(dm.read_cursor() == 2);                                 // cursor unmoved by the delete
    CHECK(ib.storage_epoch() == 5);                               // epoch unmoved (this is not a wipe)
    CHECK(ch.count() == 0);                                       // the other store was never opened
}
