# Standalone Home W4b — QA pre-check, source ledger and baselines

Date: 2026-09-27. Role: independent QA; Claude authors the brief; a separate coder implements it.

**Pre-check complete.** The current candidate reproduces its baselines. W4b can be one feature brief with a B456 instrument-first stage. This is neither an approved implementation brief nor a W4b implementation gate. The author must settle the transition details listed below and put the measured allocation through the owner’s capacity/RAM ruling step. Design §11.1 explicitly grants no allocation for this work.

Only this report and its [evidence directory](2026-09-27-standalone-mobile-home-w4b-precheck/) are added. Production, tests, tools, design, register, metal plan, tracker and MEMORY are unchanged. No staging, commit, reset or clean. Simulator sources remain read-only. Raw logs are retained in ignored `artifacts/2026-09-27-standalone-mobile-home-w4b-precheck/`; simulator builds, streams and counterfactual layout headers are outside both repositories.

## 1. Base, preservation and preparation inventory — Q1

| Input | Independently checked state |
| --- | --- |
| MeshRoute HEAD | `8360802904f7bd0023279d3da844453d61207ede` |
| Simulator HEAD | `6585649ea5a780f0542b2931853a667be56a5b2b`; clean |
| Index | No staged changes |
| Working input inventory | 1,576 MeshRoute paths and 285 simulator paths, including non-ignored untracked files and symlink targets |
| W4a QA freeze reconciliation | All prior inputs equal the QA freeze plus exactly its five documented landing changes; 28 added W4a QA report/evidence paths; no other difference |
| Prior evidence checksum verification | W1c coder 86/86, W1c QA 29/29, W3 coder 31/31, W3 QA 29/29, W4a coder 44/44, W4a QA 26/26 |

The five prior landing changes are `MEMORY.md`, the open register, current metal plan, standalone Home design and `tracker.md`. They are present-day preparation inputs, not unexpected implementation changes. The design is r2.20; the W1c, W3 and W4a candidates are already inside this inventory. Older overlapping source hashes are historical: W3 legitimately changed the W1c tree and W4a legitimately changed W3. Do not reset to any older receipt or use HEAD alone as the brief’s base.

[inputs.json](2026-09-27-standalone-mobile-home-w4b-precheck/inputs.json) is the full commit-plus-content base; [preflight.json](2026-09-27-standalone-mobile-home-w4b-precheck/preflight.json) records the reconciliation and all six checksum-index hashes. The new brief and any author preparation edits must be listed as subsequent, explicitly permitted deltas. Commits are not prerequisites. Reconcile the inventory again at coder start; a new owner commit changes the reference hash, not the requirement to include the actual working inputs.

## 2. Current navigation and rendering — Q2

These are source facts on this inventory, not inferred from the final design. Line references are navigation aids; symbols and hashes are the pins.

| Source seam | Current behavior | W4b obligation |
| --- | --- | --- |
| `firmware_ui_model.h:246`, `Screen`; `next_screen`:5572 | `status, team, inbox, send, settings`; non-team builds skip TEAM/SEND without packing their rail slots | Keep enum names/order and feature skipping. `status` becomes Home. Short advances screens only in menu mode |
| `UiState`:2447, `ListView`:278, `screen_is_entered`:377 | Boot: STATUS, cursor 0, passive. Only TEAM/INBOX use `ListView`; Settings entry comes from `Settings`; STATUS/SEND are not entered lists | Boot Home in list focus. One focus authority must cover all five top-level lists |
| `advance_or_next`:3907, `list_len`:5560 | Passive screens have length 1. Entered TEAM/INBOX wrap through records plus BACK. Settings walking beyond its last row closes the menu on the same screen | List short wraps, including Settings. Explicit MENU enters menu mode on Home, cursor reset for a later entry |
| `list_row_kind`:293, `list_activate`:324, `list_note_kind`:346 | One shared record/BACK resolver; lost-pick refusal outranks exit; passive entry never activates a record | Retain identity/refusal protections. Change only the top-level exit destination/word to MENU |
| `list_view_reset_on_leave`:363, `list_follow_screen`:4107 | Entered state resets to passive outside TEAM/INBOX | Cannot retain this predicate unchanged if `ListView` becomes global focus. Keep one explicit transition authority, not two disagreeing flags |
| `activate`:3940 | TEAM/INBOX double enters, then selects. SEND double opens channel compose. Settings double opens browsing only over an open service | Add Home entry/selection and menu-mode dispatch; distinguish the new Send top-level list from DM compose |
| `settings_follow_screen`:4049; `sync_settings`:4111 | Leaving SETTINGS closes editing/provisioning context; arrival calls the repeat-safe opener even while Settings is closed; browsing follows `CfgRow` identity | Preserve the service/draft and close-on-leave rules. Home-origin setup still lives on SETTINGS |
| `settings_rows`:652; `settings_row_label`:690; `settings_activate`:4255 | Last row `CfgRow::back` says BACK; `on_back()` does not save; close resets cursor | Last top-level row says MENU and exits to Home/menu mode. Sub-view BACK rows remain BACK |
| `draw_settings_screen`, `firmware_ui.cpp:2144` | Closed view contains `>ENTER SETTINGS`, badge/remedy tail; unavailable service displays `CFG UNAVAILABLE` on row 2 | Menu preview must lose its body `>` to satisfy §6.1.4. This is an intentional byte change, not a byte-identical preview |
| `draw_status_screen`:1415 | Mark at x12/y12, 24×24; first three text rows x40 with 14 cells; team ID, own team-local ID, known/key note; wide unread/home-age and location/restart rows | Replace body with Home at x12/19 cells. Remove mark from this body, move position to My device; keep strip home age |
| `draw_rail`:1354; `ui_nav_slot`, `firmware_ui_chrome.h:325` | Five fixed y positions 10/20/30/40/50, box x0/w10/h10; compose→SEND, detail→INBOX, Settings sub-views→SETTINGS; emergency suppresses rail | Keep mapping precedence, fixed holes and exactly one box. Add menu cue x10/w2/h10 beside selected slot. My device/help map to STATUS; blocked setup note maps to SETTINGS |
| `ui_chrome`:536, `ui_chrome_equal`:464; tick `firmware_ui.cpp:2573` | Frozen visible projection; fieldwise equality, no struct `memcmp`; token change invalidates without simulating input | Include any new visible focus cue in the projection/equality; preserve freeze and deadline behavior |
| `on_gesture`:2758; `on_tick`:2845; `unblank`:5377; `on_msg_wake`:3235 | First short/double on dark panel only wakes; long gestures pre-empt wake handling. Ordinary blank preserves screen/selection. Wake restarts detail cadence, not its page. R-7 wake merely unblanks | Preserve timing, consumed wake press, admitted push kinds and retained ordinary focus. Do not implement a blank-to-Home policy |
| `emergency_gesture`:5589 | Closes detail, closes provisioning on long gestures, exits Settings editing; committed alarm closes compose. `close_compose`:5539 also unconditionally resets cursor | Preserve safety pre-emption and result rules. Explicitly preserve the new ordinary list/menu focus and Home item; today’s unconditional cursor reset is not proof of that requirement |
| `FrameGate::step`:5720 / `on_page`:5766 | MAC-idle and complete-frame gates; freezes arrival serials when INBOX is unobscured. It does **not** test `ListView`, so a passive preview also marks frozen arrivals read | Keep exactly this scope for the menu preview. Never acknowledge only the visible rows or use stored-message sequence as the watermark |

The status strip’s mail/home/people/key/duty/battery tokens and badge priorities are unchanged. `ui_fmt_mail` (`chrome.h:156`) uses exact 0–99 then `99+`; `ui_fmt_team`:195 uses 0–9 then `9+`, with `--` for no configured team. Home can reuse the tokens, not the former STATUS sentences.

**Blanking exception the brief must address explicitly.** `on_tick`:3007 cancels an uncommitted grant confirmation through `leave_grant_chain(resume)`. With `GrantOrigin::team_roster`, that returns from SETTINGS to TEAM (`return_to_team_roster`:5201). At :3016 a saved-key offer also closes to the PROVISION menu. Existing mutations V34 and the roster-return controls protect these safety exits. Thus “all current blank behavior is unchanged” and a literal “every sub-view’s screen/arrow survives blank” are not simultaneously true. Recommend specifying §6.1.7 for ordinary top-level focus and retaining the existing hazardous-confirmation cancellation contract; the author must reconcile its wording with the owner’s rule. If the intended interpretation instead changes those safety exits, it needs an explicit decision, not a coder assumption.

The exhaustive line-level reader index is [symbol-readers.tsv](2026-09-27-standalone-mobile-home-w4b-precheck/symbol-readers.tsv). It includes comments and mutation strings and is a textual census, not a claim that every hit is executable. Actual production decisions are in the model, renderer, chrome, status and team helpers. Native UI consumers are model/chrome/status/team/send/invite tests. Tool readers include the UI runner/probe, board-UI structural runner, ABI probe/self-tests and mutation harness. Incidental `on_page`/`Screen` hits in non-UI tests are not W4b consumers. The feature ownership scanner additionally walks all production headers without naming these symbols; it must remain clean.

## 3. Minimal state and measured layout alternatives — Q3

Recommendation: reuse `UiState::list_view` for top-level list/menu focus and `UiState::screen` for the selected rail screen. Keep the old enum spelling if useful; do not add a second rail index. Freeze them with the existing frame state. The five Home lists have fixed order and at most six items, so their **profile enum plus selected item identity** is a complete list capture; six retained item bytes are not necessary.

One concrete **structural pricing candidate**, not implementation:

- `UiState`: three-byte Home capture `{profile, selected HomeItem, changed latch}`, plus one-byte Home sub-view `{list, my_device, key_help, setup_block}`. Existing cursor derives the visible index from identity. Existing `ProvBlock` can gain an unavailable enumerator without growing its byte; preserve the blocked reason rather than re-reading it at paint time.
- `UiModel`: one-byte Home return item and one-byte typed setup origin `{none, home, settings}` beside the existing return context. Do not reuse `GrantOrigin`: an invitation opened by Home can still have its own invitation-versus-roster grant origin.
- `UiSnapshot`: raw stored own name `[32]` plus counted length. No resident formatted copy or second name owner.
- `UiChrome`: derived menu cue bool, in existing padding, for frozen drawing and fieldwise invalidation. It is a projection of the model, not another focus authority.

The [layout results](2026-09-27-standalone-mobile-home-w4b-precheck/layout-pricing.json) used the stock probe’s real compilers/flags on modified **scratch headers outside the repositories**. They measured declarations and padding only: no behavior, linked RAM, stack peak or board image was inferred. The retained [measurement driver](2026-09-27-standalone-mobile-home-w4b-precheck/layout-measure.py) accepts `--output /tmp/<result>.json` when run from the repository root. Its portable replay reproduced all sizes/alignments for all five variants on all three ABIs; [measurement-audit.json](2026-09-27-standalone-mobile-home-w4b-precheck/measurement-audit.json) records the comparison and instance arithmetic.

| Type | Current native / mobile / gateway ABI | Compact-profile candidate | Full six-item capture candidate |
| --- | --- | --- | --- |
| `UiState` | 504 / 504 / 504 | 512 / 512 / 512 | 520 / 520 / 520 |
| `UiSnapshot` | 1336 / 1336 / 1336 | 1368 / 1368 / 1368 | 1368 / 1368 / 1368 |
| `UiModel` | 928 / 912 / 912 | 936 / 928 / 928 | 944 / 936 / 936 |
| `UiChrome` | 20 / 20 / 20 | unchanged | unchanged |
| New Home capture alone | absent | 3 / 3 / 3 | 9 / 9 / 9 |

Alignment remains 8 for state/snapshot/model and 2 for chrome. The compact capture’s fourth sub-view byte is outside the three-byte helper. The own-name addition is 33 logical bytes but costs 32 in this placement because existing tail padding is absorbed. Appending two private model bytes is **not** a +2 `sizeof(UiModel)` promise: board padding makes the model delta +16, including its enlarged state, versus +8 native.

`firmware_ui.cpp` has one `s_model` (:191), one `s_frame_state`, one `s_frame_snap`, and one `s_frame_chrome` (:390–401); the live snapshot is also a tick local (:2530). Counting model-contained state only once, the candidate’s static symbol-size sum is **+48 native / +56 on each measured board ABI**. The full-array candidate costs +64/+72. Both add 32 bytes to the snapshot’s structural stack footprint; this is not measured peak stack. The ordinary `gateway` image does not compile the OLED TU, so its linked RAM/image are predicted unchanged even though the gateway compiler can measure these types. OLED gateway profiles do instantiate the UI; do not confuse the two.

Stock ABI pins state/snapshot/model/chrome/InviteMember. Supplemental current measurements also establish `FrameGate=28`, `SettingsView=8`, `TeamRow=40`, `InviteIdRows=26`, `OutcomeView=52` on all three ABIs. Any new named capture/row type must join the brief’s pricing, even if the stock probe does not yet name it. Native resource assertions at `test_firmware_ui_model.cpp:10398` onward and the stock ABI pin table must be re-synced from final measurements; keep unrelated ComposeSlot/List, Node and offsets unchanged.

**Allocation is an owner item.** Design §11.1’s ≈12×2 +2 +36 is an estimate, expressly not an allocation grant; roles §7 reserves capacity/RAM rulings to the owner. The compact shape is a smaller, measured input to that ruling, not permission to spend an unbounded budget. The author should request/name the selected shape and structural bound; the coder must report its actual ABI and linked board delta and stop on additional retained state. No board measurement was run in this pre-check.

## 4. Home content and predicates — Q4

| Content or decision | Existing source | Missing work / qualification |
| --- | --- | --- |
| Team plane exists | `build_snapshot`:732 → `team_build=(MR_FEAT_TEAM!=0)` | Use this, not runtime `is_mobile`, to choose the gateway list |
| Team configured | :849 → `g_node.config().team_id`; `team_id!=0` | Static-role team-capable nodes with no membership get JOIN/CREATE |
| Content key held | :902 → `team_content_key_present()` | Missing key outranks pending local ID |
| Local ID ready | :848 → `team_local_id()` → `my_team_id` | Zero means pending; ready Home list requires nonzero ID plus key |
| Restart needed | `freeze_settings()` → `SettingsView::reboot`; `ChromeCfg::from` also reads `reboot_required()` | No new NV read. Don’t infer reboot solely from the badge: conflict/unsaved outrank that badge while reboot can still be true |
| Session unread | :727 counters publish `unread_dm/ch`; chrome sums/caps | `INBOX` at zero, otherwise `INBOX <token> NEW`; not inbox stored total |
| Known routes | :771–773 `rt_team_count()` → `team_total`, distinct from eight displayed rows | `TEAM <token> KNOWN`, using `9+`; not “online members” |
| Own identity/position | :907 key hash; :931–934 configured coordinates/fix | Own counted name is the one missing data publication |
| Available actions | :744–767 `prov_create_team`, `prov_join_team`, `prov_invite` | Production OLED team profiles are single-layer. The synthetic l2 UI arm has team support but disables child provisioning; do not mistake it for a real no-team gateway profile |

The author’s five lists are implementable with one pure profile selector and one pure item resolver: no-team; key-missing; key-held/ID-pending; ready; no-team-plane. Capture the profile and selected item, then resolve index by item identity. Count/age/name changes repaint labels without changing identity. Missing current item moves selection to item 1 and raises the changed latch; surviving items keep their identity even if their row shifts. Returning from a Home-owned sub-view restores its opener if still present, else item 1.

Use `firmware_ui_status.h` for the new pure Home/My-device text formatting beside existing status/location helpers, and the model header for item/focus decisions. No new TU, bitmap, screen-enum rename or core carrier is needed. Existing `ui_status_team`/`ui_status_me` are **not** the combined team sentence: today they make two rows. Compose the new row from the full eight-digit uppercase team ID and local-ID fact, with 18-cell `TEAM 12A1B2C3 T220`, 19-cell `TEAM 12A1B2C3 NO ID`, `NO TEAM`, or blank for no team plane. `ui_status_known` also cannot simply be reused: its no-key sentence and old standalone count belong to the removed STATUS layout, while Home has a distinct HELP item and TEAM count label.

Home row 0 is 3 + 16 = 19 cells. Ordinary list rows must reserve one marker cell: `NO TEAM KEY - HELP` is 18 cells, 19 including its `>`; the longest configured team sentence fits 19. Existing `list_first` (`cpp:1124`) already accepts visible row count: use three rows at body rows 2–4, two at 3–4 when restart occupies row 2. Reserve the geometry seam for W9’s card; do not allocate card state or blank out its future rows now. The eventual restart-plus-card priority belongs in W9’s brief.

**Author details still needed:** whether a short that dismisses `OPTIONS CHANGED` also advances, and whether a refused double consumes/clears the latch. §6.4 says “until the next press” and “next double is refused”; implement one explicit truth table, including disappear→reappear before acknowledgement and off-screen return, rather than importing the TEAM idiom by analogy. Also state the safe behavior of Home actions in the synthetic `team_build && !prov_*` probe shape; production capability checks must not be bypassed to make that fixture pass.

## 5. My device and own-name publication — Q5

`Node::effective_name(out,cap)` (`node.h:576`, W1c candidate) now returns a counted copy of the stored name, possibly empty; it never terminates. Use it once in `build_snapshot`, with capacity 32, and store its returned length. There is no `NodeConfig::name` source for this identity. `cfg set name` publishes a successful `/mrid` save into `g_node.set_name` (`firmware_config.cpp:274–283`); no NV access belongs in the UI refresh or draw path.

- Home: pass full counted name and `my_key_hash32` through W4a `ui_fmt_identity` at 16 cells, then prefix `ME `. Empty name with a known hash gives whole `0x<HASH8>`; no pre-clipping. The formatter explicitly returns `IdentityFmt::none` for empty name plus zero hash (`model.h:1487`), so name/hash fixtures must supply the intended identity; the brief should state the unavailable-identity outcome rather than assuming every formatter call succeeds or inventing a default name.
- My device: sanitize each raw byte with `ui_display_byte`; copy bytes 0–18 to row 0 and 19–31 to row 1, terminating both temporary rows. Do not use a 19-cell abbreviation and then split that shortened string. Unnamed is `NO NAME SET` plus a blank second row.
- Row 2 is `ID ` plus the existing full uppercase `0x<HASH8>` formatter. Row 3 reuses `ui_status_location` with restart override false: configured E7 coordinates, truncation toward zero to three decimals, correct negative-zero sign, `(0,0)`→`NO LOCATION`. One coordinate zero is a valid fix (`ui_status_have_fix`, `status.h:180`). Row 4 is `>BACK`; no CHANGE NAME action yet.
- Freeze through the existing `s_frame_snap`; a rename between the eight OLED pages may affect only the next frame. Add positive invalidation for visible own-name, full team-ID/local-ID and own-position changes. Today tick invalidates chrome and TEAM rows only (`cpp:2573–2585`); changing a name or a team ID can leave all strip tokens equal. Merely adding snapshot fields would leave a lit Home/My-device body stale.

No console/JSON/name-storage semantics change; the sanitized output remains panel-only. W0’s name-save faults and W7’s editor are not part of this work.

## 6. Settings admission and typed setup returns — Q6

`ensure_config_open` (`model.h:4276`) calls `open()` only when an attached service is closed. `provision_admit`:4286 calls it, refuses absent/still-closed service, then conflict before unsaved. It does not save. The unavailable branch currently has no `ProvBlock` reason because Settings browsing cannot reach it. Home is the first real UI caller that can do so; it must turn that refusal into the explicit unavailable note.

Run the shared admission **while activating the Home item**, before a generic SETTINGS arrival can mask the first closed-service call. On failure, record the typed reason and enter a SETTINGS-owned note: `SAVE OR DISCARD` / `IN SETTINGS`, `RELOAD OR DISCARD` / `IN SETTINGS`, or `CFG UNAVAILABLE` alone. Either press returns to the Home opener. A later successful service retry must not auto-resume setup or alter the frozen reason. The author must pin exact note rows. No name prompt in W4b.

Native W3 cases (`test_firmware_ui_model.cpp:4253–4400`) count loads/writes/applies. Failed `open` retries on each later Settings sync; there is no attempted-once latch. Preserve that behavior, and test Home admission with no adapter, failed open, recovered open, clean/unsaved/conflict/both flags. Assert zero saves/applies on refusal and no extra load once open. Existing `ui15-gate` :4223 proves only that the old Settings path cannot enter browsing with no usable config; it does not prove Home admission.

Home CREATE reuses the safe BACK-first create confirmation; Home JOIN reuses the existing frozen NEARBY load and confirmation. Home INVITE bypasses settings admission but retains exactly `load_invite(s) → enter_provision(invite) → request_team_announcement()` (`model.h:4498–4500`), once per fresh window. Do not bypass capability/identity checks deeper in that flow or refresh its snapshot per tick.

Typed setup origin must survive `enter_provision`’s transitions through confirmations, waiting, results, saved-key offers and remedies. It must retire when the flow genuinely leaves SETTINGS, is pre-empted, or a new caller starts. `GrantOrigin` remains separate and unchanged: roster grants still restore the selected team member; invitation grants still resume/close their own window.

Explicit current return-site checklist:

| Site in `firmware_ui_model.h` | Meaning the origin-aware exit must preserve |
| --- | --- |
| :4392, :4404 | Leave JOINING without cancelling persisted join; acknowledge ordinary join result without rerunning it |
| :4532, :4576, :4669 | BACK from create confirmation, static profile list, nearby list |
| :4843, :4858 | Terminal create/join acknowledgement after special cases; decline saved-key offer |
| :4919, :5024 | BACK from saved keys / invitation list |
| :5183–5195 | Grant terminal exit versus resume-window versus roster return; only exits that currently resolve to PROVISION menu become Home for Home origin |
| :4754, :4783, :4813–4838 | Keep special acknowledged key-received landing, full-keyring remedy and explicit saved-key offer; these are not generic menu returns |
| :3016, :3057 | Saved-key blank cancellation and no-origin cached-key edge: state their interaction with Home origin; never turn a radio arrival into unsolicited Home navigation |
| :4357, :4452, :4511, :5627 | Close, invalid/closed arm, PROVISION BACK and alarm pre-emption: clear the correct contexts and preserve the existing safety contract |

For `origin=none`, use the ruled PROVISION-menu fallback, not a guessed Home caller. Do not mechanically replace every `enter_provision(menu)` with Home: Settings entry :4232 is an **entry**, and :3057 can run on a push. Home’s return item is distinct from the current profile, so a successful CREATE that removes its opener lands on item 1.

## 7. Other screens and deliberate byte changes — Q7

TEAM and INBOX retain their current rows, ordering, identity guards and preview bytes. Only their entered exit row becomes MENU, with the new destination. Empty lists still need an operable MENU row. DM compose keeps `back, don't send`, grant action semantics and its own opener; detail keeps its existing actions/pager; result acknowledgement remains either-press.

SEND needs an explicit classification in the brief. Today it is a passive instruction screen and double opens `Compose::channel`; W4b makes the channel phrase selection the top-level Send list, ending in MENU. Preserve the current preset ordering, location markers, generation check, live-catalog send path, confirmation/send/result semantics, and DM compose behavior. Specify where a channel result acknowledgement and a catalog-change close return under the new navigation. They cannot both be called “unchanged exits” while retaining the old passive SEND destination. Reuse the existing compose logic; do not create a second send engine.

There are three intentional preview differences/qualifications:

1. Home has a new body; its menu preview is that body without a selection arrow, not the removed STATUS mark layout.
2. Settings preview loses `>` before `ENTER SETTINGS`; its badge/remedy/unavailable behavior remains.
3. Send’s passive instruction bytes may stay, but double now enters the top-level phrase list. TEAM/INBOX preview bytes stay; the gutter cue is additional chrome.

The final design’s “newest message” and `WRITE MESSAGE` entries depend on W4c/W4d and W8. W4b must explicitly use today’s Inbox order and current phrase-only Send content. No editor, name prompt, splash, Home card, record widening or new storage belongs here.

## 8. Instruments, expectation ledger and mutations — Q8

### Existing witnesses and authorized expectation classes

| Instrument | Existing pins | Intentional changes versus invariants |
| --- | --- | --- |
| `test_firmware_ui_model.cpp` | `ui-model`, `b232-*`, `ui17-*`, `ui-frame`, `ui15-*`, `w3-open/prov`, invite/roster return and resource cases | Change boot/focus/top-level walk and exit destinations, Settings wrap and Home-origin branches. Keep lost-record/member refusal, safe confirmations, result delivery, R-7, unread and detail cadence semantics |
| `test_firmware_ui_chrome.cpp` | screen/modal→rail mapping, slot mask, badge and invalidation | Add focus cue equality/projection and new sub-views; unchanged strip tokens, priority and emergency suppression |
| `test_firmware_ui_status.cpp` | old five STATUS rows, gateway silence, 14/19-cell budgets; coordinate cases | Replace removed row expectations with Home/profile/My-device facts. Preserve coordinate/fix boundary cases and no fabricated team/ID |
| `test_firmware_ui_team.cpp`, `test_firmware_ui_send.cpp` | real-gesture helpers walk STATUS→TEAM/INBOX, rail and outcome paths | Adapt fixture navigation to the new state machine; do not weaken existing row/send/tracker assertions. `invite.cpp` has state/identity layout consumers but no new invitation policy is authorized |
| Firmware-UI probe P6, P7/P8, P13, P14, P17/P18, P20+ | passive/entered lists and BACK; config badges/restart; frozen strip; rail boxes; STATUS mark/rows; origin-specific flows | Rewrite only the listed navigation/STATUS expectations. W4a name bytes, W3 paging/compose width, all send/grant effects and unaffected sub-view text remain pinned |
| Board-UI probe | canvas geometry, structural ownership, wiring W35–43 | New cue needs no board API. W42 ordinary x12/emergency x0 and W43 body/detail width remain. B418 W49/W51/W54 stay a known supplemental failure set, outside this fence |

`probe_main.cpp` helpers at :1253–1330 assume old navigation, including `leave_list` finding `>BACK`. They and fixture entry sequences must be fenced explicitly. The probe’s `rail_boxed_slot`, rectangle census and body-origin readers (:385–483) must distinguish the new x10/w2 gutter rectangle from the x0/w10 selection box; do not relax an exactly-one-box assertion to “at least one rectangle”. Old STATUS x40 allowances and mark counts must be removed from ordinary Home expectations, while strip geometry, 19-column bound and emergency full-width exception stay exact.

[native-cases.tsv](2026-09-27-standalone-mobile-home-w4b-precheck/native-cases.tsv) indexes current UI case titles/locations. [control-completeness.tsv](2026-09-27-standalone-mobile-home-w4b-precheck/control-completeness.tsv) lists every live renderer control and its independently observed result. The author should make an explicit old→new expectation/control ledger; “update tests to pass” is not a fence.

### Proposed native navigation matrix

Use real `on_gesture`/`on_tick` paths, plus pure decision tests where corrupted/absent origins are otherwise unreachable:

- Five Home profiles × boot/list/menu entry; all item sequences including empty TEAM/INBOX/phrase lists and gateway slot skipping; short wraps, double selects, MENU from each screen lands on Home/menu, double enters item 1.
- Every Home item/opener return, including opener disappearing; every profile transition with surviving/lost item; counts changing through 9/10 and 99/100; changed-latch short/double truth table and no accidental action.
- All five screens × list/menu focus × consumed short/double wake and allowed message wake; no unblank by chrome-only update; millis rollover; no input-clock refresh from repaint. Include new Home sub-views and explicitly retained confirmation safety exceptions.
- Emergency arm/cancel/fire from list/menu and each Home sub-view; ordinary focus restoration, existing pre-empted sub-views stay closed, no queued resend or duplicated act. Results retain their existing presented/acknowledgement conditions.
- Home admission clean/dirty/conflict/both/unavailable/no adapter/recovered; count opens, saves and applies. Home invite with dirty/conflicted draft still opens once and announces once. Settings remains gated. Home/settings/none setup origin through every return-site family in §6; retain independent roster GrantOrigin.
- Full name 0/16/17/19/20/32 bytes and high-byte/UTF-8 sanitizing; Home 16-cell abbreviation, full two-row My-device name, hash/fix/negative-zero cases, gateway blank team line, restart/list-window coexistence.
- Frozen frame: change name/team/profile/fix/focus between pages; current frame remains one snapshot and next frame repaints. Inbox list **and menu preview** commit frozen arrival watermarks only on complete visible frame; Home/detail/compose/emergency and aborted frames do not.
- Current catalog generation changes/no-ops, DM and channel submission/outcome, W3 0/38/39/76/241-byte detail pages and W4a labels remain correct after fixture navigation changes.

### Mutation census and required disposition

Safe AST inspection of `tools/probe_ui_model_mutations.py` was used; the harness was **not imported or run**. All 589 anchors in the suggested union currently match exactly once. The complete selected anchor list with source line, count and original purpose is [mutation-anchors.tsv](2026-09-27-standalone-mobile-home-w4b-precheck/mutation-anchors.tsv).

| Selector | Batteries at this pre-check | Entries |
| --- | --- | --- |
| (a), proposed changed production files | `model` 239, `sliceCbudget` 1, `w4aident` 9 → model header; `chrome` 44; `uistatus` 13 | 306 |
| (b), direct UI/data/action dependencies | `config` 32, `uiprov` 45, `uijoin` 26, `uiinvite` 32, `uiteam` 20, `uisend` 15, `sliceCsend` 1, `uipresets` 32, `uinearby` 11, `uinearbyrow` 9, `joinprofiles` 21, `provservice` 10, `uigeo` 18, `icons` 11 | 283 |

This is a recommendation for the coder’s named union, not an immutable post-feature entry count: new Home/navigation entries and explicit retirement/replacement of withdrawn behavior must be counted. No battery targets `firmware_ui.cpp`; its behavior requires the real renderer probe. Re-census selector (a) if the author chooses a different source fence. QA’s P6 rerun includes all then-touched batteries; the coder owns its full named union and six-environment warning census.

Important anchor families inside the changed decisions:

- Model M97–M105 (B232), M106–M120 (passive/entered lists and exits), M55–57/M59 (admission), M62–69/M71–72/M76/M78 (setup entry/ownership), M83–85/M87–88 (join continuation/push), S01–S03/S05/S07 (modal blank/emergency), V03/B249-1…3 (invitation opening), V34/V40–42/V46/V49/V51–54 (saved-key and result landings), W14–19/W22–27 (grant-origin/roster returns). Exact patterns/locations are indexed, not guessed from the label number.
- M101/M102/M109 intentionally protect the old walk-off/BACK destination. They need an explicit revised contract (Settings short wraps; MENU goes Home/menu), not a silent textual re-anchor that retains a false label. M107/M110–120 retain passive-no-action, containment and stale-pick protections under the new focus model. S03 must still prove committed alarm closes compose while the new ordinary-list cursor is preserved.
- Chrome X17–24/X27–30 cover rail/modal/mask/equality/invalidations; preserve their existing protections and add cue/no-cue controls. New Home body invalidation needs its own witness; strip-only equality is insufficient.
- Status S01/S05–12 refer to old row structure. Replace/retire each explicitly against the corresponding new Home/My-device property; do not retain unused old helpers solely to keep old tests green. S02–04/S13 preserve coordinate/fix semantics. Missing-key precedence now belongs to the Home profile, not a reused old row-2 formatter.
- Renderer C35, C83–84, C90–95, C96–104, C121–122 are the most directly affected old STATUS/list controls. Retire mark-on-STATUS positive controls with that removed behavior; preserve a negative check that Home has no old mark and no narrow x40 rows. C76–82 retain rail ownership/geometry/emergency rules, with a separate gutter predicate. C61–62/C69–75 retain frozen paint/blank behavior. W3-U1/W3-N1 retain the existing Settings unavailable/refusal rows; add separate Home-origin note rows rather than silently retargeting both.
- W4a B241a/B241b, N4/N9/O2/O8, W4a-S1…S6 and W3-D1/D2/C1/C2 are not licenses to weaken identity/pager/compose checks. Navigation fixture changes may move how they are reached; their semantic assertions remain. C0 must remain the intentional build-failure control.

Other tool readers: stock ABI source/pin and `test_probe_board_abi.py`; warning census builds the six OLED TUs; the feature ownership scanner walks these files; board-UI W1–43 reads renderer structure. W49/W54 read `firmware_commands.cpp`, W51 reads `fw_main.cpp`; none is repaired by W4b. Do not include those production files or B418’s tool repair opportunistically. No command inventory line numbers change in the proposed four-file production fence; inventory stays 197 rows.

## 9. B456 instrument-first stage — Q9

The defect is independently confirmed by source, although this fresh run is complete. `run.sh:296–309` already extracts all defined `ctl` labels and checks extraction coverage for B227, but :403–474 increments counters only when `ctl` or a known guard actually executes. The tail at :1833 prints those counters without comparing the declared label set to completed verdicts. A missing helper before `guard && ctl` can therefore skip a control without a recorded failure. Moving `once()` earlier fixed W4a’s specific incident, not this class.

Recommended contract for Stage A, against unchanged product:

1. Build the expected unique label set from every source-defined control, retaining B227’s fail-closed extractor/call-count agreement. Do not derive expectations from whichever calls actually ran, and do not use a magic count of 236 as the authority.
2. Record one terminal verdict per label through one accounting path, including C0’s required-build-failure success. Duplicate, unknown or missing label is failure. A declared label is successful only with its existing accepted outcome; ordinary build failure/crash/no failed check remains unusable.
3. Any guard failure must leave a failing run, including zero/multiple anchor match, failed sed, or undefined guard. Missing-label detection catches failures before `ctl` is entered; count-only equality cannot catch one omitted label plus one duplicate.
4. In `--no-neg`, report explicitly that controls were not run and this is not a gate. Do not classify deliberately absent controls as a full pass. Preserve all existing full-run control meanings and the inert-label/no-eval rule.

Prefer a small **runner-owned selftest mode**, invoking the same expected/observed accounting used by the real runner, with a discoverable tools test invoking it. This keeps shell plumbing testable without compiling 236 mutants for every synthetic case and avoids a second Python-only model of a shell guard. Test complete set; missing first/middle/last; duplicate; unknown; duplicate compensating a missing label at equal total count; zero expected/extraction mismatch; guard nonzero/127 with no verdict; C0 accepted build failure versus an ordinary must-build failure; no-neg explicitly non-gate. Also prove the stock final-accounting call is load-bearing (removing/bypassing it must make a missing-control regression fail). The author may use an equivalent direct tools test, but it must exercise actual runner accounting and exit status.

Freeze Stage A’s product hashes and the repaired accounting/regression files, with the old product still at 2986/197299 and all original 236 labels accounted for. Stage B will necessarily add/re-anchor/retire product controls; carry a reviewed label-disposition ledger and preserve Stage A’s completeness mechanism/regressions. A W3-style promise that the entire probe stays byte-identical is inappropriate for this feature. B456 closes only after the repaired instrument and its missing-control regressions are independently gated.

## 10. Physical residue, proposed plan edits only — Q10

The current maintained authority is [the metal plan](../../2026-09-20-metal-test-plan.md), not the archived long script. UI-01, UI-04/06 and UI-19 are OWED; UI-20 is W4a’s distinct font check. No metal entry was edited and no hardware run is claimed here.

At W4b PASS, recommend these small changes, keeping all existing status rows OWED until exercised:

- **UI-01 step 2:** boot visibly selects Home’s first item. Walk each top-level list to MENU, double to menu mode on the first rail slot; shorts move the box/cue through the enabled fixed slots; double opens the preview’s first item. Verify x10–11 cue only in menu mode, exactly one x0–9 selection box, no body arrow in previews, no overlap or old 24×24 Home mark. Keep the strip/bounds tests.
- **UI-01 step 4:** repeat dark/consumed wake and an allowed incoming-message wake from both focus states, with a non-first selection. Require the same ordinary screen/focus/item; retain the one blank bus transition plus quiet-bus capture. Exercise the author-specified confirmation safety exception separately; do not call it a failure of ordinary focus retention.
- **UI-04/06:** add the Home entry and Home return to the existing real CREATE/JOIN session, safe BACK first, and observe actual ID/key arrivals changing Home options without selecting a different action. Retain radio/NV/power-cycle checks; do not duplicate the host’s entire origin matrix on metal.
- **UI-19:** first Home list on a real no-team OLED gateway is INBOX / MY DEVICE / MENU; rail holes and blank team/home fields remain. Retain the exact existing `> err gateway_build (joinprofile is normal-node only)` refusal fixture.
- Add a short **Home/My-device glass row** (next available UI ID at author landing): set a disposable name with `cfg set name ABCDEFGHIJKLMNOPQRSTUVWXYZ123456`; expected console line is exactly `> cfg ok name (saved to /mrid)`. Verify `whoami` contains `name="ABCDEFGHIJKLMNOPQRSTUVWXYZ123456"`. Home row is `ME ABCDEFGHIJKLMNO»`; My device rows are `ABCDEFGHIJKLMNOPQRS` and `TUVWXYZ123456`, then `ID 0x<HASH8>`, configured position/`NO LOCATION`, `>BACK`. Repeat a UTF-8 name and a genuinely unnamed fixture (`whoami` contains `name=""`): sanitized cells/no mojibake, `ME 0x<HASH8>`, `NO NAME SET`. The own position has moved off Home. Use the existing reboot-required fixture to check Home’s two-row list window. PASS is legible, separated rows/cue and correct physical wake/return; STOP is clipping, missing chevron, misplaced cue or unsolicited navigation.

No new console output is introduced by W4b. The quoted lines establish fixtures; real glyphs, pixels, button cadence and quiet I²C are the physical residue. Host tests own logical lists, byte strings, admission/return matrices and watermark semantics. Do not expand metal into a second software gate.

## 11. Fresh baselines — Q11

All commands and return codes are in [runs.json](2026-09-27-standalone-mobile-home-w4b-precheck/runs.json); compact results are in [baseline-summary.json](2026-09-27-standalone-mobile-home-w4b-precheck/baseline-summary.json).

| Instrument, run here | Result |
| --- | --- |
| `pio test -e native`, then direct `./.pio/build/native/program` | **2986 cases / 197299 assertions / 0 failed / 0 skipped** |
| Fresh external simulator build against this full working checkout; `tools/run_corpus.py --require-anchors --jobs 4` | **36/36 anchors**, 0 assertion failures, all **14 per-stream fields identical** to W4a QA; stable inputs |
| s18 | **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**; current `simulation/BASELINE.md` anchor, not a recalled historical value |
| `tools/probe_firmware_ui/run.sh` | l2 **529/529**, v3 **980/980**, BLE **529/529**; **236 verified / 0 unusable** |
| Independent completeness audit | **236 unique source-declared labels = 236 observed exactly once**, no missing/duplicate/unknown verdict; C0 failed its build as required; no `command not found` or FAIL line |
| Stock `tools/probe_board_abi.py` | **290 checks; 9/9 controls RED; 0 unusable**; current sizes unchanged |
| Supplemental layout measurements | All three compiler/flag sets; baseline and counterfactual sizes in §3, not a board build |
| Board-UI `--no-neg`, supplemental only | V3 **124/124**, V4 **110/110**; trait controls **14**, missing-trait compile controls **12**; structural **23/23**; wiring **56 pass / 3 fail**, exactly **W49/W51/W54 (B418)**; runner reports 171 wiring controls. Exit **1**, not PASS |

[corpus-manifest.json](2026-09-27-standalone-mobile-home-w4b-precheck/corpus-manifest.json) is the full before-state stream manifest. W4b is `src`-only, so corpus and Node size are predicted unchanged; P6 nevertheless retains the corpus in the actual gate. No board pair, warning census, mutation run, tools discovery or metal was requested/run as a **pre-check baseline**. Their future gate obligations are not waived. The board baseline is the coder’s job under `.pio-measure/w4b/`, with paired repeatability runs and no intervening checkout/`.pio` mutation.

## 12. Packaging, recommended fence and author decisions — Q12

Recommend **one W4b feature brief**, with B456 as instrument-only Stage A and navigation + Home + My-device/setup-origin as Stage B. They share one product surface and are corpus-inert (P6). Splitting a new MENU model from its Home entry/return targets would either create a half-navigable intermediate device or require temporary navigation behavior and a second rewrite of the same fixtures. No standalone C1 cleanup is necessary: change helpers only as needed for this feature, and do not move files.

Recommended fence for the author to enumerate and hash:

- Product: `src/firmware_ui_model.h`, `src/firmware_ui.cpp`, `src/firmware_ui_chrome.h`, `src/firmware_ui_status.h`.
- Direct tests: `test/test_firmware_ui_model.cpp`, `test/test_firmware_ui_chrome.cpp`, `test/test_firmware_ui_status.cpp`, and navigation fixtures in `test/test_firmware_ui_team.cpp` / `test/test_firmware_ui_send.cpp`. Add any other file only for a specifically identified reader; `invite.cpp` is a census input, not blanket permission to change invitation behavior.
- Instruments: `tools/probe_firmware_ui/probe_main.cpp`, `tools/probe_firmware_ui/run.sh`, a specifically named discovery regression file for B456 if chosen; `tools/probe_ui_model_mutations.py` for declared anchor/semantic dispositions and new batteries; stock ABI pins in `tools/probe_board_abi.py` (and only genuinely dependent assertions in its tools test).
- Evidence and the brief’s pre-approved comment/rule amendments. Keep `lib/`, simulator, protocol/wire/NV versions, `fw_main.cpp`, `firmware_commands.cpp`, `board_ui.cpp`, bitmap/font assets and B418 repair out. QA’s eventual design/register/metal/tracker/MEMORY landing occurs after the freeze, never during paired measurement runs.

Before issuing the brief, Claude should explicitly resolve:

1. The Settings preview’s removed arrow, Send’s promoted top-level list and result/catalog-change returns, and the interim W4b-only Inbox/Send content.
2. `OPTIONS CHANGED`’s complete press/latch table, and Home/sub-view behavior under alarm/blank, acknowledging the source’s existing confirmation cancellations rather than asserting they do not exist.
3. Home-origin unavailable note placement and stable reason, all typed-origin exits (including safety/push/saved-key branches), and capability-invalid probe inputs.
4. The precise retained-state shape and its **owner allocation ruling**. The measured compact candidate is +48 native / +56 board static symbol sizes, not approved or linked RAM. No new product ruling is otherwise needed if the brief implements D1/D2/D11 and retains existing safety contracts; a contradictory interpretation must be returned for decision.
5. The expectation/control retirement and replacement ledger, new Home/body invalidation proofs, and B456’s actual completeness/regression mechanism. Old mark/BACK controls cannot silently become vacuous or be “fixed” by weakening checks.

No new production defect number was assigned in this evidence-only task. B456 and B418 are existing findings; the items above are source-backed requirements/author decisions. No register edits are implied. W4b remains unimplemented and ungated.

The evidence directory’s `SHA256SUMS` covers its compact records, measurement driver and preservation receipt. The report is intentionally outside that index to avoid a self-referential checksum; the final delivery records both hashes.
