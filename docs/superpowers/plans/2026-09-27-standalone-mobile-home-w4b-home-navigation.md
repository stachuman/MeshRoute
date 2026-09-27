<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W4b — Home and navigation, with B456 first (`src` feature)

**Revision 2 — 2026-09-27 — DRAFT, awaiting scoped Quality-Agent re-review.** It folds in the
[brief review](../evidence/2026-09-27-standalone-mobile-home-w4b-brief-review.md) (HOLD, W4B-1–W4B-4); see §8.

- **Base:** commit **`8360802`** (`8360802904f7bd0023279d3da844453d61207ede`) **plus the uncommitted, QA-passed W1c, W3 and
  W4a candidates and their landings**. This is not HEAD alone: the pre-check's inventory is the authority for the whole
  tree (§1). The simulator is at **`6585649`** (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 a brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file; eight
  of the fenced files already carry W1c, W3 or W4a work, and are edited on top of it.
- **Authorities:**
  - the [W4b QA pre-check](../evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md) — cited as "pre-check §n";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) **r2.21** — §6.1–§6.7 (with
    r2.21's settled transitions), §4.2, §11.1 (the owner's allocation) and §13 W4b;
  - the [register](../../2026-07-30-open-bug-register.md): B456;
  - the [metal plan](../../2026-09-20-metal-test-plan.md) (landing only);
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1–C3, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md), including the owner ruling of 2026-09-16 on the coder's versus
    QA's gate for a `src`-only slice.

## 0. What this is

**The feature (design §6, D1/D2/D11).**
- **Home and focus.** The landing screen becomes **Home**, and every top-level screen gets two focus states:
  - **list focus** — the body shows the `>` arrow; short moves it and wraps; double chooses;
  - **menu mode** — the left rail has focus and the body shows that screen's preview.
- **MENU rows.** Every top-level list ends with `MENU`: Home, Team, Inbox, Send and the Settings menu. `MENU` enters
  menu mode on the Home slot.
- **Home's content.** Home shows:
  - the own name (row 0);
  - a team line;
  - a state-dependent action list;
  - **My device**, a read-only sub-view.
- **Setup from Home.** JOIN and CREATE pass W3's settings admission; INVITE opens directly; the flow's origin is
  typed so its exits return to Home.
- **Removed from Home:** the 24×24 mark and the own position.

**Stage A first — B456.** The firmware-UI probe can print PASS after skipping controls. Before W4b changes and adds
many probe checks, the probe gets a control-completeness guard with its own regressions, against the unchanged product.

**Allocation — owner-ruled 2026-09-27 (design r2.21 §11.1).** +72 B of static UI structures on the OLED boards
(+64 on the host), in the exact QA-measured shape of §2.2. Any further retained state is a STOP.

**Not in W4b:**
- the splash (W5) and the Home card (W9) — keep the list window's geometry seam, allocate nothing;
- newest-first Inbox (W4c/W4d);
- the editor, the name prompt and CHANGE NAME (W7), `WRITE MESSAGE` (W8), phrases v2 (W6);
- a `Screen` enum rename (C1 — Home reuses `Screen::status`);
- `lib/`, wire, NV, simulator, `fw_main.cpp` and `firmware_commands.cpp`;
- B418's board-UI repair (W2).

**The proof:**
1. **Stage A** — the instrument repair (B456) on the unchanged product.
2. **Stage B** — the feature, with a closed **expectation, control and mutation disposition ledger** (§2.9). The probe
   cannot stay byte-identical — the navigation changes by design — so every changed, retired or added expectation is
   named there.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

"UI" is `src/firmware_ui.cpp`; "model" is `src/firmware_ui_model.h`. The complete per-seam facts are in pre-check §2;
the brief relies on them.

| Seam | Fact at the base | W4b change |
| --- | --- | --- |
| `Screen` (model `:~246`), `next_screen` (`:~5572`) | `status, team, inbox, send, settings`; non-team builds skip TEAM/SEND, keeping rail holes | Names, order and skipping kept; `status` is Home; short moves screens **only in menu mode** |
| `UiState` (`:~2447`), `ListView` (`:~278`), `screen_is_entered` (`:~377`) | Boot on STATUS, passive; only TEAM/INBOX use `ListView` | `list_view` becomes the **one focus authority** for all five screens: `interactive` = list focus, `passive` = menu mode. Spellings kept |
| `advance_or_next` (`:~3907`), `list_len` (`:~5560`), `list_row_kind` / `list_activate` / `list_note_kind` (`:~293–346`), `list_view_reset_on_leave` (`:~363`), `list_follow_screen` (`:~4107`) | Passive length 1; entered TEAM/INBOX wrap through records plus BACK; lost-pick refusal outranks exit | List short wraps; exit row `MENU` → menu mode on Home. Identity and refusal protections kept; one transition authority |
| `activate` (`:~3940`) | TEAM/INBOX enter; SEND opens `Compose::channel`; Settings opens browsing over an open service | Home activation; menu-mode dispatch; Send's promoted list (§2.6) |
| `settings_rows` (`:~652`), `settings_row_label` (`:~690`), `settings_activate` (`:~4255`), `settings_follow_screen` (`:~4049`), `sync_settings` (`:~4111`) | Last row `CfgRow::back` = BACK (`on_back()`, no save); walking off the end closes the menu; arrival opens the service | Last row `MENU` (keeps `on_back()`, then menu mode on Home); short wraps. Service, draft and close-on-leave rules kept |
| `ensure_config_open` (`:~4276`), `provision_admit` (`:~4286`) (W3) | Admission; the unavailable branch has no `ProvBlock` | First reachable caller: Home. `ProvBlock` gains `unavailable` (no byte growth) |
| `on_tick` blank arm (`:~2990–3020`) | Cancels unfinished confirmations at the blank (UI-16 OQ-3): `invite_confirm`/`invite_need_pubkey` → `leave_grant_chain(resume)` (roster origin → TEAM); `saved_key` → `enter_provision(menu)` | **Kept verbatim** — the design r2.21 §6.1 rule-7 exceptions |
| `on_gesture` (`:~2758`), `unblank` (`:~5377`), `on_msg_wake` (`:~3235`), `emergency_gesture` (`:~5589`), `close_compose` (`:~5539`) | Wake-only first press; R-7 wake; safety pre-emption; `close_compose` resets the cursor | Timing and pre-emption kept; the new ordinary focus and Home item survive (§2.1) |
| `FrameGate::step` / `on_page` (`:~5720` / `:~5766`) | Commits frozen arrival serials whenever INBOX is unobscured, preview included | **Unchanged** — the Inbox list and its menu preview mark read as today |
| UI `draw_status_screen` (`:~1415`), `draw_rail` (`:~1354`), `draw_settings_screen` (`:~2144`), `list_first` (`:~1124`); chrome `ui_nav_slot` (`:~325`), `ui_chrome` (`:~536`), `ui_chrome_equal` (`:~464`); tick invalidation (UI `:~2573`) | The STATUS body with the mark at x12/y12 and x40 rows; the rail box x0/w10/h10; `>ENTER SETTINGS`; field-wise chrome equality | §2.7 |
| `firmware_ui_status.h`: `ui_status_team`, `ui_status_me`, `ui_status_known`, `ui_status_location`, `ui_status_have_fix` | The old STATUS rows; coordinate and fix formatting | Home and My-device formatters are new there; location and fix helpers reused; old row helpers retired (§2.9) |
| `Node::effective_name` (W1c) | Counted stored name, possibly empty; never terminated | Read once per `build_snapshot` into the snapshot (§2.4) |
| `tools/probe_firmware_ui/run.sh` | Controls extracted (`:~296–309`, B227) but only counted when run (`:~403–474`); the tail (`:~1833`) never compares | Stage A (§2.8) |
| `tools/probe_board_abi.py` `PIN_TABLE` | `UiState` 504/8; `UiSnapshot` 1336/8; `UiModel` 928 / 912 / 912 (native / heltec_mobile / gateway); `UiChrome` 20/2 | Re-pinned to the approved sizes (§2.2) |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at authoring time.

| File | SHA-256 | Lines | Note |
| --- | --- | ---: | --- |
| `src/firmware_ui_model.h` | `4a499098be6b55cb2c3c02c3cf5462b87489eca34e8a15d30e6b18c2346239c9` | 5822 | W4a candidate |
| `src/firmware_ui.cpp` | `bfe5d70e0795effcbeaa7623dc3ee84f4481bc5b04d5728f8167619b92f3530e` | 2834 | W4a candidate |
| `src/firmware_ui_chrome.h` | `1fb0d8f0bd320bbf033263d203129ad101e9e2d37b772c55b0ae2d20e04fb037` | 598 | equals HEAD |
| `src/firmware_ui_status.h` | `b6e42c8e5d595c41f1f34fdf684176729b69e65f895582b6ef10c3f8648f710b` | 221 | equals HEAD |
| `test/test_firmware_ui_model.cpp` | `ba92a0f5348144150f6c9ecb6cd5c35dee728396bfae81799fd1be260a62f373` | 10434 | W4a candidate |
| `test/test_firmware_ui_chrome.cpp` | `6ab7d9307b249a1fb6eafbff1387e0e399062bb0f69fba09a3c2e866aea28e64` | 1310 | equals HEAD |
| `test/test_firmware_ui_status.cpp` | `5d70ed87a048ddff375b624fc237b2d3dcc6cdaa38a029d24face08544bc4ec4` | 392 | equals HEAD |
| `test/test_firmware_ui_team.cpp` | `1833ed10acc01bd71d7ca7142b3180ff5fb68bff2ac508181033edbd36ece38a` | 655 | W4a candidate |
| `test/test_firmware_ui_send.cpp` | `b1287e803bf336297fa4ae850cf18e59e5048dc2de50ed888e57af5cb0a694fe` | 2835 | equals HEAD |
| `tools/probe_firmware_ui/probe_main.cpp` | `3090a371e36295338a02e6c878eb1c32471f33dffd6260e68698d31b560c6905` | 7471 | W4a candidate |
| `tools/probe_firmware_ui/run.sh` | `89e41dd80920ba42e72216d982a0702c3444fcf1a62ec71df44ccddd86252468` | 1836 | W4a candidate |
| `tools/probe_ui_model_mutations.py` | `b1574c35d23cf0e5bf2f2898be6bf14c8ef453accbd6db51eefe5e71e3e10b4b` | 12493 | W4a candidate |
| `tools/probe_board_abi.py` | `d6edbea5b7437e2011d4237245e570a07129708261f1f4c517a8bb515fe6178d` | 1053 | equals HEAD |
| `tools/test_probe_board_abi.py` | `c6630098421aed5fc2ad54ea29f512e624ed6ba1f9d19b188a0b1632707951c3` | 524 | equals HEAD; edited **only** if a size assertion there depends on the re-pin |
| `tools/test_probe_firmware_ui.py` | **new file** (B456 regression) | — | absent at authoring |

**Base inventory.** The pre-check's `inputs.json` (1,576 MeshRoute paths, covered by its `SHA256SUMS`) is the authority
for every other path. After QA wrote it, only the four preparation documents below changed at authoring. New paths
since then: the pre-check report and folder, this brief, and QA review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.21) | `1d0618da3e10e1498065de04b6e7681a62a69cfeef0d0e0e102d41494ba27c1f` |
| `docs/2026-07-30-open-bug-register.md` | authority (B456; §0) | `1ff3a21bfb97cab0f56d2dc23ac597bd37c7b31e53482c5a775c73fcea9e4673` |
| `docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md` | authority (pre-check) | `38b699335991cf04ffc21c7eb9702f36f3f8a3ba640ad5ff0d9b5a51047294aa` |
| `docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b-precheck/SHA256SUMS` | evidence — covers the folder's other 14 files, including `inputs.json` (`84b8af5d…94aa`) and `corpus-manifest.json` | `a6ba6d303a256f4a6ae57453bd7432d9850b64de99a4c98857948e2335a2af85` |
| `tracker.md`, `MEMORY.md` | context pointers | `bd1566b362047afe58be0668f74c1a2a0fedb1adf2a4b96aeb486e0ff3d399f2`, `fb3826dc9254190fe66f15fa9edae81f646595601a8ac90c14bdf82d464d1092` |

This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `8360802`; the simulator at `6585649` and clean.
2. Every executable input matches its hash above, and `tools/test_probe_firmware_ui.py` is absent.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 14 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents above.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 Focus and navigation (design §6.1, r2.21)

- **One focus authority.** `UiState::list_view` means list focus (`interactive`) or menu mode (`passive`) on every
  top-level screen, and `UiState::screen` is the selected rail screen. No second flag and no second rail index.
- **Boot:** Home, list focus, arrow on item 1.
- **Menu mode:**
  - short moves to the next enabled screen in rail order, skipping as today;
  - the body shows that screen's preview (§2.7);
  - double opens the previewed screen in list focus, arrow on its first row.
- **List focus:** short moves the arrow and wraps — Settings included, which no longer walks off the end; double
  chooses. The final `MENU` row enters menu mode on the **Home** slot, so `MENU` then double returns Home from
  anywhere.
- **Sub-views** keep their own exits to their opener:
  - a person's compose list keeps `back, don't send` and returns to that person in the TEAM list;
  - message detail, results (either-press acknowledgement), setup flows, My device and notes keep theirs.
- **Blank (rule 7).** A blank keeps the screen, the focus state and the arrow (Home's selected item included). The
  consumed wake press and the R-7 message wake show exactly what was there; neither a blank nor a push changes the
  screen. **The only exceptions are the existing OQ-3 unfinished-confirmation cancellations** in `on_tick`'s blank arm,
  kept verbatim. They never land on Home, and a setup origin survives them.
- **Emergency.** Holds work in both focus states; the overlay rules and the existing pre-emptions (detail,
  provisioning, Settings editing, a committed alarm closing compose) are unchanged. Afterwards the previous
  **ordinary** focus and Home item return. Do not rely on `close_compose`'s cursor reset for this — preserve it
  explicitly.
- **Unchanged:** `FrameGate` scope; the rail naming the body in both states; the rail's box following the previewed
  screen in menu mode.

### 2.2 Retained state — the owner-approved shape (design r2.21 §11.1)

Match QA's measured "full six-item capture" variant (pre-check §3, `layout-measure.py` `reuse_focus_append_name`).
The names are suggestions; the layout is binding.

```
enum class HomeItem   : uint8_t { none, inbox, send, team, invite, my_device, menu, join, create, key_help };
enum class HomeView   : uint8_t { list, my_device, key_help, setup_block };
enum class SetupOrigin: uint8_t { none, home, settings };
struct HomeCapture { HomeItem items[6]; uint8_t count; HomeItem selected; bool changed; };   // 9 bytes, align 1
```

- **`UiState`:** a `HomeCapture` and a `HomeView`, appended after `InviteGrantResult grant{}`. Size **520** / align 8
  on all three ABIs.
- **`UiModel`:** `HomeItem` (the return item) and `SetupOrigin`, after `GrantReturn _grant_return{}`. Size
  **944 native / 936 heltec_mobile / 936 gateway**, align 8.
- **`UiSnapshot`:** `char own_name[32]` plus `uint8_t own_name_len`, appended. Size **1368** / align 8.
- **`UiChrome`:** a `bool` menu cue, appended in existing padding. Size **20** / align 2, unchanged.
- **`ProvBlock`:** gains `unavailable`, with no byte growth.

The static sum must be **+64 native / +72 on each board ABI** (one `s_model`, `s_frame_state`, `s_frame_snap`,
`s_frame_chrome`). The per-tick stack snapshot grows by 32. **Any other retained member, or a different size or
alignment, is a STOP** (owner ruling).

Re-pin `tools/probe_board_abi.py` `PIN_TABLE` for `UiState`, `UiSnapshot` and `UiModel` from the stock `--repin`
measurement, with one derivation comment. Re-sync the native size assertions in `test_firmware_ui_model.cpp`
(`sizeof(mrui::UiState) == 504u` and its siblings, `:~8215`, `:~9162`, `:~9467`, `:~9921`, `:~10398` onward) as
ledger items.

### 2.3 Home (design §6.2–§6.4, §6.5 and its r2.21 interim note)

**Profile — one pure selector over existing snapshot facts** (pre-check §4):

| Condition | Profile | Items, in order |
| --- | --- | --- |
| `!team_build` | no team plane | `INBOX` · `MY DEVICE` · `MENU` |
| `team_id == 0` | no team | `JOIN TEAM`¹ · `CREATE TEAM`¹ · `INBOX` · `MY DEVICE` · `MENU` |
| team, no content key | key missing — outranks a pending ID | `NO TEAM KEY - HELP` · `INBOX` · `TEAM` · `MY DEVICE` · `MENU` |
| key held, local ID 0 | ID pending | `INBOX` · `TEAM` · `MY DEVICE` · `MENU` |
| key held, local ID set | ready | `INBOX` · `SEND TO TEAM` · `TEAM` · `INVITE MEMBER`¹ · `MY DEVICE` · `MENU` |

¹ Only when its capability flag is set (`prov_join_team`, `prov_create_team`, `prov_invite`). Production OLED team builds
have all three, and a build without one omits the item. Never bypass a capability to make the synthetic l2 probe arm
pass.

**Rows:**
- **Row 0:** `ME ` + `ui_fmt_identity(own_name, own_name_len, my_key_hash32, 16)`. On `IdentityFmt::none` (no name and
  no hash) the row is `ME` alone — never a fabricated identity.
- **Row 1, the team line:**
  - `TEAM <8 uppercase hex> T<id>` (18 cells);
  - `TEAM <8 hex> NO ID` (19);
  - `NO TEAM`;
  - blank with no team plane.

  It uses the existing full team-ID formatter. It is new text in `firmware_ui_status.h`, not the old two-row
  helpers.
- **Row 2:** `RESTART NEEDED` (not selectable) while `SettingsView::reboot` — no new NV read. Otherwise the list.
- **List window:** rows 2–4, or 3–4 under `RESTART NEEDED`, through `list_first` with the visible row count. Each list
  row reserves one marker cell (`>NO TEAM KEY - HELP` = 19).

**Labels:**
- `INBOX`, or `INBOX <tok> NEW` with `ui_fmt_mail`'s token (1–99, `99+`); omitted at zero;
- `TEAM`, or `TEAM <tok> KNOWN` with `ui_fmt_team`'s token (1–9, `9+`); omitted at zero;
- the other items are fixed text.

A count changing inside a label is not an item change.

**Arrow by identity.**
- The arrow follows `HomeCapture::selected`, and the visible index is derived from it.
- **Returning** from anything Home opened lands on its opener, taken from `UiModel`'s return item, or on item 1 if the
  opener no longer exists.
- **`OPTIONS CHANGED`** follows the design §6.4 r2.21 table **exactly**:
  - it is raised only in list focus, lit or dark, when the selected item disappears — the arrow moves to item 1 and
    `changed` is set;
  - any non-wake short or double clears it and runs nothing;
  - the wake press only wakes;
  - further list changes keep the note and item 1;
  - an emergency leaves it in place;
  - menu mode never raises it.

**Home sub-views** (`HomeView`; the rail box is STATUS for My device and key help, SETTINGS for the setup block):
- **My device** — §2.4.
- **Key help.** The body rows are exactly `NO TEAM KEY` / `A MEMBER WHO HAS IT` / `MUST GRANT IT TO` / `THIS DEVICE` /
  `press = back`. Either press returns Home on its opener. No automatic key request.
- **Setup block** — §2.5.

### 2.4 My device (design §6.7) and the own-name publication

- **Publication.** `build_snapshot` calls `g_node.effective_name(own_name, 32)` once per tick and stores the returned
  count. No NV read. The frame freeze covers it.
- **Row 0:** name bytes 0–18, each through `ui_display_byte`. **Row 1:** bytes 19–31, the same way.
  - Split the raw counted bytes — never an abbreviated string.
  - Unnamed: `NO NAME SET` on row 0, row 1 blank.
- **Row 2:** `ID ` + `ui_fmt_member_hash_full(my_key_hash32)`.
- **Row 3:** `ui_status_location` with the restart override false. That gives E7 coordinates truncated to three
  decimals, the correct negative-zero sign, and `(0,0)` → `NO LOCATION`; a fix with one zero coordinate is valid.
- **Row 4:** `>BACK`. Double returns Home on `MY DEVICE`; short stays on BACK, the only row.
- **Invalidation.** A change to the visible own name, the full team ID or local ID, the profile, or the own position
  must repaint Home and My device on the next frame, not only the strip. The current frame stays one snapshot.

### 2.5 Settings, admission and typed setup origin (design §6.1 Settings, §6.6 r2.21)

- **Settings preview:** today's closed view **without** the body `>` (`ENTER SETTINGS`); badge, remedy and
  `CFG UNAVAILABLE` unchanged. Double opens the menu in list focus over an open service, as today.
- **Settings menu:** the last row is `MENU`. It runs `on_back()` (no save, draft kept), then enters menu mode on the
  Home slot. Short wraps. Sub-view BACK rows stay BACK.
- **Home `JOIN TEAM` / `CREATE TEAM`.**
  - *Admission at activation:* run `provision_admit` **when the Home item is activated**, before any generic SETTINGS
    arrival.
  - *On refusal:* record the reason, including `ProvBlock::unavailable` for an unattached or still-closed service, and
    open the setup-block note:
    - row 1: `SAVE OR DISCARD`, `RELOAD OR DISCARD` or `CFG UNAVAILABLE`;
    - row 2: `IN SETTINGS`, blank for unavailable;
    - no arrow; the rail box on SETTINGS.
  - *Frozen reason:* it holds until the note is dismissed. A later recovery changes nothing and never resumes setup.
    Either press returns Home on the opener.
  - *On admission:* CREATE reuses the safe BACK-first create confirmation, and JOIN reuses the frozen NEARBY load and
    confirmation. Both run with origin `home`. No name prompt (W7).
- **Home `INVITE MEMBER`.** No admission. Exactly `load_invite(s) → enter_provision(invite) → request_team_announcement()`,
  once per fresh window, with origin `home`. Capability and identity checks deeper in the flow are untouched.
- **SETTINGS → PROVISION** sets origin `settings`.
- **The typed origin:**
  - *Set* only by a new entry: a Home item activation sets `home`, and the SETTINGS menu's PROVISION row sets
    `settings`. Any action chosen from the PROVISION menu keeps the current origin — it is the same setup session.
  - *Survives* confirmations, waiting, results, saved-key offers, remedies and the OQ-3 blank cancellations.
  - *Retires* when the flow genuinely leaves SETTINGS, is pre-empted, or a new entry sets it.
  - *Where it acts* — only at these exits; every other transition keeps today's destination:

    | Exit | Origin `home` | Origin `settings` | Origin `none` |
    | --- | --- | --- | --- |
    | a return site in pre-check §6's table that goes to the PROVISION menu today | Home, on the opener (item 1 if it is gone) | the PROVISION menu, as today | the PROVISION menu (the fallback), as today |
    | the PROVISION menu's own BACK (`ProvRow::back` → `close_provisioning()`) | Home, on the opener (item 1 if it is gone) | the Settings menu (`Settings::browsing`), as today | the Settings menu, as today |

  - *Never replaced:* `enter_provision(menu)` on the SETTINGS **entry** path and on the push-driven edge, and the OQ-3
    cancellations' own destinations — a blank still closes a saved-key offer to the PROVISION menu.
  - *No unsolicited navigation:* a radio arrival never navigates to Home.
  - *The trace, pinned in the native origin matrix:* Home `JOIN TEAM` → an acknowledged join with a saved-key offer →
    blank (the offer closes to the PROVISION menu; origin `home` survives) → wake → BACK → **Home**, on item 1 (JOIN
    TEAM left with the join). Its counterpart from SETTINGS → PROVISION → JOIN ends on **the Settings menu**, as today.
    From that PROVISION menu, any fresh action keeps origin `home`, so its exits return to Home.
- **`GrantOrigin` is separate and unchanged.**

### 2.6 The other top-level screens (design §6.5 r2.21 interim content)

- **TEAM and INBOX.** The same rows, order, identity guards and preview bytes; the final row becomes `MENU`. An empty
  list shows an operable `MENU`. INBOX keeps **today's order** (W4c/W4d change it).
- **Send is the channel compose list promoted to a top-level list.**
  - *Rows:* list focus shows the enabled channel phrases, then `MENU` in place of `back, don't send`.
  - *Preview:* the menu preview is today's passive SEND text.
  - *Sending:* reuse the existing compose engine — no second send path. The send, sealed `{slot, generation}`, result
    and either-press acknowledgement are today's. Acknowledging a result returns to the Send list, arrow on item 1.
  - *Catalog change while the list is open:* re-read the list, move the arrow to item 1, and show `PRESET CHANGED` in
    place of item 1's label until the next press. That press sends nothing and follows the §6.4 table's rules. This
    keeps today's rule that a catalog change never lets a press send.
  - *DM compose:* unchanged.
- **No `WRITE MESSAGE`** (W8).

### 2.7 Rendering

- **Home** draws at the ordinary body, x12 and 19 cells. There is no 24×24 mark and there are no x40 rows on Home
  (the mark asset is kept for W5).
- **Menu-mode previews:** Home is its body without the arrow; TEAM and INBOX are unchanged; Settings drops its `>`;
  Send is today's passive text.
- **Menu cue:** a 2-pixel bar at x10–11, one rail slot high, beside the boxed slot, drawn with the existing rectangle
  call — in menu mode only.
  - It is a field of the frozen `UiChrome` projection and of `ui_chrome_equal`.
  - Keep **exactly one** x0/w10 selection box, and the fixed y slots and holes.
- **Unchanged:** the strip, W3/W4a geometry (W42's x12 and x0-emergency, W43's literals) and the emergency full width.

### 2.8 Stage A — B456, the instrument repair, against the unchanged product

Implement pre-check §9's contract in `tools/probe_firmware_ui/run.sh`:
1. **Expected set.** The expected label set is every source-declared control, keeping B227's fail-closed extractor and
   call-count agreement. Never derive it from the calls that ran, and never use a count literal.
2. **One verdict per label.** Every label gets exactly one terminal verdict through **one** accounting path, including
   C0's accepted build failure. A missing, duplicate or unknown label fails the run. A declared label passes only with
   its existing accepted outcome.
3. **Guards.** Any guard failure fails the run — a zero or multiple match, a failed `sed`, or an undefined helper (exit
   127).
4. **`--no-neg`** says explicitly that controls were not run and that it is not a gate.

**The runner-owned selftest.** Add a selftest mode that runs this **same** accounting on synthetic label and verdict
streams, without compiling mutants. A new discoverable **`tools/test_probe_firmware_ui.py`** invokes it and asserts the
exit status for each case:
- the complete set;
- missing first, middle and last;
- a duplicate; an unknown label; a duplicate compensating a missing label at the same count;
- zero expected, or an extraction mismatch;
- a guard failing nonzero or 127 with no verdict;
- C0's accepted build failure versus an ordinary must-build failure;
- `--no-neg` as a non-gate;
- **load-bearing:** bypassing the final accounting call must make a missing-control case fail.

**Run and freeze.** Run the stock probe on the **unchanged product**:
- all three arms green, 529 / 980 / 529;
- all **236** original labels accounted for exactly once, with every control meaning unchanged.

Record the SHA-256 of `run.sh` and the new test, and the product hashes, at the end of stage A.

### 2.9 Stage B — the feature, with a closed disposition ledger

**Production:** §2.1–§2.7.

**The ledger.** Write it to the evidence before the freeze. It lists, per item: old → new, the reason, and the
witness that replaces it. Anything absent from it that changed is a STOP. It covers:
- **Native expectation classes** (pre-check §8's table):
  - `test_firmware_ui_model.cpp` — boot, focus and walk, exit destinations, Settings wrap, Home-origin branches and the
    resource sizes. The lost-record/member refusal, safe confirmations, result delivery, R-7, unread and detail cadence
    are kept.
  - `test_firmware_ui_chrome.cpp` — the cue projection and equality, and the new sub-view mapping. Strip tokens,
    priorities and emergency suppression are kept.
  - `test_firmware_ui_status.cpp` — old STATUS rows retired and replaced by Home and My-device facts. Coordinate and
    fix cases are kept.
  - `test_firmware_ui_team.cpp` and `test_firmware_ui_send.cpp` — **navigation fixtures only**; no row, send or tracker
    assertion weakened.
- **Probe checks.**
  - *Rewritten:* only the navigation and STATUS expectations — P6, P7/P8, P13, P14, P17/P18, and the origin-specific
    P20+ flows.
  - *Helpers:* `leave_list` and the fixture entry sequences (`:~1253–1330`) follow the new navigation. `rail_boxed_slot`
    and the rectangle census (`:~385–483`) tell the gutter bar from the selection box, and exactly one box stays
    asserted.
  - *Kept:* W4a name bytes, W3 paging and compose width, and every send and grant effect.
- **Renderer controls** — every disposition records the checks the control actually fails, and they must include the
  named property's check:

  | Controls | Disposition | Must fail |
  | --- | --- | --- |
  | C83; C96–C98; C100–C101 | **Retired** — they pin the removed STATUS rows, their x40 origin and the mark's presence | replaced by Home's row, placement and x12/19-cell checks, the new team-line checks, and new controls 2 and 7 |
  | C121, C122, **C123** | **Retired** — all three edit the mark `draw_bitmap(kStatusMarkX, …)` call that W4b deletes | replaced by a negative witness that Home draws no mark bitmap and no rectangle in the old mark slot, and by new control 2 |
  | C35 | **Re-anchored**, property kept: the restart fact reaches the landing screen | Home's `RESTART NEEDED` row check |
  | C84 | **Re-anchored**, property kept: no configuration marker on the landing body (R-3) | a Home no-config-text check |
  | C92 | **Re-anchored**: the preview label still comes from `kSettingsEnterText`, now without the `>` | the Settings preview label check |
  | C93, C94 | **Re-anchored if needed** onto the focus predicate: the renderer follows the model's list focus versus menu preview | the focus-render checks |
  | C95 | **Re-anchored**: the list's exit row — now `MENU` — is never dropped | the `MENU` row checks |
  | C99, **C124** | **Re-anchored if needed**, property kept: no mark and no empty reserved rectangle in the chrome on any screen. If the `kStatusMark*` constants go, re-express the injected draw with the literal 12/12/24/24 geometry | C99: the forbidden-mark bitmap check; C124: the body-rectangle census |
  | **C108** | **Re-anchored if needed**, property kept: the TEAM rows are never also drawn at the old x40 origin. If `status_text` goes, inject a literal x40 draw instead | the forbidden-origin census |
  | **C102** | **Kept, re-anchored** onto My device's position row: a row that re-reads the live config mid-frame | the My-device frozen-position checks (§2.10) |
  | **C103, C104** | **Kept verbatim** — they mutate the live `build_snapshot` fix publisher (`s.own_fix = …`) | the My-device position and fix checks |
  | C90, C91, C105–C107, C109, C110 | **Kept verbatim** (the Settings preview; TEAM rows and repaint) | their existing checks |
  | C76–82 (rail ownership and emergency, plus a separate gutter predicate), C61–62 and C69–75 (frozen paint and blank), W3-U1/W3-N1 (the Settings path; new Home-origin note controls are added, not retargeted), B241a/b, N4, N9, O2, O8, W4a-S1–S6, W3-D1/D2/C1/C2 | **Kept** — reached by the new navigation, assertions intact | their existing checks |
  | C0 | **Kept** — `must_build=no` | a build failure |

  Never keep a dead product helper just so a mutant compiles.
- **New renderer controls** — each `once`-guarded, `must_build=yes` and RED on its intended new checks; at least:
  1. the cue drawn in list focus, or missing in menu mode;
  2. the mark reinjected on Home, or a Home row drawn at x40;
  3. a top-level exit row reading `BACK`;
  4. the Settings preview keeping its `>`;
  5. the My-device name abbreviated instead of split;
  6. the blocked note on the wrong row;
  7. a Home list row placed on the wrong body row;

  plus §2.10's three wiring controls.
- **The W41 reader** (`tools/probe_board_ui/run.sh`, `w41`) requires the bare statement
  `if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);` verbatim. Keep it, and draw the cue as a separate
  statement — never wrap the box statement in braces. A needed reader change returns for scoped review; B418 is not
  absorbed.
- **Native mutations** (pre-check §8's anchors):
  - *Revised contracts, not silent re-anchors:* M101, M102 and M109 (the old walk-off and BACK destination) → Settings
    wraps; `MENU` goes to Home and menu mode.
  - *Kept:* M107 and M110–120 (passive-no-action, containment, stale pick) under the new focus model; S03 still proves
    a committed alarm closes compose while the ordinary cursor survives.
  - *Chrome:* X17–24 and X27–30 kept; add cue and no-cue entries.
  - *Status:* S01 and S05–12 retired with replacements on the new formatters; S02–04 and S13 kept.
  - *Admission and setup:* the M55–59, M62–72, M76–78, M83–88, V- and W- families keep their meaning through any
    re-anchor.
  - *A new battery `w4bhome` → `src/firmware_ui_model.h`*, with at least ten entries, each RED:
    1. key-missing no longer outranks ID-pending;
    2. capability gating bypassed;
    3. the arrow kept by row instead of identity;
    4. `OPTIONS CHANGED` not raised, or not refusing the double;
    5. `MENU` returning to the wrong slot or focus;
    6. a blank re-homing;
    7. an OQ-3 cancellation removed;
    8. the Home-origin exit going to the PROVISION menu;
    9. the blocked reason re-read, not frozen;
    10. a Send `PRESET CHANGED` press that sends.
  - *`uistatus`* gains at least three entries on the new formatters: the team-line spellings, the My-device split, and
    `NO NAME SET`.
  - *Retirements:* each is listed with its replacement; no silent GREEN.
- **The new native navigation matrix** — pre-check §8's list in full, through the real `on_gesture` / `on_tick`:
  - the five profiles × boot, list and menu;
  - every item and return, including a vanished opener;
  - the `OPTIONS CHANGED` table;
  - five screens × two focus states × wake and blank;
  - emergency from every state;
  - Home admission: clean, dirty, conflict, both, unavailable, no adapter and recovered, with counted opens, saves and
    applies;
  - origin through every return-site family, including the PROVISION menu's own BACK and §2.5's saved-key blank
    trace with its Settings-origin counterpart;
  - names of 0, 16, 17, 19, 20 and 32 bytes, plus high bytes;
  - the snapshot fields and focus the model freezes — projection only; the real publication, page and refresh proofs
    are §2.10's;
  - Inbox list and preview watermarks;
  - Send result and catalog-change returns.

### 2.10 Real-renderer proofs — the firmware-UI probe

Native tests cannot reach this wiring: `test_build_src = no` keeps `src/firmware_ui.cpp` out of the native build, so an
injected `UiSnapshot` cannot catch a missing publisher, a renderer reading live state between pages, or an unwired body
invalidation. Native tests keep the projection and navigation; the probe owns these:
- **Publication.** Drive the real own-name producer — the probe's node's stored name, read by `build_snapshot` — with an
  empty, a short and a 32-byte name, and a name with high bytes.
  - Home row 0 shows the 16-cell identity: `ME ` plus the formatted name, or `ME 0x<HASH8>` when unnamed.
  - My device shows the two counted, sanitized rows, or `NO NAME SET`.
- **Frozen pages.** Change the name while a frame's OLED pages are being drawn. Every remaining page of that frame shows
  the old name, and the next complete frame shows the new one. Keep P17b's position proof, moved to My device.
- **Body refresh without a press.** Change the visible name, the full team ID or local ID, the profile (a key or an ID
  arriving) and the fix — with no navigation press and no strip-token change. The next frame repaints the Home or My
  device body. Focus and cue consistency, blank and the quiet bus stay covered at their existing boundaries.
- **Three wiring controls** (`once`-guarded, `must_build=yes`), each RED on these real-renderer checks:
  1. the publisher omits the own name, or publishes the wrong count;
  2. the renderer reads the live name instead of the frozen snapshot;
  3. the body-invalidation call is left out of the tick.

  C102 stays the position counterpart (§2.9).
- **No production hook** and no extra retained state.

### 2.11 Nothing else moves

- **Corpus:** 36/36, identical field by field to the pre-check manifest.
- **Out of scope:** no `lib/`, wire, NV, command-surface or console change; the command inventory stays at 197 rows.
- **Boards:** `gateway` does not compile the OLED UI, so its image is predicted identical. For `heltec_mobile`, the RAM
  delta must be explained by the §2.2 structures — it is measured, and its symbols attributed. Flash is measured and
  attributed.
- **Unchanged:** W1c, W3 and W4a behaviour (their tests and probe assertions still pass), and B418's known failures
  exactly.

## 3. Fence

**IN:**
- `src/firmware_ui_model.h`, `src/firmware_ui.cpp`, `src/firmware_ui_chrome.h`, `src/firmware_ui_status.h` — §2.1–§2.7
  and their comments;
- `test/test_firmware_ui_model.cpp`, `test/test_firmware_ui_chrome.cpp`, `test/test_firmware_ui_status.cpp` — §2.9;
- `test/test_firmware_ui_team.cpp`, `test/test_firmware_ui_send.cpp` — navigation fixtures only;
- `tools/probe_firmware_ui/run.sh`, `tools/probe_firmware_ui/probe_main.cpp` — stage A, then stage B per the ledger
  and §2.10;
- `tools/test_probe_firmware_ui.py` (new) — B456's regression;
- `tools/probe_ui_model_mutations.py` — the ledger's dispositions, `w4bhome`, the `uistatus`/`chrome` additions and the
  PIN;
- `tools/probe_board_abi.py` — the §2.2 re-pin;
- `tools/test_probe_board_abi.py` — only a dependent size assertion, if one exists;
- the report and evidence (§5).

**OUT:**
- `lib/`, the simulator and `platformio.ini`;
- `src/fw_main.cpp`, `src/firmware_commands.cpp`, `variants/…/board_ui.cpp`, the bitmap and font assets, and every
  other `src/` file;
- `test/test_firmware_ui_invite.cpp` and every other test — a census input only; no invitation policy change;
- `tools/probe_board_ui/*` (B418 is W2's) and every other tool;
- every existing battery's meaning outside the ledger;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7);
- the W1c, W3 and W4a candidates' other files — preserve them byte-identical.

## 4. Gates

The owner ruling of 2026-09-16 (P6) gives the coder the full chain, and QA the `src`-only independent gate.

**Ordering:** the two runs of each board pair are back-to-back, and nothing runs between or during them. Mutation
batteries run separately from board measurements. No `--no-neg` result substitutes for a control gate.

### 4.1 The coder's gate

**Baselines, on the unmodified tree, before stage A:**
- **Firmware-UI probe:** default mode, all arms, with an independent label-completeness count (pre-check: 529 / 980 /
  529, 236 / 0).
- **Board-UI supplemental:** `tools/probe_board_ui/run.sh --no-neg`. Record its exact failure set (expected: B418's
  W49/W51/W54).
- **Supplemental layouts:** the pre-check's `layout-measure.py` **baseline** variant, plus W4a's
  `TeamRow` / `InviteIdRows` / `OutcomeView` measurement.
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w4b/base-1`, then `base-2`. Then the
  stock `compare` per env on the two `manifest.json` files; both must PASS.

**Stage A (§2.8)**, recorded and frozen.

**Stage B, then the full chain:**
1. **Hygiene:** `git diff --check`. The ledger is complete against the stage A → final diff.
2. **Native:** a fresh `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts
   derived: 0 failed, 0 skipped. `PIN re-synced? YES — <derivation>`.
3. **Corpus:**
   - a fresh stock `lus`: `cmake -S <simulator> -B <new dir outside both repos> -DCMAKE_BUILD_TYPE=Release
     -DMESHROUTE_DIR=<MeshRoute>`, then `cmake --build <dir> --target lus`;
   - `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest.
4. **ABI:**
   - the stock `python3 -B tools/probe_board_abi.py` (full, controls on) at the re-pinned §2.2 sizes;
   - the supplemental measurement: the new `HomeCapture` is 9 / align 1; `TeamRow`, `InviteIdRows`, `OutcomeView`,
     `FrameGate` and `SettingsView` are unchanged;
   - report the two instruments separately.
5. **Probes:**
   - **Firmware-UI, default mode, all arms:** green, with the **stage-A completeness guard** reporting every declared
     label exactly once and 0 unusable. Report per-arm counts against the baseline and stage A, and reconcile the
     label ledger (added, retired, kept).
   - **Firmware-UI `--no-neg`:** diagnostic only, now saying so itself.
   - **Board-UI supplemental:** a failure set identical to the baseline.
6. **Tools discovery** (D5): `python3 -m unittest discover -s tools -p "test_*.py"`, including the new test. Report the
   count derived, OK, 0 skipped.
7. **Warning census:** `tools/warning_census.sh` on its six pinned OLED envs — the stated exception to the two-env
   rule. Zero new warnings, `-Wswitch` 0, pins unchanged.
8. **Boards, final.** Run `.pio-measure/w4b/final-1` and `final-2`, then the stock `compare` per env; both must PASS.
   - **Attribution.** Read the `base-1` and `final-1` manifests field by field: every `measurements.*` and payload
     difference is attributed, down to symbol level for RAM.
   - **Compatibility.** `source.*` differences are expected. `toolchain.*`, `fixed_identity.*` and `paths.*` must be
     identical.
   - **Predictions (§2.11):** `gateway` payload identical.
9. **Mutation union:**
   - **Batteries:**
     - (a) `model` 239, `sliceCbudget` 1, `w4aident` 9, `chrome` 44, `uistatus` 13 and the new `w4bhome`, with the
       ledger's dispositions and additions;
     - (b) `config` 32, `uiprov` 45, `uijoin` 26, `uiinvite` 32, `uiteam` 20, `uisend` 15, `sliceCsend` 1, `uipresets`
       32, `uinearby` 11, `uinearbyrow` 9, `joinprofiles` 21, `provservice` 10, `uigeo` 18 and `icons` 11 (pre-check §8).
   - **Expect** every configured entry RED, 0 unusable, 0 vacuous. Every worker's clean baseline equals step 2's counts.
   - **A non-RED entry** is reported with its capture; QA disposes of it, and it is never silently accepted.

**Reader audit (D6/P7), before the freeze.** Grep `lib/`, `src/`, `test/` and `tools/` for every reader of the edited
statements and changed symbols, including the directory scanners (`probe_features/ownership.py`,
`check_data_type_literals.py`) and the board-UI structural readers. Rerun any check whose predicate the edits touch.

**Input stability:** inputs are recorded before stage A and after the last step. They differ only by the fenced edits
and new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate (P6, `src`-only)

On the frozen tree, QA re-runs:
- **Native** — a fresh `pio test -e native`, then the binary;
- **Corpus** — 36/36 byte-identical;
- **Boards** — its own `pair --jobs=1` into a fresh `.pio-measure/` directory, read field by field against the coder's
  `final-1`;
- **Selector (a)** — the touched batteries, including `w4bhome`;
- **The affected probes:**
  - firmware-UI (all arms, controls), with an independent label-completeness reconciliation;
  - the stock ABI probe plus the supplemental layout measurement;
  - the new `tools/test_probe_firmware_ui.py`;
  - the board-UI supplemental `tools/probe_board_ui/run.sh --no-neg`, with a failure set identical to the baseline
    (B418's W49/W51/W54 only).

The census, the full union and full discovery stay in the coder's chain.

### 4.3 STOP conditions

- **Allocation:** retained state beyond §2.2, or any size or alignment other than §2.2's — the owner-ruled allocation.
- **Ledger:** any changed, retired or added expectation, control or mutation absent from the §2.9 ledger, or any
  weakened.
- **Blank and emergency:** re-homing on blank; losing ordinary focus through an emergency; removing or altering an OQ-3
  cancellation.
- **Capability:** a capability bypassed to make a fixture pass.
- **Fence:** an edit outside §3.
- **Stage A:** its completeness guard not load-bearing, or any stage-A label unaccounted for.
- **Controls:** C0 building; a new or re-anchored control or entry that is vacuous, does not compile, crashes, or goes
  RED only on unrelated checks.
- **Census:** a warning-census failure.
- **Boards:** a failed repeatability `compare`, an unattributed delta, a `gateway` image change, or a RAM increase that
  the §2.2 structures do not explain.
- **Corpus:** any stream delta.
- **Inputs:** a mismatched input at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false, or a transition the design r2.21 does not settle — STOP-1 to QA; never a
  coder assumption.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b/`. It holds:
- **Board manifests:** the eight per-env manifests, copied from `.pio-measure/w4b/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **Stage A:** its results, hashes and completeness receipt;
- **The ledger:** the §2.9 disposition ledger;
- **Measurements:** the supplemental layout driver, generated TU and results;
- **Durable receipts:** compact native, ABI, probe-mode, discovery, census and union summaries; the full corpus manifest
  and field comparison; the locations and hashes of the raw logs;
- **The reader-audit greps;**
- **A `SHA256SUMS`** covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md):
- raw logs go under ignored `artifacts/2026-09-27-standalone-mobile-home-w4b/`;
- raw board outputs stay under `.pio-measure/w4b/`, and the stock tool's `.pio-measure/env/` roots are not relocated;
- native builds use `.pio/`;
- simulator and mutation scratch builds stay outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baselines**, then stage A.
3. **Diff:** `git diff --stat` and the ledger.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Allocation:** the §2.2 sizes on three ABIs, the static sum, and the linked-RAM attribution.
6. **Controls and mutations:** the label reconciliation; the union by battery.
7. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
9. **Not run,** with reasons. Each reason is that the probe's predicates are unaffected — confirmed by the reader
   audit — never "it reads none of the files".

## 6. Owner rulings

- **The allocation.** The owner ruled on 2026-09-27: +72 B of static UI structures on the OLED boards (+64 host), for
  the full six-item capture shape (design r2.21 §11.1).
- **Nothing else is requested.** The transitions the pre-check listed are settled in design r2.21 within D1/D2/D11. The
  two blank-time cancellations are the existing UI-16 OQ-3 owner ruling, kept.
- **A contradictory interpretation** returns to the owner.

## 7. Landing (QA, on PASS)

- **Register:** **B456 CLOSED** (the guard and its regressions independently gated); the §0 dispatch rewritten in place.
- **Design:** the §13 W4b status.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **Metal plan (M2)** — pre-check §10's proposals, adapted at landing:
  1. **UI-01, step 2:** boot selects Home's first item; each top-level list's `MENU` enters menu mode on the first rail
     slot; the x10–11 cue shows only in menu mode; exactly one x0–9 box; no body arrow in previews; no 24×24 mark on
     Home.
  2. **UI-01, step 4:** dark, consumed-wake and message-wake behaviour from both focus states on a non-first item keeps
     the same screen, focus and item. The OQ-3 confirmation cancellation is exercised separately.
  3. **UI-04/06:** Home-entry CREATE/JOIN on the real session, BACK first; ID and key arrivals change Home's options
     without selecting a different action.
  4. **UI-19:** a real no-team OLED gateway's Home is `INBOX / MY DEVICE / MENU`.
  5. **A new Home/My-device glass row:**
     - after `cfg set name ABCDEFGHIJKLMNOPQRSTUVWXYZ123456` (`> cfg ok name (saved to /mrid)`), Home reads
       `ME ABCDEFGHIJKLMNO»` and My device reads `ABCDEFGHIJKLMNOPQRS` / `TUVWXYZ123456` / `ID 0x<HASH8>` / position /
       `>BACK`;
     - a UTF-8 and an unnamed fixture give sanitized cells, `ME 0x<HASH8>` and `NO NAME SET`;
     - a reboot-required fixture shows the two-row list window.
- **Next:** W5 (splash), W9 (Home card), W6 → W7 → W8, or the independent W0/W1b/W2/W4c, per the owner's order.

## 8. Revision history

**Revision 1 (2026-09-27)** is the first draft, built on the W4b pre-check.
- It adopts the pre-check's one-brief packaging, with B456 first.
- It pins the owner-ruled +72 B shape.
- It adopts the transitions settled in design r2.21: the rule-7 OQ-3 exceptions, the Settings preview and `MENU` row,
  capability gating, the `OPTIONS CHANGED` table, the interim Send and Inbox content, and the blocked-note rows.
- It defines the closed disposition ledger.

**Revision 2 (2026-09-27)** folds in the brief review (HOLD, W4B-1–W4B-4):
- **W4B-1:** the typed origin's table now includes the PROVISION menu's own BACK (`close_provisioning()`). The
  Home-origin return after an OQ-3 saved-key blank cancellation is pinned as a native trace, with its Settings-origin
  counterpart, and fresh actions from that menu keep the origin (§2.5).
- **W4B-2:** every nearby renderer control has a named disposition and must-fail check (§2.9):
  - C123 is retired with C121–C122;
  - C108 and C124 are re-anchored, with literal geometry if their helpers go;
  - C102–C104 are kept for the position publisher;
  - C35, C84, C92–C95 and C99 are re-anchored with their properties;
  - the W41 reader's box statement is kept verbatim;
  - QA's gate adds the board-UI supplemental run (§4.2).
- **W4B-3:** a new §2.10 gives the own-name publication, frozen pages and press-free body refresh to the real
  firmware-UI probe, with three wiring controls; the native matrix keeps projection only.
- **W4B-4:** design §11.1's closing sentence now names the W4b grant, and the design pin is refreshed.
