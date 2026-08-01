> ## ⛔ OBSOLETE — ADDRESSED AND ARCHIVED 2026-08-01
>
> **Every finding has been actioned.** Three became registered core bugs; the rest were applied to the two documents
> this reviewed. Kept as the audit trail for *why* they changed — do not action it again.
>
> ### The three core findings became REGISTERED BUGS (owner ruling: the bug register is for implemented code)
>
> | § | → | entry in `docs/2026-07-30-open-bug-register.md` |
> |---|---|---|
> | 1 | **B38** | a TEAM channel post can never report `relayed=true` ⇒ every team post's outcome is a false negative |
> | 2 | **B39** | `CmdCode::queued` with `ctr == 0` means NOT SENT — the synchronous result is ambiguous |
> | 3 | **B40** | `channel_sent.ctr` carries only the low 8 bits of a 16-bit counter |
>
> Grouped as **one slice** with a dispatch note in the register's §0; handed to an independent agent. **They are a
> prerequisite for Phase A Tasks 4/7/8**, and the plan now says so — its earlier "no core prerequisite remains" claim
> is retracted. ⚠ B38/B40 change an emitted value ⇒ expect a re-anchor; own slice, own commit (C4).
>
> ### The rest were applied to the spec and plan
>
> | § | finding | where it landed |
> |---|---|---|
> | 4 | one tracker delays the emergency | spec §2.1 (two trackers, emergency priority) · plan Tasks 2/4/6 — separate `_emg_req_pending` slot, emergency checked first, outstanding canned post abandoned |
> | 5 | U8g2 page loop drew only page 1 | spec §5 · plan Task 6 — redraw the full scene per page, from a frame-start frozen copy |
> | 6 | `emergency_hold_until_ms` written, never read | spec §4.3 · plan Task 3 — `hold_active()` reads the deadline wrap-safely; `picked_up` included |
> | 7 | DM timeout/reasons collapsed | spec §3.4.1 · plan Task 4 — full `SendFailReason` through `match_dm`, **late ack upgrades `NO CONFIRM` → `DELIVERED`** |
> | 8 | Task 7 corrections | plan Task 7 — Files line lists `firmware_commands.{h,cpp}`; `#include "console_parse.h"` added; the over-broad `unsealable` mapping removed |
> | 9.1 | failed battery reads retried every pass | spec §7 · plan Task 6 — cadence gates on *attempted*, not *succeeded* |
> | 9.2 | replies accepted with no alarm sent | spec §4.4 · plan Task 3 — state whitelist **plus** `_tries > 0` |
> | 9.3 | `size_t` without `<cstddef>` | plan Task 2 — include added, `std::size_t` qualified |
> | 10 | inbox closed; bounded-retention nuance | spec §6.1 · plan Task 7 — **per-kind** budget (4 DM + 4 CH), so a chatty channel cannot evict every DM row |
>
> All twelve of §12's acceptance cases were adopted into the spec's test section and the plan's bench matrix.
>
> ### One correction to this review
>
> §2 is right that a blocked alarm would consume an attempt, but the review's framing that the UI "must never call
> `on_send_accepted()` for a channel result with `ctr == 0`" understates what was found: `next_ctr` never returns 0
> (`node_mac.cpp:20-24`), so **`ctr == 0` is a usable sentinel today** — the documents now treat it as one
> (`awaiting_outcome`), which makes the interim behaviour correct rather than merely less wrong, while B39 remains the
> real fix for telling `unsealable` from `no_location`.

---

# On-device OLED UI design and Phase A plan — second review

*2026-08-01. Standalone second-pass review of the revised documents below. This review does not modify either source document.*

- `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`
- `docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`

The first review is retained as an obsolete audit trail at
`docs/archive/2026-08-01-onboard-oled-ui-review.md`.

**Mid-review update accounted for.** While this review was being written, Task 7 and spec §2.1 were revised to replace the incorrect `dispatch_typed` proposal with an additive, discriminated `mrfw::exec_command()` helper. That correction is accepted in §8 below; it resolves the dispatcher-seam finding but not the Node-level acceptance ambiguity in §2.

## 0. Outcome

The revision fixes most first-pass findings. In particular, it now specifies a direct bounded adapter over the already-merged `Inbox::pull()` API, labels DM and channel rows, preserves the API's DM-block-then-channel-block order, and does not claim chronological interleaving. That is the correct resolution of the inbox question: **no new inbox merge belongs in the firmware or UI**.

The plan is still not safe to execute literally. Four remaining P0 issues arise from mismatches between the proposed UI tracker and the current Node contracts:

1. a team-channel post cannot currently produce `channel_sent{relayed=true}`;
2. `CmdCode::queued` with `ctr == 0` can mean that no channel transmission was accepted;
3. the channel outcome carries only the low 8 bits of the 16-bit command counter;
4. the single tracker can defer an emergency behind an outstanding DM or canned channel transaction.

These are not cosmetic documentation points. They affect whether the alarm actually transmits and whether `PICKED UP` is truthful. The plan's assertion that no core prerequisite remains (`phase-a.md:31-33`) is therefore premature.

## 1. Team posts never produce `PICKED UP`

**Severity: P0 — the successful emergency outcome is unreachable on the Phase A target path.**

The plan expects a relayed team-channel emergency to enter `PICKED UP` (`phase-a.md:624`, `:1144-1151`). The current core deliberately treats a team re-offer differently:

- `channel_reoffer_confirm()` returns immediately when `rp.team` is true (`lib/core/node_channel.cpp:1149-1158`), so it does not emit `channel_sent{relayed=true}`;
- when the team re-offer exhausts its retries, `channel_reoffer_fire()` emits `channel_sent{relayed=false}` (`lib/core/node_channel.cpp:1131`).

Consequently, even if another team member really re-floods the emergency, the sender eventually receives `relayed=false` and the UI reports `NOT HEARD`. The Task 8 two-node acceptance case cannot pass as written.

### Proposed solution

Change the core outcome bookkeeping before implementing the UI. Preserve the team rule that one relay must not stop the remaining coverage retries, but remember that a relay was observed and report it truthfully. A compact solution is to extend/repack `ChannelReofferPending` with:

- the full originating `ctr` (§3 below);
- `relay_seen` or `outcome_emitted`.

On a team confirmation, set `relay_seen` and either emit `relayed=true` once immediately while leaving the retries active, or emit the remembered result when the retries finish. Do not later emit a contradictory `relayed=false` for the same post.

The existing structure is 12 bytes on the current layout (`lib/core/node.h:1238`). Reordering its fields should allow a `uint16_t ctr` and one extra flag without increasing the structure—and therefore `Node`—but this must be pinned with `static_assert(sizeof(...))`/the existing `sizeof(Node)` gate rather than assumed.

If core behavior is deliberately out of scope, remove `PICKED UP` from Phase A and state that relay evidence is unavailable. The UI must not synthesize confirmation from receipt of unrelated traffic.

## 2. `queued` does not mean that a channel transmission was accepted

**Severity: P0 — blocked alarms consume attempts, and some failed alarms remain on `SENDING...`.**

The plan treats every `CmdResult{queued, ...}` as an accepted transmission and immediately increments the emergency attempt count (`phase-a.md:1075-1082`). That is not the current Node contract:

- the channel min-interval/cap gate emits `send_blocked` and returns `0`, explicitly saying that no counter was minted (`lib/core/node_channel.cpp:640-645`);
- a channel seal failure emits `send_failed` and also returns `0` (`lib/core/node_channel.cpp:718-734`);
- `Node::on_command()` nevertheless wraps that zero in `CmdResult{CmdCode::queued, ctr, ...}` (`lib/core/node.cpp:1561-1578`).

For a blocked emergency, the proposed flow is therefore:

1. receive `queued, ctr=0`;
2. call `on_send_accepted()` and consume one of the three transmissions;
3. later match `send_blocked` and retry.

That directly contradicts the revised spec's rule that a pre-TX block consumes no attempt (`design.md:219-229`) and even contradicts the plan's own native expectation (`phase-a.md:495-504`).

The seal-failure path is worse: `mr_ui_on_push()` routes `send_failed` only through `match_dm()` (`phase-a.md:1020-1023`). A channel/emergency seal failure is ignored after the false acceptance, leaving the emergency state without a valid terminal outcome. Also, `refuse_reason_of(CmdCode)` cannot distinguish `unsealable` from `no_location`, because both can return `err_unsupported`; their actionable distinction exists only in `SendFailReason`.

### Proposed solution

Define an execution result that distinguishes all three cases:

- **accepted** — a real non-zero channel counter was minted;
- **blocked** — nothing was sent, with `reason` and `next_ms`;
- **refused/failed before enqueue** — nothing was sent, with the exact `SendFailReason` or parser error.

The robust fix is to return this richer result from the Node channel-origination path and adapt console formatting around it. A text-parser helper returning only today's `CmdResult` cannot recover information the result does not carry.

At minimum, the UI must never call `on_send_accepted()` for a channel result with `ctr == 0` (`next_ctr()` returns 1..65535; `lib/core/node_mac.cpp:20-24`). Its tracker needs an `awaiting_immediate_outcome` state that can accept the associated `send_blocked` or channel `send_failed` without incrementing attempts. However, because those pushes are node-wide and the failure counter can differ from the returned zero, a bounded-window workaround is weaker than a typed Node result and should not be described as exact attribution.

## 3. The proposed exact counter match compares 16 bits with 8 bits

**Severity: P0 — channel outcomes stop matching after the counter exceeds 255.**

The tracker correctly requires exact counter equality (`phase-a.md:773-779`). The two values are not currently the same representation:

- `do_send_channel()` mints and returns the full 16-bit counter (`lib/core/node_channel.cpp:647-648`, `:779`);
- the message ID retains only `c & 0xff`;
- both `channel_sent` emission sites reconstruct `ctr` as `id & 0xff` (`lib/core/node_channel.cpp:1131`, `:1158`).

After the first 255 channel sends, a tracked command counter such as 256 is compared with a push counter of 0 and can never match. Comparing only the low byte in the UI would restore matches but would weaken the very attribution guarantee the tracker was added to provide; values collide every 256 sends.

### Proposed solution

Store the full originating counter in `ChannelReofferPending` when registering an origin post, and emit that stored value in `channel_sent`. `Push::ctr` is already `uint16_t` (`lib/core/command.h:224`), so this needs no push-schema or wire-format change. It naturally belongs in the same small core correction as §1.

Add native coverage across the boundary: counters 255, 256, 257, and 65535→1, with unrelated low-byte-colliding outcomes interleaved.

## 4. One global tracker can delay an emergency behind a normal send

**Severity: P0 — a completed long press may display `SENDING...` while no alarm has been dispatched.**

The model explicitly tests that a DM and emergency outcome machine can coexist (`phase-a.md:538-547`), but Task 4 creates one global in-flight tracker (`phase-a.md:750-753`). The tick drains a model request only when that tracker is idle (`phase-a.md:979-980`).

If a `-a` DM is awaiting its end-to-end ACK, a subsequent emergency long press queues the alarm but cannot dispatch it until the DM ACK or ACK deadline closes the tracker. A canned channel outcome can cause the same delay. This violates the product promise that long press fires from any screen; navigation pre-emption alone is insufficient if send execution remains serialized behind non-emergency work.

The model also has only one pending `SendReq`, and `queue()` overwrites it (`phase-a.md:366-379`). Further normal actions can therefore replace an already-pending request while the tracker is busy.

### Proposed solution

Give emergency requests explicit priority and independent pending storage:

- allow an emergency tracker and a DM tracker concurrently; their pushes are distinguishable by kind plus counter/peer;
- when a canned channel transaction is outstanding, abandon its UI outcome tracking and let the emergency take the channel tracker immediately—late canned `channel_sent` counters will not match the emergency;
- never overwrite a pending emergency with normal UI work;
- optionally disable/mark normal compose sends busy while their own slot is occupied.

Add a test at the firmware integration boundary, not only the pure model: start an outstanding DM, fire the emergency, and prove that the emergency command reaches `Node::on_command()` in the same service pass. Repeat with an outstanding canned channel post and with delayed outcomes from the abandoned transaction.

## 5. The U8g2 page loop draws only the first page

**Severity: P1 — most of each OLED frame will be blank or stale.**

In U8g2 page mode, the complete scene must be drawn once for every page between `firstPage()` and the final `nextPage()`. The board API correctly advances one page (`phase-a.md:872-881`), but the tick calls `draw_frame()` only once when the frame begins. Later ticks call only `next_page()` (`phase-a.md:982-989`) without redrawing the scene into the next page buffer.

### Proposed solution

Freeze a small render snapshot at frame start and perform exactly one page transaction per eligible tick:

1. `begin_frame()` and retain the frozen `UiState`/`UiSnapshot`;
2. on each MAC-idle tick, call `draw_frame(frozen_state, frozen_snapshot)` for the current page;
3. call `next_page()` once and return;
4. clear dirty/finalize only after the final page.

Freezing avoids tearing when the live snapshot changes between pages. Add an instrumented board test that records eight draw calls and eight bounded page transfers for one 128×64 frame.

## 6. The explicit emergency hold deadline is written but never used

**Severity: P1 — panel-on timing does not implement the revised specification.**

The spec names `emergency_hold_until_ms` as the field driving the capped panel-on window (`design.md:235-244`). The plan sets `_emg_hold_until_ms` when firing and again on a reply (`phase-a.md:628-647`) but `blank_limit()` ignores it and instead compares the current time with `_last_input_ms` (`phase-a.md:356-361`, `:660-667`). `picked_up` is also omitted from the states receiving the 120-second limit.

As a result, receiving a reply does not actually restart the hold window despite writing a new deadline, and `PICKED UP` can fall back to the ordinary 15-second blanking rule.

### Proposed solution

Use `_emg_hold_until_ms` directly in `on_tick()` with the same wrap-safe deadline comparison used for retries. Define which transitions reset it; the current writes imply at least `long_fire` and `reply`. Include every retained emergency outcome, including `picked_up`, while keeping the state after the panel blanks.

Add tests for `picked_up`, a reply arriving near the original deadline, wraparound, and wake restoring the retained emergency screen.

## 7. DM timeout and failure reasons are collapsed

**Severity: P1 — `NO CONFIRM` is unreachable and actionable failures are lost.**

The model defines `dm_timeout`/`not_confirmed` (`phase-a.md:571-580`, `:608-615`), but `mr_ui_on_push()` passes only an `acked` flag and a `no_pubkey` flag to the tracker. Every other `send_failed`, including `e2e_ack_timeout`, becomes generic `dm_failed` (`phase-a.md:789-793`, `:1017-1023`). This contradicts the DM state table in the spec (`design.md:183-197`).

### Proposed solution

Pass the full `SendFailReason` into `match_dm()` and map at least:

- `no_pubkey` → `dm_no_key`;
- `e2e_ack_timeout` → `dm_timeout` / `NO CONFIRM`;
- all other matched reasons → `dm_failed` with a compact reason.

Decide and test the late-ACK rule. The core explicitly permits a late `send_e2e_acked` after `e2e_ack_timeout` (`lib/core/command.h:160`); the tracker should either retain enough identity to upgrade `NO CONFIRM` to `DELIVERED`, or the documents should state that the panel intentionally ignores late confirmation.

## 8. Mid-review Task 7 update — dispatcher seam resolved

**Status: resolved, with small plan/compile corrections remaining.**

The new `mrfw::exec_command()` proposal (`phase-a.md:1086-1119`) correctly recognizes that `mrfw::dispatch()` is only the firmware-verb router. `ExecResult{ok, parse_err, result}` can represent a parser rejection separately from a `CmdResult`, and calling `g_node.on_command()` before the borrowed command body goes out of scope is sound. Keeping the helper additive also avoids an unrelated refactor of the two working transport paths.

This update closes the original dispatcher-seam finding. It does **not** close §2: `ExecResult.result` is still today's `CmdResult`, so `ok && queued && ctr == 0` remains falsely classified as accepted at `phase-a.md:1077-1080`.

Apply these small corrections when revising Task 7:

- its **Files** line still says only `src/firmware_ui.cpp` (`phase-a.md:1040-1043`), although the task now also modifies `src/firmware_commands.h` and `.cpp`;
- `firmware_commands.h` does not currently include `console_parse.h`, but the proposed public `ExecResult` stores `meshroute::console::ParseErr` and names `ParseErr::ok`; add the direct include rather than relying on an unrelated transitive header;
- mapping every `err_no_binding`/`err_unsupported` to `unsealable` is over-broad, and `err_ack_ring_full` is not the deferred-send `queue_full` reason. Use send kind/context for compact synchronous wording. Exact `unsealable` versus `no_location` still requires the richer outcome described in §2 because `CmdCode` alone cannot distinguish them.

## 9. Smaller correctness gaps

### 9.1 Failed battery reads retry every service pass

**Severity: P2.** The cadence guard applies only when `s_batt_mv >= 0` (`phase-a.md:950-958`). If the board reader returns the documented unavailable value, every idle service pass performs another eight ADC reads. Track whether a sample was attempted separately from whether it succeeded; advance the 30-second deadline after every attempt while retaining the last good value.

### 9.2 Replies are accepted in states where no alarm was sent

**Severity: P2, with false-confirmation implications.** `on_reply()` accepts every non-`idle` state (`phase-a.md:628-633`), including `arming`, `cancelled`, and `failed`. A coincident channel-0 post can therefore become `REPLY` before an emergency transmission was accepted. Require at least one accepted emergency transmission and an explicit state whitelist (`firing`, `blocked`, `picked_up`, `not_heard`, or an existing `reply`).

### 9.3 The model header needs `<cstddef>`

**Severity: P2 compile detail.** The proposed header uses unqualified `size_t` in `copy_clamped()` (`phase-a.md:675-676`) but includes only `<cstdint>` (`:289-291`). Include `<cstddef>` and use `std::size_t`, or otherwise guarantee the declaration explicitly.

## 10. Inbox finding — closed

I agree with the owner's interpretation of the original item 5. The firmware already presents a merged inbox abstraction through `g_node.inbox()`/`Inbox::pull()`. “Merged” here should mean **one OLED screen containing both kinds, with every row visibly marked DM or channel**, not a new chronological merge algorithm.

The revised documents now state the important nuance correctly:

- call `g_node.inbox().pull()` directly, not textual `pull_inbox` into a bounded sink (`design.md:310-319`, `phase-a.md:1123-1125`);
- preserve the API's DM block followed by channel block;
- mark rows `DM` or `CH<n>`;
- do not imply cross-store chronological ordering;
- do not move the companion-owned durable read cursor merely because the OLED was viewed.

No further architecture change is needed for this item. During implementation, define the bounded-retention rule so “keep newest `kMaxInboxRows`” does not accidentally let the later channel block evict all DM rows solely because it is visited second. A fixed per-kind allocation or an explicit two-block truncation rule is sufficient.

## 11. Recommended revision order

Before executing Task 1 onward:

1. Resolve the team-channel outcome contract and carry the full counter (§1, §3).
2. Extend the now-correct `ExecResult` seam with a truthful accepted/blocked/refused Node result (§2); keep the dispatcher correction in §8.
3. Make emergency dispatch pre-empt normal send tracking (§4).
4. Correct the page-mode draw loop (§5).
5. Wire the actual emergency hold deadline and DM failure reasons (§6, §7).
6. Apply the smaller guards and add the integration/bench cases (§9).
7. Keep the revised inbox adapter as written, with only the bounded-retention detail made explicit (§10).

## 12. Minimum additional acceptance cases

- a team relay produces exactly one truthful `channel_sent{relayed=true}` while team coverage retries remain valid;
- a blocked emergency returns no accepted counter and does not consume one of three transmissions;
- a channel seal failure leaves `SENDING...` and shows its exact reason;
- channel counters 255, 256, 257, and 65535→1 correlate correctly;
- an outstanding DM or canned channel transaction cannot delay emergency command execution;
- delayed outcomes from an abandoned normal transaction cannot move the emergency;
- one frame redraws the scene once per U8g2 page and performs one page transfer per MAC-idle tick;
- `PICKED UP` and a late reply obey the 120-second hold deadline across `millis()` wrap;
- `e2e_ack_timeout` shows `NO CONFIRM`, and the chosen late-ACK behavior is tested;
- an unavailable battery reader is retried at 30-second cadence, not loop cadence;
- channel traffic during `arming`, `cancelled`, or `failed` cannot become a distress `REPLY`;
- the bounded inbox retains visibly labelled DM and CH rows without claiming chronological interleaving.
