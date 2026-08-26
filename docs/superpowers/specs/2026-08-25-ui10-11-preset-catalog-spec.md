<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# UI-10/UI-11 — the configurable preset catalog · implementation spec · 2026-08-25

**Status: ★ OWNER-APPROVED AND DISPATCHABLE (2026-08-25, after one QA review round — all nine reviewer
findings folded below with the owner's rulings).** The DESIGN authority is the parent
`2026-07-31-onboard-oled-ui-design.md` **§3.2.2 (catalog) + §3.2.3 (location/verbs/persistence) + the §3.3
generation-freeze paragraph**. Where this spec paraphrases the design, the design governs — EXCEPT the three
rulings in §2, which the owner settled here on review.

## 0. Present tree state (verified 2026-08-25)
- The fixed catalog: `mrui::kDmTexts` / `kChannelTexts` (`src/firmware_ui_model.h:1370-1371`, each 2 texts + the
  derived `back, don't send` row per §B66's one-table authority) and `kEmergencyText` (`:1379`).
- **`SendReq` carries ROW-INDEX identity today** (`:1394` — "the canned texts keep indices 0..sendable-1").
  The design's §3.3 freeze paragraph already rules the replacement: a `SendReq` identifies **the stable slot
  AND the generation the wearer saw**; a disabled slot or moved generation at execution ⇒ refuse and repaint,
  ⛔ never resolve the row index to newly configured words. Page-buffer painting freezes one generation per
  frame.
- The DM grammar is `send <team_local_id> "<text>" -t -a` (`firmware_ui_send.h:474`); the emergency path
  composes `send_channel <ch> "<text>" -t [-l] -e` conditionally on a fix (`:481`).
- NV precedents: `'MRC1'`/`'MRJ1'`/`'MRK1'` (`src/device_nv.h`) — the K1 keyring (`TeamKeyBlob`) is the
  template for a separate versioned record: own magic, four-state read, byte-identical write coalescing,
  factory-reset erase.
- ⚠ **Landed since the design was written, must be reconciled (R-1..R-3 below):** §UI-17's compose/outcome
  model, §UI-16 K7's `GRANT KEY` optional row inside the DM compose sub-view, and the V4/GNSS specs' ruling 8.

## 1. Reconciliations (the design predates three landed facts)
- **R-1 — K7's `GRANT KEY` row.** The DM compose list is no longer texts+back: an optional `GRANT KEY` row sits
  between the texts and `back, don't send` (owner-ruled, B245). The configurable list preserves it EXACTLY:
  enabled DM slots (stable order) → the K7 row when offered → the derived back row. ⛔ The preset rework may
  not move, gate or re-anchor K7's row semantics; its landed cases re-run untouched.
- **R-2 — ★ OWNER-RULED 2026-08-25 (QA correctly demanded this be an EXPLICIT ruling, not a "reconciliation" —
  it changes the GNSS design): the per-slot `include_location` flag is AUTHORITATIVE for the OLED's
  per-message location intent; GNSS supplies availability/freshness ONLY.** `-l` is present iff the slot says
  so (§3.2.3's table); GPS-3 must re-read its ruling 8 against this — on a preset-catalog device the
  "eligible" set is *the slots with `include_location`*, never every sealed send. ⓘ Phase A has no GPS:
  static/manual coordinates are the only source. **The emergency truth table, stated plainly (owner-approved):**
  location off ⇒ never add `-l` · location on + fix ⇒ add `-l` · location on + no fix ⇒ send WITHOUT `-l`
  (the §4.1 alarm-outranks-coordinates ruling).
- **R-3 — the emergency text.** `kEmergencyText` becomes the emergency slot's compiled default; the §4.1 ruling
  (alarm outranks coordinates — send without `-l` when no fix) is UNCHANGED and stays the emergency slot's own
  policy regardless of its `include_location` flag (the §3.2.3 table's first row).

## 2. Owner rulings (settled 2026-08-25 on QA review)
- **OQ-A — ★ RULED: maximum 17 printable ASCII bytes for EVERY preset.** ⛔ **My draft's conditional bound
  (17-only-when-loc=on) was WRONG, kept visible:** the row ALWAYS shows `L` **or** `-` per the parent design,
  so BOTH states consume selection marker 1 + location marker 1 + text ⇒ 17 in 19 columns unconditionally.
  Storage stays `text[18]` = 17 characters + terminator/canonical zero. `set` refuses 18+ bytes as `bad_text`.
- **OQ-B — ★ RULED: `reset all` closes an open selection-phase compose after a SUCCESSFUL DURABLE change** —
  exactly the same modal rule as every other successful catalog mutation; it must not retain a selection whose
  text was just replaced.
- **`busy` — ★ CORRECTED (my draft contradicted the parent) + RULED, the exact behaviour table:**
  ⛔ withdrawn: "`busy` = an open selection-phase compose modal". The parent rules a successful update CLOSES
  such a modal. `busy` belongs ONLY to an active emergency attempt series:
  | situation | behaviour |
  |---|---|
  | active emergency | EVERY mutating verb returns `busy`, including a no-op |
  | successful CHANGED mutation | **construct the canonical candidate WITH the next non-zero generation → save → publish the candidate → close a selection-phase compose without sending** (⛔ corrected 2026-08-25, QA round 2: this row read "save → publish → increment generation", contradicting the canonical-bytes rule that the persisted candidate already contains the next generation. A failed save publishes NOTHING) |
  | identical no-op | no write, no generation change, ⛔ no modal close |
  | validation/storage failure | no live change, ⛔ no modal close |
  | an already-displayed outcome | may finish |
- **The stale-generation refusal has a VISIBLE word (owner-approved): `PRESET CHANGED`** — a specific UI
  result with ZERO core submission, followed by a repaint from the current catalog. ⛔ Never a generic parser
  failure, never a silent fall-through.

## 3. Slices
| slice | kind | content | gate notes |
|---|---|---|---|
| P0 | design | this spec, owner-reviewed; OQ-A/OQ-B ruled | — |
| P1 | pure+NV | `PresetCatalog` service: `'MRU1'` `/mrui` record (17 fixed slots: `{enabled, loc, len, text[18]}` + generation, sized/asserted), the **explicit four-state storage policy (owner-ruled):** **absent** ⇒ compiled defaults, ⛔ NO warning (an ordinary first boot) · **invalid/unsupported** ⇒ defaults + a visible/COUNTED warning, and a later successful mutation MAY rewrite the complete canonical catalog and repair it · **io_failed** ⇒ defaults + a DISTINCT warning, and every mutation returns `store` with ZERO writes (⛔ never overwrite a possibly-intact record after a transient read failure) · **valid** ⇒ loaded — and **`valid` requires FULL SEMANTIC VALIDATION, not just size/header (QA round 2):** a well-formed header is still `invalid` if ANY slot violates: generation non-zero · boolean fields exactly 0/1 · length ≤ 17 · emergency enabled and non-empty · disabled-slot canonical zeroing · the text-content and tail-zero rules. ⛔ Corrupted slot fields must never classify as valid. **The two boot/status diagnostics, exact (QA demanded the observable contract):** invalid ⇒ `  ui presets = DEFAULTS (record invalid — repaired on next successful change)` · io_failed ⇒ `  ui presets = DEFAULTS (store unreadable — changes disabled)`; both natively pinned; a valid or absent store prints no presets line. **Canonical record bytes (owner-ruled — coalescing is unreliable without them):** generation starts at 1 and SKIPS ZERO on wrap · `enabled`/`loc` are exactly 0/1 · emergency has `enabled=1` · bytes after `len` are zero · disabled slots have zero length, text and location · padding/reserved bytes are NAMED and zero · the candidate generation is saved WITH the candidate, live state changes only after the save succeeds. Validation (printable ASCII, no `"` `\` CR LF, ≥1 non-space, ⛔ >17 bytes = `bad_text` per OQ-A). Byte-identical write coalescing — ⛔ the "identical set ⇒ zero writes" pin holds ONLY over a valid-or-absent store (a mutation against `invalid` may legitimately write the repaired canonical form even when its live values equal defaults). Factory-reset erase. Emergency: always-enabled, text-editable, ⛔ never clearable. NEW battery target; `src/`-only |
| P2 | console/BLE | the `ui preset list/set/clear/reset` grammar EXACTLY as §3.2.3 spells it; the three NDJSON records verbatim (`ui_preset`, `ui_presets_end` w/ capacity 17 + actives + generation, `ui_preset_err` w/ the six reasons); USB + BLE through the one dispatch; the `busy` table from §2 exactly. **Documentation targets, named (owner-ruled):** `ios-companion/INBOX_SYNC_CONTRACT.md` gains the events BEFORE any app editor · the hierarchical console help · `docs/manual/command-reference.md`. Mutating verbs return the record / full list per the design |
| P3 | UI | compose lists render ENABLED slots in stable-slot order (gaps valid, scroll ≥ visible), each row carrying its **slot id** — ⛔ never derived from the row index (§B66); the `L` marker per OQ-A; `SendReq{slot, generation}` replacing index identity, with the refuse-and-repaint on disabled/moved-generation at execution and the per-frame generation freeze; the §3.2.3 send compositions per slot kind (emergency degrade / channel strict / DM sealed-only); the zero-enabled empty state (§3.2.1's); R-1 preserved; emergency long-press reads the catalog text |
| P4 | metal | **owner runs later** — drafted at each slice, landed as one bench part on the arc's close |

Each slice: C1, the standing operational contract (parallel batteries per the runner's header, ONE full pass
per touched target, PIN derivation, both probes, no commits, no device contact), the handoff seam WITH the
unit (P3's probe drives a re-configured catalog through the REAL renderer: exact rows, gaps, the `L` marker,
the stale-generation refusal). **Documentation rule (QA round 2 — the blanket "no docs by the coder" and P2's
three named targets contradicted):** the hierarchical console help is CODE and is the P2 coder's; the two
DOCUMENT targets (`ios-companion/INBOX_SYNC_CONTRACT.md`, `docs/manual/command-reference.md`) are DRAFTED in
the P2 coder's report and landed by the supervisor on PASS — the standing division, now stated rather than
implied. **Production code is confined to `src/`** (tests, tools and the named documentation sit outside that
statement — the withdrawn "`src/`-only throughout" overstated it) ⇒ s18-inert by construction; boards are QG's
two-env comparison.

## 4. Pins (minimum; slices extend)
(1) A visible row's identity is its stable slot — reorder/disable proves no index leakage (the §B66 control).
(2) A `SendReq` seals the slot+generation it displayed; a mutation between press and execution ⇒ refuse +
repaint, ⛔ never the new words (the headline). (3) Emergency: cannot disable/clear/empty; text edit works;
no-fix alarm still sends without `-l`. (4) A channel slot with `loc=on` and no fix ⇒ loud refusal, `-l` never
stripped. (5) A DM slot with `loc=on` ⇒ the sealed-path requirement, no downgrade. (6) Zero enabled ⇒ the
empty state, back row only. (7) the four storage states each behave per the §3-P1 policy — absent silent, invalid warned+repairable,
io_failed warned+`store`-refusing-all-writes, valid loaded; `/mrcfg` untouched in every one (a phrase edit can
never reprovision — the design's own rule). (8) Identical `set` ⇒ zero writes (counted) — **over a
valid-or-absent store only** (the owner-ruled limit). (9) K7's `GRANT KEY` row byte-identical through the
rework. (10) The three NDJSON records exact (natively pinned). (11) The stale-generation refusal renders
**`PRESET CHANGED`** with zero core submission, then repaints from the current catalog. (12) The canonical-byte
rules each proven (wrap skips zero; disabled slots all-zero; tail-after-len zero; candidate-then-live order).
Mutations: index-derived slot · generation check dropped · emergency clear accepted · `-l` stripped to
fit/on failure · defaults-on-corrupt silently unwarned · io_failed treated as invalid (the repair-write
against a possibly-intact record) · the catalog written through `mrnv::Blob` instead of `/mrui` · K7 row
displaced · `PRESET CHANGED` fall-through to a generic failure · a mutating verb accepted during an active
emergency — each RED at match count 1.

## 5. Resources — with the STACK gate (owner-ruled)
`sizeof(PresetCatalog)` measured — ⛔ **corrected 2026-08-25 (P1 QG): this read "≈ ~370 B resident"; the
service retains THREE 372-byte blobs (live + two transactional scratch members, the ruled no-stack placement)
⇒ the eventual resident cost is ≈ 1.1 KB**, paid when P2 first instantiates it (P1 instantiates nothing; the
board RAM movement is measured at P2's gate); `UiSnapshot`
growth = the visible-list projection (frozen-frame rules; offsets proved). ⛔ **No full-catalog temporary on
the loop-task stack unless MEASURED safe** (the do_post_ack stack-overflow history is the reason): the
transactional candidate's placement is a design decision the P1 coder reports with sizeof/offset measurements
AND a current stack-headroom comparison. RAM/flash comparison on the ruled two envs (`heltec_mobile` +
`gateway`). Flash wear: coalescing counted natively; real wear stays the bench axis (M2).
