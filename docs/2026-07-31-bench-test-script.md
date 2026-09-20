# Archived bench record — 2026-07-31-bench-test-script

The maintained hardware procedures and current results are in the [metal test plan](2026-09-20-metal-test-plan.md).
This document was consolidated on 2026-09-20; its original text and historical results are preserved in the [archive](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md).
See the [complete disposition map](2026-09-20-metal-test-triage.md) for retirement instruments and replacement scenarios.
Historical instructions and PASS marks in the archive do not qualify a new image.

<details>
<summary>Historical section links (compatibility anchors)</summary>

<a id="firmware-bench-acceptance-library--2026-07-31-onward"></a>

[Firmware bench acceptance library — 2026-07-31 onward](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#firmware-bench-acceptance-library--2026-07-31-onward)

<a id="status-notation"></a>

[Status notation](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#status-notation)

<a id="current-qualification-snapshot--2026-08-20"></a>

[Current qualification snapshot — 2026-08-20](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#current-qualification-snapshot--2026-08-20)

<a id="bench-worksheet"></a>

[Bench worksheet](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#bench-worksheet)

<a id="execution-rule"></a>

[Execution rule](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#execution-rule)

<a id="part-0--first-boot-after-flashing"></a>

[Part 0 — first boot after flashing](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-0--first-boot-after-flashing)

<a id="part-1--parser-and-role-refusals"></a>

[Part 1 — parser and role refusals](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-1--parser-and-role-refusals)

<a id="part-2--nv-write-coalescing"></a>

[Part 2 — NV write coalescing](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-2--nv-write-coalescing)

<a id="part-3--direct-messages-with--l"></a>

[Part 3 — direct messages with `-l`](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-3--direct-messages-with--l)

<a id="part-4--peer-store-persistence"></a>

[Part 4 — peer-store persistence](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-4--peer-store-persistence)

<a id="part-5--address-book-commands"></a>

[Part 5 — address-book commands](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-5--address-book-commands)

<a id="part-6--team-channel-encryption-inbox-delivery-and-key-grants"></a>

[Part 6 — team channel encryption, inbox delivery, and key grants](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-6--team-channel-encryption-inbox-delivery-and-key-grants)

<a id="b30-regression--final-destination-versus-next-hop"></a>

[B30 regression — final destination versus next hop](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#b30-regression--final-destination-versus-next-hop)

<a id="channel-delivery-and-merged-inbox-controls"></a>

[Channel delivery and merged-inbox controls](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#channel-delivery-and-merged-inbox-controls)

<a id="part-7--id-to-hash-resolution-and-tx-admission"></a>

[Part 7 — ID-to-hash resolution and TX admission](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-7--id-to-hash-resolution-and-tx-admission)

<a id="rig-a--static-node-with-one-heard-and-one-routed-only-peer"></a>

[Rig A — static node with one heard and one routed-only peer](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-a--static-node-with-one-heard-and-one-routed-only-peer)

<a id="rig-a-prime--same-static-node-before-identity-provisioning"></a>

[Rig A-prime — same static node before identity provisioning](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-a-prime--same-static-node-before-identity-provisioning)

<a id="rig-b--blecompanion-surfaces"></a>

[Rig B — BLE/companion surfaces](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-b--blecompanion-surfaces)

<a id="rig-c--team-node-with-direct-and-multi-hop-teammates"></a>

[Rig C — team node with direct and multi-hop teammates](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-c--team-node-with-direct-and-multi-hop-teammates)

<a id="rig-d--dual-plane-and-mobile-edge-cases"></a>

[Rig D — dual-plane and mobile edge cases](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-d--dual-plane-and-mobile-edge-cases)

<a id="rig-e--three-node-owner-only-rule"></a>

[Rig E — three-node owner-only rule](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-e--three-node-owner-only-rule)

<a id="rig-f--radio-saturation-and-device-only-traces"></a>

[Rig F — radio saturation and device-only traces](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#rig-f--radio-saturation-and-device-only-traces)

<a id="part-8--heltec-v3-oled-panel-bring-up-slices-ui-5-through-ui-7"></a>

[Part 8 — Heltec V3 OLED panel bring-up (slices UI-5 through UI-7)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-8--heltec-v3-oled-panel-bring-up-slices-ui-5-through-ui-7)

<a id="ui-6-additions-2026-08-05--the-three-metal-only-behaviours-the-feature-layer-adds"></a>

[UI-6 additions (2026-08-05) — the three metal-only behaviours the feature layer adds](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#ui-6-additions-2026-08-05--the-three-metal-only-behaviours-the-feature-layer-adds)

<a id="part-9--console-response-line-integrity-b95"></a>

[Part 9 — console response line integrity (§B95)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-9--console-response-line-integrity-b95)

<a id="current-semantics-and-known-gaps--not-checklist-items"></a>

[Current semantics and known gaps — not checklist items](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#current-semantics-and-known-gaps--not-checklist-items)

<a id="811--b103-the-distress-reply-is-team-scoped-2026-08-05-was-a-live-defect"></a>

[8.11 — §B103 the distress REPLY is TEAM-scoped (2026-08-05, was a LIVE defect)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#811--b103-the-distress-reply-is-team-scoped-2026-08-05-was-a-live-defect)

<a id="812--b102-an-unread-outcome-cannot-be-dismissed-2026-08-05"></a>

[8.12 — §B102 an unread outcome cannot be dismissed (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#812--b102-an-unread-outcome-cannot-be-dismissed-2026-08-05)

<a id="813--b101-an-alarm-leaves-no-armed-compose-modal-2026-08-05"></a>

[8.13 — §B101 an alarm leaves no armed compose modal (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#813--b101-an-alarm-leaves-no-armed-compose-modal-2026-08-05)

<a id="814--b108-round-2-the-unread-cap-is-a-display-limit-and-only-the-panel-can-say-so-2026-08-05"></a>

[8.14 — §B108 round 2: the unread cap is a DISPLAY limit, and only the panel can say so (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#814--b108-round-2-the-unread-cap-is-a-display-limit-and-only-the-panel-can-say-so-2026-08-05)

<a id="815--r1b109-a-reply-lights-a-dark-panel-a-strangers-post-does-not-2026-08-05-owner-ruled"></a>

[8.15 — §R1/B109 a REPLY lights a DARK panel; a stranger's post does not (2026-08-05, OWNER-RULED)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#815--r1b109-a-reply-lights-a-dark-panel-a-strangers-post-does-not-2026-08-05-owner-ruled)

<a id="816--r2b110-a-double-under-the-emergency-overlay-does-nothing-at-all-2026-08-05-owner-ruled"></a>

[8.16 — §R2/B110 a DOUBLE under the emergency overlay does nothing at all (2026-08-05, OWNER-RULED)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#816--r2b110-a-double-under-the-emergency-overlay-does-nothing-at-all-2026-08-05-owner-ruled)

<a id="817--ui-7-the-send-path-is-real-the-composed-line-is-what-the-radio-gets-2026-08-05"></a>

[8.17 — UI-7 the send path is REAL: the composed line is what the radio gets (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#817--ui-7-the-send-path-is-real-the-composed-line-is-what-the-radio-gets-2026-08-05)

<a id="818--41-the-alarm-carries--l-only-with-a-fix-and-goes-out-either-way-2026-08-05"></a>

[8.18 — §4.1 the alarm carries `-l` ONLY with a fix, and goes out either way (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#818--41-the-alarm-carries--l-only-with-a-fix-and-goes-out-either-way-2026-08-05)

<a id="819--b69-an-unconfirmed-send-must-never-read-as-sent-2026-08-05--safety"></a>

[8.19 — §B69 an unconfirmed send must never read as SENT (2026-08-05) ★★ SAFETY](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#819--b69-an-unconfirmed-send-must-never-read-as-sent-2026-08-05--safety)

<a id="820--ui-7-an-unconfirmed-dm-must-not-brick-the-send-path-2026-08-05"></a>

[8.20 — UI-7 an unconfirmed DM must not brick the send path (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#820--ui-7-an-unconfirmed-dm-must-not-brick-the-send-path-2026-08-05)

<a id="821--b64-a-teammate-that-left-the-roster-must-never-inherit-your-dm-2026-08-05-owner-ruled--safety"></a>

[8.21 — §B64 a teammate that LEFT the roster must never inherit your DM (2026-08-05, OWNER-RULED) ★★ SAFETY](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#821--b64-a-teammate-that-left-the-roster-must-never-inherit-your-dm-2026-08-05-owner-ruled--safety)

<a id="822--b113-an-accepted-canned-post-must-reach-sent-waiting-2026-08-05"></a>

[8.22 — §B113 an accepted canned post must reach `SENT, waiting` (2026-08-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#822--b113-an-accepted-canned-post-must-reach-sent-waiting-2026-08-05)

<a id="823--b115-the-alarms-attempt-counter-must-start-at-1-of-3-2026-08-05--measured-wrong-on-metal"></a>

[8.23 — §B115 the alarm's attempt counter must START at `1 of 3` (2026-08-05) ★★ MEASURED WRONG ON METAL](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#823--b115-the-alarms-attempt-counter-must-start-at-1-of-3-2026-08-05--measured-wrong-on-metal)

<a id="824--b117-the-terminal-alarm-headline-is-not-relayed-2026-08-05-owner-ruled--safety-wording"></a>

[8.24 — §B117 the terminal alarm headline is `NOT RELAYED` (2026-08-05, OWNER-RULED) ★★ SAFETY WORDING](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#824--b117-the-terminal-alarm-headline-is-not-relayed-2026-08-05-owner-ruled--safety-wording)

<a id="825--task-8-case-4-the-blocked-countdown-is-live-and-the-retry-is-automatic-2026-08-06"></a>

[8.25 — Task 8 case 4: the `BLOCKED` countdown is LIVE and the retry is AUTOMATIC (2026-08-06)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#825--task-8-case-4-the-blocked-countdown-is-live-and-the-retry-is-automatic-2026-08-06)

<a id="826--task-8-case-6-the-emergency-pre-empts-an-outstanding-dm-or-canned-channel-send-2026-08-06"></a>

[8.26 — Task 8 case 6: the emergency PRE-EMPTS an outstanding DM or canned channel send (2026-08-06)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#826--task-8-case-6-the-emergency-pre-empts-an-outstanding-dm-or-canned-channel-send-2026-08-06)

<a id="827--task-8-case-5-an-emergency-fires-from-a-fully-blanked-panel-2026-08-06"></a>

[8.27 — Task 8 case 5: an emergency fires from a FULLY BLANKED panel (2026-08-06)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#827--task-8-case-5-an-emergency-fires-from-a-fully-blanked-panel-2026-08-06)

<a id="part-10--b132-a-gateway-is-never-a-mobile-home-2026-08-06"></a>

[Part 10 — §B132 a gateway is never a mobile home (2026-08-06)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-10--b132-a-gateway-is-never-a-mobile-home-2026-08-06)

<a id="101--a-gateway-refuses-cfg-set-host_mobiles-on-the-only-check-for-an-untested-source-file"></a>

[10.1 — a GATEWAY refuses `cfg set host_mobiles on` (the only check for an untested source file)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#101--a-gateway-refuses-cfg-set-host_mobiles-on-the-only-check-for-an-untested-source-file)

<a id="102--a-mobile-whose-home-was-the-gateway-detects-the-loss-and-re-homes-to-a-static-node--the-owners-case"></a>

[10.2 — a mobile whose home WAS the gateway detects the loss and re-homes to a static node ★★ the owner's case](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#102--a-mobile-whose-home-was-the-gateway-detects-the-loss-and-re-homes-to-a-static-node--the-owners-case)

<a id="part-11--35-durable-single-record-inbox-delete-ui-7d-slice-a-2026-08-06"></a>

[Part 11 — §3.5 durable single-record inbox delete (UI-7D slice A, 2026-08-06)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-11--35-durable-single-record-inbox-delete-ui-7d-slice-a-2026-08-06)

<a id="111--a-delete-survives-a-reboot--the-whole-point-of-the-slice-nrf52--qspi-only"></a>

[11.1 — a delete SURVIVES A REBOOT ★★ the whole point of the slice (nRF52 / QSPI only)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#111--a-delete-survives-a-reboot--the-whole-point-of-the-slice-nrf52--qspi-only)

<a id="112--a-deleted-record-never-reappears-under-new-traffic"></a>

[11.2 — a deleted record never reappears under new traffic](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#112--a-deleted-record-never-reappears-under-new-traffic)

<a id="113--the-three-outcomes-are-distinguishable-at-the-console-the-whole-reason-the-api-is-not-a-bool"></a>

[11.3 — the three outcomes are distinguishable at the console (the whole reason the API is not a bool)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#113--the-three-outcomes-are-distinguishable-at-the-console-the-whole-reason-the-api-is-not-a-bool)

<a id="113b--a-malformed-delete-target-is-refused-never-acted-on-b136-added-2026-08-07"></a>

[11.3b — a malformed DELETE TARGET is refused, never acted on (((B136)), added 2026-08-07)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#113b--a-malformed-delete-target-is-refused-never-acted-on-b136-added-2026-08-07)

<a id="115--a-torn-durable-write-is-recovered-and-the-next-record-is-still-readable--b135-added-2026-08-07--nrf52--qspi-only"></a>

[11.5 — a TORN durable write is recovered, and the next record is still readable ★★ (((B135)), added 2026-08-07) — **nRF52 / QSPI ONLY**](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#115--a-torn-durable-write-is-recovered-and-the-next-record-is-still-readable--b135-added-2026-08-07--nrf52--qspi-only)

<a id="114--the-board-control-so-111-cannot-pass-on-the-wrong-hardware"></a>

[11.4 — the board control, so 11.1 cannot pass on the wrong hardware](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#114--the-board-control-so-111-cannot-pass-on-the-wrong-hardware)

<a id="part-12--mh-s1-the-mobile-attachment-admission-boundary-2026-08-07"></a>

[Part 12 — §MH-S1 the mobile-attachment ADMISSION boundary (2026-08-07)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-12--mh-s1-the-mobile-attachment-admission-boundary-2026-08-07)

<a id="121--a-mobile-whose-own-radio-refuses-the-claim-must-not-report-itself-registered--safety"></a>

[12.1 — a mobile whose OWN radio refuses the CLAIM must NOT report itself registered ★★ SAFETY](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#121--a-mobile-whose-own-radio-refuses-the-claim-must-not-report-itself-registered--safety)

<a id="122--a-host-whose-own-radio-refuses-the-offer-says-so"></a>

[12.2 — a host whose OWN radio refuses the OFFER says so](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#122--a-host-whose-own-radio-refuses-the-offer-says-so)

<a id="part-13--mh-s4-the-confirmed-attachment-fsm-and-the-two-planes-2026-08-08"></a>

[Part 13 — §MH-S4 the CONFIRMED-attachment FSM and the two planes (2026-08-08)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-13--mh-s4-the-confirmed-attachment-fsm-and-the-two-planes-2026-08-08)

<a id="131--mobile-status-reports-claiming-before-it-reports-registered--safety--the-s0-4-surface"></a>

[13.1 — `mobile status` reports `claiming` BEFORE it reports `registered` ★★ SAFETY / the §S0-4 surface](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#131--mobile-status-reports-claiming-before-it-reports-registered--safety--the-s0-4-surface)

<a id="132--mobile-unregister-ends-the-session-and-puts-nothing-on-the-air"></a>

[13.2 — `mobile unregister` ends the session and puts NOTHING on the air](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#132--mobile-unregister-ends-the-session-and-puts-nothing-on-the-air)

<a id="133--a-busy-channel-must-not-deregister-a-healthy-mobile-b139--the-one-behaviour-check-and-why"></a>

[13.3 — a busy channel must NOT deregister a healthy mobile (((B139))) — the ONE behaviour check, and why](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#133--a-busy-channel-must-not-deregister-a-healthy-mobile-b139--the-one-behaviour-check-and-why)

<a id="134--mh-s4b-mobile-status-reports-the-solicitation-substate-and-the-retry-window"></a>

[13.4 — §MH-S4b: `mobile status` reports the SOLICITATION substate and the retry window](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#134--mh-s4b-mobile-status-reports-the-solicitation-substate-and-the-retry-window)

<a id="135--mh-s4b-the-confirmation-age-must-not-wrap-and-it-is-only-visible-on-metal"></a>

[13.5 — §MH-S4b: the confirmation AGE must not wrap, and it is only visible on metal](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#135--mh-s4b-the-confirmation-age-must-not-wrap-and-it-is-only-visible-on-metal)

<a id="136--mh-s4b-status-exposes-the-two-offer-admission-counters"></a>

[13.6 — §MH-S4b: `status` exposes the two OFFER admission counters](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#136--mh-s4b-status-exposes-the-two-offer-admission-counters)

<a id="137--mh-s4b-the-confirmed-device-log-and-mobile-unregister-really-stays-dormant"></a>

[13.7 — §MH-S4b: the CONFIRMED device log, and `mobile unregister` really stays dormant](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#137--mh-s4b-the-confirmed-device-log-and-mobile-unregister-really-stays-dormant)

<a id="part-17--mh-s5-host-row-lifetime-and-the-expired-id-return-2026-08-10"></a>

[Part 17 — §MH-S5 host-row lifetime and the expired-id return (2026-08-10)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-17--mh-s5-host-row-lifetime-and-the-expired-id-return-2026-08-10)

<a id="171---a-host-row-is-physically-gone-at-the-expiry-boundary-on-real-time"></a>

[17.1 — ★★ A HOST ROW IS PHYSICALLY GONE AT THE EXPIRY BOUNDARY, ON REAL TIME](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#171---a-host-row-is-physically-gone-at-the-expiry-boundary-on-real-time)

<a id="171b---b177-fix-a-mobile-that-is-audible-but-only-beaconing-is-still-expired-owner-ruling-ledger-116"></a>

[17.1b — ★★ §B177-FIX: A MOBILE THAT IS AUDIBLE BUT ONLY **BEACONING** IS STILL EXPIRED (owner ruling, ledger §1.16)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#171b---b177-fix-a-mobile-that-is-audible-but-only-beaconing-is-still-expired-owner-ruling-ledger-116)

<a id="172--the-per-row-hosting-view-b154-b-and-the-redirect-kind"></a>

[17.2 — the per-row hosting view (((B154)) (b)), and the REDIRECT kind](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#172--the-per-row-hosting-view-b154-b-and-the-redirect-kind)

<a id="173---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail"></a>

[17.3 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#173---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail)

<a id="part-18--mh-s5b-the-weakmissed-home-canvass-and-the-verified-echo-switch-2026-08-11"></a>

[Part 18 — §MH-S5b the weak/missed-home canvass and the verified-echo switch (2026-08-11)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-18--mh-s5b-the-weakmissed-home-canvass-and-the-verified-echo-switch-2026-08-11)

<a id="181---the-8-minute-strong-link-idle-loss-bound-is-the-accepted-trade-off-not-a-fault-spec-123-8"></a>

[18.1 — ★★ THE ≈8-MINUTE STRONG-LINK IDLE-LOSS BOUND IS THE ACCEPTED TRADE-OFF, NOT A FAULT (spec §12.3-8)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#181---the-8-minute-strong-link-idle-loss-bound-is-the-accepted-trade-off-not-a-fault-spec-123-8)

<a id="182---a-healthy-home-is-kept-silently-a-weak-but-answering-one-is-also-kept-silently-trigger-1-deferred-only-a-missed-check-canvasses-then-switches-spec-123-7"></a>

[18.2 — ★★ A HEALTHY HOME IS KEPT SILENTLY; **A WEAK-BUT-ANSWERING ONE IS *ALSO* KEPT SILENTLY** (trigger 1 DEFERRED); ONLY A **MISSED CHECK** CANVASSES, THEN SWITCHES (spec §12.3-7)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#182---a-healthy-home-is-kept-silently-a-weak-but-answering-one-is-also-kept-silently-trigger-1-deferred-only-a-missed-check-canvasses-then-switches-spec-123-7)

<a id="183---what-is-not-owed-here"></a>

[18.3 — ⓘ WHAT IS **NOT** OWED HERE](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#183---what-is-not-owed-here)

<a id="part-14--b153b157-the-retired-rts-derived-terminal-decisions-2026-08-08"></a>

[Part 14 — §B153/§B157 the retired RTS-derived terminal decisions (2026-08-08)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-14--b153b157-the-retired-rts-derived-terminal-decisions-2026-08-08)

<a id="141---a-lost-ack-must-still-deliver-exactly-once-the-behaviour-the-slice-turns-on"></a>

[14.1 — ★★ A LOST ACK MUST STILL DELIVER EXACTLY ONCE (the behaviour the slice turns on)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#141---a-lost-ack-must-still-deliver-exactly-once-the-behaviour-the-slice-turns-on)

<a id="142--the-recovery-path-costs-more-airtime-now-record-what-the-radio-says"></a>

[14.2 — the recovery path costs more airtime now; record what the radio says](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#142--the-recovery-path-costs-more-airtime-now-record-what-the-radio-says)

<a id="143--the-flight-the-implicit-ack-used-to-cancel-must-now-complete-on-its-own"></a>

[14.3 — the flight the implicit ACK used to cancel must now complete on its own](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#143--the-flight-the-implicit-ack-used-to-cancel-must-now-complete-on-its-own)

<a id="part-15--hybrid-rts-s1-the-1011-byte-unicast-rts-2026-08-08"></a>

[Part 15 — §HYBRID-RTS-S1 the 10/11-byte unicast RTS (2026-08-08)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-15--hybrid-rts-s1-the-1011-byte-unicast-rts-2026-08-08)

<a id="151---reflash-every-board-in-the-topology-together--the-precondition-not-a-test"></a>

[15.1 — ★★★ REFLASH EVERY BOARD IN THE TOPOLOGY TOGETHER — the precondition, not a test](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#151---reflash-every-board-in-the-topology-together--the-precondition-not-a-test)

<a id="152---a-mixed-pair-fails-in-the-predicted-place-run-once-deliberately-then-reflash"></a>

[15.2 — ★★ A MIXED PAIR FAILS IN THE PREDICTED PLACE (run ONCE, deliberately, then reflash)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#152---a-mixed-pair-fails-in-the-predicted-place-run-once-deliberately-then-reflash)

<a id="153--the-rts-really-is-10-bytes-on-the-air-and-11-when-the-dm-is-sealed"></a>

[15.3 — the RTS really is 10 bytes on the air, and 11 when the DM is sealed](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#153--the-rts-really-is-10-bytes-on-the-air-and-11-when-the-dm-is-sealed)

<a id="154--the-cts-wait-grew-and-only-the-no-response-path-should-feel-it"></a>

[15.4 — the CTS-wait grew, and only the NO-RESPONSE path should feel it](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#154--the-cts-wait-grew-and-only-the-no-response-path-should-feel-it)

<a id="155---superseded-by-hybrid-rts-s2-2026-08-08-the-terminal-rcvd-cts-is-now-produced"></a>

[15.5 — ⛔⛔ SUPERSEDED BY §HYBRID-RTS-S2 (2026-08-08): the terminal `RCVD` CTS is now PRODUCED](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#155---superseded-by-hybrid-rts-s2-2026-08-08-the-terminal-rcvd-cts-is-now-produced)

<a id="155-historical--the-terminal-rcvd-cts-must-still-never-appear"></a>

[15.5 (historical) — the terminal `RCVD` CTS must still NEVER appear](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#155-historical--the-terminal-rcvd-cts-must-still-never-appear)

<a id="part-16--hybrid-rts-s2-the-first-67-byte-frame-ever-to-fly-2026-08-08"></a>

[Part 16 — §HYBRID-RTS-S2 the FIRST 6/7-byte frame ever to fly (2026-08-08)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-16--hybrid-rts-s2-the-first-67-byte-frame-ever-to-fly-2026-08-08)

<a id="161---a-lost-ack-retry-completes-on-a-6-byte-cts-with-no-second-data"></a>

[16.1 — ★★ A LOST-ACK RETRY COMPLETES ON A 6-BYTE CTS, WITH NO SECOND DATA](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#161---a-lost-ack-retry-completes-on-a-6-byte-cts-with-no-second-data)

<a id="162---the-encrypted-arm-because-it-is-the-one-the-corpus-cannot-see"></a>

[16.2 — ★ THE ENCRYPTED ARM, BECAUSE IT IS THE ONE THE CORPUS CANNOT SEE](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#162---the-encrypted-arm-because-it-is-the-one-the-corpus-cannot-see)

<a id="163---what-is-not-owed-here-so-nobody-adds-it"></a>

[16.3 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds it](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#163---what-is-not-owed-here-so-nobody-adds-it)

<a id="part-19--ui-7d-slice-b-the-on-device-inbox-detaildelete-modal-2026-08-13"></a>

[Part 19 — §UI-7D slice B: the on-device inbox detail/delete modal (2026-08-13)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-19--ui-7d-slice-b-the-on-device-inbox-detaildelete-modal-2026-08-13)

<a id="191---the-delete-is-real--and-since-b134-it-survives-the-reboot-on-every-board"></a>

[19.1 — ★★ THE DELETE IS REAL — AND SINCE ((B134)) IT SURVIVES THE REBOOT ON EVERY BOARD](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#191---the-delete-is-real--and-since-b134-it-survives-the-reboot-on-every-board)

<a id="192--the-panels-own-21-columns-and-the-2-s-page-turn-on-real-time"></a>

[19.2 — the panel's own 21 columns, and the 2 s page turn on real time](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#192--the-panels-own-21-columns-and-the-2-s-page-turn-on-real-time)

<a id="193--the-emergency-interplay-which-is-the-safety-half"></a>

[19.3 — the emergency interplay, which is the safety half](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#193--the-emergency-interplay-which-is-the-safety-half)

<a id="194---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail"></a>

[19.4 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#194---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail)

<a id="195--b134-partition-capacity-at-a-full-ring-the-one-number-that-could-only-be-derived"></a>

[19.5 — ((B134)) partition capacity at a full ring (the one number that could only be derived)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#195--b134-partition-capacity-at-a-full-ring-the-one-number-that-could-only-be-derived)

<a id="196--b134-flash-wear--the-radio-critical-interval-soak"></a>

[19.6 — ((B134)) flash wear + the radio-critical interval (soak)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#196--b134-flash-wear--the-radio-critical-interval-soak)

<a id="197--b134-reflash-wipes-once--and-only-once--the-one-time-v3-meta-refusal"></a>

[19.7 — ((B134)) reflash wipes once — and only once (+ the one-time v3-meta refusal)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#197--b134-reflash-wipes-once--and-only-once--the-one-time-v3-meta-refusal)

<a id="198--b134-the-two-destructive-verbs-really-destroy"></a>

[19.8 — ((B134)) the two destructive verbs really destroy](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#198--b134-the-two-destructive-verbs-really-destroy)

<a id="199--b134-the-destructive-verbs-cannot-lie"></a>

[19.9 — ((B134)) the destructive verbs cannot lie](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#199--b134-the-destructive-verbs-cannot-lie)

<a id="1910--b134-the-sync-really-reaches-the-medium-the-one-line-no-host-gate-compiles"></a>

[19.10 — ((B134)) the sync really reaches the medium (the one line no host gate compiles)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#1910--b134-the-sync-really-reaches-the-medium-the-one-line-no-host-gate-compiles)

<a id="1911--b134-the-corrupted-metadata-refusal--and-that-it-never-fires-on-a-healthy-node"></a>

[19.11 — ((B134)) the corrupted-metadata refusal — and that it never fires on a healthy node](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#1911--b134-the-corrupted-metadata-refusal--and-that-it-never-fires-on-a-healthy-node)

<a id="1912--b134-the-prep-restart-epoch-bump-is-exactly-once-the-repeated-boot-check"></a>

[19.12 — ((B134)) the prep-restart epoch bump is exactly-once (the repeated-boot check)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#1912--b134-the-prep-restart-epoch-bump-is-exactly-once-the-repeated-boot-check)

<a id="1913--b134-the-nvs-classifier-partition-level-corruption"></a>

[19.13 — ((B134)) the NVS classifier (partition-level corruption)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#1913--b134-the-nvs-classifier-partition-level-corruption)

<a id="191n-197n--b260-the-nrf52-mirror-set-qspi-faults-are-host-unreachable-the-twin-is-retired"></a>

[19.1n-19.7n — ((B260)) the nRF52 mirror set (QSPI faults are host-unreachable; the twin is retired)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#191n-197n--b260-the-nrf52-mirror-set-qspi-faults-are-host-unreachable-the-twin-is-retired)

<a id="1914--b134-the-mark_read-ack-tells-the-truth"></a>

[19.14 — ((B134)) the mark_read ack tells the truth](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#1914--b134-the-mark_read-ack-tells-the-truth)

<a id="part-20--ui-14-the-settings-screen-and-the-one-thing-no-automated-gate-can-reach--real-mrcfg"></a>

[Part 20 — §UI-14: the SETTINGS screen, and the ONE thing no automated gate can reach — REAL `/mrcfg`](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-20--ui-14-the-settings-screen-and-the-one-thing-no-automated-gate-can-reach--real-mrcfg)

<a id="201--the-menu-and-that-a-draft-is-ram-only"></a>

[20.1 — the menu, and that a draft is RAM only](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#201--the-menu-and-that-a-draft-is-ram-only)

<a id="202---the-save-that-reaches-real-flash-and-what-it-must-not-destroy"></a>

[20.2 — ★ THE SAVE THAT REACHES REAL FLASH, AND WHAT IT MUST NOT DESTROY](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#202---the-save-that-reaches-real-flash-and-what-it-must-not-destroy)

<a id="203--discard-and-the-conflict-the-companion-can-cause"></a>

[20.3 — DISCARD, and the conflict the companion can cause](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#203--discard-and-the-conflict-the-companion-can-cause)

<a id="204--the-reboot-class-field-and-the-row-that-stays-until-the-reboot"></a>

[20.4 — the reboot-class field, and the row that stays until the reboot](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#204--the-reboot-class-field-and-the-row-that-stays-until-the-reboot)

<a id="205---the-reset-during-write-check-which-is-why-b193-is-still-open"></a>

[20.5 — ⛔⛔ THE RESET-DURING-WRITE CHECK, which is why ((B193)) is still open](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#205---the-reset-during-write-check-which-is-why-b193-is-still-open)

<a id="206---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail"></a>

[20.6 — ⓘ WHAT IS **NOT** OWED HERE, so nobody adds a step that cannot fail](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#206---what-is-not-owed-here-so-nobody-adds-a-step-that-cannot-fail)

<a id="207--notify-every-save-b194-the-other-six-verbs-added-2026-08-13"></a>

[20.7 — §notify-every-save (((B194))): the OTHER six verbs, added 2026-08-13](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#207--notify-every-save-b194-the-other-six-verbs-added-2026-08-13)

<a id="part-21--t2-hardware-tx-completion-diagnostics-2026-08-14"></a>

[Part 21 — §T2 hardware TX-completion diagnostics (2026-08-14)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-21--t2-hardware-tx-completion-diagnostics-2026-08-14)

<a id="part-22--t3-the-appui-half-of-the-tx-completion-arc-2026-08-14"></a>

[Part 22 — §T3 the app/UI half of the TX-completion arc (2026-08-14)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-22--t3-the-appui-half-of-the-tx-completion-arc-2026-08-14)

<a id="221--the-console-line-the-cores-push-produces"></a>

[22.1 — the console line the core's push produces](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#221--the-console-line-the-cores-push-produces)

<a id="222--the-panel-word-and-the-failure-shape-that-is-new"></a>

[22.2 — the panel word, and the failure shape that is new](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#222--the-panel-word-and-the-failure-shape-that-is-new)

<a id="part-23--b197b198b200-the-sleepwake-seam-2026-08-14-amended-2026-08-15"></a>

[Part 23 — §B197/§B198/§B200 the sleep/wake seam (2026-08-14, amended 2026-08-15)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-23--b197b198b200-the-sleepwake-seam-2026-08-14-amended-2026-08-15)

<a id="231---the-coexistence-test-run-it-after-236-nothing-else-in-this-part-means-anything-if-it-fails"></a>

[23.1 — ⛔⛔ THE COEXISTENCE TEST. Run it after 23.6; nothing else in this Part means anything if it fails](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#231---the-coexistence-test-run-it-after-236-nothing-else-in-this-part-means-anything-if-it-fails)

<a id="232--the-failure-line-and-the-fail-closed-behaviour-behind-it"></a>

[23.2 — the failure line, and the FAIL-CLOSED behaviour behind it](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#232--the-failure-line-and-the-fail-closed-behaviour-behind-it)

<a id="233--b198-a-frame-must-render-promptly-not-over-8-seconds"></a>

[23.3 — §B198: a frame must render promptly, not over ~8 seconds](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#233--b198-a-frame-must-render-promptly-not-over-8-seconds)

<a id="234--sleep-resumes-and-the-emergency-timing-is-unchanged"></a>

[23.4 — sleep RESUMES, and the emergency timing is unchanged](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#234--sleep-resumes-and-the-emergency-timing-is-unchanged)

<a id="235---gpio0-is-the-boot-strap-the-recovery-instruction-so-it-is-not-rediscovered-in-a-panic"></a>

[23.5 — ⚠ GPIO0 IS THE BOOT STRAP: the recovery instruction, so it is not rediscovered in a panic](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#235---gpio0-is-the-boot-strap-the-recovery-instruction-so-it-is-not-rediscovered-in-a-panic)

<a id="236---b200-a-long-press-must-not-panic-the-node-run-this-first--it-is-the-reproducer"></a>

[23.6 — ⛔⛔ ((B200)): A LONG PRESS MUST NOT PANIC THE NODE. **Run this FIRST — it is the reproducer**](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#236---b200-a-long-press-must-not-panic-the-node-run-this-first--it-is-the-reproducer)

<a id="237---retain-the-elf-for-every-image-you-flash-the-b200-capture-was-undecodable"></a>

[23.7 — ★ RETAIN THE ELF FOR EVERY IMAGE YOU FLASH (the ((B200)) capture was undecodable)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#237---retain-the-elf-for-every-image-you-flash-the-b200-capture-was-undecodable)

<a id="part-24--chrome-3-the-status-strip-and-its-repaint-invalidation-2026-08-16"></a>

[Part 24 — §CHROME-3: the status strip and its repaint invalidation (2026-08-16)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-24--chrome-3-the-status-strip-and-its-repaint-invalidation-2026-08-16)

<a id="241--the-strip-is-legible-and-nothing-overlaps-design-31"></a>

[24.1 — the strip is LEGIBLE and nothing overlaps (design §3.1)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#241--the-strip-is-legible-and-nothing-overlaps-design-31)

<a id="242--each-slot-agrees-with-the-console-and-claims-nothing-more"></a>

[24.2 — each slot agrees with the console, and claims nothing more](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#242--each-slot-agrees-with-the-console-and-claims-nothing-more)

<a id="243---idle-sleep-still-works-with-the-strip-enabled-design-1211--the-regression-guard"></a>

[24.3 — ★★ IDLE SLEEP STILL WORKS WITH THE STRIP ENABLED (design §12.11 — the regression guard)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#243---idle-sleep-still-works-with-the-strip-enabled-design-1211--the-regression-guard)

<a id="part-25--chrome-4-the-navigation-rail-the-config-badge-and-the-19-column-body-2026-08-16"></a>

[Part 25 — §CHROME-4: the navigation rail, the config badge and the 19-column body (2026-08-16)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-25--chrome-4-the-navigation-rail-the-config-badge-and-the-19-column-body-2026-08-16)

<a id="251--geometry-exactly-one-boxed-icon-and-no-text-touches-the-rail-design-121"></a>

[25.1 — geometry: exactly one boxed icon, and no text touches the rail (design §12.1)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#251--geometry-exactly-one-boxed-icon-and-no-text-touches-the-rail-design-121)

<a id="252--the-modal-mapping-the-rail-names-the-body-not-the-screen-underneath-design-122"></a>

[25.2 — the modal mapping: the rail names the body, not the screen underneath (design §12.2)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#252--the-modal-mapping-the-rail-names-the-body-not-the-screen-underneath-design-122)

<a id="253---emergency-the-rail-disappears-and-every-headline-is-complete-design-123"></a>

[25.3 — ★★ emergency: the rail disappears and every headline is COMPLETE (design §12.3)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#253---emergency-the-rail-disappears-and-every-headline-is-complete-design-123)

<a id="254--the-settings-badge-follows-the-priority-table-and-settings-still-says-why-design-128"></a>

[25.4 — the SETTINGS badge follows the priority table, and SETTINGS still says WHY (design §12.8)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#254--the-settings-badge-follows-the-priority-table-and-settings-still-says-why-design-128)

<a id="255--the-19-column-body-on-the-two-screens-where-it-is-tightest"></a>

[25.5 — the 19-column body, on the two screens where it is tightest](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#255--the-19-column-body-on-the-two-screens-where-it-is-tightest)

<a id="256---idle-sleep-still-works-with-the-rail-enabled-design-1211--the-same-regression-guard"></a>

[25.6 — ★★ IDLE SLEEP STILL WORKS WITH THE RAIL ENABLED (design §12.11 — the same regression guard)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#256---idle-sleep-still-works-with-the-rail-enabled-design-1211--the-same-regression-guard)

<a id="257--the-non-team-oled-build-gateway_heltec"></a>

[25.7 — the non-team OLED build (`gateway_heltec`)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#257--the-non-team-oled-build-gateway_heltec)

<a id="part-26--b196-the-once-per-boot-rtc-power-domain-assert-2026-08-17"></a>

[Part 26 — ((B196)): the once-per-boot RTC power-domain assert (2026-08-17)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-26--b196-the-once-per-boot-rtc-power-domain-assert-2026-08-17)

<a id="completion-record"></a>

[Completion record](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#completion-record)

<a id="part-27--b207-the-typed-team-provisioning-transaction-2026-08-17"></a>

[Part 27 — ((B207)): the typed team-provisioning transaction (2026-08-17)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-27--b207-the-typed-team-provisioning-transaction-2026-08-17)

<a id="-result--run-2026-08-18-seven-pass--three-not-run--four-findings"></a>

[✅ RESULT — RUN 2026-08-18, SEVEN PASS / THREE NOT-RUN / FOUR FINDINGS](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#-result--run-2026-08-18-seven-pass--three-not-run--four-findings)

<a id="271---stackhw-after-a-team-new---nrf52-only-run-it-on-xiao_sx1262--xiao_mobile-not-on-the-heltec"></a>

[27.1 — ★★ `stackhw` after a `team new` — ⛔ **nRF52 ONLY. RUN IT ON `xiao_sx1262` / `xiao_mobile`, NOT ON THE HELTEC.**](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#271---stackhw-after-a-team-new---nrf52-only-run-it-on-xiao_sx1262--xiao_mobile-not-on-the-heltec)

<a id="272--team-0-with-a-phy-argument-is-refused-and-nothing-changes"></a>

[27.2 — `team 0` with a PHY argument is refused, and nothing changes](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#272--team-0-with-a-phy-argument-is-refused-and-nothing-changes)

<a id="273--a-bare-team-0-still-leaves-and-preserves-the-phy-the-owner-ruling"></a>

[27.3 — a bare `team 0` still leaves, and PRESERVES the PHY (the owner ruling)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#273--a-bare-team-0-still-leaves-and-preserves-the-phy-the-owner-ruling)

<a id="274--a-truly-unchanged-same-team-request-reports-no-change"></a>

[27.4 — a truly-unchanged same-team request reports `no change`](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#274--a-truly-unchanged-same-team-request-reports-no-change)

<a id="275--a-same-team-re-key-preserves-the-local-id--the-defect-this-slice-exists-to-prevent"></a>

[27.5 — a same-team re-key preserves the local id (⛔ the defect this slice exists to prevent)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#275--a-same-team-re-key-preserves-the-local-id--the-defect-this-slice-exists-to-prevent)

<a id="276--a-static-nodes-team-new-honours-its-phy--behaviour-change"></a>

[27.6 — a static node's `team new` honours its PHY (⚠ behaviour change)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#276--a-static-nodes-team-new-honours-its-phy--behaviour-change)

<a id="277--the-save-failure-arm-if-it-can-be-provoked-at-all"></a>

[27.7 — the save-failure arm, if it can be provoked at all](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#277--the-save-failure-arm-if-it-can-be-provoked-at-all)

<a id="278---an-explicit-phy-request-is-applied-when-the-radio-diverges-from-the-record-qg-round-3-blocker"></a>

[27.8 — ★★ an explicit PHY request is APPLIED when the RADIO diverges from the record (QG round-3 blocker)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#278---an-explicit-phy-request-is-applied-when-the-radio-diverges-from-the-record-qg-round-3-blocker)

<a id="279--and-a-converged-request-still-reports-no-change"></a>

[27.9 — …and a converged request still reports `no change`](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#279--and-a-converged-request-still-reports-no-change)

<a id="2710--an-explicit-key-is-installed-when-the-live-key-is-absent---host-proven-metal-conditional"></a>

[27.10 — an explicit key is INSTALLED when the live key is absent — ⛔ **HOST-PROVEN, METAL-CONDITIONAL**](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2710--an-explicit-key-is-installed-when-the-live-key-is-absent---host-proven-metal-conditional)

<a id="2711---b209-a-team-phy-tail-must-not-authorise-home-attachment-metal-only"></a>

[27.11 — ★ ((B209)): a team PHY tail must NOT authorise home attachment (metal-only)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2711---b209-a-team-phy-tail-must-not-authorise-home-attachment-metal-only)

<a id="2712---b211-a-team-phy-tail-preserves-the-data-sf_list-real-nv--power-cycle"></a>

[27.12 — ★ ((B211)): a team PHY tail PRESERVES the DATA `sf_list` (real NV + power-cycle)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2712---b211-a-team-phy-tail-preserves-the-data-sf_list-real-nv--power-cycle)

<a id="2713---b211-resolution-reads-the-record-not-live-state-the-pin-1b-case-on-metal"></a>

[27.13 — ★★ ((B211)): resolution reads the RECORD, not live state (the pin-1b case on metal)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2713---b211-resolution-reads-the-record-not-live-state-the-pin-1b-case-on-metal)

<a id="2714---b210-team-dad-now-means-dad-and-this-repairs-278s-discriminator"></a>

[27.14 — ★ ((B210)): `team-DAD` now means DAD, and this REPAIRS 27.8's discriminator](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2714---b210-team-dad-now-means-dad-and-this-repairs-278s-discriminator)

<a id="2715---b212-the-specific-team-0-refusal-now-wins-and-the-verb-name-is-right"></a>

[27.15 — ★ ((B212)): the SPECIFIC `team 0` refusal now wins, and the verb name is right](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2715---b212-the-specific-team-0-refusal-now-wins-and-the-verb-name-is-right)

<a id="2716---b214-cfg-mobile-reg-reports-attachment-state-not-merely-a-home-id"></a>

[27.16 — ★ ((B214)): `cfg mobile-reg:` reports attachment state, not merely a home id](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2716---b214-cfg-mobile-reg-reports-attachment-state-not-merely-a-home-id)

<a id="2717---b230-the-incomplete-phy-refusal-names-the-missing-part-and-a-remedy-that-works"></a>

[27.17 — ★ ((B230)): the incomplete-PHY refusal names the MISSING part and a remedy that WORKS](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#2717---b230-the-incomplete-phy-refusal-names-the-missing-part-and-a-remedy-that-works)

<a id="part-28--ui-15-slice-2-the-mrjoin-profile-store-on-real-flash-2026-08-19"></a>

[Part 28 — §UI-15 slice 2: the `/mrjoin` profile store on REAL flash (2026-08-19)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-28--ui-15-slice-2-the-mrjoin-profile-store-on-real-flash-2026-08-19)

<a id="284---the-fresh-chip-line-heltec-v3-mr-nvs-namespace-never-written"></a>

[28.4 — ★ the FRESH-CHIP line (Heltec V3, `mr` NVS namespace never written)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#284---the-fresh-chip-line-heltec-v3-mr-nvs-namespace-never-written)

<a id="285--the-strict-index-either-board-no-reflash"></a>

[28.5 — the strict index (either board, no reflash)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#285--the-strict-index-either-board-no-reflash)

<a id="286---not-reachable-on-the-bench-stated-rather-than-glossed"></a>

[28.6 — ⛔ NOT REACHABLE ON THE BENCH, stated rather than glossed](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#286---not-reachable-on-the-bench-stated-rather-than-glossed)

<a id="part-29--ui-15-slice-5-oled-team-create-on-real-hardware-2026-08-20"></a>

[Part 29 — §UI-15 slice 5: OLED team create on REAL hardware (2026-08-20)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-29--ui-15-slice-5-oled-team-create-on-real-hardware-2026-08-20)

<a id="part-30--ui-15-slice-6-oled-static-join-on-real-hardware-2026-08-20"></a>

[Part 30 — §UI-15 slice 6: OLED static join on REAL hardware (2026-08-20)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-30--ui-15-slice-6-oled-static-join-on-real-hardware-2026-08-20)

<a id="300--equipment-build-and-evidence"></a>

[30.0 — equipment, build and evidence](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#300--equipment-build-and-evidence)

<a id="301--the-unsavedconflict-gate-remains-ahead-of-provisioning"></a>

[30.1 — the unsaved/conflict gate remains ahead of provisioning](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#301--the-unsavedconflict-gate-remains-ahead-of-provisioning)

<a id="302--sparse-list-complete-confirmation-and-safe-back"></a>

[30.2 — sparse list, complete confirmation and safe BACK](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#302--sparse-list-complete-confirmation-and-safe-back)

<a id="303--real-join-blankwake-and-correlated-result"></a>

[30.3 — real join, blank/wake and correlated result](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#303--real-join-blankwake-and-correlated-result)

<a id="304--leaving-the-waiting-screen-does-not-cancel-or-hijack-the-ui-later"></a>

[30.4 — leaving the waiting screen does not cancel or hijack the UI later](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#304--leaving-the-waiting-screen-does-not-cancel-or-hijack-the-ui-later)

<a id="305--durability-and-actual-network-service"></a>

[30.5 — durability and actual network service](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#305--durability-and-actual-network-service)

<a id="306--conditional-observations-not-forced-failures"></a>

[30.6 — conditional observations, not forced failures](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#306--conditional-observations-not-forced-failures)

<a id="307--stop-rules-and-retained-evidence"></a>

[30.7 — stop rules and retained evidence](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#307--stop-rules-and-retained-evidence)

<a id="part-31--b231b233-inbox-order--delete-refresh-on-glass-2026-08-20"></a>

[Part 31 — ((B231))/((B233)): inbox order + delete-refresh ON GLASS (2026-08-20)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-31--b231b233-inbox-order--delete-refresh-on-glass-2026-08-20)

<a id="part-32--ui-17-s4-a-lit-team-screens-ages-turn-on-their-own-2026-08-21"></a>

[Part 32 — §UI-17 S4: a lit TEAM screen's ages turn on their own (2026-08-21)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-32--ui-17-s4-a-lit-team-screens-ages-turn-on-their-own-2026-08-21)

<a id="part-33--ui-17-s5-distancedirection-on-glass-2026-08-22"></a>

[Part 33 — §UI-17 S5: distance/direction on glass (2026-08-22)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-33--ui-17-s5-distancedirection-on-glass-2026-08-22)

<a id="part-34--ui-17-s8-wake-on-receive-on-glass-2026-08-22"></a>

[Part 34 — §UI-17 S8: wake-on-receive on glass (2026-08-22)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-34--ui-17-s8-wake-on-receive-on-glass-2026-08-22)

<a id="part-35--ui-16-k1k2-the-mrteams-keyring-on-real-flash-2026-08-22"></a>

[Part 35 — §UI-16 K1/K2: the `/mrteams` keyring on real flash (2026-08-22)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-35--ui-16-k1k2-the-mrteams-keyring-on-real-flash-2026-08-22)

<a id="part-36--ui-16-n2-the-nearby-scan-on-glass-2026-08-23"></a>

[Part 36 — §UI-16 N2: the NEARBY scan on glass (2026-08-23)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-36--ui-16-n2-the-nearby-scan-on-glass-2026-08-23)

<a id="part-37--ui-16-n3-the-confirmed-nearby-join-on-glass-2026-08-23"></a>

[Part 37 — §UI-16 N3: the confirmed nearby join on glass (2026-08-23)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-37--ui-16-n3-the-confirmed-nearby-join-on-glass-2026-08-23)

<a id="part-38--ui-16-n4-the-invitation-window-on-glass-2026-08-23"></a>

[Part 38 — §UI-16 N4: the invitation window on glass (2026-08-23)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-38--ui-16-n4-the-invitation-window-on-glass-2026-08-23)

<a id="part-39--chrome-5-the-duty-gauge-on-glass-2026-08-23"></a>

[Part 39 — §CHROME-5: the duty gauge on glass (2026-08-23)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-39--chrome-5-the-duty-gauge-on-glass-2026-08-23)

<a id="part-40--ui-16-n5-the-pubkey-request-on-glass-2026-08-24"></a>

[Part 40 — §UI-16 N5: the pubkey request on glass (2026-08-24)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-40--ui-16-n5-the-pubkey-request-on-glass-2026-08-24)

<a id="part-41--ui-16-n6b-the-grants-dispatch-truth-on-glass-2026-08-24"></a>

[Part 41 — §UI-16 N6b: the grant's dispatch truth on glass (2026-08-24)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-41--ui-16-n6b-the-grants-dispatch-truth-on-glass-2026-08-24)

<a id="part-42--ui-16-k3k4--b243-the-grant-receipt-on-glass-all-three-verdicts-2026-08-25"></a>

[Part 42 — §UI-16 K3/K4 + ((B243)): the grant receipt on glass, all three verdicts (2026-08-25)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-42--ui-16-k3k4--b243-the-grant-receipt-on-glass-all-three-verdicts-2026-08-25)

<a id="part-43--ui-16-k5-the-saved-key-on-glass-2026-08-25"></a>

[Part 43 — §UI-16 K5: the saved key on glass (2026-08-25)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-43--ui-16-k5-the-saved-key-on-glass-2026-08-25)

<a id="part-44--ui-16-k6-forget-key-on-real-flash-2026-08-25"></a>

[Part 44 — §UI-16 K6: FORGET KEY on real flash (2026-08-25)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-44--ui-16-k6-forget-key-on-real-flash-2026-08-25)

<a id="part-45--ui-16-k7-b245-the-roster-grant-on-glass-2026-08-25"></a>

[Part 45 — §UI-16 K7 (((B245))): the roster grant on glass (2026-08-25)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-45--ui-16-k7-b245-the-roster-grant-on-glass-2026-08-25)

<a id="part-46--ui-1011-p2-the-ui-preset-verbs-and-the-mrui-boot-lines-on-real-hardware-2026-08-25"></a>

[Part 46 — §UI-10/11 P2: the `ui preset` verbs and the `/mrui` boot lines on real hardware (2026-08-25)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-46--ui-1011-p2-the-ui-preset-verbs-and-the-mrui-boot-lines-on-real-hardware-2026-08-25)

<a id="part-47--heltec-v4-3-first-v42v43-rf-and-native-usb-bring-up-2026-08-26"></a>

[Part 47 — Heltec V4-3: first V4.2/V4.3 RF and native-USB bring-up (2026-08-26)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-47--heltec-v4-3-first-v42v43-rf-and-native-usb-bring-up-2026-08-26)

<a id="part-48--b251-equal-counter-hosted-mobiles-on-real-radios-2026-08-27"></a>

[Part 48 — ((B251)): equal-counter hosted mobiles on real radios (2026-08-27)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-48--b251-equal-counter-hosted-mobiles-on-real-radios-2026-08-27)

<a id="part-49--custody-a-the-one-time-v5-inbox-migration-on-first-boot-after-the-fleet-reflash-2026-08-29"></a>

[Part 49 — §CUSTODY-A: the one-time v5 inbox migration on first boot after the fleet reflash (2026-08-29)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-49--custody-a-the-one-time-v5-inbox-migration-on-first-boot-after-the-fleet-reflash-2026-08-29)

<a id="part-50--custody-b--b268-the-grants-outcome-edges-on-the-real-radio-2026-08-30"></a>

[Part 50 — §CUSTODY-B / ((B268)): the grant's outcome edges on the real radio (2026-08-30)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-50--custody-b--b268-the-grants-outcome-edges-on-the-real-radio-2026-08-30)

<a id="part-51--custody-c-an-ack-holding-inbox-on-real-glass-2026-08-30"></a>

[Part 51 — §CUSTODY-C: an ack-holding inbox on real glass (2026-08-30)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-51--custody-c-an-ack-holding-inbox-on-real-glass-2026-08-30)

<a id="part-52--custody-d-confirmed-inbox-clear-survives-power-cycles-2026-08-31"></a>

[Part 52 — §CUSTODY-D: confirmed inbox clear survives power cycles (2026-08-31)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-52--custody-d-confirmed-inbox-clear-survives-power-cycles-2026-08-31)

<a id="part-53--custody-g--b59-a-custody-report-reaching-the-sender-on-real-glass-and-usb-2026-08-31"></a>

[Part 53 — §CUSTODY-G / ((B59)): a custody report reaching the sender on real glass and USB (2026-08-31)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-53--custody-g--b59-a-custody-report-reaching-the-sender-on-real-glass-and-usb-2026-08-31)

<a id="part-54--b278-a-home-translated-custody-report-reaching-the-mobile-that-owns-the-flight-2026-09-03"></a>

[Part 54 — §B278: a HOME-TRANSLATED custody report reaching the MOBILE that owns the flight (2026-09-03)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-54--b278-a-home-translated-custody-report-reaching-the-mobile-that-owns-the-flight-2026-09-03)

<a id="part-58--console-help-is-a-bare-primary-verb-index-2026-09-05"></a>

[Part 58 — console help is a bare primary-verb index (2026-09-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-58--console-help-is-a-bare-primary-verb-index-2026-09-05)

<a id="part-59--regen-answers-on-the-transport-that-asked-2026-09-05"></a>

[Part 59 — `regen` answers on the transport that asked (2026-09-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-59--regen-answers-on-the-transport-that-asked-2026-09-05)

<a id="part-61--ble-inbound-line-capacity-on-an-nrf52-gateway-2026-09-04"></a>

[Part 61 — BLE inbound line capacity on an nRF52 gateway (2026-09-04)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-61--ble-inbound-line-capacity-on-an-nrf52-gateway-2026-09-04)

<a id="part-62--the-application-dm-232-byte-cap-on-metal-2026-09-05"></a>

[Part 62 — the application-DM 232-byte cap on metal (2026-09-05)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-62--the-application-dm-232-byte-cap-on-metal-2026-09-05)

<a id="part-55a--remote-admin-v2-target-stores-physical-usb-and-real-flash-slice-3"></a>

[Part 55a — remote-admin v2 target stores: physical USB and real flash (Slice 3)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-55a--remote-admin-v2-target-stores-physical-usb-and-real-flash-slice-3)

<a id="part-55b--remote-admin-v2-controller-half-of-the-physical-usb-exchange-slice-4"></a>

[Part 55b — remote-admin v2 controller half of the physical USB exchange (Slice 4)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-55b--remote-admin-v2-controller-half-of-the-physical-usb-exchange-slice-4)

<a id="part-56--controller-secret-store-lifecycle-and-secured-ble-public-boundary-slice-4"></a>

[Part 56 — controller secret-store lifecycle and secured-BLE public boundary (Slice 4)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-56--controller-secret-store-lifecycle-and-secured-ble-public-boundary-slice-4)

<a id="part-63--the-shared-command-line-validator-at-the-ble-head-slice-6"></a>

[Part 63 — the shared command-line validator at the BLE head (Slice 6)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-63--the-shared-command-line-validator-at-the-ble-head-slice-6)

<a id="part-57a--the-persisted-remote-action-activation-delay-slice-7a"></a>

[Part 57a — the persisted remote-action activation delay (Slice 7a)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57a--the-persisted-remote-action-activation-delay-slice-7a)

<a id="part-57b--deferred-actions--prep-restart-lockout-7b-3-reservation"></a>

[Part 57b — deferred actions / prep-restart lockout (7b-3 reservation)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57b--deferred-actions--prep-restart-lockout-7b-3-reservation)

<a id="part-57d--controller-local-delivery-ndjson-re-offerack-and-the-usb-result-or-refusal-line-8a8c-reservation"></a>

[Part 57d — controller local delivery: NDJSON re-offer/ACK and the USB result or refusal line (8a+8c reservation)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57d--controller-local-delivery-ndjson-re-offerack-and-the-usb-result-or-refusal-line-8a8c-reservation)

<a id="part-57c--the-mobile-controller-carrier-real-requestresult-round-trip--a-and-custody-8b"></a>

[Part 57c — the mobile controller carrier: real request/result round trip, `-a` and custody (8b)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57c--the-mobile-controller-carrier-real-requestresult-round-trip--a-and-custody-8b)

<a id="part-57e--legacy-remote-admin-deletion-slice-9"></a>

[Part 57e — legacy remote-admin deletion (Slice 9)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57e--legacy-remote-admin-deletion-slice-9)

<a id="part-57f--main-nv-v26-reprovision-with-administration-stores-retained-slice-10"></a>

[Part 57f — main-NV v26 reprovision with administration stores retained (Slice 10)](archive/2026-09-20-bench-records/2026-07-31-bench-test-script.md#part-57f--main-nv-v26-reprovision-with-administration-stores-retained-slice-10)

</details>
