# Archived bench record — 2026-08-04-heltec-v3-oled-ui-bench-guide

The maintained hardware procedures and current results are in the [metal test plan](2026-09-20-metal-test-plan.md).
This document was consolidated on 2026-09-20; its original text and historical results are preserved in the [archive](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md).
See the [complete disposition map](2026-09-20-metal-test-triage.md) for retirement instruments and replacement scenarios.
Historical instructions and PASS marks in the archive do not qualify a new image.

<details>
<summary>Historical section links (compatibility anchors)</summary>

<a id="heltec-v3-oled-ui--compile-and-staged-bench-guide"></a>

[Heltec V3 OLED UI — Compile and Staged Bench Guide](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#heltec-v3-oled-ui--compile-and-staged-bench-guide)

<a id="0-current-metal-run--start-here"></a>

[0. Current metal run — start here](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#0-current-metal-run--start-here)

<a id="01-what-is-ready-and-what-is-deliberately-not-required-first"></a>

[0.1 What is ready, and what is deliberately not required first](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#01-what-is-ready-and-what-is-deliberately-not-required-first)

<a id="02-minimum-useful-rig"></a>

[0.2 Minimum useful rig](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#02-minimum-useful-rig)

<a id="03-preflight--do-once-per-flashed-build"></a>

[0.3 Preflight — do once per flashed build](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#03-preflight--do-once-per-flashed-build)

<a id="04-ordered-run--stop-at-the-first-unexplained-failure"></a>

[0.4 Ordered run — stop at the first unexplained failure](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#04-ordered-run--stop-at-the-first-unexplained-failure)

<a id="05-immediate-stopreport-conditions-for-this-run"></a>

[0.5 Immediate stop/report conditions for this run](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#05-immediate-stopreport-conditions-for-this-run)

<a id="1-when-to-run-each-group"></a>

[1. When to run each group](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#1-when-to-run-each-group)

<a id="2-equipment-and-topology"></a>

[2. Equipment and topology](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#2-equipment-and-topology)

<a id="minimum-for-h5"></a>

[Minimum for H5](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#minimum-for-h5)

<a id="add-for-h6"></a>

[Add for H6](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#add-for-h6)

<a id="add-for-h7-and-h8"></a>

[Add for H7 and H8](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#add-for-h7-and-h8)

<a id="add-for-h9"></a>

[Add for H9](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#add-for-h9)

<a id="3-important-gpio0-precaution"></a>

[3. Important GPIO0 precaution](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#3-important-gpio0-precaution)

<a id="4-compile-flash-and-monitor"></a>

[4. Compile, flash, and monitor](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#4-compile-flash-and-monitor)

<a id="41-choose-the-environment"></a>

[4.1 Choose the environment](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#41-choose-the-environment)

<a id="42-run-the-current-host-side-probes"></a>

[4.2 Run the current host-side probes](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#42-run-the-current-host-side-probes)

<a id="43-make-a-clean-board-build"></a>

[4.3 Make a clean board build](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#43-make-a-clean-board-build)

<a id="44-find-the-serial-port"></a>

[4.4 Find the serial port](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#44-find-the-serial-port)

<a id="45-upload"></a>

[4.5 Upload](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#45-upload)

<a id="46-open-the-console"></a>

[4.6 Open the console](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#46-open-the-console)

<a id="5-h5--tests-available-now"></a>

[5. H5 — tests available now](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#5-h5--tests-available-now)

<a id="h5-01--preflight-and-boot-identity"></a>

[H5-01 — Preflight and boot identity](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-01--preflight-and-boot-identity)

<a id="h5-02--static-task-5-frame"></a>

[H5-02 — Static Task 5 frame](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-02--static-task-5-frame)

<a id="h5-03--static-frame-stability"></a>

[H5-03 — Static-frame stability](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-03--static-frame-stability)

<a id="h5-04--vext-polarity-diagnosis-only-if-the-panel-is-dark"></a>

[H5-04 — Vext polarity diagnosis, only if the panel is dark](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-04--vext-polarity-diagnosis-only-if-the-panel-is-dark)

<a id="h5-05--reset-pin-diagnosis-only-if-h5-04-does-not-recover-the-panel"></a>

[H5-05 — Reset-pin diagnosis, only if H5-04 does not recover the panel](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-05--reset-pin-diagnosis-only-if-h5-04-does-not-recover-the-panel)

<a id="h5-06--basic-radio-sanity-with-oled-initialised"></a>

[H5-06 — Basic radio sanity with OLED initialised](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-06--basic-radio-sanity-with-oled-initialised)

<a id="h5-07--record-the-boot-strap-behavior"></a>

[H5-07 — Record the boot-strap behavior](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h5-07--record-the-boot-strap-behavior)

<a id="6-h6--run-after-task-6-lands"></a>

[6. H6 — run after Task 6 lands](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#6-h6--run-after-task-6-lands)

<a id="h6-00--panel-ack-report-b91--do-this-first-it-is-the-new-diagnostic"></a>

[H6-00 — Panel-ACK report (§B91) — do this first, it is the new diagnostic](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-00--panel-ack-report-b91--do-this-first-it-is-the-new-diagnostic)

<a id="h6-01--initial-status-screen"></a>

[H6-01 — Initial status screen](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-01--initial-status-screen)

<a id="h6-02--short-press-screen-navigation"></a>

[H6-02 — Short-press screen navigation](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-02--short-press-screen-navigation)

<a id="h6-03--blank-and-wake"></a>

[H6-03 — Blank and wake](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-03--blank-and-wake)

<a id="h6-04--rendering-stability-under-radio-load"></a>

[H6-04 — Rendering stability under radio load](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-04--rendering-stability-under-radio-load)

<a id="h6-05--dynamic-model-updates"></a>

[H6-05 — Dynamic model updates](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-05--dynamic-model-updates)

<a id="h6-06--retired-2026-08-05-by-ui-7-the-send-path-is-built-see-h7-kept-for-the-audit-trail-only"></a>

[H6-06 — RETIRED 2026-08-05 by UI-7 (the send path is BUILT; see H7). Kept for the audit trail only](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-06--retired-2026-08-05-by-ui-7-the-send-path-is-built-see-h7-kept-for-the-audit-trail-only)

<a id="h6-07--b71-the-emergency-screens-exit"></a>

[H6-07 — §B71: the emergency screen's exit](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-07--b71-the-emergency-screens-exit)

<a id="h6-08--battery-cadence-with-an-unavailable-reader"></a>

[H6-08 — Battery cadence with an unavailable reader](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-08--battery-cadence-with-an-unavailable-reader)

<a id="h6-09--b103-a-distress-reply-must-be-team-scoped--this-was-a-live-safety-defect-on-this-bench"></a>

[H6-09 — §B103: a distress REPLY must be TEAM-scoped ★★ THIS WAS A LIVE SAFETY DEFECT ON THIS BENCH](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-09--b103-a-distress-reply-must-be-team-scoped--this-was-a-live-safety-defect-on-this-bench)

<a id="h6-10--b107b108-nothing-is-lost-while-a-frame-is-painting"></a>

[H6-10 — §B107/§B108: nothing is lost while a frame is painting](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-10--b107b108-nothing-is-lost-while-a-frame-is-painting)

<a id="h6-11--r1b109-a-distress-reply-must-light-a-dark-panel-and-a-strangers-post-must-not"></a>

[H6-11 — §R1/B109: a distress REPLY must LIGHT A DARK PANEL, and a stranger's post must not](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-11--r1b109-a-distress-reply-must-light-a-dark-panel-and-a-strangers-post-must-not)

<a id="h6-12--r2b110-a-double-under-the-emergency-overlay-must-do-nothing-at-all"></a>

[H6-12 — §R2/B110: a DOUBLE under the emergency overlay must do NOTHING AT ALL](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h6-12--r2b110-a-double-under-the-emergency-overlay-must-do-nothing-at-all)

<a id="7-h7--run-after-task-7-lands"></a>

[7. H7 — run after Task 7 lands](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#7-h7--run-after-task-7-lands)

<a id="h7-01--canned-channel-send-the-happy-path"></a>

[H7-01 — Canned channel send (the happy path)](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-01--canned-channel-send-the-happy-path)

<a id="h7-02--cancel-a-canned-send"></a>

[H7-02 — Cancel a canned send](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-02--cancel-a-canned-send)

<a id="h7-03--dm-compose-and-send"></a>

[H7-03 — DM compose and send](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-03--dm-compose-and-send)

<a id="h7-04--cancel-dm-compose"></a>

[H7-04 — Cancel DM compose](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-04--cancel-dm-compose)

<a id="h7-05--inbox-presentation"></a>

[H7-05 — Inbox presentation](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-05--inbox-presentation)

<a id="h7-06--send-tracker-closure"></a>

[H7-06 — Send tracker closure](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-06--send-tracker-closure)

<a id="h7-07--b69-an-unconfirmed-send-must-never-read-as-sent--new-and-it-is-the-slices-safety-point"></a>

[H7-07 — §B69: an unconfirmed send must never read as SENT ★★ NEW, AND IT IS THE SLICE'S SAFETY POINT](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-07--b69-an-unconfirmed-send-must-never-read-as-sent--new-and-it-is-the-slices-safety-point)

<a id="h7-08--the-sub-views-lifetime-bounds-the-outcome-and-that-is-deliberate"></a>

[H7-08 — the sub-view's lifetime bounds the outcome, and that is DELIBERATE](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-08--the-sub-views-lifetime-bounds-the-outcome-and-that-is-deliberate)

<a id="h7-09--b64-a-teammate-that-leaves-the-roster-must-never-inherit-your-dm--new-owner-ruled-2026-08-05"></a>

[H7-09 — §B64: a teammate that LEAVES the roster must never inherit your DM ★★ NEW, OWNER-RULED 2026-08-05](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h7-09--b64-a-teammate-that-leaves-the-roster-must-never-inherit-your-dm--new-owner-ruled-2026-08-05)

<a id="8-h8--run-after-task-8-lands"></a>

[8. H8 — run after Task 8 lands](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#8-h8--run-after-task-8-lands)

<a id="h8-01--long-press-threshold-and-cancellation"></a>

[H8-01 — Long-press threshold and cancellation](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-01--long-press-threshold-and-cancellation)

<a id="h8-02--recipient-unavailable"></a>

[H8-02 — Recipient unavailable](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-02--recipient-unavailable)

<a id="h8-03--pickup-and-reply"></a>

[H8-03 — Pickup and reply](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-03--pickup-and-reply)

<a id="h8-04--temporary-blockbackpressure"></a>

[H8-04 — Temporary block/backpressure](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-04--temporary-blockbackpressure)

<a id="h8-05--outcome-isolation"></a>

[H8-05 — Outcome isolation](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-05--outcome-isolation)

<a id="h8-06--emergency-priority"></a>

[H8-06 — Emergency priority](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-06--emergency-priority)

<a id="h8-07--conditional-emergency-location"></a>

[H8-07 — Conditional emergency location](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-07--conditional-emergency-location)

<a id="h8-08--emergency-display-timing-and-frame-integrity"></a>

[H8-08 — Emergency display timing and frame integrity](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-08--emergency-display-timing-and-frame-integrity)

<a id="h8-09--unavailable-battery-reader-cadence---superseded-by-9-task-9-has-landed"></a>

[H8-09 — Unavailable battery-reader cadence — ⛔ SUPERSEDED BY §9 (Task 9 has landed)](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-09--unavailable-battery-reader-cadence---superseded-by-9-task-9-has-landed)

<a id="h8-10---the-alarms-attempt-counter-read-the-first-number-owner-cases-1-and-2"></a>

[H8-10 — ★★ The alarm's attempt counter: READ THE FIRST NUMBER (owner cases 1 and 2)](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h8-10---the-alarms-attempt-counter-read-the-first-number-owner-cases-1-and-2)

<a id="9-h9--run-after-task-9-lands---task-9-has-landed-so-this-section-is-now-the-acceptance-residue"></a>

[9. H9 — run after Task 9 lands · ★★ TASK 9 HAS LANDED, SO THIS SECTION IS NOW THE ACCEPTANCE RESIDUE](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#9-h9--run-after-task-9-lands---task-9-has-landed-so-this-section-is-now-the-acceptance-residue)

<a id="h9-01--nothing-measured-yet-must-read-as----never-as-a-number"></a>

[H9-01 — Nothing measured yet must read as `--`, never as a number](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h9-01--nothing-measured-yet-must-read-as----never-as-a-number)

<a id="h9-02--meter-comparison--the-check-that-validates-kvbatadcscale-and-the-only-one-that-can"></a>

[H9-02 — Meter comparison ★ THE CHECK THAT VALIDATES `kVbatAdcScale`, AND THE ONLY ONE THAT CAN](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h9-02--meter-comparison--the-check-that-validates-kvbatadcscale-and-the-only-one-that-can)

<a id="h9-03--sampling-cadence-and-mac-safety-on-the-real-timing"></a>

[H9-03 — Sampling cadence and MAC safety, on the real timing](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h9-03--sampling-cadence-and-mac-safety-on-the-real-timing)

<a id="h9-04--usb-and-battery-transitions"></a>

[H9-04 — USB and battery transitions](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h9-04--usb-and-battery-transitions)

<a id="h9-05---the-adc-control-line-is-its-idle-level-real-and-is-the-divider-parked-off-new-task-9"></a>

[H9-05 — ★★ THE ADC CONTROL LINE: IS ITS IDLE LEVEL REAL, AND IS THE DIVIDER PARKED OFF? (★NEW, Task 9)](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#h9-05---the-adc-control-line-is-its-idle-level-real-and-is-the-divider-parked-off-new-task-9)

<a id="10-evidence-template"></a>

[10. Evidence template](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#10-evidence-template)

<a id="11-stop-and-report-conditions"></a>

[11. Stop and report conditions](archive/2026-09-20-bench-records/2026-08-04-heltec-v3-oled-ui-bench-guide.md#11-stop-and-report-conditions)

</details>
