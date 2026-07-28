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
   **§10 is mine** (reviewer response). §10.1 overrules the author's Q1; §10.3 records a gap in their §4.
3. `CLAUDE.md` working rules, and `docs/2026-07-26-slice-gate-method.md` for the gate recipe.

---

## 1. STATE AS OF THIS WRITING

| | |
|---|---|
| MeshRoute HEAD | `0041ed2` — *T2: team-plane neighbour-learning parity* |
| Simulator HEAD | `4e381b0` — *sim: add the `leave` verb*; tree **clean** |
| MeshRoute tree | ⚠ **DIRTY — this is T4's in-flight work, do not commit it as yours** |
| Anchors / native counts | in `BASELINE.md` (authority) |

**Commits landed this arc:** `916de64` (QA records + s36 + spec §10) · sim `4e381b0` · `b342df8`
(reprovision push + id-0) · `2d0366d` (T0) · `36b19f3` (T1 + T1b) · `0041ed2` (T2).

Dirty files, all owned by the running T4 agent:
`lib/core/frame_codec.{cpp,h}` · `lib/core/node_mac.cpp` · `lib/core/node_query.cpp` ·
`test/test_dual_layer.cpp` · `test/test_frame_codec.cpp` · `test/test_node_r3.cpp`.

### ★ Sequencing rule this state forces

**Do not measure any scenario anchor while that tree is dirty.** An anchor taken against T4's half-landed
REQ_SYNC is a plausible-but-false number — the same failure class as the `cp -a` mtime hazard below.
Order: **finish and gate T4 → commit → only then touch scenarios.**

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

### Item 2 — s35 corrected to use `-t` → ⚠ **THE SCENARIO FILE IS LOST; RE-AUTHOR IT**

The sim-side half **survived and is committed** — `NodeRuntimeWrapper.cpp:801` has the trailing-`-t` parse:

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

**The evidence s35 must reproduce when re-authored** (recorded from T1's gate report):

> BEFORE: **860 events, 10 failures** — including `actual_reply="OK error ctr=0 depth=0"`, which is the
> literal bench string from spec §0.
> AFTER: **952 events, 0 failures**.

Spec **§5** is the authoring contract, and **§10.5** requires it to ship as a **pair**. Because it is a new
scenario it gets a fresh anchor and a BASELINE note — it does not disturb existing anchors.

### Item 3 — verify/correct the four live scenario lines → ⚠ **OPEN, all four confirmed wrong**

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

- **T4 — team REQ_SYNC (on-demand full-table pull)** — ⚠ **RUNNING** as I write (agent
  `a4be33806982d2d27`, last progress: *"probing leaf-exemption and loop-guard in handle_q"*). It owns the
  dirty tree. **Gate it, then commit it.** If the agent is gone, its preserved evidence is under the
  session scratchpad `T4/preserved/` — which **will not survive**, so re-run the gate rather than trusting
  a stale log.
- **T3 — team DV census** (spec §3) — not started.
- **T5 — team bidi plane** (spec §3) — not started.
- Spec **§8** gives the intended build order; T4 was taken before T3 deliberately.

---

## 4. ★ THE OPEN OWNER RULING (blocks a clean T5)

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

**★ The `cp -a` mtime hazard.** Preserving mtimes leaves stale build objects, so a probe returns a
confident, plausible, **false** result. It bit three consecutive slices' harnesses. Restore harness trees by
**content write + `touch`**, and rebuild shared trees from scratch.

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

1. Gate T4 → commit. (Nothing else can be measured until the tree is clean.)
2. Fix the four s23 lines + audit s28 → gate → **re-anchor s23** with a BASELINE note.
3. Re-author `s35_cochannel_isolation_meshroute.json` per spec §5 + §10.5; it must reproduce
   860/10 → 952/0, including the `OK error ctr=0 depth=0` bench string.
4. Put the §4 `stamp_origin` ruling to the owner **before** T5; fix the false comment at
   `node_mac.cpp:70` either way.
5. Then T3, then T5.
