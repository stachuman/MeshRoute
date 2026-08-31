// MeshRoute — test_custody_internal_d.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D — INBOX-ONLY CLEAR (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` §7.5, §17-Slice-D, §18.3/9).
// Slice C's file holds the READ side (what an ordinary view shows); this one holds the DESTRUCTIVE verb:
// `clear_inbox confirm` — its refusal, its persistence ORDER, its shared epoch, its partial-failure honesty and
// the isolation of everything outside the two stores.
//
// ★★★ THE THREE PROPERTIES THIS FILE EXISTS FOR, STATED ONCE, because each is a defect that a naive
//     "wipe both stores" implementation ships pre-installed:
//   ① THE BATCH-PERSIST GAP. `Inbox::record` persists the next-seq counter every EIGHT appends, so between
//      batches the records themselves are the only witness of the high-water. Erase them while the persisted
//      metadata still says the older value and the next boot REUSES sequences. ⛔ An in-memory assertion cannot
//      see this — `_dm_next` is untouched by a wipe — so every high-water case here REMOUNTS a fresh `Inbox`
//      over the cleared durable stores and asks THAT one.
//   ② THE SHARED EPOCH. The public contract exposes ONE epoch (`Inbox::storage_epoch()` = the DM store's; the
//      boot banner prints that one number). A per-store "bump only what held something" rule therefore leaves
//      the visible epoch UNCHANGED when a non-empty channel store is cleared beside an empty DM store — and the
//      companion's whole wipe-detector is that number.
//   ③ PARTIAL FAILURE IS A STATE. Two independent wipes cannot be atomic. Both are attempted, success requires
//      BOTH, and failure NEVER prints `cleared`.
//
// ⛔ NOT A STORE TEST. `SegmentedInboxStore`'s own ring/meta behaviour is pinned in test_segmented_inbox_store.cpp
//    and the RAM ring's in test_fixed_inbox_store.cpp; these cases drive the ORCHESTRATION above them.
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "inbox.h"
#include "fixed_inbox_store.h"
#include "frame_codec.h"
#include "node.h"
#include "protocol_constants.h"
#include "ram_inbox_store.h"
#include "fake_inbox_storage.h"      // the DURABLE store's two backends — the meta survives a records wipe
#include "console_json.h"            // the frozen ack family + the hoisted verdict -> lexeme decision
#include "firmware_config_parse.h"   // mrfw::parse_confirm_token — the §B115-hoisted confirmation interlock
#include "firmware_ui_model.h"       // mrui::InboxRowBudget — the OLED list this verb must need no change to
#include "support/test_hal.h"

#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// ---- record helpers ------------------------------------------------------------------------------------------
uint32_t rec_dm(Inbox& ib, uint8_t origin, uint16_t ctr, const char* s, uint64_t t) {
    return ib.record_dm(origin, /*sender_hash*/ 0, ctr, /*layer_id*/ 0,
                        reinterpret_cast<const uint8_t*>(s), uint8_t(std::strlen(s)), t);
}
uint32_t rec_chan(Inbox& ib, uint8_t chan, uint32_t msg_id, const char* s, uint64_t t) {
    return ib.record_channel(chan, msg_id, /*layer_id*/ 0,
                             reinterpret_cast<const uint8_t*>(s), uint8_t(std::strlen(s)), t);
}

struct PulledRec { uint32_t seq; InboxKind kind; uint8_t type; std::string body; };
bool pull_cb(void* ctx, const InboxEntry& e) {
    static_cast<std::vector<PulledRec>*>(ctx)->push_back(
        { e.seq, e.kind, e.type,
          std::string(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len) });
    return true;
}
std::vector<PulledRec> raw_pull(const Inbox& ib) {
    std::vector<PulledRec> v; ib.pull(0, 0, pull_cb, &v); return v;
}
// The RAW record count (pull() filters tombstones and their targets; a wipe must remove those too).
uint16_t raw_records(const InboxStore& s) {
    struct C { uint16_t n; } c{ 0 };
    s.read_since(0, [](void* v, uint32_t, const uint8_t*, uint16_t) { ++static_cast<C*>(v)->n; return true; }, &c);
    return c.n;
}

// ---- the DURABLE pair: two SegmentedInboxStores over independent fakes, exactly as the device wires them ------
// ★ The meta stores are SEPARATE objects from the records stores, so a records wipe leaves the high-water/epoch
//   on the medium — which is the split the whole §7.5.3 preservation claim rests on.
struct DurablePair {
    FakeSegmentStore dm_recs, ch_recs;
    FakeMetaStore    dm_meta, ch_meta;
    SegmentedInboxStore dm{ dm_recs, dm_meta, /*cap*/4096, /*seg*/256 };
    SegmentedInboxStore ch{ ch_recs, ch_meta, /*cap*/4096, /*seg*/256 };
    Inbox ib;
    DurablePair() { CHECK(dm.begin()); CHECK(ch.begin()); ib.on_init(&dm, &ch); }
    // ★★ THE REMOUNT. A FRESH Inbox over the SAME durable stores = the reboot. Everything the in-memory counters
    //    knew is gone; only what reached the medium answers. This is the ONLY way ① is observable.
    Inbox remount() { Inbox fresh; fresh.on_init(&dm, &ch); return fresh; }
};

}  // namespace

// =====================================================================================================
// §7.5.1 — WITHOUT THE EXACT `confirm` TOKEN, REFUSE AND CHANGE NOTHING
// =====================================================================================================

// ★★★★ THE HARDENED TOKEN CORPUS. A confirmation interlock that accepts a PREFIX is not an interlock: it turns
//      `clear_inbox confirmation-not-yet` — or a half-typed line, or a paste with a trailing word — into a
//      destroyed history. The predicate is `mrfw::parse_confirm_token`, hoisted out of `src/firmware_inbox.cpp`
//      (§B115: `src/*.cpp` is outside the native build) precisely so this corpus can exist at all.
TEST_CASE("§CUSTODY-D/1 the confirm token is EXACT — every near-miss refuses") {
    CHECK(mrfw::parse_confirm_token("confirm", 7));            // the ONE accepted form
    CHECK(mrfw::parse_confirm_token(" confirm", 8));           // the dispatcher's separating space is eaten
    CHECK(mrfw::parse_confirm_token("   confirm", 10));        // ...however many of them

    CHECK_FALSE(mrfw::parse_confirm_token("", 0));             // bare `clear_inbox`
    CHECK_FALSE(mrfw::parse_confirm_token("   ", 3));          // ...and with trailing spaces only
    CHECK_FALSE(mrfw::parse_confirm_token(nullptr, 0));        // fail-closed on no argument at all
    CHECK_FALSE(mrfw::parse_confirm_token(" confirm extra", 14));   // ★ "confirm extra" — a SECOND token
    CHECK_FALSE(mrfw::parse_confirm_token(" confirmation", 13));    // ★ "confirmation" — a longer word starting with it
    CHECK_FALSE(mrfw::parse_confirm_token(" confirmX", 9));         // ★ trailing junk, no separator
    CHECK_FALSE(mrfw::parse_confirm_token(" confirm ", 9));         // ★ a trailing space — stricter than parse_seq_arg, deliberately
    CHECK_FALSE(mrfw::parse_confirm_token(" confir", 7));           // the same LENGTH, different bytes
    CHECK_FALSE(mrfw::parse_confirm_token(" CONFIRM", 8));          // case-sensitive
    CHECK_FALSE(mrfw::parse_confirm_token(" Confirm", 8));
    CHECK_FALSE(mrfw::parse_confirm_token(" yes", 4));
}

// ★★ THE EQUIVALENCE PROOF, and why it is a MIRROR rather than a shared call. §7.5.6 requires this verb to
//    acquire no coupling that could widen `factory_reset`, and C1 forbids refactoring a second destructive verb's
//    parse inside a feature slice — so `handle_factory_reset` still carries its own expression. What must not
//    drift is the ACCEPT SET, so it is measured: the mirror below is `src/firmware_commands.cpp`'s expression
//    copied verbatim, and the two are required to agree over the whole corpus, near-misses included.
namespace {
bool factory_reset_token_mirror(const char* arg, size_t n) {   // VERBATIM from handle_factory_reset
    while (n && *arg == ' ') { ++arg; --n; }
    return n == 7 && !std::strncmp(arg, "confirm", 7);
}
}  // namespace
TEST_CASE("§CUSTODY-D/1b the token predicate accepts EXACTLY what factory_reset's own expression accepts") {
    static const char* corpus[] = { "confirm", " confirm", "   confirm", "", " ", "   ", "confirm extra",
                                    " confirm extra", "confirmation", "confirmX", "confirm ", "confir",
                                    "CONFIRM", "Confirm", "yes", "conf irm", " confirm\t", "confirm\r" };
    for (const char* s : corpus)
        CHECK(mrfw::parse_confirm_token(s, std::strlen(s)) == factory_reset_token_mirror(s, std::strlen(s)));
}

// ⛔⛔ CORRECTED IN PLACE 2026-08-31 (QG HOLD on §CUSTODY-D), AND THE CORRECTION IS THE POINT OF THE WHOLE ROUND.
//   ⓘ WHAT THIS NOTE CLAIMED: *"The verb's own early return (`handle_clear_inbox` refuses BEFORE `clear()` is
//     named) is structural; what a native case can own is that `clear()` is the ONLY thing that empties the inbox,
//     so a refusal path that never calls it cannot."* — and the case below described its own refusal arm as
//     *"modelled here by simply not calling clear()"*.
//   ⛔ WHY THAT WAS WRONG: "structural" was doing work it had not earned. `src/firmware_inbox.cpp` and
//     `src/firmware_commands.cpp` are compiled by NEITHER the native suite nor the simulator
//     (`platformio.ini` `test_build_src = no`), so NOTHING measured that `handle_clear_inbox` calls
//     `parse_confirm_token` at all, that its refusal returns before reaching `clear()`, or that
//     `dispatch()` routes `clear_inbox` to it. A case that "models" a refusal by not performing one is a
//     description of the intended behaviour, not a test of it: bypass the confirmation check in the real handler
//     and every assertion in this file stays GREEN while a bare `clear_inbox` destroys the inbox.
//   ✅ WHAT GATES IT NOW: `tools/probe_inbox_verbs/run.sh` — a feature probe that host-links the REAL
//     `firmware_commands.cpp` + `firmware_inbox.cpp` and DRIVES `mrfw::dispatch()`, asserting the exact refusal
//     bytes together with `wipe_calls == 0` on the store the verb would have destroyed. Its controls C1/C3/C4/C5
//     are the four defects this file could not see (arm deleted · check bypassed · early return removed · verdict
//     forced successful), each measured RED.
//   ⇒ THE CASE BELOW KEEPS ITS VALUE AND ITS SCOPE IS NOW STATED HONESTLY: it pins what the CORE owes a refusal —
//     that no store, cursor, high-water or epoch moves when `clear()` is not called, and that this survives a
//     remount. It does NOT show that the verb declines to call it; the probe does.
TEST_CASE("§CUSTODY-D/1c the CORE's half of a refusal: with clear() not called, nothing moves (see probe W2)") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "keep me", 1000);
    rec_chan(p.ib, 3, 0x07001122u, "channel", 1100);
    CHECK(p.ib.mark_read(InboxKind::dm, 1));
    const uint32_t epoch0 = p.ib.storage_epoch();
    const uint32_t dm_hi0 = p.ib.dm_newest_seq(), ch_hi0 = p.ib.chan_newest_seq();

    // The state below is read WITHOUT calling clear() — i.e. this asserts the core's invariants under a refusal,
    // ⛔ not that the verb refuses. That the verb refuses is `tools/probe_inbox_verbs` W2/W3, against the real
    // handler driven through the real router (see the corrected note above this case).
    CHECK(raw_pull(p.ib).size() == 2u);
    CHECK(p.ib.storage_epoch() == epoch0);
    CHECK(p.dm.read_cursor() == 1u);
    CHECK(p.ib.dm_newest_seq() == dm_hi0);
    CHECK(p.ib.chan_newest_seq() == ch_hi0);
    // ...and the records are still there after a REMOUNT too (nothing was quietly de-persisted).
    Inbox after = p.remount();
    CHECK(raw_pull(after).size() == 2u);
}

// =====================================================================================================
// §7.5.2 — A CONFIRMED CLEAR WIPES BOTH STORES: MESSAGES, RECEIPTS AND TOMBSTONES ALIKE
// =====================================================================================================

TEST_CASE("§CUSTODY-D/2 clear() empties BOTH stores — application DMs, an E2E receipt, a tombstone and channel posts") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "hello", 1000);
    const uint32_t doomed = rec_dm(p.ib, 5, 101, "delete me", 1100);
    p.ib.record_ack(/*from_origin*/ 7, /*acked_ctr*/ 100, /*layer_id*/ 0, 1200);   // an INTERNAL outcome record
    rec_chan(p.ib, 3, 0x07001122u, "post a", 1300);
    rec_chan(p.ib, 3, 0x07001123u, "post b", 1400);
    CHECK(p.ib.erase(InboxKind::dm, doomed) == InboxEraseResult::erased);          // appends a TOMBSTONE

    // Before: three DM-store records survive `pull` (the deleted one and its marker are filtered), five are on
    // the medium, and the channel store holds two.
    CHECK(raw_pull(p.ib).size() == 4u);            // hello + receipt + 2 posts (deleted DM + tombstone filtered)
    CHECK(raw_records(p.dm) == 4u);                // hello, doomed, receipt, tombstone
    CHECK(raw_records(p.ch) == 2u);

    CHECK(p.ib.clear());

    CHECK(raw_pull(p.ib).empty());                 // nothing pulls
    CHECK(raw_records(p.dm) == 0u);                // ★ the tombstone and its target are GONE from the medium too
    CHECK(raw_records(p.ch) == 0u);
    CHECK_FALSE(p.dm_recs.any_segments(nullptr));  // ★ and at the medium level: no segment holds bytes
    CHECK_FALSE(p.ch_recs.any_segments(nullptr));
    // ★ AND IT SURVIVES THE REMOUNT — an "empty" that only the RAM believed would be the [[B134]] lie again.
    Inbox after = p.remount();
    CHECK(raw_pull(after).empty());
}

// Slice C's raw-pull doctrine is UNTOUCHED and this is the case that says so: a cleared inbox pulls empty
// because it IS empty, never because a filter was added to `pull()`.
TEST_CASE("§CUSTODY-D/2b the empty pull is emptiness, not a filter — a NEW record after the clear pulls again") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "before", 1000);
    p.ib.record_ack(7, 100, 0, 1100);
    CHECK(p.ib.clear());
    CHECK(raw_pull(p.ib).empty());

    rec_dm(p.ib, 6, 200, "after", 2000);
    p.ib.record_ack(8, 200, 0, 2100);              // an internal record too — pull stays RAW (§7.4)
    const std::vector<PulledRec> v = raw_pull(p.ib);
    CHECK(v.size() == 2u);
    CHECK(v[0].body == "after");
    CHECK(v[1].type == DATA_TYPE_E2E_ACK);         // ★ the receipt streams verbatim, exactly as before the clear
}

// =====================================================================================================
// §7.5.3 — THE MONOTONIC HIGH-WATER SURVIVES  (QG correction 1: the batch-persist gap)
// =====================================================================================================

// ★★★★ THE CASE THE WHOLE ORCHESTRATION EXISTS FOR. Four records is FEWER than `kSeqPersistBatch` (8), so the
//      counter has NOT been auto-persisted: the records are the only witness. A bare store wipe here erases that
//      witness and the next boot restarts the sequence space — and a companion that has already filed seq 1..4
//      silently MISSES every message the node then hands those numbers to.
TEST_CASE("§CUSTODY-D/3 ★ the high-water is persisted BEFORE the erase — a remount never reuses a sequence") {
    DurablePair p;
    for (uint16_t i = 0; i < 4; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);   // < kSeqPersistBatch
    for (uint16_t i = 0; i < 3; ++i) rec_chan(p.ib, 3, 0x07000000u + i, "c", 1500 + i);
    const uint32_t dm_hi = p.ib.dm_newest_seq(), ch_hi = p.ib.chan_newest_seq();
    CHECK(dm_hi == 4u); CHECK(ch_hi == 3u);
    // ⛔ THE PRECONDITION, ASSERTED so the case cannot go vacuous: the medium does NOT yet know the high-water.
    //   Without this the test would pass against an implementation that never persisted anything.
    CHECK(p.dm.persisted_next_seq() < dm_hi + 1);
    CHECK(p.ch.persisted_next_seq() < ch_hi + 1);

    CHECK(p.ib.clear());

    // The LIVE object keeps counting up — necessary, and not nearly sufficient (it would pass with no persist).
    CHECK(p.ib.dm_newest_seq() == dm_hi);
    CHECK(p.ib.chan_newest_seq() == ch_hi);
    // ★★ THE REAL PROOF: a fresh Inbox over the cleared stores, i.e. after a reboot, with ZERO records to learn
    //    the high-water from. Its next assignment must still be GREATER than every sequence ever issued.
    Inbox after = p.remount();
    CHECK(after.dm_newest_seq() == dm_hi);                     // restored from the METADATA, not from records
    CHECK(after.chan_newest_seq() == ch_hi);
    const uint32_t next_dm = rec_dm(after, 9, 900, "post-clear", 9000);
    const uint32_t next_ch = rec_chan(after, 3, 0x09000000u, "post-clear", 9100);
    CHECK(next_dm == dm_hi + 1);                               // ★ 5, never 1 — no sequence is reused
    CHECK(next_ch == ch_hi + 1);                               // ★ 4, never 1
}

// The same property with the OTHER store empty — the asymmetric shape, because a per-store rule that happened to
// work when both stores were populated is exactly the class §7.5.5 caught one field along.
TEST_CASE("§CUSTODY-D/3b the high-water survives for a store that held nothing to erase") {
    DurablePair p;
    for (uint16_t i = 0; i < 3; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);
    CHECK(p.ib.chan_newest_seq() == 0u);                       // the channel store never held a record
    CHECK(p.ib.clear());
    Inbox after = p.remount();
    CHECK(after.dm_newest_seq() == 3u);
    CHECK(rec_dm(after, 9, 900, "x", 9000) == 4u);
    CHECK(rec_chan(after, 3, 0x09000000u, "x", 9100) == 1u);   // an untouched empty space still starts at 1
}

// =====================================================================================================
// §7.5.4 — BOTH READ CURSORS RESET
// =====================================================================================================

TEST_CASE("§CUSTODY-D/4 both read cursors are reset, on the medium, and stay reset across a remount") {
    DurablePair p;
    for (uint16_t i = 0; i < 3; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);
    for (uint16_t i = 0; i < 3; ++i) rec_chan(p.ib, 3, 0x07000000u + i, "c", 1500 + i);
    CHECK(p.ib.mark_read(InboxKind::dm, 3));
    CHECK(p.ib.mark_read(InboxKind::channel, 2));
    CHECK(p.dm.read_cursor() == 3u); CHECK(p.ch.read_cursor() == 2u);   // the precondition, so 0 means something

    CHECK(p.ib.clear());

    CHECK(p.dm.read_cursor() == 0u);                           // ★ a cursor past records that no longer exist
    CHECK(p.ch.read_cursor() == 0u);                           //   would leave the unread badge permanently wrong
    Inbox after = p.remount(); (void)after;
    CHECK(p.dm.read_cursor() == 0u);                           // ★ persisted, not merely a RAM reset
    CHECK(p.ch.read_cursor() == 0u);
}

// =====================================================================================================
// §7.5.5 — ONE SHARED EPOCH, BUMPED EXACTLY ONCE  (QG correction 2)
// =====================================================================================================

// ★★★★ THE ASYMMETRIC ARM, WHICH IS THE WHOLE CORRECTION. `Inbox::storage_epoch()` publishes the DM store's
//      value. Under a per-store "bump only what held history" rule this case's DM store — which is EMPTY —
//      bumps NOTHING, so a companion holding a channel cursor sees an unchanged epoch, resets nothing, and
//      keeps a cursor pointing into a history that has just been destroyed.
TEST_CASE("§CUSTODY-D/5 ★ empty DM + non-empty channel: the ONE published epoch still moves") {
    DurablePair p;
    for (uint16_t i = 0; i < 3; ++i) rec_chan(p.ib, 3, 0x07000000u + i, "c", 1500 + i);
    CHECK(raw_records(p.dm) == 0u);                            // the DM store held NOTHING
    const uint32_t before = p.ib.storage_epoch();

    CHECK(p.ib.clear());

    CHECK(p.ib.storage_epoch() == before + 1);                 // ★ the published (DM) epoch moved anyway
    CHECK(p.dm.storage_epoch() == p.ch.storage_epoch());       // ★ ...and BOTH stores hold the SAME target
    Inbox after = p.remount();
    CHECK(after.storage_epoch() == before + 1);                // persisted, on the empty store too
}

TEST_CASE("§CUSTODY-D/5b the inverse — non-empty DM + empty channel — lands both stores on one epoch") {
    DurablePair p;
    for (uint16_t i = 0; i < 3; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);
    CHECK(raw_records(p.ch) == 0u);
    const uint32_t before = p.ib.storage_epoch();
    CHECK(p.ib.clear());
    CHECK(p.ib.storage_epoch() == before + 1);
    CHECK(p.dm.storage_epoch() == p.ch.storage_epoch());
    Inbox after = p.remount();
    CHECK(after.storage_epoch() == before + 1);
}

TEST_CASE("§CUSTODY-D/5c both stores non-empty — ONE bump, not one per store") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "m", 1000);
    rec_chan(p.ib, 3, 0x07000000u, "c", 1500);
    const uint32_t before = p.ib.storage_epoch();
    CHECK(p.ib.clear());
    CHECK(p.ib.storage_epoch() == before + 1);                 // ★ +1, never +2 — the target is computed once
    CHECK(p.dm.storage_epoch() == before + 1);
    CHECK(p.ch.storage_epoch() == before + 1);
}

TEST_CASE("§CUSTODY-D/5d both stores EMPTY — the epoch still moves, so a no-op clear is still announced") {
    // ⓘ Deliberate: the operator asked for a clear and got one. The companion re-syncing from zero over an empty
    //   inbox costs one round trip and nothing else, whereas an unannounced clear is unrecoverable ambiguity.
    DurablePair p;
    const uint32_t before = p.ib.storage_epoch();
    CHECK(p.ib.clear());
    CHECK(p.ib.storage_epoch() == before + 1);
    CHECK(p.dm.storage_epoch() == p.ch.storage_epoch());
}

// ★★★ THE 19.12 CLASS — the ratchet that [[B134]] measured (epoch 2, 3, 4 with nothing having changed). A wipe
//     that does not RECORD its own acknowledgement re-triggers the next boot's external-loss detect, for ever.
TEST_CASE("§CUSTODY-D/5e ★ the epoch is stable across TWO remounts — no per-boot ratchet") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "m", 1000);
    rec_chan(p.ib, 3, 0x07000000u, "c", 1500);
    const uint32_t before = p.ib.storage_epoch();
    CHECK(p.ib.clear());
    const uint32_t after_clear = p.ib.storage_epoch();
    CHECK(after_clear == before + 1);

    Inbox boot1 = p.remount();
    CHECK(boot1.storage_epoch() == after_clear);               // ★ boot 1: unchanged
    Inbox boot2 = p.remount();
    CHECK(boot2.storage_epoch() == after_clear);               // ★ boot 2: STILL unchanged
    CHECK(p.dm.storage_epoch() == p.ch.storage_epoch());
}

// The VOLATILE store's arm. `FixedInboxStore` is the fallback for a board with neither durable backend, and its
// epoch is normally set once per boot — so a clear that left it alone would tell the companion nothing happened.
TEST_CASE("§CUSTODY-D/5f FixedInboxStore: a confirmed clear empties the ring AND moves its runtime epoch") {
    FixedInboxStore<8> dm, ch;
    dm.set_epoch(0xAABBCCDDu); ch.set_epoch(0xAABBCCDDu);      // the per-boot value the backend installs
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    CHECK(ib.mark_read(InboxKind::dm, 1));
    const uint32_t before = ib.storage_epoch();
    CHECK(before == 0xAABBCCDDu);

    CHECK(ib.clear());

    CHECK(raw_pull(ib).empty());
    CHECK(dm.count() == 0u); CHECK(ch.count() == 0u);
    CHECK(dm.read_cursor() == 0u); CHECK(ch.read_cursor() == 0u);
    CHECK(ib.storage_epoch() == before + 1);                   // ★ the runtime epoch moved
    CHECK(dm.storage_epoch() == ch.storage_epoch());           // ★ both on the one target
    // ...and the high-water still never regresses, for the same reason it does not on the durable store: the
    // counter lives in `Inbox` and a wipe does not touch it.
    CHECK(rec_dm(ib, 9, 900, "next", 9000) == 2u);
}

// ⛔ THE LEGACY ARM IS UNCHANGED — `prep-restart` / `factory_reset confirm` still call `wipe()` with no target
//    and still get the per-store transition rule. This is the control that says §CUSTODY-D widened nothing.
TEST_CASE("§CUSTODY-D/5g the no-argument wipe() keeps its EXACT pre-slice epoch policy") {
    {   // durable, HELD history -> the transition bump of exactly one
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        const uint32_t e0 = s.storage_epoch();
        s.append(1, reinterpret_cast<const uint8_t*>("x"), 1);
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == e0 + 1);
    }
    {   // durable, held NOTHING -> ⛔ no bump (destroying nothing owes the companion nothing)
        FakeSegmentStore recs; FakeMetaStore meta;
        SegmentedInboxStore s(recs, meta, 4096, 256);
        CHECK(s.begin());
        const uint32_t e0 = s.storage_epoch();
        CHECK(s.wipe());
        CHECK(s.storage_epoch() == e0);
    }
    {   // volatile -> ⛔ the per-boot epoch is NOT re-rolled (the reboot behind those verbs does it)
        FixedInboxStore<4> f;
        f.set_epoch(0x1234u);
        CHECK(f.wipe());
        CHECK(f.storage_epoch() == 0x1234u);
    }
}

// =====================================================================================================
// §7.5.6 — EVERYTHING OUTSIDE THE INBOX IS UNTOUCHED  (both directions)
// =====================================================================================================

namespace {
class DHal : public mrtest::TestHalBase {
public:
    void emit(const char*, const EventField*, size_t) override {}
};
// The non-inbox state a `status` / `routes` / `cfg` dump would show — snapshotted as VALUES so the comparison is
// about the node's state and not about a pointer still being valid.
struct NodeSnapshot {
    uint8_t  node_id; uint32_t key_hash32; uint16_t channel_ctr; uint8_t rt_count;
    std::vector<uint32_t> route_key;      // dest<<16 | next_hop<<8 | hops, per route entry
    static NodeSnapshot of(const Node& n) {
        NodeSnapshot s{};
        s.node_id = n.node_id(); s.key_hash32 = n.key_hash32();
        s.channel_ctr = n.channel_ctr(); s.rt_count = n.rt_count();
        for (uint8_t i = 0; i < n.rt_count(); ++i) {
            const RtEntry& e = n.rt_at(i);
            s.route_key.push_back((uint32_t(e.dest) << 16) | (uint32_t(e.candidates[0].next_hop) << 8)
                                  | e.candidates[0].hops);
        }
        return s;
    }
    bool operator==(const NodeSnapshot& o) const {
        return node_id == o.node_id && key_hash32 == o.key_hash32 && channel_ctr == o.channel_ctr
            && rt_count == o.rt_count && route_key == o.route_key;
    }
};
}  // namespace

// ★★★ PROVEN, NOT ASSERTED (§17-D bullet 4). The snapshot is taken over a node carrying real routes, identity
//     and a live channel counter, and re-taken across a CONFIRMED clear that really destroyed records.
TEST_CASE("§CUSTODY-D/6 ★ a confirmed clear leaves routes, identity and counters byte-identical") {
    DHal h; Node n{ h, /*id=*/4, 0x44444444u };
    NodeConfig cfg; cfg.n_layers = 1;
    cfg.layers[0].layer_id = 1; cfg.layers[0].routing_sf = 8;
    cfg.layers[0].allowed_sf_bitmap = uint16_t(1u << 8);
    cfg.routing_sf = 8; cfg.allowed_sf_bitmap = uint16_t(1u << 8);
    CHECK(n.on_init(cfg));
    n.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
    n.test_learn_route(/*dest=*/9, /*via=*/2, 2, 30, false);
    n.restore_channel_ctr(4242);

    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    n.inbox().on_init(&dm, &ch);
    rec_dm(n.inbox(), 5, 100, "hello", 1000);
    rec_chan(n.inbox(), 3, 0x07001122u, "post", 1100);

    const NodeSnapshot before = NodeSnapshot::of(n);
    CHECK(before.rt_count == 2u);                              // the precondition: there IS state to lose
    CHECK(before.channel_ctr == 4242u);

    CHECK(n.inbox().clear());
    CHECK(raw_pull(n.inbox()).empty());                        // the clear really happened...

    const NodeSnapshot after = NodeSnapshot::of(n);
    CHECK(after == before);                                    // ★ ...and nothing outside the inbox moved
    CHECK(after.rt_count == 2u);
    CHECK(after.channel_ctr == 4242u);
    CHECK(after.key_hash32 == 0x44444444u);
    CHECK(after.node_id == 4u);
}

// ★★ THE NEGATIVE DIRECTION. `Inbox::clear()` must not become reachable from — or shared machinery with — the
//    two verbs that wipe MORE than the inbox. The seam is the `target_epoch` argument: `prep-restart` and
//    `factory_reset` call `wipe()` with none, and the store proves it is looking at a different decision.
TEST_CASE("§CUSTODY-D/6b clear() and the factory_reset/prep-restart wipe are DIFFERENT operations at the store") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "m", 1000);
    rec_chan(p.ib, 3, 0x07000000u, "c", 1500);
    const uint32_t e0 = p.dm.storage_epoch();

    // The factory_reset / prep-restart spelling: per-store policy, no cursor reset from any orchestrator, and —
    // critically — NO high-water persist obligation, because both verbs reboot behind themselves.
    CHECK(p.dm.wipe());
    CHECK(p.ch.wipe());
    CHECK(p.dm.storage_epoch() == e0 + 1);
    CHECK(raw_records(p.dm) == 0u);

    // Re-populate and take the OTHER path: the same erase, a DIFFERENT epoch decision, plus the cursor reset.
    rec_dm(p.ib, 5, 101, "m2", 2000);
    CHECK(p.ib.mark_read(InboxKind::dm, p.ib.dm_newest_seq()));
    CHECK(p.dm.read_cursor() != 0u);
    const uint32_t e1 = p.dm.storage_epoch();
    CHECK(p.ib.clear());
    CHECK(p.dm.storage_epoch() == e1 + 1);
    CHECK(p.ch.storage_epoch() == p.dm.storage_epoch());       // ★ the shared target — which wipe() alone never gives
    CHECK(p.dm.read_cursor() == 0u);                           // ★ the cursor reset — which wipe() alone never does
}

// =====================================================================================================
// §7.5.7 — THE REPORT: the frozen ack family
// =====================================================================================================

// ⓘ The three shapes' BYTE-FOR-BYTE pin lives with every other writer's, in test_console_json.cpp
//   ("write_inbox_cleared / write_inbox_needs_confirm — §CUSTODY-D's three frozen shapes") — U3, that file is the
//   encoder's home. What belongs HERE is the verb's semantics: the one-way verdict mapping and the fact that the
//   numbers the ack carries are the ones the real orchestration produced.

// ⛔ THE ONE-WAY MAPPING, hoisted into console_json.h because `handle_clear_inbox` is §B115-invisible. A
//    destructive verb that can spell success over a failed erase is [[B134]]'s exact defect.
TEST_CASE("§CUSTODY-D/7b a false verdict can NEVER spell cleared") {
    CHECK(std::strcmp(meshroute::console::inbox_clear_result(true),  "cleared")  == 0);
    CHECK(std::strcmp(meshroute::console::inbox_clear_result(false), "io_error") == 0);
    CHECK(std::strcmp(meshroute::console::inbox_clear_result(false), "cleared")  != 0);
    // The refusal lexeme is a THIRD value and shares nothing with the two verdicts.
    char b[64]; const size_t n = meshroute::console::write_inbox_needs_confirm(b, sizeof b);
    CHECK(std::string(b, n).find("cleared") == std::string::npos);
    CHECK(std::string(b, n).find("epoch") == std::string::npos);    // ★ a refusal carries NO state fields
}

// The ack's numbers are the ones §7.5.7 names, taken from the real orchestration rather than from constants.
TEST_CASE("§CUSTODY-D/7c the ack reports the NEW epoch and the PRESERVED newest sequences") {
    DurablePair p;
    for (uint16_t i = 0; i < 5; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);
    for (uint16_t i = 0; i < 2; ++i) rec_chan(p.ib, 3, 0x07000000u + i, "c", 1500 + i);
    const uint32_t epoch_before = p.ib.storage_epoch();
    CHECK(p.ib.clear());

    char b[160];
    const size_t n = meshroute::console::write_inbox_cleared(
        b, sizeof b, p.ib.storage_epoch(), p.ib.dm_newest_seq(), p.ib.chan_newest_seq(),
        meshroute::console::inbox_clear_result(true));
    const std::string s(b, n);
    CHECK(s.find("\"epoch\":" + std::to_string(epoch_before + 1)) != std::string::npos);
    CHECK(s.find("\"dm_seq\":5") != std::string::npos);        // ★ the high-water, not 0
    CHECK(s.find("\"chan_seq\":2") != std::string::npos);
}

// =====================================================================================================
// THE FAILURE MATRIX  (QG correction 3) — both wipes attempted, never a false `cleared`, and RETRY works
// =====================================================================================================

TEST_CASE("§CUSTODY-D/8 DM store fails, channel store succeeds -> io_error, and the channel is STILL erased") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    dm.fail_wipe = true;

    CHECK_FALSE(ib.clear());                                   // ★ never `cleared`
    CHECK(dm.wipe_calls == 1u); CHECK(ch.wipe_calls == 1u);    // ★ BOTH attempted — no short-circuit
    CHECK(dm.count() == 1u);                                   // the failing store kept its record (honest)
    CHECK(ch.count() == 0u);                                   // ★ the healthy store was still erased
}

TEST_CASE("§CUSTODY-D/8b DM succeeds, channel fails -> io_error, and the DM store is STILL erased") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    ch.fail_wipe = true;

    CHECK_FALSE(ib.clear());
    CHECK(dm.wipe_calls == 1u); CHECK(ch.wipe_calls == 1u);
    CHECK(dm.count() == 0u);
    CHECK(ch.count() == 1u);
}

TEST_CASE("§CUSTODY-D/8c both stores fail -> io_error, both attempted, nothing claimed") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    dm.fail_wipe = ch.fail_wipe = true;

    CHECK_FALSE(ib.clear());
    CHECK(dm.failed_wipe_calls == 1u); CHECK(ch.failed_wipe_calls == 1u);
    CHECK(raw_pull(ib).size() == 2u);                          // everything still there — and the ack says so
}

// ★★★★ QG CORRECTION 1's OTHER HALF: if the high-water cannot be persisted, ERASE NEITHER STORE.
TEST_CASE("§CUSTODY-D/9 ★ a failed high-water persist erases NEITHER store — the DM side") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    dm.fail_set_next = true;                                   // the DM metadata will not go to the medium

    CHECK_FALSE(ib.clear());
    CHECK(dm.wipe_calls == 0u);                                // ★ NOT erased
    CHECK(ch.wipe_calls == 0u);                                // ★ ...and neither is the store that COULD have been
    CHECK(raw_pull(ib).size() == 2u);
    // ⛔ AND BOTH PERSISTS WERE ATTEMPTED — a `&&` would have skipped the channel counter (and its retry latch).
    CHECK(dm.failed_set_next_calls == 1u);
    CHECK(ch.set_next_calls >= 1u);
}

TEST_CASE("§CUSTODY-D/9b ★ a failed high-water persist erases NEITHER store — the CHANNEL side") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    rec_chan(ib, 3, 0x07000000u, "c", 1500);
    ch.fail_set_next = true;

    CHECK_FALSE(ib.clear());
    CHECK(dm.wipe_calls == 0u);                                // ★ the HEALTHY store is not erased either
    CHECK(ch.wipe_calls == 0u);
    CHECK(raw_pull(ib).size() == 2u);
}

// The cursor is bookkeeping too: a clear whose cursor could not be persisted has not finished.
// ⚠ NAMING CONSTRAINT, and it is an INSTRUMENT fact rather than style: this case must not END in the word
//   `io_error`. `probe_ui_model_mutations.py`'s compile-failure detector is `"error:" in build_output`, and the pio
//   wrapper prints a failing assertion as `<file>:<line>: <case name>: <assertion>` — so a case name ending in
//   `io_error` synthesises the literal `error:` and a genuinely RED mutation is reported UNUSABLE (measured here on
//   D04, 2026-08-30; registered as a finding). The harness fix is its own slice (C1); the name avoids the trap.
TEST_CASE("§CUSTODY-D/9c a cursor that will not persist makes the clear report a FAILURE") {
    RamInboxStore dm{ protocol::inbox_dm_store_bytes }, ch{ protocol::inbox_chan_store_bytes };
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 5, 100, "m", 1000);
    dm.set_read_cursor_fails = true;
    CHECK_FALSE(ib.clear());
    CHECK(dm.wipe_calls == 1u);                                // the erase still happened — it is the REPORT that is honest
}

// ★★★ RETRY. A destructive verb that cannot be re-issued after the medium recovers strands the operator with a
//     half-cleared inbox and no way forward; and the retry must complete the clear, not merely report differently.
TEST_CASE("§CUSTODY-D/10 ★ a later confirmed clear COMPLETES after the medium recovers") {
    DurablePair p;
    for (uint16_t i = 0; i < 4; ++i) rec_dm(p.ib, 5, uint16_t(100 + i), "m", 1000 + i);
    for (uint16_t i = 0; i < 2; ++i) rec_chan(p.ib, 3, 0x07000000u + i, "c", 1500 + i);
    const uint32_t dm_hi = p.ib.dm_newest_seq(), ch_hi = p.ib.chan_newest_seq();
    const uint32_t epoch0 = p.ib.storage_epoch();

    p.ch_recs.fail_erase_of(0);                                // segment 0 of the channel store will not erase
    CHECK_FALSE(p.ib.clear());                                 // attempt 1: io_error, possibly partial
    CHECK(raw_records(p.dm) == 0u);                            // the DM half DID complete
    CHECK(raw_records(p.ch) > 0u);                             // the channel half did not

    p.ch_recs.fail_erase_of(-1);                               // the medium recovers
    CHECK(p.ib.clear());                                       // ★ attempt 2 completes
    CHECK(raw_records(p.ch) == 0u);
    CHECK(raw_pull(p.ib).empty());
    // ★ AND THE INVARIANTS STILL HOLD AFTER THE RETRY: the epoch is strictly greater than where it started, and
    //   the high-water survived both attempts (checked, as always, from a REMOUNT).
    CHECK(p.ib.storage_epoch() > epoch0);
    Inbox after = p.remount();
    CHECK(after.dm_newest_seq() == dm_hi);
    CHECK(after.chan_newest_seq() == ch_hi);
    CHECK(rec_dm(after, 9, 900, "x", 9000) == dm_hi + 1);      // ⛔ no sequence reuse across a PARTIAL clear either
    const uint32_t stable = after.storage_epoch();
    Inbox again = p.remount();
    CHECK(again.storage_epoch() == stable);                    // and the epoch is stable afterwards
}

// An unwired inbox is a failure, never a silent success — `erase`/`mark_read`'s rule, applied here.
TEST_CASE("§CUSTODY-D/11 a disabled inbox reports failure, never a cleared it did not do") {
    Inbox ib;                                                  // no on_init: no stores
    CHECK_FALSE(ib.enabled());
    CHECK_FALSE(ib.clear());
    CHECK(ib.storage_epoch() == 0u);
}

// =====================================================================================================
// THE PANEL REACTS TO EMPTINESS AS IT DOES TO EVICTION — no UI code changes (the slice's boundary)
// =====================================================================================================

namespace {
// The OLED adapter's arithmetic, MIRRORED (src/firmware_ui.cpp's `inbox_row_cb`; §B115 — the shipped callback's
// controls are `tools/probe_firmware_ui/run.sh`'s). Identical in shape to §CUSTODY-C's mirror, deliberately: what
// is being shown is that this verb needs NO change to it at all.
struct OledList {
    mrui::InboxRowBudget budget;
    static bool row_cb(void* vctx, const InboxEntry& e) {
        auto* self = static_cast<OledList*>(vctx);
        if (inbox_record_is_internal(e.type)) return true;
        mrui::InboxRow r{};
        r.kind = e.kind; r.seq = e.seq; r.channel_id = e.channel_id; r.rx_age_s = 0;
        const uint8_t cap = uint8_t(sizeof r.text - 1);
        uint8_t n = (e.body_len < cap) ? e.body_len : cap;
        if (!e.body) n = 0;
        for (uint8_t i = 0; i < n; ++i) r.text[i] = mrui::ui_display_byte(e.body[i]);
        r.text[n] = '\0';
        self->budget.add(r);
        return true;
    }
    mrui::UiSnapshot fill(const Inbox& ib) {
        budget.reset();
        ib.pull(0, 0, row_cb, this);
        mrui::UiSnapshot s{};
        budget.publish(s);
        return s;
    }
};
}  // namespace

TEST_CASE("§CUSTODY-D/12 the OLED inbox view goes to zero on a clear, through the UNCHANGED eviction path") {
    DurablePair p;
    rec_dm(p.ib, 5, 100, "hello", 1000);
    rec_dm(p.ib, 5, 101, "world", 1100);
    rec_chan(p.ib, 3, 0x07000000u, "post", 1200);
    p.ib.record_ack(7, 100, 0, 1300);                          // hidden already — and stays hidden by being GONE

    OledList oled;
    mrui::UiSnapshot s = oled.fill(p.ib);
    CHECK(s.inbox_total == 3u);                                // 2 DMs + 1 post; the receipt was never a row

    CHECK(p.ib.clear());

    s = oled.fill(p.ib);
    CHECK(s.inbox_total == 0u);                                // ★ exactly what a fully-evicted store yields
    CHECK(s.inbox_shown == 0u);
}
