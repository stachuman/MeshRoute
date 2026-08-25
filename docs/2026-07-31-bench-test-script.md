<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Firmware bench acceptance library — 2026-07-31 onward

This is the **detailed authority** for manual pass conditions and retained evidence. It is intentionally comprehensive;
do not execute it from top to bottom. For the current short, ordered checklist use
[`2026-08-18-metal-session-run-sheet.md`](superpowers/plans/2026-08-18-metal-session-run-sheet.md).

## Status notation

- `[x]` — passed on the build named beside the result. It is not automatically evidence for a later build.
- `[ ]` — still owed on the current image.
- `[-]` — intentionally not run or not applicable; write the reason beside it.
- `OBSERVE` — opportunistic saturation check. Silence is neither pass nor failure; contradictory output is a failure.
- `RETIRED`, `SUPERSEDED`, `KNOWN GAP`, and historical sections are reference material, not runnable checks.

Never erase a completed result when a later build needs a re-run. Add a new current-image checkbox and retain the old
result with its revision. This keeps “done before” distinct from “qualified now”.

## Current qualification snapshot — 2026-08-20

| State | Work | Where |
|---|---|---|
| [x] | B196 sleep soak: 36,934 attempts, no panic; closed on `cb76d79` | Part 26 |
| [x] | Team transaction on `fc89e14`: 27.1, 27.3-27.5, 27.8-27.9 | Part 27 result table |
| [ ] | Current OLED chrome and sleep regression | Parts 24.1-24.3 and 25.1-25.6 |
| [ ] | Physical TxDone → `AIRED` → UI result | Parts 21-22 (B164) |
| [ ] | Post-fix team checks B209-B212 | Parts 27.11-27.15 |
| [ ] | Truthful `cfg mobile-reg:` labels | Part 27.16 (B214) |
| [ ] | Static-node `team new` | Part 27.6; requires a static/factory-erased node |
| [ ] | Real power-cut atomicity | Part 20.5 (B193); run last |
| [-] | Restart badge | Part 25.4; unavailable unless `MR_UI_BLE_ROW=1` |
| [-] | Live-keyless team install | Part 27.10; unreachable by construction |
| [-] | Real save-failure injection | Part 27.7; optional unless a safe fault-injection method exists |
| [ ] | Non-team OLED layout | Part 25.7; separate `gateway_heltec` image |
| [ ] | OLED static join from a stored profile | Part 30; run after §UI-15 slice 6 QG passes |

All earlier `[x]` marks below are preserved. They are subsystem history and are not prerequisites for every Heltec
session.

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

## Execution rule

Use the current run sheet for order and rig changes, then return here only for the referenced Part's exact commands
and pass condition. Stop at the first unexplained failure and preserve the boot-to-failure log plus the flashed ELF.
Do not continue into a noisier topology after a console, storage, TX-completion, or UI-safety invariant fails.

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
  - Do: watch the status strip from boot, before the first successful sample.
  - Pass: the **battery slot's token** (the rightmost field, beside the outline) is exactly `--`; the STATUS body
    reads exactly `batt --`; **no** text anywhere matches `<digit>.<digit>V`; no percentage anywhere.
    ⓘ §CHROME-3 moved this field from *"the bar's last field"* into a fixed right-anchored slot; the rule is unchanged.
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
    the **status strip** (§CHROME-3, see Part 24) reading `0` mail, the home slot's `--`, `--` teammates and `--`
    volts on a fresh node, then `STATUS` / `me T<team_local_id>  team <8-hex team_id>` / `DM 0, newest --` /
    `CH 0, newest --` / `batt --`.
    ⛔ **WITHDRAWN WORDING, kept visible:** this line used to read *"status bar `DM0 CH0 T0/0 --`"*. That packed text
    bar was **replaced** by the icon strip in §CHROME-3 (design §2/§3.1); seeing it means an older image is flashed.
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

- [x] **9.1 — `cfg` twenty times: every row structurally complete**
  - Do: `cfg`, twenty times, while the node is beaconing.
  - Pass: **every** received line is a whole row. Specifically **NOT** the H5-06 shapes:
    - ⛔ `  proto : duty=1.00% beacon_ms=900000168010102layer=5 leaf=5000` (labels dropped, values fused)
    - ⛔ `1Laye0000[route] dest=5 …` (a previous response's residue prefixed to the next)
  - Pass shape (values will differ; the STRUCTURE is the check — every `key=` present, one row per line):
    - `  proto : duty=1.00% beacon_ms=900000 hop_cap=16 team_hop_cap=8 lbt=1 nav=1 intra_relay=0 host_mobiles=1 nav_ignore=0`
  - ★ A row may be **absent**; it may never be **wrong**. An absence must be accompanied by 9.4's drop line.

- [x] **9.2 — `routes` twenty times with a gateway schedule present**
  - Do: `routes`, twenty times, on a node that has learned a gateway.
  - Pass: the `[route]   gw_sched period=…ms heard_ms=…` line and each `[route] dest=…` line are separate, complete
    lines. ⛔ Never `heard_ms=39214608526@]5@0125015-20[route] dest=5` (the H5-06 fusion).

- [x] **9.3 — `help` five times: no line without a line ending**
  - Do: `help`, five times.
  - Pass: every received help line ends with CRLF and **nothing follows it on the same physical line** — in particular
    the next prompt/response never starts mid-line. (The old `hl()` emitted its CRLF only when two FIFO bytes happened
    to be free; H5-06 recorded exactly that.)
  - ⓘ **EXPECTED, NOT A DEFECT: `help` is 6121 B / 75 lines and does not fit the 2048-B console stage.** It delivers
    roughly the first 25 lines and then reports the loss (9.4). What must never happen is a *garbled* or *unterminated*
    line. If the whole text is wanted at the bench, raise `MR_CONSOLE_STAGE_BYTES` (see `src/console_sink.h`).

- [x] **9.4 — the deferred drop report, verbatim**
  - Do: run `help` (which necessarily overflows the stage). Watch for the report after the delivered lines.
  - Pass: exactly this line, on its own line, **once** per burst of loss:
    ```
    !! CONSOLE_DROP lines=51
    ```
    (the count varies; the text does not — `!! CONSOLE_DROP lines=<N>`, CRLF-terminated). It must appear **after** the
    lines it refers to, never inside one, and must not repeat while nothing further is lost.

- [x] **9.5 — stop the host reading, then resume (the anti-wedge)**
  - Do: with the monitor attached, suspend the reader (`Ctrl-S` in a terminal that honours it, or pause/detach the
    monitor process — do **not** unplug), issue `cfg` a few times, then resume.
  - Pass: **the node keeps working throughout** — beacons continue, a DM still ACKs, no reset, no watchdog, no wedge.
    After resuming, output continues with **complete lines only** plus a `!! CONSOLE_DROP lines=<N>` for what was lost.
  - ⛔ Fail if the console freezes the radio (missed beacons/ACK timeouts correlated with the pause), or if a partial
    row appears after the resume.

- [x] **9.6 — boot banner intact**
  - Do: reset the board with the monitor already attached; capture the boot log.
  - Pass: every banner line complete, ending with `  node      = up. Type 'help' for commands.` Measured to be ~1.3 KB,
    i.e. inside the stage, so **no banner line should be missing at all**.

- [x] **9.7 — `reboot` still prints before it resets**
  - Do: `reboot`.
  - Pass: `> rebooting` is received **before** the reset. (This is the one deliberately blocking path,
    `GuardedConsole::flush()`; if the line is missing, the bounded drain is not working.)

- [ ] **9.8 — BLE `help` is refused, not streamed**
  - Do: from the companion/BLE console, send `help`.
  - Pass: exactly one JSON line — `{"err":"help","msg":"console_only"}` — and **no** multi-kilobyte help stream over
    NUS. (Before this fix a BLE `help` printed the text to *USB* instead; now help honours its sink, so the refusal is
    what keeps 6 KB off a link that has wedged this node before.)

- [x] **9.9 — the `cfg` SF list appears in the response, not on another transport**
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
no gate reaches is the **rendering**: the arrival serial is now uncapped and the clamp lives in exactly one place,
`UiInboxCounters::publish`. If a future edit publishes the raw serial instead, every native case still passes and the
panel silently grows a digit the 128-px line has no room for.

⛔⛔ **RETARGETED BY §CHROME-3 (2026-08-16), AND THE WITHDRAWN EXPECTATION IS KEPT VISIBLE.** This entry used to read
*"Expected status bar, exactly — `CH999`, never `CH1000`: `DM0 CH999 T0/0 --`"*. The packed text bar is **gone**; the
strip draws the **combined** session-unread value clamped to `99+` (design §4.1), so **`CH999` is no longer rendered
anywhere on the strip** and looking for it would fail a correct build. The 999 display cap survives in the **STATUS and
INBOX body rows**, which still print `DM <n>` / `CH <n>` from the same capped fields — so that is where it is checked.

1. From a second node, post more than `999` channel messages without visiting INBOX on the panel node (a scripted
   burst; nothing else reaches four digits).
2. Expected on the **strip**: the mail slot reads `99+` (not `999`, not `1000`, not a rolled-over small number), and
   ⛔ the four icons after it have **not moved** — the token is three columns wide, which is what its slot budgets.
3. Expected on the **STATUS body**, exactly: `CH 999, newest <age>` — never `CH 1000`.
4. Then walk to INBOX and let one full frame draw. Expected: the mail slot returns to `0` and the body to `CH 0`, and
   the strip's geometry is unchanged throughout.

⛔ A four-digit count anywhere, an icon that shifts, or a count that *drops* while the burst is still arriving is a
regression. ⓘ If the burst is not practical on the day, say so in the completion record rather than ticking it — the
behavioural half is natively covered; only the **rendering** half is owed here.

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
   ⓘ **§T3:** a brief **`QUEUED`** may appear first (core acceptance -> the radio's TxDone edge). ⛔ It may be too
   brief to see and **its absence is NOT a failure**; the absence of `SENT, waiting` still is.
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
⛔ **`SENT` in any form is a false confirmation in that state — `SENT, waiting`, or the no-relay reading (`NO RELAY
HEARD` since §T3; the retired spelling was `SENT, no relay`). Any of them: stop and report.** ⚠ The §T3 rename moved
the spelling of the no-relay reading; it did NOT narrow this prohibition — BOTH spellings stay forbidden here. Hard to provoke on demand;
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
   **`NO RELAY HEARD`** (§T3 renamed this reading; the retired spelling was `SENT, no relay`).
   ⓘ **§T3:** a brief **`QUEUED`** may appear between `SENDING...` and `SENT, waiting`. ⛔ Too brief to see is
   EXPECTED and its absence is NOT a failure.
   ⛔ **Sitting on `SENDING...` all the way to the settle or the 15 s auto-exit is the B113 regression** — the accepted
   post's acceptance never moved the channel state. The DM twin (`SENT, waiting` in 8.17 step 5) always worked; only the
   channel arm was missing.
   ⛔ **§T3 ADDS A SECOND FAILURE SHAPE AT THE SAME STEP: sitting on `QUEUED`** while the console shows the post
   really went out. That means the radio's completion never reached the app — the drain, the ownership rule, or the
   correlation. It is metal-only: neither native nor the simulator has a TxDone edge.

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
… txq=0 txdrop=0 txfail=0 txoutdrop=0 offerfull=0 offerrej=0 txto=0 …
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

## Part 17 — §MH-S5 host-row lifetime and the expired-id return (2026-08-10)

⚠ **M2 SCOPE, stated so nothing else is added here:** the native suite already asserts the 25-minute boundary from
BOTH sides, both removal kinds, the parallel-array compaction, and all eight §9.4 steps — with nine mutations RED.
What NO automated gate can reach is **real wall-clock time on real silicon** (the sim runs 60 simulated minutes in
seconds and no board build compiles the test suite) and **a console line, which is device-only text**. Those two,
and nothing more.

### 17.1 — ★★ A HOST ROW IS PHYSICALLY GONE AT THE EXPIRY BOUNDARY, ON REAL TIME

⚠ **USE A TEMPORARY BENCH CONSTANT.** 25 minutes of standing still is not a bench test. Rebuild the HOST only with
`protocol::mobile_liveness_ms` cut to **120000** (2 min), run this, then **rebuild the defaults** — §12.3 item 6
asks for exactly that, and the boundary arithmetic is identical at any value.

1. Host: `cfg set host_mobiles on`, attach one mobile, confirm `status` shows `hosting=1`.
2. Power the mobile **off** (⛔ not `mobile unregister` — that is the *other* path; this test is about silence).
3. Wait past the shortened boundary, then on the host: `status`.

**Expected — the per-row block is GONE and so is the count line:**

```
  hosting=1 mobile(s)
    m[0] hash=0x1A2B3C4D local=254 DIRECT age=45s/120s
```
…becomes, after the boundary, **no `hosting=` line at all** (the line is printed only when the count is non-zero).

4. Prove the id is genuinely released, which is the half a "count went to 0" reading cannot show: power a
   **different** mobile on and let it register. **Expected:** its `local=` is **254** — the departed mobile's id,
   re-offered — not 253.

⛔ **FAILURE SHAPES:** the row still listed after the boundary (the expiry never ran on device) · the row gone but
the new mobile offered **253** (the row was hidden, not removed — the id is still held) · `age=` frozen (the row
lifetime clock is not being stamped) · a P roster still advertising the departed hash (sniff it: a home must never
claim to host a mobile it has expired).

### 17.1b — ★★ §B177-FIX: A MOBILE THAT IS AUDIBLE BUT ONLY **BEACONING** IS STILL EXPIRED (owner ruling, ledger §1.16)

⚠ **THIS IS THE ONE METAL-ONLY RESIDUE §B177-FIX ADDS, AND IT IS *NOT* 17.1 REPEATED.** 17.1 powers the mobile **off**,
so no frames arrive at all. Here the mobile stays **powered, in range and beaconing** while its presence probes stop —
which is exactly the case the removed beacon → `mobile_reg_touch()` touch used to keep alive **for ever**. Native covers
the refusal (four arms, mutation `m_bcn` = 12 RED); what only metal can show is **real wall-clock expiry with real
beacons genuinely being received**, plus the device-only console line.

⚠ **USE THE SAME TEMPORARY BENCH CONSTANT AS 17.1** (`protocol::mobile_liveness_ms` → 120000 on the HOST only, then
rebuild the defaults). ⛔ Do the whole of 17.1b in the same session as 17.1 so the rebuild is paid once.

1. Host: `cfg set host_mobiles on`, attach one mobile, `status` → `hosting=1` with `m[0] … DIRECT age=<n>s/120s`.
2. On the **MOBILE**: `mobile unregister`. ★ This is the arrangement, and it is chosen because it is purely local — it
   cancels `kPresenceProbeTimerId` (probes stop) and sends **nothing** to the home, while **beaconing is untouched** and
   continues on `beacon_ms`. ⛔ Do **not** power the mobile off; that is 17.1.
3. Confirm on the host, with `debug on`, that the mobile's beacons **are still arriving** (its `hash=`/src appears in the
   beacon/neighbour traffic). ★★ **THIS STEP IS THE WHOLE TEST'S PREMISE — without it, a pass proves only that a silent
   mobile expires, which 17.1 already proves.**
4. Wait past the shortened boundary. On the host: `status`.

**Expected — the row is GONE even though the mobile is audible:** no `hosting=` line at all (it prints only when the
count is non-zero), exactly as in 17.1. Sniff the host's P roster: it must **not** advertise the departed hash.

5. ★ **THE POSITIVE CONTROL, so the step cannot pass on a node that simply lost its registry:** on the mobile,
   `mobile register`. It re-attaches, and its **probes** now resume. Wait **past another full boundary** and check
   `status` again — **expect the row PRESENT with `age=` well under the limit**, i.e. the probe really is the refresh.

⛔ **FAILURE SHAPES:** the row still listed after the boundary in step 4 **while beacons are confirmed arriving** ⇒ the
beacon touch is still in the build · the row gone in step 4 but **also** gone after step 5 ⇒ the probe refresh is broken
too (that would be a regression in `presence_refresh_hosted_row`, not this fix) · `age=` frozen in step 5 ⇒ the row's
clock is not being stamped by the probe · no beacons visible in step 3 ⇒ **the test is void, not passed.**

### 17.2 — the per-row hosting view ([[B154]] (b)), and the REDIRECT kind

The `DIRECT`/`REDIRECT` distinction and the age are **device-only console text**; no automated gate reads a console
line. Run on a host that has hosted a mobile which then re-homed elsewhere.

1. Attach mobile M to host **H1**. On H1: `status` — **expect** `m[0] … local=<id> DIRECT age=<n>s/1500s`.
2. Bring host **H2** into range at a much stronger signal and attenuate H1 until M re-homes to H2 (H2's new-home
   breadcrumb is what converts H1's row).
3. On **H1**: `status`.

**Expected — the SAME row, now a redirect, with its age RESTARTED from the breadcrumb (§9.2):**

```
  hosting=1 mobile(s)
    m[0] hash=0x1A2B3C4D local=254 REDIRECT->77 age=3s/1500s
```

⛔ **FAILURE SHAPES:** still `DIRECT` after the re-home (the breadcrumb was not attributed) · `REDIRECT->0` (the new
home id was not carried) · **`age=` continuing from the direct row's value instead of restarting near 0** — that is
the §9.2 stamp not happening, and it means the redirect will expire early and black-hole stale senders instead of
redirecting them.

### 17.3 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail

⛔ **The §9.4 expired-id return is NOT a separate bench step.** All eight of its steps are natively asserted and
step 8 is mutation-proven (a local-id-anchored last-mile turns it RED). On metal it would require a 25-minute
absence, a second mobile taking the exact freed id, and a sender still holding a stale route — an arrangement whose
setup is harder to verify than its outcome, so a bench step for it would be a step that cannot honestly fail.
**17.1 step 4 is the metal-only part of it that CAN be checked: the id really is re-offered.**
⛔ **No step is owed for the candidate-freshness rule either** — it is native-covered, and its trigger is 25 minutes
of a candidate's silence, which is the same wall-clock problem with no console surface of its own.

## Part 18 — §MH-S5b the weak/missed-home canvass and the verified-echo switch (2026-08-11)

⚠ **M2 SCOPE, stated so nothing else is added here.** The native suite asserts the probe KIND at all four quality
tiers, the missed-check trigger, the local-refusal exclusion, the per-tier byte count, the host-row refresh with its
epoch/redirect/expired/custody arms, and the verified-echo requirement — 13 mutations, all RED. What no automated gate
reaches is **real wall-clock time** (the 8-minute strong-tier period), **real RF attenuation** (a genuine weak home
next to a genuine stronger one) and **a sniffer's view of the byte that changed**. Those three, and nothing more.
⛔⛔ **AMENDED 2026-08-11 BY §MH-S5b-ii: §8.3's TRIGGER 1 (a weak home canvassing) IS DEFERRED UNDER [[B178]], SO 18.2
NOW PINS THE DEFERRAL RATHER THAN THE CANVASS.** Nothing is added and nothing is deleted — 18.2 is rewritten in place,
and it gains one step because "weak but answering" and "not answering" are now DIFFERENT expected outcomes and a bench
run must be able to tell them apart. ★★ **The residual policy it verifies on metal: a weak but consistently responding
home is never canvassed, so M changes home only AFTER connectivity begins failing — a CONSERVATIVE INTERIM POLICY, ⛔
NOT completed proactive roaming.**

### 18.1 — ★★ THE ≈8-MINUTE STRONG-LINK IDLE-LOSS BOUND IS THE ACCEPTED TRADE-OFF, NOT A FAULT (spec §12.3-8)

⚠ **RUN THIS AT THE REAL DEFAULTS — the point is the wall clock.** Budget ~10 minutes of doing nothing.

1. Attach mobile M to host H at a **strong** signal. On M: `mobile status` — confirm the reported tier is `strong`
   and the home link is `confirmed` with a small age.
2. Walk M out of range (or attenuate to zero) **immediately after** a confirmation, and generate **no traffic**.
3. Watch `mobile status` every minute.

**Expected — the confirmation AGE GROWS monotonically and nothing claims connectivity, then loss is declared at
≈8 min 15 s (495 s: 480 s at the strong tier + three 5 s retries, plus up to 8 s of jitter per deadline ⇒ ≤ 527 s):**

```
mobile home link: confirmed  age=310s      <- growing, never "connected"
mobile home link: checking   age=495s
!! home lost after 3 unanswered presence probes
```

⛔ **FAILURE SHAPES:** loss declared in well under 8 minutes (the strong-tier period was shortened — that spends
battery on every healthy mobile in the fleet, and it must be argued as a design change, not filed as a fix) · loss
never declared · the panel showing an unqualified "connected" at any point while the age grows.

### 18.2 — ★★ A HEALTHY HOME IS KEPT SILENTLY; **A WEAK-BUT-ANSWERING ONE IS *ALSO* KEPT SILENTLY** (trigger 1 DEFERRED); ONLY A **MISSED CHECK** CANVASSES, THEN SWITCHES (spec §12.3-7)

⛔⛔ **REWRITTEN IN PLACE 2026-08-11 BY §MH-S5b-ii — READ THIS BEFORE FILING A FAILURE.** Step 2 previously read
*"attenuate H1 until its roster reports M as `weak`. Expected: M's next P-probe has byte 1 = 0"*. **That expectation is
WITHDRAWN: §8.3's trigger 1 is DEFERRED under [[B178]]** (measured at −6 unique deliveries, all in `s07`, from a
fleet-wide roster storm). ⇒ **a weak home that still ANSWERS is now probed SELECTED, and a searching probe there is a
FAILURE, not a pass.** ★★ **The limitation this pins: the mobile changes home only AFTER connectivity begins failing —
a conservative interim policy, ⛔ NOT completed proactive roaming.**

Needs **two** hosts on the same PHY and a sniffer. The measurable byte is P-probe **byte 1**
(`selected_home_id`): non-zero = selected, **0 = searching**.

1. M attached to H1, healthy. Bring H2 into range at a **much stronger** signal. Leave it there for >6 minutes.
   **Expected:** `mobile status` on M lists H2 under verified candidates, `hosting` at H1 is unchanged, and the
   sniffer shows **every** P-probe from M with byte 1 = **H1's id** — ⛔ **zero searching probes and zero extra
   frames from M.** That is "adequate before optimal", and a single searching probe here is the failure.
2. ★★ Now attenuate **H1** until its roster reports M as `weak`, **but keep H1 ANSWERING** (its rosters must still
   reach M — check `mobile status` shows a `weak` tier with a small home-link age).
   **Expected — THE DEFERRAL:** `mobile status` on M shows the tier `weak`, the check period drops to **60 s** (from
   120 s at `ok`), and the sniffer STILL shows **every** P-probe from M with byte 1 = **H1's id**. ⛔ **NO searching
   probe, NO re-home, and H2's rosters carry no echo of M** — H2 is never asked.
3. ★★ Now make H1 **stop answering** (attenuate further / power it down) so M's checks go unanswered.
   **Expected — TRIGGER 2:** M's FIRST probe is still selected (it is what books the miss), and the **retry ≈5 s
   later has byte 1 = 0**. H2 answers with a roster carrying an **echo** of M's hash, and once the 60-second hold and
   the 5-minute dwell are served M re-homes to H2. (⚠ With H1 powered down its `REDIRECT->H2` row (17.2) cannot be
   read; to see that, restore H1 after the switch.)
4. On H1 while it is still answering and M is canvassing (i.e. H1 attenuated but alive, mid-ladder), `status` must
   show `m[0] … DIRECT age=` **restarting**, not frozen — M's own **searching** probes refresh the row it still
   lives in.

⛔ **FAILURE SHAPES:** searching probes while H1 is healthy (the airtime hole) · ★★ **a searching probe, or a re-home,
while H1 is WEAK BUT STILL ANSWERING** — that means trigger 1 came back and [[B178]]'s deferral is broken · no
searching probe on the retry after H1 stops answering (the canvass never fires ⇒ the switch is unreachable) · a switch
with **no** echoing roster from H2 on the sniffer (a one-way beacon decided it) · H1's `age=` climbing past the
boundary and the row being evicted while M is still attached and probing.

### 18.3 — ⓘ WHAT IS **NOT** OWED HERE

⛔ **No step for §8.3's trigger 3** (an attributable home-path failure) — it is not implemented; it is a separate
slice. ⛔ **No step for §8.3's trigger 1 as a POSITIVE either** — it is DEFERRED ([[B178]]); 18.2 step 2 pins its
ABSENCE, and a step asserting it fires would fail by design until the refined trigger lands. ⛔ **No step for the
stale-epoch refusal**: producing a genuinely stale row at an old home while the mobile canvasses needs the same
two-host arrangement as 18.2 plus a 25-minute wait, and 18.2 step 4 already checks the half that has a console surface.

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
  That is a **build** figure, not a bench check.
  ✅ **CORRECTED IN PLACE 2026-08-10 (§HYBRID-RTS-S6) — THE SWEEP IS NO LONGER OWED AND THE BOARD NUMBERS EXIST.**
  This bullet used to end *"the board sweep is still OWED and no board RAM number has been measured, so watch for an
  allocation failure at boot on the tightest env until it has been"*. **All 10 envs now link clean in both trees**, and
  ΔRAM is **exactly +296 on every single-layer env and exactly +592 on every dual-layer env** (`gateway`,
  `gateway_heltec`, `gateway_esp32s3` — `is_gateway ≡ n_layers == 2`), on three toolchains with no cell disagreeing.
  ⛔ **AND THE OLD CAUTION NAMED THE WRONG BOARD:** the tightest env in the fleet is **`gateway` at 82.7 % RAM**
  (nRF52840), not `gateway_heltec` (73.4 %) — and `gateway` is one of the three paying `+592`.
  ⇒ ⚠ **The residual bench-relevant fact, and it is the only one:** `gateway` **links**, and a link is not a boot.
  If a `gateway` board fails to come up after this arc's reflash, RAM exhaustion is the first hypothesis, not the
  radio. ⛔ There is no console line to check for it in advance, so no numbered step is written here.

## Part 19 — §UI-7D slice B: the on-device inbox detail/delete modal (2026-08-13)

⚠ **M2 SCOPE, stated first so nothing else is added here.** Every gesture meaning, the `(InboxKind, seq)` identity
rule, the 42-character paging, the 2 s cadence, all three `Inbox::erase` outcomes and the `long_arm` close are asserted
by the native suite (32 mutations, all RED), and the whole device half — the `(kind, seq)` lookup over the real
`pull()`, the in-callback body copy, the one `erase()` call and the panel's strings — is measured against a REAL
`meshroute::Inbox` by `tools/probe_firmware_ui/` (13 controls for this slice, all RED). ⇒ **only three things here are
beyond every automated gate: the real SSD1306's 21 columns, real wall-clock paging, and the fact that the ESP32 store
is RAM.**

### 19.1 — ★★ THE DELETE IS REAL *WITHIN THE RUNTIME*; ACROSS A REBOOT THERE IS NOTHING LEFT TO TEST ([[B134]])

⛔ **This is the step that exists because the automated gates CANNOT reach it: the panel's store is a volatile RAM
ring on `heltec_v3`, so the firmware's `erased` means "gone from every future pull IN THIS RUNTIME" and nothing more.**

⛔⛔ **AND READ THE NEXT SENTENCE BEFORE STEP 6, BECAUSE THE FIRST DRAFT OF THIS PART GOT IT BACKWARDS AND WROTE A
STEP THAT COULD ONLY PASS VACUOUSLY.** On ESP32 the backing store is `FixedInboxStore` — a RAM ring
(`lib/core/fixed_inbox_store.h`: `Slot _slot[Slots]`, `persisted_next_seq()` returns 0 *"volatile: no backstop -> seq
restarts at 1 each boot"*), and `fw_main.cpp` draws a **fresh random `storage_epoch` on every boot** precisely because
*"the volatile store lost its history"*. ⇒ **after a power cycle NEITHER the record NOR its tombstone exists: the WHOLE
inbox is empty.** So a deleted message **cannot come back** — and *"it is still deleted after a reboot"* passes for the
wrong reason, on a board where every message is gone. ⇒ ⛔ **DELETE DURABILITY ACROSS A REBOOT IS NOT TESTABLE ON
HELTEC AT ALL; it is owed by a DURABLE backend (nRF52 + QSPI), and Part 11.4 is already the control that pins the
volatility itself.** ⚠ The retired step is kept here as the record: it read *"POWER-CYCLE THE BOARD … the deleted
message COMES BACK. That is [[B134]] and it is expected"*. **That was false, and it is exactly the
instrument-that-cannot-fail class this arc has 23 recorded instances of — reached, this time, in a bench step.**

1. On a V3 with at least two stored messages, send yourself two DMs and one team-channel post so the INBOX screen
   lists them. Confirm over USB with `pull_inbox` and note each record's `seq` **and** its kind.
2. Cycle to INBOX, highlight a message, **double press**. Expected on the panel, exactly:
   `DM from <origin>` (or `CH<n> from <origin>`) on the header row with `1/1` at its right, the body on the next two
   rows, then `>back` and `  delete`. ⛔ **`>delete` must NOT be the selected row.**
3. `pull_inbox` again: the record is **still there**. ⛔ Opening deletes nothing.
4. **Short press** → the marker moves to `delete`. **Double press** → the modal closes to the list.
5. `pull_inbox`: that record — and **only** that record — is gone. ★ **If the DM and channel stores both held that
   `seq`, the other one MUST still be listed.** That is the one thing this step is really for.
6. ⛔⛔ **DO NOT "verify the delete survived a reboot" HERE — there is nothing to survive.** What you may check, and
   only as the [[B134]] product fact, is Part 11.4's control: power-cycle, `pull_inbox 0 0` → **ZERO lines and a CHANGED
   `epoch`**, because the whole RAM ring went with the power. ⛔ If any message at all survives, the durable store has
   been wired on ESP32 (a real change — say so) or the node did not reset, and **steps 1-5 are void until this
   behaves**. ★ **The delete-survives-reboot criterion (spec §6.2) is owed by a DURABLE backend — nRF52 + QSPI — and
   is UNTESTABLE on this board.** ⇒ record "n/a, volatile store" against it rather than a pass.

### 19.2 — the panel's own 21 columns, and the 2 s page turn on real time

1. Send yourself a DM of **more than 42 characters** (e.g. 100), open it, and watch without touching the button.
   Expected: the header's indicator reads `1/3`, then `2/3`, then `3/3`, then **back to `1/3`** — about **2 s** each.
2. ★ **Nothing may be clipped on the header row.** Worst case is `CH255 from 255 6/6` = 18 of the 21 columns; if the
   panel truncates it, the font or the padding has changed and `mrui::inbox_detail_head` is where to look.
3. ★★ **Leave it paging and do not press anything: after ~15 s of no input the modal closes back to INBOX and the panel
   blanks as usual.** A page turn deliberately does NOT postpone that. ⛔ If the modal survives indefinitely on a long
   body, the page advance is refreshing the inactivity deadline and the fix is in `UiModel::on_tick`, not here.
4. Send yourself a message with non-printable bytes if you have a way to (or an E2E-ack receipt, which has **no body**
   at all). Expected: `.` in place of each unsupported byte, an empty body renders as a blank two rows, and the
   indicator still reads `1/1`. ⛔ Never a blank screen and never a wedged modal.

### 19.3 — the emergency interplay, which is the safety half

1. Open a message, **short press so `delete` is selected**, then **hold the button** (long press).
2. Expected: the modal is **gone before** the `RELEASE!` overlay appears — and when you release early to CANCEL the
   arm, the panel returns to the **INBOX list**, ⛔ **never to the modal with `delete` still selected.**
3. Re-open the message: it must start on `>back` again.

### 19.4 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail

⛔ **No step for the `DELETE FAILED` (`io_error`) path.** On a V3 the RAM ring's append cannot fail short of the
32-tombstone cap, so provoking it on metal means filling that cap deliberately — an arrangement harder to verify than
its outcome. It is natively asserted (the modal stays open, says `DELETE FAILED`, and the record is proved still
present) and mutation-proven in both directions.
⛔ **No step for the unread counters.** That a completed detail frame does not clear them is a pure-model property with
its own native case; the panel gives it no separate surface.

## Part 20 — §UI-14: the SETTINGS screen, and the ONE thing no automated gate can reach — REAL `/mrcfg`

⚠ **M2 SCOPE, stated first so nothing else is added here.** Every gesture meaning, the row table with both conditional
rows, `short`'s two modes, the four action landings, the three markers' bytes and the `long_arm` editor close are
asserted by the native suite (29 cases; 18 new mutations, all RED). The device half — the menu's rendering, the draft
marker on STATUS, the editor's bracket, `SAVED` / `SAVE FAILED`, the one write and the live apply — is measured by
`tools/probe_firmware_ui/` against a **fake** store (8 new controls, all RED), in **both arms of the BLE-row
condition**. ⇒ **what is beyond every gate is exactly one thing, and it is the whole point of this part: the REAL
`/mrcfg` record — that a save reaches flash, survives a reboot, preserves the fields the editor does not know about,
and does not corrupt the record when the power is cut mid-write.**

⛔⛔ **UNTIL THIS PART RUNS, [[B193]] IS NOT DISCHARGED AND NOTHING MAY CLAIM THE STORAGE IS SOUND.** The service and
its bindings are proved against fakes; a green suite says the LOGIC is right, never that the storage is.

⛔⛔ **PRESS SEQUENCES CHANGED 2026-08-20 ([[B232]], owner ruling). READ THIS BEFORE ANY STEP BELOW.** SETTINGS now
**lands CLOSED**: the body shows ONE row, `>ENTER SETTINGS`. ⇒ **every "walk to `<row>`" in this Part begins with a
`double` press on that row to open the menu.** A `short` on the closed view **passes the screen** — that is the
ruling, not a fault. ⛔ **Fail if cycling past SETTINGS costs more than ONE short press.** ⓘ Leaving the menu — the
`BACK` row, or walking off the last row — returns you to `>ENTER SETTINGS`, **not** to STATUS; one further `short`
then leaves the screen. ⓘ When the service cannot open, the closed view shows the unavailable state and a `double`
**stays closed** (the QG-caught arm).

### 20.1 — the menu, and that a draft is RAM only

1. Cycle past SEND (or past INBOX on a `gateway_heltec`): the fifth screen is SETTINGS.
   ⇒ Expected body: a single row `>ENTER SETTINGS`. ⛔ **No menu rows may be visible before you `double`**
   ([[B232]]). Press `double`: the menu opens on its first row.
   ⛔⛔ **RETARGETED 2026-08-16 (§CHROME-4, QG): THIS STEP READ *"the fifth screen's title row reads `SETTINGS`"* AND
   THAT IS NOW FALSE — §7.2 REMOVED the standalone title and gave the row to the menu.** ⇒ **you are on SETTINGS when
   the NAVIGATION RAIL's fifth (bottom) slot carries the one-pixel selection frame**, per Part 25.1. The withdrawn
   wording is kept visible rather than deleted (§3 rule 3).
   ⛔ **There must be NO `BLE` row** — the UI-12 transport is not compiled on any ESP32 env. If one appears, the
   build set `MR_UI_BLE_ROW` and the rest of this part is about a different menu.
2. Expected rows **once the menu is open** ([[B232]]), in order, three at a time as you `short`-press: `DM crypt`,
   `key attach`, `auto reg`, `PROVISION`, `SAVE`, `DISCARD`, `BACK`. ⛔ **A `short` past `BACK` (the last row)
   returns to `>ENTER SETTINGS`** — it does not leave the screen. Each of the first three shows its value
   (`off` / `on`) to the right of the label.
   ★ Compare them with `cfg` over USB: **the panel's values must equal the persisted ones**, not the live ones.
3. Highlight `DM crypt`, **double press**: the value becomes **bracketed** (`>DM crypt   [off]`). **Short press**: it
   flips to `[on]`. ⛔ The cursor must NOT move to the next row while bracketed — that is the whole of `short`'s two
   modes. **Double press** accepts and the bracket goes away.
4. ⛔ **Nothing has been saved.** Over USB, `cfg` still reports the OLD value, and the title row now reads
   the marker row **`CFG* UNSAVED`** — ⚠ **RETARGETED 2026-08-16 (§CHROME-4): this read `SETTINGS CFG* UNSAVED`.**
   The **marker is RETAINED** (§6: the badge *"may replace the STATUS decoration; it may never replace the
   instruction"*); only the `SETTINGS` **title** is gone (§7.2), so the row now carries the marker alone.
   ★ The SETTINGS rail slot's badge gains its dot at the same moment — see Part 25.4.
   ★ ⛔ **[[B232]]: walk out to `>ENTER SETTINGS` and confirm `CFG* UNSAVED` is STILL on the panel there.** An
   operator who never opens the menu must still be able to read the remedy — design §6's forbidden state is an
   icon-only error.
5. Cycle to **STATUS**: ⛔⛔ **RETARGETED 2026-08-16 (§CHROME-4) — the withdrawn step read *"it reads
   `STATUS CFG* UNSAVED` on its title row"*, and §6 REMOVED that decoration together with the STATUS title.**
   ⇒ **the unsaved state is now visible from every ordinary screen as the SETTINGS rail slot's BADGE** (Part 25.4).
   ⛔ The STATUS body must NOT carry the marker; if it does, §6's removal was reverted. ⛔ **The status STRIP must not be shortened
   or overdrawn to make room** — if it is, the marker is being drawn in the wrong place.
   ⓘ The strip replaced the packed `DM… CH… T…/… …V` bar in §CHROME-3; the marker keeps its own title row, and §6's
   move of this decoration onto the SETTINGS rail icon is **slice 4**, not done here.
6. ★★ **Leave the board alone for ~20 s so the panel BLANKS, then press once to wake it.** The marker is still there
   and the value is still edited. ⛔ **A draft may never be discarded because attention timed out.**
7. Walk to `BACK` and double-press: ⛔ **CORRECTED 2026-08-20 ([[B232]]) — the withdrawn step read *"the panel
   returns to STATUS"*.** The panel stays on SETTINGS showing `>ENTER SETTINGS`, **with the marker still up**. One
   further `short` leaves for STATUS. Cycle back into SETTINGS and `double`: the edited value is **still edited**.
   ⛔ Re-entering must not reset the draft. ⓘ The menu re-opens on its **first** row, not on `BACK`.

### 20.2 — ★ THE SAVE THAT REACHES REAL FLASH, AND WHAT IT MUST NOT DESTROY

1. Before saving, record over USB: `cfg` (note `e2e_dm`, `intro_attach`, `mobile_autoregister`, `ble_mode`) **and**
   at least two fields this editor does not cover — `status` for the node id / team id, and the channel counter.
2. With the draft still standing, walk to `SAVE` and double-press. Expected on the panel: **`SAVED`**, and the
   the `CFG* UNSAVED` marker row **gone from SETTINGS** and the **rail badge back to its plain tools glyph**.
   ⚠ **RETARGETED 2026-08-16: this read *"gone from both SETTINGS and STATUS"*** — STATUS no longer carries a
   marker to lose, so the badge is what proves the clean state from an ordinary screen.
3. `cfg` over USB: the edited field now holds the NEW value. ⛔ **And every non-covered field is unchanged** — same
   node id, same team id, same channel counter, same radio floor. ★ **This is the step the fakes cannot do**: the real
   record is a whole `mrnv::Blob`, and a save that rebuilt it instead of reloading it would revert a leased counter.
4. **Power-cycle the board.** `cfg` again: the saved value **survives**, and so does everything in step 1. ⛔ If the
   value reverts, the write did not reach flash and the panel's `SAVED` was a false claim — stop and report.
5. Repeat the same edit and press `SAVE` again **without changing anything**: expected **`NO CHANGE`**, and ⛔ no NV
   write at all. (`/mrcfg` coalesces a byte-identical record — the flash-wear guard.)

### 20.3 — DISCARD, and the conflict the companion can cause

1. Edit a value, then walk to `DISCARD` and double-press. The marker clears and the panel's value returns to the
   persisted one. ⛔ `cfg` over USB must show **no change at all** — a DISCARD writes nothing.
2. ★★ **THE CONFLICT, which needs the USB console and the panel at the same time.** Open SETTINGS and edit
   `DM crypt` (leave it unsaved). Now, over USB, run `cfg set intro_attach 0` (a DIFFERENT covered field).
3. ★★★ **DO NOT TOUCH THE BUTTON. Within one repaint the panel must change to `CFG! RELOAD` BY ITSELF** — on the
   SETTINGS title and on STATUS. ⛔ **If it only appears after you press something, the notification hook is not
   firing and the marker is being discovered late** (`mr_ui_on_config_saved`, `src/firmware_ui.cpp`; the repaint comes
   from its `mark_dirty()` — without that the conflict is true and invisible). ⓘ This is the one step here that the
   automated gates get closest to and still cannot finish: the probe proves the hook repaints, but only real hardware
   proves the real `cfg set` reaches it.
4. Walk to `SAVE` and double-press. Expected: **`CFG! RELOAD`** again, ⛔ **and no write** — `cfg` over USB still shows
   your console value, not the panel's draft. ★ This is the step that proves the companion's change cannot be
   silently overwritten.
5. ★★ **THE REVERT CASE, and it is the reason the notification has to be immediate rather than eventual.** With the
   conflict standing, put the console value BACK (`cfg set intro_attach 1`). The persisted bytes now equal the panel's
   baseline again. ⛔ **`CFG! RELOAD` MUST STILL STAND, and SAVE must still be refused** — the record was touched
   twice and the operator has not acknowledged it. ⓘ A save-time byte comparison alone cannot see this, which is
   exactly what the hook exists for.
6. ⓘ **The negative half, worth one minute:** over USB run a NON-covered write (e.g. `cfg set beacon_ms 30000`).
   ⛔ **No marker may appear** — only `ble_mode`, `e2e_dm`, `intro_attach` and `mobile_autoregister` are covered.
   And run a live-only key (`cfg set nav 1`): likewise nothing, because nothing durable moved.
7. A **`RELOAD`** row has appeared above `SAVE`. Walk to it and double-press. Expected: the conflict clears, **your
   edited field keeps the panel's value**, and the field you changed over USB **keeps the console's value**.
8. Walk to `SAVE` and double-press: **`SAVED`**. `cfg` now shows both — the panel's edit and the console's change.

### 20.4 — the reboot-class field, and the row that stays until the reboot

⚠ **Only reachable on a build whose BLE transport is compiled** (`MR_UI_BLE_ROW=1`), which no env sets today. ⇒
**record "n/a, no UI-12 transport" unless you are testing such a build.** If you are: save a change to the `BLE` row,
and expect **`SAVED`** plus a **`RESTART NEEDED`** row that stays on STATUS until the node is rebooted — and ⛔ **no
`CFG* UNSAVED`**, because it IS saved. The two are independent facts.

### 20.5 — ⛔⛔ THE RESET-DURING-WRITE CHECK, which is why [[B193]] is still open

1. Set up a real edit and get the cursor onto `SAVE` — **`double` on `>ENTER SETTINGS` first** ([[B232]]).
2. Press `double` and **cut the power within a second** (pull USB / hit reset). Repeat this ~5 times, varying the
   delay.
3. On each reboot, `cfg` over USB must report **either the complete OLD record or the complete NEW one** (§3.6.5).
   ⛔ **Never a half-written record, never a version/magic rejection, and never a node that comes up unprovisioned.**
4. ⛔ If any run comes up with defaults, the `/mrcfg` write is not atomic against a reset on this platform and that is
   a REAL finding — stop, record the exact repro, and do not "fix" it from the UI layer.
5. ⓘ Also worth recording while you are here: nothing above proves flash WEAR. `cfg set` and this panel share one
   record and the store coalesces identical writes, so the wear question is about how often the operator saves; it is
   not answered by this part.

### 20.6 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail

⛔ **No step for `PROVISION`.** The row is rendered and REFUSES (`PROVISION: UI-15`); pressing it is a one-line check
that the native suite already makes, and there is nothing behind it.
⛔ **No step for the `SAVE FAILED` path.** Provoking a real NV write failure on a V3 means arranging a broken
filesystem, which is harder to verify than its outcome; it is natively asserted (the draft AND the marker survive) and
mutation-proven, and the probe drives it end to end against a failing fake.
⛔ **No step for `CFG UNAVAILABLE`.** The device store's `load()` is the §nv-ritual, which load-or-seeds — so on
hardware it cannot fail, and the state is unreachable by construction rather than untested.

### 20.7 — §notify-every-save ([[B194]]): the OTHER six verbs, added 2026-08-13

⚠ **M2 SCOPE.** §20.3 step 3 already proves the hook repaints for `cfg set`. What no gate reaches is that the SIX
other user-initiated `/mrcfg` verbs reach the same hook **on real hardware**: their call sites are in
`src/firmware_config.cpp`, which neither the native suite nor the simulator compiles, so the only automated instrument
is a source-level wiring check (`tools/probe_board_ui/` W14-W19). ⇒ **exactly two steps, and only the first is a
must-run.** ⛔ **Do this on a spare/re-provisionable node — step 1 wipes the network provisioning.**

1. ★★ **`leave` — the blocker this rule was written for.** Open SETTINGS, edit `DM crypt` and leave it unsaved
   (`SETTINGS CFG* UNSAVED`). Now, over USB, run `leave`. Expected console line:
   `> left network (kept freq=<n.nnn>) — idle; \`join\` to re-provision (live)`.
   ⛔ **DO NOT TOUCH THE BUTTON.** Within one repaint the panel must change to **`CFG! RELOAD`** by itself — `leave`
   rebuilds the record from a zeroed blob, so **all four covered fields were just reset to 0** under the draft.
   Then walk to `SAVE`: expected **`CFG! RELOAD`** again and ⛔ **no write** (`cfg` still shows the wiped values).
   Walk to `DISCARD`: the marker clears and the panel now shows the values `leave` left.
2. ⓘ **The negative half, one minute, optional.** Re-provision with `join layer=… freq=… bw=… sf=…` (expected: the
   `join_started` JSON line). With a fresh SETTINGS draft open, run it again: ⛔ **no marker may appear** — `join`
   assigns none of the four covered fields, so the notification it now sends must raise nothing. ★ That is the half
   that proves the systematic rule is self-limiting rather than merely loud.
3. ⓘ **Not owed here:** `gateway`, `create`, `team` and `password` take the same code shape as `join` (a save whose
   verdict is checked, then the hook) and are pinned individually by W14/W16/W17/W19 with their controls. Running all
   four on metal would re-test the corpus, not the residue — record "covered by W14-W19" unless one of them is being
   changed.

## Part 21 — §T2 hardware TX-completion diagnostics (2026-08-14)

T2 adds no panel state or string. Its metal-only residue is the real SX1262 completion path and its four status
counters; native tests substitute a mock radio and the simulator does not produce these outcomes.

1. Over **USB serial** (the human text console), after boot run `status`. Then exercise the existing queue-stress command
   `testsend <dst> 200 @sendms 20`, let the queue drain, and run `status` again.
2. Expected: all four diagnostics are present on every read, including when zero:

   ```
   … txq=<n> txdrop=<n> txfail=<n> txoutdrop=<n> … txto=<n> …
   ```

   `txdrop` counts synchronous queue-full admissions; `txfail` counts queued frames whose radio arm failed;
   `txto` counts started transmissions whose TxDone was not observed; `txoutdrop` counts completion reports lost
   because the four-entry outcome ring was full. ⛔ Do not assume `txoutdrop=0`; record the value.
   ⛔ The BLE/JSON status contract currently omits `txfail`, `txoutdrop`, and `txto`. Do not use a BLE/JSON response
   as evidence for this T2 metal check — use USB serial.
   ⛔⛔ **CORRECTED 2026-08-14: this line said *"extending that contract is deferred to T3"*. §T3 IS IMPLEMENTED AND
   DID NOT EXTEND IT** — T3 added an app *push* (`send_aired`), not a status field. ⇒ the omission is **still open and
   is now unassigned**; it belongs to whichever slice takes the BLE/JSON status contract, and this check stays
   USB-only until then.
3. Pass: the fields are always present, the node remains responsive after the burst, and `txq` returns to 0.
   Fail: any field is omitted at zero, `txq` remains wedged, or a non-zero counter is silently reset between reads.

## Part 22 — §T3 the app/UI half of the TX-completion arc (2026-08-14)

⛔ **THIS IS THE ONLY PLACE `send_aired` HAS EVER FIRED FOR REAL.** Neither host build has a radio and the simulator
has no TxDone edge at all, so the whole chain — a physical airing becoming a panel word — exists on metal and nowhere
else. ⚠ **T3's BLE/JSON status residue from Part 21 is UNCHANGED and still open:** T3 added an app *push*, not a
status field, so `txfail` / `txoutdrop` / `txto` remain USB-console-only. Use USB serial here too.

### 22.1 — the console line the core's push produces

1. Over **USB serial**, with a second node in range, send a DM: `send <id> "aired probe"`.
2. Expected, in order, on the sender's console:

   ```
   AIRED ctr=<n> dst=<id>
   ACKED ctr=<n>
   ```

   ⓘ `AIRED` may be interleaved with other traffic, and it may repeat if the MAC retransmits the flight — a repeat is
   expected and is not a fault. ⛔ **What must NEVER happen is `ACKED`/`E2EACK`/`FAILED` for a ctr that produced no
   `AIRED` at all** while the frame demonstrably went out: that is the completion never reaching the app.
3. Repeat with a canned/console **channel** post (`send_channel 0 "aired probe" -t`). Expected: **`AIRED ctr=<n>
   dst=0`** — ★ `dst=0` is the channel form, and `<n>` is the FULL origination handle the `ack:queued ctr=<n>` line
   reported, ⛔ **not** its low byte. On a node that has posted more than 255 times those two differ, and a mismatch
   there is the §b40 truncation defect.
4. Pass: `AIRED` appears for a locally originated DM and a locally originated channel post, carrying the same `ctr`
   the acceptance line reported.
   Fail: no `AIRED` at all; an `AIRED` for a channel post carrying a truncated `ctr`; or an `AIRED` on a node that is
   only **relaying** somebody else's traffic (⛔ a relay owns no origination and must print none — leave a third node
   forwarding for a minute and check its console stays silent of `AIRED`).

### 22.2 — the panel word, and the failure shape that is new

1. On the OLED node: `short` to **SEND**, `double`, `double` on `Got your message` (bench guide **H7-01**).
2. Expected: `SENDING...` → (possibly a brief **`QUEUED`**) → **`SENT, waiting`** → within ~36 s `PICKED UP` or
   **`NO RELAY HEARD`**.
   ⛔ **`QUEUED` may be too brief to see and its ABSENCE IS NOT A FAILURE.** Do not gate this check on observing it.
3. ⛔ **THE NEW FAILURE SHAPE: the panel STUCK on `QUEUED`** while the console shows the post really went out. It
   means the airing never reached the app — the loop's completion drain, the core ownership rule, or the UI
   correlation. Report it with the console transcript.
4. ⛔ **THE OLD FAILURE SHAPE IS UNCHANGED: the panel stuck on `SENDING...`** is still the §B113 regression.
5. Repeat for a DM (guide **H7-03**): `SENDING...` → (brief `QUEUED`) → `SENT, waiting` → `DELIVERED to <label>`.
6. ★ **THE ONE-SIDED CHECK THAT IS WORTH MORE THAN THE HAPPY PATH: power the second node OFF** and post again.
   Expected: `QUEUED` → **`SENT, waiting`** (the frame still aired — nobody was listening) → within ~36 s
   **`NO RELAY HEARD`**. ⛔ If it never leaves `QUEUED` with no peer present, the airing report is gated on something
   it must not be gated on.

## Part 23 — §B197/§B198/§B200 the sleep/wake seam (2026-08-14, amended 2026-08-15)

⛔⛔ **AMENDED 2026-08-15 BY [[B200]]. READ THIS BEFORE RUNNING ANYTHING IN THIS PART.** The image these checks were
first written for **panics on demand**: the button's light-sleep wake was armed once at boot with a LEVEL-triggered
interrupt, so **holding the button storms the shared GPIO ISR and trips the Interrupt watchdog**. The fix arms the
wake immediately before each `esp_light_sleep_start()` and disarms it the instant the CPU wakes. Consequences here:
- **23.6 is NEW and is the reproducer** — run it FIRST, before anything else in this Part;
- ⛔ **23.1(b) MUST BE RE-RUN.** Its 2026-08-15 pass was measured with the wake armed **permanently**; per-sleep
  arming is a different configuration and that result **does not transfer**;
- **23.2 now has two possible lines and a second, sharper instrument** (the `wk_*` counters).

⛔⛔ **EVERY CHECK IN THIS PART IS METAL-ONLY BY CONSTRUCTION, AND ONE OF THEM IS THE DESIGN'S PRINCIPAL UNPROVEN
HARDWARE ASSUMPTION.** No host build sleeps, the simulator has no sleep pacing at all, and neither has an ESP32-S3.
The pure halves — `InputFsm::active()` and `mrui::ui_allows_sleep` — are under the native gate (nine mutations); the
wiring and the two ESP-IDF calls are under the two UI probes. What is left is exactly this.

⚠ **PRECONDITION FOR THE WHOLE PART: DO NOT SEND A CONSOLE BYTE AFTER BOOT.** A single byte latches `g_host_present`
and the node then never light-sleeps, so every check below would pass over a node that was simply awake. Use `team 0`
(no peers ⇒ almost no RX traffic ⇒ maximum sleep duty), wait past `MR_BOOT_GRACE_MS` = 30 s, and drive the node by
BUTTON ONLY. Read `slept=` only at the END, or from a second boot.

### 23.1 — ⛔⛔ THE COEXISTENCE TEST. Run it after 23.6; nothing else in this Part means anything if it fails

⚠ **ORDER, amended 2026-08-15: 23.6 comes first** — it is the reproducer for a panic, and there is no point testing
wake sources on a node that resets when you hold its button. Everything else still hangs on 23.1.

The button uses the **digital-domain** `gpio_wakeup_enable` + `esp_sleep_enable_gpio_wakeup()`; the radio's RxDone
uses the **RTC-domain** `esp_sleep_enable_ext1_wakeup` on DIO1, untouched by this slice. **Whether the two coexist in
ESP32-S3 light sleep is UNVERIFIED on this hardware.** ⇒ prove each INDEPENDENTLY; ⛔ neither result may be inferred
from the other.

1. **(a) BUTTON.** Boot, no console byte, `team 0`, wait 45 s (past the 30 s grace and the 15 s panel blank).
   Give **one short tap**. Expected: the panel lights within a fraction of a second and shows a normal screen.
   ⛔ **It must NOT be classified as a long press** — no `EMERGENCY` arming countdown may appear.
2. **(b) RADIO.** Boot a second node in range. On the sleeping node (again no console byte, past 45 s, panel dark)
   have the second node send it a DM. Expected: the message is received — confirm afterwards on the sleeping node's
   `inbox` or by its ACK arriving at the sender.
   ⛔⛔ **RE-RUN REQUIRED (2026-08-15, [[B200]]): the pass recorded below was measured with the GPIO wake armed
   PERMANENTLY at boot. The shipped firmware now arms it per sleep and disarms it on every wake, which is a
   DIFFERENT hardware configuration — the result does not carry over.** ★ And the re-run is now much stronger than
   before: read `status` afterwards and use the per-cause counters — **`wk_ext1=` non-zero is the DIO1 edge itself
   delivering the CPU**, which is precisely the attribution the note below says this check could not make.
   ✅ **(b) PASSED ON METAL 2026-08-15 [SUPERSEDED — SEE THE RE-RUN NOTICE ABOVE] — AND IT IS THE GO/NO-GO HALF, SO THE DESIGN PROCEEDS.** Conditions met as
   written: the node was **headless** (serial was lost when the port renumbered across a reboot, so ⛔ **no console
   byte was ever sent** and `g_host_present` stayed false), **in a team** (`team_id=0x75E7479F`, persisted in NV and
   restored by the reboot), and **~5 min up** — past both the 30 s boot grace and the 15 s panel blank, therefore
   light-sleeping. **Result: a DM from the peer node was received AND ACKED, with the ACK observed at the sender.**
   ⇒ **arming the digital-domain GPIO0 wake did NOT displace the RTC-domain DIO1 radio path**, and the full RX→TX
   turnaround completed in the sleeping profile. ⚠ **STATED HONESTLY — WHAT THIS DOES *NOT* ISOLATE:** with the sleep
   capped at `MR_MAX_SLEEP_MS` = 1 s the MCU wakes at least once a second anyway, so this does **not** prove the DIO1
   edge specifically delivered the CPU rather than the deadline timer. **This check was written to test that the radio
   path SURVIVES with GPIO wake armed, and that is exactly what it establishes** — a stricter edge-attribution test
   would need a sleep cap longer than the exchange, which is not configured here.
   ⛔ **(a) IS STILL OWED** — the short tap, on the *fixed* firmware, must light the panel and **not** be classified as
   a long press.
3. Pass: **BOTH** (a) and (b) succeed, separately.
   Fail: either one. ⛔ If (b) fails while (a) succeeds, the GPIO wake has displaced the radio wake and this design
   does not proceed as written — record it and stop; the fallback named in the design (§3.1.2) is **not authorised**
   and would need its own radio regression gate.

### 23.2 — the failure line, and the FAIL-CLOSED behaviour behind it

⚠ **AMENDED 2026-08-15 ([[B200]]): there is no longer any arming at boot, so this line — if it appears at all —
appears at the FIRST IDLE SLEEP ATTEMPT (past the 30 s grace), not in the boot banner.** There are now TWO lines.

1. At boot, watch the console. Expected: **nothing** about a button wake, ever. (A line here would mean somebody
   put an arm back into `mr_ui_init()`, which is [[B200]] itself.)
2. ⛔ If you ever see **either** of
   `!! OLED button wake unavailable; sleep disabled` (the platform refused to ARM) or
   `!! OLED button wake stuck armed; sleep disabled` (⛔ **the worse one** — it refused to DISARM, i.e. a running
   core carried an armed level interrupt), the node has **deliberately disabled idle light-sleep for that whole
   boot** — that is the fail-safe working, not a second fault. Confirm it: `status` must then show `slept=`
   **STUCK** however long the node idles, with `wkarmfail=` or `wkdisarm=` non-zero. Record the board and stop; a
   node that prints either line and still increments `slept=` is the one outcome this slice exists to prevent.
3. ★ **The line is said ONCE, not once per pass.** The arm runs on every idle service pass, so a repeating line is
   itself a defect (a USB-CDC flood on a node that is already broken).
4. ⓘ `status` gained **seven** fields beside `slept=` on the ESP32 envs — `wk_gpio= wk_ext1= wk_tmr= wkbusy=
   wkarmfail= wkdisarm= wksleepfail=`. **`wkbusy=` is NOT a fault** — it counts sleeps skipped because the button
   was physically held at the instant of arming, and ⚠ **it can legitimately stay at 0** (the UI gate already
   refuses to sleep while a gesture is being classified, so this counts only the tick-to-re-sample race).
   ⓘ **`wksleepfail=`** counts `esp_light_sleep_start()` REFUSING to sleep (`ESP_ERR_SLEEP_REJECT` /
   `ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION`); such a pass is deliberately **not** counted in `slept=`.

### 23.3 — §B198: a frame must render promptly, not over ~8 seconds

Measured on metal before the fix: *"each portion of screen took 1 s to refresh, so the whole screen took like 8 sec"*
— eight page pushes paced by an up-to-1 s sleep between service passes, **on the EMERGENCY screen**.

1. From the woken state of 23.1(a), with the MAC idle, press `short` to change screen and WATCH THE REPAINT.
2. Expected: the frame assembles in **well under 250 ms** — it should read as instantaneous, not as bands appearing
   one per second. ⓘ Time it against a phone stopwatch if in doubt; the pre-fix figure was ~8 s, so the two are not
   close and no precision instrument is needed.
3. ★ **Repeat it on the EMERGENCY screen, which is the reason this is safety-relevant, not cosmetic:** hold the
   button through `arm` and let the countdown run. The countdown digits must tick **once per second, all of them** —
   ⛔ digits skipped or a screen assembling in bands is the defect back.
4. ⛔ **AND THE THING THAT MUST NOT HAVE BROKEN: page chunking is still one page per service pass.** With a second
   node exchanging traffic, drive repaints continuously for a minute. Expected: the second node's RTS/CTS/DATA
   exchanges continue to complete. A full-frame repaint (~25 ms of blocking I²C against a 5 ms CTS→DATA gap) would
   show up as dropped frames on the second node, not as anything visible on the panel.

### 23.4 — sleep RESUMES, and the emergency timing is unchanged

1. After 23.3, stop touching the node. Wait for the panel to blank (15 s) and then a further 60 s.
2. Read `status` **once** (this ends the headless run, so do it last). Expected: **`slept=` has increased**.
   ⛔ `slept=` stuck at 0 on a board that printed no wake-unavailable line means the UI is holding the CPU awake for
   ever — check whether the panel is really blank and whether a gesture is stuck part-classified.
3. On a fresh boot, re-check the emergency timing: from the debounced press, `FIRE` must still be at
   **~3.5 s**, and `arm` at ~0.8 s. ⛔ Neither constant was changed by this slice; a shift means the tick cadence
   moved, not the thresholds.

### 23.5 — ⚠ GPIO0 IS THE BOOT STRAP: the recovery instruction, so it is not rediscovered in a panic

`MR_UI_BTN_PIN` is GPIO0, which the ESP32-S3 samples **only during RESET**. Arming it as a runtime wake source adds
nothing to that hazard, but the pre-existing one is unchanged: **a board reset while the button is held enters serial
download mode and looks bricked.**

⇒ **RELEASE THE BUTTON AND RESET AGAIN.** That is the whole remedy. It is a hardware behaviour, not a fault to report.

### 23.6 — ⛔⛔ [[B200]]: A LONG PRESS MUST NOT PANIC THE NODE. **Run this FIRST — it is the reproducer**

Metal, 2026-08-15, on the arm-once-at-boot image: **one long press produced `Guru Meditation Error: Core 1 panic'ed
(Interrupt wdt timeout on CPU1)` with `Core 1 was running in ISR context`,** then two more resets
(`WATCHDOG ran 0s`, then `ran 41s`) and `Re-entered core dump! Exception happened during core dump!`. It is the
cheapest possible reproducer and no automated gate in this tree can reach it: nothing here runs an ESP32-S3.

1. Boot, no console byte, `team 0`. **Immediately** hold the button for **10 seconds** — through the emergency arm
   and fire, and well past them. Expected: the panel behaves (arm → countdown → FIRE) and **the node does not
   reset**. ⛔ Any `Guru Meditation` / `Interrupt wdt` / `Core 1 was running in ISR context` is the defect back.
2. Wait past the 30 s grace and the 15 s blank so the node is light-sleeping. Now hold the button for **10 seconds**
   again — this is the case that panicked, because the arm was live and the level asserted. Expected: the panel
   lights, the gesture classifies, no reset.
3. Repeat step 2 **five times**, and after a **cold power cycle** once more. The original fault was reproducible on
   demand, so a single clean pass is weak evidence; five is the cheapest way to make it less weak.
3a. ⛔⛔ **THE TWO REBOOT CASES. THESE ARE THE ROUND-3 REPRODUCER — (B) IS THE ONE THAT FAILED ON THE ROUND-2 IMAGE,
    and they discriminate cleanly because one of them never arms the wake at all.**
    - **(A) never-armed control.** Power on, and within ~5 s type `reboot`. Up ~5 s is inside the 30 s boot grace, so
      the node **never light-slept and therefore never armed** anything. Hold the button as the first boot lines
      appear. Expected: **no panic.** ⓘ This one passed even on the broken image — it is the control that makes (B)
      mean something, not a test of the fix.
    - **(B) armed-then-reset.** Boot, **no console byte**, wait past 45 s so the node is light-sleeping. ⛔ **READ
      `status` AND RECORD `slept=` — THIS IS NOT OPTIONAL.** A non-zero, increasing `slept=` is the ONLY evidence
      that the wake was actually armed, and the entire diagnosis rests on that assumption. (Reading `status` ends the
      headless run; that is fine, the arm has already happened.) Now type `reboot`, and **hold the button as the
      first boot lines appear**. Expected: **no panic, and the banner completes** — including the
      `inbox = RAM volatile…` line, which is precisely where the round-2 panic cut the output.
    - Repeat **(B) three times**. ⚠ If it panics, capture the whole dump: the mid-banner cut point is the signal that
      an interrupt was configured *before* this boot's code ran.
4. Read `status` **last** (it ends the headless run). Expected: **`slept=` has increased** · **`wk_gpio=` has
   increased** after the sleeping-button test (that is the button itself delivering the CPU — the attribution the
   old 23.1(b) could not make) · `wkarmfail=0` · `wkdisarm=0` · **`wksleepfail=` RECORD THE VALUE — do NOT require
   zero.**
   ⛔⛔ **`wksleepfail=0` WAS DEMANDED HERE AND THAT WAS WRONG; IT IS WITHDRAWN.** ⚠ **It is the SAME MISTAKE as the
   `wkbusy` one directly below, made in the same replacement list** — an INFORMATIONAL counter asserted as a fault
   counter. **Both results it counts are LEGITIMATE, not hardware faults:** `ESP_ERR_SLEEP_REJECT` (a wake source —
   typically the button — asserted after the physical re-sample but before sleep entry, i.e. the residual race the
   re-sample narrows and cannot eliminate) and `ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION` (the deadline came too
   close, ordinary against the ≤1 s cap). ★ **The firmware already classifies them correctly — "did not sleep",
   NOT an arm/disarm hardware failure** — which is exactly why they have their own counter and do NOT touch the
   fail-closed latch. ⇒ **RECORD it; a non-zero value is not a failure. A RAPIDLY GROWING value merits
   investigation** (it would mean the node is repeatedly trying and failing to sleep).
   ⛔ **`wkbusy=` MAY LEGITIMATELY BE ZERO, and a non-zero value is not required.** ⚠ The earlier wording here
   demanded it be non-zero *"(you held the button across sleep attempts, which is what it counts)"* — **that was
   wrong and is withdrawn**: `mr_ui_allows_sleep()` already returns false while `input.active()`, so a held button
   normally stops the arm being **attempted at all**. `wkbusy=` counts only the narrow race in which the press lands
   between the UI tick and the physical re-sample, so a non-zero value merely shows that race guard was exercised.
5. `/mrfault` must show **no new** `HARDFAULT`/`WATCHDOG` record from this session.

⚠ **WHY (B) FAILED ON THE ROUND-2 IMAGE, so the check is understood rather than merely followed:** `gpio_wakeup_disable()`
clears only the pin's WAKE-ENABLE bit — verified in the linked IDF driver — so GPIO0 kept `GPIO_INTR_LOW_LEVEL` across
the disarm **and** across a CPU-only reset, and the next boot stormed as soon as RadioLib installed the shared GPIO
ISR. The fix clears the interrupt **type** as well, and scrubs both once at boot **before** the radio comes up (for
the case where the reset happened while still armed, so no disarm ever ran).

⚠ **WHAT ONLY THIS CHECK CAN SETTLE, stated so a green run is not over-read:** the wake-to-disarm window (the few
microseconds between `esp_light_sleep_start()` returning on a GPIO wake and `gpio_wakeup_disable()`) is a HARDWARE
TIMING claim. The pattern is ESP-IDF's own and the Interrupt WDT window is ~300 ms, so it should be safe by three
orders of magnitude — but **nothing in this repository can prove it**, and step 3's repetition is the only
instrument that exists.

### 23.7 — ★ RETAIN THE ELF FOR EVERY IMAGE YOU FLASH (the [[B200]] capture was undecodable)

The B200 panic dump printed `ELF file SHA256: 7a8aaa957` over a banner reading `nogit`. Neither identified anything:
`tools/git_rev.py` was wired to **one** env and not to the Heltec ones, so **every Heltec image ever flashed carried
`nogit`** — on exactly the boards that produce the field faults. `platformio.ini` now injects `GIT_REV` on
`heltec_v3` (and thus `heltec_mobile` / `gateway_heltec` through `extends`).

1. After building an image you intend to flash, **copy `.pio/build/<env>/firmware.elf` somewhere durable** and record
   its `sha256`. ⛔ `.pio/` is gitignored and any clean destroys it.
2. Record the banner's `rev=` alongside it. A `-dirty` stamp is honest but does **not** identify a revision — if the
   work is committed, rebuild and archive the clean-stamped image.
3. Decode a future backtrace with
   `xtensa-esp32s3-elf-addr2line -pfiaC -e <that firmware.elf> <addresses>`.
4. ⚠ An ELF decodes **only** the exact image built from it, and a `-dirty` stamp does not identify a revision; once
   the slice is committed, rebuild and archive the clean-stamped one.
5. ★★ **WHAT IS ARCHIVED RIGHT NOW (updated 2026-08-17), two directories, both to be kept until [[B196]] closes:**
   - ⛔ **`~/MeshRoute-artifacts/soak-20260816-1646/`** — the **PANIC** image (the defect). `firmware.elf` sha256
     **`d964a5239b568…`**, `COMMIT.txt` **`a1e53dd`**. The only ELF matching that panic, the provenance proof cited on
     the register row, and the artefact the fix's whole equivalence audit was measured against.
   - ★ **`~/MeshRoute-artifacts/soak-20260817-1002/`** — the **FIXED** image now under soak. `COMMIT.txt`
     **`cb76d793295492d81a519f78b3a4e78fd37f8ddc`** (`cb76d79`), `firmware.elf` sha256 **`3071ef553026e75a…`**,
     `firmware.bin` 1277936 B, banner `built Aug 17 2026 11:53:20 · cb76d79-dirty`.
     ★★ **Its identity is PROVEN, not assumed, by two independent checks:** the ELF contains the exact banner string
     `Aug 17 2026 11:53:20` (so it *is* the build that printed that banner), and its `DIRT.txt` — `git status --short`
     restricted to `src/ lib/ variants/ platformio.ini tools/` — is **EMPTY**, so ⇒ **the `-dirty` stamp is docs/other-
     workstream only and the binary corresponds to `cb76d79`'s firmware source.** ⓘ `DIRT.txt` is a **new** member of
     the archive triple; keep producing it — it is what turns a `-dirty` banner from an ambiguity into a measurement.
   ⚠ **CLOCK OFFSET, so it is not later misread as a mismatch: the build machine runs ~2 h ahead of the archive host**
   (archive mtimes 16:47 vs banner 18:43:50; 10:03 vs 11:53:20 — the same offset both times). **The banner string is
   the identity, never the file mtime.**
   ⓘ **The earlier `b196/` and `b200/` archives were DELETED 2026-08-17 on an owner instruction**, and the reasoning is
   worth keeping because it is the general rule: **`b200/` — B200 closed on metal and its backtrace was already decoded
   and recorded, so no future dump can arrive to need it**; **`b196/` — those two rebuilt ELFs matched NO flashed image
   (1276784 / 1276720 against a flashed 1275984) and the captures they were built for were fault-log records with no
   console backtrace at all, so there was nothing they could ever decode.**
   ⇒ ★ **THE RULE THIS LEAVES: an archived ELF is worth keeping exactly as long as a backtrace it can decode might
   still arrive. Archive at FLASH TIME (a rebuild is not the same image — measured), keep it while its row is OPEN,
   delete it when the row closes.** ⓘ [[B206]] is the related trap on the measuring side: two `__DATE__` TUs make an
   incremental rebuild's flash figures differ by ~16-32 B, so a rebuilt image is not even size-stable.

## Part 24 — §CHROME-3: the status strip and its repaint invalidation (2026-08-16)

⛔ **THE RESIDUE ONLY.** The projection, its formatters, the slot coordinates, the eight-page replay, the blanked/no-bus
sequence and the invalidation rule are all host-gated (`test/test_firmware_ui_chrome.cpp` + `tools/probe_firmware_ui`
P13 + `tools/probe_board_ui` W38-W40). What no host can reach is **pixels on glass**, the **real I²C timing**, and the
**power** the new per-tick work costs. Nothing else belongs here.

### 24.1 — the strip is LEGIBLE and nothing overlaps (design §3.1)

1. Boot `heltec_mobile` with the panel attached and cycle all five screens.
2. Expected, left to right on the top row: **envelope + count · house + compact age · people + count · key · battery
   outline + volts**, all above the existing full-width rule, and the body untouched below it.
3. ⛔ Fail on: any glyph overlapping its neighbour or the rule · a token running off the right edge · an icon that
   MOVES when a token's width changes (walk the mail count past `9` and past `99` — the four icons after it must stay
   put) · a smeared battery outline (that is the 11-px asset drawn at 7 px — its rows are two bytes).

### 24.2 — each slot agrees with the console, and claims nothing more

| slot | compare against | ⛔ must never |
|---|---|---|
| envelope | one DM + one channel post: `0 -> 1 -> 2`; opening a message does not clear it; ONE fully drawn INBOX list returns it to `0` | show a stored total |
| house + age | `mobile status`'s home-link state and confirmation age | read as "connected"/"online" — it is a **confirmation** age, and a team message or a foreign beacon must not refresh it |
| people | `routes` / the TEAM screen row count | claim members ONLINE; with **no team configured** it reads `--`, with a team and nobody heard `0` |
| key | `team_ch_key` present/absent | claim the node's own crypto identity is missing |
| battery | a multimeter, per **H9-02**'s one-sided window | show a percentage |

⚠ On `gateway_heltec` (an OLED build with **no** mobile plane) the house slot must be **BLANK — not a crossed house**:
"not applicable" is not a fault (design §4.2).

### 24.3 — ★★ IDLE SLEEP STILL WORKS WITH THE STRIP ENABLED (design §12.11 — the regression guard)

The strip adds per-tick work and a new invalidation path to the one subsystem that took five review rounds to
stabilise. A regression there presents **only** as `slept=` failing to climb: no panic, nothing visible on the panel.

1. Persist `team 0` **before** rebooting, so the node comes up with no peers.
2. ⛔ **Send NO console byte during the test boot** — one byte latches `g_host_present` and the node then never
   sleeps, so the check would pass over a node that was simply awake.
3. Wait past the 30 s boot grace **and** the 15 s panel blank, then read `status` **once, at the end**.
4. Pass: **`slept=` > 0**, with `wkarmfail=0` and `wkdisarm=0`.
5. ⓘ `wkbusy=` and `wksleepfail=` are informational — **record them, do not require zero** (Part 23 records why).

⛔ Fail = `slept=0` on a node that met steps 1-3 ⇒ something in the new per-tick path is holding the node awake. The
host-side guards are `probe_firmware_ui` P13f (a blanked chrome change opens no frame and issues no bus command) and
P13h (a chrome change is not user input, so the attention window is not postponed).

## Part 25 — §CHROME-4: the navigation rail, the config badge and the 19-column body (2026-08-16)

⛔ **THE RESIDUE ONLY.** The §5.2 mapping, the §6 badge priority, the slot mask, the 19-column wrap and its re-derived
page count are host-gated (`test/test_firmware_ui_chrome.cpp`, `test/test_firmware_ui_model.cpp`'s `chrome4-audit:`,
`tools/probe_firmware_ui` P14, `tools/probe_board_ui` W41-W43). What no host can reach is **pixels on glass** — whether
the icons are legible at 7 px, whether anything overlaps, and whether a real `/mrcfg` write moves the badge.

### 25.1 — geometry: exactly one boxed icon, and no text touches the rail (design §12.1)

1. Cycle all five screens on `heltec_mobile`.
2. Expected: a **one-pixel box** around exactly ONE rail icon at a time, and it is the icon of the screen being shown —
   ⓘ order top to bottom is **STATUS · TEAM · INBOX · SEND · SETTINGS**, aligned with the five body rows.
3. ⛔ Fail on: two boxes at once · no box · a box around an icon that is not the current screen · any body text
   touching or overlapping the icons · any body line clipped at the right edge.
4. ⚠ The screens no longer print `STATUS` or `SETTINGS` as a heading — **that is the change, not a fault** (design
   §7.2). INBOX keeps `INBOX <shown>/<total>` and SEND keeps `SEND to team`.

### 25.2 — the modal mapping: the rail names the body, not the screen underneath (design §12.2)

1. Open an inbox message (INBOX ⇒ double). Expected: **INBOX stays boxed** through the detail modal, its paging and
   `MESSAGE GONE`.
2. From TEAM, open a teammate and send a canned DM. Expected: **SEND is boxed** for the pick-a-text list *and* for the
   result view — ⛔ not TEAM, which is the screen underneath.
3. ⛔ Fail on the box following the screen rather than the body.

### 25.3 — ★★ emergency: the rail disappears and every headline is COMPLETE (design §12.3)

⛔ **THIS IS THE SAFETY CHECK OF THE SLICE.** `Font::large` is 10 px per column on a 128-px panel = **12 columns at
x = 0**; `NOT RELAYED` spends 11 of them. A rail-shifted emergency body would have 11 columns and would clip a
distress headline.

1. Long-press to arm, then fire an alarm; also let one run to `NOT RELAYED` and cancel one.
2. Expected on EVERY emergency screen: **no rail at all**, the top status strip still present, and the headline
   (`RELEASE!` / `SENDING...` / `BLOCKED` / `PICKED UP` / `NOT RELAYED` / `REPLY` / `CANCELLED` / `FAILED`) rendered
   **complete**, starting hard against the left edge.
3. ⛔ Fail on: a rail icon visible during an alarm · a headline missing its last character(s) · a headline that starts
   12 px in.

### 25.4 — the SETTINGS badge follows the priority table, and SETTINGS still says WHY (design §12.8)

⛔⛔ **SCOPE CORRECTED 2026-08-18 (QG): THE `RESTART NEEDED` ARM IS NOT REACHABLE ON ANY CURRENT BUILD.** It requires
saving `ble_mode`, and the BLE row is compiled out — `MR_UI_BLE_ROW` defaults to **0** (`src/firmware_ui.cpp:132-133`)
and **no env sets it**. ⓘ Part **20.4** already records this (`⚠ Only reachable on a build whose BLE transport is
compiled … which no env sets today`); this Part had not inherited it.
⇒ ★ **On a stock `heltec_mobile`, test PLAIN, UNSAVED and CONFLICT only.** ⛔ **Restart-related combinations are N/A
unless the image is built with `-DMR_UI_BLE_ROW=1`** — record them **not-run with that reason**, ⛔ never as a FAIL.
⛔ **[[B232]] (2026-08-20): READ THE "SETTINGS must ALSO still print" COLUMN FROM THE CLOSED `>ENTER SETTINGS`
VIEW**, i.e. just cycle to SETTINGS — do **not** open the menu first. That is the view an operator who saw the badge
actually lands on, and §6's ban on an icon-only error has to hold *there*. ⓘ Reaching the states still needs the
menu (`double`, walk, edit, `BACK`); only the **reading** moves.

⚠ The badge is a 7x7 glyph; read it at arm's length and confirm the states are **distinguishable**.

| do this | badge on the SETTINGS rail icon | ⛔ SETTINGS must ALSO still print (read it from `>ENTER SETTINGS`) |
|---|---|---|
| nothing (fresh boot, never opened SETTINGS) | plain gear | — |
| edit a value, do not save | gear **+ dot** | `CFG* UNSAVED` |
| with that draft open, `cfg set e2e_dm 1` **over serial** | gear **+ exclamation** | `CFG! RELOAD` |
| save a reboot-class field (`ble_mode`), do not reboot | gear **+ restart marker** | `RESTART NEEDED` |
| unsaved **and** restart-required together | the **unsaved** dot (unsaved outranks restart) | both texts, on their own rows |
| conflict **and** unsaved together | the **exclamation** (conflict outranks everything) | `CFG! RELOAD` |

⛔ **Fail on an icon-only configuration error**: if the badge changes and SETTINGS does not state the remedy in words,
that is design §6's forbidden state and §13 refuses it as a release. ⓘ The badge replaced the STATUS-title
`CFG* UNSAVED` / `CFG! RELOAD` decoration — **STATUS carrying no such text is the change, not a fault**.

### 25.5 — the 19-column body, on the two screens where it is tightest

1. **Inbox detail:** open the longest message you can post (≥ 120 bytes). Expected: two body rows of **19** characters,
   a page indicator `n/N` whose **N counts every page** — page through the whole body and confirm the last page's
   content really is the tail of the message, with **no bytes missing between page N-1 and page N**.
   ⛔ Fail on a body that never shows its final characters: that is a page count computed from the old 21-column wrap.
2. **TEAM:** with a teammate whose name is long, confirm the row shows name / age / hops without running under the
   rail or off the right edge, and that a teammate that has left reads `TEAMMATE GONE, pick` **complete**
   (ⓘ 19 characters exactly — it was `TEAMMATE GONE, repick` at 21 and had to lose two columns to the rail).

### 25.6 — ★★ IDLE SLEEP STILL WORKS WITH THE RAIL ENABLED (design §12.11 — the same regression guard)

The rail adds five bitmap draws and a frame per page, i.e. six more compose calls x 8 pages per frame. A power
regression presents **only** as `slept=` failing to climb: no panic, nothing visible on the panel.

1. Persist `team 0` **before** rebooting, so the node comes up with no peers.
2. ⛔ **Send NO console byte during the test boot** — one byte latches `g_host_present` and the node then never sleeps,
   so the check would pass over a node that was simply awake.
3. Wait past the 30 s boot grace **and** the 15 s panel blank, then read `status` **once, at the end**.
4. Pass: **`slept=` > 0**, with `wkarmfail=0` and `wkdisarm=0`.
5. ⓘ `wkbusy=` and `wksleepfail=` are informational — **record them, do not require zero** (Part 23 records why).

### 25.7 — the non-team OLED build (`gateway_heltec`)

1. Flash `gateway_heltec` and cycle its screens.
2. Expected: the rail shows **STATUS, INBOX and SETTINGS only**; the TEAM and SEND slots are **EMPTY**, and the three
   remaining icons are at the **same heights** as on `heltec_mobile` (⛔ not packed up to close the gaps — design §3.2
   forbids a second layout).
3. ⛔ Fail on a TEAM or SEND icon appearing on a build with no team plane, or on the icons shifting position.

## Part 26 — [[B196]]: the once-per-boot RTC power-domain assert (2026-08-17)

★★★ **THIS IS THE ONLY CHECK THAT CAN CLOSE [[B196]], AND IT IS COUNTED IN SLEEPS, NOT IN HOURS.** The defect was our
own `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)` running once per sleep ATTEMPT: ESP-IDF
ref-counts it in an `int16_t`, so call **#32,769** hit `assert(refs >= 0)` and panic-rebooted the node. The call now
runs **once per boot** in `setup()`. ⛔ **No automated gate in this tree can see any of this** — native does not
compile the arm, the 36 corpus streams never enter it, and neither UI probe drives `fw_main.cpp`'s sleep path.

⛔⛔ **UPTIME IS NOT THE CRITERION AND MUST NOT BE SUBSTITUTED FOR ONE.** Awake periods suspend the counting entirely
(the host-attached negative control survived 9h25m with `slept=0`), so a long uptime proves nothing on its own.
★ `slept=` is the CONSERVATIVE proxy: it counts sleeps that HAPPENED, while the old defect fired on every ATTEMPT
(attempts >= `slept` always) ⇒ **`slept` > 32,769 GUARANTEES the old trip point was passed**; a lower `slept` leaves
it undecided whatever the clock says.

1. Build `heltec_v3` (or any ESP32-S3 env), **archive its `firmware.elf` and record the banner `rev=`** before
   flashing — Part 23.7's rule, and it is what made the [[B196]] diagnosis possible at all.
2. Provision the node so it is idle and headless: persist `team 0` (no peers ⇒ almost no RX ⇒ maximum sleep duty),
   then **reboot**.
3. ⛔⛔ **SEND NO CONSOLE BYTE UNTIL THE FINAL READ — not one.** A single byte latches `g_host_present`, `may_sleep`
   goes false for the rest of the boot, `slept=` stops advancing, and the soak measures NOTHING however long it runs.
   ⓘ Attaching a monitor resets the board over DTR, so "connect at the end" means "the experiment is over".
4. Leave it strictly alone for **~10 h** (the practical target for > 32,769 sleeps at the <=1 s cap; the pre-fix
   panics landed at ~9h01m ± 3 min).
5. Then type `status` **ONCE** and read: **`slept=`**, **`sleep=`**, **`wkarmfail=`**, **`wkbusy=`**,
   **`wksleepfail=`** — the last three are exactly the attempts that did NOT become sleeps, so reporting all of them
   settles the attempt-vs-sleep gap by measurement instead of estimate.
6. **PASS:** `sleep=auto`, **`slept=` > 32,769**, and `/mrfault` shows **no new `HARDFAULT · hint:PANIC`** record for
   this boot (`boot_seq` advanced by one, POR cause).
7. ⛔ **`slept=0` or `sleep=off-host` means the arm never ran — the soak measured nothing and must be re-run**, no
   matter what the uptime says.
8. ⛔ **FAIL, and it refutes the fix rather than the run:** a panic whose backtrace names `esp_sleep_pd_config` /
   `sleep_modes.c` on a build carrying the once-per-boot call. Capture it with the monitor attached, decode it against
   the archived ELF (Part 23.7 step 3), and reopen [[B196]].

## Completion record

- Firmware revision tested: `________________`
- Boards / node labels: `________________`
- Date and tester: `________________`
- Failed or skipped checks: `________________`
- Log/archive location: `________________`

## Part 27 — [[B207]]: the typed team-provisioning transaction (2026-08-17)

### ✅ RESULT — RUN 2026-08-18, SEVEN PASS / THREE NOT-RUN / FOUR FINDINGS
**Image `fc89e14`** (banner `built Aug 17 2026 22:20:08 · fc89e14-dirty`, archive
`~/MeshRoute-artifacts/soak-20260817-2023/`, `firmware.elf` sha256 `70c4323b2f510a4f…`).
**Nodes:** `heltec_mobile` (team-local 32 → 220) + `xiao_mobile` (team-local 155), team `0x3D9348A5`.

| check | verdict | evidence |
|---|---|---|
| **27.1** stack | ✅ PASS | `stackhw` 6016 → 4512 → 4408 of an 8 KB task = **54 % headroom**, 4.4× the ~1 KB floor. ⓘ The verb cost **1504 B** for the WHOLE chain (console dispatch, LittleFS save, X25519 derive, `set_team_id` teardown, retune, DAD) — ⛔ **not attributable to this slice's 888 B frame alone.** |
| **27.2** `team 0` + PHY refused | ⚠ **BEHAVIOUR PASS / DIAGNOSTICS FAIL — ⇒ EXPECTED PASS ON RE-RUN, see 27.15** | With a COMPLETE tail: the exact two-line refusal, and `cfg` after shows team, local id, key and PHY **all unchanged** ⇒ the behaviour is correct. ⛔ **But this is NOT an unconditional pass (QG 2026-08-18): the `sf=`-only and `bw=`-only repetitions this step calls for do NOT reach the intended refusal at all** — they die in the parser on [[B212]]'s generic range check, exactly as `freq=`-only did. ⇒ **the diagnostics half FAILS until B212 is fixed**, and this row must not be read as green. ⚠ The step also had to be corrected twice before it tested anything (see below). |
| **27.3** leave preserves PHY | ✅ PASS | `team 0` → `team_id=0x00000000`, `cfg` PHY unchanged. **The owner's ruling confirmed on metal.** |
| **27.4** bare same-team | ✅ PASS (scoped) | exact `no change` string. ⚠ **CORRECTED (QG): "zero writes" is established by SOURCE and HOST tests, not by this observation** — metal saw no change, but **there is no physical write counter on the device**, so the run confirms the reported verdict and the absence of visible effect, not the absence of a flash write. |
| **27.5** re-key + power-cycle | ✅ **PASS — the decisive one** | re-key **applied** while the node was keyless (`live_key_matches` false), `team_local_id` preserved at **220**; after `reboot`: **`team_local_id=220` and `team_ch_key=1` both survived** ⇒ the candidate did NOT persist `team_local_id = 0` on a same-membership request, which is exactly the defect design v2 corrected. ⚠ **QG provenance caveat: the metal conclusion assumes the documented absence of a reboot-time re-DAD, and RAW SERIAL LOGS WERE NOT ARCHIVED**, so that detail could not be independently replayed. ⇒ **archive the raw console capture on future runs**; the surviving id is strong but the no-re-DAD step rests on the design, not on a retained trace. |
| **27.8** live divergence | ✅ **PASS** | with record and request **identical** (869.4625/7/{7}/125) and only the radio diverged, the verb **APPLIED** — `↻ rx-freq → 869.4625` + `> team PHY:`. The round-3 defect is closed on metal. |
| **27.9** converged | ✅ PASS | exact `no change` string, no OLED save marker |
| **27.6** static `team new freq=` | ⛔ **NOT RUN — STILL REQUIRED BEFORE FINAL [[B207]] CLOSURE (QG 2026-08-18)** | both bench nodes are mobiles and `team 0` does not demote (R3 one-directional). ⇒ **run it on a temporary static / factory-erased node AFTER [[B209]] and [[B211]] are fixed**, since both change what this step should observe. |
| **27.7** save failure | ⛔ NOT RUN | a real `/mrcfg` write failure could not be provoked |
| **27.10** live-keyless install | ⛔ NOT RUN — **unreachable by construction**, see the section itself |

⛔⛔ **FOUR DEFECTS FOUND BY THIS RUN, ALL REGISTERED:** [[B209]] (a team PHY tail silently authorises static-home
attachment) · [[B210]] (`team-DAD:` printed whether or not DAD ran) · [[B211]] (`sf=` silently collapses the DATA
`sf_list` — corroborated **across a power-cycle**: the banner went `data sf = 6,7` → `data sf = 7`) · [[B212]] (range
error misnamed *and* masking the specific refusal).

★★ **AND THREE OF THIS PART'S OWN CHECKS WERE WRONG AND ARE CORRECTED IN PLACE — the lesson is uniform and worth
stating once:** 27.1 named `stackhw`, which **does not exist on the Heltec** (nRF52-only); 27.2 expected `team_id 0`
after a *refusal* and then used an incomplete tail that never reached the branch; 27.10 assumed a state that is
**unreachable**. **Every one was written from the design's prose rather than from the code that executes** — the same
root as the comment-derived errors in the [[B207]] arc. ⇒ **a bench step that names a field, a message or a state must
be checked against the source that produces it.**



⛔ **THE RESIDUE ONLY.** The transaction, the `KeyAction` matrix, the refusals, the save-failure guarantee and the
candidate composition are all host-gated (`test/test_firmware_provisioning_service.cpp` + `tools/probe_prov_tx` +
`tools/probe_board_ui` W12/W14-W20). What no host can reach is **a real NV backend**, **real airtime**, and **the
stack on a real frame**. Nothing else belongs here.

### 27.1 — ★★ `stackhw` after a `team new` — ⛔ **nRF52 ONLY. RUN IT ON `xiao_sx1262` / `xiao_mobile`, NOT ON THE HELTEC.**
⛔⛔ **CORRECTED 2026-08-17 — THIS CHECK NAMED AN INSTRUMENT THE HELTEC DOES NOT HAVE, and it was attempted there
first.** `status` prints `stackhw=` only under `#if defined(NRF52_PLATFORM) || defined(ARDUINO_ARCH_NRF52)`
(`src/firmware_commands.cpp:436-438`), and `loop_stack_free_bytes()` returns **0** off nRF52 by design
(`src/fw_main.cpp:251-257`: *"nRF52 only (the cramped platform; ESP32's loopTask is large, native has no task)"*).
⇒ **On a Heltec/ESP32 build the field is simply absent — that is not a failure, and there is nothing to read.**
★★ **AND nRF52 IS THE RIGHT TARGET ANYWAY, which is why this is a relocation rather than a loss:** the **888 B**
figure is the **ARM** frame, i.e. nRF52; the 4 KB Arduino-loop-task history belongs to nRF52; and the xtensa chain
(928 B) sits on ESP32's ~8 KB loopTask, where it is ~11 % of the stack rather than a threat.
⇒ **On ESP32: record "no on-device stack reading exists on this platform" and treat the host frame measurement as the
qualification.** ⓘ **Open option, NOT ruled:** `uxTaskGetStackHighWaterMark` works on ESP32 too, so the field *could*
be extended there — the in-source judgement that it is unnecessary predates this slice's +224 B. Raise it with the
owner rather than assuming either way.
★★ **FIGURES CORRECTED 2026-08-17 — the earlier explanation was measured and REFUTED.** The 256-B `NodeConfig` copy is
**not** the cost and never was: ARM `handle_team` measures the same with the projection first, with the plan+Blob first,
and with the copy **deleted outright** (`-Ofast` scalarises it — `role_enforce` touches three fields). The growth is the
**typed carriers**. Current figures: **ARM `handle_team` 888 B** (pre-slice 536), **xtensa deepest chain 928 B**
(pre-slice 704). ⛔ **The old "`stackhw` once fell to 72 B" framing is WITHDRAWN as a statement of present risk** — that
reading predates the dedicated 8 KB mesh-task fix that moved this work off the 4 KB Arduino loop stack. ⇒ this check is
a **qualification**, owner-accepted as such, not a code blocker — **but run it on the first firmware that carries the
slice.**
1. Boot, then `status` and record `stackhw=`.
2. Run `team new freq=869.4625 sf=7 bw=125`.
3. `status` again. ⛔⛔ **PASS CRITERION CORRECTED 2026-08-18 (QG) — THE OLD ONE WAS UNSATISFIABLE AND CONTRADICTED
   THE EVIDENCE THIS VERY PART ACCEPTED.** It read *"stays comfortably above ~1 KB **and does not fall across the
   verb**"*, yet the recorded pass was **6016 → 4512 → 4408**. ★ `stackhw` is a **lifetime high-water mark**, so it
   NECESSARILY falls the first time a deeper path executes — "no fall" cannot be met on a first run.
   ★ **PASS =** (a) post-command `stackhw` **comfortably above ~1 KB**; (b) the node **stays responsive and does not
   reset**; (c) the delta is **recorded** — a first-call decrease is **expected**, not a failure.
   ⓘ Optional and cheap: repeat the verb once; a second run should **not** expose a materially deeper path (the
   recorded second call cost only 104 B against the first call's 1504 B).
⛔ **FAIL (any reading under a few hundred bytes) is a STOP: report it rather than tuning around it** — the fix would be
a design question (where the projection copy lives), not a constant.

### 27.2 — `team 0` with a PHY argument is refused, and nothing changes
⚠ **A BEHAVIOUR CHANGE: this used to be a silent no-op.**
1. In a team, note `freq`/`sf`/`bw` from `status`.
2. ⛔⛔ **CORRECTED 2026-08-18 — USE A COMPLETE PHY TAIL:** `team 0 freq=869.4625 sf=7 bw=125`.
   This step read `team 0 freq=868`, which **never reaches the refusal under test**. ⛔ **CORRECTED 2026-08-18 (QG):
   an earlier wording here blamed "`phy_args_in_range` needs freq AND bw AND sf" — `bw=` is OPTIONAL and defaults to
   125 kHz (`src/firmware_config.cpp:881`).** ★ The real mechanism is TWO paths: `freq=868` trips the range check
   because **`sf` stays 0**, while `sf=7` or `bw=125` alone dies EARLIER at `!pa.has_freq` (`:889`). Both mask the
   specific leave refusal ([[B212]]) before the transaction is consulted.
   Metal-confirmed 2026-08-18: it answered `> team new err: freq 100..1000 MHz, …`, refusing correctly but for the
   wrong reason and naming the wrong subcommand.
   ⇒ with a COMPLETE tail, expect:
   `> team err: freq=/sf=/bw= make no sense on \`team 0\` (leave) — leaving a team PRESERVES the current PHY.`
   followed by the `NOTHING changed…` remedy line.
3. ⛔⛔ **CORRECTED 2026-08-17 (QG): `status` ⇒ the ORIGINAL NON-ZERO `team_id` IS STILL THERE, and freq/sf/bw are
   UNCHANGED.** This step previously read *"team_id 0"*, which contradicted the check's own point: the command was
   **REFUSED**, so it did not leave the team either. ⛔ No `cfg saved` notification, and ⛔ the OLED must not show a
   save marker — nothing was written. ⓘ A bare `team 0` DOES leave; that is §27.3.
4. Repeat for `sf=` alone and `bw=` alone.

### 27.3 — a bare `team 0` still leaves, and PRESERVES the PHY (the owner ruling)
`team 0` with **no** tail ⇒ leaves the team, and `status` shows **freq/sf/bw unchanged**. ★ This is the ruled invariant:
leaving changes membership only.

### 27.4 — a truly-unchanged same-team request reports `no change`
While in team `0xNNNNNNNN`, run `team 0xNNNNNNNN` with no tail ⇒ **expect a `no change` report, no save, and no
OLED save marker.** ⓘ Distinguishes the ruled `no_change` verdict from a silent success.

### 27.5 — a same-team re-key preserves the local id (⛔ the defect this slice exists to prevent)
1. `status` ⇒ record `team_local_id`.
2. Re-key in place: `team <current id> tkpub=<64hex> tkpriv=<64hex>`.
3. `status` ⇒ ★ **`team_local_id` and the node id are UNCHANGED**, and ⛔ **no team-DAD was fired**.
4. **Power-cycle** ⇒ `team_local_id` is still the same value. ⛔ A re-DAD here means the candidate persisted
   `team_local_id = 0` on a same-membership request — the exact defect corrected at design v2.

### 27.6 — a static node's `team new` honours its PHY (⚠ behaviour change)
On a **static** node: `team new freq=869.4625 sf=7 bw=125` ⇒ the PHY is **applied and reported**, and the role
promotion to MOBILE is reported. ⛔ Previously the PHY arguments were **silently discarded**.

### 27.7 — the save-failure arm, if it can be provoked at all
⚠ **Only if a real NV write can be made to fail** (a full/unwritable store). Then: `team new` ⇒ the verb reports the
failure, and **team, keys, role and PHY are all UNCHANGED and NO airtime is spent** (no beacon on the new team).
ⓘ If it cannot be provoked, **say so** — the host tests cover the caller's half; ⛔ **physical NV atomicity remains
[[B193]] Part 20.5 and is NOT established here.**

### 27.8 — ★★ an explicit PHY request is APPLIED when the RADIO diverges from the record (QG round-3 blocker)
⛔ **This is the defect the last correction closed; it is invisible to every host gate.**
1. `mobile register freq=868.5 sf=9 bw=250` — this retunes the radio **live and does NOT update the record**.
2. `team <current-id> freq=<the value the record already held> sf=<same> bw=<same>`.
3. ★ **PASS = it APPLIES:** `> team PHY: freq=… sf=… bw=… kHz` then `> team -> team_id=0x…`, and `status` shows the
   radio back on the record's PHY.
⛔ **FAIL, and it is the exact old defect: `> team: no change — …` while the radio stays on 868.5.**

### 27.9 — …and a converged request still reports `no change`
Immediately repeat the same `team <current> <same PHY>`. **PASS =** `> team: no change — already team_id=0x…, and the
stored record AND the live radio/key already match what you asked for. Nothing was written and nothing was applied.`
⛔ No save, and ⛔ the OLED must not repaint as saved.

### 27.10 — an explicit key is INSTALLED when the live key is absent — ⛔ **HOST-PROVEN, METAL-CONDITIONAL**
⛔⛔ **CORRECTED 2026-08-17 (QG): THE EXAMPLE THIS CHECK ORIGINALLY GAVE CANNOT CREATE THE CONDITION.** It read
*"On a node whose live team key was cleared (e.g. `team 0` then rejoin)"* — but **`team 0` clears the PERSISTED key too**
(`KeyAction::clear` zeroes the candidate, `src/firmware_provisioning_service.h:572`), so NV can never be keyed while the
live node is keyless by that route. **Verified: the only live clear is inside `set_team_id`** (`lib/core/node.cpp:683`,
its sole caller) **and boot always restores from NV** (`src/fw_main.cpp:789`) ⇒ ★ **on current firmware there is NO
operator-reachable way to reach NV-keyed + live-keyless.**
⇒ **Status: the `live_key_matches` branch is proven HOST-SIDE ONLY** — native case *"record key == request, LIVE key
differs/absent"* (3 arms: absent · different pair · **pub matches but priv differs**) with control **M-F**
(absent-treated-as-matching) and **M-E** (public-half-only compare) both RED.
⛔ **Do NOT attempt this on metal and do NOT record a FAIL for being unable to set it up.** Run it **only if** a
deliberate live-only clear is ever added (a debug verb, a failed re-adopt, or a grant path that installs without
persisting); until then this line is a **standing note of an unreachable state**, not an owed check.
ⓘ The comparison includes the **private** half deliberately: `team_channel_key_load` installs verbatim, so a pub-only
check would accept a record whose halves disagree and produce a node that cannot decrypt.

### 27.11 — ★ [[B209]]: a team PHY tail must NOT authorise home attachment (metal-only)
⛔ **No host gate reaches this** — `src/firmware_config.cpp` is outside the native suite and only metal runs the console
verb end to end. Run on a mobile bench node.
1. `cfg set mobile_autoregister 0`, **reboot**, then `mobile status` ⇒ `autoregister:false`, `home_desired:false`,
   `attachment:"dormant"`.
2. `team new freq=869.4625 sf=7 bw=125` ⇒ the team applies and a team-local id is assigned.
3. ★★ `mobile status` ⇒ **`home_desired:false`** and **`attachment:"dormant"`**, and ⛔ **NO outbound J `DISCOVER`** in
   the console log over the following minute. ⓘ **Pre-fix this read `home_desired:true` / `"seeking"` with repeated
   DISCOVERs** — that was the metal reproduction the bug was registered from.
4. ★ **Positive control on the same node, and it is not optional:** `mobile register freq=869.4625` ⇒ `attachment:"seeking"`
   and DISCOVERs **resume**. ⛔ Without this step, step 3's silence is indistinguishable from a **dead mobile plane** —
   the check would pass just as well on a node whose home-seeking was broken outright.

### 27.12 — ★ [[B211]]: a team PHY tail PRESERVES the DATA `sf_list` (real NV + power-cycle)
⛔ **What no host gate reaches: persistence through a real LittleFS/NVS write and a reboot.** The decision itself is
natively pinned; this proves it survives.
1. Pre-condition: a mobile whose `cfg` reads **`sf_list=6,7`**.
2. `team <its current id> freq=869.4625 sf=7 bw=125`.
3. **Expect exactly:** `> team PHY: freq=869.463 sf=7 bw=125.00 kHz sf_list=6,7`
4. `cfg` ⇒ **`routing_sf=7` and `sf_list=6,7`** — ⛔ **not `sf_list=7`**.
5. ★ **Power-cycle, then `cfg` again ⇒ still `sf_list=6,7`.** ⓘ The pre-fix banner went `data sf = 6,7` → `data sf = 7`
   and **survived a reboot**, so the reboot is the step that matters.

### 27.13 — ★★ [[B211]]: resolution reads the RECORD, not live state (the pin-1b case on metal)
⛔ **This is the case that separates a correct fix from one that merely looks correct.**
1. From the same node: `mobile register freq=868.5 sf=9 bw=250` ⇒ `cfg` shows **live** collapsed to
   `routing_sf=9 sf_list=9`. ★ **That collapse is UNCHANGED behaviour and must still happen** — `mobile register` was
   deliberately left alone.
2. `team <current id> freq=869.4625 sf=7 bw=125`.
3. ★ **Expect `sf_list=6,7`** in the report, and `cfg` reading `sf_list=6,7` after a power-cycle.
⛔ **`sf_list=9` here means the resolution read LIVE state and has laundered an un-persisted collapse into NV** — the
exact failure the native mutation `snap.live_allowed_sf_bitmap` reddens.

### 27.14 — ★ [[B210]]: `team-DAD` now means DAD, and this REPAIRS 27.8's discriminator
1. On a mobile already in team `<T>` with a key, issue a **same-team re-apply**: `team <T> freq=<what it already flies>`.
   ⇒ `> team -> team_id=0x<T>` and ⛔ **NO `  team-DAD: local_id=` line at all**, `team_local_id` unchanged in `cfg`,
   and **no `»tx BCN from=<id>` burst**.
2. Then a genuine membership change on the same node (`team new`, or `team <other>`).
   ⇒ the line **MUST** appear, the `»tx BCN from=<N> n=0` burst **must** follow, and `<N>` must equal the
   `team_local_id` `cfg` then reports — ★ **never `0`**, which is what a regression to `res.persisted_team_local_id`
   would print.
★★ **Consequence for Part 27.8:** the absence of the `team-DAD` line is a VALID "no DAD, no airtime" discriminator
again. It was withdrawn when [[B210]] made it meaningless; ⇒ **27.8 may rely on it once this check passes.**

### 27.15 — ★ [[B212]]: the SPECIFIC `team 0` refusal now wins, and the verb name is right
⇒ **This is 27.2's diagnostics half, re-run against the fixed build.** ⛔ It is *expected* to pass, not *recorded* as
passing — the fix is host-gated only and this TU is compiled by neither the native suite nor the simulator.
1. In a team, type each of: `team 0 freq=868` · `team 0 sf=7` · `team 0 bw=125`.
   ⇒ **each must answer EXACTLY:**
   `> team err: freq=/sf=/bw= make no sense on `team 0` (leave) — leaving a team PRESERVES the current PHY.`
   `>   NOTHING changed. To retune, leave the team first (`team 0`) and then set the PHY (`mobile register freq=… sf=… bw=…`).`
   ⛔ **It must NOT say `> team new err: freq 100..1000 MHz…`** — that was the pre-fix answer, and it named a
   subcommand the operator never typed.
2. `cfg` after each ⇒ **still in the team, PHY unchanged.**
3. ★ **Mixed tail:** `team 0 freq=868 wibble=3` ⇒ must still answer **`> team err bad/unknown key: wibble`** — ⛔ the
   unknown token must NOT be swallowed by the PHY refusal.
4. ★ **Positive controls, all must still hold:** a bare `team 0` leaves cleanly · an out-of-range `team new freq=99999
   sf=7 bw=125` still gets the range message (now `> team err:`) · and ⛔ **`team <id> freq=869.4625 sf=7 bw=125` still
   parses and APPLIES** — the destructive-tokeniser guard, whose native mutation fails with SIGSEGV.
⇒ **When 1-4 hold, mark 27.2's diagnostics half PASS.**

### 27.16 — ★ [[B214]]: `cfg mobile-reg:` reports attachment state, not merely a home id

This is a truthfulness check. It does not change or qualify the attachment algorithm.

- [ ] **Dormant is never called scanning.**
  1. `cfg set mobile_autoregister 0`, then reboot.
  2. `mobile status` must show `"attachment":"dormant"`, `home_desired:false`, and `retry_window_ms:0`.
  3. `cfg` must say exactly `mobile-reg: UNREGISTERED (dormant)`; it must not contain `(scanning)`.
- [ ] **Seeking is named seeking.**
  1. With no host answering, run `mobile register freq=<bench frequency>`.
  2. While `mobile status` says `"attachment":"seeking"`, `cfg` must say
     `mobile-reg: UNREGISTERED (seeking)`.
- [ ] **A provisional home is not reported as registered.**
  1. Let a static host offer service. Poll `mobile status` until it reports `"attachment":"claiming"`; then
     immediately run `cfg`.
  2. Pass: `mobile-reg: UNREGISTERED (claiming)`. A line beginning `REGISTERED home=` is the B214 regression.
  3. If the node reaches `attached` before `cfg`, repeat; that attempt did not exercise the claiming arm.
- [ ] **Positive control: confirmed attachment is registered.**
  - After roster confirmation, `mobile status` says `"attachment":"attached"` and `cfg` says
    `mobile-reg: REGISTERED home=<same non-zero id>`.

ⓘ `recovering` and the impossible `attached`-without-home diagnostic are host-gated. Do not destabilise a metal
topology merely to manufacture them.

### 27.17 — ★ [[B230]]: the incomplete-PHY refusal names the MISSING part and a remedy that WORKS
⇒ Metal-only because `team_report_not_applied` lives in `src/firmware_config.cpp`, a TU compiled by neither the
native suite nor the simulator. The classification is host-gated (§B230 x2 + `--target=provservice`); ⛔ only the
SENTENCE the operator reads is unreachable, and it is what the defect was.
1. On a node whose persisted `sf_list` is EMPTY (`factory_reset`, then `cfg` shows no DATA SF), type
   `team new freq=869 sf=7 bw=125` ⇒ **must answer EXACTLY:**
   `> team err: incomplete PHY — the MISSING part is this node's `sf_list` (the DATA SF set): it is EMPTY, which blocks DATA entirely.`
   `>   ★ `sf_list` is NOT a `team` key — the team PHY tail carries freq/sf/bw only and PRESERVES this node's own DATA SF set.`
   `>   set it FIRST: `cfg set sf_list 6,7`, then retry your original `team` command.`
   `>   NOTHING changed — team_id, the team channel key, the PHY and NV are all as they were.`
   ⛔ It must NOT say `need freq, routing_sf(5..12), sf_list(DATA SF), bw` and must NOT suggest
   `set them inline: `team new freq=869.0 sf=7 bw=125`` — that was the pre-fix answer, and it was the command
   that had just failed.
2. ★★ **THE FORM-NEUTRALITY CHECK, AND IT IS THE ONE A `team new`-ONLY TEST MISSES:** repeat step 1 as a **JOIN** —
   `team 0x12A1B2C3 freq=869 sf=7 bw=125` on the same empty-`sf_list` node ⇒ **the SAME four lines, verbatim.**
   ⛔ The remedy must NOT name `team new`: an operator who typed a join must be sent back to their own command,
   never told to create a different random team.
3. ★ **THE REMEDY IS FOLLOWED, which is the whole point:** `cfg set sf_list 6,7` then re-run the command from step 1
   ⇒ the team is created/joined, and `> team PHY:` ends `sf_list=6,7` (⛔ not `7` — [[B211]] still holds).
4. **Control, the generic arm:** with a valid `sf_list` in place, provoke a genuinely incomplete tail ⇒ must still
   answer `> team err: incomplete PHY — need freq, routing_sf(5..12), bw. …` followed by
   `>   set them inline on your `team` command: `freq=869.0 sf=7 bw=125` …` — ⛔ also form-neutral, and the inline
   remedy must NOT have been withdrawn: on this arm it really can repair the node.
5. `cfg` after steps 1 and 2 ⇒ **team_id, key, PHY and NV all unchanged** (each refusal spends no write, no airtime).
⇒ When 1-5 hold, [[B230]] is metal-closed.

## Part 28 — §UI-15 slice 2: the `/mrjoin` profile store on REAL flash (2026-08-19)

⛔ **THE RESIDUE ONLY.** The record layout, the tri-state read, the absent/corrupt matrix, the units and the
coalescing decision are all host-gated (`test/test_firmware_join_profiles.cpp` + `tools/probe_ui_model_mutations.py
--target=joinprofiles`, 17 entries). **What no host reaches is a real NVS/LittleFS write, wear, and a power cut** —
⛔ the power-cut half is **slice 7**, not this Part.

1. Fresh chip: `joinprofile list` ⇒ **`> joinprofile NO PROFILES`** — ★ the ordinary absent state, ⛔ never
   `PROFILE STORE INVALID`.
2. `joinprofile set 1 layer=4 freq=869.4625 bw=125 sf=9 name="hut"` ⇒ `> joinprofile set 1 ok`; then
   `joinprofile list` ⇒ `> joinprofile 1 layer=4 freq=869.4625 bw=125.00 sf=9 name="hut"`.
   ★★ **The four decimals are the Hz-not-kHz pin on metal:** 869.4625 MHz is 869462.5 kHz — not integral — so a store
   that kept kHz would come back **869.462** or **869.463**. Reading `869.4625` back proves the record kept **Hz**.
3. Repeat the identical `set` ⇒ **`> joinprofile set 1 unchanged`** and ⛔ **no flash write** (the coalescing guard).
4. ★ **Power-cycle**, then `joinprofile list` ⇒ slot 1 still there. ⓘ **This is the only step that exercises the
   backend at all** — everything above it ran against a fake store.
5. `joinprofile clear 1` ⇒ `ok`. `joinprofile reset` (no `confirm`) ⇒ **`> joinprofile err needs_confirm …`** and
   ⛔ **nothing written**. `joinprofile reset confirm` ⇒ `ok` / `unchanged`.
6. ★★ **`factory_reset confirm`, then `joinprofile list` ⇒ `> joinprofile NO PROFILES`.** This is the owner's
   ruling proved on metal — `/mrjoin` lives in the `"mr"` namespace precisely so a factory reset takes it, unlike
   `/mrfault` which sits in its own namespace to survive one.
7. On a **gateway** build: `joinprofile list` ⇒ `> err gateway_build (joinprofile is normal-node only)`.

### 28.4 — ★ the FRESH-CHIP line (Heltec V3, `mr` NVS namespace never written)
⛔ **Run BEFORE any `cfg set` / `regen`** — the first save of *any* record creates the namespace and the window closes.
`joinprofile list` ⇒ **`> joinprofile NO PROFILES`**, and ⛔ **NOT** the storage-failure line.
★ **This is the only check that can reach the ESP32 `nvs_open` / `ESP_ERR_NVS_NOT_FOUND` classifier** ([[B218]]):
`Preferences::begin()` answers the same `false` for "never written" as for "would not open", so without that
classifier a **fresh chip would report STORAGE FAILURE**.

### 28.5 — the strict index (either board, no reflash)
`joinprofile clear 2junk` and `joinprofile set 1x layer=4 freq=868 bw=125 sf=9` ⇒ **`> joinprofile err bad_index`**,
and a following `joinprofile list` shows the slots **UNCHANGED** ([[B220]] — `atol` used to accept these as slots 2
and 1, **and write**). `joinprofile list extra` ⇒ the usage line, ⛔ not silent acceptance.

### 28.6 — ⛔ NOT REACHABLE ON THE BENCH, stated rather than glossed
The **`PROFILE STORE UNREADABLE / STORAGE FAILURE`** line itself: inducing an unmountable LittleFS or an unopenable
NVS is not a bench operation. ⇒ **its BEHAVIOUR is proven natively; its RENDERING on metal is not.** Related coverage
residue: [[B221]] (the NV adapters are compiled by no automated gate).

## Part 29 — §UI-15 slice 5: OLED team create on REAL hardware (2026-08-20)

⛔ **THE RESIDUE ONLY.** The state model, the §4 gate, the adapter's PHY precondition, the two `ProvPhy` objects and
every screen string are host-gated (`test_firmware_ui_prov.cpp`, the `model`/`chrome`/`uiprov` batteries, and the
probe's **`v3` child-enabled arm** driving the REAL renderer + REAL adapter — [[B225]]). **What no host reaches: a
real live-vs-persisted retune divergence, real flash persistence, a real team-DAD on air, and the physical panel.**

1. **`gateway_heltec`:** SETTINGS lists **no `PROVISION` row** (walk the whole menu). Expected rows:
   `DM crypt / key attach / auto reg / SAVE / DISCARD / BACK` only — the owner's 2026-08-19 hide ruling on metal.
2. **Leaf (`heltec_v3` / `heltec_mobile`):** SETTINGS → `>PROVISION` → `double` ⇒ child menu `>CREATE TEAM`,
   `JOIN NETWORK`, `BACK`. `double` on **JOIN NETWORK does NOTHING** — slice 6's, ⛔ not an error line.
3. **Confirm screen:** `double` on CREATE TEAM ⇒ `CREATE NEW TEAM`, and `REPLACES <6 hex>` **only when already in
   a team**; cursor on **`>BACK`**. A `double` here (BACK selected) returns to the child menu; `cfg` shows
   ` team=0x…` **unchanged** and ⛔ **no `»tx BCN` burst** (zero transaction on BACK, proved on air).
4. **Create:** `short` (→ CREATE) then `double` ⇒ panel **`TEAM CREATED` / `0x<8 uppercase hex>` /
   `<6 uppercase hex>` / `press = back`**. ★ **The fingerprint row must be the LAST 6 of the 8 hex digits** — the
   shared-helper pin (slice 3) read off the glass. `cfg` then prints ` team=0x<same 8 hex>` +
   `team_local_id=<n>` (`(team-DAD pending)` until DAD completes).
5. ★★ **PHY divergence — the check ONLY metal can run** (the precondition compares against a genuinely retuned
   radio): `mobile register freq=869.100 sf=7 bw=125` (retunes live, persists nothing — the [[B211]] condition),
   then OLED create ⇒ panel **`PHY DIFFERS` / `USE SERIAL`**; `cfg` ` team=0x…` **unchanged**, no key change, and
   ⛔ **no `»tx BCN`** DAD burst.
6. ★ **Durability:** after a successful create, **power-cycle** ⇒ `cfg` shows the same ` team=0x…`,
   `team exportkey` returns the same keypair, and SETTINGS shows **no** `CFG! RELOAD` (the notify re-anchored the
   draft baseline).
7. **Persist tracker:** after create + DAD, note `team_local_id=<n>`; **power-cycle** ⇒ the same `<n>` and
   ⛔ **no re-DAD burst** on boot.

## Part 30 — §UI-15 slice 6: OLED static join on REAL hardware (2026-08-20)

⛔ **RUN ONLY AFTER SLICE 6 PASSES QG AND THE TESTED REVISION IS COMMITTED.** The host gates already cover the
four-term adoption correlation, every `JoinRefuseReason`, storage-state rendering and save-before-live ordering.
This Part measures what those gates cannot: the physical button flow, real `/mrcfg` persistence, radio retune,
on-air DAD/adoption, the full-layer/nibble split on a real layer above 15, panel blank/wake, and actual connectivity.

### 30.0 — equipment, build and evidence

Required:

- one OLED joiner: `heltec_mobile` or `heltec_v3`;
- one already-adopted static peer on the target network;
- a quiet bench carrier shared by both nodes. The procedure below uses **layer 17**, 869.4625 MHz, SF7/BW125.

Build and flash the exact committed revision:

```sh
pio run -e heltec_mobile
pio run -e heltec_mobile -t upload --upload-port /dev/ttyUSB0

# If the static peer is another Heltec base build:
pio run -e heltec_v3
pio run -e heltec_v3 -t upload --upload-port /dev/ttyUSB1
```

Archive `.pio/build/<env>/firmware.elf`, `firmware.map`, the flashed `.bin` files, the commit hash and SHA-256 sums.
On both nodes record `version`, `whoami`, `cfg`, `status`, and then enable `debug on`. ⛔ Stop if either banner is
`nogit`, names a different revision, or the two PHYs do not match the intended stage of the test.

Prepare the static peer on full layer **17** (wire leaf **1**) using an existing valid network, or create it first:

```text
create layer=17 freq=869.4625 bw=125 sf=7 sf_list=6,7 duty=1 name="Layer 17"
```

Wait for adoption. Record its non-zero node id as `<PEER_ID>`. On the OLED joiner create a deliberately sparse
profile list:

```text
joinprofile reset confirm
joinprofile set 1 layer=4 freq=868.5 bw=125 sf=9 name="old"
joinprofile set 3 layer=17 freq=869.4625 bw=125 sf=7
joinprofile list
```

Expected: only slots **1 and 3** are present; slot 3 keeps exactly `layer=17 freq=869.4625 bw=125.00 sf=7`.

### 30.1 — the unsaved/conflict gate remains ahead of provisioning

- [ ] Edit one SETTINGS value on the OLED but do not save it. Activate `PROVISION`.
  - **Pass:** provisioning does not open; the panel says `SAVE OR DISCARD`; no J frame is transmitted.
- [ ] `DISCARD`, then verify `PROVISION` opens normally.
- [ ] Optional conflict arm: make another unsaved OLED edit, then change a covered field through serial/BLE.
  Activating `PROVISION` must say `RELOAD OR DISCARD`, ⛔ never suggest SAVE. Resolve the conflict before continuing.

### 30.2 — sparse list, complete confirmation and safe BACK

1. SETTINGS → `PROVISION` → `JOIN NETWORK`.
2. **Pass:** the list shows `old`, `PROFILE 3`, and `BACK`; slot 2/4 do not appear. Cycling must move directly between
   those three rows. ★ `PROFILE 3` is the stored slot number, ⛔ not its second-row position.
3. Open `PROFILE 3`. The physical panel must show all of:

   ```text
   PROFILE 3
   L17 SF7 BW125.00
   869.4625 MHz
   >BACK
    JOIN
   ```

4. With BACK still selected, double-press.
   - **Pass:** returns to the profile list; `cfg`, `whoami`, the live PHY and `joinprofile list` are unchanged;
     no outbound J CLAIM appears. Background beacons do not invalidate this check — the discriminator is a J claim,
     not the aggregate TX counter.

### 30.3 — real join, blank/wake and correlated result

1. Open `PROFILE 3` again, short-press once to select `JOIN`, then double-press.
2. Immediately after the transaction returns:
   - **panel:** `JOINING`, never `JOINED` or `ADOPTED` yet;
   - **console:** the record is saved, the radio retunes to 869.4625/SF7/BW125, then an outbound J CLAIM follows
     after the listen window;
   - ⛔ a save failure must not retune or transmit. If a real failure cannot be induced, record it as not-run rather
     than pretending this branch was exercised.
3. Do not touch the button for **16-18 seconds**. The panel should have blanked at 15 seconds while DAD continues.
   Short-press **once**.
   - **Pass:** that press is consumed only as wake; it still shows `JOINING` and does not return to the menu.
4. Wait for the real correlated adoption (normally about 23 seconds without a collision).
   - **panel:** `ADOPTED`, `node <N>`, `press = back`;
   - **console:** `ADOPTED id=<N>` / `join_adopted`, with `<N>` non-zero;
   - `whoami` reports the same `<N>`.
5. ★★ **Layer-17 discriminator:** `cfg` must retain the **full layer 17**, while `whoami`/wire filtering uses
   **leaf 1**. The result must still complete. A panel left forever on `JOINING` after the console adopted is the
   full-byte-versus-nibble correlation regression.

### 30.4 — leaving the waiting screen does not cancel or hijack the UI later

1. Start the same profile again. As soon as `JOINING` appears, short-press once **before the panel blanks**.
2. **Pass immediately:** the child provisioning menu returns. There is no cancel/rollback command and no second
   write caused by leaving the screen.
3. Leave the radio running and watch the console. DAD must continue and eventually adopt a non-zero id.
4. **Pass after adoption:** the UI remains on whatever screen the operator selected; the late correlated adopt does
   not force navigation to `ADOPTED`. `whoami`/`cfg` nevertheless show the completed join.

### 30.5 — durability and actual network service

- [ ] Power-cycle the joiner. `cfg` must still show layer 17 / leaf 1, the same PHY and a non-zero adopted node id.
- [ ] While that boot settles, watch the panel: **the boot's own DAD re-adopt (`join_adopted` fires for boot DAD
  too) changes NOTHING on screen** — no `ADOPTED` appears uninvited (correlation term 1 against the REAL boot push;
  the host gate only ever synthesizes it).
- [ ] Wait for routes/config sync. From the joiner send a unique plaintext DM to `<PEER_ID>`; verify application
  delivery on the static peer, not merely CTS/ACK at an intermediate hop.
- [ ] Send a unique plaintext DM back to the joiner's adopted id; verify it appears on the joiner and in its inbox.
- [ ] Re-open JOIN NETWORK after reboot. Slot 3 and its four-decimal frequency must still render exactly as in 30.2.

### 30.6 — conditional observations, not forced failures

- **`STILL JOINING`:** ordinary DAD normally completes before 60 seconds. If a genuine collision/retry naturally
  keeps the operation active past 60 seconds, let the panel blank, then short-press once. It must show
  `STILL JOINING`, ⛔ not a failure, and the operation must continue. If adoption completes earlier, mark this metal
  arm **not-run: normal adoption completed below 60 s**; do not manufacture a collision or saturate the leaf merely
  to obtain the string. The real-renderer host probe is the mandatory control for this arm.
- **Emergency pre-emption:** already mutation/probe-gated. Exercise it on metal only in a controlled RF bench where
  transmitting/cancelling the emergency cannot be mistaken for a real alarm.
- **Unreadable `/mrjoin` and physical save failure:** neither is safely operator-reachable. Preserve their native
  evidence; do not damage NVS/LittleFS to manufacture them.
- **Power-cut atomicity:** remains Part 20.5 for `/mrcfg`; the `/mrjoin`-specific power-cut qualification is the
  separate UI-15 slice-7 gate. Run destructive power-cut work last.

### 30.7 — stop rules and retained evidence

Stop and preserve both boot-to-failure logs plus the flashed ELF if any of these occurs:

- BACK emits a J claim or changes configuration;
- the panel says `JOINED`/`ADOPTED` before the console adoption;
- layer 17 adopts on the console but the screen never correlates it;
- one wake press exits the waiting screen instead of revealing it;
- leaving `JOINING` cancels DAD or a late adopt steals the current screen;
- the node reboots into a different layer/PHY/id, or either direction of the final DM check fails.

Record each checkbox as PASS / FAIL / NOT-RUN with the exact reason. For a failure, retain the profile listing,
before/after `cfg`, `whoami`, `status`, radio trace, OLED photograph and artefact hashes.

## Part 31 — [[B231]]/[[B233]]: inbox order + delete-refresh ON GLASS (2026-08-20)

⛔ **THE RESIDUE ONLY.** The reversal, the latch, the counted single-pull and both delete arms are host-gated
(`test_firmware_ui_model.cpp` M92-M96 + the probe's no-press catch-up check). **What no host reaches: pixels on
glass and real time** — that the reorder and the time-only refresh LOOK right on the panel.

1. With ≥2 channel posts and ≥2 DMs stored: INBOX shows the DM block first, then the channel block (unchanged),
   and **within each block the NEWEST is at the TOP**. Post one more channel message ⇒ it appears at the TOP of
   the channel block; the highlight stays on the record it was on (pushed one row down, not re-targeted).
2. **Delete a MIDDLE row** from its detail ⇒ back at the list, the deleted row is **GONE with NO further press**
   (time only — within ~1 s at the 2 Hz cadence), the highlight beside where it was.
3. **Delete the LAST row of a block** ⇒ same: gone, no press, highlight on its predecessor (the arm that worked
   pre-fix must still work).

## Part 32 — §UI-17 S4: a lit TEAM screen's ages turn on their own (2026-08-21)

⛔ **THE RESIDUE ONLY.** The bucket⇄token agreement, the raise-only invalidation, the body gate and the throttle
interaction are host-gated (`test_firmware_ui_team.cpp` + `--target=uiteam` T05-T16 + probe P18d/P18e). **What no
host reaches: real time on real glass, and the power cost of the ~1 repaint/s a second-scale age asks for.**

1. On H1 with ≥1 teammate, enter TEAM and watch one row's age column for ~90 s **without pressing**. Expected:
   the token advances on its own (`5s` → `6s` → … → `1m`) and never flickers between two values.
   ⛔ FAIL if the age freezes at the value it had when the screen was opened — the pre-S4 (F-8) behaviour.
2. Leave the node alone until the panel blanks, then confirm over USB from `status` that **sleeps are still
   accumulating** (`slept=` climbs). ⛔ FAIL if idle sleeps stop — the repaint cadence must die with the panel.

## Part 33 — §UI-17 S5: distance/direction on glass (2026-08-22)

⛔ **THE RESIDUE ONLY.** The four show/blank terms, the 600 s edge, the octants, the antimeridian fold, the
integer-first precision rule and the zero-traffic counters are host-gated (`test_firmware_ui_geo.cpp` +
`--target=uigeo` G01-G18 + probe P19a-P19d). **Metal-only: real radio, real time on glass.**

1. Follow the UI-17 spec §7.3 steps 4-8 verbatim (the sealed located send `send 0x<H1-hash> "hi" -t -a -e -l`;
   the ten-minute stale window with H2 kept **alive and beaconing**; the five-minute STATUS-vs-TEAM
   outbound-baseline comparison; `cfg set lat 0`/`lon 0`; the coincident pair).
2. Expected TEAM row shape: `<name≤6> <route age> <dist> <dir>`, e.g. `Wolfga  3m 850m  NE`. Past ten minutes the
   row **REMAINS** and both right-hand columns go BLANK on the clock alone.
   ⛔ FAIL on: a distance surviving past the freshness bound · `0m` for an uncached peer (a miss is BLANK — a
   real and different answer) · any cardinal letter beside a coincident `0m`.

## Part 34 — §UI-17 S8: wake-on-receive on glass (2026-08-22)

⛔ **THE RESIDUE ONLY.** The scope (`msg_recv` any `enc`; `channel_recv` only with `enc`), the separate deadline,
the untouched input clock, the no-navigation / no-emergency-write invariants and the quiet-node sleep guard are
host-gated (`ui17-wake:` cases + `--target=model` S08-S17 + `--target=uisend` U01-U06 + probe P20a-P20e with
controls C119/C120). **Metal-only: a real radio, a real dark panel, real sleep accounting.**

1. Follow UI-17 spec §7.8 steps 1-7 verbatim.
2. Expected: within one repaint the panel **lights by itself on the screen that was current**, envelope count up
   by one. ⛔ FAIL if it switched screens or opened anything.
3. ⛔ **Step 2b is the one that must not be skipped:** a post arriving **unsealed** on the same channel id leaves
   the panel **DARK** while the unread count still moves. A panel that lights there is the `enc` gate missing —
   and with it §8.15's *"a stranger's post does not light a dark panel"*.
4. Record for the owner (⛔ not pass/fail — spec §9 R-6 / F-10): `slept=` before the wake and two minutes after it
   re-blanked, plus how many messages arrived in that window.

## Part 35 — §UI-16 K1/K2: the `/mrteams` keyring on real flash (2026-08-22)

⚠ **PRECONDITIONS:** the K2 `/mrcfg` **v24** bump means the first boot after this flash comes up **UNPROVISIONED**
— reprovision before starting, and `cfg set sf_list 6,7` first or `team new` refuses ([[B230]]).
⛔ **THE RESIDUE ONLY.** The five-term restore predicate, the governance funnel, the coalescing, P-15 and the
policy set are host-gated (`test_firmware_team_keyring.cpp` + `--target=teamkeyring` 25 entries +
`--target=provservice`). **Metal-only: real flash, real reboots, the power cut.**

1. ☐ **The boot line exists and is honest.** Reboot ⇒ the startup block prints one line beginning `  team key  = `.
   Fresh chip: `  team key  = no active binding (no saved key in use) | live key: none`. ⛔ No key material on any
   arm, ever.
2. ☐ **★★★ THE B240 FALSIFIER.** `team new freq=869.4625 sf=7 bw=125` ⇒ applied; `team exportkey` ⇒ a key.
   **Power-cycle.** ⇒ boot prints `  team key  = restored from NV (/mrteams) | live key: YES`, `team exportkey`
   returns **the same key**, and a sealed post from a teammate is readable. ⛔ Pre-slice this FAILS by construction
   — record the result either way.
3. ☐ **Retained, not reactivated (P-2b).** `team 0` ⇒ keyless. Reboot ⇒ `no active binding`, keyless. Re-join the
   **same** id and reboot ⇒ **still** `no active binding`, still keyless. ⛔ FAIL if the key returns on its own.
4. ☐ **A FULL keyring fails loudly (P-15).** `team new …` five times: the **fifth** prints `> team err: KEYRING
   FULL — this node already stores team keys for 4 teams, and a key is NEVER silently dropped to make room.` +
   `>   NOTHING changed — no team was joined, no key was stored, and no stored key was replaced or lost.` Then
   `cfg` still shows the **fourth** team and its key still exports. ⛔ FAIL on any silent eviction or a fifth join.
5. ☐ **⛔⛔ THE POWER-CUT** ([[B193]]'s class, one record over). With three teams stored, `team new …` and **cut
   power within a second**; ~5 attempts, varying delay. Each reboot: `team exportkey` + the boot line show
   **either the complete old state or the complete new one** — ⛔ never a half record, never `REJECTED — the
   stored record does not verify`, never a keyring that lost a *previously stored* team. Any `UNREADABLE` /
   `REJECTED` ⇒ a REAL finding: record the repro, ⛔ do not "fix" from the store layer.
6. ☐ **Failed-re-key honesty (the QG blocker-2 arm, on metal):** if a `/mrcfg` save failure can be observed in the
   field (it cannot be safely induced — conditional, record not-run otherwise): the console reports failure AND the
   next reboot prints `NOT COMMITTED — the saved key was never confirmed (re-grant or re-key)` and boots keyless —
   ⛔ never silently activating the failed request.
7. ☐ **Factory reset erases it.** `factory_reset confirm`, reboot ⇒ `no active binding … live key: none`; a new
   `team new` succeeds with exactly one record (the four old ones are gone, not "full").
8. ☐ ⓘ **Flash WEAR remains unmeasured** — identical-material re-puts write nothing (counted natively); operator
   re-key frequency is the wear question and this part does not answer it.

## Part 36 — §UI-16 N2: the NEARBY scan on glass (2026-08-23)

⛔ **THE RESIDUE ONLY.** The list model, own-team filter, row format, four-tier signal, first-observed order,
blank/wake retention and the zero-TX rule are host-gated (`test_firmware_ui_nearby.cpp` + `--target=uinearby` 11 +
`--target=uinearbyrow` 7 entries + probe P21 incl. the zero-TX assertion). **Metal-only: real beacons over real
air, the SSD1306's own rendering, the pre-parse leaf drop (F-1), and the 10-minute window on a real clock.**

Setup: H1 = teamless joiner, H2 = team owner, **same PHY and same leaf nibble**.

1. ☐ H2 `team new …` ⇒ `team_id=0x<TEAMID>`; H1 `team` ⇒ `team_id=0x00000000`.
2. ☐ H1: SETTINGS → PROVISION ⇒ `CREATE TEAM` · `JOIN NETWORK` · **`JOIN TEAM`** · `BACK`. ⛔ FAIL if
   `JOIN TEAM` is absent.
3. ☐ `double` on `JOIN TEAM` ⇒ **the list opens directly** (⛔ not a submenu):
   `NEARBY` / `CURRENT PHY ONLY` / `SAME RADIO + LEAF` / `>XXXXXX n/3 Ns` / ` BACK`, where `XXXXXX` = the
   **last six hex digits** of `0x<TEAMID>`. ⛔ FAIL on any name-shaped text, or if the six digits differ from the
   console's id.
4. ☐ **Name negative:** on H1 `peername 0x<H2-hash> "Wolfgangetta"`, re-enter ⇒ the row **still** reads the
   six-hex TEAM fingerprint. ⛔ FAIL if `Wolfga` appears anywhere on the panel.
5. ☐ ★ **LEAF-NIBBLE NEGATIVE (F-1) — no host gate can reach the pre-parse drop.** Change H1's `leaf_id` to a
   different nibble, reboot, re-enter ⇒ **`NO TEAMS NEARBY`** although H2 is beaconing on the same frequency.
   Restore the nibble ⇒ the row returns within one team-beacon period. This is the line `SAME RADIO + LEAF`
   exists for.
6. ☐ **Frozen per entry:** stay in the list past a beacon period ⇒ the age does **not** tick and no row appears
   or moves; leave and re-enter ⇒ it refreshes (manual refresh only).
7. ☐ **Read-only + zero TX:** after the walk, `peers`, the team-route listing and `team` unchanged
   (`0x00000000`); five minutes idle on STATUS vs five minutes entering/leaving NEARBY ⇒ no additional query,
   DATA or join-shaped transmission. Power-cycle H1 ⇒ the observation is gone (RAM-only) and returns within one
   beacon period.
8. ☐ **Own-team filter:** H1 `team 0x<TEAMID>` (the typed-id path), re-enter NEARBY ⇒ that team — now H1's
   **own** — is **no longer listed**: `NO TEAMS NEARBY` + a `BACK` row that still leaves. ⛔ FAIL if the own
   team's fingerprint shows. Then `team 0` to restore the teamless setup if continuing.
9. ☐ **Retention (empty-list BACK):** power H2 off, wait past **10 minutes** ⇒ `NO TEAMS NEARBY` and a `BACK`
   row that still leaves.

## Part 37 — §UI-16 N3: the confirmed nearby join on glass (2026-08-23)

⛔ **THE RESIDUE ONLY.** The confirmation screen, the full-id act, the words, the refusals, keylessness and the
zero-TX rule are host-gated (native §UI16-N3 blocks + `--target=uiprov` 32 / `--target=model` / probe P22 incl.
the PHY DIFFERS and BACK-performs-nothing arms). **Metal-only: real flash across a power cycle, a real sealed
post, a real retune.** Setup: H1 joins H2's team **through NEARBY** (Part 36 setup, same PHY + leaf).

1. ☐ **Membership survives a real power cycle.** H1: NEARBY → row → `double` → `short` → `double` ⇒
   `TEAM JOINED`, `0x<TEAMID>`, the six-hex fingerprint. Console `team` ⇒ `team_id=0x<TEAMID>`. **Pull power**,
   reboot ⇒ `team` ⇒ the same digits. ⛔ FAIL on `0x00000000` or any differing digit.
2. ☐ **Keyless against a real sealed post.** H1 `team exportkey` ⇒ the JSON error carrying `"no_key"` (⛔ never a
   keypair). From H2 `send_channel 0 "sealed hello" -t -e` ⇒ H1 prints the `: ENCRYPTED — no team content key …
   The post was still RELAYED.` line and ⛔ no body.
3. ☐ **A RETAINED `/mrteams` key is not reactivated by the join (P-2b — K5 not anticipated).** Precondition: H1
   was granted this team's key earlier, then `team 0` (record retained). Re-join through NEARBY ⇒ `TEAM JOINED`,
   and ⛔ the panel NEVER shows `SAVED KEY FOUND` / `USE SAVED KEY`. `team exportkey` ⇒ `"no_key"`;
   **power-cycle** ⇒ still `"no_key"`, and the record is still there (a later re-grant of the same material must
   report `unchanged`, i.e. spend no write).
4. ☐ **`PHY DIFFERS` from a real retune.** H1 `mobile register freq=<a different legal frequency>`, then a
   NEARBY join attempt ⇒ `PHY DIFFERS` / `USE SERIAL`, `team` unchanged, zero writes, zero retunes. ⓘ If the
   divergence cannot be provoked on this bench, record **not-run with that reason** — ⛔ never FAIL.

## Part 38 — §UI-16 N4: the invitation window on glass (2026-08-23)

⛔ **THE RESIDUE ONLY** (rows, two-authority snapshot, diff, handled set, freeze, expiry, blank/wake and zero-TX
are host-gated: 24 native cases, `uiinvite` 11 / `model` V01-V09, probe P23a-f). **Metal-only: real light sleep,
the wall clock, real member state.**

1. ☐ H2 (in a team): PROVISION offers **`INVITE MEMBER`** as its fourth row; `double` ⇒ title, H2's six-hex team
   fingerprint, **`NO CANDIDATES`**. ⛔ FAIL on anything name-shaped.
2. ☐ ★ A REAL new member: H1 `team 0` then re-join; open the window **before** H1 re-joins ⇒ row
   `>       T<id> <6-hex>` — name column **BLANK**, fingerprint **POPULATED**, 19 columns unclipped. ⛔ FAIL on
   `KEYLESS`.
3. ☐ ★ **Five-minute hold across REAL light sleep (P-4b + OQ-3)**: panel blanks on the ordinary ~15 s timer, the
   node still light-sleeps (`mrcon` cadence vs a STATUS baseline — the half no host gate reaches), and ⛔ no
   query/DM/post/location request appears.
4. ☐ ★ **Expiry on the wall clock (P-11)**: untouched past 5 min ⇒ wake press, then **`WINDOW CLOSED`**; `team`,
   `team exportkey` and the member list unchanged.
5. ☐ **Blank/wake**: `double` a candidate ⇒ `NEW MEMBER` + full `0x<H1-hash>`; let it blank, press once ⇒ panel
   returns to the **LIST**, window still open (its 5 min still running from the OPEN).
6. ☐ **`REJECT` on real state**: candidate leaves the list, ⛔ nothing sent, H1 still a member and still keyless,
   `/mrteams` untouched.
7. ☐ **Outside the window (P-12)**: window closed, a new member appears ⇒ ⛔ no prompt on any screen.

## Part 39 — §CHROME-5: the duty gauge on glass (2026-08-23)

⛔ **THE RESIDUE ONLY** (the bucket map, boundaries, repaint economy, geometry and authority are host-gated:
`chrome` 44 / `icons` 11 mutations, probe P24a-g + P13 restated). **Metal-only: a real radio actually refusing
to transmit, and whether six 7-px slots with one-pixel gaps are readable on the physical panel.** Extends the
Part 24/25 CHROME series.

1. ☐ `cfg set duty 1` → `cfg save` → **reboot** (⚠ `duty` is *not* a live field — `firmware_config.cpp:313`
   sets `live = false`; without the reboot the budget is not re-derived and the gauge will not move).
2. ☐ Console `duty` prints a percentage; the strip's **sixth slot (x=83..89, between key and battery)** shows a
   partially filled box that grows as traffic runs (`testch @sendms …`).
3. ☐ Drive traffic until the console prints exactly `> duty` → `100% — SILENT, ~<n> s to availability`: the slot
   shows the **full box with the warning mark knocked out of it**, and the node actually refuses to transmit.
4. ☐ `cfg set duty 0` → `cfg save` → reboot: the slot shows the **crossed box**, ⛔ never an empty one.
5. ☐ ⛔ No number and no `%` anywhere on the panel, in any of the three states.
6. ☐ Visual: the key, gauge and battery outlines do not touch (one clear pixel column between each), and the
   battery token still ends on the last column.

## Part 40 — §UI-16 N5: the pubkey request on glass (2026-08-24)

⛔ **THE RESIDUE ONLY** (the preflight, started-gating, refusal-stays, hash-matched enable, name lifecycle and
the no-auto-emission command count are host-gated: `uiinvite` 19 / `model` 161 entries, probe P23d incl. the
real-seam refusal arm). **Metal-only: the real over-the-air WANT_PUBKEY round trip and the console's own view
of it.** The step-by-step walk is the UI-16 spec's **§7.4 steps 1-3b** (N5's half — NEED PUBKEY, the
nothing-aired-without-the-operator negative, the hash-matched GRANT KEY enable, the fingerprint→name column
upgrade); run it there, tick here.

1. ☐ Spec §7.4 steps **1, 2, 3, 3b** ran on H1/H2 over real air. ⛔ FAIL conditions as written there.
2. ☐ **The refusal never claims waiting (the 2026-08-24 QG fix, on metal):** on a node with **no identity**
   (`regen` not run / identity cleared), `short` + `double` on `REQUEST PUBKEY` ⇒ the panel **still reads
   `NEED PUBKEY`** (retry = one press). ⓘ The refusal is a **typed result, not a console line** — the UI's
   `exec_command()` path returns it without printing, so the panel staying put is the whole observable. ⛔ FAIL
   if it reads `WAITING FOR PUBKEY`. ⓘ If a no-identity state cannot be safely produced on this bench, record
   not-run with that reason — ⛔ never FAIL.

## Part 41 — §UI-16 N6b: the grant's dispatch truth on glass (2026-08-24)

⛔ **THE RESIDUE ONLY** (all four dispatch outcomes, both refusals, the re-DAD correlation and the H-vs-DATA
distinction are host-gated: `teamgrant`/`grantadmit`/`grantpark` 6 entries, `uiinvite` I31/I32, `model` V21,
probe P24a's arms, the decoded `ui16-grant-parkfull-air` pin). **Metal-only: the queue-full arm against a real
radio, and the real TxDone edge.** Walk = spec §7.4 steps 3c-6; run it there, tick here.

1. ☐ **`GRANT QUEUE FULL` on glass, if provokable:** with H1's radio busy (a long channel post, or grants to
   members back-to-back inside one window), a `GRANT KEY` the TX queue refuses reads **`GRANT QUEUE FULL`** —
   ⛔ never `GRANT QUEUED`, `GRANT PARKED` or `GRANT FAILED`. Console twin: the queue/ring-FULL refusal line.
   ⓘ If the 8-deep queue cannot be filled by hand on this bench, record **not-run with that reason** — ⛔ never
   FAIL.
2. ☐ **The console prints the resolved dst:** `team grantkey 0x<hash> -t` on a resolved teammate ⇒ the queued
   line carries `ctr=<n> dst=<id>`, and `<id>` equals the teammate's team-local id on the TEAM row.
3. ☐ **`GRANT PARKED` only when really parked:** grant to a teammate in the team but **not heard** ⇒
   `PARKED (resolving…)` on console and `GRANT PARKED` on glass (ⓘ one H lookup may air — that is the
   pre-existing locate, not grant DATA); a grant with **no adopted team id** (`me T--`) ⇒ **`GRANT FAILED`**,
   ⛔ never `GRANT PARKED`.
4. ☐ Spec §7.4 step 5 unchanged: `GRANT QUEUED` first, `KEY SENT` only after the frame leaves the radio.

## Part 42 — §UI-16 K3/K4 + [[B243]]: the grant receipt on glass, all three verdicts (2026-08-25)

⛔ **THE RESIDUE ONLY** (both doors, the three-way `GrantUiRoute` classification, the ruled wording, every
negative and the drain-loop shape are host-gated: `teamkeyring` 40 entries incl. T39-T41, `model` V22-V28,
`uisend` U10-U13, probe P15k/P15k2/P15k3 + K3/K4 controls, `probe_board_ui` W47's seven controls).
**Metal-only: a `/mrteams` write that really refuses on real flash, the real SSD1306, and the power-cut.**
Extends §7.5's Part 35 series; setup = H1/H2 as in Parts 36-41.

1. ☐ **The failure is provokable with SUPPORTED VERBS — ⛔ no fault injection.** On H1: `team new …` **four**
   times (four `/mrteams` records; a fifth `team new` would print `KEYRING FULL`, Part 35 step 4). Then
   `team 0x<H2-TEAMID>` ⇒ H1 is a **member** of H2's team and **keyless** (K5 has not landed; P-2b forbids
   reactivation). `team exportkey` ⇒ nothing.
2. ☐ **The grant arrives and the panel says the TRUE thing.** H2: `team grantkey 0x<H1-hash> -t`. H1's panel
   reads, on three body rows: `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT` (keyring-full is an
   after-live-check failure ⇒ `active_unsaved` — the classification is host-verified, not assumed).
   ⛔ **FAIL if `TEAM KEY RECEIVED` appears anywhere** (B240's harm). ⛔ **FAIL if the panel says nothing**
   (B243's original defect). ⓘ The console half still prints its receipt on every arm — the gate is the
   panel's, not the log's.
3. ☐ **Both halves of the sentence are true.** Immediately: a fresh **sealed** post from H2 IS readable (the
   key really is live). Then **power-cycle H1** ⇒ boot prints `no active binding … live key: none`,
   `team exportkey` returns nothing, the same sealed post is no longer readable. ⛔ FAIL if the key survives.
4. ☐ ★★★ **THE SUPPRESSED ARM — THE PANEL SAYS NOTHING, AND THAT IS THE RULING (QG, 2026-08-25).** ⛔ Do not
   duplicate the setup: **spec §7.5 step 3 already produces it** (`team 0` on H1 ⇒ keyless and out of the
   team). Run §7.5 step 3, and while H1 is in that state have H2 send `team grantkey 0x<H1-hash> -t` ⇒ the
   console still prints its receipt line, and the **panel must not change at all** — ⛔ never
   `TEAM KEY ACTIVE`, ⛔ never `TEAM KEY RECEIVED`. ⓘ Whichever race the node lands in (`not_our_team` if the
   membership went first, `no_live_key` if the wipe did) both classify `suppressed`; the observable is the
   same. ⛔ **FAIL if the panel announces an active key** — a false statement, not merely an unhelpful one.
5. ☐ **⛔ IT NEVER NAVIGATES AND NEVER WAKES.** Repeat step 2 with H1's panel **blanked** ⇒ it **stays dark**;
   one short press wakes it to the screen it was left on, cursor unmoved — ⛔ never a provisioning screen
   (§UI-17 R-7 scoped the wake to a DM addressed to us and a sealed post; a grant receipt is neither).
6. ☐ **The success arm still works (anti-overcorrection).** Free a record (`factory_reset confirm`), re-join,
   repeat the grant with room ⇒ **`TEAM KEY RECEIVED`** and the key **survives** a power-cycle (§7.5 steps
   1-2). ⛔ FAIL if the failure wording appears on a save that succeeded.
7. ☐ ⓘ **`binding_failed` (a `/mrcfg` write failing AFTER the key landed) is NOT provokable by hand** — record
   **not-run with that reason**; host-gated by `teamkeyring` T29.

⇒ spec §7.5 step 2's *"force a save failure"* is now answerable: **the method is step 1 above**, and it needs
no fault injection.

## Part 43 — §UI-16 K5: the saved key on glass (2026-08-25)

⛔ **THE RESIDUE ONLY** (the offer, P-2b, the surgical refusal, the A→B race, the boot predicate and the S-39
wording are host-gated: `teamkeyring` 53 / `uiprov` 40 / `model` 186 entries, probe P22d-g + K5a-c controls).
**Metal-only: the real flash write, its power-cut behaviour, and the real boot path ([[B193]]'s class).**
Setup: node B holds a retained `/mrteams` record for team T (granted earlier, then `team 0`).

1. ☐ **A USED saved key survives the power cycle.** On B: panel `JOIN TEAM` → T's row → `JOIN` ⇒
   `TEAM JOINED`; press ⇒ row0 `SAVED KEY FOUND`, row1 T's six-hex fingerprint, `>BACK` / ` USE SAVED KEY`.
   `short`+`double` ⇒ row0 **`TEAM KEY ACTIVE`**, ⛔ **no second/third row** (S-27's `NOT SAVED` pair belongs
   to the RAM-only screen; this key is durable). **Power-cycle** ⇒ boot prints
   `  team key  = restored from NV (/mrteams) | live key: YES`; a sealed team post is readable. ⛔ FAIL on a
   keyless boot.
2. ☐ **BACK changes no key state, across a power cycle.** Same to the offer, `double` on `BACK` ⇒ the menu;
   STATUS shows no team key. **Power-cycle** ⇒ `no active binding … live key: none`; repeat the join ⇒ the
   offer appears again (BACK wrote and erased nothing — the record is intact).
3. ☐ ★★★ **THE A→B RACE, on metal (supported verbs).** Reach `SAVED KEY FOUND` for team A; on the serial
   console run `team <B>`; then `short`+`double` on `USE SAVED KEY` ⇒ row0 **`KEY NOT INSTALLED`** (S-39),
   row1 the service token (`not_our_team`); `team` shows B's membership and **B's key still present**.
   **Power-cycle** ⇒ the boot line restores **B's** key (`live key: YES`); re-joining A offers
   `SAVED KEY FOUND` again (A's record intact). ⛔ FAIL if B's key was cleared, if A's key went live, or if
   the panel claimed `TEAM KEY ACTIVE`.
