<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 5 — Quality-Agent pre-check ledger (2026-09-06): target authenticated session and dedup state

Authority: design §19 item 5 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1968-1979`,
incl. the 2026-09-04 correction that fences Slice 5 to PRE-transcript state), §19.1 row 5 ("target session/dedup
files | zero remote events, 36/36 unchanged; ruled pair | none", :2045), §7.1-7.3 (:573-660), §9 (:1035-1089),
§10 (:1090-1134), §15 (:1557-1623), §14 (the receive-dispatch split :1387-1420); rulings R-RA-2/5/8/10/13/22/27
(the managed profile's 2 968 candidate bytes, the partition rule, the ONE shared expiry timer with `kCap` 91 → 92);
the 0e candidate types (`test/radmin_0e_candidate_types.h:180-260`, evidence `2026-09-04-radmin-0e.md:240-330`);
the Slice 1b seam (`lib/core/node_mac_rx.cpp:1915-1975`). Written at `HEAD a0ff994` while Slice 4 runs on the main
tree ⇒ Slice 4's landings (client stores, `tools/` extensions, the CLIENT profile axis) are not yet in the tree;
re-verify every anchor by symbol at brief time against the Slice 4 closure. Hypotheses, not authority.

## 0. What Slice 5 is, in one line

On ACCEPT builds, land the target's pre-transcript authenticated-session state as core files: the per-slot
epoch and base/session-key derivation, full-key ACL discovery/bootstrap, the bounded seen-request table with
request-fingerprint classification (§10 cases 1-5 as VERDICTS), the partitioned ingress that REPLACES the legacy
one-slot staging (R-RA-27's explicit hand-off to Slice 5), the open-staging rows, and the ONE shared earliest-deadline
expiry timer — with the measured table count `N` reported. NO transcript, retry, ACK debt, `session_full`/`busy`,
rollover or `already_acknowledged` (Slice 7b); NO dispatcher context (Slice 6); NO controller side (8a); NO delegated
carrier (8b). The corpus stays 36/36 by construction (no scenario airs `0xA0`/`0xA1`).

## 1. Source facts — the seam Slice 5 inherits, and what it must replace

| fact | anchor |
| --- | --- |
| the ACCEPT-owned entry point is `Node::rx_remote_cmd_accept(pa, ui)` → `remote_inbound_stage(pa, ui, false)`: the LEGACY staging into ONE unconditional `RemoteInbound _remote_inbound` (≈246 B on EVERY profile), keyed on the 8-bit `pa.origin`, silently CLAMPING `n > inbox_max_body`, requiring no `SOURCE_HASH`; drained by `take_remote_inbound` in `fw_main.cpp:1708` into the legacy `remote_exec`. Its own comment marks FOUR things "MISSING/deferred by design — Slices 5/7b": mandatory `ui->has_source_hash` (R-RA-13), 32-bit source identity, refuse-don't-clamp, and ROLE-OWNED storage ("R-RA-22's partitioned admission replaces it in Slice 5; R-RA-27 explicitly keeps the slot unconditional here") | `lib/core/node_mac_rx.cpp:1915-1975`; `lib/core/node.h:154-163, :2944`; `src/fw_main.cpp:1708` |
| the CLIENT-owned twin `rx_remote_resp_client` uses the SAME slot (`is_response = true`) and the same drain ⇒ replacing `_remote_inbound` on ACCEPT builds must leave the client staging in place until Slice 8a's pending table exists: the slot becomes `#if MR_FEAT_RADMIN_CLIENT` (heltec_mobile RAM ±0), the accept arm stops feeding it (the legacy `rcmd` EXECUTION on targets ends here — already suspended on the bench since 1b; Slice 9 deletes the rest) | `node_mac_rx.cpp:1976-1983`; 1b evidence §"Slice 5 owns the replacement" |
| the seam ORDER is ruled: the owned handlers sit BEFORE the SEALED_RELAY / CRYPTED open steps (a v2 body is RPC-encrypted inside a PLAINTEXT-framed DM) and after every forwarding role; a v2 request is the parsed `data_unicast_inner` (`ui->body`, `ui->source_hash`, `ui->has_source_hash`, `ui->dst_key_hash32`) | `node_mac_rx.cpp:1931-1934`; the 1b brief |
| the codec is consumer-free and ready: `remote_layout` / `remote_body_decode(out, outer_type, body, keys, src, carrier, pt)` / `remote_body_encode` / `remote_ecdh_shared` / `remote_kdf_base` / `remote_kdf_session` / `remote_nonce`; `RemoteSource{present, hash}` = the carrier's `SOURCE_HASH`; `RemoteCarrier` describes the leg; `kRemoteSlotSessionMax = 9`; the decoded `RemoteDecoded` carries the domain, the request id, the controller pub (bootstrap), the epoch (responses only) | `lib/core/remote_codec.h:266-345` |
| the target's ECDH input is the ADMINISTRATION identity (`/mradmid`, Slice 3) — held NOWHERE resident today by decision ("Design §6.2's residency question is SLICE 5's, with its own RAM attribution"): the seed is read per verb and wiped. `Node` already holds the MESSAGING identity's `_x_secret[32] + _ed_pub[32]` (64 B, `set_crypto_identity`, installed by `fw_main` at boot) — the exact precedent for a resident admin pair | `src/firmware_admin_identity.h:24-28`; `lib/core/node.h:101, :2913-2914`; `lib/core/node.cpp:80-83`; `src/fw_main.cpp:814-822` |
| the ACL lives in NV (`/mracl`, Slice 3) with NO live copy — but design §6.5 REQUIRES one: "candidate copy, full validation, durable save, then LIVE ACTIVATION … save failure leaves the old ACL and sessions active … role change or removal invalidates that slot's session only after the new ACL is durable" — i.e. the target's running state is a resident ACL image the verbs re-install after a durable save. Core has no NV access (the record layer is `src/`), so the image must be INSTALLED into `Node` by the firmware, exactly as identities are | design :419-448; `src/firmware_admin_acl.h` (`AclService`, `acl_content_valid`), `src/firmware_commands.cpp` (the Slice 3 bindings) |
| entropy in core is the void `_hal.rand_bytes` with the established dead-RNG guard idiom (an all-zero draw REFUSES, no fallback) — the per-slot 64-bit `admin_epoch` at boot needs the same guard (B312 stays open) | `lib/core/node.cpp:90-100`; `lib/core/hal.h:183` |
| the timer wheel is FULL: `kCap = 91`, "all consumed", the last id `kE2eAckDeadlineTimerId = 90`; R-RA-22 rules `kCap` 91 → 92 (+8 B measured on all three ABIs) and ONE shared earliest-deadline scan; the idiom exists twice: `kParkRefloodTimerId = 89` (`node_hashlocate.cpp:2424-2427`) and `kE2eAckDeadlineTimerId = 90` (`node_mac.cpp:541-543`, `node.cpp:1340`) — cancel when nothing pends, re-arm to `earliest − now` | `lib/hal/timer_wheel.h:25`; `lib/core/protocol_constants.h:403`; `lib/core/node.h:1527-1528` |
| `sizeof(Node)` is PINNED three ways and every Node member moves it: `node.h:3997` (native 222072 static_assert + ledger), `tools/probe_board_abi.py:257/:278/:302` (222072 / 117912 / 148680), the B278 row-ABI mirror ⇒ Slice 5 needs an AUTHORIZED ABI re-pin with a per-board RAM diff and a `-Wreorder`-clean placement (D2) | those files |
| the simulator LISTS `lib/core` sources (21 lines; `remote_codec.cpp` at `CMakeLists.txt:77`) ⇒ a new `remote_session.cpp` is ONE more line in that separate repository; the `lus` binary WILL change (Node's layout moves) while every stream must stay byte-identical — predict "binary moves, 36/36 identical", never "byte-identical binary" | `/home/staszek/lora-universal-simulator/CMakeLists.txt:58-83` |
| ★ CROSS-LAYER TARGETS ARE IN SCOPE and the reply must be able to go BACK ACROSS LAYERS: the parsed request carries `has_cross_layer / n_layers / cur / layer_ids[4]` (`frame_codec.h:1339-1340`), Slice 2 caps the full cross-layer carriers (229..226) and the typed wrapper (228..226), and the design's 8b row names "target response … and every cross-layer variant". The precedent for a target-side cross-layer reply already exists: §GapB `send_xl_ack` — "a cross-layer E2E-ack = a NORMAL send on the REVERSED path" (`node_mac_rx.cpp:2591`, `node.h:1681`). ⇒ the ingress/seen row must RETAIN the return-path facts per admitted request — `source_hash` (the reply destination), the received `layer_ids[]`/`n_layers`/`cur` (reversed for the reply) and the carrier kind (the 0e `IngressOperationHeader` has `origin` + `carrier` but no path bytes: +5 B/row, to be predicted) — or a cross-layer controller's request can be executed but never answered | `lib/core/frame_codec.h:1335-1341`; `lib/core/node_mac_rx.cpp:2591`; `lib/core/node.h:1681`; design :2121 |
| the sim-only telemetry idiom for the §15 "observable counters": `MR_EMIT("remote_inbound_drop_full", …)` — device-stripped; the CONSOLE `status` is where a device-visible counter would surface (Slice 6/7b), not this slice | `node_mac_rx.cpp:1952` |

## 2. Source facts — the ruled numbers Slice 5 must reproduce, split by owner

| R-RA-22 managed-profile row | bytes (0e per-ABI, identical on host/ARM/Xtensa) | owner |
| --- | ---: | --- |
| 16 × `SeenRequestRecord` (32 B: request_id u64 · admin_epoch u64 · first_seen_ms · source_hash · slot · result_code · state · transcript_slot) | 512 | **Slice 5** (⚠ the 0e row lacks the **128-bit request FINGERPRINT** §10 requires — "retains the original authenticated request tag as the exact 128-bit request fingerprint" — so the production row is 32 + 16 = **48 B** ⇒ 768; state it, never squeeze) |
| 2 × `IngressOperationHeader` (32) + 2 × `IngressBodySlot` (236) | 64 + 472 = 536 | **Slice 5** (the "reservation shape"; the two authenticated rows PARTITIONED: one always free for owner/control) |
| 4 × `OpenStagingSlot` (24) | 96 | **Slice 5** (per-peer maximum ONE open response; open work cannot borrow authenticated capacity) |
| 4 × `TranscriptHeader` (24) + 8 × `TranscriptChunk` (210) + 2 × `DeferredActionRecord` (24) | 96 + 1 680 + 48 = 1 824 | **Slice 7b** — ⛔ not resident in Slice 5 |
| per-slot epoch (10 × u64) + resident admin pair (64) + live ACL image (10 × 33 → 340 with `role` + padding) | 80 + 64 + 340 = 484 | **Slice 5** (not in the 0e sheet — a NEW attribution the brief must predict) |
| minus the legacy slot on ACCEPT builds | − ≈246 | **Slice 5** |
| `TimerWheel` `kCap` 92 | + 8 | **Slice 5** — ⚠ **CORRECTED 2026-09-06 (S5-A1), old placement kept visible:** this row sat in the Node attribution; the wheel is `DeviceHal::_wheel` (`lib/hal/device_hal.h:175`), NOT a Node member ⇒ the +8 B is HAL RAM on EVERY build, `heltec_mobile` included, and the wheel's code may move mobile flash |

⇒ predicted `gateway` RAM ≈ **+1 650 B** (83.1 % → ≈ 83.8 % of 235 520). ⚠ **CORRECTED 2026-09-06 (S5-A1), old claim kept
visible:** this said *"`heltec_mobile` ±0 (the client keeps the slot; every accept member is `#if MR_FEAT_RADMIN_ACCEPT`)"* —
`sizeof(Node)` is unmoved there, but board RAM is NOT: the shared `TimerWheel` (+8 B) lives in `DeviceHal` on every
build, so the mobile prediction is **+8 B HAL RAM, flash attributed** (the Author's brief §4.6). The gateway delta also
gains the firmware's legacy drain statics (`static RemoteInbound ri`, `static uint8_t pt[241]`, `src/fw_main.cpp:1708/:1737`)
freed by the CLIENT-only guard. `N` = 16 seen rows per target (shared across the ten slots
as ONE pool keyed by (slot, request_id), §10 "the physical storage may be one shared bounded pool") is the number the
design asks to be REPORTED — and it fixes the automatic safe-rollover cadence (§7.3), a product cost.

## 3. Source facts — the classification the table must implement (design §10, as verdicts)

| case | input | verdict (no execution in Slice 5 — the verdict is what Slice 7b acts on) |
| --- | --- | --- |
| 1 | id absent, capacity available | `admit` (reserve the seen row + the ingress body; the transcript reservation is 7b's) |
| 2 | id present, tag identical, unacknowledged | `replay_transcript` (7b resends; Slice 5 returns the verdict + the row) |
| 3 | id present, tag DIFFERS | `reject_id_reuse` — never dispatch either plaintext |
| 4 | id present, acknowledged | `already_acknowledged` (7b emits the PROTOCOL_ERROR; the domain exists in the codec) |
| 5 | this slot's table full | `session_full` (7b answers; rollover is 7b) |
| — | tag fails / no ACL row / bad conversion / low-order point | SILENT authentication failure (§7.2: "do not reveal ACL membership") |
| — | epoch mismatch (a request sealed under an old epoch) | the tag FAILS by construction (different session key) ⇒ silent |
| — | open `status`/`routes` | never enters the seen table; bounded by the open staging + rate limit (the numbers are Slice 6/7b's) |

Bootstrap (§7.2) is read-only: exact full-key lookup over the ten LIVE rows → derive the base key for THAT row only →
verify the tag → answer `[ctl(slot)][request_id][admin_epoch][tag]` under the base key. It never changes the epoch and
never dispatches.

## 4. Gates the brief must name explicitly

| gate | what Slice 5 changes in it |
| --- | --- |
| ABI | `sizeof(Node)` MOVES on native + gateway (accept members) and must NOT on heltec_mobile (client): an AUTHORIZED re-pin of `node.h:3997` + the ledger, `probe_board_abi.py` rows 257/302 (278 unchanged), with the `-Wreorder`/padding-placement proof and the per-board RAM diff attributed member by member |
| simulator | ONE source-list line (`remote_session.cpp`); both variants compile it; the `lus` binary CHANGES (Node layout) — predict it, prove 36/36 byte-identical streams + anchors, no re-anchor |
| board pair | gateway RAM ≈ +1 650 (predicted above, every member attributed) and flash + (the session logic + the accept arm); heltec_mobile RAM/flash ±0 (the slot stays, nothing accept-side compiled) |
| feature census | new `MR_FEAT_RADMIN_ACCEPT` sites in `node.h` (members + declarations), `node_mac_rx.cpp` (the accept body), `node.cpp` (the timer case + boot install), `fw_main.cpp` (installing the admin pair + the ACL image at boot), `firmware_commands.cpp` (the Slice 3 verbs' live activation after a durable save) — the EXACT multiset on top of the Slice 4 closure census; the 1b RX-decision controls preserved |
| mutation | new per-file target for `remote_session.cpp`; changed-source: the six RX batteries on `node_mac_rx.cpp` (`b161rx/b251rx/b159rx/a0rx/sliceBrx/sliceGrx`) + whichever `node.cpp`/`node.h` batteries exist; dependency: `radmin2codec` in FULL (the codec is now CONSUMED), `radmin3acl` (the live-activation install path) |
| native | the {1,1} host build drives the REAL accept arm end to end with requests BUILT BY THE CODEC (no client logic yet): admit → replay → reuse-reject → acknowledged → full; bootstrap for every slot, wrong key, low-order point, wrong tag, wrong epoch; partition starvation BOTH directions; open-staging per-peer bound; the timer's exact-edge expiry + re-arm + cancel-when-empty + `kCap == 92` + one id; the dead-RNG epoch refusal; `N` reported |
| corpus | 36/36 identical by construction (no scenario airs the types); the recompile control is REAL this time (core changed) |
| metal | none (§19.1 row 5) — but the legacy target-side `rcmd` execution ENDS here; the bench's suspended round-trip note gains "removed in Slice 5" |

## 5. Shape the brief must pin

- **Files**: `lib/core/remote_session.{h,cpp}` (MESHROUTE_NS; the value types, the pool, the classifier, the bootstrap
  logic, the earliest-deadline computation — PURE, driven by explicit inputs and a `now`), owned by `Node` as ONE
  `#if MR_FEAT_RADMIN_ACCEPT` member block placed by the padding-placement rule; `Node::set_admin_identity(x_secret,
  ed_pub)` + `Node::install_acl(rows[10])` (the two firmware-installed images, mirroring `set_crypto_identity`);
  the accept arm rewritten to: require `ui->has_source_hash` (R-RA-13, refuse otherwise), REFUSE over-cap (never
  clamp), decode via the codec, classify, reserve; the client slot guarded `MR_FEAT_RADMIN_CLIENT`.
- **Epoch**: ten `uint64_t` minted at boot from `_hal.rand_bytes` with the dead-RNG guard; an all-zero draw leaves the
  accept path REFUSING (fail closed, reported on the boot line) — never epoch 0 in service.
- **Tables**: 16-row seen pool (48-B rows incl. the fingerprint), 2 + 2 ingress with the owner/control reservation,
  4 open-staging rows with the per-peer bound; "session records are not silently evicted while their session key
  remains valid". ⚠ **CORRECTED 2026-09-06 (S5-A2), old wording kept visible:** this said *"the ONLY clearing is
  rollover (7b) and the explicit ACK release (7b)"* — an ACK releases the TRANSCRIPT/body (7b), ⛔ NEVER the same-epoch
  seen fingerprint: design §10 case 4 needs that tombstone to answer `already_acknowledged`. The only clearing of a
  seen row is an epoch-invalidating rotation; ingress/open rows expire on the shared timer.
- **What Slice 5 answers on air** — §6.1 below (owner/Author decision).
- **Prediction**: RAM/flash per board attributed; `sizeof(Node)` per ABI; `lus` moves, 36/36 identical; native +cases;
  census multiset; inventory unchanged (no verb); help unchanged.

## 6. Open points — 6.1 and 6.2 RULED (R-RA-31, 2026-09-06); the rest are Author decisions with QA's recommendation

- **6.1 ✅ RULED 2026-09-06 (R-RA-31: YES — decode, classify, and send the bootstrap response on air, two-shaped: same-layer by hash or cross-layer on the reversed path; execute/transcript replies stay 7b's) — the question as put:** may Slice 5 answer a bootstrap on air? The state is inert unless the accept arm can REPLY. The
  target's reply leg is a same-layer by-hash send (`send_by_hash(dst = ui->source_hash, type = REMOTE_RESP, plane
  GLOBAL)` — ⚠ corrected 2026-09-06 (S5-A3): `send_by_hash` has NO `app_dm` parameter (that is `do_send`/`enqueue_data`'s,
  `node_hashlocate.cpp:1690`), and its `uint16_t` return is the immediate ctr, 0 = "not sent NOW" (parked is not a failure):
  the admission fact is the `SendDispatch* out_dispatch` out-parameter (`node.h:357`, `:1643`) — queued / parked / refused — the "target response" carrier row Slice 2 already caps at 232) and is not the mobile-delegated
  carrier Slice 8b owns. **Recommendation:** YES — wire the accept arm to decode/classify AND send the bootstrap
  response through the existing `send_by_hash`, so the native {1,1} build proves discovery end to end and the first
  metal step (a later Part 57c) has something to observe; execute/transcript replies stay 7b's. The reply seam is
  TWO-shaped from day one: same-layer by hash, or cross-layer on the REVERSED received path (the `send_xl_ack`
  precedent) — a cross-layer controller must get its bootstrap answer back too. The corpus is
  unaffected either way. The alternative keeps Slice 5 consumer-free (state + classifier only, no TX).
- **6.2 ✅ RULED 2026-09-06 (R-RA-31: authorized — native + gateway re-pinned with member-by-member attribution, heltec_mobile UNMOVED as the control) — the question as put:** the ABI re-pin. Every accept member moves `sizeof(Node)` on native and gateway; the standing rule
  says "no ABI re-pin unless separately authorized". **Recommendation:** authorize it for Slice 5 with the
  member-by-member attribution and the unmoved heltec_mobile pin as the control.
- **6.3 (Author) residency shape**: the resident admin pair (64 B) + the live ACL image (≈340 B) installed by the
  firmware at boot and re-installed by the Slice 3 verbs after a durable save (the design's "live activation"), vs a
  per-request NV read through a core→device seam. **Recommendation:** resident images, the `set_crypto_identity`
  precedent; the Slice 3 verbs gain ONE call each after `commit_` succeeds (`admin-id rotate/reset` re-install the pair
  and clear the epochs; `acl` mutations re-install the image) — a small Slice 3 file edit that IS this feature.
- **6.4 (Author) the seen row carries the 128-bit fingerprint** (48 B, not the 0e sheet's 32) — restate `N` and the
  RAM prediction from the real row; the ruling's totals are inputs, not a licence for an unexplained delta.
- **6.5 (Author) the legacy slot becomes CLIENT-only** and the target-side legacy `rcmd` execution ends in Slice 5
  (its bench check is already suspended); `take_remote_inbound`'s drain in `fw_main.cpp:1708` keeps serving the
  client's response print until 8a.
- **6.6 (Author) the timer**: `kCap` 92, ONE id (`kRadminExpiryTimerId = 91`), the scan owned by `remote_session`
  with the two precedents' cancel/re-arm shape; exact-edge expiry mutation-pinned; no per-class ids.
- **6.7 (Author) proposed register rows start at B327 + Slice 4's count** — the Author records the next free number at
  brief time (Slice 4's coder proposes from B327).

Starting pins = the Slice 4 CLOSURE's (record at brief time). Bench: none.
