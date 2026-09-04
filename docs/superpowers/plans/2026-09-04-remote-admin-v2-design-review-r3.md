<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 design — independent Quality-Agent review, round 3 (2026-09-04)

**Subject:** `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` at `HEAD c512580`
(1889 lines; round-1 A1-A8/B1-B6/C1-C14, round-2 H1-H3/F1-F11 and rulings R-RA-1..20 incorporated).
Method: re-read of every changed region against the tree and the ruling ledger
(`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`); rounds 1-2 are
`…-2026-09-03-…-review-r1.md` and `…-2026-09-04-…-review-r2.md`.

## Verdict: PASS — the design is IMPLEMENTATION AUTHORITY

| round-2 item | landed at | verdict |
| --- | --- | --- |
| H1 / R-RA-18 — `-e` mandatory, exactly one of `open` / `-e` | §6.1 :272-283 (syntax block + XOR rule + kept-visible correction of both earlier readings); §12 :998; §14.1 :1261-1262; §19.2 :1669-1672 (control: neither / `open -e` refuse; making `-e` optional or coupling `-a` reddens) | EXACT |
| H2 / R-RA-19 — dedicated Slice 1b | §19 :1506-1516 (refactor only, `node_mac_rx.cpp:2195-2212` behind accept/client entry points, behaviour-identical under the legacy gate, disabled role → `unsupported_internal`); §19.1 row 1b :1600; §20.1 #47 | EXACT |
| H3 / R-RA-20 — first-hop budget, named constants, 2× | §13 :1090-1142: nine named terms with anchors (`:133`, `:134-135`, `:273`), the first-requeue price stated with both exclusions, the two MAC wait windows assigned to Slice 0e, the "≈30 s" example retired inside the correction idiom, 2× attributed to the owner's 2026-09-04 word, first-hop rationale stated, Slice 7a owns the production function | EXACT |
| F1 Slice 5 fenced from transcripts | :1530-1536 (transcript retry / ACK debt / rollover / already-acknowledged → 7b) | EXACT |
| F2 R-RA-12 source citations + 0d comment correction | §4 :161-162 (`can_host_mobiles()` node.h:639, `flight_is_team_plane` node.h:446-452, `is_team_peer` node_routing.cpp:823); Slice 0d :1487 (`node_mac.cpp:159-161` rewrite in the fence) | EXACT |
| F3 unconditional 0a board pair | :1594 + the correction note :1614 | EXACT |
| F4/F5 bench parts | 55a (target-side, Slice 3) / 55b (controller `/mrtargets`, Slice 4) / 57a-f per owning slice, each with its expected line | EXACT |
| F6 native cases deferred deliberately | :1588-1590 | EXACT |
| F7 anchors for "no `-a`, no row" and the eight-row bound | :1267, :1269 | EXACT |
| F8 universal-cap USB consequence | :992-993 | EXACT |
| F9 kept-visible marker on the "overwriteable" correction | :195 | EXACT |
| F10 anchors `firmware_remote.cpp:148` / `:92` / `:190` | :137, :1094 | EXACT |
| F11 Slice 7a owns `remote_scheduled_reply_path_budget_ms(cfg)` | :1116-1117 | EXACT |

Stale sweep: the only remaining "approximately 30 s" is inside §13's correction idiom (:1093); no "ruled 2×", no
conditional board pair, no "refuses `-e`" outside a kept-visible quote, no "Slice 9" meaning the carrier.

## What PASS authorizes, and the order I recommend

Implementation proceeds slice by slice through the standing cycle (Quality-Agent pre-check ledger → Author brief →
brief gate → Opus dispatch → report gate → Author landings → owner commit), starting with §19 Slice 0:

- **0d** (the three `send_by_hash` home arms → `Plane::GLOBAL`, `lib/core`, prediction-first corpus) and **0e**
  (characterization: generated verb inventory, ABI/capacity/timer measurements, `remote_body_cap` derivation, the
  first-hop budget numbers) have disjoint fences from 0a-0c and unblock the most later slices — pre-check them first,
  dispatchable in parallel in isolated worktrees.
- **0a → 0b → 0c** share `src/firmware_commands.cpp` / `src/fw_main.cpp` and run sequentially.
- **B283** (census cell) remains dispatchable whenever its brief carries the control fold-in.
- Slice 1, 1b, 2 … follow §19's order; Slice 6 waits for the owner's authority classification over 0e's generated
  table; 8b waits for [[B112]].

Author landings after this PASS: the design's status line ("DESIGN PASS 2026-09-04 — implementation authority;
slices per §19, none dispatched"), the register (a remote-admin v2 row or the B279/B208 rows' pointers), `tracker.md`.
