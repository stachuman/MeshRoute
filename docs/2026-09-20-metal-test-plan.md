<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; consolidation: Codex, 2026-09-20 -->
# MeshRoute metal test plan

This is the maintained authority for **hardware qualification** (M2). Procedures were re-triaged against `d11b5a9` on 2026-09-20. No hardware was run during this rewrite. The [disposition map](2026-09-20-metal-test-triage.md) accounts for all 69 former Parts and their companion guides. The [archive](archive/2026-09-20-bench-records/README.md) preserves eight originals, including their results and superseded instructions: the six planned sources plus the overlapping inbox recheck and BLE validation sheets found during the link audit.

## Current results — update only this table

Use `OWED`, `PASS <build> <date>`, `FAIL → B###`, `N/A <reason>`, or `RETIRED → <instrument>`. A build means the recorded source snapshot **and flashed image hash**. Each PASS covers only its named board/backend and every required step; split a row by board or arm when recording different results. A missing fixture or an unobserved precondition stays OWED with a note. N/A requires an actual scope reason, not lack of time. Old passes do not qualify today's image; they remain in the archive. Register bug closure in the bug register, linking the session record here rather than copying a second bench-status table.

| Scenario | Scope | Current result |
|---|---|---|
| [BOOT-01](#boot-01) | image, roles and basic provisioning | OWED |
| [BOOT-02](#boot-02) | fresh profile-store classification | OWED |
| [DM-01](#dm-01) | keys, names, sealed location and durable delivery | OWED |
| [DM-02](#dm-02) | physical payload boundaries | OWED |
| [DM-03](#dm-03) | real TxDone, relay silence and short terminal CTS | OWED |
| [TEAM-01](#team-01) | create, re-key, live retune and persistence | OWED |
| [TEAM-02](#team-02) | channel sealing, grant and retained keys | OWED |
| [TEAM-03](#team-03) | on-device refusal and remedy text | OWED |
| [MOBILE-01](#mobile-01) | attachment, boot order and simultaneous mobiles | OWED |
| [MOBILE-02](#mobile-02) | explicit dormancy and boot policy | OWED |
| [MOBILE-03](#mobile-03) | weak-home policy, loss and redirect | OWED |
| [MOBILE-04](#mobile-04) | expiry, beacons and released IDs | OWED |
| [MOBILE-05](#mobile-05) | real admission pressure and equal-counter flights | OWED |
| [MOBILE-06](#mobile-06) | confirmation-age width at the device boundary | OWED |
| [UI-01](#ui-01) | panel, geometry and blanking | OWED |
| [UI-02](#ui-02) | inbox identity, paging and delete | OWED |
| [UI-03](#ui-03) | settings draft, conflict and real save | OWED |
| [UI-04](#ui-04) | create team on glass | OWED |
| [UI-05](#ui-05) | join a static network from sparse profiles | OWED |
| [UI-06](#ui-06) | nearby teams and keyless join | OWED |
| [UI-07](#ui-07) | invitation window | OWED |
| [UI-08](#ui-08) | pubkey ceremony and actual grant airtime | OWED |
| [UI-09](#ui-09) | grant receipt with a full keyring | OWED |
| [UI-10](#ui-10) | explicit saved-key activation | OWED |
| [UI-11](#ui-11) | forget key and keyring-full remedy | OWED |
| [UI-12](#ui-12) | presets on actual transports and flash | OWED |
| [UI-13](#ui-13) | live ages, position and selected identity | OWED |
| [UI-14](#ui-14) | receive wake and retained send result | OWED |
| [UI-15](#ui-15) | ordinary sends and completion | OWED |
| [UI-16](#ui-16) | emergency air, budget, blocking and location | OWED |
| [UI-17](#ui-17) | emergency isolation, reply wake and safe exit | OWED |
| [UI-18](#ui-18) | duty gauge | OWED |
| [UI-19](#ui-19) | non-team OLED profile | OWED |
| [UI-20](#ui-20) | identity-label glyphs, abbreviation and unnamed peers (W4a) | OWED |
| [CUSTODY-01](#custody-01) | static relay failure, persistence and deletion | OWED |
| [CUSTODY-02](#custody-02) | translated report to the originating mobile | OWED |
| [CUSTODY-03](#custody-03) | lost correlation and optional re-home | OWED |
| [USB-BLE-01](#usb-ble-01) | complete console, help and removed verbs | OWED |
| [USB-BLE-02](#usb-ble-02) | NUS lengths, chunking and raw validation | OWED |
| [USB-BLE-03](#usb-ble-03) | regen replies and real identity write | OWED |
| [USB-BLE-04](#usb-ble-04) | secured link, bond and SoftDevice/RF coexistence | OWED |
| [RADMIN-01](#radmin-01) | target trust on physical USB | OWED |
| [RADMIN-02](#radmin-02) | controller target book and public pages | OWED |
| [RADMIN-03](#radmin-03) | dedicated key lifecycle and secured public boundary | OWED |
| [RADMIN-04](#radmin-04) | interrupted trust writes and explicit recovery | OWED |
| [RADMIN-05](#radmin-05) | persisted activation delay | OWED |
| [RADMIN-06](#radmin-06) | actual mobile request and carrier acknowledgement | OWED |
| [RADMIN-07](#radmin-07) | silence, custody and exact retry | OWED |
| [RADMIN-08](#radmin-08) | retained local result, reconnect and ACK debt | OWED |
| [RADMIN-09](#radmin-09) | scheduled hardware actions and halt recovery | OWED |
| [RADMIN-10](#radmin-10) | v26 migration preserves separate trust | OWED |
| [RADMIN-11](#radmin-11) | nRF52 task stack qualification | OWED |
| [POWER-01](#power-01) | button/radio wake, long holds and reset boundary | OWED |
| [POWER-02](#power-02) | sleep-count endurance | OWED |
| [POWER-03](#power-03) | battery scale, divider and RF coexistence | OWED |
| [NV-01](#nv-01) | inbox, cursor and tombstone durability on both backends | OWED |
| [NV-02](#nv-02) | inbox capacity, write timing and torn append | OWED |
| [NV-03](#nv-03) | inbox migrations, separately identified fixtures | OWED |
| [NV-04](#nv-04) | clear, prep-restart and factory-reset domains | OWED |
| [NV-05](#nv-05) | settings and join-profile power cuts | OWED |
| [NV-06](#nv-06) | keyring compaction and preset power cuts | OWED |
| [NV-07](#nv-07) | physical fault classification and write observation | OWED |
| [RADIO-01](#radio-01) | each available board's real RF and USB | OWED |
| [RADIO-02](#radio-02) | V4 physical fault and conducted-output fixtures | OWED |
| [AUTO-01](2026-09-20-metal-test-triage.md#auto-01) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-02](2026-09-20-metal-test-triage.md#auto-02) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-03](2026-09-20-metal-test-triage.md#auto-03) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-04](2026-09-20-metal-test-triage.md#auto-04) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-05](2026-09-20-metal-test-triage.md#auto-05) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-06](2026-09-20-metal-test-triage.md#auto-06) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-07](2026-09-20-metal-test-triage.md#auto-07) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-08](2026-09-20-metal-test-triage.md#auto-08) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-09](2026-09-20-metal-test-triage.md#auto-09) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-10](2026-09-20-metal-test-triage.md#auto-10) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-11](2026-09-20-metal-test-triage.md#auto-11) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| [AUTO-12](2026-09-20-metal-test-triage.md#auto-12) | Named pure subchecks only; exclusions in disposition map | RETIRED → linked source-validated instrument |
| EXCL-01 | Removed UI-5 splash / no-send and no-join stubs | N/A removed behavior; live replacements UI-01/04/05/15 |
| EXCL-02 | MH-09 weak-only proactive canvass positive | N/A B178 feature not implemented |
| EXCL-03 | Stock BLE settings-row/restart badge and same-image OLED catalog over BLE | N/A feature combination absent from available stock images |

## Rig, evidence and order

| Label | Available board | Images used in this plan |
|---|---|---|
| H1, H2 | Two Heltec V3s | `heltec_mobile`; `heltec_v3` for static roles; `gateway_heltec` for the non-team display |
| X | XIAO ESP32-S3 | `xiao_esp32s3`, `xiao_esp32s3_mobile` |
| V | Heltec V4; record actual revision | `heltec_v4`, `heltec_v4_mobile`, `gateway_heltec_v4` |
| N | XIAO nRF52840 + SX1262 | `xiao_sx1262`, `xiao_mobile`, `gateway` |

Assign logical roles A/B/M/home/relay/target for each scenario; labels do not imply a permanent role. A home is an ordinary **single-layer static** node, never a gateway. BLE-NUS runs on N; stock ESP32 images have no BLE transport. One N can be reflashed between CLIENT and ACCEPT sessions. The key export/import test can use H1 as the second CLIENT. One V4 does not qualify both V4.2 and V4.3. Record the radio hardware fitted to X; an MCU alone is not a LoRa fixture.

Use the bench's chosen carrier, legal power/duty settings, and an antenna or rated load throughout. In commands below replace `<F>` with that frequency in MHz, `<ID>` with an observed decimal routing ID, `<HASH>` with `0x` plus the full eight hex digits, and `<TAG>` with a unique session tag. `<T>` is a full team ID, not a six-digit display fingerprint. Keys in setup are disposable test keys. Capture public identifiers; redact private seed/key exports from retained logs.

Build/flash with the matching `pio run -e <env>` and upload method in the [firmware guide](firmware-dev-guide.md). Archive the **actual** ELF, map and flashed BIN/HEX/UF2/ZIP files, their SHA-256 sums, base commit, tracked diff and every untracked build input. An uncommitted snapshot is valid; **a commit is not a prerequisite**. A `-dirty` banner alone does not identify a snapshot. Do not rebuild after flashing and call the new ELF the flashed one. Record `version`, `whoami`, `cfg`, `status`, board revision, ports and UTC time in every session. Stop if a banner is `nogit`, an image is unidentified, or the intended PHY/roles do not match. Never overlap builds with another agent editing their inputs.

Suggested sessions: BOOT and USB first; direct DM and TEAM; OLED with the team intact; MOBILE then CUSTODY; remote trust then remote execution; headless sleep on a fresh boot; storage destruction and power cuts last. BOOT-02 needs a dedicated fresh-flash fixture before ordinary provisioning. Re-provision after any test that leaves a team, changes PHY, regenerates identity or resets storage. Use the result table to select a session; this is not a command to run every test consecutively.

Read `short` as a short button press and `double` as a double press. Long holds belong to emergency. Capture photos/video for glass and timestamps for radio/power timing. A command's `queued`, a hop ACK, an `AIRED` event, application delivery and durable storage are different observations. Match the identity/counter/sequence at each boundary. Hardware faults go to the [register](2026-07-30-open-bug-register.md) with the raw failure, before changing the setup.

## Boot and provisioning

<a id="boot-01"></a>

### BOOT-01 — image, roles and basic provisioning

1. Record the rig/evidence fields above on each image. A normal boot completes without reset loops; the durable inbox banner ends `enabled=1` and has no unprovoked `mount_fault`.
2. On a disposable static peer: `create layer=17 freq=<F> bw=125 sf=7 sf_list=6,7 duty=1 name="Metal"`. On another ordinary node: `join layer=17 freq=<F> bw=125 sf=7`. Wait for nonzero adopted IDs; `whoami` reports leaf 1 and `cfg` retains full layer 17.
3. Exchange `send <ID> "<TAG>-boot"` in both directions. Check received bodies, not only hop ACKs. On a gateway, `cfg set host_mobiles on` must refuse; never use that image as a home.
4. On N, record `cfg`, issue `team 4294967296`, then `cfg` again: a `bad target` refusal and no team change. This is the real 32-bit overflow check.

**PASS:** identified images boot, provision and deliver on the intended plane, and the 32-bit refusal preserves state. **STOP:** reset, wrong plane/identity, silent overflow acceptance or failed delivery.

<a id="boot-02"></a>

### BOOT-02 — fresh profile-store classification

Fixture: disposable ESP32 flash known never to have held the `mr` namespace, or fully erased before flashing. `factory_reset confirm` alone does not prove that condition.

1. Capture first boot and make `joinprofile list` the first operator command, before `cfg set`, `regen` or provisioning.
2. Expect `> joinprofile NO PROFILES`, not a storage failure. Record how absence of the namespace was established; boot itself may initialise other records, so an unproved fresh-namespace premise does not qualify the classifier.
3. Run `joinprofile set 1 layer=4 freq=<F> bw=125 sf=9 name="hut"`, then list; the row must exist. Power-cycle and list again; the same slot and frequency survive.

**PASS:** proved fresh-state classification plus a surviving real write. **STOP:** storage failure on proved absent state, or loss of the acknowledged slot. Without the premise, leave the classifier arm OWED.

## Direct messages and team channel

<a id="dm-01"></a>

### DM-01 — keys, names, sealed location and durable delivery

1. A/B share a PHY and routing plane. On A: `reqpubkey <HASH-B>`; wait for B's authoritative key. `peers` must show the authoritative binding; `peers all` may additionally contain weaker observations.
2. `peername <HASH-B> "MetalPeer"`; record `peers`, reboot A, repeat. The key, confidence and name survive; an ordinary reboot must not wipe the peer store.
3. Set A's coordinates with `cfg set lat 50.0` and `cfg set lon 20.0`. Send `send <HASH-B> "<TAG>-located" -a -e -l` (add `-t` only on the team plane). B's `pull_inbox 0 0` contains that body, sender identity and location once; A receives its matching E2E ACK.
4. Zero both coordinates and repeat with `-l`: expect `no_location`, no located plaintext leak. Restore the recorded coordinates. Power-cycle B: the successful record, sequence and epoch survive.
5. On a spare peer slot, `peerkey <64hex-public>` then reboot: the pinned key remains pinned. Repeating a cache must not downgrade it; exact merge/write-count permutations belong to AUTO-02.

**PASS:** the real key exchange, flash restoration, sealed located delivery and no-fix refusal agree. **STOP:** wrong identity, cleartext location, duplicate body or lost acknowledged record.

<a id="dm-02"></a>

### DM-02 — physical payload boundaries

1. Use an authoritative hash binding, `cfg set e2e_dm 0`, `cfg set intro_attach 0`; retain old values. Generate ASCII bodies of exactly 231, 232 and 233 bytes on the host and record their lengths.
2. Static A: `send <HASH-B> "<233 bytes>"` → `err_too_large`, no originating RTS/queue/park event. `send <HASH-B> "<232 bytes>" -a` must queue and deliver all 232 bytes with nonzero `sender_hash` in B's raw inbox.
3. Repeat the 232-byte positive from a confirmed mobile via its home, with target unresolved locally. Prove the `MOBILE_SEND` leg; the home gains no application record, B sees the mobile's hash, and the mobile receives its original counter's E2E ACK.
4. For a separate first-contact plaintext fixture, turn `intro_attach` on and use a target with no cached peer key, so the enclosed-type wrapper is actually selected. At 232 bytes require `dm_inner_too_large`/`too_large`; at 231 bytes require a real delivery. An already-resolved ordinary wrapper does not exercise this arm.
5. Restore both policy values.

**PASS:** observed wrapper paths meet their respective boundary and deliver complete bodies. **STOP:** oversized airtime, truncated body, wrong sender hash or ACK counter.

<a id="dm-03"></a>

### DM-03 — real TxDone, relay silence and short terminal CTS

1. Capture USB `status`, run `testsend <ID-B> 200 @sendms 20`, let it drain, then `status`. Record `txq`, `txdrop`, `txfail`, `txoutdrop`, `txto`, including zeros; `txq` returns to zero without wedging.
2. `send <ID-B> "<TAG>-aired"` and a team `send_channel 0 "<TAG>-aired" -t`: match each `AIRED ctr=<n> dst=<id|0>` to its complete origination counter. Retransmission may repeat AIRED. A node relaying only must produce no locally owned AIRED for that flight.
3. With a passive trace/controlled attenuation fixture, lose the ACK **after B has received DATA**, preserving B's RAM. Observe A's repeated RTS and B's terminal CTS: 6 bytes plaintext, 7 bytes sealed. A completes without a second DATA; B has one application record. Repeat both carriers.
4. If no lost-ACK event or byte-length capture is obtained, leave that arm OWED; a second `send` creates another flight and is not a retry control. Never unplug an antenna to create it.

**PASS:** hardware completion is correlated, queues recover, and both observed retry arms deliver once. **STOP:** false/foreign completion, duplicate delivery, or an unexplained wedged transmitter.

<a id="team-01"></a>

### TEAM-01 — create, re-key, live retune and persistence

1. On a scratch node persist `cfg set sf_list 6,7`, `cfg set mobile_autoregister 0`, then reboot. `team new freq=<F> sf=7 bw=125` applies, promotes a static node if needed, and reports a nonzero team-local ID after DAD. `mobile status` stays dormant with `home_desired:false`.
2. Export the test team's key locally. Repeat `team <T>`: expect `> team: no change` with matching live state. Re-key using `team <T> tkpub=<64hex> tkpriv=<64hex>`; membership/local ID stay unchanged and no team-DAD is triggered. Power-cycle and verify ID and key.
3. Retune live only using `mobile register freq=<F2> sf=9 bw=250`, where F2 is a second bench frequency. Apply `team <T> freq=<F> sf=7 bw=125`: require an actual `> team PHY:` ending `sf_list=6,7`, not `no change`, and no same-membership DAD burst. Reboot: the DATA set is still 6,7.
4. A genuine `team new` is the positive DAD control: `team-DAD: local_id=<n>` agrees with the announced ID. `team 0` leaves while preserving PHY. Restore the intended team afterwards.
5. On N record `status`'s `stackhw` before/after creation and a repeat: first-call decrease is expected; record it, require responsiveness and comfortably more than about 1 KB remaining. ESP32 has no equivalent field.

**PASS:** real NV/radio state follows the transaction, with preserved ID/SF set and adequate observed nRF52 stack. **STOP:** reset, unexpected home-seeking, state loss or a stack reading under a few hundred bytes.

<a id="team-02"></a>

### TEAM-02 — channel sealing, grant and retained keys

1. A owns a team key; B joins `team <T>` keyless on the same PHY/leaf. A: `send_channel 0 "<TAG>-sealed" -t -e`. B reports `ENCRYPTED — no team content key` and no readable body, while relay activity remains possible.
2. Exchange B's authoritative public identity; A: `team grantkey <HASH-B> -t`. Observe grant dispatch and actual B receipt; do not equate a hop ACK with installed key.
3. Repeat the sealed post. B's `pull_inbox 0 0` contains an `inbox_channel` body with `enc:true`; power-cycle B, require `team key  = restored from NV (/mrteams) | live key: YES` and another readable sealed post.
4. A: `cfg set team_channel_crypt 0`, then `send_channel 0 "<TAG>-plain" -t`. Verify a plaintext post; reboot A and require the default sealing policy restored. Restore the session's policy/keys.
5. B: `team 0`, then `team <T>` and reboot. Knowledge of the team ID alone must not reactivate the retained key. Explicit saved-key use belongs to UI-10.
6. Conditional B30 alias fixture: `peers all` must prove one hash's fresh authoritative team ID plus `+1 stale team-id alias dropped`. `team grantkey <fresh-ID> -t` must use that fresh final `dst=`, never the stale ID; `to=` may be another next hop. Only the final member installs the key. A routine grant without the stale-alias premise does not qualify B30; keep that arm OWED until the fixture exists.

**PASS:** ciphertext, grant, actual decryption and persistence are distinguished; no implicit key reactivation. **STOP:** plaintext leakage, wrong-recipient key installation, false success or key loss after acknowledged save.

<a id="team-03"></a>

### TEAM-03 — on-device refusal and remedy text

1. In a team, record `cfg`; issue `team 0 freq=<F>`, `team 0 sf=7`, `team 0 bw=125`. Each starts `> team err: freq=/sf=/bw= make no sense on \`team 0\` (leave) — leaving a team PRESERVES the current PHY.` and explains that nothing changed. Membership/PHY remain unchanged.
2. `team 0 freq=<F> wibble=3` still names `bad/unknown key: wibble`; a bare `team 0` leaves; an out-of-range `team new freq=99999 sf=7 bw=125` still refuses. These are controls for a handler that bypassed all validation.
3. On a scratch node with proved empty persisted DATA SF set, both `team new freq=<F> sf=7 bw=125` and `team 0x12A1B2C3 freq=<F> sf=7 bw=125` say `the MISSING part is this node's \`sf_list\`` and `set it FIRST: \`cfg set sf_list 6,7\`, then retry your original \`team\` command.` Neither changes state or tells a joiner to create another team.
4. Follow that remedy, retry the same command and require an applied `> team PHY:` with `sf_list=6,7`. Restore intended membership. This is the real handler/output check; AUTO-04 owns the exhaustive pure decision matrix.

**PASS:** real refusal and working remedy preserve the operator's intended operation. **STOP:** wrong state change, swallowed unknown token or a remedy that still cannot succeed.

## Mobile with a home

<a id="mobile-01"></a>

### MOBILE-01 — attachment, boot order and simultaneous mobiles

1. Use a non-gateway static home with `cfg set host_mobiles on`; M has `cfg set mobile 1`, `cfg set mobile_autoregister 0`, then reboot. `mobile register freq=<F> sf=7 bw=125` starts the session.
2. Poll `mobile status`: claiming still means `registered:false`; attached means `registered:true`, `home_link:"confirmed"`. `cfg` says `mobile-reg: UNREGISTERED (claiming)` or `REGISTERED home=<id>` accordingly. A missed claiming window is not evidence for that arm.
3. Home `cfg` shows a `DIRECT` row matching M's hash/local ID; with trace enabled M prints `mobile ATTACHMENT CONFIRMED by the home roster`. Confirmation age is absent before any confirmation and ages between confirmations; do not demand monotonic growth across a new roster confirmation.
4. Exchange DMs both ways by hash. Then persist autoregistration on and repeat: M first/home later three times, home first once, simultaneous once. No manual register after home appears; allow three minutes after it appears and record every attach time.
5. Start two uniquely identified mobiles together. Both confirm distinct local IDs, home has both rows, and each receives its own tagged DM. Do not claim the eight-entry overflow arm with only two mobiles.

**PASS:** all observed sequences confirm and deliver under the right identity without manual rescue. **STOP:** provisional registration presented as confirmed, duplicate local ID, lost mobile or wrong recipient.

<a id="mobile-02"></a>

### MOBILE-02 — explicit dormancy and boot policy

1. With M attached and persisted autoregistration on, issue `mobile unregister`.
2. Require `> mobile unregister: home-service request cleared — attachment dormant, timers cancelled (no wire message; the old home ages the row out)` and dormant/unknown/`home_desired:false`, with no confirmation-age field.
3. Observe 20 minutes: no registration DISCOVER or presence probe. Ordinary team beacons are not registration traffic.
4. `mobile register` resumes attachment; reboot with autoregistration on resumes it too. Changing the persisted boot policy is not a required workaround for unregister.

**PASS:** explicit dormancy survives idle main-loop service until an explicit/new boot request. **STOP:** spontaneous registration restart or a deregistration frame attributed to the verb.

<a id="mobile-03"></a>

### MOBILE-03 — weak-home policy, loss and redirect

1. Confirm M at home A; wait five minutes. Make home B audible for at least 60 seconds. Keep A answering and B stronger; capture bidirectional P/roster SNR and timestamps for at least four minutes.
2. Expect no proactive **searching canvass caused solely by weak quality**: current `presence_searching_probe_due()` uses missed checks only (B178 remains open). A previously verified candidate may still be selected by the separate switch predicate; stronger beacons alone are not verification.
3. Power A off after a confirmation. Without a manual register, observe checking/loss/recovery and confirmation at B; then bidirectional DM delivery. Budget about nine minutes for a formerly strong home, recording actual timing rather than assuming a short timeout.
4. Where A remains powered during a forced re-home and hears the breadcrumb, its `cfg` row becomes `REDIRECT-><B>` with a restarted age; it must not continue serving M as DIRECT. An absent breadcrumb cannot qualify that observation.

**PASS:** real loss recovers, no beacon-only authority/search storm, and an observed redirect is truthful. **STOP:** permanent false confirmation, repeated canvass storm or misdelivery. Proactive B178 positive acceptance is N/A until that feature lands.

<a id="mobile-04"></a>

### MOBILE-04 — expiry, beacons and released IDs

1. Use the stock host lifetime (read `mobile_liveness_ms` in `lib/core/protocol_constants.h`; currently 25 minutes). Confirm M and record its home row/ID; power M off. The row remains before expiry, disappears on the expiry sweep afterwards, and no roster advertises it.
2. In an otherwise empty registry, register a different mobile; it can receive the released highest free ID. A changed count alone does not prove release.
3. Repeat with a **team member** M left powered and beaconing after `mobile unregister`. Trace actual M beacons and absence of its presence probes throughout the interval. The home row must still expire. A non-team mobile may lose its identity/beacons, so it is not a valid beacon-only fixture.
4. Positive control: `mobile register` again; correct-epoch probes resume and keep the DIRECT row alive past a full lifetime. Keep all four observations separate in the log.

**PASS:** both silence and proved beacon-only activity expire, IDs release, and probes refresh. **STOP:** stale authority or failure of the positive control. No source-constant edit is needed; a shortened build would require its own recorded snapshot and cannot qualify stock elapsed time.

<a id="mobile-05"></a>

### MOBILE-05 — real admission pressure and equal-counter flights

1. On a healthy attached pair, record USB counters, generate `testsend <ID> 200 @sendms 20` from spare nodes and capture real queue pressure. Require an observed refusal of the particular CLAIM/OFFER/presence frame before qualifying its arm.
2. A presence refusal must not falsely blame the healthy home. Trace text is `presence probe refused by OUR OWN transmitter — the home link is NOT implicated`; CLAIM/OFFER equivalents identify the local transmitter. Record `offerfull`, `offerrej`, `txdrop`; lack of saturation leaves the arm OWED.
3. For the separate equal-counter test use home H, mobiles M1/M2 and static destination D. Establish that their next plaintext `-a` sends use the same mobile counter; send back-to-back.
4. D receives both distinct bodies exactly once; both mobiles receive their own E2E ACK with their original counter. Home `ctrrefuse=0` under this ordinary two-flight load. Preserve all four logs.

**PASS:** observed pressure is attributed correctly and equal-counter real flights stay distinct. **STOP:** false home loss, swallowed/crossed flight or unexplained counter refusal. Eight-plus-mobile overflow remains AUTO-03 coverage, not a claim about this rig.

<a id="mobile-06"></a>

### MOBILE-06 — confirmation-age width at the device boundary

Fixture: a separately labelled, reviewed clock/debugger setup that can hold a confirmed snapshot with age above 2^32 without running its expiry transitions. The ordinary console has no clock-setting command; do not wait fifty days or claim the short-age observation proves width.

1. Record exact image, injected time/state and method. With age 5000000000 ms, `mobile status` must render `"home_confirm_age_ms":5000000000`, never 705032704.
2. Restore/reboot the stock image; ordinary confirmation/absence still behaves as MOBILE-01. Without the bounded fixture leave this device-boundary arm OWED; AUTO-03 separately proves the core accessor and JSON writer at that value.

**PASS:** the actual device handler preserves the injected wide age and normal operation recovers. **STOP:** truncation or a fixture that bypasses the handler whose wiring is being qualified.

## OLED walkthrough

<a id="ui-01"></a>

### UI-01 — panel, geometry and blanking

1. On H1 and V record board revision; healthy boot shows live UI and no `!! OLED panel did not ACK (check Vext / addr 0x3C / wiring)`. Do not change pin polarity as a diagnostic experiment; retain the failed image if that line appears.
2. Cycle STATUS/TEAM/INBOX/SEND/SETTINGS: one boxed rail icon, fixed slot heights, no body/rail overlap. Strip slots are mail, home, people, key, duty, battery; no stale STATUS/SETTINGS title. Check mail widths at 9→10 and 99→`99+`; icons do not move.
3. On an isolated burst fixture receive over 999 posts without viewing INBOX: strip clamps at `99+`, body at `CH 999`; neither wraps nor grows a fourth digit. A fully drawn INBOX list clears session-unread to zero. If the burst was not done, leave this arm OWED.
4. Let the panel blank; first short wakes without navigation, next short navigates. On SDA/SCL (read the current board pins), capture one blank-transition burst then a minute of quiet bus. Under DM traffic, repaints complete promptly without broken RTS/CTS exchanges.

**PASS:** legible, stable pixels and measured edge-only blanking while radio service continues. **STOP:** clipping, repeated blank I²C traffic, wrong wake gesture or radio starvation.

<a id="ui-02"></a>

### UI-02 — inbox identity, paging and delete

1. Store at least two DMs and two channel posts; include a 120-byte body with a recognisable tail. INBOX is newest-first within DM then channel blocks; arrival preserves the selected record's identity.
2. Open a record with double: header identifies DM/CH sender, indicator `n/N`, two **19-character** body rows, default `>back`. Opening removes nothing. About two seconds per page, full tail visible; inactivity blanks the panel **with the modal and selected action retained**. First short only wakes it; paging does not keep the panel lit forever.
3. Select delete and double. Only that `(kind,seq)` disappears from `pull_inbox 0 0`; an equal sequence in the other store survives. Repeat middle/last list rows; glass refreshes without another press. Reboot: surviving records and epoch remain, deleted records stay absent.
4. With delete selected, long-arm then cancel: modal closes before `RELEASE!`; return is the list and a fresh open starts at BACK.
5. With E2E ACK/custody records in the raw pull, glass and unread count still include only DMs/channel posts. Raw receipt deletion with `del_msg dm <seq>` gives `"result":"erased"`.

**PASS:** glass, real store identity and persistence agree, with full paging and safe modal exit. **STOP:** wrong-record deletion, missing tail, stale row or visible internal receipt.

<a id="ui-03"></a>

### UI-03 — settings draft, conflict and real save

1. SETTINGS lands at `>ENTER SETTINGS`; one short passes it, double opens. Edit DM crypt; `CFG* UNSAVED` appears in SETTINGS and the rail dot appears elsewhere, while persisted `cfg` stays unchanged. Blank/wake and BACK/re-enter preserve the draft.
2. Save: `SAVED`; reboot and compare `cfg`, IDs, team, SF list and counters to the baseline. Only the requested covered setting changed. Re-save unchanged: `NO CHANGE`; this sentence alone is not a physical flash-write counter.
3. Leave a draft, change the *other* covered field with `cfg set intro_attach <opposite>`. Without a press the closed SETTINGS view says `CFG! RELOAD`; SAVE refuses. Revert the console value: conflict still stands. RELOAD merges untouched fields; save and reboot preserve the resulting record.
4. A non-covered `cfg set beacon_ms <value>` or live `cfg set nav 1` raises no conflict. Restore values.
5. On a spare node leave a draft then `leave`: `> left network (kept freq=...)` and `CFG! RELOAD` appear without a button press; DISCARD adopts the reset values. Re-provision. Stock `MR_UI_BLE_ROW=0` has no BLE menu row/restart-badge acceptance arm.

**PASS:** real writes/notifications preserve unedited state and show actionable conflict text. **STOP:** false save, overwritten console change, icon-only error or lost draft.

<a id="ui-04"></a>

### UI-04 — create team on glass

1. With valid persisted PHY/SF list: SETTINGS → PROVISION → CREATE TEAM. Confirm screen `CREATE NEW TEAM`, `REPLACES <6hex>` if applicable, BACK selected. BACK changes no team/key and originates no DAD.
2. Select CREATE: `TEAM CREATED`, full `0x<8HEX>` and its last six digits; `cfg` agrees and actual DAD assigns a local ID.
3. Power-cycle: team, key and local ID survive; no spurious fresh team-DAD. Export only disposable key material for comparison.
4. `mobile register freq=<F2> sf=7 bw=125` creates live/persisted divergence. Another OLED create must show `PHY DIFFERS` / `USE SERIAL`, with no team change/DAD. Reboot to restore PHY.

**PASS:** safe BACK, real creation and durable identity, with divergence refused. **STOP:** unrequested creation, false success or loss on reboot.

<a id="ui-05"></a>

### UI-05 — join a static network from sparse profiles

1. Static peer: `create layer=17 freq=<F> bw=125 sf=7 sf_list=6,7 duty=1 name="Metal"`. On OLED: `joinprofile reset confirm`; set slot 1 with `layer=4 freq=<F2> bw=125 sf=9 name="old"`, slot 3 with `layer=17 freq=<F> bw=125 sf=7`.
2. PROVISION → JOIN NETWORK lists `old`, `PROFILE 3`, BACK. Confirmation shows `PROFILE 3`, `L17 SF7 BW125.00`, `<F with four decimals> MHz`, BACK/JOIN. BACK makes no J CLAIM or config change.
3. Select JOIN: `JOINING`, then real adoption → `ADOPTED`, `node <N>`, `press = back`. During the wait let it blank; one short wakes without cancelling. Full layer 17 and leaf 1 correlate to the same nonzero ID.
4. Repeat and leave JOINING before blanking: DAD continues; later adoption does not steal navigation. If it actually lasts over 60 seconds, `STILL JOINING` is informative, not a failure; do not force a collision just for that text.
5. Reboot: profile/PHY/layer persist, boot DAD does not open a result screen; exchange DMs with the peer both ways. Post-join discovery beacons settle after the configured discovery interval.
6. With an unsaved settings draft, PROVISION says `SAVE OR DISCARD`; a conflicting draft says `RELOAD OR DISCARD`; neither joins.

**PASS:** real adoption, flash, radio and panel correlation agree through both waiting flows. **STOP:** premature ADOPTED, wrong-layer correlation, cancelled join, unsolicited navigation or failed service.

<a id="ui-06"></a>

### UI-06 — nearby teams and keyless join

1. A teamless H1 and team owner H2 share PHY/leaf. PROVISION → JOIN TEAM opens `NEARBY` / `CURRENT PHY ONLY` / `SAME RADIO + LEAF`, with H2's six-hex team fingerprint. A peer name never replaces that fingerprint.
2. Stay in the list through a beacon: rows/ages remain frozen; re-enter to refresh. Change H1's `leaf_id` to a different nibble and reboot: `NO TEAMS NEARBY`; restore it and require the row returns on a beacon.
3. Confirm the row: `TEAM JOINED` and full team ID. Reboot: membership survives but remains keyless unless the explicit saved-key action in UI-10 was taken. A sealed post is unreadable when keyless.
4. Your own team is excluded from NEARBY. After the other team is silent for ten minutes it expires from a fresh list; BACK remains usable. Compare five minutes of entering/leaving with idle baseline: no UI query/join traffic until explicit confirmation.
5. Live PHY divergence yields `PHY DIFFERS` / `USE SERIAL`; reboot restores the intended PHY.

**PASS:** physical beacon/leaf visibility, frozen display and deliberate durable join agree. **STOP:** unsolicited transmission, wrong team, implicit key activation or a stuck list.

<a id="ui-07"></a>

### UI-07 — invitation window

1. On team owner H2 open PROVISION → INVITE MEMBER before H1 joins. Expect fingerprint and `NO CANDIDATES`; the scheduled opening announcement lets same-PHY/leaf H1 discover H2 (allow existing beacon rate limiting).
2. H1 joins. H2 shows its local ID and hash fingerprint, initially no fabricated name/`KEYLESS` verdict. Double opens `NEW MEMBER` plus full hash; blank/wake returns to the list with the window still running.
3. REJECT removes that candidate from this window without revoking membership, changing keys or sending a grant.
4. On a fresh **no-console** boot, repeat using buttons: the window blanks normally and allows real sleep. At five minutes from opening, wake gives `WINDOW CLOSED`. No repeated UI-driven announcement or queries; inspect final `status` for sleep evidence.
5. A member appearing after closure creates no prompt. An already-present member is not a new candidate when opening a fresh window; roster grant remains available through UI-08.

**PASS:** real opening announcement, member delta, lifetime and sleep are bounded. **STOP:** repeated canvass, false member identity, changed membership on reject or unwanted navigation.

<a id="ui-08"></a>

### UI-08 — pubkey ceremony and actual grant airtime

1. H1 owns a content key and identity; H2 is a keyless teammate. On TEAM, open H2 then GRANT KEY: full hash and REJECT default, never a six-digit-only target.
2. If no authoritative public key is cached, expect `NEED PUBKEY`. Nothing sends until REQUEST PUBKEY is selected; then `WAITING FOR PUBKEY`, followed by enabled GRANT KEY only for the matching key. A refused request stays NEED PUBKEY.
3. Grant: `GRANT QUEUED` followed by `KEY SENT` only on real TxDone. USB `GRANT AIRED ctr=<n> dst=<id>` matches H2; H2 gets `TEAM KEY RECEIVED` and a sealed post is readable after reboot.
4. For a real queue-full/parked/terminal failure event, require respectively `GRANT QUEUE FULL`, `GRANT PARKED`, `GRANT FAILED`, with corresponding trace; admission is not KEY SENT. If not provoked, keep those arms OWED, not passed from the happy path.
5. A keyless sender has no GRANT KEY row. A confirmed grant target must remain the selected identity if the roster changes.

**PASS:** real ceremony and hardware completion reach only the intended recipient; any exercised refusal is truthful. **STOP:** grant before approval, false KEY SENT, wrong destination or stuck queued result.

<a id="ui-09"></a>

### UI-09 — grant receipt with a full keyring

1. Fill B's four retained slots using four `team new` operations, then join A's different team by ID. Verify B is keyless and all four prior rows remain in `team keys`.
2. A grants B the key. B displays `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`; a sealed post decrypts now. Acknowledge to SAVED KEYS: no automatic deletion or grant re-request occurs.
3. Power-cycle B: that team's live key is absent; prior retained rows remain. With room created explicitly, re-grant and expect `TEAM KEY RECEIVED`, with decryption after reboot.
4. Repeat receipt while panel is blank: a grant alone never wakes/navigates. A receipt for a team B has left must not claim an active key; count that arm only if an actual arriving stale grant is traced.

**PASS:** physical receipt, display and reboot prove saved versus RAM-only outcomes. **STOP:** false durable claim, silent eviction or grant-caused navigation/wake.

<a id="ui-10"></a>

### UI-10 — explicit saved-key activation

1. B retains team A's key after `team 0`. Join A through NEARBY: `TEAM JOINED`, then `SAVED KEY FOUND` with fingerprint and BACK/USE SAVED KEY.
2. BACK: reboot remains keyless, and joining again still offers the retained record. USE SAVED KEY: `TEAM KEY ACTIVE` without NOT SAVED, reboot restores it and decrypts a real sealed post.
3. Race control: leave the A offer visible, change to team B through USB and explicitly establish B's active key. Attempt the stale A offer: `KEY NOT INSTALLED` / `not_our_team`; B's key survives reboot, A's retained record remains available later.

**PASS:** only the explicitly confirmed current team's key activates and persists. **STOP:** implicit activation, wrong team's key or collateral key loss.

<a id="ui-11"></a>

### UI-11 — forget key and keyring-full remedy

1. With four rows in `team keys`, `team forgetkey <inactive-T> confirm`: `> team: KEY FORGOTTEN` and reboot leaves three with the active row/key unchanged.
2. In SAVED KEYS, active row shows `ACTIVE KEY` / `CANNOT FORGET` with no forget action; inactive row shows `FORGET KEY`, full ID and BACK selected. BACK changes nothing; explicit confirm shortens the list.
3. Fill again and OLED CREATE TEAM: KEYRING FULL's acknowledgement lands in SAVED KEYS without creating or deleting anything. After a deliberate forget, creation requires a new deliberate CREATE.

**PASS:** real erase preserves neighbours and remedies perform no hidden action. **STOP:** active/adjacent key loss or automatic retry/create. Power-cut compaction is NV-06.

<a id="ui-12"></a>

### UI-12 — presets on actual transports and flash

1. `ui preset list` on an OLED image yields seventeen `ui_preset` records (including disabled empty texts) then `ui_presets_end` with `capacity:17`, eighteen complete lines and no `CONSOLE_DROP`.
2. `ui preset set dm3 loc=on "meet at the hut"`; reboot and list: text/location persist and there is no invalid-record boot warning. Repeat identical set; `cfg`'s **presets** `saves=` does not advance. It is not a general NV-write counter.
3. During an actual emergency attempt, even an identical `ui preset set dm1 loc=off "<current text>"` yields `{"ev":"ui_preset_err","reason":"busy"}`; list still works. After retained outcome the set works. `ui preset clear emergency` refuses `mandatory`.
4. On a build that combines this catalog with real BLE, compare eighteen reassembled lines with USB. **No stock available nRF52 image enables OLED**, so that specific catalog-over-BLE arm is N/A on the listed stock images; USB-BLE-02 still qualifies real BLE framing.

**PASS:** complete USB records, persistent preset and live busy guard. **STOP:** fused/dropped lines, false save or a phrase edit reprovisioning the team. Power-cut/reset arms are NV-06/NV-04.

<a id="ui-13"></a>

### UI-13 — live ages, position and selected identity

1. With a teammate visible, watch TEAM ages change without input during a lit interval; after blank/wake they reflect elapsed time. Do not demand a continuously lit 90-second screen with a 15-second blank timer.
2. Set distinct nonzero coordinates on H1/H2; H2 sends `send <HASH-H1> "<TAG>-geo" -t -a -e -l`. TEAM shows distance/direction. Keep H2 beaconing but send no new position for ten minutes: the row remains, distance/direction become blank.
3. An uncached peer has blank distance, not `0m`; coincident known positions give `0m` and no direction. Zero own coordinates: no invented vector. Compare idle STATUS versus TEAM RF baseline: viewing positions sends no location query.
4. Pick a teammate, remove it and wait for actual roster expiry: no selection marker, complete `TEAMMATE GONE, pick`, double sends nothing. Short re-picks. On a roster reorder, selection follows identity, not row number.

**PASS:** time/position and identity on glass match real received evidence. **STOP:** stale vector, fabricated position, extra location traffic or wrong-target send.

<a id="ui-14"></a>

### UI-14 — receive wake and retained send result

1. Leave a known screen dark; a DM addressed to this node wakes it there without navigation. Repeat with a sealed team post: wakes, unread increases.
2. An unsealed channel post increments unread but leaves the dark panel dark. A grant receipt also stays dark (UI-09).
3. Send from the panel, let the result blank, then short: the same result returns; another acknowledgement leaves it. Current result views survive blanking; the old H7-08 auto-exit expectation is obsolete.
4. On separate headless boots record final sleep counters after receive/reblank versus quiet baseline; input sent to query status ends that boot's automatic-sleep observation.

**PASS:** scoped receive wake, preserved screen/result and resumed headless sleep. **STOP:** stranger-triggered wake, navigation theft, missing retained result or unexplained sleep inhibition.

<a id="ui-15"></a>

### UI-15 — ordinary sends and completion

1. SEND → compose the configured `Got your message` preset; peer receives exactly that body. Expect `SENDING...`, possibly brief `QUEUED`, then `SENT, waiting`, finally `PICKED UP` or `NO RELAY HEARD`. Brief QUEUED need not be visible.
2. TEAM → teammate → configured `Are you OK?`: one DM, `SENT, waiting`, then `DELIVERED to <label>` only with matching acknowledgement. BACK from either unsent compose sends nothing.
3. Power the peer off; post still reaches SENT, waiting after physical air, then NO RELAY HEARD. An unconfirmed DM ends NO CONFIRM; acknowledge it and send another post successfully.
4. If a real zero-handle outcome is observed, require `NOT CONFIRMED` / `no send handle`, never SENT. Do not manufacture that arm by merely hiding the peer after a valid handle.

**PASS:** physical air, acknowledgements, truthful failure text and reusable send path agree. **STOP:** stuck SENDING/QUEUED, false SENT/DELIVERED or duplicate send.

<a id="ui-16"></a>

### UI-16 — emergency air, budget, blocking and location

1. Controlled team bench only. Read hold/attempt/blank constants from `firmware_ui_input.h`, `firmware_ui_model.h`; read `ch_min_ms` from `cfg`. Shorter-than-fire release cancels; deliberate hold fires from lit **and fully dark** panel on one gesture, with identical timing.
2. With no relay, film from the first frame: `attempt 1 of 3`, 2, 3, then complete `NOT RELAYED` / `no relay after 3`. Match distinct transmitted message IDs; no fourth attempt. Rail is absent from all emergency screens; headlines remain unclipped.
3. Fire again inside the channel interval: USB `BLOCKED channel reason=min_interval — retry in <N> ms`, glass `BLOCKED` / `retry in <n>s` visibly counts down; untouched button, automatic retry at the deadline. A blocked non-airing consumes no attempt.
4. With zero lat/lon, alarm still arrives without fabricated location. With configured nonzero coordinates, the received alarm contains them. Compare raw receiver records, not a guessed outgoing command echo.
5. A real relay earns PICKED UP; it does not mean every team member received the message.

**PASS:** hardware gestures produce the bounded real sequence and accurate complete text/location. **STOP:** shortened dark-screen safety hold, false confirmation, extra attempt or missed automatic retry.

<a id="ui-17"></a>

### UI-17 — emergency isolation, reply wake and safe exit

1. Start an ordinary DM awaiting ACK, then fire emergency; capture alarm dispatch before that DM's completion/deadline. Repeat with a channel post. No duplicate ordinary message; its late result cannot change the emergency.
2. Double twice under emergency overlay: no hidden compose/send/dismiss. Repeat from an open DM compose with a real phrase selected, long-arm/cancel, then double: no invisible DM. Reopen later at a safe default.
3. Press immediately while a terminal frame is still painting: it is not dismissed unseen. After blanking, first short wakes the retained outcome; next short may acknowledge.
4. With an alarm retained and panel dark, a teammate's team-channel post wakes to REPLY with the posted text; a stranger's unsealed post must not. A direct DM is not emergency confirmation. This proves the **current same-team-post inference**, not an authenticated reply bound to the alarm (B118 remains open).
5. Time retention against `kEmgHoldMs` read from source; a new reply starts its own hold. Local terminal outcomes alone do not widen the owner-ruled wake policy. Scope the bus as in UI-01: no repeated I²C while blank.

**PASS:** physical pre-emption, identity isolation, readable outcomes and scoped wake behave as stated. **STOP:** hidden send, unseen dismissal, unrelated result treated as confirmation or stranger reply/wake.

<a id="ui-18"></a>

### UI-18 — duty gauge

1. Record policy; `cfg set duty 1`, reboot. Generate bounded test traffic and compare console `duty` with the sixth strip slot; it fills without shifting key/battery glyphs.
2. If the budget is actually exhausted, console reports `100% — SILENT, ~<n> s to availability`; glass shows full warning-mark box and transmission is refused. No observed exhaustion means that arm is OWED.
3. On an isolated bench only, `cfg set duty 0`, reboot: crossed box, never empty box; no numerical percent on the glass. Restore policy and reboot.

**PASS:** physically readable gauge follows the observed real budget states. **STOP:** gauge/airtime disagreement or overlapping glyphs.

<a id="ui-19"></a>

### UI-19 — non-team OLED profile

1. Flash an identified `gateway_heltec` or `gateway_heltec_v4` image. Rail has STATUS/INBOX/SETTINGS at the same heights, with TEAM/SEND slots empty; house strip slot is blank, not a fault icon.
2. SETTINGS has no PROVISION row. `joinprofile list` → `> err gateway_build (joinprofile is normal-node only)`.
3. Confirm ordinary navigation/blank/wake and a real radio receive after the profile change.

**PASS:** physical display matches the compiled feature profile. **STOP:** phantom team actions, shifted rail or lost radio service.

<a id="ui-20"></a>

### UI-20 — identity labels on the physical font (W4a)

1. Use two OLED peers and UI-07’s actual new-candidate window; also view that peer on TEAM. Establish its real public-key hash and controlled name cache. On the displaying peer, `peername 0x<HASH8> "Wolfgangetta"` must return exactly `{"ev":"peer_name_set","hash":<decimal hash>,"name":"Wolfgangetta"}`; `nameof 0x<HASH8>` must retain `"name":"Wolfgangetta"` (ID fields depend on the fixture). Reopen the screens after changing the cache.
2. TEAM and the invite candidate both show `Wolfg»` in the six-cell name field: one legible final chevron cell, stable neighbouring columns, no spill. NEW MEMBER still shows the full `0x<HASH8>` and the fitting `Wolfgangetta` name.
3. Repeat with `peername 0x<HASH8> "łAB"` (UTF-8 bytes `C5 82 41 42`); expect the same acknowledgement shape with `"name":"łAB"`. On both screens the name is `..AB`, four cells, with no Latin-1 glyph or missing cell.
4. Repeat with a genuinely unnamed peer and a fresh cache on the displaying peer: verify its `nameof` result has no `name` field. TEAM shows the six-digit fingerprint; the invite name field is blank beside its separate fingerprint; NEW MEMBER shows the whole `0x<HASH8>`. Empty advertisements do not erase an older cached name: such a fixture does not exercise this arm.

**PASS:** the real `6x10_tf` font draws the expected cells legibly on both screens. **STOP:** missing/doubled marker, raw Latin-1, collapsed cells, clipped hash or fabricated invite name. Leave unexercised arms OWED if the cache/candidate fixture cannot be established; host byte captures do not establish glyph appearance. No new console line is introduced by W4a; the acknowledgements above establish the physical fixture.

## Custody over real radios

<a id="custody-01"></a>

### CUSTODY-01 — static relay failure, persistence and deletion

1. Arrange ordinary static A↔B↔C with **no A↔C path**. All `mobile status` objects say mobile false. Record routes, raw inbox high-waters/epochs and A's OLED counts; `debug on`. Record then set A's `e2e_dm=0`, `intro_attach=0`.
2. `send <ID-C> "<TAG>-control" -a` must deliver via B and return E2E ACK. Update all inbox high-waters to exclude the control.
3. Power C off, promptly `send <ID-C> "<TAG>-custody" -a`. Trace at B must prove DATA received from A → hop ACK sent → onward attempts exhausted; without accepted custody this arm is unexercised. Allow up to 180 seconds for the report.
4. A gets one `CUSTODY FAILURE reporter=<B> ... origin=<A> dst=<C> ctr=<sent> ... seq=<S> — the relay could not complete onward custody; NOT proof the destination missed it (an e2e ack may still arrive)` and one raw `custody_failure` record with the same tuple. No ordinary OLED row/unread increase; B stores no application record.
5. Power-cycle A **before deletion**: same record/sequence and epoch. `del_msg dm <S>` → `"result":"erased"`; second power-cycle keeps it absent; repeat deletion → `not_found`.
6. Power C on: no failed body in its delta. Restore policy and topology.

**PASS:** accepted physical custody produces one correlated durable diagnostic and durable deletion. **STOP:** duplicate/wrong report, application record on relay, or claim that custody proves non-delivery.

<a id="custody-02"></a>

### CUSTODY-02 — translated report to the originating mobile

1. Arrange M—H—R—T, H a confirmed ordinary home, H→T only via R. Record M hash/local ID, H/R/T IDs, layer, all inbox cursors/epochs, M's glass counts and policy; set M's `e2e_dm=0`, `intro_attach=0`.
2. With T on: `send <ID-T> "<TAG>-mobile-control" -a`; prove relay path and `E2E-ACKED ctr=<same mobile queued counter>`. Update cursors.
3. Power T off; send another tagged `-a` DM and record `ctrM`. At R require H's DATA → R's hop ACK → onward terminal; otherwise this is not a custody test.
4. H gets one direct line/raw record with reporter R, origin H, `ctrH`, no delegated fields. Its forwarding to M is witnessed by RF, not a second special USB line.
5. M gets exactly one live/raw report with `reporter=R`, `failed_origin=H`, `ctr=ctrH`, `delegated:true`, `mobile_ctr=ctrM`, and exactly one target (`target_kind:"node_id"`, `target_id:T`). No `via_home`/`home_ctr` alias. Glass gains no row/unread.
6. Power-cycle M: identical sequence, both counters and epoch survive. Restore policies/power/topology after CUSTODY-03.

**PASS:** the real four-radio chain preserves inner home identity and outer mobile correlation durably. **STOP:** cross-flight translation, duplicate record, loss on reboot or false application indication. This is B278's principal metal residue.

<a id="custody-03"></a>

### CUSTODY-03 — lost correlation and optional re-home

1. Repeat a fresh CUSTODY-02 flight; after R's hop ACK but **before its terminal**, reboot H. Prove those timestamps. H must still receive/store the direct diagnostic; M receives no translated line or new raw record. Re-register M if needed.
2. Optional fifth-board arm: originate by `send <HASH-T> "<TAG>-rehome" -a` **through H first**, then move M to H2 before the report returns. If the return is observed, it carries `target_kind:hash`, the same full hash/mobile counter and reaches M via H2. A second new send after moving is not the same flight.
3. Do not force custody-then-late-ACK on a four-node line with T off: no successful copy exists. If an independent path produces a late valid ACK, it upgrades the live operation without a second translation/downgrade. AUTO-09 owns exhaustive ACK-order/expiry rules.
4. Restore recorded policies, home and topology.

**PASS:** timed no-map control yields no false translation; any observed optional arm agrees. **STOP:** invented correlation or downgrade. Optional arms not observed are logged, never silently claimed.

## USB and BLE transport

<a id="usb-ble-01"></a>

### USB-BLE-01 — complete console, help and removed verbs

1. On full and gateway profiles capture USB `help` and `?`: byte-identical bare primary names followed by `docs/manual/command-reference.md`, no `!! CONSOLE_DROP`. Compare with `python3 tools/gen_command_inventory.py --primary <profile>`; use its current profile choices, not a historical count.
2. `help messaging` returns exactly `> help err unsupported_form (only bare \`help\` or \`?\`; usage: help | ?)` and `docs/manual/command-reference.md`. Gateway help excludes mobile/team; removed rcmd/password/unlock/lock are absent everywhere.
3. On USB issue `rcmd 1 status`, `password x`, `unlock x`, `lock`: each `> parse error`. On secured BLE each gives `{"err":"parse","msg":"unknown_cmd"}`; BLE help forms give `{"err":"help","msg":"console_only"}`. No remote traffic follows.
4. During ordinary radio traffic, capture several complete `cfg`/`status` replies under host backpressure. If an actual console overflow occurs, it must be loud as `!! CONSOLE_DROP lines=<N>`, with later complete lines and radio service recovering. No overflow observed is not an overflow-path PASS.
5. USB `reboot` prints `> rebooting` before reset. Capture reset cause and full following banner.

**PASS:** complete profile-correct output and truthful real transport/refusal/reboot behavior. **STOP:** fused/truncated reply, silent observed loss or legacy execution.

<a id="usb-ble-02"></a>

### USB-BLE-02 — NUS lengths, chunking and raw validation

1. Secure N's BLE connection; record negotiated MTU. Use a byte-capable NUS client, retaining raw notifications and reassembled newline records. Generate and count the exact lines before transmitting.
2. `send_layer <HASH> 255,255,255 "<226 ASCII bytes>" -a -e -K -l` is 274 bytes excluding newline: named `err_unsupported`, never `line_too_long`. Without `-e -l` it is 268 bytes and reaches queued or the named routing refusal `err_no_gateway`.
3. Repeat the 274-byte vector in 20-byte writes and with a 244-byte first write; reassembled responses match. Send 275 non-newline bytes then newline: exactly `{"err":"line_too_long"}`, no command ACK/origination, then `status` recovers.
4. Send a real `status`/`cfg` response longer than 244 bytes: all bytes and final newline arrive, through notifications bounded by **negotiated MTU minus 3** (244 at MTU 247). A line may span notifications; one line per notification is not a general NUS guarantee. Include a measured 245–256-byte response if available, otherwise keep that length-specific hardware observation OWED; AUTO-10 proves its software bound.
5. Send bytes `cfg set name AB` + NUL + `CD\n`: BLE returns `{"err":"bad_line","msg":"embedded_nul"}`; name unchanged and no command response leaks to USB. USB equivalent gives `> err bad_line embedded_nul`. `status\r\n` is accepted by BLE intake.
6. The 265-byte sealed/location DM vector `send <HASH> "<232 bytes>" -a -e -t -K -l` must reach semantic processing, not transport overflow; with the prerequisite team/identity/location/key fixture its seal refuses `too_large`. Missing prerequisites do not prove that particular branch.

**PASS:** observed boundary, chunk-size invariance, long-response reassembly and raw validator all hold on the real link. **STOP:** truncated tail, fused line, NUL-tail mutation or overflow that executes.

<a id="usb-ble-03"></a>

### USB-BLE-03 — regen replies and real identity write

1. Scratch N as CLIENT, with a saved dedicated admin key and target. Record self hash/public identity, name and both public listings. Secured BLE `regen` gives `> regen ok  key_hash32= 0x<8HEX>` with `  name="..."` on the same line if named, then `> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved`.
2. No command output leaks to USB. Reboot: new ordinary identity/name and unchanged dedicated keys/target book persist. Old target ACL grants to self do not magically follow it.
3. USB `regen` yields the same contract only on USB. On an ACCEPT image the success line has no CLIENT note, and independent admin-id/ACL remain unchanged. Restore trust explicitly through public-key exchange.

**PASS:** real transport isolation and persisted identity separation. **STOP:** cross-sink reply, lost unrelated stores, name loss or false transfer of ACL authority.

<a id="usb-ble-04"></a>

### USB-BLE-04 — secured link, bond and SoftDevice/RF coexistence

1. On N record bootloader/SoftDevice identity and flash the matching image. `cfg set ble_mode on`, reboot, then `python3 tools/meshroute_client_ble.py scan`: NUS advertising is present. Read the actual PIN from physical `cfg`; pair using the OS/client, never assume the historical default PIN.
2. Require secured GATT before command access; an unpaired client cannot use RX/TX. USB logs `[ble] pairing OK` and `[ble] link secured (paired/bonded)` for a fresh bond. Disconnect/reconnect and reboot: the bond resumes without re-entering the PIN. `whoami` and a received DM arrive intact through NUS.
3. While the link is active run USB-BLE-03's `regen`: no crash/hang under SoftDevice RNG ownership, connection survives or recovers normally.
4. Repeat a fixed, timestamped DM exchange workload with `ble_mode off`/reboot versus on with an active client. Record deliveries, timeouts and retries for equal sample counts and unchanged PHY/topology. Investigate a repeatable degradation beyond the baseline variability; do not invent a numerical tolerance or tune it away. Restore policy.

**PASS:** actual pairing/bond enforcement, active-link RNG and measured RF coexistence are sound for the recorded sample. **STOP:** unpaired command access, HardFault or repeatable BLE-only timing failure.

## Remote administration

<a id="radmin-01"></a>

### RADMIN-01 — target trust on physical USB

1. Fresh disposable ACCEPT target: boot `> admin-id boot state=absent`, `> acl boot state=absent count=0 owners=0 operators=0`. USB `admin-id generate` → `> admin-id generated fp=<16hex> pub=<64hex>`.
2. Obtain controller PUBLIC key Kc from `admin-key show self` on a CLIENT. Target `acl add owner <Kc>` → `> acl added slot=0 role=owner fp=<Fc> pub=<Kc>`. Compare full public keys; compute fingerprint independently on the host with `python3 -c 'import hashlib,sys; print(hashlib.blake2b(bytes.fromhex(sys.argv[1]),digest_size=64).digest()[:8].hex())' <64hex-public>`. This hashes the public key, never the seed.
3. Reboot: admin-id state ok, ACL count=1/owners=1/operators=0; `admin-id show` and `acl list` reproduce the exact root/row. `regen` and `leave`, each followed by reboot, preserve these separate stores.
4. `admin-id rotate confirm`: new root persists, ACL stays unchanged. Re-exchange that root to the controller.
5. On secured BLE to an ACCEPT N, `acl`, `acl list`, `admin-id show` and a disposable mutation form all return `{"err":"admin","msg":"console_only"}`, no USB leak or store change. Exhaustive malformed forms are AUTO-11.

**PASS:** public trust exchange and independent root/ACL durability on the named backend. **STOP:** seed disclosure/request, silent root creation, trust loss or BLE mutation.

<a id="radmin-02"></a>

### RADMIN-02 — controller target book and public pages

1. CLIENT `admin-key show self` → `> admin-key self fp=<Fc> pub=<Kc>`. Target `admin-id show` → `> admin-id ok fp=<Ft> pub=<Kt>`. Exchange Kc into target ACL and `admin-target add bench <Kt> hash=<HASH-target>` into controller; routing hash is distinct from root fingerprint.
2. Expect `> admin-target added slot=0 fp=<Ft> pub=<Kt>`. Reboot both; `admin-target show label=bench` reproduces `slot=0 label=bench fp=<Ft> pub=<Kt> hash=0x<8HEX> layer=none`, target root/ACL unchanged.
3. Fill eight disposable public book rows with distinct labels/keys; `admin-target list page=0` returns eight whole rows plus `> admin-target end page=0 count=<total>` on USB and secured N CLIENT BLE, no unsolicited next page or cross-sink output. Filler keys make no reachability claim.

**PASS:** bidirectional full-key comparison, real durable book and complete requested page. **STOP:** swapped trust/routing identity, truncation or lost acknowledged row.

<a id="radmin-03"></a>

### RADMIN-03 — dedicated key lifecycle and secured public boundary

1. On scratch CLIENT A: `admin-key generate key0`; reboot, `admin-key show key0` preserves fingerprint/public key. Export `admin-key export key0` over USB only; privately import into different CLIENT B with `admin-key import key0 <seed>`. Reboot B: identical principal, different ordinary self.
2. Over secured N CLIENT BLE, key/target list/show deliver public lines; key generate/import/export/remove/reset and target add/set/remove/reset return `{"err":"admin-client","msg":"console_only"}`, no USB leak or durable change. Use disposable values.
3. After USB-BLE-03 regen, the dedicated key/book survive. `admin-key remove key0 confirm` on A → `> admin-key removed key0`; after reboot show → `> admin-key err not_found`. B's independent copy and A's book survive. No secure physical-flash erasure claim follows.

**PASS:** real write/import/removal and transport boundary match their public state. **STOP:** secret crosses BLE, wrong principal or collateral data loss.

<a id="radmin-04"></a>

### RADMIN-04 — interrupted trust writes and explicit recovery

Fixture: disposable target/controller on **each** nRF52 and ESP32 backend, controlled power cut; corrupt/IO arms additionally need a reviewed store-specific fixture with untouched filesystem metadata.

1. Record known old/new public material. Interrupt `admin-id rotate confirm`, ACL owner addition, dedicated-key update and target-book update separately; reboot and read public state before recovering.
2. Record intact old/new records or honest `absent`/`invalid`/`io_failed` states. nRF52 remove-before-write may lose a record; that is a loss outcome, **not atomic preservation**. Any successfully acknowledged update must survive a later ordinary reboot.
3. Invalid ACL: boot `> acl boot state=invalid count=0 owners=0 operators=0`, add → `> acl err store_invalid`; `acl reset confirm` → `> acl recovered count=0 owners=0 operators=0`, then first owner can be added. Invalid admin-id similarly refuses until `admin-id reset confirm` → `> admin-id recovered fp=<F> pub=<K>`.
4. Invalid controller record resets explicitly with `admin-key reset confirm` or `admin-target reset confirm` → `> admin-key reset` / `> admin-target reset`; reboot shows a valid empty store. An `io_failed` fixture must refuse reset with `store_io_failed`, and removal of that fixture exposes untouched prior data.
5. Check neighbouring records/config before/after every arm; no broad reformat or automatic root/owner creation. Without a reviewed fault fixture those fault arms stay OWED.

**PASS:** each exercised physical loss/fault has an honest state and explicit scoped recovery. **STOP:** accepted invented rows, false acknowledged durability or silent replacement of unreadable trust.

<a id="radmin-05"></a>

### RADMIN-05 — persisted activation delay

1. ACCEPT boot prints `> remote-activation state=<state> ms=<n>`; `cfg` prints `radmin: activation_ms=<n> state=<state> cfg=<stored> floor=<f> default=<d> ceiling=299999`. Derive valid values from this PHY's actual floor.
2. `cfg set remote_action_activation_ms <valid>` → `> cfg remote_action_activation_ms=<valid> ok (live + saved)`; reboot keeps configured state/value. `<f-1>` and 300000 get `bad_value`, leaving the saved value unchanged.
3. Set 0: same success line, `state=default_derived cfg=0`. On a separate non-radiating slow-PHY fixture whose derived default exceeds ceiling, require `impossible_phy ms=0` and `impossible_activation`; restore PHY after the observation.
4. CLIENT prints no remote-activation boot line; its `cfg` still has the radmin block. `cfg set gw_announce_interval 7200000` reaches the named key's normal success on a suitable gateway fixture.

**PASS:** real handler/boot/save agree with live derived bounds. **STOP:** accepted impossible delay, silent clipping or lost configured value.

<a id="radmin-06"></a>

### RADMIN-06 — actual mobile request and carrier acknowledgement

1. RADMIN-01/02 provisioned; M confirmed at ordinary home H, static target T routable. USB M: `remote bench -e -- status`. Expect `> remote <id16>` acceptance, `> remote <id16> out <body>` lines, then `> remote <id16> completed`.
2. Capture M→H `MOBILE_SEND`, H→T request, T→M result; target session attributes stable SOURCE_HASH to M. A direct M REMOTE_CMD is the wrong path. Repeat secured BLE: remote ACK then `remote_output` and `remote_terminal` NDJSON; correlate the same ID.
3. Repeat with a second target; concurrent requests have distinct counters. `remote bench -e -a -- status` produces exactly one `> remote <id16> carrier acked ctr=<n>` (BLE `remote_carrier`, event acked). Without `-a` that carrier event is absent; result completion is a separate event.
4. `remote bench -e -a rollover` refuses bad_args. At a naturally exercised session-full boundary, one safe rollover/fresh-ID resend is allowed; no unbounded rollover loop. Host capacity/nonce proofs remain AUTO-11.

**PASS:** real wrapper/result chain, stable identity and optional carrier event are correctly correlated. **STOP:** duplicate execution, wrong principal/counter, or carrier ACK called command completion.

<a id="radmin-07"></a>

### RADMIN-07 — silence, custody and exact retry

1. With T off from the outset, issue a fresh `remote bench -e -- status`: capture one automatic exact resend at 60 seconds same-layer (150 seconds for an explicitly provisioned cross-layer route), then `unknown` at 300 seconds. Record which topology ran.
2. `remote-retry <id16>` after unknown resends the same sealed request/ID. Restore T; recover its response without inventing a new operation. Unknown is not proof of non-execution.
3. After CUSTODY-02 is qualified, use M—H—R—T and a `-a` remote request; cut T after R accepts custody. Require existing custody diagnostic followed by correlated `remote ... carrier custody_failure ... origin=<H> reporter=<R> ...`; the request remains governed by resend/deadline, not a terminal manufactured from custody.

**PASS:** actual RF silence/custody preserve one request's identity and distinct uncertainty. **STOP:** extra automatic retries, changed ciphertext on exact retry, false terminal or uncorrelated custody.

<a id="radmin-08"></a>

### RADMIN-08 — retained local result, reconnect and ACK debt

1. On N CLIENT BLE complete a request; capture complete `remote_output` records followed by `remote_terminal` with `result:"completed"` (scheduled has `activation_ms`, other details use `detail`). Disconnect before local acknowledgement.
2. Reconnect: `{"ev":"remote_retained","id":"<id16>"}` is re-offered. `remote-result show <id16>` retrieves the retained result; `remote-ack <id16>` → `{"ack":"remote-ack","id":"<id16>"}` and local re-offer stops. USB equivalents stay on USB.
3. For a separate completed encrypted request, make home unreachable before local remote-ack. Trace the 25-byte ACK wrapper attempts at 5, 10, 20 seconds then silence; `status` includes `radmin_client_ack_debt=1`. A next request to that target carries one extra debt attempt ahead of it.
4. Restore home/target and observe recovery. Loss of local connection is not consent to discard an unacknowledged result.

**PASS:** real reconnect/reassembly and RF debt have the stated lifetime and ordering. **STOP:** missing retained result, perpetual ACK burst, cross-sink leak or new request overtaking required debt attempt.

<a id="radmin-09"></a>

### RADMIN-09 — scheduled hardware actions and halt recovery

Scratch ACCEPT target with local restart access; finish other target tests first. Read build support from the identified image; remote refusals for unsupported backends are not hardware successes.

1. Before `remote bench -e -- prep-restart`, the controller must show exactly: `prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.` No new confirmation token is part of the command.
2. Target logs `> remote-action scheduled request_id=<id16> activation_ms=<n> action=prep-restart`, then `> remote-action activate request_id=<same> trigger=<ack|deadline>` **before the effect**. Measure both an ACK-earlier run and a withheld-ACK deadline run against first checked send ownership; receiving scheduled alone is not proof of air/activation.
3. After activation radio/admin service stops; remote reboot/rollover cannot recover it. Local USB `reboot` (secured BLE if supported) prints `> rebooting` and restores service; hardware reset/power-cycle also work. Verify erased inbox/epoch through NV-04.
4. On separately restored scratch fixtures exercise supported `reboot`, bare `ota`, `factory_reset confirm`, `sleep on`, `sleep off`, and `crashtest hang`, `crashtest fault`, `crashtest reboot` through the same remote syntax. Enable `debug on` locally before crashtest admission. Require scheduled/activate metadata before the actual reboot/DFU or Wi-Fi OTA entry/erase/sleep/fault; record board-specific outcomes and local recovery. Hang may need physical restart on a build without a watchdog. Remote OTA enters idempotently; it never toggles OTA off. Missing backend gives typed refused, never a fabricated hardware effect. Keep every action/backend arm explicit in the result row.
5. Provision a second disposable credential/ACL slot; while one promise is still outstanding, `remote bench using=key0 -e -- reboot` gets retained `action_busy` without replacing it. A local physical command pre-empting a promise is the accepted power-loss class. A never-sendable armed promise can block disruption until same-slot `remote bench -e rollover confirm`/local recovery; do not “fix” that ruled residual during testing.
6. Real erase failure, if observed, prints only scalar `> remote-action result request_id=<same> outcome=<name>` on target metadata; remote execution must not dump raw local handler output to USB/BLE.

**PASS:** each exercised real effect follows its promise/clock, conflict policy and documented recovery. **STOP:** premature/repeated effect, false scheduling, raw transcript leak or unrecoverable local halt. Untested effect/backends stay OWED.

<a id="radmin-10"></a>

### RADMIN-10 — v26 migration preserves separate trust

1. Prepare recorded v25 `/mrcfg` on ACCEPT and CLIENT scratch boards, with valid administration stores and public listings. Flash current v26 **without erase**; preserve old/new image hashes. An already-v26 board cannot prove this migration.
2. First boot has one `> nv: /mrcfg v26 not loaded (absent, or a pre-v26 record refused): compile-time defaults until the next cfg set`. ACCEPT then reports admin-id/ACL state ok; CLIENT key/book state ok. All public records/counts equal pre-flash; main cfg is default and has no legacy admin fields.
3. Persist a cfg value and reboot: `> nv: /mrcfg v26 loaded`; saved value and separate trust survive. Restore network configuration without erase, regen or trust resets.
4. Original controller credential completes `remote bench -e -- status`. Repeat on both real storage backends; do not substitute a fresh-boot default for an observed rejected v25 record.

**PASS:** actual schema refusal/reseed changes main configuration only and old administration trust still works. **STOP:** missing schema diagnostic, trust loss or forced reprovisioning of separate keys.

<a id="radmin-11"></a>

### RADMIN-11 — nRF52 task stack qualification

1. On N CLIENT use the existing debugger/owner-reviewed task-stack observation setup to record the task names, budgets and high-water values across boot store restoration, key import/list and a full eight-row target page.
2. Distinguish the boot Arduino task (4096-byte budget cited by B323) from the dedicated mesh/console task. `status stackhw` measures the latter; it cannot certify the former. Record the actual budgets of the flashed image.
3. Exercise normal RX/remote results while listing the full page; no reset/exhaustion. Without a task-specific instrument keep boot-stack qualification OWED; no invented minimum or production debug verb is introduced here.

**PASS:** measured tasks complete their actual worst exercised paths without exhaustion, with retained high-waters. **STOP:** reset/exhaustion or attribution of one task's reading to another.

## Sleep, power and nonvolatile stores

<a id="power-01"></a>

### POWER-01 — button/radio wake, long holds and reset boundary

1. Persist the intended quiet configuration **before reboot**. During the test boot send no console byte: it latches host-present and disables automatic idle sleep. Capture passively without DTR resetting the board; wait past 30-second boot grace and panel blank.
2. Hold the button for ten seconds immediately after boot, then after sleep; repeat five sleeping holds and once after cold power-cycle. No interrupt watchdog/panic; emergency gesture remains usable.
3. On separate headless boots test short-button wake and a received DM while dark. Final `status` records positive `slept`, button `wk_gpio` and radio `wk_ext1` respectively; ACK/received message alone does not prove the radio wake edge.
4. Reset controls: `reboot` inside boot grace, hold only as application banner starts; then another boot that demonstrably slept, final status read, reboot and hold as banner starts, three repeats. Both complete without panic. Holding GPIO0 **during reset sampling** may enter loader: release and reset to recover.
5. Repaint changes complete well under 250 ms while exchanges continue; after blanking sleep resumes. Final `wkarmfail=0`, `wkdisarm=0`; record `wkbusy`/`wksleepfail`, which may legitimately be nonzero. If `!! OLED button wake unavailable; sleep disabled` or `!! OLED button wake stuck armed; sleep disabled` appears, sleep must stay disabled for that boot and the line must not flood.

**PASS:** separately observed wake sources, repeated holds/resets and resumed sleep without fault records. **STOP:** watchdog/panic, false sleep measurement, loss of either wake source or sleep despite fail-closed diagnostic.

<a id="power-02"></a>

### POWER-02 — sleep-count endurance

1. Archive exact image/ELF; persist a quiet teamless setup and reboot headless. No console input until the final read, and no late monitor attachment that resets the board.
2. Leave it long enough to exceed the historical trip point (about ten hours at a one-second cap). Final `status` must show `sleep=auto`, **`slept > 32769`**; retain `wkarmfail`, `wkdisarm`, `wkbusy`, `wksleepfail` too. Uptime alone proves nothing.
3. Read fault history: no new panic/watchdog for this boot. If the count is below threshold or `off-host`, leave OWED and repeat an adequate run; do not turn that into a firmware PASS/FAIL.

**PASS:** measured successful sleeps exceed the old trip point without a new fault. **STOP:** panic/reset; decode with the actual flashed ELF. Historical B196 closure is retained, not reopened merely because this image has not been soaked.

<a id="power-03"></a>

### POWER-03 — battery scale, divider and RF coexistence

1. Record silkscreen and current board traits/pins (`platformio.ini`, `variants/heltec_v3/board_ui.cpp`). Battery-powered with USB detached, compare two safe cell voltages if available: panel ≤ meter and difference <0.150 V; stable idle readings differ by at most one 0.1-V step. A missing second point remains an unmeasured arm.
2. Scope ADC input and control between/during cadence bursts (read `kBattPeriodMs`): ADC is about zero between samples, rises only during sampling. On the V3 divider, active voltage is approximately VBAT/4.9; do not treat empirical ADC scale 5.42 as the resistor ratio or apply V3 traits to V4 blindly.
3. For a persistent `--`, measure actual ADC input: no conversions and divider parked off, not a continuous battery divider load. A healthy cell plus `--` can be a refused polarity probe; it is not proof of safe park without the measurement.
4. With all power removed measure control-to-rails resistance as supporting hardware evidence. Record both values; an in-circuit resistance measurement alone cannot prove a unique pull resistor or identify firmware polarity. Do not change production polarity to make a reading pass.
5. Exchange sustained DMs during sampling; no new CTS/DATA timing failures. Check battery-only/USB insertion/removal behavior and plausible readings without reset/stuck panel.

**PASS:** physical scale, off-state and observed sampling/RF behavior agree for that board revision. **STOP:** continuous divider conduction, fabricated voltage, reset or timing regression.

<a id="nv-01"></a>

### NV-01 — inbox, cursor and tombstone durability on both backends

1. On ESP32 and N separately, store three DMs and channel posts. Record `pull_inbox 0 0` sequences/epoch; `mark_read dm <seq>` → `"result":"marked"`.
2. `del_msg dm <middle-seq>` → `"result":"erased"`. Power-cycle: other records, read cursor and epoch survive, deleted record absent. Append two more; no deleted sequence is reused.
3. After a **durably reported** receive, cut power within a second. Reboot and require that record. Radio hop ACK alone is not evidence that the inbox append was durably acknowledged.

**PASS:** observed real-medium records/cursors/deletion and completed append survive power removal. **STOP:** lost acknowledged data, resurrected deletion, reused sequence or ordinary-boot epoch change.

<a id="nv-02"></a>

### NV-02 — inbox capacity, write timing and torn append

1. Disposable ESP32: fill beyond its configured DM cap, retaining FS usage through a reviewed observation fixture. Record actual partition capacity/usage; preserve margin for rolling/compaction, with no unexplained append failures. No stock free-space field means the capacity arm remains OWED without that instrument.
2. At least 40 minutes of actual radio DM writes/rolls: stable heap/operation, no watchdog/jump-to-zero or systematic CTS→DATA failures correlated with flash erase. Record append count and occupancy, not time alone.
3. On each backend, use timed power cuts during a burst, keeping acknowledged records and deleted records identifiable. Reboot pull is parseable through/after a torn tail; committed prefix survives and subsequent append remains reachable. Capture cut timing; attempts that miss the write window are not proof of torn-write recovery.

**PASS:** measured full-store and exercised write/cut timing preserve readable committed data and service. **STOP:** lost committed prefix, corrupt traversal, reset or sustained radio starvation. This does not qualify lifetime flash wear.

<a id="nv-03"></a>

### NV-03 — inbox migrations, separately identified fixtures

1. For the semantic **v4→v5 inbox** fixture, retain an old DM and E2E-ACK receipt plus epoch/high-water. Flash current without erase, radio-isolating incompatible old images. First boot is enabled, old records gone, epoch exactly +1; first new sequence exceeds old high-water. Two more reboots do not move epoch; a fresh receipt still renders `type:"e2e_ack"`.
2. Separately, old incompatible metadata fixtures (ESP32 old v3 meta or nRF52 retired twin) may intentionally refuse with `enabled=0, mount_fault=6/6`. Do not call this the v4→v5 wipe. Record which fixture was flashed and original bytes.
3. Recover the incompatible-meta scratch node with `factory_reset confirm`, then verify enabled fresh mount and stable epoch. nRF52 formats InternalFS broadly: identity/config/peers/team/profile data must be re-provisioned.
4. A separate old **peer-store-version** fixture must be refused and restore zero peers once; a following ordinary boot stays clean. A compatible current peer store must instead survive (DM-01). A normal firmware upload alone is not a reason to expect all peers erased.

**PASS:** each identified old-record shape takes its own intended real migration/refusal, once. **STOP:** reused sequence, per-boot epoch ratchet, false enabled mount or wrong data-loss claim. Missing old images/records leaves that migration OWED.

<a id="nv-04"></a>

### NV-04 — clear, prep-restart and factory-reset domains

1. Seed DMs/channels and record epoch N/high-waters. `clear_inbox confirm` → `{"ack":"clear_inbox","result":"cleared","epoch":N+1,"dm_seq":<old high-water>,"chan_seq":<old high-water>}`. Two power cycles keep N+1, empty pull and unchanged config/routes; next sequence exceeds old high-water.
2. Reseed; local `prep-restart`, then local restart: empty inbox, epoch bumped exactly once; two more boots hold it. Healthy erase has no `WARN: inbox erase incomplete (messages may remain on flash)`; a real failure must warn, never unconditionally announce success.
3. On final scratch fixture seed peer, profile, keyring, UI preset and appropriate admin stores; record public state. `factory_reset confirm` erases these ordinary domains, fresh inbox epoch 1. ACCEPT boot has admin-id/ACL absent; CLIENT admin-key/admin-target absent. Fault-history domain is retained.
4. Run separately on ESP32/N; their format/recovery implementation is not interchangeable. Do not confuse a deliberate factory reset with an ordinary firmware upload.

**PASS:** actual destructive verbs affect exactly their documented domains and epoch once. **STOP:** surviving erased payload/trust, unintended preserved-domain loss, epoch ratchet or false success.

<a id="nv-05"></a>

### NV-05 — settings and join-profile power cuts

1. For each backend record a complete old main-config snapshot, create a different settings draft on OLED where available (console cfg on N), then interrupt actual SAVE/write across about five varied delays. Each reboot must be complete old or new config, never hybrid/unprovisioned/rejected. Record missed write windows honestly.
2. Separately seed `joinprofile set 1 layer=4 freq=<F> bw=125 sf=9 name="old"`; update to `layer=17 freq=<F2> bw=125 sf=7 name="new"` and vary cuts across about five attempts. List after every boot: complete old or new slot, never `PROFILE STORE INVALID` or absent after an established record.
3. A power loss on nRF52 remove-before-write **may reveal a real failure of this stronger retention criterion**; report it, do not borrow ESP32 qualification or call honest loss atomicity. RADMIN-04's explicitly accepted optional-store loss is a different contract.
4. Restore intended records after collecting evidence. Historical B193 results apply to the recorded earlier run/backend, not all future records or boards.

**PASS:** exercised cuts preserve complete old/new records for each named store/backend. **STOP:** tear, loss/default reseed or silent success inconsistent with persisted bytes.

<a id="nv-06"></a>

### NV-06 — keyring compaction and preset power cuts

1. Record four disposable `team keys` rows, active binding and decryption controls. Interrupt `team forgetkey <inactive-T> confirm` at varied delays (~10 trials): reboot must yield all four old rows or exactly three survivors in original relative order, with their real keys still usable.
2. Separately interrupt `team new` with room available: complete old/new state, no lost previous team or silently active uncommitted binding. A failed `/mrcfg` binding save, if actually exercised, must not restore the uncommitted key after reboot.
3. On OLED ESP32, interrupt `ui preset set dm3 loc=on "cut test"` (~10 delays): each boot old or new complete catalog. `record invalid` after a cut is a failed preservation observation; on an explicitly corrupted fixture the expected diagnosis is `  ui presets = DEFAULTS (record invalid — repaired on next successful change)` and a successful set repairs it.
4. Verify one inactive key via explicit saved-key use and a sealed post, plus the active key across reboot. Merely matching IDs does not detect smeared key bytes. Record preset backend as N/A on stock non-OLED N, not passed.

**PASS:** exercised writes preserve neighbours and whole records; intentional corruption is diagnosed/repaired honestly. **STOP:** silent key/catalog corruption, false durable binding or neighbour loss.

<a id="nv-07"></a>

### NV-07 — physical fault classification and write observation

Fixture-dependent; no production debug verb or arbitrary partition-offset command is authorised by this plan. Record a reviewed backend-specific method and original bytes before any fault injection. Without it these arms remain OWED.

1. Corrupt only ESP32 inbox metadata while records remain: both refused sides report `mount_fault=6/6`; missing meta over records reports `5/5`. A one-sided nRF52 `/mri_dm` corruption gives `6/0`; the overall inbox is disabled, surviving records are not silently erased. `mark_read` returns `io_error`.
2. A real NVS lookup/partition failure must not be classified fresh (`enabled=1, epoch=1`). Observe the actual error with the fixture; deleting all keys may only prove absence and is not an IO-failure control. Recover the scratch medium explicitly and verify a healthy mount has no fault line.
3. For main config/peer/profile no-op wear observations, a physical write trace/counter with a **positive changed-write control** is required. Repeating a value five times should add zero writes; changing once adds the appropriate write. Console `no change` alone is AUTO coverage, not physical wear measurement.
4. Record erase/append counts and backend conditions for any endurance experiment. This rewrite sets no invented lifetime/wear threshold and claims no wear qualification.

**PASS:** exercised physical error states are distinguished without silently fabricating fresh stores, and any write-count claim has a positive control. **STOP:** error-as-absence, unintended format or a write count with no responding control.

## Board radio qualification

<a id="radio-01"></a>

### RADIO-01 — each available board's real RF and USB

1. On each H/X/V/N image record board/banner, RF configuration and antenna/load. Upload/reconnect/command input work with complete lines; initial RX from a known peer succeeds **before the tested board's first local TX**.
2. Exchange tagged DMs in both directions and after reply transitions; record `rx`, `tx`, `txfail`, `txto`, and any RF-mode diagnostics. Healthy exchange has no unexplained mode/start/completion failure and returns to receive.
3. On V, runtime `fem=gc1109 lna=n/a` identifies V4.2; `fem=kct8103l lna=on` identifies V4.3. Healthy line includes `radiohw=1 rfcfg=1 rfok=1 rfmodefail=0 rfbandfail=0 rfout=22 rfchip=10`. Record actual revision; do not claim the other one ran.
4. V local refusals: `cfg set freq 862.999` → `> cfg err bad_value (freq 863..928 MHz)`; `cfg set tx_power 21` → `> cfg err bad_value (tx_power 22 dBm)`. Valid live RF state remains. Native USB recovery: hold GPIO0, reset, release after loader enumeration.
5. Run UI-01/POWER-01 on the V actually present, including initial RX, button/radio sleep wake and plausible battery reading; illumination alone does not qualify those.

**PASS:** each named physical board uploads, receives, transmits, recovers RX and reports its real variant. **STOP:** wrong detector state, false success, broken receive-after-TX or unidentified image.

<a id="radio-02"></a>

### RADIO-02 — V4 physical fault and conducted-output fixtures

1. With a reviewed non-destructive detector fixture, ambiguous/unstable input must yield `radio = INIT FAILED`, `fem=unknown`, `radiohw=0`, `rfok=0`, `rfmodefail=1`. Without fixture, keep OWED; do not damage the populated FEM.
2. With reviewed radio fault injection, force an actual startTransmit failure and TX watchdog abort separately. Relevant failure counters rise and RX recovers after each; a host fake is not the hardware event.
3. With suitable conducted RF equipment measure output at the ruled chip setting and record calibration/load/loss. Until measured, `rfout=22` is nominal, not a calibrated power result. V4.3 LNA-on/bypass comparison needs separately identified approved images and current/RX measurements.

**PASS:** observed detector/recovery and calibrated output support the particular board's claims. **STOP:** ambiguous hardware transmits, recovery wedges, or measurement contradicts the configured RF mapping. A single V cannot certify both V4 revisions.

## Session log

Append one line per hardware session. Update only the current-results table above; this log is immutable evidence, not a second current-status list. Preserve older table values in the new log entry when superseding a qualification. Historical sessions/results remain in the archive; unnamed builds and ambiguous old ticks are not promoted to PASS here.

| UTC date / operator | Source snapshot + image hashes / boards | Scenario IDs and observed outcomes | Raw logs / photos / instrument setup | Findings / unexercised arms |
|---|---|---|---|---|
| — | — | No hardware session performed for this rewrite | — | All current-image qualification awaits execution |
