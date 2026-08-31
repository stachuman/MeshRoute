<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-E — typed terminal context · dispatch brief · 2026-08-31

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec
(`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`) — **§11 (the typed
terminal seam and its LOAD-BEARING seven-step order) + §17-Slice-E's four bullets + §18.4**. This slice
closes **[[B263]]** (the transit give-up pushing a generic `send_failed` under a FOREIGN `{dst,ctr}` — the
register row's close-by names exactly this slice). Foundations: Slice B's
`Node::terminal_carrier_outcome` (ALL ELEVEN post-admission carrier-death sites already route through it,
with `generic_owed` passed per-caller — the seam this slice retypes) and Slice B's deliberately-pinned
application-transit test (it exists to be FLIPPED here). ⛔ **"Do not emit custody traffic yet"** (§17-E
bullet 4): §11's step 7 (the custody-notice enqueue) is Slice F's — this slice implements steps 1-6 and the
typed context, leaving a clean seam step 7 plugs into. ⛔ NO DEVICE CONTACT.

## Scope (§11 + §17-E, made operational; ★ = the 2026-08-31 QG brief-review corrections)
0. **★ TWO SEPARATE TERMINAL SETS — do not conflate them (QG correction 1):** the eleven
   `terminal_carrier_outcome()` callers include queued/deferred carriers with NO live `PendingTx`, so the
   typed context and the seven-step sequence CANNOT apply to all eleven. Two audits, two tables:
   **(a) the eleven-caller B263 OWNERSHIP table** — per caller, the transit axis and the generic gate; the
   central gate becomes EXACTLY `generic_owed && own_origination && data_type_traits(type).generic_send_lifecycle`,
   where `generic_owed` REMAINS the caller's unchanged site-specific answer — ⛔ never recomputed centrally,
   ⛔ never blindly replaced with `carrier_owes_send_failed()`;
   **(b) the SELECTED CASCADE-TERMINAL table** — only the custody-eligible cascade branches get the typed
   stage/cause/repair context.
1. **★ The typed context + the RULED enum ownership (QG correction 3):** Slice E LANDS the single canonical
   `CustodyFailureReason` and root-stage types WITH the approved explicit values — Slice F adds only the
   codec/serialization of these existing types and may not introduce a second mapping (the §17-F wording
   update is supervisor-landed alongside this brief). No custody traffic, no wire behavior in E. The
   context at the selected terminals: root stage (**RTS root → CTS stage; DATA-ACK root → hop-ACK stage —
   ★ pinned, QG correction 2**) · terminal cause with **★ the pinned precedence `cascade_count →
   cascade_age → queue_full`, plus the SEPARATE causes `load_shed` and `one_way_throttled`** ·
   `repair_attempted` set by ACTUALLY INVOKING the repair-request logic in this terminal pass — never from
   an event-name string · an immutable REFERENCE to the live `PendingTx`, **★ valid ONLY before
   `_pending_tx.reset()` (QG correction 4)**. ★ Gateway timeout, no-route, NACK and the other §9.4-excluded
   deaths stay OUTSIDE this typed custody seam. §18.4.7's bar: one test AND one mutation per cause, and
   both stages. ⛔ §11's prohibitions stand: no ~352-B `PendingTx` stack copy; cause selection BEFORE the
   combined state is erased; ⛔ string prefixes are never enum authority (the renamed-emit mutation).
2. **The seven-step order at each terminal (steps 1-6 this slice)**: capture diagnostics while `PendingTx`
   exists → decide generic ownership → preserve the existing pre-reset generic `send_failed` for eligible
   LOCAL APPLICATION sends (ordering AND values byte-identical — §17-E bullet 2; the ack/reason vocabulary
   unchanged) → **suppress the generic event for TRANSIT and protocol-internal carriers** (the common
   ownership policy — `!has_previous_hop` for the transit axis, the trait for the internal axis; internal
   suppression already landed in Slice B — state per terminal what is NEW here vs already-landed) → reset →
   `become_free()` in the existing order. **★ The step-7 boundary, corrected (QG correction 4 — a
   `PendingTx&` cannot survive the reset, so E must not pretend a saved snapshot exists):** E establishes a
   NAMED INSERTION POINT after `become_free()` with its contract commented; **F materializes the bounded
   value snapshot BEFORE reset and consumes it after `become_free()`** — in E there is no snapshot object,
   no notice, no DATA, no wire anything, and **E's negative gate PROVES no `0x81` construction or custody
   enqueue exists** (a grep-backed test + the premature-enqueue mutation).
3. **[[B263]] closes**: a transit terminal give-up emits NO generic `send_failed` (the foreign-`{dst,ctr}`
   app-visible lie ends). The register row's own gate applies verbatim: a native case proving it + a
   mutation restoring the unconditional push ⇒ RED. Slice B's pinned application-transit case FLIPS with the
   correction idiom (it was pinned "reproducibly unchanged until Slice E" — this is Slice E).
   ⚠ Scope check, stated per site: `terminal_carrier_outcome` has eleven callers — for each, does the
   transit axis apply (can that terminal hold a transit carrier at all?), and what changes. The
   `carrier_owes_send_failed` helper (`node_carriers.h`) and `push_send_aired_if_owned`'s existing
   `has_previous_hop` gate are the in-tree prior art (U1) — ONE ownership policy, not a twelfth list.
4. **What deliberately does NOT change**: local application sends' `send_failed` ordering/values (pinned
   both sides); every protocol-specific event (`send_e2e_acked`, `team_key_grant_*`, RPC's contract); the
   B159 `expired` mapping; §6.3's not-implied list. The grant's own outcomes (B268) are protocol-specific
   and unaffected — say so with a test that still passes untouched.

## The corpus obligation — B263's visible half moves BY DESIGN
★ **THE PERMITTED DELTA IS EXACTLY ONE EVENT CLASS (QG corpus correction): only transit generic
`push{kind=send_failed}` events may disappear.** Existing `send_giveup`, `rts_giveup`, `data_ack_giveup`,
`path_cascade_exhausted`, repair and every other telemetry emit remain BYTE- AND ORDER-IDENTICAL — telemetry
is not user lifecycle and none of it is suppressed. Predict FIRST from the fresh baseline: which streams
carry transit give-ups on application carriers (s16's gateway holds are prime candidates), the exact
`push{send_failed}` events that disappear, and that NOTHING ELSE moves (deliveries, airtime, every other
event and every other push — identical; suppressing a report changes no routing). Prove with the Slice-B delta-comparator pattern (a
controlled Slice-E comparator or the documented event-diff — the Slice-A byte-comparator stays untouched);
`run_corpus.py --jobs 8` both arms; a re-anchor PROPOSAL (in-tree, §10-style) for the owner's single ruling
if hashes move — ⛔ the anchor table is edited only on that ruling. s18 keystone `76a67335/269527/0` read
from BASELINE.md (⛔ prior keystones historical).

## Gates
- Native: the B263 case verbatim · the flipped Slice-B pin (old→new with the idiom) · the local-application
  ordering/values pinned BOTH sides (before/after the same bytes) · per-terminal transit arms where
  reachable (the Slice-B/D production-shaped fixtures are prior art) · the no-string-parsing proof · the
  step-7 seam's inertness (nothing enqueued). Baseline cross-check 2418/101346/0 (derive by RUNNING;
  written derivation).
- Mutations (`sliceE*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): the unconditional push restored (B263 verbatim) · the ownership decision made AFTER reset
  (the order defect — cause/diagnostics read from a dead carrier) · a local application send's generic
  suppressed (over-correction) · the cause selected from an event-name string · the transit axis inverted ·
  ★ one arm PER CAUSE (`cascade_count`/`cascade_age`/`queue_full` precedence broken · `load_shed` merged ·
  `one_way_throttled` merged) and per STAGE (RTS→CTS vs DATA-ACK→hop-ACK swapped) per §18.4.7 ·
  ★ `repair_attempted` faked from a string · ★ the central gate's `own_origination` term dropped ·
  the step-7 insertion point prematurely enqueueing anything (the 0x81 negative gate).
- ⚠ THE WIRING-GATE RULE (standing, earned by B268 + Slice D): if this slice touches any src/ handler or
  push-consumer surface, name the instrument that compiles it and run it; `tools/probe_firmware_ui/run.sh`
  and `tools/probe_inbox_verbs/run.sh` both run regardless (cheap, and the second is new). No "makes no
  decisions" claims — evidence or a gate.
- Boards: `measure_board.py pair --jobs=2`, measured + attributed (the context struct is stack/transient —
  sizeof(Node) expected unmoved; ABI probe if any pinned struct moves). Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`; maintained docs = DRAFTS (protocol.md's terminal-order
  note if owed · the B263 register closure text · bench line if any metal residue — likely none, the
  terminals are host-reachable per B159/B/D precedent; say so). No `tracker.md`, no `platformio.ini`, no
  parallel-session files, no pollers, never pipe the battery runner.

## Report
The typed context's shape + where it lives · the seven-step order per terminal (steps 1-6, what was new vs
Slice-B-landed) · the TWO tables (eleven-caller ownership; selected-terminal typed context) · the per-cause/per-stage test+mutation matrix (§18.4.7) · the B263 case + flipped pin (old→new) · the
local-application both-sides proof · the corpus prediction-then-measurement + any re-anchor proposal ·
native + PIN · mutation ledger · the wiring-gate evidence + both probes' runs · board attribution · drafted
docs · exact final `git status --short`.
