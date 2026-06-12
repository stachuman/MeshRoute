// MeshRoute — test_fixed_inbox_store.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the heap-free FixedInboxStore (lib/core/fixed_inbox_store.h) — the interim volatile
// device inbox: append + read_since oldest-first + since-filter + drop-oldest-at-capacity + cursor/epoch +
// body round-trip. NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "fixed_inbox_store.h"

using namespace meshroute;

namespace {
struct Collector { uint32_t seqs[64] = {}; uint16_t n = 0; };
bool collect_cb(void* ctx, uint32_t seq, const uint8_t*, uint16_t) {
    Collector* c = static_cast<Collector*>(ctx);
    if (c->n < 64) c->seqs[c->n++] = seq;
    return true;
}
}

TEST_CASE("FixedInboxStore: append + read_since oldest-first + since-filter + count") {
    FixedInboxStore<4> s;
    CHECK(s.begin());
    CHECK(s.count() == 0);
    const uint8_t r[] = { 1, 2, 3 };
    CHECK(s.append(1, r, 3));
    CHECK(s.append(2, r, 3));
    CHECK(s.count() == 2);

    Collector c{};
    CHECK(s.read_since(0, collect_cb, &c) == 2);
    CHECK(c.n == 2);
    CHECK(c.seqs[0] == 1);                      // oldest-first
    CHECK(c.seqs[1] == 2);

    Collector c2{};
    CHECK(s.read_since(1, collect_cb, &c2) == 1);   // seq > since
    CHECK(c2.n == 1);
    CHECK(c2.seqs[0] == 2);
}

TEST_CASE("FixedInboxStore: drop-oldest at capacity keeps the newest") {
    FixedInboxStore<3> s;
    const uint8_t r[] = { 9 };
    for (uint32_t i = 1; i <= 5; ++i) CHECK(s.append(i, r, 1));   // 5 records into 3 slots

    CHECK(s.count() == 3);
    Collector c{};
    s.read_since(0, collect_cb, &c);
    CHECK(c.n == 3);
    CHECK(c.seqs[0] == 3);                      // 1 and 2 evicted; newest kept, oldest-first
    CHECK(c.seqs[1] == 4);
    CHECK(c.seqs[2] == 5);
}

TEST_CASE("FixedInboxStore: cursor + epoch (0 -> 1) + volatile next-seq + body round-trip") {
    FixedInboxStore<2> s;
    s.set_epoch(0);  CHECK(s.storage_epoch() == 1);    // 0 is reserved ("no durable epoch") -> forced to 1
    s.set_epoch(42); CHECK(s.storage_epoch() == 42);

    CHECK(s.read_cursor() == 0);
    CHECK(s.set_read_cursor(7));
    CHECK(s.read_cursor() == 7);
    CHECK(s.persisted_next_seq() == 0);                // volatile -> no persisted backstop

    const uint8_t body[] = { 0xAA, 0xBB, 0xCC };
    CHECK(s.append(1, body, 3));
    struct BodyCheck { bool ok = false; } bc;
    s.read_since(0, [](void* ctx, uint32_t, const uint8_t* rec, uint16_t len) {
        static_cast<BodyCheck*>(ctx)->ok = (len == 3 && rec[0] == 0xAA && rec[1] == 0xBB && rec[2] == 0xCC);
        return true;
    }, &bc);
    CHECK(bc.ok);
}
