<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §T3 — `send_aired`: the app/UI half of the TX-completion arc · IMPLEMENTATION SPEC · 2026-08-14

**Status: ⚙ IMPLEMENTED, UNCOMMITTED, QG HOLD PENDING CORRECTIONS (2026-08-14).** The firmware described below exists in the working tree and is gated (native, `s18` exact, 36/36 corpus, six board envs, both probes, the warning census). ⛔ **NOT COMMITTED (D4)** and ⛔ **NO QG APPROVAL IS CLAIMED — QG has not given one.** The OWNER approved this SPEC; that is a different statement from QG clearing the implementation, and the two must not be conflated.
⛔⛔ **WITHDRAWN IN PLACE, KEPT VISIBLE (§3 rule 3): this line previously read *"Status: SPEC — REVISED after a QG HOLD (round 2, 2026-08-14). ⛔ NO FIRMWARE UNTIL QG CLEARS THIS DOCUMENT."* That sentence was written by the QA-gate and is now FALSE — the firmware was subsequently written under an owner-approved spec.** ⚠ It is corrected here rather than deleted because it is exactly the class of stale line a reader ACTS on: for five consecutive rounds a status correction has reached this arc's prose and missed the instruction a reader follows.
★ Role split: the QA-gate wrote this spec; **QG is run by an external agent**; the **OWNER rules and commits**.
⚠ QG's findings are a recommendation relayed by the owner, not an owner ruling (ledger §3 rule 5). **All five
round-1 blockers were re-verified at the code before being accepted here**; withdrawn wording is kept visible
(§3 rule 3) and indexed in §8.

**Normative parent:** `docs/superpowers/specs/2026-08-13-tx-completion-path-design.md` — **§0, §4.4, §4.5, §7.2,
§7.3, §9**. This spec does **not** restate the design.

**Tree state (RE-MEASURED 2026-08-14 after §T2's refinement):** HEAD **`aec5e2a`**; **§T2 UNCOMMITTED** and this slice
builds on it. Native **1623 / 82523 / 0** · `lus` **`0c72d355`** · `s18` **`9868cad3` / 269905** · `sizeof(Node)`
**221880** · `kCap` **91**, all consumed · census **174 / 178 / 178**.
⛔ **CORRECTED (§8-a):** round 1 published **82513** and `lus` **`77555889`**. Both were measured before §T2's
`BusyReason::none` refinement landed and are **stale, not disputed** — QG's figures are the current ones and I
reproduced both.

---
## 0 — Scope

**T3 = the app/UI semantics.** Six parts:
1. **`PushKind::send_aired`**, appended, **with its four contract-visible seams** (§2.5);
2. **the core push rule** — the two ownership paths, ★ **with the `flood` discriminator of §2.2**;
3. ★★ **the loop-order fix of §2.1** — without it the channel case cannot work at all;
4. **the scoped monotonic rank** (§2.3);
5. **the UI half** — one explicit arm, two new states, three changed strings, ★ **plus the tracker lifecycle of §2.4**;
6. **the documentation obligations** (§6).

⛔ **OUT:** any `failed`/`unknown` UI surface · [[B186b]] recovery · a simulator-produced `aired` · the channel digest
boundary · any `Node` growth · any wire, `wire_version` or NV change.

---
## 1 — Every seam, RE-VERIFIED at the current tree (V1)

| seam | verified location | note |
|---|---|---|
| `PushKind` last enumerator | `lib/core/command.h:233` `team_channel_no_key` | **append after it** |
| `Push` layout tripwires | `command.h:349` / `:352` `sizeof(Push) == 292` | ⛔ must not move |
| JSON name switch | `lib/console/console_json.cpp:127` `pushkind_name`, **no `default:`** | `-Werror=switch` hard-fails until named |
| ★ JSON **body** branch | `console_json.cpp:389` — the final `else { // send_acked / send_failed }` | ⛔ **§2.5(b): `send_aired` would land here BY FALLBACK** |
| ★ console text switch | `src/fw_main.cpp:1170` `switch (pu.kind)`, **no `default:`** | its own comment records this is the **ONLY** detector for a new kind (fw_main is outside native) |
| ★ exhaustive name walker | `test/test_console_json.cpp:941` | must gain the new kind |
| `ui_route_send_push` fallthrough | `src/firmware_ui_send.h:315` `default: return false;` | ⇒ a new kind is **silently ignored** |
| push dispatch | `src/firmware_ui.cpp:1041` | its `default:` routes to `ui_route_send_push` |
| `SendTracker` | `src/firmware_ui_send.h:59` | the correlation authority |
| state enums | `firmware_ui_model.h:512` `DmState` · `:524` `ChanState` | |
| ★ the late-ack upgrade | `firmware_ui_model.h:1105` `case K::dm_acked` — *"incl. the LATE-ack upgrade"* | ⛔ **§2.3's counterexample** |
| render sites | `firmware_ui.cpp:750`, `:764`, `:768` | the three changed strings |
| channel ownership record | `lib/core/node.h:1867-1880` + its `static_assert` | ⛔ read, never grow |
| the id decode | `lib/core/node_channel.cpp:1016` `Node::m_inner_id` | ⛔ do not hand-roll (U1) |

### 1.1 ★ THE SIMULATOR BRIDGE IS SAFE — appending does not break the sim build
`ConsoleNames.cpp` does **not** switch on `PushKind`; `pushKindName(uint8_t)` forwards to this repo's
`pushkind_name`. Its only tripwires are `sizeof(meshroute::PushKind) == 1` and
`static_cast<uint8_t>(join_adopted) == 13`, twinned in `NodeRuntimeWrapper.cpp`. ⇒ **an APPENDED enumerator
satisfies both**; ⛔ an **inserted** one breaks both repos.

### 1.2 ⛔⛔ THE STRING SWEEP IS **SEVEN** GROUPS
⛔ **CORRECTED (§8-b):** round 1 said *"six documented sites"* and then enumerated seven. The list was right; the
count was wrong. **Four in the OLED guide** (`docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md` **:534-535**,
**:542-543**, **:561**, **:609**) **and three in the bench script** (`docs/2026-07-31-bench-test-script.md`
**:855**, **:876**, **:912-921**). ⚠ **`:609` and `:876` are PROHIBITION clauses** naming the old string
(*"it must NEVER read …"*) — rewriting them carelessly **inverts a safety check**. Update the string, **keep the
prohibition's meaning**. ⚠ `:542-543` additionally carries the §B38 note that `SENT, no relay` is **correct** at
100 % delivery on a 1-hop pair — that argument survives the rename and must move with it.

---
## 2 — What to build

### 2.1 ★★★ BLOCKER 1 — THE LOOP ORDER. Fix this FIRST; without it the channel case cannot work.

**Verified:** `src/fw_main.cpp` runs **step 2 = timers** (`for (int id; (id = g_hal.pop_due_timer()) >= 0; )`,
~`:1068`) **BEFORE step 2b = `service_tx()` + the outcome drain** (~`:1091`). `kMBcastClearTimerId`
(`lib/core/node.cpp:1317-1318`) does `if (_pending_tx && _pending_tx->m_broadcast) { _pending_tx.reset();
become_free(); }`, and it is armed at `data_air + 5` (`lib/core/node_mac.cpp:2010`) — **5 ms after CALCULATED
airtime**.

⇒ **if a loop pass is delayed past both deadlines, the M-clear timer fires FIRST, `_pending_tx` is gone before TxDone
is drained, and the channel post NEVER produces `send_aired`.** ★★ §2.2's rule requires a live `_pending_tx`, so
this is not a corner case — **it is the ordinary failure mode of the feature on a loaded node.**

⛔ **DO NOT "fix" this by widening the 5 ms margin.** That trades one timing race for a wider one and leaves the
ordering wrong.

**The fix: split completion COLLECTION from starting the next TX.**
- **Collect/drain the TxDone completion BEFORE the timer loop**;
- **retain normal TX pumping AFTER the timers** (unchanged, so a frame enqueued by a timer still departs this pass).

⚠ **This changes `DeviceHal`'s public shape** (`service_tx()` currently does both — `poll_tx_done()` handling **and**
`pump_tx()`). Keep **one** authority for each half; ⛔ do not duplicate the completion logic into two callers (U1).
★★ **An ACTUAL-ORDER REGRESSION TEST IS REQUIRED** (§4, W-check + native): a test that only proves "the push happens"
passes with the old order whenever the loop is fast. **The test must construct the delayed pass** — timers due **and**
a TxDone pending in the same iteration — and assert the push still arrives.

### 2.2 ⛔⛔ BLOCKER 2 — the channel ownership predicate. **The round-1 predicate contradicted N14d.**

⛔ **WITHDRAWN ROUND-1 PREDICATE (§8-c):** round 1 wrote *"decode the id from `pt.inner`, then find the active
`_channel_reoffer_pending` entry with `entry.id == id` and `entry.holder == false`"* — **and nothing else**.

★★ **Why that is wrong, verified at the code: a channel PULL RESPONSE carries the SAME message id.**
`enqueue_channel_m` (`lib/core/node_channel.cpp:1038-1044`) sets `item.is_channel_m = true` and writes the **same
4-byte BE id** into `inner[0..3]` — but it does **NOT** set `item.flood`. `item.flood = true` is set **only** by the
flood path (`node_channel.cpp:1588`), and `pt.flood = item.flood` carries it (`node_mac.cpp:979`). ⇒ under the
round-1 predicate a pull response airing **while the origin's re-offer slot is still active** would be reported as
**the original post airing** — a false confirmation, and it **contradicts required test N14d**.

**The corrected predicate — all three clauses:**

> **`pt.m_broadcast && pt.flood && pt.inner_len >= 6`**, then `m_inner_id(pt.inner)` ⇒ the **active**
> `_channel_reoffer_pending` entry with `entry.id == id` **and `entry.holder == false`**.

★ `pt.flood` distinguishes an **origin/holder flood** from an `enqueue_channel_m()` pull response, **and it makes the
DM and channel rows structurally disjoint** rather than merely non-overlapping in practice.
⚠ **N14d must construct a pull response whose id DELIBERATELY MATCHES an active origin entry** — a pull response with
a non-matching id passes vacuously and proves nothing.

**The complete push rule:** enqueue `send_aired` **iff** `o.kind == aired` **and** `frame_tag_of(o.tag) ==
FrameTag::data` **and** a live `_active->_pending_tx` **and** `o.seq == pt.flight_gen` **and** exactly one row:

| carrier | predicate | push fields |
|---|---|---|
| ordinary local DM | `!pt.m_broadcast && !pt.has_previous_hop` | `dst = pt.dst`, `ctr = pt.ctr` |
| locally originated channel-M | **`pt.m_broadcast && pt.flood && pt.inner_len >= 6`** + the entry match above | `dst = 0`, ★ `ctr = entry.ctr` (**full 16-bit**) |
| forwarded DM · pull response · relay/holder channel-M · beacon/RTS/CTS/ACK/NACK | — | ⛔ **telemetry only** |

⛔ **`carrier_owes_send_failed` remains the WRONG predicate here** (design §4.4.1(b)); ⚠ `entry.holder == false` stays
load-bearing — `ctr` is **0** on a holder slot.

### 2.3 ⛔ BLOCKER 4 — the rank rule was TOO BROAD. Scope it.

⛔ **WITHDRAWN ROUND-1 RULE (§8-d):** *"a transition applies only if it raises the rank"*, stated over all
transitions. **Applied literally it breaks two legitimate behaviours:** resetting a terminal view when a **new send**
starts, and the **intentional late `NO CONFIRM` → `DELIVERED` upgrade** — ★ which exists at
`firmware_ui_model.h:1105`, `case K::dm_acked`, whose own comment says *"incl. the LATE-ack upgrade"*.

**The corrected rule — scoped to applying a correlated `send_aired` WITHIN THE SAME TRANSACTION:**
- `waiting` **may become** `aired`;
- `aired` is **idempotent**;
- **every logical terminal state REFUSES `aired`**;
- ⛔ **existing terminal transitions and new-transaction resets remain authoritative and are NOT governed by this
  rank.**

★ Implement the refusal as a switch over the state domain with **no `default:`**, so `-Wswitch` forces a future state
to be classified rather than silently defaulting — the enumerated-subset failure R3.2 exists to prevent. ★ Rank
idempotence is still what removes any need for de-duplication state, so `sizeof(Node)` stays unmoved.

⛔⛔ **AND N16 MUST INVOKE THE MODEL PROMOTION DIRECTLY.** Driven through `ui_route_send_push`, the terminal outcome
has **already closed the tracker**, so the correlation fails first and the rank mutation is **never reached** —
i.e. the test would pass without measuring the rank at all. **That is an instrument that cannot fail, on the very
test written to prevent one.** N16 calls the model's promotion entry directly, with the tracker state it actually
needs.

### 2.4 ⛔ BLOCKER 3 — the tracker lifecycle and the emergency path

**`send_aired` is NON-TERMINAL.** Its matcher must therefore:
- **require an accepted tracker and exact correlation** (§2.6);
- ⛔ **NOT close or consume the tracker**;
- **leave `channel_sent`, `send_failed` and the E2E ACK free to correlate afterwards** — they are the terminal
  outcomes and they must still arrive;
- **update the ordinary DM/channel UI state** per §2.3;
- ★★ **for the EMERGENCY tracker: leave `Emergency`, `ChanState` and `EmgEvidence` UNCHANGED, and RETAIN the
  tracker.** ⛔ An attempt-level fact must not move a live alarm — the same argument `on_outcome`'s existing
  do-not-call-this note makes.

**Required test:** an **emergency `send_aired` followed by `channel_sent` must still reach the existing emergency
outcome path**. ⚠ Without this, a consumed tracker would silently disarm the alarm's own reporting.

### 2.5 ⛔ BLOCKER 5 — the four contract-visible seams a new `PushKind` drags in
- **(a) `src/fw_main.cpp:1170`** — an **explicit case with ruled console text** in the `default`-less switch. ⓘ Its
  own comment records that this switch is the **ONLY** detector for a new kind, because fw_main is outside the native
  build, and that T-K3's 15th kind failed the ESP32 build exactly here.
- **(b) `lib/console/console_json.cpp:389`** — ⛔ **an explicit `write_push` branch plus a GOLDEN JSON TEST.**
  Verified: without one, `send_aired` falls into the final `else { // send_acked / send_failed }`. It happens to emit
  `dst` and `ctr` — ★ **the right output for the wrong reason**, i.e. a behaviour no test pins and any future edit to
  that branch silently changes.
- **(c) `test/test_console_json.cpp:941`** — add the kind to the **exhaustive enum walker**.
- **(d) `ios-companion/INBOX_SYNC_CONTRACT.md`** — the named contract target for the additive `ev:"send_aired"`
  (`ctr`, `dst`) event. Purely additive; **no existing event changes.**

### 2.6 The UI half
- **One EXPLICIT arm in `ui_route_send_push`** — ⛔ never rely on `firmware_ui.cpp:1041`'s `default:` or on
  `firmware_ui_send.h:315`'s `default: return false`.
- **Correlate before applying:** a DM by **`(dst, ctr)`**; a channel post by the **16-bit `ctr` alone** (`dst` is `0`).
  ⚠ **The handle must survive above 255** — truncating re-creates the §b40 defect.
- **Two new states:** `DmState::aired_waiting`, `ChanState::aired`, both rendering **`SENT, waiting`** — the existing
  string, verbatim, **now earned**.
- **Three changed strings:** `firmware_ui.cpp:750` and `:764` → **`QUEUED`**; `:768` → **`NO RELAY HEARD`**.

---
## 3 — Traps, pre-registered
1. ★★ **`failed`/`unknown` are ATTEMPT facts** — ⛔ no terminal UI state may be produced by either (design §4.2).
2. ★★ **A later `aired` must never be suppressed; it must never overwrite a terminal state** (N13, N16).
3. **New enumerators trip `-Wswitch` everywhere** — ⓘ a **feature**; ⛔ never silence it with a `default:`.
4. ⛔ **`ChannelReofferPending` is read-only here** (`node.h:1876-1880`).
5. ⚠ **[[B169]]:** ⛔ **the push enqueue must NEVER sit inside `MR_TELEMETRY`** — it is load-bearing, and board envs
   delete those bodies invisibly to native **and** to all 36 corpus streams.
6. ⛔ **Zero free timer ids** (`kCap == 91`).

---
## 4 — Tests

**Design §7.2 (N8-N16) and §7.3 (P1-P7) are normative — implement them all**, mutation-proven with match counts.
**Plus the four this revision adds**, each of which exists because a round-1 instrument would not have failed:

| # | test | why it is not optional |
|---|---|---|
| **T3-a** ★ | **actual-order regression (§2.1):** timers due **and** a TxDone pending in the same pass ⇒ the push still arrives | with the old order the push is **lost**; a fast-loop test passes either way |
| **T3-b** ★ | **N14d, constructed properly (§2.2):** a **pull response whose id MATCHES an active origin entry** ⇒ **ZERO** pushes | drop `pt.flood` ⇒ RED with a **false confirmation** |
| **T3-c** ★ | **emergency (§2.4):** emergency `send_aired`, then `channel_sent` ⇒ the existing emergency outcome path is still reached; `Emergency`/`ChanState`/`EmgEvidence` unchanged by the `aired` | consume the tracker ⇒ RED |
| **T3-d** ★ | **N16 via the model directly (§2.3)**, five arms: `DELIVERED` · `NO RELAY HEARD` · `NO CONFIRM` · `BLOCKED` · `FAILED` | ⛔ through `ui_route_send_push` the tracker is already closed ⇒ the rank is never reached ⇒ **vacuous** |

The four carrying the most weight from the design remain **N11** (failed attempt then successful retry — zero pushes
and **no terminal state** on attempt 1), **N14a/N14b** (channel-M push; handle above 255 preserved exactly),
**P1** (canned team post: `QUEUED` → `SENT, waiting`), **P5** (drain deleted ⇒ P1 stalls on `QUEUED`).

★ **P6's scope, stated rather than left to a grep:** assert `SENT, no relay` appears nowhere in the **rendering
sources** (`src/`, `tools/`) — ⛔ **not tree-wide**: §1.2's documents will legitimately quote the old string inside
**withdrawn-wording** blocks, and a tree-wide grep would force those withdrawals to be deleted, which §3 rule 3
forbids.

⛔ **Ask of every new assertion: could it have come out otherwise?** Twenty-six recorded instruments that could not
fail — and §2.3's N16 finding is the twenty-seventh candidate, caught in specification rather than in review.

---
## 5 — Gate
1. `pio test -e native`, **then RUN the binary**. From **1623 / 82523 / 0**.
2. ★★ **`s18` reproduces the keystone READ FROM `simulation/BASELINE.md`** — ⛔ never hardcode (D1). T3 touches
   `lib/core` ⇒ **measured, not argued**.
3. ★ **Corpus: zero rows may move**, and ⛔ **state the CORRECT reason if you claim it:** the simulator's HAL
   (`FirmwareNode.cpp:184/196`) returns **only** `kSimTxOk` or `kSimTxTooLong` (`len > 255`); `kSimTxBusy` and
   `kSimTxRadioError` are produced **nowhere** in the simulator, and there is **no sim TxDone→`aired` path**. ⇒ no
   `send_aired` can be enqueued in any corpus run. ⛔ Do not repeat §T2's weaker reason (*"the simulator produces no
   hardware attempt outcomes"*), which did not cover the new core-side emit sites and was a QG finding against T2.
4. **`warning_census.sh`** at **174 / 178 / 178**, `-Wswitch` **0** — ⚠ this slice deliberately provokes `-Wswitch`,
   so the census is how you prove every arm was **added** rather than defaulted.
5. **Both UI probes** with control sets; `probe_firmware_ui` is the only venue running the whole chain.
6. ★★ **D2 explicitly:** `sizeof(Node)` **221880**, `sizeof(Push)` **292**, `sizeof(ChannelReofferPending)` **12** —
   all unmoved. Per-board RAM/flash diff.
7. **Six board envs build**; report RAM/flash. ⚠ §2.1 changes `fw_main` and `DeviceHal`, so the board build is the
   only place its compile surface is checked.

---
## 6 — Documentation owed (M1/M2)
- **All seven groups in §1.2**, updated in place, **prohibitions preserved in meaning**.
- **`ios-companion/INBOX_SYNC_CONTRACT.md`** — the additive `send_aired` event (§2.5(d)).
- **[[B164]]** — its narrowed remainder after T3; **[[B189]]** stays open for [[B186b]].
- ⛔ **[[B193]] does not close. Phase A is not complete.**

---
## 7 — Method
- ★★ **A fact is established by the act** — and §2.1 is that rule applied to the *loop*: the fact is only usable if
  the state that names it still exists when it is collected.
- ★★ **Name the third state**; ★★ **two tests that disagree about the same fact is an instrument-level defect**.
- ★ **A correction placed anywhere but the instruction a reader follows** — §1.2 is this slice's known list; **grep
  for more.**
- ⛔ **PROVENANCE (ledger §3):** never claim an owner or QA approval; **never quote an owner ruling** — reported form
  only; a QA recommendation relayed by the owner is **still a recommendation**.

**Stop and report rather than improvising if:** a corpus row moves · `sizeof(Node)`/`sizeof(Push)`/
`sizeof(ChannelReofferPending)` moves · the channel ownership lookup finds no entry for a post the panel is tracking
(⇒ **report it; inventing a fallback match is the false-confirmation defect**) · a terminal state would have to be
produced by `failed`/`unknown` to make a test pass · §2.1's split cannot be made without duplicating the completion
logic · or this spec and the code disagree (⇒ **report the conflict, do not pick a side**).

---
## 8 — Round-2 change index (every round-1 claim withdrawn, and where it now stands)
| | withdrawn round-1 claim | now |
|---|---|---|
| **a** | native **1623 / 82513 / 0**, `lus` **`77555889`** | **stale, not disputed** — measured before §T2's refinement; current figures **82523** / **`0c72d355`**, both reproduced |
| **b** | *"the string sweep is SIX documented sites"* | **SEVEN groups** — four in the guide, three in the script (§1.2); the list was right, the count wrong |
| **c** | the channel predicate = id match + `holder == false` **only** | ⛔ **contradicted N14d** — a pull response carries the same id. Now requires **`pt.m_broadcast && pt.flood && pt.inner_len >= 6`** (§2.2) |
| **d** | *"a transition applies only if it raises the rank"*, over all transitions | ⛔ **too broad** — blocks new-transaction resets and the late `NO CONFIRM → DELIVERED` upgrade at `firmware_ui_model.h:1105`. **Scoped to applying a correlated `send_aired` within one transaction** (§2.3) |
| **e** | the loop-order hazard: **absent entirely** | ★★ **§2.1** — timers run before the drain, and the M-clear timer deletes `_pending_tx` 5 ms after calculated airtime |
| **f** | the tracker lifecycle and emergency behaviour: **unspecified** | **§2.4**, with its required test |
| **g** | the `PushKind` seams: `pushkind_name` + the companion contract only | **four seams** (§2.5) — `fw_main.cpp:1170`, `console_json.cpp:389` + a golden test, the `test_console_json.cpp` walker, and the named contract file |
