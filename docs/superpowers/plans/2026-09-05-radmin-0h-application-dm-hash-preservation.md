<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0h — application-DM hash preservation · dispatch brief · 2026-09-05

**Status: QUALITY-AGENT PASS 2026-09-05 — DISPATCHED; SLICE QA-PASSED.** Dispatch model: **Opus**.
Authority: R-RA-25 and its two owner addenda in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`, register rows B296/B297,
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §19 Slice 0h, the Quality-Agent
pre-check `docs/superpowers/plans/2026-09-05-radmin-0h-precheck.md`, and the executable pass-2 reproduction in
`docs/superpowers/plans/2026-09-05-fable-review-pass2.md`.

Dispatch base is exactly **`006f358`** (the first commit after `31fb680`, subject `0g 2`). The coder verifies the
base commit before beginning, including in every worktree, and STOPs if it differs. The pre-check's figures and
line numbers are hypotheses to re-derive on that base, never measurements the coder may quote as its own.

0h is one bounded `lib/core` behaviour correction before 0b/0c and before any remote-admin carrier relies on
R-RA-13. An application DM gets one honest 232-byte body cap and never sheds identity fields to make a larger body
fit. It does not add remote admin, change a wire format, introduce a lookup, alter the hosted-mobile last mile,
change `src/`, or authorize a corpus re-anchor.

## Owner authority quoted verbatim

> **R-RA-25:** “Agree - it means less DM length, but no hidden removal of hash.”
>
> **Owner addenda:** “Agree - narrow reading and refuse.”

The binding interpretation is:

- “derivable” is narrow: only the two lookups `enqueue_data()` already performs, `key_hash_of_id(dst)` and
  `team_key_of_id(dst)`;
- no new lookup, no new override argument and no new plumbing may be introduced;
- the hosted-mobile last-mile call retains its existing destination-hash shape;
- `park_send` and `park_send_layer` refuse an over-cap body; neither clamps it; and
- a genuinely unknown by-ID destination may omit `DST_HASH`, but receives no larger body allowance.

## Required result

0h passes only when all of these hold:

1. `protocol::dm_max_body_bytes` remains the single application-DM body authority and is derived as
   `241 - 1 - 4 - 4 = 232` from named maximum-inner, origin, destination-hash and source-hash terms. The old
   two-byte “conservative prefix” explanation is retained only as corrected history, not live authority.
2. Every `app_dm=true` enqueue sets `DATA_FLAG_SOURCE_HASH` unconditionally.
3. It also sets `DATA_FLAG_DST_HASH` unconditionally when the caller supplies an override or either existing
   destination lookup returns a hash. A genuinely unknown by-ID send may omit it. Field presence is never
   conditional on body size.
4. The plaintext enqueue path checks `pack_unicast_inner()` just as the sealed path checks its pack result. A zero
   result emits the named too-large event, issues `push_send_failed(too_large)` only for a type whose trait owns the
   generic lifecycle, returns the allocated counter, and queues nothing. A zero-length inner is never admitted.
5. Bodies of 232 bytes queue with both hashes wherever the destination hash is supplied/derivable and occupy the
   complete 241-byte inner. Bodies of 233 bytes refuse synchronously at the public command boundary: no counter,
   queue slot, transmission or orphan push.
6. Both park paths reject over-cap input through their existing owning refusal policy and never store a truncated
   body. Preserve their public/private signatures and the authorized header fence. For `park_send`, `false` is the
   existing caller-visible refusal. For `park_send_layer`, the owning `on_command` check remains the synchronous
   refusal and the helper itself must not clamp or store if directly exercised. If truthful propagation requires a
   signature or `node.h` change, STOP rather than widen the slice.
7. A home receiving `MOBILE_SEND` without `DATA_FLAG_SOURCE_HASH` emits one named refusal, frees the carrier, and
   never reaches `record_dm`, `msg_recv` or the ordinary-delivery tail. This is an explicit malformed-wrapper
   refusal; the fail-closed unknown-internal guard and the type-trait table remain unchanged.
8. The TX-time `dlen == 0` bail remains deliberately silent under the B268 ruling. Its stale “unreachable from an
   origination” comment is corrected with the old claim visible; after 0h, native proof makes the corrected claim
   true for every origination path.
9. The 36 corpus streams are byte-identical to the ruled baseline. Any mover is a STOP and no re-anchor proposal is
   produced.
10. `src/device_ble.h` changes by zero bytes. Its expressions re-derive the shorter canonical `send` line while
    the `send_layer` term remains binding for the 275-byte BLE storage.
11. `sizeof(Node)`, both board RAM totals, `.bss` and `.data` are unchanged. Flash movement is attributable only to
    the authorized core decisions.

## Fence

Allowed production edits are exactly:

- `lib/core/protocol_constants.h` — named 232-byte application-DM cap derivation;
- `lib/core/node_mac.cpp` — unconditional application-DM identity fields, plaintext pack refusal, and B297 comment;
- `lib/core/node_hashlocate.cpp` — replace the two body clamps with refusal/no-store behavior; and
- `lib/core/node_mac_rx.cpp` — loud home refusal for a malformed mobile wrapper without source hash.

Allowed supporting edits are the directly affected/new native tests, the existing mutation harness, and
`docs/superpowers/evidence/2026-09-05-radmin-0h.md`. The coder may use throwaway corpus instrumentation in an
isolated copy, but it must not remain in the final diff.

No `src/`, `lib/core/frame_codec.h`, `lib/core/frame_codec.cpp`, `node.h`, `lib/console/`, `lib/hal/`,
`simulation/`, `simulation/BASELINE.md`, `variants/`, `platformio.ini`, companion, manual, protocol/frame document,
register, spec, bench, tracker or memory file belongs to the coder. The Author owns the held documentation after
QG PASS. The only ruled board environments are `gateway` and `heltec_mobile`; the warning census's own pinned set
is the standing exception.

## STOP conditions

STOP and report before widening the slice if any of the following occurs:

1. the dispatch base differs from `006f358`;
2. any corpus mover;
3. any need for a new lookup or override to satisfy the ruling;
4. a refusal that requires an orphan failure push for a non-lifecycle type;
5. the home refusal requiring a change to the fail-closed guard or the type traits;
6. any `src/` or wire-format edit;
7. RAM movement;
8. a control that stays green;
9. an edit outside the fence, including `node.h`, `frame_codec.*` or `simulation/BASELINE.md`;
10. a park refusal cannot be made honest without changing a public/private signature;
11. the 232-byte positive cannot carry every identity field available from the two existing lookups;
12. a pack failure leaves a queue slot, transmission, zero-inner item or lifecycle event inconsistent with the
    sealed sibling;
13. the throwaway corpus census cannot distinguish a live instrument from a vacuous zero;
14. a touched-source or dependency mutation is GREEN, vacuous, multi-matched or unusable and cannot be repaired
    within the authorized tests/harness; or
15. an unexplained flash symbol, warning-census movement, ABI movement, probe regression or stale PIN remains.

## 0h-0 — establish the base and prediction before editing

Before any production edit:

1. verify `git rev-parse HEAD` is `006f358`, record the clean/expected starting status, and read every target line
   in current context;
2. run the real native binary and derive its case/assertion/failure totals and current mutation PIN;
3. rebuild the simulator, record the action count and binary hash, then reproduce all 36 ruled anchors and the s18
   keystone;
4. derive the corpus DM body-length histogram and prove whether any admitted application body is at or above 233;
5. in an isolated source copy, add a temporary event at the actual flag decisions and run all 36 streams to count
   `SOURCE_HASH`-dropped-for-size and `DST_HASH`-dropped-for-size decisions before the correction;
6. prove that temporary census is alive by also reporting its total application-DM decision population and at
   least the supplied/derived/no-destination-hash classes; a zero dropped count with zero observed decisions is a
   refusal, not evidence; and
7. remove the instrumentation and prove the working tree returned byte-for-byte to the starting source before
   implementing 0h.

Prediction must be written before the AFTER run: all 36 streams byte-identical, because no current body crosses
the new cap and no current field is being dropped for size. Corpus events do not expose DATA flags, so event-log
identity alone is not the BEFORE proof.

## 0h-1 — derive the one application-DM cap

Replace the stale prefix allowance in `protocol_constants.h` with named constexpr terms for:

- maximum packed unicast inner bytes;
- one origin byte;
- one four-byte destination hash; and
- one four-byte source hash.

Derive `dm_max_body_bytes` from those terms and statically assert the arithmetic, including the full-inner equality
at 232. Keep the existing public name so all consumers follow without a copied literal. Do not move frame sizing
authority out of its landed codec functions, alter `data_inner_cap()`, or create B289's future shared overhead API
inside this slice.

Re-aim the 0e characterization assertion whose prior “no cap equals the DM cap” premise becomes false by value
coincidence at 232. Preserve its real claim—carrier caps are derived from their packers, not copied from an
unrelated DM literal—using an executed cap/cap-plus-one packing check or another direct derivation. Do not retain a
false inequality merely to keep the old test green.

## 0h-2 — make identity fields mandatory when known

In `enqueue_data()` preserve the current lookup set and order. For `app_dm=true`:

- include origin and set `SOURCE_HASH` without a size guard;
- use the supplied destination override when nonzero;
- otherwise use only the result already obtained from `key_hash_of_id(dst)` / `team_key_of_id(dst)`;
- set `DST_HASH` whenever that result is nonzero, without a size guard; and
- allow it absent only when those existing inputs genuinely yield no hash.

Do not infer a hash from another in-scope object, re-query another table, pass the hosted mobile's key hash into
the last-mile enqueue, or add an override parameter. The narrow “already derived” definition is the product
boundary.

Check the plaintext pack result. On zero, mirror the sealed too-large ownership exactly: named emit, conditional
generic lifecycle failure push, returned counter, no admission and no pump of a nonexistent item. Keep adjacent
non-application and sealed behavior byte-identical.

## 0h-3 — reject, never clamp, in parked paths

At both park helpers, test the original body length before copying. Over-cap input never allocates or refreshes a
park slot and never stores a prefix. Route the result through the existing owner-visible refusal behavior without
adding a new PushKind, event family, return channel or signature.

Native coverage must exercise both helpers at cap and cap-plus-one, including a direct/friend reach of the void
layer helper so its defensive no-store property is measured independently of `on_command`. Reverting either clamp
must make its own control RED.

## 0h-4 — refuse a malformed mobile wrapper at the home

Place the source-hash requirement at the existing `MOBILE_SEND` home-consumer boundary, before the delegation fork
can be skipped and before ordinary delivery. It must not run on a transit role or alter hosted last-mile behavior.
Use one named, scalar-only diagnostic and `become_free()`, with no body telemetry and no synthetic lifecycle push.

The production-shaped native case starts from the existing dual-layer post-ACK mobile-wrapper drive, removes only
`DATA_FLAG_SOURCE_HASH`, and proves the BEFORE defect then the AFTER contract: exactly one refusal, carrier freed,
zero home inbox record, zero `msg_recv`, zero outward enqueue, zero ordinary delivery. A valid sibling with both
hash flags must still delegate.

## 0h-5 — boundary tests and corrections

Turn the pass-2 scratch reproduction into durable native cases; do not test a lookalike packer.

For a static sender with an authoritative bound target and for a registered mobile sending through an unresolved
target/home wrapper, prove independently:

- 232 bytes: queued, both available hash flags present (`0x06` in the measured shapes), complete 241-byte inner,
  unchanged body, one queue item and an actual transmissible frame;
- 233 bytes: synchronous `err_too_large`, no returned counter, no new queue slot, no RTS/DATA and no failure push;
  and
- an unknown by-ID destination at 232 may omit only `DST_HASH`; it still carries `SOURCE_HASH` and gains no extra
  byte of body capacity.

Move existing boundary tests with the symbol and strengthen them rather than replacing them:

- `test/test_node_hashlocate.cpp` parked/hash-locate cap and cap-plus-one cases;
- `test/test_node_r3.cpp` B20/B21 plaintext sweep, now asserting flags and packed inner content, not merely “airs”;
- `test/test_dual_layer.cpp` cap relation and malformed-wrapper receive case;
- `test/test_console_parse.cpp` symbolic 0f boundary cases; and
- `test/test_radmin_characterization_0e.cpp` packing-derived authority case.

At minimum the isolated mutation controls must turn RED when each of these defects is restored separately:

1. `SOURCE_HASH` becomes optional-for-size;
2. supplied/derived `DST_HASH` becomes optional-for-size;
3. the plaintext pack-result check is removed;
4. the home malformed-wrapper refusal is removed;
5. `park_send` restores its clamp; and
6. `park_send_layer` restores its clamp.

Each mutation is exact-match count one, leaves the real checkout unchanged, and attacks the named behavior rather
than a compile accident. Any pre-existing entry made vacuous by the edit is re-aimed with the correction idiom and
the old decision kept visible.

## Mutation routing and gate

Use the harness's one-source-file-per-target ownership. Run one complete `--workers=2` pass before reporting. The
pre-check's explicit historical/dependency set is:

- `teamgrant`, `b159map` (`node.cpp`);
- `grantadmit`, `b161mac`, `b20mac`, `b159mac` (`node_mac.cpp`);
- `grantpark`, `b161hash`, `b251hash` (`node_hashlocate.cpp`); and
- `b159const` (`protocol_constants.h`).

Because 0h also changes `node_mac_rx.cpp`, the roles document's changed-source selector is separate and cannot be
silently omitted: derive and run every live target mapped by `TARGET_SRC` to that file (`b161rx`, `b251rx`,
`b159rx`, `a0rx`, `sliceBrx`, `sliceGrx`) unless the Quality Agent narrows that mechanically derived set before
dispatch. Put the new home-refusal mutation in the existing receiver battery whose scope owns the MOBILE_SEND
consumer; do not invent a second parser or cross-file target. `b20codec` is not run merely because it is related:
touching `frame_codec.h` is a STOP. Report changed-source targets and historical/dependency targets as two columns,
then their union, per the roles document.

## Final verification

The coder runs and reports, deriving every figure on the final tree:

1. the real native binary, filtered new cases, and the PIN derivation with the exact report line
   `PIN re-synced? YES — <derivation>`;
2. a clean simulator relink/rebuild with action count and binary hash, followed by
   `tools/run_corpus.py --jobs=8 --require-anchors` and an ordered byte comparison proving 36/36 unchanged;
3. both ABI instruments, including every negative control, with `sizeof(Node)` unchanged on host, ARM and Xtensa;
4. `measure_board.py` pair for exactly `gateway` and `heltec_mobile` with `--jobs=2`, repeated as required by the
   instrument; RAM/sections unchanged and every flash symbol attributed to authorized core objects;
5. `tools/warning_census.sh` on its pinned environment set, including selftest and zero new warnings;
6. the four standing feature/wiring probes—`tools/probe_console_sink/run.sh`,
   `tools/probe_firmware_ui/run.sh`, `tools/probe_inbox_verbs/run.sh` and
   `tools/probe_custody_usb/run.sh`—plus `tools/probe_ble_line/run.sh`; the BLE probe must derive the shorter
   canonical `send` line from the changed constant and independently prove storage remains 275, without a
   `src/device_ble.h` edit;
7. both standing DATA-type/matrix checkers with their selftests;
8. the complete mutation union above, one full two-worker pass, zero unusable/vacuous/multi-match entries;
9. the tools auto-discovery sweep; and
10. `git diff --check`, the exact fence diff, zero `src/` diff, zero wire-format diff, and zero
    `simulation/BASELINE.md` diff.

The evidence file includes the prediction before results; BEFORE and AFTER corpus hashes for all 36 rows; the
temporary flag-census construction, liveness control, results and removal proof; the 232/233 matrix; home-refusal
event trace; mutation target/result table; ABI/board/stack/warning attribution; complete file inventory including
untracked files; explicit STOP audit; and `PIN re-synced?` line. Nothing durable lives only in scratch.

## Held Author landings after QG PASS

The coder drafts exact text in the evidence file; the Author lands it only after PASS:

- B296 and B297 to CLOSED with measured evidence;
- R-RA-24′ bound 2's 272-byte canonical `send` figure marked historical, with the re-derived figure recorded;
- Part 61's stale send-line step corrected to the measured post-0h value;
- **Bench Part 62**: USB proves a 233-byte DM refusal before airtime, then a registered mobile sends a 232-byte
  delegated DM end to end with the home/target identities checked;
- the remote-admin design §19 Slice-0h completion row; and
- the manual's application-DM body-size statement.

No corpus re-anchor, protocol/frame document, companion change or additional bench part is owed. If an untracked
test, tool or evidence file is created, list it explicitly for `git add`; never rely on `git commit -a`.
