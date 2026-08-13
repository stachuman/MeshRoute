<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B186a — four distinct internal TX identities for the mobile operations · **+ the HAL reachability audit** · 2026-08-12

⛔ **Nothing here is an owner ruling or a QA approval.** No wire format changed, no `wire_version` moved, no
anchor table was edited, no `s07` load/window was touched and no delivery floor is proposed. The recovery
items in §5 are **proposals only** — [[B186b]] is **not implemented**.

**Baseline reproduced, then moved:** HEAD `c7bca52`; native **1512 / 81212 / 0 → 1515 / 81320 / 0** (+3 cases,
+108 assertions, binary RUN); `lus` **`316b9cb1` → `43a7b6eb`** (rebuild-proved: 34 build actions, byte-identical
md5 on re-link); corpus **36/36 green, 5 rows moved, every moved row byte-identical once the new emit is
removed** (§3).

---
## 1 — What the four identities are, and where each is captured

`TxParams::tag` is an opaque host token (`lib/core/hal.h:31`) of which exactly 3 bits were in use
(`FrameTag` 0..5). The mobile operation now rides its **high byte**: `tag = (op << 8) | FrameTag`. The low byte
is masked off before every `FrameTag` use, so retry-slot selection, the `"BCN"` label and the giveup arm see
values bit-for-bit unchanged.

| operation | `LbtKind` passed | captured at (`file:line`, the SENDING SITE) | relative to hand-off |
|---|---|---|---|
| **DISCOVER** | `mobile_discover` | `lib/core/node_mobile.cpp:141` (`mobile_discover_fire`) | **before** — the kind is an argument of `tx_initiating` |
| **OFFER** (host side) | `mobile_offer` | `lib/core/node_join.cpp:1048` (`jtx_fire` from the OFFER ring) | **before** |
| **initial CLAIM** | `mobile_claim` | `lib/core/node_mobile.cpp:258` (`mobile_claim_guard_fire`) | **before** |
| **re-CLAIM** | `mobile_reclaim` ★ NEW | `lib/core/node_mobile.cpp:359` (`mobile_reclaim_send`) | **before** |

★ **The whole of "captured before hand-off" is the new `LbtKind::mobile_reclaim` (`node.h:2164`).** A re-CLAIM
used to travel as `mobile_claim`, so the two CLAIMs were **the same value** downstream. The only code that
knows which one a frame is, is the site that decided to send it — one function each — so the fact is
established there and carried by carriers that already existed:

* an LBT-deferred frame carries it in **`DeferredLbt::kind`** (an existing `uint8_t`; **no new state**);
* at the radio it becomes the tag's high byte in **`tx_with_retry`** (`node_mac.cpp:1791`), the one encoder;
* it comes back on **`BusyInfo::tag`** and is decoded by `mobile_op_of_tag` (`node_mac.cpp:1616`).

⛔ **Not reconstructed anywhere.** `mobile_reclaim_deferred_rejected()` still derives its **refund decision**
from FSM state (`_mobile_claim_pending` / `active` / `claiming`) — **deliberately unchanged**: changing a
behavioural decision is [[B186b]]'s, not observability's. The captured kind is now available at that site for
that work, and the native suite pins that the two do not have to agree for the REPORT to be right (§2, third
case).

**Handling is unchanged.** Every site that tested `mobile_claim` now names **both** claim kinds — the
stale-attachment cancel (`node_mac.cpp:1430`), the adopt-at-handoff line (`node_mac.cpp:1485`) and node.cpp's
deferred-loss arm (`node.cpp:1418`) — so a re-CLAIM keeps its guard, its adopt call and its refund route.
Only its NAME downstream changed.

### 1.1 The refusal report

`Node::on_radio_busy` (`node.cpp:2183`) emits, **in addition to** the untouched `radio_busy`:

```
mobile_tx_refused{ op, reason, reason_name, sf, busy_until_ms }
```

— the operation, the reason (numeric **and** by name), the SF and `busy_until_ms`. All four come from
`BusyInfo`, which already carried them; ⛔ **no field was added to `BusyInfo` and no length is reported**
(it has no length member).

⚠ **The whole block sits inside `MR_TELEMETRY`, and [[B169]] is why:** `op` and the two name lookups exist
only to fill the event, so a `MESHROUTE_NO_TELEMETRY` board build must delete them WITH it. **Tested, and the
test is proved able to fail** — see §2.3.

⛔ **`p.label` stays `"BCN"` for a mobile J frame, deliberately.** The label is what the simulator prints in
its `tx` / `tx_deferred` records, so renaming it would move every mobile stream line **and break the
[[B183]] correlator**, which binds a lost J frame to a same-millisecond **BCN** refusal. The identity belongs
in the tag, which the simulator does not serialize — and that is why the corpus movement in §3 is exactly the
new emit and nothing else.

---
## 2 — Tests: positive AND negative for every subtype

Three new cases in `test/test_node_join.cpp` (the mobile TU that already owns these fixtures), +108 assertions.

| # | case | positives | negatives / controls |
|---|---|---|---|
| 1 | four distinct tags, stamped at the hand-off | each of DISCOVER / OFFER / initial CLAIM / re-CLAIM read from `TxParams::tag` **at `Hal::tx`**, each equal to its own `mobile_tx_tag(op)`; low byte still `FrameTag::beacon` | **all six pairs asserted distinct** (three-sharing-one would pass everything else); **an ordinary beacon carries op `none`** — `tx_flood`'s own tag, untouched |
| 2 | the asynchronous refusal reports op / reason / SF / `busy_until` | all four ops × the exact strings; all four `BusyReason`s reported as themselves; `oversized` really carries `busy_until 0` | plain-beacon tag ⇒ **0** reports while `radio_busy` still fires (instrument proven reached); DATA tag ⇒ 0; **unknown high byte ⇒ 0, never a guess**; and `tx_giveup` 0 + **no timer armed** (it is observability, not recovery) |
| 3 | the identity survives an LBT defer and is READ, not reconstructed | the deferred re-CLAIM's tag is still `reclaim` when the defer fires (it rode `DeferredLbt::kind`) | ★★ the mobile is then driven **out of `claiming` into `seeking`** (registration gone) and the refusal is delivered only then: the report still says `reclaim`, at the exact instant an FSM-derived answer could not |

### 2.1 Mutation proof — 7 mutations, all RED, from a changed binary each time

| mutation | what it breaks | binary | RED |
|---|---|---|---|
| `m1` re-CLAIM sends `LbtKind::mobile_claim` again (the collapse) | the fourth identity | `a94e3c9c` | **6** in 2 cases |
| `m2` `mobile_op_of_tag` always answers `none` | the decode | `a2a79818` | **17** in 3 cases |
| `m3` the emit hard-codes `op:"discover"` | the attribution | `d3a23065` | **4** in 2 cases |
| `m4` `mobile_op_of_kind(mobile_offer)` → `none` | the host-side identity | `6db36383` | **2** in 1 case |
| `m5` the report fires for EVERY refusal, mobile or not | **the three negative halves** | `6c169908` | **3** in 1 case |
| `m6` every frame is stamped `discover` | distinctness | `863d0063` | **15** in 2 cases |
| `m7` an ordinary flood is stamped with an op | **the beacon control** | `1ee518a1` | **2** in 1 case |

⛔ **Restore control:** all four touched sources are byte-identical (`md5sum -c`) after the sweep and the
native binary is back at **1515 / 81320 / 0**. ⛔ No `git checkout` was used at any point.

### 2.1b ⛔⛔ REPAIRED 2026-08-13 — `mobile_op_of_tag` CARRIED A COMMENT ASSERTING A GUARD THE CODE DID NOT HAVE

The first version claimed *"no `default:` — -Wswitch must fail the build when a fifth operation is added"* and
was wrong **two ways**: a `default: break;` sat **four lines below that very comment**, and — the reason the
first fault hardly mattered — it switched on **`hi`, a `uint8_t`**, and **`-Wswitch` gives no exhaustiveness
checking at all on an integer condition**, so the guard **could never have fired**. That is the
comment-asserting-a-protection class this arc has hit eleven-plus times, committed inside the function whose
job was to be the guard.

**The guard is now real and PROVED TO FIRE:** it switches on the **enum** with **no `default:`** — the pattern
`mobile_op_of_kind` three lines below already used.

| mutation | result |
|---|---|
| `m8` add a fifth `MobileTxOp` (`probe = 5`) | ⛔ **HARD BUILD FAILURE** under `-Werror=switch` at **two** sites: `node_mac.cpp:1632` (`mobile_op_of_tag`) and `:1649` (`mobile_tx_op_name`) |
| the **PRE-REPAIR shape** (switch on `uint8_t`, `default:` removed) + the same fifth enumerator, standalone TU | **compiles clean** ⇒ claim (b) confirmed: the old guard was unreachable even without the `default:` |

ⓘ The trailing `return MobileTxOp::none;` is **not** a `default:` in disguise: `tag` is a HAL-supplied uint16,
so its high byte can hold a value no enumerator names (a newer build's op reaching an older one), and that case
must report **nothing** rather than guess — the same representable-but-unnamed argument `label_of_frame` /
`retry_slot_of` document for `FrameTag`. ⓘ The repair is **code-identical in effect**: `lus` rebuilds
**byte-for-byte to `43a7b6eb`**, native stays **1515 / 81320 / 0**, `warning_census.sh` still **exits 0** at
174 / 178 / 178, and the 6 mobile-bearing corpus rows re-run to the same md5s.

### 2.2 What is NOT mutation-provable, stated rather than glossed

`frame_tag_of`'s **mask** cannot be made to fail through a production path today: all four ops ride
`FrameTag::beacon`, whose retry slot is −1 **either way**, and an unmasked `261` also falls through
`retry_slot_of` / `label_of_frame`'s trailing returns to the same answers. It is kept because a future
non-beacon operation would otherwise silently change retry selection — a defensive mask with a stated
purpose, **not a measured behaviour**.

### 2.3 ★ The TELEMETRY-DISABLED compilation test, and its own control

Compiling `node.cpp` / `node_mac.cpp` / `node_mobile.cpp` / `node_join.cpp` with
`-DMESHROUTE_NO_TELEMETRY -Wall -Wextra -Werror=switch`:

* **live tree: the warning multiset is IDENTICAL to the same compile of the pre-slice (HEAD) sources** — three
  pre-existing `-Wunused-parameter` (`node_mobile.cpp` `site`/`why`, `node_join.cpp` `why`) and nothing new;
* ★★ **the test is proved able to fail:** hoisting `const MobileTxOp op = …` out of `MR_TELEMETRY` produces
  `node.cpp:2209: warning: unused variable 'op' [-Wunused-variable]` under `MESHROUTE_NO_TELEMETRY` **and 0
  warnings with telemetry on** — i.e. exactly [[B169]]'s shape, invisible to native and to all 36 corpus
  streams, reproduced on demand and then not shipped;
* the authoritative board check agrees: `tools/warning_census.sh` **exits 0**, 326 objects/env, warnings
  **174 / 178 / 178 at their pins**, `-Wswitch` **0**.

---
## 3 — Corpus movement: attributed to the new diagnostic and to nothing else

`lus` **`43a7b6eb`** (from `316b9cb1`), all 36 rows, **0 assertion failures**. ⛔ **The `^### 36/36 corpus`
anchor table was NOT edited and no re-anchor is claimed or authorised.**

| row | base md5/events | new md5/events | new emits | stream with `mobile_tx_refused` lines removed |
|---|---|---|---|---|
| `s07_seattle_mobile_meshroute` | `b3b7ce31` / 107989 | `80709395` / 108060 | **71** | **`b3b7ce31` — byte-identical** |
| `s27_cross_layer_mobiles_meshroute` | `d3456515` / 9443 | `221604b1` / 9447 | **4** | **`d3456515` — byte-identical** |
| `s28_mixed_team_channels_meshroute` | `975a5131` / 3856 | `0afe6513` / 3865 | **9** | **`975a5131` — byte-identical** |
| `s29_mixed_leaf_team_meshroute` | `222ff566` / 2020 | `bb534a88` / 2025 | **5** | **`222ff566` — byte-identical** |
| `s37_team_homed_origin_meshroute` | `d836ce20` / 750 | `2efd54e4` / 751 | **1** | **`d836ce20` — byte-identical** |

**The other 31 rows are byte-identical unchanged.** Every moved row's event count grows by **exactly** its
number of new emit lines, and stripping those lines reproduces the pre-slice md5 **exactly** ⇒ the movement is
the diagnostic, measured rather than argued. No RNG draw, no timer and no HAL call was added on any path.

ⓘ **Two corpus-inert renames, measured before making them:** `mobile_tx_cancelled_stale{kind}` now reports
`"reclaim"` for a re-CLAIM (it said `"claim"`) and `tx_deferred_lost{kind}` would carry 6 instead of 4 —
**both events occur 0 times in all 36 streams**, and the one `tx_lbt_defer_dropped` in the corpus carries
`kind:2` (flood).

### 3.1 ★★ The 71 in `s07` are [[B183]]'s 71 — and two of them were previously unattributable

| | [[B183]]'s post-hoc stream correlator | this slice, in-firmware |
|---|---|---|
| DISCOVERs emitted but never aired | 23 (14 duty + 7 self-TX, **2 REFUSED as ambiguous**) | **23** = 15 `duty_cycle_exceeded` + 8 `self_tx_in_flight` |
| OFFERs emitted but never aired | 48 (45 duty + 2 channel-busy + 1 self-TX, 0 unattributed) | **48** = 45 + 2 + 1, identical split |

⇒ the identity **closes the two frames `lostj.py` correctly refused to guess at** (one duty, one self-TX):
an identity established by the act needs no correlator, so there is no ambiguity to refuse.

### 3.2 The [[B183]] fixtures still pass, and now name the operation

`docs/superpowers/evidence/b183/verify.py` → **36 checks, 0 failed, exit 0**; `selftest.py` exit 0.
The five streams now also carry per-op attribution, and **C5 exercises all four operations at once**:
`discover` 14, `claim` 1, **`reclaim` 3**, i.e. the initial-vs-re-CLAIM split visible on a real stream
(C4: `offer` 13 at the host). ⓘ C5's 3 re-CLAIMs are the same three the b183 README records reaching
`mobile_claim_exhausted`.

---
## 4 — ★★ THE HAL REACHABILITY AUDIT (evidence, not assumptions)

**The question:** per HAL implementation, can it **accept a frame and LATER invoke `on_radio_busy`**?

| # | implementation | `file:line` | sync refusal (`TxResult != ok`) | **accept-then-refuse via `on_radio_busy`** | reachability |
|---|---|---|---|---|---|
| 1 | `HalAdapter` (the simulator's HAL) | `~/lora-universal-simulator/orchestrator/runtime/NodeRuntimeWrapper.cpp:44` | **only `too_long`** — `FirmwareNode::simTx` (`FirmwareNode.cpp:183`) returns `kSimTxOk` for everything except `len > 255`; there is **no bounded queue**, so `kSimTxBusy` is unreachable | ✅ **YES** — `SimController::stepTx` refuses at the transmit step and calls `onRadioBusy` (oversized `:1877`, self-TX `:1900`, LBT, duty `:1986`) → `NodeRuntime::onRadioBusy` (`NodeRuntimeWrapper.cpp:147`) → `Node::on_radio_busy` | **SIMULATOR-ONLY** |
| 2 | `DeviceHal` (metal) | `lib/hal/device_hal.cpp:13` | ✅ **YES** — `busy` when the **8-entry** `_txq` is full (`kTxQCap`, `device_hal.h:105`; `_txq_drops++`, frame **not retained**) and `too_long` past 255 | ⛔ **NO — AND NOT BECAUSE OF ITS SEMANTICS: `Node::on_radio_busy` HAS NO CALLER ON HARDWARE AT ALL.** Grep of the whole tree: the only callers are `lib/core` (its own definition), the native suite, and the simulator wrapper. Nothing in `src/`, `lib/hal/`, `variants/` or `tools/` invokes it | **HARDWARE-REACHABLE (the sync half only)** |
| 3 | `DeviceHal::pump_tx`'s failed arm | `lib/hal/device_hal.cpp:38-46` | n/a (post-admission) | ⛔ **NO** — a failed `start_transmit` **pops and drops** the frame and the protocol is **not told** (the in-source comment says so; this is [[B164]]) | **HARDWARE-REACHABLE, silent** |
| 4 | `TestHalBase` (native fake) | `test/support/test_hal.h:36` | `tx()` returns `ok` unconditionally | ⛔ **NO** — it has no timer/queue model that could refuse later. **The native suite reaches `on_radio_busy` ONLY by direct invocation** (`test_node_r3.cpp` ×20, `test_node_join.cpp` ×15 new) | native-only |
| 5 | the 8 per-TU derived fakes | `test_node_join.cpp:61`, `test_node_r2.cpp:32`, `test_node_r3.cpp:45`, `test_node_query.cpp:39`, `test_node_channel.cpp:37` (+`TkHal:1851`), `test_node_hashlocate.cpp:36`, `test_node_e2e_ack.cpp:37`, `test_dual_layer.cpp:25` | 5 override `tx()`; `test_node_join`'s `tx_answer` can answer `busy`/`too_long` (i.e. it **models `DeviceHal`'s SYNC refusal**) | ⛔ **NO** — none of them can express a late refusal | native-only |
| 6 | `tools/probe_firmware_ui` | `probe_main.cpp:167` — the **real** `DeviceHal` + a probe `IRadio` (`:65`) | inherits `DeviceHal`'s | ⛔ **NO**; its `IRadio` can fail `start_transmit`, which reproduces **#3**'s silent drop, not a busy callback | host-compiled probe |
| 7 | `ScriptedNode` (Lua) | `~/lora-universal-simulator/orchestrator/runtime/ScriptedNode.cpp:130` | n/a | it calls the **Lua** `on_radio_busy`; it never hosts a `meshroute::Node` (and the Lua engine is deprecated) | not a `meshroute::Hal` |

### 4.1 The answer to the open question, and it is not "equivalent"

**Each refusal shape exists in exactly ONE implementation, and neither is proven for the other:**

* the **asynchronous** refusal (`on_radio_busy`) is produced **only** by the simulator. On hardware the
  function is **unreachable** — which also means the entire R4.5b stash-retry machinery behind it
  (`retries_left`, `kRadioBusyRetryTimerId`, `tx_giveup`) is **dead code on metal**;
* the **synchronous** refusal (`TxResult::busy` from a full outbound ring) is produced **only** on hardware —
  and it is measured **corpus-dark**: across all 36 streams `tx_hal_rejected` = **0**, `tx_failed` = **0**,
  `oversized` = **0**.

⇒ ⛔ **Do not read this slice's `mobile_tx_refused` as a metal diagnostic.** It fires where the simulator can
refuse late; on metal the same class of loss arrives either as `tx_hal_rejected` (sync, already
per-operation-attributable via `mobile_tx_rejected{site}`) or as `pump_tx`'s silent drop (#3, attributable to
nothing). ⚠ **`MR_EMIT` is device-stripped in any case**, so on metal the new event does not exist.
**Whether the two paths lead to the same protocol outcome remains UNVERIFIED — no hardware was run.**

### 4.2 Recorded gap

For **#2** the audit establishes reachability of the *sync* half by source and by the `kTxQCap = 8` ring, but
**how often a real radio fills that ring is unmeasured** (no bench data, and the corpus cannot produce it —
the simulator has no bounded queue). **Recorded, not assumed.**

---
## 5 — Minimal recovery proposals per reachable mobile operation — ⛔ PROPOSALS ONLY

⛔ **Nothing below is implemented, approved or recommended by anyone but this document's author.** Each is
scoped to one operation, and each names the path it would serve — because §4 shows a fix aimed at
`on_radio_busy` alone would be **simulator-only by construction**.

| operation | path that loses it | minimal proposal | cost / risk |
|---|---|---|---|
| **DISCOVER** | async (sim) | reuse the EXISTING bounded local retry: call `mobile_admission_rejected(TxAdmission::tx_rejected, "discover_busy")` from the `op == discover` arm. It already arms one jittered `kMobileDiscoverTimerId` retry and touches no home-link state | **no new state, no new timer**; ⚠ adds one RNG draw per refused DISCOVER ⇒ moves `s07`/`s28`/`s29`, so it needs its own slice + attribution |
| **OFFER** | async (sim) | call `mobile_offer_admission_rejected(TxAdmission::tx_rejected)` from the `op == offer` arm — the host-side twin, already written, which emits `mobile_offer_dropped` so the mobile's own retry is the backstop | no new state; the OFFER's 13-byte stash is already gone by then, so it **reports** rather than re-sends. A re-send needs [[B137]]/S2's keyed ring, a bigger slice |
| **initial CLAIM** | async (sim) | the CLAIM has already ADOPTED at the hand-off, so a bare retry would double-register. Minimal honest form: **clear the provisional adopt and re-enter `seeking`** (the existing `mobile_reset_registration` path) rather than retry | ⚠ this is a **behaviour** decision about a lie already told to the app ([[B111]]/[[B112]] shape) — needs a ruling, not a patch |
| **re-CLAIM** | async (sim) | **refund the retry**, exactly as the deferred path already does (`mobile_reclaim_deferred_rejected`), then let the existing confirmation deadline try again — and, now that the identity is captured, gate that refund on **`op == reclaim`** instead of on FSM state | smallest of the four; removes the last FSM-reconstruction in the chain. ⚠ it changes a decision ⇒ [[B186b]], with its own A/B |
| **all four, on METAL** | sync (`tx_hal_rejected`) + `pump_tx`'s silent drop | ⛔ **Sequence FIRST:** the sync half is already per-op attributable (`mobile_tx_rejected{site}` names `discover`/`claim`/`reclaim`; `mobile_offer_admission_rejected` names the OFFER), so what is missing on metal is **#3**, the post-admission silent drop — i.e. [[B164]]'s deferred option (b), a flight-correlated TX-completion signal | a HAL-contract change; ⛔ **the largest of the five and the only one no automated gate can reach** |

---
## 6 — D2, and what did NOT run

**D2 answered explicitly: NO — `sizeof(Node)` did not move, and it was measured on real board ABIs anyway.**
The slice adds **no data member** (one enum value, five pure static helpers, one defaulted parameter):
`sizeof(Node)` is **221880** and its `static_assert` (`node.h:3352`) still compiles — the native build is the
proof, it would have failed loudly. **`TimerWheel::kCap` is 91 and NO timer id is allocated.**
**RAM is BYTE-IDENTICAL on all five board ABIs measured** — `gateway_heltec` 240420 · `heltec_mobile` 214772 ·
`heltec_v3` 215252 · `gateway` 194860 · `xiao_sx1262` 169692 · `xiao_esp32s3` 213844 — which is the
independent confirmation that no member was added.

⚠ **FLASH MOVES, and it is reported rather than smoothed over.** A/B-measured on the same machine and session
by reverting only the four touched `lib/core` files to HEAD (the PRE arm reproduces `BASELINE.md`'s pinned
figures — `xiao_sx1262` **529044 exactly**, `xiao_esp32s3` **1218656 exactly**, `gateway` 479684 vs the pinned
479700, a 16 B session-to-session drift of the class [[B86]]/[[B138]] already record):

| env | pre | post | Δ |
|---|---|---|---|
| `xiao_sx1262` | 529044 | 529668 | **+624** |
| `gateway` | 479684 | 480388 | **+704** |
| `xiao_esp32s3` | 1218656 | 1218788 | **+132** |
| `gateway_heltec` | 1238484 (pinned) | 1238552 | **+68** |
| `heltec_mobile` | 1267744 (pinned) | 1267892 | **+148** |
| `heltec_v3` | 1273088 (pinned) | 1273228 | **+140** |

⚠ **The Xtensa figure is demonstrably noisy, which is itself a measurement:** the same tree gave `xiao_esp32s3`
**1218776** earlier in this session and **1218788** after the flash-neutral `mobile_op_of_tag` repair (ARM
identical across both builds, `lus` byte-identical) — a **±12 wobble** of exactly the class [[B63]] (Xtensa
link-order relaxation) and [[B138]] (board flash not reproducible under the current method) record. The OLED
envs moved +4 / +12 between the same two builds. ⇒ **read these Δ as the order of magnitude, not as bytes.**

★ **It is NOT the telemetry** (that is stripped, and `nm -C` finds **zero** symbols for `mobile_tx_op_name`,
`busy_reason_name`, `mobile_op_of_*`, `tx_tag_of`, `frame_tag_of` in `firmware.elf` — all inlined or GC'd).
What remains on metal is the **on-device half of the identity**: the tag OR in `tx_with_retry` and three extra
`LbtKind` comparisons. ⚠ **The per-env spread (+68 … +704) is far wider than that code is**, which is
[[B138]]'s open gate defect (board flash figures not reproducible under the current method) plus
[[B63]]/[[B86]]; ⛔ **a byte-level attribution of the spread was NOT performed.**

**Not run, named so nothing here implies a skipped gate passed (D3):** the **ten-env** sweep (only the three
OLED envs via `warning_census.sh` + the ruled `gateway`/`xiao_sx1262`/`xiao_esp32s3` trio — `sizeof(Node)` did
not move, which is D2's condition for the wider sweep) · the **UI probes** (`probe_board_ui`,
`probe_firmware_ui`) · **any metal** · **[[B186b]]**, which is deliberately not implemented.

**M2 (bench script): NOTHING IS OWED, and here is the reasoning rather than the conclusion.** The new event is
inside `MR_TELEMETRY`, so it does not exist in a board build; `Node::on_radio_busy` has **no caller on
hardware** (§4), so even the code path is unreachable there; `DeviceHal` never reads `TxParams::tag`. ⇒ the
slice adds **no metal-observable behaviour**, so it adds no bench step — the honest alternative to inventing
one.

---
## 7 — ★★★ STATUS AND WHAT REMAINS OWED — **the mobile-home investigation ENDS HERE (owner-ruled 2026-08-13)**

Written as the closing entry because nobody is expected back soon: anything not written down is lost.

| item | status | what remains owed |
|---|---|---|
| **[[B186a]]** — the four internal TX identities + the refusal report | ✅ **IMPLEMENTED AND GATED** (native 1515/81320/0 · `lus` `43a7b6eb` · 5 corpus rows moved, each byte-identical once the new emit is removed · `sizeof(Node)` 221880 · `warning_census.sh` exit 0) | **nothing.** ⓘ Not committed — the owner commits and bench-verifies |
| **[[B186b]]** — behavioural recovery | ⛔ **NOT IMPLEMENTED, deliberately.** Owner-ruled: it stays open | the four **proposals** in §5 are proposals only. ⚠ Whoever takes it must read §4 FIRST: a fix aimed at `on_radio_busy` is **simulator-only by construction** |
| **[[B189]]** — `on_radio_busy` has no hardware caller | **OPEN / RECORDED** — measured, not fixed | a decision on whether the async path should exist on metal at all; and the **unmeasured** question of how often a real radio fills `DeviceHal`'s 8-entry ring (§4.2) |
| **[[B188]]** — rolling-window coverage | ✅ **CLOSED by the non-corpus fixture** (`../b188/`, 39 checks + 39 controls) | nothing for the fixture. Its residual is that **both refusal paths it exercises are the simulator's** |
| **[[B183]]** | diagnosis stands; its §7 recommendation 1 is now landed **in a different shape** (op, not tag/len) | recommendation 2 = [[B186b]]; recommendation 3 was owner-ruled out |
| the `mobile_op_of_tag` `-Wswitch` guard | ✅ **REAL AND PROVED TO BITE** at two sites (§2.1b) | nothing |
| **corpus / anchors / floor** | ⛔ **UNTOUCHED**: no `s07` change, no re-anchor, the `^### 36/36 corpus` table not edited, the delivery floor **frozen/unratified** and no figure proposed | nothing from this arc |
| **M2 bench script** | **nothing owed**, with the reasoning in §6 (the emit is device-stripped, `on_radio_busy` is unreachable on metal, `DeviceHal` never reads `TxParams::tag`) | nothing |

⛔ **No owner or QA approval is claimed anywhere in this document.** Every figure here was measured on this tree;
every figure that was later re-derived and moved is recorded beside its previous value.
