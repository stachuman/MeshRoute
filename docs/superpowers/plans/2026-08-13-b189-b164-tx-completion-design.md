<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B189 + §B164 — one authoritative HAL TX-completion path · **DESIGN-ONLY** dispatch brief · 2026-08-13

**Status: DISPATCHED 2026-08-13 on an owner ruling — the FINAL pre-metal firmware slice.** ★ Role split: the QA-gate
wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**
⛔⛔ **THIS ROUND PRODUCES A DESIGN SPEC AND NOTHING ELSE (P2). ⛔ WRITE NO FIRMWARE.** The owner's instruction is that
you *first prepare a narrow combined design*; it is reviewed before any code. ⛔ **Do not patch the OLED tracker
independently** — that prohibition is the owner's and it is the reason the two bugs are one slice.
⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out another commit here** —
the tree carries five uncommitted slices.

**Deliverable:** `docs/superpowers/specs/2026-08-13-tx-completion-path-design.md`. **Baseline:** HEAD **`24d8931`**;
native **1615 / 82362 / 0**; `lus` **`43a7b6eb`**; `sizeof(Node)` **221880**; `TimerWheel::kCap == 91`, **all ids
consumed**.

---
## 1 — The owner's framing, verbatim in substance

- **[[B189]]:** hardware never calls `Node::on_radio_busy`; synchronous queue refusal and `pump_tx()` failure can
  silently drop an admitted frame.
- **[[B164]]:** the UI/core knows *"accepted into the queue"*, but not whether the frame actually reached the radio.
- **The shape:** **ONE authoritative HAL completion path for `aired`, `busy/retry`, and `failed/drop`.**

---
## 2 — The code state, measured before dispatch. ★ Re-verify every line (V1) — this is my measurement, not a ruling.

### 2.1 The five post-`tx()` outcomes on hardware, and who can see them

| # | path | `file:line` | what happens today | protocol told? |
|---|---|---|---|---|
| 1 | `tx()` oversized | `lib/hal/device_hal.cpp:14` | returns `TxResult::too_long` | **yes — synchronously, at the call site** |
| 2 | `tx()` ring full | `:15` | returns `busy`, `_txq_drops++` | **yes — synchronously** |
| 3 | `pump_tx()` `start_transmit` ≠ ok | `:38-46` | the entry is **popped and dropped**; the pop at `:45-46` runs **regardless of `r`** | ⛔ **NO** |
| 4 | `service_tx()` watchdog | `:52-55` | `abort_tx()` + `_tx_timeouts++` on a lost TxDone | ⛔ **NO** |
| 5 | **normal completion** | `poll_tx_done()` at `:52` | RX re-armed, nothing else | ⛔ **NO — and this is [[B164]]'s missing fact** |

⇒ **rows 3, 4 and 5 are the whole slice.** Row 5 is not an error path and must not be designed as one: *aired* is a
**fact the protocol currently cannot learn**, which is exactly why `data_ever_admitted` had to be renamed away from
`data_ever_transmitted` (`lib/core/node_carriers.h:392+`).

### 2.2 ⛔⛔ TWO FINDINGS THE FRAMING DOES NOT NAME, AND THE DESIGN TURNS ON BOTH

**A · `TxQEntry` DOES NOT CARRY THE TAG.** `lib/hal/device_hal.h:104` is
`{ uint8_t buf[255]; uint16_t len; int16_t sf; int32_t bw; int8_t cr; int16_t pre; int8_t pw; }` — **`TxParams::tag`
is consumed at `tx()` and then DROPPED.** ⇒ **today's `DeviceHal` structurally cannot name which frame completed,
failed, or was dropped**, because the identity the whole `BusyInfo` contract is built on does not survive the
enqueue. ★★ **This is the load-bearing gap, and no completion path can be built over it without fixing it first.**
ⓘ Cost, for scale: `kTxQCap == 8`, so carrying a `uint16_t` tag is **+16 B RAM**, and `heltec_v3` is at 65.91 %.

**B · THREE `_hal.tx()` CALL SITES DISCARD THE `TxResult` ENTIRELY** — `lib/core/node_mac.cpp:1533`, `:1894`
(`retry_stashed`) and the `:1533` beacon path; only `:1581` and `:1812` test it (`if (tr != TxResult::ok)`).
⇒ **the owner's *"synchronous queue refusal … can silently drop an admitted frame"* is exact for those sites: the
refusal IS returned and is then THROWN AWAY.** ★ So B189 has **two independent halves** — *no asynchronous callback
exists on metal* **and** *the synchronous answer that does exist is ignored at three sites* — and a design that fixes
only the callback leaves rows 1–2 silent at those three call sites. **Say which half each part of your design closes.**

### 2.3 What already exists and must be reused, not re-invented (U1/U2)

- `Node::on_radio_busy(const BusyInfo&)` — `lib/core/node.h:94`, defined `lib/core/node.cpp:2191`. It already
  demultiplexes by `frame_tag_of(info.tag)`, clears `awaiting_ack`, cancels `kAckTimeoutTimerId`, and drives the
  R4.5b stash retry (`retry_slot_of`, `TxStashSlot::retries_left`, `tx_giveup`).
- `struct BusyInfo { BusyReason reason; uint16_t tag; int16_t sf; uint64_t busy_until_ms; }` (`lib/core/hal.h:40`)
  and `enum class BusyReason { channel_busy, self_tx_in_flight, oversized, duty_cycle_exceeded }` (`:24`).
  ⚠ **`BusyReason` has no member for `radio_error`, a watchdog abort, or a successful airing** — establish whether
  your design extends this enum or introduces a separate outcome type, **and say why**.
- `TxResult { ok, busy, too_long, radio_error }` (`lib/core/hal.h:23`).
- **The B186a HAL reachability audit already enumerated the implementations** — `docs/superpowers/evidence/b186a/README.md`
  §4. ⛔ **Read it before writing a line of the design; do not re-derive it, and do not contradict it silently.**

---
## 3 — What the design must decide, EXPLICITLY, with options and a recommendation

⛔ **Do not silently pick any of these. Each is a decision the owner may want to make.**

1. ★★ **THE CORPUS QUESTION, AND IT IS THE BIG ONE.** The simulator HAL calls `on_radio_busy`; hardware will call
   your new path. **If the sim is NOT migrated, the two implementations diverge again — which is [[B189]]'s own
   defect wearing a new name.** If the sim IS migrated, **all 36 corpus rows may move**, and per **C4** an
   attribution-destroying re-anchor **gets its own slice/commit**. ⇒ **present both, recommend one, and state exactly
   what moves.** ⛔ **You may not edit the `### 36/36 corpus` anchor table under any option.**
2. ★★ **OBSERVABILITY versus RECOVERY, drawn as a hard line.** *Aired / busy / failed* are **facts**. Retrying a
   failed frame is **recovery** — [[B186b]] territory, which a previous owner ruling kept out of scope. ⇒ **state
   for every outcome whether the design REPORTS it or ACTS on it**, and ⛔ do not smuggle a general retry mechanism
   in behind row 3 or row 4.
3. **Where the identity lives** (finding A): the tag in `TxQEntry`, or another carrier. **One conversion path** (U2).
4. **What the core does with `aired`** — B164's actual ask. ⚠ If this becomes new `Node` state, **`sizeof(Node)`
   moves and D2 triggers**: the ten-env sweep + `warning_census.sh` + `-Wreorder` + the `sizeof` asserts + per-board
   RAM/flash. **Prefer deriving over storing, and say which you chose.**
5. **The UI half.** ⛔ **The OLED must consume the core's fact, NOT its own tracker** (owner). State exactly which
   rendered strings change and which do not — ⚠ `src/firmware_ui.cpp:750`/`:764` render **`"SENT, waiting"`** on
   admission today, and §2.1's §B69 note at `:769-772` already argues that saying SENT without evidence is the
   false-confirmation defect. **Whether `aired` may now say SENT is a design claim, so make it and justify it.**
6. **Row 4, the watchdog.** A frame aborted after `start_transmit` succeeded **may have partly aired**. ⇒ *aired*,
   *failed* and *unknown* are **three** states, and collapsing them to two is this arc's five-instance
   binary-test-over-a-ternary-domain defect. ★★ **Name the third state.**
7. **Timers: ZERO are available** (`kCap == 91`, all consumed). The completion path must ride `service_tx()`, which
   the main loop already calls. ⛔ **If your design needs a timer, STOP and report it** — that is an owner decision.

---
## 4 — Constraints that are already ruled and are NOT open

- ⛔ **No `wire_version` bump and no wire change.** This is a HAL/core-internal completion path; if you believe it
  needs a wire change, **stop and report** (M3/C4: a bump is free to deploy but gets its OWN slice for attribution).
- ⛔ **[[B193]] does not close** — no NVS/LittleFS write, wear or reset-during-write is exercised by any gate here.
- ⛔ **Phase A is not complete** — [[B164]]/[[B189]] are its gate, which is why this slice exists.
- ⚠ **`MESHROUTE_NO_TELEMETRY` strips `MR_TELEMETRY` bodies on board envs** ([[B169]]): a variable consumed only
  inside an `MR_EMIT` becomes `-Wunused-variable` on all ten board envs, **invisible to native AND to all 36 corpus
  streams**. Your design will add emits. **Say how it avoids this.**

---
## 5 — Method

- ★★ **A fact is established by the physical act, never reconstructed later** — this arc's most-repeated class, and it
  is the entire subject of this slice: `_hal.tx()` returns `ok` on **ENQUEUE**, so `handed` means **queued, never
  aired**. **Capture identity at hand-off; do not rebuild it from FSM state at completion time.**
- ★★ **Name the third state** (§3.6). Five-plus instances this session.
- ★★ **Instruments that cannot fail — 26 instances**, the newest being a tripwire built to prevent one. **Design the
  tests alongside the mechanism and state how each could come out otherwise.**
- ★ **A correction placed anywhere but the instruction a reader follows** — the last three rounds each named fewer
  sites than existed. Your design will falsify several standing comments (`node_carriers.h:392+`, `node_mac.cpp:2020+`,
  `node.h:1910`/`:2425`, `node_mac_rx.cpp:176-187`). **Enumerate them in the spec as the sweep the implementation
  owes.**
- ⛔ **PROVENANCE (ledger §3):** never claim an owner or QA approval; **never quote an owner ruling** — reported form
  only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**
- **M1:** findings A and B above, and anything you add, **land in the register** with their measurement — B164's and
  B189's rows are the place. **A bug found and not registered is a bug found twice.**

**Deliver:** the spec at the path in the header, containing — §2's state table **re-verified by you, with corrections**
· the completion path's exact shape (types, verbs, `file:line` of every seam it touches) · **each of §3's seven
decisions, with options, a recommendation and the reasoning** · the corpus impact under each option · the D2 answer
· the RAM cost · the test plan, native and probe, with how each new assertion could fail · the comment/doc sweep the
implementation will owe · **and ⛔ an explicit list of what the design does NOT do.**

**Stop and report rather than designing around it if:** the design needs a timer · a wire change · a `Node` growth you
cannot justify · or if §2's measurements are wrong (⇒ **report the correction; do not build on my table**).

---
---
# ROUND 2 — QG HOLD before implementation (relayed by the owner 2026-08-14). DESIGN REVISION ONLY; still NO firmware.

⚠ **QG's findings relayed by the owner — a recommendation, not an owner ruling** (ledger §3 rule 5), EXCEPT the six
items under R2.8, which the owner states as decisions for you.
✅ **QG confirms: the HAL completion path is well motivated and the three-slice structure is good.** ⛔ What is wrong
is the **UI outcome semantics**, which currently **confuse a PHYSICAL TX ATTEMPT with the LOGICAL SEND.**

★★★ **THE ONE-LINE SIMPLIFICATION, and every item below is a consequence of it:**
**the HAL reports ALL attempt outcomes to CORE TELEMETRY, but ONLY a positively observed `aired` becomes a new UI push.**

## R2.1 — ⛔⛔ BLOCKER: `failed` and `unknown` are NOT TERMINAL send outcomes

A queued frame can fail in `pump_tx()` or lose TxDone **while the existing ACK-timeout machinery then retries the same
flight.** The spec says recovery stays active and then makes the FIRST failed attempt terminal (`spec:230`, `:331`).
**This sequence is possible:** attempt 1 `start_transmit` fails → UI says **`NOT SENT`** ← *false* → MAC times out and
retries → attempt 2 **airs and succeeds**.
★★ **Note the shape: that is the FALSE-NEGATIVE mirror of the very defect this design exists to prevent.** Saying
`NOT SENT` about a frame that then flies is exactly as wrong as saying `SENT` about one that never did.

**Required:**
- `aired` — **app-visible**, upgrades `QUEUED` → `SENT, waiting`.
- `failed` and `unknown` — **telemetry and counters ONLY, for now.**
- The existing `send_failed`, ACK timeout, `channel_sent` etc. **remain the authoritative logical terminal outcomes.**
- ⛔ **Do NOT introduce the B186b stash retry** — but **do acknowledge in the spec that the existing MAC recovery still
  runs**, since that is what makes attempt-level silence safe.
- If attempt failures must ever be displayed, they need **explicitly NON-TERMINAL** states (e.g. `RETRYING`) that a
  later `aired` or delivery outcome can **upgrade**.
- **Rewrite tests P2 / P3 / N11 around "a FAILED ATTEMPT FOLLOWED BY A SUCCESSFUL RETRY."**

## R2.2 — ⛔ BLOCKER: "first outcome per origination" is unsafe

`spec:283`'s rule **could suppress a later `aired` after an earlier `failed`/`unknown` attempt.** ⇒ replace it with
**MONOTONIC EVIDENCE**: `queued < aired < delivered/relayed`. **A weaker attempt outcome must NEVER prevent a later
stronger fact from being consumed.** ⓘ This is the same ordering discipline the marker/latch work used — an upgrade
is allowed, a downgrade is not.

## R2.3 — ⛔ BLOCKER: the proposed `Push` cannot carry the outcome kind (verified)

`spec:283` proposes one `PushKind::send_aired` *"carrying `ctr` … and the outcome kind"*. ⛔ **`struct Push`
(`lib/core/command.h:293`) has NO such member** — I re-verified: `kind`, `reason`, `join_reason`, `origin`, `dst`,
`channel_id`, `layer_id`. ⇒ **the claimed zero-byte cost is NOT established.** The clean resolution is also the safest:
- **add ONLY `PushKind::send_aired`;**
- ⛔ **do NOT route `failed`/`unknown` through the app push ring** (which R2.1 already requires);
- **carry the existing `ctr` and `dst`** ⇒ **no new `Push` field is needed at all**;
- ★ **APPEND the enum value** — `PushKind` is **contract-visible** and the app may persist it, so nothing is
  renumbered (the same rule the `SendFailReason` additions followed, `command.h:~287`).
- **Document the JSON/companion-facing additive event.**

## R2.4 — ⛔ BLOCKER: the shared `TxParams` builder must NOT reconstruct `seq` from current state

⓵ **Verified at the code:** `retry_stashed` (`lib/core/node_mac.cpp:1887-1894`) **transmits its stored frame with no
flight check at all** — the exact-match guard `_active->_pending_tx->flight_gen == s.flight_gen` lives in a
**different** function (`duty_defer_fire`, `:1879`). ⇒ **if the shared builder reads the CURRENT
`PendingTx::flight_gen`, a stale retry is labelled as the NEW flight and FALSELY CONFIRMS it** — the field-drop /
reconstructed-fact class this whole design exists to prevent.

**Require EXPLICIT identity input** (⛔ never derived at the builder):
| caller | identity |
|---|---|
| ordinary DATA | the current `PendingTx::flight_gen` |
| `retry_stashed` | **`TxStashSlot::flight_gen`** — ⓘ it already exists and `:1879` already reads it |
| deferred carriers | the identity **stored in that carrier** |
| unrelated frames | zero |

## R2.5 — ⛔ BLOCKER: the app push needs an ORIGIN-OWNERSHIP predicate

⓵ **Verified:** `FrameTag::data` covers ordinary DMs, **channel M frames** (`lib/core/node_mac.cpp:1945` hands
`pack_m` output with `FrameTag::data`) **and forwarded traffic.** ⇒ an exact `flight_gen` proves **which local flight
completed**, but **not that it belongs to a LOCAL UI ORIGINATION.**
**Define and TEST the EXISTING carrier predicate that excludes transit** — ⓘ **do not invent one (U1): it is
`PendingTx::has_previous_hop` (`lib/core/node_carriers.h:374`), surfaced as `TxItem::is_forward` (`:505`), and
`node_carriers.h:521` already documents *forwarded* as the transit class.** At minimum the equivalent of
`!has_previous_hop`, **plus exact `ctr`/`dst` correlation in `SendTracker`.**

## R2.6 — ⛔ BLOCKER: `SENT, no relay` contradicts the design's own central rule

`spec:318` states *"nothing weaker than `aired` may say SENT"*, and `spec:342` then **deliberately retains the
admission-derived `SENT, no relay`.** ⛔ **It is the SAME `ChanState` and the SAME renderer — not an unrelated state
machine.** Either **(a)** change it now to a conservative **`NO RELAY HEARD`**, or **(b)** remember whether `aired`
was observed and say `SENT, no relay` **only in that case.** ⚠ **QG recommends (a), the conservative rename, in T3.**

## R2.7 — What R2.1–R2.6 mean for the rest of the spec
Re-derive rather than patch: §4.2's hard line, the UI string table, the test plan (P2/P3/N11 rewritten; new coverage
for the monotonic ordering, the ownership predicate and the per-caller identity), and the "does NOT do" list.

## R2.8 — ★ DECISIONS THE OWNER STATES FOR YOU (not open)

- **Bench-guide change: APPROVED in T3** — ⛔ **but do NOT require a human to visually observe a transient `QUEUED`.**
  **Automated tests pin the exact state transition; the metal guide must ALLOW it to be too brief to see.**
- ⛔ **New B186b recovery: DO NOT ADD IT.**
- **Simulator producing `aired`: DEFER** — it deserves its own attributed corpus slice.
- ⛔ **Channel digest boundary: DO NOT MOVE IT in this work.**
- **Outcome ring: capacity FOUR plus a DROP COUNTER is reasonable.** ★ **The metal load test must report
  `tx_outcome_drops` — ⛔ zero cannot be assumed.**
- **Slicing stands: T1 refactor → QG, T2 HAL path → QG, T3 app/UI semantics → QG.**

## R2.9 — Round-2 obligations
⛔ Still **DESIGN ONLY — write no firmware.** Revise the spec in place, ⛔ **correcting withdrawn claims rather than
deleting them** (§3 rule 3), and **update [[B164]]/[[B189]]'s register rows** where R2.1–R2.6 change what is owed
(M1). ⛔ No commit, no `git add`, no `git checkout`. ⛔ **[[B193]] does not close. Phase A is not complete.**
⚠ HEAD is now **`48cd17d`** (the owner committed UI-14/B194); re-read any `file:line` you cite.

---
---
# ROUND 3 — QG blocker (relayed by the owner 2026-08-14). ⛔ STILL DESIGN-ONLY. **Do not start T3 from the spec yet.**

⚠ QG's findings relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5).
✅ **The other six round-2 corrections are ACCEPTED**, including the explicit stash identity and the attempt-vs-send
semantics. ⛔ **T1 is mechanically independent, but the design is corrected FIRST.**

## R3.1 — ⛔⛔ BLOCKER: under the round-2 rule, a CHANNEL SEND CAN NEVER BECOME `aired`

★★★ **THE SHAPE OF THE MISTAKE, and it is worth naming because it is a recurring one: the predicate was reused for a
question it does not answer.** `carrier_owes_send_failed` (`lib/core/node_carriers.h:514`) does **NOT** mean *"owns any
app future"*. It means exactly *"dropping this carrier owes a **`send_failed`** push"* — and it excludes `channel_m`
**deliberately**, because a channel post's future is a **different** one: `channel_sent`, owned by
`_channel_reoffer_pending` (its own doc says so at `:518-520`). ⇒ round 2 adopted it as *stronger*; for **this**
question it is **wrong**, and the round-2 report's claim that excluding channel-M was a feature is **exactly
backwards** for the `aired` push. ⛔ **Withdraw that claim in place, do not delete it** (§3 rule 3).

**Verified consequences of `spec:320` as written:** canned channel posts and emergencies **physically transmit as
channel-M** ⇒ the rule **rejects every channel-M completion** ⇒ **`ChanState::aired` is unreachable** ⇒ it
**contradicts P1** (a canned team post becoming `SENT, waiting`) while **agreeing with N14** (every channel-M
completion produces zero pushes). ★★ **P1 and N14 CANNOT BOTH PASS HONESTLY — that is the tell, and a test plan in
which two tests disagree about the same fact is the instrument-level form of this arc's defect.**

**The second half, also verified:** `PendingTx::ctr` for a channel-M carries **only the low byte** derived from the
channel message id (`channel_msg_id_mint`, `node_channel.cpp:53-54`: `origin<<24 | (key_hash32 & 0xffff)<<8 | ctr8`).
**The full UI correlation handle lives in `ChannelReofferPending::ctr`** (`lib/core/node.h:1872`) — a **16-bit**
field whose own comment states it exists *"so `channel_sent` can be correlated past 255 posts"*, is *"a LOCAL
correlation handle only"*, and is **`0` on a holder slot (a relay owns no origination)**.

### The required correction — TWO distinct ownership paths
| carrier | predicate | push |
|---|---|---|
| **ordinary local DM** | `!m_broadcast && !has_previous_hop` | `dst = pt.dst`, `ctr = pt.ctr` |
| **locally originated channel-M** | decode `channel_msg_id` from `pt.inner`; find the active `_channel_reoffer_pending` entry with `entry.id == channel_msg_id` **and `entry.holder == false`** | `dst = 0`, **`ctr = entry.ctr`** (the full 16-bit handle) |
| **forwarded DM · pull response · relay/holder channel-M** | — | ⛔ **no `send_aired` push** |

⇒ this **reuses `_channel_reoffer_pending` as the EXISTING authority** (U1) **without misusing the `send_failed`
predicate.** ⚠ `entry.holder == false` is not decoration: the `ctr` field is **0** on a holder slot, so a holder match
would push a correlation handle that means nothing.

⛔⛔ **AND A CONSTRAINT THE SPEC MUST CARRY: `node.h:1876` `static_assert`s `sizeof(ChannelReofferPending) == 12`
with `offsetof(id) == 4` and `offsetof(ctr) == 8`**, and its message says a grown record costs real bytes
× `cap_channel_reoffer_pending` × `MR_N_LAYERS` and **moves `sizeof(Node)`**. ⇒ **the implementation may READ this
record but must NOT GROW IT** — doing so triggers D2's ten-env sweep.

### Tests this replaces P1/N14 with
- a **locally originated channel-M** produces **one matching `send_aired`**;
- a **channel handle above 255** is **preserved exactly** (this is the whole reason `entry.ctr` is 16-bit);
- a **holder re-offer** produces **none**;
- a **channel pull response** produces **none**;
- a **forwarded DM** produces **none**.

## R3.2 — Smaller correction: the monotonic ordering must cover EVERY terminal outcome

Round 2's rank stops at `delivered/relayed`. Required: **`queued < aired < EVERY logical terminal outcome`.**
⇒ **a delayed `send_aired` must not overwrite `DELIVERED`, `NO RELAY HEARD`, `NO CONFIRM`, `BLOCKED` or `FAILED`.**
**Add one delayed-airing-after-terminal regression.** ⓘ Note this is the same defect direction as R2.2, one layer
later: R2.2 stopped a weak outcome suppressing a strong one; this stops a **mid-rank** outcome **overwriting** a
terminal one.

## R3.3 — Scope of this round
1. **Correct the channel ownership section and its tests** (R3.1) and **extend the ordering** (R3.2). Re-derive the
   affected §4.2/§UI/§test-plan text rather than patching it; index the withdrawals in §12's change table.
2. **Update [[B164]]/[[B189]]'s rows** where this changes what is owed (M1).
3. ⛔ **STILL NO FIRMWARE in this round.** ⓘ Two items the owner listed as pre-T1 are **NOT yours**: the
   `duty_defer_fire` comment fix (`lib/core/node_mac.cpp:1872`, which falsely claims it *mirrors `retry_stashed`* —
   `retry_stashed` has **no** pre-transmit flight guard; only the post-tx ACK re-arm at `:1904` is guarded) and the
   **lifting of the design-only prohibition**. Both belong to the **T1 implementation brief, which the QA-gate
   writes** — this plan's header prohibition stands until that brief explicitly supersedes it.
