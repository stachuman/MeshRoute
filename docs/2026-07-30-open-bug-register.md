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

> ★★★ **For the wider picture — open topics, the four spec arcs, pending owner decisions — read
> `docs/2026-07-31-agent-handover.md`.** This file is the bug index; that one is the map.

## ★★★ CURRENT PRIORITY ORDER — owner-ruled 2026-07-31. **Most of this file is PARKED.**

★ **TIER 1 IS EMPTY.** B0 was the last live leak and it closed 2026-07-31. **Nothing remaining in this register
blocks functionality** — it is all quality, telemetry, plane-parity and dedup. The owner has therefore pivoted to the
**peer address book**, and this file is now a backlog rather than a queue.

**The order (owner-chosen, after a QA triage):**
1. ~~**B4**~~ ✅ **CLOSED 2026-07-31** — and it yielded **B24/B25**; B25 is a candidate **I2 breach**, unmeasured
2. **B17** — ★ the only remaining **device-destructive** entry: `team 4294967296` **joins garbage team `0xFFFFFFFF`**
   on the 32-bit boards. One range check; **a native test can never catch it.**
3. **B26 / NV1** — ★ **owner-queued 2026-07-31 BEFORE AB1**: factor the NV backend's 6-times-duplicated blob validation
   **above** the `#if`, so it is natively testable — which is what makes AB1's "v1-blob rejection test" runnable at all.
   **Load side + primitives ONLY; `save`'s change-detection stays untouched** (see the entry — that is the trap).
4. **AB1 → AB2 → AB3** — `docs/superpowers/specs/2026-07-29-peer-address-book-design.md`. **Fully unblocked** — nothing
   in this register touches them. (⚠ **B18 is worth taking before AB3**, which rewires `hashof`/`nameof` onto the view:
   better than building the view over a known-wrong read path.)
5. **B22 → CL2 → AB4** — ★★ **AB4 (retained location) is GATED ON CL2, and CL2 IS NOT BUILT:** `channel_flavor_crypted`
   / `team_channel_crypt` / `team_channel_no_key` have **zero hits in the tree** (QA-verified 2026-07-31). T-K1/T-K1b/T-K3
   built the team **keypair**; **nothing seals a channel message with it.** The O5 ruling makes the **team content key the
   trust anchor** for a stored location — so building AB4 first would either ship a setter with no live source, or trust a
   **plaintext** post, which that ruling rejects. ⚠ **Take B22 immediately before CL2:** while B22 is open, four scenarios'
   team-channel asserts validate behaviour **metal does not have**, so CL2 would be gated against a lying corpus.

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
  number.** Use **cold, equal-length build dirs** — a warm one produced 18628-vs-10617 and read exactly like a
  real delta.
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move.** If it does, stop and report — do not re-anchor it.
- ★★ **RE-RUN THE FOUR DETECTOR PROBES AND REPORT THE NUMBERS. Hard item — a slice that omits them is NOT gated**
  (this rule exists because a slice omitted them and QA accepted the report):

  | probe | how | expect |
  |---|---|---|
  | **P-T7** | re-add `is_team_peer(origin) &&` at the team DATA-origin learn (`node_mac_rx.cpp`) | `s38` **474 ev, 8 of 16** |
  | **P-T1** | revert the `send -t` precondition at **`node.cpp:1309`** to `!is_team_peer(dst)` — ★ **KEEP the `plane == Plane::TEAM &&` conjunct**; the bare form gives **1587 ev / 24 FAIL**, not the expected numbers — ⚠ **NOT** `node_mac.cpp`'s ack-gate fix, which is a no-op on s35a and has cost a coder a run | `s35a` **1892 ev, 20 FAIL**, incl. `actual_reply="OK error ctr=0 depth=0"` |
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
| ~~**B3**~~ | ✅ **DONE — as-built: `s22` `d1855325`/1804 → `d02f1979`/1804, event count UNCHANGED, 3 (not 2) failures mid-slice.** Original row: ⚠ **`s22` is GREEN today and the fix TURNS IT RED** | correcting my own row: `s22` passes now (`d1855325`/1804) only because the sim's AUTO papers over it. Applying the plane fix makes it **fail until the scenario adds `-t`** — so the slice is *fix + scenario edit + re-anchor*, and a red s22 mid-slice is **expected**, not a regression |
| ~~**B4**~~ | ✅ **DONE — as-built: TWO movers (s35a + s38), 4 field values.** ★ My pre-dispatch floor of "s38, one line" **under-counted 4:1** because it grepped a **sentinel value** (`rt_total:0`); s35a's real defect was `2 → 3`. Original row: ⚠⚠ **NOT byte-identical.** Expect **`s38` to re-anchor by ONE field value on ONE line**, floor-measured | ★ **Only the *skip* is inert** (`sync_response_skip` fires in **0 of 36** scenarios). **The `rt_total` telemetry is LIVE in 27 scenarios — 51 events in `s18` alone** — and on a team pull it reports the STATIC count. **Exactly ONE corpus event carries the signature `rt_total:0`: `s38_team_origin_learn`, 1 of its 2.** ⚠ That is a **floor, not the set**: a *homed* member answering a team pull reports a **nonzero-but-wrong** count, invisible to that grep. ★★ **`s18` has zero `rt_total:0` and is static-only ⇒ the keystone MUST stay `1cd21235`/271629; if it moves, the fix is not plane-gated** |
| **B5** | **re-anchor likely** | changes a live frame's contents |
| **B6, B7** | **byte-identical or small** | both are currently-zeroed bypasses |
| **B8** | **measurement only** — no fix expected until it is answered |
| **B9** | ★ **value-only re-anchor of EVERY team scenario** | `slot` is in the stream |
| **B10** | **re-anchor of `s37`** | removing the dead command is a stream edit |
| **B11–B15** | **byte-identical** | telemetry/comments/`src/`-only |
| **B16** | ⚠ **NOT `s27` only — 12 scenario JSONs use `send_layer`** | QA-grepped 2026-07-31: s09 ×2, s10, s15 ×2, s16, s17, s27, s31, s32, s33, s37. The old row said “it is the sole user” and was wrong; probe E moved **11** of them |
| **D1** | ★ **inert on 34/36 — but it DISARMS `s35a`/`s38`.** Read the entry before starting |

## Tier 1 — silent or destructive

★★ **2026-07-31: TIER 1 IS NOW EMPTY — B0, the last live leak, is CLOSED.** Both Tier-1 bugs found *inside* this arc were fixed: the cross-layer cleartext downgrade
(`§xl-crypt`, `65833f2`) and the silently-dropped delegated sealed DM (`§deleg-ack-xl`, `442809b`).

### ~~B0~~ ✅ **CLOSED 2026-07-31** (`§loc-per-send`) — the leak is shut, and **one premise of mine was wrong**
★ **36/36 byte-identical, s18 keystone unmoved**, native 1006/70360 → **1012/70417**, `sizeof(Node)` **220656 → 220648 (−8)**
⇒ **six-env grid TAKEN after measuring** (first time it was justified rather than refused). `kVersion` **22 → 23**.
★★ **The native suite ASSERTED THE LEAK** — `test_node_r3.cpp:5225` had CHECKed the 6 location bytes round-tripping off an
**unsealed** wire since 2026-06-14, so spec §5’s before-arm was already in the tree, green. **A long-lived leak tends to have a
green test defending it — look for that assertion before writing a probe.**
⚠ **`send_layer -l` REFUSES**, a reduction of §2.3’s promise: the sealed-XL path never carried location (`e2e_seal_inner` returns
0 for `CROSS_LAYER`; `build_sealed_relay_body` hard-codes `lat=0,lon=0` and the SEALED_RELAY body has **no flags word**), so
carrying one needs a **body-format change** — its own slice (C1/C4). There is also a **third** XL builder, `delegate_send_layer`.
⚠ **The “+6 B does not fit” refusal has no branch** — measured **unreachable** (the seal refuses at body 211; a gate there could
only fire above 226). The guarantee holds structurally; the old **silent** drop is gone. ⚠ **`SendFailReason::no_location` was
APPENDED** (QA endorses; owner may reverse). ★ **Correction: `frame_codec.cpp:953` is the PARSE path** (`parse_unicast_inner`,
915–964) and must stay live — **the pack site is `:1018`**. Note: `LOC-PER-SEND`. *(original entry below)*

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

### B22 — ★★ plain `send_channel` **SUCCEEDS in the sim and is REFUSED on metal** · NEW 2026-07-31
The sim's "NATURAL" arm (`NodeRuntimeWrapper.cpp:813`) sets `team = (is_mobile && team_id != 0)`, so a plain
`send_channel` from a team member becomes a **TEAM flood**. The firmware sets `team=false, global=false` for a plain
post (`console_parse.cpp:205-213`, *"plain = GLOBAL"*), and `node.cpp:1323`'s `want_global = global || !team` routes it
to the **global/home** path — where an **off-grid** member has no home and it **fails loud**
(`send_failed{channel_no_home}` + `err_no_binding`). **Both sides QA-verified in source.**
★★ **This is worse than B3, which it was found by: not a different plane but SUCCESS-vs-REFUSAL.** **10 corpus commands
in 4 scenarios take that arm** (s22 ×2, s28 ×3, s29 ×2, s34 ×3) ⇒ **s22's and s34's team-channel assertions validate a
behaviour metal does not have.** ★★ **OWNER RULING 2026-07-31: "plain `send_channel` should refuse on an off-grid member, like metal."** ⇒ **METAL IS THE
REFERENCE; the SIM is what changes.** Delete the `team_member` heuristic at `NodeRuntimeWrapper.cpp:813` so a plain post
sets `team=false, global=false` exactly as `console_parse.cpp:205-213` does. ⚠ **Consequence to expect and NOT paper
over: the 10 commands stop working as team floods** — an off-grid member's plain post will emit
`send_failed{channel_no_home}`, so **s22/s34's team-channel assertions go RED until those commands gain `-t`**, exactly
as s22's `reqpubkey` needed it in B3. **The scenario edits are the slice's real work** — and the same anti-masking bar
applies: assertion counts and bodies must be unchanged, only the *commands* gain `-t`. ⚠ **If any assert cannot be made
green by adding `-t` alone, STOP and report** — that means a mechanism is wrong, not a scenario. Note:
`SIM-PLANE-PARITY B3`.

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

### ~~B1~~ ✅ **CLOSED 2026-07-31** (`§team-target-whole`) — the whole token must now parse
⚠ **Wider than the one line I briefed:** it also refuses `team 12abc`, whose leniency the 07-30 comment called
deliberate; and the old refusal string (*"must BEGIN WITH A DIGIT"*) was **false** for `88A672BA` and was rewritten.
★ **The sim was already right** — it required whole-token consumption all along, so this **shrank** a divergence.
⚠ **NEW, open: an out-of-range target diverges BY TARGET WIDTH** — `team 4294967296` leaves on 64-bit native but
**joins garbage `0xFFFFFFFF`** on the 32-bit boards. ⇒ **B17.** *(original entry below)*

### B1 — `team 88A672BA` (hex without `0x`) silently joins **team 88** · owner-flagged
`strtoul` base 0 reads `88`, stops at `A`, value ≠ 0 so the new zero-guard does not apply ⇒ **you join the wrong
team with no error.** Same family as the `team <garbage>` bug just fixed; the fix deliberately scoped it out (C1).
**Fix: drop the `if (v == 0)` gate in `mrfw::parse_team_target`** (`src/firmware_config_parse.h`) so *any*
partially-consumed target refuses. QA verified this does **not** break the legitimate PHY tail — the token is
measured to the first space, so `team 0x88A672BA freq=868 sf=7` still parses. **One line.** Note: `BUGS-A/B`.

### ~~B2~~ ✅ **CLOSED 2026-07-31** (`§id-bind-plane`) — and my site list was wrong
★ **TWO sites, not four:** `:677`/`:878` were **already gated**; only `on_hash_bind_response` and
`on_hash_bind_snoop` were open. All **eleven** `id_bind_set` sites were audited. ★★ **And `team_key_set` was the
WRONG destination** — it has no confidence dimension, feeds team-DAD mediation, and is a beacon-fed LRU; the fix is
to **not bind at all** on the team plane. **Payoff: s34 `no_route` 8 → 0.** ⚠ Two follow-ups: **B18** (the read-side
twin) and the app-visible `send_hash_giveup` no-push. *(original entry below)*

### B2 — the hash-bind ingest is **plane-blind**: a team-scoped H answer writes the STATIC `_id_bind`
Observed as `id_bind_set{key:0x…,node:34,source:"h_relay"|"h_query"}`. **s24 asserts a static *bystander* never
does this — but team members do it to each other.** An **I2 breach**. ★ Connects to the address-book spec §2.5:
that spec **forbids** "fix `hashof` by writing the team hash into `_id_bind`" precisely because it is this bug.
Note: `PLANE-A/B`.

### ~~B3~~ ✅ **CLOSED 2026-07-31** (`§sim-plane-parity B3`) — and it yielded a **louder** divergence than itself
★ **One re-anchor: s22 `d1855325`/1804 → `d02f1979`/1804 — event count IDENTICAL, the whole delta is ONE `cmd_reply`
echo line.** 35 others byte-identical, s18 keystone unmoved, native **1012/70417/0 unchanged** (no MeshRoute code
touched — net **+3 lines** in the sim, reusing `dm_plane_from_tail`). QA re-derived the scenario edit: **35 assertions
before, 35 after, bodies and `_c` comments byte-identical** — nothing masked.
⚠ **MY ROW BELOW SAID 2 FAILURES; IT IS 3.** The third is assert **#35** ("STATIC-PLANE INVARIANCE, pinned as an exact
count"): S2's `link_bidi_confirm` goes **6 → 7** because a GLOBAL WANT_PUBKEY floods the **static** plane. The "2" was
measured 2026-07-28, **before the T5 asserts (18–35) landed on 07-29** — a point-in-time number that decayed (V2).
★ **The scenario edit ALONE is a NO-OP** (pre-fix `lus` + edited s22 = baseline + the echo line): the `-t` was being
**silently swallowed**, because `scan_hex32` breaks on the space. ⇒ my briefed hazard ("wrong hash or usage error") was
wrong in the **safe** direction — **a silent no-op is why this survived four slices of plane work.**
★★ **Yielded B22 (plain `send_channel`: success-in-sim vs REFUSAL-on-metal) and B23 (`Plane::AUTO` IS reachable on
metal + a dead `u.resolve.hard`).** Note: `SIM-PLANE-PARITY B3`. *(original entry below)*

### B3 — `reqpubkey`'s plane diverges sim-vs-metal, and it makes **s22 wrong**
The sim leaves `u.resolve.plane` at **AUTO**; firmware `console_parse.cpp:181` assigns `team ? 1 : 2`. **Measured:
1/36 movers — `s22` goes RED with 2 failures** (TeamA never caches TeamC's pubkey ⇒ the sealed DM never decrypts;
s22's `reqpubkey` needs `-t`). The **fourth** divergence of the `Plane::AUTO` class, three of which are already
closed. Expect a small re-anchor. Note: `PLANE-A/B`.

### ~~B4~~ ✅ **CLOSED 2026-07-31** (`§sync-response-plane`) — **three** defective readers, not two
★ **s35a `799af428`/2388 → `585b9cc8`/2388 · s38 `0936ebcd`/526 → `a16ec83c`/526** — event counts identical, the whole
delta is **4 `rt_total` field values on 4 lines** (QA `diff`-verified). Native **1013/70440/0**; `sizeof(Node)` **220648
unchanged on all six flag-sets**; boards 3/3 **ΔRAM 0**; s18 keystone unmoved; four probes exact.
★★ **A THIRD reader this entry never named:** `sync_response_fire`'s `sync_response_tx{rt_total}` (`node_query.cpp:387`)
— **288 events in 26 scenarios**, the same quantity on the same pull ~11 s later. Fixing only two would have left the
mechanism **half** plane-aware — **the B9 failure mode.** Scope extension **accepted**; `SyncPending.team_plane` costs
**0 bytes** (sixth application of the padding-placement rule).
★★ **METHOD LESSON, and it is the durable one: my `rt_total:0` signature grep under-counted this class 4:1.** A grep
keyed on a **sentinel value** finds only the subset where the wrong answer is degenerate; s35a's real defect was
`rt_total` **2 → 3**, invisible to it. ⇒ **for a "reads the wrong variable" bug the honest detector is a DISCRIMINATOR at
the site, not a value grep on the stream.** (My predicted node class was also wrong — no **homed** member answers a team
pull anywhere in the corpus; s35a's mover is an **off-grid member that hears static beacons**.)
★ **The derivation trap is now measured:** **12 events in 5 scenarios** are static-plane sync responses from nodes that
**hold** team routes (9 with a differing count) ⇒ a plane derived from `team_active()` would have mis-answered every one.
⚠ **A `-Wunused-variable` was introduced and fixed** — the hoisted read is unused under `MESHROUTE_NO_TELEMETRY`
(device builds strip `MR_EMIT`) ⇒ **invisible to native AND the corpus; only the warning census caught it.**
Note: `§sync-response-plane`. *(original entry below)*

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

### B26 (slice tag **NV1**) — the NV backend duplicates its blob validation **6 times** · ★ **OWNER-QUEUED 2026-07-31, BEFORE AB1**
**Owner: *"load_peers/save_peers should be then moved to one function — unless there is a good reason not to?"*** — QA read
the code and the answer is **yes to factoring, no to one function**, for a reason worth recording:

★ **What must NOT merge.** The two live arms use different storage MODELS, not different syntax: nRF52 =
Adafruit LittleFS **files** (`open`/`read`/`write`/`close`, path `/mrpeers`); ESP32 = **Preferences/NVS** key-value
(`begin`/`getBytes`/`putBytes`/`end`, key `"peers"`). One `load_peers` would just move the `#if` **inside** the function —
same duplication, worse locality.

**What IS duplicated, counted:** **6 hand-written copies of the same validation line** — `:164, :189, :208, :298, :318,
`:333` (3 blob types × 2 arms) — and **16 near-identical wrappers** (`load`/`save` × `{Blob, IdBlob, PeerBlob, FaultLog}`
× 2 arms) plus 8 stubs, differing **only** in the slot name and the read/write call.
⇒ **Two layers: a per-arm `read_slot`/`write_slot` primitive (2 functions instead of 8), and a shared validator ABOVE
the `#if`.**

★★ **THE PAYOFF THAT DECIDED THE ORDERING: it makes the validation NATIVELY TESTABLE.** The validator sits inside
`#if defined(ARDUINO)` today, which is why **nothing in `test/` includes `device_nv.h`** — yet **AB1's gate demands a
"v1-blob rejection test."** Factor first and that test simply runs; leave it and AB1 must edit the check in **two** places
and cannot test its own version bump. ⇒ **NV1 before AB1.**
★ It also surfaces an asymmetry the duplication hides: **`:164`/`:298` accept a version RANGE** (`>= 2 && <= kVersion`)
while id/peers demand **equality** — and **AB1 must decide which policy `PeerBlob` v2 takes** (reject v1, or range +
migrate). That decision is currently buried in six hand-written lines.

⚠⚠ **SCOPE RULING — the "good reason not to", and it is a real trap: the `save` paths are NOT uniform.**
`save(Blob&)` (`:166`) does **H3 change-detection** — it loads the current blob and **skips the write when
byte-identical**, because a companion slider bound to `cfg set` would otherwise hammer flash **and widen the
reset-during-write corruption window** (this codebase has already been **bricked** by NV corruption once — see
`2026-06-24-internalfs-self-heal`). It also uses a **`static`** buffer deliberately, per the `do_post_ack`
stack-overflow lesson. `save_id`/`save_peers` are **raw writes with no such guard.**
⇒ **Forcing all four saves through one path either LOSES the change-detection (flash wear + a wider corruption window)
or silently ADDS it to three paths that never had it. Both are behaviour changes hiding inside a "refactor."**
⇒ **FACTOR THE LOAD SIDE + THE PRIMITIVES ONLY. Leave `save`'s coalescing exactly as it is, and mark the asymmetry
in-source so it reads as deliberate rather than as missed dedup.**

**Gate:** `src/`-only ⇒ **corpus byte-identical BY CONSTRUCTION** (verify, do not assert) and native unchanged except the
new tests ⇒ ★ **the boards are the only validator.** ⚠ `device_nv.h` is header-inline in the one device TU, and
`factory_erase`/`mount_or_repair` touch the same slots — **confirm the InternalFS self-heal path still behaves, not just
that it compiles.** ★ **C1: this is a REFACTOR — it carries no feature and no version bump.** `kPeersVersion` stays 1;
AB1 owns the bump.

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

### B24 — `send_req_sync_q`'s `q_tx{rt_total}` is now **inconsistent with** the plane-aware responder · NEW 2026-07-31
`node_query.cpp:106` reports the **static** count on both planes **deliberately** (documented at `:103-105`, to avoid
rewriting every static `q_tx` line). After B4 the **responder** side names its plane while the **requester** side does
not — the two halves of one exchange now disagree. ★ **The deferral itself still holds:** the route-rich skip at `:89`
was **V1-verified unreachable on the team plane** (its one team caller `node_mac.cpp:993` always passes `force=true`,
and `:75` forbids a mobile originating a static pull). ⇒ **telemetry-only, and it will re-anchor every scenario carrying
a `q_tx`** — which is why it is deferred, not forgotten. Note: `§sync-response-plane`.

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

### B17 — an out-of-range `team <id>` diverges **by target width** · NEW 2026-07-31
A **fully consumed but out-of-range** token still lands: `team 4294967296` truncates to **0 = LEAVE** on 64-bit
native, but on the **32-bit boards** `strtoul` saturates (ERANGE) so the same command **joins garbage team
`0xFFFFFFFF`**. A **range** clause, not syntax ⇒ left out of B1 by C1, with a before-arm in place.
★★ **CORRECTED 2026-07-31 — I overstated this.** What a native test cannot **reproduce** is the 32-bit *saturation path*
(native `unsigned long` is 64-bit, so it truncates instead). But **the GUARD is fully natively testable, and
`test/test_firmware_config_parse.cpp` already exists** for it: `"4294967296"` exercises the `> UINT32_MAX` arm on native,
and ★ **a token that overflows even 64 bits — `"99999999999999999999999"` — raises `ERANGE` on native too**, so both arms
get real coverage. ⇒ **the fix needs BOTH arms, because the two ABIs fail differently**: `ERANGE` for the 32-bit
saturation, `> 0xFFFFFFFF` for the 64-bit truncation. Note: `REG-B1/B2`.

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
| ~~**O1**~~ | ✅ **RESOLVED — B1 CLOSED 2026-07-31** (`§team-target-whole`); the gate was dropped and it went wider than the one line (`team 12abc` too). ⚠ Its **range** sibling is still open as **B17** |
| **O2** | `noinline` on `deleg_ack_put`? ⚠ **Superseded in detail by B19** — measured **8** call sites and **≈4 KB**, not 5 and 1.8 KB, and it must **fold into B12**. ★ **PARKED: `gateway` flash is 54.9%, so there is no pressure behind it** | one line, but re-codegens 8 sites |
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
