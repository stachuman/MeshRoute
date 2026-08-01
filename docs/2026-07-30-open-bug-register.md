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
  | **P-T1** | revert the `send -t` precondition **in `Node::on_command`'s `CmdKind::send` arm — FIND IT BY CONTENT, the line number drifts** (`node.cpp` ~1309 → 1339 → **1359** as of `§o3-key-lifetime`; grep the `plane == Plane::TEAM &&` conjunct) to `!is_team_peer(dst)` — ★ **KEEP the `plane == Plane::TEAM &&` conjunct**; the bare form gives **1587 ev / 24 FAIL**, not the expected numbers — ⚠ **NOT** `node_mac.cpp`'s ack-gate fix, which is a no-op on s35a and has cost a coder a run | `s35a` **1892 ev, 20 FAIL**, incl. `actual_reply="OK error ctr=0 depth=0"` |
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

★★★★ **2026-07-31: a shipped DEVICE HANG (B29) was found by `§ab3` and CLOSED the same day by `§idbind-loop`. ⇒ TIER 1 IS EMPTY AGAIN.**

### ~~B29~~ ✅ **CLOSED 2026-07-31** (`§idbind-loop`) — one line, **and the flash delta proved the UB was real**
★ Fixed to `for (uint16_t i = 0; i < _active->_id_bind_n; ++i)` — the verbatim idiom of the correct sibling `key_hash_of_id`.
Native 1057/71046 → **1058/71061/0**, 36/36 byte-identical, keystone unmoved, **`lus` md5 UNCHANGED after 30 TUs recompiled**
(object-code proof the function is never emitted in the sim), boards 3/3 **ΔRAM 0**.
★★★ **ΔFlash came out +16/+32/+16 — the OPPOSITE of the predicted "≈0 or negative", and that IS the UB made visible:** the
loop was `const` and side-effect-free, so GCC was **entitled to delete the termination test**, and the broken version
therefore compiled **smaller**. ⇒ **"the fix should not add flash" is not a safe premise when the bug was undefined
behaviour.** Proven real, not noise, by an object-level control: the TU **without** a date stamp grew, the TU **with**
`__DATE__` did not move.
★★ **A 3-arm probe proved neither half of the one line is redundant:** `uint8_t + cap` **hangs**; `uint16_t + cap` passes
the miss test but **still returns an evicted `0xBBBB2222`**; only `uint16_t + _id_bind_n` passes both. **Two defects, one
line, two tests.**
★ **The forbidden assertion is now IN** (two places) and the suite does not hang; all five stale `✖✖`/`⚠⚠` markers were
rewritten. The stale-tail test drives the **real rehome self-heal** path via public API only.
★★ **Durable lesson: A DEAD ERROR BRANCH IS EVIDENCE ABOUT ITS CALLEE** — `firmware_remote.cpp:196`'s miss-handler was
unreachable because the callee could not miss. **When a guard cannot fire, ask whether the callee can produce the
condition** before concluding the guard is redundant. It is now live and linked in both ELFs. Note: `§idbind-loop`.
*(original entry below)*

### B29 — ★★★★ `key_hash_for_id` **NEVER RETURNS ON A MISS** — an infinite loop on the device · **NEW 2026-07-31**
`lib/core/node.h:672`:
```cpp
for (uint8_t i = 0; i < protocol::cap_id_bind; ++i)   // cap_id_bind = 256
```
★★ **`uint8_t i` spans 0…255, so `i < 256` is ALWAYS TRUE** — `++i` wraps 255→0 and the trailing `return 0` at `:674` is
**unreachable**. ⇒ **on a miss the function never returns.** **QA-verified in source**, and the coder reproduced it: a test
asserting the miss case spun the native suite for **16 minutes with no output**.
⚠⚠ **It is worse than "it hangs": the function is `const` and side-effect-free, so an infinite loop here is
UNDEFINED BEHAVIOUR** (C++11+). The compiler may hang, elide the loop, or return garbage, **and the outcome can differ
per optimisation level and per target** — which makes it untestable rather than merely broken.
**Two live call sites, both behind `unlock`:**
- `src/firmware_remote.cpp:195` — `rcmd <unknown-id> <gated-verb>`
- `src/fw_main.cpp:1264` — a sealed rcmd **response** from a node whose beacon we never heard
★★ **The proof it was unintended is the NEXT LINE of the first site:** `if (!th) { … "unknown id (no beacon heard from it
yet)" … }` — **dead code.** Someone wrote a miss-handler for a function that can never report a miss.
⚠ **Secondary defect at the same line:** it scans the whole 256-row array rather than the live `_id_bind_n` prefix, and
the compacting removers leave stale copies in the tail ⇒ **it can return the hash of an EVICTED binding.**
**FIX — one line, closes both:** `for (uint16_t i = 0; i < _active->_id_bind_n; ++i)`.
★ The correct sibling **`key_hash_of_id`** (used by the send path and by AB3's view) is properly bounded — so this is a
one-off, not a pattern. Marked `✖✖` in-source with the fix and the reachability, plus a test pinning the terminating
direction and a **"do not restore the miss-case assertion"** warning so nobody re-hangs the suite.
⚠ **BENCH WARNING: do not run `rcmd <unknown-id> <gated-verb>` on metal until this is fixed — it will hang the node.**
Note: `§ab3`.

### ~~B32 + B33~~ ✅ **CLOSED 2026-07-31** (`§err-reason`) — **two functional lines, and the fix was flash-NEGATIVE**
★ `> err_no_binding ctr=0 depth=0` (was `> err ctr=0 depth=0`); `hashof`'s advice now names the beacon/QR remedies **and**
warns that `reqpubkey <id>` is the one thing that cannot work. Native 1069/71228 → **1069/71239/0**; corpus proven at the
**binary** level (0 recompiles, bit-identical `lus`); boards 3/3 **ΔRAM 0**, `fw_main.o` **−16/−24/−5**.
★★ **The text console now emits the SAME token as the companion `{"ack":"…"}` ⇒ one bench regex serves both transports,
and no contract change is owed.** The `err_` self-labelling convention is now a **native assertion** (the pre-existing
enum-walker was blind to it).
★★★ **Yielded B34** — the same defect **7× in the simulator**, which is why this arc's refusals kept having no automated
detector. *(original entry below)*

### B32 + B33 — ★★ **a refusal that does not name its reason** · ONE SLICE · **BENCH-FOUND 2026-07-31 (owner)**
**The transcript** (owner, on metal):
```
hashof 245     -> [hashof] id=245 -> unknown (neither plane — no beacon heard; try `reqpubkey`)
reqpubkey 245  -> err ctr=0 depth=0
```
★ **The DECISION is correct and must not change.** `reqpubkey <bare decimal>` means a **team_local_id**, so it must
resolve id → hash via `team_key_of_id` first; with no hash it returns **`err_no_binding`** rather than flooding a query
for hash `0` (`node.cpp:1399-1401`, whose comment says exactly that). **C2, working as designed.** Two defects sit on
top of it, and they compose into the transcript above.

**B32 — the text console DISCARDS every `CmdCode`.** `src/fw_main.cpp:860` prints only `queued ctr=` / `err ctr=`, so
`err_no_binding`, `err_unprovisioned`, `err_unknown_dst`, `err_too_large` … **all render identically**. ★★ **The mapper
ALREADY EXISTS and is already reachable: `meshroute::console::cmdcode_name(CmdCode)` (`console_json.cpp:102`, with an
`err_no_binding` arm), and `firmware_commands.cpp:18` already includes `console_json.h`.** ⇒ **a one-line fix.**
⚠ **C2 reading: printing `err` without the reason is not "loud".** This is pre-existing, but this arc added many
fail-loud refusals whose entire value is naming the remedy — so it is newly expensive. ⓘ **Only ONE print site**
(QA-grepped: `err ctr=` appears exactly once in `src/`), so the sweep is genuinely small — **but verify that, do not
trust it.**

**B33 — `hashof`'s advice is CIRCULAR, and it is QA's own wording.** `src/firmware_commands.cpp:553` prints
*"(neither plane — no beacon heard; try `reqpubkey`)"* — but **`reqpubkey <bare-id>` REQUIRES a beacon-heard hash**,
which is precisely the state `hashof` just reported as missing. ⇒ **for a bare team-id target the suggestion cannot
possibly help**; it is valid only for a **hash** target (`reqpubkey 0x<hash>`). ★ **Introduced by `§ab3` and gated by me
— my wording, my miss.**
**The real remedies for an unheard id:** wait for / provoke a **beacon** from that member, or import the peer
out-of-band via **QR → `peerkey <hex64>`**, which needs no prior hash. ⓘ The sibling narrowed messages at `:551-552`
(`drop -t` / `drop -s`) are **correct** — do not touch them.

**Why one slice:** both are the same concern — *a refusal must name its reason and its remedy* — both are `src/`-only,
and B33's new wording is only correct once B32 makes the reason visible. Gate: **corpus byte-identical BY CONSTRUCTION**
(`src/` is compiled by neither native nor the sim — prove it with the 0-recompile / bit-identical-`lus` check), boards
3/3, `sizeof(Node)` unmoved. Note: bench transcript, this file.

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

### B31 — `key_hash_for_id` is neither **authoritative**- nor **TTL**-gated · NEW 2026-07-31
After `§idbind-loop` it shares its loop idiom with `key_hash_of_id`, but **not that sibling's gating** — so it can answer
from a `claimed` (unvouched) or TTL-lapsed `_id_bind` row. ★ **Residual is narrow and that is why it was scoped out:** the
hash it returns only feeds `peer_key_find`, which **ages independently**, and `id_bind_set` maintains the id↔hash
bijection — so a stale answer degrades to a failed lookup, not to a wrong peer. Recorded **in-source at `node.h`** as a
deliberate divergence rather than left silent. ⚠ **Decide the intent before "fixing":** if `rcmd` should refuse an
unvouched target, that is a **policy** change on the remote-admin path (mid-redesign — see B15). Note: `§idbind-loop`.

### B30 — `team_id_of_key` silently first-matches an ALIASED hash · NEW 2026-07-31
`lib/core/node_routing.cpp:855` returns the **first** id whose team-key row carries the hash. ★ **`_team_keys` genuinely
can alias:** `team_key_set` upserts **by id only** and never dedups by hash, so a teammate that re-runs team-DAD leaves
its old `(id, hash)` row live for the full 48 h TTL. ⇒ on the **live plaintext send-by-hash path** the node may pick the
**stale** id — exactly the silent-pick the address-book spec §2.1 forbids for the view.
★ **The reference implementation already exists:** AB3's `team_id_of_key_freshest(hash, &alias_dropped)` picks max
`last_seen_ms` and **reports the loser count**. Fix = route this site through it (U1). Not fixed (C1). Note: `§ab3`.

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

### ~~B28~~ ✅ **CLOSED 2026-07-31** (`§role-model`) — the invariant is enforced, and **the prediction held exactly**
★ **36/36 BYTE-IDENTICAL**, keystone unmoved, native 1023/70582 → **1026/70620**, `sizeof(Node)` 220648, **RAM flat on all
three boards**. ΔFlash +672/+1472/+1636, attributed per-TU and **dominated by the five fail-loud refusal strings that name
the way out** — the cost of C2, paid deliberately.
★★ **The A/B probe pair is the model answer to a 0/N result:** poisoning the *already-consistent* arm moved **1/36**
(`s34`) ⇒ **the inserted call IS executed**; poisoning the *R2-forcing* arm moved **0/36** ⇒ the forcing arm is never
*reached* because no counterexample exists. ⇒ **"the call runs, the decision never fires" — measured, not assumed.**
★★ **THE INVARIANT ALREADY EXISTED AS A BUILD DEPENDENCY:** `lib/core/mr_features.h:47-49` `#error`s on
`MR_FEAT_TEAM && !MR_FEAT_MOBILE` with the words *"a team member is is_mobile"*. ⇒ **R2 is that same statement at
RUNTIME** — the intent was compile-time-enforced on the *build* axis and unenforced on the *config* axis for months.
★ Design: a new `lib/core/node_role.h` holds two **pure** functions (`role_enforce`, `role_set_refusal`) — the one place
`lib/core` and `src/` can both include, which **converted two corpus-dark console decisions into natively testable
logic.** Keyed on `MR_FEAT_MOBILE` (the axis that actually decides it), not `MR_FEAT_TEAM`.
⚠ **Two edits beyond the five, both load-bearing:** `handle_team` now persists the promoted role (else every reboot leans
on the boot backstop), and the O2/R4 refusals also guard the **team-implied** promotion — otherwise `team <id>` is a back
door around `cfg set mobile 1`. ★ And the refusal there **must** be `t != 0`-guarded, or O1 fires on `team 0` and
**refuses every leave** (pinned by a test).
★ **Named residual, in-source:** `Node::on_init` is deliberately NOT a fourth enforcement point — which is *why* the
corpus can still construct the outlawed config and byte-identity stayed a real prediction. A fix there should **refuse**,
not silently normalise, and it can move team scenarios. Note: `§role-model`. *(original entry below)*

### B28 — ★★ **`is_mobile` must be set AUTOMATICALLY when a team is in use** · **OWNER-RULED 2026-07-31**
**Owner: *"is_mobile should be automatically set when team is in use — unless it is impossible (firmware without teams
handling)."*** QA agrees, and the corpus is the argument: ★★ **all 12 team scenarios, 48 team-bearing nodes, are ALREADY
`is_mobile` — ZERO counterexamples** (QA-measured). The code already says it in prose too (`firmware_config.cpp:900`:
*"Requires is_mobile (a team is mobile)"*). ⇒ **this ENFORCES an invariant the corpus and the comments already assume**,
which predicts a **corpus-inert** change — a strong, checkable prediction rather than a hope.
★ **Why it matters beyond tidiness:** `team_id != 0 && !is_mobile` is the config that **defeats the H-flood
role-exclusion invariant** (BASELINE ~line 433) and is the only way several team arms of `liveness_penalty_q4` /
`rt_merge` are reached. Outlawing it turns "corpus-dark" into "unreachable".

**FOUR CONSTRAINTS QA VERIFIED — get any of them wrong and the fix is worse than the bug:**
1. ★★ **TWO enforcement points, not one.** The live switch (`Node::set_team_id`, the single core entry for `team new` /
   `team <id>` / `team 0`) **and** the boot path — **NV persists `team_id` and `is_mobile` INDEPENDENTLY**
   (`fw_main.cpp:603-604`), so a reboot reproduces the outlawed config with no console involved. Enforcing only the
   live switch leaves the hole wide open — **the sweep-scope trap, ninth instance was yesterday.**
2. ★★ **ONE-DIRECTIONAL. `team 0` (leave) must NOT clear `is_mobile`** — a mobile with no team is legitimate (a homed
   mobile on a static network). team ⇒ mobile is an implication, **not** a toggle. **State it in-source**, or a future
   "simplification" will make it symmetric and silently un-mobile every departing member.
3. ⚠ **REPORT IT, do not flip it silently.** `is_mobile` changes beaconing, home registration, DAD and relay
   behaviour — an operator typing `team <id>` on a static node must be **told** the role changed (C2 spirit).

★ **The owner's "unless impossible" case is REAL and QA measured its shape:** on the gateway profile **both
`MR_FEAT_TEAM 0` AND `MR_FEAT_MOBILE 0`** (`lib/core/mr_features.h:12-13`) — so there is no mobile plane to enter.
⇒ on such a build the answer is **treat `team_id` as 0 / refuse loudly**, **NOT** set a flag whose plane is compiled
out. ⓘ And after **B27** removes `cfg set team_id`, a no-team build has **no console path to a non-zero team_id at all**
(`handle_team` is **absent from the gateway ELF** — the B17 link-level finding. ⚠ **CORRECTED 2026-07-31: the guard is `#if MR_N_LAYERS < 2`** (`firmware_config.h:40`), **not** `MR_FEAT_TEAM`; the conclusion holds only because `env:gateway` sets **both** `-DMR_N_LAYERS=2` and `-DMR_PROFILE_GATEWAY`. The axes are independent — a single-layer `MR_FEAT_TEAM 0` build would still compile `handle_team` with its internals stubbed), so the
exemption is nearly vacuous **by construction**; only a provisioned NV blob can still carry one.
★★★ **THE DEMOTION QUESTION — asked by the owner, and it exposed a THIRD enforcement point QA had not named.**
*"How do we move from mobile role to static role?"* QA traced all four candidates:
- ✅ **`leave` IS the sanctioned demotion, and it is already clean** (`firmware_config.cpp:1049`): it does
  `b = mrnv::Blob{}` — **zeroing the whole blob** except freq/PHY defaults, so `is_mobile`, `team_id` **and**
  `team_local_id` all go to 0 **together**, then `provision_apply_live(b, do_dad=false)` applies it live (unprovisioned +
  idle). ⇒ **`leave` cannot create the outlawed config, and B28 needs NO change there.**
- ❌ **`join` / `create` do NOT demote** — the owner's first guess, and the code is explicit against it:
  `firmware_config.cpp:494` **preserves** `is_mobile`, `team_id`, `mobile_autoregister`, `team_local_id` **and** the team
  channel key across create/join (*"§mobile: preserve team + autoreg + team-DAD id across create/join"*; the key is
  **unrecoverable if dropped**). They clear the mobile's *learned* state (`_my_mobile_reg`, via `clear_routing_state`)
  but keep the **role**. ⇒ **provisioning is not a role change** — worth knowing independently of B28.
- ⚠⚠ **`cfg set mobile 0` IS THE THIRD ENFORCEMENT POINT** (`firmware_config.cpp:220`) — a **raw flag flip** that clears
  nothing and is **reboot-to-apply**. So it can leave NV holding `is_mobile=0, team_id=X`, and **B28's boot
  normalisation would then RE-SET `is_mobile=1` and silently undo the operator's demotion.**
  ⇒ **QA recommendation: REFUSE `cfg set mobile 0` while `team_id != 0`**, naming `team 0` or `leave` as the way out.
  **Do NOT cascade** (silently clearing the team from a command that says nothing about teams would destroy the team key,
  the team-DAD id and the team routes — and only `join`/`create`/`leave` are allowed to wipe planes, per `§clean-team`).
★ **With those three points the invariant holds on every path with no silent destruction anywhere:** `leave` zeroes both
fields atomically · `cfg set mobile 0` refuses while in a team · the boot normalisation is the backstop for a
provisioned NV blob.

★★★ **B28 IS NOW SCOPED BY A SPEC: `docs/superpowers/specs/2026-07-31-node-role-model-design.md`** — the owner asked for
a consistent role model after the demotion question showed the transitions are **five mechanisms that disagree**. The spec
holds the organising principle (**the role is about how reachability is obtained**, which makes team ⇒ mobile a
*consequence* rather than an extra rule), rules **R1–R5**, the transition table, and **three owner decisions (O1–O3)**.
★ **It also found a problem this entry did not have: static → mobile ORPHANS HOSTED MOBILES** — a host that becomes a
mobile stops carrying the static plane and its guests lose their home with no notification (`mobile_reg_count()` already
detects it). ⇒ **O2.** **Read the spec before implementing B28.**

**Order: after B27** (which removes one of the two write paths, shrinking what must be normalised). Note: owner ruling,
this file + the role-model spec.

### ~~B27~~ ✅ **CLOSED 2026-07-31** (`§team-id-cfg-removal`) — the key is GONE, and the family is now closed on the device
★ **−2 executable lines, 0 added.** Native **1023/70582/0 unchanged**, 36/36 byte-identical with the **0-recompile /
bit-identical-`lus`** proof, keystone unmoved, `sizeof(Node)` 220648, boards 3/3 **ΔRAM 0** and ★ **ΔFlash NEGATIVE on all
three (−112 / −80 / −40)** — including `gateway`, which is the sharper instrument reading in the *opposite* direction
from `handle_team`: `handle_cfg_set` **is** in every ELF, so the gateway is **not** inert here.
★ **Removal fails LOUD** via the existing `unknown_key` branch (`firmware_config.cpp:339`) — C2 by construction, no new code.
★ **A stronger corpus premise than briefed: the sim has NO `cfg` verb at all** (`NodeRuntimeWrapper.cpp:741-742`), so the
key was unreachable from every scenario by construction.
⚠ **The `✖ MISSING` marker AND the `§clean-team` note above it were both removed** — QA had warned to preserve the
neighbour, and that warning was **wrong**: all five lines were about the deleted key (*"**this key** is
reboot-to-apply"*). The durable claim lives in `lib/core/node.cpp:529/574` + `node.h:364`, untouched.
★ **And it does NOT close the role hole** — `team <id>` still reaches `team_id != 0 && !is_mobile`; that is **B28**, and
the replacement marker says so in-source so B28 cannot inherit a false premise. Note: `§team-id-cfg-removal`.
*(original entry below)*

### B27 — ★★★ `cfg set team_id` has **NONE** of the three guards ⇒ **B1 and B17 are NOT closed on the device** · NEW 2026-07-31
`src/firmware_config.cpp:238` does `set_team_id((uint32_t)strtoul(val, nullptr, 0))` with the **endptr DISCARDED**, so it
enforces **no leading-digit rule, no whole-token consumption and no range clause** — all three defects `parse_team_target`
now guards. It is a **LIVE team switch**, and `cfg set` is dispatched from **every transport** (the USB reader *and* the
shared sink BLE/companion use). QA-verified in source. Spellings measured:
- `cfg set team_id exportky` → `strtoul` yields 0 → **LEAVE THE TEAM** (the 07-30 `§team-target` bug)
- `cfg set team_id 88A672BA` → **joins team 88** (B1)
- `cfg set team_id 4294967296` → **LEAVE on host / garbage `0xFFFFFFFF` on the boards** (B17)
★★ **This is the NINTH instance of the sweep-scope meta-bug, and the most pointed: two slices the owner personally
flagged were both declared closed while a second, console-typed parse path kept every defect.** ⇒ **the question was
scoped to a FUNCTION when the bug was a FAMILY.**
★★★ **OWNER RULING 2026-07-31: `cfg set team_id` is to be REMOVED, not guarded.** That is the better call and it is
the U1/dedup answer at the SURFACE level: `team <id>` already does everything this key did **plus** the three guards, the
PHY tail, channel-key minting, `team_dad_fire` and the full NV persist — so the key was a **forked, unguarded duplicate of
a destructive operation**, not a feature. Removing it deletes the parse bug instead of copying the fix into it.

★★ **THE LINE THAT MAKES THIS SLICE PRECISE — REMOVE THE *WRITE*, KEEP THE *READ*.** QA-verified surface map:
**REMOVE (3 sites):** the setter branch `src/firmware_config.cpp:238` · `team_id` from the **`cfg set` key list**
`src/firmware_commands.cpp:563` · and rewrite the explanatory line `:566` (*"team_id=0x-hex (`team new` mints)"*) to point
at the **`team` verb** instead of advertising a key that no longer exists.
**KEEP — every READ surface, untouched:** the `cfg` text dump (`firmware_commands.cpp:126-128`) and `status`
(`:197-198`) · the JSON (`console_json.h:220`) · **and the binary TLV `TAG_CFG_TEAM_ID = 0x12`** in `enc_cfg` **and**
`dec_cfg`. ⚠⚠ **DO NOT retire tag `0x12` — this is NOT the `loc_dm` case.** QA verified `dec_cfg` is a **pure decoder**
(declared `console_binary.h:108`, defined `console_binary.cpp:144`, **called from no `src/` file**) ⇒ the TLV is a
**read-out**, not a write path, and the app **depends on it**: `ios-companion/…/Inbound.swift:200` documents `team_id` as
*"ALWAYS present in cfg"*. Removing the read would break the companion. ⇒ **the Q-opcode retirement lesson does NOT
apply here; over-applying it is the failure mode to avoid.**
★ **Corpus-safe: NO scenario uses `cfg set team_id`** (QA-grepped `simulation/` and the sim's `scenarios/`). The sim's
scenario-config key `team_id` is a different mechanism (node setup, not the console) and stays.
⚠ **Live doc owed:** `docs/protocol.md` mentions `cfg set team_id` and is **QA-owned** — report it, I rewrite it.

*(superseded fix direction, kept as the record)* **Fix (U1, small): route this key through `mrfw::parse_team_target`** — do not re-implement the guards. Marked
`✖ MISSING` in-source with all three spellings. ⓘ **Blast radius checked: the companion app does NOT send this key today**
(`grep` over `ios-companion/**/*.swift`) ⇒ operator-typed, not app-driven — a fact about today, not a guarantee, since it
is a documented cfg key. **QA rates this above several parked Tier-3 items: it is destructive and reachable by typo.**
Note: `§team-target-range`.

### ~~B22~~ ✅ **CLOSED 2026-07-31** (`§b22`) — 4 re-anchors, and **the residual delta is only the command echo**
★ s22 `c5d9b6c5`/1804 · s28 `f3d6afc6`/4018 · s29 `208f29c5`/1943 · s34 `03c9d998`/921 — **event counts back to baseline
exactly**, native unchanged, s18 unmoved, 0 failures in all 36. **Assertion counts AND canonical bodies byte-identical**;
★★ **Poison D (restore the heuristic under the edited scenarios) = 0/36 ⇒ EQUIVALENCE, so masking was structurally
impossible.** ⚠ **The sim had no `-t` grammar at all** — the slice had to add it (via `dm_plane_from_tail`) or ` -t` would
have aired as payload. ⚠ **STATE 1 exposed a genuine containment breach** (s28: XH1's plain post leaked to static S2),
confirming the four scenarios were validating a plane the command never asked for. **CL2: those 10 `-t` flags are
load-bearing.** Note: `§b22`. *(original entry below)*

### B22 — ★★ plain `send_channel` **SUCCEEDS in the sim and is REFUSED on metal** · NEW 2026-07-31
The sim's "NATURAL" arm (`NodeRuntimeWrapper.cpp:813`) sets `team = (is_mobile && team_id != 0)`, so a plain
`send_channel` from a team member becomes a **TEAM flood**. The firmware sets `team=false, global=false` for a plain
post (`console_parse.cpp:205-213`, *"plain = GLOBAL"*), and `node.cpp:1323`'s `want_global = global || !team` routes it
to the **global/home** path — where an **off-grid** member has no home and it **fails loud**
(`send_failed{channel_no_home} ⚠ **NAMING CORRECTED 2026-07-31 (`§cl1`): `channel_no_home` is ONLY the `MR_EMIT` TELEMETRY string (`node.cpp:1434`); the PUSH carries `SendFailReason::mobile_no_home` (`:1435`). There is NO `channel_no_home` enumerator** — so a scenario sees `channel_no_home` while the app sees `mobile_no_home` for the same event. **Do not go looking for the enumerator.**` + `err_no_binding`). **Both sides QA-verified in source.**
★★ **This is worse than B3, which it was found by: not a different plane but SUCCESS-vs-REFUSAL.** **10 corpus commands in 4 scenarios take that arm** (s22 ×2, s28 ×3, s29 ×2, s34 ×3) — ★ **the ORIGINAL figure, restored: my
2026-07-31 "correction" to 12 was itself WRONG and the coder caught it.** ⚠ **My script matched
`command.startswith("send_channel")`, which silently includes the SUFFIXED VERBS `send_channel_g` and `send_channel_b`** —
and my `" -g" not in cmd` guard missed them because in those verbs the `g`/`b` is part of the **verb name, not a flag**.
XH1 in s28 has exactly 2 plain posts plus one `_g` and one `_b`; counting all four gave the phantom 12. ★★ **THE RULE,
now twice-earned: evaluate the PREDICATE *and* match the VERB EXACTLY. My first correction fixed the predicate half and
left the verb half broken — a prefix match on a verb name silently swallows its suffixed siblings.**
★ **Confirmed three independent ways** (the coder's derivation, the byte-diff showing exactly 10 changed `cmd_reply`
echoes, and Poison A reddening exactly those 4 scenarios) ⇒ **s22's and s34's team-channel assertions validate a
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

### ~~B26 / NV1~~ ✅ **CLOSED 2026-07-31** (`§nv1`) — and **my count was low four ways**
★ native 1014/70507 → **1023/70582** (+9 cases, +75 assertions — the **first** native coverage of `device_nv.h`), 36/36
byte-identical with the **0-recompile / bit-identical-`lus`** proof, keystone unmoved, boards 3/3 **ΔRAM 0** and
**ΔFlash NEGATIVE** (−16/−96/−560, object-attributed to exactly the 4 TUs that include the header).
**Corrections to this entry:** the **size** check was written **16** times (all 8 loads *and* all 8 saves), not 6 · each
arm had **10** definitions and there were **three** arms ⇒ **30 → 23** · ΔFlash is negative, not ≈0 (the right signature
for removing duplicated *inline* code) · ★★ **the `#else` stub arm was DEAD CODE** — `#if defined(ARDUINO)` wrapped the
whole section, so the host build had **no `mrnv::` functions at all**; NV1 makes it the host arm, so **all three arms are
now compiled by the gate** and a mutant proves the host arm is *executed*, not just compiled.
★★ **MUTANT F IS THE FINDING THAT VINDICATES THE SCOPE LINE: deleting `save()`'s H3 change-detection leaves native
1023/1023 GREEN.** Nothing automated can see it — native cannot observe a write and the corpus cannot compile `src/` —
so **the owner's bench is its only detector.** That is precisely why its logic is byte-for-byte unchanged.
★★ **Built as AB1's FORCING FUNCTION:** `test_device_nv.cpp`'s `/mrpeers` case asserts version±1 rejected **relative to**
`kPeersVersion`, so AB1's bare bump to 2 **stays green and IS the mandated v1-rejection test**, while switching to
`blob_valid_range` turns it **RED** — the range-vs-exact decision **cannot be made silently.** Three constant tripwires
(`kPeersVersion == 1`, `sizeof(PeerRec) == 36`, `sizeof(PeerBlob)`) must be updated deliberately. Note: `§nv1`.
*(original entry below)*

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

### ~~B17~~ ✅ **CLOSED 2026-07-31** (`§team-target-range`) — ⚠ **on the `team` verb ONLY; the family is still LIVE ⇒ B27**
★ 36/36 byte-identical, keystone unmoved, native 1013/70440 → **1014/70507**, `sizeof(Node)` unchanged, boards 3/3 ΔRAM 0.
Two arms — `errno == ERANGE` (32-bit saturation) and `ul > UINT32_MAX` (64-bit truncation) — verified against the
**disassembled shipped newlib**, not its docs (`ERANGE == 34` on ARM and Xtensa).
★★ **MY CORRECTION TO THIS ENTRY WAS ITSELF HALF-WRONG:** I claimed the `>2^64` token gives the ERANGE arm *real
coverage* on native. It gives **exercise, not necessity** — on 64-bit `ERANGE ⇒ ul == ULONG_MAX`, which is already
`> UINT32_MAX`, so **the width arm subsumes it**, and deleting the ERANGE arm leaves native **fully green**. The honest
framing is **one arm per ABI, each inert on the other**, asserted in-test so a "simplification" trips over it.
★ **Generalisable: "a test exercises this line" ≠ "a test would FAIL without it." Only the second is coverage — a mutant
separates them, a passing suite does not.**
★★ **And my "boards only" framing understated it:** `team 99999999999999999999999` **joins garbage `0xFFFFFFFF` on the
HOST too**. Only the *token* that triggers the damage is ABI-specific, not the damage. Note: `§team-target-range`.
*(original entry below)*

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
