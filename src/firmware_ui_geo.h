// MeshRoute — src/firmware_ui_geo.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-17 slice S5 — HOW FAR AWAY A TEAMMATE IS, AND IN WHICH DIRECTION, DECIDED WHERE A TEST CAN DRIVE IT. Every
// decision the TEAM row's two new columns express lives here: the FRESHNESS bound, the geometry, the two tokens and
// — above all — the four-term rule that says whether either column may be drawn AT ALL.
// Normative: docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md §3.4 (the four terms, the
// four load-bearing maths rules and the coincident-point ruling) and §8 strings S-13 / S-14.
//
// ★★★ WHY IT IS A FILE OF ITS OWN — the §B115 rule this cluster states at every screen
//     (`firmware_ui_model.h:102-104`): **a decision made in `firmware_ui.cpp` is a decision no automated gate can
//     read**, because that TU is compiled by neither the native suite nor the simulator. ⇒ its own header AND its own
//     mutation battery target (`--target=uigeo`), because a battery is per-SOURCE-FILE: folded into the row formatter
//     these six rules would have shared `uiteam`'s entries and neither the freshness bound nor the antimeridian fold
//     would have had an isolated control ([[B217]]'s shape).
//
// ⛔⛔⛔ WHAT THIS FILE MAY NOT DO, AND IT IS THE WHOLE POINT OF THE SLICE (spec §3.4):
//   1. **⛔ NOTHING HERE ASKS FOR A POSITION.** There is no request, no broadcast, no refresh and no timer. The
//      values arrive through the snapshot from `Node::peer_loc_find` — a `const` read of a cache the RECEIVE path
//      already fills from AUTHENTICATED traffic (`node_hashlocate.cpp`'s §AB4 ring; the two callers each prove
//      their own evidence before storing). Rendering TEAM creates NO traffic of any kind, and the probe counts
//      that rather than arguing it.
//   2. **⛔ NO EVIDENCE ⇒ NO COLUMN.** Missing own fix, unresolved peer, a cache miss or a position past the
//      freshness bound all render **BLANK** — ⛔ never `0m`, ⛔ never a retained old coordinate wearing
//      current-looking units, ⛔ never an estimate (C2). A stale fix on a safety screen is worse than no fix,
//      because the panel presents it as fact.
//   3. **⛔ A ZERO-LENGTH VECTOR HAS NO BEARING.** Coincident points render `0m` and a **BLANK** direction
//      (`has_bearing = false`) — ⛔ never octant 0 (`N`), which would be a FABRICATED cardinal. Owner-ruled
//      2026-08-20; the spec keeps the withdrawn *"`0m` with a defined bearing token"* wording visible beside it.
//   4. ⛔ The bearing is GEOGRAPHIC — from OUR coordinate to the peer's last reported one. ⛔ Not movement, ⛔ not
//      relative to how the device is held, and ⛔ cardinal TEXT rather than an arrow: this panel has no compass and
//      must not imply one.
#pragma once
#include <cmath>     // std::sqrt / std::cos — the equirectangular projection (spec §3.4's ⚠ on libm; measured in §6)
#include <cstddef>   // std::size_t
#include <cstdint>
#include <cstdio>    // snprintf — the tokens are composed HERE so the native suite asserts VISIBLE BYTES
#include "firmware_ui_chrome.h"   // ui_pad_token — REUSED, not forked: every token defines its WHOLE buffer

namespace mrui {

// ================================================================================== THE FRESHNESS BOUND (spec §3.4)
// ★★★★ **TEN MINUTES, OWNER-APPROVED, AND IT IS ONE NAMED CONSTANT** — the boundary cases (599 / 600 / 601) are
//      pinned against THIS name, so an owner ruling moves the bound in exactly one place.
// ⓘ THE COMPARISON IS `<=`: 600 s SHOWS, 601 s BLANKS. That is a decision and not an off-by-one — a mutation flips
//   it and the native suite reddens.
// ⚠ `0xFFFFFFFF` IS NOT A LARGE AGE, IT IS THE CACHE's OWN "I CANNOT DATE THIS": `peer_loc_find` returns it when the
//   clock has moved BACKWARDS (`node_hashlocate.cpp:441`), deliberately failing in the direction the app discards.
//   It is past the bound by construction, so it blanks through the same arm rather than needing a second test —
//   pinned by its own case so the agreement can never become accidental.
inline constexpr uint32_t kPeerLocMaxAgeS = 600;
inline bool ui_geo_fresh(uint32_t age_s) { return age_s <= kPeerLocMaxAgeS; }

// ===================================================================================== THE EIGHT-WAY BEARING (S-14)
// ⛔ EIGHT STATES, AND "NO BEARING" IS **NOT** ONE OF THEM — it is `has_bearing = false` below, precisely so that a
//    coincident point cannot be spelled as a cardinal by accident (the ruling above).
enum class GeoOctant : uint8_t { n = 0, ne, e, se, s, sw, w, nw };

// ★ THE LEXEMES (string inventory S-14), declared ONCE. ⛔ Cardinal TEXT, ⛔ never an arrow glyph.
// ⓘ `-Wswitch` covers the enum (it is GATE-BLOCKING in this project), so a ninth state would fail the build; the
//   trailing return exists only to satisfy `-Wreturn-type`.
inline const char* ui_geo_dir_lexeme(GeoOctant o) {
    switch (o) {
        case GeoOctant::n:  return "N";
        case GeoOctant::ne: return "NE";
        case GeoOctant::e:  return "E";
        case GeoOctant::se: return "SE";
        case GeoOctant::s:  return "S";
        case GeoOctant::sw: return "SW";
        case GeoOctant::w:  return "W";
        case GeoOctant::nw: return "NW";
    }
    return "";
}

// ======================================================================================= OUR OWN FIX, AS A CARRIER
// ★ THE THREE PUBLISHED `UiSnapshot` FIELDS, CARRIED TOGETHER because they are only ever meaningful together:
//   `(0,0)` with `have == false` is "no position", ⛔ never the Gulf of Guinea. Built at ONE site
//   (`ui_geo_fix_of`, `firmware_ui_team.h`) — ⛔ never assembled field-by-field at a second one (U2).
// ⓘ `have` IS `ui_status_have_fix`'s published answer (`UiSnapshot::own_fix`), taken at the one site that can see
//   `NodeConfig`. ⛔ Nothing here re-derives it: the core itself refuses a located send when both coordinates are
//   zero, and one surface disagreeing with that refusal is the whole failure that field exists to prevent.
struct GeoFix {
    bool    have   = false;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
};

// ===================================================================================== THE GEOMETRY (spec §3.4)
// ★★★★ FOUR RULES, AND ALL FOUR ARE LOAD-BEARING RATHER THAN STYLISTIC. Each has its own native case and its own
//      mutation, because each fails SILENTLY — a plausible number in a four-column field is exactly what this screen
//      exists not to draw.
// ⛔⛔ **CORRECTED IN PLACE 2026-08-22 (QG), AND THE FALSE VERSION IS THIS ONE — IT IS THE SENTENCE DIRECTLY ABOVE.**
//     When S5 first landed, that claim was TRUE of rules 1, 2 and 4 and **FALSE of rule 3**: there was no mutation
//     for the forbidden `float(peer) - float(own)` at all, and the case then offered as its proof — a walk of eight
//     50 m steps at 52 N — stayed **GREEN** under exactly that defect, because the quantisation there (~16 units) is
//     far below a 50 m step. ⇒ a rule stated as covered, measured by an instrument that could not fail.
//     ★ WHAT MADE IT TRUE: `test/test_firmware_ui_geo.cpp`'s HIGH-COORDINATE fixture (89 N latitude, 170 E
//     longitude — where `float`'s ULP is 64 and 128 e7 units against a metre's 89.83) renders `1m` on the correct
//     path and **`0m`** on the float-first one, and `--target=uigeo` G17/G18 are the two mutations it reddens.
//     ⓘ The lesson is the general one this file cluster keeps re-learning: **a fixture in the middle of the domain
//     cannot see a precision rule** — the boundary has to be reached deliberately.
//
//   1. ⛔⛔ **THE LONGITUDE DIFFERENCE MAY NOT BE COMPUTED IN `int32_t`.** `lon_e7` spans ±1 800 000 000, so a
//      difference reaches 3.6e9 and **OVERFLOWS** a 32-bit signed value — undefined behaviour whose visible symptom
//      is a neighbour on the far side of the planet. ⇒ both differences are taken in `int64_t`; the latitude
//      difference (±1.8e9) would fit, and is taken the same way so ONE rule covers both and no reader has to
//      remember which is which.
//   2. **ANTIMERIDIAN NORMALISATION.** `dlon_e7` is folded into ±1 800 000 000 BEFORE any conversion. Without it two
//      neighbours either side of ±180° read as half a planet apart — a `20000k`-shaped answer for a 200 m walk.
//   3. **DIFFERENCES ARE TAKEN AS INTEGERS FIRST, THEN CONVERTED TO FLOAT.** Converting the absolute e7 values first
//      loses the difference in `float`'s 24-bit mantissa (catastrophic cancellation) — and NEARBY peers are the only
//      case that matters, so the failure would land exactly where the feature is used.
//   4. **NO `atan2`.** The octant is decided by comparing `|dx|` and `|dy|` against `tan(22.5°)` — multiplies and
//      comparisons only. ⓘ `std::sqrt` and `std::cos` still pull float libm; spec §6 requires the per-env flash
//      delta to be MEASURED by the board gate, and names an integer cosine table as the fallback if it is not
//      acceptable. That is a measured decision taken THEN, ⛔ not a defensive one taken now.
//
// The equirectangular approximation itself (spec §3.4): `dy = dlat_e7 * 1.1131949e-2`, `dx = dlon_e7 * 1.1131949e-2 *
// cos(mid_lat)`, `d = sqrt(dx² + dy²)`. ⚠ It is accurate far beyond a FOUR-COLUMN token at hiking ranges; its error
// at continental distances is irrelevant to a `12k`-shaped display, which is the only consumer.
inline constexpr float   kGeoMetresPerE7 = 1.1131949e-2f;   // one 1e-7 degree of latitude, in metres
inline constexpr float   kGeoDegToRad    = 0.017453292f;
inline constexpr float   kGeoTan22_5     = 0.41421356f;     // the octant boundary — 22.5° either side of a cardinal
inline constexpr int64_t kGeoLonHalfE7   = 1800000000LL;    // 180°, in 1e-7 degrees
inline constexpr int64_t kGeoLonSpanE7   = 3600000000LL;    // 360°, ⛔ NOT representable in int32 — see rule 1

struct GeoVector {
    float     dist_m      = 0.0f;
    bool      has_bearing = false;          // ⛔ FALSE for a zero-length vector — the ruling, expressed as a state
    GeoOctant octant      = GeoOctant::n;   // ⛔ MEANINGLESS while `has_bearing` is false; ⛔ never read there
};

inline GeoVector ui_geo_solve(const GeoFix& own, int32_t peer_lat_e7, int32_t peer_lon_e7) {
    // Rule 1 + rule 3: the differences are INTEGER, and 64-bit, before anything becomes a float.
    // ⛔ THE TWO `float(...)` CASTS BELOW TAKE **THESE DIFFERENCES**, ⛔ NEVER THE ABSOLUTE COORDINATES: at 89 N one
    //    metre is 89.83 e7 units and `float`'s ULP is already 64 there, so rounding first turns a metre into `0m`.
    //    Pinned by the high-coordinate case in `test/test_firmware_ui_geo.cpp` and attacked by G17/G18.
    const int64_t dlat = int64_t(peer_lat_e7) - int64_t(own.lat_e7);
    int64_t       dlon = int64_t(peer_lon_e7) - int64_t(own.lon_e7);
    // Rule 2: the fold. ⓘ Exactly ±180° is left as it is — the two directions are equidistant and either answer is
    //   as true as the other; the fold exists for the 3.6e9-wide difference, not for that one point.
    if      (dlon >  kGeoLonHalfE7) dlon -= kGeoLonSpanE7;
    else if (dlon < -kGeoLonHalfE7) dlon += kGeoLonSpanE7;
    GeoVector v{};
    // ★★★★ THE COINCIDENT-POINT RULING, AND IT IS TESTED ON THE **INTEGER** DELTAS: `dist_m` stays 0 and
    //      `has_bearing` stays FALSE, so the caller draws `0m` and a BLANK direction. ⛔ It is NOT octant 0.
    //      ⓘ Reachable in practice — two nodes at one campsite, three decimals apart, both e7 deltas zero — which is
    //        why it is a state of its own rather than an unreachable guard.
    //      ⛔ AND IT IS DECIDED HERE, BEFORE THE FLOATS: a `dx == 0.0f && dy == 0.0f` test one line down would also
    //        fire when `cos(mid_lat)` underflows at a pole, and would then blank a bearing that genuinely exists.
    if (dlat == 0 && dlon == 0) return v;
    // ⓘ The MID-LATITUDE is an absolute value, not a difference, so converting it to float is sound (rule 3 is about
    //   differences). Integer division truncates toward zero; the cosine of a value half a metre out is unmeasurable
    //   in a four-column token.
    const float mid_lat_deg = float((int64_t(own.lat_e7) + int64_t(peer_lat_e7)) / 2) * 1e-7f;
    const float dy = float(dlat) * kGeoMetresPerE7;
    const float dx = float(dlon) * kGeoMetresPerE7 * std::cos(mid_lat_deg * kGeoDegToRad);
    v.dist_m      = std::sqrt(dx * dx + dy * dy);
    v.has_bearing = true;
    // Rule 4: the octant, by comparison alone. `dy` is NORTHWARD and `dx` EASTWARD, so the sign pair names the
    // quadrant and the two ratios name which of the three sectors inside it.
    const float ax = (dx < 0.0f) ? -dx : dx;
    const float ay = (dy < 0.0f) ? -dy : dy;
    if      (ax <= kGeoTan22_5 * ay) v.octant = (dy >= 0.0f) ? GeoOctant::n  : GeoOctant::s;
    else if (ay <= kGeoTan22_5 * ax) v.octant = (dx >= 0.0f) ? GeoOctant::e  : GeoOctant::w;
    else if (dy >= 0.0f)             v.octant = (dx >= 0.0f) ? GeoOctant::ne : GeoOctant::nw;
    else                             v.octant = (dx >= 0.0f) ? GeoOctant::se : GeoOctant::sw;
    return v;
}

// ========================================================================= THE FOUR-TERM RULE, IN ONE PLACE (§3.4)
// ★★★★ **SHOW THE COLUMNS ONLY WHEN ALL FOUR HOLD**, and THREE of them are here:
//        (1) our own position is configured — `own.have`, the published answer of the ONE predicate;
//        (3) the cache holds a position for that peer — `peer_valid`, `peer_loc_find`'s own return;
//        (4) that position is within `kPeerLocMaxAgeS`.
//      ⓘ TERM (2) — *"the team-local id resolves to a NON-ZERO peer hash"* — is answered at the PUBLISH SITE
//        (`build_snapshot`), because it is the step `label_for_team_id` already takes and the spec requires ONE
//        resolution per row handed to both (U1). An unresolvable id therefore arrives here as
//        `peer_valid == false`, which is the same blank through the same arm. ⛔ It is not silently dropped: the
//        publish site states it, and the probe drives an unresolvable row through the real renderer.
// ⛔ ANY term failing ⇒ BOTH columns blank. ⛔ Never a partial row (a distance with no direction is a different
//    state — see the coincident ruling — and a direction with no distance is meaningless).
struct GeoAnswer {
    bool      shown = false;   // ⛔ false ⇒ BOTH columns are blank; `v` is untouched and may not be read
    GeoVector v{};
};

inline GeoAnswer ui_geo_answer(const GeoFix& own, bool peer_valid, uint32_t peer_age_s,
                               int32_t peer_lat_e7, int32_t peer_lon_e7) {
    GeoAnswer a{};
    if (!own.have)                  return a;   // (1) we do not know where WE are
    if (!peer_valid)                return a;   // (2)+(3) no hash for that id, or nothing cached under it
    if (!ui_geo_fresh(peer_age_s))  return a;   // (4) too old to be a fact — ⛔ never rendered as a current one
    a.v     = ui_geo_solve(own, peer_lat_e7, peer_lon_e7);
    a.shown = true;
    return a;
}

// ===================================================================================== THE TWO TOKENS (S-13, S-14)
// ★★ THE COLUMN WIDTHS ARE THE ROW's (`%4s` and `%2s`, spec §3.2), and every arm below is proven against them by a
//    native case rather than by this comment.
inline constexpr std::size_t kGeoDistCap = 5;   // `999m` / `9.9k` / `999k` / `far` + NUL — widest is FOUR columns
inline constexpr std::size_t kGeoDirCap  = 3;   // `NW` + NUL

// ★★★ THE DISTANCE TABLE, RULED BY THE SPEC (S-13): exact metres below 1 000; ONE DECIMAL below 10 km; whole
//     kilometres to 999; then the saturation token `far`.
// ⛔ IT TRUNCATES, IT DOES NOT ROUND — the same rule STATUS row 4 applies to the coordinate (spec §2.2 note i): a
//    panel may never render a position further along, or more precise, than the evidence it holds.
// ⚠⚠ THE `far` ARM IS WRITTEN AS `!(d < 1e6f)` AND THAT IS DELIBERATE, NOT AN AWKWARD SPELLING: it is the only form
//    that also catches a NaN (every comparison against a NaN is false), and it is what makes the `uint32_t` cast
//    below TOTAL — converting a float outside the destination's range is undefined behaviour, and "undefined" on a
//    safety panel means a plausible-looking number nobody can account for.
inline void ui_geo_dist_token(char* out, std::size_t cap, float dist_m) {
    int n;
    if (!(dist_m < 1000000.0f)) {
        n = snprintf(out, cap, "far");
    } else {
        const uint32_t m = (dist_m > 0.0f) ? uint32_t(dist_m) : 0u;   // ⓘ negative is unreachable from `sqrt`; total anyway
        if      (m < 1000u)  n = snprintf(out, cap, "%lum", (unsigned long)m);
        else if (m < 10000u) n = snprintf(out, cap, "%lu.%luk", (unsigned long)(m / 1000u),
                                                                (unsigned long)((m % 1000u) / 100u));
        else                 n = snprintf(out, cap, "%luk", (unsigned long)(m / 1000u));
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);   // the neighbours' rule: the WHOLE buffer is defined
}

// ★ THE TWO COLUMNS AS THE ROW WILL DRAW THEM. Both buffers are FULLY DEFINED on every arm (`ui_pad_token`), so a
//   byte-for-byte comparison of two `GeoCols` is sound and a blank column is an EMPTY STRING rather than whatever
//   was on the stack.
struct GeoCols {
    char dist[kGeoDistCap] = {};
    char dir[kGeoDirCap]   = {};
};

inline void ui_geo_columns(GeoCols& out, const GeoFix& own, bool peer_valid, uint32_t peer_age_s,
                           int32_t peer_lat_e7, int32_t peer_lon_e7) {
    ui_pad_token(out.dist, sizeof out.dist, 0);   // ⛔ BLANK IS THE DEFAULT, and every refusal below simply returns
    ui_pad_token(out.dir,  sizeof out.dir,  0);
    const GeoAnswer a = ui_geo_answer(own, peer_valid, peer_age_s, peer_lat_e7, peer_lon_e7);
    if (!a.shown) return;
    ui_geo_dist_token(out.dist, sizeof out.dist, a.v.dist_m);
    // ★★★ AND THE DIRECTION IS WRITTEN **ONLY** WHEN THE VECTOR HAS ONE. A coincident peer leaves this column blank
    //     beside a perfectly valid `0m` — the ruled shape, and the one thing a "tidier" implementation gets wrong.
    if (!a.v.has_bearing) return;
    const int n = snprintf(out.dir, sizeof out.dir, "%s", ui_geo_dir_lexeme(a.v.octant));
    ui_pad_token(out.dir, sizeof out.dir, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ================================================================== THE REPAINT BUCKET (spec §1.9 F-8, S4's rule)
// ★★★★ THE SAME RULE THE AGE COLUMN ALREADY OBEYS ONE FILE OVER (`ui_team_age_bucket`), and it is why this exists at
//      all: a lit TEAM screen must repaint when a DRAWN TOKEN turns and ⛔ NEVER because a raw input moved.
//      A peer walking three metres, or our own fix twitching, changes `dist_m` on every tick and changes the drawn
//      `1.2k` not at all — comparing the raw coordinates (or the raw location age) would repaint a panel that did
//      not change, for ever. ⇒ this returns a COMPARISON VALUE that is equal exactly when the two columns would draw
//      the same bytes.
// ⛔ IT IS NEVER RENDERED, NEVER STORED AND NEVER PUBLISHED — nothing may start reading it as a distance.
// ⚠ IT IS A SECOND EXPRESSION OF THE TOKEN'S BOUNDARIES, WHICH IS A DRIFT RISK, AND THE ANSWER IS MEASUREMENT:
//   `test/test_firmware_ui_geo.cpp` sweeps distances across every boundary and asserts `bucket(a) == bucket(b)` **if
//   and only if** the two rendered columns are byte-identical. A table moved on either side fails that sweep.
// ⓘ LAYOUT: bits 0-3 are the DIRECTION (0 = blank, otherwise the octant + 1) and bits 4+ the distance token's own
//   key — a unit tag plus the count it prints. ⛔ **ZERO MEANS BOTH COLUMNS ARE BLANK** and no shown row can produce
//   it: every distance arm sets a non-zero tag, which a case pins.
inline uint32_t ui_geo_dist_key(float dist_m) {
    if (!(dist_m < 1000000.0f)) return 0x04000000u;                  // `far` — every distance past the cap is one token
    const uint32_t m = (dist_m > 0.0f) ? uint32_t(dist_m) : 0u;
    if (m < 1000u)  return 0x01000000u | m;                          // `%um`    — the token turns every metre
    if (m < 10000u) return 0x02000000u | (m / 100u);                 // `%u.%uk` — ...every hundred metres
    return 0x03000000u | (m / 1000u);                                // `%uk`    — ...every kilometre
}

inline uint32_t ui_geo_bucket(const GeoFix& own, bool peer_valid, uint32_t peer_age_s,
                              int32_t peer_lat_e7, int32_t peer_lon_e7) {
    const GeoAnswer a = ui_geo_answer(own, peer_valid, peer_age_s, peer_lat_e7, peer_lon_e7);
    if (!a.shown) return 0;
    const uint32_t dir = a.v.has_bearing ? uint32_t(uint8_t(a.v.octant)) + 1u : 0u;
    return (ui_geo_dist_key(a.v.dist_m) << 4) | dir;
}

}  // namespace mrui
