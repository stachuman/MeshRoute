# W6 independent source review

Reviewed the working-tree changes against owner base 70ff486 and approved brief revision 2. No source, tests or tools were edited by QA.

- The record keeps MRU1 and adopts version 2, slot 167 B/blob 2852 B with a named one-byte tail. Old-v1 recognition uses size/magic/version only; the old body is not interpreted. Boot defaults never write, and the next successful change writes v2 transactionally. Failed saves do not publish.
- Paging is canonical 1..5, bare list means 1, invalid/extra tokens return bad_page; intervals partition all 17 slots. End records preserve capacity=17 and add text_max=163, with page/pages last on list replies only. Reset-all installs eight defaults before its unpaged reply. The 244-byte record-line and separate 81-byte boot-line bounds agree with the writers.
- The inbox fake stores 2852 B and reports actual retention. Exact-capacity, over-capacity, dishonest-retention controls, MrProbeNv::holds and boot reload are exercised by the OLED router arm. Deferred-action source and its inbox-builder seam remain unchanged.
- Compose rows remain 20 B, with full live catalog bytes used for sending. Only the display projection abbreviates. The shared union preserves the Inbox two-row pager; review uses three rows with the ruled word-wrap, BACK selected, frozen header, LOC and capture binding. The binding is separate from an owed request.
- The production resolver distinguishes a known zero hash from no known hash. The review captures team/hash/generation; execution asks the live answers again. Refusal does not submit. Alarm admission remains before ordinary gates. The single 199-byte writable line is in firmware_ui.cpp; no extra retained carrier was found.
- Review invalidations, blank/wake and alarm paths were traced through model and firmware calls. Settings, Home and existing Inbox semantics outside the disposition ledger remain unchanged.
- The native/probe/mutation disposition ledger was checked against the changes. Navigation edits explicitly confirm reviews; old assertions are retained or strengthened where a one-double send no longer exists. The new controls cover capture execution and omitted capture, the line owner/capacity, persistence lies, old-v1 interpretation and response overflow. Unreddened preconditions/defence-in-depth checks are not claimed as independently mutation-proven.
- firmware_commands.cpp remains 1915 lines with usage/comment changes only. No board-UI anchor or command-inventory row moves. No lib/, simulator, partition, wire or mrcfg change is present.

The four separately verified follow-ups are recorded in followup-findings.json and alarm-fixture-result.json. The generation-close finding is a static mutation-target gap, not a claimed surviving mutant. The legacy alarm fixture is byte-identical to the base. Neither is evidence of a new W6 runtime failure.

The coder's stack totals are compiler-frame sums, not measured task high-water marks. QA audits that distinction and does not claim an independent runtime-stack or physical-NVS measurement. Metal qualification remains owed.

The retained-count wording follow-up includes five unchanged comment lines across three headers and the old two-arm aggregate banner after three executed inbox arms; exact locations are in comment-drift.json. This wording debt does not change the measured reader, writer or execution coverage.
