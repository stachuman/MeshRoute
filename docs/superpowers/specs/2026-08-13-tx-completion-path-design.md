<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B189 + §B164 — ONE authoritative HAL TX-completion path · **DESIGN ONLY** · 2026-08-13, **REVISED ROUND 2 + ROUND 3 2026-08-14**

⛔⛔ **THIS IS A DESIGN. NO FIRMWARE WAS WRITTEN**, in round 1 or round 2. Nothing under `lib/`, `src/`, `test/` or
`tools/` was modified; the only edits outside this file are the two register rows M1 obliges (B164, B189).
⛔ **NO OWNER OR QA APPROVAL IS CLAIMED ANYWHERE IN THIS DOCUMENT.** Every ruling referenced is in **reported form**
and is never quoted. **The round-2 QG findings arrived relayed by the owner and are a RECOMMENDATION, not an owner
ruling** — except the six items the owner states as decisions, which are marked **[R2.8 decision]** where they bind.
⛔ **[[B193]] does not close** (nothing here writes flash). ⛔ **Phase A is not complete.**

**Round-2 status: QG HOLD before implementation. The HAL completion path and the three-slice structure are
confirmed; the UI outcome semantics were wrong and are RE-DERIVED below.**
**Round-3 status: QG BLOCKER on the ownership predicate — the round-2 rule made a CHANNEL SEND unable to ever become
`aired`. Corrected in §4.4.1(b), with its tests. ⛔ T3 MUST NOT BE STARTED FROM THIS SPEC YET**; T1 is mechanically
independent, but its go-ahead belongs to the **T1 implementation brief the QA-gate writes**, which is what supersedes
the design-only prohibition — ⛔ **not this document** (R3.3).
⛔ Withdrawn round-1 and round-2 claims are **corrected in place and kept visible as withdrawals**, never deleted
(§12 is the navigable index of all eleven).

**Tree state:** HEAD **`48cd17d`** (*"UI14/B194"*); every `file:line` below was **re-read at `48cd17d`** in round 2.
**Reference figures (`simulation/BASELINE.md`):** native **1615 / 82362 / 0**; `lus` **`43a7b6eb`**; `s18` keystone
**`9868cad3` / 269905**; `sizeof(Node)` **221880**; `TimerWheel::kCap` **91**, all ids consumed.

---
## 0 — ★★★ THE ORGANISING DISTINCTION, ADDED IN ROUND 2

**A HAL completion is an ATTEMPT-LEVEL fact. The app surface speaks SEND-LEVEL facts. They are not the same
alphabet, and round 1 conflated them.**

| | attempt level (new) | send level (existing, authoritative) |
|---|---|---|
| produced by | `DeviceHal` per `_hal.tx()` hand-off | the MAC / channel machinery per **origination** |
| members | `aired` · `failed` · `unknown` · `refused` · sync `busy`/`too_long` | `send_acked` · `send_failed` · `send_e2e_acked` · `channel_sent` · the ACK timeout |
| terminal? | ⛔ **NO — a failed attempt is routinely followed by a successful retry** | ✅ yes |

★★ **Exactly ONE attempt outcome is safe to raise into the send surface: `aired`** — because it can only ever be an
**upgrade** (`queued → sent`) and no later attempt can contradict it. `failed` and `unknown` are **non-monotonic**:
a later attempt supersedes them. ⇒ **the HAL reports ALL attempt outcomes to core telemetry and counters; only a
positively observed `aired` becomes a new UI push.** Every round-2 correction below is a consequence of that line.

---
## 1 — The objective, and the four halves it actually has

**One authoritative HAL TX-completion path reporting `aired`, `busy/retry` and `failed/drop`.**

| # | half | bug | closed by |
|---|---|---|---|
| H1 | no **asynchronous** completion/refusal callback exists on hardware at all | [[B189]] | §3 — the new path |
| H2 | the **synchronous** refusal that DOES exist is **discarded at two call sites** | [[B189]] | §3.6 |
| H3 | `pump_tx()`'s failed arm and the watchdog **drop an admitted frame silently** | [[B189]]/[[B164]] | §3 — `failed` / `unknown`, **as telemetry + counters** (§4.2) |
| H4 | the core/UI knows *"admitted"*, never *"aired"* | [[B164]] | §3 — `aired` + §4.4/§4.5's **single** app consumer |

---
## 2 — §2's state table, RE-VERIFIED AT THE CODE (V1). **Three corrections, all confirmed again at `48cd17d`.**

### 2.1 The five post-`tx()` outcomes on hardware

| # | path | `file:line` (verified) | what happens today | protocol told? |
|---|---|---|---|---|
| 1 | `tx()` oversized | `lib/hal/device_hal.cpp:14` | `return TxResult::too_long` | **partly** — correction **C2** |
| 2 | `tx()` ring full | `lib/hal/device_hal.cpp:15` | `_txq_drops++; return TxResult::busy` | **partly** — correction **C2** |
| 3 | `pump_tx()` `start_transmit` ≠ ok | `lib/hal/device_hal.cpp:38-46` | popped and dropped; the pop at `:45-46` runs **regardless of `r`** | ⛔ **NO** |
| 4 | `service_tx()` watchdog | `lib/hal/device_hal.cpp:52-55` | `abort_tx()` + `_tx_timeouts++` | ⛔ **NO** |
| 5 | **normal completion** | `poll_tx_done()` at `lib/hal/device_hal.cpp:52` | the `true` edge is consumed **only to short-circuit the `&&`** | ⛔ **NO** |

⇒ rows 3, 4 and 5 are the slice. Row 5 is confirmed **not an error path**: `IRadio::poll_tx_done` is documented
(`lib/hal/iradio.h:33-36`) as returning `true` **exactly once per `start_transmit()`, on the TxDone edge** — the
*aired* fact is **already produced by the radio seam today and discarded one line later** by the `!` in `service_tx`.
Nothing must be invented to observe it.

### 2.2 CORRECTION C1 — finding **A** confirmed; its RAM cost is **WRONG BY THE FULL AMOUNT**

`lib/hal/device_hal.h:104` is exactly
`struct TxQEntry { uint8_t buf[255]; uint16_t len; int16_t sf; int32_t bw; int8_t cr; int16_t pre; int8_t pw; };`
and `DeviceHal::tx` (`:13-29`) never reads `p.tag`. **`TxParams::tag` is consumed at `tx()` and dropped.**

⛔ **Corrected:** the brief prices a `uint16_t` tag at *"+16 B"*. **It costs ZERO.** `TxQEntry` has `alignof 4`, lays
out `buf[255]`@0 `len`@256 `sf`@258 `bw`@260 `cr`@264 `pre`@266 `pw`@268 — ending at 269 with three bytes of tail
padding and **`sizeof == 272`**. A `uint16_t tag` lands at **offset 270**, inside that padding.
★ **MEASURED by `static_assert` on four ABIs this session** — host `g++`, `arm-none-eabi-g++`, `xtensa-esp32-elf-g++`,
`xtensa-esp32s3-elf-g++`: all four accept `sizeof(A)==272 && sizeof(B)==272 && offsetof(B,tag)==270`. (A further
`uint32_t seq` measures 272 → **276**, i.e. **+32 B** for the ring — see §4.3.)

### 2.3 CORRECTION C2 — finding **B** right in substance, **wrong in its enumeration, twice**

Whole-tree grep returns **exactly four** `_hal.tx(` call sites:

| `file:line` | function | result | recovery if the frame is lost |
|---|---|---|---|
| `lib/core/node_mac.cpp:1533` | **`rts_duty_defer_fire`** — an **RTS**, ⛔ not a beacon | ⛔ **DISCARDED** | `start_rts_timeout()` on the next line |
| `lib/core/node_mac.cpp:1581` | `tx_flood` — **the actual beacon path** | ✅ tested (`tx_hal_rejected`, `return false`, digest stays dirty) | the beacon cadence |
| `lib/core/node_mac.cpp:1812` | `tx_with_retry` | ✅ tested (`tx_hal_rejected`, `TxHandOff::rejected`) | the MAC ack-timeout, armed for `handed` **and** `rejected` |
| `lib/core/node_mac.cpp:1894` | **`retry_stashed`** | ⛔ **DISCARDED** | the ack-timeout re-armed three lines below |

⇒ **TWO sites discard, not three**; the brief names `:1533` twice, once as *"the beacon path"*, which it is not
(`:1533` sends an `"RTS"` with `p.tag = FrameTag::rts`). **H2 is a two-line fix at two named sites.**

### 2.4 CORRECTION C3 — every discarded refusal already has a MAC recovery

`:1533` arms `start_rts_timeout()`; `:1894` re-arms the ack-timeout; `tx_with_retry` **deliberately** arms the
ack-timeout for `rejected` as well as `handed` (the §tx-admission TX1 argument at `node_mac.cpp:2059-2062`).
⇒ **the defect is the SILENCE, not an absence of recovery.**

★★ **In round 2 this becomes the load-bearing fact of the whole design, not just of §4.2:** it is precisely because
the existing MAC recovery keeps running that **attempt-level silence at the app surface is safe** — a failed attempt
is followed by a retry, and the retry's `aired` is what the app hears. (R2.1.)

⚠ **One measured exception, already ruled and NOT changed here:** a beacon has no per-frame recovery, and `tx_flood`
commits `commit_channel_digest_advertised` at admission, so a beacon dropped at row 3 burns an advertisement horizon
for a digest that never aired. `lib/core/node.h:1910-1911` and `:2425-2427` both already state that a later
`pump_tx` error is **outside** the owner-ruled transmitter-admitted boundary. **[R2.8 decision] the boundary is NOT
moved in this work** (§9).

### 2.5 What already exists, re-verified (U1)

* `Node::on_radio_busy(const BusyInfo&)` — `lib/core/node.h:94`, defined **`lib/core/node.cpp:2191`**. ✅ as briefed.
* `BusyInfo` `lib/core/hal.h:40`; `BusyReason` `:23` — ✅ **no member for a radio error, a watchdog abort or a
  successful airing** (§4.1 answers whether to extend it: no).
* `TxResult { ok, busy, too_long, radio_error }` — `lib/core/hal.h:22`. ✅
* `TxParams::tag` — `lib/core/hal.h:31`; low byte `FrameTag`, high byte §B186a's `MobileTxOp`. ✅
* **b186a §4** is read, **not re-derived**, and **not contradicted**. Its conclusions this design rests on: the async
  refusal is **simulator-only**; the sync refusal is **hardware-only and corpus-dark**
  (`tx_hal_rejected` = `tx_failed` = `oversized` = **0** across all 36 streams). Its §4.2 gap — how often a real radio
  fills the ring — **stays unmeasured**.
* ★ Its **row 6**: `tools/probe_firmware_ui` hosts the **real `DeviceHal`** over a probe `IRadio` whose
  `start_transmit` can fail. Verified: `run.sh:70` links `lib/hal/device_hal.cpp` and all `lib/core/*.cpp`;
  `probe_main.cpp:450` already calls `g_hal.service_tx()`. ⇒ rows 3, 4 and 5 are reachable in an automated host probe.

---
## 3 — The completion path: exact shape, every seam named

### 3.1 The one new type (`lib/core/hal.h`, beside `BusyInfo`)

```
enum class TxOutcomeKind : uint8_t { aired, refused, failed, unknown };

struct TxOutcome {
    TxOutcomeKind kind;
    BusyReason    reason;        // meaningful iff kind == refused (verbatim BusyInfo::reason)
    TxResult      error;         // meaningful iff kind == failed  (verbatim start_transmit's answer)
    uint16_t      tag;           // the TxParams::tag the SENDING SITE stamped — echoed, never rebuilt
    uint32_t      seq;           // the flight identity the SENDING SITE supplied; 0 = none  (§4.3)
    int16_t       sf;
    uint64_t      busy_until_ms; // refused only
};
```

⛔ **`BusyInfo` is NOT extended and NOT deleted** (§4.1) — `TxOutcome` is a **separate** type so the simulator's
braced init at `NodeRuntimeWrapper.cpp:147` compiles unchanged.

### 3.2 The one authoritative core entry

`void Node::on_tx_complete(const TxOutcome& o);` — `lib/core/node.h`, beside `on_radio_busy` (`node.h:94`).
`on_radio_busy(const BusyInfo&)` becomes a **four-line adapter** building
`TxOutcome{ refused, info.reason, TxResult::ok, info.tag, 0, info.sf, info.busy_until_ms }`; the **entire present
body of `node.cpp:2191-2270` moves verbatim** into the `refused` arm. ⇒ both HALs drive **one** core function and the
simulator needs **no change at all** to stop diverging.

### 3.3 The device half (`lib/hal/device_hal.{h,cpp}`)

| step | where | what changes |
|---|---|---|
| a | `device_hal.h:104` | `TxQEntry` gains `uint16_t tag; uint32_t seq;` — the identity survives the enqueue |
| b | `device_hal.cpp:13-29` (`tx`) | copies `p.tag` / `p.seq` into the slot. **No other behaviour change; return values untouched** |
| c | `device_hal.cpp:34-47` (`pump_tx`) | on `ok`: latch `{tag, seq}` into a one-deep `_inflight` (half-duplex). On `≠ ok`: push `TxOutcome{failed, error = r}` **before** the existing pop. ⛔ pop + drop unchanged |
| d | `device_hal.cpp:52` (`service_tx`) | `poll_tx_done()`'s **true edge** pushes `TxOutcome{aired}` for `_inflight` and clears it. ⛔ the `&&` short-circuit is preserved exactly |
| e | `device_hal.cpp:52-55` (watchdog) | after `abort_tx()` + `_tx_timeouts++`, push `TxOutcome{unknown}` and clear `_inflight` |
| f | `device_hal.h`, device-loop glue | `bool pop_tx_outcome(TxOutcome& out);` — the idiom of the existing `pop_due_timer()` at `:64` (U3). Ring **capacity 4** **[R2.8 decision]** + `tx_outcome_drops()` beside `txq_drops()` (C2: a dropped report is counted, never silent) |

⛔ No function pointer, no sink object, no `Node&` inside `DeviceHal`; `device_hal.cpp` stays Arduino-free.

### 3.4 The runtime glue (`src/fw_main.cpp`)

One line immediately after `src/fw_main.cpp:1091`'s `g_hal.service_tx()`:
`for (meshroute::TxOutcome o; g_hal.pop_tx_outcome(o); ) g_node.on_tx_complete(o);`
— the shape of the timer drain at `:1069`. **No timer is armed anywhere** (§4.7). `fw_main` stays glue (U3).

### 3.5 The app/UI half

The `aired` arm — **and only the `aired` arm** (§4.2) — may enqueue one `Push`, which reaches the OLED through the
seam that **already exists**: `src/fw_main.cpp:1160`'s `while (g_node.next_push(pu)) mr_ui_on_push(pu);`.
⇒ **the OLED consumes the core's fact through the core's own app channel; it grows no tracker of its own and
`lib/hal/mr_ui.h` gains no fifth hook.**

### 3.6 H2 — the two discarded synchronous refusals

`node_mac.cpp:1533` and `:1894` capture the `TxResult` and, on `≠ ok`, emit the **existing** `tx_hal_rejected` with
the site's existing label (U1). ⛔ **No behaviour beyond the emit changes** — `:1533` still arms
`start_rts_timeout()`, `:1894` still re-arms the ack-timeout, because §2.4 shows those ARE the recovery.

---
## 4 — The seven decisions. Options · recommendation · reasoning

### 4.1 ★★ THE CORPUS QUESTION

The dichotomy "migrate the sim or diverge" is false: the divergence B189 names is that **the two HALs call two
different core functions**, and §3.2 removes it with **zero simulator changes**.

| option | what it is | corpus movement | verdict |
|---|---|---|---|
| **1A** | unify **in the core**; `on_radio_busy` a verbatim-body adapter; simulator untouched | ⛔ **NOTHING.** Provable: `lus` rebuilds and `s18` must reproduce **`9868cad3` / 269905**, 36/36 at the six documented movers' published values | ★ **RECOMMENDED** |
| **1B** | 1A + point the sim wrapper at `on_tx_complete`, delete the adapter | nothing, if the translation is exact. Costs a **second-repo** edit (`NodeRuntimeWrapper.cpp:147`) + rewriting ~35 direct `on_radio_busy` invocations in the native suite | follow-up slice (C1: it is a pure refactor) |
| **1C** | + the simulator **reports `aired`** | ⛔ **all 36 rows**, one outcome per frame per node | ⛔ **[R2.8 decision] DEFERRED to its own attributed corpus slice** |

Under 1A the `aired` / `failed` / `unknown` arms are **corpus-dark by construction** — the simulator has no bounded
TX queue, no `start_transmit` failure and no TxDone edge. Their cover is native + the host probes (§7). ⛔ **That
limit must be written into the register, this spec and `BASELINE.md`**, or the next reader re-discovers B189.
⛔ Under every option the `^### 36/36 corpus` anchor table is **NOT edited**.

★★ 1C is also the precondition for §4.4's rejected option 4B: any core *decision* or corpus-visible *label*
consuming `aired` would read **false on every simulator run** while the sim cannot produce the fact.

### 4.2 ★★ OBSERVABILITY versus RECOVERY — **RE-DERIVED IN ROUND 2 (R2.1)**

⛔⛔ **WITHDRAWN, ROUND 1 (kept visible, §12-a):** round 1 wrote *"`failed` → new emit + **the push's failure arm**"*
and *"`unknown` → new emit + **the push's `unknown` arm**"*, and §4.5 gave them the terminal panel strings
**`NOT SENT`** and **`TX UNCONFIRMED`**. ★★ **THAT WAS WRONG, AND WRONG IN THE EXACT SHAPE THIS DESIGN EXISTS TO
PREVENT.** The sequence is reachable today: *attempt 1's `start_transmit` fails → the panel says `NOT SENT` →
the MAC ack-timeout fires → attempt 2 airs and the message is delivered.* **Saying `NOT SENT` about a frame that
then flies is exactly as wrong as saying `SENT` about one that never did — it is the FALSE-NEGATIVE MIRROR of
[[B164]].**

**The corrected line, in three tiers:**

| outcome | producer | core **TELEMETRY** | **COUNTER** | **APP PUSH / UI** |
|---|---|---|---|---|
| `aired` | metal only | ✅ new emit | — | ★ ✅ **the ONLY one** — upgrades `QUEUED` → `SENT, waiting` |
| `failed` | metal only | ✅ new emit (carries `TxResult`) | ✅ a new `tx_failed_arms` beside `txq_drops()` | ⛔ **NO** |
| `unknown` | metal only | ✅ new emit | ✅ the **existing** `_tx_timeouts` (`device_hal.h:115`) | ⛔ **NO** |
| `refused` | simulator only | `radio_busy` + `mobile_tx_refused`, **verbatim** | — | ⛔ **NO** |
| sync `busy` / `too_long` | metal only | `tx_hal_rejected` at all four sites (§3.6) | the **existing** `_txq_drops` | ⛔ **NO** |

**The authoritative LOGICAL terminal outcomes are UNCHANGED and remain the app's truth:** `send_acked`,
`send_failed`, `send_e2e_acked`, `channel_sent`, and the ACK timeout.

⛔ **`failed` is NOT routed into `retry_slot_of` / `kRadioBusyRetryTimerId`** — **[R2.8 decision] no B186b recovery
is added.** Two reasons: it is recovery, out of scope; and it would be **the first time the R4.5b stash-retry
machinery ever ran on metal** (b186a records it as dead code there), i.e. an untested behaviour change wearing a
diagnostic's name.

★ **AND THE SPEC MUST SAY THIS OUT LOUD, because it is what makes attempt-level silence safe:** the existing MAC
recovery **still runs** for every affected frame — `start_ack_timeout()` for retry-eligible frames (armed for
`rejected` as well as `handed`), `start_rts_timeout()` for the RTS, the beacon cadence for a flood (§2.4, with its
one ruled exception). **Nothing is left stranded by reporting an attempt failure only to telemetry.**

ⓘ **If attempt failures must EVER be displayed**, they need **explicitly NON-TERMINAL** states — e.g. `RETRYING` —
that a later `aired` or delivery outcome can **upgrade**. ⛔ **Not designed here, and not implemented here**;
recorded so the option is not re-invented as a terminal state a third time.

### 4.3 WHERE THE IDENTITY LIVES (finding A) — **AMENDED IN ROUND 2 (R2.4)**

| option | shape | cost | verdict |
|---|---|---|---|
| **3A** | `uint16_t tag` in `TxQEntry` + a one-deep `_inflight` latch | **+0 B** (measured, §2.2) | sufficient for H1–H3, **insufficient for H4** |
| **3B′** | 3A **+ carry the EXISTING flight identity** as `TxParams::seq` → `TxQEntry::seq` → `TxOutcome::seq` | ring **+32 B**; `TxParams` +4 B (stack); ⛔ **0 B in `Node`** | ★ **RECOMMENDED** |
| **3B″** | a **new** monotonic HAL counter minted per hand-off | +32 B **plus** a new `Node` member ⇒ `sizeof(Node)` moves ⇒ D2's ten-env sweep | ⛔ rejected (U1: the tree already has an identity) |
| **3C** | the core reconstructs from FSM state at completion | 0 B | ⛔ **REJECTED** — §5's prohibited class |

**Reasoning for 3B′.** 3A cannot bind an `aired` DATA to a *flight*: a completion arriving after the flight was
replaced would be attributed to the new one — a false confirmation on the exact surface B164 is about. The tree
already has the exact identity: `PendingTx::flight_gen`, the monotonic per-flight value the L9 fix introduced.

⛔⛔ **ROUND-2 AMENDMENT — THE BUILDER MUST NEVER DERIVE `seq`, AND ROUND 1 LEFT THE DOOR OPEN (§12-b).** Round 1
said only *"the implementation must introduce one `TxParams` builder and route all four sites through it"*. **If that
builder reads the CURRENT `PendingTx::flight_gen`, a stale retry is labelled as the NEW flight and falsely confirms
it** — the reconstructed-fact class this design exists to prevent.

★ **Verified at `48cd17d`, and this is why the hazard is real and not theoretical:** `duty_defer_fire` performs its
exact-match flight guard **BEFORE** transmitting (`lib/core/node_mac.cpp:1873`:
`if (tag == FrameTag::data && (!_active->_pending_tx || _active->_pending_tx->flight_gen != s.flight_gen)) return;`),
but **`retry_stashed` (`:1886-1894`) transmits its stored frame with NO pre-transmit flight check at all** — its only
`flight_gen` test (`:1900`) sits **after** `_hal.tx()` and guards the ACK re-arm, not the transmission.
ⓘ Citation correction: the QG note cites `:1879` for the guard; at `48cd17d` `:1873` is the **pre-transmit** guard
and `:1879` is `duty_defer_fire`'s **post-hand ACK-re-arm** guard. Both read `s.flight_gen`; the finding is unchanged.

**⇒ REQUIRED: identity is an EXPLICIT INPUT per caller, never derived inside the builder.**

| caller (`file:line` at `48cd17d`) | identity supplied |
|---|---|
| ordinary DATA — `tx_with_retry` `node_mac.cpp:1812` | the current `PendingTx::flight_gen` |
| **`retry_stashed` `node_mac.cpp:1894`** | ★ **`TxStashSlot::flight_gen`** — it already exists and `:1900` already reads it |
| deferred carriers (`rts_duty_defer_fire` `:1533`) | the identity **stored in that carrier** — `RtsDutyDefer::flight_gen`, already read at `:1519` |
| unrelated frames (beacon/flood, CTS/ACK/NACK) | **zero** |

⚠ **U2 hazard (the S1/L9 field-drop shape):** a fifth `TxParams` site that forgets `seq` reports flight `0` and its
completion is unattributable. The builder exists to make that impossible — **but it must take the identity as an
argument**, so a caller that has no identity must pass `0` deliberately rather than inherit one by accident. Per C1,
the builder lands in the **refactor** slice T1.

### 4.4 WHAT THE CORE DOES WITH `aired` — **RE-DERIVED IN ROUND 2 (R2.2, R2.3, R2.5)**

| option | shape | `sizeof(Node)` | verdict |
|---|---|---|---|
| **4A** | **forward, store nothing.** Emit (telemetry) + enqueue **one** `Push` for the ownership-qualified case. Uses the **existing** `_push_ring` | ⛔ **UNMOVED (221880)** ⇒ D2's ten-env sweep not triggered | ★ **RECOMMENDED** |
| **4B** | add `PendingTx::data_ever_aired`, flip `basis` to `local_aired` | ⛔ **9 corpus rows move** for a label rename, **and the flag reads `false` on every simulator run** ⇒ the categorical `basis=alternate_path` claim becomes a lie corpus-wide | ⛔ **REJECTED** |
| **4C** | a `Node` accessor the UI polls | the fact is lost between polls | ⛔ rejected |

`aired` cannot be *derived* — it is a physical act — but it need not be *stored*: it is **forwarded at the instant it
is established**. ⇒ **`data_ever_admitted` keeps its name, its meaning and its single writer**, exactly as
`node_mac.cpp:2103-2109` already prohibits changing.

#### 4.4.1 The push — three round-2 corrections

**(a) R2.3 — the `Push` carrier. ⛔ WITHDRAWN ROUND-1 CLAIM (§12-c):** round 1 said the push carries *"`ctr`
(already a `Push` member) **and the outcome kind**"*. ⛔ **`struct Push` (`lib/core/command.h:293`) has NO
outcome-kind member** — re-verified at `48cd17d`: `kind`, `reason` (a `SendFailReason`), `join_reason`, `origin`,
`dst`, `channel_id`, `layer_id`, `enc`, `blocked_channel`, `relayed`, `peer_conf`, `ctr`, … . The round-1 zero-byte
claim was therefore **not established as stated**.

**The corrected shape, which is also the safest:** add **ONLY** `PushKind::send_aired`; it carries the **existing**
`ctr` and `dst`; ⛔ **`failed`/`unknown` never enter the push ring** (§4.2) ⇒ **the `PushKind` IS the outcome kind
and NO new `Push` field is needed at all.** `sizeof(Push) == 292` (`command.h:352`) and the `offsetof` trio at `:349`
stay untouched — **now for a reason that holds.**

★★ **APPEND the enumerator after `team_channel_no_key` (the current last), never insert.** `PushKind` is
contract-visible, and the reason is **stronger than style**: `command.h:229-232` records that **the simulator bridges
`PushKind` on its underlying `uint8_t` with a `static_assert` pinning `join_adopted == 13`**
(`lora-universal-simulator` `ConsoleNames.cpp` + `NodeRuntimeWrapper.cpp`). ⇒ an insertion silently renames an
existing kind for every scenario **and** trips a cross-repo assert. ★ And appending comes with a **free build-time
tripwire in this repo**: `pushkind_name` (`lib/console/console_json.cpp:127-146`) switches on `PushKind` with **no
`default:`**, so `-Werror=switch` hard-fails until the new kind is named.
**Document the additive JSON/companion event** (`ev:"send_aired"`, `ctr`, `dst`) in the companion contract — it is
purely additive; no existing event changes.

**(b) THE ORIGIN-OWNERSHIP PREDICATE — ⛔⛔ RE-DERIVED IN ROUND 3 (R3.1). ROUND 2 WAS WRONG HERE.**

⛔⛔ **WITHDRAWN ROUND-2 CLAIM, KEPT VISIBLE (§12-i).** Round 2 wrote: *"Use `carrier_owes_send_failed(...)` … the
project's one definition of 'this carrier owns an app future keyed on `(dst, ctr)`' … ⇒ **this is STRICTLY STRONGER
than R2.5's stated minimum**: it also excludes channel-M frames, which the minimum would have let through."*
★★ **THAT IS EXACTLY BACKWARDS FOR THIS QUESTION, and the shape of the mistake is worth naming because it recurs:
THE PREDICATE WAS REUSED FOR A QUESTION IT DOES NOT ANSWER.** `carrier_owes_send_failed`
(`lib/core/node_carriers.h:527`, doc block from `:514`) does **not** mean *"owns any app future"*. It means exactly
*"dropping this carrier owes a **`send_failed`** push"*, and it excludes `channel_m` **DELIBERATELY** — its own doc
at `:518-520` says why, re-read at `48cd17d`: *"It is fire-and-forget: the app's future for a channel POST is the
**`channel_sent`** Push, **owned by the `_channel_reoffer_pending` slot**, NOT by any individual frame."* ⇒ the
exclusion exists because the future is a **DIFFERENT** one, **not because there is none.** Round 2 reported the
exclusion as a feature; it was the bug.

★★★ **THE VERIFIED CONSEQUENCE, and its tell.** A canned channel post and an emergency **physically transmit as
channel-M** (`do_data_tx`'s `if (pt.m_broadcast)` arm, `node_mac.cpp:1916-1946`, which hands `pack_m` output with
`FrameTag::data` at `:1945`). ⇒ the round-2 rule **rejects every channel-M completion** ⇒ **`ChanState::aired` is
UNREACHABLE** ⇒ it **contradicts P1** (a canned team post reaching `SENT, waiting`) while **agreeing with N14**
(every channel-M completion produces zero pushes). ⛔ **P1 AND N14 COULD NOT BOTH HAVE PASSED HONESTLY — and a test
plan in which two tests disagree about the same fact is the instrument-level form of this arc's own defect class.**

**Second half, verified:** `PendingTx::ctr` for a channel-M carries **only the low byte** — `channel_msg_id_mint`
(`node_channel.cpp:54-58`) is `origin<<24 | (key_hash32 & 0xffff)<<8 | (ctr & 0xff)`. **The full UI correlation
handle lives in `ChannelReofferPending::ctr`** (`lib/core/node.h:1872`), a **16-bit** field whose own comment states
it exists *"so `channel_sent` can be correlated past 255 posts"*, that it is *"A LOCAL CORRELATION HANDLE ONLY"*, and
that it is **`0` on a holder slot**. Confirmed at the writers: `channel_reoffer_register` (`node_channel.cpp:1114`,
called from the origination at `:786` with the full 16-bit `c`) sets `holder = false`; `channel_holder_reoffer_register`
(`:1136`) sets `holder = true; ctr = 0`.

#### The corrected rule — TWO DISTINCT OWNERSHIP PATHS

| carrier | predicate | push fields |
|---|---|---|
| **ordinary local DM** | `!pt.m_broadcast && !pt.has_previous_hop` | `dst = pt.dst`, `ctr = pt.ctr` |
| **locally originated channel-M** | decode the id from `pt.inner`, then find the **active** `_channel_reoffer_pending` entry with `entry.id == id` **AND `entry.holder == false`** | `dst = 0`, ★ **`ctr = entry.ctr`** — the full 16-bit handle |
| **forwarded DM · pull response · relay/holder channel-M** | — | ⛔ **no `send_aired` push** |

⇒ this **reuses `_channel_reoffer_pending` as the EXISTING authority for the channel plane** (U1) **without misusing
the `send_failed` predicate for a question it does not answer.**

★ **The decode is an EXISTING helper — do not hand-roll it (U1).** `pt.inner` for an M frame is
`[channel_msg_id 4 BE][channel_id][flavor][body]` (`node_mac.cpp:1913-1914`), and **`Node::m_inner_id(const uint8_t*)`**
(`node_channel.cpp:1017-1020`) is the shared BE decode whose own comment says it exists *"so the BE decode isn't
hand-rolled per call site"*; `do_data_tx:1919` already calls it. ⚠ Guard `pt.inner_len >= 6` first — `do_data_tx:1917`
already fails loud on a shorter inner, and this read must not be the one that underflows.

⚠ **`entry.holder == false` is LOAD-BEARING, not decoration:** `ctr` is **0** on a holder slot (a relay owns no
origination), so a holder match would push a correlation handle that means nothing — a false confirmation carrying a
meaningless id.

⛔⛔ **AND A HARD CONSTRAINT THE IMPLEMENTATION MUST CARRY: READ `ChannelReofferPending`, NEVER GROW IT.**
`node.h:1876-1880` `static_assert`s `sizeof(ChannelReofferPending) == 12` with `offsetof(id) == 4` and
`offsetof(ctr) == 8`, and its message states that a grown record costs real bytes
`× cap_channel_reoffer_pending(4) × MR_N_LAYERS` and **moves `sizeof(Node)`** — i.e. it triggers D2's ten-env sweep.
The design needs **no** new field there: `id`, `ctr` and `holder` all already exist.

**The push rule, complete:** enqueue `send_aired` **iff** `o.kind == aired` **and** `frame_tag_of(o.tag) ==
FrameTag::data` **and** a live `_active->_pending_tx` **and** `o.seq == pt.flight_gen` **and** the carrier matches
**one** of the two ownership rows above. Beacons, RTS, CTS, ACK, NACK, forwarded transit, pull responses and
holder/relay channel-M ⇒ **telemetry only, never a push.** The consumer additionally requires exact correlation
against `SendTracker`'s live slot — `(dst, ctr)` for a DM, the 16-bit `ctr` handle for a channel post (§4.5).

**(c) R2.2 — MONOTONIC EVIDENCE replaces "first outcome per origination". ⛔ WITHDRAWN ROUND-1 RULE (§12-d):**
round 1 wrote *"at most one push per origination, on the **first** outcome"*. ⛔ **A first-outcome rule can SUPPRESS
A LATER `aired` after an earlier weaker attempt** — the same false-negative shape as R2.1.

**The corrected rule — ⚠ EXTENDED IN ROUND 3 (R3.2), because round 2 stopped one rank too early (§12-j):**

> ★★ **`queued < aired < EVERY logical terminal outcome`** — applied as a monotonic rank. A transition is applied
> **only if it raises the rank**; ⛔ **a weaker fact may never prevent, overwrite or downgrade a stronger one.**

⛔ **WITHDRAWN ROUND-2 WORDING:** round 2 wrote the rank as *"`queued < aired < delivered/relayed`"*, which names only
**two** of the terminal outcomes. ⇒ under that wording **a DELAYED `send_aired` could OVERWRITE a terminal state that
had already arrived** — `DELIVERED`, `NO RELAY HEARD`, `NO CONFIRM`, `BLOCKED` or `FAILED`. ⓘ **Same defect
direction as R2.2, one layer later:** R2.2 stopped a weak outcome *suppressing* a strong one; R3.2 stops a
**mid-rank** outcome *overwriting* a terminal one. **Every** terminal state outranks `aired`, without exception, and
the rank must be defined over the whole `DmState`/`ChanState` domain rather than over an enumerated subset — an
enumerated subset is exactly what would go stale when a ninth state is added. (Same ordering discipline the
marker/latch work used: an upgrade is allowed, a downgrade is not.)

★ **And this deletes the round-1 state entirely.** With rank monotonicity in the UI model, a repeated `aired` is
idempotent, so **no de-duplication bit is needed anywhere** — ⛔ **the round-1 fallback "one bool in `PendingTx` at a
measured +0" is WITHDRAWN as unnecessary** (§12-e), and `sizeof(Node)` is provably unmoved **with no fallback at
all**. The cost is at most one push per **aired DATA attempt** of a locally-originated flight; attempts are bounded
by the MAC's own retry limits and the consumer is idempotent by rank, so the 32-deep drop-oldest `_push_ring` is not
at risk.

### 4.5 THE UI HALF — **RE-DERIVED IN ROUND 2 (R2.1, R2.6, R2.8)**

**What renders today** (`src/firmware_ui.cpp:744-780`, re-read at `48cd17d`):

| state | reached when | string today |
|---|---|---|
| `DmState::submitting` / `ChanState::submitting` | the request was drained to dispatch | `SENDING...` |
| `DmState::waiting_ack` | `on_send_accepted(dm)` — `firmware_ui_model.h:1033` | **`SENT, waiting`** |
| `ChanState::waiting` | `on_send_accepted(else)` — `firmware_ui_model.h:1034` | **`SENT, waiting`** |
| `ChanState::no_relay` | `channel_no_relay` | **`SENT, no relay`** |

★★ **THE MEASUREMENT THAT FORCES THE CLAIM.** `on_send_accepted` is called after `tr.accept(r.ctr)` with a non-zero
`ctr` — the core minted a counter and queued the message. That is *weaker* than HAL admission: it does not establish
that `_hal.tx()` was ever called. Between it and the air sit five measured gaps (the core queue, the oversize reject,
the ring-full drop, `pump_tx`'s failed arm, a lost TxDone). ⇒ **today's `SENT, waiting` is the §B69 false
confirmation, one layer further out than the case §B69 was written about.**

**THE DESIGN CLAIM, unchanged and now applied consistently: `aired` MAY say SENT, and nothing weaker may.**
`poll_tx_done()`'s true edge **is** the SX1262 TxDone edge — the physical act, established by the act.

**Strings that CHANGE — exactly THREE:**

| state | today | proposed | why |
|---|---|---|---|
| `DmState::waiting_ack` | `SENT, waiting` | **`QUEUED`** | reached at core admission |
| `ChanState::waiting` | `SENT, waiting` | **`QUEUED`** | same |
| `ChanState::no_relay` | `SENT, no relay` | ★ **`NO RELAY HEARD`** | **R2.6** — see below |

**States that are ADDED — exactly TWO:**

| new state | reached by | string |
|---|---|---|
| `DmState::aired_waiting` | `send_aired` (DM, `(dst, ctr)`-correlated) | **`SENT, waiting`** — the existing string, verbatim, now earned |
| `ChanState::aired` | `send_aired` (channel, correlated) | **`SENT, waiting`** |

⛔⛔ **WITHDRAWN ROUND-1 UI STATES (§12-a), KEPT VISIBLE:** round 1 added **`NOT SENT`** (a `failed` attempt, via a
new `FailReason::tx_dropped`) and **`TX UNCONFIRMED`** (an `unknown` attempt). **Both are withdrawn under R2.1: they
are TERMINAL renderings of NON-TERMINAL attempt facts**, and the reachable sequence *attempt 1 fails → `NOT SENT` →
MAC retries → attempt 2 airs and delivers* makes them the false-negative mirror of [[B164]]. `failed` and `unknown`
now surface **only** as core telemetry plus the counters in §4.2. ⓘ A future non-terminal `RETRYING` state, which a
later `aired` could upgrade, is the correct shape if they must ever be shown — **not designed here.**

**R2.6 — `SENT, no relay`.** Round 1 kept it, arguing it was a different state machine, and that was wrong: it is the
**same `ChanState`, rendered by the same function** (`firmware_ui.cpp:766`), so retaining it directly contradicts the
rule stated three states above it. The two options are **(a)** the conservative rename and **(b)** gate the word SENT
on `aired` having been observed. ★ **Recommendation: (a), in T3** — it is what QG recommends, and independently it is
the right shape here: (b) requires remembering an attempt-level fact inside a state produced by the *origin re-offer*
outcome, i.e. new state to make a string conditional, when the honest reading (*we heard no relay*) needs neither.
**`NO RELAY HEARD` also reads more truthfully**: `channel_no_relay` means the re-offer exhausted without overhearing
a relay — an observation about what was **heard**, which is exactly what the new string says.

**Strings that DO NOT change:** `SENDING...`, `DELIVERED to <label>`, `PICKED UP`, `NO KEY`, `NO CONFIRM`,
`NOT CONFIRMED` / `no send handle`, `BLOCKED`, and the whole emergency ladder.

**The seam the UI half touches:** `src/firmware_ui.cpp:1041`'s `switch (pu.kind)` has a `default:` routing to
`mrui::ui_route_send_push`, whose own `default: return false` (`src/firmware_ui_send.h:287-289`) is documented as
correct. ⇒ **a new `PushKind` would be silently ignored there.** The arm must be added **explicitly in
`ui_route_send_push`**, and applied through the monotonic rank of §4.4.1(c).

**The correlation, per plane (R3.1) — the two paths do NOT share one key:**

| the push says | correlate against `SendTracker`'s live slot by | why |
|---|---|---|
| DM: `dst = pt.dst`, `ctr = pt.ctr` | **`(dst, ctr)`** | the DM's own origination handle |
| channel: `dst = 0`, `ctr = entry.ctr` | ★ **the 16-bit `ctr` handle alone** (`dst` is `0` and carries nothing) | `SendTracker::_ctr` is the handle `tr.accept(r.ctr)` stored — the **same** value `channel_reoffer_register` was given at `node_channel.cpp:786`, so the two ends already agree |

⚠ **The channel handle must survive above 255.** `entry.ctr` is 16-bit precisely so `channel_sent` can correlate past
255 posts; truncating it anywhere in this path re-creates the §b40 defect that field was added to fix. §7's
"handle above 255" test exists to make that unfalsifiable.

**The bench guide — [R2.8 decision].** The change is **approved in T3**, ⛔ **but it must NOT require a human to
visually observe a transient `QUEUED`.** The automated tests pin the exact transition (§7); the metal guide is
written so that `QUEUED` **may be too brief to see and its absence is NOT a failure**. The metal expectation becomes:
`SENDING... → SENT, waiting → DELIVERED to <label>`, with a note that a brief `QUEUED` may appear between the first
two. ⛔ The **absence** of `SENT, waiting` remains the §B113 failure it is today.

### 4.6 ROW 4 — ★★ NAMING THE THIRD STATE (unchanged in substance; its consumer changed)

`pump_tx` set the deadline **after** `start_transmit` returned `ok` — the PA was keyed and the frame was clocked into
the SX1262 FIFO. The watchdog fires when the TxDone edge never arrives within `1.5 × airtime + 100 ms`. The frame may
have **fully aired, partly aired, or not aired at all**, and nothing on this device can distinguish them.

★★ **THE THIRD STATE IS `TxOutcomeKind::unknown`**, telemetry name **`tx_unknown`**. Its declaration must carry:

> *the transmit was STARTED and its completion was never observed. It may have aired in whole, in part, or not at
> all.* ⛔ *It is NOT `aired` and it is NOT `failed`; collapsing it into either is this arc's
> binary-test-over-a-ternary-domain defect.*

⛔ **Round-2 change: it has NO panel string** (§4.5's withdrawal). It surfaces as telemetry plus the **existing**
`_tx_timeouts` counter. ★ **The naming argument is unchanged and is if anything stronger** — collapsing `unknown`
into `failed` **in the telemetry** would still be the ternary defect, and §7's N3 pins it there.

⇒ the outcome domain has **five** members: sync-refused (rows 1-2), async-refused (sim only), `aired`, `failed`,
`unknown`. `TxOutcomeKind` carries four; the fifth stays in `TxResult`, returned synchronously.

### 4.7 TIMERS — none are needed

`TimerWheel::kCap == 91` (`lib/hal/timer_wheel.h:25`), all ids consumed. **This design arms ZERO timers.** The path
rides `service_tx()` (`src/fw_main.cpp:1091`) and the existing push drain (`:1160`). The watchdog deadline is a plain
`uint64_t _tx_deadline_ms` compared inside `service_tx` (`device_hal.h:114`) — **not** a `TimerWheel` id and it does
not become one. ⇒ **no stop-and-report on this axis.**

---
## 5 — [[B169]]: how this design avoids the invisible board warning

The rule: **a local that exists only to fill an emit lives INSIDE `MR_TELEMETRY(...)`.**

1. The new emits read `TxOutcome`'s own members — live function parameters ⇒ **no diagnostic local is needed** on the
   `aired`/`failed`/`unknown` arms. Any name lookup is declared inside the `MR_TELEMETRY` block, as `node.cpp:2205`
   already does for `op`.
2. ⛔ **The push enqueue, the counters and the ownership predicate are NOT telemetry and must NEVER be wrapped** —
   they are load-bearing, and `MESHROUTE_NO_TELEMETRY` is set on the board envs, so wrapping them would delete the
   entire B164 fix on metal while leaving native and all 36 corpus streams green. This is B169's mirror-image trap
   and it earns a comment at the site.

**The instrument:** `tools/warning_census.sh` must exit **0** at its pinned counts (b186a §2.3: **174 / 178 / 178**,
`-Wswitch` **0**), **and** the implementation must reproduce b186a's own failing control — hoist one diagnostic local
out of `MR_TELEMETRY`, confirm the `-Wunused-variable` under `-DMESHROUTE_NO_TELEMETRY` and **0** with telemetry on,
then not ship it. **A census that was never made to fail is not evidence.**

---
## 6 — D2 and the RAM cost

**D2: NO, the wide sweep is not triggered — provided the implementation MEASURES it.** No `Node` member is added
(and, after §4.4.1(c), **no fallback member either**), so `sizeof(Node)` stays **221880** and its native
`static_assert` (`node.h:3352`) is the tripwire. No `node.h` reorder ⇒ no `-Wreorder` axis.

| item | measured / estimated | bytes |
|---|---|---|
| `TxQEntry::tag` (uint16) | ★ **MEASURED** on host + `arm-none-eabi` + `xtensa-esp32` + `xtensa-esp32s3`: 272 → 272, tag @270 | **0** |
| `TxQEntry::seq` (uint32) | ★ **MEASURED** on the same four: 272 → 276 | **+32** (×8) |
| `_inflight` latch (tag + seq + validity) | estimate | ≈ **+8** |
| outcome ring `TxOutcome[4]` + indices + `tx_outcome_drops` + `tx_failed_arms` | estimate (`sizeof(TxOutcome)` ≈ 24) | ≈ **+108** |
| **`DeviceHal` total** | | ≈ **+148** |
| `Node` | | **0** |
| `Push` / `_push_ring` | ★ a `PushKind` value only — **no new field** (§4.4.1(a)) | **0** |
| `PendingTx` | round-1 bit **withdrawn** as unnecessary | **0** |
| `UiModel` | three enum values in existing `uint8_t` enums | **0** |

⇒ ≈ **+148 B** of static RAM per board (`heltec_v3` is at **65.91 %**). ⚠ **A per-board RAM diff IS owed even though
`sizeof(Node)` did not move**, because `DeviceHal` is a **separate global** in `fw_main` and it grew. Board-gate
scope: the standing 2026-07-28 three-env ruling (`gateway` + `xiao_sx1262` + `xiao_esp32s3`) **plus**
`warning_census.sh` over the three OLED envs **plus** the RAM diff. ⛔ Flash will move and per b186a §6 board flash is
**not currently reproducible** to the byte ([[B63]]/[[B86]]/[[B138]]) ⇒ report Δ as an order of magnitude, not bytes.

---
## 7 — The test plan, and how each assertion could come out otherwise — **RE-DERIVED IN ROUND 2**

★★ Designed alongside the mechanism (§5's "instruments that cannot fail — 26 instances"). ⛔ **A control that cannot
redden is a failure of the control, not a pass.** ⚠ Probe labels are parsed at `%-64s`; **keep every new label ≤ 64
characters** or it silently drops out of the reddened roll-up (b186a and §UI-14 each lost controls this way).

### 7.1 Native — `test/test_device_hal.cpp` (the HAL half; the TU and its MockRadio already exist)

| # | assertion | could come out otherwise when |
|---|---|---|
| N1 | a frame handed with `tag = T`, `seq = S` yields exactly one `aired` carrying `T`/`S` after `poll_tx_done()`'s edge | the tag is dropped at enqueue (today's behaviour) ⇒ `0`/`0` |
| N2 | `start_transmit` → `radio_error` yields exactly one `failed{error = radio_error}` **and the entry is still popped** | move the pop inside the `ok` arm ⇒ the ring wedges; both halves are asserted |
| N3 | a started TX with **no** TxDone, driven past `1.5×airtime + 100 ms`, yields exactly one **`unknown`** — ⛔ never `aired`, never `failed` | collapse `unknown` into either ⇒ RED. **Decision 6's tripwire, now at the telemetry level** |
| N4 | **ordering**: three frames, the middle failing to arm ⇒ `aired, failed, aired` with the right tags | one mis-latched `_inflight` slot ⇒ wrong pairing |
| N5 | ⛔ **negative**: `tx()`'s ring-full and oversize rejects yield **NO** outcome (synchronous; never entered the ring) | push an outcome there ⇒ RED (it would double-report) |
| N6 | the outcome ring's **overflow is counted** — force > capacity 4 and assert `tx_outcome_drops()` | drop silently ⇒ RED (C2) |
| N7 | `on_radio_busy(BusyInfo)` behaviour is bit-identical after the adapter refactor: the ~35 existing direct invocations (`test_node_r3.cpp` ×20, `test_node_join.cpp` ×15) **stay green unchanged** | the body was not moved verbatim ⇒ some go red. **T1's whole gate** |

### 7.2 Native — the core arm

| # | assertion | could come out otherwise when |
|---|---|---|
| N8 | `aired` with `seq == flight_gen` of the live, locally-originated flight ⇒ exactly one `send_aired` push | drop the guard ⇒ a push per DATA |
| N9 | ★ `aired` with a **stale** `seq` (the flight was replaced between hand-off and completion) ⇒ **ZERO** pushes | ★★ **decision-3 tripwire.** Use 3A (tag only) and the wrong flight is confirmed ⇒ RED |
| N10 | `aired` for beacon / RTS / CTS / ACK / NACK ⇒ **ZERO** pushes, telemetry only | an unconditional push ⇒ the ring floods; assert ring depth |
| **N11** ★ **REWRITTEN (R2.1)** | **a FAILED ATTEMPT FOLLOWED BY A SUCCESSFUL RETRY.** Attempt 1 `failed` ⇒ telemetry + `tx_failed_arms` incremented + **ZERO pushes** and **no terminal state anywhere**; the MAC retry then airs ⇒ **exactly one `send_aired`** | route `failed` into the push ring, or into any terminal UI state ⇒ RED. ⛔ **This case is the false-negative mirror of B164 and is the single most important test in the plan** |
| N12 | ⛔ **negative — `failed` arms NO timer and consumes NO stash retry** (`retries_left` unchanged, `kRadioBusyRetryTimerId+slot` not armed) | ★★ **decision-2 / R2.8 tripwire.** Route `failed` into the stash retry ⇒ RED |
| **N13** ★ **NEW (R2.2)** | **monotonicity, both directions.** `aired` **after** a `failed` attempt still upgrades `QUEUED → SENT, waiting`; a later `failed`/`unknown` **never** downgrades an already-`aired` or delivered state | reinstate "first outcome per origination" ⇒ the first half goes RED; allow a downgrade ⇒ the second half goes RED |
| **N14** ⛔ **WITHDRAWN AND REPLACED IN ROUND 3 (R3.1, §12-k)** | ⛔ round 2's N14 asserted *"a **channel-M** DATA that airs ⇒ **ZERO** pushes"*. **That assertion was FALSE and it contradicted P1** — a canned team post transmits as channel-M, so N14 and P1 could not both pass honestly. Replaced by **N14a-N14e** below | — |
| **N14a** ★ **NEW (R3.1)** | a **locally originated channel-M** that airs ⇒ **exactly ONE** matching `send_aired`, carrying `dst = 0` and `ctr = entry.ctr` | reinstate the round-2 rule (`carrier_owes_send_failed`) ⇒ **RED**, and `ChanState::aired` is proved reachable rather than assumed |
| **N14b** ★ **NEW (R3.1)** | ★★ a channel handle **above 255** is **preserved EXACTLY** end to end (origination `ctr` 300 ⇒ push `ctr` 300) | push `pt.ctr` (the low byte) instead of `entry.ctr` ⇒ **RED at 44** — this is the whole reason `ChannelReofferPending::ctr` is 16-bit (§b40) |
| **N14c** ★ **NEW (R3.1)** | a **holder** re-offer that airs ⇒ **ZERO** pushes | drop the `entry.holder == false` clause ⇒ **RED**, and the push would carry `ctr = 0`, a meaningless handle |
| **N14d** ★ **NEW (R3.1)** | a **channel pull response** that airs ⇒ **ZERO** pushes | match any active slot regardless of ownership ⇒ RED |
| **N14e** ★ **NEW (R2.5, retained)** | a **forwarded DM** (`has_previous_hop`) that airs ⇒ **ZERO** pushes; a local DM that airs ⇒ **exactly one**, carrying `(pt.dst, pt.ctr)` | drop `!has_previous_hop` ⇒ RED: the companion would receive a completion for a send it never made |
| **N15** ★ **NEW (R2.4)** | **per-caller identity.** Drive `retry_stashed` with a **stale** stash slot while `_pending_tx` holds a **newer** flight ⇒ the outcome carries `TxStashSlot::flight_gen`, ⛔ **not** the current one, and produces **ZERO** pushes for the new flight | ★★ mutate the builder to read the current `PendingTx::flight_gen` ⇒ RED **with a false confirmation** — the exact defect R2.4 names |
| **N16** ★ **NEW (R3.2)** | ★★ **delayed airing after a terminal outcome.** Drive each terminal state first — `DELIVERED` · `NO RELAY HEARD` · `NO CONFIRM` · `BLOCKED` · `FAILED` — **then** deliver a late `send_aired` for the same handle ⇒ ⛔ **the terminal state is UNCHANGED in all five cases** | rank `aired` above any terminal state, or write it unconditionally ⇒ RED. ⓘ Five arms, not one: an enumerated-subset rank passes the two round 2 named (`DELIVERED`/`relayed`) and fails the other three |

### 7.3 `tools/probe_firmware_ui/run.sh` — ★ the only venue that runs the WHOLE chain

Links the real `DeviceHal` (`run.sh:70`), the real `lib/core`, the real `src/firmware_ui.cpp`, and a probe `IRadio`
whose `start_transmit` can fail; already calls `g_hal.service_tx()` (`probe_main.cpp:450`).

| # | assertion | mutation that must redden it |
|---|---|---|
| **P1** ★ **RE-DERIVED (R3.1)** | submit a **canned team post** ⇒ panel reads **`QUEUED`**; pump to the TxDone edge ⇒ **`SENT, waiting`**. ★★ **This is the case round 2's ownership rule made UNREACHABLE**, and it is now the positive half of N14a at the whole-chain level: the post transmits as **channel-M**, so it exercises the `_channel_reoffer_pending` path end to end | revert the `waiting` arm to `SENT, waiting` ⇒ the two states become indistinguishable ⇒ RED. ⛔ **Reinstate the round-2 `carrier_owes_send_failed` rule ⇒ the panel STALLS on `QUEUED` ⇒ RED** — this is the control that proves P1 and N14a now agree instead of contradicting |
| **P2** ★ **REWRITTEN (R2.1)** | **failed attempt, then a successful retry.** Script `start_transmit` → `radio_error` on attempt 1, then success on the MAC's retry ⇒ the panel goes `QUEUED` → **stays `QUEUED`** → `SENT, waiting`. ⛔ It must **NEVER** show a terminal failure in between | reinstate a terminal `NOT SENT` on `failed` ⇒ RED. ★ Also assert `tx_failed_arms == 1`, so the fact is proved *reported*, not merely *hidden* |
| **P3** ★ **REWRITTEN (R2.1)** | **lost TxDone, then a successful retry.** Script "arm, never complete", advance past the deadline (⇒ `unknown`), then let the retry air ⇒ same expectation, and `tx_timeouts() == 1` | reinstate a terminal `TX UNCONFIRMED` ⇒ RED; suppress the later `aired` ⇒ RED (this is P-level cover for N13) |
| P4 | ⛔ **negative**: a push of an unrelated kind moves neither new state | an over-broad `ui_route_send_push` arm ⇒ RED |
| P5 | ⛔ **negative**: with the completion drain deleted from the glue, P1 stalls on `QUEUED` | the control that proves P1 measures the **drain**, not the model |
| **P6** ★ **NEW (R2.6)** | the no-relay outcome renders **`NO RELAY HEARD`** and ⛔ the string `SENT, no relay` appears **nowhere** in the tree | keep the old string ⇒ RED; a grep-style assertion makes the absence load-bearing |
| **P7** ★ **NEW (R2.5)** | a `send_aired` whose `(dst, ctr)` does **not** match the live `SendTracker` slot moves **nothing** | drop the correlation ⇒ RED |

### 7.4 `tools/probe_board_ui/run.sh` — the call sites no host build compiles

A `W`-check that `src/fw_main.cpp` drains `pop_tx_outcome` into `on_tx_complete` **after** `service_tx()`, with four
reverts (deleted · moved before `service_tx` · the loop replaced by a single pop · routed to the wrong node method).

### 7.5 What CANNOT be tested here, stated rather than implied

* ⛔ **All 36 corpus streams are blind to `aired` / `failed` / `unknown`** (§4.1's 1A residue). The corpus's role is
  **purely a tripwire**: `s18` must reproduce **`9868cad3` / 269905** and 36/36 must sit at the six documented
  movers' published values. **Zero rows may move.**
* ⛔ **No real radio, no real TxDone edge, no real ring-full condition.** b186a §4.2's gap stays **unmeasured**.
* ⇒ **M2 obligations (this slice owes bench checks), written in `docs/2026-07-31-bench-test-script.md`:**
  1. the metal panel sequence **`SENDING... → SENT, waiting → DELIVERED to <label>`**, with the note that a brief
     `QUEUED` may appear between the first two and **may be too brief to see — its absence is NOT a failure**
     **[R2.8 decision]**; ⛔ the absence of `SENT, waiting` remains a failure;
  2. after a load burst, a `status` read reporting **`txq_drops`, `tx_timeouts`, `tx_failed_arms` and
     `tx_outcome_drops`** — ★ **[R2.8 decision] `tx_outcome_drops` must be REPORTED; zero cannot be assumed.**

---
## 8 — The comment/doc sweep the implementation OWES

| # | site (`48cd17d`) | what becomes false |
|---|---|---|
| 1 | `lib/core/node_carriers.h:392-410` | *"nothing consumes the true 'aired' fact"* / *"the completion-signal option is DEFERRED"* — it now exists and has one consumer. ⛔ The flag **keeps** its name; say so, or the next reader renames it back |
| 2 | `lib/core/node_mac.cpp:2020-2035` | *"establish it there the moment something genuinely needs 'aired'"* — that moment is now |
| 3 | `lib/core/node_mac.cpp:2088-2098` | *"option (b) stays DEFERRED"* — landed |
| 4 | `lib/core/node_mac.cpp:2103-2109` | the prohibition *"NEVER approximate it by adding the assignment at B164's two retry sites"* — **still binding**; record that the design honoured it |
| 5 | `lib/core/node_mac_rx.cpp:176-190` | *"`pump_tx()`'s failed arm can drop the frame afterwards and nothing can unset the flag"* — the drop is now reported (the flag is still not unset, deliberately) |
| 6 | `lib/hal/device_hal.cpp:32-33` | *"A failed arm drops the frame (rare radio_error; not retried here)"* — still not retried, no longer silent |
| 7 | `lib/hal/device_hal.h:100-104` | the `TxQEntry` block must state that tag/seq are **carried**, and why (finding A) |
| 8 | `lib/core/node.h:1910-1911` **and** `:2425-2427` | both say a `pump_tx` error is *outside* the transmitter-admitted guarantee. ⛔ **Still true; the boundary is NOT moved [R2.8 decision].** Both must add: the event is now **observable**, and moving the boundary is a separate decision |
| 9 | `docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md:534-537, 561` | the verbatim `SENDING... → SENT, waiting` sequence — ⛔ **must be updated in T3**, written per §7.5's transient-`QUEUED` rule |
| **10** ★ **NEW (R2.6)** | `src/firmware_ui.cpp:766` + `firmware_ui_model.h`'s `channel_no_relay` note | the `SENT, no relay` string and any comment that reasons about it |
| **11** ★ **NEW (R2.3)** | `lib/core/command.h` `PushKind` block + the companion contract doc | the additive `send_aired` event, and **why it is appended** (the cross-repo `join_adopted == 13` assert at `command.h:229-232`) |
| **12** ★ **NEW (R2.4)** | `lib/core/node_mac.cpp:1886-1900` (`retry_stashed`) | that it now supplies `TxStashSlot::flight_gen` explicitly, and ⛔ **why the builder must never derive it** |
| **13** ★ **NEW (R3.1)** | `lib/core/node_carriers.h:514-527` (`carrier_owes_send_failed`) | ⛔ **nothing about it becomes false — it must be left EXACTLY as it is.** What the sweep owes is a one-line warning at the site: it answers *"does dropping this carrier owe a `send_failed`?"*, ⛔ **NOT** *"does this carrier own an app future?"* — this design misread it that way once, and the misreading made `ChanState::aired` unreachable |
| **14** ★ **NEW (R3.1)** | `lib/core/node.h:1867-1880` (`ChannelReofferPending` + its `static_assert`) | that `id`/`ctr`/`holder` are now **read** by the `send_aired` path, and ⛔ that the record must **not** be grown |

⛔ **NOT IN THIS SWEEP AND NOT THIS DESIGN'S TO CLAIM (R3.3):** the `duty_defer_fire` comment fix at
`lib/core/node_mac.cpp:1872` (it says it *mirrors `retry_stashed`*, which is false — `retry_stashed` has no
pre-transmit flight guard; only its post-tx ACK re-arm at `:1904` is guarded) and the lifting of the design-only
prohibition **both belong to the T1 implementation brief, which the QA-gate writes.** They are named here only so
nobody picks them up out of this spec by mistake.

Plus: `simulation/BASELINE.md` gains a slice note (⛔ **not** an edit to the `### 36/36 corpus` anchor table);
`docs/2026-07-31-bench-test-script.md` gains §7.5's two checks (M2); the B164/B189 register rows are updated (M1);
`docs/protocol.md` gains the TX-completion contract — ⛔ **not** `docs/frames.md`, which is wire-layout only.

---
## 9 — ⛔ WHAT THIS DESIGN DOES **NOT** DO — **RE-DERIVED IN ROUND 2**

1. ⛔ **No wire change, no `wire_version` bump, no NV/schema change.**
2. ⛔ **No timer.** `kCap` stays 91, no id allocated (§4.7).
3. ⛔ **No `Node` growth**, and after §4.4.1(c) **no fallback member either** ⇒ `sizeof(Node)` stays 221880. ⚠ To be **measured**.
4. ⛔ **No retry, no recovery, no re-send** on `failed` or `unknown` — **[R2.8 decision] no B186b.**
5. ⛔ **No change to the R4.5b stash-retry machinery.**
6. ⛔ **No simulator change and no corpus movement** (1A). **[R2.8 decision] simulator `aired` is deferred to its own attributed slice.**
7. ⛔ **No edit to the `### 36/36 corpus` anchor table**; no re-anchor proposed.
8. ⛔ **`data_ever_admitted` is NOT renamed, NOT re-purposed, NOT given a second writer.**
9. ⛔ **[R2.8 decision] the owner-ruled transmitter-admitted boundary for the channel digest is NOT moved.**
10. ★ ⛔ **NO TERMINAL UI STATE IS DERIVED FROM AN ATTEMPT OUTCOME** (R2.1). `failed`/`unknown` never reach the panel.
11. ★ ⛔ **`failed` and `unknown` NEVER enter the app push ring** (R2.1/R2.3).
12. ★ ⛔ **No `RETRYING` state is added** — recorded in §4.2 as the correct future shape, not designed or built.
13. ★ ⛔ **No new `Push` field** and ⛔ **no `PushKind` renumbering** — appended only (R2.3).
14. ★ ⛔ **No new ownership authority is invented** — the DM path uses the carriers' own `m_broadcast`/`has_previous_hop`, the channel path uses the **existing** `_channel_reoffer_pending` slot (R3.1, U1). ⛔ **And `carrier_owes_send_failed` is NOT reused for it** — it answers a different question (§4.4.1(b)).
15. ★ ⛔ **The `TxParams` builder derives NOTHING** — identity is an explicit per-caller argument (R2.4).
16. ★ ⛔ **`ChannelReofferPending` is READ, never GROWN** — `node.h:1876`'s `static_assert` (sizeof 12, `offsetof(id) == 4`, `offsetof(ctr) == 8`) must still hold, or `sizeof(Node)` moves and D2's ten-env sweep triggers (R3.1).
17. ★ ⛔ **The channel handle is never truncated** — `entry.ctr` is 16-bit and travels whole; ⛔ `pt.ctr` (the low byte, `channel_msg_id_mint`) is NOT the UI handle for a channel post (R3.1).
18. ★ ⛔ **No terminal state is ever overwritten by a late `aired`** — the rank covers **every** terminal outcome, not an enumerated subset (R3.2).
19. ⛔ **The `duty_defer_fire` comment fix and the lifting of the design-only prohibition are NOT in this spec's scope** — both belong to the T1 implementation brief the QA-gate writes (R3.3).
20. ⛔ **[[B193]] does not close**; ⛔ **Phase A is not complete.**
21. ⛔ **b186a §4.2's gap stays open** — how often a real radio fills the ring remains unmeasured.
22. ⛔ **No claim that the sim and metal refusal paths lead to the same protocol outcome** — b186a records it UNVERIFIED and it stays so.

---
## 10 — Slicing — **[R2.8 decision] the slicing stands: T1 → QG, T2 → QG, T3 → QG**

| slice | content | rule | gate |
|---|---|---|---|
| **T1 — REFACTOR ONLY** | `TxOutcome` + `Node::on_tx_complete`; `on_radio_busy`'s body moved **verbatim**; the adapter; the `TxParams` builder **taking identity as an argument** (§4.3) routed through all four hand-build sites | C1: **no behaviour change** | native **1615/82362/0** unchanged · `s18` **`9868cad3` / 269905** EXACT · 36/36 at published values · a normalized verbatim diff of the moved body · N7 |
| **T2 — the HAL path (feature)** | `TxQEntry` tag/seq, `_inflight`, the outcome ring (cap 4 + drop counter), `pop_tx_outcome`, the three producers, `fw_main`'s drain, §3.6's two discarded refusals, the two new counters | C2: overflow counted | N1-N7 with their mutations · `s18` EXACT (⚠ `lib/core` moves ⇒ **measure**, don't argue) · 3 board envs + `warning_census.sh` at its pins + a per-board RAM diff |
| **T3 — app/UI semantics (feature)** | `PushKind::send_aired` (appended), the push rule incl. `carrier_owes_send_failed`, `ui_route_send_push`'s explicit arm + `(dst,ctr)` correlation, the monotonic rank, the three changed strings, the two added states, the bench-guide + companion-contract updates | the owner instruction: the OLED consumes the core's fact | N8-N15 · `probe_firmware_ui` P1-P7 with controls · `probe_board_ui` W-check · the bench-guide update per §7.5 |

⚠ T2 and T3 both touch `lib/core` ⇒ **D1/D2 apply in full to each**, and `s18` byte-identity must be **measured**
after each. Slices may be gated together but must remain **separately attributable**.

---
## 11 — Stop-and-report items, and what remains open

| trigger | status |
|---|---|
| the design needs a **timer** | ⛔ **NO** (§4.7) |
| a **wire change** | ⛔ **NO** (§9.1) |
| an unjustifiable **`Node` growth** | ⛔ **NO** — and round 2 **removed the fallback**: with the monotonic rank in the UI model there is no core bit at all (§4.4.1(c)) |
| §2's measurements **wrong** | ⚠ **YES, THREE**, reported in §2.2 / §2.3 / §2.4. ⛔ The design is built on the corrected table |

### Open questions

1. **Whether option 1C is ever taken** — **[R2.8 decision] deferred to its own attributed corpus slice.** Until then
   the three new outcome kinds are permanently corpus-dark; that limit is recorded, not resolved.
2. **How often a real radio fills the 8-entry ring** — unmeasured (b186a §4.2); no gate in this design can measure
   it. ★ §7.5's bench check reports `txq_drops` and `tx_outcome_drops`, which is the first measurement that will
   exist — **and [R2.8 decision] zero cannot be assumed.**
3. ⓘ **CLOSED IN ROUND 2** — round 1 left *"should `SENT, no relay` follow?"* open. **R2.6 answers it: yes, by the
   conservative rename, in T3** (§4.5).
4. ⓘ **CLOSED IN ROUND 2** — round 1 left the channel-digest boundary open. **[R2.8 decision] it is not moved.**

---
## 12 — ★ ROUND-2 CHANGE INDEX — every round-1 claim that was WITHDRAWN, and where it now stands

⛔ **Nothing was deleted; each is corrected at its own site** (§3 rule 3). This index exists so a reader who saw the
round-1 claim can find its withdrawal.

| id | round-1 claim | status | where |
|---|---|---|---|
| **a** | `failed` → panel **`NOT SENT`**; `unknown` → panel **`TX UNCONFIRMED`**; both terminal | ⛔ **WITHDRAWN (R2.1)** — terminal renderings of non-terminal attempt facts; the false-negative mirror of B164. Now telemetry + counters only | §4.2, §4.5, §4.6 |
| **b** | *"introduce one `TxParams` builder and route all four sites through it"* (silent on where `seq` comes from) | ⚠ **AMENDED (R2.4)** — the builder must **never derive** identity; it is an explicit per-caller argument, and `retry_stashed` has **no pre-transmit flight check** to derive from | §4.3 |
| **c** | the push carries *"`ctr` … **and the outcome kind**"*; zero-byte cost | ⛔ **WITHDRAWN (R2.3)** — `Push` has no outcome-kind member. Corrected: **only `aired` is pushed**, so the `PushKind` **is** the kind; it rides existing `ctr`/`dst`; **no new field**, and the zero-byte claim now holds for a reason that is true | §4.4.1(a), §6 |
| **d** | *"at most one push per origination, on the **first** outcome"* | ⛔ **WITHDRAWN (R2.2)** — could suppress a later `aired`. Replaced by monotonic evidence `queued < aired < delivered/relayed` | §4.4.1(c) |
| **e** | fallback *"one bool in `PendingTx` at a measured +0"* for de-duplication | ⛔ **WITHDRAWN** — unnecessary once the rank is monotonic. `sizeof(Node)` is unmoved **with no fallback at all** | §4.4.1(c), §6, §11 |
| **f** | `SENT, no relay` retained because it is *"a different state machine"* | ⛔ **WITHDRAWN (R2.6)** — it is the **same `ChanState`, same renderer**, so retaining it contradicted the design's own rule. Renamed **`NO RELAY HEARD`** in T3 | §4.5 |
| **g** | the push needed only `flight_gen` correlation | ⚠ **AMENDED (R2.5)** — `FrameTag::data` also covers channel-M and forwarded transit; the existing `carrier_owes_send_failed` (`node_carriers.h:527`) is required, plus `(dst,ctr)` correlation | §4.4.1(b) |
| **h** | tests **P2 / P3 / N11** written around a terminal failure display | ⛔ **REWRITTEN (R2.1)** — all three are now *"a failed attempt followed by a successful retry"*; N13/N15/P6/P7 added | §7 |
| **i** | ★★ round 2 used **`carrier_owes_send_failed`** as the ownership predicate and reported the channel-M exclusion as *"STRICTLY STRONGER"* | ⛔⛔ **WITHDRAWN (R3.1) — IT WAS EXACTLY BACKWARDS.** The predicate answers *"does dropping this owe a **`send_failed`**?"*, and it excludes `channel_m` **deliberately** because a channel post's future is `channel_sent`, owned by `_channel_reoffer_pending` (`node_carriers.h:518-520`). ⇒ the round-2 rule made **`ChanState::aired` unreachable** and put **P1 and N14 in direct contradiction**. Replaced by **two distinct ownership paths** | §4.4.1(b) |
| **j** | the rank written as *"`queued < aired < delivered/relayed`"* | ⚠ **EXTENDED (R3.2)** — only two terminal outcomes were named, so a **delayed `aired` could OVERWRITE** `NO RELAY HEARD`, `NO CONFIRM`, `BLOCKED` or `FAILED`. Now **`queued < aired < EVERY logical terminal outcome`**, defined over the whole state domain, not a subset | §4.4.1(c) |
| **k** | test **N14** (*"a channel-M DATA that airs ⇒ ZERO pushes"*) and **P1** as written | ⛔ **N14 WITHDRAWN (R3.1)** — it asserted a falsehood and **contradicted P1**; ★ *two tests disagreeing about the same fact is the instrument-level form of this arc's defect class.* Replaced by **N14a-N14e**; **P1 re-derived** with the round-2 rule as its own negative control; **N16** added for R3.2 | §7 |
