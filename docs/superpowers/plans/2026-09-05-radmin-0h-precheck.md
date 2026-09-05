<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0h — Quality-Agent pre-check ledger (2026-09-05): R-RA-25, one 232-byte application-DM cap, hash fields never dropped for size

Authority: R-RA-25 (`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md:506`), register rows B296 (HIGH) +
B297 (LOW), design §19 Slice 0h (:1571) and the pass-2 ledger `docs/superpowers/plans/2026-09-05-fable-review-pass2.md`
(S1 measurements + the embedded reproduction harness). Verified at `HEAD 31fb680` ("0g"; the Author's six landing
files uncommitted). Every `file:line` moves — re-verify before quoting (V2). Hypotheses, not authority.

## 0. What 0h is, in one line

A `lib/core` BEHAVIOUR change (its own slice, corpus-attributed): the application-DM body cap becomes 232 =
241 − 1 origin − 4 DST_HASH − 4 SOURCE_HASH; `SOURCE_HASH` is unconditional on every `app_dm=true` carrier;
`DST_HASH` is unconditional whenever supplied or already derivable; the plaintext enqueue arm refuses when the inner
does not pack; a mobile wrapper without its source hash is refused at the home, never inboxed.

## 1. Every `app_dm=true` producer and its destination-hash availability (item 1)

All application DMs reach `enqueue_data(app_dm=true)` through `do_send` (`node_mac.cpp:523-525`) or the ONE direct
call at `node_hashlocate.cpp:1820`. `override_dst_hash != 0` = SUPPLIED; otherwise the hash is DERIVABLE iff
`enqueue_data`'s existing lookups (`key_hash_of_id(dst) || team_key_of_id(dst)`, `node_mac.cpp:219`) find it.

| producer | site | dst hash | note |
| --- | --- | --- | --- |
| `send <id>` by node id (console) | `node.cpp:1608` | derivable iff bound | the ONE genuine "unknown hash" case R-RA-25 exempts from DST_HASH (still 232) |
| `send_by_hash` TEAM arm (team local id) | `node_hashlocate.cpp:1690` | derivable (`team_key_of_id`) | |
| `send_by_hash` authoritative id-bind | `:1715` | derivable by construction (the id came FROM the hash) | today set only if it fits — must become unconditional |
| `send_by_hash` AUTO team cascade | `:1728` | derivable | |
| delegated wrapper arms 1/2/3 (registered mobile → home, `MOBILE_SEND`) | `:1788`, `:1800`, `:1805` | SUPPLIED (`key_hash32`) | the S1 shape-2/3 site; wrapper prefix +1 byte on arms 1/2 |
| cached-home same-layer arm | `:1889` | SUPPLIED | |
| park drain, resolved same-layer | `:2544` | SUPPLIED (`p.key_hash32`) | |
| park drain, resolved by id | `:2605` | derivable | |
| hosted-mobile LAST MILE (direct `enqueue_data`, `addr_len=1`) | `:1820` | `override 0`; the mobile's `key_hash32` is IN SCOPE (loop match) but NOT passed | ⚠ OPEN POINT 4.1 — "derivable" must be defined as "by the enqueue's EXISTING lookups", or this arm gains DST_HASH and MOVES the corpus (hosted last-mile flights exist in s27/s07) |
| team-channel post via the home wrapper (`MOBILE_SEND` enclosing `CHANNEL_POST`) | `node_channel.cpp:810-812` | SUPPLIED (`override_dst_hash = _key_hash32`, our OWN hash — the home's fork keys on `source_hash` + `body[0] == CHANNEL_POST`) | body 2 + n ≤ 202 B, far below the cap; unaffected |
| E2E-ack re-origination / INTRO / custody carriers | via `send_by_hash` `type != 0` | as above | `data_type_traits(type).generic_send_lifecycle` decides the push |

Non-application producers (`app_dm=false`: `send_remote_cmd/response` `node_mac.cpp:828-834`, `mobile_layer_answer`
`node_mac_rx.cpp:2168`, beacons/RREQ/etc.) are OUT of R-RA-25 by definition — the ruling names `app_dm=true`.
`enqueue_cross_layer` (`node_mac.cpp:576-598`) already sets both hashes unconditionally and refuses on `n == 0`:
the reference shape.

## 2. Refusal ownership — synchronous vs lifecycle (item 2)

| layer | today | after 0h |
| --- | --- | --- |
| `Node::on_command` `send` | `body_len > dm_max_body_bytes` → `CmdCode::err_too_large` SYNCHRONOUS, no ctr, no queue slot, no TX (`node.cpp:1570-1571`) | same contract, cap 232: bodies 233..239 now refuse HERE — before airtime, no push (the app holds the CmdResult) |
| `Node::on_command` `send_layer` | same at `node.cpp:2085` | same, cap 232 (the XL carrier cap 226 < 232 still binds via `err_too_large` at `node_mac.cpp:786-787`) |
| `enqueue_data` SEALED arm | `SealOutcome::too_large` → `MR_EMIT e2e_seal_too_large` + `push_send_failed(too_large)` iff `generic_lifecycle`, `return ctr`, nothing enqueued (`node_mac.cpp:330-345`) | unchanged — the model for the new plaintext backstop |
| `enqueue_data` PLAINTEXT arm | `pack_unicast_inner` return UNCHECKED (`:398`) → `inner_len 0` queued | ⛔ C2 backstop: on `n == 0` MIRROR the sealed sibling (emit + `push_send_failed(too_large)` iff `generic_lifecycle`, `return ctr`, nothing enqueued). This is the "truthful lifecycle ownership" R-RA-25 demands — no orphan push for non-generic types |
| the hash-flag decisions `:217-226` | optional-for-size | unconditional SOURCE_HASH for `app_dm`; DST_HASH unconditional when supplied or derivable (no NEW lookups — see 4.1) |
| TX-time `dlen == 0` bail `:2193-2215` | "deliberately silent" (B268 ruling) + a FALSE "unreachable from an origination" comment (B297) | stays silent (ruled); the comment is corrected: now truly unreachable from origination |
| `park_send` / `park_send_layer` clamps | `p.body_len = min(body_len, dm_max_body_bytes)` SILENT (`node_hashlocate.cpp:2392`, `:2447`) | unreachable once `on_command` refuses > 232, but C2-shaped: the brief chooses refuse-or-assert; never a clamp |
| the HOME receiving a `MOBILE_SEND` WITHOUT `SOURCE_HASH` | fork skipped (`node_mac_rx.cpp:1914` needs `has_source_hash`); `MOBILE_SEND` is application-bearing (`frame_codec.h:884`) so the internal guard (`:2403`) does not fire → ORDINARY DELIVERY into the home's own inbox | ⛔ R-RA-25: must REFUSE loud (a named emit, `become_free`, no `record_dm`/`msg_recv`). This is an RX-path `lib/core` change — prediction-first (expected 0 corpus occurrences: every corpus body ≤ 219 carries the hash) |

## 3. Corpus reach (item 3) — measured on my 0g-gate corpus (36 streams, `HEAD 31fb680` lib/core)

- `delivered` events: **753**; payload-length histogram (20-B bins): 0:342 · 20:229 · 40:102 · 60:33 · 80:3 · 120:8 ·
  140:15 · 160:12 · 180:7 · 200:2 — **max bin 200-219, ZERO payloads ≥ 233**. ⇒ the 232 cap refuses nothing the
  corpus sends; every corpus DM body already fits WITH both hashes (≤ 232), so making SOURCE_HASH unconditional
  changes no corpus byte PROVIDED no new lookup adds a DST_HASH where none is set today (4.1).
- `tx_enqueue` fields are `ctr, depth, dst, origin` — the corpus events do NOT expose flags; the coder's
  prediction must come from a flags census (a throwaway `MR_EMIT` on the flag decision, 0d's instrument idiom) or
  from the r3 sweep, not from the event log. `send_failed` events: 222 (reasons unaffected unless a body > 232
  exists — none does).
- Prediction: **36/36 byte-identical**, and the throwaway instrument must show 0 `SOURCE_HASH`-dropped and 0
  `DST_HASH`-dropped decisions across the corpus BEFORE the change (so identity is explained, not hoped).

## 4. Open points for the brief — ALL RULED by the owner 2026-09-05 ("Agree - narrow reading and refuse"; 4.3 agreed): 4.1 NARROW, 4.2 REFUSE, 4.3 named terms. Recorded as R-RA-25 addenda in the rulings ledger.

- **4.1 "derivable"**: define it as "the destination hash `enqueue_data` ALREADY looks up" (`key_hash_of_id` /
  `team_key_of_id`). Adding lookups (e.g. passing the mobile's hash on the hosted last-mile `:1820`) is a wire change
  on flights the corpus exercises and belongs to its own slice if wanted. Recommendation: no new lookups in 0h.
- **4.2 the park clamps**: refuse (mirror on_command) vs `assert`-style unreachable; recommendation: refuse.
- **4.3 the derivation spelling**: `dm_inner_prefix_bytes = 2` (`protocol_constants.h:1058-1062`, comment stale by
  construction) → replace by named terms `1 + 4 + 4` so the 232 is derived where 239 was; the BLE header's
  `kXlInnerOverheadBytes` mirror (device_ble.h) should reference the same named terms or be left as the labelled
  mirror it is (B289 owns the shared constexpr).

## 5. The 232/233 boundary and the mobile-home reproduction (item 5)

- Reuse the pass-2 harness VERBATIM (`…-fable-review-pass2.md` §"Harness") as native cases:
  static bound target — 232 → `queued`, flags `0x06`, `inner_len 241`; 233 → `err_too_large` SYNCHRONOUS, no ctr,
  `tx_queue_n` unchanged, no TX; registered mobile delegated wrapper — 232 → `queued`, flags `0x06`, `inner_len 241`
  (type `0x02`); 233 → refused the same way (was: SOURCE_HASH dropped at 233-236, `inner_len 0` at 237-239).
- Home side: `test_dual_layer.cpp`'s `drive_post_ack_mobile_send_typed` (:545-566) builds `_post_ack` with explicit
  flags and calls `do_post_ack()` — the reproduction is the same drive with `DATA_FLAG_SOURCE_HASH` ABSENT (pack
  without it): today it must show `record_dm`/`msg_recv` at the home (the defect); after 0h a named refusal and
  nothing inboxed. Controls: restore the optional-for-size decision → the 233 case GREEN again (RED expected);
  remove the pack check → `inner_len 0` queued; remove the home refusal → the inbox record returns.
- Tests that MOVE with the cap (symbolic where they use `dm_max_body_bytes`): `test_node_hashlocate.cpp:842-857`,
  `:892-902` (239/240 → 232/233); `test_node_r3.cpp` §B20/B21 sweep table `:7350-7353` (plaintext caps 239 → 232 —
  and ADD the flag assertions the sweep never had); `test_console_parse.cpp` §0f cases (symbolic — follow);
  `test_dual_layer.cpp:2815` (`kCap + 1 < dm_max_body_bytes` — re-check with 232); ⚠ `test_radmin_characterization_0e.cpp:350`
  `CHECK(cap != P::dm_max_body_bytes)` — the "home → target by key hash" carrier cap IS 232 (0e table), so this
  control goes RED by VALUE COINCIDENCE the moment the cap is 232 → re-aim it (its premise "no copied literal" is
  now the derived cap).

## 6. BLE-capacity re-derivation (item 6)

`src/device_ble.h` needs NO edit: `kSendLineMaxBytes` = 5+10+2+`dm_max_body_bytes`+1+15 → **265** (was 272) via
`protocol::dm_max_body_bytes`; `kSendLayerLineMaxBytes` stays **274** (226 = the XL cap < 232, still the binding
producer) ⇒ `kLineStorageBytes` stays **275**; `static_assert(kSendLayerBodyCapBytes < dm_max_body_bytes)` holds
(226 < 232). `tools/probe_ble_line` self-adjusts (`line_send_max()` uses the constant; A5/C3/C13; pins 40/8
unchanged in COUNT). The coder re-derives and prints the new numbers; the Author corrects R-RA-24′ bound 2's "send
272" (historical) and Part 61 step 4 ("272-byte send line" → 265) in the bench script.

## 7. Starting pins (item 7; post-0g, my 0g gate on `31fb680`)

| instrument | pin |
| --- | --- |
| native | 2604 cases / 109619 asserts / 0 failed; `PIN_CASES, PIN_ASSERTS = 2604, 109619` |
| ABI `sizeof(Node)` | 222072 (host) / 117912 (ARM) / 148680 (Xtensa) — unchanged expected (no Node layout change) |
| boards | gateway RAM 195844 · flash 511500 · objects 283; heltec_mobile RAM 205684 · flash 1354672 · objects 327 — RAM ±0 expected; flash moves in `node_mac.cpp.o`/`node.cpp.o` only |
| corpus | 36/36, s18 `32afbf11` / 269517 / 0; lus `eb298576` — the lus REBUILDS (lib/core changes) and must reproduce all 36 |
| tools sweep | 279 OK; console-sink `PINS profiles=6 checks=720 structural=20 ble_guard=212 controls=47`; probe_ble_line 40/8 |
| mutation battery (item 4) — targets whose sources 0h touches | `node.cpp`: `teamgrant`, `b159map` · `node_mac.cpp`: `grantadmit`, `b161mac`, `b20mac`, `b159mac` · `node_hashlocate.cpp`: `grantpark`, `b161hash`, `b251hash` · `protocol_constants.h`: `b159const` · (`frame_codec.h`: `b20codec` only if touched) · `device_ble.h`: none. Run EXACTLY these with `--workers=2` (B286), one full pass of touched targets before reporting |

Base commit at dispatch = the owner's commit of the six Author landing files on top of `31fb680` (name it in the
brief; STOP if different). Fence hypothesis: `lib/core/protocol_constants.h`, `lib/core/node_mac.cpp`,
`lib/core/node_hashlocate.cpp` (clamps), `lib/core/node_mac_rx.cpp` (the home refusal), the moved tests + new
cases, evidence; `src/` untouched; `simulation/BASELINE.md` untouched (36/36 predicted — a mover is a STOP, not a
re-anchor). Bench: a metal part for the 233-byte refusal on USB and the mobile 232-byte delegated DM end-to-end.
