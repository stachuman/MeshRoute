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
#pragma once
#include "device_nv.h"                     // mrnv::Blob — the PERSISTED record the precondition reads
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

// ---- the model's seam, implemented ONCE, HERE --------------------------------------------------------------------
// ★ The op dispatch is `default`-less so a second intent (slice 6's join) cannot be added without a reader stating
//   what performs it. ⛔ `none` FAILS CLOSED: it performs nothing and answers `none`, which renders nothing — an
//   intent nobody raised must never become an act.
class UiProvisionAdapter : public mrui::IUiProvision {
  public:
    explicit UiProvisionAdapter(ITeamCreateDevice& dev) : _dev(dev) {}
    mrui::UiProvAnswer perform(const mrui::UiProvIntent& intent) override {
        switch (intent.op) {
            case mrui::UiProvOp::create_team: return ui_prov_create_team(_dev);
            case mrui::UiProvOp::none:        break;
        }
        return mrui::UiProvAnswer{};
    }

  private:
    ITeamCreateDevice& _dev;
};

}  // namespace mrfw
