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

- [ ] Other observed defects - cfg set freq does not survive reboot


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

- [x] **6.6 — Empty encrypted post without location is refused**
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

- On a one-hop co-located team, `NOT RELAYED` / `relayed:false` may be truthful even when all members received the post:
  no relay had to rebroadcast it. Use a 2+ hop recipient to test relay confirmation. ⚠ **It is acceptable ONLY WHEN NO
  REPLY WAS RECEIVED** — the earlier unconditional wording masked [[B114]], a bench run where the team received all
  three distress posts *and answered* while the panel reported no relay. A **channel** reply must lift the panel to
  `REPLY` (a failure to do so is a live defect); a **DM** reply legitimately does not (owner-ruled 2026-08-05) and
  belongs on B116, not ticked off here.
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
★★ **§B115/§B117 (2026-08-05) ADD 8.23 and 8.24, and both are FIRST-READING checks.** 8.23 reads the alarm's attempt
counter on the **first** post (the shipped defect was invisible on the last two); 8.24 pins the owner-ruled terminal
headline. ⚠ **RE-RULED 2026-08-05: the headline is `NOT RELAYED`.** Every earlier line in this script that quoted
`NOT HEARD` — and then, briefly, the never-approved `NO RELAY` — as expected panel text has been moved to `NOT RELAYED`
in the same slice; a stale quote would fail H7 on correct firmware.
★★ **TASK 8 (2026-08-06) ADDS 8.25 / 8.26 / 8.27 and closes the last three gaps against the owner's nine validation
cases.** Task 8 needed **no new firmware** — its Step-1 render landed with Tasks 1–7 and the §B115/§B117 slices — so
the whole of Task 8 is this bench matrix. **The nine cases map: 1 → 8.23 · 2 → 8.23 · 3 → 8.24 · 4 → 8.25 · 5 → 8.27
(fire from dark) + 8.15 (reply wakes) · 6 → 8.26 · 7 → 8.18 · 8 → 8.10 · 9 → 8.4.** The detailed procedures are
`docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md` **H8-01…H8-10**; the 8.x lines are their compact acceptance residue.

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
  - ⛔ **CORRECTED IN PLACE 2026-08-06:** this entry used to end *"Until Task 9, `battery_sample_mv()` returns `-1`…"*.
    **Task 9 has landed** — the reader is real (`variants/heltec_v3/board_ui.cpp`), so that sentence is withdrawn.
  - Do: battery-powered, **USB detached**, compare the panel reading with a meter on the cell.
  - Pass: `panel <= meter` and `meter - panel < 0.150 V`. ⓘ The window is one-sided because `fmt_volts` **truncates**
    to one decimal (up to −99 mV by construction) on top of the ±50 mV analogue budget. A panel reading **above** the
    meter cannot come from truncation and means the ratio is wrong.
  - Fail: a **constant ratio** error, holding at **both** voltage points ⇒ **`kVbatAdcScale`** is wrong for this board —
    record the measured ratio at both points, **do not** tune the constant from one voltage point.
  - Fail: an error that **varies with voltage**, or a fixed mV **offset** ⇒ **ADC calibration**, not the divider.
  - ⛔ **CORRECTED 2026-08-06 ([[B126]]):** the constant was called `kVbatDivider` and read as a resistor ratio. It is
    not — the physical network is **390 kΩ / 100 kΩ = 4.9** and the shipped value is **4.9 × ≈1.106**, the extra being an
    empirical ADC full-scale correction. ⇒ **do not report a proportional error as "resistor tolerance"** without the
    second voltage point and the ADC-node reading (guide H9-05 part A).
  - ⇒ Full procedure, both failure shapes and the second voltage point: guide **H9-02**.

- [ ] **8.28 — TASK 9: the divider must not be CONDUCTING between samples** ★★ SAFETY / BATTERY LIFE
  - ★ Why: `MR_UI_ADC_CTRL` gates the divider. Heltec inverted it past rev 3.2, so the firmware **probes** the polarity
    — and ⛔ **nothing in this tree or the vendor port establishes that the line has a defined idle level.** A wrong
    park does not show a wrong voltage; it leaves the divider **on for ever** ([[B90]]'s Vext problem restated).
  - ⛔ **REWRITTEN 2026-08-06 ([[B123]] round 2): the previous form could not fail.** It asked the tester to read
    `s_adc_active_high` — a file-static in `variants/heltec_v3/board_ui.cpp`, **not reachable from the bench** — leaving
    only *"the line toggles"*, which a divider parked ON also satisfies.
  - Do: battery-powered and idle, meter in DC volts on the **ADC input** (`MR_UI_VBAT_READ`) to ground, watched
    **between** samples; record `MR_UI_ADC_CTRL`'s level and the **board revision** alongside.
  - Pass: the ADC node reads **~0 V between samples** and rises to roughly **VBAT ÷ 4.9** only during the burst, once
    per **`kBattPeriodMs`**.
  - Fail: the ADC node sits at **VBAT ÷ 4.9 continuously** ⇒ the divider is permanently enabled. **Stop and report** —
    a permanent drain, invisible on the panel.
  - ⇒ Plus the power-off resistance check that says whether the idle level exists at all: guide **H9-05 part B**.

- [ ] **8.31 — TASK 9: when the reader REFUSES, the FAIL-SAFE PARK must still leave the divider off** ★★ SAFETY
  - ★ Why: on a floating control line there is no detected polarity, so the park comes from
    **`kAdcCtrlFailsafePark`** — documented-inactive for **V3.2 and later only**, and ⛔ **not claimed safe on a pre-3.2
    board.** The shipped code parked the V3.2 *measuring* level on exactly this path ([[B123]] round 2).
  - Do: run this **whenever the panel shows a permanent `--`**. Meter in DC volts on `MR_UI_VBAT_READ` to ground for at
    least two **`kBattPeriodMs`**; record `MR_UI_ADC_CTRL`'s level and the board revision.
  - Pass: the ADC node reads **~0 V and never moves** — no burst at all, because a refusal takes no conversion.
  - Fail: the ADC node sits at **VBAT ÷ 4.9** ⇒ the refusal path parked the divider ENABLED. **Stop and report** with the
    revision — the fail-safe constant is wrong for this board.
  - Fail: periodic bursts while the panel says `--` ⇒ the refusal is not honoured (host cover: `probe_board_ui` P8w, so
    a red here means the flashed build and the probed source have diverged).
  - ⇒ Full procedure and the residual this does NOT close: guide **H9-05 part C**.

- [ ] **8.29 — TASK 9: an unmeasurable battery renders `--`, never a number**
  - Do: watch the status bar from boot, before the first successful sample.
  - Pass: the bar's last field is exactly `--`; the STATUS body reads exactly `batt --`; **no** text anywhere matches
    `<digit>.<digit>V`; no percentage anywhere.
  - Fail: `0.0V` / `batt 0mV` (the plausibility window **`kBattMinMv`**/**`kBattMaxMv`** is not being applied, or a
    raw-0 read is rendered) · a plausible voltage appearing instantly at boot (a fabricated default) · a percentage
    (ruled out — plan Task 9 Step 3, spec §3.3).
  - ⓘ A `--` **with a healthy cell on the meter** is most likely the polarity refusal, not the scale ⇒ run **8.31**
    (the refusal's fail-safe park) and then 8.28.

- [ ] **8.30 — TASK 9: the ADC burst must not disturb the MAC**
  - Do: sustained DM load while the reader is live; compare CTS timeouts against the same load on a build with the
    reader inert.
  - Pass: no CTS-timeout or delivery regression attributable to the burst; a sample is deferred while the radio is busy
    and appears promptly once it is idle.
  - Fail: a regression that appears **only** with the battery-capable build ⇒ the burst is landing inside an exchange.
    ★ The relevant threshold is `cts_to_data_gap_ms` (config) — read it, do not quote it here.
  - ⓘ The cadence and the MAC-idle gate themselves are host-gated (`tools/probe_firmware_ui/` C6–C8); only the **real
    timing** is metal-only. This entry is the residue, not a re-test of the corpus.

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
  - Do: drive the real alarm to a retained result (`PICKED UP`, `NOT RELAYED`, `REPLY`, or a genuine `FAILED` refusal),
    wait until the result has been fully drawn, then press **short** once.
  - Pass: the alarm screen clears and the normal cycle resumes (the next short press advances STATUS → TEAM → …).
  - Do: long-press again to reach `SENDING...`, and press **short** while it is showing.
  - Pass: **nothing happens** to the alarm screen — an outcome the user has not seen is sticky. (The screen underneath
    may advance; the overlay must not clear.)
  - Do: let the panel blank on a `FAILED`/`NOT RELAYED` screen (past `MR_UI_BLANK_MS`, then past `kEmgHoldMs`), then press
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

ⓘ Deliberate and not a bug: a `BLOCKED` / `PICKED UP` / `NOT RELAYED` / `FAILED` outcome arriving at a dark panel does
**not** light it. R1 rules on the REPLY only; widening it is an open owner question.

⚠ **PROVISIONAL — step 3 is not validation of the reply PATH.** *That the panel wakes* is owner-ruled and is a real
pass/fail. *That the post was a reply* is an **INFERENCE**: any same-team channel post arriving while an alarm is live
qualifies, because nothing on the wire marks a post as an answer. **[[B118]]** — the owner's app-code design, bound to
the original post's `channel_msg_id` — will replace that inference; it is **not built** and its authentication floor is
**unruled**. ⛔ Do not report step 3's pass as *"the reply path is validated"*. Full note: bench guide **H8-03**.

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
panel must read **`NOT CONFIRMED` / `no send handle`** (canned) or **`NOT RELAYED` / `unconfirmed x3`** (alarm).
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

### 8.23 — §B115 the alarm's attempt counter must START at `1 of 3` (2026-08-05) ★★ MEASURED WRONG ON METAL

★★★ **READ THE FIRST NUMBER, NOT THE LAST.** The shipped panel stepped `2 of 3` -> `3 of 3` -> `4 of 3` against exactly
three posts on the wire, and the owner confirmed **`1 of 3` was never displayed** — a uniform `+1`, present from the
first attempt. ⚠ **`2 of 3` and `3 of 3` are individually PLAUSIBLE**, so a check asking *"does it say N of 3?"* passes
on the bug. Only the FIRST reading discriminates.

1. Long-press to fire an alarm and watch the overlay **from the first frame it appears**.
2. Expected detail line, in order, one per accepted transmission: **`attempt 1 of 3`**, then `attempt 2 of 3`, then
   `attempt 3 of 3`. The headline is `SENDING...` throughout.
3. ⛔ **`attempt 2 of 3` on the FIRST post is the B115 regression.** So is anything above `3 of 3`.
4. Cross-check against the console on the same node: the number of distinct `»tx M` ids must equal the highest ordinal
   the panel showed. Three ids and a panel reading `4 of 3` is the exact shipped defect.
5. ⓘ The counter is deliberately **not clamped** — that rawness is the only reason the defect was ever visible. If you
   ever see `4 of 3`, report it; do not treat it as cosmetic.

### 8.24 — §B117 the terminal alarm headline is `NOT RELAYED` (2026-08-05, OWNER-RULED) ★★ SAFETY WORDING

The old headline `NOT HEARD` overstated what was measured. What the node measures is that **no relay transmission was
overheard**; what a user in distress reads is *"nobody received it"*. On the B114 bench run those two readings diverged
and the misleading one was wrong — the team had received all three posts and had replied. **`NOT RELAYED` states exactly
what was measured and implies nothing about receipt.**

1. Drive an alarm to its terminal no-relay result (isolate the relay path, or run on a one-hop team).
2. Expected: headline **`NOT RELAYED`** (11 chars, one column spare in the 12-column large font), detail
   `no relay after 3` (or `unconfirmed x3` on the handle-less path, per 8.19).
3. ⛔ **`NOT HEARD` means pre-ruling firmware was flashed. `NO RELAY` means the intermediate build was flashed** — that
   8-char string was substituted by an implementing slice and **never approved by anyone**; it is superseded, not a
   fallback, and it must not be treated as an acceptable reading.
4. ⛔ A **clipped** headline (`NO RELAY HEAR`) means the first ruled wording `NO RELAY HEARD` was used: 14 chars = 140 px
   in the 10x20 font on a 128 px panel, and it does not fit. Report it — a truncated distress string is worse than the
   old wording. ★ Read the LAST character: `NOT RELAYE` (10 chars shown) would mean the panel is clipping at a narrower
   budget than 12 columns, which W11b cannot see because W11b only counts characters.

### 8.25 — Task 8 case 4: the `BLOCKED` countdown is LIVE and the retry is AUTOMATIC (2026-08-06)

★ **What a host gate already covers, stated so this stays residue and not a re-test of the corpus (M2):** the
`blocked` → armed-retry → `firing` transition is natively gated (`tick_emergency` re-fires from `_retry_armed`; the
deadline comes from the outcome time, §B74), and the strings are structural. **What NO host gate can reach:** that the
digit on the panel actually *decrements*, and that the re-fire happens with **no button press** on real radio timing.

1. `cfg get` on the panel node and record `ch_min_ms` (default **10000**). Every deadline below is read against it.
2. Long-press to fire an alarm; let it be accepted (`SENDING...` appears).
3. **Inside** `ch_min_ms`, long-press again.
4. Expected on the console, one line: `BLOCKED channel reason=min_interval — retry in <N> ms`.
   ⓘ The synchronous answer to the post is still `ack:queued` with **`ctr=0`** — nothing aired. That is the documented
   channel self-gate (`fw_main.cpp:1131`), **not** a second failure to report.
5. Expected on the panel: headline **`BLOCKED`** (large), detail **`retry in Ns`** (small) where `N` **counts down**,
   changing at least once per second of wall time. ⛔ A frozen digit means the countdown is rendered from a stored
   duration instead of the live deadline — report it.
6. Expected next, **with the button untouched**: the panel returns to **`SENDING...`** at approximately the deadline
   the console printed, and the second node receives the alarm body.
7. ⛔ **FAILURE SHAPES, each distinct — record which one you saw:**
   - the panel stays on `BLOCKED` past the deadline and never re-fires ⇒ the retry was never armed;
   - `retry in 0s` sits there indefinitely ⇒ the deadline fired but `tick_emergency` did not re-queue;
   - it only resumes **after** you press the button ⇒ the retry is input-driven, which defeats the whole point on a
     device the user has already put down;
   - the panel jumps straight to a terminal headline (`NOT RELAYED` / `FAILED`) without a second `SENDING...` ⇒ the
     block was treated as an attempt outcome instead of a deferral.
8. **Budget cross-check** (do not assert a fixed ordinal — derive it): per 8.23, the highest `attempt N of 3` the panel
   ever shows must equal the number of **distinct `»tx M` ids** in this node's console for this alarm. A block that
   aired nothing must not have consumed one.

### 8.26 — Task 8 case 6: the emergency PRE-EMPTS an outstanding DM or canned channel send (2026-08-06)

★ **Host coverage:** the model's request slot is separate (`queue()` gives `SendKind::emergency` **its own slot, never
overwritten**) and that is natively gated. **What only metal answers:** whether the alarm reaches `Node::on_command` in
the **same service pass** rather than waiting behind the DM's ack or its deadline — a scheduling property of the real
loop, which neither the native suite nor `probe_firmware_ui` runs.

**Half A — an outstanding acknowledged DM.**
1. From TEAM, compose and send a DM with `-a` (per 8.17) so the panel sits on `SENDING...`/`waiting`.
2. **While it is still awaiting its ack**, long-press past the fire threshold.
3. Expected: the emergency overlay takes the body immediately, and the alarm's `send_channel 0 "I'm in danger" -t …`
   line appears on the console **before** the DM's `ACK`/timeout line — timestamps, not impression.
4. Expected: the DM is **not** duplicated on the receiving node (exactly one copy).
5. Expected after the alarm terminates: a short press (once the outcome has been drawn, §B71) returns to the normal
   cycle — the abandoned DM must not leave the UI stuck.

**Half B — an outstanding canned channel post.**
6. Issue a canned channel post from the panel (8.22), then long-press while it still reads `SENT, waiting`.
7. Expected: the alarm proceeds; the canned post's UI tracking is abandoned.
8. Expected: when the abandoned post's own late outcome arrives, the **emergency state does not move** — no headline
   change, no ordinal change. Cross-check with 8.19's attribution rule.
9. ⛔ **FAILURE SHAPES:** the long press does nothing until the DM resolves ⇒ the alarm is queued behind ordinary
   traffic, the defect this case exists for · a second copy of the DM on the receiver ⇒ the pre-emption re-submitted it
   · the abandoned post's late outcome flipping the alarm to `PICKED UP` / `NOT RELAYED` ⇒ a false confirmation from an
   unrelated send (§2.1), **stop and report**.

### 8.27 — Task 8 case 5: an emergency fires from a FULLY BLANKED panel (2026-08-06)

★ 8.15 covers the other half of case 5 (an incoming **reply** lighting a dark panel). This is the half where the panel
is dark **when the user acts**. Nothing automated reaches it: the wake is a physical `set_power_save` transition.

1. Leave the node untouched until the panel is **fully dark** (no alarm outstanding: just past the blank timer).
2. Long-press past the fire threshold **in the dark**, without a preparatory wake press.
3. Expected: the panel lights and the alarm arms and fires — headline `RELEASE!` with detail `EMERGENCY IN <n>` while
   held, then **`SENDING...`** with `attempt 1 of 3` on release past the threshold (8.23 governs the ordinal).
4. Expected: the safety hold is **not shortened** by having been dark — time it from the serial log, not by feel.
5. Expected on a second same-team node: the alarm body arrives.
6. ⛔ **FAILURE SHAPES:** the first long press only wakes the panel and the alarm needs a **second** press ⇒ the waking
   press was consumed, which spec §5 exempts a long press from — on a rescue device that is a lost alarm · the panel
   stays dark while the console shows the send ⇒ the fire path does not un-blank · the alarm fires on a hold visibly
   shorter than from a lit panel ⇒ the arm timer started before the wake.
7. ⓘ **Deliberate and not a bug, so it is not a failure shape:** the four LOCAL outcomes (`BLOCKED` / `PICKED UP` /
   `NOT RELAYED` / `FAILED`) arriving at a panel that has since gone dark do **not** light it — see 8.15's closing
   note. Widening that is an **open owner item**, not a defect to file from this run.

## Part 10 — §B132 a gateway is never a mobile home (2026-08-06)

★ **What the host gate already covers, stated so this Part stays residue and not a re-test of the corpus (M2):** the
whole eligibility invariant is natively gated — `Node::can_host_mobiles()` and **twelve** test cases (`§B132/1` OFFER,
`/1b` both leaves, `/2` stale-or-forged CLAIM, `/3` the refused-`on_init` state with a live hosted entry, `/4` the
positive control, `/5` the two-host bench topology, `/6` each clause isolated; then **§B132b** — `/1` the staged OFFER
killed by a gateway transition **and** the committed-vs-transmitted distinction itself, `/2` the positive control that
timer 80 does transmit, `/3` the boundary re-check alone, `/4` the `on_init` cleanup alone, `/5` a real gateway
`on_init` clearing the hosted registry), every one of them mutation-verified to go RED when its clause is reverted.
⓵ **§B132b adds NO entry here, deliberately:** it is pure `lib/core`, its five cases assert the **parsed frame on the
wire**, and its two live triggers (`cfg set mobile 1` / `cfg set host_mobiles off` inside the OFFER's **100..1000 ms**
de-storm jitter) are a race no hand-typed procedure can reliably hit — a step that cannot be executed is a check that
cannot fail. The knobs' **refusals** are already covered by 10.1 below.
**What NO host gate can reach:** (a) `src/firmware_config.cpp` is **not in the native
build** (`[env:native]` uses `build_src_filter = +<sim_main.cpp> -<fw_main.cpp>` and never compiles `src/`), so the
console refusal has **no automated coverage at all**; and (b) a real mobile deciding its home is gone and
re-registering elsewhere is radio timing plus NV, which neither native nor the simulator reproduces.

### 10.1 — a GATEWAY refuses `cfg set host_mobiles on` (the only check for an untested source file)

1. On the dual-layer gateway: `cfg get`. Record the role fields — expected `gateway=1` and `n_layers` **2**.
2. Expected on the same dump: **`host_mobiles=0`**.
   ⓘ This is the **derived** value, not a stored one: `on_init` forces it off for any `is_gateway` node. A gateway
   showing `host_mobiles=1` means the force-off did not run — report it, and continue to step 3 anyway.
3. `cfg set host_mobiles on`
4. Expected, one line, and it must be a **refusal**:
   `> cfg err refused (host_mobiles: a GATEWAY is never a mobile home — one radio split across two leaves cannot serve last-mile/presence/liveness continuously. Set `cfg set n_layers 1` + reboot first)`
5. `cfg get` again → **`host_mobiles=0` still**.
6. ⛔ **FAILURE SHAPES, distinct — record which:**
   - `> cfg ok` and `host_mobiles=1` ⇒ the refusal is missing; the knob now lies (the core still refuses, so hosting
     will not actually happen — which is exactly what makes this the dangerous shape: the dump disagrees with reality);
   - `> cfg ok` and `host_mobiles=0` ⇒ accepted-then-ignored: no refusal printed, so the operator has no signal;
   - a refusal naming **`a MOBILE never hosts`** on a gateway ⇒ the two arms of the message are swapped;
   - any `cfg err bad_value` / `bad_args` ⇒ the key stopped parsing; that breaks the opt-out path too, so check 10.2.
7. **The negative control, so this entry cannot pass on a node that refuses everything** — on an ordinary
   **single-layer static** node: `cfg set host_mobiles off` → `> cfg ok`, `cfg get` → `host_mobiles=0`; then
   `cfg set host_mobiles on` → `> cfg ok`, `cfg get` → `host_mobiles=1`. ⛔ If the static node also refuses, the
   condition is testing the wrong thing (a role test that matches every role).

### 10.2 — a mobile whose home WAS the gateway detects the loss and re-homes to a static node ★★ the owner's case

⚠ **Rig:** the gateway from 10.1, an **always-on single-layer static** node on the mobile's leaf, and the mobile
(the Heltec). The static node must show `host_mobiles=1` (10.1 step 7). ★ **Do this on a mobile that is ALREADY
registered to the gateway from the pre-fix firmware** — that is the whole point; a fresh mobile never had the bad
home and would pass trivially.

1. **Before flashing**, on the mobile: record `mobile` (or the registration line) → expected `REGISTERED home=5`
   (the gateway's identity on the mobile's leaf), and on the gateway `routes` → `hosted-mobiles n=1` with the
   mobile's `hash=` and `local_id=`. ★ **Record both verbatim — this is the defect's evidence and the only
   before-state you get.**
2. Flash the gateway **and** the mobile with this firmware. Power both, plus the static node.
3. Expected on the **gateway**, after boot: `routes` → **`hosted-mobiles n=0`** and no `[hosted-mobile]` line, ever.
   ⓘ Two independent things produce this and both are intended: `on_init` clears the registry for an `is_gateway`
   node, and the CLAIM path now refuses to record a new one.
4. Expected on the **mobile**: it detects the home is gone and re-registers. It must end at
   **`REGISTERED home=<static node id>`** — *not* 5. Allow it the home-loss interval before judging; if
   `mobile_autoregister` is off it will not re-home on its own, so check that first (`cfg get`).
5. Expected on the **static node**: `routes` → `hosted-mobiles n=1` carrying the **same `hash=`** recorded in step 1.
   ★ Compare the hash, not the count — a count of 1 could be some other mobile.
6. Confirm the home is actually *serving*: from a third node, send a by-hash DM to the mobile and see it delivered
   **through the static node**. Then repeat it 5 times with ~30 s between sends.
   ★★ **WHY THE REPETITION IS THE MEASUREMENT AND A SINGLE SUCCESS IS NOT:** the pre-fix gateway home worked
   *whenever a send happened to align with its 7.5 s window*. One success proves nothing; **5 of 5** through a
   continuously-present home is the property. ⛔ If any of the 5 misses, the home may still be the gateway (re-check
   step 4) — record the ratio either way.
7. ⛔ **FAILURE SHAPES, distinct — record which:**
   - the mobile still reports `home=5` after the home-loss interval ⇒ it never detected the loss (re-flash check:
     did the MOBILE get the new firmware? the fix is on the HOST side, so a stale mobile is not the cause here);
   - the gateway shows `hosted-mobiles n=1` again at any point ⇒ a CLAIM was recorded; capture the console around it,
     this is the defect reopening and it is the one shape that means the fix failed;
   - the mobile reports `REGISTERED` but with `home=0`/no id ⇒ it lost the home and found no replacement: the static
     node was not audible or has `host_mobiles=0` (10.1 step 7), which is a **rig** fault, not this defect;
   - the mobile oscillates between homes across the 5 sends ⇒ report with both ids; that is home selection, a
     separate concern from eligibility;
   - fewer than 5 of 5 deliveries with a stable static home ⇒ **not this bug** — record it and treat it as a
     separate last-mile finding.

## Part 11 — §3.5 durable single-record inbox delete (UI-7D slice A, 2026-08-06)

★ **What the host gate already covers, so this Part stays residue (M2):** the whole tombstone mechanism is natively
gated — `Inbox::erase(InboxKind, uint32_t)` with nine cases (`§3.5/1` the record leaves `pull` and the survivors keep
their order, `/2` the ORDERING hazard (the marker is physically last in the log and the target is still filtered),
`/3` identity is the pair `(kind, seq)` across the two independent seq spaces, `/4` the delete survives a simulated
reboot and no seq is reused, `/5` the three shapes of `not_found`, `/6` `io_error` deletes nothing, `/7` a disabled
inbox is `io_error`, `/8` the tombstone cap is enforced at the writer, `/9` a delete is not a wipe) and **nine
mutations**, every one measured RED.
**What NO host gate can reach:** (a) real flash — the native cases run against a `std::deque` fake, so *"the marker
reached the medium"* is untested; (b) `src/firmware_inbox.cpp` is **not in the native build** (`[env:native]` never
compiles `src/`), so the `del_msg` verb, its parser and its three-way ack have **no automated coverage at all**.
⚠ **ADDED 2026-08-07 after independent QA rejected slice A (§B133b).** Two blockers were fixed and each brought a
metal-only residue: **11.3b** covers the strict destructive-target parser's *glue* ([[B136]]; the pure predicate is
natively gated by 30 assertions) and **11.5** covers **torn-write recovery on a real QSPI flash across a real
reset** ([[B135]] — pre-existing since 2026-06-12, natively gated by 6 cases against a RAM fake with a **mid-frame**
fault injector, but *"the seal survived actual flash"* is unreachable from any host gate).

⛔⛔ **11.1 MUST BE RUN ON AN nRF52 (QSPI) NODE — `xiao_sx1262`, `gateway` or `production`. RUNNING IT ON A HELTEC /
ESP32 NODE PASSES FOR THE WRONG REASON.** `src/fw_main.cpp:168-179`: only `QSPIFLASH=1` wires the durable
`DeviceInboxStore`; every ESP32 target uses the **volatile** `FixedInboxStore<32>` RAM ring, which loses the *entire*
inbox at reboot. *"The deleted message is gone after a reboot"* is therefore **vacuously true** on a Heltec, and would
prove nothing about the tombstone. 11.4 is the control that catches this being run on the wrong board.

### 11.1 — a delete SURVIVES A REBOOT ★★ the whole point of the slice (nRF52 / QSPI only)

1. On the QSPI node, receive **three** DMs from a peer (any content; note them in order).
2. `pull_inbox 0 0` → three `{"ev":"inbox_dm",…}` lines. **Record the `seq` of the MIDDLE one** — call it `S`.
3. `del_msg dm S`
4. Expected, exactly one line: `{"ack":"del_msg","kind":"dm","seq":S,"result":"erased"}`
5. `pull_inbox 0 0` → **two** lines; neither carries `"seq":S`; the other two are in their original order.
   ⓘ `{"ev":"inbox_end",…}`'s `dm_seq` is the store's high-water and **may be larger than before** — the marker
   consumed a sequence of its own. That is correct: history keeps a hole, sequences are never reused.
6. **Power-cycle the node** (a full reset, not a soft reboot).
7. `pull_inbox 0 0` → still **two** lines, still no `"seq":S`, and `"epoch"` in `inbox_end` is **unchanged from
   step 5** (a one-record delete is not a wipe; a bumped epoch means the store was formatted and this check is void).
8. ⛔ **FAILURE SHAPES, distinct — record which:**
   - `"seq":S` is back after the power cycle ⇒ the marker never reached flash — the `erased` in step 4 was a lie;
   - step 5 already still shows `"seq":S` ⇒ the read filter is not wired (the marker may still be durable);
   - **more than one** record vanished ⇒ the identity resolved to the wrong record; capture the whole dump;
   - `"epoch"` changed at step 7 ⇒ the store was wiped, not edited — this is a **worse** defect than a failed delete
     and must be reported even though `"seq":S` is (trivially) absent.

### 11.2 — a deleted record never reappears under new traffic

1. Continuing from 11.1 step 7: receive **two more** DMs.
2. `pull_inbox 0 0` → four lines, none with `"seq":S`, and none reusing the value `S` as a *new* record's seq.
3. ⛔ Failure: any line carrying `"seq":S` — either the old record resurfaced, or a sequence was reused (both are
   corruption of the companion's dedup identity, not merely a UI annoyance).

### 11.3 — the three outcomes are distinguishable at the console (the whole reason the API is not a bool)

1. `del_msg dm S` **again** → `{"ack":"del_msg","kind":"dm","seq":S,"result":"not_found"}` (already deleted).
2. `del_msg dm 999999` → `…"result":"not_found"` (never existed / evicted). ★ The panel will render both of these as
   `MESSAGE GONE`; neither may say `erased`.
3. `del_msg chan S` → `…"kind":"chan","seq":S,…` and **whatever it reports must match what `pull_inbox` shows for the
   channel block** — the two sequence spaces are independent, so this must NOT delete the DM again.
4. `del_msg banana 1` → `{"ev":"err","code":"del_msg","msg":"kind must be dm|chan"}` — a bad kind is refused loudly,
   never defaulted to `dm`.
5. ⛔ Failure shapes: a bare `ok` / `err` with no `result` field ⇒ the three outcomes collapsed; `"result":"erased"`
   for step 1 or 2 ⇒ success is being reported for a record that is not there, which is what makes the panel's
   `MESSAGE GONE` state unreachable.

### 11.3b — a malformed DELETE TARGET is refused, never acted on ([[B136]], added 2026-08-07)

`src/firmware_inbox.cpp` is outside the native build, so the **glue** that calls the parser is metal-only (the
parser itself is natively gated by 30 assertions). On any node, with at least the DM whose seq is `1` present:

1. `del_msg dm 1oops` → `{"ev":"err","code":"del_msg","msg":"seq must be one unsigned decimal number"}`
2. `del_msg dm 1 extra` → the **same** error line.
3. `del_msg dm +1` → the same error line.
4. `pull_inbox 0 0` → the record with `"seq":1` is **still there** after all three.
5. `del_msg dm 1` → `…"result":"erased"` — the strict parser did not break the legitimate form.
6. ⛔ **FAILURE SHAPE, and it is the whole point of this check:** any of steps 1–3 answering
   `{"ack":"del_msg",…,"result":"erased"}`, or step 4 showing `"seq":1` gone. That means a **typo deleted a message
   the operator never named** — report it as a destructive-input regression, not a cosmetic parser issue.

### 11.5 — a TORN durable write is recovered, and the next record is still readable ★★ ([[B135]], added 2026-08-07) — **nRF52 / QSPI ONLY**

⛔ **Same board restriction as 11.1, and for the same reason: on an ESP32 the RAM ring has no framing to tear, so
this passes vacuously.** ⚠ This is the one check no host gate can reach — the native cases run against a RAM fake,
so *"the seal survived a real flash and a real reset"* is untested until it is done here.

★ **What is being provoked:** the store writes a record as `[u16 framed_len][u32 seq][body]` in **two** flash
writes. A reset between them leaves a header claiming bytes that are not there. Before [[B135]] the **next** record
landed behind that header and was swallowed by it — physically stored and permanently unreachable.

1. On the QSPI node, receive **two** DMs. `pull_inbox 0 0` → two lines; note both `seq` values.
2. Arrange a reset **during** a record write. Two ways, in preference order:
   **(a)** start a burst of DMs from a peer (e.g. `testsend`-driven traffic at a fast cadence) and **pull power**
   mid-burst — repeat up to 5 times; **(b)** if (a) never lands inside a write, drive the burst and press RESET
   repeatedly. ⓘ This step is **probabilistic** — record how many attempts were made and whether step 4 ever showed
   a gap; "no tear was ever produced" is an honest, reportable outcome, and it is NOT a pass.
3. Power the node back up. Receive **one more** DM (call its expected seq `N`).
4. `pull_inbox 0 0` → **expected:** every record that was complete before the reset, in order, **plus a line with
   `"seq":N` carrying the correct body**. At most **one** record (the interrupted one) may be missing.
5. Repeat step 3–4 once more: a second post-tear record must also appear.
6. ⛔ **FAILURE SHAPES, distinct — record which:**
   - `"seq":N` **never appears** although the node acknowledged the DM ⇒ the new record landed behind the torn
     header and is unreachable — **this is exactly the [[B135]] defect and means the seal is not working on metal**;
   - a line appears with a seq that was **never sent**, or with a garbled/truncated body ⇒ a **phantom record** is
     being decoded out of the torn frame plus the next one;
   - **more than one** pre-reset record is missing ⇒ more than the tail frame was damaged (page-level corruption —
     outside what [[B135]] covers, and worth its own entry);
   - `"epoch"` in `inbox_end` **changed** ⇒ the store was formatted rather than recovered; the check is void and
     the format-on-dirty path is what needs investigating.

### 11.4 — the board control, so 11.1 cannot pass on the wrong hardware

On a **Heltec / ESP32** node: receive two DMs, `pull_inbox 0 0` (two lines), power-cycle, `pull_inbox 0 0`.
**Expected: ZERO lines** and a **changed `epoch`** — the RAM ring is volatile by design.
⛔ If the ESP32 node still shows its messages after a power cycle, then either the durable store is now wired on
ESP32 (a real change — say so) or the node did not actually reset; **either way 11.1's result on any board is void
until this control behaves.**

## Part 12 — §MH-S1 the mobile-attachment ADMISSION boundary (2026-08-07)

⛔⛔ **RE-SCOPED IN PLACE 2026-08-07 (§MH-S1b, QA round 2) — THE PREVIOUS SCOPE WAS TOO BIG, AND THE REASON IS
WORTH MORE THAN THE CHECKS.** This part used to open: *"no automated gate anywhere can produce a real
`DeviceHal::tx` refusal … only metal can show `tx_rejected`."* **THAT WAS A STATEMENT ABOUT THE HARNESS, NOT
ABOUT REACHABILITY.** `TestHal::tx` answered `ok` unconditionally — and it was **ONE FIELD** away from being
able to refuse. `TestHal::tx_answer` now exists, and the whole `tx_rejected` half of §6.1/§6.2/§6.3 is covered
natively, **immediate AND deferred, for all three frames**, by the three `§MH-S1b` cases in
`test/test_node_join.cpp` (each mutation-proven, including a harness-vacuity control that reddens exactly
those three). ★ **THE RULE THIS SLICE EARNED: before writing a metal-only check, ask what ONE parameter would
make it testable.** M2 says this document holds only what **no automated gate can reach** — and "cannot reach"
must mean the gate is *incapable*, not that today's fake happens not to.

★ **WHAT GENUINELY REMAINS, and it is now a PLUMBING check, not a behaviour re-test.** The core's *reaction* to
a refusal is native-covered. Two things still are not, and both are outside `lib/core`:
1. **that the real `DeviceHal::tx` produces the refusal at all** — its 8-entry outbound ring answering `busy`,
   bumping `txq_drops`, retaining nothing. `lib/hal/device_hal.cpp` is a **different ABI and a file neither
   native nor the simulator compiles**, so no gate links it;
2. **that the two report lines actually reach a console** — `fw_main`'s sink gates ordinary `_hal.log` on
   `g_mr_trace_on`, and both §MH-S1 lines are trace-gated, not `!!`. A line that is emitted but never printed
   is precisely the 2026-08-01 QA-P2 defect, and only hardware can show it.
⇒ **12.1/12.2 below are ONE bench run each** and their verdict is: *the real refusal happened (`txdrop` moved)
AND the exact line appeared*. ⛔ Do not re-test the branch logic here — the gate owns it.

### 12.1 — a mobile whose OWN radio refuses the CLAIM must NOT report itself registered ★★ SAFETY

The pre-S1 defect: the CLAIM was handed to `tx_initiating`, the result discarded, and the mobile adopted
unconditionally — `mobile status` said registered at a home that had never been sent anything.

1. On the **mobile**, `debug on` (the S1 report is trace-gated, not `!!`).
2. Drive the outbound queue to overflow while a registration is in flight — the reliable bench lever is the
   scheduled-send workload: `testsend <dst> 200 @sendms 20` (a send every 20 ms saturates the 8-entry ring), then
   immediately `mobile register`.
3. Watch the console.

**Expected — the exact line, and it must appear instead of a registration:**

```
mobile attach attempt refused by OUR OWN transmitter — retrying; the home is NOT implicated
```

then, within ~2 s, an automatic retry (a second DISCOVER), and `mobile status` **still `"registered":false`**.

⛔ **FAILURE SHAPES, and they are different bugs:**
- `mobile status` shows `"registered":true` while no home lists the mobile in its roster ⇒ **the §6.3 fix is not
  live on this build** — the CLAIM's admission result is being discarded again. This is the safety failure.
- the line appears but **no** second DISCOVER follows within ~2 s ⇒ the bounded retry (gate 6) is missing; check
  that `_mobile_arm_once` is being restored (with `mobile_autoregister=false` the retry is a no-op without it).
- `mobile_no_host` / a "no host found" report appears instead ⇒ **§6.1 is violated**: a local transmitter refusal
  is being blamed on the home.
- **`txdrop` > 0 but NO line at all** ⇒ this is now the *primary* thing 12.1 exists to catch (§MH-S1b): the
  branch itself is gate-proven, so a real refusal with no console output means the **sink** is swallowing it —
  the trace gate, not the protocol. Re-check `debug on` first, then `fw_main`'s log sink.
- nothing at all appears **and `txdrop` == 0** ⇒ the queue never actually overflowed. Confirm with `status`
  that **`txdrop` > 0** before reading any result here; without that the check is void.

### 12.2 — a host whose OWN radio refuses the OFFER says so

On an eligible **static home** with `debug on`, saturate its TX queue as in 12.1 and have a mobile DISCOVER at it.

**Expected line:**

```
mobile OFFER dropped at our own transmitter — not sent; the mobile's own retry is the backstop
```

and the mobile re-DISCOVERs and attaches on a later round.

⛔ **FAILURE SHAPES:** silence on the host while the mobile never attaches ⇒ the §6.2 report is not live and the
OFFER is vanishing exactly as it did pre-S1 · the host logs the line but the mobile **never** retries ⇒ the backstop
this branch depends on is broken, and §6.2's *reschedule* alternative (S2/S3) is needed after all.

⚠ **NOT A CHECK OF THE DEFER PATH.** A merely *deferred* OFFER or DISCOVER is admitted, prints nothing here, and is
correct — 12.1/12.2 fail only on a definitive refusal. `status`'s `txdrop` is the witness that one occurred.
⛔ **AND NOT A CHECK OF THE DEFERRED-THEN-REFUSED PATH EITHER (§MH-S1b).** "Deferred into the LBT ring, then
refused by the HAL when the slot fires" — for DISCOVER, CLAIM *and* OFFER — is fully covered by the native
`§MH-S1b` cases, including the one that matters most: a deferred CLAIM must not register the mobile until the
handoff. ⇒ **it was deliberately NOT added here.** Two checks, not five, is the whole point of the re-scope.

## Part 13 — §MH-S4 the CONFIRMED-attachment FSM and the two planes (2026-08-08)

★ **WHY ANYTHING IS OWED HERE AT ALL, stated before the checks (M2).** §MH-S4 is almost entirely native-covered:
the FSM, the triple match, the bounded re-CLAIM, the [[B139]] admission gate and the two planes each carry a
mutation-proven case in `test/test_node_join.cpp`. Following Part 12's own rule — *"before writing a metal-only
check, ask what ONE parameter would make it testable"* — the behaviour is **not** re-tested here. What remains is
**console-visible surface**, and it is owed because §10 makes wording part of the contract and because the
`registered:true` push MOVED, which no automated gate can observe on a real companion link:

1. **`mobile status` gained a three-plane block** (`attachment`, `home_link`, `home_confirm_age_ms`, `last_result`,
   `home_desired`, `claim_retries`/`claim_retry_max`, `offers`, `scan_idx`/`scan_count`, `candidates`). The JSON
   writer is native-pinned byte-for-byte; what is NOT is that `src/firmware_config.cpp` fills it from the right
   accessors on a real node — a different ABI and a file neither native nor the simulator compiles.
2. **`mobile unregister` is a new console verb** whose whole contract is that it transmits NOTHING.
3. ⛔ **The word "connected" must appear on no surface** (§10). The native gate asserts that for the JSON writer;
   only a bench run can confirm the plain-text lines a human reads.
4. ★ **§MH-S4b added four more (13.4–13.7):** the solicitation substate + retry window (`claim_solicited`,
   `retry_window_ms`), the **64-bit** confirmation age (the `uint32_t` cast that wrapped it at ~49.7 days lived in
   `src/`, which neither native nor the simulator compiles), the two `status` OFFER admission counters
   (`offerfull=` / `offerrej=`), and the two metal-only log lines — §10's **CONFIRMED** log plus the
   `mobile unregister` dormancy check whose `autoregister=1` arm §MH-S4 got backwards.

### 13.1 — `mobile status` reports `claiming` BEFORE it reports `registered` ★★ SAFETY / the §S0-4 surface

The pre-S4 defect: a CLAIM lost to an RX collision left the mobile reporting `"registered":true` to the app for
≈135 s while the home held no row at all. Post-S4 the app-facing field is the CONFIRMED attachment only.

1. Provision one ordinary static home (`cfg set host_mobiles on`) and one mobile, in range, same PHY.
2. On the mobile: `mobile status`.
3. Watch the field values across the attach.

**Expected — while the CLAIM is outstanding (poll fast, or attenuate the home so the first CLAIM is lost):**

```
{"ev":"mobile_status","mobile":true,"registered":false,...,"attachment":"claiming","home_link":"unknown","last_result":"none",...,"claim_retries":0,"claim_retry_max":3,...}
```

**Expected — once the home's roster carries our (hash, local id, epoch):**

```
{"ev":"mobile_status","mobile":true,"registered":true,...,"attachment":"attached","home_link":"confirmed","last_result":"confirmed","home_desired":false,"home_confirm_age_ms":<small and GROWING between polls>,"claim_retries":0,...}
```

★ **The two must be SEPARATE fields in every sample** — `attachment` and `home_link` are orthogonal planes (§4.1),
and a build that folded them would print only one.
★ **`home_confirm_age_ms` must GROW between two polls taken a minute apart, and must be ABSENT (not `0`) before
the first confirmation.** An age that is always 0, or present-and-zero on a never-confirmed node, is the
display-shaped-field defect this plane exists to prevent.

⛔ **FAILURE SHAPE:** `"registered":true` appearing while `"attachment":"claiming"` — that is the §S0-4 defect
reaching metal, i.e. the app-facing field was wired to the provisional flag again. Equally a failure:
`home_confirm_age_ms` present with value `0` on a mobile that has never attached, or the literal string
`connected` anywhere in the object.

### 13.2 — `mobile unregister` ends the session and puts NOTHING on the air

1. With the mobile `attached` from 13.1, start a receive capture on a third node (or watch the home's console).
2. On the mobile: `mobile unregister`.

**Expected, exactly:**

```
> mobile unregister: home-service request cleared — attachment dormant, timers cancelled (no wire message; the old home ages the row out)
```

3. `mobile status` immediately after.

**Expected:** `"registered":false`, `"attachment":"dormant"`, `"home_link":"unknown"`, `"home_desired":false`,
and **no** `home_confirm_age_ms` field.

4. ★ **The capture must show ZERO frames originated by the mobile as a result of the verb** — §4.3 adds no
   deregistration wire message; the home ages the row out under §9.

⛔ **FAILURE SHAPE:** any J or P frame from the mobile within the verb's turnaround, or `attachment` reading
`recovering`/`seeking` straight after the verb on a `mobile_autoregister=0` node.

⚠ **On a node with `mobile_autoregister=1` this is EXPECTED to be transient**: the verb returns the node to
`dormant`, and the autonomy licence (§4.2 — "recovery may continue indefinitely") then legitimately re-enters
`seeking` on the next FSM tick, which will emit a DISCOVER. Run 13.2 on a mobile with `mobile_autoregister=0`,
which is §4.2's own use case for the verb; both arms are asserted natively.

### 13.3 — a busy channel must NOT deregister a healthy mobile ([[B139]]) — the ONE behaviour check, and why

★ This is the only behaviour re-test in Part 13, and it is here for a reason Part 12's rule allows: the native
case drives the refusal through `TestHal::tx_answer`, which proves the CORE's reaction. What it cannot prove is
that a **real** congested channel produces refusals of the *presence probe* specifically — the probe rides
`LbtKind::flood` through the real LBT/duty path, and B139's own record notes it defers in that hot branch.

1. Bring a mobile to `attachment:"attached"`, `home_link:"confirmed"`.
2. Generate heavy channel traffic from two other nodes so the mobile's own LBT/defer ring saturates (the same
   NAV traffic Part 12 uses). Leave the home powered and in range and otherwise healthy.
3. Poll `mobile status` for at least `presence_check_max_ms` plus the retry ladder.

**Expected:** `attachment` stays `attached` throughout. `home_link` may read `checking` (a real probe went out
unanswered) and must return to `confirmed`. `last_result` may read `tx_rejected` or `defer_full`.

⛔ **FAILURE SHAPE:** `home_link":"lost"` or `attachment":"recovering"` while the home was healthy and in range —
that is B139 back, at a fourth admission site §6.4's own text does not enumerate. With `trace on`, the
diagnostic line for the refusal is exactly:

```
presence probe refused by OUR OWN transmitter — the home link is NOT implicated
```

⚠ That line is trace-gated (not `!!`), so it prints only with `trace on` — Part 12's own caveat applies.

### 13.4 — §MH-S4b: `mobile status` reports the SOLICITATION substate and the retry window

Added because §7.1 step 3 is now **two** deadlines with a substate between them, and the substate is what makes
"we asked and are waiting" distinguishable from "we asked and were answered". The JSON writer is native-pinned; what
is not is that `src/firmware_config.cpp` fills the two new fields from the right accessors on a real node.

1. Bring up one host and one mobile as in 13.1. Attenuate or power-cycle the home so the first CLAIM is lost.
2. Poll `mobile status` **fast** (a second or two apart) across the attach.

**Expected — during `claiming`, BEFORE the solicitation probe goes out (~3 s window):**

```
..."attachment":"claiming","home_link":"unknown","last_result":"none","home_desired":true,"claim_retries":0,"claim_retry_max":3,"claim_solicited":false,"retry_window_ms":0,...
```

**Expected — after the solicitation probe, while its ~12 s roster window runs:**

```
..."attachment":"claiming",...,"claim_retries":0,...,"claim_solicited":true,"retry_window_ms":0,...
```

**Expected — after the window expires with no roster (one re-CLAIM spent, next ask armed):**

```
..."attachment":"claiming",...,"claim_retries":1,"claim_retry_max":3,"claim_solicited":false,...
```

★ **`claim_solicited` must be ABSENT once `attachment` is `attached`** — it is a substate of `claiming` only.
★ `retry_window_ms` is the **no-host DISCOVER backoff** (§5.2), so it reads `0` on a first try and grows
`5000 → 10000 → 20000 …` only while `attachment` is `seeking` with no host answering. ⛔ It is **not** a countdown
to the next attempt; that field is owed by S5 ([[B154]]) and must not appear yet.

⛔ **FAILURE SHAPES:** `claim_solicited` never reading `true` (the ask is not happening, or the substate is not
wired) · `claim_retries` incrementing in the same poll interval as the probe (the verdict is being spent before the
answer can arrive — the §MH-S4 defect back) · `claim_solicited` present on an `attached` node.

### 13.5 — §MH-S4b: the confirmation AGE must not wrap, and it is only visible on metal

`Node::mobile_home_confirm_age_ms()` is 64-bit and was being cast to `uint32_t` in `src/firmware_config.cpp`, so the
displayed age wrapped at **~49.7 days** and a months-stale confirmation rendered as a fresh one. The cast is gone
and the field is 64-bit end to end, but the only place the whole chain exists is a device.

1. Attach a mobile and confirm it (`home_link":"confirmed"`).
2. Note `home_confirm_age_ms`, then poll again several minutes later.
3. ★ **The long check, which is the point:** leave the node running (or resume a long-lived node) and poll again
   after **more than 49.7 days of uptime since the confirmation**, or reproduce it by holding the mobile attached
   while its home is silent for that long.

**Expected:** `home_confirm_age_ms` increases MONOTONICALLY, in milliseconds, with no reset. Past
**4 294 967 295** it must keep counting — e.g. `"home_confirm_age_ms":5000000000`.

⛔ **FAILURE SHAPE:** the value dropping back near zero, or reading ≈`705032704` where ≈`5000000000` is expected —
that is the u32 truncation, and it renders a two-month-old confirmation as an eight-day-old one.
⚠ A 50-day bench run is impractical; the practical substitute is to confirm the field is **not clamped and not
truncated at 32 bits** by checking that it passes `4294967295` on any node kept alive that long, and otherwise to
accept the native pins (a serializer case above `UINT32_MAX`, a `static_assert` on the field type, and a core
accessor case). ★ Record which of the two you did — do not tick this as a full pass on the short check alone.

### 13.6 — §MH-S4b: `status` exposes the two OFFER admission counters

§10 asks for an OFFER-ring overflow counter and a transmitter-rejection counter beside `txdrop`. Both existed only
as native accessors. They are HOST-side, so run this on the node that hosts mobiles.

1. On a host with `cfg set host_mobiles on`: `status`.

**Expected — the two fields ride immediately after `txdrop`, and are ALWAYS present (a `0` is a reading):**

```
… txq=0 txdrop=0 offerfull=0 offerrej=0 txto=0 …
```

and over BLE/JSON: `…"txq":0,"txdrop":0,"offer_full":0,"offer_reject":0,"rx":…`

2. Make them move: bring **more than `cap_pending_mobile_offers` (8)** mobiles into range at once so the pending
   ring refuses an admission (`offerfull` rises), and generate heavy channel traffic during a DISCOVER burst so the
   host's own transmitter refuses an armed OFFER (`offerrej` rises).

⛔ **FAILURE SHAPES:** either field missing from the line · either field OMITTED when zero (that makes "never
counted" indistinguishable from "counted zero") · `offerrej` staying 0 while `mobile_offer_dropped` events occur —
[[B146]] required those two to be equal by construction.

### 13.7 — §MH-S4b: the CONFIRMED device log, and `mobile unregister` really stays dormant

Two lines, both metal-only.

1. With `trace on`, attach a mobile. **Expected, exactly:**

```
mobile ATTACHMENT CONFIRMED by the home roster
```

and, when the attachment needed a re-CLAIM to land (attenuate the home so the first CLAIM is lost), exactly:

```
mobile ATTACHMENT CONFIRMED by the home roster (healed by a re-CLAIM)
```

★ This is the **CONFIRMED** third of §10's scheduled / transmitter-admitted / confirmed log triple; before §MH-S4b
it had no metal surface at all (`MR_EMIT` is device-stripped). ⛔ **FAILURE SHAPE:** neither line ever printing, or
the `(healed…)` variant printing on a clean first-time attach.

2. ★★ **The `autoregister` ON arm, which is the one that was wrong:** on a mobile with
   `cfg set mobile_autoregister 1`, attach it, then `mobile unregister`, then **wait at least 20 minutes** without
   touching the console.

**Expected:** `mobile status` reads `"attachment":"dormant","home_desired":false` and **stays** there. ⛔ **NOT ONE
frame** on the air from the mobile FSM — no DISCOVER, no presence probe. Then `mobile register` and it must attach
again within one normal discovery cycle.

⛔ **FAILURE SHAPE:** the node re-entering `seeking`, or airing a DISCOVER, on its own after the verb. That is the
§MH-S4 contradiction (the verb reported `dormant` while `mobile_autoregister` still armed the FSM).
⚠ A `cfg set mobile_autoregister 0` after the verb is **not** required and must not be needed — the flag is the BOOT
policy only. Conversely, `cfg set mobile_autoregister 1` on a dormant mobile **is** expected to start a session
immediately (it routes through the same path as `mobile register`).

## Part 14 — §B153/§B157 the retired RTS-derived terminal decisions (2026-08-08)

⚠ **Why this part exists at all (M2), and why it is short.** ⛔ **The wire did NOT change** (the unicast RTS is
still 7 B, no `wire_version` bump, no mixed-firmware hazard), and the behaviour change is fully covered by the
corpus (36/36 green) plus six native regressions with a mutation battery. **Exactly one thing is beyond every
automated gate: the extra airtime of the new recovery path on a real radio, and the recovery path itself under
real packet loss.** The simulator models loss it was told to model; a bench radio loses frames its own way.

### 14.1 — ★★ A LOST ACK MUST STILL DELIVER EXACTLY ONCE (the behaviour the slice turns on)

The retired short-circuits meant a retried exchange ended at the CTS. Now the duplicate DATA really flies, and
the receiver must ACK it and **not** deliver it again.

1. Two boards, A and B, in radio range. On B: `log level` up (or `trace on` if built with the decoded trace).
2. From A: `send <B> hello-once`.
3. Confirm B shows the message **once** and A reports `SEND-ACKED`.
4. Now force the failure the slice is about — easiest reliable way on a bench: put B where it can hear A but A
   can only marginally hear B (move B to the edge of range, or attenuate A's RX), so B's **ACK** is the frame
   that gets lost while the DATA still arrives. Send again: `send <B> hello-twice`.

**Expected:** A retries the whole exchange (a second `rts_tx`/DATA is visible in its log), B logs a **second
DATA reception**, and — ★ the assertion — **B's application shows `hello-twice` EXACTLY ONCE**. A ends
`SEND-ACKED`, not `SEND-FAILED`.
⛔ **FAILURE SHAPES, both reportable:** the message appearing **twice** on B (the DATA-level dedup is not holding
— that is the duplicate-delivery risk this design accepts responsibility for), or A ending `SEND-FAILED` while B
has the message (the duplicate was refused rather than ACKed).
ⓘ A `CTS` with the `RCVD` marker must **never** appear in a trace from new firmware — `already_received` is
reserved and never emitted. Seeing it means the peer is running pre-§B153 firmware.

### 14.2 — the recovery path costs more airtime now; record what the radio says

Same two boards. `cfg set sf 12` (or the highest routing SF you bench at), repeat 14.1's lost-ACK case, and read
the duty/airtime accounting (`status`, or `duty`) before and after.

**Expected:** one lost-ACK recovery costs roughly **+3.3 s at SF12 / 125 kHz / CR4/5** (a 20-byte body: 2342 ms
→ 5667 ms) and **+130 ms at SF7** (83 → 212 ms) versus the retired path, because the duplicate DATA and its ACK
now fly. ⓘ This is a **recording, not a pass/fail** — write down what the device's own accounting shows so the
model and the radio can be compared once. ⛔ Successful traffic must show **no** change at all: the RTS is still
7 bytes.

### 14.3 — the flight the implicit ACK used to cancel must now complete on its own

Only runnable with three boards in a line (A → B → C) so A can overhear B forwarding onward.

Send A→C. **Expected:** the DM completes. ⛔ **FAILURE SHAPE:** A reporting neither delivery nor failure — that
would be the silent discard [[B157]] produced when an overheard forward was credited to the wrong flight.
ⓘ A will now wait out its ACK timeout and retry in some cases where it used to stop early; that is the accepted
cost, and it is visible as an extra `rts_tx` in A's log, not as a failure.

## Part 15 — §HYBRID-RTS-S1 the 10/11-byte unicast RTS (2026-08-08)

⚠⚠ **WHY THIS PART EXISTS (M2), AND IT IS THE FIRST HARD FLAG-DAY THIS BENCH SCRIPT HAS EVER CARRIED.**
Part 14 above could say *"the wire did NOT change … no mixed-firmware hazard"*. **That sentence is now FALSE for
anything built after §HYBRID-RTS-S1.** The unicast DM RTS is **10 bytes (plaintext) / 11 bytes (encrypted)** and
the old **7-byte form is REJECTED OUTRIGHT — there is deliberately NO compatibility parser** (design §2.4;
MeshRoute is not deployed, so the owner ruled a clean break over an ambiguous one). ⇒ **no automated gate can
reach this: the simulator runs ONE build of the firmware, so a mixed fleet is structurally invisible to it.**
⛔ No `wire_version` bump was taken, so a mixed pair does **not** refuse to join — it joins and then silently
fails to move DMs, which is the worst of the two failure shapes and the reason this check is here.

### 15.1 — ★★★ REFLASH EVERY BOARD IN THE TOPOLOGY TOGETHER — the precondition, not a test

Before any bench session on post-S1 firmware: **flash every node you intend to use, and confirm each reports the
same build.** `status` (or `ver`) on each board; write the revision into the Completion record below.
⛔ **A single un-reflashed node is not a degraded node — it is a node whose DMs cannot be authorized at all.**

### 15.2 — ★★ A MIXED PAIR FAILS IN THE PREDICTED PLACE (run ONCE, deliberately, then reflash)

Worth doing exactly once so the failure signature is known to the tester rather than discovered mid-session.
1. Board A: post-S1 firmware. Board B: pre-S1 firmware (keep one old `.uf2`/`.bin` for this).
2. From A: `send <B> mixed-fleet-probe`.

**Expected — and the point is WHICH side is silent:** B **never CTSes** (its parser sees a 10-byte frame where it
demands 7 and returns nothing at all — no NACK, no log line), so **A retries to exhaustion and reports
`SEND-FAILED`**. In A's log: repeated `rts_tx`, **zero** `cts_rx`.
3. Now reverse it — from B: `send <A> reverse-probe`. **Expected:** A never CTSes (its parser rejects B's 7-byte
   RTS), B reports `SEND-FAILED`.
⛔ **FAILURE SHAPE THAT MATTERS MORE THAN THE ABOVE:** if either side *does* complete the DM, the compatibility
parser this design forbids exists somewhere — report it, because it means frame length has stopped being the
plaintext/encrypted discriminator.
★ Then reflash B and confirm 14.1 passes again. **Beacons, joins, channel floods and M-broadcasts are UNAFFECTED**
(9 B and 43 B are byte-identical), so the two boards will still see each other in `peers` — that is exactly the
trap: **presence is not evidence of compatibility on this wire.**

### 15.3 — the RTS really is 10 bytes on the air, and 11 when the DM is sealed

Needs a board built with the decoded frame trace (`trace on`), or a third board as a passive listener.
1. Two post-S1 boards. `trace on` on the listener/receiver.
2. `send <B> plain` → the RTS line must show a **10-byte** frame.
3. With keys exchanged (`reqpubkey`, then `cfg set e2e_dm on` or `send <B> sealed -e`) → the RTS must show an
   **11-byte** frame.
**Expected:** exactly those two lengths, never 7, never 8, never 12. ⓘ **This is the ONE check that exercises the
encrypted 11-byte arm against a real radio: the simulator corpus produces only TWO 11-byte RTS frames in all 36
scenarios, so metal is effectively its primary evidence.**

### 15.4 — the CTS-wait grew, and only the NO-RESPONSE path should feel it

`cfg set sf 12` (or your highest routing SF) and `cfg set bw 62500` if available — the PHY where the correction is
largest. Send to an **absent** destination id that has a stale route, so the RTS goes unanswered.
**Expected:** the retry cadence is measurably SLOWER than pre-S1 by roughly the per-PHY figure — **+1049 ms per
attempt at SF12/BW62.5 k/CR4/8 for a sealed DM, +20 ms at SF8/BW125 k, and +0 ms at several PHYs where the extra
bytes fall in the same LoRa symbol bucket.** ⓘ **A recording, not a pass/fail** — write down the observed gap.
⛔ A SUCCESSFUL exchange must show **no** change: an ordinary CTS cancels the wait the moment it arrives.

### 15.5 — ⛔⛔ SUPERSEDED BY §HYBRID-RTS-S2 (2026-08-08): the terminal `RCVD` CTS is now PRODUCED

⚠ **Read Part 16 instead.** S2 restored the emitter, so a `CTS` carrying `RCVD` is now EXPECTED on an exact
retry of a completed flight and is no longer evidence of a stale build. The paragraph below is retained because
it is the ONLY record of what the marker meant between §B153 and §S2, and because its length note still holds:
a terminal CTS is exactly 6 B (plaintext) or 7 B (encrypted) and carries no NAV byte.

### 15.5 (historical) — the terminal `RCVD` CTS must still NEVER appear

★ Unchanged from 14.1's note and re-stated because the codec now *can* build one: `already_received` has **zero
producers** in S1. A `CTS` carrying the `RCVD` marker — at ANY length — means either a pre-§B153 peer or that S3
has landed. **Report it either way.**

## Part 16 — §HYBRID-RTS-S2 the FIRST 6/7-byte frame ever to fly (2026-08-08)

⚠ **WHY THIS PART EXISTS (M2), stated narrowly so it does not become a re-test of the corpus.** The simulator
already proves the *behaviour* (36/36 green, 202 terminal CTS frames, 691 → 728 deliveries), so none of that
belongs here. What NO automated gate can reach is the **radio**: §HYBRID-RTS-S1 built the terminal CTS codec but
gave it ZERO producers, so **until S2 no MeshRoute node had ever transmitted a 6- or 7-byte frame.** Every other
control frame this firmware airs is 3, 4, 9, 10, 11 or 43 bytes. A LoRa PHY can behave differently at a new short
length (preamble/CRC/implicit-header interactions, and the SX1262 FIFO/IRQ path at a length nothing has exercised),
and the simulator models none of that.

### 16.1 — ★★ A LOST-ACK RETRY COMPLETES ON A 6-BYTE CTS, WITH NO SECOND DATA

1. Two boards, A and B, both post-S2, one hop apart, `nav` ON (the default).
2. From A: `send <B> terminal-cts-probe`. Confirm normal delivery on B.
3. Force the ACK loss: the cheapest reliable way is to power B's radio down for ~1 s **immediately after** B logs
   `data_rx` (or simply repeat the send while B is momentarily shielded). A must time out and retry the RTS.

**Expected on A:** a second `rts_tx` followed by `cts_rx`, then **NO `data_tx` for that ctr** and the send
completes. **Expected on B:** a `cts_tx` carrying `already_received` and **no second `data_rx`, no second
`delivered`** — the payload reaches the app exactly once.
⛔ **The failure that matters is not "it didn't optimise" — it is A sending the DATA again and B DELIVERING IT
TWICE.** Record the app-side count, not just the log lines.

### 16.2 — ★ THE ENCRYPTED ARM, BECAUSE IT IS THE ONE THE CORPUS CANNOT SEE

The 7-byte terminal CTS is **corpus-dark**: all 429 measured cache hits are plaintext (§HYBRID-RTS-S2 (3)).
Repeat 16.1 with a sealed DM (`send -e`, both boards holding each other's authoritative pubkey). Expect the same
shape, and confirm the CTS on A is **7 bytes** rather than 6 if you have a frame length in the log.

### 16.3 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds it

- The mixed-fleet flag day is **Part 15's**, unchanged: S2 alters no frame FORMAT, only which frames get produced.
- RAM: `sizeof(Node)` grows **+296 bytes per layer** (a 12-slot completed-flight cache plus 8 bytes of `PendingRx`).
  That is a **build** figure, not a bench check — but the board sweep is still OWED and no board RAM number has
  been measured, so watch for an allocation failure at boot on the tightest env until it has been.

## Completion record

- Firmware revision tested: `________________`
- Boards / node labels: `________________`
- Date and tester: `________________`
- Failed or skipped checks: `________________`
- Log/archive location: `________________`
