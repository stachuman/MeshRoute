// MeshRoute — test_firmware_ui_input.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// NB: the native build is -fno-exceptions, so doctest's REQUIRE is unavailable (it throws to abort) —
//     CHECK only, guard any dependent step with an `if`. Every "REQUIRE" in the plan is a CHECK here.
//
// UI-1 (plan docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md Task 1, spec §3.2/§4): the pure
// one-button gesture classifier. Timing tables are miserable to debug on hardware and trivial here, which is the
// whole reason firmware_ui_input.h is a pure header reachable via `-I src` (platformio.ini:83).
#include "doctest.h"
#include "firmware_ui_input.h"
#include <cstdint>

using namespace mrui;

// Drive the classifier at a 5 ms poll cadence over an INCLUSIVE ms window and report the FIRST non-none gesture.
// The classifier must be polled every pass, pressed or not: a tap is only single in hindsight (the short_press is
// emitted when the double window expires, with the button already released).
static Gesture run_until(InputFsm& f, bool pressed, uint32_t from_ms, uint32_t until_ms) {
    Gesture got = Gesture::none;
    for (uint32_t t = from_ms; t <= until_ms; t += 5) {
        const Gesture g = f.update(pressed, t);
        if (g != Gesture::none && got == Gesture::none) got = g;
    }
    return got;
}

// Same, but stepped by COUNT rather than by end-time, so a window may cross the millis() wrap (where `t <= until`
// is false from the first iteration and run_until would silently do nothing — a 0/N that means "cannot reach").
static Gesture run_steps(InputFsm& f, bool pressed, uint32_t from_ms, unsigned steps, uint32_t step_ms = 5) {
    Gesture got = Gesture::none;
    uint32_t t = from_ms;
    for (unsigned i = 0; i < steps; ++i, t += step_ms) {
        const Gesture g = f.update(pressed, t);
        if (g != Gesture::none && got == Gesture::none) got = g;
    }
    return got;
}

// ---------------------------------------------------------------- the plan's six cases

TEST_CASE("ui-input: single tap yields short_press after the double window") {
    InputFsm f;
    CHECK(run_until(f, true,  0,   60)  == Gesture::none);
    CHECK(run_until(f, false, 65,  200) == Gesture::none);
    CHECK(run_until(f, false, 205, 500) == Gesture::short_press);
}

TEST_CASE("ui-input: two taps inside the window yield double_press") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120); run_until(f, true, 125, 180);
    CHECK(run_until(f, false, 185, 400) == Gesture::double_press);
}

TEST_CASE("ui-input: hold yields long_arm then long_fire") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)    == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 3600) == Gesture::long_fire);
}

TEST_CASE("ui-input: release between arm and fire cancels and never fires") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)      == Gesture::long_arm);
    CHECK(run_until(f, false, 905, 1200)  == Gesture::long_cancel);
    CHECK(run_until(f, false, 1205, 5000) != Gesture::long_fire);
}

TEST_CASE("ui-input: bounce shorter than debounce_ms is ignored") {
    InputFsm f; f.update(true, 0); f.update(false, 10);
    CHECK(run_until(f, false, 15, 600) == Gesture::none);
}

TEST_CASE("ui-input: hold_ms reports countdown progress") {
    InputFsm f; run_until(f, true, 0, 1000);
    CHECK(f.hold_ms(1000) >= 950);
}

// ---------------------------------------------------------------- coverage the plan's six leave open

// The negative in "bounce is ignored" is worth a positive control: the SAME instrument, with the level held one
// debounce period longer, does produce a gesture. Without this, "== none" could mean "run_until cannot fire".
TEST_CASE("ui-input: positive control — a press one debounce longer than the ignored bounce DOES register") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 30) == Gesture::none);      // stabilises at 25, no gesture yet
    CHECK(run_until(f, false, 35, 500) == Gesture::short_press);
}

// §4.3/§5 comparisons are wrap-safe unsigned everywhere in this feature; the classifier is the first place a
// millis() rollover can be observed, because it is the only unit that timestamps a physical edge.
TEST_CASE("ui-input: arm and fire survive the millis() wrap") {
    InputFsm f;
    const uint32_t base = 0xFFFFFF00u;                     // 256 ms before the rollover
    CHECK(run_steps(f, true, base, 200) == Gesture::long_arm);    // 200 * 5 ms = 1000 ms, crossing 0
    CHECK(run_steps(f, true, base + 1000u, 600) == Gesture::long_fire);
}

TEST_CASE("ui-input: hold_ms is wrap-safe and zero while released") {
    InputFsm f;
    CHECK(f.hold_ms(1234) == 0);                           // never pressed
    run_steps(f, true, 0xFFFFFF00u, 20);                   // press stabilises just before the wrap
    CHECK(f.hold_ms(0x00000100u) > 0);
    CHECK(f.hold_ms(0x00000100u) < 1000u);                 // ~356 ms, not a 4-billion-ms garbage value
    run_steps(f, false, 0x00000105u, 20);
    CHECK(f.hold_ms(0x00000200u) == 0);                    // released -> no hold progress
}

// A sparse poll (a loop pass blocked by a long radio op) must not fold arm and fire into one call: the model
// enters `arming` on long_arm and only then may fire, so a single call emitting both would skip ARMING entirely.
TEST_CASE("ui-input: one call never emits both long_arm and long_fire, however sparse the polling") {
    InputFsm f;
    CHECK(f.update(true, 0)     == Gesture::none);         // edge seen, not yet debounced
    CHECK(f.update(true, 5000)  == Gesture::none);         // debounced press: the hold clock starts HERE
    CHECK(f.update(true, 10000) == Gesture::long_arm);     // 5000 ms held -> arm (fire threshold also passed)
    CHECK(f.update(true, 10005) == Gesture::long_fire);    // fire needs the NEXT call
}

TEST_CASE("ui-input: after long_fire, holding on emits nothing and the release is silent") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)      == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 3600)   == Gesture::long_fire);
    CHECK(run_until(f, true, 3605, 8000)  == Gesture::none);   // no second fire, no arm
    CHECK(run_until(f, false, 8005, 9000) == Gesture::none);   // NOT long_cancel: it already fired
    CHECK(run_until(f, false, 9005, 9600) == Gesture::none);   // and NOT a short_press either
}

TEST_CASE("ui-input: a fresh tap classifies normally after a completed fire") {
    InputFsm f;
    run_until(f, true, 0, 900); run_until(f, true, 905, 3600); run_until(f, false, 3605, 4200);
    CHECK(run_until(f, true, 5000, 5060)  == Gesture::none);
    CHECK(run_until(f, false, 5065, 5600) == Gesture::short_press);
}

// The double window is armed on RELEASE, so a contact glitch inside it must not consume the pending tap: the
// press never debounces, therefore it is neither a second tap nor a reason to drop the first.
TEST_CASE("ui-input: a glitch inside the double window neither doubles nor swallows the tap") {
    InputFsm f;
    run_until(f, true, 0, 60);
    CHECK(run_until(f, false, 65, 190) == Gesture::none);
    f.update(true, 200); f.update(false, 210);             // 10 ms glitch, below debounce_ms
    CHECK(run_until(f, false, 215, 500) == Gesture::short_press);
}

TEST_CASE("ui-input: three taps read as double_press then short_press") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120); run_until(f, true, 125, 180);
    CHECK(run_until(f, false, 185, 240) == Gesture::double_press);
    CHECK(run_until(f, true, 300, 360)  == Gesture::none);
    CHECK(run_until(f, false, 365, 800) == Gesture::short_press);
}

// A tap followed by a HOLD inside the double window is a hold, not a double: the second press never releases
// inside the window, so on_release sees `_armed` and reports the cancel, having dropped the pending tap.
TEST_CASE("ui-input: tap then hold inside the double window arms instead of doubling") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120);
    CHECK(run_until(f, true, 125, 1000)   == Gesture::long_arm);
    CHECK(run_until(f, false, 1005, 2000) == Gesture::long_cancel);
    CHECK(run_until(f, false, 2005, 3000) == Gesture::none);     // the swallowed tap is NOT replayed
}

// The button may already be down when the classifier is constructed (a stuck button, or a boot with the button
// held). It must debounce from the first sample rather than treating the initial level as a fresh edge at t=0.
TEST_CASE("ui-input: a button already down at construction still debounces before arming") {
    InputFsm f;
    CHECK(f.update(true, 9000) == Gesture::none);          // first sample: edge recorded, not stable
    CHECK(f.update(true, 9010) == Gesture::none);          // still inside debounce_ms
    CHECK(f.update(true, 9030) == Gesture::none);          // debounced press, hold clock starts
    CHECK(f.update(true, 9800) == Gesture::none);          // 770 ms held: below arm_ms
    CHECK(f.update(true, 9835) == Gesture::long_arm);      // 805 ms held
}

// ---------------------------------------------------------------- §B197: active() — "a gesture is being classified"
// ★★★ WHY THIS PREDICATE EXISTS AND WHY IT IS TESTED HERE. The device sleep gate may not light-sleep while a gesture
//   is undecided: an ESP32 light-sleep pass runs up to MR_MAX_SLEEP_MS = 1000 ms, which is longer than debounce_ms
//   (25), double_gap_ms (350) AND arm_ms (800) — so sleeping between the edge and the decision is what turned a real
//   short tap into "nothing happened" on metal ([[B197]]). The classifier is the only unit that knows.
// ⚠ THE CASES BELOW ARE WRITTEN AS "TRUE THROUGH THE WHOLE WINDOW, FALSE THE INSTANT THE DECISION LANDS" rather than
//   as spot checks, because the harm is a HOLE in the window, not a wrong value at one instant.

TEST_CASE("ui-input: active() is false at rest and true from the very first pressed sample") {
    InputFsm f;
    CHECK(f.active() == false);                            // constructed idle: nothing to protect
    f.update(true, 0);                                     // the raw edge — NOT yet debounced
    CHECK(f.active() == true);                             // ...and the CPU must already be held awake
    f.update(true, 10);  CHECK(f.active() == true);        // still inside debounce_ms
    f.update(true, 30);  CHECK(f.active() == true);        // debounced press: held
}

TEST_CASE("ui-input: active() spans the whole single-tap decision and clears exactly when short_press lands") {
    InputFsm f;
    run_until(f, true, 0, 60);
    CHECK(f.active() == true);                             // held
    CHECK(f.update(false, 100) == Gesture::none);
    CHECK(f.active() == true);                             // RELEASE debounce: _raw is already false, _stable is not
    CHECK(f.update(false, 130) == Gesture::none);
    CHECK(f.active() == true);                             // _pending_tap: single vs double is still undecided
    // ★ THE WHOLE double_gap_ms WINDOW, sampled densely — a hole anywhere in it is a pass the device could sleep on.
    // ⓘ The window is measured from the RELEASE EDGE (t = 100), not from the debounce completion — `on_release`
    //   sets `_release_ms = _edge_ms`. So the decision is due at 100 + double_gap_ms = 450, and 445 is the last
    //   sample that must still read active.
    for (uint32_t t = 135; t < 450; t += 5) { f.update(false, t); CHECK(f.active() == true); }
    CHECK(f.update(false, 450) == Gesture::short_press);    // the decision lands...
    CHECK(f.active() == false);                             // ...and only now may the CPU sleep
}

TEST_CASE("ui-input: active() clears on a double_press, which never enters the pending-tap window") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120); run_until(f, true, 125, 180);
    CHECK(f.active() == true);
    CHECK(f.update(false, 185) == Gesture::none);           // release debounce of the SECOND tap
    CHECK(f.active() == true);
    CHECK(f.update(false, 215) == Gesture::double_press);
    CHECK(f.active() == false);
}

TEST_CASE("ui-input: active() holds through arm and fire, and clears once the fired press is released") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900) == Gesture::long_arm);
    CHECK(f.active() == true);
    CHECK(run_until(f, true, 905, 3600) == Gesture::long_fire);
    CHECK(f.active() == true);                              // still held down — the alarm screen is live
    CHECK(run_until(f, true, 3605, 5000) == Gesture::none);
    CHECK(f.active() == true);
    CHECK(f.update(false, 5100) == Gesture::none);
    CHECK(f.active() == true);                              // release debounce
    CHECK(f.update(false, 5130) == Gesture::none);          // _fired consumes the release: no gesture, no pending tap
    // ⛔⛔ THE ONE THAT WOULD BE A DEFECT IF IT WERE TRUE. `_fired` stays set until the next debounced press, so a
    //    predicate that included it would hold the CPU AWAKE FOR EVER after a single emergency — on a battery-powered
    //    safety device. This is why the design gives `_armed`/`_fired` no term of their own.
    CHECK(f.active() == false);
}

TEST_CASE("ui-input: active() clears on long_cancel too (armed, released before the fire)") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900) == Gesture::long_arm);
    CHECK(f.update(false, 1000) == Gesture::none);
    CHECK(f.active() == true);                              // release debounce
    CHECK(f.update(false, 1030) == Gesture::long_cancel);
    CHECK(f.active() == false);                             // `_armed` is history; it must not hold the CPU up
}

// A bounce shorter than debounce_ms produces NO gesture — but it is still an undecided edge while it lasts, and the
// device must not sleep through it. ★ This is the case that separates `_raw` from `_stable`: `_stable` never moves
// here, so a predicate built on `_stable` alone reads "idle" across a real physical press.
TEST_CASE("ui-input: active() is true during a sub-debounce bounce that yields no gesture at all") {
    InputFsm f;
    f.update(true, 0);
    CHECK(f.active() == true);
    CHECK(f.update(false, 10) == Gesture::none);            // the bounce ends before debounce_ms
    CHECK(run_until(f, false, 15, 600) == Gesture::none);
    CHECK(f.active() == false);                             // settled again, nothing pending
}

// The cfg is per-instance, so a bench retune of fire_ms must be honoured without touching the class.
TEST_CASE("ui-input: InputCfg is honoured — a retuned fire_ms moves the fire, not the arm") {
    InputFsm f(InputCfg{/*debounce_ms=*/25, /*double_gap_ms=*/350, /*arm_ms=*/800, /*fire_ms=*/1500});
    CHECK(run_until(f, true, 0, 900)    == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 1600) == Gesture::long_fire);   // 1500, not 3500
}
