# Archived bench record — 2026-08-20-metal-session-walkthrough

The maintained hardware procedures and current results are in the [metal test plan](../../2026-09-20-metal-test-plan.md).
This document was consolidated on 2026-09-20; its original text and historical results are preserved in the [archive](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md).
See the [complete disposition map](../../2026-09-20-metal-test-triage.md) for retirement instruments and replacement scenarios.
Historical instructions and PASS marks in the archive do not qualify a new image.

<details>
<summary>Historical section links (compatibility anchors)</summary>

<a id="metal-session-walkthrough--follow-top-to-bottom--2026-08-20"></a>

[Metal session WALKTHROUGH — follow top to bottom · 2026-08-20](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#metal-session-walkthrough--follow-top-to-bottom--2026-08-20)

<a id="phase-0--build-flash-archive"></a>

[PHASE 0 — build, flash, archive](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-0--build-flash-archive)

<a id="phase-1--the-one-shot-fresh-chip-check-on-x-first-p-284---confirmed-ok"></a>

[PHASE 1 — the ONE-SHOT fresh-chip check, on X, FIRST (P 28.4) - confirmed OK](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-1--the-one-shot-fresh-chip-check-on-x-first-p-284---confirmed-ok)

<a id="phase-2--baseline-all-three-nodes---confirmed-ok"></a>

[PHASE 2 — baseline (all three nodes) - confirmed OK](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-2--baseline-all-three-nodes---confirmed-ok)

<a id="phase-3--the-mrjoin-store-over-real-flash-on-h1-p-281-285"></a>

[PHASE 3 — the `/mrjoin` store over real flash (on H1) (P 28.1-28.5)](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-3--the-mrjoin-store-over-real-flash-on-h1-p-281-285)

<a id="phase-4--team-up-then-everything-that-needs-the-team"></a>

[PHASE 4 — team up, then everything that NEEDS the team](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-4--team-up-then-everything-that-needs-the-team)

<a id="4a--the-status-strip-on-glass-p-241-242"></a>

[4a — the status strip on glass (P 24.1-24.2)](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#4a--the-status-strip-on-glass-p-241-242)

<a id="4b--the-rail-badge-and-19-column-body-p-251-255---confirmed-ok"></a>

[4b — the rail, badge and 19-column body (P 25.1-25.5) - confirmed OK](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#4b--the-rail-badge-and-19-column-body-p-251-255---confirmed-ok)

<a id="4c--physical-tx-completion-p-221-222-all-three-nodes---confirmed-ok"></a>

[4c — physical TX-completion (P 22.1-22.2, all three nodes) - confirmed OK](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#4c--physical-tx-completion-p-221-222-all-three-nodes---confirmed-ok)

<a id="phase-5--oled-team-create-on-h1-p-29"></a>

[PHASE 5 — OLED team create (on H1) (P 29)](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-5--oled-team-create-on-h1-p-29)

<a id="phase-6--oled-static-join-p-30--h2--joiner-x--static-peer"></a>

[PHASE 6 — OLED static join (P 30) — H2 = joiner, X = static peer](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-6--oled-static-join-p-30--h2--joiner-x--static-peer)

<a id="phase-7---the-mrjoin-power-cut--last-the-only-destructive-step-slice-7-gate"></a>

[PHASE 7 — ⚠⚠ the `/mrjoin` POWER-CUT — LAST, the only destructive step (slice-7 gate)](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-7---the-mrjoin-power-cut--last-the-only-destructive-step-slice-7-gate)

<a id="phase-8--separate--optional-record-not-run-with-reasons-if-skipped"></a>

[PHASE 8 — separate / optional (record not-run with reasons if skipped)](../../archive/2026-09-20-bench-records/2026-08-20-metal-session-walkthrough.md#phase-8--separate--optional-record-not-run-with-reasons-if-skipped)

</details>
