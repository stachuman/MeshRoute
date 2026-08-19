<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 — on-device provisioning: IMPLEMENTATION PLAN **v5** · 2026-08-19

**Status: PLAN v4 — THIRD QG round applied, plus the owner's PHY ruling. ⓘ v3 fixed four blockers (two were
contradictions inside v2, one a real RF defect); v4 fixes a correlation term that was UNSATISFIABLE above layer 15.**
⛔ **NO PRODUCTION CODE, no storage-format change, no bench-baseline change.**
★ Role split: the QA-gate wrote this; **the OWNER rules.** Normative: `2026-07-31-onboard-oled-ui-design.md` §3.6.1-3.6.5.
⚠ Where that design and the **as-built** code disagree, this plan records the as-built.

---
## §1 — Reconciliation (⛔ v1 OVERSTATED THIS AND THE CORRECTION IS THE PLAN'S BACKBONE)
v1 claimed *"the engine, the atomicity, the entry point and the outcome vocabulary are built"*. **That is true of TEAM
and FALSE of STATIC JOIN.**

| §3.6.3 needs | as-built (verified) |
|---|---|
| team creation engine | ★ **EXISTS** — `ProvisioningService::apply_team`, `TeamRequest`, `TeamPlan`, `ProvResult`, 12-arm `ProvErr`, `IEntropy`/`IProvLive` (`src/firmware_provisioning_service.h:649`) |
| team atomicity | ★ **DELIVERED by [[B207]]** — stage without mutation → ONE save → apply → `fire_dad` last; failure applies nothing, spends no airtime |
| PROVISION entry point | ★ EXISTS — `CfgRow::provision` (`src/firmware_ui_model.h:164`) |
| ⛔ **static-join engine** | ⛔⛔ **DOES NOT EXIST.** `JoinRequest` count in the service = **0**; `handle_join` is a hand-written console verb (`src/firmware_config.cpp:769`) |
| four `/mrjoin` profiles | ⛔ does not exist |
| team-id fingerprint | ⛔ does not exist |
| `sf_list` as shared PHY | ⛔ **REFUTED** (`lib/core/node.h:302-305`; [[B211]] preserves it) ⇒ profiles carry `routing_sf`, **never `sf_list`** |

⇒ **OLED static join cannot be built on what exists. A typed join transaction must be extracted first (slice 1).**

---
## §2 — The typed seams
### 2.1 Team (exists)
`UiProvIntent` (pure, model-owned) → adapter builds a **`TeamRequest`** → `apply_team` → `ProvVerdict`/`ProvErr`.
⛔⛔ **CORRECTED v3 — v2 SAID `phy.present = false` MEANS "the current effective PHY". IT DOES NOT.** It **preserves the
PERSISTED PHY and performs no retune** — so if the live radio differs from NV (which `mobile register` can cause
without persisting, the [[B211]] condition), it does **not** durably capture what the node is actually flying.
★★ **OWNER RULING 2026-08-19, EXPLICIT AND SPECIFIC TO THIS CASE (reported form, ⛔ not quoted): OLED team creation
PRESERVES THE PERSISTED PHY and REFUSES with `PHY DIFFERS — USE SERIAL` when live and persisted PHY differ.**
ⓘ Recorded as its own ruling because the earlier `team 0` PHY ruling is **related but not identical** — that one
governs *leaving*, this one governs *creating from the OLED*.
★★ **IMPLEMENTATION (U1): reuse `live_phy_matches()`** (`src/firmware_provisioning_service.h:336`) with a `ProvPhy`
built from the **persisted** values — ⛔ **including `sf_list`** — rather than writing a second equality predicate that
could drift from it.
⚠ **THE TWO `ProvPhy` OBJECTS ARE DIFFERENT AND MUST NOT BE CONFLATED:** `live_phy_matches` **early-returns `true`
when `!phy.present`** (`:337`), so the **precondition** builds its OWN `ProvPhy` from persisted values with
**`present = true`** purely to run the comparison — while the **`TeamRequest` still carries `phy.present = false`**, so
no retune is requested. ⛔ Setting `present = true` on the request itself would re-introduce the [[B209]] path. ⓘ The alternative — snapshot and persist the live PHY — would force a
decision about a live-divergent `sf_list`, and is **not** taken here.
⇒ `TeamRequest::phy.present = false`, **plus an explicit live-vs-stored equality precondition**. ⛔ Still no PHY tail:
a tail routes through `apply_phy` and, per **[[B209]]**, must never start static-home discovery. The OLED create is a
membership operation only.

### 2.2 Static join (⛔ must be EXTRACTED — slice 1)
A **sibling** transaction, ⛔ **not** an overload of the team one:
- **`JoinRequest { uint8_t layer; double freq_mhz; uint32_t bw_hz; uint8_t routing_sf; }`**
  ⛔⛔ **CORRECTED v3: the TRANSIENT request keeps `double freq_mhz`.** v2 specified `uint32_t freq_khz` and that is a
  **real RF defect** — **869.4625 MHz is 869462.5 kHz**, so a `uint32_t` rounds it to 869462 and **changes the
  frequency the bench actually runs on**. It would also have broken slice 1's byte-identity claim. ⇒ **preserve the
  existing parser's `double` exactly**; only the *stored* profile is integral (§3).
- **its OWN typed `JoinErr`** — ⛔ not merely `started`/`refused`/`nv_failed`: arms for **invalid layer**, **invalid
  frequency**, **invalid BW**, **invalid SF**, **load failure** and **save failure**. ⛔ **`ProvErr` is the TEAM
  vocabulary and must not be pressed into service as the join vocabulary.**
- ★★ **AND IT NEEDS AN INJECTED LIVE SEAM (v2 omitted it): a natively-tested save-before-apply transaction CANNOT call
  the device-only `provision_apply_live()`.**
  ```
  struct IJoinLive { virtual void apply_and_start(const mrnv::Blob&) = 0; };
  ```
  The device implementation delegates to the existing `provision_apply_live(blob, /*do_dad=*/true)`. **The fake pins:**
  **zero** live calls on validation / load / save failure · **exactly one** after a successful save · and
  **save-before-live ordering**.
- shared validation + candidate construction; **ONE save**, then the live seam.
- **console `handle_join` and the OLED adapter both call it.**
- ★★ **During the extraction slice the console/JSON output must be BYTE-IDENTICAL** — that is what makes it a
  refactor (C1) rather than a behaviour change riding a refactor.
- may reuse **`ICfgStore`**.

### 2.3 ⛔⛔ THE ASYNCHRONOUS OUTCOME — v1 MISSED THIS ENTIRELY
**A successful transaction only STARTS DAD.** The real outcome arrives later as a push, and **both push kinds are
shared channels carrying unrelated events**:
- **`PushKind::join_adopted`** (`lib/core/command.h:223`) fires for *"verb join/create, **boot DAD**, OR the **heal
  re-adopt**"* — ⇒ ⛔ **a boot DAD or a heal re-adopt would complete an OLED screen that had nothing to do with it.**
- **`PushKind::join_refused`** (`:204`) covers *"wire_version mismatch / leaf full"* — ⇒ ⛔ **a wire-version
  OBSERVATION ABOUT SOME OTHER PEER would fail the operator's join**, and mobile-home PHY/`sf_list` refusals ride the
  same kind.

**Required specification:**
1. a successful transaction shows **`JOINING`**, ⛔ **never `JOINED`**;
2. **only a CORRELATED `join_adopted` completes** the screen, showing the resulting node id;
3. ★★ **an explicit CORRELATION / ELIGIBILITY RULE** — which pushes belong to *this* operation (started-by-us,
   in-window, on the requested layer) — and ⛔ **it must be MUTATION-TESTED**: a mutation that accepts any
   `join_adopted`, or any `join_refused`, must go **RED**. ⓘ This is the *"a success that isn't"* class this project has
   already recorded once;
4. ★ **BACK during `JOINING` only LEAVES THE SCREEN** — ⛔ it does **not** cancel or roll back an already-persisted
   operation (QG-recommended, adopted);
5. ★ **TIMEOUT: after 60 s show `STILL JOINING`, ⛔ NOT a failure.** Normal adoption is ~23 s; one conflict/retry can
   reach ~53 s; **retries are not finitely bounded** — so a deadline that declared failure would lie;
6. ★★ **NO `join_refused` reason terminally fails UI-15 v1.** The shared push cannot reliably separate static DAD, team
   DAD and unrelated observations — ⇒ **ignore them all for completion purposes** rather than guess;
7. ★★ **COMPLETE ONLY ON `join_adopted`, AND ⛔⛔ v3's FIRST TERM WAS UNSATISFIABLE ABOVE LAYER 15 — CORRECTED.**
   v3 required *"the stored full requested layer equals the live layer"*. **It can never hold for layer > 15:** the
   join stores the **full** byte in `Blob::layer0_id` (`cfg set layer0_id` validates 0..255) but only the **nibble** in
   `Blob::leaf_id` (validated 0..15), and **live apply mirrors the NIBBLE into `layers[0].layer_id`** —
   `lc.layers[0].layer_id = b.leaf_id` (`src/firmware_config.cpp:753`), the same mirroring single-layer init performs
   (`lib/core/node.cpp:459`). ⇒ a request for **layer 17** persists **17**, lives as **1**, and pushes leaf **1**;
   the old rule would have made OLED join **permanently fail on every layer above 15**.
   ★ **THE CORRECTED RULE — four terms, each compared LIKE-FOR-LIKE:**
   1. **a UI join session is active**;
   2. the **cached requested FULL layer equals the current persisted `/mrcfg.layer0_id`** (persisted↔persisted);
   3. **`push.layer_id == requested_layer & 0x0F`** (nibble↔nibble);
   4. **`push.dst == g_node.canonical_node_id()`, and non-zero** (id↔id).
   ⛔ **Mutation-test ALL FOUR terms separately.**
   ⚠ ⛔ **Do NOT "fix" the live full-layer behaviour inside slice 1** — it is a separate, pre-existing design question;
   touching it would destroy that slice's byte-identity claim.
8. **emergency pre-emption** of the waiting screen (§3.6.5).

⚠ **AUDIT ITEM, ⛔ NOT TO BE "FIXED" INSIDE THE BYTE-IDENTICAL EXTRACTION SLICE:** `reset_join_for_reprovision()`
appears to cancel **only the claim guard**, not the old listen/retry timers (`lib/core/node_join.cpp:531`). ⇒ **audit
it, and if it is a defect, REGISTER it (M1) and fix it in its own slice** — folding it into slice 1 would destroy that
slice's byte-identity claim, which is the only thing making it a refactor.

---
## §3 — `/mrjoin`: the storage contract
A NEW `mrnv` record, **separate from `/mrcfg`** (§3.6.3: corruption must not reset config, identity, team keys or
presets). Own `kMagic`, own `kVersion`; ESP32 by `Slot::ns`+`Slot::key`, nRF52 by file — ⛔ backend split unchanged.

- **Layout:** fixed **four** slots, no dynamic count.
  `struct JoinProfile { uint8_t present; uint8_t layer; uint8_t routing_sf; uint8_t name_len; uint32_t freq_hz;
  uint32_t bw_hz; char name[12]; }` — ★ **naturally 24 bytes, so ⛔ do NOT invent per-profile padding** (v2 asked for
  it; QG measured that it is unnecessary). Put **explicit `reserved`** in the **record header** if any is wanted, and
  keep **`static_assert(sizeof(JoinProfile) == 24)`** plus one for the record.
  ★ **Zero the unused `name` bytes** — deterministic bytes are what make write-coalescing meaningful.
- ★★ **`freq_HZ`, NOT kHz (CORRECTED v3):** 869.4625 MHz is **869462.5 kHz** — not representable in `uint32_t` kHz —
  but **exactly 869462500 Hz**, which fits comfortably. ⇒ store **`uint32_t freq_hz`**; the adapter performs **one**
  `freq_hz / 1'000'000.0` conversion. ⛔ No `double` in a new record; `/mrcfg` keeps its own `double freq_mhz`
  unchanged.
- ⛔ **No `sf_list`** (§1).
- **Command grammar** (one shared console/BLE handler; the OLED only *selects*):
  `joinprofile list` · `joinprofile set <1..4> layer= freq=<MHz> bw=<kHz> sf= [name="…"]` · `joinprofile clear <1..4>` ·
  ★ **`joinprofile reset confirm`**.
  ⛔⛔ **THE RESET VERB IS REQUIRED, NOT OPTIONAL (v3 named a "clear/reset" recovery that the grammar never defined):
  a CORRUPT record cannot be repaired slot-by-slot, because the other three slots cannot be loaded to rewrite them.**
  ⇒ `reset confirm` **replaces `/mrjoin` wholesale with a valid EMPTY four-slot record**; `clear <slot>` operates
  **only on a valid record**; and ⛔ **a missing `confirm` refuses WITHOUT WRITING.**
  ★ **The grammar states `freq=<MHz>` and `bw=<kHz>` explicitly, matching the existing `join` verb** — the operator
  types the same units everywhere; only storage is integral.
  ⛔ **A malformed or out-of-range index refuses loudly and writes nothing** (C2).
- **Validation ranges shared with the typed join transaction** (§2.2) — ⛔ one authority, never two spellings.
- ★★ **ABSENT vs CORRUPT ARE DIFFERENT OUTCOMES, and this is the honesty point:**
  - **absent** ⇒ ordinary **`NO PROFILES`**;
  - **invalid / truncated / version-mismatched** ⇒ **visible `PROFILE STORE INVALID`**, ⛔ **no automatic write**, and
    ⛔ **no effect on `/mrcfg`**.
  ⇒ **silent fallback would make corruption indistinguishable from a fresh device.**
- **Byte-identical write coalescing**, as `/mrcfg` already does (`src/device_nv.h:484-501`).
- ⛔⛔ **DO NOT ADD `/mrjoin` TO THE nRF52 CORRUPTION-PROBE LIST — v2 REQUIRED THIS AND IT CONTRADICTED ITS OWN
  ISOLATION RULE.** `mount_or_repair()` recovers by calling **`InternalFS.format()`**, and its own comment states
  *"a reformat wipes `/mrid` too → the node re-mints its identity + loses its join → must be re-provisioned"*
  (`src/device_nv.h:400-406`). ⇒ **listing an OPTIONAL profile store there would make its corruption DESTROY identity
  and config** — precisely what §3.6.3 forbids. ⓘ QG overturned its own earlier recommendation after reading the
  implementation; **this plan records the reversal rather than the first answer.**
  ⇒ **Handle a failed / short / invalid `/mrjoin` read LOCALLY as `PROFILE STORE INVALID`**; allow an explicit
  **`joinprofile reset confirm`** (⛔ **not** `clear` — see the grammar) to remove and recreate **only that file**; and
  if that write fails, **report a storage failure — ⛔ never format the filesystem.**
- ★★ **FRESH-DEVICE (ABSENT) BEHAVIOUR — SPECIFIED, because v4 defined absent-vs-corrupt but never said what the verbs
  DO on an absent record:**
  | verb | on ABSENT | on CORRUPT |
  |---|---|---|
  | `set <1..4> …` | ★ **seeds a valid empty four-slot record, applies the slot, and performs ONE write** | ⛔ refuses (`PROFILE STORE INVALID`) |
  | `list` | **`NO PROFILES`** — an ordinary, non-alarming state | **`PROFILE STORE INVALID`** |
  | `clear <1..4>` | operates on the seeded/valid record | ⛔⛔ **MUST NOT recover corruption** |
  | `reset confirm` | valid empty record | ★ **the ONLY recovery path** |
  ⇒ ⛔ **`clear` must never be a backdoor repair**: it would rewrite three slots it could not read.
- ★ **Factory reset DELETES `/mrjoin`** — it is **user configuration**, not fault history, so the `/mrfault`
  preservation precedent does **not** apply.
- **Mutation controls:** wrong magic · wrong version · wrong size · slot index off-by-one · **units swapped
  (kHz↔MHz, Hz↔kHz)**.

---
## §4 — The unsaved-draft precondition (⛔ v1 CONFLATED TWO STATES)
§3.6.3 requires SAVE-or-DISCARD first. **But two distinct states exist and the remedy differs:**
- **`conflict()`** ⇒ the note must say **`RELOAD OR DISCARD`**. ⛔ **Suggesting SAVE here points the operator at an
  operation that should refuse.**
- otherwise **`config_unsaved()`** ⇒ **`SAVE OR DISCARD`**.
⛔ **PROVISION must never silently save on the operator's behalf** (C2).

---
## §5 — State model (QG-recommended shape, adopted)
As-built: `Screen{status,team,inbox,send,settings,count}` (`:139`), `Settings{closed,browsing,editing}` (`:155`).
- **Add `Settings::provisioning`** *plus a **separate explicit `Provision` enum***, QG-recommended arms adopted:
  **`closed · menu · create_confirm · create_result · join_select · join_confirm · join_waiting · join_result`**. ⛔ **Never a `bool in_provision`** — `:155` carries its own warning about the
  binary-test-over-a-ternary-domain defect this arc has hit five times.
- ★ **Pin the invariant: provisioning is CLOSED whenever SETTINGS is left** — the same rule `editing` already obeys
  (`sync_settings`).
- ⛔ **No sixth cycle slot**; PROVISION stays inside SETTINGS.
- Confirmations open with **BACK selected**.

---
## §6 — Availability (⛔ v1 got the predicate wrong)
Govern by **the ACTUAL CHILD PREDICATE — principally `MR_N_LAYERS < 2`**, which is what gates `join`/`create`/`team`.
⛔ **Do NOT hide static join merely because `MR_FEAT_TEAM` is off** — static join has nothing to do with the team plane.
- Per **[[B209]]**: **hide** a child with no support (conditional like `CfgRow::reload`), ⛔ not a refusing stub.
- `MR_FEAT_OLED` gates the screens; the child predicates gate the children. ⛔ Do not conflate.

## §7 — Fingerprint (QG-recommended, adopted)
★ **EXACTLY: uppercase, zero-padded hex of `team_id & 0x00FFFFFF`** — e.g. `0x12A1B2C3` → **`A1B2C3`**.
⛔ v3's *"six digits derived from the id"* was under-specified and still admitted incompatible high-bits / low-bits /
hash implementations; two independent implementations would then disagree, which is the one thing a shared token
must not do. One pure helper, shared with
§3.6.4 so the two can never disagree.
- ★ **The retained full id remains the selection authority; the fingerprint is DISPLAY-ONLY** and must never make a
  routing or airtime decision — the standing rule, and the mirror of [[B210]]/[[B214]].
- ⛔ **ZERO wire bytes.** Nearby beacons already carry the full `team_id` (type-5 TLV), so the token is derived locally.

## §8 — Fault controls (§3.6.5)
1. **Emergency pre-empts** any provisioning screen — including §2.3's **waiting** state; an unconfirmed destructive
   action does not survive.
2. **No screen claims success before the save returns** — ★ already structural for team; §2.3 extends it to *"and no
   screen claims JOINED before a correlated adopt"*.
3. **Reset/power-cut leaves the complete old or complete new record** — ★ this is **[[B193]]**, now over **two** records.
4. **Never triggered by the waking press.**

---
## §9 — Slices (QG-revised order, C1-safe, each independently gated)
| # | slice | note |
|---|---|---|
| **1** | **Typed static-join transaction; route the console through it** | ★ **console/JSON output BYTE-IDENTICAL** — a pure refactor |
| **2** | **`/mrjoin` record + validation + console handler** | storage + console only, no UI |
| **3** | **Pure fingerprint helper** | one function, shared with §3.6.4 later |
| **4** | **Pure provisioning state model + unsaved/conflict gate + platform hiding** | model + gating, no screens |
| **5** | **Team-create adapter and screens** | `phy.present=false` (§2.1) |
| **6** | **Static-join adapter, ASYNC outcome handling and screens** | §2.3's correlation rule lands here, mutation-tested |
| **7** | **Metal qualification, including `/mrjoin` power-cut** | see §10 |
⛔ No slice mixes a move with a behaviour change. ⛔ Slice 2 must not touch `/mrcfg`'s layout or `kVersion`.

## §10 — Gates
- ⛔⛔ **CORRECTED v3: [[B193]] CANNOT be a PRE-implementation gate for `/mrjoin` — v2's ordering was circular**, since
  B193's `/mrjoin` power-cut test **requires `/mrjoin` to exist**. ⇒ **two distinct gates:**
  - **before enabling destructive OLED provisioning:** the existing **`/mrcfg` Part 20.5** must discharge;
  - **after slice 2 and before UI-15 closure:** a **`/mrjoin`-specific power-cut run** (slice 7).
- ★ **The current metal backlog is a gate too: ALL APPLICABLE UNCHECKED PREREQUISITES IN THE LIVE BENCH SCRIPT.**
  ⛔⛔ **This plan names NO Parts and NO tally.** v3 froze *"zero metal validation"* (already stale when written) and
  v4 still listed specific Parts immediately before deferring to the live checklist — **a frozen list is the same
  defect as a frozen count, one indirection later.** ⇒ **`docs/2026-07-31-bench-test-script.md` is the authority
  (M2); this plan states the DEPENDENCY only.**
- Per slice: native + four probes + census at pins + four-step corpus + **two board envs** + D2.

## §11 — Open questions
ⓘ **v1's Q4 is REMOVED — already settled:** the owner approved **four configured static-join profiles for v1**.
ⓘ **v2's five questions are now all SETTLED by QG recommendations this plan adopts** — sub-states (§5), the 60 s
`STILL JOINING` timeout, BACK-leaves-only, `factory_erase` **deletes** `/mrjoin`, and **no** `join_refused` reason
failing v1. ⇒ **the only items still needing the OWNER are:**
1. ✅ **SETTLED 2026-08-19 by owner ruling** — OLED create preserves the persisted PHY and refuses with
   `PHY DIFFERS — USE SERIAL` (§2.1). ⓘ No longer open.
2. ✅ **SETTLED 2026-08-19 (owner disposition): AUDIT `reset_join_for_reprovision()` AND REGISTER ANY DEFECT DURING
   SLICE 1 — but ⛔ DEFER THE FIX TO A SEPARATE SLICE**, which is what preserves slice 1's byte-identity requirement.
   ⇒ **no open questions remain; the plan is ready to slice.**
ⓘ Also adopted without needing a ruling: fingerprint = **six uppercase hex** (§7), profile names **optional, ≤12 chars,
empty renders `PROFILE 1…4`**, corruption **operator-visible and locally handled** (§3).
