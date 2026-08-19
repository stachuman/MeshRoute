<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# ★★★ STATE / HANDOVER — 2026-08-19 · written at context exhaustion

**Read this first.** Everything below is durable repo state. ⛔ **Nothing important lives only in the conversation.**

---
## 1 — WHERE THINGS STAND
**HEAD `164bc53`** ("U14 bug fixing"). ⛔ **A LARGE UNCOMMITTED TREE** — the owner commits (D4). Three workstreams:

| scope | files |
|---|---|
| **UI-15 slices 1+2 (+corrections)** | `src/firmware_join_service.h` · `src/firmware_join_profiles.h` · `src/device_nv.h` · `src/firmware_config.{h,cpp}` · `src/firmware_config_parse.h` · `src/firmware_commands.cpp` · `test/test_firmware_join_{service,profiles}.cpp` · `test/test_device_nv.cpp` · `test/test_firmware_config_parse.cpp` · `tools/probe_ui_model_mutations.py` |
| **QA-gate docs** | the bug register · the bench script · the UI-15 plan · 4 plans/briefs + 1 evidence file |
| ⚠ **not ours** | `B164.md` · `docs/manual/` · `tools/probe_board_ui/run.sh` |

**Gate at handover (all measured):** native **1764 / 84705 / 0** · `probe_prov_tx` 19/19 + 40 RED · `probe_board_ui`
120/120 + 14/14 + 52/52 + 154 RED · `probe_firmware_ui` 229/229 · `probe_console_sink` 52/52 + 16/16 ·
census **174/178/178**, `-Wswitch` 0 · `lus` `b77cfd3d` · s18 **`9868cad3` / 269905** · `sizeof(Node)` **221880**.

---
## 2 — THE UI-15 ARC
**Plan: `docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md` — v6, SIX revisions, no open
questions.** ⚠ It keeps withdrawn wording visible; ⛔ **the latest correction always wins.**

| slice | state |
|---|---|
| **1 — typed static-join transaction** | ✅ done, QG-passed. `JoinRequest` carries **`double freq_mhz` + `double bw_khz`** |
| **2 — `/mrjoin` store + console** | ✅ done; **three QG blockers found and fixed** (B218/B219/B220) |
| **3 — fingerprint helper** | ⛔ NOT STARTED — `team_id & 0x00FFFFFF`, uppercase zero-padded hex, **display-only, zero wire bytes** |
| **4 — provisioning state model + gating** | ⛔ not started |
| **5 — team-create screens** | ⛔ not started — `phy.present=false` **+ a live-vs-persisted equality precondition** |
| **6 — static-join screens + async outcome** | ⛔ not started — ★ the **four-term correlation rule** is the hard part |
| **7 — metal qualification incl. `/mrjoin` power-cut** | ⛔ not started |

### ★★ Three traps that will bite slices 3-7
1. **`join_adopted` fires for the verb, BOOT DAD *and* the heal re-adopt; `join_refused` carries wire-version
   observations about OTHER peers.** ⇒ the four-term correlation rule (plan §2.3) is **mandatory and
   mutation-tested**; no `join_refused` reason may fail v1.
2. ⛔ **`layer0_id` is the full byte, `leaf_id` the nibble, and live apply mirrors the NIBBLE.** A rule comparing
   "stored full layer == live layer" is **unsatisfiable above layer 15**. Compare like-for-like.
3. **`src/firmware_config.cpp` is compiled by NEITHER the native suite NOR the simulator.** ⇒ **extract pure logic
   into `firmware_config_parse.h` to make it provable** — this worked three times (B212, B220, slice 1's classifier).

---
## 3 — OPEN REGISTER ROWS NEEDING ACTION
- **B214** — label FIXED (QA-gate authored, ⛔ nobody else reviewed) but ⛔ **the regression guard was never built**;
  the `probe_console_sink` extension + five controls are owed. **The row does not close.**
- **B215** — `reset_join_for_reprovision()` leaves listen/retry timers armed ⇒ **a node told to LEAVE can put a
  `J_CLAIM` on air up to 10 s later.** Measured. Needs its **own slice** (`lib/core` ⇒ re-runs the corpus).
- **B216** — `freq=nan` / `bw=nan` accepted; a node can be provisioned with **`bw_hz = 0`**. Deliberate per a standing
  instruction; **`/mrjoin` refuses it at its own boundary**. ⛔ No ruling on closing it.
- **B217** — ★★ **a stale `BASE_CASES/BASE_ASSERTS` pin ABORTS every mutation battery without applying a mutation.**
  Re-pinned to **1764/84705**. ⛔ **Re-pin whenever native counts move, and confirm the battery actually RAN** — an
  aborted battery reports no RED lines, which reads exactly like one with nothing to find.
- **B221** — the NV adapters are compiled by no gate (reading, not measurement).
- **B205/B206/B208/B210-fix/B213** — smaller, all recorded with fix shapes.

---
## 4 — THE METAL BACKLOG
**Run sheet: `docs/superpowers/plans/2026-08-18-metal-session-run-sheet.md` (v2)** — sequencing, preconditions and
stop rules over the bench script. ⛔ **The bench script is the authority; if they disagree the sheet is wrong.**
⚠ **Hardware at last check: 2 × `heltec_mobile` + 1 × `xiao_esp32s3`** ⇒ **27.1 (stack) CANNOT run — `stackhw` is
nRF52-only**, and it is **not** discharged (B209-B212 changed that call path).
⚠ **`xiao_esp32s3` reported `nogit` (B213)** — fixed in `platformio.ini`, ⛔ **unverified on metal**.
**Owed:** Parts 24/25 (chrome) · 27.11-27.15 · Part 22 · **28.1-28.6** (`/mrjoin`) · 27.6 (needs a static node) ·
**B193 Part 20.5 LAST — it is the destructive one.**

---
## 5 — STANDING RULES THAT MUST SURVIVE
- ⛔ **The QA-gate never commits** (D4) and never runs `git add` / `git checkout --`. **The owner commits.**
- ⛔ **QG is dispatched by the OWNER.** Never claim an approval that was not given; ⛔ never quote an owner ruling —
  reported form only.
- ⛔ **Never edit `simulation/BASELINE.md`'s `^### 36/36 corpus` anchor table.**
- ★★ **BOARD BUILDS: TWO ENVS.** The census is **exempt** (owner-ruled, challenged, **re-confirmed**) — ⛔ do not
  re-litigate. ⛔ **A brief must never pre-authorise more; I broke that in four consecutive briefs.**
- ★★ **Control rule:** defect-specific regressions must **FAIL** under a controlled mutation; **unchanged positive
  controls must stay GREEN.** ⛔ Never "everything must go red" — that is impossible and I wrote it once.
- ★★★ **NINETEEN instruments in this arc were green against the defect they were written to catch.** ⇒ **prefer a
  COUNTED discriminator over a state assertion** (timer arms, write counts, preserved-field counts, literal
  occurrences). Ask of every check: *could it have come out otherwise?*
- ★★ **Verify against the DECLARATION, never a comment.** Five comments carried false claims into decisions this arc
  (`adopt_priv` "cannot fail here" · the `NodeConfig` copy "dies at the closing brace" · `mobile_register_phy` "kicks
  the FSM" · the `sf_list` "common PHY" · `git_rev.py` "nRF52 only").
- ★ **Evidence must land IN THE REPO.** Slice 1's byte-identity harness was built in a scratchpad and **is gone**.
- ★ **State HEAD as observed at dispatch, never as fact** — it moved under three briefs.
- ⛔ **Never freeze a status count or a Parts list in a plan** — it ages into a false claim. Point at the live doc.

---
## 6 — IMMEDIATE NEXT STEPS
1. **Owner: review + commit** the tree (UI-15 slices 1-2 are a coherent unit; the docs can ride along or follow).
2. **QG the slice-2 corrections** (B218/B219/B220 are recorded as closed **pending that review**).
3. **Then either:** slice 3 (fingerprint — small, pure, no metal dependency), **or** the metal session, which is the
   larger backlog and gates UI-15's later slices.
4. **B214's guard** is the cheapest outstanding debt and closes a row.
