<!-- Author: Claude (brief author); QA: Codex (pre-checks, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# W7 + W8 — the one-button editor, the panel rename and written messages (`src`, paired under P6)

**Revision 2 — 2026-10-04 — for QA's scoped re-review.** The
[brief review](../evidence/2026-10-04-standalone-mobile-home-w7w8-brief-review.md) held revision 1
(`6890e3cf…afbd`) on W7W8R-1–W7W8R-3; this revision folds them in (§8).

- **Base:** owner commit **`4c1a000`** (`4c1a000bc71706ac438e64161a9452966a66615f`), plus four uncommitted inputs:
  - the QA-passed tool-package landing, including the bounded F07 harness line;
  - the W7 and W8 pre-check evidence;
  - the author's r2.25–r2.27 documentation delta.

  The W8 pre-check's `inputs.json` (2,495 MeshRoute paths, 285 simulator paths), plus the documentation delta declared
  in §1, is the authority for the tree. The simulator is at **`6585649`**, clean and read-only.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 the brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file.
- **Authorities:**
  - [design r2.27](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) — §4.3, §4.4, §5,
    §6.5–6.7, §7.1–7.5, §7.4.1, §11.1, §12 (D4–D6, D10, D11, D16–D19), §14, §15;
  - the [W7 pre-check](../evidence/2026-10-03-standalone-mobile-home-w7-precheck.md) — cited as "W7 §n";
  - the [W8 pre-check](../evidence/2026-10-03-standalone-mobile-home-w8-precheck.md) — cited as "W8 §n";
  - the [brief review](../evidence/2026-10-04-standalone-mobile-home-w7w8-brief-review.md) — W7W8R-1–W7W8R-3; its
    accepted portions stand;
  - the [register](../../2026-07-30-open-bug-register.md): B480 and B481 in scope; B444's panel path in scope under
    D19, while B444 itself stays open; everything else out of scope;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1, C2, U1, U2, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md).

## 0. What this is

The design's last two messaging packages, delivered together because they share one editor (D17):

- **W7 — the editor and the name.**
  - The one-button editor (§5, D4).
  - My device's `CHANGE NAME`, with its review and result (§4.3). The panel becomes the second caller of W0's
    rename service.
  - The name prompt before JOIN/CREATE from Home (§4.4, D10/D11).
- **W8 — written messages.**
  - `WRITE MESSAGE` on the Send list and in a person's compose list (§7.5).
  - The locked 163-byte draft (§5.6).
  - The kinds `dm_text` / `channel_text` with kind-scoped gates; the review, send and result per caller (§7.3,
    §7.4, §7.4.1).
  - Never `-l` (D6).

**Owner rulings:**
- **D16** (2026-10-03): the shared 163-byte draft now — +192 B board / +200 B host objects.
- **D17** (2026-10-03): W7 and W8 paired under P6 — one brief, one gate.
- **D18** (2026-10-04): +16 B board / +8 B host beyond D16, for `SendReq`'s draft ID and an explicit outcome record.
  That is **+208 B in total on every ABI**.
- **D19** (2026-10-04): before the team-local ID exists, the panel refuses both ordinary team-post kinds.
- **Folds** (2026-10-03): B480 and B481.
- **Earlier rulings** (2026-09-23): the reviewed D4, D5, D6, D10 and D11.

**Shape:** one brief, one final freeze, one gate. The coder works through four internal checkpoints (W8 §7). They
are not releases, and QA gates none of them:
1. the pure editor and its tests;
2. the name flow and the prompt;
3. written messages;
4. the renderer, the probe controls and the complete chain.

C1: this is a feature package. No refactor or file move is mixed in.

**What changes on the panel:**
- **My device:** its action row becomes ` CHANGE NAME >BACK`.
- **An unnamed JOIN/CREATE:** shows `NO NAME SET` / ` SET NAME` / `>SKIP`.
- **WRITE MESSAGE:** appears on the Send list and in a person's compose list, so `GRANT KEY` moves down one row.
- **D19:** a team phrase or written team post before the team-local ID exists answers `NOT SENT` /
  `NO TEAM ID YET` and airs nothing. This is a named change to today's phrase behaviour.

**Not in this package:**
- B444's core fix (W1b) — non-UI senders keep the exposure;
- B446, B479, B489, B493–B497;
- location for written messages, preset editing on the panel, lowercase;
- W4c, W4d, W5 and W9;
- any `lib/`, wire, NV, console-verb, `fw_main.cpp`, configuration-service or simulator change.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints)

**Seams.** Facts from W7 §2–§7 and W8 §2–§9, re-checked by the author at the base:

| Symbol (hint) | Fact at the base | Use |
| --- | --- | --- |
| `firmware_config.h::rename_node`, `RenameResult` (:235–236) | W0's service: one live→record conversion, at most one `save_id`, live name only once durable | Called once per SAVE; read-only |
| `firmware_ui_input.h::Gesture`, `InputFsm` | Classified gestures; debounce 25 ms, double gap 350, arm 800, fire 3500 | Consumed; read-only |
| `firmware_ui_model.h::UiSnapshot::own_name` (:2232) | Counted own name, published once per tick | Preload; the `WAS` source |
| `HomeView` (:2255), `SetupOrigin` (:2259), `home_gesture` (:4321), `home_activate` (:4353), `provision_admit` (:4836), `settings_follow_screen` (:4579) | My device has one `BACK` row; JOIN/CREATE run the gate, then `enter_setup_from_home`; leaving SETTINGS retires the setup origin | §2.3 |
| `ReviewPhase` (:2666), `on_review_captured` (:3830), `review_wrap_line` / `review_page_count` / `review_page_rows` (:1580–1600), the review/detail union, `review_header` | W6's phrase review: three wrapped rows per page in a 60-B union shared with the Inbox detail; 20-B header | §2.3, §2.4 |
| `ComposeRow` and `compose_row_*` (:1754–1830), `compose_gesture` (:6035), `send_list_row_override` (:1822), `open_send_list` (:4411), `next_screen` (:6225) | One row authority for both compose lists; the Send screen is reachable from menu mode with no ID gate | §2.4 |
| `SendKind` (:1837), `SendReq` (:1858, 16 B), `take_send_request` (:3558), `compose_open` (:3702) | Three kinds; both `_req` and `_review` own a `SendReq`; the drain is emergency first | §2.4 |
| `on_gesture` (:3046), `on_tick` (:3232, :3295), `on_msg_wake` (:3544), `emergency_gesture` (:6235), `FrameGate::step` / `on_page` (:6381, :6422), `ui_allows_sleep` (:6474) | Priority, blank, wake and overlay authorities (W7 §5) | §2.5 |
| `firmware_ui_send.h::SendLive` (:512, 12 B), `send_gate_of` (:518), `ui_compose_send_line` (:562), `ui_perform_send` (:603), `SendTracker::match_dm` (:141), `match_aired` (:175), `ui_pump_trackers` (:260), `refuse_reason_of` (:449) | The gate, composer, trackers and execution path; `ui_pump_trackers` closes normal tracking when `!compose_open()` | §2.4 |
| `firmware_ui_chrome.h::ui_nav_slot` (:325), `ui_chrome` (:558) | Rail mapping; DM compose on TEAM already boxes SEND | §2.6 |
| `firmware_ui.cpp::ui_send_live` (:584), `ui_exec` (:552), `draw_review` (:2388), `draw_compose` (:2397), the My device body (:1440–1452), the request drains (:2639–2643), `build_snapshot`'s `my_team_id` (:876) | Resolves a peer for `SendKind::dm` only; execution goes `exec_command` → real `parse_command` → `Node::on_command` | §2.3, §2.4, §2.6 |
| `variants/heltec_common/board_ui.cpp` (:306, :309) | The 6×10 font; all 42 repertoire bytes and `_` are six-pixel glyphs (W7 §2) | Read-only |
| `lib/core/command.h::SendFailReason` (:322) | 18 values; producers traced in W8 §3 | §2.4's table; read-only |
| `tools/probe_firmware_ui/probe_main.cpp::draw_hline` (:629) | Counts calls only — no geometry | §2.8 |

**Fenced files.** Each hash is the working-tree SHA-256 at the base:

| File | SHA-256 | Lines |
| --- | --- | ---: |
| `src/firmware_ui_editor.h` | **new** — absent | — |
| `src/firmware_ui_model.h` | `6ec20c82a720a09afe01afc847f8aba9433b0cbe8a656e2c555fc88fcccb61cf` | 6478 |
| `src/firmware_ui_send.h` | `2d6b09354adf590f48972013a1e47e3a933614f9ed837d83178a9d5ab18c5563` | 787 |
| `src/firmware_ui_chrome.h` | `b4c1ff24451725a03d2e966fd1da3a8102c61815b74cc995f1fd8f2c0ee6a181` | 621 |
| `src/firmware_ui.cpp` | `11789f4392624b2e312492ad19c221659df9ccd01276070ef145f1c9fa8a32b1` | 2936 |
| `test/test_firmware_ui_editor.cpp` | **new** — absent | — |
| `test/test_firmware_ui_model.cpp` | `5f7d61cadf1b50a66cd07e2f229c9478c8c27c5e1f66a0597ff580a1b8397287` | 12527 |
| `test/test_firmware_ui_send.cpp` | `a79e804ac53cd714455232e4cfdc92ec27f599dd262119e52f5ce7ec9bfbe736` | 2994 |
| `test/test_firmware_ui_chrome.cpp` | `b19c08ad5a4e2e841d92f1a1b9972e6f1ec46cdadb7d58ed12d09db2d4b2e478` | 1389 |
| `tools/probe_firmware_ui/probe_main.cpp` | `188bb4eab5128de44536dd7897180bdf2bf29227093466f92e44c7e593a6776d` | 7886 |
| `tools/probe_firmware_ui/run.sh` | `ffcb62af6655a1a3a5eec8164f0152cc2136e2ad13b5adee922e75436afcaab5` | 2116 |
| `tools/probe_board_ui/run.sh` | `366a8bf089604c5391b3933b284561c33b6ce5e05cdf5a9e0c8255210beea5bf` | 1665 |
| `tools/probe_board_ui/expected.tsv` (592 identities) | `2a30e4d1f10683a17b46eccbdd1b31de4ac7252a7491d9ff60e2d202edcb2196` | 598 |
| `tools/probe_board_ui/accounting.py` | `2a23f7521805f38c6a48cd24008cd9eeeac1e8ad7b0b2aa8e3a85e38398f1e09` | 256 |
| `tools/probe_ui_model_mutations.py` | `27807b320427fb090840e927bf559bfff909f78761d98b29e3aff8b3cb4d177d` | 13163 |
| `tools/probe_board_abi.py` | `fabd78ac4f48177cb27d21181c724dd7a9898d6c062c58c721ca3522f0bd9e2f` | 1087 |

**Read-only dependencies,** named because the package reads, links or reuses them:

| File | SHA-256 |
| --- | --- |
| `src/firmware_ui_status.h`; `src/firmware_ui_input.h`; `src/firmware_ui_team.h` | `338548341e579a3faac514592103cda794e2cca8cd1dd5fcc28a3a645d4c38cd`; `30c74299c9bb580e016ed7efa847f1e6af29b5b560891cdbd2bb732d0ce4ecc0`; `63f9074be7ba73072816ba3a3d3654853da1a721b8dfeff7db57e854e9c72cdf` |
| `src/firmware_ui_presets.h`; `src/firmware_ui_invite.h` | `1d67253f74490dbb43d83eae06b7bb5389151d4256da37b3c97e114000aebe23`; `f3f61d692752bb24b131390690b2529327c75b47a6a769d720f36749220b9c7c` |
| `src/firmware_config.h`; `src/firmware_config.cpp`; `src/device_nv.h` | `c72d59600745da4d7b65b8cbd37d3da5b9f5a74da33cc5a71a98a4fa8aac8bd6`; `accb667703290ba57e590b4ec07d20ab6bf21dc8ae7d8b9ae1840e2bf3dfdc18`; `36296bcbc1c6975a6cf0bea2caf1ca5eebd182832f2550c12de1a7185429fffa` |
| `src/firmware_commands.cpp`; `src/fw_main.cpp`; `lib/core/command.h` | `48f9b19a9f402585c8f3c061999d4c993ec53cf936b6f6b9a4921e586bc060fd`; `4b9b86afd3303cec913a343faa159d4305591d31cf185fe8e8dfc27b509f7766`; `f537b1cc28d5ac6ff22f8243913933455aa371a2e50989a88a6bc1d28c9c612d` |
| `variants/heltec_common/board_ui.cpp` | `35d7c07995d2fd6886da46dd556a6af47bf414dbec4e31d36c1bebf9641daa46` |
| `test/test_firmware_ui_input.cpp`; `test/test_firmware_ui_status.cpp`; `test/test_firmware_ui_team.cpp` | `6aefa729e89321f1d09f215fd31188bc43fcfe71b96511e382431e047ee57fed`; `4605ac3df22c451b1b639a7a970e90fb71b3370b92277796542b7f02f611f6d8`; `a97383e0df80825a0b8f39e047c6a261e8357f2ab714fad419c63fb586c296c8` |
| `tools/probe_board_ui/probe_main.cpp`; `negctl.py`; `fakes/Arduino.h` | `a06c448fb7bb28e25cb2f86fa2fd18a12bd57aadfc0d2bed3ae157389e67b204`; `7634d66b9391b6fc90f293f51ef2b9376324c83da521fc8ba6abbcc0e1ff54c4`; `deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853` |
| `tools/probe_accounting.sh`; `tools/test_probe_firmware_ui.py`; `tools/test_probe_board_ui.py` | `785cf0f70951e0647429ac0b29587055636bf269b47ff71f8fd15dc87a26d9f5`; `836ba7ac6eccae3b73b18e6929765fbf12d2181515c9ee2361b0e9cc43784047`; `82a344994d963adc80b02d1758b9a3c81a73cb3af7834f5df06cf66fbf7c51e2` |
| `tools/test_mutation_unusable_reason.py`; `tools/test_worker_formula_derived.py`; `tools/gen_command_inventory.py` | `7c04519c2f40f11fdac5fc1d55648349dd8ac10fa2237cad9313a72b186fb61d`; `fb8704f2d3b0629b577b7c4c9ea0a4544e08f6c0d4c585321ad1f1f801c18176`; `b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a` |

Every other path stays at its W8 `inputs.json` hash.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| design r2.27 | authority | `7ed161d6e777f1eb98fa65319de9bed2f7864bcb532004eeb00efc912a2325f6` |
| `docs/2026-07-30-open-bug-register.md` | authority (§0, B444, B480, B481) | `b3ac76d9a49793a256b53d678b729138cd959ae895bcd8ccc6f2686c6919a3ef` |
| `tracker.md`, `MEMORY.md` | context pointers | `a0b5231a8c5a12a3f1b895a4a5c06b418a835b3a8ee4ce1f24fb4cf6d5844be9`, `f8c41ba28bf903a3c66fd98bce740d50af0c59024922688f2dfec70729a740ad` |
| `docs/superpowers/evidence/2026-10-03-standalone-mobile-home-w7-precheck.md` and `…/SHA256SUMS` (26 entries) | authority | `e4148d274271addf9e0ff1535b4fce6a9db8155ce54df4b73f929e1d9524be1e`, `037b975bd3737808627ff90fb389ed0fea649121b5e0f1d60dd434c564b5af6f` |
| `docs/superpowers/evidence/2026-10-03-standalone-mobile-home-w8-precheck.md` and `…/SHA256SUMS` (30 entries) | authority | `2598f88c2894f3b6896e62ce6e9a2a2f83357e7b7d93afd0f6baa696e9e79a13`, `7b51d522015834bdc906f0f534c8b3df0b23a3f0eaf593e84b7cf50a1933db55` |
| `docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8-brief-review.md` and `…/SHA256SUMS` (10 entries) | authority (brief review, HOLD) | `3eb6c846f395257257df8ae7617d2919cc5174b4df888885c6a52d6b6b6ce877`, `ca6e3c27292220e00f99721308a3d1f18bc5ef608b9a9684c0f93065583cdd88` |
| `docs/superpowers/evidence/2026-10-02-b478-b487-b488-qa-regate.md` and `…/SHA256SUMS` (40 entries) | inherited (tool package) | `f5490a4fe7e1df92a3c8dd12e6295e5405b7af5622f16c9806bf8045013c1fbd`, `c0e3b26fd893ee5d35f22f92eba726e829737a25f5f56b5ca1c17d0808e8907d` |

**Document history since the W8 pre-check:**
- The W8 pre-check entered with the register at `49a0f71b…1a09` and registered B497, giving `24faa8aa…1ce8`.
- The author then recorded D18/D19 in §0 and B444, giving `8f4fa98e…f537` for revision 1. For revision 2 the author
  rewrote that §0 entry in place to name the brief review and revision 2, giving `b3ac76d9…a3ef`.
- The design went from r2.25 (`af8e9ff3…9184`) to r2.26 (`215f05ec…c920`, revision 1) and r2.27 (revision 2);
  `tracker.md` and `MEMORY.md` changed in one line each per revision.
- No finding was added; the next free number is **B498**.

This brief is pinned by the hash QA issues, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `4c1a000`; the simulator at `6585649`, clean. If the owner has
   committed the inherited inputs since, check the new commit's parent and blob contents against these pins, and
   record the new hash.
2. Every fenced file and read-only dependency matches its hash, and both new paths are absent.
3. Every preparation-set hash matches, and each seal verifies (`sha256sum -c SHA256SUMS`): W7 26, W8 30, brief
   review 10, re-gate 40.
4. Every path in the W8 `inputs.json` has its recorded hash, except the four preparation documents above.
5. Classify every other path. QA reports and evidence added after this brief are explained; anything else is
   reported.

Any of the following is STOP-1 to QA: a mismatched input, a changed preparation file, or an unexplained path.

## 2. Contract

### 2.1 The editor core — new `src/firmware_ui_editor.h`

A pure header. It includes standard headers and `firmware_ui_input.h` only. It never includes `firmware_ui_model.h`
(an include cycle) or `firmware_config.h` (Arduino). No allocation, no `strlen`, counted bytes throughout.

- **Repertoire (D4, §5.2):** exactly these 42 bytes, each once, in this order, in seven groups of six:
  `ABCDEF` · `GHIJKL` · `MNOPQR` · `STUVWX` · `YZ0123` · `456789` · `SPACE . , ? ! -`.
- **Rings:**
  - the group ring: the seven groups, then `EDIT`;
  - a character ring: its six characters, then `BACK`;
  - the control ring: `DEL LEFT RIGHT DONE DISCARD BACK`.

  Entering a ring highlights its first item. After an insertion the highlight returns to group 1.
- **States (§5.4):**
  - **E1, the group ring:** short = next item, with wrap. Double on a group = E2 on its first character; double on
    `EDIT` = E3 on `DEL`.
  - **E2, a character ring:** short = next, with wrap. Double on a character inserts it at the cursor if
    `len < cap`; otherwise it shows `FULL` and writes nothing. Either way it returns to E1 on group 1. Double on
    `BACK` = E1 on the same group.
  - **E3, the control ring:**
    - `DEL` removes the byte before the cursor and stays on `DEL`; it does nothing at 0.
    - `LEFT` / `RIGHT` move within 0..len and stay.
    - `DONE` opens the review, unless the draft is empty or all spaces (note `EMPTY`).
    - `DISCARD` leaves at once when the draft is empty; otherwise it opens E4.
    - `BACK` = E1 on group 1.
  - **E4, discard confirmation:** `DISCARD DRAFT?`, the first 19 bytes, then `>BACK` / ` DISCARD`, BACK first.
    `BACK` = E3 on `DEL`; `DISCARD` clears the draft and returns to the opener.
- **Landings (r2.26):**
  - Returning to the editor — from the review's `EDIT`, from a refused or not-saved result, or after an alarm that
    closed a review or result — lands on group 1.
  - An alarm over an open E1–E4 keeps its ring position.
  - The cursor is kept on every return.
- **Notes (r2.26):** `FULL`, `EMPTY`, `BUSY`, `TEAM CHANGED`, `RECIPIENT CHANGED`.
  - A note clears at the next eligible press: a lit short or double the emergency overlay does not absorb.
  - That press still performs its normal action.
  - The first press on a dark panel only wakes it and leaves the note.
- **Layout (§5.3), five rows by 19 columns:**
  - **Row 0:** the caller part, plus used/cap right-aligned: `NAME           4/32`, `TO TEAM      45/163`,
    `TO STAN      45/163`. A DM label takes at most 8 columns, `»` included.
  - **A note owns row 0 alone (W7W8R-1, design r2.27).** While it shows, the note is left-aligned and the counter
    is hidden: `RECIPIENT CHANGED` is 17 cells, and no note fits beside `163/163`. The press that clears it restores
    the normal header and still performs its normal action.
  - **Rows 1–2:** the draft on a fixed 19-column grid, showing grid rows *r* and *r* + 1, with
    *r* = max(0, ⌊cursor / 19⌋ − 1). A cursor at a multiple of 19 sits in column 0 of the next row.
  - **Rows 3–4:** the current ring, `>` on the highlighted item, with worst widths 14 / 17 / 18. SPACE is drawn `_`
    in ring rows only and announced `ADD SPACE`. With `BACK` highlighted in a character ring, row 4 reads
    `BACK TO GROUPS`.
- **Preload (§4.3):** all or nothing. A name opens preloaded only if every byte is in the repertoire; otherwise it
  opens empty. Nothing is uppercased, transliterated or dropped.

### 2.2 Storage — D16 + D18, exact

| Type | Contents | Size / align |
| --- | --- | --- |
| the draft, one, in `UiModel` | `bytes[163]` with no terminating byte; length; cursor; caller; cap; `draft_id` (u32); `locked` | 176 / 4 |
| the name origin, one, in `UiModel` | `none`, `my_device`, `setup_join`, `setup_create` | 1 / 1 |
| the outcome record, one, in `UiModel` (model-private) | the written request's state, and its reason, refusal and code bytes | 4 / 1 |
| the editor descriptor, in `UiState` (counted twice: model and frozen frame) | phase, group, ring item, used length, cap, visible cursor row and column, note, panel result, selection | 10 / 1 |
| `SendReq`, in `_req` and `_review` | + `draft_id` | 16 → **20** / 4 |
| `SendLive` (transient, built per execution) | + one bool, "team-local ID exists", in the padding after `peer_found` | **12** / 4, unchanged |

- **Aliases:** the editor's two visible rows alias the existing 60-B review/detail union. `WAS` and the editor's DM
  label reuse the 20-B `review_header`.
- **Never stored:** a second text array, a whole-draft copy in `UiState`, `UiSnapshot` or any stack local, or an
  own-name copy.

**Pins (W8 §6):**
- `UiState` 568→**576** host / 560→**568** boards.
- `UiModel` 1016→**1216** host / 1000→**1200** boards.
- `SendReq` 16→**20** on all three ABIs.
- Unchanged: `UiSnapshot` 1368, `UiChrome` 20, `SendLive` 12, `SendTracker` 16, `InputFsm` 28.

Any other retained field is STOP-1, for measurement and an owner ruling.

**`draft_id`:** a u32 advanced (mod 2^32) each time a review freezes the draft; never reset when the text clears;
compared only for equality.

### 2.3 The name flow (W7)

- **My device:**
  - The action row ` CHANGE NAME >BACK` (18 columns), BACK selected on entry; short toggles.
  - Double on `BACK` returns, as today. Double on `CHANGE NAME` opens the editor: caller name, cap 32, origin
    `my_device`, preloaded per §2.1.
  - W4b's H24 keeps its meaning: a short never leaves.
- **Review (r2.25):**

  ```
  SAVE NAME?
  <name bytes 0–18>
  <name bytes 19–31>
  WAS <old name at 15 columns, `»` past 15> | WAS NO NAME SET
   SAVE >EDIT
  ```

  - `WAS` is captured from that tick's snapshot when the review opens, then frozen.
  - Selection is on `EDIT` at entry, after a blank and on `long_arm`.
  - There is no page token and no `LOC`.
- **The request:**
  1. `SAVE` sets the name phase to requested.
  2. `take_name_request` marks it taken once and exposes a counted read-only view of the model's bytes.
  3. A new `ui_service_name_request` in `firmware_ui.cpp` calls `mrfw::rename_node` **exactly once**. It maps
     `RenameResult` exhaustively to a pure panel result, which goes back to the model.

  The service runs in the tick:
  - outside the normal send busy gate, so a pending DM never blocks a save;
  - after the existing emergency drain, with every existing drain's call count unchanged.

  Nothing else saves: not a redraw, wake, cancellation or acknowledgement, and not a stale or duplicate answer.
- **Results (r2.25):**
  - `saved` / `unchanged` → `NAME SAVED`;
  - `nv_save_failed` → `NAME NOT SAVED` / `NV WRITE FAILED`;
  - `too_long` → `NAME NOT SAVED` / `NAME TOO LONG`;
  - `bad_args` → `NAME NOT SAVED` / `BAD NAME`.

  The last two are unreachable from a valid draft, but they are still shown, and the draft is kept.
- **Returns (§7.4.1):**
  - `NAME SAVED`, acknowledged: the draft is released. Origin `my_device` returns to My device. A setup origin asks
    `provision_admit` again: success enters the chosen step with the setup origin `home`; refusal shows the
    setup-block note, then Home.
  - `NAME NOT SAVED`, acknowledged: the editor, on group 1, same draft, no retry.
  - `long_fire` on the review: the review closes; after the alarm, the editor shows the draft.
  - `long_fire` on `NAME SAVED`: the draft is released; after the alarm, My device, or Home for a setup origin.
  - `long_fire` on `NAME NOT SAVED`: after the alarm, the editor with the draft.
- **The prompt (§4.4, r2.25):**
  - A new `HomeView` arm, rail STATUS. It is shown after `provision_admit` admits a Home JOIN/CREATE, and only when
    the counted own name is empty.
  - Rows 0–2 read `NO NAME SET` / ` SET NAME` / `>SKIP`, SKIP selected.
  - `SKIP` asks the gate again: success enters the existing step; refusal shows the setup-block note, then Home.
  - `SET NAME` opens the editor with origin `setup_join` or `setup_create`.
  - `DISCARD` in that editor returns to the prompt, SKIP selected.
  - `long_fire` at the prompt returns to Home after the alarm, and the pending setup is dropped.
  - `_home_return` keeps the opener.
  - Every exhaustive `HomeView` switch — model, renderer, chrome — gains the arm.
  - Gateways (`HomeProfile::no_plane`) have no JOIN/CREATE, so no prompt; My device rename works there.
- The name flow never touches `SendReq`, a send slot, a tracker or the content lock.

### 2.4 Written messages (W8)

- **Entry (§7.5, r2.26):** a typed write action in the one row authority (`ComposeRow` and `compose_row_*`):
  - the Send list: team phrases → `WRITE MESSAGE` → `MENU`;
  - a person: DM phrases → `WRITE MESSAGE` → `GRANT KEY` when offered → `back, don't send`.

  WRITE carries no location marker and never yields a slot. An empty catalog still shows WRITE and the exit, plus
  GRANT when offered. Every positional reader derives from the authority; out of range still fails closed. K7's
  semantics are unchanged; only its index moves.
- **Binding at `WRITE MESSAGE`:**
  - `_review` takes the kind, the team, and for a DM the peer ID, known bit and hash. These are resolved once,
    through `Node::team_key_of_id`; `ui_send_live` resolves both DM kinds.
  - The draft's caller is team or DM, with cap 163.
  - The DM header label is formatted at 8 columns from the full raw name, never from a six-column TEAM label.
- **A broken binding:**
  - Team changed or left, or the recipient's hash changed: the note `TEAM CHANGED` / `RECIPIENT CHANGED`, with the
    draft kept.
  - While the binding is broken, `DONE` shows the note again and opens no review. `DISCARD` is the way out. Nothing
    re-binds.
  - A catalog change has no effect on written text.
- **The review (§7.3, §7.2):**
  - **Row 0:** `TO TEAM <ID8>`, `TO <label ≤ 7> <HASH8>`, or `TO T<n> UNVERIFIED`.
  - **Rows 1–3:** three word-wrapped lines per page, from W6's `review_wrap_line` / `review_page_count` /
    `review_page_rows`. At most six pages, every byte kept, and the concatenation equal to the payload.
  - **Row 4:** ` SEND >EDIT     n/m` — 19 cells, page token in columns 16–18, `EDIT` selected. Never `LOC`.
  - **Paging:** pages advance on `kDetailPageMs`, never on a press. A blank suspends the cadence and resets the
    selection to `EDIT`.
  - **Projection:** a source-kind branch projects from a borrowed counted view of the draft into the union. It never
    goes through `ui_review_capture`'s catalog path, and Inbox and phrase `_detail_body` ownership is unchanged.
- **`SEND`:**
  - While an ordinary request is still pending: the note `BUSY` replaces row 0 until the next eligible press,
    nothing queues, and the review stays.
  - Otherwise: the draft is content-locked, the bound request queues whole into `_req` (U2), and the result view
    `SENDING...` opens.
- **The gate (`send_gate_of`), per kind:**

  | Kind | Draft | Catalog | Destination |
  | --- | --- | --- | --- |
  | emergency | — | exempt | exempt |
  | `dm`, `channel_canned` | — | slot, generation, enabled, phrase kind | live team; DM known hash; `channel_canned` also needs the team-local ID (D19) |
  | `dm_text`, `channel_text` | still locked, same `draft_id`, in bounds | **none** | live team; DM known hash; `channel_text` also needs the team-local ID (D19) |

  Each refusal is typed and makes zero core submissions. Two new refusals:
  - `NOT SENT` / `DRAFT CHANGED` for a written request whose lock or ID fails;
  - `NOT SENT` / `NO TEAM ID YET` for an ordinary team post before the team-local ID exists.

  `SendLive` supplies that ID's existence from `g_node.team_local_id()`. The existing refusals and their wording are
  unchanged.
- **Two phases share the gate (W7W8R-2, design r2.27).** D19's team-ID check belongs to **execution only**.
  - **Review admission** keeps exactly today's validation: `ui_review_capture` (fed by the device's
    `ui_service_review`, with `review_note_of` translating its answer) checks catalog, team and recipient for a
    phrase. The written review opens from `DONE` on the draft's binding. Neither ever applies D19, so a valid saved
    team phrase — or a written team message — opens its review before the team-local ID exists. `review_note_of`
    never sees a D19 answer.
  - **Execution** (`ui_perform_send`, at the drain) asks the same validation, then D19's check for
    `channel_canned` and `channel_text`. The coder chooses the mechanism — a phase argument, or a separate
    execution-only check after the shared gate answers `send` — so long as the shared validation is not
    duplicated or weakened, and no positive live ID is invented.
  - **Required traces, native and real firmware-UI, for both ordinary team kinds:** pre-ID selection or editing →
    the review opens → explicit `SEND` → `NOT SENT` / `NO TEAM ID YET`, with zero executor and zero core
    submissions, then each caller's acknowledgement return (a phrase: its list; written: the editor, unlocked).
    Also: the ID lost between the review and the drain (refused at execution), success with an ID, and the
    emergency exemption.
- **The composer:**
  - It branches on the written kinds before any catalog indexing.
  - The forms are `send <id> "<draft>" -t -a` and `send_channel <ch> "<draft>" -t -e`, written with `%.*s` from a
    borrowed counted view of the locked draft. Never `-l`.
  - Too little output capacity refuses (0).
  - The worst lines are 181 (DM) and 189 (channel) bytes with NUL, or 188/196 at the ten-digit `%u` bound; both
    fit the unchanged 199-byte static line.
  - Execution is `ui_exec` → `exec_command` → the real `parse_command` → `Node::on_command`.
- **Request states (§7.4.1),** recorded in the outcome record at the event that attributes them:
  - **queued:** pending, locked, and a press is ignored.
  - **known refused:** a gate refusal, an empty or truncated composition, a parser or synchronous executor refusal,
    or an attributable known-not-aired reason (below). The draft is unlocked and kept.
  - **accepted, open:** the executor returned `queued`, including `ctr == 0`, recorded as accepted-open without
    changing the emergency attempt counter or its expiry order. `SENT, waiting` keeps tracking open.
  - **accepted, final:** any other attributable terminal outcome. `NO CONFIRM` may still upgrade to `DELIVERED`
    while the view is open.
- **Failure classification (W8 §3; all 18 values):**

  | `SendFailReason` | Class |
  | --- | --- |
  | `no_pubkey`, `no_identity`, `too_large`, `bad_rng`, `joining`, `mobile_no_home`, `unsealable`, `no_location` | known not aired, when attributable |
  | `cap`, `min_interval` | known not aired, on the existing attributable `send_blocked` path only |
  | `no_ack`, `e2e_ack_timeout`, `no_cts`, `gateway_unreachable`, `no_route`, `queue_full`, `reprovisioned` | may have aired |
  | `none`, and any value or producer not classified here | may have aired |

  Attribution comes first. An unmatched push changes nothing. A matched but unclassified failure counts as
  accepted, final. No outcome at all stays accepted, open.
- **Trackers:** one shared pure classification of DM / ordinary / written families, used by `match_dm`,
  `match_aired`, `match_channel_sent`, `match_blocked`, `tick` and `submit`. Preserve:
  - exact ctr+peer DM attribution, and channel ctr attribution;
  - emergency-first routing;
  - the late-ACK upgrade;
  - no ctr-less channel `send_failed` matcher (B80/B84);
  - B111's DM-zero handling, channel bounded expiry, and the 8-second correlation window.
- **`ui_pump_trackers`:** a written result keeps the normal transaction open after the editor body closes.
  Acknowledgement or pre-emption closes it exactly once. A later outcome is ignored, and an ordinary outcome never
  moves the emergency.
- **`long_fire`:**
  - It withdraws only a pending **written** ordinary request (checked by kind), unlocks the draft, and shows the
    editor after the overlay, on group 1.
  - The emergency request drains first, untouched. A saved phrase's owed request keeps its existing behaviour.
  - Never a bare `_req_pending = false`; never retained ownership inferred from `_fail` / `_refuse`.
- **Returns (§7.4.1):**
  - known refused, acknowledged: the editor, unlocked;
  - accepted, open, acknowledged: the view closes, tracking ends, the draft is released — the declared residual;
  - accepted, final, acknowledged: back to the list WRITE MESSAGE came from, draft released.

  `take_send_request`'s reset arms gain the written kinds. A D19-refused team phrase acknowledges back to its
  phrase list.

### 2.5 Interruption (§5.5, W7 §5, W8 §8)

The editor joins the existing authorities in their order: long gestures first, then the waking press, then overlay
absorption, then ordinary dispatch. Required proofs:
- **Every surface — E1–E4, the review, the result, the prompt:** blank and wake (the first press only wakes);
  receive (counters and wake only, no navigation); `long_arm`, `long_cancel` and `long_fire`; no hidden action under
  the overlay; frozen pages while live input or the name changes.
- **Edge timing:** `millis` wrap, a blank and a page boundary on the same tick, and sleep after inactivity —
  `ui_allows_sleep` lets a blanked editor sleep.
- **Existing behaviour kept:** blank's existing cancellations (key grants, saved-key offers, `ConfigService::on_blank`)
  stay as they are. The Settings field editor's long-arm cancellation does not apply to the new editor. M36 and M37
  still attack the Settings field editor.

### 2.6 Rails and rendering

- **Rails:** the name editor, review, result and the prompt box STATUS. Message editors, reviews and results box
  SEND — DM ones too, even from TEAM → person. The emergency suppresses every rail first. `ui_nav_slot` covers
  every new arm.
- **`firmware_ui.cpp` draws:**
  - the editor;
  - the cursor underline: `draw_hline(12 + 6 × column, baseline + 1, 6)` on the visible row;
  - `draw_review`, extended for the name's `SAVE`/`EDIT` and the written `SEND`/`EDIT n/m`;
  - the results, the prompt and My device's row.

  Frames read frozen state only. No live draft or name is read while drawing, and only one `FrameGate` exists.
  `FrameGate` never credits an Inbox read because the review shares rows.

### 2.7 B480 and B481

- **B480:** one memory-safe model mutation deleting the statement `if (preset_generation_moved(s)) close_compose();`
  (exactly one match). It must go RED through the DM generation-close assertions. Saved-phrase closure stays, and
  written text proves its exception: a catalog change has no effect on it.
- **B481:** retitle and recomment the legacy case at `test_firmware_ui_model.cpp` ~:1165 to say what it proves — the
  overlay absorbs compose gestures, and the alarm is kept. Its assertions stay. The neighbour at ~:1149 is unchanged.

### 2.8 Instruments

- **Native.**
  - The new `test_firmware_ui_editor.cpp`, plus model, send and chrome cases, cover every §14 row: Input
    boundaries, Repertoire and rings, Editor transitions, Interruption, Name save, Home rows and lists (the prompt),
    Multiline and send, Context and lifetime, Production reachability and Resources.
  - Add real-parser checks of the composed written lines in the send tests.
  - Add composer cases for empty, all-space, 17/18, 163/164 and too-small capacity.
  - Use canary and sentinel tests for every draft operation at both ends.
- **Mutation.**
  - A new battery `uieditor` → `TARGET_SRC` `src/firmware_ui_editor.h`, registered in all three places (target map,
    list, `MUTS_BY_TARGET`). Its families: repertoire order and uniqueness, each ring bound and wrap, insertion's
    return to group 1, E2 `BACK` keeping its group, E3 `BACK` to group 1, insertion at the cursor, DEL before the
    cursor, cursor clamp, FULL without a write, EMPTY / all-space DONE, both DISCARD arms, safe DISCARD
    confirmation, and all-or-nothing preload.
  - **Model additions:** origin, gate order, request once, alarm returns, results, the written lock, withdrawal,
    gate and binding, and B480.
  - **`uisend` additions:** the gate per kind and the composer.
  - **`chrome` additions:** the rails.
  - Every entry must be memory-safe — change a bounded choice or a transition, never a copy bound — must match
    once, compile, and go RED for its intended assertion.
  - Re-anchor existing entries (W7 §7, W8 §9) only where their statement actually moves; keep each attacked
    property and match count. Derive `PIN_CASES` / `PIN_ASSERTS` and report the new battery's entry count.
- **Firmware-UI probe.**
  - A signature-correct recorded fake for `mrfw::rename_node`: five scripted results, exact counted bytes and length,
    a call count, and live-name publication on `saved` / `unchanged` only. Name it honestly: W0's inbox identity
    arms remain the real-service witness.
  - `draw_hline` records x, y and width (height 1).
  - Raw `InputFsm` samples reach SAVE and SEND; exact renders of every new screen.
  - Executor captures of the written lines: count, body and no `-l`. Zero submissions on cancellation and refusal,
    D19 included, through §2.4's two-phase traces.
  - Exact real-renderer rows for all five notes, including over a full-length draft, with the wake and overlay
    press rules (W7W8R-1).
  - Comparisons of the frozen frame across a frame's pages.
  - Each new check has a classified control. Existing controls keep their contracts (W7 §7, W8 §8). The B456
    accounting stays complete.
- **Board-UI probe.**
  - Its current predicates hold, including the frozen draw-frame arguments, one `FrameGate`, the drains and the
    executor seam.
  - New wiring identities and negative controls are added only for the name-request drain and the draft-source
    wiring that no old predicate reaches.
  - `expected.tsv` and the accounting census change for exactly those additions, each named.
- **ABI.**
  - The stock pins re-pin `UiState`, `UiModel` and `SendReq` to §2.2.
  - A supplemental `--extra-pins` manifest in the evidence measures the draft, the descriptor, the name origin, the
    outcome record and `SendLive` (12), under the names the code uses.

### 2.9 The closed ledger

- **Production:** the new editor header; the model (state, flows, gate wiring, trackers and their classification);
  `firmware_ui_send.h` (kinds, gate, composer, review projection, trackers); `firmware_ui_chrome.h` (rails, new
  arms); `firmware_ui.cpp` (drains, live answers, rendering).
- **Tests:** the new editor test; model, send and chrome cases; B481's retitle.
- **Tools:**
  - the firmware-UI probe and runner;
  - the board-UI runner, manifest and census — only the named additions;
  - the mutation harness — the new battery, new entries, B480, re-anchors and the native pins;
  - the ABI stock pins.
- **Recorded behaviour changes:**
  - My device's row;
  - the unnamed JOIN/CREATE prompt;
  - WRITE rows, and GRANT KEY's index;
  - D19's phrase refusal;
  - written messages;
  - name and message renders, rails and results;
  - B481's title.

Anything else is a STOP.

### 2.10 Nothing else moves

- **Unchanged files:** `firmware_ui_status.h`, `firmware_ui_input.h`, `firmware_ui_team.h`,
  `firmware_ui_presets.h`, `firmware_ui_invite.h`, the configuration service and its TU, `device_nv.h`, the command
  TU, `fw_main.cpp`, all of `lib/`, `variants/` and the simulator, every shared fake, the transcript tool, the inbox
  and deferred-actions probes, and the tool tests.
- **Unchanged outputs:** console outputs, the 197-row command inventory, phrase bytes and location flags, word wrap
  and cadence, BACK as the phrase review default, K7's semantics, Inbox read and navigation rules, and the
  emergency's geometry, budget and priority.
- **Corpus:** predicted 36/36 byte-identical, because the package is `src`-only.

## 3. Fence

**IN:**
- `src/firmware_ui_editor.h` (new), `src/firmware_ui_model.h`, `src/firmware_ui_send.h`, `src/firmware_ui_chrome.h`,
  `src/firmware_ui.cpp`;
- `test/test_firmware_ui_editor.cpp` (new), `test/test_firmware_ui_model.cpp`, `test/test_firmware_ui_send.cpp`,
  `test/test_firmware_ui_chrome.cpp`;
- `tools/probe_firmware_ui/probe_main.cpp` and `run.sh`;
- `tools/probe_board_ui/run.sh`, `expected.tsv` and `accounting.py` — the named additions only;
- `tools/probe_ui_model_mutations.py`;
- `tools/probe_board_abi.py` — its stock pins;
- the report and evidence (§5), including the supplemental ABI manifest.

**OUT:** every §2.10 file; `test_firmware_ui_input.cpp`, `test_firmware_ui_status.cpp` and
`test_firmware_ui_team.cpp`; the tool tests; the design, register, tracker, `MEMORY.md` and metal plan (QA lands);
historical evidence.

If an existing input, status or team test's expectation genuinely moves, or a tool test must change, that is
STOP-1: name it and get it fenced first.

## 4. Gates

### 4.1 The coder's gate

**Baselines, on the unmodified tree, before any edit** (the pre-checks' figures are the reference, re-measured):
1. Preflight (§1).
2. **Native:** `pio test -e native`, then `./.pio/build/native/program`: **3059 / 199638 / 0**.
3. **Corpus:** fresh stock `lus` (`e304147d…caa2`), `tools/run_corpus.py --require-anchors`: **36/36**, with s18 at
   **269517** events and MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`. Keep the full per-stream manifest.
4. **Board pair, with repeatability (W7W8R-3):**
   - `tools/measure_board.py pair --jobs=1 --output .pio-measure/w7w8/base-1`, then the same into `base-2`, on the
     ruled pair (gateway + heltec_mobile).
   - Then the stock `tools/measure_board.py compare` of `base-1/gateway/manifest.json` against
     `base-2/gateway/manifest.json`, and of `base-1/heltec_mobile/manifest.json` against
     `base-2/heltec_mobile/manifest.json`. Both must PASS.
   - Between the two runs of a sequence: no checkout edit, no evidence write, no native or probe build, and no
     normal `.pio/` change. Copy the per-environment manifests into the evidence only after both runs.
5. **Warning census:** `tools/warning_census.sh`, all six OLED envs.
6. **ABI:** the stock probe — **290 checks, 9/9 controls RED**.
7. **Firmware-UI default:** **591 / 1059 / 591**, **240 controls**, accounting complete.
8. **Board-UI default:** **592** identities exactly once (canvas 124/110, traits 14, missing traits 12,
   structural 23, wiring 60 + 186 controls, negctl 60/3).
9. **Tools discovery:** **447** OK. **Command inventory:** **197** rows.
10. **Static census:** the AST census of the union batteries (W8 §9) — selector (a) 375, selector (b) 225, union
    **15 batteries / 600 entries**.

**Checkpoints 1–4 (§0):**
- Run focused native and touched-battery work at each checkpoint, and run the full mutation union in development
  before the final chain (W6's lesson).
- None is a freeze.
- Never edit while a battery, build or measurement runs.

**The final chain, on one freeze:**
1. **Scope and inputs:** only §3's files and the evidence differ from the preflight inventory; every read-only input
   is byte-identical; `git diff --check` is clean; the simulator is clean.
2. **Native (W7W8R-3):** `pio test -e native` on the frozen tree, then `./.pio/build/native/program`; 0 failed,
   0 skipped; derive the new counts per added case. A binary from a checkpoint never stands in.
3. **Corpus:** fresh `lus`, 36/36, every per-stream field identical to baseline step 3.
4. **Boards:**
   - The pair into `final-1`, then `final-2`, under baseline step 4's rules; then the stock `compare` of `final-1`
     against `final-2` for gateway and for heltec_mobile. With the baseline's two, that is **four comparison
     PASS results**; the eight per-environment manifests go into the evidence.
   - Base to final is an attributed manifest / section / object / symbol comparison, not `measure_board compare`
     across sources.
   - heltec_mobile's linked RAM growth equals the §2.2 objects plus their section alignment. gateway is predicted
     unchanged, because its stock image has no OLED TU.
   - Flash growth is reported with its attribution.
5. **ABI:** the stock probe at §2.2's re-pins with all controls, plus the supplemental manifest.
6. **Warning census:** no new warning against baseline step 5.
7. **Firmware-UI default:** every new check and control, accounting complete; the counts derived.
8. **Board-UI default:** the manifest equals its baseline plus exactly the named additions.
9. **Mutation:** the union — selectors (a) and (b), the new `uieditor` and every new entry, B480 included.
   - All RED; zero unusable, vacuous or missing.
   - Every worker baseline at the new native floor.
   - Report each battery's count and every re-anchor.
10. **Tools discovery:** 447 plus additions, derived per file; OK, no skips. **Command inventory:** 197,
    byte-identical.
11. **Dependency safety nets:** both are inputs this package keeps read-only.
    - The inbox-verbs default (all five arms, including W0's real identity arms).
    - The two-profile stock transcript: 202/206 rows, with each fresh pair comparing 0.
12. **Reader audit (D6/P7):**
    - Every reader of a changed symbol across `lib/`, `src/`, `test/` and `tools/`: `SendKind`, `SendReq`
      (including aggregate initializers in tests and mutants), `HomeView`, `ComposeRow`, `ReviewPhase` if extended,
      `send_gate_of`, the trackers, `ui_nav_slot`.
    - Every whole-tree scanner.
    - Report each reader and whether its predicate moved.
13. **Input stability:** frozen-input hashes at the start and end of the chain.

### 4.2 QA's independent gate (P6)

On the frozen tree, QA re-runs:
- native, rebuilt (`pio test -e native`, then the binary), corpus, and the ruled board pair (RAM and flash). QA's
  own final manifests must reproduce the coder's qualification and measurement fields; any provenance difference
  is identified and explained;
- the ABI, stock and supplemental;
- the changed-source batteries: selector (a) plus `uieditor`;
- the firmware-UI and board-UI defaults with their accounting;
- the reader and preservation checks;
- tools discovery, because runners and the harness change.

The coder carries the full union and the warning census; QA may add a dependency battery with a reason.

**Not run, by either side:** metal (§7 lands its residue), and the full 109-battery union. The static census proves
unrun tables unchanged, not RED.

### 4.3 STOP conditions

- **Fence:**
  - an edit outside §3;
  - any change to `lib/`, wire, NV, console verbs, `fw_main.cpp`, the configuration service or the simulator;
  - a shared-fake or builder change.
- **Storage:** any retained field outside §2.2, or a size other than its pins (STOP-1, for measurement and an owner
  ruling).
- **Contract:** each is a STOP.
  - an implicit re-bind;
  - a written request reading the catalog;
  - `-l` on a written line;
  - a phrase behaviour change other than D19;
  - a send without review, or a save from anything but SAVE;
  - an emergency request touched by a withdrawal;
  - a second draft, a text copy or a queue.
- **Instruments:**
  - an existing control retired or weakened;
  - a control that does not produce its classified outcome;
  - a mutant that is not memory-safe, or that is credited RED by crashing;
  - a manifest change beyond the named additions.
- **Source facts:** a §1 fact found false is STOP-1 to QA. A measurement that disagrees with this brief is STOP-2.
  Never relax a rule.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8.md`, with an author line on line 1.
**Evidence:** `docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8/`, sealed by `SHA256SUMS` after the
last write. Raw logs go under ignored `artifacts/2026-10-04-standalone-mobile-home-w7w8/`, and board pairs under
`.pio-measure/w7w8/`.

**The report contains:**
1. preflight;
2. baselines;
3. each checkpoint's notes;
4. the diff and the §2.9 ledger;
5. every §4.1 figure, derived;
6. the classification of each changed assertion and control;
7. the size and RAM/flash attribution;
8. the exact line `PIN re-synced? YES — <derivation>`. The derivation states the native counts, corpus 36/36, the
   §2.2 sizes, the probe and battery counts, discovery and the inventory;
9. the freeze inventory;
10. not-run items, with reasons that the instruments' predicates are unaffected.

## 6. Owner rulings

- **Already ruled:** D4, D5, D6, D10 and D11 (2026-09-23); D16 and D17 (2026-10-03); D18 and D19 (2026-10-04); the
  B480/B481 folds (2026-10-03).
- **Author decisions within the design (r2.25–r2.27):**
  - the name review rows and `WAS` at review open;
  - the result strings and the request seam;
  - the prompt's placement, `SKIP`'s re-check and its alarm return;
  - the draft window, note dismissal and landings;
  - the DM SEND rail;
  - the binding rules, `DRAFT CHANGED`, `BUSY` and the `draft_id` sequence;
  - the typed WRITE row.
- **Returning to the owner:** any allocation beyond §2.2, or a contradictory interpretation.

## 7. Landing (QA, on PASS)

- **Register:**
  - **B480** and **B481** CLOSED.
  - **B444** stays OPEN, with its panel path closed by D19.
  - §0 rewritten in place.
- **Design:** the §13 W7 and W8 rows. **Pointers:** `tracker.md` and `MEMORY.md`.
- **Metal plan (M2):** add **EDIT-01** (W8 §11), and the new observations for UI-04/UI-06 (the prompt), UI-13
  (labels after a rename), UI-15 (written messages and the D19 line), UI-16/UI-17, UI-19 (gateway rename) and
  POWER-01, with the exact panel lines frozen from this brief.
- **Next:** the owner's pick — W5, W9, W4c→W4d, W1b, or the open follow-ups.

## 8. Revision history

**Revision 1 (2026-10-04)** is the first draft, built on the W7 and W8 pre-checks and design r2.26. It adopts:
- the pre-checks' module boundary, the request seam, the gate table, the failure classification, the tracker
  classification, the projection branch, the probe instruments and the gates;
- the owner's D16–D19 and the B480/B481 folds;
- the author's r2.25/r2.26 details.

**Revision 2 (2026-10-04)** folds in the brief review (HOLD, W7W8R-1–W7W8R-3). The review reproduced D18's
+208 B on every ABI and accepted the rest.
- **W7W8R-1:** a note owns row 0 alone, with the counter hidden (§2.1, design r2.27). The probe pins exact rows for
  all five notes, including over a full-length draft (§2.8).
- **W7W8R-2:** D19's check runs at execution only. Review admission keeps today's validation and never applies it;
  the two-phase traces are required for both ordinary team kinds (§2.4, design r2.27).
- **W7W8R-3:** the final chain rebuilds native on the frozen tree; four stock board `compare` PASS results, base and
  final, with the sequence rules and the eight manifests in the evidence; QA rebuilds and reproduces the coder's
  manifest fields (§4.1, §4.2).
- **Pins:** the design (r2.27), register, tracker and MEMORY are refreshed; the review receipt and its seal join
  the preparation set.

Nothing else changes.
