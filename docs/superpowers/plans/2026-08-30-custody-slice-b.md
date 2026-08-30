<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-B — common internal behavior · dispatch brief · 2026-08-30

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec
(`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`) — **§6.2's eight rules are
THE contract**, with §6.3 (what internal does NOT imply), §6.4 (application envelopes), §17-Slice-B and
§18.2. Owner scope ruling 2026-08-30, five bullets: **trait-consumer wiring · fail-closed unknown-internal
handling · [[B264]] runtime closure · stray-`0x94` raw-key-delivery prevention · duplicated DM-floor lists
replaced by the shared authority.** (The spec's six §17-B items all map inside these: lifecycle suppression,
protocol-event preservation and the envelope proof ARE the trait-consumer wiring.) Foundation: Slice A's
landed-but-unconsumed `data_type_traits()` (0 bytes today — this slice is where it starts costing and paying)
and A0's matrix/census. ⛔ [[B263]] (transit send_failed) stays Slice E's. ⛔ NO DEVICE CONTACT.

## Scope (§6.2 made operational)
1. **The duplicated DM-floor exemption lists replaced** by `generic_send_lifecycle`/the trait authority —
   the two hand-copied lists in `become_free()`/`issue_send()` (A0 census; identical today, not drifted) and
   the 15 bare `!= DATA_TYPE_E2E_ACK` sites where the trait is the true predicate (each site judged: some
   are protocol-specific per §6.3 and STAY explicit — list every site with its verdict). §6.2.4: an internal
   own-origination bypasses `dm_min_interval_ms` and does not stamp `_last_dm_origin_ms`.
2. **Generic user-send lifecycle suppressed for ALL internal types** (§6.2.5): no `send_blocked`/`send_acked`/
   `send_failed`/`send_aired` for `internal=true` sends. ⚠ Bounded exactly: §6.2.6 — every PROTOCOL-SPECIFIC
   result stays (`send_e2e_acked`, `team_key_received`, RPC's own contract); §6.3 — internal is NOT an
   RTS-backstop exemption, NOT custody-exclusion, NOT priority. ⛔ The B263 unconditional `giveup_flight` push
   is NOT touched here beyond what the trait gate naturally does for internal types — the transit-vs-own
   dimension is Slice E's; state precisely what changes at each push site and what deliberately does not.
3. **Fail-closed handling at the delivery tail** (§6.2.2 — closes [[B264]]'s runtime half). **★ QG-corrected
   predicate (2026-08-30): the terminal guard drops ANY `traits.internal` value that reaches the
   ORDINARY-DELIVERY TAIL** — `known` means allocated-and-understood, it does not prove the handler RAN, so
   `internal && !known` would let a known-but-unwired internal type become inbox text (the exact B264
   class). The guard is "internal reached the tail", full stop: every wired internal handler consumes and
   returns before the tail, so a healthy frame never meets the guard; anything internal that does is by
   construction unhandled. Emits BOUNDED unsupported-internal telemetry and DROPS — never `record_dm`,
   never `msg_recv`, never ordinary inbox text. **★ Required control: a mutation weakening the guard to
   `internal && !known` must FAIL** (a known-but-unwired arm proves it). **The stray-`0x94` raw-key delivery
   dies here**: A0's characterization pinned the OLD behavior — that test FLIPS with the correction idiom.
   **★ S0 — telemetry discovery is an explicit ZERO-PRODUCTION sub-slice, FIRST**: inventory the existing
   bounded/rate-limited telemetry shapes (QG's own search found no obvious shared generic unsupported-DATA
   limiter); if none fits, **STOP after S0 and report — the bound/state decision gets its own review before
   any implementation.** No guard code lands until S0's answer is ruled.
4. **Every forwarding role preserved** (§6.2.3) — **★ QG-corrected placement: the guard runs AFTER (a)
   ordinary relay forwarding, (b) cross-layer forwarding, AND (c) hosted-mobile LAST-MILE forwarding by
   `DST_HASH`** — a home is the outer wire destination but only a PROXY for its hosted mobile, so a guard
   placed before that branch would consume unknown-internal traffic intended for the mobile. Tests: the
   ordinary relay/destination pair on the same frame PLUS **★ the hosted-mobile last-mile case** (an
   unknown-internal frame addressed through a home to its mobile is forwarded, not eaten).
5. **Application envelopes retain user semantics** (§6.4, the proof): INTRO/MOBILE_SEND/SEALED_RELAY/
   CHANNEL_POST keep their pacing, outcomes and fallbacks; an unknown APPLICATION-range type retains the
   typed-application fallback (⛔ outer `0x04`'s ordinary-delivery fall-through is APP-RANGE behavior and
   deliberately SURVIVES this slice — A0's characterization of it stays green; nobody "fixes" it here).
6. **The spec's sub-slice rule**: if an existing helper must be extracted to wire the trait, the
   behavior-neutral extraction is its OWN gated sub-slice (report it as such) before semantics expand.
7. **★ The [[B263]] boundary, explicit (QG-required):** INTERNAL transit failures lose generic lifecycle
   (that is §6.2.5 doing its job); **APPLICATION transit failures remain REPRODUCIBLY UNCHANGED until
   Slice E** — a test pins the current (defective, B263) application-transit `send_failed` behavior AS-IS so
   Slice E flips it deliberately. ⛔ No ownership/`has_previous_hop` fix here.
8. **★ `persistent_outcome` authorizes NO new generic storage dispatcher (QG-required):** E2E-ACK
   persistence stays its existing explicit path; PROVE no other current internal type is stored (a sweep +
   a test); Slices C/G own any generalized trait-driven consumption later.
9. **★ [[B266]]'s three missing coverage cases fold INTO this slice's tests (QG-ruled — the register's
   "whichever slice next touches the mobile/channel dispatch surface" close-by has arrived):** (a) the
   `MOBILE_LAYER_ANSWER` dispatch arm driven through the real receive path; (b) the CHANNEL_POST home-side
   unwrap driven with `etype=0x04`; (c) `mobile_layer_query_fire` exercised. **Coverage, not behavior** —
   each case pins current behavior; plus the dead `drive_post_ack_pubkey_push` scaffolding removed or used.

## The corpus obligation — behavior MOVES this time, by design
Slice A proved semantics frozen; Slice B changes them deliberately (suppressed generics, floor unification,
the fail-closed arm). The gate is therefore PREDICTED-DELTA, not identity:
- **Predict FIRST, per stream, from the fresh baseline** (BASELINE.md's 2026-08-29 table — the first slice to
  enjoy the clean re-anchor): exactly WHICH event kinds disappear/appear where (which streams emit generic
  lifecycle events for internal types today — measure from the NDJSON before coding; which streams carry
  unknown-internal or `0x94` frames — A0 says none, confirm ⇒ the fail-closed arm should be corpus-inert).
- Measure the deltas event-for-event against the prediction; any unpredicted delta = STOP. **★ QG-corrected
  instrument ruling: the Slice-A comparator (`tools/compare_corpus_semantics.py`) is PRESERVED AS-IS — its
  default ordered-renumbering behavior and 6/6 controls stay intact; build a SEPARATE controlled Slice-B
  comparator** (its own negative controls: an unpredicted event kind ⇒ RED · a predicted-suppressed event
  still present ⇒ RED · an unrelated field moved ⇒ RED) rather than overloading the proven one with a mode.
- Deliveries/duplicates/airtime figures re-stated with attribution; if hashes move (they will wherever an
  emit disappears), produce the **re-anchor PROPOSAL** for the owner's single ruling — same §10-style
  in-tree table, ⛔ never editing the anchor table (the 2026-08-29 ruling forbids agent edits without a new
  one).
- s18 keystone `b7aeaeeb/269905/0` read from BASELINE.md as the pre-slice reference (⛔ `9868cad3` is
  historical — never cite it as current).

## Gate
- Native: cases per §6.2 rule (floor bypass both ways · no-generic-for-internal + every protocol-specific
  event proven PRESENT · the fail-closed arm (drop + telemetry + nothing inboxed) with the `0x94` stray as a
  named case · relay-forwards-unknown-internal · the §6.4 envelope proofs · unknown-APP fallback unchanged).
  A0's flipped characterization edits listed old→new. Baseline cross-check 2351/100374/0 (derive by running;
  written derivation for the delta).
- Mutations (`sliceB*` isolated-harness targets, match 1, full pass per touched target, anchors re-derived):
  **★ SEPARATE arms for BOTH historical DM-floor lists — `become_free()`'s and `issue_send()`'s — each
  reverted to its exact-type list independently (§18.2.7)** · the terminal guard dropped (falls through to
  `record_dm` — the old `0x94` behavior verbatim) · **★ the guard weakened to `internal && !known` ⇒ RED**
  · the forwarding arm dropped (relay eats unknown-internal) · the hosted-mobile last-mile arm dropped (the
  home eats its mobile's frame) · a protocol-specific event suppressed (over-correction) · an app envelope
  losing its generic lifecycle (over-correction the other way) · the telemetry bound removed.
- Boards: the ruled pair, flash/RAM measured + attributed (the trait consumers now cost bytes — expected
  small; the removed duplicate lists may offset); ABI probe only if a pinned struct moves. Warning census;
  `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --` (the owner authorized ONE supervisor commit for Slice A —
  that authorization does not extend). ⛔ Maintained docs = DRAFTS in the report: protocol.md §2.4 gains the
  fail-closed rule (A0's §7.3 drafted text — "an addressed DATA whose TYPE has no handler is delivered as an
  ordinary DM today" becomes the fail-closed statement, landed by the slice that makes it true = THIS one);
  the register rows ([[B264]] runtime/0x94 closure; [[B266]] coverage closure; anything new). No `tracker.md`,
  no `platformio.ini`, no parallel-session files, no pollers, never pipe the runner.
- Metal residue: likely none (behavior host-reachable; no wire change) — say so or draft the exception.

## Report
The site-by-site floor-list verdict table · each §6.2 rule's evidence · the flipped A0 characterization
(old→new) · the corpus prediction-then-measurement with the per-stream event delta + any re-anchor PROPOSAL ·
native + PIN · mutation ledger · board attribution · drafted protocol.md/register texts · exact final
`git status --short`.
