<!-- Coder preflight only. QA authors the reissue and follow-up fences; owner rules allocation and commits. -->
# Slice 7b-3 — parallel typed-plan preflight and B391 instrument repair

**Read-only behavior preflight delivered; 7b-3 behavior remains HOLD.** Executed 2026-09-14 at
`b9d75aaca55a0e350d9707512e9b6eec8a81ab22`, alongside the separately fenced codec implementation.
No behavior brief reissue, runtime fallback, prepared-action allocation, production effect extraction,
Node re-pin, independent QA PASS or owner commit is claimed.

## B389: complete enumeration, not yet a complete prepared allocation

The actual policy table has **48 disruptive rows** across twelve families. The parallel read-only worker
enumerated every row, including aliases, coarse family entries and individual cfg/create arguments, against
real handlers. Source hashes were rechecked by the parent before publishing. The enumeration's inputs remain
byte-identical to b9d75aa; `f993191..b9d75aa` has no `lib`/`src` changes. The concurrent codec edits are not
enumeration inputs. See [full source-backed analysis](2026-09-13-radmin-slice7b3-typed-plan/typed-plan-preflight.md)
and [all 48 policy rows](2026-09-13-radmin-slice7b3-typed-plan/all-48-rows.md).

The important distinction is an **owned parsed request versus an owned, fully prepared action**. Existing
JoinRequest/TeamRequest builders are useful, but public apply services still make mutable decisions, perform
durable transactions and immediately change live state. Retaining a request and calling those services later
would not fulfill the accepted-before-scheduled contract. A stale whole config Blob is also unsafe: the real
ConfigService reloads and merges only covered fields to preserve unrelated channel-counter/key writes.

| Proposed preparation group | Policy rows | Source-backed conclusion |
| --- | ---: | --- |
| reboot/prep aliases and OTA | 4 | No parameter payload; typed effect outcome and explicit sink/backend binding still needed. Existing `ota_start()` is idempotent; do not use the toggling wrapper. Prep reports erase outcome only through Print today; do not parse it to infer typed success. |
| factory reset, sleep, crash | 8 | Small separate C1 grammar/admission/effect extraction proposed. Keep exact confirm, existing sleep/crash prefix grammar, debug/backend checks and reset/wipe order. |
| disruptive cfg, gateway, join/create/leave, team, regen | 36 | Actual prepared delta/transaction services needed. These are **conditional R-RA-39 refusal candidates**, not recorded exceptions or an implemented fallback. QA must list precise rows and fenced follow-ups before behavior coding. |

Prep-restart remains schedulable under R-RA-38; it cannot be silently removed by the fallback. Non-disruptive
team/key verbs remain outside the proposed exceptions. Predictable validation, capacity and generation failures
must occur before scheduled; activation cannot become a routine revalidation refusal. The full analysis names
specific follow-up fences and closure proofs for simple effects, config, provisioning, team and regeneration.
These are recommendations for QA's preparation, not unilateral scope changes.

Fresh **compile-only**, sequential native/ARM/Xtensa pricing uses each real PlatformIO environment and nm.
No board link or linked-RAM claim belongs to this preflight. The existing production Node remains
230896 native /157264 gateway /117912 mobile. JoinRequest is 32/8, TeamRequest 136/8, TeamPlan 128/8;
ProvSnapshot is 40/8 native versus 32/8 boards and contains borrowed pointers, so is not an owned-plan bound.

A deliberately limited **four-row effect-only comparison** measures action 40/8; independent transcript
header 24/8→32/8; two diagnostic bytes plus alignment; complete comparison state 8824/8→8904/8, **+80 bytes**.
This is not a 48-row representation, final proposed scope, Node re-pin or owner allocation request. The final
representation depends on the prepared service/fallback scope still to be authored; **B389 remains open**.

Supporting artifacts are individually hashed in
`2026-09-13-radmin-slice7b3-typed-plan/artifact-sha256.json`; `artifacts.tar.gz` contains the exact scripts,
source audit, full JSON enumeration, candidate TU, real compiler commands/idedata, measurements, logs and
three small ABI objects. `published-sha256.json` binds the archive and readable copies. Raw workspace:
`/tmp/mr-codex-s7b3-typed-plan-ZXuR9w`. Two private setup failures (omitted custom boards, then missing Git
metadata) are disclosed in the archived `measurement-attempts.md`; the corrected three-target run uses the
actual pinned revision override. No shared-tree build or mutation was performed by the enumeration worker.

## B391: separately attributed repair delivered with the codec tool file

The sole production-file consumer edit is **none**: `lib/core/node_mac_rx.cpp` stays byte-identical. X09's
source pattern in `tools/probe_ui_model_mutations.py` no longer depends on B388's correctly retired comment;
it uniquely selects the same `radmin_expiry_arm()` call plus code/gate boundary. The mutation still deletes
that call, never changes the behavior under test to obtain a RED.

The source-cardinality audit moves X09 **0→1 matches** and leaves all other historical matches one. The final
full radmin5rx battery is **14 RED /0 unusable**; X09 compiles and fails **1 assertion /1 match**. Both worker
baselines are the codec candidate's **2912/184461/0** and both restore source MD5
`13aab3b4d2b0d9e36b39d4201790d030`. Before/after tool, search/replacement and unchanged source hashes are
preserved in the codec's `preservation-checkpoint.json`.

The full tools discovery and 52-battery union are part of the concurrent
[codec gate receipt](2026-09-13-radmin-slice7b3-0.md), whose current status is authoritative; this focused
result alone is not a full gate. B391 closure belongs to independent QA. The later behavior reissue waits
for the codec's independent PASS and separate owner commit as well as the remaining allocation/preparation.
