<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; review brief: Claude, specification author, 2026-09-24 -->
# Standalone mobile Home design — independent review brief

**2026-09-24 · READY FOR INDEPENDENT REVIEW.** The subject is
[the standalone-mobile Home design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md),
revision 2.14. Every owner decision in its §12 is ruled. This is the design cycle's spec gate
([agent roles](../../2026-09-02-agent-roles.md), "What a spec must do"). It authorizes no implementation, no
implementation brief and no resource allocation.

## 1. Role and authority

You are the **Quality Agent in a fresh context that did not write the design**. Independence is the point: every
recorded defect class in this codebase was caught by a context that had not written the artefact.

- **Review only.** Never edit the design, its source audit or any other file under review. Your outputs are one
  report (§6) and, where warranted, new rows in the maintained register (M1).
- **Owner rulings are not reopened.** They are D1–D13 and D13b: quoted in design §1, recorded in §12. If a ruling
  rests on a fact you find false, report it as a finding marked `OWNER RULING REQUESTED`, with the evidence, and
  keep reviewing.
- **Verdict vocabulary** (agent roles): **PASS**, **PASS with fold-ins** (text corrections that need no
  re-review), or **HOLD** (numbered required corrections; one re-review of the changed sections only).
- **Working rules:** the board in `AGENTS.md` (identical to `CLAUDE.md`), especially V1/V2, U1–U3, C1–C4, P4–P7
  and M1–M3. MeshRoute is not deployed, so no backward-compatibility machinery is expected (M3; the owner restated
  this for stored formats with D8).

## 2. Pinned inputs

| Item | Pin |
| --- | --- |
| Repository | `/home/staszek/MeshRoute`, base commit `20357578700270d7315dcf86bc3bb5725d8024cb` (committed sources and earlier specs are pinned by it) |
| Simulator | `/home/staszek/lora-universal-simulator`, `6585649ea5a780f0542b2931853a667be56a5b2b`, clean, read-only |
| Reviewed texts | SHA-256 in the [input manifest](../evidence/2026-09-24-standalone-mobile-home-review-inputs.json), itself SHA-256 `0e3319fa9fd2df423985a93266135dae90302f9193fe6ce4508afb599e7bfab6` |
| Expected extra untracked files | this brief and that manifest, created after the manifest's status snapshot |
| Preserve | the `B164.md` deletion, every untracked path, the owner's uncommitted edits; never stage, commit, reset, clean or restore |

**First step:** verify both commits, the manifest's own hash and each reviewed file's hash, then report the
working tree. On a mismatch:

- **Design or audit differs:** STOP and ask the owner before reviewing; a later revision may have folded changes
  in.
- **Register differs:** diff it. A change inside rows B440–B448 or the addenda on B231, B241, B286, B335, B418
  and B441 is a STOP. A change elsewhere is ordinary churn: record it and proceed.
- **`tracker.md` or `MEMORY.md` differ:** context only; proceed.

## 3. Required reading

1. The rules: `AGENTS.md`, [CODE_GUIDELINES](../../CODE_GUIDELINES.md), [agent roles](../../2026-09-02-agent-roles.md).
2. The **whole** design, then its [source audit](../evidence/2026-09-22-standalone-mobile-home-source-audit.md).
3. The author's assignment,
   [the specification-author handoff](2026-09-22-standalone-mobile-home-spec-author-handoff.md). Its §§4–8 list
   what the design had to settle and its completion criteria; review against them.
4. [Register](../../2026-07-30-open-bug-register.md) rows B440–B448 and the addenda above; B118, B191 and B236 as
   dependencies.
5. The earlier authorities the design revises or retains (design §10):
   - [UI-17](../specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md), §1, §2 and §9 R-1–R-7.
   - [UI-16](../specs/2026-08-22-ui16-nearby-onboarding-spec.md), R-1, R-13 and the candidate row (S-35, F-15).
   - [UI-10/11 presets](../specs/2026-08-25-ui10-11-preset-catalog-spec.md), OQ-A, P3, R-1, R-2 and pin 6.
   - The [parent OLED design](../specs/2026-07-31-onboard-oled-ui-design.md), §§1, 3.2 and 3.6.
   - The [GPS design](../specs/2026-08-25-heltec-v4-mobile-l76k-gnss-and-automatic-location-design.md), §0.1 and
     §4.5.
   - The [address-book design](../specs/2026-07-29-peer-address-book-design.md), §2.3.
6. The [metal plan](../../2026-09-20-metal-test-plan.md), UI-01 to UI-19, NV-06 and POWER, for the §15 mapping.

Anchors pin by **symbol**; a line number is a navigation hint (P4). Comments and older specs are not evidence (V1).

## 4. What to review

**A. Source fidelity (V1).** Re-derive, don't copy. Resolve every [FACT] statement and anchor by symbol: design §2,
the "today" paragraphs of §§4.6, 6.6, 8 and 9, and §§7.1, 7.3, 11 and the audit's §3. Check these high-risk claims
independently:

1. The §7.1 capacity chain:
   - frame 255 → DATA inner 241 → app-DM admission 232, sealed DM 214 (208 with `-l`);
   - channel 200 → sealed plaintext 174 → team text 173 (163 with `-l`);
   - the parser clamp at 241;
   - `kSendLineCap` is 96 today, and the 199-byte derivation for the longest composed line.
2. `/mrui` version 2: T = 163 → slot 167 B, record 2852 B, catalog +7440 B.
3. Location:
   - `-l` strictness on both paths (`enqueue_data`, the `on_command` send_channel arm);
   - R-2's per-slot authority (`ui_compose_send_line`);
   - `refuse_reason_of` giving `REFUSED` plus the code on a synchronous refusal, and `NO FIX` only through the
     asynchronous `send_failed`.
4. Names:
   - the `Node::effective_name` default;
   - `peer_key_set` ignoring an empty name and refreshing a non-pinned row;
   - the codec's nameless key request;
   - every simulated node carrying a name (`JsonConfig.cpp` `require_field`; 783 nodes in 36 scenarios);
   - `whoami`, `ready` and peer JSON rows;
   - B447 and B448.
5. Inbox:
   - `rx_time_ms` is uptime (`ArduinoClock`);
   - `dm_newest_seq()`/`chan_newest_seq()` are restored before `mr_ui_init` and nothing is recorded before the
     main loop;
   - `Inbox::clear()` keeps the high-water;
   - B445.
6. Navigation and setup:
   - `next_screen`, `list_len` and today's passive screens;
   - the settings service opening on arrival and never closing;
   - provisioning closing when SETTINGS is left;
   - the `prov_invite` predicate;
   - `DeviceInvite` writing no settings;
   - B444's `send_channel` `-t` admission against the team-DM guard.
7. Wake: `ui_route_recv_push`, where a DM always wakes and a team post wakes only when sealed; nothing navigates.
8. Editor cost: re-run the model as stated in the audit's §4.
   - Counts: `STAN` 30, `END OF SHIFT` 84, `RETURN TO BASE NOW` 126, the 33-character sample 217.
   - Averages on uniformly random text: 7.50 gestures per character, against 7.93 when returning to the group just
     used.
9. Glyphs and geometry:
   - `»` (0xBB) is present in U8g2 `6x10_tf` (the copy under `.pio/libdeps/heltec_mobile/U8g2`);
   - rail x 0–9, gutter bar x 10–11, body x = 12, 19 columns, five rows;
   - the splash coordinates in §9.

**B. Owner rulings carried through.** For each of D1–D13 and D13b:
- its consequences appear consistently in §§4–15;
- no ruled item still carries proposal wording;
- no two sections contradict each other on it.

**C. The author's proposals.** Sections still marked [PROPOSED] get the substantive design review:
- §6.4 arrow-rule details;
- §6.5 item targets;
- §7.2 word wrap;
- §7.4 submission (`SendKind`, `SendReq`, the three new `send_gate_of` questions, the composer forms);
- §7.5 entry points.

Judge them for correctness, completeness (every state, exit and refusal) and consistency with the ruled sections.

**D. Safety invariants**, across the whole design:
- emergency precedence and overlay absorption (§5.5), with no automatic submission after pre-emption;
- review invalidation and the safe preselection;
- draft lock and lifetime against B236;
- the preset generation gate and the team/recipient bindings;
- UI-17 R-1, R-5 and R-7, and S8;
- `SEND TO TEAM` hidden while the ID is pending (B444);
- written messages never adding `-l`;
- the Home card never marking read, navigating or waking.

**E. Strings and geometry.**
- Every wireframe and quoted panel string fits its budget: 19 columns, the labels at 16, 8 and 7, and the TEAM row
  at 6.
- Row budgets hold with `RESTART NEEDED` and with the Home card showing.
- No hash is ever clipped; UI-16 F-15 bans that.

**F. Resources (§11.1).**
- The arithmetic is right.
- Nothing is presented as measured that is only estimated.
- No allocation is granted beyond the catalog estimate the owner accepted with D7.

**G. Packages (§13).**
- C1: W3 is a separate refactor. C4: no wire change.
- P6: each package's gate obligations.
- P7: symbol fences, including `effective_name`, `kUiPresetTextMax`, `SendReq`, `SendKind`, the navigation helpers
  and `InboxRowBudget::publish`. No `lib/core` translation unit is added, so the simulator source list is
  untouched.
- The two-env board gate.
- Dependency order: W1 and W1c before W4a, W2 before W6, W4c before W4d.
- W1c's "predicted corpus-inert" claim.

**H. Acceptance and metal (§§14–15).**
- Every contract has an automated seam, with production reachability through `probe_firmware_ui`.
- Metal rows are physical residue only (M2).
- Nothing has been added to the metal plan yet, as §15 states.

**I. Authorities (§10).** Every revised earlier ruling is named by its anchor. Search for any behaviour change the
table does not list.

**J. Register and audit.**
- For B440–B448 and the addenda: each measurement is reproducible from source, each CLOSE BY is actionable, and
  none duplicates an existing row.
- The audit's "not verified" list is honest.
- B335 remains OPEN.

**K. Assignment compliance.** Against the handoff's §8:
- the owner agreements are preserved;
- proposals, facts and rulings are visibly distinct;
- nothing claims a PASS, a measured board figure or an owner ruling that doesn't exist.

## 5. Fence

- **Read-only** for `src/`, `lib/`, `test/`, `tools/`, `variants/`, `platformio.ini`, the simulator, `simulation/`,
  the metal plan and every file under review.
- **Allowed writes:**
  - the report (§6);
  - new register rows for genuinely new code or document defects outside the design, at the next free number;
    re-read the next-free line before allocating (B449 at the pin).
  - Defects in the design itself are **not** registered; they go in the report for the author to fold in.
- **No builds or test runs are required.** A disposable calculation (widths, arithmetic, a host `sizeof` in a
  scratch directory) is allowed outside the tree and must be labelled a measurement, not a result.
- Durable output lives in the repository. An agent's scratch space is volatile.

## 6. Deliverable

Write `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-design-review.md` with an author line on line 1,
containing:

1. **Verdict:** PASS, PASS with fold-ins, or HOLD, with the date.
2. **Inputs verified:** the commits and every hash, with the result of each; any mismatch and how it was handled.
3. **Findings table.** Columns: `DR-<n>` · severity · design § · finding · evidence (`file:symbol`, line as a
   hint) · required correction · `OWNER RULING REQUESTED` (yes/no). Severity:
   - **BLOCKER:** a false fact or an unsafe rule that would mislead an implementation brief;
   - **MAJOR:** a contradiction or omission to fix before briefs;
   - **MINOR:** a wording, width or anchor fold-in.
4. **Verified independently:** what you checked and how.
5. **Not verified.**
6. **Register rows added,** by number.

## 7. After the review

- The owner relays the report to the author. The author folds in the corrections as revision 2.15 and records each
  `DR-<n>` as fixed or disputed.
- A HOLD gets one re-review of the changed sections only.
- On PASS the design's status line records the review.
- A Quality-Agent pre-check ledger then precedes each §13 package brief.
- B335 stays open until its implementation passes independent QA.
