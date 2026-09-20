# Archived bench record — ble-bench-validation

The maintained hardware procedures and current results are in the [metal test plan](2026-09-20-metal-test-plan.md).
This document was consolidated on 2026-09-20; its original text and historical results are preserved in the [archive](archive/2026-09-20-bench-records/ble-bench-validation.md).
See the [complete disposition map](2026-09-20-metal-test-triage.md) for retirement instruments and replacement scenarios.
Historical instructions and PASS marks in the archive do not qualify a new image.

<details>
<summary>Historical section links (compatibility anchors)</summary>

<a id="ble-companion--on-metal-bench-validation"></a>

[BLE companion — on-metal bench validation](archive/2026-09-20-bench-records/ble-bench-validation.md#ble-companion--on-metal-bench-validation)

<a id="0-prerequisite--is-the-s140-softdevice-actually-flashed--hard-gate"></a>

[0. Prerequisite — is the S140 SoftDevice actually flashed?  (HARD GATE)](archive/2026-09-20-bench-records/ble-bench-validation.md#0-prerequisite--is-the-s140-softdevice-actually-flashed--hard-gate)

<a id="1-flash--provision-a-node"></a>

[1. Flash + provision a node](archive/2026-09-20-bench-records/ble-bench-validation.md#1-flash--provision-a-node)

<a id="2-enable-ble"></a>

[2. Enable BLE](archive/2026-09-20-bench-records/ble-bench-validation.md#2-enable-ble)

<a id="3-verify-advertising"></a>

[3. Verify advertising](archive/2026-09-20-bench-records/ble-bench-validation.md#3-verify-advertising)

<a id="4-connect--pair--validates-step-6--a3"></a>

[4. Connect + pair  (validates Step 6 / §A.3)](archive/2026-09-20-bench-records/ble-bench-validation.md#4-connect--pair--validates-step-6--a3)

<a id="5-round-trip-a-command--a-delivery-over-ble--validates-step-5"></a>

[5. Round-trip a command + a delivery over BLE  (validates Step 5)](archive/2026-09-20-bench-records/ble-bench-validation.md#5-round-trip-a-command--a-delivery-over-ble--validates-step-5)

<a id="6-bond-persists-across-reconnect"></a>

[6. Bond persists across reconnect](archive/2026-09-20-bench-records/ble-bench-validation.md#6-bond-persists-across-reconnect)

<a id="7--keystone--regen-under-a-live-ble-link--the-sd-rng-guard-hardware-only-validation"></a>

[7. ⚠ KEYSTONE — `regen` under a live BLE link  (the SD-RNG guard; HARDWARE-ONLY validation)](archive/2026-09-20-bench-records/ble-bench-validation.md#7--keystone--regen-under-a-live-ble-link--the-sd-rng-guard-hardware-only-validation)

<a id="8--step-9--does-ble-jitter-the-lora-timing--the-keystone-risk-spec-3"></a>

[8. ⚠ Step 9 — does BLE jitter the LoRa timing?  (the keystone RISK, spec §3)](archive/2026-09-20-bench-records/ble-bench-validation.md#8--step-9--does-ble-jitter-the-lora-timing--the-keystone-risk-spec-3)

<a id="what-to-report-back"></a>

[What to report back](archive/2026-09-20-bench-records/ble-bench-validation.md#what-to-report-back)

</details>
