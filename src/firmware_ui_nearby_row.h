// MeshRoute — src/firmware_ui_nearby_row.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 N2 — THE NEARBY ROW'S THREE TOKENS, i.e. the render half of `src/firmware_ui_nearby.h`: the
// `n/3` signal token (owner ruling R-4) and the `<fingerprint> <n/3> <age>` row (spec string S-6).
//
// ⚠⚠ WHY IT IS A SEPARATE FILE FROM `firmware_ui_nearby.h` — REPORTED, because the spec's file list named
//    ONE new unit. It is an INCLUDE-ORDER FACT of this tree, measured rather than preferred:
//    `src/firmware_ui_chrome.h:36` includes `firmware_ui_model.h`, so a header the MODEL includes may not
//    include chrome — and `firmware_ui_nearby.h` IS model-included, because `UiSnapshot` publishes its
//    `NearbyRow` array and `UiState` freezes its `NearbyList`. The two tokens below genuinely need
//    chrome's SHARED formatters (`ui_fmt_team_fingerprint`, `ui_fmt_home_age`, `ui_pad_token`), and
//    ⛔ re-spelling any of them locally is the one thing U1 forbids here — so they sit downstream instead.
// ★ THAT IS THIS TREE'S OWN LAYERING: `TeamRow` lives in `firmware_ui_model.h` and its formatter
//   `ui_team_row` in `firmware_ui_team.h` (model + chrome + geo); `firmware_ui_status.h` sits the same
//   way. ⇒ each half keeps its OWN mutation target (`uinearby` / `uinearbyrow`), so R-4's
//   "second definition of signal quality" control and the own-team filter's control never share a file.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>                  // snprintf — §B115: the row is composed HERE so the suite reads the BYTES
#include "protocol_constants.h"    // presence_quality_tier — THE tier map (⛔ never a second definition)
#include "firmware_ui_nearby.h"    // NearbyRow — the carrier this file renders
#include "firmware_ui_chrome.h"    // ui_fmt_team_fingerprint / ui_fmt_home_age / ui_pad_token — CALLED, never forked

namespace mrui {

// --------------------------------------------------------------------------------- the three tokens
// ★★★★ THE SIGNAL TOKEN (owner ruling R-4, 2026-08-22) — FOUR LEVELS, `0/3 1/3 2/3 3/3`, AND THE TIER IS
//      **`protocol::presence_quality_tier`'s ANSWER**, ⛔ NEVER A SECOND DEFINITION OF SIGNAL QUALITY.
//      That function (`lib/core/protocol_constants.h`) is the tree's one SNR -> tier map: pure,
//      `constexpr`, boundaries −12 / −4 / +4 dB, already shared by the home's per-mobile EWMA and the
//      mobile's heard-candidate EWMA. The draft proposed a THREE-level token derived from
//      `bucket_of_snr_4b` and it was REFUSED on two counts — that function is not a UI token source, and
//      a three-level mapping would be a second definition of the same quantity.
// ⛔ THE UI'S JOB IS THE SPELLING AND NOTHING ELSE. Re-derive the tier here from raw q4 thresholds and
//    the panel and the re-home logic can disagree about the same link — which is why the mutation that
//    does exactly that is this file's headline control.
// ⓘ ASCII BY RULING, three fixed columns, so it survives every font and every console transcript.
// ⓘ THE DENOMINATOR IS DERIVED, ⛔ not typed: `presence_q_strong` IS the top tier, so a fifth tier moves
//   the token with it instead of leaving `3/3` naming a scale that no longer exists.
inline constexpr uint8_t kNearbyTierMax = uint8_t(MESHROUTE_NS::protocol::presence_q_strong);
inline constexpr std::size_t kNearbySignalCap = 4;    // `3/3` + NUL
inline void ui_fmt_nearby_signal(char* out, std::size_t cap, int16_t snr_q4) {
    const unsigned tier = MESHROUTE_NS::protocol::presence_quality_tier(snr_q4);
    const int n = snprintf(out, cap, "%u/%u", tier, unsigned(kNearbyTierMax));
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);   // the neighbours' rule: the WHOLE buffer is defined
}

// ★★★★ THE ROW (spec S-6): `<fingerprint> <n/3> <age>` -> `3D9348 2/3 42s`.
// ★★★ THE FINGERPRINT IS `mrui::ui_fmt_team_fingerprint` AND NOTHING ELSE (U1, spec §8's opening rule).
//     That helper was landed by §UI-15 slice 3 FOR THIS SCREEN and had no caller until now; its own
//     header names this list as one of the two consumers arriving later. ⛔ A second spelling of "the low
//     24 bits, uppercase, zero-padded" here would be the S1/L9 fork this project keeps paying for — and a
//     selection token that disagrees between the inviter's panel and the joiner's is worse than none.
// ★★ THE AGE IS `ui_fmt_home_age`'s TOKEN, REUSED (spec S-6): one bucketing of a millisecond age in this
//    tree, `--` included. ⛔ Not a second `%us`/`%um` ladder.
// ⛔⛔ AND THERE IS **NOTHING NAME-SHAPED ON THIS ROW** (spec §3 P-5/P-5b, ruling R-13 rule 1). A NEARBY
//     row is identified by the **TEAM** fingerprint, and ⛔ AN ADVERTISER'S NODE NAME IS NEVER PRESENTED
//     AS THE TEAM'S NAME. The temptation is real rather than theoretical — the beacon sender's id is in
//     scope at the observation site and the tree already has a name resolver one screen over
//     (`label_for_team_id`, src/firmware_ui.cpp) — so the mutation that resolves the sender's name into
//     this row is one of this file's headline controls. There is no team LABEL anywhere in this firmware
//     to show (spec F-3), and a node's name is a different thing about a different entity.
// ⓘ WIDTH, DERIVED (never guessed): 6 fingerprint + 1 + 3 signal + 1 + at most 3 age = 14, and the
//   renderer's own cursor marker makes 15 of the 19-column body.
inline constexpr std::size_t kNearbyRowCap =
    (kTeamFpTokenCap - 1) + 1 + (kNearbySignalCap - 1) + 1 + (kAgeTokenCap - 1) + 1;
inline void ui_fmt_nearby_row(char* out, std::size_t cap, const NearbyRow& r) {
    if (!out || cap == 0) return;
    char fp[kTeamFpTokenCap];   ui_fmt_team_fingerprint(fp, sizeof fp, r.team_id);
    char sig[kNearbySignalCap]; ui_fmt_nearby_signal(sig, sizeof sig, r.snr_q4);
    char age[kAgeTokenCap];     ui_fmt_home_age(age, sizeof age, r.age_valid, r.age_ms);
    const int n = snprintf(out, cap, "%s %s %s", fp, sig, age);
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

}  // namespace mrui
