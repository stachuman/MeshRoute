# Revision-3 source-validation evidence

This archive supports receipt §6 in [the slice evidence](../2026-09-18-radmin-slice8b.md).
It is not an implementation freeze or full gate.

`state.json` and the inventories pin revision 3, the two checkouts and every existing tracked/untracked input.
`brief-revision3.md` preserves the exact dispatched brief. `preparation.patch` records the existing QA changes.
`commands.json` contains the exact compile commands and exits. `logs/` preserves the initial fanout fixture
compile failure as well as the final successful runs and the expected failing order control.

The two `.cpp` characterizations link the real native core/console archives built in the complete revision-2
snapshot at `/tmp/mr-s8b-r2-ksqkjdbw/snapshot`. All 358 production/test/tool/platformio input hashes were checked
against both that snapshot and the revision-3 checkout before using those archives; only documentation changed.
No new full native build is claimed. To reproduce, use a complete checkout with those primary input hashes, run
`pio test -e native`, then invoke `python3 run_proofs.py --snapshot /absolute/path/to/that/checkout`.
New logs go to a fresh temporary directory and leave this archive unchanged.

`full-mapping-proof.cpp` embeds the unchanged fixture prefix from `test/test_remote_client.cpp` and adds two
bounded characterization cases. Its carrier refusal is synthetic; it tests actual controller error/phase
handling, not a real-radio ring-fill chain. It distinguishes initial capacity refusal from a later carrier
refusal, including after an actual authenticated bootstrap exchange.

`fanout-order-proof.cpp` uses the real `console::write_event`, `console::write_push` and `LineSink`. The remote
event is synthetic because the observer has not been implemented. It demonstrates that newline transmission
is immediate at the brief's prescribed placement. `--after` changes only the order of the two call sites and
must fail the BEFORE-placement characterization; it is a bounded order control, not a mutation-union result.
`ARDUINO` is defined only around the Print adapter include; core/console retain the native feature profile.

The first fanout compile incorrectly applied `ARDUINO` to the whole TU (altering Node's profile/layout) and
qualified the `EF_*` macros as namespace functions. Those fixture mistakes were corrected without touching
production or disabling any production assertion. No result is claimed from that failed compile.
