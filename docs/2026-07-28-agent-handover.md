<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# HANDOVER — 2026-07-28, mid team-routing arc

**Written at end-of-context by the QA-gate coordinator.** Supersedes `2026-07-25-agent-handover.md`
for everything after `916de64`; that document is still correct for the carrier/CTS arc that preceded it.

> **My role, as the owner set it:** *"start separate coding agents by preparing instructions, monitor,
> then check quality."* I do **not** write feature code. Coding agents implement; I gate.
> ⚠ **Standing exception granted 2026-07-27 for this arc only:** *I* may commit (coding agents may not).
> That exception was scoped to the overnight team-routing run — **re-confirm it before using it again.**

---

## 0. READ THESE FIRST, IN THIS ORDER

1. `simulation/BASELINE.md` — ★ the anchor authority. **Never hardcode an md5 here or anywhere** (rule D1).
   The keystone + every per-scenario anchor lives there and re-anchors legitimately. Notes for this arc
   run `26s…26w`, `27x…27zh`, then `T0`, `T1`/`T1b`, `T2`.
2. `docs/superpowers/specs/2026-07-27-team-plane-static-parity-routing-design.md` — the spec being built.
   **§10 and §11 are mine** (reviewer response + the ruling write-up). §10.1 overrules the author's Q1;
   §10.3 records a gap in their §4; **§3/T6 and §11 are new** — the owner's 2026-07-28 ruling. The body of
   the spec now carries inline ⚠/★ corrections wherever implementation contradicted the design.
3. `CLAUDE.md` working rules, and `docs/2026-07-26-slice-gate-method.md` for the gate recipe.

---

## 1. STATE AS OF THIS WRITING

| | |
|---|---|
| MeshRoute HEAD | `fe1c2fd` — *T4: team REQ_SYNC (on-demand full-table pull)*; tree **clean** |
| Simulator HEAD | `4e381b0` — *sim: add the `leave` verb*; tree **clean** |
| Anchors / native counts | in `BASELINE.md` (authority) |

**Commits landed this arc:** `916de64` (QA records + s36 + spec §10) · sim `4e381b0` · `b342df8`
(reprovision push + id-0) · `2d0366d` (T0) · `36b19f3` (T1 + T1b) · `0041ed2` (T2) · `a7282a0` (this
handover) · `fe1c2fd` (T4).

### ★ Sequencing rule — currently satisfied, but re-read it before the next slice

**Never measure a scenario anchor against a dirty tree.** An anchor taken against half-landed work is a
plausible-but-false number — the same failure class as the stale-artifact hazard in §5. This blocked the
s23/s35 scenario work for as long as T4 held the tree; both repos are clean now, so it is released.

---

## 2. THE OWNER'S THREE ITEMS (their last instruction)

### Item 1 — `_rreq_last_team` age-out → ✅ **ALREADY DONE**, no work owed

Landed in **T1b, commit `36b19f3`**. Verified at this writing:

- `lib/core/node_route_discovery.cpp` — `Node::age_out_rreq_last()` sweeps **both** tables:
  `_rreq_last` and, under `#if MR_FEAT_TEAM`, `_rreq_last_team`, both with
  `protocol::route_request_seen_ttl_ms`.
- Called from `lib/core/node.cpp:952`; declared `lib/core/node.h:679`.
- Carries the in-source ★ SCOPE comment (`node_route_discovery.cpp:106`) stating what is swept and why —
  per the *mark done-vs-missing in code* rule.

⚠ Note the TTL: `route_request_seen_ttl_ms`, **not** `send_defer_ttl_ms`. I got that wrong when briefing
T1b and the coder was right to push back.

### Item 2 — s35 corrected to use `-t` → ✅ **DONE** (re-authored as `s35a` + `s35b`)

> ## ★★ CORRECTION 2026-07-28 — THE CLAIM BELOW IS WRONG, AND ACTING ON IT WOULD HAVE MADE s23 WORSE
>
> I wrote that T1's sim-side patch "survived and is committed" at `NodeRuntimeWrapper.cpp:801`. **It did not.**
> That line sits inside the **`send_hash`** branch (`:788`–`:813`) and predates T1 by a week (sim `a54baf7`,
> 2026-07-21, tagged `§F-TR-1`). The **id-addressed `send` verb at `:896` had no `-t` handling at all** —
> `c.u.send.plane` was assigned at exactly one site in the whole file. I mistook a pre-existing parse for the
> lost one, and the `.patch` I found preserved in a scratchpad was a red herring.
> ⇒ **T1's sim patch was lost WITH s35.** Converting s23 to the suffix form on that tree would have aired
> `hop_test -t` as the body, still on plane AUTO. The coder caught it and wrote the missing 10 lines; QA
> verified the enclosing branch and the date independently.
>
> ✅ **Both halves now landed** — sim patch committed, and s35 re-authored as
> `s35a_cochannel_isolation_meshroute.json` (74 asserts, `f95bf6ce`/2336) +
> `s35b_..._control_meshroute.json` (5 asserts, `4f49e969`/1063), **both mandatory**. Corpus is now **34**.
> Discrimination proven by poison probe, not by the unexecutable "must fail before T1": P-T1 → 20 failures
> including the literal bench string. **s35b held 0 failures under every probe**, which is what makes it a
> control. ⚠ **s35 does NOT cover T2** — declared, not silent. Full record in the `BASELINE.md` SCEN note.

The original claim, kept as the record of the error — `NodeRuntimeWrapper.cpp:801` has the trailing-`-t` parse:

```cpp
// §team-parity T1: an optional TRAILING " -t" selects the TEAM plane, EXACTLY as send_hash above does
if (body.size() >= 3 && body.compare(body.size() - 3, 3, " -t") == 0) {
    c.u.send.plane = static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM);
    body.erase(body.size() - 3);
}
```

Without it, `Command.u.send.plane` could never be TEAM from an id-addressed scenario verb, so node.cpp's
`send -t` guard was **unreachable from any scenario**.

**What was lost:** `s35_cochannel_isolation_meshroute.json` (33 assertions), plus `storm_probe.json` and
`ledger_probe.json`. T1's coder wrote them into its scratchpad; the scratchpad died with the session.
`simulation/` today holds s31–s34 and s36 — **no s35**.

⚠ **Cause not fully established — a second candidate, self-disclosed.** The T4 agent reclaimed 42 GB from
the shared scratchpad (the disk had hit 100%, 170 MB free — nothing could run). Its preservation filter kept
`.txt/.md/.log/.diff/.patch` but **not `.json`**. It searched afterwards: no `s35*` exists anywhere on disk,
no root-level file was touched, no preserved report mentions it — consistent with session death. But that
deletion happened **before** this document was written, so T4 cannot exclude itself and flagged it rather
than letting it be discovered. Either way the remedy is the same: **re-author.** The lesson is unchanged and
now doubly earned — durable output goes in the repo.

**The evidence s35 must reproduce when re-authored** (recorded from T1's gate report):

> BEFORE: **860 events, 10 failures** — including `actual_reply="OK error ctr=0 depth=0"`, which is the
> literal bench string from spec §0.
> AFTER: **952 events, 0 failures**.

Spec **§5** is the authoring contract, and **§10.5** requires it to ship as a **pair**. Because it is a new
scenario it gets a fresh anchor and a BASELINE note — it does not disturb existing anchors.

### Item 3 — verify/correct the four live scenario lines → ✅ **DONE**, all four were wrong

> ✅ **Corrected to the suffix form. `s23` re-anchors `d019abfc` → `96f6ffe3` with the event count UNCHANGED at
> 795 — a VALUE-ONLY re-anchor.** ⚠ I predicted the count would move; it does not. The delta is the DATA hex
> losing `2d7420` (`-t `), airtime 150 → 139 ms, and every downstream timestamp shifting.
> ✅ **`s28` audit: no defect.** Corpus sweep found **11 other ` -t` uses, all `send_hash`, all correct since
> 2026-07-21 — s23's four lines were the only defect in the corpus.**

`simulation/s23_mobile_team_multihop_meshroute.json`:

| line | today | intent |
|---|---|---|
| 25 | `"send 254 -t hop_test"` | team-plane send |
| 31 | `"send 254 -t hop_test"` | team-plane send |
| 37 | `"send 254 -t hop_test"` | team-plane send |
| 43 | `"send 115 -t hop_back"` | team-plane send |

The parser takes `send <dst> <rest>` and `rest` is the **body**. `-t hop_test` does not end in `" -t"`, so
the plane stays **AUTO** and the literal string `-t hop_test` is transmitted as payload. It only *appears*
to work because AUTO dispatches on `is_team_peer`. `s28` is the only other scenario matching `-t ` — check
it the same way.

**Correct form:** `"send 254 hop_test -t"` / `"send 115 hop_back -t"`.

★ **This WILL re-anchor s23.** My earlier note that "the suffix form does not re-anchor" was about the sim
*parser* patch, not about fixing these lines: the body loses 3 bytes (`-t ` stops being payload) **and** the
plane goes AUTO→TEAM explicitly. Airtime changes ⇒ md5 and event count both move. That is a legitimate
re-anchor — gate it and write the BASELINE note; do not treat the diff as a regression.

---

## 3. WORK IN FLIGHT AND QUEUED

- **T4 — team REQ_SYNC (on-demand full-table pull)** — ✅ **GATED AND COMMITTED `fe1c2fd`.** Tree is clean;
  the sequencing rule above is released. Full record in the `BASELINE.md` T4 note, including the three
  premises of mine it disproved (`q_opcode` is 2 bits ⇒ `team_sync` had to take 0, the last free codepoint;
  `-Werror=switch` was structurally blind to it; the `_node_id == 0` guard also blocked off-grid members).
  ✅ **The docs T4 left owed are LANDED** (`a6c73f9`): `frames.md`'s Q rows + the `TEAM_SYNC` tail row + the
  "opcode field is now FULL" note, `protocol.md` §3's team-pull paragraph, and the pre-existing `frames.md`
  drift that called the byte-3 `mobile` bit "effectively inert".
- **s23 `-t` fix + `s35` re-author** — ⚠ **IN FLIGHT** with one agent, two separately-measured sub-slices. It
  was also asked to measure the homed-vs-off-grid `origin` question behind §11. Needs gating + a BASELINE note.
- **T3 — team DV census** (spec §3) — not started. ★ **It also owns the hop-cap asymmetry T0 left open**:
  team RREQ floods at `team_hop_cap` 8 while team DV accepts combined hops to `dv_hop_cap` 16.
- **T6 — team origin namespace + plane-keyed ledgers** — ✅ **GATED AND COMMITTED `9c7b40a`.** s28/s29 value-only
  re-anchors, `s37_team_homed_origin` new, corpus **35**, native **930/69355/0**, `sizeof(Node)` **unmoved at
  220592** (my escalation premise was false). Record in the `BASELINE.md` **T6** note.
- ★★★ **T7 — ✅ GATED AND COMMITTED `a3886ee`. THE §0 BENCH FAILURE IS CLOSED.** One line: `&& is_team_peer(origin)`
  removed. `s35a` **lost 69 events** because the receiver now learns the originator from the DM and needs no
  discovery for its reply; new `s38_team_origin_learn` `d0fbb4cc`/519; corpus **36**; native **934/69377/0**.
  ★ Its before-arm found an **I2 breach no learn-site audit could have found** — see the new spec **§12**.
- ⚠ **STILL OWED, excluded from T7 with an accepted C1 argument:** the E2E-ACK gate's `is_team_peer(dst)`
  (`node_mac.cpp:89`) is not plane-qualified — a **send-side** fix (`flight_is_team_plane`) against T7's
  receive-side learn, and its control case exists in no scenario, so it needs its own file.
- ★ **NEW CANDIDATE SLICE — a plane audit of the READ paths** (spec §12). §10.3/§9-Q4 audited *writes*; the s38
  breach entered through `rt_find(..., AUTO)` falling through to `_rt`. Every plane-typed lookup that can silently
  degrade to the static table needs the same treatment the write sites got.
- *(historical, T6's original brief)* Part A
  `stamp_origin` gains a plane (5 callers, incl. two channel-M paths); Part B plane-keys the four ledgers
  (**moves `sizeof(Node)` ⇒ all TEN board envs, the exception to the 3-env rule**); Part C owes a homed-member
  scenario, because s35a/s35b are off-grid only. ★ **Acceptance now includes R3 compliance** — zero static-plane
  `r_tx`/`rreq_forward` attributable to a team-plane send.
- **T5 — team bidi plane** (spec §3) — not started.
- ★ **Order is `T6 → T3 → T5`** — the owner pulled **T6 first** on 2026-07-28 (*"yes, do T6 first"*) once the
  measurement showed it fixes a live delivery defect in a configuration already run on metal, not latent
  hardening. Spec §8 + §3/T6 carry it. **T6 is IN FLIGHT.**

---

## 4. ✅ THE OWNER RULING — MADE 2026-07-28, and it created a new slice

> **RULED: a `Plane::TEAM` send stamps `team_local_id()` — "team id, bundled with the ledger fix."**
> The owner accepted QA's recommendation *with its condition*: it ships as ONE slice together with plane-keying
> `_seen_origins`, `_per_origin_channel`, `_hash_query_seen`, `_mediated_recent`. **That is now `T6`, ordered
> after T3 and before T5** (T5 keys link state by id and must not sit on an ambiguous id space).
> Full definition in spec **§3/T6**; the ruling itself in **§11**.
>
> ★ **The argument that makes T6 non-optional:** `node.h:1503-1523` documents each of those four ledgers as
> *"safe today"*, and every stated reason is an assertion that the team plane is quiet — *"the planes rarely
> co-relay the same origin id"*, *"no node today processes BOTH the static and the team H-flood plane"*. This
> arc's whole purpose (R1) is to end that. **And `s35` falsifies them by construction**, since §5 puts a static
> node physically between two teammates on one PHY. T6 is required by the test we are already committed to.
>
> ⚠ Being measured, and it changes urgency not design: whether a homed member's team `-a` ack actually fails
> was **reasoned, not observed**. The s35 agent was asked to measure it.
> ⇒ `node_mac.cpp:70`'s comment **becomes true** under this ruling instead of needing correction.

The question as originally put, kept as the record of what was ruled on:

**Should a `Plane::TEAM` origination stamp `origin = team_local_id()` instead of the home id?**

`lib/core/node.h:821`:

```cpp
const bool mob = _cfg.is_mobile && _my_mobile_reg.active;
item.origin = mob ? _my_mobile_reg.home_id : _node_id;
```

There is **no team-plane exception**, so a homed teammate stamps its **home's static id** on a team DM.

Why it needs the owner, not me: changing it moves **anti-spam accountability** to the team id and changes a
`_seen_origins` dedup key. That is a policy call with a plane-crossing blast radius.

⚠ **A FALSE INVARIANT WAS LEFT IN THE SOURCE BY T1** — `lib/core/node_mac.cpp:70` claims *"the origin it
stamps is the team_local_id, which every teammate CAN route."* Per the code above that is **not true today**.
Fix the comment whichever way the ruling goes (rule V1: fix drifted comments you touch).

### Other open items carried forward

- A purged re-offer slot strands `channel_sent`.
- An unprovisioned node answers its own-hash locate with 0.
- `enqueue_data`'s DST_HASH is plane-blind.
- Team beacons advertise `_rt_team` to static receivers.
- §10.3 **plane-blind ledgers**: `_seen_origins`, `_per_origin_channel`, `_hash_query_seen`,
  `_mediated_recent` — team and static ids alias; they suppress rather than leak, but the spec's §4
  invariant list omits them.

---

## 5. METHOD — what this arc cost me to learn

**The poison probe (standing method).** Make the code return a wrong-but-valid value, rebuild, run every
scenario, record which ones move. ★ **Every 0/N result MUST have a same-site control that DOES move** —
otherwise "0" means *unreachable*, not *inert*. This is what proved the `send -t` guard was dead code.

**"Executed ≠ observable."** Counting site executions does not establish observability. Measure *values*.

**★ The stale-artifact hazard — now FOUR consecutive slices.** A probe returns a confident, plausible,
**false** result because the binary it ran was not the binary it thought it built. Two variants seen:
*(a)* `cp -a` preserves mtimes, leaving stale objects — restore harness trees by **content write + `touch`**
and rebuild shared trees from scratch; *(b)* **a failed build leaves the previous binary in place**, so the
runner measures the old code. T4 hit (b) and self-caught it: its first mutant run reported 921/921 green for
code that **does not compile**. ⇒ **`rm` the binary before every build, and refuse to report a result
without a regenerated one.** I applied this to my own verification of T4.

**A brief's premises are hypotheses, not facts.** T4 disproved three of mine and was right each time; T1b
corrected my TTL; T2 corrected my saturation claim. Write briefs so a coder can *measure* the premise, and
say plainly that disproving one is a valid result.

**The sweep-scope meta-bug — 7+ instances this arc.** An audit is only as wide as its scope. Ways I got it
wrong: directory scope · flag scope (`-Wswitch` blind behind a `default:`) · path form (absolute vs the
relative paths pio actually logs) · build profile (`#if MR_N_LAYERS>=2`) · indirection through a helper
(`airtime_routing_ms` hid an `active_cr()` call from an audit I had already published as exhaustive).
**Before publishing any "every use of X" claim, name the scope and prove the scope.**

**Plane separation.** Team writes land only in `_rt_team` / `_team_peer` / `_team_liveness` — never `_rt`,
`_id_bind`, `_link_bidi`, `_dest_seen_ms`, `_peer_liveness`. The isolation pattern to copy is
`node_route_discovery.cpp:183`: the `return` for a `team_scoped` F sits **outside** `#if MR_FEAT_TEAM`, so a
static build stays inert without the guard swallowing the dispatch.

**★ Write durable output into the repo, not the scratchpad.** The scratchpad dies with the session. That is
exactly how s35 was lost. BASELINE notes and docs survive; agent scratch does not.

**Telemetry is SIM-ONLY by design** — stripped on device. Byte-identity cannot validate app/console/NV
behaviour, because the oracle sees events hardware never emits.

**Gate classes** — pick the right one and say which you used: byte-identity of the corpus (pure refactor) ·
delivery-parity + re-anchor (behaviour change) · assertion-count exactness (scenario work).

---

## 6. SUGGESTED NEXT ACTIONS

1. ~~Gate T4 → commit.~~ ✅ DONE (`fe1c2fd`). Tree clean — scenario work is unblocked.
1b. Land the `frames.md` / `protocol.md` updates T4 left owed (listed in the BASELINE T4 note), incl. the
    **pre-existing** `frames.md:269` drift calling the byte-3 `mobile` bit "effectively inert".
2. Fix the four s23 lines + audit s28 → gate → **re-anchor s23** with a BASELINE note.
3. Re-author `s35_cochannel_isolation_meshroute.json` per spec §5 + §10.5; it must reproduce
   860/10 → 952/0, including the `OK error ctr=0 depth=0` bench string.
4. ~~Put the stamp_origin ruling to the owner.~~ ✅ RULED 2026-07-28 → **T6** (spec §3/T6, §11): stamp
   `team_local_id()` on the TEAM plane, bundled with plane-keying the four ledgers. Ordered after T3,
   before T5. `node_mac.cpp:70`'s comment becomes true rather than being corrected.

5. **T3** (also owns the hop-cap asymmetry T0 left open) → **T6** → **T5**.
