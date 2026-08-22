// MeshRoute — test_firmware_ui_geo.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-17 slice S5 — the native suite for the location unit (`src/firmware_ui_geo.h`): spec §3.4's FOUR-TERM rule,
// the 600 s freshness bound, the four load-bearing maths rules (int64 longitude · the antimeridian fold ·
// integer-differences-first · the atan2-free octant), the coincident-point RULING, and the two ruled token tables
// (string inventory S-13 / S-14).
//
// ★★★ WHY IT EXISTS AS ITS OWN SUITE — the same two reasons `test_firmware_ui_team.cpp` gives one file over:
//     `src/firmware_ui.cpp` is compiled by NEITHER the native suite NOR the simulator (§B115), so a decision made
//     there is a decision no gate can read; and a mutation battery is per-SOURCE-FILE, so its own file is what gives
//     `--target=uigeo` an isolated control for each term of the rule, each table boundary and each maths rule.
//
// ★★ EVERY CASE IS WRITTEN TO BE ABLE TO COME OUT OTHERWISE. The two columns are asserted as their EXACT BYTES —
//    including the empty string, which is the whole point of a blank: a `strlen(dist) < 5` style check would pass
//    against `0m` for a peer nobody has heard from in a week, which is the one substitution this screen exists to
//    refuse.
#include "doctest.h"
#include "firmware_ui_geo.h"
#include <cstdint>
#include <cstring>

using namespace mrui;

namespace {

// ★ THE FIXTURE'S OWN COORDINATES, and they are DELIBERATELY NOT `(0,0)`: that pair is the core's "no fix at all"
//   (`ui_status_have_fix`), so a suite built on it would be driving every case from a position the device would have
//   refused to publish. 52.0000000 N, 21.0000000 E — Warsaw-ish, a real mid latitude where `cos` is neither 1 nor 0.
constexpr int32_t kOwnLat = 520000000;
constexpr int32_t kOwnLon = 210000000;
const GeoFix kOwn{ /*have=*/true, kOwnLat, kOwnLon };

// One 1e-7 degree of latitude is 1.1131949e-2 m, so a metre is ~89.83 units. ⓘ Every offset below is chosen CLEAR of
// its token's boundary, so a change in the last float bit cannot move an expected string.
constexpr int32_t kUnitsPerKm = 89832;   // 1 000 m of latitude, near enough for a fixture

// The columns for one peer, at a stated age, from our own fix.
GeoCols cols_at(int32_t lat_e7, int32_t lon_e7, uint32_t age_s, bool valid = true, const GeoFix& own = kOwn) {
    GeoCols c;
    ui_geo_columns(c, own, valid, age_s, lat_e7, lon_e7);
    return c;
}

bool blank(const GeoCols& c) { return c.dist[0] == '\0' && c.dir[0] == '\0'; }

}  // namespace

// ============================================================ THE FOUR-TERM RULE AND THE FRESHNESS BOUND — §3.4

TEST_CASE("ui17-geo: the freshness bound is 600 s — 599 and 600 SHOW, 601 BLANKS") {
    // ★★★★ THE OWNER-APPROVED TEN MINUTES, PINNED AT ITS EDGE. `<=` is a DECISION: a `<` here would blank a position
    //      that is still inside the bound, and a wider bound would draw a position that is outside it.
    const int32_t north = kOwnLat + kUnitsPerKm;      // ~1 km north — a token far from any boundary
    CHECK(std::strcmp(cols_at(north, kOwnLon, 0).dist,   "1.0k") == 0);
    CHECK(std::strcmp(cols_at(north, kOwnLon, 599).dist, "1.0k") == 0);
    CHECK(std::strcmp(cols_at(north, kOwnLon, 600).dist, "1.0k") == 0);   // ⛔ the bound INCLUDES its own value
    CHECK(blank(cols_at(north, kOwnLon, 601)) == true);
    // ...and the constant is the one the row is budgeted against, not a second literal.
    CHECK(kPeerLocMaxAgeS == 600u);
    CHECK(ui_geo_fresh(600u) == true);
    CHECK(ui_geo_fresh(601u) == false);
}

TEST_CASE("ui17-geo: `0xFFFFFFFF` is the cache's UNDATEABLE, and it blanks — ⛔ never a fresh-looking position") {
    // ⚠ `peer_loc_find` returns `0xFFFFFFFF` when the clock has moved BACKWARDS (node_hashlocate.cpp:441) — it fails
    //   in the direction the app discards. It is past the bound by construction; this pins that the two agree, so
    //   the agreement can never become accidental (a re-derivation of the age at the publish site would break it).
    const int32_t north = kOwnLat + kUnitsPerKm;
    CHECK(blank(cols_at(north, kOwnLon, 0xFFFFFFFFu)) == true);
    CHECK(ui_geo_fresh(0xFFFFFFFFu) == false);
}

TEST_CASE("ui17-geo: EVERY term of the four-term rule blanks BOTH columns on its own") {
    const int32_t north = kOwnLat + kUnitsPerKm;
    // (1) we do not know where WE are. ⛔ The coordinates are still carried — `(0,0)` with `have == false` is "no
    //     position", never the Gulf of Guinea — so this is the FLAG's rule and not the coordinates'.
    CHECK(blank(cols_at(north, kOwnLon, 30, true, GeoFix{ false, kOwnLat, kOwnLon })) == true);
    // (2)+(3) the id resolved to no peer hash, or the cache holds nothing under it. Both arrive as `valid == false`.
    CHECK(blank(cols_at(north, kOwnLon, 30, /*valid=*/false)) == true);
    // (4) the position is too old to be a fact.
    CHECK(blank(cols_at(north, kOwnLon, 5000)) == true);
    // ...and with all four satisfied the columns fill. ⛔ Without this line every check above is negative space.
    const GeoCols shown = cols_at(north, kOwnLon, 30);
    CHECK(std::strcmp(shown.dist, "1.0k") == 0);
    CHECK(std::strcmp(shown.dir,  "N")    == 0);
}

TEST_CASE("ui17-geo: a cache MISS is BLANK — ⛔ never `0m`, which is a real and different answer") {
    // ★★★ THE SUBSTITUTION THIS SCREEN EXISTS TO REFUSE. A default-constructed `TeamRow` carries `(0,0)` and
    //     `peer_loc_valid == false`; treating that as a position would draw `far`/`0m`-shaped nonsense for every
    //     teammate nobody has ever heard a location from. ⓘ And `0m` IS reachable — see the coincident case — which
    //     is exactly why "blank" and "zero" must not share a rendering.
    CHECK(blank(cols_at(0, 0, 0, /*valid=*/false)) == true);
    const GeoCols coincident = cols_at(kOwnLat, kOwnLon, 0, /*valid=*/true);
    CHECK(std::strcmp(coincident.dist, "0m") == 0);
    CHECK(blank(coincident) == false);
}

// ================================================================== THE COINCIDENT-POINT RULING — spec §3.4 / S5

TEST_CASE("ui17-geo: coincident points render `0m` and a BLANK direction — ⛔ NEVER `N`") {
    // ★★★★ OWNER-RULED 2026-08-20, and the withdrawn wording is kept visible in the spec: the first draft asked for
    //      *"`0m` with a defined bearing token"*. A ZERO-LENGTH VECTOR HAS NO BEARING, so any token there would be
    //      FABRICATED — the exact class this panel exists to avoid. ⇒ a valid distance and `has_bearing = false`.
    //      ⓘ Reachable in practice: two nodes at one campsite, three decimals apart, both e7 deltas zero.
    const GeoVector v = ui_geo_solve(kOwn, kOwnLat, kOwnLon);
    CHECK(v.dist_m == 0.0f);
    CHECK(v.has_bearing == false);
    const GeoCols c = cols_at(kOwnLat, kOwnLon, 30);
    CHECK(std::strcmp(c.dist, "0m") == 0);
    CHECK(std::strcmp(c.dir,  "")   == 0);
    // ⛔ AND IT IS NOT OCTANT 0 WEARING A BLANK: the direction column must differ from what `N` would have drawn.
    CHECK(std::strcmp(c.dir, ui_geo_dir_lexeme(GeoOctant::n)) != 0);
    // ⛔ ...nor any other cardinal.
    bool any_cardinal = false;
    const GeoOctant all[] = { GeoOctant::n, GeoOctant::ne, GeoOctant::e,  GeoOctant::se,
                              GeoOctant::s, GeoOctant::sw, GeoOctant::w,  GeoOctant::nw };
    for (const GeoOctant o : all) if (std::strcmp(c.dir, ui_geo_dir_lexeme(o)) == 0) any_cardinal = true;
    CHECK(any_cardinal == false);
    // ⓘ AND THE BUCKET SAYS SO TOO: a coincident row is SHOWN (non-zero) and its direction half is the blank code,
    //   so a panel showing `0m` and one showing nothing at all can never compare equal.
    CHECK(ui_geo_bucket(kOwn, true, 30, kOwnLat, kOwnLon) != 0u);
    CHECK(ui_geo_bucket(kOwn, false, 30, kOwnLat, kOwnLon) == 0u);
}

// ============================================================================== THE EIGHT BEARINGS — S-14 / §3.4

TEST_CASE("ui17-geo: all EIGHT bearings are driven directly, and each draws its own lexeme") {
    // ★ The offsets are symmetric in e7 units around our own fix. ⓘ `cos(mid_lat)` shortens the EAST-WEST leg at 52°
    //   (0.615), so a diagonal is not exactly 45° — it is still comfortably inside its octant, which is the point of
    //   an eight-way display rather than a compass.
    const int32_t d = kUnitsPerKm * 4;                 // ~4 km — clear of every table boundary
    struct K { int32_t dlat, dlon; const char* want; };
    const K k[] = {
        { +d,  0,  "N"  }, { +d, +d, "NE" }, {  0, +d, "E"  }, { -d, +d, "SE" },
        { -d,  0,  "S"  }, { -d, -d, "SW" }, {  0, -d, "W"  }, { +d, -d, "NW" },
    };
    bool all_ok = true, bounded = true;
    for (const K& c : k) {
        const GeoCols got = cols_at(kOwnLat + c.dlat, kOwnLon + c.dlon, 60);
        if (std::strcmp(got.dir, c.want) != 0) all_ok = false;
        if (std::strlen(got.dir) > 2) bounded = false;
        if (got.dist[0] == '\0') all_ok = false;       // every one of them has a distance too
    }
    CHECK(all_ok  == true);
    CHECK(bounded == true);
    // ⛔ AND THE EIGHT LEXEMES ARE THE RULED ONES (S-14): cardinal TEXT, ⛔ never an arrow, and all eight distinct.
    const char* lex[8] = { ui_geo_dir_lexeme(GeoOctant::n),  ui_geo_dir_lexeme(GeoOctant::ne),
                           ui_geo_dir_lexeme(GeoOctant::e),  ui_geo_dir_lexeme(GeoOctant::se),
                           ui_geo_dir_lexeme(GeoOctant::s),  ui_geo_dir_lexeme(GeoOctant::sw),
                           ui_geo_dir_lexeme(GeoOctant::w),  ui_geo_dir_lexeme(GeoOctant::nw) };
    const char* want[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    bool lex_ok = true;
    for (int i = 0; i < 8; ++i) if (std::strcmp(lex[i], want[i]) != 0) lex_ok = false;
    CHECK(lex_ok == true);
}

TEST_CASE("ui17-geo: the octant BOUNDARY is 22.5 degrees either side of a cardinal, and it is driven at the edge") {
    // ★★ WHAT THIS EXCLUDES: thresholds swapped (`|dx|` against `|dy|` the wrong way round) or a 45-degree split,
    //    both of which leave the four cardinals looking perfectly right and put every diagonal in the wrong sector.
    // ⓘ Driven on a NORTH-EAST-ish vector whose east leg is deliberately just inside, then just outside, the
    //   `tan(22.5°)` ratio. ⚠ Expressed in METRES first and converted back to e7, because `cos(52°)` shortens the
    //   longitude leg — a comment cannot do that arithmetic, so the fixture does.
    const float lat_units_per_m = 1.0f / kGeoMetresPerE7;
    const float cos_mid         = 0.6157f;                       // cos(52°), near enough for a fixture
    const int32_t north_m = 1000;
    // just INSIDE the N sector: east leg = 0.30 * north (< tan 22.5 = 0.414)
    const int32_t e_in  = int32_t(300.0f  * lat_units_per_m / cos_mid);
    // ...and just OUTSIDE it: east leg = 0.60 * north (> tan 22.5), which is NE
    const int32_t e_out = int32_t(600.0f  * lat_units_per_m / cos_mid);
    const int32_t n_e7  = int32_t(float(north_m) * lat_units_per_m);
    CHECK(std::strcmp(cols_at(kOwnLat + n_e7, kOwnLon + e_in,  60).dir, "N")  == 0);
    CHECK(std::strcmp(cols_at(kOwnLat + n_e7, kOwnLon + e_out, 60).dir, "NE") == 0);
    // ...and the mirror on the other axis: a mostly-EAST vector with a small north leg is `E`, not `NE`.
    CHECK(std::strcmp(cols_at(kOwnLat + int32_t(300.0f * lat_units_per_m),
                              kOwnLon + int32_t(1000.0f * lat_units_per_m / cos_mid), 60).dir, "E") == 0);
}

// ================================================================= NEGATIVE COORDINATES, ALL FOUR QUADRANTS — §3.4

TEST_CASE("ui17-geo: negative coordinates work in all four quadrants (the sign is never assumed)") {
    // ★ Four fixtures, one per quadrant, each with a peer ~2 km NORTH-EAST of it. A sign dropped anywhere in the
    //   difference, the fold or the octant shows up as a wrong bearing here rather than as a plausible number.
    const int32_t d = kUnitsPerKm * 2;
    struct Q { int32_t lat, lon; };
    const Q q[] = {
        {  520000000,  210000000 },   // NE quadrant — Europe
        {  400000000, -740000000 },   // NW — New York
        { -338600000,  1512000000 },  // SE — Sydney
        { -230000000, -434000000 },   // SW — Rio
    };
    bool ne_ok = true, sw_ok = true;
    for (const Q& o : q) {
        const GeoFix own{ true, o.lat, o.lon };
        GeoCols c;
        ui_geo_columns(c, own, true, 60, o.lat + d, o.lon + d);
        if (std::strcmp(c.dir, "NE") != 0) ne_ok = false;
        ui_geo_columns(c, own, true, 60, o.lat - d, o.lon - d);
        if (std::strcmp(c.dir, "SW") != 0) sw_ok = false;
    }
    CHECK(ne_ok == true);
    CHECK(sw_ok == true);
}

// ==================================================== THE ANTIMERIDIAN AND THE 64-BIT DIFFERENCE — §3.4 rules 1+2

TEST_CASE("ui17-geo: a pair straddling ±180° is NEIGHBOURS, not half a planet apart") {
    // ★★★★ TWO OF THE FOUR MATHS RULES IN ONE FIXTURE, and both fail SILENTLY without it:
    //      · rule 1 — `lon_e7` spans ±1 800 000 000, so this difference (-3 598 000 000) **OVERFLOWS** `int32_t`.
    //        Taken in 32 bits it wraps to +696 967 296, i.e. +69.7°, and the panel reads `far` for a 22 km hop;
    //      · rule 2 — without the FOLD the 64-bit difference is -3 598 000 000, i.e. -359.8°, and the panel reads
    //        `far` again, this time pointing WEST.
    //      ⇒ the expected token below is reachable ONLY when both rules hold.
    const GeoFix own{ true, 0, 1799000000 };            // 179.9° E, on the equator (cos = 1, so the arithmetic is plain)
    const GeoCols c = cols_at(0, -1799000000, 60, true, own);   // 179.9° W — 0.2° away, ~22 km
    CHECK(std::strcmp(c.dist, "22k") == 0);
    CHECK(std::strcmp(c.dir,  "E")   == 0);
    // ...and the same pair read the other way round is the same distance, WEST.
    const GeoFix own_w{ true, 0, -1799000000 };
    const GeoCols back = cols_at(0, 1799000000, 60, true, own_w);
    CHECK(std::strcmp(back.dist, "22k") == 0);
    CHECK(std::strcmp(back.dir,  "W")   == 0);
}

TEST_CASE("ui17-geo: an ORDINARY longitude pair is untouched by the fold") {
    // ⛔ THE INVERSION, and without it a fold that ALWAYS fired would pass the case above: two peers 0.2° apart in
    //    the middle of the map must NOT be normalised into a 359.8° separation.
    const GeoFix own{ true, 0, 210000000 };
    const GeoCols c = cols_at(0, 212000000, 60, true, own);     // +0.2°, ~22 km east
    CHECK(std::strcmp(c.dist, "22k") == 0);
    CHECK(std::strcmp(c.dir,  "E")   == 0);
}

TEST_CASE("ui17-geo: ★ INTEGER-FIRST is what makes ONE METRE visible at a HIGH coordinate") {
    // ★★★★ RULE 3, AT THE MANTISSA-LOSS BOUNDARY, AND THIS IS THE CASE THAT ACTUALLY MEASURES IT (QG, 2026-08-22).
    //      ⛔ **WITHDRAWN CLAIM, KEPT VISIBLE:** the walk below was offered as the proof of this rule and it is NOT —
    //      it stays GREEN under the forbidden `float(peer) - float(own)`, because at 52 N a 50 m step is ~4 492 e7
    //      units, far above the ~16-unit quantisation there, so the wrong answers remain distinct and non-zero.
    //      A case that cannot come out otherwise proves nothing; the walk is kept as a SUPPORTING sweep and the two
    //      fixtures below are the instrument.
    // ★★★ HOW THE BOUNDARY IS MADE OBSERVABLE, and both figures are MEASURED rather than reasoned about:
    //      · `float` carries a 24-bit mantissa, so near a LATITUDE of 8.9e8 its ULP is **64** e7 units and near a
    //        LONGITUDE of 1.7e9 it is **128** — while ONE METRE is only ~**89.83** units.
    //      · ⇒ a peer exactly 90 units away (1.0019 m, the token `1m`) collapses when the two ABSOLUTE values are
    //        rounded first: the latitude pair lands 64 units apart (0.7124 m ⇒ **`0m`**) and the longitude pair lands
    //        in the SAME float bucket, i.e. 0 units apart (0.0000 m ⇒ **`0m`**, and the bearing turns from `E` to a
    //        fabricated `N`).
    //      ⇒ correct: `1m`. Forbidden: `0m`. The panel would tell a searcher their teammate is AT THEM.
    {
        // (i) the LATITUDE leg, at 89.0000000 N — `own` exactly representable, the peer 90 units north.
        const GeoFix own_hi{ true, 890000000, 210000000 };
        GeoCols c;
        ui_geo_columns(c, own_hi, true, 60, 890000090, 210000000);
        CHECK(std::strcmp(c.dist, "1m") == 0);
        CHECK(std::strcmp(c.dir,  "N")  == 0);
        // (ii) the LONGITUDE leg, at 170.0 E on the equator — both values fall inside ONE float bucket.
        const GeoFix own_lon{ true, 0, 1699999950 };
        ui_geo_columns(c, own_lon, true, 60, 0, 1700000040);
        CHECK(std::strcmp(c.dist, "1m") == 0);
        CHECK(std::strcmp(c.dir,  "E")  == 0);
        // ⛔ VACUITY GUARD, AND IT IS THE MEASUREMENT THAT EXPLAINS THE WITHDRAWN CLAIM ABOVE: the SAME 90-unit
        //    offset at the suite's ordinary 52 N fixture renders `1m` on BOTH paths (float-first gives 1.0687 m
        //    there), so only a HIGH coordinate can separate them. A fixture in the middle of the map is exactly
        //    the instrument that cannot fail.
        ui_geo_columns(c, kOwn, true, 60, kOwnLat + 90, kOwnLon);
        CHECK(std::strcmp(c.dist, "1m") == 0);
    }
}

TEST_CASE("ui17-geo: ...and a 50 m walk keeps moving the token (the supporting sweep)") {
    // ⓘ KEPT, AND ITS SCOPE IS NOW STATED HONESTLY (QG, 2026-08-22): this sweep shows the metres arm advances one
    //   token per step and never collapses to `0m` — it does NOT prove the integer-first rule, which is the case
    //   directly above. ⛔ It is not deleted: a rule can be broken in more than one way, and "every step moves the
    //   token" is a property the table itself owes.
    // ⓘ Driven as a WALK: eight consecutive 50 m steps north must each move the rendered token, and none may repeat.
    bool all_distinct = true, all_nonzero = true;
    const int32_t step = int32_t(50.0f / kGeoMetresPerE7);      // ~4 492 units
    char prev[kGeoDistCap] = {};
    for (int i = 1; i <= 8; ++i) {
        const GeoCols c = cols_at(kOwnLat + step * i, kOwnLon, 60);
        if (std::strcmp(c.dist, prev) == 0) all_distinct = false;
        if (std::strcmp(c.dist, "0m") == 0) all_nonzero = false;
        std::memcpy(prev, c.dist, sizeof prev);
    }
    CHECK(all_distinct == true);
    CHECK(all_nonzero  == true);
}

// ================================================================================= THE DISTANCE TABLE — S-13

TEST_CASE("ui17-geo: the ruled distance table, boundary by boundary, and NEVER wider than four columns") {
    // ★ Driven on the TOKEN directly, in metres, so each boundary is exact rather than approached through a
    //   coordinate pair. S-13: exact metres below 1 000; one decimal below 10 km; whole km to 999; then `far`.
    struct K { float m; const char* want; };
    const K k[] = {
        {       0.0f, "0m"   }, {       1.0f, "1m"   }, {     850.0f, "850m" }, {     999.0f, "999m" },
        {    1000.0f, "1.0k" }, {    1250.0f, "1.2k" }, {    9999.0f, "9.9k" },
        {   10000.0f, "10k"  }, {   12000.0f, "12k"  }, {  999999.0f, "999k" },
        { 1000000.0f, "far"  }, { 40000000.0f, "far" },
    };
    bool all_ok = true, bounded = true;
    char tok[kGeoDistCap];
    for (const K& c : k) {
        ui_geo_dist_token(tok, sizeof tok, c.m);
        if (std::strcmp(tok, c.want) != 0) all_ok = false;
        if (std::strlen(tok) > 4) bounded = false;
    }
    CHECK(all_ok  == true);
    CHECK(bounded == true);
    // ⛔ IT TRUNCATES, IT DOES NOT ROUND — STATUS row 4's rule, one screen over: a panel may never say a peer is
    //    further along than the evidence holds. 999.9 m is `999m`, ⛔ not `1.0k`.
    ui_geo_dist_token(tok, sizeof tok, 999.9f);
    CHECK(std::strcmp(tok, "999m") == 0);
    ui_geo_dist_token(tok, sizeof tok, 1999.0f);
    CHECK(std::strcmp(tok, "1.9k") == 0);
}

TEST_CASE("ui17-geo: the SATURATION token is `far`, and a NaN cannot become a number") {
    char tok[kGeoDistCap];
    // ⚠ THE `far` ARM IS SPELLED `!(d < 1e6f)` PRECISELY SO IT ALSO CATCHES A NaN — every comparison against a NaN
    //   is false, so the natural `d >= 1e6f` would fall through to a `uint32_t` cast whose result is UNDEFINED.
    //   "Undefined" on a safety panel means a plausible-looking number nobody can account for.
    const float nan_v = std::sqrt(-1.0f);
    ui_geo_dist_token(tok, sizeof tok, nan_v);
    CHECK(std::strcmp(tok, "far") == 0);
    // ...and the widest real distance on this planet is still four columns.
    ui_geo_dist_token(tok, sizeof tok, 20015000.0f);            // pole to pole, the long way
    CHECK(std::strcmp(tok, "far") == 0);
}

// ============================================================ THE REPAINT BUCKET — §1.9 F-8, the S4 rule extended

TEST_CASE("ui17-geo: the distance BUCKET agrees with the drawn token, MEASURED across every boundary") {
    // ★★★★ THE BUCKET IS A SECOND EXPRESSION OF THE TABLE'S BOUNDARIES — a real drift risk — so the agreement is
    //      SWEPT rather than asserted: for every consecutive pair of distances the keys are equal IF AND ONLY IF the
    //      tokens are byte-identical. A boundary moved on either side breaks it in one direction or the other.
    struct Sweep { uint32_t from, to; };
    const Sweep sweeps[] = {
        {       0u,   1200u },   // `0m` .. `1.2k`, every metre — the metres/decimal crossing
        {    9900u,  10200u },   // the decimal/kilometre crossing
        {  999800u, 1000200u },  // ...and the saturation crossing
    };
    bool iff_ok = true;
    int  turns  = 0;
    for (const Sweep& sw : sweeps) {
        char prev[kGeoDistCap] = {}, cur[kGeoDistCap] = {};
        uint32_t prev_k = 0;
        bool have_prev = false;
        for (uint32_t m = sw.from; m <= sw.to; ++m) {
            ui_geo_dist_token(cur, sizeof cur, float(m));
            const uint32_t k = ui_geo_dist_key(float(m));
            if (have_prev) {
                const bool same_tok = (std::strcmp(cur, prev) == 0);
                const bool same_k   = (k == prev_k);
                if (same_tok != same_k) iff_ok = false;
                if (!same_tok) ++turns;
            }
            std::memcpy(prev, cur, sizeof prev);
            prev_k = k; have_prev = true;
        }
    }
    CHECK(iff_ok == true);
    // ⛔ VACUITY GUARD (§T3 P6's rule): a sweep whose token never turned would satisfy the `iff` trivially. The first
    //    sweep alone turns 1 000 times in its metres arm, so a three-figure floor is honest.
    CHECK(turns > 900);
    // ...and no distance arm can ever produce the BLANK bucket, which is what keeps "shown" and "blank" apart.
    bool never_zero = true;
    const float probe[] = { 0.0f, 1.0f, 999.0f, 1000.0f, 9999.0f, 10000.0f, 999999.0f, 1000000.0f };
    for (const float m : probe) if (ui_geo_dist_key(m) == 0u) never_zero = false;
    CHECK(never_zero == true);
}

TEST_CASE("ui17-geo: the BUCKET is what the panel draws — ⛔ never the raw coordinates or the raw age") {
    // ★★ §8.2's argument, applied to this column: two positions that draw the SAME four characters are the same
    //    panel, and repainting for the difference between them costs a lit screen its 2 Hz ceiling for ever.
    const int32_t km2 = kOwnLat + kUnitsPerKm * 2;
    const uint32_t a = ui_geo_bucket(kOwn, true, 30, km2, kOwnLon);
    // a peer that drifted three metres — same `2.0k`, same `N`
    CHECK(ui_geo_bucket(kOwn, true, 31, km2 + 270, kOwnLon) == a);      // ⛔ and the AGE moved too, deliberately
    // ...but a move that crosses the token's boundary is a different panel and must compare different.
    CHECK(ui_geo_bucket(kOwn, true, 30, km2 + kUnitsPerKm / 5, kOwnLon) != a);
    // ...and so is a change of DIRECTION at the same distance.
    CHECK(ui_geo_bucket(kOwn, true, 30, kOwnLat - kUnitsPerKm * 2, kOwnLon) != a);
    // ...and so is OUR OWN fix moving, which changes every row on the screen.
    CHECK(ui_geo_bucket(GeoFix{ true, kOwnLat + kUnitsPerKm, kOwnLon }, true, 30, km2, kOwnLon) != a);
    // ⛔ AND CROSSING THE FRESHNESS BOUND IS A REPAINT: the columns go blank, so the bucket must go to 0.
    CHECK(ui_geo_bucket(kOwn, true, 600, km2, kOwnLon) == a);
    CHECK(ui_geo_bucket(kOwn, true, 601, km2, kOwnLon) == 0u);
    // ...as is losing our own fix, or the cache entry.
    CHECK(ui_geo_bucket(GeoFix{ false, kOwnLat, kOwnLon }, true, 30, km2, kOwnLon) == 0u);
    CHECK(ui_geo_bucket(kOwn, false, 30, km2, kOwnLon) == 0u);
}

TEST_CASE("ui17-geo: the bucket and the COLUMNS answer the same question, swept together") {
    // ★★★ THE TWO ARE USED FOR DIFFERENT THINGS — one decides a repaint, the other draws the row — so the ONE thing
    //     that must never drift is that they agree. ⓘ [[B226]]'s shape: a rule proven in isolation and a renderer
    //     that shows something else is not proven.
    bool agree = true;
    int  shown = 0, blanked = 0;
    for (int32_t step = -20; step <= 20; ++step) {
        for (uint32_t age : { 0u, 599u, 600u, 601u, 0xFFFFFFFFu }) {
            const int32_t lat = kOwnLat + step * (kUnitsPerKm / 4);
            GeoCols c;
            ui_geo_columns(c, kOwn, true, age, lat, kOwnLon);
            const uint32_t b = ui_geo_bucket(kOwn, true, age, lat, kOwnLon);
            if (blank(c) != (b == 0u)) agree = false;
            if (b == 0u) ++blanked; else ++shown;
        }
    }
    CHECK(agree == true);
    CHECK(shown   > 0);      // ⛔ vacuity: the sweep really did cover both answers
    CHECK(blanked > 0);
}
