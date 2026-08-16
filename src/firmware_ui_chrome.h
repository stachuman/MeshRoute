// MeshRoute — src/firmware_ui_chrome.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CHROME-1 — THE ONE FROZEN CHROME PROJECTION, its compact formatters and the ONE navigation mapping. Design
// `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md` §4, §5.2, §5.3, §6 and §8.2.
//
// ★★★ WHAT THIS UNIT IS FOR. The OLED gains a glanceable status strip (§3.1) and a persistent navigation rail (§3.2).
//     Both are CHROME: they are redrawn on every frame, on every screen, and they must not be re-derived at each draw
//     site. §8.2 rules ONE pure projection that the renderer consumes and that ⛔ MUST NOT query `g_node`,
//     `ConfigService`, the counters or the battery while U8g2 is replaying later pages of a frame.
//
// ★★★ AND IT IS PURE, WHICH IS WHAT MAKES IT MEASURABLE. Every classification below — four home states into an icon,
//     an age into a token, three key states, four badge priorities, the whole modal navigation mapping — is a
//     decision the panel makes. In `src/firmware_ui.cpp` none of it would be reachable by any automated gate
//     (§B115's rule: that TU is compiled by neither the native suite nor the simulator). Here every arm is driven by
//     `test/test_firmware_ui_chrome.cpp` and mutated by `tools/probe_ui_model_mutations.py --target=chrome`
//     (the icon assets by `--target=icons`, and the snapshot's 64-bit age carrier by `--target=model`'s M51).
//
// ⓘ SCOPE — this is slice 1 of four (§10). It BUILDS the projection; it draws nothing. The canvas primitives are
//   slice 2, the strip renderer + repaint invalidation slice 3, the rail + the 19-column body migration slice 4.
//   ⛔ FIVE of the snapshot fields this projection reads (`mobile_build`, `home_link`, `home_confirmed_ever`,
//     `home_confirm_age_ms`, `team_key_present`) are DEFINED BUT NOT YET PUBLISHED — `build_snapshot` gains their
//     assignments in slice 3. Until then the projection reports the declared "nothing established" states, which are
//     honest, not placeholder.
//
// ⛔⛔ EQUALITY IS FIELD-BY-FIELD. `memcmp` over this struct is FORBIDDEN (§8.2) — it has padding, padding is
//     INDETERMINATE after aggregate initialisation, and a `memcmp` would therefore report differences that do not
//     exist. Since §8.3 marks the model dirty on a chrome difference, that is not a cosmetic bug: it is a repaint
//     every tick, on the one subsystem that took five review rounds to stabilise.

#pragma once

#include <cstddef>   // std::size_t
#include <cstdint>
#include <cstdio>    // snprintf — the compact tokens are formatted HERE so the native suite asserts VISIBLE BYTES
#include "firmware_ui_model.h"

namespace mrui {

// ============================================================================== §4 — the compact display domains
//
// ★★★ EVERY ONE OF THESE ENUMS NAMES A THIRD STATE THAT A BOOLEAN WOULD HAVE LOST, and that is the reason each is an
//     enum rather than a flag. §4.2's home slot is blank on a non-mobile build and `--` when nothing was ever
//     confirmed — two different silences. §4.4's key slot is blank with no team and CROSSED with a team but no
//     content key — "irrelevant" is not "missing". The binary-test-over-a-ternary-domain defect is one this arc has
//     already hit five times (see `Settings`' block in firmware_ui_model.h).

// §4.2's icon table, plus the `blank` state §4.2's last paragraph requires.
// ⛔ `blank` IS NOT `lost`: "on a non-mobile OLED build the home slot is blank, NOT crossed. 'Not applicable' must
//    not be rendered as a fault." A crossed house on `gateway_heltec` would be a claim about a plane it does not run.
enum class HomeIcon : uint8_t {
    blank = 0,   // no mobile-home plane on this build — draw NOTHING (and no age token either)
    unknown,     // never confirmed, or the home service is dormant
    confirmed,   // a recent correlated bidirectional exchange with the SELECTED home succeeded
    checking,    // a confirmation is due — an outstanding probe, NOT a failure
    lost         // the bounded run of presence misses failed
};

// §4.4's three-state key slot.
enum class KeyIcon : uint8_t {
    blank = 0,   // no team configured — the content key is IRRELEVANT, so nothing is drawn
    absent,      // team configured, no content key — crossed key
    present      // team configured, content key held — normal key
};

// §6's configuration badge. The enumerators are LISTED in the visible priority order as a reading aid — ⛔ but that
// is all it is: ★ WHAT ACTUALLY DECIDES THE PRIORITY IS `ui_cfg_badge`'s EXPLICIT `if` CHAIN AND THE EIGHT-ROW TRUTH
// TABLE THAT PINS IT (`test/test_firmware_ui_chrome.cpp`, mutations X15/X16). Reordering these enumerators changes
// NOTHING about the badge; reordering those `if`s changes everything. ⚠ Round 1's comment credited the enum order
// with preventing drift, which would have sent the next reader to the wrong place when they came to change it.
// ⓘ The badge is the COMPACT indicator only: §6 requires SETTINGS to keep rendering the actionable
// text (`UNSAVED`, `RELOAD`/conflict, `RESTART NEEDED`). ⛔ The icon may replace the STATUS decoration; it may never
// replace the instruction.
enum class CfgBadge : uint8_t { clean = 0, restart, unsaved, conflict };

// §3.2's five rail slots, plus `none`. ⓘ `none` is REACHED, not decorative: §5.3 suppresses the rail entirely during
// an emergency, and no slot may be selected then.
enum class NavSlot : uint8_t { none = 0, status, team, inbox, send, settings };

// The AVAILABLE-SLOT MASK's bit per slot (§8.2's amendment: "which of the five slots this build draws at all").
// ⛔ Derived from the enumerator, never a hand-written literal: a hand-written table is the positional coupling §B66
//   exists to warn about.
inline constexpr uint8_t slot_bit(NavSlot s) {
    return (s == NavSlot::none) ? uint8_t(0) : uint8_t(1u << (uint8_t(s) - 1u));
}

// ================================================================================ §4 — the compact formatters
//
// ★ ALL FOUR WRITE THE WHOLE BUFFER. Each pads with NULs to `cap`, so the bytes of a token are FULLY DEFINED and a
//   byte-for-byte comparison of two tokens is sound. `snprintf` alone leaves the tail of a shorter token untouched,
//   which would make `UiChrome`'s equality depend on whatever was there before.
inline void ui_pad_token(char* out, std::size_t cap, std::size_t used) {
    for (std::size_t i = used; i < cap; ++i) out[i] = '\0';
}

// §4.1 — the combined session-unread value.
// ⚠ THE SUM IS OF TWO ALREADY-CLAMPED VALUES (each <= `kUnreadCap` = 999, see `UiInboxCounters::publish`), so above
//   the cap the combined figure means "AT LEAST this many" and is never an exact total. That is why `99+` is honest
//   and why ⛔ nothing here or downstream may describe the number as a total.
// ⛔ AND IT IS NOT `inbox_total` (§4.1): a stored-total badge stays non-zero after reading and, on the Heltec's RAM
//    inbox, vanishes at reboot.
inline constexpr uint8_t kMailMax = 99;    // §4.1's table: 0..99 exact, 100 or more -> `99+`
inline void ui_fmt_mail(char* out, std::size_t cap, uint8_t value, bool overflow) {
    const int n = overflow ? snprintf(out, cap, "%u+", unsigned(kMailMax))
                           : snprintf(out, cap, "%u", unsigned(value));
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// §4.2 — the compact confirmation age. THE ONE PLACE THE 64-BIT AGE IS BUCKETED.
// ★★★★ `age_ms` IS `uint64_t` AND STAYS `uint64_t` ALL THE WAY INTO THE DIVISIONS. §4.2: "a cast to 32-bit
//      milliseconds is forbidden; it would reintroduce the already-fixed approximately 49.7-day wrap." A `uint32_t`
//      parameter here would turn a 100-day-old confirmation into `13h`, i.e. a stale link rendered as a live one, on
//      a safety device. ★ Pinned by a native case at `UINT32_MAX + 1` ms and another at 100 days + 1 ms, both of
//      which a 32-bit cast fails loudly.
// ⛔ AND THE NAME IS `age`, NEVER "connected": this is the age of the latest correlated bidirectional confirmation
//    with the SELECTED home. A team message, a foreign beacon or a one-way receive does not refresh it (§4.2).
inline constexpr std::size_t kAgeTokenCap = 4;   // widest token is 3 chars (`99d`, `59m`, `old`) + NUL
inline void ui_fmt_home_age(char* out, std::size_t cap, bool ever, uint64_t age_ms) {
    int n;
    if (!ever) {                                  // §4.2: never confirmed -> `--`, which is NOT `0s`
        n = snprintf(out, cap, "--");
    } else {
        const uint64_t s = age_ms / 1000u;
        const uint64_t m = s / 60u;
        const uint64_t h = m / 60u;
        const uint64_t d = h / 24u;
        if      (s <  60u) n = snprintf(out, cap, "%us", unsigned(s));
        else if (m <  60u) n = snprintf(out, cap, "%um", unsigned(m));
        else if (h <  24u) n = snprintf(out, cap, "%uh", unsigned(h));
        else if (d < 100u) n = snprintf(out, cap, "%ud", unsigned(d));
        else               n = snprintf(out, cap, "old");   // §4.2: 100 days or more
    }
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// §4.3 — teammates HEARD/KNOWN. ⛔ NOT "members online": this slice invents no online threshold, no membership
// database and no presence timeout. The value is the TRUE `team_total` (route rows), never the `team_shown` UI
// capacity — the old `T8/12` fraction is retired.
inline constexpr uint8_t kTeamMax = 9;     // §4.3's table: 0..9 exact, 10 or more -> `9+`
inline void ui_fmt_team(char* out, std::size_t cap, bool configured, uint8_t count, bool overflow) {
    int n;
    if (!configured)   n = snprintf(out, cap, "--");        // NO TEAM — which is not "a team with zero teammates"
    else if (overflow) n = snprintf(out, cap, "%u+", unsigned(kTeamMax));
    else               n = snprintf(out, cap, "%u", unsigned(count));
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// §4.5 — the battery voltage, one decimal, TRUNCATED, `V` included, `--` before any valid measurement.
// ⓘ Byte-for-byte the existing `fmt_volts` (src/firmware_ui.cpp:273) expressed over DECIVOLTS instead of millivolts:
//   `mv/1000` is `dv/10` and `(mv%1000)/100` is `dv%10`. ⚠ THE DUPLICATION IS DECLARED, NOT ACCIDENTAL (U1): this
//   slice may not touch the renderer, so ★ SLICE 3 MUST DELETE `fmt_volts` AND CALL THIS. A native case asserts the
//   exact bytes of both branches so the migration is a verified move rather than a rewrite.
// ⛔ NO PERCENTAGE, NO ESTIMATED TIME, NO LOW-BATTERY THRESHOLD (§4.5, §9) — all three would need a chemistry model
//    nobody has approved.
// ★★★★ AND THE UPPER BOUND IS **GEOMETRIC, NOT A PLAUSIBILITY JUDGEMENT** — the correction QG made to round 1.
//      §3.1 freezes the battery slot at **35 px**: the 11-px outline plus FOUR small-font columns (`4.1V` = 4 x 6 px
//      = 24 px), and the strip must still keep its "at least 15 px" spacing reserve (design §3.1's table). ⇒ the
//      widest token the slot can draw is `d.dV`, i.e. decivolts **0..99**. `10.0V` is five characters = 30 px, which
//      with the icon is 41 px and overruns the frozen slot — pushing every earlier icon out of budget.
// ⛔⛔ A VALUE TOO WIDE TO RENDER AS `d.dV` IS **UNAVAILABLE**, and unavailable renders `--`. ⛔ It is NOT clamped to
//     a plausible-looking `9.9V`: that is exactly the *"never substitute a plausible default voltage"* rule the
//     battery path already carries (`UiSnapshot::batt_mv < 0` is its existing expression), and `--` already has a
//     slot that fits. ⚠ Round 1 emitted `99.9V` and justified it as "nobody will mistake it for a cell" — a
//     PLAUSIBILITY argument against a GEOMETRIC defect.
// ⓘ TWO LAYERS, ONE AUTHORITY: `ui_chrome` CLASSIFIES (out-of-domain -> `batt_dv = -1`, see its §4.5 block); the
//   guard here FAILS CLOSED so no caller can make this formatter emit an over-wide token. Each is pinned by its own
//   case, so neither masks the other's mutation.
inline constexpr int16_t     kBattMaxDv    = 99;   // `9.9V` — the widest token §3.1's 35-px slot can draw
inline constexpr std::size_t kVoltsTokenCap = 5;   // `9.9V` + NUL — sized to the SLOT, not to the type's range
inline void ui_fmt_batt(char* out, std::size_t cap, int16_t decivolts) {
    const bool renderable = decivolts >= 0 && decivolts <= kBattMaxDv;
    const int n = !renderable ? snprintf(out, cap, "--")
                              : snprintf(out, cap, "%u.%uV", unsigned(decivolts / 10),
                                                             unsigned(decivolts % 10));
    ui_pad_token(out, cap, (n < 0) ? 0u : std::size_t(n) + 1u);
}

// ================================================================================ §5.2 — the ONE navigation mapping
//
// ★★★ ONE PURE FUNCTION, AND EVERY DISPATCH IN IT IS A `switch` WITH NO `default:`. That is the whole design of it:
//     `-Werror=switch` (platformio.ini:47, owner-ruled 2026-07-27) then turns a future screen, modal or compose kind
//     that nobody classified into a BUILD FAILURE. With a `default:` the same omission becomes a WRONG ICON — the
//     rail would confidently name a body it is not showing, which §5.2 exists to prevent. ⛔ Never add one, and
//     ⛔ never rewrite these as if-chains: -Wswitch cannot see those (node.h's `home_link_name` says so too).
//
// ★★ ORDER IS SIGNIFICANT AND IS THE §5.2 TABLE'S OWN: a body-replacing view wins over the screen underneath it,
//    because the rail must describe THE BODY ACTUALLY BEING SHOWN. The DM compose modal is opened from the TEAM
//    screen, so without this precedence the rail would say TEAM while the panel shows a send.
inline NavSlot ui_nav_slot(const UiState& st, Emergency emg) {
    // §5.3 — the emergency exception, and it is a BINARY predicate by the design's own wording ("when
    // `Emergency != idle`"), not a nine-arm classification: every non-idle state suppresses the rail for the same
    // reason (the 10x20 headline needs all 128 px, or `NO RELAY HRD` clips). A tenth emergency state needs no ruling.
    if (emg != Emergency::idle) return NavSlot::none;

    // Both compose kinds AND the send-result phase read as SEND (§5.2 row 2). `compose_result` needs no test of its
    // own: it is a PHASE of an open modal (`UiState::compose_result`'s block), so `compose != none` covers it.
    // ⛔⛔ COMPOSE IS TESTED **BEFORE** THE INBOX DETAIL, AND THE ORDER IS COPIED FROM THE RENDERER, NOT CHOSEN.
    //    `draw_frame` (`src/firmware_ui.cpp:949-953`) draws emergency -> compose -> inbox detail -> screen, each arm
    //    `return`ing. `compose != none` together with `detail != closed` are BOTH legal `UiState` values today, and
    //    with the two clauses the other way round the renderer draws COMPOSE while the rail says INBOX.
    // ★★ §5.2 IS THE TIE-BREAKER AND IT IS UNAMBIGUOUS: *"the rail must describe the body ACTUALLY BEING SHOWN"* ⇒
    //    THE RENDERER IS THE AUTHORITY HERE. ⛔ Never reorder these two to "read better"; a rail that names a body the
    //    panel is not showing is the one failure this mapping exists to prevent. Round 1 of this slice had them
    //    reversed AND had a test pinning the wrong answer, which is how it survived a full gate.
    switch (st.compose) {
        case Compose::dm:
        case Compose::channel: return NavSlot::send;
        case Compose::none:    break;
    }
    // Inbox list, Inbox DETAIL and `MESSAGE GONE` all read as INBOX (§5.2 row 1).
    // ⓘ STATED PLAINLY: for every state the MODEL can reach today this clause is REDUNDANT — the detail modal is only
    //   ever opened from `Screen::inbox` (`activate`), so the screen mapping below would answer `inbox` anyway. It is
    //   kept because §5.2 lists it as its own row and because it is what keeps that row TRUE if a detail modal ever
    //   becomes reachable from elsewhere. ⚠ Its mutation control therefore drives a legal-but-model-unreachable
    //   `UiState` (detail open on a non-inbox screen) and is labelled in the suite as a contract test for a
    //   DEFENSIVE clause — ⛔ not as a behaviour a user can produce today.
    switch (st.detail) {
        case InboxModal::body:
        case InboxModal::gone:   return NavSlot::inbox;
        case InboxModal::closed: break;
    }
    // The settings EDITOR (§5.2 row 3). ⓘ Redundant with the screen mapping below today — `Settings` is non-`closed`
    // only on `Screen::settings` (`sync_settings` closes it on leaving) — and listed anyway, because §5.2 lists it
    // and because the redundancy is what makes the row survive a future editor reachable from elsewhere.
    switch (st.settings) {
        case Settings::browsing:
        case Settings::editing: return NavSlot::settings;
        case Settings::closed:  break;
    }
    switch (st.screen) {
        case Screen::status:   return NavSlot::status;
        case Screen::team:     return NavSlot::team;
        case Screen::inbox:    return NavSlot::inbox;
        case Screen::send:     return NavSlot::send;
        case Screen::settings: return NavSlot::settings;
        // ⛔ `count` IS THE ENUM'S BOUND, NOT A SCREEN. C2, fail closed: NO slot is selected rather than a plausible
        //    wrong one. It is listed rather than defaulted so that a sixth REAL screen breaks the build here.
        case Screen::count:    return NavSlot::none;
    }
    return NavSlot::none;   // -Wreturn-type only; -Wswitch covers the enum (the file's own `settings_row_label` idiom)
}

// §5.3 — is the rail drawn at all? ⓘ The top status strip REMAINS VISIBLE during an emergency, as today; only the
// rail goes, and only so the `Font::large` headlines keep `x=0` and the full 128 px.
inline bool ui_rail_visible(Emergency emg) { return emg == Emergency::idle; }

// §6 — the badge, and the priority is the enumerator order (see `CfgBadge`).
// ⓘ The three inputs are INDEPENDENT FACTS, never collapsed into one "config is odd" flag: §3.6.1 rules that a save
//   which needs a reboot is durably saved and therefore NO LONGER unsaved, and `conflict` is a third comparison
//   again. `settings_note` / `cfg_marker_text` in firmware_ui_model.h keep the same three-predicate shape.
inline constexpr CfgBadge ui_cfg_badge(bool conflict, bool unsaved, bool restart_required) {
    if (conflict)         return CfgBadge::conflict;
    if (unsaved)          return CfgBadge::unsaved;
    if (restart_required) return CfgBadge::restart;
    return CfgBadge::clean;
}

// The three configuration predicates, carried as ONE value so the projection takes one parameter rather than three
// positional bools a call site could transpose in silence.
// ★ `from()` IS THE ONE CONVERSION PATH (U2) — ⛔ never rebuild this field-by-field at a call site, which is the
//   S1/L9 field-drop rot. A NULL service FAILS CLOSED to `clean`, which is the model's own documented state for an
//   unattached service (`UiModel::attach_config`): nothing has been edited, so nothing is claimed.
struct ChromeCfg {
    bool unsaved = false, conflict = false, restart_required = false;
    static ChromeCfg from(const mrfw::ConfigService* c) {
        ChromeCfg o{};
        if (!c) return o;
        o.unsaved          = c->config_unsaved();
        o.conflict         = c->conflict();
        o.restart_required = c->reboot_required();
        return o;
    }
};

// ================================================================================ §8.2 — the frozen chrome projection
//
// ★★★ ONLY ALREADY-CLASSIFIED DISPLAY FACTS LIVE HERE, and "classified" is a stronger claim than "copied": every
//     numeric field below is CLAMPED TO WHAT THE PANEL DRAWS and the home age is BUCKETED TO ITS TOKEN. That is not
//     tidiness — it is what makes §11.1's last requirement true, that *visible chrome equality changes only when the
//     RENDERED OUTPUT changes*. A raw combined mail count of 100 and one of 1000 both draw `99+`; a raw age of 5 000
//     and 5 400 ms both draw `5s`; 4 123 and 4 199 mV both draw `4.1V`. Carrying the raw values would make §8.3 mark
//     the model dirty on every one of those, i.e. a repaint per tick for a panel that did not change.
//
// ⓘ WHY THE ASYMMETRY (values for three slots, a TOKEN for the age) — it is §8.2's own list: "mail value and overflow
//   state", "team configured/count/overflow", "battery decivolts/unavailable", but "home icon state and COMPACT AGE".
//   The three value slots are visible-exact once clamped; the age is only visible-exact once bucketed, and its bucket
//   IS the token.
struct UiChrome {
    // ---- §3.1's status strip -------------------------------------------------------------------------------------
    uint8_t  mail          = 0;      // §4.1, ALREADY CLAMPED to 0..kMailMax — the drawn digits, not the true sum
    bool     mail_overflow = false;  // true -> `99+`; see ui_fmt_mail's "at least this many" note
    HomeIcon home          = HomeIcon::blank;
    char     home_age[kAgeTokenCap] = {};   // §4.2's compact token, fully NUL-padded; EMPTY while `home == blank`
    bool     team_configured = false;       // §4.3/§4.4: a configured team is what makes `0` and the key slot mean anything
    uint8_t  team_count      = 0;    // §4.3, ALREADY CLAMPED to 0..kTeamMax
    bool     team_overflow   = false;// true -> `9+`
    KeyIcon  key           = KeyIcon::blank;
    int16_t  batt_dv       = -1;     // §4.5 DECIVOLTS; < 0 = unavailable -> `--`, never a guess
    CfgBadge badge         = CfgBadge::clean;
    // ---- §3.2's navigation rail (the three fields §8.2's amendment ruled in) --------------------------------------
    // ★★ THEY LIVE HERE BECAUSE `UiState` ALONE CANNOT DERIVE THE RAIL HONESTLY, and the code says so: emergency is a
    //    SEPARATE member (`UiModel::emergency()` returns `_emg`, not part of `_st`), and TEAM/SEND availability is a
    //    build-profile fact on the SNAPSHOT (`team_build`). A renderer that reached past the projection for those two
    //    would be doing exactly what §8.2 exists to forbid.
    bool     rail_visible  = false;
    NavSlot  nav           = NavSlot::none;   // ⛔ `none` whenever the rail is suppressed — see the normalisation note
    uint8_t  slots         = 0;               // OR of slot_bit(); 0 whenever the rail is suppressed
};

// ★★★ FIELD-BY-FIELD, AND ⛔ NEVER `memcmp` (§8.2). `UiChrome` has padding — after `mail_overflow`, around
//     `home_age`, before `batt_dv` — and padding bytes are INDETERMINATE, so a `memcmp` would report differences
//     that do not exist and §8.3 would repaint for ever. ⚠ It would also PASS a naive test, because two chromes
//     built the same way in one process usually do have identical padding. That is why the native case builds the
//     two operands differently.
// ⓘ `home_age` is compared over its WHOLE capacity, which is sound because `ui_fmt_home_age` NUL-pads it (see
//   `ui_pad_token`). A `strncmp` would also work; the explicit loop says what is being compared.
inline bool ui_chrome_equal(const UiChrome& a, const UiChrome& b) {
    for (std::size_t i = 0; i < kAgeTokenCap; ++i)
        if (a.home_age[i] != b.home_age[i]) return false;
    return a.mail            == b.mail
        && a.mail_overflow   == b.mail_overflow
        && a.home            == b.home
        && a.team_configured == b.team_configured
        && a.team_count      == b.team_count
        && a.team_overflow   == b.team_overflow
        && a.key             == b.key
        && a.batt_dv         == b.batt_dv
        && a.badge           == b.badge
        && a.rail_visible    == b.rail_visible
        && a.nav             == b.nav
        && a.slots           == b.slots;
}

// ============================================================ §8.3 / §8.3.1 — THE REPAINT INVALIDATION, AS A RULE
//
// ★★★ IT LIVES HERE, NOT IN THE TICK, FOR THE §B115 REASON: `src/firmware_ui.cpp` is compiled by neither the native
//     suite nor the simulator, so a rule written there could only ever be grepped. As three lines of pure code over
//     the real `UiModel` it is DRIVEN by `test/test_firmware_ui_chrome.cpp` (the `chrome-invalidate:` cases) and
//     mutated by `tools/probe_ui_model_mutations.py --target=chrome` (X27-X29), which can read `dirty` directly —
//     something no renderer probe can do.
//
// ★★★★ THE ONE THING IT MUST NEVER DO IS **CLEAR** (§8.3.1, and §B107 before it). `FrameGate::step` tests `blanked`
//      FIRST (`firmware_ui_model.h`, the `if (m.state().blanked)` arm), sets `_open = false` and returns `blank` — so
//      while the panel is dark `dirty` is NEVER EXAMINED, no frame opens, `frame_open()` stays false and light sleep
//      is NOT inhibited. Blanking itself deliberately SETS `dirty`, and the gate records the rule outright: *"nothing
//      is CLEARED here — an invalidation raised while dark survives"*.
// ⛔⛔ AN EARLIER VERSION OF §8.3.1 REQUIRED A BLANKED CHROME CHANGE TO MARK THE MODEL **CLEAN**, AND THAT INSTRUCTION
//     IS **WITHDRAWN**: clearing a dirty bit while dark ERASES A LEGITIMATE PENDING REDRAW. It is recorded here rather
//     than merely not implemented, because the tempting shape ("the panel is off, so nothing is owed") is exactly what
//     a later reader would re-derive. ⇒ this function RAISES or does NOTHING. There is no third arm, and
//     `tools/probe_board_ui/run.sh`'s W3 independently forbids `firmware_ui.cpp` from naming `clear_dirty` at all.
//
// ★★ WHAT IT COMPARES IS THE **WHOLE** PROJECTION, and the reference is the chrome frozen for the MOST RECENTLY
//    OPENED FRAME — updated AT THE FREEZE, never at the observation (§8.3 rule 4). That is what makes rule 5 true: a
//    value that moves while a page loop is open keeps the model dirty every tick, so ONE follow-up frame renders the
//    newer projection instead of the change being consumed by the frame that could no longer show it.
// ⓘ WHY THE RAIL AND BADGE FIELDS ARE IN THE COMPARISON THOUGH SLICE 3 DRAWS NEITHER: both are visible TODAY through
//   another surface. The badge's three inputs are the same three predicates `cfg_marker_text` / `kCfgRestartText`
//   already render on the STATUS title and last body row, and the nav slot only ever moves on a gesture, which has
//   already marked the model dirty. ⇒ no invalidation here is for pixels that cannot change. ⛔ And a SECOND,
//   "strip-only" equality was declined: §8.2 rules ONE field-by-field equality, and a parallel comparator is the
//   field-drop rot (U1) that slice 4 would then have to delete.
inline bool ui_chrome_invalidate(UiModel& m, const UiChrome& live, const UiChrome& frozen) {
    if (ui_chrome_equal(live, frozen)) return false;   // ⛔ NOTHING is cleared on this arm either
    m.mark_dirty();
    return true;
}

// §4.2's core-state -> icon table, `default`-less for the `ui_nav_slot` reason.
// ⛔ THE `blank` ARM IS NOT IN THIS SWITCH BY DESIGN: it is not a core state at all, it is the ABSENCE of the plane,
//    and it is decided by `mobile_build` one level up. Folding it in as a fifth case would invite somebody to map a
//    non-mobile build onto `lost` — the crossed house §4.2 forbids.
inline HomeIcon ui_home_icon(MESHROUTE_NS::Node::MobileHomeLink l) {
    switch (l) {
        case MESHROUTE_NS::Node::MobileHomeLink::unknown:   return HomeIcon::unknown;
        case MESHROUTE_NS::Node::MobileHomeLink::confirmed: return HomeIcon::confirmed;
        case MESHROUTE_NS::Node::MobileHomeLink::checking:  return HomeIcon::checking;
        case MESHROUTE_NS::Node::MobileHomeLink::lost:      return HomeIcon::lost;
    }
    return HomeIcon::unknown;   // -Wreturn-type only
}

// ★★ THE PROJECTION. One construction, from the frozen snapshot + the frozen model state + the service's three
//    predicates. ⛔ It calls NOTHING: no `g_node`, no `ConfigService` (its caller reads that once, into `ChromeCfg`),
//    no clock. That is what lets §8.3 build it every UI tick and compare it against the frame's frozen copy.
inline UiChrome ui_chrome(const UiSnapshot& s, const UiState& st, Emergency emg, const ChromeCfg& cfg) {
    UiChrome c{};

    // §4.1 — the SUM of two already-clamped session-unread values, clamped again to the drawn digits.
    const uint32_t mail_total = uint32_t(s.unread_dm) + uint32_t(s.unread_ch);
    c.mail_overflow = mail_total > uint32_t(kMailMax);
    c.mail          = c.mail_overflow ? kMailMax : uint8_t(mail_total);

    // §4.2 — home. `mobile_build` decides APPLICABILITY; the core state decides which of the four icons.
    if (!s.mobile_build) {
        c.home = HomeIcon::blank;
        // ⛔ EMPTY, NOT `--`. "Not applicable" and "never confirmed" are different silences (§4.2's last paragraph),
        //    and a `--` beside a blank slot would invite a reader to think a home was expected and missing.
        ui_pad_token(c.home_age, kAgeTokenCap, 0);
    } else {
        c.home = ui_home_icon(s.home_link);
        ui_fmt_home_age(c.home_age, kAgeTokenCap, s.home_confirmed_ever, s.home_confirm_age_ms);
    }

    // §4.3/§4.4 — a team is CONFIGURED when this build has the plane AND an id is set. `team_id == 0` is the core's
    // own "we are not in a team" (node.h:261, node.cpp:180/257), so this reads the field's meaning rather than
    // reinterpreting it.
    c.team_configured = s.team_build && s.team_id != 0;
    if (c.team_configured) {
        // ⛔ `team_total`, NEVER `team_shown`: `team_shown` is the UI's 8-row capacity and the retired `T8/12`
        //    fraction is exactly the display-shaped number §4.3 removes.
        c.team_overflow = s.team_total > kTeamMax;
        c.team_count    = c.team_overflow ? kTeamMax : s.team_total;
        c.key           = s.team_key_present ? KeyIcon::present : KeyIcon::absent;
    }   // else: count 0 / no overflow / KeyIcon::blank — and `ui_fmt_team` renders `--`, not `0`

    // §4.5 — decivolts, TRUNCATED. ⛔⛔ AND THE UPPER BOUND IS THE FROZEN SLOT'S, NOT A PLAUSIBILITY JUDGEMENT: a
    //   reading that cannot be drawn as `d.dV` inside §3.1's 35-px battery slot is UNAVAILABLE, and unavailable is
    //   `-1` -> `--`. See `kBattMaxDv` for the pixel arithmetic. ⛔ Never clamp to `kBattMaxDv` itself: `9.9V` is a
    //   plausible-looking voltage this node never measured, which is the one substitution the battery path forbids.
    const int32_t dv = (s.batt_mv < 0) ? int32_t(-1) : (s.batt_mv / 100);
    c.batt_dv = (dv < 0 || dv > int32_t(kBattMaxDv)) ? int16_t(-1) : int16_t(dv);

    c.badge = ui_cfg_badge(cfg.conflict, cfg.unsaved, cfg.restart_required);

    // §3.2/§5.3 — the rail. ★★ BOTH RAIL FIELDS ARE NORMALISED TO NOTHING WHEN THE RAIL IS SUPPRESSED, and that is
    // deliberate rather than incidental: §11.1 requires visible equality to change only when the rendered output
    // does, and while the rail is not drawn NEITHER the selection NOR the slot mask is on the panel. Leaving `slots`
    // populated under an emergency would make two builds that render identically compare unequal.
    c.rail_visible = ui_rail_visible(emg);
    if (c.rail_visible) {
        c.nav = ui_nav_slot(st, emg);
        // §3.2: "builds where TEAM/SEND are unavailable do not draw misleading dead icons. Their canonical slots
        // remain empty; the remaining icons keep the same locations." ⓘ The gated pair is TEAM and SEND, which is
        // `next_screen`'s own set (firmware_ui_model.h) — SETTINGS is in BOTH cycles because its four covered fields
        // are durable on every build, `gateway_heltec` included.
        c.slots = uint8_t(slot_bit(NavSlot::status) | slot_bit(NavSlot::inbox) | slot_bit(NavSlot::settings));
        if (s.team_build) c.slots = uint8_t(c.slots | slot_bit(NavSlot::team) | slot_bit(NavSlot::send));
    }
    return c;
}

}  // namespace mrui
