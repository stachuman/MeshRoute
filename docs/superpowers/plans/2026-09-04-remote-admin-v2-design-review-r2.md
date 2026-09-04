<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 design — independent Quality-Agent review, round 2 (2026-09-04)

**Subject:** `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` (working tree, 1794 lines,
round-1 items A1-A8/B1-B6/C1-C14 and R-RA-1..17 folded in). Verified against `HEAD 59b8f01`. Round 1:
`docs/superpowers/plans/2026-09-03-remote-admin-v2-design-review-r1.md`. Rulings:
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`.

## Verdict: HOLD (round 2) — narrow; three blocking items, the rest fold-ins for the same pass

Round 2 discharges round 1 almost completely: 25 of 28 items and five and a half of the six new rulings landed
EXACT, four corrections carry the `⚠ CORRECTED 2026-09-04` idiom with the old claim beside them, every §8
envelope figure closes to the byte, every re-verified citation (30+ anchors) is exact, the register's stale
"Slice 9" was fixed (M1), §14.1 now states the optional-ACK consequences (row cost, the eight-row bound, the
carrier without a row), §19 is split (0a-0e, 7a/7b, 8a/8b/8c, 9, 10 standalone) with a per-slice gate table and
bench parts 55-57, and the nonce formula is buildable. Nothing below reopens a ruling.

## Blocking (each a sentence or a table row)

| # | finding | anchor | required correction |
| --- | --- | --- | --- |
| H1 | **R-RA-15 addendum WEAKENED.** The owner ruled "there is no `-e` on `remote` … the wrapper REFUSES `-e`". The design says only *"`-e` keeps its existing encryption meaning and is not an ACK alias"* — in the paragraph that lists the wrapper's options, which reads as `-e` being available there; the refusal appears nowhere; §19.2 controls only "parsing `-e` as ACK" | `:273-274`, `:1222-1223`, `:1754`, `:1589` | state that `remote` accepts no `-e` and refuses it (authenticated requests are always sealed by §8; `open` is cleartext by ruling 4); add the refusal to the §19.2 control |
| H2 | **The one s18-governed `lib/core` edit has no owning slice.** §4 :124-126 calls the pre-tail handler at `node_mac_rx.cpp:2195` "a compatibility invariant" that v2 must replace with capability-owned handlers, §14 :1178 warns the replacement must not drop either owner, §19.2 :1576-1580 carries the control — but `lib/core/node_mac_rx.cpp` appears in NO §19 slice text and in no §19.1 row except 8b's client-side `node_mac*`. The accept-side `REMOTE_CMD` handler is unowned | §19, §19.1 | assign it: accept-side `REMOTE_CMD` handler → Slice 5 or 7b (named), client-side `REMOTE_RESP` → 8b; each with the corpus expectation (36/36 by construction, prediction-first) and the ruled pair |
| H3 | **The activation derivation is not yet a derivation.** *"Every input is named from the production timing authorities"* is false of the document: it names input CLASSES, and `protocol_constants.h` offers three incompatible prices for "one permitted reply-path requeue" — `cascade_requeue_base_ms` 5000 (:273), `cascade_requeue_backoff_cap_ms` 30000 (:274), `send_defer_ttl_ms` 30000 (:294). Under the first the default is ≈20-30 s, under the others ≥60 s; the *"approximately 30 s"* example therefore reproduces the retired literal with a ≥2× uncertainty | `:1075-1084` | either name the constants (and which requeue price, with the reason) or delete the 30 s example and let Slice 0e publish the first number; "2× margin" is the round-1 proposal the owner accepted ("we will accept worst … make it configurable") — attribute it as such, not as "the ruled 2×" (`:1083`) |

## Fold-ins for the same pass

| # | finding | anchor | correction |
| --- | --- | --- | --- |
| F1 | Slice 5 consumes Slice 7b: "exact retry … `session_full`/`session_busy`" needs the transcript pool and terminal frames 7b produces (§10 case 2 cannot be proven before 7b) | `:1472-1476` vs `:1488-1491` | reorder, or fence 5 to seen-table/fingerprint/epoch logic and move transcript-dependent behaviour to 7b |
| F2 | R-RA-12's source half: the invariant's source expression is `can_host_mobiles()` (`lib/core/node.h:639`: `!is_mobile && !is_gateway && n_layers == 1`) and the collision mechanism is `flight_is_team_plane` (`node.h:446-452`) over `is_team_peer` (`node_routing.cpp:823`); the design cites only the bug tag. ⚠ Not booked anywhere: `lib/core/node_mac.cpp:159-161` asserts *"the ONLY producers of `Plane::GLOBAL` on a DM flight are `console_parse.cpp:259` and the sim wrapper; every lib/core enqueue_data/do_send call passes AUTO or TEAM"* — Slice 0d falsifies that live comment; its brief must carry the V1 correction | `:154-158`, `:1439-1442` | cite the three anchors; add the comment correction to 0d's fence |
| F3 | "ruled pair if production bytes move" makes the two-env board gate conditional on one row | `:1524` (row 0a) | delete the condition — the pair runs every slice (D1) |
| F4 | Slice 3's Bench Part 55 ("physical-USB first owner and recovery") is only half-executable before Slice 4 lands `/mrtargets` (§6.4 step 3 is a controller-side write) | `:1464-1467`, `:1531` | say so, or move Part 55 to Slice 4 |
| F5 | Bench Part 57 is assigned to six slices (7a, 7b, 8b, 8c, 9, 10); M2 wants a residue per slice with its expected console line | §19.1 | split or name sub-parts |
| F6 | §19.1 lists the five gate elements but the table carries four — native cases are deferred to the briefs; acceptable if stated as deliberate | `:1517-1541` | one sentence |
| F7 | Two load-bearing new claims carry no anchor: "Without `-a`, R1=A allocates no delegated-correlation row" (true: `node_hashlocate.cpp:1640/1666/1669`) and "eight rows … 300 s" (true: `node.h:3185`, `protocol_constants.h:790`) | `:1224`, `:1276-1278` | cite |
| F8 | One universal `common_command_max_bytes` across USB (1024 B input) and the best RPC carrier (206 B) necessarily shortens the local USB console command; the escape clause at `:973` covers it procedurally but the consequence is never named | `:976` vs `:176` | name it so the characterization ruling is taken knowingly |
| F9 | A5's correction silently deleted the old "overwriteable" sentence (no kept-visible marker, unlike the four `⚠ CORRECTED` blocks beside it) | `:190-191` | add the marker |
| F10 | Two inherited anchor drifts: the 3 s deferral is `src/firmware_remote.cpp:148` (not :151, which is blank); `pt[64]`/`v[64]` are at `:92` (not :95-96); `:190` is the `vl` truncation | `:1073`, `:136` | fix the lines |
| F11 | Slice 0e "derives `remote_scheduled_reply_path_budget_ms(cfg)`" but "lands no configuration" — the function's owning slice is unnamed | `:1447-1450` | name it (7a) |

## Round 3

One Author pass over H1-H3 and F1-F11 with the correction idiom. I re-read only the changed lines against the
tree; a clean pass is **PASS = implementation authority**, which authorizes the Slice 0 briefs (0a, 0b, 0c, 0d,
0e) through the pre-check → brief → gate cycle. No owner ruling is needed for round 3.
