<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Firmware bench acceptance checklist — 2026-07-31 work

This is the manual hardware gate for the 2026-07-31 firmware work and its follow-up fixes. It is organized for execution:
prepare each rig once, work through the checkboxes, and save full serial captures outside this document.

## Current release state

- Parts 0-7 describe the committed firmware gates. Part 8's OLED Task 7 tree is **QG-approved but uncommitted** on
  top of `cbbd69e`; record the exact `version`/dirty state used for every OLED result.
- B43's one-command by-ID `reqpubkey` flow landed at `2ff40dc`.
- B30's freshest-authoritative team-ID selection landed at HEAD `ea1e324`.
- Flash `ea1e324` or newer for the current gate.
- Automated baseline: native 1,149 cases / 72,681 assertions, board builds, and 36 simulator scenarios. This checklist
  focuses on device-only wiring, persistent storage, radio behavior, and operator surfaces.

## How to use this document

- `[x]` means the user had already marked the check successful in the previous document.
- `[ ]` means pending on current firmware. Check it only after observing the stated pass condition.
- `OBSERVE` means saturation may not occur on demand. No event is not a pass or failure; a contradictory event is.
- `RETIRED` and `KNOWN GAP` entries are not runnable checks.
- Add a short note only for the build, node IDs, or output needed to reproduce a failure.

The checked state was migrated from the old `ok` markers. It does not mean those checks were rerun on `ea1e324`.

## Bench worksheet

| Role | Label / board | Build (`version`) | Static ID / hash | Team-local ID | Team key |
|---|---|---|---|---|---|
| A — primary sender |  |  |  |  | yes / no |
| B — direct peer |  |  |  |  | yes / no |
| C — multi-hop peer |  |  |  |  | yes / no |
| D — relay / keyless peer |  |  |  |  | yes / no |

Before each multi-node block:

1. Run `version`, `whoami`, and `cfg` on every participating node.
2. Confirm frequency, SF/BW/CR, team ID, and intended role match.
3. Record whether `team_ch_key=1`; do not infer possession from team membership.
4. Use unique message text such as `B30-20260802-01`.
5. In radio traces, `to=` is the immediate next hop and `dst=` is the final destination. CTS/ACK proves one-hop
   handoff, not application delivery or key installation.

## Suggested run order

1. Fresh or expendable 32-bit node: Parts 0–2.
2. Two directly connected nodes: Parts 3–5.
3. Team rig, including one keyless node: Part 6.
4. Static, team, dual-plane, and saturation rigs: Part 7.
5. Heltec V3 mobile UI: use the focused guide and run **H5, H6, then H7 through H7-09**. In this document, Part 8's
   Task 6/7 checks are live except retired 8.1/8.9, conditional diagnostics, and Task-9-only 8.6. **Stop before H8**
   in the focused guide; H8 and H9 are not yet released for execution.

If Part 1 or Part 2 fails, stop and report before continuing.

---

## Part 0 — first boot after flashing

These checks reject old-version NV state. Use a node whose old configuration and peer store may be reset.

- [ ] **0.1 — Peer-store version transition**
  - Do: flash current firmware and watch the first boot lines.
  - Pass: `peers = 0 restored (0 pinned, 0 authoritative)` appears; rejection of the old store is observable once.

- [ ] **0.2 — Configuration-store version transition**
  - Do: run `status` after first boot.
  - Pass: the node is unprovisioned. This reset is independent of the peer store.

- [ ] **0.3 — Clean second boot**
  - Do: reboot without adding peers.
  - Pass: `peers = 0 restored ...` appears and there is no `REJECTED` warning.

---

## Part 1 — parser and role refusals

Use `xiao_sx1262` or another 32-bit target. Check 1.2 exercises a width difference the 64-bit native build cannot.

- [x] **1.1 — Valid prefixed team ID**
  - Do: `team 0x88A672BA`
  - Pass: accepted; the node joins that team.

- [ ] **1.2 — Reject a value wider than 32 bits**
  - Do: `team 4294967296`
  - Pass: `bad target` says the whole token must fit in 32 bits; the node must not join `0xFFFFFFFF`.

- [x] **1.3 — Reject unprefixed hexadecimal text**
  - Do: `team 88A672BA`
  - Pass: refused; it must not silently parse as decimal 88.

- [x] **1.4 — Accept the maximum in-range value**
  - Do: `team 0xFFFFFFFF`
  - Pass: accepted.

- [x] **1.5 — Removed raw team-ID config key**
  - Do: `cfg set team_id 5`
  - Pass: `unknown_key`. Acceptance means the wrong firmware is flashed.

- [ ] **1.6 — Read surface retained**
  - Do: `cfg`
  - Pass: no `loc_dm=` is present, while `team_id=0x...` remains readable.

- [x] **1.7 — Team member cannot become static in place**
  - Do: with a team set, run `cfg set mobile 0`.
  - Pass: `role_refused in_a_team` with guidance to leave first.

- [x] **1.8 — Role refusal is conditional**
  - Do: `team 0`, then `cfg set mobile 0`.
  - Pass: accepted.

- [x] **1.9 — Joining a team makes a static node mobile**
  - Do: on a static node, run `team 0x1234`, then `status`.
  - Pass: `role -> MOBILE` prints before the team line; status reports `is_mobile`.

- [x] **1.10 — Gateway remains static**
  - Do: on a gateway build, run `cfg set mobile 1`.
  - Pass: `role_refused gateway_is_static`.

- [ ] **1.11 — Host cannot abandon active guests**
  - Do: on a node hosting at least one mobile, run `cfg set mobile 1`.
  - Pass: `role_refused hosting_mobiles n=<N>`.

---

## Part 2 — NV write coalescing

The stock console exposes no config-write counter. An `ok` reply alone cannot prove this section. Use a temporary
device-side write trace/breakpoint around the NV backend or an external flash-write observation method. Otherwise leave
2.1 and 2.2 unchecked.

- [ ] **2.1 — Identical values do not write**
  - Do: note the current `beacon_ms` and set that same value five times.
  - Pass: every command returns `ok`; the NV backend records zero writes.

- [ ] **2.2 — One real change produces one write**
  - Do: change `beacon_ms`, then repeat that same command four times.
  - Pass: only the first command writes.

- [ ] **2.3 — The real change persists**
  - Do: reboot and run `cfg`.
  - Pass: the changed value survives.

---

## Part 3 — direct messages with `-l`

Use two nodes that hear each other.

- [x] **3.1 — Location requested without a fix**
  - Do: set latitude and longitude to zero; run `send <peer> "hi" -l`.
  - Pass: `no_location`, not an encryption error.

- [x] **3.2 — Location cannot downgrade to cleartext**
  - Do: set a real location, turn `e2e_dm` off, remove the peer key, then run `send <peer> "hi" -l`.
  - Pass: `unsealable`; no location-bearing frame is sent.

- [x] **3.3 — Ordinary plaintext DM remains available**
  - Do: in the same state, run `send <peer> "hi"` without `-l`.
  - Pass: the DM sends normally.

- [ ] **3.4 — Sealed located DM succeeds**
  - Do: acquire the peer key, then `send <peer> "hi" -l -e`.
  - Pass: the peer receives the message and `msg_recv` contains the position.

- [ ] **3.5 — Cross-layer located DM is unsupported**
  - Do: run `send_layer ... -l` with valid remaining arguments.
  - Pass: `err_unsupported`.

- [ ] **3.6 — Non-team located channel post cannot be sealed**
  - Do: `send_channel <ch> "x" -l` without `-t`.
  - Pass: `unsealable`.

---

## Part 4 — peer-store persistence

- [x] **4.1 — Learn an authoritative peer on air**
  - Do: `reqpubkey 0x<hash>`.
  - Pass: `KEY CACHED ... conf=authoritative nv=<put>`.

- [ ] **4.2 — On-air key survives reboot**
  - Do: reboot.
  - Pass: `peers = 1 restored (0 pinned, 1 authoritative)`.

- [ ] **4.3 — Restored key is immediately usable**
  - Do: before another request, `send <peer> "x" -e`.
  - Pass: sealed DM succeeds.

- [ ] **4.4 — QR provenance remains pinned**
  - Do: import with `peerkey <hex64>`, reboot, and inspect `peers`.
  - Pass: the peer restores as pinned.

- [ ] **4.5 — Peer name survives reboot**
  - Do: `peername 0x<hash> "Alice"`, reboot, then `nameof 0x<hash>`.
  - Pass: `Alice` is returned.

Observe without treating it as a binary pass: a busy mesh should not visibly churn NVS pages, and post-boot trial
decryption with a full 16-key store should remain responsive.

---

## Part 5 — address-book commands

- [x] **5.1 — Set a peer name**
  - Do: `peername 0x<hash> "Alice"`.
  - Pass: `{"ev":"peer_name_set","hash":...,"name":"Alice"}`.

- [x] **5.2 — Reject a name for an unknown hash**
  - Do: `peername 0x<unknown> "X"`.
  - Pass: `peer_name_err` with `reason:"unknown_hash"`.

- [ ] **5.3 — Reject overlong names without truncation**
  - Do: set a 40-character name.
  - Pass: `too_long`; no shortened name is stored.

- [ ] **5.4 — Naming does not demote pinned trust**
  - Do: name a QR-pinned peer and inspect `peers`.
  - Pass: confidence remains pinned.

- [ ] **5.5 — Team ID resolves after key acquisition**
  - Do: on an off-grid team node, acquire with `reqpubkey <team-id> -t`, then `hashof <team-id> -t`.
  - Pass: the teammate's hash resolves.

- [x] **5.6 — Bounded and full views differ intentionally**
  - Do: compare `peers` and `peers all`.
  - Pass: `peers` is the bounded key book; `peers all` may add ID-only route rows.

---

## Part 6 — team channel encryption, inbox delivery, and key grants

Use A as keyholder/sender, B as keyholder/receiver, and C as a keyless team member. Confirm `team_ch_key` separately on
each. A keyholder with `team_channel_crypt=1` encrypts even when `-e` is omitted; explicitly disable the default to test
plaintext.

- [x] **6.1 — Explicit encrypted team post with location**
  - Do: `send_channel 7 "at the col" -t -l -e`.
  - Pass: B shows `CH 7 [enc]` and A's peer row on B gains team-sourced location.

- [x] **6.2 — Default encryption applies without `-e`**
  - Do: with `team_channel_crypt=1`, run `send_channel 7 "at the col" -t -l`.
  - Pass: B receives an encrypted post.

- [x] **6.3 — Position-only encrypted post**
  - Do: `send_channel 7 "" -t -l -e`.
  - Pass: accepted and B receives the location.

- [x] **6.4 — Location without a team is refused**
  - Do: `send_channel 7 "x" -l` without `-t`.
  - Pass: `unsealable`.

- [x] **6.5 — Global cleartext copy with location is refused**
  - Do: `send_channel 7 "x" -t -g -l -e`.
  - Pass: `unsealable`.

- [ ] **6.6 — Empty encrypted post without location is refused**
  - Do: `send_channel 7 "" -t -e`.
  - Pass: synchronous empty-post error; no `FAILED` push is expected.

- [ ] **6.7 — Opt-out prevents implicit location sealing**
  - Do: `cfg set team_channel_crypt 0`; then `send_channel 7 "x" -t -l`.
  - Pass: `unsealable`.

- [ ] **6.8 — Explicit `-e` overrides the live opt-out**
  - Do: in the 6.7 state, run `send_channel 7 "x" -t -l -e`.
  - Pass: encrypted post succeeds.

- [ ] **6.9 — Missing fix is reported before key advice**
  - Do: restore crypt default, zero latitude/longitude, then send with `-t -l`.
  - Pass: `no_location`, not `no_key`.

- [ ] **6.10 — Explicit encryption from a keyless sender is refused**
  - Do: on C itself, confirm `team_ch_key=0`; run `send_channel 7 "x" -t -e`.
  - Pass: `no_key`.

- [ ] **6.11 — Keyless sender may still post plaintext**
  - Do: still on C, run `send_channel 7 "x" -t` without `-e`.
  - Pass: accepted as plaintext. Do not run this control on a keyholder with default encryption enabled.

- [ ] **6.12 — Team switch clears content key**
  - Do: on A, run `team 0x<other>`, then `cfg`.
  - Pass: `team_ch_key=0`.

- [ ] **6.13 — Sealed size boundaries**
  - Do: test 173/174-byte bodies, then 167/168-byte bodies with `-l`.
  - Pass: 173 and 167 succeed; 174 and 168 are refused.


### B30 regression — final destination versus next hop

The old-firmware failure was reproduced on 2026-08-02: hash `0x7B18ADA2` had stale team ID 245 and current team ID 86;
`team grantkey 86` produced `to=86 dst=245`. Node 86 correctly acted only as the next-hop relay, returned CTS/ACK, and
did not install the key. That is historical failure evidence, not a pass on fixed firmware.

- [ ] **6.14 — Preserve a meaningful aliased-ID precondition**
  - Do: after flashing `ea1e324` or newer without erasing the relevant peer NV, run `peers all` on the grantor.
  - Pass: the current row is `team_id=86(auth)` for `0x7B18ADA2` and reports `+1 stale team-id alias dropped`. If no
    known alias exists, mark N/A; an ordinary grant cannot validate B30.

- [ ] **6.15 — Grant targets freshest authoritative ID end to end**
  - Do: on the grantor, run `team grantkey 86 -t` and watch both nodes.
  - Pass on sender: RTS and DATA use final `dst=86`, never `dst=245`. `to=` may be 86 or another next hop.
  - Pass on receiver: after DATA is processed, `team exportkey` succeeds. A hop ACK is insufficient evidence.

### Channel delivery and merged-inbox controls

- [ ] **6.16 — Encrypted post reaches the durable channel inbox**
  - Do: with A and B holding the same key, send `send_channel 0 "INBOX-ENC-<nonce>" -t -e`; then run
    `pull_inbox 0 0` on B.
  - Pass: B emits live encrypted channel receive and a matching `inbox_channel` row with `team_id`, `enc:true`, and the
    same body; `inbox_end.chan_seq` advances.

- [ ] **6.17 — Explicit plaintext post reaches the durable channel inbox**
  - Do: on A, set `team_channel_crypt=0`; send `send_channel 0 "INBOX-PLAIN-<nonce>" -t` without `-e`; pull B's inbox;
    then restore `team_channel_crypt=1`.
  - Pass: B receives readable plaintext and a matching `inbox_channel` row with encryption false or omitted. Do not
    leave the sender's default encryption enabled for this test.

- [x] **6.18 — Keyless receiver reports but does not inbox encrypted content (old-firmware observation)**
  - Do: receive an encrypted team post on a node with `team_ch_key=0`.
  - Pass: `ENCRYPTED — no team content key` is reported, relay behavior remains available, and no readable
    `inbox_channel` row is created. This was observed in the supplied 2026-08-02 trace.

Expected but easy to misread:

- On a one-hop co-located team, `NOT HEARD` / `relayed:false` may be truthful even when all members received the post:
  no relay had to rebroadcast it. Use a 2+ hop recipient to test relay confirmation.
- `team_channel_crypt` is live-only and returns to its privacy-safe default after reboot.
- A keyholder with `team_channel_crypt=0` may intentionally send plaintext and emits `channel_crypt_skipped`.

---

## Part 7 — ID-to-hash resolution and TX admission

Part 7 is committed. Current one-command behavior:

1. An unresolved by-ID `reqpubkey` sends a by-ID hash query and reports `dh=0x0`.
2. The owner answers with a claimed ID-to-hash binding.
3. The requester automatically starts the existing by-hash pubkey exchange.
4. The key arrives without a second operator command.

### Rig A — static node with one heard and one routed-only peer

- [x] **7.1 — Directly heard static ID resolves consistently**
  - Do: `hashof <heard-id>`, then `reqpubkey <heard-id>`.
  - Pass: both use the same hash; request reports `queued ... dh=0x<hash> plane=static`.

- **7.2 — RETIRED intermediate-firmware check.** It was marked successful when unresolved ID correctly returned
  `err_no_binding`. S4a intentionally replaced that behavior with 7.22/7.28, so it is not a current pass.

- [x] **7.3 — Explicit team plane on a static node gives useful refusal**
  - Do: `reqpubkey <heard-static-id> -t` while `team_id=0`.
  - Pass: `err_no_binding` names the team plane and suggests removing `-t`.

- [ ] **7.4 — Explicit static flag matches AUTO**
  - Do: `reqpubkey <heard-static-id> -s`.
  - Pass: same resolved hash and static plane as 7.1.

- [ ] **7.5 — Plane flags are mutually exclusive**
  - Do: `reqpubkey <id> -t -s`.
  - Pass: parse error; no frame.

- [ ] **7.6 — Full view includes route-only static IDs honestly**
  - Do: compare `routes` with `peers all`.
  - Pass: an unbound route appears as `[peer] static_id=N` with no confidence suffix; the node's own row is omitted.

- [ ] **7.7 — Bounded peer JSON excludes route-only rows**
  - Do: compare plain `peers` over USB and BLE.
  - Pass: it remains the at-most-16-row key book.

- [ ] **7.11 — Self-query is refused**
  - Do: `reqpubkey 0x<this-node-hash>`.
  - Pass: `err_unsupported` with useful guidance; no frame.

- [x] **7.22 — B43 stage 1: unresolved routed ID is queried**
  - Do: choose an ID in `routes` but without a hash in `peers all`; run `reqpubkey <id>` once.
  - Pass: `queued ... dh=0x0 plane=static`; the binding later appears as `static_id=<id>(claimed)`.
  - Recorded state: the old fast-gate row marked this successful. Full completion remains 7.28.

- [ ] **7.28 — B43/S4b full one-command completion**
  - Do: repeat 7.22 with a different unresolved routed ID, enter it once, and wait. Do not reissue.
  - Pass: the claimed hash appears and the peer key is cached automatically; BLE receives `peer_key_cached`.

- [ ] **7.29 — Unanswered intent times out loudly**
  - Do: with `debug off`, query an unused ID and wait up to about 85 seconds.
  - Pass: `!! reqpubkey <id>: nobody answered ... no pubkey was requested`. The intent budget is about 25 seconds, but
    cleanup runs on the 60-second aging sweep.

- [ ] **7.30 — Pending-intent ring refuses rather than evicts**
  - Do: within about 20 seconds, query five distinct unresolved IDs.
  - Pass: fifth returns `err_resolve_pending_full`; first four remain active.

- [ ] **7.31 — Reissuing one ID refreshes one intent**
  - Do: query the same unresolved ID twice.
  - Pass: both return `queued`; the second consumes no additional slot.

### Rig A-prime — same static node before identity provisioning

- [ ] **7.10 — By-hash request without identity is honest**
  - Do: before `regen`, run `reqpubkey 0x<any-peer-hash>`.
  - Pass: `err_no_identity ... dh=0x... plane=static`, no frame, and no BLE `reqpubkey_sent`.

- [ ] **7.32 — By-ID request without identity is refused before stage 1**
  - Do: before `regen`, run `reqpubkey <unresolved-id>`.
  - Pass: `err_no_identity ... dh=0x0 plane=static`; no by-ID query.

### Rig B — BLE/companion surfaces

- [ ] **7.8 — Resolved by-ID event carries resolved hash**
  - Do: over BLE, request a directly resolved static ID.
  - Pass: `{"ev":"reqpubkey_sent","hash":<resolved-hash>,"plane":"static"}`.

- [ ] **7.13 — BLE refusals do not claim TX admission**
  - Do: repeat 7.10–7.12 over BLE.
  - Pass: `{"ack":"err_..."}` and no `reqpubkey_sent`. A hosted-mobile cache hit is the success exception:
    `queued` plus `peer_key_cached`, without `reqpubkey_sent`.

- [ ] **7.21 — Team-binding confidence is explicit in JSON**
  - Do: request `peers` over BLE on a team node.
  - Pass: every `team_id` has `team_auth:true|false`; neither appears alone.

- [ ] **7.34 — iOS command keeps explicit team flag**
  - Do: run MeshRouteKit tests and capture `Command.reqPubkeyTeam(localID:)` output.
  - Pass: `reqpubkey <id> -t`, not a bare decimal request.

### Rig C — team node with direct and multi-hop teammates

- [ ] **7.20 — Confidence label matches learning source**
  - Do: for a directly heard teammate, run `hashof <id> -t` and `peers all`.
  - Pass: directly heard is `(auth)`; ID-only has no suffix; a by-ID answer may be `(claimed)`.

- [ ] **7.23 — Team-plane B43/S4b completion**
  - Do: choose a multi-hop teammate shown without a hash; run `reqpubkey <team-id> -t` once and wait.
  - Pass: stage 1 reports `dh=0x0 plane=team`, a claimed binding appears, then the key is fetched automatically. The
    claim alone must not authorize sealing; the later authoritative pubkey exchange does.

- [ ] **7.26 — `-t` without membership cannot leak onto static plane**
  - Do: with `team_id=0`, run `reqpubkey <id> -t`.
  - Pass: `err_no_binding ... plane=team`, not-in-team guidance, and no frame.


### Rig D — dual-plane and mobile edge cases

- [ ] **7.9 — Same numeric ID on both planes is ambiguous**
  - Do: arrange the same numeric ID on static and team planes; run bare `reqpubkey <id>`.
  - Pass: `err_ambiguous_plane`, both rows from `hashof`, and no frame. Explicit `-s` and `-t` select their rows even
    when both rows have the same hash.

- [ ] **7.12 — Off-grid mobile cannot originate static-plane query**
  - Do: on an unregistered mobile, request a statically resolved ID with AUTO or `-s`.
  - Pass: `err_no_gateway` and no frame. Control: `reqpubkey <teammate-id> -t` is accepted on the same node.

- [ ] **7.27 — Unresolved AUTO ID is ambiguous on homed team mobile**
  - Do: on a registered team mobile, run bare `reqpubkey <unbound-id>`.
  - Pass: `err_ambiguous_plane` and no frame; explicit `-s` and `-t` each send on their selected plane.

### Rig E — three-node owner-only rule

- [ ] **7.24 — Only the ID owner answers a by-ID assertion**
  - Do: A knows X's binding first-hand; C does not. From C, query X by ID while watching A with `debug on`.
  - Pass: A forwards the H query and never reports `h_resolved`; X answers. Control: A can answer the by-hash form from
    the same peer-key row.

### Rig F — radio saturation and device-only traces

These are observational because a full hardware queue cannot be forced reliably. Drive several nodes with channel/test
bursts, beacons, and requests. Record an outcome only when the relevant condition occurs.

- [ ] **7.14 — OBSERVE immediate bounded-queue refusal**
  - Pass when observed: `err_tx_queue_full` plus retry guidance. It may refer to the four-slot LBT defer ring or the
    eight-entry DeviceHal queue; it is not `err_ack_ring_full`.

- [ ] **7.15 — OBSERVE DeviceHal rejection is failure**
  - Pass when observed: a request refused by `DeviceHal::tx` returns `err_tx_queue_full` and emits no
    `reqpubkey_sent`.

- [ ] **7.16 — OBSERVE late deferred-request loss is loud**
  - Pass when observed: synchronous `queued` followed by
    `!! deferred TX dropped at the radio queue — a request reported as accepted never aired`.

- [ ] **7.18 — OBSERVE rejected beacon retains dirty digest**
  - Do: with `debug on`, create a dirty channel entry and saturate TX as a beacon becomes due.
  - Pass when observed: rejected beacon does not fire `channel_dirty_cleared`; a later accepted beacon advertises it.

- [ ] **7.18b — OBSERVE deferred beacon retires digest only on admission**
  - Pass when observed: entering the LBT defer ring does not retire the digest. DeviceHal rejection keeps it dirty;
    DeviceHal acceptance may retire it.

- [ ] **7.19 — OBSERVE late-loss line bypasses `debug off`**
  - Do: repeat the late-loss setup with `debug off`.
  - Pass when observed: the `!! deferred TX dropped ...` line still prints.

- [ ] **7.25 — Device trace names by-ID query correctly**
  - Do: with `debug on`, watch a by-ID H query pass through.
  - Pass: trace contains `BY_ID id=<N> ... HARD`, not `hash=<N>`.

## Part 8 — Heltec V3 OLED panel bring-up (slices UI-5 through UI-7)

Rig: **one Heltec WiFi LoRa 32 V3**, `pio run -e heltec_v3 -t upload` (or `heltec_mobile`). Nothing else needs to be on
air. These are here because **no native test and no simulator can reach the panel, the button or the ADC** (rule M2);
the automated side of UI-5 is in `simulation/BASELINE.md` §UI-5.

⚠ **UI-5 wires only the boot frame.** `set_power_save`, `button_pressed` and `battery_sample_mv` are compiled but
**garbage-collected out of the UI-5 image** (register B88) — 8.4/8.5/8.6 below are therefore **Task 6 / Task 9** checks
and cannot pass on a UI-5 build. Do not mark them from this firmware.
★★ **UI-6 (2026-08-05) CLOSES B88 and RETIRES 8.1.** All nine canvas entry points are now called by
`src/firmware_ui.cpp`, so nothing is collected — **8.4 / 8.5 / 8.7 are live from a Task-6 build**, and **8.8–8.10 are
new.** 8.1's static splash **no longer exists**: the boot frame was UI-5's `--gc-sections` anchor and the feature layer
replaced it with the real page-chunked render. ⛔ Do not mark 8.1 from a UI-6 build.
★★ **UI-7 (2026-08-05) RETIRES 8.9 and makes 8.17–8.22 live.** The send path is real. Run the detailed H7-01…H7-09
procedures in `docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md`; 8.17–8.22 are their compact acceptance residue.
**8.6 remains Task-9-only.**

- [ ] ~~**8.1 — The panel lights, and shows exactly this**~~ ⛔ **RETIRED BY UI-6 — the splash is deleted.** On a
      Task-6 build the first thing on the panel is the **live STATUS screen** (see 8.8). Kept for the audit trail:
  - Do: flash, then watch the panel from power-on. The frame is painted once, at the end of `setup()`, right before the
    console prints `  node      = up. Type 'help' for commands.`
  - Pass: two lines of text with a horizontal rule between them —
    - large (10×20): `MeshRoute`
    - a full-width rule under it
    - small (6×10): `OLED UI-5 ok`
  - The frame is **static** and stays until power-off (UI-5 has no tick-side render; that is Task 6).
  - ⛔ If the panel is **blank/dark**, do NOT start with the driver. Work the two board facts below in this order —
    both are one-line edits in `variants/heltec_v3/board_ui.cpp`, and both are unresolved *hardware* questions, not code
    defects (registers B90, B91; spec §14 Q1).

- [ ] **8.2 — Vext polarity (register B90) — the FIRST suspect on a dark panel**
  - Background: nothing in this tree drove GPIO 36 before UI-5. UI-5 drives it **LOW**, the level MeshCore's working V3
    port leaves it at. Whether the panel is on that rail is *not* established by that port.
  - Do: if 8.1 is dark, change `kVextOnLevel` from `LOW` to `HIGH`, reflash, repeat 8.1.
  - Pass: record **which level lights the panel**. That answer closes B90; leave the winning level in place.

- [ ] **8.3 — Panel reset pin 21 (spec §14 Q1) — the SECOND suspect**
  - Background: 21 comes from MeshCore's `SSD1306Display.h` `PIN_OLED_RESET` default and from our own pre-A0 seam note;
    MeshCore's V3 *variant* defines none, which is what left the question open.
  - Do: only if 8.1 is still dark after 8.2, change `kOledRst` to `U8X8_PIN_NONE` (255) and reflash.
  - Pass: record whether the panel needs the explicit reset. Either answer closes §14 Q1.
  - ✅ **NO LONGER SILENT — UI-6 closed B91.** `mrui::board_init()` returns a bool from a real I²C address probe, and
    `mr_ui_init()` prints the line in 8.8 when nothing ACKs. On a UI-6 build that line is the first instrument to read:
    **line present ⇒ the panel is not answering at all** (rail / address / wiring), **line absent but panel dark ⇒ the
    panel ACKs and the fault is downstream** (reset, contrast, render). That split is exactly what 8.2/8.3 lacked.

- [ ] **8.4 — TASK 6: blanking produces no repeated bus traffic**
  - Do: on a Task-6 build, leave the node untouched past `MR_UI_BLANK_MS` and watch the I²C lines (scope or logic
    analyser on SDA 17 / SCL 18).
  - Pass: **one** short burst at the blank transition, then **silence** — not a repeating ~1 KB frame.
  - This is the metal half of the edge-triggered-blanking probe; the host half is §UI-5's control C1.

- [ ] **8.5 — TASK 6: user button on GPIO 0**
  - Do: on a Task-6 build, press the user button.
  - Pass: the UI reacts (pressed reads **LOW**, `INPUT_PULLUP`).
  - ⚠ **GPIO 0 is the ESP32-S3 boot strap.** Separately confirm that holding the button **across a reset** enters
    serial-download mode — expected hardware behaviour (spec §10.1), to be documented for users, not fixed in firmware.

- [ ] **8.6 — TASK 9: battery reading against a multimeter**
  - Do: on a Task-9 build, compare the reported millivolts with a meter on the cell.
  - Pass: within ~50 mV. A consistent *ratio* error means this board revision's divider differs — record the measured
    value, do not tune the constant to taste.
  - Until Task 9, `battery_sample_mv()` returns `-1` and the panel must render `--`, never a number.

- [ ] **8.7 — Paint versus radio (spec §5 rule 1; the check most likely to fail)**
  - Do: on a Task-6 build, run a DM load while cycling screens continuously.
  - Pass: no CTS-timeout regression versus the same load with the panel idle.
  - Rationale, so the threshold is not guessed: a **full** frame is ~25 ms of blocking I²C at 400 kHz against
    `cts_to_data_gap_ms = 5`. UI-5 links the **128 B page-buffer** mode (`nm firmware.elf | grep 'buf\$'` reads `128`,
    not `1024`), so one page is ~3 ms — inside the RX window slop, but that is spec §14 Q4's open assumption and this
    is the check that tests it.

### UI-6 additions (2026-08-05) — the three metal-only behaviours the feature layer adds

- [ ] **8.8 — TASK 6 / §B91: the panel-ACK line, and it must be ABSENT on a working board**
  - Do: on a Task-6 build, watch the console from power-on.
  - Pass on a **healthy** panel: the line below **does not appear**, and the panel shows the live STATUS screen —
    status bar `DM0 CH0 T0/0 --` (a fresh node: no mail, no teammates heard, no battery reader until Task 9), then
    `STATUS` / `me T<team_local_id>  team <8-hex team_id>` / `DM 0, newest --` / `CH 0, newest --` / `batt --`.
  - Pass on a **dead** panel: exactly this line, **once**, at boot —
    `!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)`
    — and the node **keeps meshing** (the report is not fatal; confirm beacons/DMs still work).
  - ★ POSITIVE CONTROL, so the absence above is evidence and not just silence: unplug the panel's SDA (17) or SCL (18),
    reflash/reboot, and confirm the line **does** appear. An absence you have never made appear proves nothing.
  - ⛔ Fail if the line prints on a panel that is visibly rendering — that means the probe address is wrong, not the panel.

- [ ] ~~**8.9 — TASK 6: the send path is NOT BUILT**~~ ⛔ **RETIRED BY UI-7.** The loud-refusal stub and
  `!! UI send path not built` line no longer exist. On the current tree, a send must reach the real executor; use
  **8.17–8.22 / H7-01…H7-09**. Seeing the old line means the wrong firmware was flashed, not that this check passed.

- [ ] **8.10 — TASK 6 / §B71: the emergency screen's exit**
  - Do: drive the real alarm to a retained result (`PICKED UP`, `NOT HEARD`, `REPLY`, or a genuine `FAILED` refusal),
    wait until the result has been fully drawn, then press **short** once.
  - Pass: the alarm screen clears and the normal cycle resumes (the next short press advances STATUS → TEAM → …).
  - Do: long-press again to reach `SENDING...`, and press **short** while it is showing.
  - Pass: **nothing happens** to the alarm screen — an outcome the user has not seen is sticky. (The screen underneath
    may advance; the overlay must not clear.)
  - Do: let the panel blank on a `FAILED`/`NOT HEARD` screen (past `MR_UI_BLANK_MS`, then past `kEmgHoldMs`), then press
    short **twice**.
  - Pass: the **first** press only wakes the panel and the outcome is **still displayed**; the **second** clears it.
    ★ That consumed-waking-press is the whole reason a short press is safe here.
  - Do: long-press from the sticky screen.
  - Pass: it re-fires (a fresh three-transmission budget), from any screen and from a blanked panel.

## Part 9 — console response line integrity (§B95)

Rig: **one node with a USB serial monitor**, under normal radio traffic. These are here because **no automated gate can
reach the bytes that leave the UART** (rule M2): `src/` is outside the native build and the simulator compiles only
`lib/core`. The host half is `tools/probe_console_sink/` (52 checks + 13 negative controls) and
`simulation/BASELINE.md` §B95.

⚠ **Prefer `heltec_v3` — it is the board that produced the defect** (its `Serial` is UART0 with a **128-byte** hardware
TX FIFO, the tightest transport we ship). ⛔ **Blocked by B96 today**: no Heltec env builds on the Linux host. Until that
is fixed, run these on `xiao_sx1262` (256-B CDC FIFO) and note the board — the guarantee is the same, the pressure is
lower, so a `heltec_v3` rerun is still owed.

- [ ] **9.1 — `cfg` twenty times: every row structurally complete**
  - Do: `cfg`, twenty times, while the node is beaconing.
  - Pass: **every** received line is a whole row. Specifically **NOT** the H5-06 shapes:
    - ⛔ `  proto : duty=1.00% beacon_ms=900000168010102layer=5 leaf=5000` (labels dropped, values fused)
    - ⛔ `1Laye0000[route] dest=5 …` (a previous response's residue prefixed to the next)
  - Pass shape (values will differ; the STRUCTURE is the check — every `key=` present, one row per line):
    - `  proto : duty=1.00% beacon_ms=900000 hop_cap=16 team_hop_cap=8 lbt=1 nav=1 intra_relay=0 host_mobiles=1 nav_ignore=0`
  - ★ A row may be **absent**; it may never be **wrong**. An absence must be accompanied by 9.4's drop line.

- [ ] **9.2 — `routes` twenty times with a gateway schedule present**
  - Do: `routes`, twenty times, on a node that has learned a gateway.
  - Pass: the `[route]   gw_sched period=…ms heard_ms=…` line and each `[route] dest=…` line are separate, complete
    lines. ⛔ Never `heard_ms=39214608526@]5@0125015-20[route] dest=5` (the H5-06 fusion).

- [ ] **9.3 — `help` five times: no line without a line ending**
  - Do: `help`, five times.
  - Pass: every received help line ends with CRLF and **nothing follows it on the same physical line** — in particular
    the next prompt/response never starts mid-line. (The old `hl()` emitted its CRLF only when two FIFO bytes happened
    to be free; H5-06 recorded exactly that.)
  - ⓘ **EXPECTED, NOT A DEFECT: `help` is 6121 B / 75 lines and does not fit the 2048-B console stage.** It delivers
    roughly the first 25 lines and then reports the loss (9.4). What must never happen is a *garbled* or *unterminated*
    line. If the whole text is wanted at the bench, raise `MR_CONSOLE_STAGE_BYTES` (see `src/console_sink.h`).

- [ ] **9.4 — the deferred drop report, verbatim**
  - Do: run `help` (which necessarily overflows the stage). Watch for the report after the delivered lines.
  - Pass: exactly this line, on its own line, **once** per burst of loss:
    ```
    !! CONSOLE_DROP lines=51
    ```
    (the count varies; the text does not — `!! CONSOLE_DROP lines=<N>`, CRLF-terminated). It must appear **after** the
    lines it refers to, never inside one, and must not repeat while nothing further is lost.

- [ ] **9.5 — stop the host reading, then resume (the anti-wedge)**
  - Do: with the monitor attached, suspend the reader (`Ctrl-S` in a terminal that honours it, or pause/detach the
    monitor process — do **not** unplug), issue `cfg` a few times, then resume.
  - Pass: **the node keeps working throughout** — beacons continue, a DM still ACKs, no reset, no watchdog, no wedge.
    After resuming, output continues with **complete lines only** plus a `!! CONSOLE_DROP lines=<N>` for what was lost.
  - ⛔ Fail if the console freezes the radio (missed beacons/ACK timeouts correlated with the pause), or if a partial
    row appears after the resume.

- [ ] **9.6 — boot banner intact**
  - Do: reset the board with the monitor already attached; capture the boot log.
  - Pass: every banner line complete, ending with `  node      = up. Type 'help' for commands.` Measured to be ~1.3 KB,
    i.e. inside the stage, so **no banner line should be missing at all**.

- [ ] **9.7 — `reboot` still prints before it resets**
  - Do: `reboot`.
  - Pass: `> rebooting` is received **before** the reset. (This is the one deliberately blocking path,
    `GuardedConsole::flush()`; if the line is missing, the bounded drain is not working.)

- [ ] **9.8 — BLE `help` is refused, not streamed**
  - Do: from the companion/BLE console, send `help`.
  - Pass: exactly one JSON line — `{"err":"help","msg":"console_only"}` — and **no** multi-kilobyte help stream over
    NUS. (Before this fix a BLE `help` printed the text to *USB* instead; now help honours its sink, so the refusal is
    what keeps 6 KB off a link that has wedged this node before.)

- [ ] **9.9 — the `cfg` SF list appears in the response, not on another transport**
  - Do: `cfg` over USB; then `rcmd <id> cfg`-style / companion `cfg` if reachable.
  - Pass: `sf_list=6,7` (whatever the real list) is inside the `radio :` row of the response itself. ⛔ Fail if the SF
    list appears on the USB console while missing from a captured/remote response — that was the
    `print_sf_list(bitmap)` global-sink bypass.

## Current semantics and known gaps — not checklist items

- **Transmitter-admitted is the synchronous boundary.** `queued` / `reqpubkey_sent` means accepted by the current TX
  path, not proven RF airtime or end-to-end delivery. Observable deferred loss uses the `!!` operator line.
- **7.17 / B50 — KNOWN GAP:** `DeviceHal::_txq_drops` has no general console counter. Some non-request hardware TX drops remain invisible.
- **B52:** companion JSON lacks confidence for `static_id`; `team_auth` covers only team bindings.
- **7.33 / B55/B56 — KNOWN GAP:** `reqpubkey_sent.hash == 0` means the by-ID query was accepted. Stage-2 failure or timeout has no dedicated
  BLE push; watch USB for the bounded `!!` timeout.
- Claimed-store upgrade/demotion/eviction is covered natively; there is no useful two-node sequence for S2b rehome.
- Multi-hop `team grantkey` and sealed send are not unlocked by a claimed binding. They require an authoritative
  binding; that trust boundary is intentional.

### 8.11 — §B103 the distress REPLY is TEAM-scoped (2026-08-05, was a LIVE defect)

Needs a second node that is **not** in the panel node's team. Nothing automated can reach this: the reply scope is
decided in `src/firmware_ui.cpp`, which neither the native suite nor the simulator compiles.

1. Panel node (team set): fire an alarm, leave it on a retained outcome.
2. Stranger node, `team_id == 0`: `send_channel 0 "hello"`.
3. Expected on the panel: the status bar `CH` count **increments**, and the emergency overlay is **unchanged**.
   ⛔ `REPLY` appearing here is a regression of §B103 and invalidates every reply indication on the build.
4. Teammate node, same `team_id`: `send_channel -t 0 "on my way"` ⇒ the panel **does** show `REPLY <name>: on my way`.

### 8.12 — §B102 an unread outcome cannot be dismissed (2026-08-05)

The panel is the only instrument; there is no console line.

1. Put the node under load so the MAC-idle gate stretches the repaint (`send` something a beat earlier).
2. Drive an alarm to a terminal outcome and press the button **immediately**.
3. Expected: the outcome **stays on the panel**. Only a press after it is fully drawn clears it.

### 8.13 — §B101 an alarm leaves no armed compose modal (2026-08-05)

1. On TEAM, double-press to open the DM compose list, short-press once so a **real message** is highlighted.
2. Long-press to fire the alarm; let it reach a retained outcome; short-press to acknowledge.
3. Expected: the plain TEAM screen, **no list, no highlight**. Then double-press: the list re-opens at
   `back, don't send`. ⛔ A message flying at step 3 is the §B101 mis-send.

### 8.14 — §B108 round 2: the unread cap is a DISPLAY limit, and only the panel can say so (2026-08-05)

The arithmetic is fully native-gated (`ui-frame: B108 round 2 — a mid-frame arrival survives AT the unread cap`). What
no gate reaches is the **status bar itself**: the arrival serial is now uncapped and the clamp lives in exactly one
place, `UiInboxCounters::publish`. If a future edit publishes the raw serial instead, every native case still passes
and the bar silently grows a fourth digit that the 128-px line has no room for.

1. From a second node, post more than `999` channel messages without visiting INBOX on the panel node (a scripted
   burst; nothing else reaches four digits).
2. Expected status bar, exactly — `CH999`, never `CH1000` and never a rolled-over small number:
   `DM0 CH999 T0/0 --`
3. Then walk to INBOX and let one full frame draw. Expected: `CH0`, and the bar layout is unchanged throughout.

⛔ A four-digit count, a truncated `T<n>/<n>` field, or a count that *drops* while the burst is still arriving is a
regression of this slice. ⓘ If the burst is not practical on the day, say so in the completion record rather than
ticking it — the behavioural half is natively covered; only the **rendering** half is owed here.

### 8.15 — §R1/B109 a REPLY lights a DARK panel; a stranger's post does not (2026-08-05, OWNER-RULED)

Nothing automated reaches this. The model half is natively gated (and W6 pins the `FrameStep` → `set_power_save`
mapping structurally), but **"the screen came on by itself"** has exactly one instrument: the panel.

1. Panel node: fire an alarm, let it settle on a retained outcome, then **leave the button alone** until the panel is
   fully dark (`kEmgHoldMs` + the blank timer).
2. Teammate node, same `team_id`: `send_channel -t 0 "on my way"`.
3. Expected, with **no button press**: the panel lights showing — exactly —
   `REPLY` on the large line and `<name>: on my way` beneath it.
   It must light **once**: any flicker or strobe means a per-tick `set_power_save` write, not an edge.
4. Leave it: `kEmgHoldMs` after the **reply's own arrival** it blanks again, `REPLY` retained.
5. ⛔ **The negative half.** Repeat from a stranger node (`team_id == 0`): `send_channel 0 "hello"`. The panel **stays
   dark**. A panel that lights for a passer-by means the wake was wired to the arrival instead of to the reply — the
   §2.1 false-confirmation class in power form, and a battery-drain vector on a rescue device.

ⓘ Deliberate and not a bug: a `BLOCKED` / `PICKED UP` / `NOT HEARD` / `FAILED` outcome arriving at a dark panel does
**not** light it. R1 rules on the REPLY only; widening it is an open owner question.

### 8.16 — §R2/B110 a DOUBLE under the emergency overlay does nothing at all (2026-08-05, OWNER-RULED)

The overlay covers the body, so the whole hazard is invisible by construction — a second node is the only way to see
the half that leaves the device.

1. On TEAM (at least one teammate listed), long-press to fire an alarm.
2. Double-press **twice**, about a second apart.
3. Expected: the overlay is unchanged, and a second node sees **nothing arrive on channel 0**.
   ⛔ A message arriving is the hidden mis-send: two doubles used to open an invisible compose list and send from it.
4. The one-press variant: on TEAM double-press to open the DM list, short-press onto a **real** message, then long-press
   to **ARM** and release before it fires (`ARMING` keeps the modal open by design). While `RELEASE!` is up,
   double-press once. ⛔ A DM leaving the node here is the same defect reached by a single press.
5. The rest of the contract, unchanged: **long** re-fires from a sticky outcome, **short** exits once the result has
   been drawn (§B71/§B102). A double must never dismiss — even a fully presented outcome.

### 8.17 — UI-7 the send path is REAL: the composed line is what the radio gets (2026-08-05)

The composed COMMAND is asserted byte-for-byte by the native suite; what only metal can answer is whether it reached
the radio and what the other node received.

1. Node A (team mobile, teammate B visible on TEAM): `short` to SEND, `double`, `double` on `Got your message`.
2. Expected on A's USB console: **one** send, answered `ack:queued ctr=<n>` — not two, not zero.
3. Expected on B: the channel message with body **`Got your message`**.
4. Expected on A's panel: `to: team ch 0` header, then `SENT, waiting`, bottom line `press = back`.
5. Repeat for the DM: TEAM -> teammate -> `double` -> `double` on `Are you OK?`.
   Expected console line executed: `send <id> "Are you OK?" -t -a` (⚠ **no `-e`** — the parser rejects it on an id
   target). Expected panel end state: **`DELIVERED to <label>`**.

### 8.18 — §4.1 the alarm carries `-l` ONLY with a fix, and goes out either way (2026-08-05)

★ **The only instrument is the console**, because the difference is one flag on a line the panel never shows.
`Node::on_command` REFUSES a located post outright when both coordinates are zero, so an unconditional `-l` would turn
"no fix" into **no alarm at all**.

1. On a node with `cfg set lat_e7 0` and `cfg set lon_e7 0`, long-press to fire.
2. Expected: the executed line is `send_channel 0 "I'm in danger" -t -e` — **no `-l`** — and it is answered
   `ack:queued`. ⛔ An `ack:err_unsupported` here means the flag went out unconditionally: STOP and report.
3. Set a real position, fire again. Expected: `send_channel 0 "I'm in danger" -t -l -e`, also `queued`.
4. Both runs: a second node in the team receives the alarm body.

### 8.19 — §B69 an unconfirmed send must never read as SENT (2026-08-05) ★★ SAFETY

Bench guide **H7-07** carries the full procedure. The one-line residue for this script: on a `ctr == 0` outcome the
panel must read **`NOT CONFIRMED` / `no send handle`** (canned) or **`NOT HEARD` / `unconfirmed x3`** (alarm).
⛔ **`SENT` or `SENT, no relay` in that state is a false confirmation — stop and report.** Hard to provoke on demand;
record it opportunistically. The most reachable trigger is a node whose team channel key was removed after `create`.

### 8.20 — UI-7 an unconfirmed DM must not brick the send path (2026-08-05)

★ **No automated gate can reach this**: the slot gate lives in `mr_ui_tick`, in a TU neither the native suite nor the
simulator compiles. The harm is *every later send silently never happening*, which looks like a dead radio.

1. Node A sends a DM to a teammate that is POWERED OFF, from the panel.
2. Wait for the panel to reach **`NO CONFIRM`** (the e2e-ack deadline; up to a minute).
3. Let the sub-view close, or press to acknowledge it.
4. Now send a canned channel post from the panel.
5. Expected: it goes out — A's console shows the `send_channel` line and a second node receives it.
   ⛔ **Nothing on the console is the defect**: the unconfirmed DM held the single normal slot and every later send was
   never issued at all. Silent, and indistinguishable from a radio fault without this check.

### 8.21 — §B64 a teammate that LEFT the roster must never inherit your DM (2026-08-05, OWNER-RULED) ★★ SAFETY

★ **Why it is here and not only in the native suite:** the model's refusal IS natively gated, but the *panel* half is
not — `src/firmware_ui.cpp` is compiled by neither the native suite nor the simulator. And the panel half is the safety
property: a `>` marker left beside whatever now occupies that row would name a target the model has already refused.

1. Three same-team nodes in range; on A, `short` to **TEAM** and put the `>` on the **LAST** teammate.
2. Power that teammate down; wait for it to leave the roster (`T<shown>/<total>` in the bar falls).
3. Expected panel: **no `>` marker anywhere**, and the last body line reads exactly **`TEAMMATE GONE, repick`**.
4. Press `double`. Expected: **no compose sub-view opens**, and **A's console shows NOTHING sent.**
   ⛔ A `send <id> "Are you OK?" -t -a` here is the B64 mis-send live — the DM went to a teammate the user never
   highlighted. **STOP and report.**
5. Press `short`: the marker reappears on another teammate and the message clears. `double`, `double` → the DM goes out,
   and the console id matches the row now marked.
6. **The reorder half:** put the `>` on the MIDDLE teammate and let the roster re-sort (scores/ages move, or bounce a
   different teammate). The marker must stay on **that teammate** even as it changes ROW, and the DM must carry its id.
   ⚠ A marker that stays on a fixed row while the names shift under it means identity tracking regressed to indexing.

Full procedure with every expected line: bench guide **H7-09**.

### 8.22 — §B113 an accepted canned post must reach `SENT, waiting` (2026-08-05)

★ The state transition is natively gated; what only the panel can answer is whether the string is drawn. It is a
one-line residue of bench guide **H7-01**.

1. On A, `short` to SEND, `double`, `double` on `Got your message`.
2. Expected panel: `SENDING...` for one frame, then **`SENT, waiting`**, then (within ~36 s) `PICKED UP` or
   `SENT, no relay`.
   ⛔ **Sitting on `SENDING...` all the way to the settle or the 15 s auto-exit is the B113 regression** — the accepted
   post's acceptance never moved the channel state. The DM twin (`SENT, waiting` in 8.17 step 5) always worked; only the
   channel arm was missing.

## Completion record

- Firmware revision tested: `________________`
- Boards / node labels: `________________`
- Date and tester: `________________`
- Failed or skipped checks: `________________`
- Log/archive location: `________________`
