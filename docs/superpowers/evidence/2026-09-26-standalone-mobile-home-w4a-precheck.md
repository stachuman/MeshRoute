# Standalone Home W4a — independent pre-check ledger

Date: 2026-09-26. Role: Codex QA; Claude is the brief author. **Pre-check complete; ready for brief authoring with the decisions and instrument dispositions below. This is not implementation approval or a W4a gate.**

Scope: reviewed design revision 2.19, §4.1 / §10 UI-16 / §13 W4a; B441 and B449. Only this report and its evidence folder were added. Production, tests, tools, design, register, tracker and MEMORY were not edited. No staging, commit, reset or cleanup. Simulator sources remained read-only. All inherited W1c/W3 work is preserved.

The main conclusions are:

- Read all **32 counted name bytes before formatting**. Fixing only a renderer downstream of today's 14-byte copy loses the information needed to distinguish an exact fit from a longer name. It also loses the distinction between an actual hash fallback and a name that happens to begin `0x`.
- Zero resident growth is feasible: TEAM 6, compose header 15, compose result 19, reply identity 14, invite name 6 with its separate fingerprint, invite confirmation name 14. These are recommendations for the author, not new owner rulings. The two 14-column choices intentionally preserve current storage bounds despite wider physical space.
- One existing pure header can own the helper: `firmware_ui_model.h`, after `ui_display_byte`. The include direction prevents `firmware_ui_invite.h` from including that header to call it. Pass a preformatted six-column name into the invite row formatter; do not introduce a cycle or move the sanitizer/hash helpers as an incidental refactor.
- B449 is independently reproduced. A scratch repair of O8's second substitution changes its match count from 0 to 1 and makes the real invite row show `>0x00be T221 BEDEAD`. It fails the correct blank-name checks. This can be W4a's instrument-first stage.
- **Additional instrument disposition needed: O6.** Its current failure is a lowercase hash on the nameless pubkey-request screen, not the named confirmation described by its label. O20 already protects the latter. Explicit retirement of O6 with O20 retained is preferable to duplicating O20; see Q5/Q6. This finding is reported here for the author to register because the request expressly prohibits register edits.
- The installed `u8g2_font_6x10_tf` has the intended single-byte `0xBB` glyph. Other high bytes are not filtered by the driver. Physical appearance remains a metal check.

## Q1. Reconciled base and preparation inputs

| Repository | Exact HEAD | State at entry |
|---|---|---|
| MeshRoute | `8360802904f7bd0023279d3da844453d61207ede` | 1,455 tracked/nonignored-untracked paths; inherited W1c/W3 candidate and QA documentation; nothing staged |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b` | 285 inventoried paths; clean |

The base is **HEAD plus the complete file inventory**, not HEAD alone. [inputs.json](2026-09-26-standalone-mobile-home-w4a-precheck/inputs.json) records every path, SHA-256, symlink target, missing-file state and full porcelain status in both repositories. The starting tree reconciles exactly to W3 QA's original inventory plus its recorded four documentation landings and added evidence. W1c is included in that preserved base. There is no unexplained delta.

Verified complete checksum lists: W1c coder 86/86, W1c QA 29/29, W3 coder 31/31, W3 QA 29/29. The five W3 implementation/probe hashes, unchanged renderer hash, authorized W3 brief and coder receipt match the W3 freeze. [preflight.json](2026-09-26-standalone-mobile-home-w4a-precheck/preflight.json) gives all hashes and the reconciliation result; the four `*-checksums.txt` files give the individual checks.

The author's next brief should pin this inventory and checksum index, then enumerate/hash its own permitted preparation deltas. In particular, the design, register, tracker, MEMORY, existing receipts and reviewed briefs are inherited inputs, not disposable work. Current design SHA-256 is `a6970198b11012762597bdbe77428f2beb46731e4e61cb96ebede565a3c6032a`. No commit is needed to advance.

Source facts below are pinned by **file and symbol**; lines are navigation hints per P4. All were read from this candidate, not inferred from an earlier design or the coder's figures.

## Q2. Device-label census and available columns

The ordinary body starts at x=12 on a 128-pixel panel: 116 pixels / 6 = **19 complete small-font cells** (`firmware_ui.cpp:kBodyX/kBodyCols`, :1008–1012). STATUS's first three rows have a narrower 14-cell field beside the logo. The emergency detail starts at x=0 and has 21 complete small-font cells.

| Site / source symbol | Identity source and current bytes | Actual space / current bound | W4a effect |
|---|---|---|---|
| TEAM passive and interactive rows; `build_snapshot` → `label_for_team_id` → `ui_team_row` (:777, :1456; `firmware_ui_team.h:159`) | Authoritative `team_key_of_id`; then raw `peer_name_find`. No name: lowercase `0x%08lx`; no known hash: `id %u`. Row `%c%-6.6s %3s %4s %2s` silently clips the label | Name field **6**; row 19; current stored label 14+NUL | Format while the resolved hash and full raw name are still available. Name gets `cols−1` + `0xBB`; nameless known hash becomes the six-digit **member** fingerprint. `id 255` remains exactly six cells. Age/location/marker geometry stays |
| Compose header, including the header above results; `draw_compose` (:2269–2280) | `label_for_team_id(st.compose_peer)`; `to: %s`; current stack label 15 bytes | `to: ` consumes 4 of 19, leaving **15** identity cells; current name only 14 | Full raw lookup and a 15-cell result in a 16-byte stack buffer; named overflow marked, nameless full uppercase hash |
| Compose delivered result; `draw_compose_result` (:2193–2215) | Same peer lookup; `DELIVERED to` on row 1, label on row 2 | **19** identity cells; current stack label 14+NUL | Up to 19 cells in a 20-byte stack buffer, from the complete raw name. The other result states do not display this extra peer row; their common header is covered above |
| Emergency REPLY sender; `label_for_origin` → `mr_ui_on_push` → `ui_route_recv_push` → `UiModel::on_reply` → `freeze_outcome` → `draw_emergency` (:467, :2739–2741, :944, :2385) | Nonzero `Push::sender_hash` resolves name/hash; otherwise `id <origin>`. Model retains `_reply_who`; frozen `OutcomeView::who`; final line `%s: %s` | Whole line **21** cells. Existing identity bound **14**, separator 2, leaving at least 5 visible text cells at that bound. Text carrier itself allows 20 bytes | Recommend retain 14 identity cells, format before `on_reply`, copy the result verbatim thereafter. Full hash is 10. Do not call the name sanitizer again on the inserted marker or alter reply/wake/scope semantics |
| Invite candidate row; `build_snapshot` → `InviteMember::name` → `invite_sel_rows` → `ui_fmt_invite_row` (:788–808, :1989–2000; `firmware_ui_invite.h:446`) | Raw cached peer name, or empty; authoritative hash separately carried. `%c%-6.6s T%-3u %6s` | Name **6**, member fingerprint **6**, full row **19** | Same named abbreviation as TEAM. **Only an unnamed name field is omitted**; an actual name remains beside the fingerprint. Never insert a hash fallback into this name field. `T<n>` remains separate |
| NEW MEMBER confirmation, both window entry and roster grant; `invite_id_rows` → `draw_provision_screen` (:2022–2024) | Frozen selection hash `st.invite.sel_hash`; current snapshot name looked up by that hash. Full uppercase hash always; optional raw name on its own row | Each row physically **19**. Full hash 10. Name currently **14** in `InviteMember` and `InviteIdRows` | Keep full hash regardless of name. Recommend a 14-cell formatted name, supplied from the complete raw lookup at snapshot construction. A choice of 19 instead needs the measured carrier growth in Q3 |
| Grant result; `invite_result` (:2042–2046) | **Verdict's** `st.grant.hash`, not the discarded window selection | Full hash 10 in a 19-cell row | Already correct; retain `ui_fmt_member_hash_full`. Never replace it with a cached name or fingerprint |
| NEED PUBKEY and WAITING FOR PUBKEY (:2052–2071) | Frozen `st.invite.sel_hash` | Full hash 10 in a 19-cell row | Already correct; retain full hash. These are the sites O6 actually mutates |
| STATUS own identity; `ui_status_me` (`firmware_ui_status.h:96`) | Team-local own ID: `ME T<n>`, `ME NO ID`, or omitted when not in a team | 14-cell STATUS field; actual ID form at most 7 | No peer-name input, no key hash in this field. Preserve. Home/My device own-name work is W4b |
| Static JOIN adopted-result row; `join_fmt_node` (`firmware_ui_join.h:234`) via `draw_provision_screen` (:1775) | Typed adoption answer `st.prov_answer.node_id`: `node %u` | At most **8** of 19 cells | Preserve this numeric device ID. No peer hash/name is supplied by this projection |
| Inbox preview/list; `draw_inbox_screen` (:1489) | Kind/channel tag, message preview, age; **no sender identity field** | 19 | No W4a label conversion |
| Inbox detail header; `inbox_detail_head` (`firmware_ui_model.h:2275`) | Numeric stored `origin`: `DM from %u` / `CH%u from %u`, plus page indicator; **not** `id <n>` or `T<n>` here | Widest current header 18 of 19 cells; no stable peer hash/name carrier | Preserve numeric origin and page layout. Adding a live name resolution here would introduce a new identity policy, not repair an existing name label |
| Remaining alarm/status text | Outcomes, counters, coordinates, own team identity, home age | Existing line budgets | No additional peer-name producer. REPLY is the alarm name site above |

Adjacent identities that must **not** be converted:

- `firmware_ui_nearby_row.h:ui_fmt_nearby_row` (:71) and `ui_fmt_nearby_join_title` (:96) show the **team_id** fingerprint through `ui_fmt_team_fingerprint`, never the advertiser's device name/key hash. The list token is 6 cells; the row is at most 14 cells before its outer marker (15 with the marker); `JOIN <six>?` is 12. N4/N9 protect this separation.
- Provision CREATE/REPLACES/nearby/saved-key rows, saved-key confirmation/forget, and the INVITE header show **team** IDs/fingerprints. They keep the existing chrome helpers. The member and team tokens must not be merged merely because both have six hex digits.
- Static JOIN profile labels (`firmware_ui_join.h:join_row_label`) identify stored radio/join profiles, not devices; their formatting is outside W4a.
- `firmware_ui_geo.h` produces only distance/direction/freshness values. It neither selects nor formats a device label. TEAM still uses its fields beside the new label.
- Home/My device name, the review screen, unnamed advertising, and named-peer cache precedence stay with their assigned packages. W1c has already removed the manufactured advertised name; W4a must not change that core accessor or clear old cached defaults.

### Exact label/data readers

[identity-reader-census.json](2026-09-26-standalone-mobile-home-w4a-precheck/identity-reader-census.json) records 293 matching lines in 21 files across `src`, `lib`, `test`, and `tools`; comments are included and identifiable. The live graph is:

- `label_from_hash`: definition :448; called only by `label_for_team_id` :463 and `label_for_origin` :470. Probe-only callers are the W1 trampoline and renderer mutants.
- `label_for_team_id`: production calls at :777, :2195, :2275. `label_for_origin`: production call :2739. The nearby/invite uses in `run.sh` are deliberately wrong controls, not production consumers.
- `kLabelCap`: model constant :224; invite-capacity equality assertion :232; `TeamRow::label` :1731; `_reply_who` :3759; renderer `OutcomeView::who` :371 and the three stack label/`who` locals. Tests pin bounds/copies; runner C120 and B241 controls read these lines.
- `kInviteNameCap`: invite constant :55; `InviteMember::name` :202 and `InviteIdRows::name` :419; equality assertion above; native invite/model fixtures. It is **15 bytes including NUL**, not a 15-cell budget.
- `kTeamLabelCols`: team constant :96; width assertion; `ui_team_rows_equal` :238's visible-label comparison. `TeamRow::label` is filled by the snapshot, read by `ui_team_row` and that comparison. The model's selection is by ID, not label.
- `_reply_who`: written by `on_reply` :3603, exposed by `reply_who` :3628, copied by renderer `freeze_outcome` :944. `ui_route_recv_push` (`firmware_ui_send.h:608`) passes the already supplied `who`; it does not resolve names. Native model/send tests cover the copy and admission guards.
- `InviteMember::name`: filled only by the renderer snapshot; whole members are copied by `invite_sel_rows` (:355). Text readers are `ui_fmt_invite_row`, `invite_name_of` (:402) and `invite_id_rows` (:421). The latter is the optional-name confirmation path. Identity/admission/handled-set logic uses hash/ID; I30 deliberately substitutes a name-derived key to prove that prohibition.
- `InviteIdRows` and `InviteSelList` are local projection results, not added fields in `UiModel`. `UiState::invite` retains identities, not another name array.

Other instrument readers: `probe_board_abi.py` includes model/invite and pins their layouts; `probe_board_ui/run.sh` scans the real renderer and board/model separation; `probe_features/ownership.py` and `check_data_type_literals.py` scan source trees. The board-UI W49/W51/W54 anchors are in command/boot/console dispatch, not these label statements; B418 stays with W2. The command inventory's explicit surfaces do not include the four recommended production files, so no inventory regeneration is predicted. New headers would expand this census; none is needed for the recommended shape.

## Q3. Full names, buffers, and resident cost

Today `label_from_hash` gives `peer_name_find` **cap−1**, then appends NUL. At the usual cap 15 it receives at most 14 bytes and cannot tell whether the source had 14, 15 or 32. Invite publication independently does the same 14-byte copy. None of these current copies sanitize a name. W1's raw 32-byte API contract must remain intact.

Use a **counted raw stack buffer of 32 bytes**, with the returned length, at the existing resolver/publication seams. Never apply `strlen` or `%s` to that nonterminated raw buffer. Format into an output buffer with explicit display budget and sufficient NUL space. A stored name that looks like `0xdeadbeef` is still a name; do not infer identity provenance from its spelling.

| Carrier / local | Current use | Zero-growth recommendation |
|---|---|---|
| `TeamRow::label[15]`, 8 rows in `UiSnapshot` | Resident frozen row, also a per-tick snapshot local | Publish the final **6-cell** label from the full raw lookup; leave the array size unchanged. This also makes visible-label invalidation correct for an exact six-byte name changing into a longer name with the same prefix |
| Compose header local `[15]` | Stack, looked up by peer ID | Local output `[16]` for 15 cells; full raw `[32]` in the resolver's call frame |
| Delivered-result local `[15]` | Stack | Local output `[20]` for 19 cells; full raw `[32]` in the resolver |
| Push `who[15]` → `_reply_who[15]` → `OutcomeView::who[15]` | Stack → resident model → resident frozen outcome | Format at **14** before the model; keep all three sizes |
| `InviteMember::name[15]`, 8 rows in snapshot | Resident frozen optional name, also copied into local candidate lists | For a named peer, format at **14** from the full raw name; for unnamed, retain `""` and do not call a fallback-producing branch |
| `InviteIdRows::name[15]` | Stack confirmation projection | Copy that 14-cell display string unchanged; no new sanitizing pass |
| Candidate name/row scratch | Stack | Derive a **6-cell** name from the 14-cell prepared optional name, then pass it to the pure row placement function; its ID/fingerprint remain separate |

The last recommendation needs an explicit contract, not an accidental double-format. For **names only**, formatting to 14 and then to 6 is equivalent to formatting the complete original name directly to 6: any generated marker in the first result is at index 13, outside the second pass's retained prefix 0..4. Raw `0xBB` was already converted to `.`. Short names contain only sanitized ASCII and need no marker. The second pass must be strictly narrower; a second equal/wider sanitizing pass would destroy the marker. Confirmation renders the first result directly. [format-feasibility.json](2026-09-26-standalone-mobile-home-w4a-precheck/format-feasibility.json) records 8,976 hypothetical composition checks (all byte values at lengths 0..32, plus a raw `0xBB` at each position of mixed names); the accompanying script is a mathematical feasibility measurement, **not shipped code or an implementation test**. The reasoning is byte-local and does not claim exhaustive enumeration of all 32-byte strings.

The author can instead choose another explicit carrier scheme, but should not silently widen all `kLabelCap` users, store a clipped raw prefix as a complete name, or exempt input `0xBB` from sanitization. If the two-stage name-only projection is chosen, pin and test its contract and never apply it to TEAM's hash fallback.

### Measured layouts, not linked RAM

Fresh stock ABI probe: 290 checks, 9/9 controls RED, 0 unusable. Additional compile-only queries measured the unpinned local types too. All three real toolchain/flag sets were derived through `probe_board_abi.py`; no board firmware was linked.

| Type | native | heltec_mobile | gateway |
|---|---:|---:|---:|
| `Node` | 235208 | 122176 | 157304 |
| `UiState` | 504 | 504 | 504 |
| `UiSnapshot` | 1336 | 1336 | 1336 |
| `UiModel` | 928 | 912 | 912 |
| `TeamRow` | 40 | 40 | 40 |
| `InviteMember` | 20 | 20 | 20 |
| `InviteIdRows` | 26 | 26 | 26 |
| Renderer `OutcomeView` | 52 | 52 | 52 |

For clarity, these are **measured alternatives in temporary header copies**, not authorized allocations:

| Alternative | Measured changes on all three ABIs | Source-derived static UI carrier delta |
|---|---|---:|
| Keep the recommended resident capacities | No member/array type changes | **0 B** |
| Only invite name 14→19 cells | `InviteMember` 20→28; snapshot 1336→1400; local `InviteIdRows` 26→31 | **+64 B** |
| Only reply identity 14→19 | model +8; `OutcomeView` 52→60 | **+16 B** |
| Globally raise label cap 14→19 and invite cap 15→20 | `TeamRow` 40→44; member 20→28; snapshot 1336→1432; model +8; outcome +8 | **+112 B** |

The static multiplier comes from `firmware_ui.cpp:s_model`, `s_frame_snap`, and `s_frame_out` (:191, :391–392); there is one static snapshot, not two. `s_frame_state` is separate but its size is unchanged. The per-tick snapshot and candidate/confirmation projections are stack objects. Gateway's profile does not compile the OLED renderer: measuring a type there is **not** a claim that these objects occupy gateway RAM. [layout-measurements.json](2026-09-26-standalone-mobile-home-w4a-precheck/layout-measurements.json) includes sizes/alignments and all alternatives. The one independent invite-capacity alternative removes the old equality assertion only in the throwaway measurement copy.

The recommended implementation may add stack scratch and change flash. Neither peak stack nor linked RAM/flash was measured here. Coder baseline/final board measurements belong below `.pio-measure/w4a/`, with repeatability and field-by-field attribution per the stock tool's rules. Gateway image is predicted unchanged; mobile resident state is predicted unchanged, not flash-neutral.

## Q4. Pure seam, byte safety, and freeze boundaries

Recommended home: `firmware_ui_model.h`, immediately after `ui_display_byte` (:1446). It already includes `firmware_ui_invite.h` (:191), so both member hash helpers are visible there. Team includes model, and the renderer already consumes it. No new header/TU or moved sanitizer is necessary.

**Include-cycle constraint:** invite is below model. Its row helper cannot call a later model definition by including model back. Give `ui_fmt_invite_row` an explicitly prepared six-cell name argument (or equivalent narrowly specified row input), and call the identity formatter in the renderer before row placement. Fence every native/probe caller of that changed signature. Keep `InviteMember`'s hash/ID in the same whole-carrier path; do not rebuild the member field by field.

The formatter contract should spell out these points in the brief:

1. Counted raw bytes, at most 32 from the existing name API. Sanitize to ASCII cells with **the existing** `ui_display_byte(uint8_t)`. No UTF-8 decoding or glyph substitution table. Each `C5 82` therefore produces `..`.
2. If truncation is needed, retain `cols−1` sanitized cells and **then** append byte `0xBB`, followed by NUL. A source name's own `0xBB` is `.`; a generated marker remains `0xBB`.
3. No name plus a known hash: use `ui_fmt_member_hash_full` at budgets ≥10; use `ui_fmt_member_fingerprint` at budgets 6..9. These helpers are currently `snprintf` wrappers and can themselves truncate if given an inadequate cap; the new caller must enforce sufficient capacity before using them.
4. No known hash: return/control that case explicitly so the current caller's ID fallback survives. Do not fabricate `0x00000000`. Invite's duplicate-name-field suppression is a caller rule, not a request to hide a real name.
5. There is no production label site below six cells. Still define invalid-capacity/no-fit behavior: no clipped hash on cols 0..5, no out-of-bounds write, cap 0 writes nothing, and an available one-byte destination can be NUL-cleared. A typed/no-fit result or an enforced precondition is preferable to silently inventing a fallback. The author should pin the exact API, including destination cap versus cell budget.

A focused [262-check measurement](2026-09-26-standalone-mobile-home-w4a-precheck/byte-copy-proof.txt) drives the existing sanitizer and real `UiModel` reply copy, then the current snapshot/`InviteIdRows`/row functions; its [source](2026-09-26-standalone-mobile-home-w4a-precheck/byte-copy-proof.cpp) is an evidence-only measurement, not a new implementation test. It also checks the same `%s` copy spelling used by the outcome freeze (without claiming to execute the private freeze function). The downstream copies preserve `0xBB`: `copy_clamped` (:3831) copies `char` bytes up to NUL; `on_reply` calls it; `freeze_outcome` uses `%s`; `invite_id_rows` copies byte by byte; whole snapshots/outcomes use assignment; `%-6.6s` is byte precision and copies a marker that is already among the six bytes. The inserted byte would be lost only by an additional sanitizer or a too-small/clipping buffer. Add real-path tests for these seams.

Production label adapters are in the renderer's anonymous namespace. Their consumers terminate at panel data/drawing. No label output is passed to console, JSON, the companion contract, `peer_name_set`, wire encoding or NV. Core/raw names and console/companion text stay unchanged. The newly reusable pure helper must remain a panel formatter; a lone `0xBB` is not UTF-8.

TEAM/member arrays and REPLY's `who` cross the explicit frame freeze (`s_frame_snap = s`, `s_frame_out = freeze_outcome(s)`, then `draw_frame`, :2558–2571). **Compose labels are currently an exception to a blanket “all identities are frozen” claim:** both draw functions perform their own live `label_for_team_id` lookup during rendering. W4a need not add resident state to change that lifecycle; do not claim it proves rename-between-pages atomicity. Any new freeze policy is separate scope. The grant target itself remains the frozen hash; formatting never chooses an action target.

## Q5. Tests, changed expectations, and mutation readers

### Existing tests and required expectation changes

| Instrument / location | What it pins now | W4a disposition |
|---|---|---|
| `test_firmware_ui_team.cpp`, `ui17-team` :118–137, :175–214 | Long `Wolfgangetta` becomes `Wolfga`; a supplied `0xdeadbeef` is clipped to `0xdead`; 14 W's become 6 W's; exact 19-cell rows | Replace the raw fixture publisher with an explicit identity projection where appropriate. Expected long name `Wolfg` + `BB`; long W name `WWWWW` + `BB`. A **true unnamed** hash `0xDEADBEEF` yields `ADBEEF`. A literal stored name `0xdeadbeef` remains a name and abbreviates as such; do not guess its provenance from the string |
| Same file, invalidation :549–563 and surrounding tests | Names sharing their first six raw bytes do not repaint; only visible columns compared | Preserve no-repaint for equal **rendered** labels. Add exact-six→longer-same-prefix edge (`Wolfga`→`Wolfg`+`BB`) that must repaint; long names sharing first five cells and marker remain equal. Retain visible-column, age and geo bounds |
| `test_firmware_ui_invite.cpp`, :474–510 | Literal `>Wolfga T221 6C2971`, `>Wolfga T7   6C2971`, blank nameless name, short-name padding | Change only named abbreviation expectations to `Wolfg`+`BB`; retain ID/fingerprint placement and row length. Caller/signature changes must preserve independent literal oracles |
| Same file, fingerprint/full-hash :543–596; name-independent identity :599–615; `invite_id_rows` :853–880 | Uppercase member tokens, hash collisions remain distinguishable in full form, full hash even with a name, empty optional name for absent peer | Existing hash/authority expectations stay. `Wolfgangetta` is 12 bytes and still fits the recommended 14-cell confirmation budget. Add long/high-byte optional-name cases |
| `test_firmware_ui_model.cpp`, reply-copy :971–982; reply state/scope cases; `test_firmware_ui_send.cpp` receive routing | Copy bounds, NUL, scope, wake/whitelist and retained state | Keep model-copy semantics. A direct overlong synthetic `on_reply` input is a copy-bound test, not a raw-name formatter test. Add a preformatted marker-copy case; do not put name/hash policy in the receive router |
| Model invite/roster identity tests, including :8553–8583 | Mutating the name/local ID cannot move the frozen grant target; optional name can update independently | Preserve. Carrier text is display only. Tests calling the revised row API must be fenced |
| Chrome/geo/presets/custody tests and node-hashlocate tests named by the reader census | Mostly snapshot construction, layouts, unrelated member records or formatters | Read/audit, not blanket rewrite permission. No raw core API expectation changes; existing 32-byte W1 regression remains |
| UI probe P18 (:3167–3395) | Real TEAM named/nameless/keyless rows, cursor, frozen rows, live-age repaint and blanking | Update literal `Wolfga` to `Wolfg`+`BB` throughout this phase, and unnamed `0x00c0` to `C0FFEE`. Preserve age strings, marker placement, frame counts and geo/blank assertions |
| UI probe P23/P24 (:5384–5693) | Real invite rows, name arrival, request/grant identity; navigation strings contain `>Wolfga T221` | Change the named six-cell row prefix and corresponding `walk_to` strings. Leave blank name column, full hashes, 12-byte confirmation name and state transitions intact |
| UI probe P28a (:6799–6839) | W1 trampoline: poisoned 15-byte destination, NUL/guards, full-prefix clipping for long names, lowercase full-hash fallback, caps 0/1 | Retain all poison/canary/short-rename/zero-capacity checks. Replace 15/27/32-byte truncation oracles with first 13 sanitized bytes + `BB` at budget14; exactly14 stays whole. Fallback becomes uppercase full hash. Adapt trampoline if adapter signature changes, keeping its include-the-file-under-test guarantee |
| UI probe P28b (:6880–6882) | ` 0xb241 ` and ` 0xb2ee ` TEAM fields | ` 410007 ` and ` EE41EE ` respectively; `H1` and `id 93` unchanged |
| UI probe P28c/P28d (:6889–6952) | Compose/result/reply lowercase `0xb2410007`, `0xb2ee41ee`; named and hash-less variants | Uppercase `0xB2410007`, `0xB2EE41EE`; short names and `id 93` unchanged. Add long-name cases at the chosen individual budgets |
| Probe P21 advertiser-name precondition :4831; negative “no advertiser/granter name” checks :4204/:4891 | Raw cached `Wolfga` prefix and absence of node names from team-only UI | **Do not mechanically replace every `Wolfga` occurrence.** The raw-cache prefix remains raw; keep/strengthen absence tests to exclude both the full name and its new abbreviation on team-only screens |

There is no existing B441 end-to-end high-byte name matrix. Add direct pure cases for every byte 0x7F, 0x80..0x9F and 0xA0..0xFF; valid multibyte UTF-8 such as `C5 82`; empty, exact budget, budget+1, and 32 bytes. Include control bytes/embedded NUL in the counted **synthetic** helper input as applicable, clearly distinguished from console-admissible names. Test name-vs-hash provenance, literal raw `0xBB`, 6/9/10 column hash boundaries, 0/1 cap safety and both guard bytes. No strlen on a nonterminated raw input.

The real-UI probe must show that the actual name lookup reaches the formatter at TEAM, invite row and optional confirmation name, both compose sites and REPLY. Literal expected bytes should be independent of the formatter under test; building the expected row with the same faulty formatter can conceal a defect. Explicitly exercise first-pass 14→second-pass6 marker preservation if that shape is chosen. The new controls must compile, finish, and fail the intended assertions, not merely exit nonzero.

### Renderer controls: keep, re-anchor, or retire explicitly

| Control | Fresh baseline | Recommended handling |
|---|---:|---|
| B241a / B241b | 10 / 5 failed checks | Keep their meanings: missing terminator and last-byte-only terminator. Re-anchor the adapter body if needed; both must still fail the poisoned short-name/rename checks. Formatting changes are not permission to lose this coverage |
| N4 | 8 failed checks | Keep advertiser device-name-as-team mutant; re-anchor any adapter signature change and guard both substitutions |
| N9 | 8 failed checks | Keep team-ID-as-peer-hash mutant; re-anchor if needed. Correct device full hash is still the wrong team token |
| O2 | 1 failed check | Keep wrong team header identity source; adapt signature/budget without making it accidentally choose the correct team token |
| O6 | 1 failed check | **Do not carry forward its current meaning claim.** It substitutes two pubkey-screen full-hash calls, not `invite_id_rows`. Only nameless P23d fails on lowercase spelling. The named-confirmation defect is already O20 (3 failed checks) and native I29. Recommend explicit retirement of O6 in the brief/receipt, preserving O20 and all full-hash checks, or a distinctly justified new target; do not duplicate O20 or allow a silent GREEN |
| O8 / B449 | 2 failures, but wrong checks before repair | Repair on unchanged source first, with exact-one guard per substitution. Before/after failure attribution is in Q6. Re-anchor its data path after the production conversion so it still proves that an unnamed invite row may not gain a clipped-0x name field |
| O20 | 3 failed checks | Keep: actual named NEW MEMBER confirmation loses its full hash. It is the coverage needed when disposing of O6 |

Also inspect/re-anchor C120's `label_for_origin` statement (:1117), O3's candidate-row call (:1520), the W1 generated-wrapper declaration (:151), and any changed call-site signatures. O5/O9 hit the two `st.invite.sel_hash` full-hash sites; O18/O22 target the result's `st.grant.hash`; preserve their purpose and current topology. C114's peer-location `if (hash != 0)` anchor was intentionally distinct from the invite publication guard `if (hash != 0u)`; do not collapse them and cause an unintended second match.

### Native mutation selector census

Safe AST/literal inspection only: the mutation module was **not imported**, and no mutation battery was executed in this pre-check. [mutation-selectors.json](2026-09-26-standalone-mobile-home-w4a-precheck/mutation-selectors.json) includes target maps, every recommended union entry, exact anchor/replacement, count and line. All 379 recommended existing entries have exactly one source match on this base.

For the recommended four-file production fence:

| Set | Batteries | Existing entries |
|---|---|---:|
| (a), changed source files | `model` 239 + `sliceCbudget` 1 (`firmware_ui_model.h`); `uiteam` 20 (`firmware_ui_team.h`); `uiinvite` 32 (`firmware_ui_invite.h`) | **292** |
| (b), dependencies recommended to the coder | `uisend` 15 + `sliceCsend` 1 (reply routing); `chrome` 44 (team/member namespace and layout dependency); `uinearbyrow` 9 (team-only labels); `uigeo` 18 (TEAM's adjacent columns) | **87** |

No battery targets `firmware_ui.cpp`; its executable protection is the real renderer probe. `chrome`, `uinearbyrow` and `uigeo` are **not** selector (a) merely because they are nearby in the UI. If their files actually change, reclassify them; no such change is recommended. Add the new identity mutations to the chosen pure helper's source target and update the native expected-count pin from execution, not prediction. The final union therefore has a floor of 379 plus the new entries, conditional on this fence. QA's P6 minimum is (a) plus affected probes; the coder names/justifies (b).

Anchors in/adjacent to the proposed changed statements:

- `model` M13: exact `ui_display_byte` definition, one match at :1446. Leave its body/anchor alone; the new formatter must actually use it. Reply copy/label capacity have no existing targeted model mutation. W01/W04 are grant identity semantics nearby, not formatting; preserve.
- `uiteam` T02 and T03: row snprintf/marker :164–165; T09: six-column visible-label comparison :238. Keep all three meanings. Formatting before publication can preserve their source anchors, but update fixtures to represent formatted labels and retain the offscreen-byte comparison contract.
- `uiinvite` I07/I08/I09 share row snprintf :449: fingerprint replaced by name; fake clipped-0x fallback; removal of the column bound. Re-anchor to the new prepared-name input without accidentally making I09 inert because both the producer and row now bound the name. A malformed/overlong synthetic row input can retain the row's independent width contract; document it. I10 attacks full-hash spelling :392; I29 keeps full hash with a name :423; I30 wrongly keys the handled set by a name :354. These must retain their behavior and match counts.
- Dependency U04 keys off the receive-router signature :610. Nearby Z02/Z03/Z08 attack team fingerprint spelling; they do not become member-formatting controls. The remaining exact anchors are in the JSON census; none needs movement under the recommended fence.

## Q6. B449 instrument-first proof, and the O6 finding

The focused reproduction uses the stock runner's compilation functions and **real v3 renderer**, with isolated source mutants in `/tmp`. Its copied probe adds one diagnostic `printf` of the existing candidate row and changes no assertion. It is separate from the fresh stock full probe.

| Variant | Substitution changes | Visible nameless candidate | Result / failed assertions |
|---|---|---|---|
| Live source | none | `>       T221 BEDEAD` | 964/964 |
| Current O8 | 1, **0** | `>       T221 BEDEAD` | 962/964; P23d named `Wolfga` arrival and P23e named confirmation fail because the surviving `mem.name[nn] = '\0'` blanks the name |
| Scratch O8 repair | **1, 1** | `>0x00be T221 BEDEAD` | 962/964; **P23b blank name field** and **P23d blank-name precondition** fail |
| Current O6 | two full-hash call sites | candidate itself unchanged | 963/964; only **P23d request confirmation full hash** fails |

[control-reproduction.json](2026-09-26-standalone-mobile-home-w4a-precheck/control-reproduction.json) contains exact failure lines and the source scripts. The initial scratch-harness setup attempt resolved ROOT relative to `/tmp` and failed before compiling source; it was corrected to the explicit repository root and rerun. No result from that attempt is used. Logs are retained under the ignored artifact directory and checksummed.

**Recommended sequencing:** one W4a brief, instrument stage A on unchanged production. Repair O8, attach an exact-one guard to each substitution (W3's existing `once` mechanism is reusable), and assert the failure identity above. Freeze/hash that stage. Decide O6's explicit retirement/re-aim with the existing O20 coverage named. Stage B changes the product display and its deliberately changed expectations. Unlike W3's render-preserving refactor, the entire probe cannot remain hash-identical between stages: W4a intentionally changes label bytes. Instead carry a precise before/after expectation and control-disposition ledger.

After the formatter lands, O8 must still inject the **forbidden identity into the otherwise empty invite name field**, visibly reach the candidate row, and fail a literal blank-field/19-column/fingerprint assertion. Merely calling the newly correct formatter is not proof of the old truncated-0x defect; it may now produce a different safe token, or a later name formatter may abbreviate it differently. Authorize the exact post-change mutant shape in the brief, retain both unique-substitution checks where the two-stage injection remains, and record its actual row bytes and failed assertions. Do not count a compile error, no-match, blanked legitimate name, or unrelated failure as the repair's control proof.

**New finding for the author's register landing:** O6's stale label/target is a pre-existing instrument fact, separate from B449. At this frozen base the next free register number is B455; no number is allocated by this report. Evidence: `run.sh:1525–1526`, actual targets `firmware_ui.cpp:2055/:2070`, reproduction above, and existing O20 at :1583–1584. No production defect or owner ruling is inferred. The register remains untouched under this request.

## Q7. Font and proposed metal residue

Actual panel driver: `variants/heltec_common/board_ui.cpp:set_font` (:303–306), small font **`u8g2_font_6x10_tf`**, large font `u8g2_font_10x20_tf`. Installed library is **U8g2 2.35.30**. Small font's 2,000-byte declaration is in `.pio/libdeps/heltec_mobile/U8g2/src/clib/u8g2_fonts.c:2752`; its hash and decoder/header/version inputs are recorded in [font-inputs.json](2026-09-26-standalone-mobile-home-w4a-precheck/font-inputs.json).

A throwaway host program compiled the actual installed font and U8g2 glyph-lookup/width decoder. [font-glyphs.txt](2026-09-26-standalone-mobile-home-w4a-precheck/font-glyphs.txt) records all 256 byte lookups:

- ASCII 0x20..0x7E: 95/95 glyphs, six-pixel advance.
- 0x7F..0x9F: 0/33 glyphs, zero advance. Raw bytes in this range can disappear rather than occupy the intended cell.
- 0xA0..0xFF: 96/96 glyphs, six-pixel advance, using Latin-1 font encodings. They are not treated as UTF-8 or mapped to dots by the driver.
- 0xBB: present, advance 6, glyph bitmap 6×5, two right-pointing chevrons:

```text
#..#..
.#..#.
..#..#
.#..#.
#..#..
```

`board_ui.cpp:draw_text` (:309) passes the bytes to `drawStr` without filtering. U8g2's `u8g2_DrawStr` (`u8g2_font.c:1039–1042`) uses `u8x8_ascii_next` (`u8x8_8x8.c:290`), which returns byte encodings directly (except string/newline termination). It does not use the UTF-8 decoder. Thus raw `C5 82` can show a Latin-1 glyph followed by a missing glyph; the source-side sanitizer is necessary. The host canvas only records strings, not physical pixels. This measurement confirms the font asset, not panel legibility.

Add a short W4a row to the **current** metal authority, `docs/2026-09-20-metal-test-plan.md` (e.g. new UI-20, after checking the ID at landing). Do not resurrect the archived bench script or duplicate automated tests. Proposed contents:

1. On two OLED-capable same-team peers with a fresh/controlled cache, record each build and `whoami` hash; open TEAM and, on a fresh candidate, INVITE MEMBER. Use the existing `peername <HASH> "Wolfgangetta"` local label fixture, or the peer's own `cfg set name Wolfgangetta` before a fresh cache/exchange. Read back the actual name through `nameof <HASH>`/`peers` before judging the panel.
2. TEAM's six-cell name and invite's six-cell name both read **`Wolfg»`**, with one legible chevron cell, no overwritten age/ID/fingerprint columns. NEW MEMBER retains the full uppercase hash beside any name.
3. Repeat with the exact UTF-8 name `łAB` (bytes `C5 82 41 42`); TEAM/invite name reads **`..AB`**, two dot cells for the two high bytes, no Latin-1 glyph or collapsed spacing.
4. Repeat with a genuinely unnamed peer on a **fresh cache**. TEAM shows the six uppercase low-24-bit device fingerprint; invite's name field is blank and its separate fingerprint remains. Confirmation shows `0x<HASH8>`. Do not use an empty incoming name to “clear” an old cache: W1c deliberately preserves existing cached names.

**PASS:** those pixels are legible and identical in their allotted fields on the actual font/panel, with the correct identities and unshifted neighbor columns. **STOP:** missing/doubled marker, raw Latin-1 rendering, collapsed high-byte cells, clipped hash, or a fabricated invite name. If exact fixture readback/fresh-cache state cannot be established, leave the arm OWED rather than claiming a display failure or pass. No new firmware console line is expected from this display-only package; name readbacks use existing commands.

## Q8. Fresh baseline runs

[runs.json](2026-09-26-standalone-mobile-home-w4a-precheck/runs.json) records commands, exit codes and durations. [gate-summary.txt](2026-09-26-standalone-mobile-home-w4a-precheck/gate-summary.txt) retains the concise instrument output. The full raw logs remain ignored and are indexed by SHA-256.

| Instrument | Independently reproduced result |
|---|---|
| `pio test -e native`, then **direct** `./.pio/build/native/program` | **2973 cases / 196111 assertions / 0 failed / 0 skipped** |
| `tools/probe_firmware_ui/run.sh` | **l2 518/518; v3 964/964; BLE-row 518/518; 231 controls verified; 0 unusable**. Includes C0's required build failure. O8's existing RED is counted by the stock instrument but its semantic defect is separately exposed above |
| `python3 -B tools/probe_board_abi.py` | **290 checks; 9/9 controls RED; 0 unusable**; sizes above unchanged |
| Fresh simulator build linked to this complete core tree, then `tools/run_corpus.py --require-anchors --jobs 4` | **36/36**, no assertion/anchor failure; inputs stable; **all per-scenario manifest fields identical** to W3 QA |
| s18 | **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**, taken from current `simulation/BASELINE.md` and reproduced |

[corpus-manifest.json](2026-09-26-standalone-mobile-home-w4a-precheck/corpus-manifest.json) records every stream's source hash, bytes, full MD5/SHA-256, events, assertions and anchor result. [corpus-comparison.json](2026-09-26-standalone-mobile-home-w4a-precheck/corpus-comparison.json) records the independent W3 comparison. The corpus was run despite the src-only scope, per P6. **Prediction for W4a: 36/36 byte-identical**, since no lib/simulator/wire source is in the proposed fence.

Not run here: board firmware measurements/build pair, warning census, native mutation union, tools discovery, full board-UI probe, or metal. They were not requested pre-check baselines. Coder board measurements and final gates are still required by the eventual approved brief. B418's W49/W51/W54 state is source-audited against W3's receipt here, not newly executed or claimed fixed.

## Q9. Packaging, fence, and author decisions

Recommend **one W4a brief with an instrument-first stage**, then the display fix. B441, formatter call-site conversion and B449 share this surface; no wire/NV/lib work, C1 refactor, new persistent state, or simulator work is required. Do not bundle general header/file moves, dead-field removal, source reformatting, peer-cache policy, new Home/review pages, or frame-lifecycle changes. No owner product ruling is presently required. The author still needs to choose and explicitly state the per-site budgets, formatter buffer/no-fit contract, invite prepared-name projection, O6 disposition and exact post-change O8 mutant.

Recommended edit fence (actual selection is the reviewed brief's responsibility):

- `src/firmware_ui.cpp`: adapters and stack scratch, full-name snapshot publication, explicit site budgets, invite row call, accurate adjacent comments; no selection/admission/radio/clock behavior changes.
- `src/firmware_ui_model.h`: pure helper next to `ui_display_byte`, label carrier contract comments and any compile-time bounds; **no resident member/array growth**, no receive/cadence policy change in the zero-growth option.
- `src/firmware_ui_invite.h`: prepared candidate-name parameter/row contract and optional-name documentation; retain full-hash, whole-carrier and authority semantics.
- `src/firmware_ui_team.h`: formatted-label/visible-width contract comments and only the necessary row seam if the author chooses one; unchanged column geometry/geo/age and correct visible-label invalidation.
- `test/test_firmware_ui_model.cpp`, `test/test_firmware_ui_team.cpp`, `test/test_firmware_ui_invite.cpp`: pure formatter/copy/row/authority/invalidation regressions and deliberate expectation changes. Add `test_firmware_ui_send.cpp` only if an additional real receive-routing marker proof is placed there; this is optional, not a reason to alter routing production.
- `tools/probe_firmware_ui/probe_main.cpp`, `tools/probe_firmware_ui/run.sh`: actual-source trampoline, real panel checks, O8 repair, explicit control dispositions, guards and precise expectations.
- `tools/probe_ui_model_mutations.py`: named anchor moves/new identity controls, expected native count, union evidence; no unrelated battery cleanup. `tools/probe_board_abi.py` only if an author-approved allocation actually changes a pin; no pin change is predicted for the recommendation.
- QA landing: current metal plan, design/register status and findings, tracker/MEMORY pointers, brief/receipt/evidence. These remain frozen preparation inputs during implementation except for explicitly authorized evidence. Do not edit them during paired measurements.

The brief should separately list all read-only dependencies from the census. `lib/core` (including `peer_name_find`), console/JSON/companion code, board font/driver, NEARBY/geo/chrome product logic and simulator are outside the implementation fence. The board-UI B418 repair stays W2; B447's named-peer precedence remains separate.

For the eventual gate, retain P6: native binary, complete anchored corpus, both ruled boards via the stock measurement tool, selector-(a) mutations and affected probes independently on QA; coder runs its named combined union and six-environment warning census as well. Test-tool changes require the tool suite. Run the real UI probe on all three arms with controls and ABI sweep; no `--no-neg` result substitutes for either gate. Preserve W1's poisoned-buffer proof and W1c's counted 32-byte API proof. The exact before/after counts, expected byte changes, negative-control meanings, resource deltas and frozen-input hashes belong in the coder receipt.

Final preservation and evidence index: [preservation.json](2026-09-26-standalone-mobile-home-w4a-precheck/preservation.json), [SHA256SUMS](2026-09-26-standalone-mobile-home-w4a-precheck/SHA256SUMS). The new findings remain in this report under the evidence-only instruction; the author must carry their disposition into the brief and register before dispatch.
