<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 5 — team-create adapter + screens · dispatch brief · 2026-08-19

⚠ **OPEN OWNER RULING (added 2026-08-20, survives the slice):** four result-screen strings are HOUSE-STYLE DRAFTS,
not owner-ruled — **`TEAM CREATED` · `CREATE REFUSED` · `NOTHING CHANGED` · `REPLACES <fp>`** (plus `no service`
for a null seam; `CREATE NEW TEAM` is the design's own name; `PHY DIFFERS`/`USE SERIAL` IS ruled). Each is one line
in `firmware_ui_model.h`/`firmware_ui_chrome.h` with a native case pinning it. QG gated WITH the drafts; a later
ruling is a one-line edit + battery re-pin, ⛔ never a silent rewrite.

**Status: DISPATCHED.** ⛔ **NO DEVICE CONTACT.** ⛔ Build on the current tree (HEAD `9cef214` + the uncommitted,
QG-passed slice 4 incl. its B222/B223 corrections); do not revert or re-derive anything.
**Normative:** the UI-15 plan §2.1, §5, §6, §8, slice table §9 row 5 — *"Team-create adapter and screens ·
`phy.present=false` (§2.1)"* — and the design doc `2026-07-31-onboard-oled-ui-design.md` §3.6.3 for screen content.
⚠ Latest correction wins; re-verify cited `file:line` against the tree (V2). ⛔ **If the design under-specifies a
screen's text or behaviour, STOP and report — do not invent copy or semantics** (P3).

## As-built facts verified at dispatch (2026-08-19)
- `apply_team(TeamRequest&, const NodeConfig&, const ProvSnapshot&)` — `src/firmware_provisioning_service.h:668`;
  `live_phy_matches(const ProvPhy&, const ProvSnapshot&)` — `:336`, **early-returns `true` when `!phy.present`**.
  Console caller: `src/firmware_config.cpp:1740`.
- ⚠ **`UiProvIntent` DOES NOT EXIST.** Plan §2.1's "(exists)" is about the team ENGINE; the pure, model-owned intent
  carrier is CREATED by this slice.
- Slice 4 left, stated in-source: `create_confirm` entry moved out ([[B222]]) — **this slice re-lands it**;
  `UiSnapshot::prov_create_team` / `prov_join_static` declared but **unpublished** (`build_snapshot`,
  `src/firmware_ui.cpp:471`, reads them `false`) — **this slice owes those two lines**.
- `mrui::ui_fmt_team_fingerprint` (slice 3) is THE fingerprint definition — if any screen draws a team fingerprint,
  it calls this; ⛔ never a second spelling (U1).

## Scope
1. **Publish the two snapshot predicates** in `build_snapshot` per §6: `prov_join_static` from the actual child
   predicate (`MR_N_LAYERS < 2`); `prov_create_team` from that AND team support. ⛔ Static join must not depend on
   the team feature. C3: runtime-gated so unsupported builds stay inert; the model stays parameter-fed, no `#if`.
2. ★ **NEW OWNER RULING 2026-08-19 (reported form): the parent PROVISION row is HIDDEN when NO child is available**
   (e.g. gateway `MR_N_LAYERS=2`). Extend the `settings_rows` conditional-row pattern; renderer follows;
   model-side behaviour mutation-tested. ⛔ The menu-offering-only-BACK state must no longer be reachable on such
   builds.
3. **Re-land the create half of the flow** ([[B222]]'s pending statements are the map): `menu → create_confirm` on
   CREATE TEAM; the confirm dispatch (BACK → `menu`, CONFIRM → run the transaction → `create_result`). ⛔ The
   `join_select` entry STAYS a no-op — slice 6's. Confirm opens with BACK selected (already model state; now it is
   read). Update the done-vs-missing statements to match what is now live.
4. **The adapter (§2.1) — the slice's core, and the trap is spelled out in the plan:**
   - the pure intent (model-owned) → adapter builds a **`TeamRequest` with `phy.present = false`** (no retune);
   - **an explicit live-vs-persisted PHY equality precondition**: build an **OWN `ProvPhy` from the PERSISTED
     values — including `sf_list` — with `present = true`** purely to run `live_phy_matches` (U1: reuse it, never a
     second predicate). ⚠ **The two `ProvPhy` objects must not be conflated** — `present = true` on the REQUEST
     would re-introduce the [[B209]] path;
   - on mismatch **refuse with `PHY DIFFERS — USE SERIAL`**, applying nothing;
   - ⛔ no PHY tail; the OLED create is a MEMBERSHIP operation only; never start static-home discovery ([[B209]]).
   - Pure decision logic goes where the native suite compiles it (the B212/B220/B223 lesson — `firmware_ui.cpp`
     and `firmware_config.cpp` are compiled by NO automated gate); the device TU keeps only forwards/wiring.
5. **Screens** (`create_confirm`, `create_result`) per design §3.6.3: renderer thin in `firmware_ui.cpp`, text and
   decisions in model/chrome where the suite reaches them. **§8 pins apply:** no screen claims success before the
   save returns (structural for team — assert it, don't argue it); emergency pre-empts every provisioning screen
   incl. `create_result`, and an unconfirmed destructive action does not survive; never triggered by the waking
   press.
6. **Chrome projection + UI probes:** update `firmware_ui_chrome.h` projections/tests for the new screens; keep the
   structural UI probes (`probe_firmware_ui`, `probe_board_ui`'s structural halves) green — extend their
   expectations for the new screens and report their counts. Heavy probe batteries beyond that are QG's.

## Pins
1. Gate → `menu` → `create_confirm` → CONFIRM drives EXACTLY ONE transaction; BACK anywhere drives ZERO (counted:
   store writes AND live/apply calls).
2. The PHY precondition: live≠persisted ⇒ refusal with `PHY DIFFERS — USE SERIAL`, ZERO transaction calls, ZERO
   writes; live==persisted ⇒ proceeds; and the REQUEST still carries `phy.present == false` in BOTH cases
   (asserted on the captured request, not argued).
3. `create_result` renders the transaction verdict only after the transaction returned; no success text exists on
   any earlier state.
4. Parent-row hiding: no child ⇒ no PROVISION row (and the row returns when a child predicate holds).
5. `join_select` remains unreachable by gesture; slice-4's `ui15-pending` case updates to cover ONLY the join row.
6. Close-on-leave still holds over the newly reachable arms via `provision_reset_on_leave` (the B223 guard now
   earns its eight-arm coverage); emergency pre-emption asserted for the new arms.
7. Defect-specific mutations RED at match count 1: at minimum — precondition dropped or inverted; `present` forced
   `true` on the request; refusal text/remedy swapped; confirm firing the transaction on BACK; result claiming
   success early; parent row shown with no children. Unchanged positive controls stay GREEN.

ⓘ ★★ **[[B217]] STANDS: re-pin `BASE_CASES`/`BASE_ASSERTS`** (current **1783 / 85884**) when counts move,
derivation in place, **confirm every battery you rely on RAN**. Restore mutation sources exactly; `git diff
--check` clean.

## Verification you run (QG runs the full gate — no boards, no corpus)
1. `pio test -e native`, then **RUN `./.pio/build/native/program`** — real counts, 0 failed.
2. The **`model`** and **`chrome`** batteries (+ any target whose source you touched) — RED counts + proof each ran.
3. The structural UI probes for the files you touched — counts.
4. `git diff --check` clean.

## Report
The intent carrier + adapter shape (signatures, files) · how the two `ProvPhy` objects are kept distinct · each pin
with case name and match count · the two published predicate lines verbatim · the parent-row ruling's
implementation · which done-vs-missing statements changed · **the M2 bench-script lines you owe, DRAFTED in the
report** (exact expected screen/console behaviour for the metal-only half — ⛔ do not edit the bench script) · final
native counts and the new pin · proof each battery ran · exact final `git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`. ⛔ Evidence lands IN THE REPO. ⛔ C1: no refactors ride
along. ⛔ **Stop and report** on any conflict between plan, design doc and as-built that the plan does not already
resolve.
