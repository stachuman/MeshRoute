<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# B157 — the implicit-ACK terminal inference · dispatch brief · 2026-08-08

**Status: ⛔ SUPERSEDED 2026-08-08. DO NOT IMPLEMENT THIS PLAN.** Its deletion/hint recommendation was locally safe,
but the later four-arm system measurement found a non-additive delivery and airtime cost when both RTS optimisations
were removed. The owner ruled to strengthen RTS identity and restore both optimisations. The live plan is
[`2026-08-08-hybrid-rts-flight-identity.md`](2026-08-08-hybrid-rts-flight-identity.md). The text below is retained as
the investigation record only. **D4: the owner commits.**

⚠ **Do not start this until B153's cleanup and its exact gate are complete** (QA's sequencing, §5 below).

---
## 1 — Why this exists: the second terminal inference from a frame that cannot support one

**The B153 principle: RTS AUTHORIZES RECEPTION; ONLY DATA PROVES MESSAGE IDENTITY.**
A 7-byte unicast RTS cannot distinguish a **retry of message A** from the **first attempt of message B** sharing
`(hop src, dst, ctr_lo, payload_len)` — those frames are **byte-identical**, so no receiver algorithm can return a
terminal verdict from one. B153 removed the `already_received` inference on that basis.

★ **`implicit_ack_from_forward` is the SAME inference at a second site** (`handle_rts`, ~165 lines before the
`already_received` path): *"I saw my downstream neighbour forward my message, so treat it as ACKed."* **A 7-byte RTS
cannot prove WHICH message was forwarded**, so the credit can clear the wrong pending TX. **Measured:** in `s27` the
gateway credited message-111's forward to flight-114, which had never sent a DATA — the same silent discard B153 fixed.

## 2 — The measurements that frame it (already in the repo; do not re-derive)

- **Aliasing, corpus-wide:** 4137 last-acked stores ⇒ **66 at-risk pairs, 4 aliased = 6.1 %**.
- ★★ **Different-origin — the gateway's design case: 4 / 5 = 80 %**, because symmetric reply traffic leaves peer
  counters **correlated**, not independent. **Same-origin: 0 / 61** (needs the 4-bit counter to advance a multiple of
  16 inside the 10 s TTL; observed deltas were 1 and 2).
- ⚠ **The earlier "1/16" estimate resembled the aggregate by coincidence and described the WRONG mechanism.** Record
  both numbers; the 80 % is the one that matters operationally.
- **`implicit_ack_from_forward` firing counts:** **61×** corpus-wide; **10× in `s18`**; **1× in `s27`, and that one
  firing is the message-losing one.**

⇒ ★★ **THIS IS WHY B153 AND B157 CANNOT BE ONE SLICE:** `s18` byte-identity requires the implicit-ACK site
**untouched** (it fires 10× there); `s27 → 0 failures` requires it **fixed**. **Both cannot hold without a re-anchor.**

## 3 — QA's preferred solution: DOWNGRADE the implicit ACK to a SCHEDULING HINT

The matching forwarded RTS is **ambiguous**, so it must **never** cancel the pending transmission or report delivery.
It may justify only a **non-terminal** action:

- **keep the pending message;**
- **postpone / re-arm its ACK deadline** past the observed downstream exchange;
- **still require a real ACK;**
- **retry normally if that ACK never arrives.**

★ **A false match then causes only a BOUNDED DELAY, never message loss** — which is the whole point.

⇒ **Fallback, explicitly authorised by QA:** *"If implementing the deadline extension requires risky state changes,
simply DELETE the implicit-ACK optimisation first. Correctness is more important than avoiding a possible
retransmission."*

### ⚠ A claim of the QA-gate's that QA CORRECTLY REFUSED, recorded so it is not repeated
The QA-gate asserted that removal *"costs a round trip on every forwarded hop."* **That was never established.**
**Explicit ACK already exists.** ⇒ **The B157 gate must MEASURE, not assume:** of the **61** implicit-ACK firings, how
many removals actually produce **another RTS/DATA attempt, additional airtime, or a timeout**? Report the count and the
airtime via `airtime_ms()`. **If the cost is near zero, deletion is the simpler correct answer and the hint machinery is
unjustified complexity.**

## 4 — The CTS test-validity defect (QA confirms; partially fixed already)

After B153 there are two `cts_tx` emit sites: the pending-RX **re-CTS** branch emits **`dup: true` while
`already_received = false`**, and the **fresh** CTS emits **no `dup` field**. ⇒ **the telemetry `dup` field no longer
corresponds to the wire bit.**
- The wire bit must be asserted by **parsing the emitted CTS** (bit 0 of byte 0), never through `dup`.
- ⓘ The new regressions appear to have adopted that already (`test/test_node_r3.cpp:~391`).
- ⛔ **One older `CHECK_FALSE(cts->dup)` remains** — either **replace it with a parsed-wire assertion**, or **label it
  explicitly as a TELEMETRY-SHAPE test, not an `already_received` test.**

## 5 — Sequencing and gates (QA's, verbatim in substance)

**Stage 1 — B153 (in flight elsewhere):** remove the receiver's `already_received` inference · **preserve `s18` byte
identity** · ⚠ **`s27` may remain RED temporarily and MUST be attributed explicitly to B157.**
**Stage 2 — B157 (this brief):** non-terminal hint **or** deletion · **re-anchor the affected scenarios with
attribution** · **make `s27` green.**
**Final cumulative gate: ALL scenarios green.** ⛔⛔ **DO NOT NORMALISE THE `s27` LOSS INTO AN ACCEPTED BASELINE** — a
red row that becomes the new anchor is exactly how a silent message loss gets adopted as correct behaviour.

### Cleanup QA requires before the gate runs (mostly B153's, listed so nothing is dropped)
1. **Rewrite B153** as the **7-byte, DATA-authoritative** fix — ⛔ the `flight_id` design must not stand as though it
   shipped; fence it as **REFUTED** with the information-theoretic argument recorded.
2. **Reopen B157** and describe the remaining implicit-ACK terminal inference.
3. **Withdraw/supersede B158** — the 11-byte RTS timeout it described no longer exists.
4. **Remove the production `last_acked_ttl_ms`** constant (now dead); tests needing the historical 10 s window use a
   **test-local named constant**.
5. **Correct stale 11-byte / `flight_id` text** in `docs/frames.md` (3 sites), `docs/protocol.md:~49`, and the current
   `simulation/BASELINE.md` note.
6. **Confirm ZERO local producers of `already_received = true`.**

## 6 — Method obligations carried from this arc

- ★★★ **Before trusting any check, ask whether it COULD have failed.** ★★ **Mutation-check every test at match
  count == 1**; distinguish an **inert** mutation from a **coverage gap**.
- ★ **Assert the observable side effect** — parsed frames, delivered payloads, the ACK/NACK on the wire — ⛔ never a
  telemetry field or a flag alone (§4 is precisely that failure, live in the tree).
- ⚠ **A counter/budget is spent by the physical act, never by the request** (B84 · B139 · B145/B146 · the re-CLAIM
  budget — five sites so far).
- ⚠ **Identity is the tuple** (B133 · B142 · B147 · B153) — and B157 is the same error at a fifth site.
- ⚠ **`tx_initiating` returns TRUE for a deferred frame** — a deferred frame is legitimately in flight; only
  **definitive** refusals are refusals.
- ⛔ **No `wire_version` bump** — owner-confirmed, MeshRoute is not deployed, and B157 needs no wire change.
