<!-- Author: Claude (brief author); QA: Codex (pre-check, brief review, independent gate); coder: a separate agent dispatched by QA; the owner rules and commits -->
# Standalone Home W4a — one identity formatter for every device label (B441, with B449 and B455; `src`)

**Revision 2 — 2026-09-26 — QUALITY-AGENT PASS with fold-ins; authorized by the [QA brief-review receipt](../evidence/2026-09-26-standalone-mobile-home-w4a-brief-review.md).**

- **Base:** commit **`8360802`** (`8360802904f7bd0023279d3da844453d61207ede`) **plus the uncommitted, QA-passed W1c and W3
  candidates and their landings**. This is not HEAD alone: the pre-check's inventory is the authority for the whole
  tree (§1). The simulator is at **`6585649`** (`6585649ea5a780f0542b2931853a667be56a5b2b`), clean.
- **Freeze:** the coder pins this brief by the hash QA issues when it passes review. Under P4 a brief is frozen from
  preflight PASS until the implementation freeze.
- **Where:** `/home/staszek/MeshRoute` on `main`, never a worktree. Preserve every tracked and untracked file,
  including the W1c and W3 candidates; four of their files are fenced here and are edited on top of them.
- **Authorities:**
  - the [W4a QA pre-check](../evidence/2026-09-26-standalone-mobile-home-w4a-precheck.md) — cited as "pre-check Qn";
  - the [design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) r2.20, §4.1 (the formatter
    and, new in r2.20, the per-site budgets), §10's UI-16 row and §13 W4a;
  - the [register](../../2026-07-30-open-bug-register.md): B441 (the display defect), B449 (probe control O8) and B455
    (probe control O6);
  - the process rules: `AGENTS.md` / `CLAUDE.md` (C1–C3, P4–P7, D1–D6, M1–M2) and
    [agent roles](../../2026-09-02-agent-roles.md), including the owner ruling of 2026-09-16 on the coder's versus
    QA's gate for a `src`-only slice.

## 0. What this is

**The defect (B441).** Device names reach the panel as raw cached bytes. A UTF-8 name renders as Latin-1 mojibake or
loses cells, since the `6x10_tf` font has no glyph for 0x80–0x9F. A long name is silently clipped. An unnamed teammate
shows a clipped lowercase hash (`0x12ab`) — the "third spelling" UI-16 bans.

**The fix.** One pure formatter renders every device label on the panel at that site's column budget (design §4.1):
- **Sanitized cells.** Each name byte passes through the existing `ui_display_byte`.
- **Visible abbreviation.** A name wider than its budget becomes `cols − 1` bytes plus a generated `»` (0xBB).
- **Unnamed.** A known hash shows `0x<HASH8>` when 10 or more columns fit, otherwise the six-digit member fingerprint.
  A hash is never clipped.
- **No hash.** The caller keeps its `id <n>` fallback.

The name is read in full — up to 32 counted bytes — **before** formatting. The 14-byte copies used today cannot tell an
exact fit from a longer name, or a real hash fallback from a name that happens to begin `0x`.

**With it (probe instruments only):**
- **B449** — control O8 is repaired so it really injects the forbidden clipped-hash name into the invite row.
- **B455** — control O6 is retired explicitly, since O20 already covers the property its label claims.

**Not in W4a:**
- the own name on Home and My device (W4b), and the review screen (W6/W8);
- the raw `peer_name_find` contract (W1 keeps its full 32 bytes) and named-peer precedence (B447);
- team identities (team fingerprints and IDs, NEARBY, the provision team rows) — they are a different namespace and
  stay as they are;
- any `lib/`, wire, NV, simulator, console, JSON or companion change;
- any change to what a site selects or acts on — labels are display only.

**This slice changes what is on screen**, so, unlike W3, the probe cannot stay byte-identical. The proof is:
1. **Stage A** — the instrument repairs, against the unchanged product;
2. **Stage B** — the fix, with a closed **expectation-change ledger** (§2.6). Every other expectation stays.

## 1. Verified seams and pinned inputs (pin by symbol; lines are hints from the pre-check inventory)

"UI" is `src/firmware_ui.cpp`; "model" is `src/firmware_ui_model.h`.

| Seam | Fact at the base (pre-check) | W4a change |
| --- | --- | --- |
| `ui_display_byte` (model `:~1446`) | 0x20–0x7E as-is; anything else `.` | **None** — used by the formatter. M13's anchor stays intact |
| `ui_fmt_member_hash_full` / `ui_fmt_member_fingerprint` (`src/firmware_ui_invite.h` `:~390` / `:~379`) | `0x%08lX` / `%06lX` of the low 24 bits; `snprintf` wrappers that truncate if the cap is too small | **None** — called by the formatter only with sufficient capacity |
| `label_from_hash` (UI `:~448`), called only by `label_for_team_id` (`:~463`) and `label_for_origin` (`:~470`) | W1: terminated result; raw `peer_name_find` with cap − 1, so at most 14 bytes; no-name fallback is lowercase `0x%08lx`; no sanitizing | §2.2 |
| TEAM rows: `build_snapshot` → `label_for_team_id` (UI `:~777`) → `ui_team_row` (`src/firmware_ui_team.h` `:~159`, `%c%-6.6s %3s %4s %2s`) | `TeamRow::label[15]`; the row clips at 6; `ui_team_rows_equal` (`:~233`) compares the six visible label bytes | Formatted at **6** before publication |
| Compose header, `draw_compose` (UI `:~2269–2280`) | `to: %s`, a live `label_for_team_id` into a 15-byte stack local | **15** cells, `[16]` stack |
| DELIVERED result, `draw_compose_result` (UI `:~2193–2215`) | `DELIVERED to` on row 1, the peer on its own row 2; a 15-byte stack local | **19** cells, `[20]` stack |
| Emergency REPLY sender: `label_for_origin` (UI `:~2739`) → `ui_route_recv_push` (`src/firmware_ui_send.h` `:~608`, passes `who` through) → `UiModel::on_reply` (`copy_clamped` into `_reply_who[15]`) → `freeze_outcome` (UI `:~944`) → `draw_emergency` | Line `%s: %s` on 21 cells; identity bound 14 | Formatted at **14** before `on_reply`, then copied verbatim |
| Invite publication in `build_snapshot` (UI `:~788–808`) | `InviteMember::name[15]` (`kInviteNameCap`) from raw `peer_name_find` (14 bytes max), `""` when unnamed; hash carried separately | Named: formatted at **14** from the full raw name. Unnamed: `""` with no formatter call |
| Candidate row: `invite_sel_rows` (`src/firmware_ui_invite.h` `:~348`) → `ui_fmt_invite_row` (`:~446`, `%c%-6.6s T%-3u %6s`) | Name 6, `T<id>`, fingerprint 6; 19 columns | Takes a **prepared six-cell name** argument (§2.3) |
| NEW MEMBER confirmation: `invite_id_rows` (`:~421`) → `draw_provision_screen` (UI `:~2022–2024`) | Full uppercase hash always; optional name on its own row, copied byte by byte | Name already formatted at **14**; copied unchanged, with no second sanitize |
| Grant result (`st.grant.hash`), NEED/WAITING FOR PUBKEY (`st.invite.sel_hash`) (UI `:~2042–2071`) | Full uppercase hash | **None** |
| STATUS `ui_status_me`, JOIN `join_fmt_node`, Inbox list and detail header, NEARBY/`ui_fmt_team_fingerprint`, provision team rows, `firmware_ui_geo.h` | Own team ID, numeric IDs, team tokens, or no device identity (pre-check Q2) | **None** |
| Panel font: `variants/heltec_common/board_ui.cpp` `set_font`, `u8g2_font_6x10_tf` (U8g2 2.35.30) | 0xBB glyph present (`»`, advance 6); 0x7F–0x9F have no glyph; 0xA0–0xFF draw as Latin-1; the driver does not filter | **None** — metal residue (§7) |
| `tools/probe_firmware_ui` | W1 wrapper plus trampoline `mr_probe_label_from_hash`; W3's `once <anchor> <script>` guard (`run.sh` `:~1270`); controls B241a/b, N4, N9, O2, O5, O6, O8, O9, O18, O20, O22, C114, C120 bear on labels (pre-check Q5) | §2.5–§2.6 |
| `tools/probe_ui_model_mutations.py` | `model` 239, `sliceCbudget` 1, `uiteam` 20, `uiinvite` 32; (b) `uisend` 15, `sliceCsend` 1, `chrome` 44, `uinearbyrow` 9, `uigeo` 18 — each matches once (pre-check census). `PIN_CASES, PIN_ASSERTS = 2973, 196111` | A new battery, re-anchors and the PIN (§2.7) |
| `tools/probe_board_abi.py` | Stock pins: `InviteMember` 20, `UiSnapshot` 1336, `UiModel` 928 / 912 / 912, `UiState` 504; `Node` 235208 / 122176 / 157304 (native / heltec_mobile / gateway) | **None** — zero resident growth, no re-pin |
| Pre-check Q3 supplemental compile-only measurement, using the three real ABI toolchains | `TeamRow` 40, `InviteIdRows` 26, `OutcomeView` 52 on each ABI. These three are **not stock ABI-probe entries** | Repeat the supplemental measurement under §4; do not claim the stock sweep measures them |
| `tools/probe_board_ui/run.sh` W43 | Requires the literals `static_assert(kBodyCols == mrui::kDetailCols,` and `constexpr int kBodyCols = 19;` in the UI | Keep both. B418's W49/W51/W54 are W2's |

**Executable inputs.** These are the fenced files. Each hash is the working-tree SHA-256 at authoring time.

| File | SHA-256 | Lines | Note |
| --- | --- | ---: | --- |
| `src/firmware_ui.cpp` | `6fe8c435f0c3ada0a5355a2c4d8b934875c5d4e0cfd06f6fa1c90f07a4307e2f` | 2785 | equals HEAD |
| `src/firmware_ui_model.h` | `2a452940474091d90b20e91f43205b2c04f160206ace8042342e357f62079af1` | 5777 | W3 candidate |
| `src/firmware_ui_invite.h` | `fc1c72570f80fe399d868bd020d7a991594a2fd0f5dd1a49ba3e003379750c9f` | 746 | equals HEAD |
| `src/firmware_ui_team.h` | `aefa86f4785f30aa6551308f3f666da4d04eee20426e33eddd4591edb165fbc3` | 280 | equals HEAD |
| `test/test_firmware_ui_model.cpp` | `42dc27ffb4c2205bf8743a9c3d39d80fc6d9b6e33068262bff9c6ddc8799ee63` | 10229 | W3 candidate |
| `test/test_firmware_ui_team.cpp` | `b14780f646a4eaea8d51d1ab0bd916383c246c6f54a155179e4dfe999de2397d` | 592 | equals HEAD |
| `test/test_firmware_ui_invite.cpp` | `2a2f7c6fd67af6a2580404247557afea22a31bb3299454f4a7c13b80df5626a7` | 1314 | equals HEAD |
| `tools/probe_firmware_ui/probe_main.cpp` | `d0cf6502737b3d12f60bda7a54cc04c2fe9b82896204a4369472a4019d89673a` | 7283 | W3 candidate |
| `tools/probe_firmware_ui/run.sh` | `f754b745c75968dd615e80e092144bad681775d10b6a99c6914446249b4f2579` | 1753 | W3 candidate |
| `tools/probe_ui_model_mutations.py` | `9429254f5d2ec79c32ac1dd4beca3fc9fbadab0d432e8fa57087f8bb4cebe932` | 12450 | W3 candidate |

**Base inventory.** The pre-check's `inputs.json` (1,455 MeshRoute paths, covered by its `SHA256SUMS`) is the authority
for every other path. After QA wrote it, only the four preparation documents below changed at authoring. New paths
since then: the pre-check report and folder, this brief, and QA review artefacts.

**Permitted preparation set.** These uncommitted documents are preserved unchanged:

| Path | Kind | SHA-256 |
| --- | --- | --- |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | authority (r2.20: §4.1 budgets) | `0b5706dec5a8dbae27d231a6b019c2eb2947f2f789b12ad7ecbec52998adadf8` |
| `docs/2026-07-30-open-bug-register.md` | authority (B441, B449, B455; §0) | `922564134f257cb0c06a0c5edcb55ebf527f9d226a3d2ddbacd5a473a27249ef` |
| `docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a-precheck.md` | authority (pre-check) | `f7e55b32d4c268ed04bbf0ff88e0fba39c4c31d920586b7a453410af614288ca` |
| `docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a-precheck/SHA256SUMS` | evidence — covers the folder's other 30 files, including `inputs.json` (`a5f9b0eb…84b764`) and `corpus-manifest.json` | `e4268d8d429a023161a90942857a0c88aa47ff1037bd499421038d69fd1ae92a` |
| `tracker.md`, `MEMORY.md` | context pointers | `a03b3cbe4f9caedf4fa077d1b20344cdb7a0411bfccab7b476da2d9d9cc9cb08`, `271edd08f42e1a4d1f43b954bfbe293bbf91e360ee8548892d9fc61d733a5278` |

This brief is pinned by the hash QA issues on PASS, not listed here.

**Preflight:**
1. Check both repositories: MeshRoute `HEAD` = `8360802`; the simulator at `6585649` and clean.
2. Every executable input matches its hash above.
3. Every preparation-set hash matches, and the pre-check folder verifies (`sha256sum -c SHA256SUMS`, 30 OK).
4. Every path in `inputs.json` still has its recorded hash, except the four preparation documents above.
5. Classify every other path. QA review reports and evidence added after this brief are explained preparation
   additions; anything else is reported.

Any of the following is STOP-1 to QA: a different `HEAD`, a mismatched input, a changed preparation file, or an
unexplained path. The whole preparation set stays unchanged from preflight through the freeze.

## 2. Contract

### 2.1 The formatter — one pure function in `src/firmware_ui_model.h`, right after `ui_display_byte`

```
enum class IdentityFmt : uint8_t { name, hash, none, no_fit };
IdentityFmt ui_fmt_identity(char* out, std::size_t cap, const char* bytes, uint8_t len,
                            uint32_t key_hash32, uint8_t cols);
```

The names are the design's. An equivalent spelling is acceptable if it keeps every rule below.

**The input is counted** — `len` bytes of `bytes`, which may hold any byte, including NUL. It is never `strlen`'d,
and it is never read when `len == 0`, so `bytes` may be null then.

**Capacity:**
- `cap == 0` writes nothing and returns `no_fit`.
- If `cap < cols + 1`, write `out[0] = '\0'` and return `no_fit` — never a partial or clipped result.

**Named (`len > 0`):**
- `len ≤ cols`: each byte through **the existing** `ui_display_byte`, then NUL. Returns `name`.
- `len > cols`: the first `cols − 1` bytes sanitized, **then** the byte `0xBB`, then NUL. Returns `name`.
- A name's own `0xBB` byte becomes `.`. Only the generated marker is `0xBB`.
- A stored name that looks like a hash (`0xdeadbeef`) is a name and is formatted as one — its provenance is never
  inferred from its spelling.
- `cols == 0` writes `out[0] = '\0'` when `cap > 0`, then returns `no_fit`. No name byte is read.

**Unnamed (`len == 0`) with `key_hash32 != 0`:**
- `cols ≥ 10`: `ui_fmt_member_hash_full`, which gives `0x<HASH8>` uppercase.
- `6 ≤ cols ≤ 9`: `ui_fmt_member_fingerprint`, six uppercase digits.
- Either way it returns `hash`, and the capacity is always sufficient for the helper.
- `cols < 6`: `out[0] = '\0'`, returning `no_fit`. Never a clipped hash.

**No name, no hash:** `out[0] = '\0'`, returning `none`. It never fabricates `0x00000000`; the caller keeps its
`id <n>` fallback.

**Scope:** no UTF-8 decoding, no substitution table, no allocation, no state. It is a panel formatter only — a lone
0xBB is not UTF-8, and no console, JSON, companion, wire or NV path may call it (pre-check Q4).

### 2.2 The resolver seam — `src/firmware_ui.cpp`

- **`label_from_hash`** gains the site's `cols`. It reads the full raw name into a counted stack buffer of
  `protocol::peer_name_max` (32) bytes through `peer_name_find(hash, raw, 32)`, then calls `ui_fmt_identity(out, cap,
  raw, n, hash, cols)`.
  - W1's guarantees survive: the result is terminated for any `cap ≥ 1`, and nothing is written for `cap == 0`.
  - Raw `peer_name_find` stays unchanged and unterminated.
- **`label_for_team_id` and `label_for_origin`** pass `cols` through and keep their `id %u` fallback for "no hash".
  `label_for_team_id` still returns the hash it resolved (W3's single-resolution rule).
- **Named budgets.** Each site's budget is a named constant derived from existing geometry, with no new bare literal:
  - TEAM = `kTeamLabelCols` (6);
  - compose header = the body's 19 columns minus `to: ` (15);
  - DELIVERED peer row = the body (19);
  - REPLY sender = `kLabelCap` (14);
  - invite name = `kInviteNameCap − 1` (14);
  - candidate-row name = the invite row's name column (6).

  Each new stack output buffer is sized from its budget (budget + 1); existing resident carriers keep their size
  and have at least that capacity. Every production budget is at least six. State these bounds with compile-time
  checks where applicable, so `no_fit` is unreachable at the listed production sites.

### 2.3 The sites and their budgets (design §4.1 r2.20) — zero resident growth

| Site | Change | Budget | Storage |
| --- | --- | ---: | --- |
| TEAM rows (`build_snapshot`) | Publish the **formatted** label into `TeamRow::label[15]` | 6 | unchanged |
| Compose header (`draw_compose`) | `label_for_team_id(…, 15)` into a `[16]` stack local | 15 | stack |
| DELIVERED peer row (`draw_compose_result`) | into a `[20]` stack local | 19 | stack |
| REPLY sender (`mr_ui_on_push` → `on_reply`) | Formatted at 14 **before** `on_reply`, then copied verbatim by `copy_clamped` and `freeze_outcome`, with no second sanitize | 14 | `_reply_who[15]`, `OutcomeView::who[15]` unchanged |
| Invite publication (`build_snapshot`) | Raw lookup; `n == 0` → `""` (no formatter call, no fallback); `n > 0` → `ui_fmt_identity(mem.name, sizeof mem.name, raw, n, hash, 14)` | 14 | `InviteMember::name[15]` unchanged |
| Candidate row (`ui_fmt_invite_row`) | Takes an extra `const char* name6` argument. The renderer prepares it with `ui_fmt_identity(name6, 7, m.name, strlen(m.name), 0, 6)` — `m.name` is the terminated formatted carrier, never the raw buffer. The row still bounds the name column itself (`%-6.6s`) | 6 | stack |
| NEW MEMBER name (`invite_id_rows`) | Copies the 14-cell carrier unchanged | 14 | unchanged |

**Two-pass rule for names only.** The candidate row's name is the 14-cell carrier re-formatted at 6. For names this
equals formatting the full name at 6, as the pre-check measured. A first-pass marker sits at index 13, outside the
second pass's kept cells 0–4, so it is never sanitized into `.`. The second pass is **strictly narrower** than the
first, and it is never applied to a hash. Pin this with a native test: the full-name-at-6 result equals the 14-then-6
result, over a matrix of lengths and bytes, including a raw 0xBB at every position.

**Selection is unchanged.** The unnamed invite name field stays blank and its fingerprint column stays.
`InviteMember`'s hash and ID travel in the same whole carrier (never rebuilt field by field), and every act keys on the
frozen hash.

**Frames.** The compose labels keep their live lookup at draw time. W4a adds no freeze and claims no rename-between-
pages atomicity (pre-check Q4).

### 2.4 TEAM row repaint

- **The comparison.** `ui_team_rows_equal` keeps comparing the six visible label bytes, now of the formatted label.
- **Equal renders** still do not repaint.
- **New edge that must repaint:** an exact six-byte name (`Wolfga`) changing to a longer name with the same prefix
  (`Wolfg` + `»`). Long names that render equal (the same five cells plus marker) stay equal.
- **Preserved:** the age, geo and visible-column bounds.

### 2.5 Stage A — the instrument repairs, against the unchanged product

Run on the unmodified production source, before any `src/` edit.

- **O8 (B449).**
  - *The repair:* O8 injects the member name through the old fallback. Guard **each** of its two substitutions with
    `once` (W3's helper): each anchor occurs exactly once and each script changes exactly one line.
  - *Expected on the unchanged source:* the nameless candidate row reads `>0x00be T221 BEDEAD`, and the control fails
    exactly the **P23b** blank-name-field check and the **P23d** blank-name precondition.
  - *Not a proof:* a compile error, a zero match, a blanked real name or an unrelated failure is not O8's proof.
- **O6 (B455).** Retire it explicitly: remove the call and leave a comment naming B455 and O20 as the coverage.
  - O20 (the named confirmation loses its full hash) stays, as do every full-hash check and O5/O9's two-site topology.
  - Record O6's pre-retirement measurement: only P23d fails, on the lowercase spelling.
- **The run.** Run the stock firmware-UI probe on all arms with controls. Record per arm the checks and "controls
  verified / unusable", and O8's exact failing checks with its row bytes.
- **Freeze.** Record the SHA-256 of `run.sh` and `probe_main.cpp` at the end of stage A.

### 2.6 Stage B — the fix, with a closed expectation-change ledger

**Production:** §2.1–§2.4.

**Existing expectations that change** (pre-check Q5). No other existing expectation may change:

| Instrument | Change |
| --- | --- |
| `test_firmware_ui_team.cpp` `ui17-team` (`:~118–137`, `:~175–214`) | Use an explicit identity projection in the fixture where it feeds formatted labels. `Wolfgangetta` → `Wolfg` + `»`; 14 `W` → `WWWWW` + `»`. A literal stored name `0xdeadbeef` abbreviates as a name (`0xdea` + `»`); a **true unnamed** hash `0xDEADBEEF` → `ADBEEF` |
| same file, repaint (`:~549–563`) | Add §2.4's edge; keep the rest |
| `test_firmware_ui_invite.cpp` rows (`:~474–510`) | Named abbreviation `Wolfg` + `»`. ID and fingerprint placement, the blank nameless field and the row length are unchanged. Callers of the new `ui_fmt_invite_row` signature keep independent literal oracles |
| probe P18 (`:~3167–3395`) | `Wolfga` → `Wolfg` + `»`; unnamed `0x00c0` → `C0FFEE`. Age, marker, frame counts, geo and blank checks are unchanged |
| probe P23/P24 (`:~5384–5693`) | `>Wolfga T221` → `>Wolfg» T221`, including the `walk_to` strings. The blank field, full hashes, the 12-byte confirmation name and the transitions are unchanged |
| probe P28a (`:~6799–6839`) | Keep poison, canary, short-rename and cap 0/1. The 15-, 27- and 32-byte oracles become the first 13 sanitized bytes plus `»` at budget 14; exactly 14 stays whole; the fallback becomes uppercase `0x<HASH8>`. The trampoline takes the new signature and still includes the **file under test** |
| probe P28b (`:~6880–6882`) | `␠0xb241␠` → `␠410007␠`; `␠0xb2ee␠` → `␠EE41EE␠`. `H1` and `id 93` are unchanged |
| probe P28c/P28d (`:~6889–6952`) | Lowercase → uppercase full hash (`0xB2410007`, `0xB2EE41EE`); short names and `id 93` unchanged |

**Must not change mechanically:**
- P21's raw-cache precondition (`:~4831`) stays raw.
- The team-only absence checks (`:~4204`, `:~4891`) are **strengthened** to exclude both the full name and its
  abbreviation.

**C and C++ pitfall.** Write the marker as its own literal (`"Wolfg" "\xBB"`), or as `\xBB` followed by a non-hex
character. `"\xBBA"` would swallow the `A` into the escape.

**New cases:**
- **Native formatter matrix** (`test_firmware_ui_model.cpp`):
  - every byte 0x7F, 0x80–0x9F and 0xA0–0xFF; `C5 82` → `..`;
  - empty, exactly the budget, budget + 1, and 32 bytes;
  - control bytes and embedded NUL in counted synthetic input, kept distinct from what the console can admit;
  - a literal raw 0xBB; name versus hash provenance;
  - the hash boundaries at `cols` 6, 9 and 10;
  - **unnamed, nonzero hash:** `cols` 0–5 returns `no_fit` with empty output when cap > 0;
  - **named:** `cols == 0` returns `no_fit` with empty output when cap > 0; `cols` 1–5 follows the same exact-fit
    or abbreviation rule as any positive budget (including a marker-only abbreviation at one column);
  - cap 0 and 1 safety, with guard bytes on both sides;
  - `none` with no hash;
  - the two-pass equivalence (§2.3);
  - a preformatted marker copied through `on_reply` unchanged.
- **Native invite and team cases:**
  - retain or add a labelled **synthetic over-long TEAM carrier**, passed directly to `ui_team_row` without identity
    projection, to prove its independent six-column bound and keep `uiteam` T01 RED; ordinary formatted labels
    alone cannot distinguish T01;
  - retain or add a labelled **synthetic invisible-tail difference** in two TEAM carriers whose first six bytes
    match, to keep T09 RED on its original off-screen comparison property. This is separate from §2.4's reachable
    rename cases; do not rely on two identical, zero-padded formatted labels to expose T09;
  - a synthetic over-long prepared name fed to `ui_fmt_invite_row`, which must stay 19 columns — this keeps `uiinvite`
    I09 meaningful;
  - long and high-byte optional names on the confirmation.
- **The real renderer probe** shows the actual name lookup reaching the formatter at every §2.3 site: TEAM, the
  candidate row, the confirmation name, the compose header, the DELIVERED row and REPLY.
  - Cases: a long name at each budget, a high-byte name, and an unnamed peer.
  - Expected bytes are literals, never built with the formatter under test.

**Controls after the change** (`run.sh`). Every substitution a new or re-anchored control relies on is guarded by
`once`:
- **B241a / B241b** keep their meanings — the missing terminator and the last-byte-only terminator — and still fail
  the poisoned short-name and rename checks. Re-anchor them to the new adapter body.
- **N4, N9, O2** keep their meanings through the signature change. O2 must not accidentally select the correct team
  token.
- **O8, post-change.** The unnamed member's name field is filled from the formatter's **hash branch**: the publication
  formats even when the raw lookup returned 0, passing the real hash. The row then shows a hash-derived token in the
  name column and must fail P23b/P23d's literal **blank-name** assertions. The row must still be 19 columns and
  retain the correct separate fingerprint: those conjuncts are preserved invariants, not required failures. Record
  the actual row bytes and failed checks.
- **O20, O5, O9, O18, O22, C114, C120, O3** keep their purpose and topology. C114's `if (hash != 0)` and the invite
  guard's `if (hash != 0u)` stay distinct, and are never collapsed into a second match.
- **At least four new controls**, each RED on its intended new checks:
  1. a site that loses the marker, clipping silently;
  2. a site that skips `ui_display_byte`;
  3. the DELIVERED row formatted at the header's budget (15 instead of 19);
  4. the REPLY sender re-sanitized after formatting, so the `»` becomes `.`.
- **C0** keeps `must_build=no`.

**Report the stage A → final diff** of `probe_main.cpp` and `run.sh`, classified against the ledger above: changed
expectations, new checks, re-anchors, the retirement.

### 2.7 Mutations

- **A new battery `w4aident`** → `src/firmware_ui_model.h` (`ui_fmt_identity`), registered in `TARGET_SRC`, with a
  `MUTS_W4AIDENT` list and a `MUTS_BY_TARGET` entry. At least seven entries, each matching exactly once, compiling, and
  RED on its intended cases:
  1. the marker dropped (a silent clip);
  2. the sanitizer bypassed;
  3. the marker appended before sanitizing (becomes `.`);
  4. a clipped `0x…` hash at 6–9 columns instead of the fingerprint;
  5. the fingerprint used at 10 or more columns;
  6. `none` fabricating `0x00000000`;
  7. the marker written one past the budget.
- **Re-anchors where needed, meaning kept; retain an existing anchor verbatim when its source statement stays:**
  - `uiteam` T01, T02, T03, T09 — the normal fixtures now carry formatted labels; T01's independent row bound and
    T09's off-screen-byte comparison remain protected by the labelled synthetic inputs in §2.6;
  - `uiinvite` I07, I08, I09 onto the prepared-name input — I09 stays RED through §2.6's over-long synthetic input.
  - Unchanged: I10, I29, I30, `model` M13 and every other entry.
  - Each re-anchor is listed old → new, with its RED.
- **No entry is retired or weakened.** The configured counts of existing batteries stay unchanged.
- **Native PIN:** re-sync `PIN_CASES, PIN_ASSERTS` — the literal plus one derivation line (D5).

### 2.8 Nothing else moves

- **Layout:** no resident member or array change. `TeamRow`, `InviteMember`, `InviteIdRows`, `OutcomeView`,
  `UiSnapshot`, `UiModel`, `UiState` and `Node` all stay at their §1 sizes on all three ABIs.
- **Corpus:** 36/36, identical field by field to the pre-check's `corpus-manifest.json`; s18 per `simulation/BASELINE.md`.
- **Boards:**
  - `gateway` does not compile the OLED renderer, so its image is predicted identical;
  - `heltec_mobile` RAM is predicted unchanged; flash is measured and attributed;
  - the report states the stack locals added per call path.
- **Unchanged:** console, JSON and companion text, `lib/`, wire and NV.

## 3. Fence

**IN:**
- `src/firmware_ui.cpp` — the adapters with budgets and full raw reads, the §2.3 call sites and their sized buffers, the
  invite publication, the candidate-row call, and adjacent comments. W43's two literals are kept. No selection,
  admission, radio or clock change;
- `src/firmware_ui_model.h` — `IdentityFmt` and `ui_fmt_identity` right after `ui_display_byte`, carrier-contract
  comments and compile-time bounds; no resident member change;
- `src/firmware_ui_invite.h` — `ui_fmt_invite_row`'s prepared-name argument and the optional-name documentation;
  full-hash, whole-carrier and authority semantics unchanged;
- `src/firmware_ui_team.h` — the formatted-label and visible-width contract comments; `ui_team_row` geometry unchanged;
- `test/test_firmware_ui_model.cpp`, `test/test_firmware_ui_team.cpp`, `test/test_firmware_ui_invite.cpp` — §2.6's
  cases and ledger changes;
- `tools/probe_firmware_ui/probe_main.cpp`, `tools/probe_firmware_ui/run.sh` — stage A, then stage B per the ledger;
- `tools/probe_ui_model_mutations.py` — the new battery, the §2.7 re-anchors and the PIN literal with one derivation
  line;
- the report and evidence (§5).

**OUT:**
- `lib/` (including `peer_name_find`), console, JSON and companion code, `platformio.ini` and the simulator;
- the panel driver and font (`variants/heltec_common/board_ui.cpp`);
- `src/firmware_ui_send.h` (routing unchanged), `firmware_ui_nearby_row.h`, `firmware_ui_geo.h`, `firmware_ui_chrome.h`,
  `firmware_ui_status.h`, `firmware_ui_join.h` and every other `src/` file;
- `test/test_firmware_ui_send.cpp` and every other test;
- `tools/probe_board_abi.py` (no re-pin), `tools/probe_board_ui/*` (B418 is W2's) and every other tool;
- every existing battery's meaning, and every entry outside §2.7;
- the design, register, tracker, `MEMORY.md` and metal plan (QA lands, §7);
- the W1c and W3 candidates' other files — preserve them byte-identical.

## 4. Gates

The owner ruling of 2026-09-16 (P6) gives the coder the full chain, and QA the `src`-only independent gate.

**Supplemental ABI measurement (alongside, never instead of, the stock probe):** use the pre-check's
`layout-measure.py` **base** recipe for `TeamRow`, `InviteIdRows` and `OutcomeView`, without any counterfactual
capacity edits. Read the current headers and extract the current `OutcomeView` declaration exactly once from
`firmware_ui.cpp`; do not hand-copy or maintain a mirror declaration. Generate size/alignment symbols in a temporary
TU outside the checkout and use the stock ABI module's `measure` with each target's real flags/toolchain. Derive and
compare the three layouts with the pre-check and the coder's before-state. Preserve the small driver, generated TU,
input hashes and per-target results in evidence. No stock probe edit or pin change is authorized. This is a layout
measurement, not a claim of instantiated RAM; the stock board pair remains the RAM authority.

**Ordering:** the two runs of each board pair are back-to-back, and nothing runs between or during them. Mutation
batteries run separately from board measurements. No `--no-neg` result substitutes for a control gate.

### 4.1 The coder's gate

**Baselines, on the unmodified tree, before stage A:**
- **Supplemental ABI measurement:** the three types above, on all three ABIs. Run it outside the paired board runs.
- **Firmware-UI probe:** default mode, all arms. Record the counts and controls (pre-check: 518 / 964 / 518, 231 / 0).
- **Board-UI supplemental:** `tools/probe_board_ui/run.sh --no-neg`. Record its exact failure set (expected: B418's
  W49/W51/W54).
- **Boards:** `python3 tools/measure_board.py pair --jobs=1 --output .pio-measure/w4a/base-1`, then `base-2`. Then the
  stock `compare` per env on the two `manifest.json` files; both must PASS.

**Stage A (§2.5)**, recorded.

**Stage B, then the full chain:**
1. **Hygiene:** `git diff --check`. The stage-A → final probe diff is classified against the §2.6 ledger.
2. **Native:** a fresh `pio test -e native`, then **run the binary** `./.pio/build/native/program`. Report the counts
   derived: 0 failed, 0 skipped. `PIN re-synced? YES — <derivation>`.
3. **Corpus:**
   - a fresh stock `lus`: `cmake -S <simulator> -B <new dir outside both repos> -DCMAKE_BUILD_TYPE=Release
     -DMESHROUTE_DIR=<MeshRoute>`, then `cmake --build <dir> --target lus`;
   - `python3 -B tools/run_corpus.py --out <new dir> --lus <dir>/orchestrator/lus --require-anchors`;
   - expect 36/36, compared field by field with the pre-check manifest.
4. **ABI:** `python3 -B tools/probe_board_abi.py` (full, controls on), plus the supplemental three-type measurement
   above. Every §1 size is unchanged; stock controls are 9/9 RED. Report the two instruments separately.
5. **Probes:**
   - **Firmware-UI, default mode, all arms:** green; every control verified, including the new and re-anchored ones
     and O8's post-change proof; 0 unusable. Report per-arm counts against the baseline and stage A, and explain O6's
     retirement in the count.
   - **Firmware-UI `--no-neg`:** diagnostic only; B350 applies.
   - **Board-UI supplemental:** a failure set identical to the baseline.
6. **Tools discovery** (D5): `python3 -m unittest discover -s tools -p "test_*.py"`. Report the count derived, OK, 0
   skipped.
7. **Warning census:** `tools/warning_census.sh` on its six pinned OLED envs — the stated exception to the two-env
   rule. Zero new warnings, `-Wswitch` 0, pins unchanged.
8. **Boards, final.** Run `.pio-measure/w4a/final-1` and `final-2`, then the stock `compare` per env; both must PASS.
   - **Attribution.** Read the `base-1` and `final-1` manifests field by field: every `measurements.*` and payload
     difference is attributed, down to symbol level where RAM or a symbol moves.
   - **Compatibility.** `source.*` differences are expected. `toolchain.*`, `fixed_identity.*` and `paths.*` must be
     identical.
   - **Predictions (§2.8):** `gateway` payload identical; `heltec_mobile` RAM unchanged.
9. **Mutation union:**
   - **Batteries:**
     - (a) `model` 239, `sliceCbudget` 1, `uiteam` 20, `uiinvite` 32 and the new `w4aident`;
     - (b) `uisend` 15, `sliceCsend` 1, `chrome` 44, `uinearbyrow` 9 and `uigeo` 18 (pre-check Q5).
   - **Floor:** 379 configured entries plus `w4aident`'s.
   - **Expect** every entry RED, 0 unusable, 0 vacuous. Every worker's clean baseline equals step 2's counts.
   - **A non-RED entry** is reported with its capture; QA disposes of it, and it is never silently accepted.

**Reader audit (D6/P7), before the freeze.** Grep `lib/`, `src/`, `test/` and `tools/` for every reader of the edited
statements and changed signatures, including directory scanners (`probe_features/ownership.py`,
`check_data_type_literals.py`) and the console-sink structural readers. Rerun any check or control whose predicate the
edits touch.

**Input stability:** inputs are recorded before stage A and after the last step. They differ only by the fenced edits
and new evidence; a change in between restarts the chain.

### 4.2 QA's independent gate (P6, `src`-only)

On the frozen tree, QA re-runs:
- **Native** — a fresh `pio test -e native`, then the binary;
- **Corpus** — 36/36 byte-identical;
- **Boards** — its own `pair --jobs=1` into a fresh `.pio-measure/` directory, read field by field against the coder's
  `final-1`;
- **Selector (a)** — `model`, `sliceCbudget`, `uiteam`, `uiinvite` and `w4aident`, the touched batteries;
- **The affected probes** — firmware-UI (all arms, controls), the stock ABI probe and an independent supplemental
  measurement of the three types above.

The census, the full union and discovery stay in the coder's chain.

### 4.3 STOP conditions

- **Expectations:** an existing expectation changed outside the §2.6 ledger, or any expectation weakened.
- **Resident state:** a resident member or array change, or any ABI change.
- **Fence:** an edit outside §3.
- **Formatter:** a first identity projection that reads a pre-clipped raw name, or any re-sanitization of formatted
  output except §2.3's explicitly permitted name-only 14→6 projection; any label reaching a console, JSON or
  companion path. The permitted projection must prove its equivalence and must never re-sanitize a retained marker
  at an equal or wider budget.
- **Controls and entries:**
  - O8's proof failing on the wrong checks, or a new or re-anchored control or entry that is vacuous, does not compile,
    crashes or goes RED only on unrelated checks;
  - an existing control losing its meaning, other than O6's registered retirement;
  - C0 building.
- **Census:** a warning-census failure.
- **Boards:** a failed repeatability `compare`, an unattributed delta, a `gateway` image change, or any RAM increase.
- **Corpus:** any stream delta.
- **Inputs:** a mismatched input at preflight, a changed preparation file, or an input change during the chain.
- **Source facts:** a §1 fact found false — a semantic disagreement (P4), STOP-1 to QA.

## 5. Report and evidence

**Report:** `docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a.md`, with an author line on line 1.

**Evidence directory:** `docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a/`. It holds:
- **Board manifests:** the eight per-env manifests, copied from `.pio-measure/w4a/` **after the last measurement run**;
- **Board comparisons:** the four `compare` outputs and the attribution table;
- **Stage A:** its measurements and hashes;
- **The ledger:** the classified stage A → final probe diff;
- **Mutations:** the re-anchor table;
- **Durable receipts:** compact native, ABI, probe-mode, discovery, census and union summaries; the full corpus
  manifest and field comparison; the locations and SHA-256 hashes of the raw logs;
- **The reader-audit greps;**
- **A `SHA256SUMS`** covering everything.

**Retention.** Follow [the evidence-retention policy](../evidence/README.md):
- raw runs and logs go under ignored `artifacts/2026-09-26-standalone-mobile-home-w4a/`;
- raw board outputs stay under `.pio-measure/w4a/`, and the stock tool's `.pio-measure/env/` roots are not relocated;
- native builds use `.pio/`;
- simulator and mutation scratch builds stay outside both repositories.

**The report contains:**
1. **Preflight** and the inventory check.
2. **Baselines**, then stage A, with O8's row bytes and failing checks and O6's pre-retirement measurement.
3. **Diff:** `git diff --stat` for the fenced files, and the classified probe diff.
4. **Figures:** every §4.1 figure, derived by the coder.
5. **Controls and mutations:** each new, re-anchored and retired control; the union by battery.
6. **The pin line:** the exact line `PIN re-synced? YES — <derivation>`.
7. **Freeze inventory:**
   - the final SHA-256 of every fenced file;
   - the authorized brief hash;
   - the simulator commit and status;
   - the preparation-set hashes, unchanged;
   - the inventory check;
   - the new evidence paths with hashes;
   - the stability statement.
8. **Not run,** with reasons. Each reason is that the probe's predicates are unaffected — confirmed by the reader
   audit — never "it reads none of the files". Expected: the full board-UI probe (B418, W2); console sink, inbox verbs,
   custody USB, BLE line, the feature matrix and deferred actions; and metal.

## 6. Owner rulings

None requested. D10 and the design's §4.1 rules are the owner's. The per-site budgets are the spec author's choices,
made within them and recorded in design r2.20.

## 7. Landing (QA, on PASS)

- **Register:**
  - **B441 CLOSED** — the formatter at every site, with the high-byte matrix;
  - **B449 CLOSED** — O8 repaired and re-aimed;
  - **B455 CLOSED** — O6 retired, O20 retained;
  - the §0 dispatch rewritten in place.
- **Design:** the §13 W4a status.
- **Pointers:** `tracker.md` and `MEMORY.md`.
- **Metal plan (M2)** — a new row in `docs/2026-09-20-metal-test-plan.md` (e.g. UI-20; check the ID at landing), from
  pre-check Q7:
  1. On two OLED peers with a fresh or controlled cache, set `Wolfgangetta` with `peername` or the peer's own
     `cfg set name`, reading it back with `nameof` or `peers`. TEAM and the invite row both read `Wolfg»`, with one
     legible chevron cell and neighbouring columns intact. NEW MEMBER keeps the full `0x<HASH8>`.
  2. With `łAB` (`C5 82 41 42`): the name reads `..AB`.
  3. A genuinely unnamed peer on a **fresh** cache: TEAM shows its six-digit fingerprint; the invite name field is
     blank beside its fingerprint; the confirmation shows `0x<HASH8>`.

  **PASS:** legible, identical in the allotted fields. **STOP:** a missing or doubled marker, raw Latin-1, collapsed
  cells, a clipped hash, or a fabricated invite name. **OWED:** when the fixture state cannot be established.
- **Next:** W4b (Home and navigation; it needs W4a and W3), per the owner's order.

## 8. Revision history

**Revision 1 (2026-09-26)** is the first draft, built on the W4a pre-check.
- It adopts the pre-check's zero-growth budgets (recorded in design r2.20), its formatter home and its
  instrument-first sequencing.
- It fixes the formatter's API and no-fit contract, the two-pass rule for names, O8's pre- and post-change proofs, and
  O6's retirement (registered as B455).
- It defines the closed expectation ledger.

**Revision 2 (2026-09-26)** folds in QA W4R-1–W4R-3: precise formatter edge/STOP wording and O8 failure
attribution; explicit supplemental layout measurements; retained synthetic TEAM width/tail controls. Product
choices, resident allocation, source fence and gate split are unchanged.
