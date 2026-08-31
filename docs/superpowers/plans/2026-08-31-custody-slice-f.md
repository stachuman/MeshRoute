<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-F — custody codec and relay generation · dispatch brief · 2026-08-31

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec — **§9 (the wire record, verbatim) · §10
(the twelve eligibility conditions + the deferred-sites list) · §11 step 7 (the snapshot lifetime E's
boundary defined) · §12 (transmission behavior) · §17-Slice-F (as corrected 2026-08-31: the codec
SERIALIZES Slice E's landed enums — ⛔ no second mapping) · §18.5/§18.1's surviving items.** Foundations:
Slice E's canonical enums + typed context + the NAMED step-7 insertion point (contract commented in
`node_cascade.cpp`), Slice A's `0x81` reservation ("Slice F owns its enum addition"), Slice B's tail guard
(the receive-side fate of the notice until G — see the intermediate state below). ⛔ Slice G (receiver
validation/persistence/output) is LATER; ⛔ the §10.2 deferred custody-loss sites stay untouched. ⛔ NO
DEVICE CONTACT.

## The intermediate state, stated up front (QG should ratify this explicitly)
F makes `0x81` an EMITTED type while G has not landed its receiver: **a transmitted notice is received by
neighbors and DROPPED at the Slice-B tail guard** (bounded `unsupported_internal` telemetry — the guard
doing exactly its job on a not-yet-consumed internal type). That is the spec's own slice order (§17 F
before G) and §12's contract makes it sound — ★ QG-corrected wording: the notice still uses ORDINARY hop
ACKs and hop retries like any DATA flight; what it never has is an E2E ACK, an application retry/deadline,
or a generic outcome. The
brief treats it as a FEATURE of the gate: the corpus will show the notices airing, relaying and dropping,
all attributable.

## Scope
1. **The enum member**: `DATA_TYPE_CUSTODY_FAILURE = 0x81` lands (Slice A's fence lifts — this is the
   owning slice). Trait row (per the one-authority doctrine): `known=true` (it now has a producer and §9's
   defined meaning), `internal=true`, `application_bearing=false`, `generic_send_lifecycle=false`,
   **`persistent_outcome=false` IN F with the in-source note that G flips it when the storing consumption
   lands** (the mark-done-vs-missing rule; ⚠ flagged for QG — §7.1's endpoint set includes it, but the
   trait should become true in the slice that makes it true). ⚠ The A0 matrix checker binds the CURRENT
   NAMESPACE table — the evidence row + checker pins update DELIBERATELY (the stale-value control exists);
   `check_data_type_literals.py` re-run (the new member's values must be symbolic everywhere).
   ★ Sweep the STALE persistent-set statements (QG correction 6): `frame_codec.h:848`-area claims custody
   joins the persistent set "when the codec slice adds 0x81" — now false under the ruled
   `persistent_outcome=false`-in-F; correct to "G flips it and closes the §18.2 endpoint" (the idiom).
   ★ Record explicitly (QG addition): NO `wire_version` bump — the post-Slice-B receiver safely drops
   `0x81`, and older pre-namespace fleets are already unsupported under the standing reflash-together
   ruling (protocol.md §2.4); the existing wire_version control keeps failing on any bump.
2. **The v1 codec — ★ ownership split ruled (QG):** F DEFINES the one pack/parse API, USES packing for
   generation, and EXERCISES parsing in native tests; **G wires parsing into receive handling, Push JSON,
   pulled JSON and persistence — F must NOT prematurely implement those G consumers.** §9.2's
   "do not re-read byte offsets separately" is the contract the API's existence enforces; golden byte vectors both directions;
   `record_len >= 24` tail-acceptance parsing (a v1 reader stores the validated tail, interprets 24).
   `notice_flags` per §9.3 — **exactly one of `failed_at_cts`/`failed_at_ack`** (derived `1u << stage`
   from E's `CustodyRootStage` — the design E's bit-number choice set up; an `invalid` stage can never
   reach the codec: E's seam guarantee + a codec-side refusal, both tested); `repair_attempted` bit 3;
   `next_was_one_way` bit 4; `has_dst_hash` bit 5 gating `dst_hash32` (⛔ never invented from a node id —
   §10's rule). `CustodyFailurePlane` §9.5: v1 transmits `static_same_layer` ONLY.
3. **Eligibility** (§10.1's twelve conditions, each a named predicate term and each TESTED both sides):
   live `PendingTx` · transit · normal DATA (not channel M/FLOOD) · plaintext · **★ static/global same-layer BOUND TO THE EXISTING AUTHORITY (`lib/core/node.h:425` or an
   already-captured equivalent wire-plane fact — QG correction 3; ⛔ explicitly FORBIDDEN: `pt.plane !=
   TEAM`-style re-derivation) with BOTH controls: AUTO resolving to TEAM ⇒ ineligible · GLOBAL stays
   eligible despite a colliding team ID** · none of the §10.1.6 exclusions · inner parses · parsed origin ==
   `PendingTx::origin` · valid ids 1..254 · ctr nonzero · type not `0x81`/`E2E_ACK` (⛔ the
   never-about-itself and never-about-an-ack rules) · death in a selected terminal branch. ⓘ Internal
   types otherwise eligible — **including `AUTHORITATIVE_H_ANSWER_PUBKEY`, the exact B59 case** (a test
   proves it). The §10.2 deferred list: untouched, verified per site (the Slice-E tables are the map).
4. **The snapshot + ordering** (§11 step 7, E's boundary honored): F materializes the BOUNDED VALUE
   snapshot (the 24-B record's fields + what §12 needs — measured size stated; ⛔ never the ~352-B
   `PendingTx` copy) BEFORE `_pending_tx.reset()`, and CONSUMES it at E's named insertion point after
   `become_free()`. The notice never inherits the failed carrier's counter, nonce seed, previous-hop
   exclusion, retry counters, flight generation or alternatives (§11's list — a test per field where
   observable, or the construction-path argument stated).
5. **Transmission** (§12, verbatim): the EXISTING standard typed-DATA enqueue path (U2 — ⛔ no
   field-by-field `TxItem` construction); `Plane::GLOBAL` explicit (⛔ never AUTO — the team-collision
   rule); fresh reporter counter; no E2E_ACK_REQ, no CRYPTED, no user deadline; the internal trait already
   exempts it from the DM floor and suppresses generics (Slice B's machinery — verify, don't re-implement);
   queued after the failed flight closes, never preempting the flight `become_free()` installed; a terminal
   failure of the notice itself = bounded telemetry only, ⛔ never another notice (the recursion gate
   TESTED: a notice's own terminal death generates nothing).

## The corpus obligation — REAL MOVEMENT EXPECTED (the first traffic-adding slice)
New frames air: eligible transit terminal deaths now enqueue 0x81 flights, which route, occupy airtime,
shift LBT/collision timing, and get received-and-dropped (the intermediate state). Predict FIRST, per
stream, from the Slice-E evidence's transit-give-up census: which streams have §10.1-ELIGIBLE deaths
(the twelve conditions prune hard — plaintext static same-layer selected-branch only), the expected notice
count per stream, and the expected knock-on classes (new `tx`/`rx`/`unsupported_internal` events; timing
reshuffles where airtime moved). ★ The two directions split honestly (QG correction 4 — a generated notice cannot prove its own
eligibility): THE CORPUS proves enqueued ⇒ eligible (every observed notice checked against independently-
derived failed-carrier/terminal facts from the BEFORE stream); NATIVE CASES AND MUTATIONS prove eligible ⇒
enqueued (each §10.1 term's both-sides tests + the per-term mutations); the never-about-itself/never-about-ack invariants hold
corpus-wide (zero 0x81-about-0x81, zero about E2E_ACK); deliveries/duplicates attributed if they move
(airtime shifts can reshuffle — the B177/s15 class; flag any delivery movement for the owner per the
standing rule). Instrument: a controlled Slice-F comparator/accountant (own negative controls); the A/B/E
comparators untouched. A re-anchor PROPOSAL (in-tree, §-style) for the owner's single ruling — expected
LARGER than E's; ⛔ the anchor table moves only on that ruling. s18 keystone `32afbf11/269517/0` from
BASELINE.md (⛔ all prior keystones historical).

## Gates
- Native: the codec goldens both directions + tail acceptance · the twelve eligibility terms each
  both-sides · the B59-case eligibility proof · the recursion gate · the snapshot ordering (fields read
  pre-reset, consumed post-become_free — the §CUSTODY-E/1c-style proof) · the not-inherited list · the
  §12 behavior set (no ack-req, no deadline, no generics — the Slice-B suppression verified applying) ·
  the intermediate-state receive test (a real 0x81 arriving drops at the tail guard with the bounded
  emit). Baseline cross-check 2437/101569/0 (derive by RUNNING; written derivation).
- Mutations (`sliceF*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): a codec offset shifted · the exactly-one-stage flags rule broken · ★ ONE MUTATION PER INDEPENDENT ELIGIBILITY TERM (QG correction 5 — no "at minimum"), or a NAMED
  compiler/structural proof per term that cannot be mutated (the Slice-E deliberate-absence idiom: evidence,
  not a waiver) · the recursion gate dropped ⇒ RED · the snapshot read
  after reset · dst_hash invented from a node id · the plane sent as AUTO-derived · a `TxItem` built
  field-by-field (structurally pinned if not battery-expressible — evidence, not a claim, per the standing
  wiring rule).
- THE WIRING-GATE RULE: name the instrument for any touched src/ surface; both standing probes run
  regardless. The A0 matrix checker + literal checker re-run (the namespace grew). Boards: `pair
  --jobs=2` (flash will grow — the codec + generation; attribute); ABI probe if a pinned struct moves
  (the snapshot type is new — state whether anything pins it). Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`; maintained docs = DRAFTS (frames.md's 0x81 row flips
  from reserved to live w/ the 24-B record table · protocol.md's custody-notice paragraph · the register
  updates · the CURRENT NAMESPACE evidence row) — supervisor lands. No `tracker.md`, no `platformio.ini`,
  no parallel-session files, no pollers, never pipe the battery runner.
- Metal residue: likely none in F (the notice path is host-reachable; G owns the visible outcome) —
  say so or draft the exception.

## Report
The enum landing + trait row (with the persistent_outcome flag decision) · the codec + goldens · the
twelve-term eligibility table with both-sides evidence · the deferred-sites verification · the snapshot
lifetime proof · the §12 behavior set evidence + the recursion gate · the intermediate-state receive proof ·
the corpus census-prediction-then-measurement + the notice accounting + the re-anchor PROPOSAL · native +
PIN · mutation ledger · checker/probe runs · board attribution · drafted docs · exact final
`git status --short`.
