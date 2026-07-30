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

> ⚠ **The companion contract has PENDING updates too.** `ios-companion/INBOX_SYNC_CONTRACT.md` now opens with a
> **PENDING CONTRACT CHANGES** box listing everything spec'd-but-unbuilt, so the app team does not implement against
> a surface about to move. ★ **One item needs app action ahead of the slice: `loc_dm` is being REMOVED** (field,
> cfg key **and** binary TLV) — if the app reads it, it must stop. **QA writes that file; a coder never edits it —
> report what is owed instead.**

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
  number.** Use **cold, equal-length build dirs** — a warm one produced 18628-vs-10617 and read exactly like a
  real delta.
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move.** If it does, stop and report — do not re-anchor it.
- ★★ **RE-RUN THE FOUR DETECTOR PROBES AND REPORT THE NUMBERS. Hard item — a slice that omits them is NOT gated**
  (this rule exists because a slice omitted them and QA accepted the report):

  | probe | how | expect |
  |---|---|---|
  | **P-T7** | re-add `is_team_peer(origin) &&` at the team DATA-origin learn (`node_mac_rx.cpp`) | `s38` **474 ev, 8 of 16** |
  | **P-T1** | revert the `send -t` precondition at **`node.cpp:1309`** to `!is_team_peer(dst)` — ⚠ **NOT** `node_mac.cpp`'s ack-gate fix, which is a no-op on s35a and has cost a coder a run | `s35a` **1892 ev, 20 FAIL**, incl. `actual_reply="OK error ctr=0 depth=0"` |
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
- **Report as:** INVENTORY CONFIRMED / DESIGN / COVERAGE / GATE / DEVIATIONS / MINE-VS-THEIRS. Report failures
  with their output; if you skipped a step, say so.

### 0.1 Expected corpus outcome per entry — so a moved stream is interpretable

★ **If a scenario moves when this table says byte-identical, that is a FINDING, not a re-anchor:** it means a
scenario was relying on the broken behaviour. Attribute it and report before proceeding.

| entry | expect | why |
|---|---|---|
| **B0** | **byte-identical expected** | `loc_in_dm` is **off** by default in the whole corpus — ⚠ if any scenario moves, a scenario has `loc_dm` on and is airing position in clear, which is a finding |
| **B1** | **byte-identical** | `handle_team` is in `src/`, outside both the sim and native builds |
| **B2** | **re-anchor likely** | the ingest fires in team scenarios; attribute per scenario |
| **B3** | **re-anchor, and `s22` should go GREEN** | it is currently **RED** — that is the point of the fix |
| **B4** | **byte-identical** | inert while `sync_response_min_routes` defaults 0 |
| **B5** | **re-anchor likely** | changes a live frame's contents |
| **B6, B7** | **byte-identical or small** | both are currently-zeroed bypasses |
| **B8** | **measurement only** — no fix expected until it is answered |
| **B9** | ★ **value-only re-anchor of EVERY team scenario** | `slot` is in the stream |
| **B10** | **re-anchor of `s37`** | removing the dead command is a stream edit |
| **B11–B15** | **byte-identical** | telemetry/comments/`src/`-only |
| **B16** | **`s27` only, if anything** | it is the sole user |
| **D1** | ★ **inert on 34/36 — but it DISARMS `s35a`/`s38`.** Read the entry before starting |

## Tier 1 — silent or destructive

Both Tier-1 bugs found *inside* this arc are **FIXED**, but ★ **one pre-existing Tier-1 leak is open — B0**: the cross-layer cleartext downgrade
(`§xl-crypt`, `65833f2`) and the silently-dropped delegated sealed DM (`§deleg-ack-xl`, `442809b`).

### B0 — ★★ `loc_in_dm` airs your COORDINATES IN THE CLEAR · **LIVE** · owner-ruled fix
`node_mac.cpp:149-152` gates `DATA_FLAG_LOCATION` on `app_dm && loc_in_dm && has-a-fix && it-fits` — **there is no
crypt check**, and the seal decision happens *after*. So on a node with `loc_in_dm = 1`, a **plaintext** DM (`e2e_dm`
off, no `-e`, or simply no peer key) flies with a 6-byte position **in clear** (`frame_codec.cpp:953`, the unsealed
pack path). ⚠ **`node_carriers.h:233` claims the opposite** — *"DATA_FLAG_LOCATION, sealed inner"* — which is true
only of a CRYPTED DM, and is why the leak reads as intended behaviour (V1 drift).
**Owner ruling 2026-07-30 (TWICE): location becomes a PER-SEND `-l` flag, `cfg set loc_dm` is REMOVED entirely, and a `-l` send REFUSES if it will not be sealed.** — do not silently omit the location (the app would believe it shared a
position it did not) and do not send in clear. **Spec: `2026-07-30-channel-crypt-and-location-privacy-design.md`
§2.3, slice CL3** — independent of the other two slices, and the only one closing a live leak. ★ **The second ruling dissolved the blast-radius worry (old O1, struck):** with a per-send flag an ordinary DM is
untouched and a refusal is attributable to the one send that asked for a position. ⚠ **But CL3 is bigger than it
looks** — removing `loc_dm` touches **eleven surfaces including an app-facing binary TLV field**, and needs
**`kVersion` 22 → 23**. ★ The retired TLV number **must never be reused** — the Q-opcode lesson.

⚠ **One near-miss also remains in this class — see B1.**

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
