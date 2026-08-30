<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-C — diagnostic inbox classification · dispatch brief · 2026-08-30

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec
(`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`) — **§17-Slice-C's six
bullets ARE the scope**, with §7 (durable internal outcome records: the opt-in set, the record mapping, the
never-through-the-text-encoder rule), §14 (the live/pulled diagnostic contract) and §18.3. Foundations:
Slice A's `data_type_traits()` (`persistent_outcome` — membership today exactly `{E2E_ACK}`; Slice F adds
CUSTODY_FAILURE via the SAME classification, which is the point of generalizing now) and Slice B's landed
consumers. ⛔ Slices D (inbox-only clear), E (typed terminal), F/G (custody codec/receiver) are LATER — no
custody record exists yet and none is minted here. ⛔ NO DEVICE CONTACT.

## Scope (§17-C's six bullets, made operational)
1. **Generalize internal-outcome record classification — ★ THE FIRMWARE PREDICATE IS
   `data_type_traits(type).internal`, NOT `persistent_outcome` (QG brief-review correction, load-bearing):**
   `persistent_outcome` controls which internal types may be WRITTEN; it is not the fail-closed presentation
   predicate — classifying by it would display an unknown/future internal record (reserved `0x81`, retired
   `0x94`, any value newer firmware writes) as ordinary text. The parent spec explicitly excludes
   `data_type_is_internal(type)` from ordinary views. ★ Required tests: ordinary DM/application records
   remain visible · E2E ACK hidden · reserved `0x81` hidden · another known/unknown non-persistent internal
   value hidden — which HONESTLY proves future CUSTODY_FAILURE needs no Slice-C presentation change (no
   synthetic-type fixture needed; the internal range itself is the proof). V1 the stored-type semantics
   first (`record_dm` stores 0, `record_ack` the E2E-ACK type; §B134's store is type-carrying). ⛔ NOT a
   storage change: what is WRITTEN stays as-is; this slice classifies at READ/presentation time.
   ★ Clarify in-code and in-docs: the shared trait predicate is FIRMWARE-SIDE — the Swift companion
   necessarily consumes semantic wire names (`"e2e_ack"` etc.), never the C++ trait table.
2. **E2E ACK classified by symbolic traits, not literal 3** — find every remaining consumer that
   distinguishes the ack record by number or by name-string where the trait/symbol is the true predicate
   (the A0 census + the §18.1.4 checker's current PASS bound the search space; anything the checker already
   forced symbolic just gets re-pointed at the shared predicate where that is the honest shape).
3. **Excluded from DEFAULT presentation, BEFORE budgets — ★ AND THE SPLIT IS RULED (QG brief-review
   correction, load-bearing): `pull_inbox` STAYS RAW.** The brief's first draft proposed filtering the
   companion's default pull at the firmware verb — that contradicts §7.4 and would BREAK live behavior: the
   companion uses a pulled E2E receipt to mark an offline outgoing message DELIVERED and advances its DM
   cursor by consuming it, and `inbox_end` does not independently advance those cursors — a firmware filter
   would repeatedly pull the same hidden sequence and lose offline delivery confirmation. The ruled shape:
   - `Inbox::pull()` and `pull_inbox` remain RAW and unchanged; `inbox_end.count` remains the raw streamed
     count. **The existing `pull_inbox` IS the diagnostic access the spec promises — no new verb, no flag.**
   - The COMPANION DECODER (its drafted contract text) consumes the receipt, correlates delivery, advances
     its cursor — and creates no conversation row and no unread message. (Companion-side semantics ride the
     wire names, per the firmware-side-predicate clarification above.)
   - The OLED filters firmware-side by the internal predicate, BEFORE its row budget and `inbox_total` (an
     inbox holding 3 DMs + 5 acks presents 3 rows, unread 3, and a budget of N is spent on visible rows
     only).
   Sweep every consumer of the inbox read path (`pull_inbox` — verdict: raw-by-design · the UI model's
   list/detail/unread · `inbox_end`/count surfaces) and give each a verdict: OLED-excluded /
   raw-by-design / untouched-with-reason.
4. **The live `send_e2e_acked` fast path PRESERVED** — the exact-correlation transition to `DELIVERED` is
   independent of the inbox and must be proven untouched (a pinned test, not an assertion of "we didn't go
   near it").
5. **Retained in DIAGNOSTIC pull — satisfied by bullet 3's ruled shape**: `pull_inbox` raw IS the
   diagnostic retention (§14 / §7.4 honored by not filtering it); prove with a case that a pulled stream
   still carries the ack records verbatim after the OLED exclusion lands.
6. **Eviction/deletion equal — proven** — internal records still age out of the ring and `Inbox::erase`
   them identically (no pinning, no separate lifetime); the §B134 tombstone/dedup machinery blind to the
   classification. Both directions tested (an ack evicted exactly like a DM under pressure; an ack erased
   by seq like a DM).

## Boundaries (stated so nobody re-litigates them mid-slice)
- ⛔ `record_ack`'s WRITE path and the stored bytes are untouched (Slice A already made it stamp the
  symbolic type). ⛔ No new PushKind, no notification change (B268's machinery is Slice B's, done).
- The B261 companion follow-up (mark-ack parsing) stays open and separate.
- §7.2's custody-failure record mapping and §7.4's ordering are IMPLEMENTED by F/G — here the
  internal-range predicate already proves CUSTODY_FAILURE arrives pre-hidden (bullet 1's `0x81` test).
- ⚠ The companion contract: the WIRE is unchanged (`pull_inbox` raw), so the drafted INBOX_SYNC_CONTRACT
  text documents the DECODER obligation (consume the receipt → correlate delivery → advance cursor → no
  conversation row, no unread) rather than a wire change; still V1 the live consumer code (`reconcile.py`,
  the iOS contract docs) to confirm today's behavior already matches or the doc names the gap ([[B261]]'s
  sibling class — report, don't fix companion-side here).

## Corpus / gates
- Predict FIRST: the classification and presentation seams live in `src/` + possibly `lib/core/inbox.h`
  read helpers; the sim never wires inbox stores (§B134) but DOES serialize pushes (§Slice-B's lesson) —
  state precisely whether anything sim-compiled changes behavior reachable in sim context; expected
  corpus-inert, PROVEN by the canonical runner (`tools/run_corpus.py --jobs 8` + `--compare` vs a pristine
  HEAD run — the new instrument's first production outing; `--jobs=1` arbitration if any doubt).
- Native: cases per bullet (the budget-before-exclusion arithmetic both sides; the fast-path pin; the
  eviction/erase equality pair; the diagnostic-pull retention — the generalization proof is bullet 1's
  internal-range visibility matrix, no synthetic type; obsolete wording removed at QG's round-2 note).
  A0/B134/UI-7D characterization edits listed old→new where presentation changes flip them.
- Mutations (`sliceC*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): the classification predicate dropped (acks reappear in the OLED list) · the predicate
  weakened to `persistent_outcome` (an `0x81`-class record becomes visible — the QG correction verbatim) ·
  the exclusion applied AFTER the budget (the ordering defect verbatim) · unread counting internal
  records · `pull_inbox` filtered (the over-correction that breaks the companion — the raw-by-design
  verdict attacked) · the eviction-equality broken (internal records pinned).
- Boards: the ruled pair via `measure_board.py pair --jobs=2` (the new instrument), measured + attributed;
  ABI probe only if a pinned struct moves. Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ Maintained docs = DRAFTS in the report (the
  INBOX_SYNC_CONTRACT correction · protocol.md's §2.4 presentation note if owed · register rows) —
  supervisor lands. No `tracker.md`, no `platformio.ini`, no parallel-session files, no pollers, never
  pipe the battery runner.
- Metal residue: likely ONE line (an ack-holding inbox on real glass shows only DMs; unread on the panel
  matches) — draft it; else say none.

## Report
The stored-type semantics V1 · the classification predicate + every presentation-seam verdict · the
budget-ordering proof · the fast-path pin · the diagnostic-pull shape per §14 · the eviction/erase equality
proofs · the companion-compat measurement · the generalization claim for CUSTODY_FAILURE · corpus
prediction + canonical-runner result · native + PIN · mutation ledger · board attribution (pair runner) ·
drafted docs + the bench line · exact final `git status --short`.
