<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# OPEN BUG REGISTER

*Opened 2026-07-30 at the owner's request, so findings stop living only inside `BASELINE.md` notes and agent
reports. **This file is the index; `simulation/BASELINE.md` carries the evidence.** Each entry names the note
that has the measurement.*

⚠ **Every `file:line` here drifts** — this tree moves several times a day and eight slices landed on 2026-07-28/30
alone. **Re-locate by symbol before acting (V1/V2).** A line number in this file is a hint, never a fact.

★ **Nothing here is speculative.** Every entry was either measured, or found in-source with a marker left by the
slice that declined to fix it (C1). Where an entry is *unmeasured*, it says so explicitly.

---

## Tier 1 — silent or destructive

Nothing outstanding. Both Tier-1 bugs found in this arc are **FIXED**: the cross-layer cleartext downgrade
(`§xl-crypt`, `65833f2`) and the silently-dropped delegated sealed DM (`§deleg-ack-xl`, `442809b`).

⚠ **One near-miss remains in this class — see B1.**

---

## Tier 2 — wrong behaviour, currently masked or worked around

### B1 — `team 88A672BA` (hex without `0x`) silently joins **team 88** · owner-flagged
`strtoul` base 0 reads `88`, stops at `A`, value ≠ 0 so the new zero-guard does not apply ⇒ **you join the wrong
team with no error.** Same family as the `team <garbage>` bug just fixed; the fix deliberately scoped it out (C1).
**Fix: drop the `if (v == 0)` gate in `mrfw::parse_team_target`** (`src/firmware_config_parse.h`) so *any*
partially-consumed target refuses. QA verified this does **not** break the legitimate PHY tail — the token is
measured to the first space, so `team 0x88A672BA freq=868 sf=7` still parses. **One line.** Note: `BUGS-A/B`.

### B2 — the hash-bind ingest is **plane-blind**: a team-scoped H answer writes the STATIC `_id_bind`
Observed as `id_bind_set{key:0x…,node:34,source:"h_relay"|"h_query"}`. **s24 asserts a static *bystander* never
does this — but team members do it to each other.** An **I2 breach**. ★ Connects to the address-book spec §2.5:
that spec **forbids** "fix `hashof` by writing the team hash into `_id_bind`" precisely because it is this bug.
Note: `PLANE-A/B`.

### B3 — `reqpubkey`'s plane diverges sim-vs-metal, and it makes **s22 wrong**
The sim leaves `u.resolve.plane` at **AUTO**; firmware `console_parse.cpp:181` assigns `team ? 1 : 2`. **Measured:
1/36 movers — `s22` goes RED with 2 failures** (TeamA never caches TeamC's pubkey ⇒ the sealed DM never decrypts;
s22's `reqpubkey` needs `-t`). The **fourth** divergence of the `Plane::AUTO` class, three of which are already
closed. Expect a small re-anchor. Note: `PLANE-A/B`.

### B4 — `schedule_sync_response` reads the **static** `_rt_count` on both planes
Its route-starved skip *and* its `rt_total`. **Inert only because `sync_response_min_routes` defaults to 0** —
raise that knob and a team member answers a team pull using its static route count. Marked ✖ MISSING in-source
with the trigger condition. Note: `T4`.

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

## Tier 3 — telemetry, docs, cleanup

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
| **O1** | **B1** — drop the `if (v == 0)` gate so `team 88A672BA` refuses? | one line; QA verified it does not break the PHY tail |
| **O2** | `noinline` on `deleg_ack_put` to recover **~1.8 KB flash**? | one line, but it re-codegens the 5 pre-existing call sites |
| **O3** | **T-K2 must rule before it seals:** `set_team_id` deliberately does **not** clear the team channel key, so a `team <other>` switch leaves the **previous team's key** in place. Inert today; the moment T-K2 seals, **a switched member seals for its new team under the old key.** Marked in-source and pinned by a test | a decision, then a small change |
| **O4** | The **BLE console exposure** is no longer a watch-item — under the `team exportkey` ruling it is **the only control protecting the team content key.** Closing it (pairing / auth gate / console allow-list) makes "any transport" safe | its own slice |

---

## How to use this file

1. **Pick a tier, not a line.** Tier 2 before Tier 3; anything that fails *silently* jumps the queue.
2. **Read the named `BASELINE.md` note first** — it has the measurement, the probe matrix and the reason the
   fixing slice declined.
3. **Re-locate every symbol.** See the warning at the top.
4. **Close the entry here in the same commit as the fix**, or this file becomes the next thing that rots — which
   is precisely what it exists to prevent.
