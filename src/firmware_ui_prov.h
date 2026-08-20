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
struct ITeamCreateDevice {
    virtual ~ITeamCreateDevice() = default;
    virtual bool       load_record(mrnv::Blob& out) = 0;
    virtual void       device_facts(ProvSnapshot& snap, ProvPhyFloor& floor) = 0;
    virtual ProvResult apply(TeamRequest& rq, const ProvSnapshot& snap) = 0;
    virtual void       on_applied(const ProvResult& r) = 0;
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
