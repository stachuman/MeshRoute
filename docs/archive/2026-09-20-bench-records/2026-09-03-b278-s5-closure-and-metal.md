<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B278 S5 — consolidated closure gate and metal Part 54 · dispatch brief · 2026-09-03

**Status: QUALITY-AGENT PASS 2026-09-03 — AUTHORIZED FOR DISPATCH.**
Dispatch model: **Opus**. Authority:
`docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md`, especially
§12-S5 and §§13–16. Pre-check input:
`docs/superpowers/plans/2026-09-03-b278-s5-precheck.md`. S4 evidence:
`docs/superpowers/evidence/2026-09-02-b278-s4.md`.

⛔ **START CONDITION:** the seven Author S4 landings which followed commit `da7cbb6` are
committed at `08a1e1c`. The coder records that commit or a later clean authoritative HEAD and
requires `git status --short` empty. The S5 BEFORE is one commit, never an unexplained
working-tree overlay.

S5 adds no product behavior and no test. It is the consolidated B278 gate on one committed
tree, one comment-only correction, and four held Author documentation drafts: the two design
updates, bench Part 54, and status/tracker closure text. It leaves B278
**SOFTWARE-COMPLETE / METAL-PENDING** after QG. Only the Owner's successful Part 54 run closes
B278 fully.

## Contract quoted verbatim from the reviewed spec

> ### S5 — full gates, docs and metal
>
> Run all gates below, propose any corpus re-anchor in-tree, and add bench Part 54. B278 becomes
> software-complete only after QG; it closes fully after Part 54.

Closure criteria, also verbatim:

> B278 is closed only when:
>
> 1. the reviewed rulings in §1.1 are accepted or replaced explicitly;
> 2. one correlation authority covers ACK and custody without counter-only matching;
> 3. direct custody behavior remains compatible;
> 4. translated records use the existing mobile routing/last-mile authority;
> 5. ACK-before/after monotonic behavior is proven;
> 6. no-map, ambiguity, full-ring, expiry and re-home cases fail safely;
> 7. protocol and frame documents match the implemented wire bytes;
> 8. native, mutation, corpus, board, ABI, warning and wiring gates pass; and
> 9. bench Part 54 passes on real radios.

## OWNER RULINGS R-S5-1 through R-S5-4 — LANDED 2026-09-03

Spec §15 step 8 asks the four-radio line topology to produce custody first and then a valid
late E2E ACK. Source inspection proves that sequence is structurally unreachable there: R
emits the notice only at its cascade terminal, then destroys its last copy; with T removed,
no later delivery exists from which T could generate the ACK. Producing the sequence on metal
requires a second independently successful path and therefore at least a fifth radio plus
timing control.

**Ruled:** retain the existing S1b/S3 production-shaped host tests as the normative
ACK-after-custody proof, and make Part 54 step 8 a **conditional observation**: if an
independent copy yields a late valid ACK, it must upgrade the correlated live operation
without a second translation or downgrade; absence of that optional second path is not a
metal failure. Do not force a five-radio topology merely to reproduce a monotonic state rule
already fully host-reachable.

**R-S5-2:** use the feasible reboot form for the no-map arm in §15 step 9. Reboot H1
after R's hop ACK but before R's terminal report. The 300 s expiry form is not physically
reachable because R's terminal occurs within the 60 s cascade lifetime. Re-register M1 after
that control if H1's volatile roster was lost before any later optional arm.

**R-S5-3:** go straight to metal. S5 adds no host twin and no native case; the production-path
premise is the reviewed code trace plus Part 54 itself. If Part 54 fails at the relay's custody
eligibility boundary, a host twin becomes a new reproduction task rather than hidden S5 scope.

**R-S5-4:** B283 runs independently and may be dispatched in parallel. Its tool-only fence
does not widen S5 and its result is not imported into S5's six-environment census baseline.

## Required result

S5 passes only when:

- every B278 mutation, corpus, ABI, board, warning, wiring and checker instrument passes on
  one final tree;
- the remaining live-stale comment is corrected with its old claim visible;
- the four outstanding §14 documentation landings are complete drafts in the evidence;
- Part 54 is a command-level, falsifiable metal script reflecting R-S5-1 through R-S5-3; and
- the coder's diff contains no production change and no anchor-table edit.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the starting tree is dirty, the seven S4 Author landings are not committed, or S5 cannot
   identify one authoritative starting HEAD;
2. any corpus stream differs from the seventh owner-ruled table after a proven relink, any
   translated event unexpectedly gains corpus reach, or a BASELINE re-anchor is proposed;
3. any arc mutation target fails to run in full, has a non-RED/unusable/vacuous/multi-match
   live entry, or native does not reproduce the unchanged PIN;
4. `Node`, `DelegAck`, `DelegCustodyAction`, ruled-board RAM/payload, warning pins or any
   production symbol differs from the committed S4 state;
5. a wiring probe, ABI probe, checker, tools suite, warning census or `git diff --check` fails;
6. the stale-comment correction changes code/preprocessed output or removes the historical
    claim instead of correcting it in place;
7. the internal-custody or remote-admin draft rewrites historical v1 scope, claims B112 is
    closed, broadens custody eligibility, or treats the report as authentication/failure/
    permission to retry;
8. Part 54 lacks exact commands, counters, trace-order evidence, persistence, no-map or
    restoration steps, or claims a four-node line produced the structurally impossible late
    ACK sequence;
9. an unrelated B280/B281/B282/B283/B112 fix enters the slice; or
10. any new owner ruling beyond R-S5-1 through R-S5-4 is required.

## Fence

Allowed implementation files:

- `test/test_custody_relay_f.cpp`—comment-only correction of the stale S2 interim-state note;
- `docs/superpowers/evidence/2026-09-03-b278-s5.md`.

No `lib/`, `src/`, other `tools/`, simulator scenario, `simulation/BASELINE.md`, wire doc,
bench script, design spec, register or `tracker.md` edit belongs to the coder. The coder
places exact proposed documentation in the evidence; the Author lands it after QG PASS.

The only board environments are `gateway` and `heltec_mobile`; the warning census's pinned
set is the standing exception. B283's owner-approved census widening is a separate instrument
slice and may run in parallel in another clean worktree, but it is not imported into S5.

## S5-1 — stale-comment closure

Correct the live-stale banner near `test/test_custody_relay_f.cpp:565` in place. Keep the old
claim visible and name its replacement truth:

- S2 created the form in an intermediate state;
- S3 now produces it at the translating home;
- S4 now consumes it at the configured mobile;
- the cases in this pure-codec section prove bytes/codec behavior, not receiver refusal; and
- the static-receiver refusal lives in the S4 receiver cases.

The edit is comment-only. Prove the test TU's preprocessed output hash unchanged and preserve
its line count where practical; if line count moves, apply the B254 disclosure rather than
claiming byte identity without measurement.

Run the stale-language sweep from the pre-check over `lib src test tools docs/protocol.md
docs/frames.md`, classify every remaining hit, and require zero live-stale claims outside an
adjacent correction/historical marker.

## S5-2 — consolidated arc gate on one tree

Derive the mutation target set mechanically as:

```text
TARGETS map ∩ production files changed by B278 from the S0 parent through the S4 commit
```

The expected set must be checked, not blindly copied: `b251hash`, `b251rx`, `sliceGrx`,
`sliceGjson`, `sliceFtypes`, `sliceGtypes`, `sliceFcodec`, `sliceGcodec`, `sliceGinbox`,
`sliceCpull`, and `sliceCinbox`. Run every live entry in every selected target at match count
one; all must be RED with zero unusable/vacuous entries. A discrepancy between the derived
set and this list is a finding, not permission to omit a target.

Produce one arc-level table with at least:

| gate | S0/S1/S2/S3/S4 last accepted state | S5 result | verdict |
| --- | --- | --- | --- |
| native and PIN | 2578 / 108904 / 0 | reproduce from the binary; PIN unchanged | |
| all B278 mutation targets | per-target entries/RED/unusable | fresh full runs | |
| corpus | seventh ruled table | fresh 36/36 | |
| correlation census | delegated/custody intersection and lifecycle | fresh result | |
| ABI + B278 row probes | Node/row/action on three ABIs | fresh full sweeps | |
| ruled board pair | S4 manifests | fresh sequentially comparable pair | |
| warning census | its six pinned envs | fresh census and `-Wswitch` | |
| firmware UI / inbox verbs / custody USB | last pins | fresh default gates | |
| tools discovery | all auto-discovered tests | fresh run | |
| DataType/A0 checkers | last contracts | fresh runs | |
| stale-language and source/doc searches | classified inventory | fresh result | |
| `git diff --check` and untracked inventory | clean/complete | fresh result | |

Long gates may run as detached jobs, but every result must be collected against the same
source snapshot and no partial run may be reported as complete.

### Corpus

Rebuild `../lora-universal-simulator/build/orchestrator/lus` after the final test/comment edit
and prove freshness; then run:

```text
tools/run_corpus.py --jobs=8 --require-anchors --out <fresh ignored directory>
```

Require 36/36 exact against the seventh owner-ruled table, s18 exact, all four
`deleg_custody_*` events zero, `custody_failure_reject` zero, and the eleven direct receipts
and Push events unchanged. S5 has no simulator/production edit; any movement is STOP 2. No
re-anchor proposal is permitted.

### ABI, boards, warnings and wiring

- Run `tools/probe_board_abi.py` and `tools/probe_b278_row_abi.py` in full, including controls.
- Run `tools/measure_board.py pair --output <fresh ignored directory> --jobs=2` for only
  `gateway` and `heltec_mobile`; compare all qualified fields and payloads to committed S4.
- Run `tools/warning_census.sh` on its own pinned six-env set and require zero `-Wswitch`.
- Run `tools/probe_firmware_ui/run.sh`, `tools/probe_inbox_verbs/run.sh`, and
  `tools/probe_custody_usb/run.sh` with default negative controls.
- Run `python3 -m unittest discover -s tools -p "test_*.py"`, both DataType/A0 checkers and
  `git diff --check`.

S5 adds no native case. Reproduce **2578 / 108904 / 0** from the binary, require the
non-disarming `PIN_*` cross-check to stay unchanged, and include the exact report line
`PIN unchanged? YES — 2578 / 108904 / 0 reproduced`.

## S5-3 — documentation drafts held for Author landing

The evidence file must include complete proposed text for these Author-owned landings. The
coder edits none of them.

### A. Internal DATA/custody design extension

Add a dated B278 addendum to
`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` without
rewriting its v1 history. It must state:

- v1 intentionally excluded mobile delegation/hosted last mile;
- B278 reuses the existing eight-row E2E correlation only for delegated `-a` flights;
- a static relay's direct report remains at H1, while H1 appends the eight-byte tail and
  returns the same `0x81` type through `send_by_hash`;
- complete identity is `{failed_origin, reporter_layer, target_kind, target_value,
  mobile_ctr, failed_type}`, never counter-only or the record key;
- the configured mobile validates stable-hash or active-home context, persists before Push,
  and keeps the report diagnostic/unauthenticated/non-retrying; and
- team/cross-layer/last-mile custody generation remains outside scope.

### B. Remote-admin RPC prerequisite update

Correct every active future-tense B278 claim in
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` with the historical
statement visible. The landed contract is:

- an RPC request sent from a mobile through its home requests the existing E2E ACK;
- a static-leg custody report returns as `custody_failure` with ctrH retained and ctrM in
  `mobile_ctr`;
- a remote-admin consumer matches the complete six-field body tuple;
- the report cannot become an RPC response/auth failure or prove non-execution;
- it may support the separately designed UNCERTAIN/retry policy only under that design's
  exact-byte/idempotence rules; B278 itself never retries; and
- **B112 remains open and separate**; B278 must not be described as resolving first-hop
  admission truthfulness.

### C. Bench Part 54

Draft a concise, executable Part 54 for `docs/2026-07-31-bench-test-script.md` using the
pre-check's source-derived commands and exact surfaces. It must include:

1. four-radio topology M1—H1—R—T; H1→T forced through R;
2. record initial `whoami`, `mobile status`, inbox cursors/counts and prior `e2e_dm` /
   `intro_attach` values;
3. set plaintext/no-intro policy and run the positive `send T_ID "part54-control-<tag>" -a`;
   require queued ctrM equals `E2E-ACKED` ctr;
4. power T off, send a fresh tagged `-a`, and record ctrM;
5. require trace order at R: receive H1 DATA → send hop ACK → onward terminal → custody
   notice; without that order the run is invalid;
6. on H1, require the direct USB line and raw-pull record under R/ctrH, with no delegated
   fields; use trace, not USB, to witness H1's silent forward to M1;
7. on M1, require exactly one translated line/record under R/ctrM, H1/ctrH still inside,
   exactly one target field, no aliases, no ordinary OLED row/unread increment;
8. power-cycle M1 and prove the same raw record/sequence survives;
9. under R-S5-1, cite the host-authoritative ACK-order proof and make late ACK on metal
   conditional unless a genuine independent path exists; if observed, require upgrade with
   no second translation/downgrade;
10. no-map: repeat a fresh send, reboot H1 after R's hop ACK but before R's report, require
    H1 retains the direct diagnostic and M1 receives no translation; re-register M1 later if
    the reboot lost its roster;
11. optional fifth-node H2 re-home/hash arm; and
12. restore configuration, power and RF topology.

State explicitly that USB/OLED and four-radio timing are the irreducible metal residue;
JSON compatibility remains host-proven and is not re-tested manually.

### D. Status and tracker

Draft:

- B278 register/spec status as **SOFTWARE-COMPLETE / METAL-PENDING — Part 54** after S5 QG;
- a final close sentence reserved for the Owner's Part 54 PASS;
- `tracker.md` refreshed to the current date, recording B278's mobile/static custody
  extension separately from the completed v1 A0–G arc and keeping optional Slice H parked;
  and
- the next queue: Part 54 metal, then remote-admin preparation; B280/B281/B282/B283/B112
  remain independently tracked.

`docs/frames.md` and `docs/protocol.md` require no S5 edit: S2–S4 already landed their final
wire and behavior text. Verify that claim by search and quote the anchors in evidence.

## Required report shape

The evidence file must report:

- clean committed starting HEAD and exact S5 diff;
- the arc-level consolidated gate table with every figure derived during S5;
- complete mutation target derivation and per-target RED/unusable/match counts;
- simulator freshness and exact 36/36 corpus/census results;
- ABI, board manifest/payload, warnings, wiring probes, tools and checker results;
- stale-language sweep plus preprocessed/line-count proof for the comment correction;
- exact held drafts for the two specs, Part 54 and tracker/register closure;
- explicit no-production/no-wire/no-RAM/no-reanchor result;
- `PIN unchanged? YES — 2578 / 108904 / 0 reproduced`;
- `git diff --check`, staged/untracked inventory, and files requiring explicit add; and
- PASS/HOLD with R-S5-1 through R-S5-4 recorded exactly and every STOP disposition enumerated.

No S5 software PASS may be described as full B278 closure. The only final close authority is
the Owner's successful Part 54 metal record.
