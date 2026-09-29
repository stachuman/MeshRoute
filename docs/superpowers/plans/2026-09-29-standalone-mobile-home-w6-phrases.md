<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W6 — saved phrases of up to 163 bytes: catalog v2, paged replies and a review before every send (`src` + NV feature)

**Revision 2 — 2026-09-29 — for QA's scoped re-review.** The [brief review](../evidence/2026-09-29-standalone-mobile-home-w6-brief-review.md)
held revision 1 (`b89b7743…066b`) on W6R-1–W6R-3; this revision folds them in (§8).

- **Base:** owner commit **`70ff486`** (`70ff486b40c9b07001b33bcbcdf640ad494c1149`, "W2"), with W2 and B459/B461
  committed and no uncommitted candidate. The pre-check's `inputs.json` is the authority for the whole tree (§1). The
  simulator is at **`6585649`** (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean and read-only.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 the brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file.
- **Authorities:**
  - the [W6 QA pre-check](../evidence/2026-09-29-standalone-mobile-home-w6-precheck.md) — cited as "pre-check §n";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) **r2.23** — §7.1–§7.3,
    §7.4 and §7.4.1 for saved phrases, §7.7–§7.9, §11.1, §12 D5–D9, D14 and D15, and §13 W6;
  - the [register](../../2026-07-30-open-bug-register.md): B335 and B475, which W6 closes; B476 stays separate;
  - the companion contract `ios-companion/INBOX_SYNC_CONTRACT.md` and `docs/manual/command-reference.md`, both
    changed by W6;
  - the [metal plan](../../2026-09-20-metal-test-plan.md) — landing only;
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1–C4, U1–U3, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md), including the owner ruling of 2026-09-16 on the coder's versus
    QA's gate.

## 0. What this is

**The feature (design §7, D5–D9).** A saved phrase holds up to 163 bytes, end to end:
- **The record:** `/mrui` version 2 stores 163 bytes per slot. An old v1 record boots as the compiled defaults and
  says so at every boot, with zero boot writes; nothing reads its contents (D8).
- **The verbs:** `ui preset set` accepts 1..163 bytes, and the end record gains `text_max`.
- **The replies:** `ui preset list` answers in pages of at most four records (D14), so no reply outgrows the USB
  console stage (B475). The reply-line buffer is derived from the widest record; the boot diagnostic has its own
  smaller bound.
- **The panel:** a phrase row shows its first 16 bytes and `»` when longer. A double on a phrase opens a review of
  the whole text — word-wrapped pages, `LOC` when the phrase requests location, `BACK` preselected — and only
  `SEND` there queues it (D5).
- **The send path:** a phrase request binds the team and, for a DM, the peer's hash, and both are checked again at
  execution. The send line becomes one static 199-B buffer.
- **The defaults:** eight phrases (D9).

**Owner rulings:** D5–D9 (2026-09-23); **D14**, pages of four, and **D15**, +303 B of board objects beyond the
catalog (+319 on the host), both 2026-09-29.

**Not in W6:**
- the editor, names and rename (W7); written messages, drafts and the `dm_text`/`channel_text` kinds (W8);
- a per-message location switch;
- B476 (BLE `tx_line`), B460 and B462–B473;
- any Home, Inbox-order or splash change;
- `lib/`, the wire, the simulator, `/mrcfg`, the partition table and `platformio.ini`.

**The proof:** one feature package with a closed **disposition ledger** of every changed, retired or added
expectation, control and mutation (§2.9). Product behaviour changes by design — a double on a phrase no longer
sends — so nothing here is byte-identical except where §2.10 says so.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

"Model" is `src/firmware_ui_model.h`; "send" is `src/firmware_ui_send.h`; "verbs" is `src/firmware_ui_preset_verbs.h`.
The complete per-seam facts are in pre-check §3–§10; the brief relies on them.

| Seam | Fact at the base | W6 change |
| --- | --- | --- |
| `device_nv.h`: `UiPresetSlot`, `UiPresetBlob`, `kUiPresetTextMax`, `kUiPresetMagic`, `kUiPresetVersion`, `UiPresetRead`, `ui_preset_blob_state`, `load_ui_presets` (`:~340–389`, `:~926–960`, `:~1504`) | Slot 21 B, blob 372 B, `'MRU1'` version 1, 3 named tail bytes; four read results | §2.1 |
| `firmware_ui_presets.h`: `validate_preset_text` (`:~329`), `preset_boot_line` (`:~290–296`), `preset_defaults`, `PresetCatalog::{begin, read_store, commit}` (`:~501`, `:~615`, `:~646`) | 1..17 bytes; two diagnostic lines; five defaults; three blobs, 1144 B host / 1132 B boards | §2.1, §2.2, §2.6 |
| Verbs: `kPresetLineMax` (`:~78`), `preset_emit_record` / `preset_emit_list` / `preset_emit_err` (`:~183–200`), `preset_boot_restore` (`:~229`), `preset_render`, `preset_verb` (`:~286–332`), the end writer (`:~155`) | 160-B buffers; `list` emits all 17 plus the end; a token after `list` prints the usage line | §2.2 |
| `firmware_commands.cpp`: `PresetPrintLines` (`:~192`), `preset_boot_restore_console` (`:~220`), `handle_ui`'s usage line (`:~229`) | The one adapter for USB and BLE | §2.2 (the usage text only) |
| Model: `kComposeTextCols` (`:~1566`), `ComposeSlot` / `ComposeList` (`:~1579–1594`), `compose_project` | 20 B / 161 B rows; text copied to 17 columns | §2.3 |
| Model: `compose_gesture` (`:~5838`), `queue` (`:~3907`), `take_send_request` (`:~3436`), `on_preset_changed`, `close_compose`, `emergency_gesture`, `unblank`, `refresh_detail_page`, `on_gesture`, `on_tick`; `detail_line` (`:~2686`, `kDetailBodyRows` = 2) | A double on a phrase row queues the request directly (UI-10/11 P3) | §2.4 |
| Model: `SendKind`, `SendReq` (`:~1785–1805`, 8 B) | `{kind, peer_id, slot, generation}` | §2.5 |
| Send: `kSendLineCap` (`:~472`, 96), `send_gate_of` (`:~493`), `ui_compose_send_line` (`:~534`), `ui_perform_send` (`:~576`, `char line[kSendLineCap]`) | Gate: emergency first, then generation, slot, enabled, kind | §2.5 |
| `firmware_ui.cpp`: the compose renderer (`ui_fmt_compose_row` / `draw_compose`), frame freezing, the executor wiring in `mr_ui_tick` | Rows drawn from the frozen snapshot | §2.3, §2.4, §2.5 |
| Console stage (`src/console_sink.h`, `MR_CONSOLE_STAGE_BYTES` = 2048) | Pumps between lines; a stalled host loses whole lines past 2048 B (pre-check §5) | **Unchanged**; §2.8 proves every reply fits |
| `tools/probe_inbox_verbs/fakes/Preferences.h`: `MrProbeNvSlot`, `MrProbeNv::put`, `Preferences::putBytes` (`:~43–49`, `:~74–82`, `:~115–120`) | Each record slot holds 2304 B; `put` silently skips a larger record while `putBytes` still reports the full length, so a 2852-B `/mrui` write would look saved (B477) | §2.8: capacity and truthful retention |
| `tools/probe_deferred_actions/run.py` (`:~140–172`) and `probe.cpp` (`:~4`) | Reuse the inbox runner's builder prefix — up to its one `rc=0` / `if ! build_support;` boundary — its fakes, and its `probe_main.cpp` | **Unchanged**; §2.8 keeps the builder contract, and both gates rerun it |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at the base.

| File | SHA-256 | Lines |
| --- | --- | ---: |
| `src/device_nv.h` | `47ca6053e56e4268c78b1044dbadad8e7d3eee2baf5258822cdf7df7912eac54` | 1564 |
| `src/firmware_ui_presets.h` | `7f0c2bbacf4e203229a2f15dfadecdf49f0973698d18fd99fd90bf19907827a3` | 705 |
| `src/firmware_ui_preset_verbs.h` | `2c003d4d29ed592cfd19d576a582a7edea68406f0dcd78f863beef2f1ac329dd` | 394 |
| `src/firmware_ui_model.h` | `ddf0025148a7723b24b31e0893ee7017506ce2d14b2c17c2e381fd0b76e3e286` | 6212 |
| `src/firmware_ui_send.h` | `0a12ffc21cef8c7d55abf06add55bcac1bf6da2f436da0fe3652d418eb0ce44d` | 699 |
| `src/firmware_ui.cpp` | `3d32d7cb9c81f39b7188861c973b8213261783810dc6ca8428c0fe305b9e9fe9` | 2873 |
| `src/firmware_commands.cpp` | `3eea954914b6b3d788dbe9c672972578da15144bd8f6944ae65d3367ef13893f` | 1915 |
| `test/test_device_nv.cpp` | `93865b037b534addf7f7bfcbc084d94372a30bbf91cbe70205bf26b1085d1212` | 930 |
| `test/test_firmware_ui_presets.cpp` | `d0f321aec4b96db54feb4d0dcb74d2eccf7180db3da493068d713de5677f49dd` | 904 |
| `test/test_firmware_ui_preset_verbs.cpp` | `a720c95b488aefa9c3ea04f8d9a249a81b67a875746ff91013cd8f5ab56b5b64` | 520 |
| `test/test_firmware_ui_model.cpp` | `3ac8c6f2457c4d784a0661a531ef467ace82ba55f675c156abf4180a130ff6c7` | 11944 |
| `test/test_firmware_ui_send.cpp` | `66527ed7c3d858fb8767e522a3fb19da86f225ce81e5c59ce2368ba68adba0dd` | 2856 |
| `test/test_firmware_ui_chrome.cpp` | `544875a89e6683d2cb50c33591f828d189925376369e60acc9d31f06b098641f` | 1378 |
| `tools/probe_firmware_ui/run.sh` | `107f243425bd7922241550d6fd47bc637ff02e913df0494965288de4c925fdcd` | 2037 |
| `tools/probe_firmware_ui/probe_main.cpp` | `fb45d28e69b0e7ae280c77d91840df659ade5d9f4784837f6317d84793aff9d3` | 7691 |
| `tools/probe_console_sink/run.sh` | `f543dbf1829bd09d2e672bdca1f32456b358cb0b5addc507a96f920061588b73` | 457 |
| `tools/probe_console_sink/probe_main.cpp` | `f087651e66daaaca4238042438d60371b79a4817d4659397fd96345efaaa9eb4` | 556 |
| `tools/test_probe_console_sink.py` | `4a3c3eae62dbdbe982f226c44777b974008abcf5920628ead907b8cdc2a90a1f` | 376 |
| `tools/probe_inbox_verbs/run.sh` | `06d49b25e3126970ac30e9e279d844a433f3b2f1fd1c994ebad762f43fcf76c4` | 873 |
| `tools/probe_inbox_verbs/probe_main.cpp` | `e6f6ef9f13431b7bbb2560dd4eeea51f84acb5526977ec0b4d9c8b55327bbab6` | 2116 |
| `tools/probe_inbox_verbs/fakes/Preferences.h` | `67e329fb4f21a90e2c603002e4a6d206d07c7e7f4427d0496ba32c67f382a68b` | 127 |
| `tools/probe_board_abi.py` | `4a5c0997fff20fb3c04cfd75c2527fd09f0b55edfb9b3f273c3afc99f0b2a579` | 1069 |
| `tools/test_probe_board_abi.py` | `c6630098421aed5fc2ad54ea29f512e624ed6ba1f9d19b188a0b1632707951c3` | 524 |
| `tools/probe_ui_model_mutations.py` | `81842cc53d806f1ed4dbce5bb7699a945e50dc6381f8258cf3c60eec73e2781a` | 12699 |
| `ios-companion/INBOX_SYNC_CONTRACT.md` | `7528854944693b3ff5d3db0735cc6a69f3220c9a737aaa985fab0d83faa7fd82` | 1387 |
| `docs/manual/command-reference.md` | `b438fdcdd203e272ca12ac41c57ec8c5f5a47b0e02ee0f7915c64b6d654f9ecf` | 571 |

**Read-only, rerun by both gates:** `tools/probe_deferred_actions/run.py` (`694387bd…b55f`, 190 lines) and
`tools/probe_deferred_actions/probe.cpp` (`57364be0…e587`, 222 lines).

**Base inventory.** The pre-check's `inputs.json` (1,977 MeshRoute paths, covered by its `SHA256SUMS`) is the authority
for every other path. Since QA wrote it, only the four preparation documents below have changed. New paths since
then: the pre-check report and folder, this brief, and QA's review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.23) | `d6ad9a12443eb545cb4db181d5b19fcd35f7d02840f47aa87b20ee5ee5d34bca` |
| `docs/2026-07-30-open-bug-register.md` | authority (B335, B475–B477; §0) | `b78da31ba37ad694f1817265829161fce177d14fc34f2d47fd3746a0bd0a1e85` |
| `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck.md` | authority (pre-check) | `3b7305a06b699e6188a531b0395b767a0ac7aa0ccf8178bb4879ac87d94831e8` |
| `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck/SHA256SUMS` | evidence — 45 entries, including `inputs.json` (`3d318f22…76f8`) | `dfd4b52d1e28717e80d7fb1f707ad4c6115a5b50551ba0adf50afc091023b039` |
| `tracker.md`, `MEMORY.md` | context pointers | `b9472fd2a1e421f28d6dad626bcac3743a17a315bae52aeaed238e71d0d07e21`, `b90794dab03a4f8c5073b145a69557d884892878564cf12ae96140fb58834d0b` |

The design changes are r2.23: the D14/D15 rulings, the pre-check's contract completions, and the W6R-2 erratum
(`reset all` at most 1517 B). The register changes are B475's ruling and corrected maximum, B335's corrected D8
wording, and the W6 entry in §0; QA registered B476 at the pre-check and B477 at the brief review. This brief is
pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `70ff486`; the simulator at `6585649` and clean.
2. Every executable input matches its hash above.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 45 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 The catalog record, version 2 (design §7.7–§7.8 r2.23; pre-check §3)

**Layout — measured by the pre-check, pinned on all three ABIs:**
- `UiPresetSlot`: `enabled`, `loc`, `len` (three `uint8_t`), then `text[kUiPresetTextMax + 1]` — **167 B, align 1**;
- `kUiPresetTextMax` = **163**;
- `UiPresetBlob`: the unchanged 12-B header, 17 slots and **one** named tail byte — **2852 B, align 4**;
- the magic stays `kUiPresetMagic` (`'MRU1'`), and `kUiPresetVersion` becomes **2**;
- `PresetCatalog`: **8584 B host / 8572 B boards**, exactly +7440.

Keep **17 slots**. Update the assertions on 21, 372, `text[18]`, `tail[3]`, 1116, 1144/1132 and 160 by derivation.
Do not replace every literal 17: the display width and the slot count remain 17.

**Reading — five classified results.** `UiPresetRead` gains `old_v1`; the four existing results keep their meaning.
The branch order:
1. a backend failure;
2. absent, and over-size, as today;
3. **size 372, magic `'MRU1'` and version 1** — this is `old_v1`, tested before the exact-v2 check;
4. the exact-v2 and canonical checks.

Nothing interprets an old record's slots, flags, generation or padding, and no migration struct or parser exists. A
test feeds an old header with a nonsensical payload and gets the same `old_v1` result.

**The boot line:** exactly `  ui presets = DEFAULTS (old v1 record — re-enter custom phrases)`, indented and spelled
like the other two lines, through `preset_boot_line`.

**The state table — driven through the real classifier, the catalog and the boot emitter:**

| Stored record | At boot | Boot writes | Later mutations |
| --- | --- | --- | --- |
| absent | the compiled defaults, no line | 0 | as today: restating a default is a zero-write no-op; a real change writes v2 |
| canonical v2 | the stored values, no line | 0 | as today: equality coalescing, save before publish |
| `old_v1` | the compiled defaults, the old-v1 line at **every** boot | 0 | the first successful mutation replaces the record, even when it restates a default — it is a repair |
| any other size, header or canonical failure | the defaults, the existing invalid line | 0 | as today: a repair may write; corrupt data is never an unchanged baseline |
| `io_failed` | the defaults, the existing unreadable line | 0 | as today: every mutation refuses `store`, with no write |
| a failed save | the previous live bytes and generation | — | no publish; retry as today |

`PresetDiag::boot` already holds the read result, so no retained counter is added.

### 2.2 Validation, verbs and replies (design §7.7 r2.23, D14; pre-check §5–§6)

**Validation.** `validate_preset_text` accepts 1..163 bytes of the same grammar: printable ASCII 0x20–0x7e, no `"`
or `\`, at least one non-space. Its canonical-validation and mutation callers agree at 163 and 164, including the
padding and empty disabled slots.

**The end record** — exact bytes and field order:
- `reset all`: `{"ev":"ui_presets_end","capacity":17,"text_max":163,"dm_active":<n>,"channel_active":<n>,"generation":<g>}`;
- a `list` page: the same, with `,"page":<n>,"pages":5` before the closing brace.

`capacity` stays the slot count (`kUiPresets`) and `text_max` is `kUiPresetTextMax`; both are asserted independently.
The overlength arm of `bad_text` uses `text_max`. Empty, all-space and forbidden-byte text still answers `bad_text`,
never a new reason.

**The paged list (D14):**
- `ui preset list [<page>]`, with a named page-size constant of **4**. The page count (**5**) is derived from it and
  the slot count, never written as a literal.
- Page *n* lists the slots in [4(*n*−1), min(4*n*, 17)) in record order — `emergency`, `dm1`..`dm8`,
  `channel1`..`channel8` — every slot in range, enabled or not. Page 5 holds only `channel8`, and every slot appears
  on exactly one page.
- A bare `list` is page 1.
- `<page>` accepts only the canonical decimal spellings `1`..`5`. Any other token, or a second token, answers
  `{"ev":"ui_preset_err","reason":"bad_page"}`. This replaces today's usage-line answer for a token after `list`.

**Unchanged replies:**
- a mutating verb (`set`, `clear`, `reset <slot>`) answers with the one resulting record;
- `reset all` answers with the full list of the compiled defaults — 17 records plus the end record, without `page`
  fields; at most **1517 B** at the D9 defaults, with a ten-digit resulting generation (1508 B with a one-digit one);
- `busy`, location, save errors and coalescing behave as today.

A save that succeeded is never reported as failed merely because its reply could not be delivered.

**The reply line and the boot bound:**
- `kPresetLineMax` is derived by `static_assert` from the field spellings, the longest slot name, the u32
  generation's digits and `kUiPresetTextMax`: 244 B with the NUL. The real writer's maximum returns 243.
- The console emitters — a record, a page, the full list and an error — size their buffers from it.
- `preset_boot_restore` uses its **own** bound, derived with `sizeof` over the actual diagnostic strings, including
  their two leading spaces and the new old-v1 line (81 B today).
- Stack peaks are measured on the final candidate (§4.1); nothing claims no growth.

**Text outside the source:**
- In `firmware_commands.cpp`, only text changes, and every edit keeps its line count, so the file stays at 1915 lines
  and the tracked command inventory's anchors do not move (W6R-3):
  - `handle_ui`'s usage line spells `ui preset list [<1..5>]`;
  - the comments W6 makes false are corrected in place: "six reason spellings" (`:~171–175`) becomes seven, with
    `bad_page`; the "FOUR-valued read" (`:~201–205`) becomes five, with `old_v1`; "three 372-B records" and
    "≈ 1.15 KB" become the v2 records and the measured catalog size;
  - the historical stack rationale stays as history. It must not describe the enlarged catalog as a current
    1.15-KB allocation, or suggest that a headless nRF board owns this OLED instance;
  - no executable line, and no board-UI anchor, changes.
- The companion contract's `ui preset` section (`:~1215–1310`) changes together, per pre-check §6:
  - record version 2, and text 1..163 with the advertised `text_max`;
  - abbreviation on the panel, and review of the full text; the old single-row reason for the limit is withdrawn;
  - the end-record schema with `text_max`, and `capacity` still 17;
  - the paged list, with its generation-restart rule and `bad_page`;
  - the old-v1 boot line, zero boot writes and no migration.
- The manual's `ui preset` rows (`docs/manual/command-reference.md :~290–311`) change to match.

### 2.3 Compose rows stay 20 B (design §7.7 r2.23; pre-check §7)

`compose_project` sees the whole live slot. It copies all bytes when `len` ≤ 17; otherwise it copies 16 and appends
Latin-1 `0xBB` (`»`) and the NUL. There is no length field: `ComposeSlot` stays **20 B** and `ComposeList` **161 B**.
Neither the renderer nor any later sanitisation pass may turn the appended `0xBB` into another character.
`kComposeTextCols` stays 17. The sender and the review read the live catalog, never the projected text.

### 2.4 The review for saved phrases (design §7.2, §7.3 and §7.4.1 r2.23, D5; pre-check §7)

**Entry.** A double on a phrase row opens a review bound to that phrase, with `BACK` selected. It queues nothing,
touches neither tracker and submits nothing to the core. W6 inserts the review **before** queueing — never a second
send action after a request is already queued.

**Capture.** A request/answer step in the UI tick, as when the Inbox opens a message:
1. resolve the bound slot and generation against the live catalog;
2. obtain the destination's raw name and hash;
3. copy the exact review bytes before a frame starts.

Rules for the capture:
- It never marks `_req_pending` and never calls the executor.
- No pointer into the catalog is kept.
- No frame reads the live catalog or a live name while it draws.

**State — the D15 shape (§2.7):**
- The review's three page rows share the Inbox's `detail_line` storage through a union: `review_line[3][20]` over
  `detail_line[2][20]`. The Inbox keeps its two-row type and its 241-B byte pager byte-identical;
  `kDetailBodyRows` stays 2.
- `UiState` also gains a 20-B review header, one phase byte and two flags.
- The model's existing 242-B body, length, page, page count and timer are reused, because the detail modal and the
  review are never open together. Tests prove that exclusivity (§2.9), never its size alone.

**The screen, per design §7.3:**
- **Row 0**, one of three forms:
  - `TO TEAM <8 hex>` for a team phrase;
  - `TO <label ≤ 7> <8 hex>` for a verified DM — the full raw name through `ui_fmt_identity` at 7 columns;
  - `TO T<n> UNVERIFIED` when no hash is known.
- **Rows 1–3:** one page of the word-wrapped text.
- **Row 4:** `SEND` and `BACK`, `BACK` first selected; `LOC` at zero-based columns 12–14 only when the bound phrase
  requests location; the page token (for example `1/2`) fits the 19 columns.

`LOC` states the phrase's request only. It never promises a fix, and it never suppresses the core's existing
no-fix refusal.

**The word wrap** is a pure, counted-byte helper that follows §7.2's branch order exactly: three lines per page, at
most six pages for 163 bytes, and the concatenated lines equal the payload. W3's `detail_page_rows<19,2>` byte
pager stays unchanged for the Inbox.

**Transitions.** Pre-check §7's table is the contract. In summary:

| Event | Behaviour |
| --- | --- |
| short on the review | toggles `SEND` ↔ `BACK` only |
| double `BACK` | returns to that phrase's list, arrow on it |
| double `SEND` | queues exactly once, through the existing `queue` path |
| a page change | the existing `kDetailPageMs` cadence, time-driven only — never counted as input, never a send |
| blank | the review and its page are kept; the action resets to `BACK`; the cadence is suspended |
| wake | the press is consumed and the cadence restarts on the same page |
| `long_arm` | the review is kept, reset to `BACK`; emergency precedence is unchanged |
| `long_fire` before `SEND` | the review closes; after the alarm, the phrase list; no ordinary request |
| team change | the review closes → the phrase list with `TEAM CHANGED`; Home if no team remains |
| DM identity change or loss | the review closes with `RECIPIENT CHANGED`; a removed teammate takes the existing Team-list `TEAMMATE GONE, pick` row |
| generation moved | the review closes → the list with `PRESET CHANGED` (the existing rule) |
| after `SEND` | the saved-phrase column of §7.4.1: a press closes the view while the request still executes in today's slot order; an alarm leaves it queued; a result press returns to the phrase list. W8's draft withdrawal is not imported |
| reboot | the review and any request are lost; the catalog restores by §2.1 |

**Preserved as they are:** grant rows, top-level `MENU`, body/rail identity, overlay precedence, late-ACK handling
and `FrameGate`'s Inbox read watermark. A review never makes the Inbox read.

### 2.5 The send path (design §7.4 for phrases; pre-check §8)

**The request.**
- `SendReq` becomes **16 B**: `{kind, peer_id, slot, generation}` plus the bound `team_id` and, for a DM,
  `peer_hash` with a separate **known** bit held in the existing padding. A zero hash value is never read as
  "unknown". Today's resolver never produces a known zero hash — `Node::team_key_set` rejects hash zero, and
  `ui_fmt_identity` answers `none` for an unnamed zero hash — so the known-zero gate case is a labelled synthetic
  one. Neither `lib/` nor the resolver changes to manufacture it.
- No `draft_id`, and no new `SendKind`: those are W8's.
- The **review binding** (16 B, in `UiModel`) is kept apart from `_req`. A confirmed request that is still owed —
  for example across an alarm — is never overwritten by a later review.

**The gate.** `send_gate_of` keeps emergency first and unconditional. For the ordinary kinds it then checks, before
any submission:
- the existing generation, slot, enabled and kind checks;
- **team equality**, against `g_node.config().team_id`;
- for a DM with a known hash, **hash equality**, through the existing `Node::team_key_of_id` authority or its public
  adapter.

Each refusal has its own typed note and zero core submission. Nothing uses an ID-indexed static array or a cached
six-cell label.

**The line.**
- `kSendLineCap` is re-derived by `static_assert` from the named constants (199 B: `send_channel ` + 10 promoted
  digits + ` "` + 163 + `"` + ` -t -l -e` + NUL).
- The line becomes **one static buffer owned by `firmware_ui.cpp`**, passed into the pure operation. It must not
  become one static array per TU or test instantiation, and it must not be live through a recursive executor.
- The composer still reads the live slot, and 163 body bytes arrive intact in all six kind × fix combinations.

**The emergency path is unchanged,** with the same location policy (§7.9). The pre-check's fixture — a maximum
163-byte emergency phrase on a three-digit channel — composes 188 command bytes without a fix and 191 with one;
real phrases and channels can be shorter.

### 2.6 Defaults (D9)

`preset_defaults` keeps emergency `I'm in danger` (location on), `dm1` `Are you OK?`, `dm2` `I'm OK`, `channel1`
`Got your message` and `channel2` `All good`. It adds `dm3` `Where are you?`, `channel3` `Return to base now` and
`channel4` `On my way`, all with location off. That gives 3 DM and 4 channel phrases, plus the emergency slot.

`Return to base now` (18 B) projects as the 17 cells `Return to base n»`, while the review and the send carry all
18 bytes. A canonical v2 record keeps its own values and never receives new defaults.

### 2.7 Allocation — owner-ruled (D15, design §11.1 r2.23; pre-check §9)

| Type | Base host / boards | W6 host / boards |
| --- | --- | --- |
| `UiPresetSlot` / `UiPresetBlob` | 21 / 372 | **167 / 2852** |
| `PresetCatalog` | 1144 / 1132 | **8584 / 8572** |
| `ComposeSlot` / `ComposeList` | 20 / 161 | unchanged |
| `SendReq` | 8 | **16** |
| `UiState` | 520 / 520 | **568 / 560** |
| `UiSnapshot` | 1368 | unchanged |
| `UiModel` | 944 / 936 | **1016 / 1000** |
| `UiChrome` | 20 | unchanged |
| the static send line | — | **199** |

On the boards that is +303 B beyond the catalog: the model +64, the frozen state +40 and the send line +199. With the
catalog's +7440, **+7743 B of board objects**. The linked RAM is measured and attributed down to the symbols (§4.1).

**Any other size or alignment, or any further retained state, is a STOP:** a reply cursor, a note payload, an
observer, a pointer or a label copy. So is a material excess of the linked catalog figure over its +7440 estimate,
which returns to the owner.

### 2.8 Real-path proofs

**The firmware-UI probe** (the real `firmware_ui.cpp`):
- compose rows with `»` and its unsanitised `0xBB`;
- the review's header forms, pages, action row and `LOC`, drawn from frozen frames — a catalog change during a
  frame does not tear the review;
- exact composed 163-byte send lines through the real composer into the faked executor;
- the static send line's single owned instance.

Existing direct-double fixtures must enter the review and confirm deliberately; never just lower their counts.
P29a's 241-byte Inbox pages stay two rows and byte-identical.

**An OLED-enabled router arm in the inbox-verbs probe** (the real `exec_console_line` → `dispatch` → `handle_ui` →
`PresetPrintLines` path):
- long `set` replies, every `list` page, `bad_page`, errors and end records, and `reset all`;
- a labelled emergency stub for the `busy` arm;
- **persistence (W6R-1):** after a successful 163-byte `set` through the router, the NV medium holds exactly the
  expected 2852-B v2 blob (`MrProbeNv::holds`), and a catalog reload through the real boot restore shows the
  phrase. A captured success line never substitutes for the stored bytes;
- the existing failure and no-publish cases, and the fake's deliberate `retain_on_fail` / `drop_on_ok` controls,
  stay as they are.

This is the first executed `ui preset` path through the command TU; both existing arms have OLED off.

**The NV fake (B477).** `tools/probe_inbox_verbs/fakes/Preferences.h` changes in exactly two ways:
- each record slot holds at least `sizeof(mrnv::UiPresetBlob)` (2852 B), and the probe asserts at compile time that
  every record an arm stores fits;
- `putBytes` reports the full length only when the medium actually stored the bytes. An over-capacity or no-slot
  write reports a short write and stores nothing, while the two deliberate dishonest controls keep their meaning.

Its regressions: a record of exactly the capacity is stored and read back; one byte more reports a short write and
leaves nothing behind; the dishonest controls still turn their checks RED. No production code learns about the
fake.

**The builder contract stays.** `tools/probe_deferred_actions/run.py` reuses the inbox runner's prefix up to its one
`rc=0` / `if ! build_support;` boundary, and `probe.cpp` includes the inbox `probe_main.cpp`:
- keep that boundary exactly once, and keep `build_support` and `build_variant` callable as today;
- keep the OLED arm's code out of the deferred-actions build;
- if the deferred-actions probe's source must change, that is STOP-1.

**The stage proof** (console-sink, or the OLED router arm if it drives the real `GuardedConsole`), through the real
writer, verb, catalog, adapter and 2048-B stage:
- **Fits on its own:** on a clean stage with a transport that drains **nothing**, every `list` page at maximum
  length (the largest, page 4, is 1097 B) and `reset all` — with a ten-digit resulting generation and across the
  wrap (at most 1517 B) — fit whole: zero `CONSOLE_DROP`, and no reliance on pumping.
- **Pages and generation:** every slot, enabled or disabled, appears on exactly one of the five pages. A `set`
  between two page reads changes the `generation` the later page reports; with no change, the pages report
  equal generations — the companion's restart rule.
- **A draining transport** delivers everything.
- **Two controls:**
  - a 160-B line — every maximum record must disappear;
  - an unpaged seventeen-record list under the same non-draining transport — it must drop lines with
    `!! CONSOLE_DROP`, which proves the instrument sees an overflow.

If a proof needs a change to a **shared** fake (`tools/probe_board_ui/fakes/*` or `tools/probe_console_sink/fakes/*`),
that is STOP-1: a fence question. The inbox-verbs NV fake above is fenced; these are not.

### 2.9 The closed disposition ledger

The coder writes the ledger into the evidence, closed against the full diff. Every changed, retired or added native
expectation, probe check, control and mutation appears in it with its reason and its must-fail result; anything
else is a STOP. Each actual changed assertion or control is classified on its own — never by a broad category such
as "direct-double fixtures changed". Known entries (pre-check §3, §7, §10):
- `kPresetLineMax == 160` and the 98-B widest-record test → derived 244 / 243;
- the 21/372/1144/1132 pins → §2.7;
- the five defaults → eight;
- `1..17` → `1..163`;
- `list` → pages;
- the usage-line answer to `list <token>` → `bad_page`;
- every direct-double send → review then `SEND` — including `test_firmware_ui_chrome.cpp :~850–882` (a real
  outcome/rail test that sends on one double), UI-10/11 P3's cases and the firmware-UI gesture fixtures;
- `SendReq`'s size and fields;
- P26/P27/P29b/P28/P30 and controls C134–C141, each audited individually;
- the NV fake's capacity and truthful retention (B477), with its regressions.

**Mutations:**
- **Selector (a)** — the batteries whose source W6 changes, 383 entries at the base, all matching exactly once:
  `model` 238, `devicenv` 42, `uisend` 15, `uipresets` 32, `uipresetverbs` 19, `sliceCbudget` 1, `sliceCsend` 1,
  `w4aident` 9 and `w4bhome` 26.
- **Selector (b):** `chrome` 48, `uiteam` 20, `uiinvite` 32 and `consoleline` 12 — 495 in all.
- If the implementation touches the transitions or field carriers of `uistatus`, `uiprov`, `uijoin`, `provservice`,
  `config` or `teamkeyring`, add that battery to (b) and say so; otherwise state each one's exclusion.
- **New entries**, in the batteries of the files they mutate, must cover:
  - old-v1 recognition and its zero writes;
  - the 163/164 limits;
  - the page size and `bad_page`;
  - the whole-reply bound;
  - full-text capture;
  - `BACK` preselected;
  - no-send paging;
  - the team, hash and generation races;
  - the pending phrase's coexistence with an alarm;
  - the union's exclusivity.
- Changing a limit or moving a statement is never a reason to retire a safety property.

**Probes:**
- **board-UI:** W49–W54 read `firmware_commands.cpp`. W6 may not move any of their anchors, and the default run
  stays exit 0 with its 592-identity manifest unchanged. An anchor that must move is STOP-1.
- **ABI:** re-pin the §2.7 sizes on all three ABIs, and add supplemental measurements for any new review or binding
  type.
- **deferred-actions:** its default probe reruns with its source unchanged (§2.8's builder contract).

### 2.10 Nothing else moves

- **Corpus:** 36/36 streams byte-identical to the pre-check manifest, every per-scenario field included; s18 at
  `simulation/BASELINE.md`'s keystone. No `lib/`, wire or simulator input changes.
- **`gateway`:** headless, so its linked image is predicted **unchanged**.
- **Command inventory:** 197 rows, byte-identical under `--check`. `firmware_commands.cpp`'s line count is unchanged
  (§2.2).
- **Earlier packages:** W1c through W4b and B459 keep their independent properties. Their assertions change only
  where the §2.9 ledger records an intended W6 contract change — direct-double sends and default counts, for
  example — and the board-UI manifest is unchanged.

## 3. Fence

**IN:**
- the seven `src/` files of §1 — §2.1–§2.7 and their comments. In `firmware_commands.cpp`, only the usage text and
  the named comment corrections (§2.2), with the line count kept at 1915;
- the six `test/` files of §1 — the §2.9 ledger; no new test file;
- `tools/probe_firmware_ui/run.sh` and `probe_main.cpp` — §2.8 and the ledger;
- `tools/probe_inbox_verbs/run.sh` and `probe_main.cpp` — the OLED-enabled router arm;
- `tools/probe_inbox_verbs/fakes/Preferences.h` — its capacity and truthful retention only (§2.8, B477);
- `tools/probe_console_sink/run.sh` and `probe_main.cpp` — the stage proof, if it lives there; and
  `tools/test_probe_console_sink.py` only for an assertion that depends on it;
- `tools/probe_board_abi.py` — the §2.7 re-pins; `tools/test_probe_board_abi.py` only for a dependent size assertion;
- `tools/probe_ui_model_mutations.py` — the §2.9 dispositions, additions and PIN;
- `ios-companion/INBOX_SYNC_CONTRACT.md` and `docs/manual/command-reference.md` — §2.2;
- the report and evidence (§5).

**OUT:**
- `lib/`, the simulator, `platformio.ini`, partitions and `src/fw_main.cpp`;
- every other `src/` file, including `console_sink.h` (the stage size is fixed by B208) and `device_ble.h` (B476);
- the shared fakes (`tools/probe_board_ui/fakes/*`, `tools/probe_console_sink/fakes/*`), the board-UI and
  deferred-actions probe files, and every other tool or tools test;
- a new production header or TU, and a new test file;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7).

If a repair seems to need anything outside this fence, that is STOP-1: a fence question for the owner.

## 4. Gates

The owner ruling of 2026-09-16 (P6) gives the coder the full chain, and QA the `src`-only independent gate.

**Ordering:** the two runs of each board pair are back-to-back, and nothing runs between or during them. Mutation
batteries run separately from board measurements. No `--no-neg` result substitutes for a gate.

### 4.1 The coder's gate

**Baselines, on the unmodified tree:**
- **Native:** the count (pre-check: 3031/198613/0).
- **Probes:** firmware-UI all three arms (557/1023/557, 238 controls); board-UI default (592); console-sink
  default; inbox-verbs default; deferred-actions default.
- **ABI:** stock `probe_board_abi.py` (290 checks, 9/9 controls RED).
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w6/base-1`, then `base-2`, then the
  stock `compare` per env on the two `manifest.json` files; both must PASS.

**The final chain:**
1. **Hygiene:** `git diff --check`. The ledger is complete against the diff.
2. **Native:** a fresh `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts
   derived: 0 failed, 0 skipped.
3. **Corpus:**
   - build a fresh stock `lus` into a new directory outside both repositories:
     `cmake -S <simulator> -B <dir> -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=<MeshRoute>`, then
     `cmake --build <dir> --target lus`;
   - run `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest.
4. **ABI:** the stock probe at the §2.7 re-pins, plus the supplemental measurement for any new type; report the two
   separately.
5. **Probes:**
   - firmware-UI default, all arms, with every declared control accounted for;
   - board-UI default, exit 0, with 592 identities;
   - console-sink and inbox-verbs default, including the new arm, the persistence proof, the fake's regressions and
     the stage proof;
   - deferred-actions default, with its source unchanged;
   - report the counts against the baselines, and reconcile the ledger.
6. **Stack:** `-fstack-usage` frames for the console and boot paths on the `heltec_mobile` toolchain against the
   pre-check's base figures (§6 there), with the same caveat that these are compiler frames, not a task high-water
   mark.
7. **Tools discovery** (D5): `python3 -m unittest discover -s tools -p 'test_*.py'`. Report the count derived, OK,
   0 skipped.
8. **Checkers:** `python3 tools/gen_command_inventory.py --check` (197 rows), the authority and ownership checkers,
   and `tools/warning_census.sh` on its six pinned OLED envs — zero new warnings, `-Wswitch` 0.
9. **Boards, final.** Run `.pio-measure/w6/final-1` and `final-2`, then the stock `compare` per env; both must PASS.
   - **Attribution.** Read the `base-1` and `final-1` manifests field by field: every `measurements.*` and payload
     difference is attributed, down to symbol level for RAM — the catalog, `s_model`, `s_frame_state` and the send
     line.
   - **Predictions:** `gateway` unchanged; the catalog's linked figure reported against +7440.
10. **Mutation union:** selectors (a) and (b) with the §2.9 dispositions and additions.
    - Every configured entry must be RED, with 0 unusable and 0 vacuous.
    - Every worker's clean baseline equals step 2's counts.
    - A non-RED entry is reported with its capture; QA disposes of it, and it is never silently accepted.
11. **The pin line:** `PIN re-synced? YES — <derivation>`.

**Reader audit (D6/P7), before the freeze.** Grep `lib/`, `src/`, `test/` and `tools/` for every reader of the changed
statements and symbols, including the directory scanners and the board-UI structural readers. Pre-check §10's
ledgers are the starting point. Rerun any check whose predicate the edits touch.

**Input stability:** inputs are recorded before the baselines and after the last step. They differ only by the fenced
edits and new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate (P6, `src`-only)

On the frozen tree, QA re-runs:
- **Native** — a fresh `pio test -e native`, then the binary;
- **Corpus** — 36/36 byte-identical;
- **Boards** — its own `pair --jobs=1` into a fresh `.pio-measure/` directory, read field by field against the coder's
  `final-1`;
- **Selector (a)** — the nine batteries with W6's additions;
- **The affected probes:** firmware-UI, board-UI default, console-sink and inbox-verbs — including the new arm, the
  persistence proof, the fake's regressions and the stage proof — and deferred-actions default; the stock ABI probe
  plus the supplemental measurement.

The union's selector (b), full discovery and the warning census stay in the coder's chain.

### 4.3 STOP conditions

- **Allocation:** any size, alignment or retained state other than §2.7's.
- **Replies:** a reply shape other than D14's; a stage-size change; a truncated record; a silently omitted end record;
  an indefinitely blocking flush.
- **Record:** a migration reader or struct for v1; a boot write on any arm; old-v1 content interpreted.
- **Review:** a phrase that can send without the review; a review opened with `SEND` selected; a page change that can
  send; a review that overwrites an owed request; a frame reading the live catalog.
- **Ledger:** any changed, retired or added expectation, control or mutation absent from the §2.9 ledger, or any
  weakened.
- **Fence:** an edit outside §3; a moved board-UI anchor; a changed `firmware_commands.cpp` line count or executable
  line; a shared-fake change; an NV-fake change beyond capacity and truthful retention; a changed inbox builder
  contract, or any deferred-actions source change.
- **Controls:** a new or re-anchored control that is vacuous, does not compile, crashes, or goes RED only on unrelated
  checks.
- **Census:** a warning-census failure.
- **Boards:** a failed repeatability `compare`, an unattributed delta, a `gateway` image change, or a RAM increase
  that §2.7 does not explain.
- **Corpus:** any stream delta.
- **Inputs:** a mismatched input at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false, or a transition that design r2.23 and pre-check §7 do not settle — STOP-1 to
  QA; never a coder assumption.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6/`. It holds:
- **Board manifests:** the eight per-env manifests, copied from `.pio-measure/w6/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **The ledger:** the §2.9 disposition ledger;
- **Measurements:** the ABI and supplemental sizes, and the stack frames;
- **Durable receipts:** compact native, probe, discovery, census and union summaries; the full corpus manifest and
  field comparison; the locations and hashes of the raw logs;
- **The reader-audit greps;**
- **A `SHA256SUMS`** covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md):
- raw logs go under ignored `artifacts/2026-09-29-standalone-mobile-home-w6/`;
- raw board outputs stay under `.pio-measure/w6/`, and the stock tool's `.pio-measure/env/` roots are not relocated;
- native builds use `.pio/`;
- simulator and mutation scratch builds stay outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baselines.**
3. **Diff:** `git diff --stat` and the ledger.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Allocation:** the §2.7 sizes on three ABIs, the object sum, the linked-RAM attribution, and the catalog's linked
   figure against +7440.
6. **Controls and mutations:** the label reconciliation, and the union by battery.
7. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`.
8. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
9. **Not run,** with reasons. Each reason is that the instrument's predicates are unaffected — confirmed by the reader
   audit — never "it reads none of the files".

## 6. Owner rulings

- **D5–D9** (2026-09-23): the review before every send, 163 bytes for every message, T = 163 for phrases with the
  catalog estimate accepted, no v1 migration, and the eight defaults.
- **D14** (2026-09-29): `ui preset list` answers pages of at most four records.
- **D15** (2026-09-29): +303 B of board objects beyond the catalog (+319 host), in the §2.7 shape.
- **Nothing else is requested.** The pre-check's contract completions are recorded in design r2.23 as author
  decisions within these rulings.
- **A contradictory interpretation** returns to the owner.

## 7. Landing (QA, on PASS)

- **Register:** **B335, B475 and B477 CLOSED** in place; B476 stays OPEN; the §0 dispatch is rewritten in place.
- **Design:** the §13 W6 status.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **Metal plan (M2)** — pre-check §4 and §11, adapted at landing:
  1. **UI-12:** the eight defaults on glass; `Return to base n»`; `ui preset list 1`..`5` over USB with no
     `CONSOLE_DROP`; the old-v1 boot line on every boot of a board carrying a v1 record, until the first change.
  2. **Review:** a maximum 163-byte phrase — review pages, `LOC`, `BACK` preselected, wake and blank on a page, an
     alarm from the review; the sent bytes are complete.
  3. **NV-06:** repeated 2852-B replacements on a populated partition, NVS stats before and after, and a power cut
     across the whole write window — no invalid or partial catalog.
- **Next:** W7 (needs W0 and W6), W0, W5, W9, W4c or W1b, in the owner's order.

## 8. Revision history

**Revision 1 (2026-09-29)** is the first draft. It is built on the W6 pre-check and design r2.23.
- It adopts the pre-check's one-package recommendation.
- It pins the owner-ruled D14 reply shape and the D15 allocation.
- It takes the pre-check's measured facts as the contract: layout, reading, the stage behaviour, the stack frames,
  the projection, the review geometry, the transitions and the send line.
- It defines the closed disposition ledger and the real-path proofs.

**Revision 2 (2026-09-29)** folds in the brief review (HOLD, W6R-1–W6R-3):
- **W6R-1:** the inbox-verbs NV fake held 2304 B per record and reported writes it did not store (B477). It is now
  fenced for capacity and truthful retention only, with regressions. The router arm must prove persistence by the
  exact stored blob and a catalog reload. The deferred-actions probe, which reuses the inbox builder, keeps its
  contract and is rerun by both gates.
- **W6R-2:** `reset all`'s maximum is 1517 B, with a ten-digit generation; 1508 B is the one-digit case. The stage
  proof covers both, the page interval is explicit — page 5 holds only `channel8` — and the paged generation
  cases are required.
- **W6R-3:** the comments in `firmware_commands.cpp` that W6 makes false are corrected in place, line-preserving.
- **Also, from the review's notes:** the routed call order; the synthetic known-zero hash case; the qualified
  188/191-byte emergency figures; per-assertion ledger classification; and the earlier packages' properties read
  with the ledger.
- **Pins:** the design (the r2.23 erratum) and the register (B477, and the revision-2 status).

Nothing else changes.
