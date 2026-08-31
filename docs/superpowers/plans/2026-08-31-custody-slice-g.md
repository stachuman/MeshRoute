<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-G — receiver, persistence and factual output · dispatch brief · 2026-08-31

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec — **§13 (the EIGHTEEN receiver validations,
verbatim) · §7.2 (the stored record mapping) · §7.3 (record-BEFORE-push, the five-step order) · §7.4 (the
presentation split Slice C already built) · §14 (PushKind/JSON/USB) · §17-Slice-G's six bullets ·
§18's items.** Foundations: F's one codec (`parse_custody_failure` — G wires the consumers F was forbidden
to touch) · the ruled `persistent_outcome` flip (G's job, noted in-source at the trait) · Slice C's
internal-classification machinery (custody records arrive PRE-HIDDEN — proven there; G must not re-implement
presentation) · B134's store (the record body is BINARY — §7.2's never-through-the-text-encoder rule) · the
B268 PushKind discipline (append, sweep every mapping, `-Wswitch`-complete) · the Slice-D wiring-gate rule.
⛔ Slice H (optional user-send presentation) is LATER. ⛔ NO DEVICE CONTACT.

## The state transition, stated up front
G ENDS the ratified intermediate state: an addressed `0x81` stops dying at the Slice-B tail guard and is
CONSUMED by a wired handler BEFORE ordinary DM delivery (the guard keeps protecting everything else — a
test proves both: `0x81` consumed, another unknown-internal still guard-dropped). Corpus-wide, the
`unsupported_internal{type=0x81}` events DISAPPEAR and `custody_failure` pushes appear — movement again,
with a re-anchor proposal.

## Scope (§17-G's six bullets, made operational)
1. **Validate and consume** (§13): the eighteen validations, each a named check, BEFORE storage/push/
   correlation/output; failures drop with bounded local telemetry (the F/B `unsupported_internal`-class
   shape — find the honest lexeme; a MALFORMED record is not evidence: §13's outer-vs-body rule), are
   neither stored nor user-exposed; a valid unknown tail is retained durably and ignored by the v1 decoder.
   ⚠ Consumption happens at the dispatch position §13 fixes ("before ordinary DM delivery") — state where
   that lands relative to the Slice-B guard and the hosted-mobile last-mile (the three-forwarding-roles
   lesson: a home must still FORWARD a mobile-addressed notice, not consume it — test it).
2. **Record before Push** (§7.3's five steps, order load-bearing): parse+validate → append the internal
   outcome per §7.2's mapping (`kind=dm · type=0x81 · origin=reporter · msg_id=failed_ctr · body=the
   validated record incl. tail · body_len=record_len`; ⛔ the body NEVER passes the text encoder/OLED
   sanitizer) → obtain the gap-tolerant seq → enqueue the live Push carrying it → return WITHOUT DM
   delivery or E2E-ACK generation. Storage-disabled ⇒ push carries `seq=0`; append failure/drop-oldest
   unchanged, no retry, no protected slot.
3. **`PushKind::custody_failure`** (§14.1): APPENDED, `Push` does NOT grow — the existing fields per the
   table verbatim; ⛔ the custody reason NEVER in `Push::reason` (a `SendFailReason` — deliberately
   independent enums); JSON/human output parse `Push::body` through F's codec (⛔ no second offset-reader —
   §9.2's contract). The full B268 sweep: every serialized PushKind mapping updated, `-Wswitch`-complete,
   the probe layer's PushKind twins updated IN THE SAME SLICE (the B271/B272 lesson — the probes compile;
   run them).
4. **Live + pulled semantic JSON and USB** (§14.2): the `custody_failure` event with the required fields
   verbatim (`stage`/`reason` as SEMANTIC names from E's enums — one name table beside the codec, the
   `pushkind_name` idiom); a pulled record may add its receive timestamp; the USB human line (the
   established console shape — drafted for the command-reference). ⚠ `persistent_outcome` FLIPS TRUE here
   (the ruled G job) — with the §18.2-endpoint note closed at the trait, and the Slice-C visibility matrix
   re-run: the stored record STAYS pre-hidden from ordinary views/unread (C's machinery, proven still
   binding — not re-implemented).
5. **Excluded from ordinary message UI**: Slice C's internal predicate already does this — the proof is a
   test showing a STORED custody record produces 0 OLED rows / 0 unread / raw-pull-visible, plus the §18.3
   item-10 both-paths case (one received E2E ACK → live DELIVERED transition AND no ordinary row — extended
   to the custody record's analogue).
6. **Invalid/unsupported reports stay out of storage**: per-validation rejection tests (eighteen arms — the
   F golden-rejection idiom: each byte broken off a control that parses) with the drop-telemetry bounded
   (fixed fields, no body bytes — the ruled S0 bound class).

## The corpus obligation — the intermediate state ends
Predict FIRST from F's accounting: the 25 notices' receiver-side fate flips (guard-drops → consumptions);
per stream: `unsupported_internal{0x81}` events that DISAPPEAR, `custody_failure` push events that APPEAR,
inbox-store writes where wired (⚠ the sim wires no stores — B134: `on_init` unreached ⇒ seq=0 pushes in sim
context; verify), and any knock-on timing. ⚠ The failed_origin==self validation (§13.11) means only the
ORIGINAL SENDER consumes; relayed notices en route still... (V1: a relay is not the addressee — the notice
routes to failed_origin, intermediate hops forward it as normal transit DATA; only the addressee consumes —
state and test). The accountant/comparator extended or a Slice-G sibling built (own controls); a re-anchor
PROPOSAL for the owner's single ruling; ⛔ the anchor table only on that ruling. s18 keystone
`32afbf11/269517/0` (s18 had no notices — predict whether it stays byte-identical). Deliveries/duplicates
attributed if they move (flag for the owner per the standing rule).

## Gates
- Native: the eighteen validation arms · the five-step order proof (record-before-push observable — the
  seq in the push equals the stored seq; storage-disabled ⇒ seq=0) · the state-transition pair (0x81
  consumed / other unknown-internal still guard-dropped) · the hosted-mobile forward case · the
  §14.2 JSON goldens (live + pulled, semantic names) · the visibility matrix re-run · the B59 END-TO-END
  case (the arc's founding scenario: a 0x8B transit death → notice → the original sender's stored record +
  push with the B59 fields — the closing proof this design exists for). Baseline cross-check 2463/102123/0
  (derive by RUNNING; written derivation).
- Mutations (`sliceG*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): a validation arm dropped (per §13 term or named structural proof — the F per-term standard) ·
  push-before-record (the order defect) · the reason placed in `Push::reason` · a second offset-reader
  introduced (structurally pinned if not expressible) · the guard consuming for a non-addressee · the text
  encoder reached by a record body · `persistent_outcome` not flipped (the §18.2 endpoint unmet) · the
  tail dropped from storage.
- THE WIRING-GATE RULE: the new PushKind touches src/ consumers — name the instruments, run both standing
  probes + any touched probe twins; the A0/literal checkers re-run. Boards: `pair --jobs=2`, attributed
  (`sizeof(Push)` must NOT grow — its static_assert; ABI probe if anything pinned moves). Warning census;
  `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`; maintained docs = DRAFTS (protocol.md's receiver
  paragraph replacing the intermediate-state warning · frames.md's row losing the "receivers DROP" note ·
  the INBOX_SYNC_CONTRACT custody-record section (the companion sees a new `ev:custody_failure` and a new
  stored type — B261-class compat statement measured, not assumed) · register updates · the command-
  reference USB line) — supervisor lands. No `tracker.md`, no `platformio.ini`, no parallel-session files,
  no pollers, never pipe the runner.
- Metal residue: likely ONE drafted line — the B59 scenario on real glass/USB (a relay's custody failure
  surfacing at the sender as the pulled record + USB line) — the arc's first operator-visible outcome;
  draft it or argue none.

## Report
The eighteen-validation table with per-arm evidence · the five-step order proof · the state-transition +
hosted-mobile cases · the PushKind sweep (every mapping named) · the JSON/USB goldens · the visibility
re-proof · the B59 end-to-end case · the persistent_outcome flip + endpoint closure · the corpus
prediction-then-measurement + re-anchor PROPOSAL · native + PIN · mutation ledger · probes/checkers ·
board attribution (`sizeof(Push)` unmoved) · drafted docs + the bench line · exact final
`git status --short`.
