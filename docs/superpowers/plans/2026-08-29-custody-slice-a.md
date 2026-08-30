<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-A — the DATA-namespace transition · dispatch brief · 2026-08-29

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process).**
Authority: the custody spec (`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`)
— **§5 (ranges + the EXACT assignment table + §5.3 no-wire-version-bump ruling) · §6 (the one trait
authority) · §7.6 (store v4→v5 migration) · §17-Slice-A (the scope list) · §18.1 (the verification gate) ·
the documentation-ownership gate (§18 tail)**. Inputs from A0 (all in
`docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md`): the matrix, the census, the pre-Slice-A corpus
baseline (§5.2 incl. the 15 pre-existing movers), and rows **[[B265]]** (closed by this slice) + **[[B267]]**
(a statement owed by this slice's report). ⛔ **No custody codec, PushKind or terminal behavior belongs here
(§17); [[B263]]/Slice E and [[B264]]'s runtime arm/Slice B stay OUT.** ⛔ NO DEVICE CONTACT.

## Scope (spec §17-A, made operational; ★ = the 2026-08-29 QG brief-review corrections)
1. **The range/trait authority + exact enum assignments** — §5.1's ranges with the EXACT predicate
   (`t >= 0x80 && t <= 0xBF`; ⛔ the spec names `t & 0x80` as the WRONG form — it admits `0xC0..0xFE`);
   §5.2's assignments for every CURRENT member. **★ The enum boundary, ruled exactly:** ADD/reserve
   `DATA_TYPE_APP_MESSAGE = 0x05` WITHOUT behavior; ⛔ do NOT add `DATA_TYPE_CUSTODY_FAILURE` (0x81) —
   **Slice F owns its enum addition**; §5.2's row for it is a forward reservation only. **★ The trait
   authority's EXACT Slice-A truth table (★ QG-required; derived from §6 — the coder verifies every cell
   against §6/§6.4 and STOPs on any conflict; landed as a table-driven native test, NO Slice-B consumer
   wired):**

   | value(s) | known | internal | app_bearing | generic_send_lifecycle | persistent_outcome |
   |---|---|---|---|---|---|
   | `0x00` ordinary DM | T | F | T | T | F |
   | `0x01` INTRO · `0x02` MOBILE_SEND · `0x03` SEALED_RELAY | T | F | T | T | F |
   | `0x04` CHANNEL_POST | T | F | T | T | F |
   | `0x05` APP_MESSAGE (reserved, unimplemented) | **F** | F | T | T | F |
   | `0x80` E2E_ACK | T | T | F | F | **T** |
   | known internal: `0x88` H_ANSWER · `0x89` AUTH_H_ANSWER · `0x8B` AUTH_H_ANSWER_PUBKEY · `0x90` MOBILE_H_ANSWER · `0x91` BREADCRUMB · `0x92` LAYER_QUERY · `0x93` LAYER_ANSWER · `0x95` MOBILE_H_ANSWER_PUBKEY · `0x96` KEY_FORWARD · `0xA0` REMOTE_CMD · `0xA1` REMOTE_RESP · `0xA2` TEAM_KEY_GRANT | T | T | F | F | F |
   | `0x8A` (type 4's heir — reserved, never emitted) | **F** | T | F | F | F |
   | `0x94` (type 12's heir — RETIRED, zero producers/consumers) | **F** | T | F | F | F |
   | unknown application-range (`0x01..0x7F` unallocated) | F | F | T | T | F |
   | unknown internal-range (`0x81..0xBF` unallocated — **incl. `0x81` until Slice F adds CUSTODY_FAILURE**) | F | T | F | F | F |
   | `0xC0..0xFD` reserved · `0xFE` tombstone (never wire) · `0xFF` invalid | F | F | F | F | F |

   Rationale, pinned: **reserving a number ≠ knowing the type** — reserved `APP_MESSAGE`, reserved `0x8A`
   and retired `0x94` are `known=F`, taking their range's unknown behaviour (fail-closed in the internal
   range once Slice B lands its unsupported arm, which also retires A0's stray-type-12 raw-key-bytes
   delivery; the typed-application fallback in the app range per §6.4). **Exact `persistent_outcome`
   membership at Slice A = {E2E_ACK} only** (Slice F adds CUSTODY_FAILURE); the trait governs internal
   OUTCOME records (§7) — application-message inbox persistence is the ordinary `record_dm` path, not this
   trait's subject. **CHANNEL_POST gets ONE context-independent result (its row above)**; separately noted:
   the trait does NOT promise an outer addressed handler — A0's established outer-18 ordinary-delivery
   fall-through REMAINS until Slice B. The authority LANDS without wiring Slice-B behavior into any
   consumer (the duplicated-list replacement stays Slice B's).
2. **Renumber every current type; remove numeric literals; update active contracts** — [[B265]]'s three
   sites (`console_json.cpp:539` `type == 3` + the `console_json.h:336` doc comment + `test_console_json.cpp:216`),
   **★ PLUS `lib/core/frame_trace.h:76` — five live numeric DataType `case 1:`..`case 5:` labels QG found
   beyond A0's census (post-renumber they'd silently misidentify frames in debug output): replace with
   symbolic cases, include SWITCH-CASE LABELS in the §18.1.4 structural literal check and its mutation, and
   record the A0-statement correction ("console_json.cpp is the sole production numeric coupling" — now
   known false) as a Slice-A correction in the evidence file's correction idiom, without reopening A0** —
   plus anything the §18.1.4 check finds; that check itself lands (a controlled search rejecting surviving
   semantic literals INCLUDING case labels — a reintroduced known instance must make it fail, §18.0.3).
   **★ The active contracts that must move, named:**
   `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` (REMOTE_CMD/RESP 6/7 → 0xA0/A1)
   and `docs/superpowers/specs/2026-08-05-channel-app-code-draft.md` (the APP_MESSAGE reservation) — the
   coder DRAFTS their corrections (supervisor lands; parallel-session files stay untouched by the coder);
   superseded/archived documents may keep old numbers only where clearly fenced as historical.
   **★ The A0 matrix checker must SURVIVE the renumbering without rewriting history:** A0's pre-Slice-A
   corpus baseline and historical matrix content stay UNCHANGED; the evidence file gains a clearly
   positional "CURRENT NAMESPACE" table (or current-value column) that Slice A updates; `check_a0_matrix.py`
   re-binds to THAT table specifically (positionally — not any matching row anywhere), with new controls:
   a STALE current value ⇒ RED, and a MISSING `APP_MESSAGE` row ⇒ RED.
3. **`protocol::wire_version` unchanged, with the control** (§18.1.6): a case/assert that FAILS if it is
   bumped during this transition — the owner ruling pinned in code.
4. **Store v4→v5** (§7.6): semantic version only — the 28-B layout is unchanged, so the bump rides the
   LANDED same-length version-upgrade path (wipe records · retain `next_seq` · reset read cursor · epoch +1
   once). ⚠ Post-B134 the "no second increment on empty detection" bullet is satisfied by the upgrade wipe
   leaving `records_state = empty` (ACKNOWLEDGED) — prove it (three boots after the upgrade: one bump total).
   §18.1.8: an old stored type-3 E2E receipt can NEVER reappear as a sealed-relay/application record —
   test the arm. Volatile store stays boot-scoped (§18.1.9).
5. **The A0-§7.2 comment corrections land HERE — SIX source sites across four A0 findings (★ QG count
   correction: the earlier "five" undercounted):** the `0x01`-alias truth at `frame_codec.h:682/698/702`
   (one finding, three lines = site 1), the breadcrumb originator at `:753` (site 2), type 12 RETIRED at
   `:756` (site 3), the two not-enforced-invariant qualifiers at `:760` (site 4) and `:762` (site 5), and
   `node_mac_rx.cpp:1767`'s "harmless" (site 6) — all per the correction idiom, B254-aware
   (line-count-neutral or size-fields reported).

## The corpus obligation — the slice's center of gravity
Renumbering moves the TYPE byte in nearly every typed frame ⇒ most air-bytes move ⇒ **hashes may move
everywhere; SEMANTICS may move NOWHERE.** The gate is semantic-neutrality, proven not asserted:
- **Predict FIRST** from A0's matrix corpus-reach data: which of the 36 streams carry typed DATA (those
  hashes move) and which carry none (those must stay byte-identical). [[B267]]: types 1/13/16/19 have zero
  corpus reach — their renumbering is proven by NATIVE round-trips only; SAY SO in the report.
- **A/B against A0's recorded baseline** (evidence §5.2 — NOT the stale anchor table). **★ The comparison
  is ORDERED EVENT-BY-EVENT, not count-equality (QG: one event can change into another while counts stay
  equal):** normalize ONLY the explicitly identified old→new TYPE fields and their directly derived
  raw-frame/checksum material; EVERY other event name, field, order and timestamp must be IDENTICAL,
  per stream. Event counts, delivery counts and analyzer figures remain SECONDARY cross-checks. **Negative
  control required:** a comparison run with one non-TYPE event field (or an event kind) altered must FAIL —
  the comparator is an instrument, not a formatter. Any semantic delta = STOP, it is a defect.
- **The re-anchor is a PROPOSAL, not an act**: produce the full measured 36-row new-hash table with the
  per-stream attribution, as the owner's single-ruling re-anchor package (the standing anchor-table
  discipline — the s18 keystone re-anchors only by owner ruling; ⛔ the coder never edits
  `simulation/BASELINE.md`'s anchor table; supervisor lands it ON the ruling).
- Native: §18.1.1 static asserts pin every new value + both range boundaries; §18.1.5 every live type
  round-trips `pack_data`/`parse_data` at its new value; A0's characterization suite (2343/98966 base) must
  pass UNCHANGED except where a case pins an OLD number — each such edit listed with the old/new pair.

## Gate
- Mutations (new `sliceA*` isolated-harness targets, each RED at match 1): the range predicate degraded to
  `t & 0x80` · one renumbered value reverted to its old number · the wire_version control deleted ·
  the v5 migration's wipe arm dropped · the type-3-receipt-reappears arm broken · the [[B265]] literal
  reintroduced (must also trip the §18.1.4 search control) · **★ a `frame_trace.h` case label reverted to
  its numeric form (trips both the trace test and the search control)**. Checker selftest additions (★):
  a stale current-namespace value ⇒ RED · a missing `APP_MESSAGE` row ⇒ RED. The event comparator's
  negative control (a non-TYPE field altered ⇒ comparison FAILS) runs with the corpus A/B. Full pass per
  touched target; anchors re-derived.
- Native derived-not-quoted (2343/98966/0 is the cross-check; written derivation for the delta). Boards:
  the ruled pair, flash movement measured + attributed (an enum renumber should be near-noise — if it isn't,
  explain); no struct change expected ⇒ ABI probe only if one moves. Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ Maintained docs = DRAFTS in the report — and this
  slice OWES two big ones per the spec's doc-ownership gate: the complete renumbered DATA table for
  `docs/frames.md` (wire-oriented: values, ranges, layouts — no behaviour prose) and the `docs/protocol.md`
  namespace + mixed-firmware/reflash ruling + migration text. Supervisor lands both on PASS. No
  `tracker.md`, no `platformio.ini`, no parallel-session files, no pollers, never pipe the runner.
- Metal residue: the fleet consequence only — after this lands, old and new firmware disagree on every
  typed frame, so **the whole test fleet reflashes together** (M3 makes it free; the mixed-firmware warning
  goes in the drafted protocol.md text). One drafted bench line: first boot after reflash shows the v5
  migration (`epoch` +1, empty inbox, records wiped once) — plus the B267 statement.

## Report
The full old→new landing (every enum member accounted against §5.2's table) · the literal-removal census
result + the standing search control · the wire_version control · the v4→v5 proofs (wipe/retain/cursor/
epoch-once/acknowledged-empty/type-3-never-reappears) · the six comment-correction sites across four A0
findings · the trait truth-table test result · the corpus prediction-then-measurement with the
ordered-event semantic-identity proof (+ the comparator's negative control) and the RE-ANCHOR PROPOSAL
table · native + PIN derivation · mutation ledger · board attribution · the FOUR drafted
maintained-document packages (the frames.md table · the protocol.md namespace/reflash/migration rulings ·
the remote-admin RPC design correction · the channel-app-code draft correction) + the drafted bench line ·
exact final `git status --short`.
