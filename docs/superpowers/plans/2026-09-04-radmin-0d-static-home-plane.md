<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0d — static-home plane invariant · dispatch brief · 2026-09-04

**Status: DRAFT — awaiting Quality-Agent review.** Dispatch model after PASS: **Opus**.
Authority: the design-pass
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`, R-RA-12, and the
D-0d-1 scope decision below. Pre-check input:
`docs/superpowers/plans/2026-09-04-radmin-0d-precheck.md`.

This is one `lib/core` **behaviour-fix** slice. Every `send_by_hash` arm whose immediate
destination is a mobile home must stamp `Plane::GLOBAL`, because a home is a static node.
It fixes a real collision failure: `AUTO` can select the team routing table when a teammate's
team-local ID equals the home's static ID, causing the wrapper to leave toward the teammate
instead of the home. It does not add remote RPC behavior, change any frame, or refactor an
unrelated routing path.

## Contract quoted verbatim from the reviewed design

> - **0d, static-home plane invariant:** implement R-RA-12 as the only behaviour change. ⚠ **SCOPE
>   CORRECTION D-0d-1, 2026-09-04; the passed three-arm wording remains visible:** the passed design said
>   “the three `send_by_hash` delegation arms that send a wrapper toward the mobile's static home stamp
>   `Plane::GLOBAL` instead of `Plane::AUTO`.” The same invariant also governs the cached-home arm that sends
>   to a target mobile's static home (`lib/core/node_hashlocate.cpp:1859` at pre-check): an unregistered team
>   mobile has the same local-ID collision exposure. Slice 0d therefore changes all **four** home-bound arms
>   together and re-aims B278-S3's static-sender AUTO-equivalence pin to explicit GLOBAL. Pin “home is static,
>   never gateway” and the mixed-ID collision
>   that previously selected team plane; predict and attribute every corpus delta before accepting it. Its
>   source fence includes the correction-idiom rewrite of `lib/core/node_mac.cpp:159-161`, whose claim that
>   the named paths are “the only producers of GLOBAL” becomes false when these four producers land; and

The gate ownership row is also authority:

> | 0d | four home-bound arms, `lib/core/node_hashlocate.cpp` | prediction-first 36-row delta; re-anchor ruling if hashes move; ruled pair | none |

## D-0d-1 — include the fourth cached-home arm

**AUTHOR DECISION 2026-09-04:** include the cached-home send at the pre-check's
`lib/core/node_hashlocate.cpp:1859` as the fourth arm. B278-S3 proved `AUTO` equivalent to
GLOBAL only for a static sender; that attribution pin was not a permanent policy ruling.
An unregistered team mobile can take this arm and has the same ID-collision exposure as the
three registered-mobile wrapper arms. The immediate destination is still a static home, so
one invariant and one slice own all four sites.

Re-aim the B278-S3 case from “AUTO happens to equal GLOBAL for this static sender” to “the
cached-home arm stamps GLOBAL explicitly”, then add the unregistered-team-mobile collision
case that makes the difference observable. This does not alter B278 correlation, custody
state, destination hash, source hash, counter, or ACK behavior.

## Required result

0d passes only when:

- all four current home-bound `do_send` sites use `Plane::GLOBAL`, with no fifth policy list
  or caller-specific reconstruction;
- a collision at each reachable arm routes on `_rt`, uses a static RTS, and reaches the home;
- the same fixtures with no collision retain the pre-slice packet and routing behavior;
- the registered-mobile enqueue guard and origin stamp remain unchanged;
- the cached-home arm is proved for both its ordinary static-sender case and the formerly
  vulnerable unregistered-team-mobile case;
- the stale `node_mac.cpp` producer comment is corrected with its old claim visible;
- the corpus delta is a subset of the prediction instrument's nonzero-collision streams and
  every changed line is attributed; and
- no wire, layout, RAM, feature, remote-admin, B278, or unrelated routing behavior changes.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. a home-bound arm cannot be made explicit GLOBAL without changing carrier bytes other
   than consequences of selecting the correct routing plane;
2. any changed site can legitimately target a team-plane peer rather than a static home;
3. the cached-home arm needs a policy different from the three registered-mobile wrapper
   arms, or fixing it changes B278 correlation/custody semantics;
4. `can_host_mobiles()` no longer proves that a home is non-mobile, non-gateway and
   single-layer, or another production definition of “home” contradicts it;
5. a new helper, state field, flag, frame field, DataType, timer, feature macro, or
   `wire_version` change is required;
6. any file outside the fence must change, including `src/`, a codec, board configuration,
   protocol constants, or `simulation/BASELINE.md`;
7. the throwaway BEFORE instrument cannot distinguish the four arms or cannot prove the
   real checkout unchanged after its run;
8. an AFTER corpus mover comes from a stream whose measured BEFORE collision count was
   zero, or a changed event/packet cannot be explained by correcting that collision;
9. delivery, duplicate, failure, routing, or airtime figures move without exact
   per-occurrence attribution; the coder does not accept the trade or edit anchors;
10. the keystone read fresh from `simulation/BASELINE.md` moves despite containing no
    mobile, or any zero-predicted stream moves;
11. `sizeof(Node)`, any ruled-board RAM value, or an ABI-visible layout changes; or
12. a mutation is GREEN, vacuous, multi-matched or unusable and cannot be repaired within
    the authorized tests/harness.

## Fence

Allowed production edits:

- `lib/core/node_hashlocate.cpp`: only the four home-bound plane arguments and comments
  whose truth depends directly on them; and
- `lib/core/node_mac.cpp`: only the correction-idiom rewrite of the stale
  `Plane::GLOBAL`-producer comment near the pre-check's lines 159–161.

Allowed supporting edits:

- `test/test_dual_layer.cpp`;
- `test/test_custody_receive_g.cpp`, only to re-aim the B278-S3 plane assertion and add its
  directly related control;
- `tools/probe_ui_model_mutations.py`, for correctly routed mutation entries and PIN only;
- `tools/count_radmin_0d_home_collisions.py` plus
  `tools/test_count_radmin_0d_home_collisions.py`, only if existing corpus tooling cannot
  count and validate the throwaway events safely; and
- `docs/superpowers/evidence/2026-09-04-radmin-0d.md`.

No file under `src/`, `lib/console/`, `lib/hal/`, `variants/`, `simulation/`, or
`ios-companion/` may change. No `platformio.ini`, `simulation/BASELINE.md`, protocol/frame
doc, bug-register, tracker, bench-script, spec-status or memory edit belongs to the coder.
The coder drafts the two held documentation landings in evidence; the Author lands them
after QG PASS and any required corpus ruling.

The only board environments are `gateway` and `heltec_mobile`. The warning census's own
pinned environment set is the sole standing exception.

0d may dispatch in parallel with 0e only in an isolated worktree with private build/output
roots. Their production fences do not overlap. Both may mechanically update the native PIN;
the second branch to land must rebase, rerun the real native binary and derive the combined
PIN rather than choosing one side of a conflict. Any corpus or board comparison made across
worktree paths must respect B262's heltec-mobile payload-hash limitation.

## 0d-1 — verify the four exact production sites before editing

Re-read and record the current function, branch predicate and full `do_send` argument list
for each site; line numbers in the pre-check are locators, not authority:

1. registered-mobile sealed-relay wrapper;
2. registered-mobile nonzero enclosed-type wrapper;
3. registered-mobile plain `MOBILE_SEND` wrapper; and
4. cached target-home send, including its static/home-forward and unregistered-mobile
   reachability.

At each site change only the immediate home-bound plane argument from `AUTO` to `GLOBAL`.
Do not alter the caller's requested plane elsewhere, the target destination, flags,
enclosed type, source/destination hashes, counters, encryption choice, or callback ordering.

Explicitly exclude the typed H-answer arms and parked-send drains identified by the
pre-check: they reply to a query origin or replay a home's forward and are not another
wrapper-to-home site. A grep census of all `send_by_hash`/`do_send` plane arguments must
publish why every apparent sibling is in or out.

## 0d-2 — make the invariant observable at the routing seam

For each of the first three arms, drive a registered mobile whose active home ID is also set
in `_team_peer` and whose static and team routing tables choose different next hops. Assert
the complete AFTER consequence, not just the enum:

- queued `PendingTx.plane == GLOBAL`;
- `flight_is_team_plane(...) == false`;
- the next hop comes from `_rt`, never `_rt_team`;
- the RTS source/address shape is static rather than team-local; and
- the real home receives and unwraps the wrapper through the production receive path.

The no-collision twin must preserve the pre-slice bytes and route. Pin that the registered
mobile cannot newly trip the `enqueue_data` global/no-home refusal and that `stamp_origin`
still uses the home ID with `mobile_src=true`.

For the fourth arm, preserve the B278 static-sender positive case while changing its asserted
authority to explicit GLOBAL. Then drive an unregistered team mobile with the target's
cached home ID colliding with a teammate's local ID and prove it routes to the cached home on
the static table. The case must fail if only the first three arms are fixed.

## 0d-3 — prediction-first corpus attribution

Before changing production behavior, create an isolated throwaway copy of the current tree
and add one sim-only emit at each of the four exact sites. Each record must identify the arm
and report the actual `is_team_peer(home_id)` decision at enqueue time. Run the full 36
streams on that BEFORE binary, save the per-stream counts in evidence, then discard the
instrumented copy and prove the live checkout byte-identical.

The JSON-derived candidate set in the pre-check is a search bound, not the prediction. The
measured nonzero counts are authority. Record separately:

- all 36 streams and the count for each arm;
- the predicted mover set (`count > 0`);
- zero-count mobile-only and team-only controls; and
- the keystone's zero-mobile/zero-count control, reading its current anchor from
  `simulation/BASELINE.md` rather than quoting a remembered value.

After the implementation, rebuild the simulator with a demonstrated recompile/relink and
run the canonical parallel corpus comparator against the current ruled table. Every mover
must have a nonzero BEFORE count and every changed line/field must follow from the plane
correction. Compare event order, deliveries, duplicates, failures, routing and airtime—not
only hashes.

If no stream has a nonzero count, require 36/36 byte identity. If hashes move, write a full
in-tree re-anchor **proposal** with old/new hashes and exact attribution, but do not edit
`simulation/BASELINE.md`; the owner rules after QG. Any delivery or other semantic movement
is separately highlighted for the same ruling.

## 0d-4 — mutation and regression batteries

Route four independent mutations into the existing `b251hash` target, whose configured
source is `lib/core/node_hashlocate.cpp`; do not invent a `radm0d` target for the same file.
Each mutation restores `Plane::AUTO` at exactly one changed arm.
Each anchor matches once and each mutation must compile and turn RED for the arm's own
production-shaped collision case. A single all-arms mutation does not prove site coverage.

Derive both mutation selectors required by the standing brief template:

1. every configured battery whose source file is changed by 0d; and
2. every dependency/historical battery defining this behavior, including at minimum
   `b251hash`, `b161hash`, and `grantpark`, plus the B278-S3 assertion being re-aimed.

Explain differences and run the union in full. A comment-only `node_mac.cpp` edit may be
excluded from a code-decision battery only after a comment-stripped/preprocessed equality
proof; the report may not silently assume comments are irrelevant to target selection.
Re-match every live entry at count one, re-anchor rather than delete moved authorities, and
keep zero GREEN/vacuous/decorative live mutations.

## Corpus, ABI, boards, warnings and probes

Run and report:

1. clean native build and the real test binary, with every case/assertion delta derived;
2. the BEFORE collision census and full AFTER corpus as described above;
3. the full ABI probe, proving `sizeof(Node)` unchanged on host, ARM and Xtensa;
4. deterministic `tools/measure_board.py pair --jobs=2` for exactly `gateway` and
   `heltec_mobile`, attributing all flash movement and requiring RAM/layout invariance;
5. `tools/warning_census.sh` at its own pinned set, with no silent re-pin;
6. both standing wiring probes, both namespace/matrix checkers and the auto-discovered tools
   suite; and
7. `git diff --check`, final process census, and exact tracked/untracked inventory.

The simulator must demonstrably rebuild from the changed `lib/core` source. A stale binary
is a refusal, and the rebuilt `lus` binary hash must differ from the starting binary; an
unchanged hash is a STOP because the behavioral source edit was not represented in the
artifact. This slice owes no metal test: the collision and routing-table choice are fully
host-reachable, while existing Parts 48/54 cover the surrounding real-radio wrapper path.

## Held Author landings

The evidence must contain ready-to-land drafts for:

- `docs/protocol.md`: one concise statement that every wrapper whose immediate destination
  is a mobile home uses the global/static plane by definition; and
- this design's §4/§19 wording changing the R-RA-12 implementation state from required to
  landed, retaining the old AUTO claim through the correction idiom.

If corpus hashes move, also provide the complete re-anchor proposal and register wording for
the owner ruling. Do not draft a new defect row merely because the fourth arm was included;
D-0d-1 deliberately closes the same-class instance in this slice.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-04-radmin-0d.md`

It must contain:

- starting HEAD and exact four-site source census;
- the full in/out sibling-arm table;
- every native case and mutation result with match counts;
- the BEFORE throwaway-instrument method, tree-integrity proof and 36-row count table;
- predicted versus actual corpus movers and complete semantic attribution;
- current anchor/keystone results read from the table, plus any re-anchor proposal;
- ABI, board, warning, probe, checker and tool results;
- the stale-comment correction with old/new text and code-neutral proof;
- every STOP condition evaluated explicitly;
- held documentation drafts;
- the exact line `PIN re-synced? YES — <derivation>`; and
- final `git status --short`, separating pre-existing work and naming every untracked file
  which requires explicit `git add`.

Every figure is derived by the coder. The pre-check's counts, candidate streams, line
numbers and size predictions are hypotheses or cross-checks only. No owner ruling is
expected if the corpus is byte-identical; any re-anchor or delivery/semantic movement is a
formal owner ruling request with the coder's recommendation.
