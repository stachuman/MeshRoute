<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-H — user-send presentation (`DM UNCERTAIN`) · dispatch brief · 2026-09-01

**Status: ⛔ RE-PARKED 2026-09-01 (QG brief review) — DO NOT DISPATCH. The blocking finding is a PRODUCT
REACHABILITY GAP, not a design defect:** the OLED DM path always sends team-plane (`send <peer> "<text>"
-t -a`, `firmware_ui_send.h:545`), while custody v1 permits only static/global same-layer flights and
explicitly defers team-plane loss (§10.1.5-6 / §10.2) — so an OLED send has a retained `SendTracker` but
cannot legitimately produce a custody notice, and a console static/global send can produce one but has no
OLED tracker. Implementing H now would create a host-testable but OPERATIONALLY DEAD UI branch. **THE
PREREQUISITE (QG-recommended, product-aligned): a reviewed spec extension for TEAM-PLANE custody reporting**
(new eligibility + `CustodyFailurePlane::team` semantics — a P2 design effort), after which this brief's
shape (ratified below) implements cleanly. The corrections from the review are folded below so the brief is
correct WHEN un-parked. (Historical note: the owner reports the metal backlog tested 2026-09-01; the brief's
earlier "full Parts 35-53 passed" phrasing was QG-corrected as overclaiming what Part 53's pass establishes.) Authority: the custody spec — **§15 (two independent results; the
`{failed_dst, failed_ctr}` pair key; the EIGHT monotonic evidence rules; §15.4's ownership split) ·
§17-Slice-H's five bullets · §18.6's ten items (item 9: a match-by-counter-only MUTATION is RED — the
spec names it) · the arc-wide rule "no slice adds automatic payload retry".** Foundations: G's validated
receipt + `PushKind::custody_failure` (live, `{dst, ctr}` in the push) and pulled record · the OLED send
tracker (`SendTracker` — the UI-16/B268 machinery: `GRANT QUEUED→KEY SENT` etc. and the DM send-status
states; V1 its exact state set and correlation identity first) · B277 (the companion decoder gap — see the
boundary). ⛔ NO DEVICE CONTACT.

## The two-owner split, stated up front (§15.4)
- **The firmware OLED** correlates ONLY a LIVE report to a STILL-RETAINED local `SendTracker` — no durable
  correlation, no history screen (explicitly not required), the default inbox exclusion untouched.
- **The companion** owns durable/outbox correlation across reconnects using the PULLED record — that is
  COMPANION-SIDE work: this slice delivers the firmware surface + the CONTRACT text ONLY. ★ Honest
  completion statement (QG-corrected): contract text does NOT complete the companion half — the durable
  outbox-correlation state machine remains UNIMPLEMENTED, and B277 covers only decoding/cursor advancement,
  not that machine ⇒ a SEPARATE companion follow-up row is registered at dispatch time (or B277 explicitly
  widened — QG's choice offered; the separate row is this brief's default).

## Scope (§17-H's five bullets, made operational)
1. **Correlate exact `{dst, ctr}` user sends** (§15.2): the live push's pair matched against the retained
   tracker — ⛔ never counter alone (§18.6.9's named RED mutation); no cross-layer/stable-hash correlation
   (v1 generates no such reports — a guard, not a feature).
2. **`DM UNCERTAIN` presentation, nonterminal** (§15.3 rule 2): a waiting/queued/aired-waiting/
   not-confirmed send shows `DM UNCERTAIN` — WITHOUT consuming its E2E correlation identity (the late ack
   must still find it — state the mechanism: the tracker keeps its `{dst, ctr}` slot live). The OLED
   lexeme: `DM UNCERTAIN` is the spec's own term — verify it fits the panel's established row format
  (the S-lexeme inventory discipline; if the 21-column grid forces a variant, STOP and propose).
3. **Deadline + late-ACK behavior preserved** (§15.3 rules 5-6): the E2E deadline still fires and records
   its timeout fact but the UNCERTAIN presentation stays (the report does not cancel the deadline; the
   timeout does not erase the report); a LATER matching E2E ACK upgrades UNCERTAIN → DELIVERED. ★ FIVE
   ordering arms tested (QG-corrected — the earlier wording said "both" and listed three, omitting a
   required direction): report→timeout stays uncertain · timeout→report (NO CONFIRM becomes uncertain) ·
   report→ACK delivered · ACK→report remains delivered · timeout→report→ACK as the full late-ACK proof.
4. **Monotonicity** (§15.3 rules 1/3/4/7/8, §18.6): no tracker ⇒ diagnostic-only (rule 1 — the founding
   pubkey case's fate, unchanged); DELIVERED never downgrades (rule 3); locally-FAILED never overwritten
   (rule 4 — such a match is "stale, malformed or forged evidence": retain the diagnostic, touch nothing);
   the same report key idempotent for user state (rule 7); different reporters for one message = every
   diagnostic retained, ONE uncertain user state (rule 8).
5. **Uncorrelated reports stay diagnostic-only** — G's surfaces untouched; the correlation is a CONSUMER of
   the push, adding no storage, no new record, no wire anything. ⛔ THE ARC RULE: `DM UNCERTAIN` is not
   permission to retry — no retransmission, no route mutation, no trust mutation (§18.6.10; a test proves
   zero TX from the correlation path).

## Boundaries
- ⛔ No wire change, no storage change, no new PushKind, no lib/core behavior change expected — the natural
  home is the OLED send-status consumer (`firmware_ui_send.h`'s router + the tracker model) consuming the
  EXISTING `custody_failure` push. If a lib/core seam is genuinely needed, STOP and report.
- ⛔ The B268 grant machinery, the E2E-ack fast path, and G's diagnostic surfaces untouched (tests pin).
- The contract text: the companion's §15.4 obligation drafted (correlate on the pair from the pulled
  record; the same monotonic rules; render as a claim) — extending B277's close-by, not duplicating it.

## Corpus / gates
- Corpus: src/-side UI consumption of an existing push ⇒ expected INERT BY CONSTRUCTION (no lib/core
  change); prove with `run_corpus.py --jobs 8` + `--compare` vs pristine HEAD (byte-identity, the strong
  form — any movement is a STOP). s18 keystone `32afbf11/269517/0` from BASELINE.md.
- Native: the eight monotonic rules each as a case (the ordering arms per bullet 3); the counter-only
  mutation RED (§18.6.9 verbatim); the zero-TX proof; the no-tracker diagnostic-only case; the
  idempotency and multi-reporter cases. Production-shaped where the seam allows (the real push through the
  real router — the B268/4a precedent); model tests labeled per the established split. Baseline cross-check
  2503/103323/0 (derive by RUNNING; written derivation).
- Mutations (`sliceH*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): match-by-counter-only (§18.6.9) ⇒ RED · DELIVERED downgraded ⇒ RED · locally-FAILED
  overwritten ⇒ RED · the late-ACK upgrade dropped (UNCERTAIN terminal) ⇒ RED · the E2E identity consumed
  by the report ⇒ RED · a retry/TX initiated from the correlation ⇒ RED · the repeat-report transition
  re-fired ⇒ RED.
- THE WIRING-GATE RULE: `firmware_ui_send.h`/UI model are probe-compiled — the UI probe MUST cover the new
  arm (its PushKind sweep + the correlation checks; the B271/B272 lesson: probe twins updated same-slice,
  probe run); `probe_inbox_verbs` run regardless. Boards: `pair --jobs=2`, attributed (`sizeof(UiState)`/
  UI structs watched — ABI probe if a pinned struct moves). Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`; maintained docs = DRAFTS (the contract §15.4 section ·
  the OLED spec's send-status addition (S-lexeme entry) · register/protocol notes · the bench line) —
  supervisor lands. No `tracker.md`, no `platformio.ini`, no parallel-session files, no pollers, never
  pipe the battery runner.
- ★ The verification split, QG-corrected (the earlier metal sequence was INVALID — after terminal custody
  failure the carrier and payload are DESTROYED; bringing the destination back recreates nothing, so a
  "late ack" from power restoration is impossible; a real late ACK needs the DATA delivered BEFORE the
  custody report with the ack independently delayed — a controlled scenario):
  · HOST: the exact-pair chain custody report → uncertainty → timeout → late ACK → delivered, as native
    cases (fully host-reachable).
  · METAL: only a transition genuinely reachable once the team-plane prerequisite exists — drafted THEN,
    not now. Pin the actual starting OLED wording after V1 (likely `SENT, waiting` → `DM UNCERTAIN`, not a
    generic WAITING).

## Report
The tracker-state V1 (the exact state set + correlation identity) · each §15.3 rule's case + the ordering
arms · the counter-only RED proof · the zero-TX proof · the OLED lexeme/row evidence · the corpus
byte-identity · native + PIN · mutation ledger · probe runs · board attribution · drafted docs + the bench
line · exact final `git status --short`.
