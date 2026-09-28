# Independent W4b source and instrument review

Reviewed the semantic production diff against owner commit c8e36d8, the revision-3 contract and the coder's
closed disposition ledger. Normalized diffs are navigation aids only; their line numbers are not source anchors.

- Publication/render: `firmware_ui.cpp:912` publishes the counted own name through the real Node accessor.
  `draw_home_screen:1409` uses the frozen snapshot; My device sanitizes and splits raw counted bytes, and Home
  uses the 16-cell identity formatter. `ui_home_invalidate:2624` is called before FrameGate chooses a frame.
  Chrome keeps the W41 rail-box statement and adds the x10..11 cue separately (`:1349`).
- Model: the one focus field, five profile lists and capability gates, identity-based Home capture, note consumption
  (including same-tick raise), top-level MENU/wrap, interim Send catalog latch, typed setup origin and explicit exits
  match the contract. Home admission delegates to W3's opener/gate. INVITE remains ungated. OQ-3 blank cancellations,
  ordinary emergency focus and Home identity, and the Inbox frame watermark remain in their established boundaries.
- B457: `sync_settings:4452`, normalization `:4470`, closes browsing/editing over a closed service. Provisioning is
  excluded so an otherwise ungated invite or roster grant can run. The native cases cover both entry paths, failed
  service, rail escape, recovery and open-service counterparts. `draw_settings_screen:2171` still draws the existing
  unavailable text (`:2185`), now with the correct focus/cue projection.
- Stage A: its 236 control labels/scripts equal HEAD; its accounting implementation is byte-identical to final.
  All 19 current accounting tests execute the actual runner, including missing/duplicate/unknown labels, failed and
  undefined guards, must-build semantics and the bypassed-final-accounting negative witness.
- Stage B ledger: source reconciliation reproduces 840 -> 873 probe labels (817 same, 6 expressions changed,
  17 retired, 50 new); 236 -> 238 controls (219 verbatim, 8 re-anchored, 9 retired, 11 new); only model/chrome/uistatus
  and the new w4bhome mutation lists change. Read the changed comparisons and substitutions against the named
  contract changes. Team/send native changes are navigation fixtures; W4a label and W3 paging assertions stay.
- Layout: stock probe plus the actual declarations on all three ABIs reproduce the granted sizes. No additional
  resident member or global is introduced beyond the approved allocation. QA board ELF, firmware and normalized
  symbol hashes equal the coder's final-1 on both boards.

## M103 — both removed effects audited, not just the cursor

The source anchor at `tools/probe_ui_model_mutations.py:3514` matches once and removes `_st.cursor = 0` AND
`_cfg_sel_valid = false` from `UiModel::close_settings_menu` (`model.h:4426`). It is unchanged from HEAD.

1. Settings MENU (`model.h:4631`) immediately calls `go_menu_home:4108`: that sets cursor 0 and invokes
   `settings_follow_screen:4397`, which clears the selection-valid flag (`:4412`). Both removed effects are repeated.
2. B457's normalization (`:4471`) sets the closed preview and menu mode. The renderer returns in its unavailable or
   closed-view arm before any cursor-indexed menu row. `sync_settings` returns at `:4482` before its only selection-valid
   read (`:4501`). A short sees `list_len:5950` = 1, and `advance_or_next:4247` resets the cursor while walking the rail;
   leaving Settings clears the flag. A double while unavailable does nothing; after service recovery it enters only
   via `open_settings_menu:4422`, which re-establishes both fields. Blanking and emergency overlays do not add a reader
   of either field in that closed preview. The old claim of a selectable, cursor-highlighted closed entry is obsolete.

The stock full model battery supplies the native survival measurement. The QA-only replay copies all 1,674 frozen
tracked/untracked paths, installs exactly this mutation in the copy, and runs the stock firmware-UI diagnostic:
557/1023/557, zero failures. This supports the source proof; it is not a substitute for the default control gate and
is not a universal equivalence claim for future call sites. No current behavior needs a new production hook merely
to redden the redundant assignments.

The live properties retain their independent witnesses: M101 (wrap), M102 (MENU destination), M105 (unavailable
admission), M100 (open on arrival), and H25 (B457 normalization). Formal disposition and the measured native results
belong in the QA receipt after the required batteries complete. M103 must never be reported RED or silently dropped.

## Additional real-render B457 fault

In the same evidence-only snapshot, restoring the baseline model and applying the exact H25 predicate replacement
matches once and compiles. The stock `--no-neg` runner exits 1: the two P3v cue/rail-escape checks fail, as does the
existing P3u recovery-preview check. The text-only CFG UNAVAILABLE check is not the failure witness. The layered and
BLE arms remain 557/557. `focused-controls.json` preserves the exact failed lines and hashes.

## Existing-register dispositions verified

- B350 is the same firmware-UI `--no-neg` bare-PASS defect already registered, not a new finding. The stock diagnostic now explicitly ends NOT A GATE and the regression checks that no PASS is emitted. Its close condition is met by B456's implementation.
- The title/comment at `test/test_firmware_ui_team.cpp:303` still calls BACK the TEAM exit. Its assertions measure the unchanged sub-view BACK token only; the current MENU width is separately asserted in model/status/native and P18b. This was explicitly left outside the fixture-only edit fence by the coder. Add to the existing B443 comment-drift inventory, not a new product finding or a silent claim of TEAM-exit coverage. The other reviewed old-navigation comment blocks have explicit adjacent W4b supersession notes.
