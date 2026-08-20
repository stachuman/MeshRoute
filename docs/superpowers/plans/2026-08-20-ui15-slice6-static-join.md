<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 6 — static-join adapter, ASYNC outcome + screens · dispatch brief · 2026-08-20

✅ **SETTLED 2026-08-20 — OWNER RULING (reported form): ALL drafted strings APPROVED UNCHANGED, `ADOPTED` included.**
ⓘ Kept below as written for the record; no wording change is owed.
⚠ ~~OPEN OWNER RULING (added at completion, survives the slice):~~ these panel strings were HOUSE-STYLE DRAFTS, not
owner-ruled — **`STORAGE FAILURE`/`CHECK faults`** (io_failed; plan §3 rules no lexeme for the third state) · the
two-row **split** of the ruled `PROFILE STORE INVALID` (21 cols vs the 19-col body, the `PHY DIFFERS` precedent) ·
**`NO JOIN SERVICE`** · **`ADOPTED`** · **`JOIN REFUSED`** · the **`JOIN`** button · the adapter's **`empty slot`**
token · the value-line formats (`L255 SF12 BW500.00` / 4-dp MHz / `node <id>`, drafted to the console's own
precision). Ruled-and-reused, NOT drafts: `NO PROFILES`, `PROFILE 1…4`, `JOINING`/`STILL JOINING`, slice 5's
`SAVE FAILED`/`NOTHING CHANGED`/`press = back`. Each draft is one line in a pure unit with a native case pinning
it; a later ruling is a one-line edit + battery re-pin, ⛔ never a silent rewrite.

⚠ **DECLARED BEHAVIOUR (a consequence stated, not an ambiguity):** a correlated adopt landing after the operator
left `join_waiting` ENDS the session and shows nothing — a push never navigates the panel (nothing in §2.3/§3.6.5
authorises it). Pinned by mutation M87 (a push that moves the operator → RED). If the owner wants a passive
notification instead, that is a new ruling, not a bug.

**Status: DISPATCHED.** ⛔ **NO DEVICE CONTACT.** ⛔ Build on HEAD `7976ee5` ("slice 5") — slices 1-5 are all
committed; revert nothing.
**Normative:** the UI-15 plan §2.2 (the typed join transaction), **§2.3 — ALL EIGHT requirements, this slice's
core**, §3 (the store verbs/matrix), §5 (the four `join_*` arms), §8; design doc §3.6.3; slice table §9 row 6.
⚠ Latest correction wins; re-verify cited `file:line` (V2). ⛔ **Under-specified screen copy or an ambiguity the
plan does not resolve ⇒ STOP and report** (P3).

## As-built you build on (verified 2026-08-20)
- **Slice 1:** `JoinService(ICfgStore&, IJoinLive&)` — `src/firmware_join_service.h:185`, `apply_join:196`,
  `validate_join:150`, `IJoinLive:135` (fake pins already prove zero-live-on-failure / one-after-save / ordering).
  The request carries `double freq_mhz` + `double bw_khz`; ⛔ NaN semantics are a standing instruction — do not
  "fix" them.
- **Slice 2:** the `/mrjoin` store service (`src/firmware_join_profiles.h`) with the FOUR-state read
  (`ok/absent/invalid/io_failed` → `ProfileErr::store_io_failed`). Stored profiles are INTEGRAL (`freq_hz`,
  `bw_hz`); **the adapter performs the one conversion back to the request's MHz/kHz doubles** (plan §3) — U2, one
  conversion path.
- **Slices 4-5:** `Provision::{join_select,join_confirm,join_waiting,join_result}` defined, close-on-leave via
  `provision_reset_on_leave` (all eight arms), JOIN NETWORK activation is a pending NO-OP (`ui15-pending`) —
  **this slice re-lands it**; the confirm default BACK is model state; the probe's child-enabled **`v3` arm**
  ([[B225]]) exists and MUST be extended for the join flow.
- **The push tap EXISTS:** `mr_ui_on_push(const MESHROUTE_NS::Push&)` — `src/firmware_ui.cpp:1686`. `Push` carries
  `kind`, `join_reason`, `origin`, `dst`, `layer_id` (`lib/core/command.h:313`). ⛔ **`lib/core` is UNTOUCHED by
  this slice** — a core change re-runs the corpus and [[B215]] (the armed listen/retry timers) is ITS OWN slice;
  stop and report if slice 6 appears to need one.

## §2.3 — the heart, and the two traps (handover-ranked, both mutation-tested)
1. ⛔ **`join_adopted` fires for the verb, BOOT DAD, *and* the heal re-adopt; `join_refused` carries wire-version
   observations about OTHER peers.** An uncorrelated push completing (or failing) the screen is the *"a success
   that isn't"* class this project has recorded.
2. ⛔ **`layer0_id` is the FULL byte; `leaf_id`/live layer is the NIBBLE.** A full==live comparison is
   unsatisfiable above layer 15 — compare LIKE-FOR-LIKE.

★ **THE FOUR-TERM CORRELATION RULE (plan §2.3.7, verbatim in intent) — a PURE unit, each term mutated separately:**
   1. a UI join session is active;
   2. the cached requested FULL layer == the current persisted `/mrcfg.layer0_id` (persisted↔persisted);
   3. `push.layer_id == requested_layer & 0x0F` (nibble↔nibble);
   4. `push.dst == canonical node id`, and non-zero (id↔id).

**And the rest of §2.3:** success shows **`JOINING`, ⛔ never `JOINED`**; only a CORRELATED `join_adopted`
completes, showing the resulting node id; ★ **NO `join_refused` reason terminally fails v1 — ignore them ALL for
completion**; BACK during `JOINING` only leaves the screen (⛔ no cancel/rollback); **60 s ⇒ `STILL JOINING`, ⛔ NOT
a failure** (retries are unbounded — a deadline would lie); emergency pre-empts.

## Scope
1. Re-land `menu → join_select`; `ui15-pending` retires or narrows accordingly; done-vs-missing statements updated.
2. **`join_select`** per §3/§3.6.3: the four slots (name, or the adopted `PROFILE 1…4` default when empty); store
   states on the panel — absent ⇒ `NO PROFILES`, invalid ⇒ `PROFILE STORE INVALID`, `io_failed` ⇒ its DISTINCT
   storage-failure text (⛔ never collapsed — that distinction is what B218 bought). Selecting a present slot →
   `join_confirm` (BACK default, already model state).
3. **The adapter:** stored profile → `JoinRequest` (integral→double, the ONE conversion) → the slice-1
   `JoinService` through its existing seams. CONFIRM ⇒ exactly one `apply_join`; BACK ⇒ zero (counted). Transaction
   success ⇒ `join_waiting`.
4. **The async outcome:** the correlation rule as a pure, natively-tested unit; wired from `mr_ui_on_push` with the
   device TU forwarding only. Session bookkeeping (what arms/starts a session, what the cached requested layer is,
   when it ends) per plan §2.3 — if the plan under-specifies session lifetime after BACK, STOP and report rather
   than rule.
5. **Screens** (`join_waiting`, `join_result`): renderer thin, decisions/text native-reachable; `STILL JOINING`
   after 60 s on the waiting screen; result shows the resulting node id.
6. **Probe:** extend the **`v3` arm** through the REAL renderer for the whole flow — select (incl. store-state
   arms) → confirm → waiting → a synthesized correlated adopt → result — plus uncorrelated-push and refused-push
   negative arms, with controls per the probe's tempting-wrong-fix discipline.
7. **Strings:** where the design rules no lexeme, follow slice 5's precedent — house-style drafts, one line each in
   a pure unit, each pinned by a native case, **listed in your report for the owner's ruling**.

## Pins
1. The store matrix on the panel — all four states, each its own text, `io_failed` never reading as absent/invalid.
2. BACK at select/confirm/waiting/result ⇒ ZERO transaction/write/apply (counted); CONFIRM ⇒ exactly ONE.
3. ⛔ No `JOINED`-shaped success text reachable before a correlated adopt; transaction success shows `JOINING`.
4. **The four terms mutated SEPARATELY** — each single-term drop accepts an uncorrelated push and goes RED. The
   uncorrelated shapes tested by name: boot-DAD-shaped adopt, heal-re-adopt-shaped adopt, wrong layer nibble,
   wrong/zero dst, no active session.
5. **Every `JoinRefuseReason` arm ignored for completion** — the screen stays `JOINING`; a mutation that fails the
   screen on any refused push goes RED.
6. 60 s ⇒ `STILL JOINING`, not failure — a mutation turning it into a terminal failure goes RED; BACK from waiting
   leaves the screen without touching the persisted operation (zero writes after the save).
7. Close-on-leave + emergency pre-emption hold over the newly reachable arms (the existing eight-arm guard + M66
   pattern extended as needed).
8. Layer > 15: a join requested at a full layer (e.g. 17) correlates via nibble (1) and completes — the trap-2
   regression, tested at a value above 15.
9. Defect-specific mutations RED at match count 1; unchanged positive controls GREEN.

ⓘ ★★ **[[B217]]: re-pin `BASE_CASES`/`BASE_ASSERTS`** (current **1800 / 86200**) when counts move, derivation in
place, confirm every battery RAN. Restore mutation sources exactly; `git diff --check` clean.

## Verification you run (QG runs the full gate — no boards, no corpus)
1. `pio test -e native` + RUN the binary — real counts, 0 failed.
2. Batteries for every target whose source you touch (`model`, `chrome`, `uiprov`, `joinprofiles`, + a new target
   if you add a pure header) — RED counts + proof each ran.
3. `tools/probe_firmware_ui/run.sh` — both arms + controls, full counts.
4. `git diff --check` clean.

## Report
The adapter + correlation unit shape (signatures, files) · the session bookkeeping you implemented and the plan
text it follows · each pin with case name and match count · the four terms' four mutations explicitly · the string
drafts needing a ruling · **the M2 bench lines you owe, DRAFTED in the report** (⛔ do not edit the bench script) ·
final native counts and the new pin · proof each battery ran · both probe arms' counts · exact final
`git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md`, `docs/manual/`, or **anything under `lib/`**. ⛔ Evidence lands IN THE REPO.
⛔ C1: no refactors ride along.
