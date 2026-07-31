<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# The node role model — when a node is MOBILE and when it is STATIC

*Opened 2026-07-31 at the owner's request: **"Let's make then a clean proposal — when node get mobile role
(consistently!) and when static role."*** Written after the B28 ruling (*"`is_mobile` should be automatically set when
team is in use"*) exposed that the role's **transitions** are, today, five different mechanisms that disagree.

★ **This is a DESIGN RECORD, not a work order.** §1 is code-verified; §2 is the proposal; §4 holds the three decisions
that are the owner's. Nothing here is implemented.

---

## 0. Why this needs a rule set rather than a patch

The owner's B28 ruling is right and narrow: `team_id != 0` must imply `is_mobile`. But answering *"then how do we move
from mobile back to static?"* took **four** code paths to establish, and they behave differently — one is live, one is
reboot-to-apply, one preserves the role, one zeroes it. ⇒ **the inconsistency is not in one command, it is in the
absence of a model.** Patching B28 alone would add a sixth behaviour.

## 1. Present state (code-verified 2026-07-31)

### 1.1 ★★ The role is large, and it is ON THE WIRE

`is_mobile` has **216 references in `lib/core/` and 23 in `src/`**, across 12+ translation units — the heaviest
concentrations being `node_beacon.cpp` (39), `node.h` (22), `node_mobile.cpp` (17), `node_mac_rx.cpp` (16),
`node_join.cpp` (14).

★★ **And it is advertised:** `frame_codec.cpp:92` unpacks it from a **beacon** (bit `0x20`) and `:767` from a **join
frame** (bit `0x40`). `node_beacon.cpp:313` and `node_join.cpp:152/:381` pack it. ⇒ **peers make decisions from our
role**, so a role change is an **announcement**, not a local preference. Any transition that does not re-announce
leaves the mesh believing the old role.

### 1.2 Five write paths, five different behaviours

| # | path | sets `is_mobile` | sets `team_id` | clears role state? | live or reboot? |
|---|---|---|---|---|---|
| 1 | **`cfg set mobile 0\|1`** (`firmware_config.cpp:220`) | **yes, raw flip** | no | **nothing** | ⚠ **reboot-to-apply** |
| 2 | **`team <id>` / `team new`** (`handle_team`) | ⚠ **NO** | yes, via `set_team_id` | clears the **team** plane | **live** |
| 3 | **`join` / `create`** (`:494`) | ⚠ **PRESERVES** | ⚠ **PRESERVES** (+ `team_local_id` + the team channel key, *unrecoverable if dropped*) | clears **learned** mobile state (`_my_mobile_reg` via `clear_routing_state`) | live |
| 4 | **`leave`** (`:1049`) | **yes → 0** | **yes → 0** | **everything** (`b = mrnv::Blob{}`, keeps only freq/PHY defaults) | live, → unprovisioned + idle |
| 5 | **NV boot** (`fw_main.cpp:603-604`) | restores | restores | n/a | boot |

★ **Only path 4 is a coherent role transition.** It is also the only one that changes `is_mobile` and `team_id`
**together**, which is why it can never produce the config B28 outlaws.

⚠ **`join`/`create` are NOT role changes** — the comment is explicit (*"§mobile: preserve team + autoreg + team-DAD id
across create/join"*). They re-provision the *network*, keeping the *role*. This surprised the owner and is worth
stating loudly: **provisioning ≠ role.**

⚠ **The `mobile` verb cannot promote** — `handle_mobile` (`:970`) and its `register`/`gateways`/`query`/`status`
subcommands all *require* `is_mobile` already (`:973` answers a JSON error otherwise). ⇒ **`cfg set mobile 1` is the
only promotion path today**, and it is the reboot-to-apply raw flip.

### 1.3 The four inconsistencies that follow

| | inconsistency | consequence |
|---|---|---|
| **I-a** | **team without mobile is reachable** — `team <id>` never sets `is_mobile`, and NV restores the two fields **independently** | the config that defeats the H-flood role-exclusion invariant (register **B28**) |
| **I-b** | **`cfg set mobile 0` leaves mobile state behind** and is **reboot-to-apply** | between command and reboot, NV holds `is_mobile=0, team_id=X`; a B28 boot-normalisation would then **re-set the flag and silently undo the operator** |
| **I-c** | **`is_gateway && is_mobile` is UNRULED** — the only combined read is incidental (`node_beacon.cpp:368`) | a gateway can be told it is roaming; nothing refuses it |
| **I-d** | **promotion is live for one path, reboot for the other** — `team` applies live, `cfg set mobile` at reboot | the same logical change has two latencies, and only one re-announces |

## 2. The proposal

### 2.1 ★ The organising principle — the role is about HOW REACHABILITY IS OBTAINED

Not about hardware, not about movement:

- **STATIC** — reachable at **its own DAD-assigned `node_id`** on a leaf, and **carries the static plane for others**
  (relays, may host mobiles, may be a gateway).
- **MOBILE** — reachable **through someone else**: a **HOME** (a static host) on the static plane, and/or by
  **`team_local_id`** on the team plane. **Does not carry the static plane for others.**

⇒ **A team member is a mobile by construction**, because `team_local_id` *is* a through-someone-else identity. That is
what makes the owner's B28 ruling a consequence of the model rather than an extra rule.

### 2.2 The rules

- **R1 — Exactly one role, always.** `is_mobile` is the sole discriminator. No third state, no "unset".
- **R2 — `team_id != 0` ⇒ `is_mobile`** (owner ruling, B28). Enforced at **three** points, because there are three
  ways in: the core switch `Node::set_team_id`, the **boot** normalisation (NV carries the two fields independently),
  and a **refusal** at the `mobile` setter (§2.3).
- **R3 — The implication is ONE-DIRECTIONAL.** `mobile` implies **neither** a team **nor** a home. Both of these are
  legitimate, must stay reachable, and must not be "tidied" away:
  - a **homed mobile with no team** (an ordinary roaming endpoint);
  - an **off-grid team member with no home** (the hiking group — it routes on the team plane alone).
  ⇒ **`team 0` (leave a team) must NOT clear `is_mobile`.**
- **R4 — `is_gateway` ⇒ STATIC, exclusively.** A gateway is two-layer infrastructure; it cannot be reachable
  through someone else. **Refuse `is_gateway && is_mobile`.** ⓘ Naturally true on the gateway *build*
  (`MR_FEAT_MOBILE 0`), so this rule is about the normal build, where both flags exist.
- **R5 — A role change is a TRANSITION: cleanup + live apply + re-announce.** Never a raw flag flip — the flag is on
  the wire (§1.1) and 216 sites branch on it.

### 2.3 The transition table

| from → to | verb | must clear | must announce |
|---|---|---|---|
| **static → mobile** | `cfg set mobile 1`, **or implied by `team <id>` / `team new`** (R2) | ★ see the hosting problem below | re-beacon with `is_mobile=1` |
| **mobile → static** | **`cfg set mobile 0`, refused while `team_id != 0`** (tell the operator `team 0` or `leave`) | `_my_mobile_reg` (the home registration), pending presence/registration state | re-beacon with `is_mobile=0` |
| **mobile → static (blunt)** | **`leave`** — already correct, no change | everything but freq | → unprovisioned + idle |
| **any → unprovisioned** | `leave` | everything but freq | — |

★★ **NEW PROBLEM this table exposes — static → mobile ORPHANS HOSTED MOBILES.** A static node that is *hosting*
mobiles stops carrying the static plane the moment it becomes one. Its guests lose their home and their reverse-ack
path with no notification. The count is already available (`Node::mobile_reg_count()`, used by the `ready` JSON).
⇒ **decision O2 below.**

⚠ **Do NOT cascade.** A demotion must not silently clear `team_id`, and a team switch must not silently clear a home:
only `join` / `create` / `leave` are sanctioned to wipe planes (`§clean-team`, 2026-07-27). Every other transition
**refuses** rather than destroying state the operator did not mention.

### 2.4 ★★ "creating or joining a team" — the two meanings of JOIN/CREATE in this codebase

**The owner's O3 wording is exactly right about intent and dangerously ambiguous about location, so it is pinned here:**
*"setting mobile if creating or joining a team"* means the **TEAM verbs** — **`team new`** (mint/create a team) and
**`team <id>`** (join an existing one) — **both of which route through `Node::set_team_id`**, which is therefore the
single enforcement point for R2's live arm.

⚠⚠ **It does NOT mean the `join` / `create` verbs.** Those are **NETWORK PROVISIONING** (`handle_join` / `handle_create`),
and §1.2 path 3 shows they **deliberately PRESERVE** `is_mobile`, `team_id`, `team_local_id` and the team channel key.
**Wiring R2 into them would be wrong twice over:** it would make provisioning a role change (contradicting R5 and the
`§clean-team` boundary), and it would fire on a node with `team_id == 0`, where the implication has no antecedent.
⇒ **Two unrelated operations share the English words "join" and "create" here. R2 belongs to the TEAM pair only.**

★ **So "consistent" resolves to exactly three edits, no more:** (a) `set_team_id` sets `is_mobile` when the new team is
non-zero — covering `team new` **and** `team <id>` in one place (U1); (b) the **boot** normalisation, because NV carries
the two fields independently; (c) the `cfg set mobile 0` **refusal** (O1). Plus O2's promotion refusal and R4's gateway
refusal, which are role-surface rules rather than team ones.

## 3. What this changes, concretely

1. **R2's three enforcement points** — B28's slice, unchanged in scope by this document.
2. **`cfg set mobile` becomes a transition** rather than a flip: live apply, `_my_mobile_reg` cleared on demotion,
   re-beacon in both directions, and the two refusals (R2's team check, R4's gateway check). ⚠ Making it **live** is
   the substantive part: today's reboot-to-apply accidentally re-announces via the reboot, so a live version **must**
   re-beacon explicitly or the mesh keeps the stale role.
3. **R4's refusal** — new, one condition, no state.
4. **Nothing changes in `leave`, `join`, `create`, or `team 0`.** ★ That is the test of the model: the paths that were
   already coherent stay untouched.

## 4. Decisions for the owner

★★ **ALL THREE RULED BY THE OWNER 2026-07-31 — every QA recommendation was accepted.** The table is kept as the
record of what was weighed.

| | decision | ruling |
|---|---|---|
| ~~**O1**~~ | ✅ **RULED: REFUSE.** `cfg set mobile 0` while in a team is refused, naming `team 0` / `leave` as the way out | ★ **REFUSE** — cascading destroys the team channel key, the team-DAD id and the team routes from a command that never mentions teams. One extra step (`team 0`) makes the destruction explicit (C2) |
| ~~**O2**~~ | ✅ **RULED: REFUSE.** Promotion is refused while `mobile_reg_count() > 0`; the guests keep their home | ★ **REFUSE** for v1 — it protects the guests' reachability, needs no new frame, and eviction-with-notice is its own slice. ⚠ Refusing means a busy host cannot be promoted without first shedding guests, which is the honest trade |
| ~~**O3**~~ | ✅ **RULED: KEEP the key — and MAKE IT CONSISTENT.** Owner: *"ok so we keep it — but we make it consistent (e.g. setting mobile if creating or joining a team)"* ⇒ **R2 fires on team adoption, both spellings** (§2.4). No `role` verb | ★ **Keep the key** for now. A new verb is idiomatic here (cf. the send-verb consolidation) but it is contract churn for the app and a separate slice; the key can be made a proper transition without renaming it |

## 5. Gate expectations (whenever this is built)

Standard gate — `docs/2026-07-26-slice-gate-method.md` + register §0 for the dispatch contract. Specifics:
- ★★ **The corpus already satisfies R2: all 12 team scenarios, 48 team-bearing nodes, are already `is_mobile` — ZERO
  counterexamples** (QA-measured 2026-07-31). ⇒ **R2's enforcement is predicted CORPUS-INERT**, and that prediction is
  the gate: **if any scenario moves, a scenario was relying on the outlawed config** and that is a finding, not a
  re-anchor.
- ★ **R4 and R5's refusals are corpus-DARK by construction** — `cfg set` and the role verbs live in `src/`, which
  **neither** native nor the sim compiles. ⇒ **the boards are the only validator, and native tests must cover the
  refusals via the parse/config layer** (`test/test_firmware_config_parse.cpp` is the precedent).
- ⚠ **`is_mobile` rides two frames.** Any change to *when* it is set must keep `s18` byte-identical (a static-only
  corpus never sets it) — and **the keystone `1cd21235` / 271629 is the tripwire.**
- ★ **A re-announcement is observable**: if the live transition re-beacons, expect **team/mobile scenarios to move** if
  any of them ever flips a role mid-run. QA measured that none do today.
