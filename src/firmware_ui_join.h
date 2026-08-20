// MeshRoute — src/firmware_ui_join.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 6 — THE STATIC-JOIN SCREENS' PURE UNIT: the profile list the SELECT screen walks, the four panel
// strings its store states produce, the CONFIRM screen's value lines, the waiting screen's two headlines, and — the
// heart of the slice — ★★★★ **THE FOUR-TERM CORRELATION RULE** (plan §2.3 rule 7).
// Normative: docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md §2.3/§3, design doc §3.6.3,
// dispatch brief docs/superpowers/plans/2026-08-20-ui15-slice6-static-join.md.
//
// ★★★ WHY IT IS A FILE OF ITS OWN rather than more of `firmware_ui_model.h`: the correlation rule is the ONE
//     decision in this slice that a mutation must be able to attack TERM BY TERM, and a battery is per-SOURCE-FILE
//     (`tools/probe_ui_model_mutations.py`'s own header states it — every safety guard is keyed by the resolved
//     path). A rule buried in the 2600-line model would share `model`'s battery with 60 unrelated entries; here it
//     gets its own target (`uijoin`), so "each of the four terms was mutated SEPARATELY and each went RED" is a
//     measurement rather than a claim. ⓘ Same argument `firmware_ui_prov.h` made one slice ago, for the same class
//     of owner-ruled decision.
//
// ★★★★ THE TWO TRAPS THIS FILE EXISTS AROUND, both named by the plan and both mutation-tested:
//   1. ⛔ **`PushKind::join_adopted` IS A SHARED CHANNEL.** `lib/core/command.h:223` says it fires for *"verb
//      join/create, **boot DAD**, OR the **heal re-adopt**"*. ⇒ a node that reboots, or that heals its claim, emits
//      exactly the push an OLED waiting screen is looking for — and completing on it would be the *"a success that
//      isn't"* class this project has already recorded once. `join_refused` is the same shape from the other side:
//      it carries wire-version OBSERVATIONS ABOUT OTHER PEERS (`:204`), so failing on one would fail the operator's
//      join because somebody else's node is on an old build.
//   2. ⛔⛔ **`layer0_id` IS THE FULL BYTE AND THE LIVE/PUSH LAYER IS THE NIBBLE.** `blob_put_static_join` persists
//      the FULL byte in `Blob::layer0_id` and the NIBBLE in `Blob::leaf_id`, and the live apply mirrors the NIBBLE
//      into `layers[0].layer_id` (`src/firmware_config.cpp`'s `provision_apply_live`, matching single-layer init at
//      `lib/core/node.cpp:459`). ⇒ a join requested at layer 17 persists **17**, lives as **1** and pushes leaf
//      **1**. A full==live comparison is therefore UNSATISFIABLE above layer 15, and plan v3 shipped exactly that
//      before v4 corrected it: OLED join would have permanently failed on every layer above 15. ⇒ every term below
//      compares LIKE FOR LIKE, and the nibble is taken from `mrfw::join_leaf_of_layer` — the ONE spelling of
//      `& 0x0F` this feature has (U1), the same one the candidate was composed with.
#pragma once
#include <cstdint>
#include <cstdio>                    // snprintf — the panel lines are formatted HERE so the suite reads the BYTES
#include <cstring>                   // memcpy — the name copy (⛔ the stored label is NOT NUL-terminated)
#include "command.h"                 // MESHROUTE_NS::PushKind — the SHARED channel the rule filters
#include "device_nv.h"               // mrnv::JoinBlob / JoinProfile / kJoinProfiles — THE store's own carrier (U2)
#include "firmware_join_profiles.h"  // mrfw::ProfileResult / join_profile_count — the SERVICE's typed answer, held
                                     //   VERBATIM below (⛔ never a `mrui::` mirror — the `UiState::cfg_save` rule)

namespace mrui {

// ================================================================================== the SELECT screen's ONE carrier
// ★★★ IT HOLDS THE STORE's OWN RECORD AND THE SERVICE's OWN ANSWER, BOTH VERBATIM. That is `UiState::cfg_save`'s
//     rule one feature over: a `mrui::` mirror of `ProfileVerdict`/`ProfileErr` would be the parallel enum U1
//     forbids, and the panel could then claim a store state the service never returned. ⛔ The record is filled by
//     ONE call (`JoinProfileService::list`) and is never rebuilt slot-by-slot at a second site (U2).
// ⓘ `served` IS A THIRD FACT and not a fifth store state: it says the model had NO SEAM AT ALL (a build with no
//   join child, or a partially-wired probe). A store that answered is a different thing from a store nobody asked,
//   and the panel says so — the `run_create_team` "no service" rule, one screen over.
// ⓘ COST, MEASURED not assumed: `sizeof(mrnv::JoinBlob)` is 104 (pinned by device_nv.h's own static_assert) and
//   `sizeof(UiJoinList)` is 108 on the host. It is held ONCE in `UiState`, which the OLED envs instantiate TWICE (the
//   model's and the frame's frozen copy) — see `UiState::join_list` for the measured `sizeof(UiState)` figure and for
//   ⚠ D2's standing warning that native alignment hides the BOARD number.
struct UiJoinList {
    mrnv::JoinBlob      rec{};        // ⛔ the whole record, as `list()` produced it (VALID-EMPTY on any non-ok read)
    mrfw::ProfileResult res{};        // the SERVICE's typed answer, verbatim. Default = `refused` / `none`.
    bool                served = false;
};

// How many slots hold a profile — through the store service's OWN counter (U1), ⛔ never a second loop.
inline uint8_t join_list_count(const UiJoinList& l) { return mrfw::join_profile_count(l.rec); }

// ------------------------------------------------------------------- the rows, AS IDENTITIES and never as indices
// ★★ §B66's rule, a third menu deep: the visible list is built from the store's `present` flags, so a row's meaning
//    may not be derived from its position — four stored profiles minus slot 2 puts slot 3 where slot 2 was.
// ⛔ AND `back` IS A FLAG RATHER THAN "slot 0": §B74's discipline is that no arithmetic value stands in for a state.
//    `valid_profile_slot` is 1-based, so 0 *is* free — and that is exactly the reasoning that produces a `0` meaning
//    two things the day somebody adds slot indices from a different base.
struct JoinSelRow {
    uint8_t slot1 = 0;      // 1..kJoinProfiles — MEANINGFUL ONLY while `!back`
    bool    back  = false;
};
struct JoinSelList {
    JoinSelRow row[mrnv::kJoinProfiles + 1] = {};   // the four slots at most, plus the UNCONDITIONAL BACK
    uint8_t    n = 0;
    // ⛔ FAILS CLOSED (C2), exactly as `ProvRowList::at` does: an out-of-range index names NO row and the caller
    //    must do nothing. Here the row one press from BACK starts a JOIN, which re-provisions the node.
    bool at(uint8_t i, JoinSelRow& out) const { if (i >= n) return false; out = row[i]; return true; }
};
// ★ ONLY PRESENT SLOTS ARE ROWS. An empty slot is not offered, so a `double` can never land on one — which is what
//   keeps "selecting a present slot opens the confirmation" (brief scope 2) true without inventing a refusal the
//   design does not describe. ⓘ An empty NAME is a different thing entirely and IS offered: it renders as
//   `PROFILE <n>` (plan §11's adopted default) — see `join_row_label`.
// ⓘ BACK is UNCONDITIONAL, the same rule `provision_rows` states: leaving must never depend on a store.
inline JoinSelList join_sel_rows(const UiJoinList& l) {
    JoinSelList out{};
    // ⛔ A NON-`ok` READ OFFERS NO SLOTS AT ALL. `list()` leaves a VALID EMPTY record behind on `absent`/`invalid`/
    //    `io_failed`, so the loop below would already find nothing — this states it rather than relying on it, and
    //    it is what makes "a corrupt store cannot be joined from" a property instead of a side effect.
    if (l.served && l.res.verdict == mrfw::ProfileVerdict::ok) {
        for (uint8_t i = 0; i < mrnv::kJoinProfiles; ++i) {
            if (!l.rec.prof[i].present) continue;
            out.row[out.n].slot1 = uint8_t(i + 1);
            out.row[out.n].back  = false;
            ++out.n;
        }
    }
    out.row[out.n].back = true;
    ++out.n;
    return out;
}

// ------------------------------------------------------------------------------ the store states, AS PANEL TEXT
// ★★★★ THE THREE FAILURE TEXTS ARE THREE DIFFERENT TEXTS, AND THAT DISTINCTION IS WHAT [[B218]] BOUGHT (plan §3):
//     `absent` is an ORDINARY fresh-device state; `invalid` means the RECORD is wrong and `joinprofile reset
//     confirm` rewrites it; `io_failed` means the STORE WOULD NOT OPEN, so nothing whatever is known about the four
//     profiles and the remedy is a DEVICE one. ⛔ Collapsing any two would tell the operator to retype four presets
//     the flash still holds, or to discard four intact ones because a mount failed.
// ★ THE `invalid` PAIR CONCATENATES TO PLAN §3's RULED `PROFILE STORE INVALID` — split across two rows by §7.1
//   rule 5, exactly as the owner's `PHY DIFFERS — USE SERIAL` is, because the ruled sentence is 21 columns against a
//   19-column body. ⛔ Neither half may be reworded.
// ⚠ REPORTED, NOT INVENTED (the slice-5 precedent): plan §3 rules the lexemes for `absent` and `invalid` and rules
//   NONE for `io_failed` or for a missing seam. `STORAGE FAILURE` / `CHECK faults` is this file's house style
//   applied to the console's OWN vocabulary for the same fault (`joinprofile err PROFILE STORE UNREADABLE -
//   STORAGE FAILURE (… check \`faults\`, and treat it as a device fault …)`, src/firmware_config.cpp), and
//   `NO JOIN SERVICE` is the `no service` token `run_create_team` already uses. Both are pinned by native cases, so
//   an owner ruling changes them here and nowhere else.
inline const char* join_store_head(const UiJoinList& l) {
    if (!l.served) return "NO JOIN SERVICE";
    switch (l.res.verdict) {
        case mrfw::ProfileVerdict::ok:
            // ★ A VALID RECORD WITH FOUR EMPTY SLOTS IS `NO PROFILES` TOO — the same answer `joinprofile list`
            //   gives, and the same reason: two different FACTS with one honest operator-facing answer.
            return join_list_count(l) == 0 ? "NO PROFILES" : "";
        case mrfw::ProfileVerdict::empty: return "NO PROFILES";     // ABSENT — ⛔ never an error
        case mrfw::ProfileVerdict::refused:
            return l.res.err == mrfw::ProfileErr::store_io_failed ? "STORAGE FAILURE" : "PROFILE STORE";
        // ⓘ Unreachable from `list()`, which is READ-ONLY and performs no write. Listed rather than defaulted so a
        //   fifth verdict fails the build (§B72's rule), and answered with the state that is at least TRUE: the
        //   panel has no profiles it may offer.
        case mrfw::ProfileVerdict::unchanged:
        case mrfw::ProfileVerdict::nv_failed: return "PROFILE STORE";
    }
    return "";
}
inline const char* join_store_detail(const UiJoinList& l) {
    if (!l.served) return "";
    if (l.res.verdict != mrfw::ProfileVerdict::refused) return "";
    // ⛔ THE TWO REMEDIES ARE DIFFERENT, AND THAT IS THE WHOLE POINT OF THE FOURTH STORE STATE.
    return l.res.err == mrfw::ProfileErr::store_io_failed ? "CHECK faults" : "INVALID";
}

// ------------------------------------------------------------------------------------------ the row/value strings
// ★ FORMATTED IN THIS PURE UNIT for the §B115 reason: a string built in `src/firmware_ui.cpp` is a string no
//   automated gate can read. ⚠ WIDTH IS A CONSTRAINT, NOT A PREFERENCE — the rail leaves a 19-column body, and
//   every cap below is DERIVED from the widest value its field can hold, never guessed ([[B120]]).
inline constexpr std::size_t kJoinLabelCap = sizeof(mrnv::JoinProfile::name) + 1;   // 12 label bytes + NUL
// The slot's label: the operator's name, or plan §11's adopted `PROFILE 1…4` default when he typed none.
// ⛔ THE STORED NAME IS NOT NUL-TERMINATED — `name_len` bounds it (device_nv.h says so at the field). A `%s` over
//    `prof.name` would run into `freq_hz`; this copies exactly `name_len` bytes and terminates.
inline void join_row_label(char* out, std::size_t cap, const mrnv::JoinProfile& p, uint8_t slot1) {
    if (!out || cap == 0) return;
    std::size_t n = p.name_len;
    if (n > sizeof p.name) n = sizeof p.name;          // a record can hold anything; the RENDERER trusts nothing
    if (n == 0) { snprintf(out, cap, "PROFILE %u", unsigned(slot1)); return; }
    if (n > cap - 1) n = cap - 1;
    memcpy(out, p.name, n);
    out[n] = '\0';
}
// `L255 SF12 BW500.00` — the layer, the routing SF and the bandwidth, i.e. three of design §3.6.3's four "complete
// values". ⓘ INTEGER ARITHMETIC ON THE STORED Hz, deliberately: the record is integral (plan §3) and this build's
// `snprintf` may have no float support linked at all on the nRF52 target. The ONE integral -> double conversion this
// feature performs is `mrfw::join_request_from_profile`, and it belongs to the REQUEST (the adapter's job), ⛔ never
// to a display path — a second conversion here is exactly the drift U2 forbids.
inline constexpr std::size_t kJoinPhyLineCap = 19;    // `L255 SF12 BW500.00` = 18 + NUL
inline void join_fmt_phy(char* out, std::size_t cap, const mrnv::JoinProfile& p) {
    if (!out || cap == 0) return;
    const unsigned long khz = (unsigned long)(p.bw_hz / 1000u);
    const unsigned long cen = (unsigned long)((p.bw_hz % 1000u) / 10u);   // 2 dp: 62.50 / 41.67 are real LoRa BWs
    snprintf(out, cap, "L%u SF%u BW%lu.%02lu", unsigned(p.layer), unsigned(p.routing_sf), khz, cen);
}
// `869.4625 MHz` — the fourth value, at the 4 decimal places `joinprofile list` prints, which is what makes
// 869.4625 render EXACTLY. ⓘ Sub-100 Hz digits are not shown; that is a DISPLAY choice and the stored Hz is what
// the join actually uses.
inline constexpr std::size_t kJoinFreqLineCap = 15;   // `1000.0000 MHz` = 13 + NUL, with a column spare
inline void join_fmt_freq(char* out, std::size_t cap, const mrnv::JoinProfile& p) {
    if (!out || cap == 0) return;
    const unsigned long mhz  = (unsigned long)(p.freq_hz / 1000000u);
    const unsigned long frac = (unsigned long)((p.freq_hz % 1000000u) / 100u);
    snprintf(out, cap, "%lu.%04lu MHz", mhz, frac);
}
// The CONFIRMATION's two actions, by IDENTITY (§B66: ⛔ never by position). ⓘ It REUSES `ProvConfirm` rather than
// growing a second two-member enum — `firmware_ui_model.h`'s own note said slice 6 would — so the BACK-is-zero
// default is the same one every transition primitive re-establishes.
// ⚠ REPORTED, NOT INVENTED: design §3.6.3 names the operation *"Join static network"* and requires the confirmation,
// but rules no LEXEME for the button. `JOIN` is the menu row's own verb, one word.
inline const char* join_confirm_label(bool confirm) { return confirm ? "JOIN" : "BACK"; }

// ------------------------------------------------------------------------------------- the WAITING screen's words
// ★★★ PLAN §2.3 RULE 5, VERBATIM IN SUBSTANCE: *"after 60 s show `STILL JOINING`, ⛔ NOT a failure"*. Normal
//     adoption is ~23 s and one conflict/retry can reach ~53 s, and ⛔ **retries are not finitely bounded** — so a
//     deadline that declared failure would LIE. This is a WORD CHANGE and nothing else: no state moves, no
//     transaction is re-run, nothing is cancelled, and the correlation rule below is unaffected by it.
inline constexpr uint32_t kJoinStillMs = 60000;
inline const char* join_wait_head(bool still) { return still ? "STILL JOINING" : "JOINING"; }
// The RESULT's second row once a correlated adopt has landed: plan §2.3 rule 2's *"showing the resulting node id"*.
inline constexpr std::size_t kJoinNodeLineCap = 10;   // `node 255` = 8 + NUL, with a column spare
inline void join_fmt_node(char* out, std::size_t cap, uint8_t node_id) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "node %u", unsigned(node_id));
}

// ================================================================================ ★★★★ THE FOUR-TERM CORRELATION
// ★★★★ PLAN §2.3 RULE 7, AND EVERY TERM IS ITS OWN STATEMENT SO THAT EVERY TERM IS ITS OWN MUTATION. The four are
//      compared LIKE FOR LIKE — that is the correction v4 had to make, and the reason each line names which two
//      quantities it is holding against each other:
//        1. a UI join session is ACTIVE                                        (state)
//        2. the cached requested FULL layer == the current persisted layer0_id (persisted <-> persisted)
//        3. `push.layer_id` == `requested_layer & 0x0F`                        (nibble    <-> nibble)
//        4. `push.dst` == the canonical node id, and NON-ZERO                  (id        <-> id)
// ⛔ AND THE KIND GATE IS FIRST AND IS NOT ONE OF THE FOUR: plan §2.3 rule 6 is that ⛔ **NO `join_refused` reason
//    terminally fails UI-15 v1** — the shared push cannot separate static DAD from team DAD from an unrelated
//    wire-version observation, so every one of them is IGNORED FOR COMPLETION rather than guessed at. ⇒ this
//    function answers "does this push COMPLETE the operator's join", and for every kind but `join_adopted` the
//    answer is simply NO. A caller that treated `false` as a FAILURE would re-create the defect from the other side;
//    `UiModel::on_join_push` therefore returns without touching the screen.
// ⓘ WHY THE FACTS ARE PARAMETERS AND NOT READS: `layer0_id` lives in `/mrcfg` and the canonical id in `g_node`,
//   neither of which a pure unit may touch — and passing them in is what lets the suite drive term 2 and term 4
//   through their whole domain. The DEVICE reads them (src/firmware_ui.cpp), and only while a session is active, so
//   an ordinary push costs no flash read.
struct UiJoinSession {
    bool     active          = false;   // term 1 — set by a `started` transaction, cleared by a correlated adopt
    uint8_t  requested_layer = 0;       // the FULL byte the operator's profile asked for (⛔ never the nibble)
    uint32_t started_ms      = 0;       // for the 60 s `STILL JOINING` word change, and for NOTHING else
};

inline bool join_push_correlates(const UiJoinSession& sess,
                                 MESHROUTE_NS::PushKind kind, uint8_t push_layer_id, uint8_t push_dst,
                                 uint8_t persisted_layer0_id, uint8_t canonical_node_id) {
    // ⛔ THE KIND GATE (plan §2.3 rules 2 + 6): only an ADOPT can complete, and NO refusal reason may fail.
    if (kind != MESHROUTE_NS::PushKind::join_adopted) return false;
    // TERM 1 — a UI join session is ACTIVE. ⛔ Without it a BOOT DAD adopts the panel into a result screen for an
    //   operation nobody started; `join_adopted` fires at every boot on every provisioned node.
    if (!sess.active) return false;
    // TERM 2 — PERSISTED <-> PERSISTED. The record must still hold the layer THIS session asked for; if a serial
    //   `join`/`cfg set layer0_id` moved it underneath, the adopt that arrives belongs to THAT operation, not this
    //   one. ⛔ It is the FULL byte on both sides — the record's own field and the session's cached request.
    if (sess.requested_layer != persisted_layer0_id) return false;
    // TERM 3 — NIBBLE <-> NIBBLE, and this is trap 2. `Push::join_adopted` documents `layer_id = leaf_id`
    //   (command.h:223), which is the NIBBLE; the request is the FULL byte. ⇒ the nibble is DERIVED through the ONE
    //   spelling of `& 0x0F` this feature has (U1). ⛔ Comparing `push.layer_id` against the full byte would make
    //   every join above layer 15 wait for ever.
    if (push_layer_id != mrfw::join_leaf_of_layer(sess.requested_layer)) return false;
    // TERM 4 — ID <-> ID, and NON-ZERO. `dst` is the adopted node id; 0 is the UNPROVISIONED value (device_nv.h's
    //   `blob_put_static_join` writes `node_id = 0` precisely to mean "not adopted"), so a 0 on both sides would
    //   make a node that adopted NOTHING complete the screen. ⛔ Two clauses, because they are two facts.
    if (push_dst == 0) return false;
    if (push_dst != canonical_node_id) return false;
    return true;
}

}  // namespace mrui
