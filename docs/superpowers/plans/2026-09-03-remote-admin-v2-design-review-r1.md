<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 design — independent Quality-Agent review, round 1 (2026-09-03)

**Subject:** `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` (working tree, 1573 lines,
R-RA-1..11 landed). Verified against `HEAD 59b8f01`. Method: full read; every source claim re-verified against the
tree (one sweep over §4/§6/§8/§12-§19 citations, one coherence sweep after R-RA-8); my own protocol review of
§7-§10, §13-§15. Owner rulings are the owner's; the ruling ledger is
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`.

## Verdict: HOLD (round 1) — the model is sound; the document is not yet implementation authority

The independent-request model, the three-level authority, the four-store trust split, the node-owned crypto
boundary, the frozen 64-bit random `request_id`, the R-RA-8 capability split and the B278 consumption rules are
coherent and match the landed code field for field. What blocks authority is (A) four places where the design
asserts an invariant the named existing carrier does NOT satisfy, (B) five protocol details a coder cannot build
or an operator would trip over, and (C) slice-boundary hygiene. All are fixable in one Author round; three items
need an owner word (§D). Nine of the eleven rulings landed EXACT; R-RA-6 and R-RA-9 are under-landed (§C1, §C2).

## A. Source findings that change a design statement (V1 — verified at the code, not the comments)

| # | finding | anchor | required correction |
| --- | --- | --- | --- |
| A1 | **"never `Plane::AUTO`" (§14, ruling 21) is not satisfied by the carrier the design names.** `send_by_hash` takes a `plane` argument but all THREE delegation arms hard-code `/*plane=*/Plane::AUTO` into `do_send`; only the team-resolution guard reads the caller's plane. On a mobile whose home is also a teammate, `flight_is_team_plane(AUTO, home_id)` stamps the TEAM plane on the wrapper leg | `lib/core/node_hashlocate.cpp:1610` (signature), `:1771-1773`, `:1783-1785`, `:1787-1788` (the three `Plane::AUTO`), `:1725` (the guard), `lib/core/node_mac.cpp:168` | §4 must state the fact; Slice 8 must thread the caller's `GLOBAL` into those three `do_send` calls (a `lib/core` change, s18-governed — corpus attribution owed) or the design must say the wrapper leg is exempt and why |
| A2 | **The existing remote helpers attach NO `SOURCE_HASH`.** `send_remote_cmd`/`send_remote_response` call `enqueue_data(..., app_dm=false, ...)` and `SOURCE_HASH` is attached only under `app_dm`. §5/§14's mandatory AEAD-bound `SOURCE_HASH` is therefore unreachable through "the existing typed-DM path" as named | `lib/core/node_mac.cpp:805-813` (helpers), `:200-207` (`app_dm` gate); `do_send`/`send_by_hash` pass `app_dm=true` (`:505`) | §5 item 3 and §19 Slice 8 must name the `send_by_hash`/`do_send` path (app_dm=true, GLOBAL, type 0xA0/0xA1) as the carrier and retire the two helpers in Slice 9 |
| A3 | **The legacy sealed path caps a command at 56 bytes and TRUNCATES silently**, not "at most 204" | `src/firmware_remote.cpp:95-96` (`pt[64]`), `:189-190` (`verb[64]`, `vl = min(qn,63)`), `lib/core/admin_auth.cpp:60` | §4 :128: keep the envelope arithmetic, add the shipped truncation facts; §12's `common_command_max_bytes` characterization must NOT use the current remote path as its "existing command corpus" (no evidence above 56 B exists) |
| A4 | **The 241-byte governing term holds only for a PLAINTEXT carrier.** `data_inner_cap(CRYPTED, type≠0)` = 238 (the B20 banner). §14.1's "RPC sealing does not set `DATA_FLAG_CRYPTED`" is what keeps 231/206 true | `lib/core/frame_codec.h:730-748` | make the dependency explicit in §4 :109 and §8.11 (a control: setting CRYPTED on an RPC carrier reddens the cap test) |
| A5 | The inbound remote slot **refuses** a second frame (`remote_inbound_drop_full`), it does not overwrite | `lib/core/node_mac_rx.cpp:2195-2199`, `lib/core/node.h:2905` | §4 :171-172 wording |
| A6 | The legacy deferred action fires after **3 s**; the proposed 30 s is a 10× change, not a reuse | `src/firmware_remote.cpp:151` | see C2 |
| A7 | Drifted anchors: handler at `node_mac_rx.cpp:2195` (not :1958; the tail guard it precedes is `:2403`); `mr_features.h` span `:11-45` (the gateway arm at `:11-14` is outside `:20-44`); the `Node` admin members are `node.h:2901-2903` (`:103-110` are the accessors) | — | §4 citations |
| A8 | **The corpus carries ZERO remote-admin traffic** (no type 0xA0/0xA1, no `rcmd` emit in 36 streams). Replacing the legacy handler is corpus-inert; its fail-closed boundary can only be proven natively | s5 gate corpus streams | state it in §17/§19 as the gate consequence (C6) |

## B. Protocol findings (my review of §7-§10, §13-§15)

| # | finding | required correction / ruling |
| --- | --- | --- |
| B1 | **§8.1's nonce domain is unbuildable as written**: `selected_key32` is used once (`:551`) and never defined (base key? session key?); "the base-key bootstrap … domain" does not say request vs response, yet the bootstrap REQUEST nonce cannot include an epoch the controller does not know (§7.2) | define `selected_key32` = the key the message is sealed/tagged under (base for bootstrap/rollover-result, session otherwise); state that `admin_epoch_le64` enters ONLY the bootstrap RESPONSE and ROLLOVER_RESULT domains; then Slice 2's KATs can be authored |
| B2 | **The nonce-safety argument for shared credentials is implicit.** Two controller nodes sharing one seed share the session key; equal random IDs are the 2^-33 case, and what actually separates their nonces is `source_hash_le32` in the domain (their `/mrid` hashes differ). The design says "fingerprint detection" (§9) — that is the AFTER-the-fact check, not the safety property | state in §9 that the per-controller `SOURCE_HASH` in the nonce domain is the nonce-uniqueness argument across sharers, and that two sharers with cloned `/mrid` are out of scope |
| B3 | **`session_full` cadence is a product behaviour the design leaves implicit.** Seen records persist until rollover (§10 "not silently evicted"; an ACK frees the transcript, not the record), so every slot hits `session_full` after N executes where N = the measured table size (likely 8-16). §7.3 says the controller "may" send `SAFE_ROLLOVER` | state that the controller performs SAFE_ROLLOVER AUTOMATICALLY on `session_full` (one extra round trip, no operator action), and that R-RA-2's measurement must report N so the owner sees the cadence |
| B4 | **Every RPC request also requests the E2E ACK** (§14.1 bullet 1) ⇒ two return flights per request (the ACK plus the OUTPUT/TERMINAL frames) on the scarcest resource, AND the mobile's concurrent RPC requests are bounded by the delegated-correlation ring (`kDelegAckCap` 8, `node.h:3185`, 300 s TTL) shared with ordinary `-a` sends, because under R1=A only an ACK-requesting delegated flight reserves a row. The design never states either cost | **owner ruling needed (D1)**: keep the E2E ACK on RPC requests (custody feedback + delivered signal, +1 short flight) or drop it (RPC response = receipt; custody feedback then needs an R1 exception). Either way §15's controller pending table must state the ≤8 coupling |
| B5 | `PROTOCOL_ERROR` (REMOTE_RESP 0x4) is listed for both authenticated (slot 0..9) and clear (F) frames; an authenticated request that fails the tag is a silent drop (no oracle), and every authenticated refusal is already a TERMINAL result code | define exactly when an AUTHENTICATED `PROTOCOL_ERROR` is emitted, or restrict the opcode to the clear/open path |
| B6 | Case 4 of §10 ("ID present and response already acknowledged") says what the target must NOT do but not what it sends (nothing? a TERMINAL `completed` replay of the compact record?) | state the response, since the controller's exact-retry path may hit it after a lost ACK |

## C. Coherence and slice-plan findings

| # | finding | anchor | required correction |
| --- | --- | --- | --- |
| C1 | **R-RA-6 under-landed**: the main-NV cleanup is "their own attributable NV-version commit" INSIDE four-topic Slice 9, while §6.5 and the ruling require its OWN slice | §19 :1368-1371 vs §6.5 :388 | make it Slice 10 (or 9b) |
| C2 | **R-RA-9 under-landed**: `remote_action_activation_delay_ms = 30 * 1000` is a bare literal with no derivation; the ruling required a justification line; the legacy value is 3 s (A6) | §13 :1009 | derive it (e.g. from the worst-case response flight time on the delegated carrier: RTS/CTS/DATA/ACK plus one requeue) or rule it (D2) |
| C3 | **Slice 1 silently deletes a live capability**: `MR_FEAT_REMOTE_MGMT` gates the legacy `rcmd` ISSUER as well as accept; `client=0` on static/gateway removes it four slices before legacy deletion, inside a slice that claims "no v2 wire behaviour yet" | `src/firmware_commands.cpp:1071`, `src/firmware_remote.cpp:22/162/192` | Slice 1 keeps `MR_FEAT_REMOTE_MGMT` for the legacy paths beside the two new flags; Slice 9 deletes it |
| C4 | **Native/bare builds default every feature ON** (`mr_features.h:5,27`), so the host binary — the primary gate — would be `{client=1, accept=1}`, which §14 :1081 forbids. Native tests NEED both roles in one process (the S4Chain idiom drives home + mobile in one binary) | `lib/core/mr_features.h:27` | restrict "not both, not neither" to BOARD profiles; state native = both, with the asserts scoped accordingly |
| C5 | Slice 8 emits the structured BLE output/ACK framing that Slice 9 defines and gates (R-RA-11) | §19 :1364 vs :1370 | move the companion-contract extension + its executed gate into or ahead of Slice 8 |
| C6 | **No slice names its gate surface** ("native/corpus/s18/simulator" appear four times in 1573 lines, never per slice). The one `lib/core` edit v2 requires (replacing the handler at `node_mac_rx.cpp:2195`; plus A1's plane threading) is s18-governed, but the corpus has no remote traffic (A8) ⇒ corpus-inert by construction; the fail-closed boundary and every carrier cap are native-only proofs | §19 | each slice states: native cases, mutation targets (file map), corpus expectation (36/36 by construction unless a `lib/core` path with corpus reach moves), ruled board pair, bench part |
| C7 | **M2 debt unbooked**: physical-USB first-owner provisioning, BLE reconnect re-offer, and the USB console-stage-drop rule are metal-only; no bench part is nominated (the only bench reference is Part 54) | §6.4, §8.10 | name the bench parts per slice (3, 4, 8) |
| C8 | §19.1 lists no control for R-RA-9's two deadlines nor for R-RA-10's starvation partition, though the list "must distribute, but not omit" | §19.1 | add both |
| C9 | The control "neither `remote` nor `admin-key` is accepted recursively through target RPC" can only be a structural-absence check after R-RA-8 (those verbs do not exist in accept builds) | §19.1 :1407 | say it is structural, and add the behavioural twin: an accept build receiving `remote …` as the command line returns `unknown_command` |
| C10 | Slice 8 bundles ~12 deliverables (admission, credential selection, target resolution, discovery cache, sealing, carrier, retry, caps, assembly, USB, BLE, pressure) — unattributable under the document's own C1 | §19 :1361-1367 | split at least: 8a controller state + sealing/opening + discovery (host-only), 8b carrier + retry + custody consumption, 8c local USB/BLE delivery |
| C11 | §20.1 #28 "The **primary** controller topology is a node reached locally **over USB**" contradicts §14.1 :1116 "the **only** … over USB or secured BLE" | :1514 | reword #28 |
| C12 | §12.1's physical-only list puts a CLIENT operation (dedicated controller-seed generate/import/export/remove) in the TARGET-side authority list; after R-RA-8 no accept build hosts it; and bullet five is not an operation | :963-967 | split the list by build |
| C13 | Stale cross-references: "for Slice 9" inside §14.1's kept-visible quote (:1124) now means legacy deletion; the register's B278 row still says "REQUIRED BY REMOTE-ADMIN v2 SLICE 9" (`docs/2026-07-30-open-bug-register.md:195`) | — | annotate the quote ("then Slice 9, now Slice 8"); Author fixes the register row (M1) |
| C14 | Slice 6 consumes the owner's authority classification that §20.2 lists as not yet obtained; no fence says the slice cannot start without it | §19 :1354 | add the dependency |

## D. Owner rulings this round needs

- **D1 — E2E ACK on RPC requests (B4).** Keep (custody feedback + delivered signal, one extra short return flight per request, RPC concurrency bounded by the 8-row ring) or drop (response is the receipt; custody feedback for RPC then needs an R1 exception in a separate ruling). My recommendation: KEEP for v2 — the ring bound (8 in flight per mobile, 300 s) is generous for administration traffic and the ACK is the only delivered signal for a command that prints nothing; state both costs in §15.
- **D2 — the 30-second activation promise (C2).** Rule the value or accept a derived one; my proposal: derive from the delegated carrier's worst-case response path (one requeue + the hop timers, currently ≈ 15 s) with 2× margin, stated as a named formula.
- **D3 — native build flags (C4).** Confirm the host build is `{client=1, accept=1}` and that the "not both" assert is board-profile-only.

## E. What round 2 must show

Author folds A1-A8, B1-B6, C1-C14 in with the correction idiom (old claim visible where a statement changes),
records D1-D3 once ruled, and re-marks the status line. I re-read the changed sections against the tree; PASS
then authorizes the Slice 0 briefs (0a/0b/0c/0d) and the characterization slice through the normal pre-check →
brief → gate cycle. Nothing in this review reopens R-RA-1..11.
