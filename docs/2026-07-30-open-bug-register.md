<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# BUG REGISTER

*Opened 2026-07-30 at the owner's request, so findings stop living only inside `BASELINE.md` notes and agent
reports. **This file is the index; `simulation/BASELINE.md` carries the evidence.** Each entry names the note
that has the measurement.*

⚠ **Every `file:line` here drifts** — this tree moves several times a day and eight slices landed on 2026-07-28/30
alone. **Re-locate by symbol before acting (V1/V2).** A line number in this file is a hint, never a fact.

★ **Nothing here is speculative.** Every entry was either measured, or found in-source with a marker left by the
slice that declined to fix it (C1). Where an entry is *unmeasured*, it says so explicitly.

---

## Current status checklist — authoritative as of 2026-08-03

Use this section to choose work and mark completion. The detailed records below preserve the full evidence,
superseded premises and gate history; their older status language describes the time of the record and does not
override this checklist.

Legend:

- `[x]` — fixed, closed or resolved; retain the numbered record.
- `[ ] OPEN` — implementation, measurement or an owner decision is still available.
- `[ ] PARKED` — recorded but not currently dispatched; do not implement without a new ruling.
- `RECORDED` — useful constraint, not a defect.

### Open

- [ ] **B20 — OPEN:** encrypted DM lengths 215–216 can disappear without `send_failed`.
- [ ] **B21 — OPEN:** an oversized DM reports the wrong condition and no `send_failed`.
- [ ] **B24 — OPEN:** `q_tx.rt_total` remains plane-inconsistent telemetry.
- [ ] **B25 — OPEN / UNMEASURED:** a team member may answer a static sync with a team-plane beacon.
- [ ] **B31 — OPEN / POLICY:** `key_hash_for_id` has no authoritative or TTL gate.
- [ ] **B34 — OPEN:** the simulator still collapses refusal reasons to generic `error`.
- [ ] **B35 — OPEN / SILENT:** channel self-skip compares ids across planes.
- [ ] **B36 — OPEN / CONTRACT:** received message JSON does not expose attached location.
- [ ] **B52 — OPEN / CONTRACT:** JSON exposes team confidence but not static confidence.
- [ ] **B54 — OPEN / OWNER CALL:** the first claim in a full first-hand team table evicts one beacon row.
- [ ] **B55 — OPEN / CONTRACT:** `reqpubkey_sent.hash == 0` needs its S4b meaning documented.
- [ ] **B56 — OPEN / CONTRACT DECISION:** stage-2 `reqpubkey` failure is not app-visible.
- [ ] **B60 — OPEN / SPEC READY; ACK POLICY OPEN:** multi-gateway `send_layer` resolves the final hash on an intermediate layer instead of selecting the next gateway.
- [x] **B61 — FIXED 2026-08-03 (own commit, UNCOMMITTED):** `board_name()`'s silent `#else → "native"` is now an `#error`; `MESHROUTE_NATIVE` gets its own explicit arm.
- [ ] **B62 — OPEN / ONE LINE, DEFERRED BY DESIGN:** three in-source paths still say `src/board_ui.cpp` after §A0 moved the file to `variants/heltec_v3/` — including the file's own line-1 header. Fold into the Phase-A OLED slice.
- [ ] **B63 — RECORDED (gate methodology):** on xtensa, an object entering or leaving the link set moves `.flash.text` by up to ±200 B **even when it is zero bytes**; free on ARM. Retires the "+192 B = leaving `src/`" rule.

### Parked or trigger-gated

- [ ] **B5 — PARKED:** `channel_pull` has no team scope and exposes the static source id.
- [ ] **B6 — PARKED:** the team plane has no budget-penalty mirror.
- [ ] **B7 — PARKED:** the team plane has no slow-reprobe state.
- [ ] **B8 — PARKED / UNMEASURED:** relay behavior for `src == 0` is unresolved.
- [ ] **B9 — PARKED / SIM TELEMETRY:** team `rt_update.slot` is mislabelled.
- [ ] **B10 — PARKED / TEST DEBT:** the simulator has no working `routes` command.
- [ ] **B11 — PARKED / TRACE DEBT:** frame-trace switches omit live values.
- [ ] **B12 — PARKED / REFACTOR:** seal-or-refuse logic is triplicated.
- [ ] **B13 — PARKED / REFACTOR:** the team liveness scan is duplicated.
- [ ] **B14 — PARKED / COMMENT:** a `node.h` routing comment has drifted.
- [ ] **B15 — PARKED / REDESIGN DEPENDENCY:** the binary config TLV omits `team_ch_key`.
- [ ] **B16 — PARKED / SIM PARITY:** `send_layer` grammar differs across sim and metal.
- [ ] **B18 — PARKED / D2:** a team H relay can read the static binding table.
- [ ] **B19 — PARKED / FOLD INTO B12:** `deleg_ack_put` is duplicated at eight call sites.
- [ ] **B23 — PARKED / POLICY:** the metal `resolve` verb reaches AUTO and carries a dead field.
- [ ] **B37 — PARKED / DEPLOYMENT TRIGGER:** symbolic wire tests cannot detect format drift.
- [ ] **B57 — PARKED / DELIBERATE:** a beacon-learned binding does not consume a pending `reqpubkey` intent.
- [ ] **B59 — PARKED / DO NOT DISPATCH:** reliable repair may require a routing/custody algorithm change.

### Closed or fixed

- [x] **B0:** plaintext coordinate disclosure removed.
- [x] **B1:** whole-token team-id parsing enforced.
- [x] **B2:** team H answers no longer write the static binding table.
- [x] **B3:** `reqpubkey` plane behavior aligned between simulator and firmware.
- [x] **B4:** sync-response route count made plane-aware.
- [x] **B17:** out-of-range team ids rejected.
- [x] **B22:** plain `send_channel` behavior aligned between simulator and metal.
- [x] **B26:** NV blob validation factored into a testable shared path.
- [x] **B27:** unsafe `cfg set team_id` write surface removed.
- [x] **B28:** team membership now enforces the mobile-role invariant.
- [x] **B29:** `key_hash_for_id` miss no longer loops or invokes undefined behavior.
- [x] **B30:** aliased hashes resolve to the freshest authoritative team id.
- [x] **B32:** command refusals retain their specific reason.
- [x] **B33:** `hashof` refusal advice names remedies that can work.
- [x] **B38:** a team post remembers and reports an observed first relay.
- [x] **B39:** `ctr == 0` semantics documented without claiming that every zero means failure.
- [x] **B40:** `channel_sent` carries the full local 16-bit correlation counter.
- [x] **B41:** the simulator renders `channel_sent.relayed`.
- [x] **B42:** by-id `reqpubkey` resolves both planes with explicit ambiguity handling.
- [x] **B43:** routable-but-unheard ids can complete the two-stage pubkey workflow.
- [x] **B44:** `peers all` includes static routed-but-unkeyed peers.
- [x] **B45:** the local self-binding is no longer printed as a peer.
- [x] **B46:** claimed observations cannot demote or displace authoritative bindings.
- [x] **B47:** `reqpubkey` admission reports early and transmitter rejection honestly.
- [x] **B48:** display de-duplication no longer decides whether airtime may be spent.
- [x] **B49:** the `CmdCode` invariant test derives its bound.
- [x] **B50:** `tx_with_retry` propagates the transmitter result.
- [x] **B51:** channel-digest retirement follows the approved transmitter boundary.
- [x] **B53:** inspection resolves at the claimed floor while send paths remain authoritative.

### Recorded constraint

- [x] **B58 — RECORDED, NOT A DEFECT:** the by-id intent ring fills before the LBT defer-ring rejection can be reached
  through the same command.

### Non-bug decisions and deferred audits

- [ ] **D1 — TRIGGER-GATED:** revisit the team DV hop-cap only when a team path exceeds eight combined hops.
- [ ] **D2 — OPEN AUDIT:** audit plane-typed read paths that can fall back to the static table.
- [x] **O1 — RESOLVED:** B1 closed the team-target parsing decision.
- [ ] **O2 — PARKED:** fold `deleg_ack_put` de-duplication into B12, never take it alone.
- [x] **O3 — RESOLVED:** a team channel key lives exactly as long as its `team_id`.
- [ ] **O4 — OPEN SECURITY DECISION:** decide how BLE access to `team exportkey` is protected.

---

> ⚠ **The companion contract has PENDING updates too.** `ios-companion/INBOX_SYNC_CONTRACT.md` now opens with a
> **PENDING CONTRACT CHANGES** box listing everything spec'd-but-unbuilt, so the app team does not implement against
> a surface about to move. ★ **One item needs app action ahead of the slice: `loc_dm` is being REMOVED** (field,
> cfg key **and** binary TLV) — if the app reads it, it must stop. **QA writes that file; a coder never edits it —
> report what is owed instead.**

> ★★★★ **START HERE IF YOU ARE PICKING THIS UP: `docs/2026-08-01-agent-handover.md`** — state, the open queue in
> priority order, the rulings that must not be re-litigated, and the method that earned its place. **It supersedes the
> 07-31 handover.**
>
> ★★★ **For the wider picture — open topics, the four spec arcs, pending owner decisions — read
> `docs/2026-07-31-agent-handover.md`.** This file is the bug index; that one is the map.

## Historical priority and owner rulings — preserved from 2026-07-31

This section records the ordering and rationale that governed the 2026-07-31 work. It is historical context, not
the current queue; use the checklist above for present status.

★ **TIER 1 IS EMPTY.** B0 was the last live leak and it closed 2026-07-31. **Nothing remaining in this register
blocks functionality** — it is all quality, telemetry, plane-parity and dedup. The owner has therefore pivoted to the
**peer address book**, and this file is now a backlog rather than a queue.

**The order (owner-chosen, after a QA triage):**
1. ~~**B4**~~ ✅ **CLOSED 2026-07-31** — and it yielded **B24/B25**; B25 is a candidate **I2 breach**, unmeasured
2. **B17** — ★ the only remaining **device-destructive** entry: `team 4294967296` **joins garbage team `0xFFFFFFFF`**
   on the 32-bit boards. One range check. ✅ **CLOSED** — ⚠ but see **B27**: the same family is still live on `cfg set team_id`.
3. **B26 / NV1** — ★ **owner-queued 2026-07-31 BEFORE AB1**: factor the NV backend's 6-times-duplicated blob validation
   **above** the `#if`, so it is natively testable — which is what makes AB1's "v1-blob rejection test" runnable at all.
   **Load side + primitives ONLY; `save`'s change-detection stays untouched** (see the entry — that is the trap).
4. ~~**B27**~~ ✅ **CLOSED** — removed; ΔFlash negative on all three boards. *(was: owner-ruled REMOVE, not guard)* — it
   deletes a forked surface. **Remove the write, KEEP every read** — see the entry; tag `0x12` is **not** retired.
5. ~~**B28**~~ ✅ **CLOSED** — enforced at 3 points + 2 refusals; 36/36 byte-identical. *(was: owner-ruled auto-set `is_mobile`)* (two enforcement points, one-directional, reported not silent — see the entry)
6. **AB1 → AB2 → AB3** **→ AB4 (DM source)** — ★★ **OWNER RULING 2026-07-31: finish the address book FULLY first; channel
   crypt is SPEC-ONLY for now.** ⇒ **AB4 is RESCOPED, not blocked:** its **DM** location source is **live today** (CL3
   shipped `send -l`; the receive path already parses, authenticates and emits the position — only *retention* is
   missing), and it is the **better-authenticated** half (pairwise, not group). The **channel** source is the part that
   needs CL2 and is marked `✖ MISSING` with CL2 as its trigger. ★ Build `loc_src` (`peer`|`team`) from the start so CL2
   later adds a *source*, not a *schema change*. ⚠ **AB4 moves `sizeof(Node)`** (256 B ring) ⇒ D2 in full.
   in this register touches them. (⚠ **B18 is worth taking before AB3**, which rewires `hashof`/`nameof` onto the view:
   better than building the view over a known-wrong read path.)
7. **B22 → CL2 → AB4** ★★ **CL2 NOW CARRIES A WIRE DECISION (owner correction 2026-07-31):** `send_channel -t -l -e` is wanted, so
   T-K2's `[inner_type u8]` — an XOR of text-or-location — **cannot express it** and must become a **FLAGS byte**
   (`bit0` text, `bit1` location), with **`pack_loc6` (6 B)** not the 8 sketched. **Settle it when CL2 builds; afterwards it
   is a wire change.** See the channel-crypt spec **§2.2.1** + **open decision O6** (what `-t -l` without `-e` refuses on). — ★★ **AB4 (retained location) is GATED ON CL2, and CL2 IS NOT BUILT:** `channel_flavor_crypted`
   / `team_channel_crypt` / `team_channel_no_key` have **zero hits in the tree** (QA-verified 2026-07-31). T-K1/T-K1b/T-K3
   built the team **keypair**; **nothing seals a channel message with it.** The O5 ruling makes the **team content key the
   trust anchor** for a stored location — so building AB4 first would either ship a setter with no live source, or trust a
   **plaintext** post, which that ruling rejects. ⚠ **Take B22 immediately before CL2:** while B22 is open, four scenarios'
   team-channel asserts validate behaviour **metal does not have**, so CL2 would be gated against a lying corpus.

★★ **NEW 2026-08-01 — B38 / B39 / B40: ONE SLICE, and it is a PREREQUISITE, not backlog.** All three are channel-origination
outcome bugs found by the OLED-UI second review (archived at `docs/archive/2026-08-01-onboard-oled-ui-second-review.md`).
They are grouped because **B38 and B40 touch the same struct and the same two emit sites**, and B39 is the same seam one
level up. **Owner handed these to an independent agent 2026-08-01.**
- **B38 is the functional one:** a team channel post can never report `relayed=true`, so its outcome is a false negative
  for *every* consumer — the companion app today, and the OLED distress call the moment it exists.
- **Take them before the OLED Phase A plan** (`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`): that plan's
  emergency path correlates on `channel_sent.ctr` (B40) and counts attempts on the synchronous result (B39), and its
  `PICKED UP` state is unreachable without B38. The plan's "no core prerequisite remains" line is **superseded by these**.
- ⚠ **B38/B40 change an EMITTED VALUE** ⇒ expect a re-anchor on channel-carrying scenarios; own slice, own commit (C4),
  and pin `sizeof(ChannelReofferPending)`/`sizeof(Node)` with `static_assert` rather than assuming the reorder is free (D2).

⚠ **Two rulings owed, both OUTSIDE this file, and one is time-critical:**
- **O3 must be ruled BEFORE CL2** (not after): `set_team_id` deliberately does not clear the team channel key, so a
  `team <other>` switch leaves the **previous team's key** in place. Inert today — **the moment CL2 seals, a switched
  member seals for its new team under the old key.**
- **O4 is a live security exposure, not a watch-item:** `team exportkey` prints the team **private** key on **any**
  transport including BLE, which has no auth gate — and under the export ruling it is *the only* control protecting that
  key. Shipped since T-K1b. **Not blocking, but more serious than anything parked below.**

**PARKED with reasons (do not pick these up without a new ruling):** B5/B6/B7 team-plane quality — the primary flood
still delivers; B5 bites at scale, not in a hiking group · **B20/B21** the worst *class* (a send failing with **no**
`send_failed`) but the narrowest *reach* (body 215–216 and ≥237 B); they pair into one "no silent send failures" slice
when wanted · B8 an unmeasured counterfactual · B9–B16/B19/B23 telemetry labels, a dead test command, sim grammar, dedup
— ★ **and the dedup entries have no pressure behind them: `gateway` flash is 54.9%, RAM 80.8%. RAM is the constraint to
watch, not flash** · D1 has an explicit trigger that has not tripped.

---

## 0. ★★ BEFORE YOU TOUCH ANYTHING — the dispatch contract

⚠ **This section exists because the register FAILED its own test on 2026-07-30.** Grepped for the ten things a
dispatched coder needs, it scored **zero on all ten**. It was an index for a coordinator who already knew the
gate; an agent handed only the file above would have reproduced every failure this arc spent itself finding.
**If you are picking up an entry, this section is not optional reading.**

**Read first, in this order:**
1. **`docs/2026-07-26-slice-gate-method.md`** — this **IS** the gate. §E (the poison probe) and §D4 (boards) are
   the two hardest-earned parts.
2. **`CLAUDE.md`** — **C1** (one concern per slice: fixing an adjacent bug is a *separate* slice), **C2** (fail
   loud, no unagreed fallback), **C3** (respect the planes), U1/U2/U3, **V1** (verify against code, never a
   comment — see the note below), D1–D4.
3. **The `BASELINE.md` note named in your entry.** The evidence, the probe matrix and the reason the previous
   slice declined all live there. Do not re-derive them.

**Hard rules, each earned:**
- **QA-owned — do NOT touch:** `simulation/BASELINE.md`, `docs/*.md`, `ios-companion/*`, `tools/*`, and
  `simulation/*.json` **unless your task explicitly grants it**. Report what they need; QA writes them.
- **Never `git commit` / `add` / `stash` / `checkout --` / `checkout-index`, or offer to** (D4). To undo your own
  edit, restore from a snapshot **you** took, or `git show HEAD:path > path`. ★ Two coders have broken this; both
  recovered only because they had their own snapshot. **Snapshot before probing.**
- ★ **`rm` the native binary before every build**, and run it directly — `pio test -e native` **misreports "0 test
  cases"**, and a failed build leaves the previous binary in place. **Eight slices in this arc were bitten by a
  stale artifact.** Cross-check the event count of anything you re-run.
- **Boards: THREE envs** — `gateway`, `xiao_sx1262`, `xiao_esp32s3`. ★ The six-env escalation is **your decision
  after a compile-only `sizeof(Node)` measurement**, never a grant made in advance; **push back on any brief that
  starts at six.** ⚠ **Do not chase flash deltas** — there is a reproducible **±32 B noise floor** from
  `__DATE__`/`__TIME__` baked at `src/fw_main.cpp:420` + `src/firmware_commands.cpp:261`. **RAM is the trustworthy
  number.** ★ **Sharper instrument, found 2026-07-31: `handle_team` is ABSENT from the `gateway` ELF** (`MR_FEAT_TEAM 0`
  garbage-collects it) ⇒ for team-console work, **`gateway` ΔFlash 0 is a LINK-LEVEL inertness proof, not a noise reading.**
  Use **cold, equal-length build dirs** — a warm one produced 18628-vs-10617 and read exactly like a
  real delta.
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move.** If it does, stop and report — do not re-anchor it.
- ★★ **RE-RUN THE FOUR DETECTOR PROBES AND REPORT THE NUMBERS. Hard item — a slice that omits them is NOT gated**
  (this rule exists because a slice omitted them and QA accepted the report):

  | probe | how | expect |
  |---|---|---|
  | **P-T7** | re-add `is_team_peer(origin) &&` at the team DATA-origin learn (`node_mac_rx.cpp`) | `s38` **474 ev, 8 of 16** |
  | **P-T1** | revert the `send -t` precondition **in `Node::on_command`'s `CmdKind::send` arm — FIND IT BY CONTENT, the line number drifts** (`node.cpp` ~1309 → 1339 → **1359** as of `§o3-key-lifetime`; grep the `plane == Plane::TEAM &&` conjunct) to **`!is_team_peer(c.u.send.dst_id)`** (⚠ **the literal `dst` does NOT compile — there is no such local in that arm**; §b39 hit this) — ★ **KEEP the `plane == Plane::TEAM &&` conjunct**; the bare form gives **1587 ev / 24 FAIL**, not the expected numbers — ⚠ **NOT** `node_mac.cpp`'s ack-gate fix, which is a no-op on s35a and has cost a coder a run | `s35a` **1892 ev, 20 FAIL**, incl. `actual_reply="OK error ctr=0 depth=0"` |
  | **P-T6A** | revert T6's team arm in `stamp_origin` (`node.h`) | `s37` **851 ev, 12 of 36** |
  | **P-T6A + P-T7** | both | `s37` **917 ev, 16 of 36** |

- **Poison-probe every site you change, with a SAME-SITE control.** ★ **A 0/N result means "the corpus cannot
  reach it", NEVER "it is inert"** — prove reachability by tracing the line *immediately above* your site.
  ⚠ And for a **comparison-only** score (anything consumed relatively), a **uniform-offset** poison is an invalid
  control — it cancels. Use a **differential** one.
- ★★ **The premises in your task are HYPOTHESES.** Every brief in this arc contained at least one wrong premise;
  one contained four. **Disproving one is the most valuable thing you can return** — including "this bug is
  narrower/louder than described" and "the reference implementation I was told to copy is itself broken", both of
  which have happened. ⚠ **V1 applies to comments too: verifying that a comment exists is not verifying that it
  is true.** A drifted note cost a whole extra defective site on 2026-07-30.
- ⚠⚠ **RESTORING A PROBED FILE IS NOT ENOUGH — PROVE THE REBUILD HAPPENED. Three incidents now, and the two build
  systems need OPPOSITE fixes:** **ninja** (the sim) keys on **mtime**, so `cp -a`/`cp -p` restore a file the build then
  **skips** ⇒ **`touch` it after restoring**; **PlatformIO** keys on a **content signature**, so `touch` does NOTHING
  ⇒ **delete the `.o`.** ★ **In both cases the control is the same: the rebuilt binary's md5 must return to its clean
  value.** `§cl2b` ran a whole probe pass on contaminated binaries and caught it only because the post-restore corpus
  showed 5 phantom movers.
- ★★ **DURABLE OUTPUT GOES IN YOUR REPORT, NEVER ONLY IN A SCRATCHPAD** — a proven 33-assert scenario was **LOST** this
  way. ⚠⚠ **AND THE SESSION SCRATCHPAD IS SHARED BETWEEN CONCURRENT SESSIONS** (proven 2026-07-31: another agent's
  `before/`/`after/`/`pristine/` directories were already present, and its files appeared **mid-slice**). ⇒ **prefix EVERY
  scratchpad path with your slice tag** (`nv1-before/`, never `before/`), and **if a comparison looks impossible, suspect
  the shared directory before you suspect the tree.** Same family as the `cp -a` preserved-mtime incident.
- **Report as:** INVENTORY CONFIRMED / DESIGN / COVERAGE / GATE / DEVIATIONS / MINE-VS-THEIRS. Report failures
  with their output; if you skipped a step, say so.

### 0.1 Expected corpus outcome per entry — so a moved stream is interpretable

★ **If a scenario moves when this table says byte-identical, that is a FINDING, not a re-anchor:** it means a
scenario was relying on the broken behaviour. Attribute it and report before proceeding.

| entry | expect | why |
|---|---|---|
| **B5** | **re-anchor likely** | changes a live frame's contents |
| **B6, B7** | **byte-identical or small** | both are currently-zeroed bypasses |
| **B8** | **measurement only** — no fix expected until it is answered |
| **B9** | ★ **value-only re-anchor of EVERY team scenario** | `slot` is in the stream |
| **B10** | **re-anchor of `s37`** | removing the dead command is a stream edit |
| **B11–B15** | **byte-identical** | telemetry/comments/`src/`-only |
| **B16** | ⚠ **NOT `s27` only — 12 scenario JSONs use `send_layer`** | QA-grepped 2026-07-31: s09 ×2, s10, s15 ×2, s16, s17, s27, s31, s32, s33, s37. The old row said “it is the sole user” and was wrong; probe E moved **11** of them |
| **D1** | ★ **inert on 34/36 — but it DISARMS `s35a`/`s38`.** Read the entry before starting |

---

## Original closed index — 2026-07-31 pass

These entries were already closed when this register was established. Later closures remain in the detailed records
and are checked in the current-status section above. Each row names the original evidence tag in
`simulation/BASELINE.md`.

| # | the defect | the fix | evidence |
|---|---|---|---|
| ~~**B0**~~ | ★★ a plaintext DM aired the node's COORDINATES IN THE CLEAR (`loc_in_dm` had no crypt check) | location became a per-send `-l` that REFUSES unless sealed; `cfg set loc_dm` removed across 12 surfaces; `kVersion` 22→23 | `§loc-per-send` |
| ~~**B1**~~ | `team 88A672BA` (hex without `0x`) silently joined team **88** | the whole token must parse | `§team-target-whole` |
| ~~**B2**~~ | a team-scoped H answer wrote the **static** `_id_bind` (I2 breach) | do not bind at all on the team plane — s34 `no_route` 8 → 0 | `§id-bind-plane` |
| ~~**B3**~~ | `reqpubkey`'s plane diverged sim-vs-metal (sim left it AUTO) | the sim mirrors the console; s22 gained `-t` | `§sim-plane-parity B3` |
| ~~**B4**~~ | `schedule_sync_response` read the **static** route count on both planes | the plane is passed by the caller — and there were **three** defective readers, not two | `§sync-response-plane` |
| ~~**B17**~~ | an out-of-range `team <id>` joined garbage `0xFFFFFFFF` on the 32-bit boards | `errno == ERANGE` **and** a width guard — one arm per ABI | `§team-target-range` |
| ~~**B22**~~ | a plain `send_channel` SUCCEEDED in the sim and was REFUSED on metal | metal is the reference; the sim's `team_member` heuristic is gone and 10 scenario posts gained `-t` | `§b22` |
| ~~**B26**~~ | the NV backend duplicated its blob validation (16 size checks, 30 definitions) | factored **above** the `#if` so it is natively testable — and it is AB1's forcing function | `§nv1` |
| ~~**B27**~~ | `cfg set team_id` had **none** of the three guards `team <id>` carries | the key is REMOVED; the read surfaces (incl. TLV `0x12`) all stay | `§team-id-cfg-removal` |
| ~~**B28**~~ | `team_id != 0` did not imply `is_mobile` | enforced at **three** points + O2/R4 refusals; the invariant already existed as a build `#error` | `§role-model` |
| ~~**B29**~~ | ★★★ `key_hash_for_id` **never returned on a miss** — an infinite loop, and UB | one line: `uint16_t i < _id_bind_n`, which also stopped it returning an EVICTED binding | `§idbind-loop` |
| ~~**B32 + B33**~~ | the console discarded every `CmdCode`, and `hashof`'s advice was circular | the reply names its reason; the advice names remedies that work — and the fix was flash-NEGATIVE | `§err-reason` |
| ~~**B41**~~ | the sim's push bridge had no `channel_sent` arm, so `relayed` never reached a stream | one arm — and it CONFIRMED B38: team plane **0 `true` / 9 `false`** | `§b41` |

---

## Detailed records — open, parked, and subsequently closed

Full detail remains because these records carry implementation traps, measurements and rejected alternatives. Some
entries were fixed after being written; their original work order remains underneath the current status checklist so
references and evidence are not lost.

### B5 — `channel_pull` carries no `team_id`, and airs `src = _node_id`
⇒ it **cannot** receive the mixed-leaf exemption that `team_sync` got (there is nothing to scope on), so a
cross-nibble teammate never answers a channel repair; and it leaks the static id where the team plane expects a
team id. Note: `T4`.

### B6 — team `budget_penalty_q4` is a zeroed bypass with no team mirror
`node_routing.cpp:159` reads `_neighbor_budget_tier`, an R4.2 `node_id`-keyed map that has no team twin, so the
team plane silently skips anti-spam-tier scoring. Named in-source so the asymmetry is visible. Note: `T5`.

### B7 — team **slow-reprobe** does not exist
`node_cascade.cpp:172` reads `_link_bidi[from_next]`; static-only *by construction* (the `pt.plane == TEAM` branch
returns above). A team version needs `_link_reprobe_last_ms`, **another 2048 B array** — hence deferred, not
forgotten. Note: `T5`.

### B8 — ⚠ **UNMEASURED:** does a relay forward a frame whose `src` is 0?
The one path a **not-yet-DAD'd** member could take. T7's harness control was **vacuous**, so the question is open
rather than answered. If it *does* forward, the T6/T7 coupling becomes live rather than counterfactual. Notes:
`T7`, `T8`.

---

### B9 — `rt_update.slot` is **wrong on the team DV path**
`node_beacon.cpp:876/879` label a beacon-DV merge `"primary"`/`"alt"` **regardless of which table was merged**.
Measured **~120 mislabelled vs 9 correct** corpus-wide. **Sim-only telemetry, so not a firmware defect** — but it
invalidates any analysis keyed on `slot`, and it has already cost **s37 and s38** an explicit in-file workaround.
⚠ **Not free:** `slot` is in the stream ⇒ fixing it is a **value-only re-anchor of every team scenario.**

### B10 — `s37`'s `routes` command is dead
The sim has **no `routes` verb** — it replies `ERROR: unparsed command`, and the `_desc` claim that asserts 16/17
read its output **was never true**. Left in place because removing it is a stream edit ⇒ a re-anchor for no gain.
Note: `T8`.

### B11 — `frame_trace.h`'s type/opcode switches are incomplete
The DATA-type switch (`:76`) names only **1..5**, so 6..19 print as bare numbers; the Q switch omits opcode 2
(`CONFIG_PULL`). ★ **`-Wswitch` cannot help** — both switch a raw `uint8_t`, not the enum. Fixing one gap at a
time was correctly refused as a drive-by; fix the class or leave it.

### B12 — a **three-way** duplicate of the seal-or-refuse logic
`want_crypt` + `build_sealed_relay_body` + the outcome→reason mapping now appear at `node_hashlocate.cpp:1075`,
`node.cpp:1408` and `node_mac.cpp:462`. ⚠ **Read `§deleg-ack-xl`'s design note before deduping**: collapsing the
pair was *rejected* there because the duplication **is** the local asymmetry detector — the very thing whose
absence hid a silent-drop bug. Dedup carefully or not at all.

### B13 — `liveness_penalty_q4`'s inline scan is duplicated by `team_liveness_find`
Exactly one scan to fold. Marked ✖ MISSING/C1 in-source. Note: `T5`.

### B14 — `node.h:1016` comment drift
Claims `sort_candidates` threads "wire-only degraded". **It never touches `degraded`.** Pre-existing, outside any
recent hunk. Note: `T5`.

### B15 — `enc_cfg`'s binary TLV lacks `team_ch_key`
Trivially additive on the `TAG_CFG_TEAM_HOP_CAP = 0x1C` precedent, but it is the **remote-admin** path, which is
mid-redesign — hence not taken. Note: `T-K1b`.

### B16 — `send_layer`'s sim-vs-metal **grammar** still diverges on four axes
Crypt capability is now aligned, but: argument **order** (sim `<layer> <hash> <text>` vs console
`<0xhash> <l1,l2,…> "<text>"`), **radix** (sim bare decimal vs console demands `0x`), **path arity** (sim single
layer vs console comma-list), **quoting** (sim unquoted vs console mandatory). Sim also lacks `-K`/`-t`. **Only
`s27` uses it — 6 lines, all currently correct.** Note: `§xl-crypt`.

---

### B18 — the **read-side** twin of B2: a relay answers a team H from its static `_id_bind` · NEW 2026-07-31
Fixing B2 removed the only corpus-reachable *use* of the read side, which is how it surfaced: a **relay** was
answering a repeat team-scoped H out of its **static** `_id_bind` instead of forwarding. Delivery is preserved
(the owner answers, ~1.5–2 s later — that is the s24/s25/s26 event delta), so this is **correctness, not loss**.
Marked ✖ MISSING at `handle_h`. ⇒ **belongs to D2, the read-path plane audit.** Note: `REG-B1/B2`.

### B19 — `deleg_ack_put` is inlined at **8 sites**, costing ≈4 KB · ★ **FOLD INTO B12, do not take alone**
The function is **584 B** compiled and has **8 call sites** (7 in `node_hashlocate.cpp`, 1 in `node_mac_rx.cpp`), with
no LTO ⇒ ≈**4.7 KB** of duplicated code where one copy + 8 call sequences would be ≈0.7 KB. **Recoverable ≈ 4 KB**
(not the 1.8 KB I first quoted — that was only the 3 sites `§deleg-ack-xl` *added*; `noinline` also de-duplicates the
5 pre-existing copies).
★★ **Why the inlining buys nothing here: the cost centre is `_hal.now()`, a VIRTUAL call on `IHal` that inlining
cannot optimise through.** Every copy still makes the indirect call, so 584 B buys the removal of one `bl` and a few
register moves — on a **cold** path (a delegated re-origination; 1–4 hits per scenario). `kDelegAckCap = 8`, so GCC
is unrolling an 8-iteration scan at each site.
⚠ **Flash is NOT the argument** — headroom is 54.8% / 59.9% / 35.6% used, so ≈4 KB is under 1%. The argument is a
large cold function duplicated eight times for **zero** speed gain.
★★ **DO NOT TAKE THIS ALONE.** `noinline` re-codegens **all eight** sites, which destroys the precise attribution
`§deleg-ack-xl` relied on (*"exactly 2 of 283 objects changed"* proved inertness on `gateway`). A one-token change
whose verification work dwarfs it is the wrong slice shape. ⇒ **Fold into B12** — the three-way seal dedup at
`node_hashlocate.cpp:1075` / `node.cpp:1408` / `node_mac.cpp:462`, which is **already a refactor of that file**,
already churning those objects, and already owes a flash investigation. **NOT B18** (a fix — C1 forbids folding a
refactor in). **Owner agreed 2026-07-31.**
⚠ Two caveats for whoever takes it: **measure on the BOARD build** (`MR_EMIT` is device-stripped, so 584 B is the
board figure and native would mislead); and the result is valid **only for the build configuration measured** — LTO
is off today (`platformio.ini` has no `-flto`) and GCC does honour `noinline` under it, but that is the same trap as
the `__DATE__` flash noise.

### B20 — a CRYPTED DM in a 2-byte band fails with **NO `send_failed` AT ALL** · NEW 2026-07-31
`max_payload_bytes_hard_cap` subtracts `data_inner_overhead = 6` (a **4-byte** MAC), but a **CRYPTED** frame’s trailer is **8**
⇒ the cap is **2 B too generous for a sealed DM**. For `body_len` **215–216** (209–210 with `-l`) `e2e_seal_inner` succeeds
(inner ≤ 241), then `pack_data` refuses at **TX time** and **nothing is pushed to the app** — the send simply vanishes.
Found by the `-l` fit sweep, **not fixed** (C1); marked at the site. ⚠ **Fix the CAP, not the gate** — re-deriving the sealed
bound at a call site would fork a second copy of the seal’s size arithmetic (U1). Note: `LOC-PER-SEND`.

### B21 — an oversize DM `≥ 237 B` emits `e2e_no_pubkey` with **no `send_failed`** · NEW 2026-07-31
At `body_len ≥ 237` the DST_HASH fit-check drops the flag, so the `!(item.flags & DATA_FLAG_DST_HASH)` branch reports
`e2e_no_pubkey` — **a misleading reason** (the key is fine; the body is too big) — and returns **without**
`push_send_failed`, so the app is told nothing. Same sweep, same slice, deliberately untouched. Note: `LOC-PER-SEND`.

### B23 — the `resolve` verb's surface: **`Plane::AUTO` IS reachable on metal**, and `u.resolve.hard` is dead · NEW 2026-07-31
★★ **Two defects at one site, and the first one falsifies a claim this project has repeated:** the console verb
`resolve <0xhash> [hard]` (`console_parse.cpp:149-160`) assigns **no plane**, and `request_resolve`
(`node_hashlocate.cpp:1561`) calls `emit_hash_query(key_hash32, hard)` — **2 args** — so `node.h:809`'s default
`Plane plane = Plane::AUTO` applies. It is dispatched on hardware (`fw_main.cpp:472` USB, `:799` BLE). ⇒ *"AUTO is
simulator-only"* is **FALSE**; the defensible claim is **"AUTO is never carried in a `Command` plane field."**
BASELINE lines ~299/~320 are corrected. Three more AUTO-default `emit_hash_query` sites are metal-live:
`node_join.cpp:468` (DAD discriminator), `node_mac_rx.cpp:1361` (RX re-flood), `node.cpp:1495` (`send_layer` park arm —
console-unreachable, **unverified**). **(b)** `u.resolve.hard` is **dead** on the `reqpubkey` path — `node.cpp:1382`
hard-codes `/*hard=*/true`; only `CmdKind::resolve` reads it. Proven by a probe that returned **0/36** and was correctly
reported as *a field that lies*, not a weak probe. ⚠ **Decide whether AUTO-on-`resolve` is intended** before "fixing"
either half. Note: `SIM-PLANE-PARITY B3`.

### B24 — `send_req_sync_q`'s `q_tx{rt_total}` is now **inconsistent with** the plane-aware responder · NEW 2026-07-31
`node_query.cpp:106` reports the **static** count on both planes **deliberately** (documented at `:103-105`, to avoid
rewriting every static `q_tx` line). After B4 the **responder** side names its plane while the **requester** side does
not — the two halves of one exchange now disagree. ★ **The deferral itself still holds:** the route-rich skip at `:89`
was **V1-verified unreachable on the team plane** (its one team caller `node_mac.cpp:993` always passes `force=true`,
and `:75` forbids a mobile originating a static pull). ⇒ **telemetry-only, and it will re-anchor every scenario carrying
a `q_tx`** — which is why it is deferred, not forgotten. Note: `§sync-response-plane`.

### B25 — ⚠ **UNMEASURED:** does a team-adopted member answer a **STATIC** `req_sync` with a **TEAM-plane** beacon? · NEW 2026-07-31
★★ **Mechanism QA-verified in source:** `emit_beacon`'s plane self-selection is
`const bool team_active = _cfg.is_mobile && _cfg.team_id != 0 && team_local_id() != 0;` (`node_beacon.cpp:410`), and
`team_emit = team_active` (`:431`) picks `src_rt = _rt_team` and `src = team_local_id()`. **That is a property of the
NODE, not of the pull being answered** — so a team-adopted member replying to a **static** `req_sync` would air a
**team-plane** table to a **static** requester, which installs it in `_rt`. ⇒ **a candidate I2 breach** (team ids in a
static table), the class B2/B18/s38-assert-12 exist for.
★ **Why it is UNMEASURED and not simply open:** B4 proved **12 static-plane sync responses in 5 scenarios come from nodes
holding team routes**, so the *responder* side is live — but `sync_response_*` emits carry **no plane**, so the stream
cannot say whether those repliers were `team_active`. **The measurement: a temporary plane discriminator on the beacon
emit** (the method B4 used), then check whether any static requester installs the replied ids. **Do not fix before
measuring** — if it is unreachable, a guard here is decoration. Note: `§sync-response-plane`.

### B30 — `team_id_of_key` silently first-matches an ALIASED hash · FIXED 2026-08-02
The old `team_id_of_key` returned the **first** id whose team-key row carried the hash. ★ **`_team_keys` genuinely
can alias:** `team_key_set` upserts **by id only** and never dedups by hash, so a teammate that re-runs team-DAD leaves
its old `(id, hash)` row live for the full 48 h TTL. ⇒ on the **live plaintext send-by-hash path** the node may pick the
**stale** id — exactly the silent-pick the address-book spec §2.1 forbids for the view.
★ **CLOSED 2026-08-02 by `§B30 send`.** The AB3 resolver is now the one shared reverse policy: it accepts a confidence
floor, filters out below-floor rows, then picks max `last_seen_ms` among the qualifying aliases. Address-book callers
pass `claimed`; the live send reader retains its default `authoritative` floor. Thus a fresher claim remains visible
and labelled in the view but cannot shadow an older authoritative send target.
★ **Bench state pinned verbatim:** authoritative aliases `245 → 0x7B18ADA2` then `86 → 0x7B18ADA2`; a sealed type-19
TEAM grant must queue with `dst=86`, never stale `245`. The inverse-hash and command-path tests both assert it.
**Poison control:** inverting the freshness comparison fails three cases, including `§B30 send` at `dst == 86`.
★★ **QA REVIEW 2026-08-02 — GO, with one in-source claim CORRECTED and one standing cost now on the record.**
① `node_hashlocate.cpp` (the `on_hash_bind_snoop` header) asserted *"a repeat `send -t 0x<hash>` to an unheard
teammate now resolves from cache instead of re-flooding the locate."* **False, and it cannot ever be true as built:**
the send reader is at the default `authoritative` floor, both ingest sites write `claimed` **unconditionally**, so the
lookup misses, falls to the `§F-TR-1` flood, and the answer lands `claimed` again — **no convergence.** Corrected in
place with the five-step chain. ⇒ ★ **the team ingest buys the VIEW (`hashof`/`peers all` can name and label an unheard
teammate), NOT the send.** ② ⚠ **The accepted cost: a repeat `send -t 0x<hash>` to a claim-only teammate re-floods
every time** (rate-limited only by `hash_query_seen_ttl_ms`). **DO NOT lower the floor to reclaim that airtime — it
reverses spec §3-D7** (a false claimed binding does not fail closed; L2c *forwards* the DM to the owner of the false
hash). The convergent cure is a first-hand beacon or a QR. ③ A `⚠` now sits on `team_id_of_key_freshest`'s declaration:
its default is the permissive `claimed` while the `team_id_of_key` wrapper's is the strict `authoritative`, so a future
**send**-path caller reaching for the raw resolver would silently route on a claim. All callers correct today.
④ **Gate coverage was 2 envs, not 3** (`xiao_esp32s3` + `gateway`); QA built the missing `xiao_sx1262` — **SUCCESS, RAM
70.9 % / 167044 B = S3's 166980 + S4b's +64 exactly, so B30 itself is +0 RAM.** Native **1149/72681/0** and s18
`1cd21235`/271629 with 0 assertion failures both QA-reproduced.
**Gate:** native **1149/72681/0**; target `xiao_esp32s3` and feature-off `gateway` link; **36/36** streams are byte-identical to
clean HEAD with zero assertion failures, and s18 remains `1cd21235` / 271629.
Note: `§ab3`, `§B30 send`.

★★ **2026-07-31: TIER 1 IS NOW EMPTY — B0, the last live leak, is CLOSED.** Both Tier-1 bugs found *inside* this arc were fixed: the cross-layer cleartext downgrade
(`§xl-crypt`, `65833f2`) and the silently-dropped delegated sealed DM (`§deleg-ack-xl`, `442809b`).

### B31 — `key_hash_for_id` is neither **authoritative**- nor **TTL**-gated · NEW 2026-07-31
After `§idbind-loop` it shares its loop idiom with `key_hash_of_id`, but **not that sibling's gating** — so it can answer
from a `claimed` (unvouched) or TTL-lapsed `_id_bind` row. ★ **Residual is narrow and that is why it was scoped out:** the
hash it returns only feeds `peer_key_find`, which **ages independently**, and `id_bind_set` maintains the id↔hash
bijection — so a stale answer degrades to a failed lookup, not to a wrong peer. Recorded **in-source at `node.h`** as a
deliberate divergence rather than left silent. ⚠ **Decide the intent before "fixing":** if `rcmd` should refuse an
unvouched target, that is a **policy** change on the remote-admin path (mid-redesign — see B15). Note: `§idbind-loop`.

### B34 — ★★★ the SIMULATOR drops every refusal reason, **7 times over** ⇒ fail-loud refusals are corpus-untestable BY CONSTRUCTION · NEW 2026-07-31
`orchestrator/runtime/NodeRuntimeWrapper.cpp` lines **656, 818, 843, 872, 901, 941, 964** each carry
`(r.code == CmdCode::queued) ? "queued" : "error"`. ⇒ **no scenario can assert WHICH refusal happened.**
★★ **This is the structural explanation for a pattern that has cost this arc real coverage:** every fail-loud refusal
added since 2026-07-29 — `unsealable`, `no_location`, `role_refused`, `err_no_binding`, `unknown_key`, `too_long` — is
**untestable in the corpus by construction, not merely unexercised.** Slice after slice reported *"native or the bench
only"*; **this is why.**
⚠⚠ **Fixing it moves a DETECTOR PROBE: `OK error ctr=0 depth=0` is P-T1's own expected signature** (register §0), so the
fix re-anchors scenarios **and** re-baselines a documented probe expectation ⇒ **it must be its own slice, and §0's P-T1
row must be updated in the same commit.** ★ **Payoff: it would make the whole `err_*` family assertable in scenarios** —
the single biggest coverage gain available to this corpus. Note: `§err-reason`.

### B35 — `ingest_channel_m`'s self-skip is **PLANE-BLIND** ⇒ a teammate's posts can be SILENTLY SWALLOWED · NEW 2026-08-01
`ingest_channel_m:252` skips on `origin != _node_id` — comparing a **TEAM-plane origin** against the **STATIC node id**.
On a **registered (dual) member** those are different id spaces, so a teammate whose `team_local_id` numerically equals
our static `node_id` has its channel posts **silently dropped: no inbox row, no push, and (since `§cl2b`) no retained
position — while the flood still relays them.** §18 numeric-collision class; **predates CL2a**, found by `§cl2b`, not
fixed (C1). ⚠ **Silent** is the severity: the sender sees a normal post, the receiver sees nothing, and no telemetry
names it. Note: `§cl2b`.

### B36 — a located DM's position reaches **no app surface** — `send -l` is only visible via the address book · NEW 2026-08-01
`Push::has_location/lat_e7/lon_e7` are set at `node_mac_rx.cpp:1196` and **consumed by nothing**: `write_push`'s
`msg_recv` arm emits no coordinates, the console renderer prints none, `record_dm` has no location field. **QA-verified:**
the only `has_location` consumer in `console_json.cpp` is **`write_peer_row`** — AB4's peers row, a different struct.
⇒ ★ **CL3 shipped `send -l` and its position becomes visible ONLY through the address book (`§ab4`).** `§cl2b` mirrored
that deliberately rather than forking a richer channel surface, so **the per-message JSON carries no coordinates on
EITHER plane** — consistent, but probably not what an app author expects. ⚠ **Fixing it is a contract addition on BOTH
planes and its own slice** — decide whether a position belongs on the message or only on the contact. Note: `§cl2b`.

### B37 — ★★ a **symbolic-only** assertion on a WIRE CONSTANT is not coverage · CLASS, found 2026-08-01
`§cl2c`'s poison P1 renumbered `channel_inner_flag_source` **0x04 → 0x08** and **the entire 1091-case suite stayed
green** — every assertion named the flag **symbolically**, so the KAT was **self-referential about the one thing two
independently-built nodes must agree on.** A node built before the renumber and one built after would have failed to
interoperate **with all tests passing on both**. Fixed for the channel inner (numeric pins on every flag, both widths,
and a literal `inner[0]` in the KAT); ⚠ **CL2b's values were equally unpinned.**
⚠⚠ **QA DOWNGRADED THIS 2026-08-01, ON THE OWNER'S CHALLENGE — the original framing OVERSOLD it.** I wrote that
*"two independently-built nodes would fail to interoperate with all tests passing."* **That scenario is nearly
unreachable here:** it needs two nodes on **different builds**, and this project is **undeployed, reflashed all-at-once,
and wire changes are FREE by M3** — so a wire constant changing is **expected**, not a hazard.
★★ **What IS true is different, and more useful: NO TEST IN THIS PROJECT CAN DETECT A WIRE-FORMAT CHANGE AT ALL.**
**One `lus` executable drives both ends of every simulated link** and the native suite compiles one copy of the
constants, so a renumber moves both sides identically. ⇒ **every "byte-identical" result this file relies on is
byte-identical AGAINST ITSELF: the corpus validates BEHAVIOUR, never FORMAT.** Our only real format checks are the
**KATs** — and `§cl2a`'s is the pattern to copy, because it recomputes key/nonce/AAD **from the spec wording** and opens
with raw `crypto_aead_unlock` instead of calling our own sealer.
★ **What a numeric pin actually buys: not bug prevention, a DELIBERATENESS TRIPWIRE** — it turns "someone silently
changed a wire number" into "someone had to edit a test that says `0x04`". Worth most **at the moment a constant is
CREATED**, because retrofitting one later means first establishing what the number *should* be, and by then the only
record is the code that may have drifted.
⇒ ★ **PRIORITY: NOT NOW. Pin on creation; do not sweep.** A sweep would touch ~161 constants for low yield while the
reflash-all assumption holds. **Two triggers make it real: the first time two boards run DIFFERENT builds, and
DEPLOYMENT** — at which point M3 also stops being true and this entry should be re-read together with it.
*(the original, overstated framing, kept as the record:)* ★★ **THE CLASS, and it is worth a sweep of its own:** `CHECK(x == kFlag)` proves the code is **self-consistent**; only
`CHECK(kFlag == 0x04)` proves it matches **the other end of the link.** ⇒ **audit every wire constant** — DATA flags,
`q_opcode`, frame types, `PushKind`/`SendFailReason` numeric values, the cfg TLV tags — **for a numeric pin**, and add
one where it is missing. A renumber is otherwise invisible to this project's entire gate. Note: `§cl2c`.

#### Related ruling — `relayed` stays as-is on a one-hop team
**Owner:** *keep `relayed`, accept `NOT HEARD` on 1-hop teams.* ⇒ **ACCEPTED BEHAVIOUR, not a defect.**
On a fully-1-hop team (3–10 co-located members — the actual hiking case) **nobody re-broadcasts**: the frugal
`{self + hops-1}` seed marks every neighbour covered, so `flood_forward_decision` goes silent and **there is nothing to
overhear.** Measured after `§b38-b40`: s22 **0 `true` / 1 `false`**, s29 **0 / 2**, with **every teammate having received
the post.** ⇒ `relayed=false` is **TRUTHFUL** — we observed no relay — and the honest alternative was rejected as
costing new airtime for a display nicety.
⚠⚠ **CONSEQUENCES, stated so they are not re-litigated:** (1) the **OLED emergency will spend its full 3-attempt budget
and display `NOT HEARD` on a 1-hop team, at 100 % delivery** — expected; (2) the shipped app's stop-and-back-off on
`relayed:false` fires there too, so **back-off must never be presented to the user as delivery failure**; (3) ★ **`relayed`
is NOT a delivery signal on either plane** — a real one needs a per-member ack, which the owner **declined**. ⇒ **anyone
proposing to "make `relayed` true when delivery succeeded" is reversing this ruling.** Evidence: `§b38-b40`.

### B38 — ★★★ a TEAM channel post could never report `relayed=true` · FIXED 2026-08-01
`channel_reoffer_confirm` (`node_channel.cpp:~1155`) returns **before** `emit_channel_sent(true, …)` when `rp.team`:
```
if (rp.team) return;                                   // keep re-offering for far members
emit_channel_sent(true, static_cast<uint16_t>(id & 0xff)); …   // <- unreachable on the team plane
```
The team carve-out is **correct in intent** (one near relay ≠ full coverage of a multi-hop chain, the s28 class) but it
also **discards the observation**. Retry exhaustion then emits `emit_channel_sent(false, …)` at `:~1131`. ⇒ **a team post
that WAS relayed by every teammate still ends `relayed=false`.** The only truthful outcome the channel plane can produce
is unreachable on the plane teams actually use, for **every** consumer — companion app included, not just the OLED UI.
★★★★ **SEVERITY RAISED 2026-08-01 by `§b41`: this is LIVE IN THE SHIPPED APP, not only the future OLED.**
`INBOX_SYNC_CONTRACT.md:502` — *"the app treats `channel_sent{relayed:false}` as stop-and-back-off (don't keep firing)"*
⇒ **the companion already backs off from EVERY SUCCESSFUL TEAM CHANNEL POST.** Measured: **0 `true` / 9 `false`** on the
team plane corpus-wide, with **every** post actually received by every teammate.
★ **Why it is severity-3:** the OLED emergency (spec `2026-07-31-onboard-oled-ui-design.md` §4) retries on
`relayed=false`, so a distress call would **always transmit its full 3-attempt budget and always display `NOT HEARD`,
even when the whole team received it.** A safety feature reporting failure on success.
★★★ **OWNER RULING 2026-08-01 — `relayed` ON A TEAM POST MEANS "FIRST RELAY ONLY". We cannot guarantee a full flood,
and the field must not be read as coverage.** ⇒ the fix emits the *observation* (**at least one relay was heard**), not
a completion claim. ⚠ **NAME IT IN THE CONTRACT AND IN-SOURCE:** the boolean already means "the flood completed" on the
NON-team plane, so after this fix **one field carries two meanings depending on the plane** — leave that unstated and
the false negative simply becomes a false positive on the same safety feature.
⚠⚠ **CONSEQUENCE THE OLED SPEC MUST RULE ON (QA flag, not a blocker):** today the emergency retries its **full**
3-attempt budget because `relayed` is always false. After the fix it will **stop at the first relay confirm**. For a
distress call, "one teammate heard me" may or may not be a reason to stop transmitting — **that is an OLED-spec
decision, not this slice's.**
★ **QA-VERIFIED IN SOURCE 2026-08-01:** `:1157`'s `if (rp.team) return;` sits **immediately before** `:1158`'s
`emit_channel_sent(true, …)`, and `:1131` is the only other emit (`false`) ⇒ **the `true` branch is structurally
unreachable on the team plane.** The entry is exactly right.
★★ **AND QA MEASURED WHAT IT COULD: the path is WELL EXERCISED but the OUTCOME IS INVISIBLE.** `channel_sent` fires as a
**Push** (not an `MR_EMIT`) **93 times across 9 scenarios** — s28 ×7, s22, s29, s34 among them — but the sim renders
`{"kind":"channel_sent","ctr":…,"dst":…}` **with no `relayed`**. ⇒ **B38 is corpus-blind, and B41 is the reason.**
**Fix shape (with B40):** remember the observation instead of discarding it — extend `ChannelReofferPending` with
`relay_seen`, keep the retries running, and emit the remembered truthful result once (either immediately on first
confirm, or at exhaustion). **Never emit a contradictory `relayed=false` for a post already confirmed relayed.**
⚠ `ChannelReofferPending` is 12 B (`node.h:~1238`); B40 adds a `uint16_t` and this adds a flag — a field **reorder**
should absorb both without growing `Node`, but that must be **pinned by `static_assert`, never assumed** (D2).
⚠ **Emitted-value change ⇒ expect a re-anchor** on any scenario with channel re-offers; give it its own slice (C4).
Found by the OLED-UI second review, `docs/archive/2026-08-01-onboard-oled-ui-second-review.md` §1. **UNMEASURED on
metal** — found in-source; the sim corpus should show it as a `channel_sent{relayed:false}` on a delivered team post.

### ~~B39~~ ✅ **CLOSED 2026-08-01** (`§b39-ctr0`) — the interim landed, and **the entry's premise was too strong**
★ Comments at **four** sites + a test pinning `next_ctr`'s no-zero invariant. Native 1093/71851 → **1094/71859** (+8 =
exactly the new test); corpus **36/36 `cmp`-identical** with a **bit-identical `lus`**; boards **ΔRAM 0 AND ΔFlash 0
exactly** — not even the ±32 B noise.
★★★ **THERE ARE THREE PRODUCERS OF `ctr == 0`, AND THE THIRD IS A SUCCESS.** `node.cpp:1565-1573`: on a **registered
mobile**, a plain/`-g` GLOBAL post takes `do_send_channel_delegated`, which returns **`true` after a real MOBILE_SEND DM
flew** — but the **home** mints the channel ctr, so `ctr` stays 0. ⇒ **a SUCCESSFUL delegated global post already answers
`> queued ctr=0` on metal**, indistinguishable from blocked or seal-failed.
⇒ ★★ **this entry's "`ctr == 0` IS the sentinel [for not sent]" is TOO STRONG. The sound reading is "this node minted no
channel ctr."** `ctr != 0` ⇒ originated locally with that handle; `ctr == 0` ⇒ **no local handle exists, and whether
anything flew is not answerable synchronously.**
⚠⚠ **THE REAL FIX MUST ACCOUNT FOR IT: a discriminated result that only splits accepted / blocked / refused would
classify producer (3) — a success — as a FAILURE.** A local handle for a remote mint is the missing piece.
ⓘ Also verified: `-t -g` reports the **TEAM** copy only (`gctr` discarded), and `queued, 0` is routine elsewhere
(`join`/`resolve`/`reqpubkey`/`peername` mint nothing; the hash-addressed `send` arm's 0 can mean **parked behind an H
resolve**, i.e. sent *later*). Note: `§b39-ctr0`. *(original entry below)*

#### B39 original finding — `CmdCode::queued` with `ctr == 0` was ambiguous
Two `do_send_channel` paths return `0` and say so in their own comments — the pre-TX gate (`node_channel.cpp:~645`,
`// not sent (no ctr minted)`) and a seal failure (`:~734`, `// NOT sent (the caller's queued becomes ctr=0)`) — and
`Node::on_command` wraps that zero unchanged: `return CmdResult{ CmdCode::queued, ctr, … }` (`node.cpp:~1578`).
⇒ **the synchronous result cannot distinguish accepted from blocked from failed-before-enqueue.** `next_ctr` never
returns 0 (`node_mac.cpp:20-24`, wraps 65535→1), so `ctr == 0` IS the sentinel — but it is undocumented at the seam and
every current caller ignores it. A caller that counts `queued` as "on the air" (any retry/attempt budget) miscounts a
**blocked** send as a transmission, and a **seal failure** leaves it waiting for an outcome push that names a different
ctr. ⚠ `CmdCode` alone also cannot separate `unsealable` from `no_location` — both surface as `err_unsupported`; the
actionable distinction exists only in `SendFailReason`, which the synchronous path does not carry.
**Fix shape:** return a discriminated result from the channel-origination path — **accepted** (non-zero ctr) ·
**blocked** (`reason` + `next_ms`) · **refused** (`SendFailReason`) — and adapt console formatting around it.
★ **QA-VERIFIED:** `next_ctr` is `c = (c >= 65535) ? 1 : c + 1` — **never 0**, so the sentinel is real.
ⓘ **NARROWER THAN WHEN WRITTEN:** `§err-reason` (B32) has since made a *refusal* name itself (`> err_no_binding …`), so
the residual gap is precisely the **`queued` + `ctr == 0`** case — *"command accepted, nothing minted"*. **The interim is
the right call; the discriminated result is a design change that can wait.**
**Minimum interim:** document `ctr == 0` as "not sent" at the `on_command` seam so callers stop treating `queued` as
proof of transmission. Found by the OLED-UI second review, §2. **UNMEASURED** — found in-source.

### B40 — ★★ `channel_sent.ctr` carried only the low 8 bits of a 16-bit counter · FIXED 2026-08-01
`do_send_channel` mints and returns the **full 16-bit** `next_ctr` (`node_channel.cpp:~647`), but the message id keeps
`c & 0xff`, and **both** `channel_sent` emit sites reconstruct from it: `emit_channel_sent(…, static_cast<uint16_t>(id
& 0xff))` (`:~1131`, `:~1158`). `Push::ctr` is already `uint16_t` (`command.h:~224`), so the width is available and
unused. ⇒ **an origination handle of 256 is answered by a push ctr of 0 and never matches again**; low-byte comparison
"works" only by colliding every 256 posts, which is precisely no correlation at all. Any consumer correlating its own
channel post to its outcome — the OLED UI's send tracker, and any future app-side equivalent — is affected.
★ **QA-VERIFIED, with one caveat to state in the fix:** `Push::ctr` is `uint16_t` and both emits mask `id & 0xff`, as
described. ⚠ **But `item.ctr` is ALSO masked** (`:1002`, `:1491`) — and *that* one is **by design**: the channel
msg-id's low byte **is** the ctr on the wire. ⇒ the fix is right and needs no wire change, but **the 16-bit ctr it emits
is a LOCAL correlation handle only — no peer can echo more than 8 bits.** Say so, or someone will later try to match it
against a received id.
**Fix shape:** store the full originating ctr in `ChannelReofferPending` and emit **that**. **No push-schema or wire
change** (the field is already 16-bit). Naturally one slice with **B38** — same struct, same emit sites.
**Coverage owed:** ctr 255 · 256 · 257 · 65535→1, with a low-byte-colliding unrelated outcome interleaved.
Found by the OLED-UI second review, §3. **UNMEASURED** — found in-source.

---

> ★★ **B42–B46 ARE ONE FAMILY: id → hash resolution.** The design + slicing lives in
> `docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md` (**v2**, revised against
> `docs/archive/2026-08-01-id-to-hash-resolution-design-review.md`).
> ★ **2026-08-01: B42 / B44 / B45 / B46 / B47 are FIXED (slices S1 / S2 / S2b + the S1b/S2b-fix round), green and
> UNCOMMITTED** — see each entry's CLOSED line and `simulation/BASELINE.md`'s three `§id-hash` notes for the gate
> numbers and poison matrices.
> ⚠⚠ **B46 WAS CLOSED PREMATURELY ONCE (2026-08-01) AND RE-OPENED THE SAME DAY** by the independent implementation
> assessment (`docs/2026-08-01-id-to-hash-resolution-implementation-assessment.md`, finding P1a): the first fix guarded
> the matching row but left `id_bind_evict_other_hash_holders` un-gated, so the identical demotion still walked in
> through the REVERSE uniqueness rule. **The coder's own poison matrix could not see it — and the re-probe proved why
> in numbers: even with the corpus's relayed learns FORCED to `claimed`, the rehome case fires 0 times in 304 885
> `id_bind_set` calls.** ⇒ ★ **a "guard the write" fix must be checked against every OTHER path that mutates the same
> invariant**, and byte-identity on a corpus that cannot produce the precondition is not evidence either way.
> ★ **2026-08-02: B43's WIRE HALF IS BUILT (`§id-hash S4a`), and B53 closed with it.** `H_FLAG_BY_ID` is on the wire,
> both planes ingest a `claimed` binding, and `peer_book_by_id` reads at the `claimed` floor so the tier is visible and
> LABELLED. **B54 stays open by decision, not omission** (see its entry). **B55 is new** — the two-stage
> `reqpubkey_sent.hash == 0` meaning, owed to the companion contract.
> ★★ **2026-08-02: `§id-hash S4b` IS BUILT — the by-id flow is ONE command** (bounded `resolve-id-for-pubkey` intent +
> on-node second stage + a bounded loud timeout; note in `BASELINE.md`). ⚠ **B55 did NOT close with it and was NOT
> meant to**: `reqpubkey_sent.hash == 0` is the honest report of a stage-1 acceptance and stays — what changed is the
> INSTRUCTION it carries (`do not re-issue`), so the owed contract paragraph is now a **correction**, not an addition.
> **B56 / B57 / B58 are new** (S4b's registered residuals). **S5** (`confirmid`) remains undispatched, still on O4/O5.
> ⚠⚠ **AND THE FAMILY'S INHERITED PREMISE — "the corpus is structurally blind to the `claimed` tier" — IS NOW
> DISPROVEN IN BOTH DIRECTIONS.** S3 showed the *floor* is corpus-live (a beacon-stamps-claimed probe moves 5/36);
> S4a shows the *producer* is corpus-live too — **26 team-plane ingests across five scenarios** — while the streams
> stay 36/36 byte-identical **because the floor contains them**, proven by disabling the floor (3/36 move) and then
> disabling it again with the producer reverted (back to clean). ⇒ **stop writing "the corpus cannot see this tier";
> write which LAYER it cannot see, and measure it.**
> ⚠ **B46 was a live demotion bug that exists independently of the feature** — registered and fixed on its own merits,
> in its own slice, not buried inside the design entry.
> ⚠⚠ **ONE MEASUREMENT THE WHOLE FAMILY SHOULD INHERIT (S2b, 2026-08-01): the 36-scenario corpus contains ZERO
> `IdBindConf::claimed` bindings.** Instrumented directly: **299 441** matching-row `id_bind_set` updates, every one
> `existing=authoritative, incoming=authoritative, same hash`; plus **5 444** NEW-row inserts, all `authoritative`
> (4559 `bcn` · 858 `self` · 14 `h_relay` · 13 `h_query`). ⇒ **the corpus is structurally blind to the entire `claimed`
> tier**, so S3/S4a — which are ABOUT that tier — will be equally invisible to it unless a scenario is authored to
> produce a relayed soft H answer. Budget scenario work into those slices; do not expect the corpus to gate them.

### B42 — ★★ by-id `reqpubkey` was team-only by construction · FIXED 2026-08-01
`console_parse.cpp:~232` sets `bool team = (out.u.resolve.dst_id != 0)` — a bare decimal **forces** the TEAM plane — and
`:~234` accepts only `-t`, so **no `-s` exists**. `node.cpp:~1646` then resolves via `team_key_of_id` alone, whose first
line is `if (_cfg.team_id == 0 || !is_team_peer(id)) return false;` (`node_routing.cpp:~842`). ⇒ with `team_id == 0`
every bare-id `reqpubkey` returns `err_no_binding`, **including for a directly-heard neighbour held authoritatively**.
★ **MEASURED on the bench 2026-08-01**, and the two-verb contrast is the whole proof on one node: `hashof 186` →
`0x61CD83EA` (reads `_id_bind` via `key_hash_of_id`) while `reqpubkey 186` → `err_no_binding` (reads `_team_keys`).
This is verbatim the defect `firmware_commands.cpp:527-530` records from 2026-07-30 — *"Each verb was correct about its
own table; neither answered the question"* — **whose fix landed on `hashof` and never on `reqpubkey`**.
★ **A SECOND SITE, and the fix is half-landed without it** (the sweep-scope meta-bug's tenth instance, this time across
**transports**): `src/fw_main.cpp:~490-491`, the **BLE** `reqpubkey_sent` echo, resolves through `team_key_of_id` too, so
a static-plane by-id `reqpubkey` would still echo `hash=0` to the companion after `on_command` is fixed.
**Fix shape:** both sites read `Node::peer_book_by_id` (U1 — it already searches both planes and already returns a
*mask*); add `-s`, make `-s`/`-t` exclusive, and specify plane-ambiguity (spec §3-D9). No wire change.
**Coverage owed:** static-only node · team-only node · the same numeric id live in BOTH planes · unresolved id on a
dual-plane node · the BLE echo's resolved hash. **MEASURED** (bench) + in-source.
★ **CLOSED 2026-08-01 by slice `§id-hash S1` (green, UNCOMMITTED).** Both sites now read `Node::peer_book_by_id`; `-s`
lands, `-s`/`-t` are exclusive, a bare id goes out as plane 0/AUTO and `on_command` picks per §3-D9 (both planes hold
it ⇒ the new `CmdCode::err_ambiguous_plane`, and **no airtime is spent guessing**). The BLE echo no longer re-resolves
anything: `CmdResult` now carries `dst_hash` (the RESOLVED hash) and a new `plane` field, and every transport reads
them. All five owed coverage cases are native tests (`test_node_hashlocate.cpp`, the `§id-hash S1` block).
⚠ **ONE OWED CASE COULD NOT BE BUILT AS SPECIFIED, and the reason is a spec correction — see §3-D9 in the ENTRY BELOW
and BASELINE's S1 note:** *"unresolved id on a dual-plane node"* has **no distinguishable outcome in S1**, because an
unresolved by-id `reqpubkey` refuses (`err_no_binding`) BEFORE any plane is selected or any query flies. It becomes a
real case in S4a, where an unresolved id does fly a by-id query. Pinned as such by a test rather than faked.
⚠ **NEW FINDING while building it → B47 below** (an off-grid mobile answers `queued` for a GLOBAL by-id request that
provably airs nothing).
⚠⚠ **COMPANION CONTRACT OWED (reported, NOT written — `ios-companion/INBOX_SYNC_CONTRACT.md` is QA's file).**
★★ **CORRECTION 2026-08-01, and it was my error: I first reported these as "three ADDITIVE changes, all
backward-safe". THAT WAS WRONG, and the assessment (P1b) caught it.** The RESPONSE-side changes are additive and the
Swift decoder tolerates them; **the OUTGOING command's MEANING changed, and that is a break**:
① ★★ **BREAKING (outgoing):** `Command.reqPubkeyTeam(localID:)` emitted the bare `reqpubkey <id>` and relied on the
firmware reading a bare decimal as implicitly TEAM. S1 made a bare decimal **AUTO** ⇒ that operation no longer
guarantees its own name: it draws `err_ambiguous_plane` when both namespaces hold the number, or silently selects
**static** when only a static binding exists. **FIXED in the checked-in companion** — `Command.swift:150` now emits
`reqpubkey <id> -t`, `CommandEncoderTests.swift` pins the exact line, and the stale "implicitly TEAM" comments are
corrected. ⚠ **NOT gate-verified: `swift` is unavailable in this environment — bench/CI-owed.**
② three new ack codes: **`err_ambiguous_plane`** (CmdCode 10), **`err_no_identity`** (11) and
**`err_tx_ring_full`** (12). `AckCode` in `Inbound.swift` has a `.unknown` forward-compat case so all three degrade,
exactly as the already-unmodelled `err_ack_ring_full` does. ⚠ **`err_tx_ring_full` is TRANSIENT** — the app should
retry rather than surface a configuration error; it is NOT `err_ack_ring_full` (different ring, different remedy).
③ `{"ack":…}` may now carry **`"plane":"team"|"static"`**, and `{"ev":"reqpubkey_sent"}` the same — **both omitted
when absent, so every pre-S1 line is byte-identical** (pinned by a test).
④ ★ **`reqpubkey_sent.hash` is now the RESOLVED hash** for a by-id request, where it used to be `0` on the static
plane — and, per B47, the event now fires **only when the TX path ACCEPTED the frame**. ⚠ **"accepted", not "aired"**
(owner ruling 2026-08-02): a synchronous `CmdResult` cannot prove a future transmission, because an LBT-deferred frame
reaches the radio after `on_command` has already returned. Acceptance = no bail-out, no LBT-ring drop, no `DeviceHal`
rejection; a later `pump_tx` radio-start error is **outside** the guarantee. The residual is covered by the late
`!!` report, not by this event.
⑤ the grammar itself: `reqpubkey <0xhash|id> [-s|-t]`, flags mutually exclusive, bare id = AUTO.

### B43 — ★★ no id → hash for a node we **route to** but never heard directly (both planes) · FIXED 2026-08-02
`_team_keys` is written at exactly one site — `node_beacon.cpp:~831` — reached only from a **directly-heard same-team
beacon**. A multi-hop teammate still gets its `_team_peer` dispatch bit, from the DV merge at `node_beacon.cpp:~939-940`,
off a route entry that **carries no key**. Static is the same shape: `_id_bind` is fed by a heard beacon
(`node_beacon.cpp:~664`) or an H answer, never by a route. ⇒ a peer is **routable but unidentifiable**, which blocks
`reqpubkey`, sealed send, `team grantkey` and any `hashof` answer.
★ **MEASURED on the bench 2026-08-01, on BOTH planes**: team — `[peer] team_id=114` / `team_id=214` with no hash while
228 is keyed; static — `_rt` holds routes to 48 (3 hops), 59 and 109 (2 hops) and **not one appears in `peers all`**.
★ **Not a new discovery — the deferred half of §hashbind-plane / B2.** `on_hash_bind_snoop`'s header already scoped it
and marked it `✖ MISSING` on 2026-07-31 (`node_hashlocate.cpp:~1245-1248`): *"a team-plane bind store with its own
confidence field … **needs the trust question in (1) answered first**."* The owner answered it 2026-08-01: an on-air
id→hash answer is a **claim**, never authoritative.
**Fix shape:** spec S3 + S4a/S4b — `H_FLAG_BY_ID` (byte 7 has four free bits), owner-only answers, `claimed` landing on
both planes, and the two-stage `reqpubkey` completion.
**Coverage owed:** the spec's §9 gate list. **MEASURED** (bench, both planes).
★ **CLOSED — THE BINDING HALF — 2026-08-02 by slice `§id-hash S4a` (landed at HEAD `2ff40dc`).** `H_FLAG_BY_ID = 0x10`
reuses H bytes 2-5 as a **canonical** zero-extended id (bytes 3-5 zero, ids 0/255 refused on pack **and** parse,
`h_by_id_key_canonical` is the one predicate all three sites share); `by_id` joins the `HashQuerySeen` key (at **0
bytes** — `offsetof == 19`, `sizeof` 24 unmoved) and rides every forward. **Only the OWNER answers**, self-matching on
`_node_id` (static) / `team_local_id()` (team), never from a cache — spec §3-D3's principle: *a cached answer is
allowed exactly when the answer is self-verifying, and an id→hash one is not.* The answer is a plain
`DATA_TYPE_H_ANSWER` (§3-D4's `binding_verifiable = false`), so it lands **`claimed`** on the static plane through the
existing codepoint and — **newly built** — in `_team_keys` on the team plane, ★ **without ever touching `_team_peer`**.
`reqpubkey <id>` on an unresolved id now FLIES that query instead of refusing (spec §5 stage 1), which is the
originator B43 needs; **B53's floor, lowered in the same slice, is what makes the resulting row visible.**
★★ **CLOSED IN FULL — 2026-08-02 by `§id-hash S4b` (landed at HEAD `2ff40dc`).** A bounded
`resolve-id-for-pubkey` intent records stage 1, consumes the owner's claimed binding answer, and automatically emits
the existing hash-keyed pubkey request as stage 2. A bounded loud timeout clears an unanswered intent. The operator
workflow is therefore one `reqpubkey <id>` command on both planes.
ⓘ `reqpubkey_sent.hash == 0` remains the honest stage-1 acceptance report; its app-contract meaning is tracked by B55.
⚠ **NOT unblocked, per spec §8:** multi-hop `team grantkey` and sealed send still refuse a claim. That is the owner's
confidence split working, not a gap.
★★ **THE MEASUREMENT WORTH INHERITING (and it contradicts spec §6's own "re-anchors" prediction): the corpus is
36/36 BYTE-IDENTICAL, and that is CONTAINMENT rather than absence.** Instrumented: the new team ingest fires **26
times** across s24/s25/s26/s28/s34 (13 destination `h_query` + 13 relay `h_relay`, **every one arriving on an
AUTHORITATIVE frame type**); of those, **13 are refused by S3's D5c① rule**, **6 insert new claimed rows** and **7
refresh a claim**. The streams do not move because every reader is either behind the default `authoritative` floor or
lives in `src/` (outside the sim build). **Proof, not inference:** disabling the two team floors moves **3/36**
(s24/s25/s26, event counts *falling* 1574→1408 / 792→638 / 1045→854 — a cache doing its job), and doing that *with the
ingest reverted* returns all 36 streams **byte-identical to clean**.

### B44 — `peers all` had no static equivalent for routed-but-unkeyed peers · FIXED 2026-08-01
`peer_book_walk` (`node_hashlocate.cpp:~462-507`) runs four passes — `_peer_keys` → `_id_bind` → `_team_keys` →
`_team_peer` bits. The fourth emits an id-only row for a teammate we route to but hold no key for; **there is no pass
over `_rt`**, so the static plane has no counterpart and the team plane is currently the *more* informative of the two.
★ **MEASURED on the bench 2026-08-01**: 114/214 listed, 48/59/109 absent despite live routes (same transcripts as B43).
**Fix shape:** a static `_rt` pass mirroring team pass (4), gated on `include_id_rows` so the JSON book
(`include_id_rows=false`, `:~466`) stays untouched. **MEASURED** (bench).
★ **CLOSED 2026-08-01 by slice `§id-hash S2` (green, UNCOMMITTED).** Pass **(2b)** added, sitting between `_id_bind`
and the team passes; the JSON book is untouched (asserted: `include_id_rows=false` returns the `_peer_keys` count with
ten live routes present). ⓘ **Its dedup is against `_id_bind` MEMBERSHIP, not `key_hash_of_id`** — the accessor filters
`claimed` rows and hash-0 rows that pass (2) still emits, so testing through it would print those ids twice; a native
test pins exactly that (a `claimed` binding for 48 ⇒ still one row).
ⓘ **Renderer honesty, folded in:** `peers_text_row` printed `(auth)`/`(claimed)` from `static_authoritative` for EVERY
`static_id`, so an id-only row would have read `static_id=48(claimed)` — a claim nobody made. The suffix now prints
only when the row carries a hash to be authoritative *about*. This also cleans up pass (2)'s hash-0 rows.

### B45 — `peers all` listed the local node as its own peer · FIXED 2026-08-01
`id_bind_set(_node_id, _key_hash32, IdBindSource::self, IdBindConf::authoritative)` (`node.cpp:~77`, `:~539`, `:~864`)
seeds our own binding into `_id_bind`, and `peer_book_walk`'s pass (2) has **no self-skip**.
★ **MEASURED on the bench 2026-08-01**: node 42 reports `[peer] hash=0x8CC9BDFF static_id=42(auth)`, so `count=2`
actually means "one peer and me". **Text-console only** — the JSON book passes `include_id_rows=false` and we are not in
`_peer_keys`. **Fix shape:** skip self in pass (2). **MEASURED** (bench).
★ **CLOSED 2026-08-01 by slice `§id-hash S2` (green, UNCOMMITTED).**
⚠ **THE PREDICATE IS THE SELF-BINDING, NOT THE ID**, and that is load-bearing rather than pedantic: the skip is
`node_id == _node_id && key_hash32 == _key_hash32` — `id_bind_set`'s own self-defence test (`:~59`), verbatim (U1).
`node_id == _node_id` alone would ALSO hide a FOREIGN key claiming our id, i.e. an address collision — the single most
diagnostic row this dump can carry, and the exact condition `addr_conflict_self_defended` exists to surface.

### B46 — ★★ a claimed observation could demote or displace an authoritative binding · FIXED 2026-08-01
On a matching row, `node_hashlocate.cpp:~102-104` writes the incoming `source` and `confidence` **unconditionally**:
```cpp
_active->_id_bind[i].last_seen_ms = now;
_active->_id_bind[i].source       = static_cast<uint8_t>(source);
_active->_id_bind[i].confidence   = static_cast<uint8_t>(confidence);
```
⇒ **live today, with no new feature required**: a relayed soft H answer (`IdBindSource::h_relay`,
`node_hashlocate.cpp:~1252-1253`) demotes a first-hand beacon binding to `claimed`, and the **seal path then refuses**
— `key_hash_of_id:~148` filters `confidence != authoritative` — until the next beacon re-asserts it. The sibling store
already has the correct rule and is the reference: `peer_key_set:~255-261` upgrades and never downgrades.
⚠ **Also a correction to the record:** there is **no `IdBindConf` NV encoding at all** (`src/device_nv.h`'s `kPeerConf*`
is `PeerKeyConf`'s). `_id_bind` is RAM-only, TTL-bound at 48 h (`protocol_constants.h:~535`) — which is why the spec's
manual-confirm verb is scoped as *ephemeral*, not as a pinned trust anchor.
**Fix shape:** confidence upgrade-only; and a `claimed` sighting must **not** extend an `authoritative` row's
`last_seen_ms` (a claim must not keep an unverified binding alive) — deliberately symmetric with the spec's team-plane
rule §3-D5c. ⚠ **May legitimately re-anchor** — an unattributable re-anchor is a failed gate.
**Coverage owed:** claimed-after-authoritative (no demotion, no TTL extension) · authoritative-after-claimed (promotes)
· same-node re-key still applies · the self-binding stays exempt. **UNMEASURED** — found in-source by the design review.
⚠⚠ **RE-OPENED 2026-08-01 (assessment P1a), THEN CLOSED — read both halves.**
**FIRST FIX (incomplete):** the matching-row write became upgrade-only. **What it missed:** `id_bind_set` also enforces
the REVERSE rule (one hash -> one node_id) through `id_bind_evict_other_hash_holders`, and **both** accept paths called
it without looking at confidence. So the same demotion survived through a different door: authoritative `{10,H}` +
a relayed claimed `{20,H}` takes the NEW-node_id path, **evicts the authoritative row**, and inserts the claim.
★ **SECOND FIX — CLOSED 2026-08-01 (green, UNCOMMITTED):** a claimed observation may not displace an authoritative
holder of the same hash. New `id_bind_auth_holder_other()` (gates = `key_hash_of_id`'s verbatim, U1: authoritative +
fresh, self exempt from the TTL); the whole write is REFUSED with a named `addr_rehome_refused` emit, because
inserting-without-evicting would leave two rows for one hash and break the bijection `id_bind_find_by_hash` relies on
— strictly worse. **Owner ruling: claimed -> claimed stays NEWEST-WINS** (no trust ordering between two claims; keeping
it makes this a fix, not a redesign — C1). An **authoritative** rehome still evicts, held as the positive control.
An **expired** authoritative holder does not block (it is invisible to every reader already), also tested.
★ Confidence is upgrade-only; `source` and `last_seen_ms` are frozen with it, so a claim can neither relabel the
provenance nor fake first-hand liveness. All four originally-owed coverage cases are native tests, plus the
conflict/self-defence arms as controls. **`IdBindSource::manual` was
DELIBERATELY NOT added** — its only producer is `confirmid`, which is S5, and an enumerator with no writer is exactly
the `PeerKeyConf::overheard` smell the spec criticises in §2.4. **Spec §6's S2b row should be amended to drop it.**
★★ **IT DID NOT RE-ANCHOR — 36/36 byte-identical — AND THE REASON IS MEASURED, NOT ASSUMED.** Both doors were
re-probed after the second fix (assessment §4.6): same-row demotion **0/36**, cross-id claimed rehome **0/36**. Then a
CAPABILITY probe made the instrument capable — force the corpus's own `h_relay`/`h_query` learns to land `claimed`
(the exact input class this rule protects against): that alone moves **3/36**, so the precondition is now live, and
under it the numbers are decisive across **304 885** `id_bind_set` calls:
· **same-row demotion: 15 occurrences** ⇒ the guard IS executed — but reverting it under the same poison still moves
  **0/36**, because the corpus re-asserts those rows from a first-hand beacon **296 397** times and the demotion window
  closes before any reader looks. A real masking mechanism, not an absent one.
· **cross-id claimed rehome: 0 of 304 885** ⇒ **structurally unreachable even with claimed bindings forced**, because
  no corpus soft answer ever brings a hash in under a SECOND id. ★ **That is precisely why the original poison matrix
  could not have caught P1a, and it is the honest statement to inherit: this door has no corpus gate at all, only
  native.**
⚠ **THE INTENDED SIDE EFFECT, stated so it is not later read as a regression:** an authoritative row that only ever
gets re-CLAIMED now ages out at `id_bind_ttl_ms` (48 h) instead of living forever on hearsay. That is the point of the
liveness half of the rule; the self-binding remains exempt via `id_bind_age_out`'s `self_keep`.

### B47 — an off-grid mobile could accept a GLOBAL `reqpubkey` that transmitted nothing · FIXED 2026-08-01
`emit_hash_query` bails at `want_pubkey && mobile_req && origin == _node_id && !team_scoped`
(`node_hashlocate.cpp:~1557`) — an unregistered mobile's `_node_id` is a LOCAL id with no static return path, so the
owner could not answer. It emits `h_want_pubkey_mobile_no_route` and **no frame**, but `on_command` has already
returned **`CmdResult{queued}`**, so the operator/app is told the request went out.
★ **MEASURED 2026-08-01** by a native test (`test_node_hashlocate.cpp`, the `§id-hash S1 §3-D9` case): on an
unregistered mobile the `-s` arm produces `queued`, `h_want_pubkey_mobile_no_route`, zero `h_tx`, zero tx_frames —
while the `-t` arm on the SAME node in the SAME test flies a real frame (the control).
**PRE-EXISTING, NEWLY REACHABLE.** The bail is old and already documented in `s22_mobile_team`'s `_desc`
(*"on this homeless off-grid member NO h_tx is emitted at all"*). What changed with `§id-hash S1` is the WAY IN: a bare
`reqpubkey <id>` used to force TEAM, so this arm needed an explicit `reqpubkey 0x<hash>`; now a bare id that resolves
STATICALLY on an off-grid member lands there too.
**This is B39's class** — `queued` means "accepted", never "sent".
⚠⚠ **WIDER THAN I RECORDED, and the assessment (P1c) found the rest: `emit_hash_query` is `void` and returns early in
FOUR ways, not one** — degenerate/self target, **no crypto identity**, off-grid mobile with no return path, and a
`pack_h` codec failure. `on_command` answered `queued` through all four and BLE turned every one into
`{"ev":"reqpubkey_sent"}`, whose contract meaning is *"the on-air request was flooded"*.
★ **The no-identity case also DISPROVES two claims of mine**: the in-source comment at the BLE echo and the companion
contract both said that path *"keeps its existing error ack"*. **There was no such ack** — it reported success.
★ **CLOSED 2026-08-01 by `§id-hash S1b` (green, UNCOMMITTED).** `emit_hash_query` now returns
`Node::HQueryOutcome{sent, degenerate, no_identity, no_return_route, encode_failed}` and the reqpubkey arm maps it to
an honest `CmdResult`: `err_unsupported` / **`err_no_identity`** (new code) / `err_no_gateway` / `err_too_large`,
each still echoing `dst_hash` + `plane`. ⓘ **The "preferred" option was taken and it did NOT become refactor-plus-fix
(C1): widening `void` -> the enum changed ZERO call sites**, because all four other callers already discarded the
absent return and there is no `[[nodiscard]]` — so the diff is the function's own returns plus one reader.
★ **`CmdResult::aired`** (free — it lands in the existing tail pad, `sizeof` stays 20) is what `reqpubkey_sent` is now
keyed on, because one **accepted** outcome legitimately airs nothing: the **hosted-mobile local cache hit**, which is
a genuine success reported through its own `peer_key_cached` push and must not also claim a flood.
**Coverage:** each branch asserts the `CmdResult` **and** the BLE-visible disposition (a test-local mirror of
fw_main's exact `code == queued && aired` predicate), every negative paired with a same-fixture successful flight.
**MEASURED** (native; 4 cases / 10 assertions redden when the outcome mapping is bypassed).
⚠⚠ **AND IT WAS STILL ONE LAYER SHORT — a FIFTH bail point, found by the second QA pass.** `tx_initiating`
(`node_mac.cpp:1095`) was itself `void` and **discarded** `schedule_lbt_defer`'s `bool`, which is `false` when the
4-slot LBT defer ring is full (*"ring full -> drop loudly"*). ⇒ frame **dropped** → outcome still `sent` → `aired=true`
→ **`reqpubkey_sent`**. Same false-success class, one call deeper, and it breached spec **§5.1**'s *"must not be
reachable from any bail point"*.
⚠⚠ **RE-OPENED 2026-08-01 A SECOND TIME (assessment §6) — and this one reached REAL HARDWARE.** S1c stopped at the
Node's LBT ring. One layer below, `tx_with_retry` did `_hal.tx(...)` and **discarded the `TxResult`**, then
`return true // handed`. `DeviceHal::tx` answers `busy` when its **8-entry outbound ring** is full — it bumps
`txq_drops` and **does not retain the frame** — and an H/beacon frame has `slot < 0`, so there is no stash retry
either. A definitive hardware drop still reported acceptance. Neither automated gate could see it: this repo's test
HAL returned a hard `ok`, and the sim's `FirmwareNode::simTx` pushes onto an **unbounded vector**.
★★ **OWNER RULING that settled the semantics (2026-08-01), after two rounds of chasing a stronger claim:**
`reqpubkey_sent` means **"the TX path ACCEPTED the frame — nothing rejected it"**, NOT a claim of airtime. *"Emitted
only when a frame actually left"* is **unsatisfiable synchronously** — a deferred frame reaches the radio when a timer
fires, long after `on_command` returned. ⇒ `CmdResult::aired` is renamed **`accepted`** and documented as such.
★ **CLOSED 2026-08-01 by `§id-hash S1c` + `§tx-admission TX1` + `§id-hash S1d`:** `tx_initiating` returns `bool`;
`tx_with_retry` inspects the `TxResult` (see **B50**); `HQueryOutcome::tx_dropped` maps to **`err_tx_queue_full`**
(renamed from `err_tx_ring_full` — see below). ⓘ **U1 CHECKED: `err_ack_ring_full` (9) was NOT reused** — same shape, different ring, and the
shipped contract documents it as the pending-E2E-ack ring, so reusing it would hand the app a wrong diagnosis and a
wrong remedy. This one is the only **TRANSIENT** refusal on the verb ("retry in a moment"), and the hint text says so.
⚠ **AND THE SAME REASONING FORCED A RENAME ONE DAY LATER: `err_tx_ring_full` → `err_tx_queue_full`.** **TWO** bounded
queues can reject this command — the Node's 4-slot LBT defer ring and `DeviceHal`'s 8-entry outbound ring — so a name
(or an operator hint) fingering one of them is a wrong diagnosis half the time. The hint now says *"a bounded TX queue
rejected the frame (the radio or the channel is saturated)"* and names neither. Renaming was free (M3, and the code
was uncommitted).
★ **THE DEFERRED RESIDUAL — no silent loss.** A frame ACCEPTED into `_deferred_lbt` can still meet a full HAL queue
when its timer fires, and unlike DATA an H query has **no MAC timeout** behind it to recover. That death is now
reported LATE: `tx_deferred_lost` **plus an operator-critical `_hal.log`**. ⚠ **CORRECTED (QA P2): my first version of this was
FALSE on metal** — `fw_main`'s sink gated every log line on `g_mr_trace_on`, so under `debug off` the report was
completely silent and `_hal.log` is a DEBUG channel, not an operator channel. The message now carries a `!!`
operator-critical marker and the sink prints those regardless of trace.
⚠ **A BOUNDED RE-DEFER WAS CONSIDERED AND REFUSED, and the reason is specific to the payload:** re-deferring would air
an H query at an unbounded later time, after the operator has been told and has plausibly retried by hand —
duplicate airtime for a question that is already stale — and it needs retry state plus a second timer path for a case
no automated gate can reach. ⓘ A per-command PUSH was also refused: correlating the loss back to the `reqpubkey` that
queued it needs a handle the frame does not carry, and `send_failed{ctr:0}` is exactly the uncorrelated shape **B39**
exists to fix. Owed to B39 (C1), recorded rather than faked.
★ **SCOPE RULING (owner, superseded and restated 2026-08-01): a SUCCESSFUL defer is `accepted`** — the TX path
took it. ⚠ It is **NOT** a claim that it flew (that is unsatisfiable synchronously); a deferred frame that dies later
is reported by the late `tx_deferred_lost` + the operator log. Only a DROP is false. `lbt_complete`'s own early returns are excluded on inspection: both are
RTS-only (a stale-flight cancel, and a duty defer that does fly) and neither is reachable from `emit_hash_query`,
which always passes `LbtKind::flood`.
ⓘ **The widening again changed ZERO call sites** — 22 callers across 8 files discard it and there is no
`[[nodiscard]]`; `git diff` shows the six files holding 18 of them were not touched at all.
**MEASURED** (native): the ring-full fixture reddens 1 case / 3 assertions when S1c's `bool` is discarded; the
HAL-rejection fixtures redden **2 cases / 9 assertions** when the `TxResult` is discarded, **2 / 7** when
`lbt_complete` swallows it, and **1 / 2** when the deferred-loss report is removed.
★ **Corpus: 0 HAL rejections and 0 deferred losses — structurally, not accidentally** (the sim's `simTx` has an
unbounded queue and can only answer `too_long` at len > 255, which no packer produces). A **capability probe** (force
the HAL to reject every flood) reaches **7123** rejections, and the behavioural delta isolates to **s22's
`"OK reqpubkey queued"` → `"OK reqpubkey error"`** — with the propagation reverted under the same poison, all 36
streams return **byte-identical to clean**, proving the poison is otherwise inert.

### B48 — ★ a display de-duplication rule was making an airtime decision · FIXED 2026-08-01
`peer_book_by_id`'s team arm read `if (team_key_of_id(id, th) && !(mask && th == h))` — it suppressed the TEAM
presence bit whenever both planes resolved the **same hash**. Harmless while only `hashof` read the mask (it was a
tidiness choice, from `§AB3`); **`§id-hash S1` made that mask select a query plane**, and there it produced two wrong
answers on a node where one identity occupies the same number in both namespaces:
· a bare `reqpubkey <id>` **silently selected STATIC** instead of §3-D9's ambiguity refusal;
· an explicit `reqpubkey <id> -t` saw `has_team == false` and returned **`err_no_binding` for a team binding that
  exists**.
★ **Found by the independent implementation assessment (P2)**, not by the corpus: the original D9 test covered only
DIFFERING hashes, so the exact-duplicate branch was untested.
★ **CLOSED 2026-08-01 by `§id-hash S1b` (green, UNCOMMITTED):** the resolver now reports PRESENCE per plane and
nothing else — hash equality never made the two planes' routes, return paths or flood scope equal, which is exactly
what the flag selects. Identity de-duplication, if ever wanted, is a **renderer** concern; `handle_hashof` now prints
both rows and their equal hashes say "one identity, two planes" more clearly than the suppressed row did.
**Coverage:** the same-id/same-hash dual-plane test — bare = `err_ambiguous_plane` with no airtime, `-t` sends on the
team plane, `-s` selects the static row. **MEASURED** (native; restoring the de-dup reddens 2 cases / 12 assertions).
⇒ ★ **The durable rule: a shared resolver returns facts. The moment a "tidy display" filter lives inside one, some
future caller will make a decision on the filtered answer.**

### B50 — ★★ `tx_with_retry` discarded the HAL transmitter result · FIXED 2026-08-01
`lib/core/node_mac.cpp`'s central TX helper did `_hal.tx(bytes, len, p);` and then `return true; // handed`, throwing
away the only answer the radio layer gives. On hardware `DeviceHal::tx` returns **`busy`** when its 8-entry outbound
ring is full (it increments `txq_drops` and **does not retain the frame**) and **`too_long`** past the SX1262 length
register. ⇒ **every** TX caller in the tree — not just `reqpubkey` — could not distinguish "queued for the radio" from
"dropped on the floor".
⚠⚠ **CORRECTION 2026-08-01 — THE CLAIM "a function every TX path goes through" WAS FALSE, and it hid a second site
for a full round.** `tx_flood` does **not** go through `tx_with_retry`; it calls `_hal.tx` **directly**. The dispatch
that produced TX1 asked for a sweep of *`tx_with_retry`'s callers*, which is the wrong set — the right one is **every
direct `_hal.tx` caller**. ★ **That is the arc's next sweep-scope instance** (after directory-vs-file, verb-prefix,
predicate-vs-pattern and transport scope), and it was in the dispatch, not the implementation.
★ **THE FULL CLASSIFICATION — all FOUR direct `_hal.tx` call sites, which is what B50 should have listed from the
start:**
| # | site | consumer of the result | verdict |
|---|---|---|---|
| 1 | `node_mac.cpp:~1300` `rts_duty_defer_fire` | none — the function is `void` and calls `start_rts_timeout()` unconditionally | **OK as-is.** An RTS is retry-eligible in effect: the CTS-wait timeout it arms IS the recovery, exactly the argument that keeps TX1's three readers arming theirs. No app-visible claim, no state burned. |
| 2 | `node_mac.cpp:~1335` `tx_flood` immediate | ★ **`emit_beacon:517` reads it as `sent` and `:525` commits the channel digest** | ★★ **WAS BROKEN — fixed by TX2.** Not telemetry: a dropped beacon burned an advertisement horizon. |
| 3 | `node_mac.cpp:~1493` `tx_with_retry` | 3 readers (`duty_defer_fire`, both `do_data_tx` arms) | **fixed by TX1.** |
| 4 | `node_mac.cpp:~1534` `retry_stashed` | none directly; it re-arms `awaiting_ack` + the ack timeout below | **OK as-is** — retry-eligible frame, the MAC timeout is the recovery (same argument as #1). |
⇒ **exactly one of the four was a real defect beyond TX1, and it is the one the wrong sweep could not have found.**
★ **This is registered separately from B47 on purpose:** B47 is one verb's contract; this is the shared TX layer, and
its attribution must stay separable (C4).
⚠⚠ **THE OBVIOUS FIX WOULD HAVE CAUSED A REGRESSION, and finding that changed the slice's shape.** The dispatch
assumed `tx_with_retry` might be a `void`/all-discard function. **It is neither:** it already returned `bool`, and
**three callers READ it** — `duty_defer_fire` and both `do_data_tx` arms — each using it to decide *"arm the post-TX
state?"*. Their existing `false` means **"not sent, but a re-send timer IS armed"** (the duty defer). A HAL rejection
arms nothing, so folding it into that same `false` would have suppressed `start_ack_timeout()` on a dropped DATA and
left the flight with **no recovery at all** — strictly worse than the reporting defect. For a retry-eligible frame the
MAC timeout **is** the recovery (`device_hal.h` says so).
★ **FIXED 2026-08-01 by `§tx-admission TX1` (green, UNCOMMITTED, its own commit):** the return becomes a three-way
`Node::TxHandOff{handed, deferred_retry_armed, rejected}`. The three existing readers branch on
`!= deferred_retry_armed`, which is **bit-for-bit their previous `true`** — pinned by two `static_assert`s beside them
so a future fourth enumerator on the wrong side of that line is a build failure, not a silent DATA regression. The
result propagates through `lbt_complete` (whose two RTS-only early-outs are excluded on inspection: a stale-flight
cancel abandons a dead flight, and the duty defer re-arms) and out of `tx_initiating`.
ⓘ Telemetry only (`tx_hal_rejected`) — MR_EMIT is device-stripped, and the metal-side diagnostic `txq_drops` already
exists. ⚠ **`txq_drops` has no console surface**, so on metal a rejection is currently invisible unless it hits the H
path; noted in the bench script, not fixed here (C1).
**MEASURED** (native + capability probe; see B47). **Corpus 36/36 byte-identical** — 0 rejections, structurally.

### B51 — ★★ `tx_flood` discarded admission results and burned channel-digest state · FIXED 2026-08-02
`tx_flood` answered `true` in two cases where the frame was definitively gone: a **full 4-slot LBT defer ring**
(`schedule_lbt_defer`'s result discarded) and a **HAL rejection** (`_hal.tx`'s `TxResult` discarded).
★★ **NOT a telemetry defect.** `emit_beacon` takes that boolean as `sent` and gates
`commit_channel_digest_advertised` on it, under the comment *"burn an ad_count … ONLY for advertisements that ACTUALLY
AIRED … the air-honesty fix"*. The commit does `++e.bcn_ad_count` and, on horizon, `e.dirty = false`. ⇒ **a dropped
beacon consumed the advertisement horizon and could RETIRE a digest nothing ever received** — the air-honesty
mechanism defeated by two discarded returns.
★ **FIXED 2026-08-01 by `§tx-admission TX2` (green, UNCOMMITTED) at ZERO bytes.** Both sites return the real result;
the digest commit now follows the **acceptance** boundary — the same one the owner ruled for `reqpubkey_sent` — so a
ring-full drop and a HAL rejection both leave the entry **dirty**, and an accepted defer commits.
★★ **THE RESIDUAL WAS RULED ON, AND THE RULING REVERSED THE EARLIER CALL — `§tx-admission TX3`, 2026-08-02.**
> for channel-digest accounting, **"sent" means accepted by the transmitter/DeviceHal** — the strongest boundary the
> current architecture can observe. It does not mean literal RF airtime.
⇒ commit-on-LBT-entry is gone. The advertised digest ids ride the deferred slot (`DeferredLbt::digest_ids[3]`) and
the commit happens in node.cpp's defer arm **iff the deferred `lbt_complete` reaches `_hal.tx` and DeviceHal answers
ok**; a ring-full drop or a HAL rejection leaves **both** the `bcn_ad_count` and the dirty flag untouched. Immediate
beacons commit right after their own `_hal.tx == ok`. The commit now lives in `tx_flood`/the defer arm — the two
ADMISSION points — not at `emit_beacon`'s call site, which could only ever have meant "we tried".
★ **Why the earlier reasoning was incomplete:** the `reqpubkey_sent` ruling settled an **app event**; digest
retirement is an **independently load-bearing state machine**, so carrying one boundary to the other was a design
decision, not an implementation detail.
⚠ **WHAT THE BOUNDARY IS NOT, said in every comment that states it:** a later `DeviceHal::pump_tx` radio-start error
drops the frame AFTER admission and is **outside** the guarantee.
★ **COST CAME IN UNDER THE ESTIMATE, MEASURED BY `offsetof`: +48 B, not +64.** `sizeof(DeferredLbt)` **164 → 176**
(+12/slot × 4); `offsetof(digest_ids) == 12`, `offsetof(buf)` 12 → 24 — it lands in the 4-aligned run before `buf`
and opens no new hole. `sizeof(Node)` **220976 → 221024**, and **ΔRAM = +48 on all three boards**, so the native
measurement holds on both ABIs. The count byte was dropped (`digest_ids[0] == 0` terminates — a live channel id can
never be 0), which is what bought back the 16 bytes.
⚠⚠ **IT RE-ANCHORS SIX SCENARIOS, ATTRIBUTED MECHANICALLY** — s15 · s15_metal · s17 · s28 · s29 · sim_9node_base
(s18 keystone UNMOVED). 3824 beacon defers corpus-wide is the exposure. **s28 settles the mechanism: the entire delta
is ONE `channel_dirty_cleared` moving `t=820265` → `t=820380`** — same node, id, channel, `ad_count`, `reason` —
**115 ms = exactly the LBT defer delay**. s15_metal 32/32 changed lines are `channel_dirty_cleared`, s28 2/2, s29 2/2,
sim_9node_base 6/6; s15 (29/33) and s17 (50/104) carry the expected SECOND-ORDER tail — an entry dirty ~115 ms longer
is re-advertised in the next beacon, so that beacon's content and its receivers' `beacon_rx`/bidi lines shift.
⚠ **AND TX3 COST B51 ITS OBSERVABLE:** once ring entry commits nothing, a ring-full drop and an accepted defer have
**identical** digest outcomes, so the digest can no longer discriminate `tx_flood`'s ring-full return — that poison
reddened nothing. The surviving discriminator is **`beacon_tx.result`** (0 admitted / 2 dropped), now asserted; with
it the step-6 poison reddens 1 case / 2 assertions. ⇒ **fixing one layer can silently remove the observable another
layer's test depended on.**
★ **B51's ring-full fixture now EXISTS and is gate-complete** (the 6-step recipe, with every premise asserted —
`tx_lbt_defer == 4`, `tx_flood_skipped == 0`, and a 1-hop neighbour installed so holder-coverage cannot retire early;
that guard is what my first `DEFER=0` attempt lacked).
**MEASURED** (native; corpus: 0 HAL rejections structurally — the sim's queue is unbounded — and the 6 attributed TX3 movers above).

### B49 — the `CmdCode` self-labelling test had a stale literal bound · FIXED 2026-08-01
`test_console_json.cpp`'s *"every refusal's token begins with `err_`"* loop — the ONLY detector for the §err-reason/B32
convention that `src/fw_main.cpp` prints the token BARE — ran `for (unsigned v = 0; v < 10; ++v)`. The moment `CmdCode`
grew past 9 it stopped testing the new enumerators, while still looking complete. ★ **Its own comment claimed the
opposite** — *"the `ord()` switch above already breaks the build when an enumerator is added, so this cannot go stale
unnoticed"* — which is true of the sibling walker and **false of this loop**, because `-Wswitch` cannot reach a literal
bound. Found by the second QA pass; three enumerators (10/11/12) were already excluded.
★ **CLOSED 2026-08-01 (`§id-hash S1c`): the bound now DERIVES from `ord()`** — walk the full underlying range and let
`kUnlisted` filter, exactly as `check_mapper_covers_every_enumerator` already did — so listing an enumerator in the
`-Wswitch`-guarded walker (which the build forces) automatically enrolls it here. **Bumping the literal was explicitly
rejected: it fails again identically at 13.**
ⓘ **A `_count` sentinel was considered and refused:** adding one to `CmdCode` puts a non-value enumerator into every
`switch` over it, so `-Wswitch` would then demand a `case _count:` arm at each mapper — a permanent tax on the
instrument that is working, to fix the one that is not.
**MEASURED** (native): mis-naming enumerator 12 is caught by the derived bound (1 assertion) and is **completely
invisible** with the old `v < 10` restored — 0 failures. The under-cover demonstrated, not argued.

### B52 — the JSON address book carries the TEAM plane's confidence but still NOT the STATIC plane's · NEW 2026-08-02
`§id-hash S3` added `"team_auth"` to `write_peer_row` (`lib/console/console_json.cpp`), so an app can finally tell
*"we heard that teammate's own beacon"* from *"somebody told us her number"*. **`static_id` is still emitted bare.**
The row already carries `PeerBookRow::static_authoritative` and the TEXT console has rendered `static_id=N(auth)` /
`(claimed)` since §id-hash S2 — only the JSON drops it. ⚠ **This is not hypothetical on the static plane the way it
is on the team plane: a relayed soft H answer lands `IdBindConf::claimed` in `_id_bind` TODAY**
(`on_hash_bind_snoop` → `id_bind_set(..., h_relay, claimed)`), and every such row reaches the companion as an
unlabelled `static_id`. An app that treats an id as identity is therefore already able to be wrong, on the plane that
has had the ladder longest.
★ **NOT FIXED IN S3 on purpose (C1):** it is a second shipped-contract change, and S3's contract with its own gate is
inertness. One line beside the `team_auth` one, plus its contract paragraph.
ⓘ **OWED REGARDLESS: `ios-companion/INBOX_SYNC_CONTRACT.md` has no `team_auth` entry** — QA owns that file, a coder
never edits it. The field contract as built is documented in `lib/console/console_json.h` beside `write_peer_row`.
**MEASURED** (native): the presence/absence rule is pinned three ways in `test_console_json.cpp` — `team_auth` rides
with `team_id` always, a static-only row's line is byte-identical to its pre-S3 golden, and true/false render
distinctly.

### B53 — inspection resolved ids at the authoritative rather than claimed floor · FIXED 2026-08-02
Spec `2026-08-01-id-to-hash-resolution-design.md` §3-D6 sets the display and pubkey-inspection floor at **`claimed`**
(*"shows a claim, labelled as one"*; *"the pubkey self-verifies against that hash, so fetching it is how you inspect a
claim"*). `Node::peer_book_by_id` — the ONE resolver behind all three verbs since §id-hash S1 — passes the
**`authoritative`** default on both arms. ⇒ a claimed STATIC binding is invisible to `hashof <id>` today, and the
team plane will inherit the same blindness the moment S4a writes its first claimed row.
★ **DELIBERATELY NOT CHANGED IN S3, and the reason is the gate:** lowering it is **not inert** — claimed static rows
exist in the live tree now, so `hashof` would start answering for them and `reqpubkey <id>` would start spending
**AIRTIME** at a hash the operator was never shown. Spec §6's S3 row requires *"s18 keystone reproduces by
construction (defaults)"*, which that would break.
⚠ **WHEN IT LANDS (S4a) IT MUST MOVE ON BOTH ARMS TOGETHER.** A resolver that filters one plane harder than the other
is spec §1-C's asymmetry defect rebuilt, and §1-C is one of the five defects this whole arc exists to remove.
ⓘ Already prepared: both arms read the accessor's `actual` and propagate it into
`static_authoritative`/`team_authoritative` instead of hardcoding, so the display cannot start lying when the floor
moves. `node_hashlocate.cpp`'s in-source note states the whole of this beside the code.
**MEASURED** (native): a test pins the present boundary (a `claimed` team binding returns mask 0 from
`peer_book_by_id`) precisely so S4a's change shows up as a failing assertion rather than a silent one.
★ **CLOSED 2026-08-02 by slice `§id-hash S4a` (green, UNCOMMITTED)** — and the planted assertion did exactly its job:
three test cases went red on the first build and were rewritten to the new contract, so the change is a visible diff.
**BOTH arms moved together**, as this entry required. ⚠ **What did NOT move, and the distinction is the trust model:**
`key_hash_of_id` / `team_key_of_id` / `team_id_of_key` keep their `authoritative` DEFAULT, so DST_HASH stamping,
sealing and `team grantkey` still refuse a claim (spec §3-D6/D7). Only the display + pubkey-inspection resolver was
lowered, and `actual` (prepared by S3) means the row renders `(claimed)` rather than `(auth)`.
⚠ **THE JSON BOOK IS UNAFFECTED, verified at source rather than assumed:** the app's rows are built by
`peer_book_join_ids`, which resolves through `id_bind_find_by_hash` / `team_id_of_key_freshest` — **neither takes a
floor** — so lowering `peer_book_by_id` cannot leak an unlabelled claim to the companion. **B52's scope is unchanged.**
**MEASURED** (corpus): reverting both arms to `authoritative` under the full S4a tree moves **0/36** — `peer_book_by_id`
has no simulator-reachable caller on the by-id path (`NodeRuntimeWrapper.cpp` parses only `reqpubkey <hex>`), so this
is native-gated by construction. Positive control in the same file: poisoning the statement the by-id branch feeds
(`answer_hash`) moves **10/36** with **37 assertion failures**.

### B54 — the FIRST claim into a FULL first-hand `_team_keys` still evicts one beacon row · NEW 2026-08-02
Spec §3-D5c requires eviction to *"prefer a claimed victim over any authoritative row"*, and `team_key_set` now does:
it drains the claimed cohort completely before it will consider a first-hand row. **Residual:** with all 16 slots
first-hand and no claim yet resident, the fallback is still oldest-wins, so the **first** claimed insert costs one
genuine beacon row. Every claim after it consumes only the previous claim.
**Bound:** exactly **one** row per storm, re-learned on that teammate's next beacon; and it needs 16 simultaneously
live teammates to be reachable at all.
★ **NOT WIDENED IN S3 (C2 cuts both ways here):** refusing the insert outright is a *stricter* policy than the spec
asked for, and choosing it belongs to the slice that actually creates claimed writes (S4a) or to the owner — not to a
slice whose contract is inertness. Recorded with its number so the decision is made, not inherited.
**MEASURED** (native): both halves are asserted — the fallback eviction (id 1 is displaced by the first claim) and
the cohort rule (a 16-frame storm afterwards leaves every first-hand row intact).
★ **STILL OPEN, AND S4a — THE SLICE THIS DECISION WAS DEFERRED TO — DELIBERATELY DID NOT WIDEN IT (2026-08-02).**
Reasoning, so the decision is made rather than inherited again: refusing the first claimed insert outright would make
the by-id answer **the operator explicitly asked for** the one write that silently does nothing, which trades a
one-row cost for a silent failure — the worse of the two (C2 cuts toward *reporting*, and there is nothing to report
here). Cost stands at **exactly one** first-hand row per storm, re-learned on that teammate's next beacon, and it
needs **16 simultaneously-live teammates** to be reachable at all.
**MEASURED** (corpus, S4a): **unreachable today** — the largest team scenario inserts 12 `_team_keys` rows against a
16-slot table, so no eviction of any kind occurs in the 36-scenario corpus. Native keeps both halves pinned, and S4a
added a third case driving the residual through the real ingest path rather than the direct setter.
⇒ **owner call if it should become a refusal; it is not a coder's to widen.**

### B55 — `reqpubkey_sent.hash == 0` is a NEW app-visible meaning that the companion contract does not describe · OPEN, MUTATED BY S4b 2026-08-02
`§id-hash S4a` gave a by-id `reqpubkey` a **two-stage** shape: when the id has no binding, the frame that flies is the
**id→hash** query, not the pubkey request. The BLE event still fires (the TX path did accept a frame — the 2026-08-01
owner ruling's meaning), but it carries **`"hash":0`**, and that value is the only thing distinguishing stage 1 from
stage 2. An app that treats `reqpubkey_sent` as *"a pubkey is coming"* will now wait for one that is not.
★ **The firmware side is built and documented in `lib/console/console_json.h` beside `write_reqpubkey_sent`.** What is
owed is the contract paragraph — plus, ideally, the app behaviour: on `hash == 0`, re-issue `reqpubkey <id>` once the
binding lands (or simply wait for **S4b**, which does the second stage on-node and removes the case entirely).
ⓘ **Two smaller contract deltas ride with it:** (a) `err_ambiguous_plane` now has a SECOND cause — an *unresolved* id
on a node that lives on both planes, where the by-id query itself must pick one (before, it meant only "both planes
hold this number"); (b) `err_no_binding` on a bare/`-t` id is now reachable from exactly ONE place, an explicit `-t`
on a node with `team_id == 0`.
ⓘ **OWED, NOT WRITTEN: `ios-companion/INBOX_SYNC_CONTRACT.md` is QA-owned and a coder never edits it.** Stacked with
**B52**'s owed `team_auth` line — one documentation pass covers both.
**MEASURED** (native): the stage-1 result is pinned (`code == queued`, `accepted`, `dst_hash == 0`, `plane == 1`) with
a same-fixture control proving the resolved case still carries the real hash.

★★★ **UPDATED BY `§id-hash S4b` (2026-08-02) — AND THE DISPATCH'S EXPECTATION THAT THIS ENTRY WOULD CLOSE IS WRONG,
REPORTED RATHER THAN QUIETLY TICKED.** S4b makes the node perform stage 2 itself, so the brief predicted the
`hash == 0` case would "disappear for the normal path". **It does not, and it must not.** The value is the *honest
report of a real stage-1 acceptance*: the frame the TX path took **was** the id→hash query, and the hash is precisely
what that frame went to ask for — a synchronous `CmdResult` cannot carry a value that does not exist yet. Removing the
case would require either suppressing a true event (re-creating the silence S1b was built to remove) or inventing a
hash. This is the same wall the `aired`→`accepted` rename hit, one level up: **an acknowledgement may only claim what
it can know.**
⇒ **WHAT ACTUALLY CHANGED IS THE INSTRUCTION, WITH THE BYTES UNMOVED** — and that makes the owed contract text a
CORRECTION, not an addition:
· **was** (S4a): `hash == 0` ⇒ *"expect no pubkey; re-issue `reqpubkey <id>` once the binding lands."*
· **is** (S4b): `hash == 0` ⇒ *"do NOT re-issue — the node consumes the answer and emits the pubkey request itself."*
⚠ **LIVE HAZARD FOR AN ALREADY-WRITTEN APP:** a companion coded against the S4a wording now fires a redundant second
`reqpubkey` while the node is already escalating — duplicate airtime for one question. Harmless (dedup + the intent
refresh absorb it) but wasteful, and it is exactly the kind of drift that made this entry necessary.
★ **The two continuations the app CAN rely on:** `peer_key_cached` (the whole workflow completed — an existing push,
no contract change) or nothing, bounded by S4b's timeout. **A stage-2 FAILURE is not app-visible at all — that is
B56.**
ⓘ **STILL OWED, STILL NOT WRITTEN: `ios-companion/INBOX_SYNC_CONTRACT.md` is QA-owned.** Stacked with **B52**'s
`team_auth` line and now **B56**'s decision — one documentation pass covers all three.
**MEASURED** (native, S4b): the stage-1 ack is re-pinned unchanged (`queued` / `accepted` / `dst_hash == 0`), with a
same-fixture control proving the *second stage* then flies by hash without a second command.

### B56 — a STAGE-2 `reqpubkey` failure never reaches the app · NEW 2026-08-02
`§id-hash S4b` completes the by-id workflow on-node, so the pubkey request is emitted from an **RX callback** — with
no command in scope. `reqpubkey_sent` is written only on the SYNCHRONOUS BLE command path (`src/fw_main.cpp`), so a
stage-2 refusal (`degenerate` — the answer named our own hash; `tx_dropped` — a bounded TX queue) and the bounded
**timeout** both reach the operator console (`!!`-prefixed `_hal.log`, prints under `debug off`) and telemetry, and
**not the companion**. The app sees the stage-1 `reqpubkey_sent{hash:0}` and then either `peer_key_cached` (success)
or silence.
★ **NOT A REGRESSION — a gap S4b makes reachable and does not widen.** Before S4b the app was told to re-issue by
hand, so the silence was covered by the operator's second command; now the second command is gone, so the silence is
the whole failure report.
**THE FIX IS AN APP-CONTRACT DECISION, WHICH IS WHY A CODER DID NOT TAKE IT:** it needs a new `PushKind` (appended, per
`command.h`'s enum rule) — plus its name in the sim's `ConsoleNames.cpp`/`NodeRuntimeWrapper.cpp` bridge, i.e. a
SECOND repo. ⚠ **The two obvious reuses are both wrong and are recorded so they are not re-proposed:** `send_failed`
would render a *failed message* for a command that sent none, and `hash_resolved` keys on a hash we never learned.
**MEASURED** (native): the console/telemetry half is asserted both ways — the `!!` line and its `MR_EMIT` fire on a
degenerate stage 2 and on the timeout, with a same-fixture control proving neither fires on the success path.

### B57 — a beacon-resolved id does not complete a pending `reqpubkey` · PARKED / DELIBERATE 2026-08-02
`§id-hash S4b`'s intent is consumed only in `on_hash_bind_response` (spec §5 step 3 names the *answer*). If the id→hash
binding instead arrives from a heard **beacon** — `node_beacon.cpp`'s `_id_bind` / `_team_keys` writers — the intent
sits until its bounded timeout, and the operator must re-issue (which then resolves immediately from the
beacon-learned row).
★ **DELIBERATE, WITH THE COST PRICED:** the hook would sit on the hottest corpus path in the tree, re-anchoring all 36
streams for an ergonomic gain on the case the by-id query exists precisely because it does **not** cover — an id we
route to but have never heard, i.e. one no beacon is arriving for. `drain_resolved_parked_sends` is the precedent for
the beacon-triggered shape if this is ever wanted; it would be its own slice (C1) and its own re-anchor (C4).
**MEASURED** (native): the answer-driven consume is pinned; the beacon path is asserted only as *not* consuming, via
the timeout test's late-answer arm.

### B58 — the intent ring and the LBT defer ring cannot both be exercised by one command · NEW 2026-08-02 · NOT A DEFECT, A TEST-DESIGN CONSTRAINT
`§id-hash S4b` refuses a full intent ring (`err_resolve_pending_full`) **before** `emit_hash_query`, which is the
correct order (D9: never spend airtime on a decision that is going to be refused). A consequence: with
`cap_pending_id_pubkey == 4` and the shared LBT defer ring also 4 slots, four by-id commands fill the intent ring
first, so **a fifth by-id command can never reach the TX-rejection path**. The LBT-drop fixture therefore has to fill
the defer ring through the **by-hash** door.
★ Registered because it is invisible from the source and cost a test iteration to find: a future slice that changes
either capacity, or the refusal order, silently changes which fixtures can reach which failure. **If the two caps ever
need to be exercised together, the honest lever is `cap_pending_id_pubkey`, not the refusal order.**
**MEASURED** (native): the by-hash-filled fixture reaches `err_tx_queue_full` and proves the intent is unwound; the
by-id-filled attempt returns `err_resolve_pending_full` instead, which is how the constraint was found.

### B59 — a relay can ACK custody, start route repair, then discard the transit DATA before that repair can help · NEW 2026-08-03 · PARKED — POTENTIAL ROUTING-ALGORITHM CHANGE
**METAL-CONFIRMED** on the static four-node topology `42 — 186 — 109 — 48`, with weak/asymmetric links. Node 48's
by-hash `reqpubkey 0x8CC9BDFF` reached owner 42. Owner 42 generated authoritative pubkey answer DATA `ctr=3598` and
sent it to relay 186. Relay 186 received and cached the answer, ACKed 42, then exhausted RTS attempts on its selected
direct next hop 48. It emitted an RREQ for 48, performed the one-way slow reprobe, and terminally reported
`FAILED ctr=3598 (no CTS — next hop silent)`. On the same node, the valid RREP arrived immediately *after* that
failure and installed `186 → 42 → 109 → 48`; requester 48 never received `ctr=3598`.

The failure is the interaction between two existing routing policies, not a broken hash-answer encoder. In
`cascade_to_alt`, §P3 can emit asynchronous route discovery when the exhausted next hop is silent, but the MF4
`LinkBidi::one_way` arm then bypasses `try_cascade_requeue`: one final probe is allowed, and a failure inside the
reprobe throttle window calls `giveup_flight`. The upstream ACK has already transferred custody, so neither owner 42
nor requester 48 receives the relay's terminal outcome.

★ **CONTROL, SAME METAL TOPOLOGY:** after the RREP had installed the repaired routes and the H-query dedup window had
expired, node 48 repeated the command. Owner 42's new answer DATA `ctr=3602` selected `42 → 109 → 48`, survived ordinary
RTS/DATA retries and was ACKed at node 48, which stored `0x8CC9BDFF` as authoritative (`nv=inserted`). This proves the
first frame was not merely delayed: route repair benefited only the later transmission.

★ **A HOLD-AND-RETRY PATCH IS NOT SUFFICIENT FOR THIS EXACT CASE.** At relay 186 the repaired route begins with node
42, which is the transit frame's `previous_hop`; `next_hop_selectable` deliberately rejects it to prevent loop-back.
Removing that guard is not an acceptable local fix. Retaining the frame while RREQ is outstanding would help only
when discovery returns a selectable non-previous-hop route. Rescuing this topology may require explicit custody
return/NACK semantics, a narrowly proven route-repair turnaround rule, or origin-level retry — all are routing
algorithm/protocol decisions with possible loop, duplicate, airtime and corpus-wide consequences.

★★ **OWNER RULING 2026-08-03: REGISTER, DO NOT DISPATCH.** The behavior is real and potentially affects any forwarded
DATA that reaches this combined silent + one-way + no-selectable-alt state, but no solution is approved and there is
currently no requirement to address it. Do not implement a one-line relaxation, remove the previous-hop guard, or
extend retries under B59 without a separate routing design and adversarial loop/duplicate/airtime evaluation.

### B60 — multi-gateway `send_layer` resolves the FINAL hash on the first intermediate layer and never selects the next gateway · NEW 2026-08-03 · OPEN — SPEC READY; ACK POLICY OPEN
**METAL-CONFIRMED** on the explicit route `7 → 5 → 6`. A registered mobile on leaf 7 issued
`send_layer 0x7B18ADA2 5,6 "Test multi layer 1"` for a mobile hosted on leaf 6. The mobile correctly sent a
`DATA_TYPE_MOBILE_SEND` wrapper to home 177; the home correctly re-originated the preserved path `[7,5,6]` toward
the gateway bridging layers 7/5 (local node IDs 7/8). After entering leaf 5, however, that gateway repeatedly emitted
`H leaf=5 hash=7B18ADA2`. The destination and its home were on leaf 6, so no valid leaf-5 answer existed and the
message never reached gateway 5/6.

The path and cursor are not lost. `originate_layer_path` prepends the source layer correctly, and
`bridge_cross_layer` advances `cur` when another entry remains. The defect is the handoff's destination decision:
`bridge_cross_layer` always calls `id_on_leaf_by_hash` / `mobile_home_on_leaf` for the final `dst_hash` on the layer
being entered, even when `cur + 1 < n_layers`. The unresolved handoff then reaches
`drain_xl_handoffs_for_leaf`, where `dst_node_id == 0` has only one meaning — *resolve the final recipient* — so it
H-floods that hash on the intermediate layer. No branch reads the advanced cursor and calls
`select_gateway_for_leaf` for the next path entry.

**Expected intermediate behavior:** on leaf 5, preserve the final hash but MAC-address the relay leg to the gateway
that serves leaf 6. Only the final gateway, after entering leaf 6, may resolve/H-flood `0x7B18ADA2` and route it to
the destination or its mobile home. A final-hash binding accidentally present on leaf 5 must not alter that decision.

★★ **WHY TESTS WERE GREEN:** the native two-hop explicit-path test asserts only that origination encodes
`[source,h0,h1]` with `cur=1` and sends it to the first gateway. It never drives a handoff through two gateways.
The source comment says multi-gateway transit “just works,” while the bridge comment still calls it “reserved”; the
original gateway design explicitly left it outside v1. This is a coverage and documentation contradiction, not a
radio-loss diagnosis.

★★ **OWNER REQUIREMENT 2026-08-03:** the repair must support an explicit route of up to **16 total layer entries,
including the source** — therefore at most **15 user-supplied `send_layer` hops**. The current independent cap is four
total entries (`gw_env_max_hops=4`), and `CmdResult::layer_path` can echo only four bytes, so widening the codec array
alone is not a complete implementation. The parser, command/result carriers, mobile wrapper/home re-origination,
cursor bridge, reversed E2E ACK policy, memory gates and tests must move together. Gateway selection and the wire leaf
gate use `layer_id & 0x0F` in the current layer's local context, so **non-adjacent repeated nibbles are valid**. Only
an adjacent same-nibble transition is structurally impossible: `validate_gateway_layers` already refuses a gateway
whose own two leaves alias. The old `gw_env_max_hops` symbol must be deleted so the compiler exposes every four-entry
assumption rather than silently widening missed validation guards.

**OPEN OWNER DECISION B60-O1:** long-path `-a` admission/deadline policy. Linear scaling reaches 75 minutes and can
occupy all eight ACK slots; keeping 300 seconds can false-timeout. The design recommends bounded ACK depth until
resources are redesigned.

**Implementation design:** `docs/superpowers/specs/2026-08-03-multi-gateway-explicit-layer-path-routing-design.md`.
This is **not B59** and must not relax ordinary route loop guards, custody, cascade selection or previous-hop
rejection. The next gateway is supplied by the explicit layer path and selected with the existing per-leaf gateway
directory; ordinary routing is reused only to reach that selected gateway within the active leaf.

### B61 — `board_name()` silently reports `"native"` when a board env forgets its `-DBOARD_*` macro · NEW 2026-08-03 · ✅ FIXED 2026-08-03 (own commit, UNCOMMITTED)
**MEASURED** during the §board-split Phase-0 assessment (`simulation/BASELINE.md`, 2026-08-03 note). `src/firmware_commands.cpp:339-349`
is the **only** genuinely board-discriminating code in the firmware — 3 of the 26 board-macro conditional sites, all in this one
`#if/#elif/#elif` chain — and its `#else` arm returns `"native"`:

```c
const char* board_name() {
#if defined(BOARD_XIAO_WIO_SX1262)   return "xiao_nrf52";
#elif defined(BOARD_XIAO_ESP32S3)    return "xiao_esp32s3";
#elif defined(BOARD_HELTEC_V3)       return "heltec_v3";
#else                                return "native";
#endif
}
```

⚠ **That `#else` is unreachable in all eleven envs** — `firmware_commands.cpp` is excluded from `[env:native]`'s
`build_src_filter` (`platformio.ini:73`) and `test_build_src = no`, so every env that compiles it defines exactly one
`BOARD_*`. ⇒ it is not a host arm; it is a **silent fallback**, and per **C2** it should be `#error`.

**Why it matters now, not academically:** the whole point of the board-source-split work is that *adding a board becomes
adding a directory*. A new board env that omits its `-DBOARD_*` **still links and still boots**, and reports `board=native`
in the `version` banner and in `print_banner` (`:352-358`) — a provenance lie in exactly the diagnostic a bench operator
trusts to tell two boards apart. **Fix:** replace the `#else` with `#error "no BOARD_* defined — add one to this env"`.
Cost is one line; it converts a mislabelled image into a build failure. **Flash/RAM impact nil** (the arm is never compiled).

**✅ FIX LANDED 2026-08-03 (own commit, uncommitted).** `#else return "native"` → `#elif defined(MESHROUTE_NATIVE) return
"native"` + `#else #error "No supported BOARD_* or MESHROUTE_NATIVE target selected"`, plus an 8-line comment recording why
the old arm was dead rather than host-facing. **Precondition re-verified independently, not taken on trust:** all four
target macros are declared exactly once (`platformio.ini:79 / :134 / :218 / :263`); all **seven** extending envs re-list
`${env:<board>.build_flags}` (`:324 :331 :338 :357 :369 :375 :381`) despite `:307`'s warning that `extends` does not
auto-merge them; the simulator never compiles `src/`; and there is **no `__LINE__` and no `assert(` anywhere in
`firmware_commands.cpp`**, so the +12-line shift cannot perturb emitted code.

★ **GATE — 11/11 envs rc=0** (object counts 23 / 194 / 284, none zero). Native **1149 cases / 72681 assertions / 0
failed** from the RUNNER's stdout; s18 keystone **`1cd21235`/271629 EXACT**; **36/36 corpus scenarios byte-identical to
the pre-fix run and 0 assertion failures in every one** (s18 is inert by construction — the sim compiles `lib/`, not
`src/`). **Symbol multisets identical on all eleven**, warning counts identical on all eleven, `-Wswitch` **0 ×11**.

⚠⚠ **I PREDICTED "byte-identical on every board env" AND THAT PREDICTION WAS WRONG — recorded because the correction is
the useful part.** Against the pre-slice eleven-env baseline, 8 of 11 envs showed one moving section: the four ESP32 envs
`.debug_line` **+3 B** (DWARF, **not flash-bearing**, pio RAM/Flash unchanged), and `xiao_sx1262` `.text` **−32 B** /
`gateway` `.text` **+32 B** — opposite signs, with symbol multisets identical.

★ **The ±32 B is NOT this change, and that is measured rather than argued.** Reverting `board_name()` to its exact
pre-fix form and rebuilding still yields `.text` **511044** — the *same* value as the fixed build, i.e. **−32 vs the
original baseline even with the fix removed**. ⇒ the offset is a build-environment artefact that survives full reversion.
Two builds of identical source *within one session* reproduce exactly (`.text`, flash 512020, symbols — **0 differing
sections**), so it is not per-build noise either; it is a **one-time, content-dependent step** consistent with
`__DATE__`/`__TIME__` literal packing in the banner strings (`firmware_commands.cpp:354` / `fw_main.cpp:427`), which on
this ld script land in `.text`. **That is exactly the "±32 B `__DATE__`/`__TIME__` flash floor" the QA brief named — now
measured on ARM, where it is invisible in the symbol multiset.** `gateway`'s +32 is the same artefact class by analogy
(it is `extends env:xiao_sx1262`, identical symbol multiset); **verified directly only on `xiao_sx1262`.**

★★ **THE GATE THAT ACTUALLY DECIDES B61 — pre-fix source vs post-fix source, both built in the SAME session:**
**0 differing sections · symbol multiset identical · RAM 167044 identical · Flash 512020 identical · warnings identical ·
`-Wswitch` 0.** ⇒ the fix is provably **code-neutral**, as the dead-arm reasoning predicted.

⇒ ★ **METHOD CONSEQUENCE for the next slice: an eleven-env baseline is only a valid comparand for builds made in the
same session.** A0 must rebuild its own BEFORE arm rather than diff against the 2026-08-03 grid, or ±32 B of banner-string
packing will be misattributed to the file move — on top of the ~192 B the move genuinely costs.

★★ **POSITIVE CONTROL — the `#error` was PROVEN to fire, because 11/11 green is otherwise a 0/11 that cannot be read.**
Rebuilding `heltec_v3` with `PLATFORMIO_BUILD_UNFLAGS` dropping `-DBOARD_HELTEC_V3` fails at **`src/firmware_commands.cpp:357`**
with exactly `error: #error "No supported BOARD_* or MESHROUTE_NATIVE target selected"`, and `-Werror=return-type` fires
behind it as a second independent guard. ⇒ the guard is live, not decorative, and the green grid means "every env
satisfies an arm", not "the instrument cannot fire".

ⓘ **Honest residue:** the new `MESHROUTE_NATIVE` arm is itself still **unreachable in all eleven envs**, because
`[env:native]` does not compile this TU. It exists so a future host build that *does* compile it has a legitimate answer
instead of tripping the `#error` — not because anything reaches it today.

### B62 — three in-source paths still say `src/board_ui.cpp` after §A0 moved it · NEW 2026-08-03 · OPEN (one-line fix, deliberately deferred)
**MEASURED / created by §A0** (`simulation/BASELINE.md`, 2026-08-03 §A0 note). The file now lives at
`variants/heltec_v3/board_ui.cpp`, but three places still name the old path:

| site | text |
|---|---|
| `variants/heltec_v3/board_ui.cpp:1` | `// MeshRoute — src/board_ui.cpp` — **the file's own path header lies about itself** |
| `lib/hal/mr_ui.h:5` | *"implement these three hooks in a TU compiled under `#if MR_FEAT_OLED` (src/board_ui.cpp)"* |
| ✅ plan File-Structure table + Tasks 5/9 | **ALREADY CORRECTED 2026-08-03 by QA** — this row was stale when written; the table now reads `variants/heltec_v3/board_ui.{cpp,h}` and the `-I variants/heltec_v3` was moved to **Task 5**, where it first becomes load-bearing |
| ✅ `lib/hal/mr_ui.h:5` | **FIXED 2026-08-03** — now names the new path and states the port is per-BOARD (V4 brings its own) |
| ✅ plan architecture paragraph (`:7`) | **FIXED 2026-08-03** — flags that only the board-INDEPENDENT units remain in `src/` |
| ⏳ `variants/heltec_v3/board_ui.cpp:1` | `// MeshRoute — src/board_ui.cpp` — **the file's own path header lies about itself.** ★★ **DELIBERATELY STILL OPEN, and the reason is a real conflict with the QA recommendation to "clean B62 before the owner commit": editing this file makes A0 STOP BEING A 100 % RENAME**, which is the property the A0 approval was verified against (`R100`, 0 insertions, 0 deletions). ⇒ **fix it in the FIRST commit AFTER A0 lands** — the OLED slice touches this file anyway (Task 9)

⚠ **Not an oversight — A0's dispatch scoped it out** (*"⛔ Nothing else moves. No content edit"*, C1: never fold a
semantic/textual edit into a file move). `platformio.ini:221`'s comment **was** fixed, because it sits inside the file A0
rewires. **Fix:** the Phase-A OLED slice rewrites `board_ui.cpp` in full — correct line 1 and `mr_ui.h:5` there, and
update the plan's file table when Task 5 lands `board_ui.h` in `variants/heltec_v3/` (which is also when the `-I
variants/heltec_v3` the A0 brief asked for genuinely becomes necessary — see the §A0 note's premise 2). Zero build
impact; a stale path header in a board-port file is exactly what the "code is read, docs rot" rule is about.

### B63 — RECORDED (gate methodology, not a defect): on xtensa, adding or removing a **zero-byte** object from the link set moves `.flash.text` by up to +176 B · NEW 2026-08-03
**MEASURED and decoupled by probe** in the §A0 note. `src/board_ui.cpp` compiled to an object with **`.text` 0 /
`.data` 0 / `.bss` 0** on every non-Heltec env (`MR_FEAT_OLED` defaults 0). Dropping that empty object from the three
`xiao_esp32s3`-family link lines nevertheless moved `.dram0.bss` **−8**, `.flash.rodata` **−8** and `.flash.text`
**+176 / +56 / +132**, and resized ~30 **Arduino/ESP-IDF framework** functions (`WiFiGenericClass::mode`,
`STAClass::connect`, `APClass::create`, `NetworkClient::write`, …) — **no symbol of ours affected**. Putting the same
file back into the link set *from its new directory* reproduced the pre-move image **section-for-section and
symbol-for-symbol** (P-A0-2) ⇒ **the source's directory is irrelevant; link-set membership is the entire effect**, and
it is xtensa link-order relaxation. **The identical removal is free on ARM** (4 nRF52 envs: 0 differing sections,
identical symbols, identical RAM and Flash).

⇒ ★ **Consequence for every future size gate:** *"an object entered or left the link set"* is an xtensa-only **±200 B
flash / ∓8 B RAM** event **even when the object is empty**, and it is visible in the symbol multiset only as framework
functions changing size. Do not attribute it to the slice's own code, and do not expect the ARM envs to corroborate it.
⚠ It also **retires the "+192 B is the price of moving a file out of `src/`" rule** recorded on 2026-08-03: that number
came from relocating `device_ota.cpp` (123 lines, a 227 KB object), i.e. from reordering **non-empty** objects.

---

## Deferred with an explicit trigger


### D1 — the team **DV hop-cap flip** (T3 Part C)
`node_beacon.cpp:861` still reads `hop_cap_for(false)`, so team RREQ floods at `team_hop_cap` **8** while team DV
accepts combined hops to `dv_hop_cap` **16** — a deviation from **R4**. Measured **inert on 34/36**, and no value
of `team_hop_cap` restores `s35a`/`s38` (1→5 fails, 2→3, 3/4/8→9; **the window is empty**) because with one cap a
node's DV reach equals its RREQ reach and radius-clipping dies as a test method. ★ **TRIGGER TO REVISIT: the first
time any team scenario produces a team DV path of >8 combined hops.** Part B (`team_hop_cap`'s config surface)
already shipped, so the flip is a one-token change on that day. Note: `T3`.

### D2 — the **read-path** plane audit
§10.3's plane audit was scoped to **write** sites and was therefore structurally blind to the s38 breach, which
entered through `rt_find(…, AUTO)` degrading to `_rt`. **Every plane-typed lookup that can silently fall back to
the static table needs the treatment the write sites got.** Spec `2026-07-27-…-routing-parity-design.md` §12.

---

## Owner decisions pending

| | decision | cost of the fix |
|---|---|---|
| ~~**O1**~~ | ✅ **RESOLVED — B1 CLOSED 2026-07-31** (`§team-target-whole`); the gate was dropped and it went wider than the one line (`team 12abc` too). ⚠ Its **range** sibling is still open as **B17** |
| **O2** | `noinline` on `deleg_ack_put`? ⚠ **Superseded in detail by B19** — measured **8** call sites and **≈4 KB**, not 5 and 1.8 KB, and it must **fold into B12**. ★ **PARKED: `gateway` flash is 54.9%, so there is no pressure behind it** | one line, but re-codegens 8 sites |
| ~~**O3**~~ | ✅ **RULED 2026-07-31 (owner) — THE KEY LIVES EXACTLY AS LONG AS THE `team_id` IT WAS GRANTED FOR.**
**`set_team_id` must CLEAR `team_ch_pub`/`team_ch_priv`/`team_ch_key_present` whenever `team_id` actually changes,
including `team 0`.** QA-verified the current state: `set_team_id` already clears routes, the peer set, liveness, the
**peer** key cache and the DAD id — but **not** the channel keypair, and that key is **UNRECOVERABLE (no seed derives
it)**. ★ **Fails safe:** after a switch the member holds no key, so a post refuses `team_channel_no_key` and the app
prompts *"ask a teammate for the key"* — the flow T-K2 already defines. **Cost accepted:** switching away and back needs
one re-grant, which is precisely what T-K3 exists for. ⚠ **`create`/`join` must STILL PRESERVE the key** — they do not
change `team_id`, so the rule does not touch them, and `blob_take_team_channel_key` stays as built.
★★ **The rejected alternative, recorded so it is not re-proposed:** tagging the key with its own `team_id` and refusing
on mismatch would never destroy the key — but it needs a **new persisted field ⇒ a `kVersion` bump ⇒ a THIRD reprovision
event** on top of the two already stacked, for a rare case. **⇒ CL2 IS UNBLOCKED.** |
| **O4** | The **BLE console exposure** is no longer a watch-item — under the `team exportkey` ruling it is **the only control protecting the team content key.** Closing it (pairing / auth gate / console allow-list) makes "any transport" safe | its own slice |

---

## How to use this file

1. **Pick a tier, not a line.** Tier 2 before Tier 3; anything that fails *silently* jumps the queue.
2. **Read the named `BASELINE.md` note first** — it has the measurement, the probe matrix and the reason the
   fixing slice declined.
3. **Re-locate every symbol.** See the warning at the top.
4. **Close the entry here in the same commit as the fix**, or this file becomes the next thing that rots — which
   is precisely what it exists to prevent.
