// MeshRoute — test_custody_internal_c.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-C — DIAGNOSTIC INBOX CLASSIFICATION (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` §7, §7.4, §7.5, §14, §18.5).
// Slice B's file holds the RECEIVE-side fail-closed behaviour; this one holds the READ side: which stored records
// an ordinary view shows, in what order the exclusion is applied relative to the row budget, what the raw
// diagnostic pull still carries, and the two lifetime equalities (eviction, deletion) an internal record must NOT
// be exempt from.
//
// ★★★ THE ONE THING THIS SLICE IS ABOUT, STATED ONCE: `data_type_traits(t).internal` is the presentation
//     predicate — ⛔ NOT `persistent_outcome`. §CUSTODY-C/1c is the case that makes that difference measurable
//     rather than asserted, and it is why a future CUSTODY_FAILURE (`0x81`) needs no presentation work at all.
//
// ⛔ NOTHING HERE IS A STORAGE TEST. What is WRITTEN is untouched by this slice (`record_ack`'s own round-trip is
//    pinned in `test_inbox.cpp`, "record_ack stores an E2E-ack RECEIPT"); these cases READ.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "inbox.h"
#include "frame_codec.h"
#include "node.h"
#include "protocol_constants.h"
#include "ram_inbox_store.h"
#include "fixed_inbox_store.h"   // the SLOT ring the OLED boards and the panel probe actually run
#include "support/test_hal.h"
#include "firmware_ui_model.h"   // mrui::InboxRowBudget — the OLED row budget this seam publishes through
#include "firmware_ui_send.h"    // mrui::ui_route_recv_push — where the panel's UNREAD count is actually driven

#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// ---- the OLED adapter, MIRRORED (src/firmware_ui.cpp's `inbox_row_cb` + `fill_inbox_rows`) -------------------
// ⚠ IT IS A MIRROR AND IS LABELLED ONE. `src/firmware_ui.cpp` is compiled by neither the native suite nor the
//   simulator (§B115), so the SHIPPED callback's controls are `tools/probe_firmware_ui/run.sh`'s. What this
//   mirror buys is that the PREDICATE and the ARITHMETIC — the two halves a native case can own — redden here
//   the moment `inbox_record_is_internal` changes meaning, which is exactly what the isolated harness attacks.
// ★ THE ORDER IS COPIED VERBATIM AND IS THE POINT: gate FIRST, then build the row, then `add` (which is both the
//   ring insert and the `inbox_total` count). A mirror that gated after `add` would pass on the defect.
struct OledList {
    mrui::InboxRowBudget budget;
    uint16_t             raw_visited = 0;    // what `pull()` returned — kept only to prove it is NOT the total
    uint16_t             sanitized   = 0;    // §7: how many bodies reached the byte sanitizer

    static bool row_cb(void* vctx, const InboxEntry& e) {
        auto* self = static_cast<OledList*>(vctx);
        if (inbox_record_is_internal(e.type)) return true;      // ★ the gate, first — no row, no total, no sanitizer
        mrui::InboxRow r{};
        r.kind = e.kind; r.seq = e.seq; r.channel_id = e.channel_id; r.rx_age_s = 0;
        const uint8_t cap = uint8_t(sizeof r.text - 1);
        uint8_t n = (e.body_len < cap) ? e.body_len : cap;
        if (!e.body) n = 0;
        for (uint8_t i = 0; i < n; ++i) r.text[i] = mrui::ui_display_byte(e.body[i]);
        r.text[n] = '\0';
        ++self->sanitized;
        self->budget.add(r);
        return true;
    }
    mrui::UiSnapshot fill(const Inbox& ib) {
        budget.reset(); raw_visited = 0; sanitized = 0;
        raw_visited = ib.pull(/*dm_since=*/0, /*chan_since=*/0, row_cb, this);
        mrui::UiSnapshot s{};
        budget.publish(s);
        return s;
    }
};

// ---- raw pull collection (the DIAGNOSTIC half) ---------------------------------------------------------------
struct PulledRec { uint32_t seq; InboxKind kind; uint8_t origin; uint32_t msg_id; uint32_t sender_hash;
                   uint8_t layer_id; uint8_t type; uint8_t body_len; std::string body; };
struct PullSink { std::vector<PulledRec> recs; };
bool pull_cb(void* ctx, const InboxEntry& e) {
    static_cast<PullSink*>(ctx)->recs.push_back(
        { e.seq, e.kind, e.origin, e.msg_id, e.sender_hash, e.layer_id, e.type, e.body_len,
          std::string(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len) });
    return true;
}
std::vector<PulledRec> raw_pull(const Inbox& ib) { PullSink s; ib.pull(0, 0, pull_cb, &s); return s.recs; }

void rec_dm(Inbox& ib, uint8_t origin, uint16_t ctr, const char* s, uint64_t t) {
    ib.record_dm(origin, /*sender_hash*/ 0, ctr, /*layer_id*/ 0,
                 reinterpret_cast<const uint8_t*>(s), uint8_t(std::strlen(s)), t);
}

}  // namespace

// =====================================================================================================
// §CUSTODY-C/1 — THE CLASSIFICATION PREDICATE
// =====================================================================================================

// ★★★★ THE FOUR-VALUE VISIBILITY MATRIX the slice is required to show, in ONE case so the four verdicts are read
//      together. ⓘ NO SYNTHETIC TYPE IS INVENTED: the internal RANGE itself is the generalization proof — `0x81`
//      is the number CUSTODY_FAILURE will take, and it is already hidden with no codec, no handler and no arm.
TEST_CASE("§CUSTODY-C/1 the ordinary view: application records VISIBLE, every internal record HIDDEN") {
    // ① the ordinary untyped DM — the overwhelming common case, and it must stay visible
    CHECK_FALSE(inbox_record_is_internal(0x00));
    // ② the KNOWN application envelopes, and a RESERVED one (0x05 APP_MESSAGE) — all application-range, all visible
    CHECK_FALSE(inbox_record_is_internal(DATA_TYPE_INTRO));
    CHECK_FALSE(inbox_record_is_internal(DATA_TYPE_MOBILE_SEND));
    CHECK_FALSE(inbox_record_is_internal(DATA_TYPE_SEALED_RELAY));
    CHECK_FALSE(inbox_record_is_internal(DATA_TYPE_CHANNEL_POST));
    CHECK_FALSE(inbox_record_is_internal(DATA_TYPE_APP_MESSAGE));
    // ③ the E2E ACK — the one durable internal outcome that EXISTS today
    CHECK(inbox_record_is_internal(DATA_TYPE_E2E_ACK));
    // ④ the FORWARD RESERVATION `0x81` — CUSTODY_FAILURE's number, hidden before its codec exists (§17-F)
    CHECK(inbox_record_is_internal(0x81));
    // ⑤ ...and another internal value that is NOT a persistent outcome, to show the range and not the opt-in set
    //    is what hides things: a hash answer, a retired number, and a KNOWN-but-mobile-only key forward.
    CHECK(inbox_record_is_internal(DATA_TYPE_H_ANSWER));
    CHECK(inbox_record_is_internal(DATA_TYPE_MOBILE_PUBKEY_PUSH));      // 0x94, RETIRED — still hidden
    CHECK(inbox_record_is_internal(DATA_TYPE_MOBILE_KEY_FORWARD));      // 0x96, known = true — still hidden
}

// The predicate is the RANGE, over the whole byte — a sweep, not a spot check, so a switch that grew an arm
// cannot quietly change what an ordinary view shows.
TEST_CASE("§CUSTODY-C/1b the hidden set is EXACTLY the internal range, for all 256 values") {
    unsigned hidden = 0;
    for (unsigned v = 0; v <= 255; ++v) {
        const uint8_t t = uint8_t(v);
        CHECK(inbox_record_is_internal(t) == data_type_is_internal(t));
        if (inbox_record_is_internal(t)) ++hidden;
    }
    CHECK(hidden == 64u);                                     // 0x80..0xBF inclusive, closed at BOTH ends
    CHECK_FALSE(inbox_record_is_internal(inbox_rec_type_tombstone));   // 0xFE is outside the bound and needs no arm
    CHECK_FALSE(inbox_record_is_internal(0xFF));
}

// ★★★★ THE QG CORRECTION, MADE MEASURABLE. This is the case that would have to be DELETED for the weaker
//      predicate to look acceptable, which is exactly why it exists: `persistent_outcome` is the WRITE opt-in
//      (membership `{E2E_ACK}` today), so classifying a READ by it shows `0x81` — and every future internal
//      record — as ordinary message text.
TEST_CASE("§CUSTODY-C/1c `persistent_outcome` is NOT the presentation predicate — it fails OPEN on 0x81") {
    // They agree on the ONE type that is both today...
    CHECK(data_type_traits(DATA_TYPE_E2E_ACK).persistent_outcome);
    CHECK(data_type_traits(DATA_TYPE_E2E_ACK).internal);
    // ...and DISAGREE on every other internal value, which is the whole argument.
    unsigned disagree = 0;
    for (unsigned v = 0; v <= 255; ++v) {
        const uint8_t t = uint8_t(v);
        if (data_type_traits(t).internal != data_type_traits(t).persistent_outcome) ++disagree;
    }
    CHECK(disagree == 63u);                                   // 64 internal values, exactly one of them persistent
    CHECK(data_type_traits(0x81).internal);                   // hidden by the shipped predicate...
    CHECK_FALSE(data_type_traits(0x81).persistent_outcome);   // ...and VISIBLE under the weakened one
    CHECK_FALSE(data_type_traits(0x81).known);                // and nothing has taught the tree about it yet
}

// =====================================================================================================
// §CUSTODY-C/2 — THE OLED VIEW: EXCLUSION **BEFORE** THE BUDGET AND THE TOTAL (§7.4)
// =====================================================================================================

// ★★★★ THE SLICE'S HEADLINE ARITHMETIC, BOTH SIDES: an inbox holding 3 DMs + 5 acks presents 3 rows, a total of
//      3, and spends 3 (not 8) of its budget — while `pull()` still VISITED all 8.
TEST_CASE("§CUSTODY-C/2 3 DMs + 5 E2E receipts present 3 rows and inbox_total 3, not 8") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    for (int i = 0; i < 3; ++i) rec_dm(ib, /*origin=*/7, uint16_t(100 + i), "hello", 1000);
    for (int i = 0; i < 5; ++i) ib.record_ack(/*from_origin=*/3, uint16_t(200 + i), /*layer*/ 0, 2000);

    OledList oled;
    const mrui::UiSnapshot s = oled.fill(ib);
    CHECK(oled.raw_visited == 8);              // ★ the RAW pull still visits every record...
    CHECK(s.inbox_shown == 3);                 // ...and the panel shows only the three messages
    CHECK(s.inbox_total == 3);                 // ⛔ NOT 8 — §7.4: the total counts admitted records only
    CHECK(oled.budget.dm_count() == 3);
    CHECK(oled.budget.ch_count() == 0);
    CHECK(oled.sanitized == 3);                // §7: five bodies never reached the byte sanitizer
    for (uint8_t i = 0; i < s.inbox_shown; ++i) CHECK(s.inbox[i].kind == InboxKind::dm);
}

// ★★★★ §7.4's OWN NAMED HAZARD, and it is the reason the ORDER is ruled rather than incidental: *"internal
//      records preceding a newer application message cannot hide that message behind the display budget."*
//      The DM ring holds `kInboxRowsPerKind`; here the store holds that many acks FOLLOWED by that many DMs, so
//      an exclusion applied AFTER the budget would have let the acks occupy the ring first and evict... and,
//      worse, publish a total that counts records the operator cannot see or open.
TEST_CASE("§CUSTODY-C/2b the budget is spent on VISIBLE rows only — receipts cannot push a message off the panel") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    // Interleave, so neither "all internal first" nor "all internal last" is what makes it work.
    for (uint8_t i = 0; i < mrui::kInboxRowsPerKind; ++i) {
        ib.record_ack(/*from_origin=*/3, uint16_t(500 + i), 0, 2000);
        char body[8]; std::snprintf(body, sizeof body, "m%u", unsigned(i));
        rec_dm(ib, /*origin=*/7, uint16_t(100 + i), body, 1000);
    }
    OledList oled;
    const mrui::UiSnapshot s = oled.fill(ib);
    CHECK(oled.raw_visited == uint16_t(2 * mrui::kInboxRowsPerKind));
    CHECK(s.inbox_shown == mrui::kInboxRowsPerKind);       // ★ every one of the DM slots holds a MESSAGE
    CHECK(s.inbox_total == mrui::kInboxRowsPerKind);       // ⛔ not 2x — the hidden half is not a mailbox size
    // ★ AND THE NEWEST MESSAGE IS STILL THE TOP ROW ([[B231]]'s order, unchanged by the exclusion).
    if (s.inbox_shown == mrui::kInboxRowsPerKind) {
        CHECK(std::string(s.inbox[0].text) == std::string("m") + char('0' + (mrui::kInboxRowsPerKind - 1)));
        CHECK(std::string(s.inbox[mrui::kInboxRowsPerKind - 1].text) == "m0");
    }
}

// An inbox holding NOTHING BUT receipts is an EMPTY ordinary view — not a list of blanks, and not a non-zero
// total with zero rows (the shape that would make the panel say `INBOX 0/5`).
TEST_CASE("§CUSTODY-C/2c an inbox of receipts only presents an EMPTY list, total 0") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    for (int i = 0; i < 5; ++i) ib.record_ack(/*from_origin=*/3, uint16_t(i), 0, 2000);
    OledList oled;
    const mrui::UiSnapshot s = oled.fill(ib);
    CHECK(oled.raw_visited == 5);
    CHECK(s.inbox_shown == 0);
    CHECK(s.inbox_total == 0);
    CHECK(oled.sanitized == 0);
}

// The budget's own half of §7.4, driven directly: the total is what was ADMITTED, taken BEFORE the ring cap, so
// a truncated list still reports an honest mailbox size.
TEST_CASE("§CUSTODY-C/2d InboxRowBudget: inbox_total is the ADMITTED count, taken before the ring cap") {
    mrui::InboxRowBudget b; mrui::UiSnapshot s{};
    mrui::InboxRow r{}; r.kind = InboxKind::dm; r.seq = 1;
    for (uint8_t i = 0; i < uint8_t(mrui::kInboxRowsPerKind + 4); ++i) { r.seq = i + 1; b.add(r); }
    b.publish(s);
    CHECK(s.inbox_shown == mrui::kInboxRowsPerKind);                       // the ring capped what is DRAWN...
    CHECK(s.inbox_total == uint16_t(mrui::kInboxRowsPerKind + 4));         // ...and the total says so
    CHECK(b.admitted() == uint16_t(mrui::kInboxRowsPerKind + 4));
    b.reset();
    b.publish(s);
    CHECK(s.inbox_total == 0);                                             // reset() clears the count too
}

// =====================================================================================================
// §CUSTODY-C/3 — THE DIAGNOSTIC PULL STAYS RAW (§7.4 / §14, the ruled `pull_inbox` shape)
// =====================================================================================================

// ★★★★ THE RETENTION PROOF, TAKEN **AFTER** THE OLED EXCLUSION HAS RUN in the same case — because the claim is
//      not "pull works" but "the view hides them AND the diagnostic still has them", and two separate cases
//      could both pass over two different stores.
TEST_CASE("§CUSTODY-C/3 the raw pull still carries every receipt VERBATIM after the OLED exclusion") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, /*origin=*/7, /*ctr=*/100, "hello", 1000);
    const uint32_t ack_seq = ib.record_ack(/*from_origin=*/3, /*acked_ctr=*/55, /*layer_id=*/9, /*now=*/2000,
                                           /*acker_hash=*/0xC0FFEEu);
    rec_dm(ib, /*origin=*/7, /*ctr=*/101, "world", 3000);

    OledList oled;
    const mrui::UiSnapshot s = oled.fill(ib);
    CHECK(s.inbox_shown == 2); CHECK(s.inbox_total == 2);        // the panel hides the receipt...

    const std::vector<PulledRec> raw = raw_pull(ib);             // ...and the diagnostic pull does NOT
    CHECK(raw.size() == 3);
    int receipts = 0;
    for (const auto& r : raw) {
        if (r.type != DATA_TYPE_E2E_ACK) continue;
        ++receipts;
        CHECK(r.seq         == ack_seq);            // the seq `del_msg dm <seq>` takes (§7.5)
        CHECK(r.kind        == InboxKind::dm);      // it rides the DM seq-space, as it always did
        CHECK(r.origin      == 3);                  // the dest that confirmed
        CHECK(r.msg_id      == 55u);                // the acked ctr
        CHECK(r.sender_hash == 0xC0FFEEu);          // the cross-layer match key
        CHECK(r.layer_id    == 9);
        CHECK(r.body_len    == 0);                  // a receipt has no body — nothing to stringify (§14.2)
    }
    CHECK(receipts == 1);                           // ★ present exactly once, unchanged, in the raw stream
}

// ⛔ THE OVER-CORRECTION, NAMED AND REFUSED. The tempting "tidy-up" is to filter at `Inbox::pull()` so the
//    companion never sees a receipt — and it would silently destroy the OFFLINE delivery confirmation the
//    companion builds from exactly these records (`AppModel.importInboxEntry`'s `isReceipt` arm) and the bench
//    oracle's ack ledger (`tools/lab/reconcile.py`). This case is the standing refusal.
TEST_CASE("§CUSTODY-C/3b pull() is RAW BY DESIGN — the count and the stream include internal records") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    for (int i = 0; i < 3; ++i) rec_dm(ib, 7, uint16_t(100 + i), "m", 1000);
    for (int i = 0; i < 5; ++i) ib.record_ack(3, uint16_t(200 + i), 0, 2000);
    PullSink sink;
    CHECK(ib.pull(0, 0, pull_cb, &sink) == 8);      // ★ the RETURN is the raw visited count (the `inbox_end` count)
    CHECK(sink.recs.size() == 8);
    int internal = 0;
    for (const auto& r : sink.recs) if (inbox_record_is_internal(r.type)) ++internal;
    CHECK(internal == 5);
    // ...and a CURSORED pull behaves the same: internal records are streamed, so the companion's per-record
    // cursor advance can consume them (`inbox_end` advances no cursor of its own).
    PullSink after; ib.pull(/*dm_since=*/4, /*chan_since=*/0, pull_cb, &after);
    CHECK(after.recs.size() == 4);
    for (const auto& r : after.recs) CHECK(r.type == DATA_TYPE_E2E_ACK);
}

// =====================================================================================================
// §CUSTODY-C/4 — LIFETIME EQUALITY: NO PINNING, NO PROTECTION (§7.5 / §18.5.7-8)
// =====================================================================================================

// ★★ EVICTION, BOTH DIRECTIONS IN ONE CASE: the ack is evicted under pressure exactly like a DM, and the store
//    behaves IDENTICALLY when the same slot holds a DM instead — which is what "no reserved capacity" means.
//    ⓘ The control arm is what makes it a measurement: without it, "the ack was evicted" is also satisfied by a
//      store that evicts differently and happened to reach the same count.
TEST_CASE("§CUSTODY-C/4 eviction is EQUAL — an internal record ages out exactly like a DM, no reserved slot") {
    const uint32_t kCap = 12 * (inbox_record_header_bytes + 8 + 2);   // room for ~12 short records
    // ARM A: the OLDEST record is an E2E receipt.
    RamInboxStore dmA(kCap), chA(protocol::inbox_chan_store_bytes);
    Inbox a; a.on_init(&dmA, &chA);
    const uint32_t ack_seq = a.record_ack(/*from_origin=*/3, /*acked_ctr=*/55, 0, 1000);
    for (int i = 0; i < 40; ++i) rec_dm(a, 7, uint16_t(100 + i), "abcdefgh", 2000);
    // ARM B: the identical stream with an ordinary DM in that first slot.
    RamInboxStore dmB(kCap), chB(protocol::inbox_chan_store_bytes);
    Inbox b; b.on_init(&dmB, &chB);
    rec_dm(b, 3, 55, "", 1000);
    for (int i = 0; i < 40; ++i) rec_dm(b, 7, uint16_t(100 + i), "abcdefgh", 2000);

    const std::vector<PulledRec> ra = raw_pull(a), rb = raw_pull(b);
    CHECK(ra.size() == rb.size());                        // ★ the same number of survivors — no reserved capacity
    CHECK(dmA.count() == dmB.count());
    bool ack_survives = false;
    for (const auto& r : ra) if (r.seq == ack_seq) ack_survives = true;
    CHECK_FALSE(ack_survives);                            // ★ the receipt was dropped-oldest, like anything else
    for (size_t i = 0; i < ra.size() && i < rb.size(); ++i) CHECK(ra[i].seq == rb[i].seq);   // same seqs survive
    // ...and the OTHER direction: a receipt written LAST survives exactly as a last-written DM would.
    const uint32_t fresh = a.record_ack(/*from_origin=*/9, /*acked_ctr=*/77, 0, 3000);
    bool fresh_survives = false;
    for (const auto& r : raw_pull(a)) if (r.seq == fresh) fresh_survives = true;
    CHECK(fresh_survives);                                // no special lifetime in EITHER direction
}

// ★★ THE SAME EQUALITY ON THE **SLOT RING**, and it is a second case rather than a second assertion because the
//    two backends evict by different arithmetic: `RamInboxStore` drops oldest by BYTE cap, `FixedInboxStore` by
//    SLOT. §18.5.8's "normal fixed/durable drop-oldest eviction may remove it; no reserved capacity exists" is a
//    claim about BOTH, and this is the one the OLED board and the panel probe actually run.
TEST_CASE("§CUSTODY-C/4c the slot ring evicts a receipt exactly like a DM — no protected slot") {
    constexpr uint16_t kSlots = 4;
    FixedInboxStore<kSlots> dmA, chA;
    Inbox a; a.on_init(&dmA, &chA);
    const uint32_t ack_seq = a.record_ack(/*from_origin=*/3, /*acked_ctr=*/55, 0, 1000);   // the OLDEST slot
    CHECK(dmA.count() == 1);
    for (int i = 0; i < int(kSlots); ++i) rec_dm(a, 7, uint16_t(100 + i), "m", 2000);      // push it off the ring

    FixedInboxStore<kSlots> dmB, chB;                     // the control: an ordinary DM in that first slot
    Inbox b; b.on_init(&dmB, &chB);
    rec_dm(b, 3, 55, "", 1000);
    for (int i = 0; i < int(kSlots); ++i) rec_dm(b, 7, uint16_t(100 + i), "m", 2000);

    CHECK(dmA.count() == kSlots);                         // ★ the ring is FULL, not full+1 — nothing was protected
    CHECK(dmA.count() == dmB.count());
    const std::vector<PulledRec> ra = raw_pull(a), rb = raw_pull(b);
    CHECK(ra.size() == rb.size());
    bool ack_survives = false;
    for (const auto& r : ra) { if (r.seq == ack_seq) ack_survives = true; CHECK(r.type != DATA_TYPE_E2E_ACK); }
    CHECK_FALSE(ack_survives);                            // ★ evicted, exactly as the DM in arm B was
    for (size_t i = 0; i < ra.size() && i < rb.size(); ++i) CHECK(ra[i].seq == rb[i].seq);
}

// ★★ DELETION, the §7.5 contract verbatim: *"An internal report has no deletion protection. Once its sequence is
//    known from the diagnostic stream, the existing `del_msg dm <seq>` path may delete it exactly like another
//    DM-store record."* The seq comes from the RAW pull, which is the only place it is knowable — so this case
//    also demonstrates the diagnostic access §7.4 promises being USED.
TEST_CASE("§CUSTODY-C/4b erase(dm, seq) deletes a receipt exactly like a DM — same verdicts, both orders") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    rec_dm(ib, 7, 100, "keep me", 1000);
    const uint32_t ack_seq = ib.record_ack(3, 55, 0, 2000);
    const uint32_t dm_seq  = ib.record_dm(7, 0, 101, 0, reinterpret_cast<const uint8_t*>("bye"), 3, 3000);

    // The receipt's seq is learnt from the diagnostic stream, exactly as an operator would learn it.
    uint32_t seen = 0;
    for (const auto& r : raw_pull(ib)) if (r.type == DATA_TYPE_E2E_ACK) seen = r.seq;
    CHECK(seen == ack_seq);

    CHECK(ib.erase(InboxKind::dm, ack_seq) == InboxEraseResult::erased);      // ★ erased, not refused
    CHECK(ib.erase(InboxKind::dm, ack_seq) == InboxEraseResult::not_found);   // ...and a repeat is not_found, as for a DM
    std::vector<PulledRec> after = raw_pull(ib);
    for (const auto& r : after) CHECK(r.seq != ack_seq);                      // gone from the RAW stream too
    for (const auto& r : after) CHECK(r.type != inbox_rec_type_tombstone);    // and no tombstone leaked
    // the control: an ordinary DM in the same store answers identically
    CHECK(ib.erase(InboxKind::dm, dm_seq) == InboxEraseResult::erased);
    CHECK(ib.erase(InboxKind::dm, dm_seq) == InboxEraseResult::not_found);
    CHECK(raw_pull(ib).size() == 1);                                          // only "keep me" is left
}

// =====================================================================================================
// §CUSTODY-C/5 — THE LIVE FAST PATH IS UNTOUCHED (§7.4's last paragraph / §18.5.10)
// =====================================================================================================

namespace {

// A two-node pair driven over the REAL MAC, like §CUSTODY-B's `Chain` — public API only, so what is pinned is
// what a peer can actually cause. Node 1 originates a `-a` DM to node 2; node 2's E2E ACK comes back.
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kPostAckTimerId      = 9;

struct CFrame { std::string label; std::vector<uint8_t> bytes; };
class CHal : public mrtest::TestHalBase {
public:
    std::vector<CFrame> tx_frames;
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(CFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
        return TxResult::ok;
    }
    void emit(const char*, const EventField*, size_t) override {}   // the fast-path pin reads PUSHES, not telemetry
    void rand_bytes(uint8_t* o, size_t n) override { for (size_t i = 0; i < n; ++i) o[i] = uint8_t(0x5Au ^ (i * 17u)); }
    size_t label_count(const char* l) const { size_t c = 0; for (auto& f : tx_frames) if (f.label == l) ++c; return c; }
    std::vector<uint8_t> last(const char* l) const {
        for (auto it = tx_frames.rbegin(); it != tx_frames.rend(); ++it) if (it->label == l) return it->bytes;
        return {};
    }
};

struct Pair {
    CHal h1, h2;
    Node n1{h1, /*id=*/1, 0x11111111u};
    Node n2{h2, /*id=*/2, 0x22222222u};
    RamInboxStore dm1{protocol::inbox_dm_store_bytes}, ch1{protocol::inbox_chan_store_bytes};
    RamInboxStore dm2{protocol::inbox_dm_store_bytes}, ch2{protocol::inbox_chan_store_bytes};
    uint64_t now = 100000;
    Pair() {
        NodeConfig cfg; cfg.n_layers = 1;
        cfg.layers[0].layer_id = 1; cfg.layers[0].routing_sf = 8;
        cfg.layers[0].allowed_sf_bitmap = uint16_t(1u << 8);
        cfg.routing_sf = 8; cfg.allowed_sf_bitmap = uint16_t(1u << 8);
        CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg));
        n1.inbox().on_init(&dm1, &ch1);          // ★ node 1 is the SENDER: its inbox is where the receipt lands
        n2.inbox().on_init(&dm2, &ch2);
        n1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n2.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
        h1._now = h2._now = now;
        Push d{}; while (n1.next_push(d)) {} while (n2.next_push(d)) {}
    }
    void step() { h1._now = h2._now = ++now; }
    // One complete RTS -> CTS -> DATA -> ACK -> post-ack hop.
    void hop(Node& src, CHal& sh, Node& dst, CHal& dh) {
        const std::vector<uint8_t> rts = sh.last("RTS");
        CHECK_FALSE(rts.empty()); if (rts.empty()) return;
        step(); dst.on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, int8_t(-1)});
        const std::vector<uint8_t> cts = dh.last("CTS");
        CHECK_FALSE(cts.empty()); if (cts.empty()) return;
        step(); src.on_recv(cts.data(), cts.size(), RxMeta{10.0f, -75.0f, 0, int8_t(-1)});
        step(); src.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = sh.last("DATA");
        CHECK_FALSE(data.empty()); if (data.empty()) return;
        step(); dst.on_recv(data.data(), data.size(), RxMeta{10.0f, -75.0f, 0, int8_t(-1)});
        const std::vector<uint8_t> ack = dh.last("ACK");
        if (!ack.empty()) { step(); src.on_recv(ack.data(), ack.size(), RxMeta{10.0f, -75.0f, 0, int8_t(-1)}); }
        step(); dst.on_timer(kPostAckTimerId);
    }
};

}  // namespace

// ★★★★ §18.5.10 VERBATIM: *"One received E2E ACK simultaneously proves both independent paths: the exactly
//      correlated live send becomes DELIVERED, while the stored receipt contributes no ordinary OLED/companion
//      row, visible total or unread count."* THIS IS THE PIN — the two facts are asserted about ONE ack, in one
//      case, because a fast path proved on a different ack than the one that was hidden proves nothing about
//      their independence. §7.4: *"Hiding the stored receipt from the ordinary inbox must not suppress, delay,
//      recreate or otherwise mediate that live push."*
TEST_CASE("§CUSTODY-C/5 one E2E ACK: send_e2e_acked fires LIVE and the stored receipt shows NO row and NO total") {
    Pair p;
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 2;
    c.u.send.flags = DATA_FLAG_E2E_ACK_REQ; c.u.send.plane = 0;
    static const char body[] = "hi";
    c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    const CmdResult r = p.n1.on_command(c);
    CHECK(r.code == CmdCode::queued);
    const uint16_t sent_ctr = r.ctr;

    p.hop(p.n1, p.h1, p.n2, p.h2);              // the DM reaches node 2, which enqueues its E2E ACK back
    p.n1.test_suspend_tx_drain(false);
    p.hop(p.n2, p.h2, p.n1, p.h1);              // the ACK reaches node 1

    // ① THE LIVE FAST PATH — the exact-correlation push, immediately, with no inbox pull involved.
    bool acked = false;
    Push pu{};
    while (p.n1.next_push(pu))
        if (pu.kind == PushKind::send_e2e_acked && pu.dst == 2 && pu.ctr == sent_ctr) acked = true;
    CHECK(acked);                                // ★ THE PIN: `DELIVERED` is reachable without reading the inbox

    // ② THE STORED RECEIPT — present in the raw stream, absent from the ordinary view.
    const std::vector<PulledRec> raw = raw_pull(p.n1.inbox());
    int receipts = 0;
    for (const auto& rec : raw) if (rec.type == DATA_TYPE_E2E_ACK && rec.msg_id == sent_ctr) ++receipts;
    CHECK(receipts == 1);                        // the durable half exists...
    OledList oled;
    const mrui::UiSnapshot s = oled.fill(p.n1.inbox());
    CHECK(s.inbox_shown == 0);                   // ...and contributes NO row,
    CHECK(s.inbox_total == 0);                   // NO visible total,
    CHECK(oled.raw_visited == uint16_t(raw.size()));   // while the raw pull still visited it

    // ③ THE UNREAD COUNT — the receipt is not a `msg_recv`, so the panel's arrival serial never moves for it.
    //   ⓘ This is where the ordinary unread count comes from (`ui_route_recv_push`, firmware_ui_send.h): it is
    //     driven by the LIVE push kind, never by an inbox record — which is why an internal outcome cannot
    //     increment it and why widening that arm to `send_e2e_acked` is the defect this pins against.
    mrui::UiInboxCounters ctr{};
    mrui::UiModel model;
    Push ack_push{};
    ack_push.kind = PushKind::send_e2e_acked; ack_push.dst = 2; ack_push.ctr = sent_ctr;
    CHECK(ctr.unread_dm() == 0);
    CHECK_FALSE(mrui::ui_route_recv_push(ctr, model, ack_push,
                                         /*ui_team_channel_id=*/1, /*same_team_post=*/false, "peer", 1000));
    CHECK(ctr.unread_dm() == 0);                 // ★ an E2E ACK is not an arrival — no unread, no recency stamp
    CHECK_FALSE(ctr.have_dm);
    // ...and the CONTROL, so "0" is a measurement and not a counter that never moves: a real DM arrival DOES
    // increment it through the very same router.
    Push dm_push{};
    dm_push.kind = PushKind::msg_recv; dm_push.origin = 2; dm_push.ctr = 9;
    CHECK(mrui::ui_route_recv_push(ctr, model, dm_push, 1, false, "peer", 1000));
    CHECK(ctr.unread_dm() == 1);
    CHECK(ctr.have_dm);
}
