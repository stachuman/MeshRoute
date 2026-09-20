# Archived bench record — 2026-08-11-mobile-home-metal-test-guide

The maintained hardware procedures and current results are in the [metal test plan](2026-09-20-metal-test-plan.md).
This document was consolidated on 2026-09-20; its original text and historical results are preserved in the [archive](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md).
See the [complete disposition map](2026-09-20-metal-test-triage.md) for retirement instruments and replacement scenarios.
Historical instructions and PASS marks in the archive do not qualify a new image.

<details>
<summary>Historical section links (compatibility anchors)</summary>

<a id="mobile-home-attachment--real-equipment-test-guide"></a>

[Mobile-home attachment — real-equipment test guide](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mobile-home-attachment--real-equipment-test-guide)

<a id="1-when-to-run-which-tests"></a>

[1. When to run which tests](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#1-when-to-run-which-tests)

<a id="2-equipment-and-topology"></a>

[2. Equipment and topology](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#2-equipment-and-topology)

<a id="3-build-flash-and-connect"></a>

[3. Build, flash and connect](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#3-build-flash-and-connect)

<a id="31-record-the-source-state"></a>

[3.1 Record the source state](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#31-record-the-source-state)

<a id="32-build-the-environment-matching-each-role"></a>

[3.2 Build the environment matching each role](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#32-build-the-environment-matching-each-role)

<a id="33-upload-examples"></a>

[3.3 Upload examples](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#33-upload-examples)

<a id="34-open-consoles"></a>

[3.4 Open consoles](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#34-open-consoles)

<a id="4-test-record"></a>

[4. Test record](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#4-test-record)

<a id="5-common-provisioning"></a>

[5. Common provisioning](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#5-common-provisioning)

<a id="6-stage-a--run-after-b177-passes-code-qg"></a>

[6. Stage A — run after B177 passes code QG](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#6-stage-a--run-after-b177-passes-code-qg)

<a id="mh-00--build-and-topology-sanity"></a>

[MH-00 — build and topology sanity](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-00--build-and-topology-sanity)

<a id="mh-01--manual-first-attachment-and-confirmation"></a>

[MH-01 — manual first attachment and confirmation](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-01--manual-first-attachment-and-confirmation)

<a id="mh-02--the-original-real-world-order-mobile-boots-before-any-home"></a>

[MH-02 — the original real-world order: mobile boots before any home](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-02--the-original-real-world-order-mobile-boots-before-any-home)

<a id="mh-03--several-mobiles-start-together"></a>

[MH-03 — several mobiles start together](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-03--several-mobiles-start-together)

<a id="mh-04--explicit-unregister-is-local-and-remains-dormant"></a>

[MH-04 — explicit unregister is local and remains dormant](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-04--explicit-unregister-is-local-and-remains-dormant)

<a id="mh-05--healthy-home-checks-do-not-create-a-search-storm"></a>

[MH-05 — healthy-home checks do not create a search storm](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-05--healthy-home-checks-do-not-create-a-search-storm)

<a id="mh-06--missed-home-checks-cause-loss-detection-and-recovery"></a>

[MH-06 — missed home checks cause loss detection and recovery](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-06--missed-home-checks-cause-loss-detection-and-recovery)

<a id="mh-07--weak-but-answering-home-remains-selected-before-b178"></a>

[MH-07 — weak but answering home remains selected before B178](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-07--weak-but-answering-home-remains-selected-before-b178)

<a id="mh-08--host-row-lifetime-and-b177-regression"></a>

[MH-08 — host-row lifetime and B177 regression](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-08--host-row-lifetime-and-b177-regression)

<a id="7-stage-b--only-after-b178s-refined-proactive-trigger-lands"></a>

[7. Stage B — only after B178's refined proactive trigger lands](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#7-stage-b--only-after-b178s-refined-proactive-trigger-lands)

<a id="mh-09--weak-home-plus-a-genuinely-eligible-better-candidate"></a>

[MH-09 — weak home plus a genuinely eligible better candidate](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-09--weak-home-plus-a-genuinely-eligible-better-candidate)

<a id="8-stage-c--release-candidate-endurance"></a>

[8. Stage C — release-candidate endurance](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#8-stage-c--release-candidate-endurance)

<a id="mh-10--default-duration-row-expiry"></a>

[MH-10 — default-duration row expiry](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#mh-10--default-duration-row-expiry)

<a id="9-completion-summary"></a>

[9. Completion summary](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#9-completion-summary)

<a id="10-failure-report-minimum"></a>

[10. Failure-report minimum](archive/2026-09-20-bench-records/2026-08-11-mobile-home-metal-test-guide.md#10-failure-report-minimum)

</details>
