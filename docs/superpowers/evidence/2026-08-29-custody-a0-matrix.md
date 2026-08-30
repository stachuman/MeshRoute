<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §A0 — DATA-path characterization matrix, ownership trace, census and pre-Slice-A baselines

*2026-08-29. Coder-authored evidence for **Slice A0** of
`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` (§17 + §18.0). This file is the
matrix artefact the spec's dispatch operationalization requires. It is **evidence, not a maintained document** —
the register, the spec, the bench script and `simulation/BASELINE.md` stay supervisor-landed from the drafted text
in §7 below.*

**⛔ A0 CHANGED NO PRODUCTION CODE.** See §6.1 for the raw-`git diff` proof. Every finding below is an
observation; not one of them was acted on in this slice.

**Tree:** `HEAD = 391e923` ("doc fix"), clean at dispatch and carrying no other slice's uncommitted work — so the
C1 proof is the clean form (raw diff against HEAD), not a starting-snapshot reconstruction.

---

## 0. What was and was not established

| | |
|---|---|
| **Established** | The full DATA universe is enumerated (§1) with per-row producers, relay treatment, addressed consumer, persistence, outcomes, flags, feature gate and executable coverage. The send/receive ownership trace names a component per result (§2). The census located every raw numeric type comparison, every duplicated policy list and the unknown-type fall-through (§3), each with a disposition. Ten characterization cases are landed and all seven mutations are RED (§4). Native, the COMPLETE 36-row corpus, the warning census and the ruled board pair are recorded (§5). |
| **Not established** | No production defect was fixed. No document was corrected (§7 carries the drafts). The corpus's 15 pre-existing movers are **recorded, not explained** — attributing them is B159/B134/B251/B260's business, not A0's. Metal residue: **none** — A0 adds no device-reachable behaviour, so nothing is owed to the bench script. |
| **Blockers** | **NONE.** No finding blocks Slice A. The strongest finding (A0-F1) is already inside the designed arc's scope (Slice E), so it is an adjacent defect with a named close-by, not a blocker. See §6.3 for why that call was made rather than escalating. |

---

## 1. The matrix — the FULL DATA universe

Per spec §18.0.1 the matrix covers more than the allocated enum. The five **named special-row classes** are
carried explicitly and are keyed by the tokens the structural control (`tools/check_a0_matrix.py`) requires:
`UNTYPED_DM`, `ENCLOSED_ONLY`, `ALLOCATED_NOT_EMITTED`, `UNKNOWN_REPRESENTATIVE`, `TOMBSTONE_NON_WIRE`.

### 1.0 The length authority and the flags/TYPE-byte interaction (applies to every row)

`data_frame_len` / `data_inner_cap` (`lib/core/frame_codec.h:729` / `:734`) are the ONE length authority
(§B20/B21). Both spell the same terms in the same order:

```
overhead = DATA_HDR_LEN + (type != 0 ? 1 : 0) + data_mac_len(flags)
```

⇒ **the TYPE byte costs exactly one inner byte, and only when `type != 0`.** `data_mac_len` (`:710`) is 8 under
`DATA_FLAG_CRYPTED` and 4 otherwise. The APP bit is **derived, never set by hand**: `pack_data`
(`frame_codec.cpp:915`) computes `type != 0 ? (flags|APP) : (flags & ~APP)` and emits the byte at offset 8 at
`:925`. ⚠ `parse_data` does **not** enforce the converse — see A0-F6.

**Standing context, V1-verified against this tree, not against the brief:**
- The DATA flags byte is **exhausted**: all eight bits are allocated (`frame_codec.h:684-698`).
- `DATA_FLAG_PRIORITY` (`0x01`) is aliased LIVE as `DATA_FLAG_MS_ENCLOSED_TYPE` (`frame_codec.h:704`) on the
  homed-mobile path. Its three writers are `node_channel.cpp:811`, `node_hashlocate.cpp:1742` and `:1754`; its two
  behavioural readers are `node_mac_rx.cpp:1581` and `:1638`. ⇒ **there is no spare codepoint here**, and two of
  the enum's own comments say otherwise — see A0-F9.

### 1.1 SPECIAL-ROW: UNTYPED_DM — type 0, the ordinary DM (no TYPE byte)

| field | fact (file:line) |
|---|---|
| **UNTYPED_DM** | type `0`; **not an enum member**; `DATA_FLAG_APP` clear, so no TYPE byte is emitted (`frame_codec.cpp:913-915`) |
| producers | `node.cpp:1595` (`on_command`, by-id `send`) · `node.cpp:1563` (`send <hash>`) — ⚠ the latter may become type 15 at `node_hashlocate.cpp:1618-1625`; funnel `do_send` `node_mac.cpp:485` → `enqueue_data` `node_mac.cpp:99` |
| relay | content-blind; `forward_item_from_post_ack` `node_mac_rx.cpp:55` copies `it.type = pa.type` (`:58`) |
| addressed consumer | the generic deliver tail `node_mac_rx.cpp:1936-2002` — no early return |
| persistence | **yes** — `record_dm` `node_mac_rx.cpp:1988`; the store writes `type = 0` unconditionally (`inbox.cpp:173`) |
| generic outcomes | full lifecycle: `send_blocked` `node_mac.cpp:929` · `send_acked` `node_mac_rx.cpp:2257` · `send_failed` `node.cpp:2192` · `send_aired` `node.cpp:2246` |
| type-specific | `msg_recv` `node_mac_rx.cpp:2003`; `send_e2e_acked` only under `E2E_ACK_REQ` |
| DM floor | **subject** — this is the carrier the floor exists for |
| feature gate | none |
| tests | `test_frame_codec.cpp:1619` · `test_inbox.cpp:101` · `test_console_json.cpp:207` · §A0-2/2b/2c |

### 1.2 The allocated enum — 19 members (`frame_codec.h:743-764`)

Columns: **Sym | # | class | producer(s) | relay | addressed consumer | persist | DM-floor | gate | tests**.
"early return" means the arm ends `become_free(); return;` and never reaches the deliver tail.

| Symbol | # | class | producer(s) | relay | addressed consumer | persist | floor | gate | executable coverage |
|---|---|---|---|---|---|---|---|---|---|
| `DATA_TYPE_H_ANSWER` | 1 | internal | `node_hashlocate.cpp:1240` (`send_hash_bind_response`, non-verifiable arm) | **cache-on-pass** `node_mac_rx.cpp:2016` | `:1800` → `on_hash_bind_response` — early return | no | subject | none | `test_dual_layer.cpp:7009,7164,7190` · `test_frame_codec.cpp:2640` · §A0-2/2c · corpus: **0 tx** |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER` | 2 | internal | `node_hashlocate.cpp:1241` | **cache-on-pass** `:2016` | `:1800` — early return | no | subject | none | `test_dual_layer.cpp:4277,4716,7010` · corpus: **98 tx** |
| `DATA_TYPE_E2E_ACK` | 3 | internal **outcome** | `node_mac.cpp:757,770,775` (`send_e2e_ack`) · `:814,827` (`send_xl_ack`) · `node_mac_rx.cpp:1613` | content-blind + the `RTS_FLAG_E2E_ACK` backstop hint (set `node_mac.cpp:1201`, honoured `node_mac_rx.cpp:634`, anti-spoof `:1160-1165`) | `:1830-1848` — early return | **YES** — `record_ack` `inbox.cpp:186` via `node_mac_rx.cpp:1841` | **EXEMPT** | none | `test_node_e2e_ack.cpp` (whole file) · `test_inbox.cpp:101` · `test_node_r3.cpp:4949-5137` · §A0-2/2b/2c/4b |
| `DATA_TYPE_H_ANSWER_PUBKEY` | 4 | **ALLOCATED_NOT_EMITTED** | **NONE** — reserved; only the enum (`frame_codec.h:747`) and a trace label (`frame_trace.h:80`) name it | content-blind (**no** snoop arm) | **NONE** — falls through to the deliver tail | would be inbox'd (A0-F7) | subject | none | **zero** before A0; now §A0-4 |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY` | 5 | internal | `node_hashlocate.cpp:1263-1266` | **cache-on-pass** `:2021` (hand-computed offset — no `ui` on the forward path) | `:1807` → `on_hash_bind_pubkey` — early return | no | subject | none | `test_dual_layer.cpp:4488,7127` · `test_node_hashlocate.cpp:1425` · §A0-2c · corpus: **1 tx** · ★ **the exact B59 payload type** |
| `DATA_TYPE_REMOTE_CMD` | 6 | internal | `node_mac.cpp:781-783`; callers `src/firmware_remote.cpp:205,212` | content-blind; ⚠ **not** RTS-hinted, so it is floor-exempt yet backstop-subject | `:1812-1828` — stages `_remote_inbound`, early return | **no** | **EXEMPT** | consumer **ungated** (documented `node.h:2522`); execution `MR_FEAT_REMOTE_MGMT` | `test_node_r3.cpp:6397` · §A0-2/2c/4b |
| `DATA_TYPE_REMOTE_RESP` | 7 | internal | `node_mac.cpp:784-786`; callers `firmware_remote.cpp:101,133,147,155,156` | as above | `:1812` — early return | no | **EXEMPT** | as above | `test_node_r3.cpp:6430` · §A0-2/2c/4b |
| `DATA_TYPE_MOBILE_H_ANSWER` | 8 | internal | `node_hashlocate.cpp:1240` (mobile-proxy arm) | content-blind — **deliberately no snoop** (a proxy binding must stay out of `_id_bind`, `:1341-1343`) | `:1723` — early return; body validated to exactly 7 B (`:39`) | no | subject | runtime `_mobile_reg_n>0`; consumer ungated | `test_dual_layer.cpp:4142,4173,4203,4250` · corpus: **28 tx** |
| `DATA_TYPE_MOBILE_BREADCRUMB` | 9 | internal | `node_join.cpp:1254-1255` (XL) · `:1260-1262` (same-layer) — ⚠ the **NEW** home, not the mobile (A0-F10) | content-blind | `:1735-1765` — early return | no | subject | ungated; runtime `_mobile_reg_n>0` | `test_dual_layer.cpp:5790,6288` · `Node::test_drive_breadcrumb` (`node.h:973`) users |
| `DATA_TYPE_MOBILE_LAYER_QUERY` | 10 | internal | `node_mobile.cpp:603-604` · `node.h:736` | content-blind | `:1769` — early return; ⚠ gated `n_layers==2`, **not** `is_gateway` | no | subject | responder **ungated** | `test_dual_layer.cpp:6464`; ⚠ the **producer has no test** |
| `DATA_TYPE_MOBILE_LAYER_ANSWER` | 11 | internal | `node_mac_rx.cpp:1785-1786` (inside the type-10 arm) | content-blind + the hosted-mobile last-mile fork `:1659` | `:1791` — early return | no | subject | consumer `#if MR_FEAT_MOBILE` + `is_mobile` | `test_dual_layer.cpp:6464` (producer); ⚠ **the `pa.type==11` arm itself is never driven** — the ingest test calls `learned_layers_ingest` directly (`:6496`) |
| `DATA_TYPE_MOBILE_PUBKEY_PUSH` | 12 | **ALLOCATED_NOT_EMITTED** (retired) | **NONE** — retired; replaced by the presence probe's `HAS_PUBKEY` block (`node_join.cpp:798`). Retirement stated at `node_mac_rx.cpp:1766-1768` | content-blind | **NONE** — handler deleted; falls through | would be inbox'd (A0-F8) | subject | none | negative assertions `test_dual_layer.cpp:4454,4466`; ⚠ dead scaffolding `drive_post_ack_pubkey_push` (`:324`) has **zero call sites**; now §A0-4 |
| `DATA_TYPE_MOBILE_H_ANSWER_PUBKEY` | 13 | internal | `node_hashlocate.cpp:1399-1405` | content-blind | `:1729` — early return; body `40+name_len`, `name_len<=32` (`:41-47`) | no | subject | ungated; runtime `_mobile_reg_n` | `test_dual_layer.cpp:4378,4431` · corpus: **0 tx** |
| `DATA_TYPE_MOBILE_SEND` | 14 | application **envelope** | `node_hashlocate.cpp:1741,1753,1758` (same-layer) · `node_mac.cpp:857` (XL) · `node_channel.cpp:812` (channel) | content-blind | `:1551-1654` — three unwrap arms (channel `:1581`, XL `:1591`, same-layer `:1630`); **always** early return | no on every intended path; ⚠ **yes** on a malformed no-DST_HASH frame (`:1988`) | subject | producers `#if MR_FEAT_MOBILE`; **unwrap ungated** | `test_dual_layer.cpp:3832,3863,3891,3913,4540,6634,6676` · `test_node_r3.cpp:10452` · corpus s27/s28 |
| `DATA_TYPE_INTRO` | 15 | application **envelope** | `node_hashlocate.cpp:1625` · `node.cpp:2135` · `node_mac_rx.cpp:1645-1647` (home re-origination) | content-blind (**no** cache-on-pass — deliberate) | `:1862-1885` — **the only arm that does NOT return**: validates, caches the key, strips the 33+n prefix (`:1880`), falls through | **yes**, as an ordinary DM (`:1988`, `enc=0`) | subject | attach + receive ungated; XL producer `#if MR_FEAT_MOBILE` | `test_node_hashlocate.cpp:1958,1999,1225` · `test_dual_layer.cpp:6591,6618,6667` · corpus s22 |
| `DATA_TYPE_MOBILE_KEY_FORWARD` | 16 | internal | `node_hashlocate.cpp:1452-1454` | 1-hop last mile (`addr_len=1`); no relay code | `:1795` — early return; ⚠ **doubly** conditional (`is_mobile` **and** `#if MR_FEAT_MOBILE`) | no on the mobile path | subject | producer ungated; **consumer gated** | `test_node_hashlocate.cpp:1818,1907,1922`; ⚠ no corpus scenario |
| `DATA_TYPE_SEALED_RELAY` | 17 | application **envelope** | `node.cpp:2118` · `node_mac.cpp:697` · `node_mac_rx.cpp:1611,1646` (home re-origination) | content-blind | `:1901-1907` — opens the seal, sets `crypted_ok`, **falls through** (does not return) | **yes** — `record_dm(..., enc=1)` `:1988` | subject | ungated; crypto-inert without an identity | `test_node_hashlocate.cpp:1138,1162,1183` · `test_dual_layer.cpp:2010,2107,2267,3913` · corpus s27 |
| `DATA_TYPE_CHANNEL_POST` | 18 | **ENCLOSED_ONLY** — **(a) the INTENDED enclosed marker** | `node_channel.cpp:806` — written into a **body byte** of a `MOBILE_SEND` wrapper, never into a TYPE field | n/a — it is not an outer type on this path; the *wrapper* (14) is what relays | consumed **inside the MOBILE_SEND handler**: `:1581-1590` reads it from the wrapper body, strips it, re-originates via `do_send_channel`; early return | no (the re-originated flood is persisted as a channel record, `node_channel.cpp:408`) | subject (as the wrapper) | producer `#if MR_FEAT_MOBILE`; **consumer ungated** | `test_node_channel.cpp:1751` (producer only); ⚠ **the home-side unwrap at `:1581` is never driven by a test**; corpus s28 |
| ↳ *the same value 18, but received as an OUTER TYPE byte* | 18 | **(b)** falls into **UNKNOWN_REPRESENTATIVE** | no production path originates it as an outer type | content-blind forward, like any unrecognised type | ⛔ **NONE — there is no `pa.type == 18` arm anywhere.** An outer type-18 DATA reaches the generic deliver tail `:1936` | **yes** — `record_dm` `:1988`, `msg_recv` `:2003`, exactly as any unknown type | subject | none | §A0-4 (added 2026-08-29, QG matrix correction) |
| `DATA_TYPE_TEAM_KEY_GRANT` | 19 | internal (sealed security transfer) | `node.cpp:239-240` only (`CryptIntent::on` forced) | content-blind | `:1927-1934` — early return on **both** arms (unsealed ⇒ loud reject `:1929`) | **no**, structurally (`node.cpp:268-270`) | subject | RX **ungated** (deliberate, `:1925`) | `test_node_channel.cpp:2067-2530` (the dedicated block) · `test_dual_layer.cpp:2203`; ⚠ no corpus scenario |

### 1.3 SPECIAL-ROW: ENCLOSED_ONLY — values intended to ride inside an enclosure

`DATA_TYPE_CHANNEL_POST` (18) is the one such value today, and it carries **two distinct interpretations that
this file kept separate only after the QG A0 review** (see the two `DATA_TYPE_CHANNEL_POST` rows in §1.2):

- **(a) the INTENDED enclosed marker** — a body byte inside a `MOBILE_SEND` wrapper, consumed *inside the
  MOBILE_SEND handler* (`node_mac_rx.cpp:1581-1590`). This is the only path any production code originates.
- **(b) an OUTER TYPE byte of 18** — **not structurally prevented**, and with **no dedicated consumer**: there
  is no `pa.type == 18` arm, so it falls through to ordinary delivery exactly like any unknown type
  (characterized by §A0-4).

⛔ **The first version of this section conflated the two**, listing the MOBILE_SEND enclosed consumer as though
it consumed an outer type-18 DATA. It does not. **The enum's claim that 18 is "never a wire frame type" is true
of the origination set and is NOT an enforced invariant** — see A0-F4.

The wrapper's single enclosed-type slot admits, measured from source:

- same-layer wrapper (`node_hashlocate.cpp`): `SEALED_RELAY` (`:1739`), any non-zero `itype` — in practice
  `INTRO` (`:1751`), `CHANNEL_POST` (`node_channel.cpp:806`), or **no byte at all** (plain arm `:1758`);
  `TEAM_KEY_GRANT` is refused (`:1716-1720`).
- XL wrapper (`node_mac.cpp:868`, byte always present): `0`, `E2E_ACK`, `SEALED_RELAY`, `INTRO`.
- **Home-side acceptance is unbounded** — no allow-list at `node_mac_rx.cpp:1597` or `:1639`.

### 1.4 SPECIAL-ROW: ALLOCATED_NOT_EMITTED — allocated, with status stated

| Symbol | # | status, verified |
|---|---|---|
| `DATA_TYPE_H_ANSWER_PUBKEY` | 4 | **RESERVED, never emitted.** Proven by absence: the only two references in the whole tree are the enum (`frame_codec.h:747`) and a trace label (`frame_trace.h:80`). The comment is CORRECT. ⚠ There is also no **consumer** (A0-F7). |
| `DATA_TYPE_MOBILE_PUBKEY_PUSH` | 12 | **RETIRED.** Zero producers, zero consumers; the handler was deleted (`node_mac_rx.cpp:1766-1768`). ⚠ The enum comment still describes it in the present tense (A0-F10). |

### 1.5 SPECIAL-ROW: UNKNOWN_REPRESENTATIVE — representative unknown values and their fall-through

Measured over the real two-node wire exchange in §A0-4 (not asserted from reading):

| value | at an ADDRESSED node | at a RELAY |
|---|---|---|
| `20`, `100` | delivered as an ordinary DM: `delivered` emit `:1946`, `record_dm` `:1988`, `msg_recv` `:2003` | content-blind forward, TYPE byte preserved (`:58`) |
| `0x80`, `0xBF` (future internal range) | **same** — no fail-closed handling exists today | same |
| `0xC0` (future reserved range) | **same** | same |
| `0xFE` (the tombstone value, on the wire) | **same** | same |
| `0xFF` (future invalid) | **same** | same |
| **`18` (`CHANNEL_POST`) as an OUTER type** | **same** — the enclosed marker has no outer-type arm, so it is delivered as an ordinary DM (QG matrix correction, 2026-08-29) | same |

The stored record cannot even preserve which type arrived: `record_dm` hard-codes `type = 0` (`inbox.cpp:173`),
so the durable row is indistinguishable from a plain DM. And the reply decision is downstream of the whole
dispatch — `DATA_FLAG_E2E_ACK_REQ` alone earns an ack (`:1999-2002`), so the fall-through is an amplification
surface as well as a mis-delivery one (A0-F3).

### 1.6 SPECIAL-ROW: TOMBSTONE_NON_WIRE — the explicit exclusion

`TOMBSTONE_NON_WIRE`: `inbox_rec_type_tombstone = 0xFE` (`inbox.h:64`) is an **inbox-store deletion marker, NOT a
DataType** (`inbox.h:35`; `pull()` never emits one). It is excluded from the wire universe by definition. ⚠ It is
**not** protected against a wire TYPE of `0xFE` — §A0-3 shows the codec packs and parses it, and §A0-4 shows an
addressed one is delivered. The two cannot collide *today* only because `record_dm` discards the wire type.

---

## 2. The ownership trace — which component owns each result

Spec §17-A0 bullet 2. Read down: admission → queueing → routing → retries → hop custody → HAL refusal →
terminal cleanup.

| stage | owning component (file:line) | success result | failure result | owner of the failure |
|---|---|---|---|---|
| **admission** | `Node::enqueue_data` `node_mac.cpp:99` | `TxItem` appended `:395`; `SendDispatch::Admit::queued` `:397` | `Admit::refused` `:397` (queue/park full); structural refusals: type-19 unsealed `:267-270`, XL type-19 `:554-557`, delegated type-19 `node_hashlocate.cpp:1716-1719` | `push_send_failed(unsealable)`; the caller maps via `SendDispatch` (`node.cpp:257-264`) |
| **queueing / pacing** | `Node::become_free` `node_mac.cpp:891` | picks the first item with `next_attempt_ms <= now` `:903` | DM-floor defer-in-place `:920-930` | `send_blocked{min_interval}` — `emit_send_blocked` `:929`. ⚠ carries **no ctr** |
| **floor stamp** | `Node::issue_send` `node_mac.cpp:1147-1150` | `_last_dm_origin_ms = now` | n/a | — (this half has no failure result; that asymmetry is why A0-F1b matters) |
| **routing** | `Node::issue_send` `node_mac.cpp:1063-1075` → `pick_next_cascade_hop` | `_pending_tx` installed, `flight_gen = ++_flight_gen` `:1140` | no route: a **forwarder DROPS** `:1069`; an **originator DEFERS** | `send_no_route` emit; `push_send_failed(no_route)` `node_cascade.cpp:341,381` |
| **retries / cascade** | `Node::handle_nack` / `try_cascade_requeue` `node_cascade.cpp:288` | requeue with backoff `:318+` | count cap · age cap · queue full `:291-300`; load shed `:307-316` | `giveup_flight(giveup_fail_reason(ge), …)` — **the reason is inferred from an event-name STRING PREFIX** (`node_cascade.cpp:16-21`) |
| **one-way reprobe** | `node_cascade.cpp:255-272` | one probe re-armed `:262` | inside the throttle window ⇒ clean giveup `:271` | `giveup_flight` |
| **hop custody** | `Node::do_post_ack` `node_mac_rx.cpp:1537` | receiver ACKs, then delivers (`:1936`) or forwards (`:2004`) | gateway intra-relay drop `:2009` | `gateway_intra_relay_drop` emit **only** — no push |
| **gateway deadline** (B159) | `Node::gateway_deadline_expired` `node_cascade.cpp:60` | attempt admitted | start at/after `enqueue + gateway_send_giveup_ms` | `send_giveup{gateway_unreachable_timeout}` + `giveup_flight(gateway_unreachable)` `:108,508` |
| **HAL refusal** | `DeviceHal::pump_tx` vs `TxParams::deadline_ms`; adapter `Node::on_tx_complete` `node.cpp:2313` | `TxOutcomeKind::aired` → `push_send_aired_if_owned` `:2317` | `failed` / `unknown` → **telemetry only**, deliberately (`node.cpp:2306-2309`); `expired` → `giveup_flight(gateway_unreachable)` `node.cpp:2345` | as noted |
| **terminal cleanup** | `Node::giveup_flight` `node_cascade.cpp:27` | — | **always** `push_send_failed(reason, dst, ctr)` then `reset()` then `become_free()`, in that order | ⛔ **unconditional — see A0-F1** |
| **reprovision purge** | `node.cpp:757-762` | — | `carrier_owes_send_failed(is_channel_m, is_forward)` `node_carriers.h:538` gates the push | ★ **the ONE site that asks who owns the result** |

### 2.1 The two ownership predicates that exist, and where they are not used

| predicate | file:line | what it answers | used by |
|---|---|---|---|
| `carrier_owes_send_failed(channel_m, forwarded)` | `node_carriers.h:538` | "does dropping this carrier owe a `send_failed`?" | **`node.cpp:761` only** |
| `pt.has_previous_hop` guard inside `push_send_aired_if_owned` | `node.cpp:2255` | "did we originate this, or are we transit?" | `send_aired` only |

⇒ **`send_aired` is transit-aware; `send_failed` is not.** `giveup_flight` — the terminal helper reached from
seven call sites (`node_cascade.cpp:108,271,300,316,508`; `node_mac_rx.cpp:2320,2390`) — pushes unconditionally,
and none of those call sites gates on `has_previous_hop` or on type. That is A0-F1, and it is exactly the seam
spec §11 says Slice E must replace.

---

## 3. The census

Spec §17-A0 bullet 3 + §18.0.3. **Every zero result below is controlled**: `tools/check_a0_matrix.py --selftest`
re-runs each structural check against a mutated tree and requires it to be rejected (7/7 controls RED, §4.2), and
the seven `a0*` mutations do the same for the behavioural claims.

### 3.1 Raw numeric type comparisons in production code

| hit | file:line | disposition |
|---|---|---|
| `if (type == 3) j.lit(",\"type\":\"e2e_ack\"")` | **`lib/console/console_json.cpp:539`** | **quality/observability debt** (A0-F5). The ONE numeric literal for a DataType in production. `console_json.cpp` deliberately does not include `frame_codec.h` (`:3-5`), so the decoupling is intentional — but nothing binds the two, and Slice A's renumbering makes `3` mean `SEALED_RELAY`. Closed by Slice A + the §18.1.4 structural check. |
| `3 (DATA_TYPE_E2E_ACK)` in a default-arg doc comment | `lib/console/console_json.h:336` | **stale documentation once Slice A lands**; correct in Slice A. |
| `/*type=*/3` | `test/test_console_json.cpp:216` | test literal — re-anchor in Slice A. |

**Controlled zero:** no `type == <n>`, `"type":<n>` or equivalent DataType literal exists anywhere in `src/`,
`tools/*.py` or `tools/lab/*.py`. The Python and iOS companions match the **semantic string** `"e2e_ack"`
(`tools/lab/reconcile.py:54,264`), so they are insulated from the renumbering. ⇒ **the C++ encoder is the only
numeric coupling**, which is a materially smaller Slice-A surface than §1.4 of the spec assumed.

> ⛔⛔ **THE SENTENCE ABOVE IS FALSE AND IS SUPERSEDED BY §9.1 (Slice A, 2026-08-29).** `console_json.cpp` was
> **not** the sole production numeric coupling: `lib/core/frame_trace.h:76` carried **five live numeric
> `case 1:`..`case 5:` DataType labels** that this census missed. The A0 text is left standing rather than
> rewritten — see §9.1 for what the search missed, why, and the standing control that now closes it.

### 3.2 Duplicated policy lists

| # | duplication | sites | disposition |
|---|---|---|---|
| 1 | **the DM-floor exempt set**, written out twice by hand with no shared symbol | `node_mac.cpp:918-919` (CHECK) · `:1147-1148` (STAMP) | **verified existing behavior** — the two are IDENTICAL today (same three types, same order); *not* drifted. Recorded as fragility, characterized by §A0-2/2b/2c and mutation-controlled A01/A02/A03. Replaced by the trait authority in Slice B. |
| 2 | `canonical_typed_answer_body_valid` | `node_mac_rx.cpp:34-49` | **verified existing behavior**, with a caveat: its `default: return true` (`:47`) means every other type — including unknown ones — passes body validation unexamined. It is the only `default` arm in the whole type surface and it is **permissive**. |
| 3 | the H-answer snoop list, twice with **different body extraction** | addressed `:1800,:1807` vs relay `:2016,:2021` (hand-computed offset at `:2022`) | **quality debt** — the divergence is necessary today (no `ui` on the forward path) but is a second place to update. |
| 4 | bare `!= DATA_TYPE_E2E_ACK`, 15 sites, no helper | `node_mac.cpp:412,620,884` · `node_mac_rx.cpp:1160,1385,1609,1615` · `node_hashlocate.cpp:1629,1630,2244,2257,2263,2311,2317,2349` | **quality debt** — 15 independent expressions of one predicate; Slice B's trait authority should absorb them. |
| 5 | the `team_key_grant_refused` structural triple | `node_mac.cpp:267` · `:554` · `node_hashlocate.cpp:1716` | **verified existing behavior.** ⓘ The enum's "three places" claim (`frame_codec.h:763`) is an **understatement, not an error** — a fourth, generalising backstop exists at `node_mac.cpp:676-682` (`type != 0` under `want_crypt`). |
| 6 | `o.e2e_is_ack = (o.type == DATA_TYPE_E2E_ACK)` | `frame_codec.cpp:966` | codec-level convenience duplicating predicate #4 a third time. |

### 3.3 Unknown-type fall-through arms

**The addressed dispatch has no `else`/`default` arm at all** (`node_mac_rx.cpp:1551-1934`). "Unknown type" is
not a decision anywhere in the code; it is the absence of a guard. **`-Wswitch` is structurally blind to this** —
an if-chain has no exhaustiveness diagnostic, so adding a `DataType` member produces no warning on any of the six
OLED envs (warning census: zero `-Wswitch`, §5.3). That is A0-F2, and it is *why* `tools/check_a0_matrix.py`
exists as a script rather than a native test.

The relay branch (`:2004-2036`) has an `else if` chain and likewise no `else`; an unknown type is forwarded
verbatim.

**Controlled zero:** no known-type validation exists anywhere — `grep` for `known_type|is_known_data_type|data_type_valid|type > 19|type <= 19` over `lib/` and `src/` returns nothing, and `parse_data` copies `frame[8]`
verbatim with no check of any kind (`frame_codec.cpp:959`). Mutation A06 reintroduces a range reject and is RED,
which is what makes that zero a measurement.

### 3.4 Behavior asserted only by comments

All five are §7 draft corrections; none was corrected in this slice.

| # | claim | asserted at | what the code does |
|---|---|---|---|
| A0-F4 | CHANNEL_POST is "an ENCLOSED-type marker (**never a wire frame type**)" | `frame_codec.h:762` | True of the origination set; **not enforced**. A wrapper with `MS_ENCLOSED_TYPE` and body exactly `[18]` fails the `size() >= 2` guard at `:1581`, falls to the same-layer arm, takes `etype = wb[0] = 18` at `:1639` and is re-originated with `type=18` at `:1646`. The XL arm does the same at `:1597`/`:1611`. |
| A0-F9 | `DATA_FLAG_PRIORITY` is "decoded-only (no behaviour wired yet)" and "**NEVER set by any origination path**" | `frame_codec.h:682,698,702` | Both false. Three origination paths set it (`node_channel.cpp:811`, `node_hashlocate.cpp:1742`, `:1754`) and two sites branch on it (`node_mac_rx.cpp:1581`, `:1638`). |
| A0-F10 | type 12 "a mobile pushes its ed_pub[32] to its home … Re-sent on re-home" (present tense) | `frame_codec.h:756` | Retired; zero producers, zero consumers (`node_mac_rx.cpp:1766-1768`). |
| A0-F10b | a stray type 12 falling through is "**harmless**" | `node_mac_rx.cpp:1767-1768` | It is `record_dm`'d and `msg_recv`-pushed as 32 raw key bytes. |
| A0-F11 | type 16 "Mobile-only consume; **a static never sees it**" | `frame_codec.h:760` | An addressing convention, not enforced: the fork is `is_mobile`-gated **and** `#if MR_FEAT_MOBILE`-gated (`:1790-1799`), so a static or a gateway build falls through to the deliver tail and the 32-B requester key lands in the inbox as text. |
| A0-F12 | breadcrumb: "a moved mobile tells its OLD home I re-homed" | `frame_codec.h:753` | The **NEW home** sends it (`node_join.cpp:1235`, D10 replacement). No mobile-side producer exists. |

⚠ One more, adjacent and worth the supervisor's eye: `test_dual_layer.cpp:3474` cites the exemption as
"node_mac.cpp:394-396"; the lists are at `:918-919` and `:1147-1148`. A drifted line-number comment in a test —
listed, not corrected (it is not a file this slice touches).

### 3.5 Feature-gated paths missing from native AND the simulator

**Controlled result: NONE are missing from native.** `[env:native]` (`platformio.ini:67-92`) sets no
`MR_PROFILE_*`, so `mr_features.h:28-45` defaults `MR_FEAT_TEAM/MOBILE/MOBILE_HOST/GATEWAY/REMOTE_MGMT` all to 1
⇒ every gated path in §1.2 is compiled by the native suite. The simulator compiles the same `lib/core` from the
sibling repo.

What *is* missing is **corpus reach**, which is a different and weaker thing — recorded so it is not mistaken for
coverage:

| type | corpus transmissions | positive coverage rests on |
|---|---|---|
| `H_ANSWER` (1) | **0** | native only |
| `MOBILE_H_ANSWER_PUBKEY` (13) | **0** | native only |
| `MOBILE_KEY_FORWARD` (16) | **0** — no scenario does a static→registered-mobile WANT_PUBKEY | native only |
| `TEAM_KEY_GRANT` (19) | **0** — s18-inert (no identities ⇒ no seals) | native only |
| `AUTHORITATIVE_H_ANSWER_PUBKEY` (5) | **1** | native + one corpus frame |

### 3.6 Paths with production code but no executable coverage at all

| gap | evidence |
|---|---|
| the `pa.type == MOBILE_LAYER_ANSWER` dispatch arm (`:1791`) | the ingest test calls `learned_layers_ingest` directly (`test_dual_layer.cpp:6496`); the arm is never entered |
| the CHANNEL_POST home-side unwrap (`:1581-1589`) | `drive_post_ack_mobile_send_typed` exists (`test_dual_layer.cpp:406`) but no call site passes `etype = 18` |
| `mobile_layer_query_fire` (the producer, `node_mobile.cpp:598`) | `grep mobile_layer_query_tx test/` → 0 hits |
| the DM-floor exemption of types 6/7 | before A0, only the E2E_ACK arm was covered (`test_node_r3.cpp:5137`); **closed by §A0-2/2c** |
| the unknown-type fall-through | zero tests before A0; **closed by §A0-4** |

**Dead test scaffolding:** `DualLayerTestAccess::drive_post_ack_pubkey_push` (`test_dual_layer.cpp:324-334`) has
zero call sites.

---

## 4. Characterization and its controls

### 4.1 The cases — `test/test_data_type_audit_a0.cpp` (new, 10 cases)

⚠ **This TU uses the PUBLIC `Node` API only.** No `friend` was added to `node.h`, because that would be the
production edit C1 forbids. The §A0-4* cases therefore drive the **real two-node RTS/CTS/DATA/ACK exchange**
(`on_recv` + `on_timer`) rather than poking a `PostAck` through a white-box seam — slower to set up, and strictly
more faithful.

| case | claim | authority |
|---|---|---|
| §A0-1 | the 19-member numbering is contiguous 1..19, no gap, no duplicate; each member pinned individually | `frame_codec.h:743-764` |
| §A0-1b | 0xFE collides with no allocated DataType | `inbox.h:64` |
| §A0-2 | the DM-floor exempt set is **exactly** {E2E_ACK, REMOTE_CMD, REMOTE_RESP} — 15 rows incl. the B59 type as NON-exempt | `node_mac.cpp:918-919` |
| §A0-2b | an exempt origination lays **no** floor stamp (the STAMP half alone) | `node_mac.cpp:1147-1150` |
| §A0-2c | with the floor ARMED by an ordinary DM, an exempt type still flies (the CHECK half alone) | `node_mac.cpp:918-921` |
| §A0-3 | pack/parse round-trip 14 type bytes incl. `0xFE`/`0xFF` — the codec validates nothing | `frame_codec.cpp:955-961` |
| §A0-3b | APP=1 with a `0x00` TYPE byte parses as type 0, inner still offset past it | `frame_codec.cpp:959-961` |
| §A0-4 | an addressed UNKNOWN type is delivered as an ordinary DM (9 values incl. types 4 and 12) | `node_mac_rx.cpp:1551-1934` (no default arm) |
| §A0-4b | control — a CONSUMED type (3/6/7) is not delivered | `node_mac_rx.cpp:1812,1830` |
| §A0-4c | an addressed UNKNOWN type **with** `E2E_ACK_REQ` is delivered **AND** earns an ack (positive arm), and the identical type **without** the flag earns none (negative control) | `node_mac_rx.cpp:1999-2002` |

⛔⛔ **§A0-4c WAS CORRECTED 2026-08-29 (QG A0 review, blocker 1) AND THE CORRECTION IS RECORDED RATHER THAN THE
CASE QUIETLY REWRITTEN.** What it *claimed*: its name said "an unknown type carrying `E2E_ACK_REQ` still earns an
E2E ACK". What it *proved*: the fixture sent **without** the flag and asserted only that no ack appeared — i.e.
the converse of its own title, and no support whatever for A0-F3's amplification claim. What it *proves now*: a
real positive arm (delivered **and** acked) plus the old flagless arm retained, correctly labelled, as the
negative control that makes the FLAG rather than the type the deciding input.

⇒ **the executable-coverage claim for A0-F3 is NOT withdrawn — it is now actually met.** Reaching the type × flag
combination needed a test-side edit of the DATA bytes already captured off the HAL (no public seam originates an
unknown TYPE together with `E2E_ACK_REQ`, and adding one would be the production edit C1 forbids). That forgery
is accepted by the receiver as a *measured property of the identity design*, not by luck: a plaintext flight's
identity is `rts_flight_identity_plain(origin, ctr)` (`dm_crypto.cpp:57`) — origin and ctr only — `E2E_ACK_REQ`
leaves `d.crypted` and therefore the domain and width untouched, `data_mac_len` stays 4 so no offset moves, and
`payload_len` is explicitly excluded from the identity (`node_mac_rx.cpp:1109`). It is also the faithful threat
model: an attacker does not use our `on_command` surface either.

ⓘ One measured detail worth keeping: the assertion is on the ack's **origination** (`e2e_ack_tx`,
`node_mac.cpp:775` → `enqueue_data`'s emit at `:406`), not on a returning `DATA_TYPE_E2E_ACK` frame. The ack is a
new flight, so only its RTS has aired when `do_post_ack` returns — scanning for the DATA frame was a false
negative, and was measured as one before being changed.

Cases §A0-1, §A0-2, §A0-2b, §A0-2c and §A0-4 are marked in-source `[CHANGES IN SLICE A/B]`: they are **expected
to go RED** when those slices land, and must be re-anchored there with the movement stated, never deleted.

### 4.2 The controls

**Mutations — 8/8 RED, 0 unusable, each at match count exactly 1** (`tools/probe_ui_model_mutations.py`,
isolated scratch trees; "real tree untouched: all 38 target files byte-identical to launch"):

| target | entry | result |
|---|---|---|
| `a0mac` | A01 drop REMOTE_CMD from the CHECK list | RED (1 assertion) |
| `a0mac` | A02 widen the exempt set to a hash answer | RED (2) |
| `a0mac` | A03 drop E2E_ACK from the STAMP list only | RED (1) |
| `a0rx` | A04 install Slice B's fail-closed default arm early | RED |
| `a0rx` | **A08** the E2E-ack reply is gated on a KNOWN type (kills A0-F3's amplification) | RED |
| `a0rx` | A05 the E2E-ack arm stops consuming | RED |
| `a0codec` | A06 `parse_data` gains an enum-range reject | RED |
| `a0codec` | A07 the APP bit stops being derived from the type | RED |

⛔ **A01 MEASURED NOTHING ON ITS FIRST RUN, and the fix was to the test.** Dropping a type from the CHECK list is
invisible to a same-type pair: the STAMP list still exempts that type, so the floor never arms and the CHECK
half's `!exempt_type` term is never reached. §A0-2c was added to supply the observable arrangement (an ordinary
DM arms the floor first), and A01 went RED. ⇒ **the battery found a hole in the characterization before any
production change existed to hide in it** — which is the whole reason to run controls on a no-change slice. No
mutation was removed as unmeasurable; the one that started unmeasurable was made measurable.

**Structural control** — `tools/check_a0_matrix.py` (new): parses `enum DataType` from source, requires a matrix
row per member **stating that member's current numeric value**, and binds each of the five named special classes
to a **POSITIONAL section** (`SPECIAL-ROW: <KEY>` heading + substantive table/list content). `--selftest` proves
each check can fail: **7/7 controls RED**.

⛔⛔ **CHECK (3) WAS REWRITTEN 2026-08-29 (QG A0 review, blocker 2), and this is the vacuity class one level up.**
What it *claimed*: that all five special-row classes are present. What it *proved*: only that each class NAME
occurred **somewhere** in the document. QG deleted the entire `UNKNOWN_REPRESENTATIVE` evidence section, left its
name in the introductory list, and the checker still passed — it was measuring a mention, not evidence. What it
*proves now*: each class has its own heading with real content beneath it, located positionally.

★ **AND THE OLD SELFTEST COULD NOT HAVE CAUGHT THIS** — it replaced *every* occurrence of the token, destroying
the heading and the summary mention together, so it went RED for the wrong reason and never exercised the
binding. The selftest now deletes **only** the section, deliberately leaving the summary references intact
(it reports `summary mention left intact: yes` for all five), which is exactly the state QG built by hand.
Verified independently as well: hand-deleting §1.5 and re-running the checker gives
`FAIL … missing NAMED SPECIAL-ROW SECTION for UNKNOWN_REPRESENTATIVE`, exit 1.

### 4.3 Anchor population — derived mechanically, not quoted

Derived by parsing the harness's own `MUTS_BY_TARGET` / `TARGET_SRC` and counting entries, then re-matching every
entry's anchor against its target file:

```
pre-A0:  45 targets, 865 anchors, 0 broken (count != 1)     <- the 865 cross-check reproduces
A0 adds: 3 targets (a0mac, a0rx, a0codec), 8 anchors
post-A0: 48 targets, 873 anchors, 0 broken                  <- 865 + 8 = 873 ✓
```

ⓘ 8, not the 7 first reported: the QG A0 review's blocker 1 added **A08** (the E2E-ack reply gated on a known
type), which is the negative control for A0-F3's amplification claim.

---

## 5. Pre-Slice-A baselines

### 5.1 Native — DERIVED by running the binary

The pio wrapper misreports "0 test cases"; these figures are from `./.pio/build/native/program`.

```
clean run BEFORE A0 :  2333 cases / 98448 assertions / 0 failed
A0 additions        :  +10 cases / +518 assertions   (test/test_data_type_audit_a0.cpp, 10 cases)
clean run AFTER  A0 :  2343 cases / 98966 assertions / 0 failed
DERIVATION          :  2333 + 10 = 2343  ·  98448 + 518 = 98966  ✓
```

ⓘ **The QG-review corrections moved assertions only, not cases: 98939 -> 98966 (+27), cases UNCHANGED at 2343.**
Both corrections landed inside existing cases — §A0-4c was rewritten in place (it gained a real positive arm and
kept the flagless arm as a labelled negative control, +2 arms in one case), and the matrix correction added outer
type 18 as a tenth probe row to §A0-4's existing loop. Nothing was added or removed at case granularity.

The `2333 / 98448` cross-check in the dispatch brief **reproduced exactly** — no divergence to investigate. The
harness's `PIN_CASES/PIN_ASSERTS` cross-check has been re-synced to `2343, 98939` with this derivation recorded
in place (`tools/probe_ui_model_mutations.py:424`), preserving the prior value in the chain.

### 5.2 Corpus — the COMPLETE current record (spec §18.0 correction)

Produced by the canonical analyzer `lus` md5 **`1fccb4a0d46787cfb86893731d0fd3dd`**, over **every** scenario in
`simulation/` (`topo_9node.json` excluded — a topology input, not a scenario, and it has no anchor row).

**36/36 rows measured · 0 assertion failures · 21 unchanged · 15 pre-existing movers.**

★ **KEYSTONE: `s18_meshroute` = `9868cad3` / 269905 / 0 — EXACT against `simulation/BASELINE.md`.** The value was
READ from that file's `### 36/36 corpus` table, never hardcoded. ⛔ **Nothing is re-anchored by this slice and the
anchor table is not edited.**

The 15 movers are **pre-existing** — A0 touches zero `lib/core` bytes, so none of them is attributable to this
slice. They are recorded, not explained; attribution belongs to B159/B134/B251/B260.

| scenario | anchor | measured now | delta |
|---|---|---|---|
| `s07_seattle_mobile_meshroute` | `b3b7ce31`/107989 | `70a2b60c`/109837 | +1848 |
| `s15_three_layer` | `b53c6f47`/52293 | `bab18b30`/52441 | +148 |
| `s15_three_layer_metal` | `304e558b`/51232 | `342edaca`/51819 | +587 |
| `s16_dense_gateway` | `cfd86c16`/24397 | `5d9a7186`/23902 | −495 |
| `s21_mobile_dm_milestone_meshroute` | `f3f950ad`/680 | `30b9c048`/680 | byte-only |
| `s22_mobile_team_meshroute` | `ea03873f`/1822 | `531b4adf`/1830 | +8 |
| `s24_static_and_team_multihop_meshroute` | `6f9d78e4`/1583 | `ca55271a`/1583 | byte-only |
| `s25_two_team_separation_meshroute` | `6ca24694`/792 | `37d07a86`/792 | byte-only |
| `s26_team_reroute_meshroute` | `2ab37318`/1045 | `2efbef99`/1045 | byte-only |
| `s27_cross_layer_mobiles_meshroute` | `a7d5cc86`/9205 | `6941d78d`/9410 | +205 |
| `s28_mixed_team_channels_meshroute` | `19136ba5`/3856 | `e226ea17`/3865 | +9 |
| `s29_mixed_leaf_team_meshroute` | `d3af0db6`/2020 | `bb534a88`/2025 | +5 |
| `s34_team_switch_clears_plane` | `2dcd78e9`/921 | `88b8899b`/921 | byte-only |
| `s37_team_homed_origin_meshroute` | `d836ce20`/750 | `2efd54e4`/751 | +1 |
| `twin_9node_dm` | `ca698d79`/13221 | `1c9280e7`/14645 | +1424 |

ⓘ `s22` `531b4adf`/1830 and `s27` `6941d78d`/9410 corroborate the figures `simulation/BASELINE.md:30` records as
the current post-B251 state — an independent cross-check that the measurement is sound.

The 21 unchanged rows reproduce their anchors exactly: `s06`, `s09`, `s09_metal`, `s10`, `s17`, **`s18`**, `s19`,
`s20`, `s21_leaf`, `s22_leaf`, `s23_leaf`, `s23_mobile`, `s30`, `s31`, `s32`, `s33`, `s35a`, `s35b`, `s36`,
`s38`, `sim_9node_base`.

**A0's own inertness — the 0-build-action proof, WITH its recompile control:**

| step | result |
|---|---|
| `lus` md5 before | `1fccb4a0d46787cfb86893731d0fd3dd` |
| canonical `cmake --build build` | **0 relevant build actions** |
| `lus` md5 after | `1fccb4a0d46787cfb86893731d0fd3dd` — IDENTICAL |
| ★ **recompile control** — `touch lib/core/node.cpp`, rebuild | **5 build actions fired**, and the binary reproduced `1fccb4a0` exactly (`git diff` on that file: empty — `touch` changed no bytes) |

⇒ the "0 actions" is a **measurement**, not a broken build system. The reason it is inert is structural: A0
touches **0 files under `lib/core` or `lib/console`**, and the simulator's `CMakeLists.txt` names no `test/`,
`tools/` or `docs/` path.

### 5.3 Warning census — PASS at its pins, nothing re-pinned

`tools/warning_census.sh`, all six OLED envs:

| env | objects | warnings | pinned | `-Wswitch` | RAM | flash |
|---|---|---|---|---|---|---|
| `gateway_heltec` | 327 | 173 | 173 | **0** | 230892 | 1304036 |
| `gateway_heltec_v4` | 328 | 178 | 178 | **0** | 231164 | 1301964 |
| `heltec_mobile` | 327 | 177 | 177 | **0** | 205620 | 1353824 |
| `heltec_v3` | 327 | 177 | 177 | **0** | 206100 | 1359136 |
| `heltec_v4` | 328 | 182 | 182 | **0** | 206372 | 1357176 |
| `heltec_v4_mobile` | 328 | 182 | 182 | **0** | 205892 | 1351812 |

`PASS — 6 OLED env(s) match their pinned warning baseline.` ⚠ The zero `-Wswitch` is real but does **not** cover
the DATA-type dispatch: it is an if-chain, which `-Wswitch` cannot see (A0-F2).

### 5.4 Boards — the ruled pair, via the certified runner

`tools/measure_board.py build` (which accepts only `{gateway, heltec_mobile}` — the ruled pair is enforced by the
tool, not by convention). Both PASS with a fixed build identity (`stamp=Jan 1 2000`, `git=b206b206b206`).

| env | RAM | flash | objects | loadable sections | elf sha256 (16) | payload sha256 (16) |
|---|---|---|---|---|---|---|
| `gateway` | 195660 | 505452 | 283 | `.text` 504468 · `.data` 976 · `.ARM.exidx` 8 | `4e023e34576e068b` | `f3330139798abc4c` |
| `heltec_mobile` | 205620 | 1353824 | 327 | `.flash.text` 1031706 · `.flash.rodata` 217988 · `.iram0.text` 77843 · `.dram0.data` 25260 · `.iram0.vectors` 1027 · `.flash.appdesc` 256 · `.rtc.text` 256 · `.rtc.force_fast` 8 | `55f32aecafed02bf` | `7c8e1fd65bb03537` |

⚠ **§B262: the `heltec_mobile` payload hash is SAME-PATH-ONLY.** It is recorded for repeatability within this
checkout and **must never be compared across trees**. RAM / flash / objects / sections are the cross-tree fields.

ⓘ `heltec_mobile` reproduces the warning census's independent RAM/flash reading (205620 / 1353824) exactly.

---

## 6. C1, dispositions and the blocker question

### 6.1 C1 — proven by raw `git diff` against HEAD

The owner committed before dispatch, so the clean form applies. Final tree state:

```
 M tools/probe_ui_model_mutations.py     <- 3 new a0* targets + entry lists + the PIN re-sync
?? docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md
?? test/test_data_type_audit_a0.cpp
?? tools/check_a0_matrix.py
```

**`git diff HEAD -- lib/ src/` is EMPTY.** Zero production-source changes. The V1 fix-drifted-comments exception
was **not used**: every drifted comment found (§3.4, six of them) is *listed* for the slice that establishes the
replacement truth, not corrected here — none of them sits in a file this slice touches, so correcting one would
itself have been a production edit.

### 6.2 The findings ledger — every finding has exactly one disposition

Spec §17-A0's five dispositions. **No finding disappears into prose (§18.0.7).**

| id | finding | disposition | close-by / authority |
|---|---|---|---|
| **A0-F1** | `giveup_flight` (`node_cascade.cpp:27`) pushes `send_failed` **unconditionally** from all 7 call sites, with no `has_previous_hop` and no type gate ⇒ a TRANSIT flight's terminal give-up emits a generic user-send failure under the ORIGINAL sender's `{dst, ctr}` — which at this relay is someone else's identity. `send_aired` already gates on transit (`node.cpp:2255`) and `carrier_owes_send_failed` (`node_carriers.h:538`) already exists but is used at exactly one unrelated site. | **adjacent correctness defect** | **Slice E** ("suppress transit/internal generic completions through the common ownership policy"). Severity: medium — spurious/colliding app-visible completion; no protocol-state corruption. Dependency: none. See §6.3 for why not a blocker. |
| **A0-F1b** | The DM-floor policy is hand-copied into two lists with no shared symbol (`node_mac.cpp:918-919` / `:1147-1148`). They agree today; nothing enforces that. | **quality debt** | Missing truth: one authority. Gate that closes it: Slice B's `data_type_traits` + mutations A01/A03, which now fail on a half-divergence. |
| **A0-F2** | The addressed type dispatch is an if-chain with no default arm, so **`-Wswitch` is structurally blind** to a new `DataType`. No gate anywhere notices an added type. | **quality/observability debt** | Missing truth: exhaustiveness. Gate: `tools/check_a0_matrix.py` (landed, selftest 7/7 RED) binds the enum to the matrix; Slice B's fail-closed arm supplies the runtime half. |
| **A0-F3** | An addressed unknown type carrying `E2E_ACK_REQ` is **both** delivered as a message **and** acknowledged (`node_mac_rx.cpp:1999-2002`, downstream of the whole dispatch) ⇒ the fall-through is an amplification surface, not only a mis-delivery. | **verified existing behavior** | Authority: `node_mac_rx.cpp:1999-2002`. Characterized by §A0-4c (positive arm + negative control), mutation-controlled by **A08**. Slice B's fail-closed arm removes it for the internal range. |
| **A0-F4** | `frame_codec.h:762` claims CHANNEL_POST is "never a wire frame type". True of the origination set; **not enforced** in either direction: (i) a crafted wrapper with body exactly `[18]` re-originates with `type=18` (`node_mac_rx.cpp:1639`/`:1646`, XL `:1597`/`:1611`); (ii) an **outer** type-18 DATA has no consumer at all and is delivered as an ordinary DM. | **false/stale active documentation** + latent input-validation gap | Correct the wording in **Slice A**. The missing enclosed-type allow-list belongs to **Slice B**. ★ Now characterized: §A0-4 includes outer 18 among the fall-through probes (added by the QG matrix correction), and §1.2 carries **two** CHANNEL_POST rows — (a) the intended enclosed marker, (b) the outer type. ⚠ Flagged for the supervisor: the spec's §5.2 row for `0x04` says "not a standalone outer wire type **today**" — accurate as written, but it must not be read as an invariant. |
| **A0-F5** | `console_json.cpp:539` compares `type == 3` for E2E ACK — the only DataType numeric literal in production. | **quality debt** (becomes a defect the moment Slice A renumbers) | Slice A + the §18.1.4 structural check. ⓘ Scope is smaller than §1.4 assumed: the Python/iOS companions match the string `"e2e_ack"`, not the number. |
| **A0-F6** | `frame_codec.h:741-742` asserts type 0 is "never on the wire". True of `pack_data`; `parse_data` does not enforce the converse — APP=1 with TYPE `0x00` parses as type 0 with the inner already offset past the byte. | **verified existing behavior** (+ documentation nuance) | Authority: `frame_codec.cpp:955-961`. Characterized by §A0-3b. Wording correctable in Slice A. |
| **A0-F7** | Type 4 is allocated, correctly never emitted — **and has no consumer**, so an arriving type-4 frame is inbox'd as text. | **verified existing behavior** | Authority: absence of any arm in `node_mac_rx.cpp:1551-1934`. Characterized by §A0-4. Closed by Slice B's fail-closed rule. |
| **A0-F8** | Type 12 is retired; its handler was deleted, so a stray type-12 delivers 32 raw key bytes to the inbox as message text. | **verified existing behavior** | Same as A0-F7. Characterized by §A0-4. |
| **A0-F9** | `frame_codec.h:682,698,702` call `DATA_FLAG_PRIORITY` "decoded-only … NEVER set by any origination path". Both halves false — 3 writers, 2 behavioural readers. | **false/stale active documentation** | Correct in **Slice A** (it is a wire-flag fact; `docs/frames.md` owns the byte, the enum comment owns the symbol). ★ This is the "spare codepoint" trap in its third appearance — recorded so it is not offered a fourth time. |
| **A0-F10** | `frame_codec.h:756` describes type 12 in the present tense though it is retired; `node_mac_rx.cpp:1767` calls its fall-through "harmless". | **false/stale active documentation** | Correct in **Slice A**. |
| **A0-F11** | `frame_codec.h:760` says a static "never sees" type 16. Not enforced; a static/`MR_FEAT_MOBILE 0` node inboxes the 32-B requester key as text. | **false/stale active documentation** | Correct in **Slice A**; the runtime half is Slice B's. |
| **A0-F12** | `frame_codec.h:753` says the *mobile* sends the breadcrumb; the **new home** does (`node_join.cpp:1235`). | **false/stale active documentation** | Correct in **Slice A**. |
| **A0-F13** | Three production paths have no executable coverage at all: the type-11 dispatch arm, the CHANNEL_POST home-side unwrap, and `mobile_layer_query_fire`. Plus dead test scaffolding (`test_dual_layer.cpp:324`, zero call sites). | **quality debt** | Missing truth: coverage of three live arms. Gate: a case per arm, owed by whichever slice next touches the mobile/channel delegation surface. |
| **A0-F14** | `canonical_typed_answer_body_valid`'s `default: return true` (`node_mac_rx.cpp:47`) passes every unlisted type's body unexamined — the only `default` in the type surface, and it is permissive. | **verified existing behavior** | Authority: `node_mac_rx.cpp:34-49`. Deliberate today (type 5 is excluded on purpose, `:2231-2233`). Slice B should decide whether the internal range inverts it. |
| **A0-F15** | Corpus reach is zero for types 1, 13, 16 and 19; one frame for type 5. Positive coverage for those rests on native, not on the corpus. | **quality debt** (recorded, not actioned) | Missing truth: the corpus cannot witness the renumbering of four types. ⇒ **Slice A's re-anchor must not be read as covering them**; their proof is native. |

### 6.3 Why no blocker was declared

A0-F1 is the only finding severe enough to consider. It is **not** a blocker because a blocker is defined as
something whose fix must land in its own reviewed slice *before Slice A*, and A0-F1 does not gate Slice A: the
renumbering does not interact with the terminal-cleanup seam, and the defect is already inside the designed arc's
scope as **Slice E**, which exists precisely to "suppress transit/internal generic completions through the common
ownership policy" (spec §17 Slice E, §11). Escalating it would duplicate a slice the design already schedules.
⇒ registered as an adjacent correctness defect with Slice E as its named close-by. **Nothing was fixed in A0.**

---

## 7. Drafted text for the maintained documents

⚠ **DRAFTS. Not landed by this slice** — the register, `docs/protocol.md`, `docs/frames.md`, the bench script and
`simulation/BASELINE.md` are supervisor-landed.

### 7.1 Drafted bug-register rows

```
[[A0-F1]] transit give-up emits a generic user-send failure under a foreign {dst,ctr}
  SEVERITY  medium (app-visible false/colliding completion; no protocol-state corruption)
  EVIDENCE  `giveup_flight` (lib/core/node_cascade.cpp:27) calls push_send_failed(reason, dst, ctr)
            unconditionally. All 7 call sites (node_cascade.cpp:108,271,300,316,508;
            node_mac_rx.cpp:2320,2390) pass the flight's dst/ctr with no has_previous_hop and no type
            gate. For a TRANSIT flight those are the ORIGINAL sender's values, so this relay reports a
            user-send failure for a send it never made -- and can collide a real local {dst,ctr}.
            The asymmetry is measurable: push_send_aired_if_owned (node.cpp:2255) DOES gate on
            pt.has_previous_hop, and carrier_owes_send_failed (node_carriers.h:538) already exists but
            is used at exactly one unrelated site (node.cpp:761, the reprovision purge).
  DEPENDENCY none
  CLOSE BY   Slice E of the custody design (typed terminal context) -- spec §11/§17. Gate: a native case
            proving a transit terminal give-up emits NO generic send_failed, plus a mutation restoring
            the unconditional push (RED).

[[A0-F2]] no gate notices a new DataType: the dispatch is an if-chain, so -Wswitch is blind
  SEVERITY  low (quality/observability)
  EVIDENCE  The addressed consumer dispatch (lib/core/node_mac_rx.cpp:1551-1934) is a flat if-chain of
            early-returning guards with no else/default arm. -Wswitch has no exhaustiveness diagnostic
            for an if-chain; the warning census measures zero -Wswitch on all six OLED envs and is
            structurally unable to see this. Adding an enum member produces no diagnostic anywhere.
  MISSING TRUTH  "every DataType has a defined addressed behaviour".
  CLOSE BY   (a) landed in A0: tools/check_a0_matrix.py binds the enum to the matrix and fails on an
            unrowed member (selftest 7/7 RED). (b) Slice B's fail-closed arm for the internal range
            supplies the runtime half.

[[A0-F5]] the E2E-ACK DataType is a bare numeric literal in the companion JSON encoder
  SEVERITY  low now; becomes a correctness defect the moment Slice A renumbers
  EVIDENCE  lib/console/console_json.cpp:539 -- `if (type == 3) j.lit(",\"type\":\"e2e_ack\"")`.
            console_json.cpp deliberately does not include frame_codec.h (:3-5), so nothing binds the
            literal to the enum. After Slice A, 3 means SEALED_RELAY. Also lib/console/console_json.h:336
            (doc comment) and test/test_console_json.cpp:216 (test literal).
            SCOPE IS NARROWER THAN FEARED: the Python and iOS companions match the semantic string
            "e2e_ack" (tools/lab/reconcile.py:54,264), not the number, so they are insulated.
  CLOSE BY   Slice A + the spec's §18.1.4 structural check rejecting surviving semantic literals.

[[A0-F13]] three live DATA arms have no executable coverage
  SEVERITY  low (quality)
  EVIDENCE  (a) the pa.type == DATA_TYPE_MOBILE_LAYER_ANSWER dispatch arm (node_mac_rx.cpp:1791) is never
            entered -- the ingest test calls learned_layers_ingest directly (test_dual_layer.cpp:6496);
            (b) the CHANNEL_POST home-side unwrap (node_mac_rx.cpp:1581-1589) is never driven -- the
            helper exists (test_dual_layer.cpp:406) but no call site passes etype=18;
            (c) mobile_layer_query_fire (node_mobile.cpp:598) -- grep mobile_layer_query_tx test/ = 0.
            Dead scaffolding: DualLayerTestAccess::drive_post_ack_pubkey_push (test_dual_layer.cpp:324)
            has zero call sites.
  CLOSE BY   a case per arm, owed by whichever slice next touches the mobile/channel delegation surface.

[[A0-F15]] four DataTypes have zero corpus reach, so Slice A's re-anchor cannot witness them
  SEVERITY  low (quality; a scoping fact for Slice A's evidence)
  EVIDENCE  Corpus transmissions: type 1 = 0, type 13 = 0, type 16 = 0 (no scenario does a
            static->registered-mobile WANT_PUBKEY), type 19 = 0 (s18-inert: no identities => no seals).
            Type 5 = 1. Native compiles every one of them ([env:native] sets no MR_PROFILE_*).
  CLOSE BY   stated in Slice A's report: the renumbering of types 1/13/16/19 is proven by native only.
```

### 7.2 Drafted `docs/frames.md` / enum-comment corrections (Slice A)

Wire-oriented only, per the map. All five are **active false claims**, not drift-in-passing:

1. `frame_codec.h:682,698,702` — `DATA_FLAG_PRIORITY` is **not** "decoded-only" and **is** set by three
   origination paths. Replace with: *"0x01 — aliased LIVE as `DATA_FLAG_MS_ENCLOSED_TYPE` (see :704). Set by
   `node_channel.cpp:811`, `node_hashlocate.cpp:1742`, `:1754`; read behaviourally at `node_mac_rx.cpp:1581`,
   `:1638`. No PRIORITY semantics are wired."*
2. `frame_codec.h:753` — the **new home**, not the moved mobile, originates the breadcrumb (`node_join.cpp:1235`).
3. `frame_codec.h:756` — type 12 is **RETIRED** (zero producers, zero consumers); replaced by the presence
   probe's `HAS_PUBKEY` block.
4. `frame_codec.h:760` — "a static never sees it" is an addressing convention, **not enforced**.
5. `frame_codec.h:762` — "never a wire frame type" describes the origination set; it is **not an enforced
   invariant** (`node_mac_rx.cpp:1639`/`:1597`).

Plus `node_mac_rx.cpp:1767` — a stray type 12 is **not** "harmless"; it is inbox'd as 32 raw key bytes.

### 7.3 Drafted `docs/protocol.md` addition (Slice B, when the truth exists)

*"An addressed DATA whose TYPE has no handler is today delivered as an ordinary DM — inbox record plus
`msg_recv` — and, if it set `E2E_ACK_REQ`, is also acknowledged. The durable record does not preserve the wire
type (`record_dm` stores 0)."* ⇒ replace with the fail-closed rule **in Slice B**, not before.

### 7.4 Bench script (`docs/2026-07-31-bench-test-script.md`)

**Nothing owed. A0 adds no device-reachable behaviour** — it is an audit; the one new TU is native-only and the
two new tools are host scripts. No metal residue, and no exception is drafted.

### 7.5 `simulation/BASELINE.md`

**No re-anchor, and the `### 36/36 corpus` table is NOT edited.** The complete current record is §5.2 of this
file. If the supervisor wants the pre-Slice-A corpus state carried into BASELINE, §5.2's table is the text — but
it is a *record of movement already caused by other slices*, not an A0 result, and A0 claims no authority to
re-anchor it.

---

## 8. Reproducing this

```bash
# native (the wrapper lies; run the binary)
pio test -e native && ./.pio/build/native/program        # 2343 / 98966 / 0

# the matrix structural control + its negative controls
python3 tools/check_a0_matrix.py                          # PASS
python3 tools/check_a0_matrix.py --selftest               # 7/7 controls RED

# the characterization's mutation controls (isolated scratch trees; the real tree is untouched)
python3 tools/probe_ui_model_mutations.py --target=a0mac    # 3 RED / 0 unusable
python3 tools/probe_ui_model_mutations.py --target=a0rx     # 3 RED / 0 unusable
python3 tools/probe_ui_model_mutations.py --target=a0codec  # 2 RED / 0 unusable

# warnings
bash tools/warning_census.sh                              # PASS at 173/178/177/177/182/182, 0 -Wswitch

# boards (the runner accepts only the ruled pair)
python3 tools/measure_board.py build --env gateway       --output .pio-measure/a0_gw
python3 tools/measure_board.py build --env heltec_mobile --output .pio-measure/a0_hm

# corpus (lus md5 1fccb4a0d46787cfb86893731d0fd3dd)
for f in simulation/s*.json simulation/sim_9node_base.json simulation/twin_9node_dm.json; do
  ../lora-universal-simulator/build/orchestrator/lus -e meshroute "$f" /tmp/$(basename $f .json).ndjson
done
```

---

## 9. §CUSTODY-A (SLICE A) ADDENDUM — the CURRENT namespace, and one A0 statement corrected

*Added 2026-08-29 by **Slice A** (the DATA-namespace transition). ⛔ **NOTHING ABOVE THIS LINE WAS REWRITTEN.**
§1's matrix, §3's census, §4's controls, §5's baselines and §6's ledger are the A0 record and stay as measured —
they describe the tree **before** the transition and that is exactly their value. This section is the SLICE-A
MAINTAINED half: §9.1 corrects the one A0 statement the slice proved false, and §9.2's **CURRENT NAMESPACE**
table carries the post-transition assignments that `tools/check_a0_matrix.py` binds to positionally.*

### 9.1 CORRECTION to §3.1 — "console_json.cpp is the only numeric coupling" was FALSE

**What A0 claimed** (§3.1's "Controlled zero"): the C++ companion JSON encoder held the ONE numeric DataType
coupling in production, so Slice A's literal surface was three sites.

**What is true:** there were **eight**. Beyond `console_json.cpp:539`, `console_json.h:336` and
`test_console_json.cpp:216`, `lib/core/frame_trace.h:76` carried **five live numeric `case 1:`..`case 5:` labels**
in the decoded frame-trace switch. The QG brief review found them; A0's census did not.

**Why the census missed them, stated so the class is not repeated:** the search shape was
`type == <n>` / `"type":<n>` — a *comparison*. A `case <n>:` label is the same coupling written with different
punctuation, and it is the **worse** shape of the two:

- it still **compiles** after a renumbering — it simply stops firing for its own type and starts firing for
  whatever now owns that number (post-transition `case 3:` would have labelled a `SEALED_RELAY` as `E2E_ACK`);
- `-Wswitch` cannot see it (the subject is a `uint8_t`, not the enum — the sibling of finding **A0-F2**); and
- `frame_trace.h` is `#if defined(ARDUINO)`, so **neither the native suite nor the simulator ever compiles it**.
  No gate in this project could have noticed the regression.

**Disposition:** the five labels are symbolic as of Slice A, and the standing control is
`tools/check_data_type_literals.py` — a structural search whose scope explicitly **includes switch-case labels**
and whose `--selftest` reintroduces this exact form (4/4 controls RED). ⓘ **A0 is not reopened**: its gate, its
findings and its corpus baseline are unchanged, and [[A0-F5]]'s close-by simply turns out to have had a fourth
site. The register row for [[B265]] should record the corrected count.

### 9.2 SPECIAL-ROW: CURRENT NAMESPACE — the post-transition assignments (SLICE-A MAINTAINED)

⛔ **THIS TABLE IS THE ONE `tools/check_a0_matrix.py` BINDS TO FOR CURRENT VALUES, POSITIONALLY** — it is
located by this heading, not by matching a row anywhere in the document, precisely so §1.2's historical `#`
column can keep the pre-transition ordinals without satisfying the check. A stale value here, or a missing
`DATA_TYPE_APP_MESSAGE` row, is RED (checker selftest controls C8/C9).

| Symbol | current | old | class | status |
|---|---|---|---|---|
| `DATA_TYPE_INTRO` | `0x01` | 15 | application envelope | live |
| `DATA_TYPE_MOBILE_SEND` | `0x02` | 14 | application envelope | live |
| `DATA_TYPE_SEALED_RELAY` | `0x03` | 17 | application envelope | live |
| `DATA_TYPE_CHANNEL_POST` | `0x04` | 18 | application marker | live (enclosed-only by convention, not enforced — A0-F4) |
| `DATA_TYPE_APP_MESSAGE` | `0x05` | — | application envelope | **RESERVED, unimplemented** (no producer, no consumer; `known=false`) |
| `DATA_TYPE_E2E_ACK` | `0x80` | 3 | internal outcome | live · the ONLY `persistent_outcome` at Slice A |
| `DATA_TYPE_H_ANSWER` | `0x88` | 1 | internal | live · **zero corpus reach** ([[B267]]) |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER` | `0x89` | 2 | internal | live |
| `DATA_TYPE_H_ANSWER_PUBKEY` | `0x8A` | 4 | internal | **RESERVED, never emitted, no consumer** (`known=false`) |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY` | `0x8B` | 5 | internal | live · ★ the exact B59 payload type |
| `DATA_TYPE_MOBILE_H_ANSWER` | `0x90` | 8 | internal | live |
| `DATA_TYPE_MOBILE_BREADCRUMB` | `0x91` | 9 | internal | live |
| `DATA_TYPE_MOBILE_LAYER_QUERY` | `0x92` | 10 | internal | live |
| `DATA_TYPE_MOBILE_LAYER_ANSWER` | `0x93` | 11 | internal | live |
| `DATA_TYPE_MOBILE_PUBKEY_PUSH` | `0x94` | 12 | internal | **RETIRED**, zero producers/consumers (`known=false`) |
| `DATA_TYPE_MOBILE_H_ANSWER_PUBKEY` | `0x95` | 13 | internal | live · **zero corpus reach** ([[B267]]) |
| `DATA_TYPE_MOBILE_KEY_FORWARD` | `0x96` | 16 | internal | live · **zero corpus reach** ([[B267]]) |
| `DATA_TYPE_REMOTE_CMD` | `0xA0` | 6 | internal | live (firmware-only; no corpus reach) |
| `DATA_TYPE_REMOTE_RESP` | `0xA1` | 7 | internal | live (firmware-only; no corpus reach) |
| `DATA_TYPE_TEAM_KEY_GRANT` | `0xA2` | 19 | internal | live · **zero corpus reach** ([[B267]]) |

Not allocated, and deliberately so:

- `0x81` — the forward reservation for `DATA_TYPE_CUSTODY_FAILURE`. ⛔ **Slice F adds the enum member**; at
  Slice A it is an ordinary UNALLOCATED internal value (`known=false`, `persistent_outcome=false`), and the
  namespace test pins it as such.
- `0x06..0x7F` · `0x82..0x87` · `0x97..0x9F` · `0xA3..0xBF` — unallocated inside the two live ranges.
- `0xC0..0xFD` reserved · `0xFE` the inbox-store tombstone (never a wire DataType) · `0xFF` invalid.

`protocol::wire_version` is **UNCHANGED at 1** by owner ruling (design §5.3), pinned by a `static_assert` in
`test/test_data_type_namespace.cpp` whose failure text names the ruling.


---

## 10. SPECIAL-ROW: PRE-RULING RE-ANCHOR PROPOSAL — the measured post-Slice-A corpus

⛔⛔ **THIS IS A PROPOSAL, NOT AN ANCHOR.** `simulation/BASELINE.md`'s anchor table is **NOT** edited by this
slice and moves only on the owner's single ruling. The rows below are a MEASUREMENT of what the DATA-namespace
transition did to every stream, published in the tree so the ruling is made against evidence that lives
somewhere durable rather than inside one agent's report.

⚠ **THE "pre-Slice-A" COLUMN IS §5.2's MEASURED RECORD, NOT `BASELINE.md`'s ANCHOR TABLE.** That table was
already stale for 15 of these streams before this slice, for reasons predating it (B159/B134/B251/B260, see
§5.2). Comparing the new hashes against it would silently re-attribute those 15 pre-existing movers to the
renumbering. The honest baseline is the state A0 measured on this same tree, and that is what is used here.

**How this was produced, and what makes it evidence rather than a diff:** `tools/compare_corpus_semantics.py`
walks the two runs **in order, event by event**, and requires every event name, field, value and timestamp to be
IDENTICAL. Exactly three carriers may differ, and each is DERIVED rather than pattern-matched:

1. the frame's own **TYPE field at offset 8**, whenever the APP bit says one is present;
2. a **MOBILE_SEND wrapper's enclosed-type slot**, at the offset the tool COMPUTES from that frame's flags and
   inner layout the way `parse_unicast_inner` computes it (measured: offset 18 on a same-layer wrapper under
   flags `0x87`, offset 21 on a cross-layer wrapper under `0xC6`); and
3. `pkt`, the simulator's FNV-1a identity of the raw frame, re-derived on BOTH sides from their own bytes
   before a before→after map is learned from the `tx` events.

A differing byte **anywhere else fails**, even when its before/after pair is a valid old→new DataType mapping.
⛔ That last clause exists because the FIRST version of this instrument did not have it: it accepted any
valid-looking mapping at any offset, so a legitimate outer renumber plus an unrelated payload corruption
compared CLEAN. The `an ARBITRARY PAYLOAD byte remapped` negative control is what proves the clause bites; the
selftest requires **5/5** controls RED (a field value · an event name · event order · a dropped event · the
camouflage attack).

**Result: `PASS` — 36 streams compared, 19 carried typed DATA and differ ONLY in TYPE bytes, 17 are
byte-identical. 11 010 events touched · 1 339 frame TYPE bytes · 16 enclosed-type bytes · 10 telemetry fields ·
9 661 derived packet-identity references. NO event count changed, in any row. NO semantic delta anywhere.**

⚠ **[[B267]] — four types the corpus CANNOT witness.** `H_ANSWER` (0x88), `MOBILE_H_ANSWER_PUBKEY` (0x95),
`MOBILE_KEY_FORWARD` (0x96) and `TEAM_KEY_GRANT` (0xA2) have **zero** corpus reach, so no row below covers their
renumbering — it is proven by native round-trips only (`test/test_data_type_namespace.cpp`, §CUSTODY-A/5). The
same is true of `H_ANSWER_PUBKEY` (0x8A, never emitted), `MOBILE_PUBKEY_PUSH` (0x94, retired), the reservation
`APP_MESSAGE` (0x05) and the firmware-only `REMOTE_CMD`/`REMOTE_RESP` (0xA0/0xA1). ⇒ **this table must not be
read as covering them.**

| # | scenario | pre-Slice-A (measured) | **MEASURED post-Slice-A** | events | per-stream attribution — every typed DATA frame whose TYPE byte moved |
|---|---|---|---|---:|---|
| 1 | `s06_seattle_lifecycle` | `e7bd0f08` | **`aa2380be`** | 65835 | E2E_ACK 3→0x80 ×317 |
| 2 | `s07_seattle_mobile_meshroute` | `70a2b60c` | **`57d608bc`** | 109837 | E2E_ACK 3→0x80 ×277 · BREADCRUMB 9→0x91 ×17 |
| 3 | `s09_two_layer_gateway` | `71120178` | `71120178` *(unchanged)* | 2266 | — none; **byte-identical** |
| 4 | `s09_two_layer_gateway_metal` | `17d1418f` | **`c0fc1168`** | 2345 | E2E_ACK 3→0x80 ×2 |
| 5 | `s10_two_layer_separation` | `c44c0b39` | `c44c0b39` *(unchanged)* | 2266 | — none; **byte-identical** |
| 6 | `s15_three_layer` | `bab18b30` | **`0b546cf5`** | 52441 | AUTHORITATIVE_H_ANSWER 2→0x89 ×40 |
| 7 | `s15_three_layer_metal` | `342edaca` | **`8bbff735`** | 51819 | AUTHORITATIVE_H_ANSWER 2→0x89 ×31 · E2E_ACK 3→0x80 ×4 |
| 8 | `s16_dense_gateway` | `5d9a7186` | `5d9a7186` *(unchanged)* | 23902 | — none; **byte-identical** |
| 9 | `s17_metro` | `42e69427` | `42e69427` *(unchanged)* | 1181179 | — none; **byte-identical** |
| 10 | `s18_meshroute` ★ **keystone** | `9868cad3` | **`b7aeaeeb`** | 269905 | E2E_ACK 3→0x80 ×410 |
| 11 | `s19_singlelayer_multihop_chain` | `c669b1ef` | `c669b1ef` *(unchanged)* | 1065 | — none; **byte-identical** |
| 12 | `s20_random_mesh` | `db240065` | `db240065` *(unchanged)* | 40566 | — none; **byte-identical** |
| 13 | `s21_leaf_config_divergence` | `d7db6a04` | `d7db6a04` *(unchanged)* | 390 | — none; **byte-identical** |
| 14 | `s21_mobile_dm_milestone_meshroute` | `30b9c048` | **`1c5db032`** | 680 | MOBILE_H_ANSWER 8→0x90 ×2 |
| 15 | `s22_leaf_config_join` | `baadfbed` | `baadfbed` *(unchanged)* | 215 | — none; **byte-identical** |
| 16 | `s22_mobile_team_meshroute` | `531b4adf` | **`e2f8f5a1`** | 1830 | E2E_ACK 3→0x80 ×4 · AUTH_H_ANSWER_PUBKEY 5→0x8B ×1 · MOBILE_H_ANSWER 8→0x90 ×1 · INTRO 15→0x01 ×2 |
| 17 | `s23_leaf_config_epoch_write` | `0cd16bd5` | `0cd16bd5` *(unchanged)* | 219 | — none; **byte-identical** |
| 18 | `s23_mobile_team_multihop_meshroute` | `568c684f` | `568c684f` *(unchanged)* | 924 | — none; **byte-identical** |
| 19 | `s24_static_and_team_multihop_meshroute` | `ca55271a` | **`74fec485`** | 1583 | AUTHORITATIVE_H_ANSWER 2→0x89 ×7 |
| 20 | `s25_two_team_separation_meshroute` | `37d07a86` | **`700d3437`** | 792 | AUTHORITATIVE_H_ANSWER 2→0x89 ×6 |
| 21 | `s26_team_reroute_meshroute` | `2efbef99` | **`86010f57`** | 1045 | AUTHORITATIVE_H_ANSWER 2→0x89 ×8 |
| 22 | `s27_cross_layer_mobiles_meshroute` | `6941d78d` | **`721de4b7`** | 9410 | MOBILE_H_ANSWER 8→0x90 ×24 · BREADCRUMB 9→0x91 ×5 · LAYER_QUERY 10→0x92 ×12 · LAYER_ANSWER 11→0x93 ×13 · MOBILE_SEND 14→0x02 ×14 · INTRO 15→0x01 ×30 · SEALED_RELAY 17→0x03 ×40 |
| 23 | `s28_mixed_team_channels_meshroute` | `e226ea17` | **`9a3b4cac`** | 3865 | AUTHORITATIVE_H_ANSWER 2→0x89 ×4 · MOBILE_SEND 14→0x02 ×2 |
| 24 | `s29_mixed_leaf_team_meshroute` | `bb534a88` | `bb534a88` *(unchanged)* | 2025 | — none; **byte-identical** |
| 25 | `s30_team_dad_mediation_meshroute` | `02a7d4d0` | **`4a1de37d`** | 1034 | INTRO 15→0x01 ×4 |
| 26 | `s31_dual_carrier_gateway` | `4eafb125` | `4eafb125` *(unchanged)* | 2300 | — none; **byte-identical** |
| 27 | `s32_dual_cr_gateway` | `9574f5dd` | `9574f5dd` *(unchanged)* | 2266 | — none; **byte-identical** |
| 28 | `s33_mixed_cr_channel_overhear` | `814ef421` | `814ef421` *(unchanged)* | 2845 | — none; **byte-identical** |
| 29 | `s34_team_switch_clears_plane` | `88b8899b` | **`61ecb33e`** | 921 | AUTHORITATIVE_H_ANSWER 2→0x89 ×2 |
| 30 | `s35a_cochannel_isolation_meshroute` | `bda1713b` | `bda1713b` *(unchanged)* | 2356 | — none; **byte-identical** |
| 31 | `s35b_cochannel_isolation_control_meshroute` | `7dbc19ae` | `7dbc19ae` *(unchanged)* | 1063 | — none; **byte-identical** |
| 32 | `s36_reprovision_purges_carriers` | `76d02e58` | `76d02e58` *(unchanged)* | 472 | — none; **byte-identical** |
| 33 | `s37_team_homed_origin_meshroute` | `2efd54e4` | **`18f4b4aa`** | 751 | E2E_ACK 3→0x80 ×3 |
| 34 | `s38_team_origin_learn_meshroute` | `1d0bb046` | **`626cf1ff`** | 526 | E2E_ACK 3→0x80 ×4 |
| 35 | `sim_9node_base` | `c7d74ddd` | **`f729db96`** | 4959 | E2E_ACK 3→0x80 ×14 |
| 36 | `twin_9node_dm` | `1c9280e7` | **`2e038758`** | 14645 | E2E_ACK 3→0x80 ×39 |

**Totals across the 19 moving streams**, by type — this is the whole wire surface the renumbering touched:

| old → new | frames |
|---|---:|
| `E2E_ACK` 3 → `0x80` | 1074 |
| `AUTHORITATIVE_H_ANSWER` 2 → `0x89` | 98 |
| `SEALED_RELAY` 17 → `0x03` | 40 |
| `INTRO` 15 → `0x01` | 36 |
| `MOBILE_H_ANSWER` 8 → `0x90` | 27 |
| `MOBILE_BREADCRUMB` 9 → `0x91` | 22 |
| `MOBILE_SEND` 14 → `0x02` | 16 |
| `MOBILE_LAYER_ANSWER` 11 → `0x93` | 13 |
| `MOBILE_LAYER_QUERY` 10 → `0x92` | 12 |
| `AUTHORITATIVE_H_ANSWER_PUBKEY` 5 → `0x8B` | 1 |
| **total** | **1339** |

Plus 16 enclosed-type bytes inside `MOBILE_SEND` wrappers (`INTRO` ×6, `SEALED_RELAY` ×8, `CHANNEL_POST` ×2, at
the two computed slots 18 and 21) and 10 `mobile_delegate_xl.enclosed_type` telemetry values.

**Reproduce:**

```bash
# ⓘ SUPERSEDED 2026-08-30 (§GATE-SPEED): use `python3 tools/run_corpus.py --out <dir> --jobs 8` — the
#   canonical, self-validating runner. The historical loop below is the record of this file's own figures.
# 1. the PRE-slice corpus (from a tree without the namespace transition)
for f in simulation/*.json; do b=$(basename $f .json); [ "$b" = topo_9node ] && continue
  ../lora-universal-simulator/build/orchestrator/lus -e meshroute "$f" before/$b.ndjson; done
# 2. the POST-slice corpus, same loop into after/
# 3. the ordered semantic comparison + its five negative controls
python3 tools/compare_corpus_semantics.py before after
python3 tools/compare_corpus_semantics.py before after --selftest   # 5/5 controls RED
```

⛔ **On the ruling:** if the owner accepts, `simulation/BASELINE.md`'s `### 36/36 corpus` table takes the
**MEASURED post-Slice-A** column above, and the s18 keystone becomes `b7aeaeeb` / 269905 / 0. Until then, the
keystone of record remains the pre-slice `9868cad3`, and this section is the standing evidence for the change.

