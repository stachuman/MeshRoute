# MeshRoute Protocol — behaviour map

Companion to [`frames.md`](frames.md) (wire structure). This is a **navigation map, not a spec**: each
behaviour plane gets a few sentences of *what it does and why*, then points to the **source of truth** — the
code (`file::function`) and the design spec(s).

> **For future agents — read this first.**
> - **The code is authoritative.** This file is deliberately high-level; it tells you *where* the behaviour
>   lives and *why* it exists, not the line-by-line *how*. When in doubt, the code wins.
> - **To get oriented:** skim the plane list below, then jump to the cited `Source:` (code) or `Spec:` (design doc).
> - **To keep it from rotting:** when you change behaviour, update a plane's one-line summary here *only if the
>   shape changed*. Put the real detail in the code comment + the spec — never re-narrate it here. If an entry
>   ever disagrees with the code, the code is right and the entry is a bug: fix the pointer, don't grow the prose.
> - Specs live under `docs/superpowers/specs/` (recent) and `docs/specs/` (the 2026-05/06 foundation set). They
>   are point-in-time design records; the newest one for a topic supersedes older ones.

---

## 1. Routing

Distance-vector route table (per-leaf), merged from beacons; **on-demand AODV** (F-frame RREQ/RREP flood) fills a
gap when the table has no route. Candidates are scored from link SNR + hop count; the primary is `candidates[0]`.
- **Source:** `node_routing.cpp` (`rt_merge`, `learn_direct_neighbor`, scoring) · `node_route_discovery.cpp` (RREQ/RREP)
- **Spec:** `docs/specs/2026-05-30-r2-route-hardening-design.md` · `docs/specs/2026-05-29-c3-h-f-floods-design.md`

## 2. MAC / data plane

Single-slot stop-and-wait: **RTS→CTS→DATA→ACK** (NACK to refuse), each hop re-running the handshake. Listen-before-
talk (LBT) + NAV virtual carrier sense gate the TX; a rolling duty-cycle budget tier throttles under load; failed
next-hops cascade to alternates / hop-budget reroute.

### 2.1 Flight identity on the unicast RTS, and the two terminal decisions it licenses (`§hybrid-rts` S1–S5, 2026-08-08/10)

**The unicast RTS carries the flight's canonical identity.** 10 B plaintext (`origin|ctr_hi|ctr_lo` — all 16 bits of
`ctr`) / 11 B crypted (`BLAKE2b-512(0xE1|nonce_seed[8]|ctr_hi|ctr_lo|dst)[:4]`, which deliberately exposes **no
`origin`**, because a `CRYPTED` DATA seals it). Byte offsets: `docs/frames.md`. One producer, one comparator
(`rts_flight_identity*` / `rts_flight_identity_equal`, `lib/core/dm_crypto.h`); the comparison is **full width +
domain**, never a prefix and never a truncated tag. `M_BROADCAST` (9 B) and `FLOOD` (43 B) carry **no** identity and
did not grow; a legacy 7-B unicast frame is **rejected** — there is no compatibility parser.

**Why the frame grew, and why nothing smaller would do.** The old 7-B RTS identified a flight only by
`(immediate src, dst, ctr_lo[4], payload_len)`: `src` is the sender **of that hop** (a gateway when it relays),
`ctr_lo` is **4 bits**, and `payload_len` is a length. Measured across the 36-stream corpus that tuple aliased
**4 of 5 gateway/different-origin pairs (80 %)** and **6.1 % overall**, because peer send counters are *correlated*,
not independent. ★★ **The refutation of building a terminal verdict on it is information-theoretic, and it remains
the durable rule for any frame that carries no identity:** a 7-B RTS **cannot distinguish a RETRY of message A from
the FIRST ATTEMPT of message B** sharing that tuple — the frames are byte-identical — so no receiver state and no
cleverer matching can make one safe. ⇒ **A frame that carries no flight identity authorizes reception only; the
identity has to be ON it.** ⛔ A 4-B opaque `flight_id` tail was proposed first and **refuted**: it widened the frame
without making the tag *recomputable*, so a receiver still could not check it against the DATA that followed. What
S1 added instead is the identity **both endpoints can recompute from the frames themselves** — which is what makes
an RTS-time answer bindable. ⓘ Owner ruling: ledger §1.10.

**DATA must reproduce the identity before the receiver stores completion.** At DATA reception the receiver recomputes
the identity from the canonical DATA fields and compares. On mismatch: a named diagnostic, `PendingRx` cleared, and
**no** app delivery, **no** ACK, **no** cache store and **no** route-success credit. B161 normalized typed hash answers
(DATA types 1, 2, 8 and 13) onto the same plaintext-unicast envelope: their stamped `origin` is now present on the wire,
while their type-specific bodies remain unchanged. There is no parser for the former raw shape. B251 closes the hosted-
mobile counter collision at the home boundary without changing the envelope or any frame length: a qualifying plaintext
static/global transit keeps the mobile's `ctrM` on the direct hop, while the home allocates its own destination-scoped
`ctrH` and forwards under `ctrH`. For direct by-id transit, the home reserves the forward and any required E2E-ACK
correlation before ACKing `ctrM`. A `MOBILE_SEND` hash wrapper retains the existing B112
first-hop-ACK behavior, but reserves required correlation before that ACK and uses `SendDispatch` after PostAck as the
only proof of outward admission: queued activates the mapping and emits origin evidence, parked retains the reservation
until it is admitted, and refused releases it without either a mapping or evidence.
The verified hosted-mobile hash distinguishes equal first-hop counters in loop de-duplication; the completed-flight cache
continues to distinguish the mobiles by their immediate on-air sender. A returning ACK is keyed by mobile, `ctrH`, return
peer and layer, then translated back to `ctrM`. Team-plane and ordinary CRYPTED traffic do not enter this path; encrypted
counters remain untouched. Independent QG passed and B251/B161 are closed. B157 and B153 remain open and are ready
for explicit re-evaluation. B112 remains a separate, non-blocking first-hop-ACK issue.

**(1) The terminal `already_received` CTS — restored, and terminal only because it echoes the identity.** A receiver
that has *completed* this exact flight answers a retried RTS with the 6/7-B terminal CTS: the complete identity echo
plus the wire-declared plane bit, **no NAV byte and no chosen SF** (`docs/frames.md`). It allocates no `PendingRx`.
★★ **The sender acts on it only after a COMPLETE correlation — endpoint · plane · domain · width · every identity
byte — checked BEFORE any timer cancellation, link learn/confirm, state clear or success-shaped telemetry**
(owner-ruled verbatim, ledger §1.11). A mismatch **may be billed as physical airtime** — it did occur — but must
change nothing else: no liveness refresh, no timer, no routing, pending-state or application effect. An RTS cache
**miss** follows ordinary receive admission and waits for the DATA; a pending-RX duplicate re-CTS stays ordinary
(`already_received = 0`, 3/4 B, DATA still required).

**The completed-flight cache.** Bounded, per `LayerRuntime`: `CompletedFlight[cap_completed_flights = 12]`, keyed on
**the on-air immediate sender (`from`) + `dst` + the wire-declared team/static plane + the full identity (domain +
width + bytes)**, with an absolute `expiry_ms`. ⛔ `meta.src_hint` (the simulator oracle's static id) is **never** the
link identity — that was [[B156]]. TTL is `completed_flight_cache_ttl_ms`, **defined as** `gateway_send_giveup_ms`
(150 000 ms) and not re-spelled as a literal, because the horizon it must cover is the gateway-hold retry class; the
capacity is **measured, not chosen** (capacity 8 loses 5 of 429 corpus hits, capacity 1 loses 105). It is a deadline
scan on absolute expiry — **no timer id is allocated**.

**Plane handling is two deliberately different contracts.** `rts_wire_team_plane(addr_len, mobile_src)` is the
**sender-declared, receiver-independent** plane — the only thing that may be stored on a flight or echoed on a
terminal CTS. `team_addr_for_us(next, addr_len)` is **receiver-relative address admission** and says nothing about
the frame's plane; the two genuinely disagree on a real frame (a host's `(1,0)` last-mile to a hosted mobile matches
`team_addr_for_us` at any team member whose `_team_local_id` collides numerically, while the wire says STATIC — and
the wire is right). ⛔ And never `is_team_peer(src)`: that is our own state, not the frame's declaration.

**(2) The overheard-forward credit — restored, with exact identity matching.** A sender that overhears its selected
next hop forwarding **this exact flight** (expected next hop · destination · plane · domain/width · full identity)
may clear its own redundant pending copy, under one of two **named, diagnostic-only** bases: `local_admitted` (this
node had previously **admitted** a DATA for the flight to its radio) or `alternate_path` (no DATA has been admitted
locally). ⛔ **Neither basis is evidence a DATA of ours reached the air**: the redundancy of the local copy is the
sole justification, and it holds on both. ⛔ It never emits `send_acked`, `send_e2e_acked`, `delivered` or
`send_failed`, and never disturbs an independently armed end-to-end ACK wait; a mismatch changes no deadline,
pending state, route state or app outcome. ⓘ `_hal.tx()` returns `ok` on **ENQUEUE**, so the flag behind
`local_admitted` is written at the **one HAL-admission crossing point** every admission passes—never at a caller's
exit. Label owner-ruled: ledger §1.12. §T2 now establishes the separate, flight-correlated hardware completion fact
and reports it through telemetry, but deliberately does not rewrite this admission flag or change either
implicit-credit action. Its app/UI consumer remains T3; neither basis here became delivery evidence.

★★ **`payload_len` is a LENGTH and never disambiguated identity** (withdrawn claim, `§hybrid-rts` S4 item 5): it is a
frame-consistency / NAV field only — it sizes an overhearer's reservation and cheaply filters the **non-terminal**
pending-RX re-CTS fast path, where a wrong match costs a retry and never a message. ⛔ It is absent from every
identity comparison — the cache key, the terminal-CTS bind and the forward credit — and must not be added to any of
them "for extra safety": the comparison is the full identity or nothing.

**The DATA-level dedup remains the sole authority for delivery**, independently of either optimisation: keyed on the
canonical `(origin, dst, ctr)` — all 16 bits — or on the whole 8-byte cleartext nonce-seed for a `CRYPTED` flight,
with a TTL **derived from the enforced retry horizon**: `seen_origin_ttl_ms = gateway_send_giveup_ms (150 s) +
mac_exchange_margin_ms (300 s)` = **450 s**, where the margin is a legal-PHY-envelope bound (one worst-case MAC
exchange at SF12/BW 7.8 kHz/CR8 with a 255-B frame = 279 765 ms by `airtime_ms()`, pinned by a recomputing test).
The horizon is REAL, not assumed: no gateway-bound RTS may *physically start* at or after `enqueue +
gateway_send_giveup_ms` — Node-side admission guards cancel early, and `DeviceHal::pump_tx()` (immediately before
`start_transmit()`) is the terminal physical-start authority; a queued-past-deadline RTS is refused loudly
(`send_giveup{gateway_unreachable_timeout}`, correlated to exactly its own flight). Fresh DATA → ACK,
deliver/forward, record. Same message, same prev-hop (the real lost-ACK case) → **ACK only**, returning before the
deliver/forward step. Same message, different prev-hop → `LOOP_DUP` NACK.
**Duplicate suppression is narrowed, never disabled** — that is what stops a lost ACK from delivering twice.
✅ [[B159]] (a retried DATA arriving later than `seen_origin_ttl_ms` delivered twice) is **CLOSED 2026-08-28** by
the retention/deadline pair above. ⚠ Residual, separate: identity *reuse* within the 450 s retention is [[B258]]
(same `ctr`, different payload — suppressed as a duplicate).

**The airtime trade, measured on the corpus rather than argued.** The 3 identity bytes are **not** free in general
and **are** free at many PHYs — at 22 of 36 scenarios' PHY `airtime(10) == airtime(7)` because the bytes fall in the
same LoRa symbol bucket, while `s16_dense_gateway` paid **+28.7 %** RTS airtime and lost deliveries to the resulting
congestion. Restoring both optimisations pays that back and more: total PHY airtime **6 421 497 ms** with both
deleted → **5 830 644 ms** with both restored (**−9.2 %**), and **−0.9 %** against the pre-deletion baseline while
delivering more. ⛔ Do not read a falling total alone as efficiency — a lost delivery also takes its DATA/ACK airtime
with it; read it beside the delivery figure. ⓘ Timing: `start_rts_timeout` prices the **actual** 10/11-B request plus
the possible 6/7-B terminal answer through the same `unicast_rts_wire_len`/`terminal_cts_wire_len` helpers the packers
use, so its margin is non-negative at all 128 supported PHY cells. ⚠ [[B158]] (MeshRoute-native jitter) and [[B166]]
(`nav_duration_rts` under-reserves an ordinary exchange) are **open and out of this arc**.

**Sender-CR advertisement — sizing the receiver's DATA wait (2026-07-27).** After CTSing an RTS the receiver arms
a DATA-wait window (`start_pending_rx_expiry`) and abandons the flight when it expires. That window is
`CTS airtime + cts_to_data_gap + DATA airtime + margin`, and the DATA airtime depends on the **sender's** coding
rate — CR multiplies the payload-symbol count directly. It used to be computed from `active_cr()`, the
**receiver's own** CR, which is correct only while every node shares one CR.

A gateway does not: each layer may carry its own CR, so a gateway transmitting on a CR4/8 layer talks to leaves at
CR4/5. **CR is auto-detected at the LoRa PHY** — the explicit header carries it, so such a frame demodulates
perfectly — but the firmware's *clock* was wrong: the receiver armed for its own lighter CR, timed out, and
dropped a frame still on the air. Measured in `s32_dual_cr_gateway` (SF11 / BW 250 kHz, `payload_len` 38): the
receiver armed **607 ms** for a DATA needing **853 ms** — a **246 ms shortfall** — abandoning it 130 ms before it
landed. Both gateways were affected **symmetrically**, giving 26 `data_rx_timeout`s, an exhausted path cascade and
zero cross-layer joiner delivery.

⇒ **mixed-CR links were interoperable at the PHY but not in this firmware's RX-window sizing.** The fix: the RTS
states the sender's CR in two previously-reserved bits (`cr_adv`, no length change and no `wire_version` bump —
layout in `frames.md`), the receiver stashes it on its `PendingRx`, and the DATA term is computed from *that*.
s32's 26 timeouts go to 0, both cross-layer joiner DMs deliver, and the retry churn disappears (3015 → 2266
events).

**Sender-CR sizing completed for the overhearer (2026-07-27, `§rts-cr-overhear`).** The advert is now consumed
everywhere the datum exists, not only by the addressed receiver:
- the two **channel-overhear retune** windows (`FLOOD` and `M_BROADCAST` RTS-M) — an overhearer at CR4/5 used to
  retune off the data SF before a CR4/8 sender's M-frame finished, and lose it to `drop_sf_mismatch`;
- the **NAV reservation taken from an overheard RTS**, which reserves for that sender's own DATA. ⚠ **The CR term is
  fixed; the site is NOT otherwise correct — [[B166]] is OPEN** (measured by `§hybrid-rts` S5): `nav_duration_rts`
  prices the responder's CTS at **3 B** while the longest legal ordinary CTS is **4 B**, so an ordinary exchange
  under-reserves by up to **−131 ms** and releases NAV **early** — the dangerous direction. The corpus CTS census is
  `{4: 4709, 6: 172}`: **not one 3-B CTS exists.** The *terminal* direction over-reserves at every cell (worst +14 ms);
  ⛔ this predates the hybrid-RTS arc and is not its doing;
- the **anti-spam airtime ledger**'s RTS and DATA terms. This one is an *accountability* fix, not a timing one:
  the ledger answers *"how much airtime did this sender impose on us"*, so costing a peer's frame at our own rate
  **under-billed** every heavier-coded sender against `originator_airtime_share`. ⚠ Its RTS term reached
  `active_cr()` **indirectly, through `airtime_routing_ms()`** — invisible to a grep for `active_cr()`.

Measured in `s33_mixed_cr_channel_overhear` (cr8 gateway layers flooding to cr5 leaves): `channel_msg_received`
**26 → 35** and `channel_msg_overheard` **17 → 26**, so the nine frames the overhearers now catch remove nine
repair pulls (`channel_pull_sent` **10 → 1**) and the contention they cost (`tx_lbt_defer` 17 → 7, `collision`
8 → 4). Net cost **+1.0 % on-air for +35 % delivery** — 911 → 683 ms per delivered message. In
`s32_dual_cr_gateway` the ledger fix shows as the two cr8 senders' `orig_airtime_ms` **rising** (600 → 821,
320 → 443) while the cr5 senders' are untouched. ⚠ Only the FLOOD retune and the ledger are corpus-*validated*;
the `M_BROADCAST` retune and the NAV-from-RTS site execute constantly but only ever with cr5 senders, so native
tests are their sole regression detectors.

**Deliberately deferred — the honest remainder:**
- The **ACK wait** (`start_ack_timeout`) still uses `active_cr()`. Its dominant term is our *own* DATA, where that
  is correct; only the small `airtime_routing_ms(3)` ACK term is the peer's. In the gateway-high-CR shape a
  light-CR node awaiting a heavy-CR gateway's ACK **over**-waits, which fails safe.
- ✔ **The NAV reservation taken from an overheard CTS — CLOSED (2026-07-27, `§cts-len6-cr2`).** This was the one
  site of the class that failed in the **dangerous** direction: it sizes the DATA a CTS is clearing — a third
  party's frame at that party's CR — and against a heavier-CR peer the overhearer **under**-reserved and released
  NAV while the DATA was still on the air, the hidden-terminal collision NAV exists to prevent, failing in the one
  case it is for. The premise that blocked it (*"the CTS has no free bits"*) was **retired rather than worked
  around: byte 3 did not have to stay an exact 8-bit length.** Quantizing it to 4-B units (`len6`, rounded **UP**)
  frees two bits for the CR, and because `payload_len` counts inner+MAC only, the old flat `+13` was already
  over-reserving by 4–5 B against a true header of `DATA_HDR_MAX_LEN = 9` — so the quantization is paid for out of
  existing slack. **No byte added, no `wire_version` bump, and the CR becomes exact.** Measured (cr5 overhearer,
  cr8 peer, BW 250 kHz): SF11/38 reserved **690 ms for an 870 ms frame → now 870, hole 0**; SF11/120 **−549 → 0**;
  SF12/120 **−1196 → 0**. A uniform-CR mesh moves **1–4 B less** reservation (the old `+13` over-reserved by more
  than quantization adds back), never below the true frame length.
  ⚠ **Corpus-invisible:** no scenario has a third node overhearing a CTS that clears a heavier-CR sender *with*
  observable contention (a poison probe on the CR moves 0/31), so the exhaustive codec sweep and the
  `test_dual_layer` end-to-end test are its **only** regression detectors — **a scenario is owed**.
  *(Earlier −245 / −614 / −1327 ms figures overstated the hole by 25–30%: they computed "needed" with the `+13`
  header, double-counting the fudge this change retires.)*
- The **CTS term of the anti-spam ledger** stays at our own CR — and ★ **byte 3 does NOT help it**: that term bills
  the CTS *frame*, whose sender is `tx_id`, while byte 3's CR belongs to `rx_id` (the node being cleared). Wrong
  node — using it would swap one error for another. A fixed 4 B on the routing SF, single-digit ms.

**Fleet assumption.** The 2-bit code is total over CR 5..8, so all-zero bits mean **CR4/5**, not "unknown". Every
board env compiles `-DLORA_CR=5` and `radio_cr` defaults to 5, so an un-reflashed node genuinely *is* at CR4/5 and
a new receiver decodes it correctly. The hazard is reachable only if the fleet's **global** CR is moved off 5
while un-reflashed nodes remain — do that only behind a `wire_version` bump.
- **Source:** `node_mac.cpp` (`do_data_tx`, `duty_over_budget`, budget tiers) · `node_mac_rx.cpp` (RX handlers) · `node_cascade.cpp` (alt-walk)
- **Spec:** `docs/specs/2026-05-30-r3-data-plane-design.md` · `2026-05-31-r4.5-lbt-design.md` · `2026-06-07-nav-virtual-carrier-sense-design.md` · `2026-05-31-r4-budget-nack-design.md`
- **Mobile marks (codec — §mobile Slice 1).** A mobile uses a home-assigned LOCAL id that can collide with a global id, so **RTS/DATA carry `addr_len=1`** (`next` is a mobile local-id), **RTS a `MOBILE` bit** (byte-5 b1 — the `src`/originator is a mobile), and **ACK a `MOBILE` bit** (byte-1 b1 — the `to` is a mobile local-id); the **ORDINARY CTS relies on the marked-RTS context** (its flags nibble is full), while the **TERMINAL CTS carries an explicit plane bit** (`CTS_TERM_PLANE_BIT`, byte-0 b3 — §2.1), because it ends a flight and may not infer the plane. These keep the mobile plane's local-ids distinct from global ids. The codec round-trips them (marks default `0` → backward-compatible); wire layout = `frames.md`. The mobile plane itself (registration, last-mile, presence) is **§12–14** below.

### 2.2 Hardware TX completion is an attempt outcome, not a send verdict (§T2, 2026-08-14)

On hardware, `DeviceHal::tx()` admits into an eight-entry queue; it does not mean that the frame aired. The
sending site's opaque `TxParams::tag` and flight `seq` travel with that queue entry and the one-deep in-flight
record. They are echoed verbatim on completion—never reconstructed from whichever flight is current later.

`DeviceHal::collect_tx_completion()` reports three mutually exclusive asynchronous attempt outcomes through a
bounded four-entry, drop-newest ring:

- `aired`: the radio produced its TxDone edge;
- `failed`: `start_transmit` rejected an already-admitted queue entry, carrying the exact `TxResult`;
- `unknown`: transmission started, but no TxDone arrived before the watchdog. It may have aired fully, partly, or
  not at all; it is neither `aired` nor `failed`.

These three hardware outcomes carry `BusyReason::none`; only a synchronous/asynchronous `refused` outcome carries
an actual busy reason.

The device loop drains every pending outcome immediately after `collect_tx_completion()` into
`Node::on_tx_complete()`. The core emits `tx_aired`, `tx_failed`, or `tx_unknown` with `tag`, `seq`, and
resolved physical `sf` (`tx_failed` also carries `result`). `failed` and `unknown` are telemetry-and-counters
only: they enqueue no app push, mutate no protocol state, arm no timer, and consume no retry stash. Existing MAC
recovery remains authoritative, so an attempt failure is never presented as a terminal send failure.

**Collection runs BEFORE the node's timers, and pumping runs after (§T3, 2026-08-14).** The completion half and the
"start the next queued frame" half are two separate calls — `collect_tx_completion()` and `pump_tx()` — with the
Node's timer drain between them. The order is load-bearing rather than tidy: `kMBcastClearTimerId` deletes the
in-flight channel-M flight five milliseconds after its calculated airtime, and the ownership rule below needs that
flight alive to attribute the airing to the origination that owns it. With the collection after the timer loop, a
loop pass delayed past both deadlines lost the channel post's completion entirely.

### 2.3 `send_aired` — the one attempt outcome the app hears (§T3, 2026-08-14)

`aired` is the only attempt outcome raised into the app surface, and the reason is monotonicity: it can only ever be
an upgrade from *queued* to *sent*, and no later attempt can contradict it. `failed` and `unknown` are non-monotonic
— a later attempt supersedes them — so presenting either as an outcome would be a false negative in exactly the shape
the premature *sent* was a false positive.

The core enqueues one `PushKind::send_aired` when **all** of the following hold: the outcome is `aired`; the frame's
tag is `FrameTag::data`; a flight is live; the outcome's `seq` equals that flight's `flight_gen` exactly; and the
carrier matches exactly one of two ownership rows.

| carrier | predicate | push fields |
|---|---|---|
| ordinary local DM | `!m_broadcast && !has_previous_hop` | `dst` = the peer, `ctr` = the origination counter |
| locally originated channel post | `m_broadcast && flood && inner_len >= 6`, plus an **active**, **non-holder** `_channel_reoffer_pending` entry whose `id` matches the decoded message id | `dst` = 0, `ctr` = that entry's full 16-bit handle |

Everything else — beacons, RTS, CTS, ACK, NACK, forwarded transit, channel **pull responses** and relay/holder
re-floods — is telemetry only. The `flood` clause is what separates the two channel cases: a pull response carries the
**same** message id as the original post but is not a flood, so without it a pull response airing while the origin's
re-offer slot is still active would report as the original post airing.

`send_aired` is **not terminal and not an acknowledgement**. `send_acked`, `send_e2e_acked`, `send_failed`,
`channel_sent` and the ACK timeout remain the authoritative send-level outcomes and still arrive afterwards. A
consumer applies it under a scoped monotonic rank — `queued < aired < every terminal outcome` — so a repeat is
idempotent and a delayed one can never overwrite a terminal state. Nothing about it is stored in `Node`: the fact is
forwarded at the instant it is established, and the consumer's rank is what makes de-duplication state unnecessary.

Outcome-ring overflow increments `tx_outcome_drops`; failed radio arms increment `tx_failed_arms`. Both are
visible in device `status` as `txoutdrop` and `txfail`, alongside `txdrop` and `txto`. Synchronous
`busy`/`too_long` returns never enter the outcome ring and therefore cannot double-report. All four core
hand-off sites now inspect their synchronous `TxResult`; the two formerly discarded sites report
`tx_hal_rejected` while retaining their pre-existing timeout/recovery behavior.

The simulator still reports only its asynchronous `refused` path through the same
`Node::on_tx_complete()` entry. It does not synthesize hardware `aired`/`failed`/`unknown` events, so those
three outcomes remain corpus-dark and are covered by the native DeviceHal tests.

## 3. Beacons

In **discovery** (first ~60 s, or route-starved) a node beacons fast + full-page and broadcasts `Q:REQ_SYNC` to pull
neighbours' tables; in **steady state** it sends dirty-only differential beacons under an adaptive channel-busy
throttle. (Gateways override this — see §6.)

**Team plane (§team-parity T4, 2026-07-28).** The same pull exists for team members as `Q:TEAM_SYNC`, carrying the
`team_id` as a 4-byte scope so it is decidable from the frame alone. A **team member with an adopted
`team_local_id`** may emit it — including an **off-grid** member (`node_id == 0`), which the static-plane guards
had also been refusing. It is answered **only by same-team members**, with the team-tagged `"sync"` beacon
carrying the full `_rt_team` table; a static node drops it at the `handle_q` dispatch site *before* the dedup
ring, so it spends no state. The **mixed-leaf exemption applies** — a teammate on a foreign leaf nibble still
answers, because a mixed-leaf team is supported by design and is what shipped to metal. `Q:CHANNEL_PULL` does
**not** get that exemption: it carries no `team_id`, so there is nothing to scope the exemption on.
Static-plane `REQ_SYNC` remains forbidden to mobiles; the team pull **refuses rather than downgrading** to one,
which would air the sender's node_id into every static `_rt`.
- **Source:** `node_beacon.cpp` (`emit_beacon`, `periodic_beacon_fire`) · `node_query.cpp` (REQ_SYNC, TEAM_SYNC)
- **Spec:** `docs/specs/2026-05-29-r1-beacon-emit-design.md` · `2026-05-29-c5-bcn-design.md` · `2026-05-31-r4.3-beacon-throttle-design.md`

## 4. Routing-liveness

Reception-driven freshness: any frame heard from a neighbour stamps it fresh (`mark_dest_seen`); repeated RTS
timeouts escalate a peer suspect→silent→dead, applying a score penalty so routing avoids it; tiers gossip to peers.
A route's next-hop must be fresh to be viable (cross-layer gateway routes are exempt).
- **Source:** `node_routing.cpp` (`is_next_hop_fresh`, `record_peer_rts_timeout`, `liveness_penalty_q4`)
- **Spec:** `docs/superpowers/specs/2026-06-17-routing-liveness-plane-port.md` · `docs/specs/2026-05-31-r4.2-tier-penalty-design.md`

## 5. Asymmetric-link bidirectionality

A per-next-hop plane **orthogonal to liveness**: it scores down a link that is **one-way** — we hear a neighbour but it can't hear us, so the RTS→CTS→DATA→ACK handshake can never complete (liveness is beacon-cleared, hence blind to this). Detection is **proactive gossip** — a node advertises its complete `hops==1` heard-set (its direct-neighbour route entries) under a beacon `heard_set_complete` flag; a receiver absent from a *complete* heard-set learns the link is one-way, present ⇒ confirmed bidirectional (a real CTS also confirms). The verdict adds a **sort-only** penalty (never a `next_hop_selectable` hard gate — a sole one-way route still flies) and rides a transitive `degraded` route-entry bit so the mesh routes around it; a doomed sole route slow-re-probes once per TTL instead of RTS-storming, and recovers for free when the link flips back. Keep-don't-delete + backward-compatible (both wire bits default 0). Gateways skip the census (leaf-only — the leaf→gateway direction is liveness-backstopped).
- **Source:** `node_routing.cpp` (`bidi_penalty_q4`, `candidate_degraded`, `note_link_confirmed`) · `node_beacon.cpp` (`update_link_bidi_from_beacon`, the census in `emit_beacon`) · `node_cascade.cpp` (the one-way slow-reprobe)
- **Spec:** `docs/superpowers/specs/2026-06-29-asymmetric-link-aware-routing-design.md`

## 6. Gateway dual-layer

A gateway time-multiplexes two leaves: each leaf owns a window on an **absolute grid** (`epoch + k·period`); a busy
switch slips to protect an in-flight exchange but snaps back to the grid (bounded drift). It advertises a per-leaf
schedule (receiver-anchored countdown) and beacons a leaf **reactively** (dirty / `Q:REQ_SYNC` + a duty-gated
heartbeat — not on a timer). Senders defer their RTS to the advertised window + a herd-jitter spread sized from the
gateway's 1-hop herd × the airtime-computed exchange. Cross-layer DMs bridge via a layer-path; each layer may run its
own RF frequency (provisioning-only).
- **Source:** `node.cpp` (`window_switch_fire`, `window_grid_now`, `activate_layer`, `gateway_schedule_defer_ms`, `gateway_spread_nibble`, `exchange_airtime_ms`) · `node_beacon.cpp` (schedule emit, `maybe_emit_gateway_beacon`) · `node_cascade.cpp` (`gateway_doorstep_hold`)
- **Spec:** `docs/superpowers/specs/2026-06-12-gateway-dual-layer-design.md` · `2026-06-14-multihop-gateway-discovery.md` · `2026-06-19-gateway-provision-command-design.md`

## 7. Channel plane (group messages)

Leaf-scoped broadcast groups: a message rides a managed **flood** (FLOOD RTS-M + coverage bitmap, 1-hop-suppressed
re-flood) on the data SF; a BCN channel-digest + `Q:CHANNEL_PULL` are the repair backstop for misses. **Principle 11:**
a dual-layer gateway is entirely out of the channel plane (never originates, pulls, or bridges channel traffic).
**Plane-keyed coverage (§S7):** the SAME flood coverage/re-flood/backoff code serves both planes — a team-scoped
flood's coverage consults the TEAM peer set, a static flood the static rt (+ the home's hosted mobiles, so
registered mobiles RECEIVE leaf channels, receiver-only — they never re-flood; off-grid mobiles receive none).
`send_channel`: `-t` = team · plain/`-g` = GLOBAL (a registered mobile delegates to its home, which mints) ·
`-t -g` = both. ⚠ a team mobile's plain send flipped team→global (D17, DM symmetry).
- **Source:** `node_channel.cpp` (`process_channel_digest`, `channel_origin_admit`, `flood_forward_decision`)
- **Spec:** `docs/superpowers/specs/2026-06-08-channel-flood-redesign.md` · `2026-06-09-lean-channel-m-frame-design.md`

## 8. E2E DM crypto

Opt-in sealed-sender DMs: X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305 seals `origin` + everything after it
(only `dst_key_hash32` stays cleartext as AAD). The receiver recovers the sender by **trial decryption** over its
cached peer keys; no candidate opens ⇒ silent drop. Peer keys are provisioned via the H `WANT_PUBKEY`/`REQ_PUBKEY`
mutual exchange; for a **registered mobile** the mutual half is home-mediated (§S3): the home answering on the
mobile's behalf also caches the requester's attached key and last-miles it to the mobile
(`MOBILE_KEY_FORWARD`), and the mobile TX-free-caches a requester key it overhears for its own hash — so a
sealed DM to a mobile opens. **First contact needs no key round at all (§S2 INTRO):** a plaintext
hash-addressed DM to a peer whose custody is unconfirmed auto-attaches the sender's pubkey (+33 B once);
the reply attaches back by the same rule ⇒ mutual keys in one round trip, then sealed traffic sets
`peer_confirmed` and attaching stops. `cfg set intro_attach` opts out; `-K` suppresses one send.
**Cross-layer + delegated sealed DMs ride `SEALED_RELAY` (§S4):** the sealer seals same-layer-shaped under a
CARRIED `seal_ctr` (so a delegating home re-originates under its own frame ctr without re-sealing) and the
recipient does a DIRECTED open keyed by the clear `SOURCE_HASH` (sealed-vs-clear cross-checked). ⚠ these are
sealed-CONTENT but attributable-envelope; sealed-SENDER privacy (trial-decrypt, origin hidden) holds only for
same-layer direct sealed DMs. XL E2E-acks are cleartext, parity with same-layer acks. Optional 6-B sender
location rides the sealed inner.
- **Source:** `dm_crypto.{h,cpp}` · `node_mac.cpp`/`node_mac_rx.cpp` (seal/open at enqueue/deliver) · `node_hashlocate.cpp` (pubkey resolution)
- **Spec:** `docs/superpowers/specs/2026-06-16-e2e-sealed-sender-redesign.md` · `2026-06-15-phase1-e2e-dm-crypto.md` · `2026-06-16-e2e-peer-key-provisioning.md` · `2026-06-14-location-propagation.md`

## 9. Identity / join

Node-ids are claimed by **Duplicate-Address-Detection** (listen → pick a free id → CLAIM → adopt unless DENY'd);
`key_hash32`-only tiebreak (lower wins). Reserved id bands: 0 unprovisioned, 1–16 gateways, 17–254 normal, 0xFF
sentinel (the reservation is a Join-time convention, enforced there — not at config time).
- **Source:** `node_join.cpp` (`join_start_claim`, `handle_j`)
- **Spec:** `docs/specs/2026-06-05-node-id-auto-assignment-design.md` · `docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md` · `2026-06-15-join-e2e-phase0.md`

**Id 0 is never bound (`§id0-never-bound`).** `set_identity()` sets `_node_id` / `key_hash32` / the Hal short-id
unconditionally but writes the authoritative **self-binding only for a non-zero id**, matching `on_init` and
`activate_layer`. Previously every `set_identity(unjoined_node_id, …)` minted an authoritative
`{node_id:0, our key_hash32}` row — persisting after `forced_rejoin` (the heal deliberately keeps its routes) and
`mobile_reset_registration`, and **at boot on an unprovisioned device**: `fw_main.cpp:659` runs 46 lines *before*
`on_init` (`:705`), so `on_init`'s `if (_node_id != 0)` guard — written for exactly that case — was defeated by
ordering. ⚠ Separate and still open: `handle_h_query` and `request_resolve` read `_node_id` directly, so an
unprovisioned node still answers an own-hash locate with **0**.

## 10. Hash-locate

H-frame flood resolves an identity `key_hash32` → `node_id` (soft = any cache answers; hard = owner-only) and, with
`WANT_PUBKEY`, the peer's E2E pubkey; the answer routes home as a DATA `H_ANSWER`. Relays cache answers on-pass.

**Mobile locate (§mobile §4).** A mobile has no global id — it's addressed by its stable `key_hash32`. Its **registrar (home_node) proxies** that hash: on an H-query for `M` the home answers a **`MOBILE_H_ANSWER (M → home_id)`** (a distinct DATA TYPE, always *claimed*). The sender caches `M → home` in a **separate mobile-home cache — NOT `id_bind`** (the mobile's LOCAL id must stay out of the global id-plane), keyed by the registration **epoch** so the **freshest** home wins during an old+new-home overlap. A DM to the mobile then routes `dst=home, dst_hash=M` and the home does the last mile (the `addr_len=1` mark). **Staleness (three overlapping heals, §S6):** the **NEW home** sends the `MOBILE_BREADCRUMB` to the old home on adopt (home-originated since S6 — survives a sleeping mobile; same-layer today), the old home thereafter **redirecting** (`MOBILE_H_ANSWER (M → new_home)`); the mobile's next **P-probe** naming its selected home makes any OTHER registry holding its hash **prune instantly** (§14); the liveness prune is the backstop. Spec `2026-07-08-mobile-slice4a/4b` + `2026-07-17-…-first-contact` §S6.4-D.
- **Source:** `node_hashlocate.cpp` (`handle_h`, the proxy + `on_mobile_hash_bind_response`) · `node_join.cpp` (`presence_notify_old_home`)
- **Spec:** `docs/specs/2026-05-29-c3-h-f-floods-design.md`

## 11. Anti-spam

Airtime *fairness* layered on the duty-cycle governor: the duty plane bounds each node's **volume**, anti-spam adds **fairness** (no origin hogs the shared air) + **smoothness** (burst floors). Two planes, split by what a relay can see:
- **Channel (group) messages** — a per-**cleartext-origin** cap = a fair share of the leaf's duty-bounded channel capacity `C = D/T_ch` among the active originators (SF- and mesh-aware, `∝ 1/N`), plus a 10-s spacing floor. Enforced at the receiver (drop the over-cap re-broadcast) and self-applied at origination. A dual-layer **gateway is exempt** — it bridges, never originates.
- **Direct messages** — the plain duty budget + a per-**physical-sender** measured-airtime backstop at each relay (keys on the immediate sender, never the **sealed** e2e origin — so it works without seeing who a DM is from), plus a 3-s self-spacing floor. **E2E delivery-acks are exempt** from the backstop (throttling an ack is self-defeating), guarded by a verify-at-DATA anti-spoof.
- **Companion** — an advisory `limits` query ("next send in N s" + live caps) and send-outcome feedback (`send_blocked` / `send_failed` / `channel_sent`) so the app paces itself.
- **Tunables** (per-leaf, on the C frame): `channel_active_fraction`, `channel_min_interval_ms`, `dm_min_interval_ms`.
- **Source:** `node_routing.cpp` (`channel_cap_origin`) · `node_channel.cpp` (channel admit + self-gate) · `node_mac_rx.cpp` (DM backstop + e2e-ack exempt/anti-spoof) · `node_mac.cpp` (DM floor)
- **Spec:** `docs/superpowers/specs/2026-06-30-antispam-duty-channel-cap.md` · **user guide:** `docs/anti-spam.md`

## 12. Mobile node (home / care-of)

A **mobile** is a roaming endpoint: a stable `key_hash32`, a home-assigned LOCAL id, reachable by hash — the
mesh resolves hash → its current **home** (§10 proxy), routes the DM to the home, and the home does the
`addr_len=1` last mile. A registered mobile never floods on the static plane itself: hash-addressed sends are
**delegated** to the home (`DATA_TYPE_MOBILE_SEND` wrapper), which re-originates with `SOURCE_HASH`=M so replies
and E2E-acks route back (the delegated ctr map translates them). Registration rides the J family (leaf-exempt
DISCOVER + targeted, jitter-stashed OFFER + CLAIM); a reprovision (`join`/`create`/`leave`) wipes the host's
registry + suspends OFFERs and rosters while `_node_id == 0`.
- **Source:** `node_mobile.cpp` (the FSM) · `node_join.cpp` (host side) · `node_hashlocate.cpp` (delegation, `:866`) · `node_mac_rx.cpp` (`MOBILE_SEND` unwrap, last-mile)
- **Spec:** `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md` · `2026-07-17-cross-layer-mobile-first-contact-design.md`

## 13. Team plane

**Switching teams (`§clean-team`, 2026-07-27).** Every live `team_id` change — `team new`, `team <id>`, `team 0`
— goes through **`Node::set_team_id()`** (★ `cfg set team_id` was a FOURTH spelling and was **REMOVED 2026-07-31**, `§team-id-cfg-removal`: it had none of the three guards `team <id>` carries), which drops the whole team plane before the new id
takes effect: `_rt_team`/`_team_peer`, the team liveness mirror, the **team key cache `_team_keys`**, the team RREQ
dedup/rate rings, and `_team_local_id`. It returns *switched*, so a same-team no-op clears nothing and skips the
re-DAD. ⚠ **The static plane is deliberately untouched** — a homed mobile changing team keeps its routes,
id-bindings, gateway schedules, hosted mobiles and home registration; only `join`/`create`/`leave` wipe both
(`clear_routing_state()`, which calls the same helper). **Boot assigns `team_id` directly and must not clear** —
nothing is stale at boot, and the persisted DAD id is loaded immediately after.
Why it matters: `rt_find` dispatches on `is_team_peer` with **no `_rt` fallback**, so a stale team peer does not
merely misroute — it **shadows the static plane**. And a stale `_team_keys` entry is worse than a stale route: it
maps a reused team-local id to the *previous* team member's `key_hash32`, i.e. **mis-addressing** (wrong DST_HASH
at `node_mac.cpp:94`; hash → wrong teammate at `node_hashlocate.cpp:988/1019`). `is_team_peer` does not guard it —
the bit is set from a multi-hop DV entry that carries no key.
**The channel plane too (`§clean-team-channel`).** `do_data_tx` stamps every emitted team-flavored M frame with the
*current* `team_id`, so a team-scoped channel payload that survives a switch is re-broadcast **into the team you
just joined**. `set_team_id()` therefore also calls **`purge_team_channel_state()`**, which drops that payload from
all **four** carriers it is copied into — the buffer row, an in-progress `_flood[]` state (★ it re-floods from its
own cached `fs.body` and never reads the buffer, so dropping the row alone would **not** stop it), a staged
`_tx_queue` item, and the in-flight `_pending_tx` — plus any `_channel_reoffer_pending` slot they orphan. It is a
**selective** compaction keyed on the row's own scope (`team_id != 0 || flavor & channel_flavor_team`) with no
old-id comparison, because both writers stamp the team live at that moment: **the node's non-team leaf channel rows
survive**, since a registered team mobile is a full leaf-plane participant. `team 0` (leave) purges as well — a left
team's messages must not be servable — and `do_data_tx` now **refuses** to air a team-flavored M while
`team_id == 0`: it would carry `team_id` 0, which parses as a **plain leaf message** and would leak the team's
content to every static node on the leaf.
**The leaf axis too (`§clean-join-carriers`).** The same disease sits on the network-scope axis: `do_data_tx` and
both RTS builders stamp `leaf_id` (and `src` from `_node_id`, and the PHY) from the **live** config at TX time, so a
staged/in-flight frame outliving a `join`/`create`/`leave` is re-stamped onto the **new** leaf. For a DM this is
**mis-delivery**, not merely a scope leak: `clear_routing_state()` has just wiped `_rt` and `_id_bind` — the state
the flight's `next` hop was chosen from — and `reset_join_for_reprovision()` has already set `_node_id` to 0, so the
frame airs claiming **`src = 0`** while its payload still carries the old network's `origin`. `clear_routing_state()`
therefore calls the **same** sweep as the team switch, `purge_tx_carriers()`, with `PurgeAxis::reprovision`: **one
mechanism, two predicates** — the team axis drops team-scoped rows and **keeps** the leaf plane, the reprovision axis
keeps **nothing** and sweeps **every** leaf (a gateway stages bridged DMs on both, and `leave` is dispatched on the
gateway build). The sweep is no longer team-feature-gated, since the reprovision axis has nothing to do with teams.
**And it tells the app (`§reprovision-push`).** A dropped carrier that is a DM **this node originated** now pushes
`send_failed{reason:"reprovisioned"}` — the staged `_tx_queue` item, the `_pending_tx` flight, and the parked
`_deferred` send — so a companion's future completes instead of hanging until its own timeout. A staged **channel M**
and a **transit leg** push nothing: neither has a local app future (a channel post's future is `channel_sent`, owned
by the re-offer slot). The reason is distinct from `no_route` on purpose: a reprovision changes the *network*, so the
old `dst` id is void and the app must **re-address** rather than retry. The **team axis pushes nothing** — it drops
only `m_broadcast` channel Ms. ⚠ Still open: a re-offer slot dropped by the purge strands its `channel_sent`, exactly
as plain buffer eviction already does (`node_channel.cpp:676`).
⚠ Already-**packed** stashes (`_deferred_lbt`, `_rts_duty_defer`, `_tx_stash`, and the jittered H/F/OFFER stashes)
are deliberately **not** purged: their byte-0 leaf nibble froze at pack time, so a late fire is rejected by the new
leaf's gate — wasted airtime on the leaf just left, never mis-delivery into the one just joined.

A `team_id`-scoped overlay of mobiles (member-to-member routing + group chat), fully **separated** from the
static plane and from other teams: team beacons/DV (`_rt_team`), team F discovery, a team-scoped H-flood, and a
`team_id`-tagged channel — a static node never learns, relays, or ingests any of it (the s18 byte-identity
tripwire + s22/s24/s25 assert both axes). Members self-assign `team_local_id`s by team-DAD (no host needed —
an off-grid team routes among itself); a DUAL member (also home-registered) keeps the two id spaces distinct.
Reaching a teammate REQUIRES the team plane (`send -t`); a plain send is global/home.
**Leaf-agnostic (P2-1, 2026-07-20):** team membership is `team_id`, never `leaf_id` — a team's `create` defines
the PHY all members share, and members homed onto DIFFERENT layers (mixed leaf nibbles) stay team-reachable:
every team-scoped RX path (beacon, H, F, RTS/MAC, channel-M) accepts a same-team frame despite a foreign leaf
(`Node::same_team()` is the ONE predicate; statics keep the plain leaf gate → byte-identical). Corollary
(ruled option (a)): a TEAM member **refuses to home** onto a layer whose PHY (freq/bw/routing-sf/cr) differs
from its team-provisioned `layers[0]` — `mobile_home_phy_mismatch`, stays off-grid-but-team-reachable;
cross-LAYER same-PHY re-home is the supported case. (Residual: the explicit `mobile register freq=/bw=`
console path bypasses the guard — operator's deliberate choice.)
- **Source:** `node_beacon.cpp` (team beacon/DV) · `node_route_discovery.cpp` (team F) · `node_hashlocate.cpp` (team H) · `node_channel.cpp` (team M) · `node_mobile.cpp` (team-DAD)
- **Spec:** `docs/superpowers/specs/2026-07-15-team-plane-routing-parity-design.md` · `2026-07-16-team-plane-liveness-2c-design.md` · `2026-07-10-protocol-plane-separation.md`

## 14. Presence plane (mobile ↔ home)

Replaces every periodic mobile↔home poll (the 10-min re-CLAIM keepalive, the 10-min layer-directory pull, the
pubkey push — all retired) with two **leaf-free, unacked 1-hop broadcasts**: a mobile's tiny **P-probe**
(quality-adaptive period, jittered; carries its selected-home pair, optionally its last home + its pubkey =
key custody) and the home's coalesced **P-roster** (its hosted list + per-mobile 2-bit link quality + `has_key`
+ the layer-directory `dir_epoch`; one roster refreshes every listener). Semantics: absent-from-roster ⇒
re-register now; probe naming ANOTHER home ⇒ that registry prunes instantly; unanswered probes ⇒ home lost in
minutes (not the old 30); weak quality ⇒ pre-scan and re-home (bottleneck-direction ranking, dwell + hysteresis)
while both homes are alive — the new home then notifies the old (§10 staleness).
⛔ **CORRECTED IN PLACE 2026-08-11 (§MH-S5b-ii): this line said *"weak quality ⇒ pre-scan and **PROACTIVELY** re-home"*
and that word is now FALSE.** A weak home still unlocks the re-home EVALUATION but **no longer makes the mobile
canvass**, and a switch needs a verified echo that only a canvass can produce ⇒ **the switch is reachable only once the
home starts MISSING checks.** (⚠ candidate *collection* was never gated on quality at all — `presence_note_candidate`
is unconditional.) [[B178]] holds the deferral and the block below states the limitation in full.

★★★ **WHEN A PROBE IS `SEARCHING`, AND WHY THE LIST IS CLOSED — new in §MH-S5b, NARROWED TO THREE ENTRIES BY
§MH-S5b-ii (both 2026-08-11).** A probe is
`searching` exactly when it names no home (`selected_home_id == 0`); a *selected* probe is answered only by the home
it names, a *searching* one by **every** eligible home **and with an echo of the prober's own hash**. That echo is the
only thing that can prove the reverse link, so it is the only thing that can authorise a voluntary switch (below).
The permitted reasons are a **closed list**, because a fleet of mobiles each canvassing for a better home is a
fleet-wide roster storm bought with nothing:

1. while `claiming` — the §MH-S4b confirmation solicitation;
2. on home **loss** — the immediate recovery canvass;
3. **the home missed a check** (§MH-S5b, `_presence_miss > 0` — ⛔ an *admitted* miss; a probe our own transmitter
   refused is not one).

⛔⛔ **AND A FOURTH REASON WAS IMPLEMENTED, MEASURED AND *DEFERRED* — [[B178]], §MH-S5b-ii (2026-08-11). ⛔ This list
previously carried *"the home's reported quality is weak or critical"* as a live entry; that is corrected in place, not
appended to.** §MH-S5b landed it (`_presence_prescan`) and the corpus priced it: **+31 % P-roster airtime**, `s07`
collisions 2775 → 3528 and **6 unique deliveries lost, all in `s07`** (734 → 728) — a fleet-wide roster storm arising
from a trigger the design itself permits. ★★ **THE LIMITATION THAT LEAVES, stated rather than implied: a weak but
CONSISTENTLY RESPONDING home will not proactively initiate candidate verification, so the mobile changes home only
AFTER connectivity begins failing.** ⇒ **a CONSERVATIVE INTERIM POLICY, NOT completed proactive roaming**; the design's
§8.3 is **NOT** satisfied and §S6.4-C's *leave a weak home BEFORE loss* purpose is **NOT** met. ⓘ A weak home still
**unlocks the switch evaluation** (`_presence_prescan`'s remaining role) and candidate collection was always
unconditional — what the mobile no longer does is spend airtime **asking**, and without an echo the evaluation has
nothing verified to select. What returns is a narrower trigger (weak home **and** a fresh, compatible, passively observed,
still-unverified candidate whose one-way quality could satisfy the two-tier rule, with hold and dwell already served),
after [[B177]] is fixed.

★ It is a **KIND** decision, never a cadence one: the frame is the same 8 bytes on the same deadline, so the mobile's
own airtime per unit time is unchanged at every quality tier. What *is* new airtime is the other homes' answers, and
the home-side coalescing plus the 10-second `presence_roster_min_interval_ms` floor remain the shared de-storm.
★★ **And a searching probe from a mobile the home actually hosts refreshes that row** — `last_heard_ms`, the
per-mobile SNR EWMA and §S6 A.4 key custody, exactly as a selected check probe does. It is gated on the row being
**live and direct** (an expired or redirect row must not be resurrected) **and on the probe's `reg_epoch` matching the
row's**, because a searching probe names no home: without the epoch term an old home would keep refreshing a row the
mobile left behind, and the instant `presence_prune_stale` self-heal does not fire on a probe that selects nobody.

★★★ **THE EPOCH-BEARING P PROBE IS THE *SOLE* ONGOING AUTHORITY OVER A HOSTED ROW — new in §B177-FIX (2026-08-11,
owner-ruled, ledger §1.16), and it corrects two things this document said in place.**
- ⛔ **A BEACON NO LONGER REFRESHES A HOSTED ROW AT ALL. It is a presence/candidate HINT.** The old touch matched the
  registry row by **hash alone**, so it restamped a **redirect** row past §9.2's breadcrumb clock, **resurrected an
  expired** row before compaction (ledger §1.14), and — the BCN wire carrying **`key_hash32` and no `reg_epoch`** — kept
  a **pre-re-home** row alive at an old home for as long as the mobile stayed audible there. ⛔ It could not be *gated*
  into correctness for exactly that reason, which is why the resolution is a **removal**; and ⛔ **no epoch byte or TLV
  was added to the beacon** (permanent airtime for a mechanism the P plane already provides). **A stationary mobile is
  still covered because it is the case that probes:** its adaptive check cadence is 1–8 minutes, inside the 25-minute
  expiry.
- ★ **Both probe arms now ask the same tuple.** The **selected**-check arm previously refreshed on `sel_me` plus a
  hash match — no live-direct term and no epoch term (`sel_me` proves only that the probe names *us*). The two arms
  therefore disagreed and the older was the weaker; they now share one predicate, `(live · direct · epoch matches)`.
- ⓘ **A refused refresh is never a refused answer** (C2): the probe is still ingested and still gets its coalesced
  roster, and that roster carries the row's **own** `reg_epoch` — which is the evidence the mobile re-registers on.
- ⚠ **MEASURED COST, recorded not smoothed over:** removing the beacon touch moves **one** corpus row
  (`s07_seattle_mobile_meshroute`) and costs **5 unique deliveries there (737 → 732)**, taking the total **below the
  then-`≥733` floor ⇒ ★ **the canonical floor was RE-SET to `≥732` (owner-ruled 2026-08-11, provisional pending [[B163]]; ledger §1.17), on the measured ground that the −5 is a static↔static collision reshuffle with ZERO observed mobile-delivery delta**. The selected-arm half is **byte-inert on all 36 rows and delivery-neutral**. Both attributed by
  in-tree A/B; the disposition is the owner's ([[B177]] / `simulation/BASELINE.md` §B177-FIX).

★★★ **A VOLUNTARY SWITCH REQUIRES A VERIFIED ECHO — also new in §MH-S5b.** All six conjuncts must hold: fresh
evidence (within `mobile_liveness_ms`), **`echo_tier != 0xFF`, i.e. the candidate echoed one of OUR OWN probes**,
a bottleneck ≥ `presence_rehome_tier_delta` (2) tiers better, the 60-second `presence_candidate_hold_ms`, the
5-minute `presence_rehome_dwell_ms` anti-flap, and a compatible `wire_version`. ⛔ **A beacon, or a roster with no
echo of us, is only a HINT** — it proves reception in one direction and willingness to host in neither. Since only a
verified candidate can be selected, the layer-nibble equality test is gone: a verified candidate may advertise
another full layer id (same PHY by construction — the table only ever holds what was received on the tuned PHY),
while the team-PHY restriction is untouched, because the move is *reset + ordinary J discovery* and `team_phy_ok`
gates that.

★★ **Host rows are MORTAL, and this is new in §MH-S5 (2026-08-10).** Until that slice `protocol::mobile_liveness_ms`
(25 min) had exactly ONE consumer — the hash-locate proxy gate — so past the boundary the home stopped answering FOR
a silent mobile and changed nothing else: the row stayed in the registry, in every roster, and holding its local id
for ever. Now `mobile_reg_age_out()` **physically compacts the row out** at that boundary through one
`mobile_reg_remove(slot, reason)` primitive that also compacts every parallel array. It is a **deadline scan on the
existing periodic aging sweep** (no timer id was available: the wheel's cap is 91 and all are consumed) and it is
additionally run **before an id is allocated** and **before a roster is emitted**, so the 60-second sweep period is
invisible to the two decisions that could otherwise act on a corpse.
- **A REDIRECT row gets its OWN full lifetime, stamped at BREADCRUMB RECEIPT**, so a sender holding the 5-minute
  mobile-home cache always has a bounded window to follow the breadcrumb instead of being black-holed.
- ★ **The asymmetry is intended:** the mobile detects loss in minutes (§14.2), the old home deliberately retains the
  row far longer so delayed traffic can still be redirected. Silence eventually expires both sides; nothing is immortal.
- ★★ **A local id may therefore be REUSED, and every last-mile decision is HASH-ANCHORED for exactly that reason.**
  A mobile that returns after half an hour finds its remembered local id may belong to somebody else: its stale P
  exchange cannot confirm (the roster carries that id under another hash), it enters `recovering`, and it is issued a
  currently-free id by ordinary discovery. ⛔ **The epoch is NOT the collision protection** — it distinguishes
  generations of ONE mobile, never two different mobiles; hash matching plus reservation-aware id selection do, with
  the targeted CLAIM DENY as a residual race backstop only.

### 14.1 Attachment is CONFIRMED, and the three planes are separate (§MH-S4, 2026-08-08)

⛔ **Registration is not one-dimensional.** Three different questions have three different authorities, and no
answer may be substituted for another:

| Plane | Question | The only authority |
|---|---|---|
| **Attachment** | *Which static node believes it is our home?* | a matching chosen-home **P roster** carrying our **(hash, local id, reg_epoch)** |
| **Home link** | *Can this mobile and that home currently communicate, both ways?* | a recent **correlated bidirectional exchange** with that home |
| **Mesh service** | *Can that home reach a particular destination?* | the **result of that specific route/send** |

★ Presence can establish attachment and home-link confidence. It **cannot** assert general mesh connectivity: a
roster proves the home holds our row and answered us, and nothing beyond that home.

**Attachment states:** `dormant` (no attachment requested) · `seeking` (DISCOVER cycles, no chosen home, no
prior attachment) · `claiming` (a home/local id was chosen and the CLAIM crossed the admission boundary;
confirmation pending) · `attached` (the chosen home's roster confirmed the triple) · `recovering` (a previously
attached home was lost).

**Home-link states, independent of the above:** `unknown` · `confirmed` · `checking` · `lost`. The two are
**orthogonal** — `attached` + `checking` is normal and must render as such.

**Confirmation without a new wire message (no frame or field was added).** J CLAIM stays idempotent and
claim-stands on the home; P already carries the proof. After choosing an OFFER the mobile adopts the offered PHY
and local id **provisionally** and enters `claiming`; only the first chosen-home roster matching all three of
`(hash, local_id, reg_epoch)` promotes it to `attached`, sets the home link to `confirmed`, stamps the
confirmation age and emits the app-facing registration event. ⛔ A **two-of-three** match never confirms: a
wrong epoch or a wrong local id is a fail-loud re-register, not a confirmation.

**A lost CLAIM heals itself.** A chosen-home roster that omits our hash while we were never confirmed, or
silence at the confirmation deadline, re-sends **the same CLAIM — same chosen host, same local id, same epoch** —
up to `presence_claim_max_retries` (one shared budget for both triggers, because both mean "the CLAIM was not
recorded"). After exhaustion the mobile returns to `seeking`; it never remains falsely registered. ⓘ A home
**ignores** a check probe for a hash it does not host, so with an empty home the *silence* trigger is the only
one available — which is why the budget is shared rather than per-trigger.

**Home-link confidence rides the existing adaptive P cadence — no new periodic protocol and no new steady-state
airtime.** A matching roster confirms both planes at once. An admitted-but-unanswered probe moves the link to
`checking`; `presence_probe_k_miss` of them reach `lost`. ⛔ Inbound **beacons alone are one-way hints** and
never confirm a link. ⛔⛔ A local **TX-admission** failure — an LBT deferral, a full defer ring, a HAL refusal —
or an unrelated route failure moves **nothing**: it is a statement about this node's own transmitter, reported
as the last attempt result only ([[B139]] was exactly this leak at the probe site). ★ The accepted consequence:
a mobile that walks away from a **strong** home right after a confirmation and then generates no traffic can take
≈8 minutes plus jitter and retries to notice — the deliberate price of not adding a keepalive to a duty-cycled
link, not a defect.

★ **Every surface therefore says "home link" and renders the AGE of the latest confirmation** — "Home confirmed
7 min ago", never an unqualified "connected".

- **Source:** `node_mobile.cpp` (`presence_probe_fire`, `presence_ingest_roster`, `presence_maybe_rehome`) · `node_join.cpp` (`presence_ingest_probe`, `presence_emit_roster`) · `frame_codec.cpp` (`pack/parse_p_*`)
- **Spec:** `docs/superpowers/specs/2026-07-17-cross-layer-mobile-first-contact-design.md` §S6 · `2026-08-07-mobile-home-attachment-reliability-design.md` §4.1/§7 (§MH-S4) (wire = `frames.md` §P)

---

*Device-side concerns (console/`cfg`, NV blob, BLE companion, OTA, persistent inbox) are firmware integration, not
mesh protocol — see `src/fw_main.cpp` + the `docs/superpowers/specs/2026-06-10-*` / `*-inbox-*` specs.*
