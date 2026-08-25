// MeshRoute — src/firmware_ui_prov.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 5 — THE OLED TEAM-CREATE ADAPTER, i.e. plan §2.1's seam made real:
//   `mrui::UiProvIntent` (pure, model-owned) -> a `TeamRequest` -> `ProvisioningService::apply_team` ->
//   `mrui::UiProvAnswer` (pure, model-rendered).
// Normative: docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md §2.1/§8 and the design doc's
// §3.6.3; the OWNER RULING of 2026-08-19 (reported form) is the whole reason this file exists rather than a three-line
// call in `src/firmware_ui.cpp`.
//
// ★★★ WHY IT IS A FILE OF ITS OWN AND NOT PART OF THE DEVICE TU — the [[B212]]/[[B220]]/[[B223]] lesson, four times
//     over in this arc: `src/firmware_ui.cpp` and `src/firmware_config.cpp` are compiled by NEITHER the native suite
//     (`test_build_src = no`) NOR the simulator, so a decision written in either is a decision no automated gate can
//     drive and no mutation can redden. EVERY decision below — the PHY precondition, `phy.present = false`, the
//     verdict mapping — is therefore here, in a pure header (no Arduino, no `Print`, no `g_node`, no globals), and the
//     device TU keeps only the four forwards of `ITeamCreateDevice`.
//
// ★★★★ THE ONE TRAP, SPELLED OUT BECAUSE THE PLAN SPELLS IT OUT: **THERE ARE TWO `ProvPhy` OBJECTS HERE AND THEY MUST
//      NOT BE CONFLATED.**
//        · the PRECONDITION's — built from the PERSISTED record with **`present = true`**, purely so that
//          `live_phy_matches` actually COMPARES: that predicate early-returns `true` when `!present`
//          (`firmware_provisioning_service.h:337`), so a `present = false` phy would make the precondition a no-op
//          that always passes — the defect this ruling exists to prevent, silently;
//        · the REQUEST's — left at its default **`present = false`**, so the transaction PRESERVES the persisted PHY
//          and performs NO retune. ⛔ Setting `present = true` on the REQUEST would route a PHY through `apply_phy`
//          and re-introduce the [[B209]] path (a provisioning PHY apply must never start static-home discovery).
//      ⇒ the OLED create is a MEMBERSHIP operation and nothing else. The two objects are named `persisted` and
//        `rq.phy` below and are never assigned to one another.
//
// ★★★★ §UI-15 slice 6 ADDED THE SECOND HALF — the STATIC-JOIN adapter — TO THIS FILE RATHER THAN TO A NEW ONE, and
//      that is a boundary decision rather than convenience: `mrui::IUiProvision` is ONE seam with a `default`-less
//      op dispatch (the model's own note asked for exactly that), so its implementation must be ONE class. ⛔ A
//      second adapter file would have needed a second seam, a second `attach_*` and a second null-check — the
//      parallel dispatch U1 forbids. What the join half adds is `IJoinDevice` (three forwards), the ONE
//      integral -> double conversion (`join_request_from_profile`, §3's single authority) and the verdict mapping.
#pragma once
#include "device_nv.h"                     // mrnv::Blob / JoinProfile — the PERSISTED records both halves read
#include "firmware_join_profiles.h"        // mrfw::ProfileResult + join_request_from_profile — the ONE Hz conversion
#include "firmware_join_service.h"         // mrfw::JoinRequest / JoinResult / JoinErr — slice 1's typed transaction
#include "firmware_provisioning_service.h" // the transaction, `live_phy_matches` (U1: REUSED, never re-spelled)
#include "firmware_ui_model.h"             // mrui::UiProvIntent / UiProvAnswer / IUiProvision — the pure carriers

namespace mrfw {

// ---- the DEVICE half, as a seam ---------------------------------------------------------------------------------
// ★★ FOUR OPERATIONS, EVERY ONE OF THEM A FORWARD. ⛔ No decision may be implemented behind this interface: the
//    device implementation (`src/firmware_ui.cpp`, which owns `g_node` and the OLED layer's bindings) reads facts,
//    calls the transaction and performs the post-save bookkeeping — it never chooses.
//   `load_record`   -> `ICfgStore::load`, i.e. the SAME durable seam the transaction itself writes through (U1).
//   `device_facts`  -> the live snapshot + the build floor (`LORA_FREQ` / `LORA_BW` are board macros, so they cannot
//                      be named in a pure header — the same reason `ProvPhyFloor` is passed IN to the transaction).
//   `apply`         -> `ProvisioningService::apply_team(rq, g_node.config(), snap)`.
//   `on_applied`    -> ⛔ CALLED ON THE `applied` ARM AND NOWHERE ELSE (see `ui_prov_create_team`): the
//                      §notify-every-save hook plus the team-DAD persist tracker `ProvResult` reports for exactly
//                      this purpose. A `no_change`, a refusal or a failed save must NOT notify — that is [[B194]]
//                      inverted, and the transaction's own contract says the write never happened.
// ★★★★ §UI-16 K5 ADDS THE FIFTH AND SIXTH, AND THEY GO **HERE** RATHER THAN ON A SEAM OF THEIR OWN for this file's
//      own stated reason (see the header's slice-6 note): `mrui::IUiProvision` is ONE seam with a `default`-less op
//      dispatch, so its implementation is ONE class — and both of these are TEAM-device operations, on the same
//      `g_node` and the same one keyring instance the transaction already writes through.
//   `has_saved_key` -> `mrfw::has_saved_team_key`, i.e. `TeamKeyringService::has_record`. ⛔ A BOOLEAN: *"is a key
//                      for this exact 32-bit team id RETAINED?"*. ⛔ ZERO writes, ⛔ nothing installed, ⛔ no
//                      material returned — P-2b is that knowing the PUBLIC id may never reactivate a secret, so all
//                      this earns is an OFFER whose default is `BACK`.
//   `use_saved_key` -> `mrfw::team_keyring_use_saved`, i.e. `TeamKeyringService::use_saved` over the SAME two
//                      adapters the boot restore and the grant receive compose (`adopt_key` verifies by re-deriving
//                      the pub; `commit_active` writes the `/mrcfg` binding). ⛔ THERE IS NO SECOND INSTALL PATH IN
//                      THIS TREE, and this line is the seam where that is visible.
struct ITeamCreateDevice {
    virtual ~ITeamCreateDevice() = default;
    virtual bool       load_record(mrnv::Blob& out) = 0;
    virtual void       device_facts(ProvSnapshot& snap, ProvPhyFloor& floor) = 0;
    virtual ProvResult apply(TeamRequest& rq, const ProvSnapshot& snap) = 0;
    virtual void       on_applied(const ProvResult& r) = 0;
    virtual bool        has_saved_key(uint32_t team_id) = 0;
    virtual SavedKeyUse use_saved_key(uint32_t team_id) = 0;
};

// ---- the ACT ----------------------------------------------------------------------------------------------------
// ★★★ THE PRECONDITION IS RUN **BEFORE** ANYTHING ELSE AND REFUSES WITH NOTHING APPLIED — owner ruling 2026-08-19
//     (reported form): *"OLED team creation PRESERVES THE PERSISTED PHY and REFUSES with `PHY DIFFERS — USE SERIAL`
//     when live and persisted PHY differ."* The condition is real on this device and not hypothetical:
//     `mobile register freq=…` retunes the radio and moves `_cfg.layers[0]` WITHOUT persisting
//     (`Node::adopt_mobile_phy`, `lib/core/node.cpp:869`), so the record and the radio genuinely diverge — and a
//     create that carried no PHY would then persist a membership under a PHY the node is not flying.
// ★★ U1, AND IT IS THE POINT OF THE RULING'S IMPLEMENTATION NOTE: the comparison is `live_phy_matches`, the predicate
//    the transaction's own `no_change` rule already uses. ⛔ A second equality written here could drift from it —
//    and the one field a hand-written version would forget is `sf_list`, which is exactly [[B211]].
// ⓘ THE `sf_list` IS INCLUDED, and it comes from the RECORD (`cand.allowed_sf_bitmap`'s source), never from the live
//   reading: comparing live-against-live would make the predicate trivially true and the refusal unreachable.
inline mrui::UiProvAnswer ui_prov_create_team(ITeamCreateDevice& dev) {
    mrui::UiProvAnswer a{};
    ProvSnapshot snap{};
    ProvPhyFloor floor{};
    dev.device_facts(snap, floor);

    mrnv::Blob rec{};                                   // value-initialised: a failed load may still have written
    if (!dev.load_record(rec)) {                        // into it (device_nv.h's §nv-ritual warning)
        a.outcome = mrui::UiProvOutcome::refused;
        // ⛔ FAILS CLOSED (C2): without the record there is no persisted PHY, so the precondition cannot be
        //    ESTABLISHED — and an unestablished precondition may never be treated as satisfied. The transaction would
        //    refuse on its own load anyway (`ProvErr::nv_load_failed`), and this reports the SAME typed reason.
        a.reason  = prov_err_name(ProvErr::nv_load_failed);
        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime
    }
    // ⚠⚠ THE COMPARISON's `ProvPhy` — `present = TRUE`. See the file header: `live_phy_matches` early-returns `true`
    //    on `!present`, so this flag is what makes the precondition a comparison at all. ⛔ THIS OBJECT NEVER REACHES
    //    THE REQUEST.
    ProvPhy persisted{};
    persisted.present           = true;
    persisted.freq_mhz          = rec.freq_mhz;
    persisted.routing_sf        = rec.routing_sf;
    persisted.bw_hz             = rec.bw_hz;
    persisted.allowed_sf_bitmap = rec.allowed_sf_bitmap;
    if (!live_phy_matches(persisted, snap)) {
        a.outcome = mrui::UiProvOutcome::phy_differs;   // the panel says `PHY DIFFERS` / `USE SERIAL`
        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime, 0 retunes
    }

    // ★★ THE REQUEST — a MINT, and DELIBERATELY NOTHING ELSE. `phy` stays default-constructed (`present = false`) so
    //    the transaction carries the persisted PHY through untouched and never calls `IProvLive::apply_phy`; no key
    //    tail is supplied, so `team new`'s own mint path draws the keypair inside the candidate (the owner's ruling
    //    that a CREATOR ALWAYS ENDS UP HOLDING A KEYPAIR, guaranteed by the candidate plus a `void` install).
    // ⛔ THE FLOOR IS STILL CARRIED: it is what a `0` in the persisted record resolves to for the incomplete-PHY
    //    check, and omitting it would turn a legitimately seeded record into a refusal for the wrong reason.
    TeamRequest rq{};
    rq.mint  = true;
    rq.floor = floor;
    const ProvResult r = dev.apply(rq, snap);
    switch (r.verdict) {
        case ProvVerdict::applied:
            // ★ THE ONE ARM THAT MAY SAY SO, and the bookkeeping happens HERE rather than in the device forward so
            //   that "notify only on applied" is a decision the native suite drives (the device half is a body, not
            //   an arm).
            dev.on_applied(r);
            a.outcome = mrui::UiProvOutcome::created;
            a.team_id = r.team_id;
            return a;
        case ProvVerdict::nv_failed:
            a.outcome = mrui::UiProvOutcome::save_failed;
            return a;
        case ProvVerdict::refused:
            a.outcome = mrui::UiProvOutcome::refused;
            a.reason  = prov_err_name(r.err);           // the SERVICE's own token (U1) — never a second table
            return a;
        // ⓘ ⚠ UNREACHABLE FOR A MINT, BY CONSTRUCTION, AND MARKED RATHER THAN CLAIMED AS TESTED: `no_change` requires
        //   `!membership_changed`, and `project_team` resamples until the minted id is neither 0 nor the CURRENT team
        //   (`kTeamIdMintTries`), so a mint always changes membership. It is written out because `-Werror=switch`
        //   requires every verdict to be named and because a silent fall-through would report a create that never
        //   happened as a success. ⛔ A mutation deleting this arm stays GREEN by construction; it is not counted.
        case ProvVerdict::no_change:
            a.outcome = mrui::UiProvOutcome::refused;
            a.reason  = "no change";
            return a;
    }
    return a;
}

// ============================================== §UI-16 N3 — THE NEARBY-TEAM JOIN, ON THE **SAME** SEAM (§3.6.4 pt 3)
// ★★★★ IT IS THE SAME `ITeamCreateDevice` AND THE SAME `ProvisioningService::apply_team`, AND THAT IS THE WHOLE
//      DESIGN: §3.6.4 point 3 asks for *"the same role/PHY/team-DAD/persistence validation as the guarded
//      `team <id>`"*, and the guarded `team <id>` IS `TeamRequest{ mint = false, team_id }` through that transaction
//      (`src/firmware_provisioning_service.h`). ⛔ A second device seam, a second transaction or a second validation
//      would be exactly the parallel path U1 forbids — and the field it would eventually forget is `sf_list`.
// ★★★ THE THREE WAYS THIS ARM DIFFERS FROM THE CREATE ONE, EACH DELIBERATE:
//      1. `mint = false` and `team_id = <the observed id>` — a JOIN, ⛔ not a mint. A `mint = true` here would draw a
//         NEW random id and a NEW keypair: the operator would end up alone in a team that never existed, holding a
//         key nobody else has, having asked to join the team on the panel in front of them. ★ That is the headline
//         control of this slice's battery, and it is plausible precisely because it is one word.
//      2. the answer is `team_joined` / `join_refused`, ⛔ never `created` / `refused` — F-4: a join by id lands the
//         SAME `ProvVerdict::applied`, so without its own outcome the panel would say `TEAM CREATED` for a join.
//      3. ⛔ NO KEY IS SUPPLIED AND NONE IS SOUGHT. `key_supplied` stays false and `mint` is false, so the
//         transaction's key plan is `clear` on a membership change (`node.cpp:683`'s ruling, one layer down) ⇒ the
//         joiner ends up a **KEYLESS MEMBER** (P-2), which is what §3.6.4 point 4 requires. ⛔⛔ AND THE BAN HELD
//         WHEN §UI-16 K5 LANDED — ⛔ CORRECTED IN PLACE 2026-08-25, THE WITHDRAWN WORDING KEPT VISIBLE: this read
//         *"AND IT MUST NOT ANTICIPATE §UI-16 K5 … the explicit `SAVED KEY FOUND` / `USE SAVED KEY` offer is K5's,
//         **later**"*. K5 is here now, and ⛔ **NOTHING ABOUT THIS SENTENCE'S SUBSTANCE CHANGED**: a team whose key
//         is RETAINED in the `/mrteams` keyring is still ⛔ NOT installed here, because mere knowledge of the PUBLIC
//         team id may never reactivate a stored secret (P-2b). The one thing the applied arm now does is **ASK** —
//         `a.saved_key = dev.has_saved_key(...)`, a BOOLEAN — so the acknowledgement of this result can OFFER the
//         key on a screen whose default is `BACK`. ★ The record is still left exactly as it was found.
// ★★ THE PHY PRECONDITION IS **THE SAME RULING, REUSED** (F-5, U1): `live_phy_matches` — the transaction's own
//    predicate — against the PERSISTED record with `present = true`, while the REQUEST's `ProvPhy` stays
//    `present = false` so the transaction preserves the persisted PHY and performs NO retune. ⛔ Two objects, never
//    assigned to one another; see this file's header, which states the trap once for both arms.
// ⚠⚠ AND THE ASSIGNMENTS BELOW ARE DELIBERATELY **NOT COLUMN-ALIGNED** LIKE THE CREATE ARM'S, WHICH IS A REAL
//    CONSTRAINT AND NOT A STYLE SLIP: `tools/probe_ui_model_mutations.py --target=uiprov` anchors THREE landed
//    controls (V04/V05/V06) on those exact aligned lines by substring, so a byte-identical twin here would make each
//    of them match TWICE and be reported VACUOUS — a landed control silently retired by a new slice.
//    ⓘ MEASURED, ⛔ NOT ANTICIPATED, AND IT ACTUALLY FIRED: the first full `uiprov` pass of this slice reported
//    **`VACUOUS V04 … match count 2`**, because `allowed_sf_bitmap` is the block's longest field name and therefore
//    already sits one space from its `=` in the create arm — so THAT twin was byte-identical while the four shorter
//    ones were not. ⇒ its `=` below carries a second space, which is the whole of the fix and the reason the line
//    looks slightly off. (§UI-16 N2 hit the same hazard from the other side; its `N05` entry needed a two-line
//    anchor.)
inline mrui::UiProvAnswer ui_prov_join_team(ITeamCreateDevice& dev, uint32_t team_id) {
    mrui::UiProvAnswer a{};
    // ⛔ FAILS CLOSED (C2), and it is a FLOOR the model already holds one level up (`run_join_team` refuses a 0 pick
    //    before building an intent): 0 is not a team, and `TeamRequest{ mint = false, team_id = 0 }` is `team 0` —
    //    a LEAVE. ⇒ a join screen may never be able to perform one, whichever caller reached this seam.
    if (team_id == 0) {
        a.outcome = mrui::UiProvOutcome::join_refused;
        a.reason  = "no team";
        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime
    }
    ProvSnapshot snap{};
    ProvPhyFloor floor{};
    dev.device_facts(snap, floor);

    mrnv::Blob rec{};                                   // value-initialised: see the create arm's §nv-ritual note
    const bool have_rec = dev.load_record(rec);
    if (!have_rec) {
        // ⛔ AN UNESTABLISHED PRECONDITION IS NEVER TREATED AS SATISFIED (the create arm's rule, verbatim in
        //    substance): without the record there is no persisted PHY to compare against.
        a.outcome = mrui::UiProvOutcome::join_refused;
        a.reason  = prov_err_name(ProvErr::nv_load_failed);
        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime
    }
    ProvPhy persisted{};
    persisted.present = true;      // ⚠ THE COMPARISON's object — `live_phy_matches` early-returns true on `!present`
    persisted.freq_mhz = rec.freq_mhz;
    persisted.routing_sf = rec.routing_sf;
    persisted.bw_hz = rec.bw_hz;
    persisted.allowed_sf_bitmap  = rec.allowed_sf_bitmap;  // ⓘ from the RECORD, never the live reading ([[B211]])
    const bool phy_ok = live_phy_matches(persisted, snap);
    if (!phy_ok) {
        a.outcome = mrui::UiProvOutcome::phy_differs;   // the panel says `PHY DIFFERS` / `USE SERIAL`
        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime, 0 retunes
    }

    // ★★ THE REQUEST — a JOIN BY ID, and deliberately nothing else. `phy` stays default-constructed
    //    (`present = false`) so the transaction carries the persisted PHY through untouched and never calls
    //    `IProvLive::apply_phy` ([[B209]]); no key tail is supplied, so nothing is minted and nothing is installed.
    TeamRequest rq{};
    rq.mint    = false;            // ★★★ A JOIN, ⛔ NEVER A MINT — see difference 1 in the block above
    rq.team_id = team_id;          // ★★★ the FULL 32 bits the operator selected (P-1: byte-equal to the observed id)
    rq.floor   = floor;            // ⛔ still carried: a 0 in the record resolves against it (the create arm's note)
    const ProvResult res = dev.apply(rq, snap);
    switch (res.verdict) {
        case ProvVerdict::applied:
            // ★ THE ONE ARM THAT MAY SAY SO, and §notify-every-save's condition is a write that HAPPENED — the same
            //   rule, the same seam, the same single call site pattern as the create arm's ([[B194]] inverted).
            dev.on_applied(res);
            a.outcome = mrui::UiProvOutcome::team_joined;
            a.team_id = res.team_id;                    // ⛔ the TRANSACTION's own id, echoed — never the request's
            // ★★★★ §UI-16 K5 — AND THE **ONLY** THING THE JOIN DOES ABOUT A RETAINED KEY: it ASKS, and REPORTS the
            //      answer. ⛔ Still nothing installed, ⛔ still nothing read out of the record, ⛔ still no write —
            //      the paragraph above (difference 3) is UNCHANGED and K5 did not weaken it. What the flag earns is
            //      an OFFER screen with `BACK` selected, two presses away from an act (P-2b: the id is PUBLIC, so
            //      knowing it may never be enough).
            // ⛔ IT IS ASKED WITH THE **TRANSACTION's** id, ⛔ never the request's and ⛔ never a display token: the
            //    question must be about the team that was actually joined.
            // ⓘ ASKED ONLY ON THIS ARM: a refusal, a failed save and `no_change` joined nothing, so there is no
            //   team to offer a key for — and a keyring read on those paths would be a load spent to no purpose.
            a.saved_key = dev.has_saved_key(res.team_id);
            return a;
        case ProvVerdict::nv_failed:
            // ⛔ THE PREVIOUS MEMBERSHIP AND KEY ARE UNTOUCHED and the panel says so (`SAVE FAILED` /
            //    `NOTHING CHANGED`): the transaction applies NOTHING when its one write fails.
            a.outcome = mrui::UiProvOutcome::save_failed;
            return a;
        case ProvVerdict::refused:
            a.outcome = mrui::UiProvOutcome::join_refused;
            a.reason  = prov_err_name(res.err);         // the SERVICE's own token (U1) — never a second table
            return a;
        // ⓘ ⚠ REACHABLE, BARELY, AND HANDLED HONESTLY: `no_change` means we are ALREADY in that team. The NEARBY
        //   list filters our own team out, so the ordinary path cannot produce it — but the list is FROZEN at entry
        //   (owner ruling R-10), so a console `team <id>` between the capture and the confirmation can. ⛔ It may
        //   NOT render as a success: nothing was written, and `TEAM JOINED` on a screen that changed nothing is the
        //   "success that isn't" this project registers.
        case ProvVerdict::no_change:
            a.outcome = mrui::UiProvOutcome::join_refused;
            a.reason  = "no change";
            return a;
    }
    return a;
}

// ================================================ §UI-16 K5 — `USE SAVED KEY`, THE EXPLICIT ACTIVATION (§3.6.4 pt 4)
// ★★★★ IT IS A **MAPPING AND NOTHING ELSE**, and that is the whole design: every decision — what "retained" means,
//      the HANDLING-TIME MEMBERSHIP RE-CHECK, the load, the VERIFY-BY-ADOPTING, the activation ORDER, and the rule
//      that every non-installing arm installs NOTHING and leaves the RETAINED RECORD UNTOUCHED (refusing
//      SURGICALLY — ⛔ it may not clear a live key that belongs to the team we are in) — belongs to
//      `mrfw::TeamKeyringService::use_saved`
//      (`src/firmware_team_keyring.h`), which is pure, which `test/test_firmware_team_keyring.cpp` DRIVES and which
//      `--target=teamkeyring` attacks. ⛔ A second install sequence written here would be exactly the parallel path
//      U1 forbids — and the term it would eventually forget is the pub/priv cross-check, i.e. the one that keeps a
//      corrupt record from being installed as if it were a key.
// ★★★ THE ZERO FLOOR IS A REAL FLOOR AND NOT DECORATION (C2), and it is the THIRD ask (the model refuses an offer
//     that names no team; the service refuses id 0 too): 0 is not a team, and a screen that can reach a stored
//     secret must be unable to reach it with a wildcard, whichever caller arrives at this seam.
// ⛔⛔ AND THE TWO ENDINGS SAY ONLY WHAT IS TRUE: `installed` ⇒ the key is live AND the binding is committed, so it
//     survives the power cycle; every other arm ⇒ **THE SAVED KEY WAS NOT INSTALLED** (spec §8 S-39,
//     `KEY NOT INSTALLED`), with the SERVICE's token beside it. ★ THE FAILING WORD NAMES **THIS ACT'S OUTCOME** AND
//     ⛔ NEVER THE NODE'S KEY INVENTORY, which is what keeps it true: a refusal installs nothing and PRESERVES
//     whatever unrelated key state the node already had — only `binding_failed` undoes THIS operation's own
//     adoption. ⛔ No arm reports a failure as a success, and ⛔ no key byte reaches either.
inline mrui::UiProvAnswer ui_prov_use_saved_key(ITeamCreateDevice& dev, uint32_t team_id) {
    mrui::UiProvAnswer a{};
    if (team_id == 0) {
        a.outcome = mrui::UiProvOutcome::saved_key_failed;
        a.reason  = saved_key_use_name(SavedKeyUse::zero_team);   // the SERVICE's own token (U1)
        return a;                                       // ⛔ 0 keyring calls, 0 writes, 0 airtime
    }
    const SavedKeyUse v = dev.use_saved_key(team_id);
    // ★ ONE ARM IS THE SUCCESS AND IT IS NAMED, ⛔ never `v != something`: a verdict added to `SavedKeyUse` later
    //   must land on the FAILING side by default, because a new outcome is a new way for the activation not to have
    //   completed. ⓘ That is the fail-closed direction; the opposite would be a "success that isn't".
    if (v == SavedKeyUse::installed) {
        a.outcome = mrui::UiProvOutcome::saved_key_used;
        // ⛔ NO SECOND ROW AND ⛔ NO ID: `UiProvAnswer::team_id` is documented as meaningful for `created` /
        //    `team_joined` only, and the success screen's whole message is that the key is active. A token nobody
        //    renders is a field that eventually gets rendered by somebody.
        return a;
    }
    a.outcome = mrui::UiProvOutcome::saved_key_failed;
    a.reason  = saved_key_use_name(v);                  // ⛔ a FACT token, ⛔ never material
    return a;
}

// ================================================================== §UI-15 slice 6 — THE STATIC-JOIN HALF (§3.6.3)
// ---- the DEVICE half, as a seam ---------------------------------------------------------------------------------
// ★★ THREE OPERATIONS, EVERY ONE OF THEM A FORWARD, and the shape is `ITeamCreateDevice`'s deliberately (U3):
//    read · act · notify-on-the-one-arm.
//   `list_profiles` -> `mrfw::JoinProfileService::list` (slice 2), i.e. the SAME service the `joinprofile` verbs use.
//                      ⛔ NOT a second reader of `/mrjoin`: `list()` is where "leave a VALID EMPTY record behind on a
//                      failed read" lives, so a panel that rendered anyway shows nothing rather than garbage.
//   `apply`         -> `mrfw::JoinService::apply_join` (slice 1), i.e. the SAME transaction the `join` verb runs.
//                      ⛔ Never a second validate/load/save path — that is the whole reason slice 1 was extracted.
//   `on_started`    -> ⛔ CALLED ON THE `started` ARM AND NOWHERE ELSE (see `ui_prov_join_static`): it is the
//                      §notify-every-save hook, whose condition is "a durable write happened". A refusal spends no
//                      write and a failed save reports one that did not land, so neither may notify — [[B194]]
//                      inverted, exactly as `ITeamCreateDevice::on_applied` states it one screen over.
struct IJoinDevice {
    virtual ~IJoinDevice() = default;
    virtual ProfileResult list_profiles(mrnv::JoinBlob& out) = 0;
    virtual JoinResult    apply(const JoinRequest& rq) = 0;
    virtual void          on_started(const JoinResult& r) = 0;
};

// ---- the SELECT screen's ONE read ------------------------------------------------------------------------------
// ★ `served = true` is set BEFORE the read and not after it: it means *"a seam answered"*, ⛔ not *"the answer was
//   good"*. The four store states are `res`'s, and the panel tells them apart (`mrui::join_store_head`).
inline mrui::UiJoinList ui_prov_join_profiles(IJoinDevice& dev) {
    mrui::UiJoinList l{};
    l.served = true;
    l.res    = dev.list_profiles(l.rec);
    return l;
}

// ---- the ACT ----------------------------------------------------------------------------------------------------
// ★★★★ THE ONE INTEGRAL -> DOUBLE CONVERSION OF THIS FEATURE, AND IT LIVES HERE BECAUSE THE REQUEST IS WHAT NEEDS IT
//      (plan §3): the STORED profile is integral Hz — 869.4625 MHz is exactly 869462500 Hz and is ⛔ not
//      representable in integral kHz — while the TRANSIENT `JoinRequest` carries the operator's raw MHz/kHz
//      `double`s, because `valid_bw_khz` must be able to accept the NaN the console has always accepted
//      ([[B216]], and `firmware_join_service.h`'s standing instruction). ⇒ `join_request_from_profile` is the ONE
//      authority for that conversion (U2) and is CALLED, ⛔ never re-derived here: a second `freq_hz / 1e6` would let
//      the panel and the radio disagree about which carrier a profile means.
// ⛔ AN EMPTY SLOT IS NOT A JOIN, and this is a SECOND floor rather than a duplicate check: the model already refuses
//    a pick that names no present slot, and this refuses one that reached the seam anyway (a probe, a future caller).
//    ⓘ It costs zero writes and zero airtime, which is what a floor must cost.
inline mrui::UiProvAnswer ui_prov_join_static(IJoinDevice& dev, const mrnv::JoinProfile& p) {
    mrui::UiProvAnswer a{};
    if (!p.present) {
        a.outcome = mrui::UiProvOutcome::join_refused;
        a.reason  = "empty slot";
        return a;                                    // ⛔ 0 transaction calls, 0 writes, 0 airtime
    }
    const JoinRequest rq = join_request_from_profile(p);
    const JoinResult  r  = dev.apply(rq);
    switch (r.verdict) {
        case JoinVerdict::started:
            // ★ THE ONE ARM THAT WROTE. The bookkeeping happens HERE rather than in the device forward so that
            //   "notify only on started" is a decision the native suite drives.
            dev.on_started(r);
            // ⛔⛔ `joining`, ⛔ NEVER `joined`: the transaction has written once and STARTED DAD, and the real
            //    outcome arrives later as a push that `mrui::join_push_correlates` must accept first (plan §2.3).
            a.outcome = mrui::UiProvOutcome::joining;
            return a;
        case JoinVerdict::nv_failed:
            a.outcome = mrui::UiProvOutcome::save_failed;
            return a;
        case JoinVerdict::refused:
            a.outcome = mrui::UiProvOutcome::join_refused;
            a.reason  = join_err_name(r.err);        // the TRANSACTION's own token (U1) — never a second table
            return a;
    }
    return a;
}

// ---- the model's seam, implemented ONCE, HERE --------------------------------------------------------------------
// ★ The op dispatch is `default`-less so a third intent (§UI-16's nearby-team join) cannot be added without a reader
//   stating what performs it. ⛔ `none` FAILS CLOSED: it performs nothing and answers `none`, which renders nothing —
//   an intent nobody raised must never become an act.
class UiProvisionAdapter : public mrui::IUiProvision {
  public:
    UiProvisionAdapter(ITeamCreateDevice& team, IJoinDevice& join) : _dev(team), _join(join) {}
    mrui::UiProvAnswer perform(const mrui::UiProvIntent& intent) override {
        switch (intent.op) {
            case mrui::UiProvOp::create_team: return ui_prov_create_team(_dev);
            case mrui::UiProvOp::join_static: return ui_prov_join_static(_join, intent.join);
            // ★ §UI-16 N3 — the THIRD op, and the `default`-less switch is what forced this line to be written: the
            //   nearby join goes to the TEAM device (`_dev`), because it is the TEAM transaction. ⛔ Sending it to
            //   `_join` would run the STATIC-network join for a team the operator selected.
            case mrui::UiProvOp::join_team:   return ui_prov_join_team(_dev, intent.team_id);
            // ★ §UI-16 K5 — the FOURTH op, and the `default`-less switch is what forced this line to be written:
            //   the saved-key activation goes to the TEAM device (`_dev`), because the keyring, the live key and
            //   the `/mrcfg` binding are all that device's. ⛔ It carries `intent.team_id` — the id the TRANSACTION
            //   joined — and ⛔ never `intent.join`, which is another plane's record entirely.
            case mrui::UiProvOp::use_saved_key: return ui_prov_use_saved_key(_dev, intent.team_id);
            case mrui::UiProvOp::none:        break;
        }
        return mrui::UiProvAnswer{};
    }
    mrui::UiJoinList profiles() override { return ui_prov_join_profiles(_join); }

  private:
    ITeamCreateDevice& _dev;
    IJoinDevice&       _join;
};

}  // namespace mrfw
