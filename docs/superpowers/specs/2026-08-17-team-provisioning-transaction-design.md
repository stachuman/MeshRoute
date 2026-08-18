<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §PROV-TX — one typed team-provisioning transaction · DESIGN SPEC **v4** · 2026-08-17

**Status: ✅ v4 design APPROVED · IMPLEMENTED · ALL QG BLOCKERS CLOSED (3 rounds) · QG PASSED 2026-08-17. ⛔ UNCOMMITTED
(D4) and NOT yet metal-qualified — bench Part 27.** Registered as **[[B207]]**.
⛔ **Header corrected twice: it read *"No code is written (P2)"* (stale from dispatch) and then *"QG HELD … corrections in
flight"* (stale after round 3). Both are withdrawn.**
★ Role split: the QA-gate wrote this spec; **the OWNER runs QG and rules.**

⚠ **THREE QG HOLDS answered.** v2 fixed v1's key **ordering** and same-team handling · v3 fixed v2's key
**primitives** (v2 named a **fallible** call as the post-save install) · **v4 withdraws v3's self-contradictory
`team 0` PHY rule and a PROVENANCE ERROR** — v3 reported a QG recommendation as endorsement when that recommendation
had been made **without knowledge of the owner's PHY ruling**. Corrections are marked **`CORRECTED v2/v3/v4`**;
withdrawn wording is kept visible (§3 rule 3). ⛔ **Do not implement anything marked withdrawn.**

★★ **OWNER RULING 2026-08-17 (reported form, ⛔ not quoted).** Register the team-provisioning atomicity defect and fix
it as a **separate prerequisite slice before UI-15**. ⛔ **Do not merely reorder the final save.** Introduce **one typed
provisioning transaction shared by the console and the future OLED path**: validate and stage without mutation, persist
a complete candidate with **`team_local_id = 0` meaning DAD pending**, then apply live and start DAD. **A save failure
must leave live PHY, role, team, keys and NV unchanged and spend no airtime.** Route all `team new`, `team <id>` and
`team 0` forms through it. **Keep UI-15's screens and `/mrjoin` out.**

⚠ **SCOPE OF THE "NV UNCHANGED" CLAUSE — CORRECTED, because as written it conflicted with this spec's own §5.1.** The
ruling's intent is preserved and is achievable: **the transaction attempts EXACTLY ONE write and applies nothing on
failure**, so nothing further is written and no live domain moves. ⛔ **What it must NOT be read as: a guarantee that a
FAILED physical write left the stored record byte-intact.** A backend can fail *after* a partial write — that is
precisely [[B193]]'s open question — and no host test can establish otherwise (§5.1). ⇒ **state the guarantee as "one
write attempted, nothing applied, no airtime spent", never as "no flash was changed".**

ⓘ Recorded for UI-15, **not implemented here**: **PROVISION is HIDDEN where there is no supported provisioning child**,
and **four configured static-join profiles suffice for v1** (resolves the design's §14 Q6).

---
## 1 — The defect, verified at the code (V1)

`handle_team` (`src/firmware_config.cpp:1077-1254`) performs **six live mutations before** `mrnv::save` at `:1249`:

| # | line | mutation | domain |
|---|---|---|---|
| ① | `:1159` | `mobile_register_phy(phy)` — ⛔ **THREE actions, see §1.3** | **PHY + home-service authorisation** |
| ② | `:1190` | `team_channel_key_adopt(tk_pub, tk_priv)` | **keys** |
| ③ | `:1196` | `team_channel_key_mint()` — consumes CSPRNG entropy | **keys** |
| ④ | `:1225` | `set_team_id(t)` — drops the old team's plane/key-cache/DAD id; role via `role_enforce` | **team + role** |
| ⑤ | `:1229` | `team_channel_key_adopt_priv(tk_priv)` — re-installs what ④ destroyed | **keys** |
| ⑥ | `:1240` | `team_dad_fire()` | ★★ **AIRTIME** |

`:1250` prints *"team is LIVE but NOT persisted — will revert on reboot"*.
⚠ **A live console-path defect today:** a `team new` whose flash write fails **presents as success** and loses
membership at the next power cycle.

### 1.1 — Why "move the save up" cannot work
**The candidate is DERIVED from the mutations:** `:1241` reads `canonical_node_id()` (⑥ may have moved it), `:1242`
reads `team_local_id()` (⑥ assigned it), `:1243` reads `c.is_mobile` (④ set it), `:1244` reads the key ②/③/⑤
installed. ⇒ **restructure, not reorder.**

### 1.3 — ⛔⛔ CORRECTED 2026-08-18: THIS SPEC'S OWN DESCRIPTION OF ① WAS WRONG, AND [[B209]] IS THE CONSEQUENCE
Row ① read *"retunes the radio **and kicks the FSM**"* — a **conflation inherited from the old call site's comment**,
carried into this spec unchecked and therefore into the implementation. ★ **The real contract is THREE actions**
(`lib/core/node.h:647-649`): **`adopt_mobile_phy` (retune) + `mobile_request_home_service()` + an immediate
DISCOVER.** The middle one **authorises static-home attachment**, which is a *different plane and a different intent*
from a team PHY retune.
⇒ **The slice therefore reproduced a hidden authorisation:** on a node with `mobile_autoregister=false`,
`team new … freq=…` yields `home_desired:true` / `attachment:"seeking"` and repeated outbound J DISCOVERs.
**Registered as [[B209]]** (metal-confirmed) — ⛔ **fixed THERE, not here**, and this spec's §3.1/§3.2 seam is already
correct in separating `apply_phy` (PHY) from `fire_dad` (airtime); only this row's prose was wrong.
ⓘ **A caution I raised was REFUTED and is withdrawn:** I suggested the FSM kick might be load-bearing for team-DAD on
the no-host path. It is not — `test/test_node_join.cpp` already pins an **auto-OFF team member performing team-DAD
with ZERO DISCOVERs**, so a retune-only seam built on `adopt_mobile_phy()` is safe.

### 1.2 — ★ Four further defects this slice must also correct (all verified)
1. **The PHY parse is gated on the OLD role.** `:1151` `if (phy_args && *phy_args && c.is_mobile)` ⇒ on a **static**
   node, `team new freq=869 sf=7 bw=125` **silently discards the PHY arguments** and the incomplete-PHY check then runs
   against live values. Silently ignoring supplied arguments violates C2.
2. **The incomplete-PHY check reads LIVE state after ① retuned it** (`:1168-1175`), and its comment claims *"team_id,
   _team_local_id, NV all unchanged"* — true, but **the radio is already retuned** (acknowledged at `:1187`).
3. ★★ **`team_fnv1a32` has NO zero guard.** `src/firmware_config_parse.h:419-425` returns the raw FNV-1a hash, and
   `:1081` assigns it straight to `t`. **If it returns 0, `team new` mints a key and then executes `team 0` = LEAVE** —
   every `t != 0` guard is skipped and the verb does the opposite of what was asked. Astronomically unlikely
   (~1 in 2³²), **structurally reachable, and free to exclude.**
4. **Success is announced before the save.** `> team channel key: ADOPTED` (`:1194`), `MINTED` (`:1200`) and
   `> team PHY: …` (`:1161`) all print **before** `:1249`. The OLED design's §3.6.5 forbids exactly this ("no screen
   may claim success before the save returns"); the console has the same obligation.

---
## 2 — The mechanism the ruling depends on ALREADY EXISTS (U1)

| existing mechanism | where | what it gives |
|---|---|---|
| `0` **is** the documented sentinel | `lib/core/node.h:353` — *"0 = not team-DAD'd"* | a persisted 0 is meaningful |
| persisted non-zero ⇒ CONFIRMED, no re-DAD | `lib/core/node.h:354` | happy path unchanged |
| a mobile with `team_id != 0 && team_local_id == 0 && !_team_dad_pending` **auto-fires DAD** | `lib/core/node_mobile.cpp:40`, `:214` | **persisting 0 self-heals** |
| the DAD result is **already** written back | `src/fw_main.cpp:1026` (`team_changed` `:1030`, saved `:1041-1042`) | no new persistence machinery |
| a moved `node_id` covered the same way | `src/fw_main.cpp:1029` `join_changed` | `:1241`'s post-DAD read becomes unnecessary |

### 2.1 — The correct discipline already exists one layer over
`src/firmware_config_service.h` (natively tested): `ICfgStore::save` — *"false = THE WRITE FAILED (nothing may be
applied live)"* (`:228`); `ICfgLive::apply_live` — *"★ called ONLY after the durable write returned success"* (`:235`).
⚠ **`src/firmware_config.cpp` is OUTSIDE the native suite** (`test/test_firmware_config_parse.cpp:277`), so logic left
in `handle_team` is unreachable by every gate.

---
## 3 — Design (v2)

### 3.1 — Seams
A header-only **`ProvisioningService`** beside `ConfigService` (sibling — `firmware_config_service.h:54` disclaims
provisioning), **reusing `ICfgStore` unchanged** (U1). Plus:

- ★★ **`IEntropy`** — a seam for CSPRNG bytes. **CORRECTED v2:** key material must be minted **at stage time, before
  the save**, so the generator cannot be `Node`'s post-save call. An injectable seam also makes minting **natively
  testable and deterministic**.
- **`IProvLive`** — ⛔ **CORRECTED v2: `key_mint` and `key_adopt` are REMOVED from it.** v1 listed them, which
  contradicted the transaction: the key must be **inside the candidate before persistence**, and v1's phase order
  ("PHY → keys → set_team") was **wrong** because `set_team_id` **clears the key**. `IProvLive` now carries only:
  `set_team` · `install_key` (⛔ **`void`** — see §3.6) · `apply_phy` · `fire_dad`.
  ★ `fire_dad` is the **airtime** operation and the one a fake counts.
  ⛔ **CORRECTED v3: `install_key` must be `void`.** v2 called it `load_staged_key` and described `adopt_priv` as
  infallible — **that was wrong** (§3.6).

### 3.2 — ★★ THE REQUIRED SEQUENCE (owner/QG-specified; this replaces v1's four phases)

1. **Parse** console input into a **typed request** (no globals, no `Print&`).
2. **Validate and PROJECT** role and PHY **without live mutation** (§3.3, §3.4).
3. **Stage** a **non-zero** team id (§3.5) and **key material** (§3.6).
4. **Compose the candidate** (§3.7).
5. **Persist** — `store.save(candidate)`. On false: **return a failure verdict; apply NOTHING; no `fire_dad`, so no
   airtime.**
6. **On success ONLY, in this order:**
   a. **switch team** — *only if membership changed*;
   b. **apply the plan's `KeyAction`** (§3.6.1) — `install` calls the **`void`** `team_channel_key_load`;
      `preserve` makes **no key call at all**; `clear` happens via `set_team`;
   c. **apply the staged PHY**;
   d. ★ **start DAD LAST**, and **only for a changed, non-zero team**.
7. **Notify the UI and format the success result** (§3.8).

★★★ **EVERY POST-SAVE OPERATION MUST BE INFALLIBLE GIVEN A VALIDATED PLAN.** ⛔ **No fallible key derivation or
validation may occur after persistence** — that is the property that makes step 5's guarantee real rather than
nominal.

### 3.3 — Projected role (unchanged from v1, and confirmed good)
⛔ Do **not** hand-derive the role. `set_team_id`'s role change is a general `role_enforce(_cfg)` pass whose `RoleFix`
is discarded (`lib/core/node.cpp:701`). ★ `role_enforce(NodeConfig&)` is a free `static inline`
(`lib/core/node_role.h:79`) and `NodeConfig` is a plain struct (`node_carriers.h:91`) ⇒ **copy the config, set
`team_id` on the copy, run the real `role_enforce`, read the result** (and the `RoleFix`, still owed as a report per
B28 constraint 3). Zero mutation, zero duplication, natively reachable. ⓘ `role_enforce` never touches `is_gateway`
(`node_role.h:77`) and `team 0` is one-directional (`node.cpp:692`, R3).

### 3.4 — ★ PHY validation against the CANDIDATE, using the PROJECTED role
**CORRECTED v2 — v1 said only "move the check before the retune". That is insufficient.**
- The incomplete-PHY check must evaluate the **staged effective PHY** (candidate + defaults), ⛔ never live state.
- The PHY tail must be parsed under the **projected** role, fixing §1.2.1: a static node promoted by
  `team new … freq=…` must have its PHY arguments **honoured**, not silently dropped.
- ✅ **RULED 2026-08-17 (owner, reported form): ON LEAVE THE PHY IS PRESERVED.** ⇒ **two rules, and v4 makes them
  CONSISTENT — v3's pair was self-contradictory:**
  1. ★ **`team 0` NEVER resets, clears or re-derives the PHY.** Leaving changes membership only; frequency, routing SF,
     `sf_list` and bandwidth carry through untouched. ⓘ This matches today's no-tail path (`:1158`), but as an
     **invariant the transaction upholds**, not an accident of an early `if`.
  2. ★★ **`team 0` WITH ANY PHY ARGUMENT IS REFUSED LOUDLY, BEFORE THE SAVE.** ⛔ Not honoured, ⛔ not ignored, ⛔ not
     partially parsed. **Refusal ⇒ zero save, zero live application, zero airtime.** ⓘ This is the same shape as the
     existing `team 0 tkpub=` refusal (`src/firmware_config.cpp:1139-1142`) — *"tkpub=/tkpriv= make no sense on `team 0`
     (leave)"* — so the verb already has the idiom and the precedent.
  ⛔⛔ **WITHDRAWN v4 — v3's rule 2 SAID THE OPPOSITE AND WAS SELF-CONTRADICTORY**, asserting both that `team 0` never
  changes the PHY *and* that its PHY tail is honoured. **Both cannot hold.** ★ **And the claimed endorsement was
  invalid:** v3 cited a QG recommendation as agreement, but that recommendation was made **without knowledge of the
  owner's PHY ruling** — the owner said so when relaying it. ⇒ **an inference stacked on a recommendation given in
  ignorance of the ruling, then reported as endorsed. Provenance error, withdrawn in full** (ledger §3: never claim an
  approval that was not given).

<details><summary>⛔ WITHDRAWN v3 wording, kept visible (§3 rule 3)</summary>

- ✅ **RULED 2026-08-17 (owner, reported form): ON LEAVE THE PHY IS PRESERVED.** ⇒ **two explicit rules**, so the
  behaviour stops being incidental:
  1. ★ **`team 0` NEVER resets, clears or re-derives the PHY.** Leaving a team changes membership only; frequency,
     routing SF, `sf_list` and bandwidth are carried through untouched. ⓘ This matches today's no-tail path
     (`:1158` — *"`none` (empty tail, e.g. `team 0`) = keep the current PHY"*), but as an **invariant the transaction
     must uphold**, not an accident of an early `if`.
  2. ★ **A PHY tail on `team 0` IS HONOURED** — staged, persisted in the candidate, applied after the save. ⛔ It must
     **not** be silently dropped: that is §1.2.1's C2 defect in a second location, and leaving a team while retuning
     to a static network's PHY is a coherent, useful operation. ⓘ If it is ever to be rejected instead, it must be
     rejected **loudly**, like `team 0 tkpub=` (`:1139-1142`) — ⛔ never ignored.
  ✅ **BOTH RULES ARE NORMATIVE (v3).** Rule 1 is the owner's ruling. Rule 2 was this spec's reading of it applied to
  the argument case, and QG **independently recommended the same** on the same three grounds (it preserves current
  console behaviour, it is coherent when leaving a team for a static network, and an explicit argument must be honoured
  or loudly refused — never ignored). ⇒ **the earlier open question is CLOSED and rule 2 is binding.**

</details>

  ⓘ Consistency check: a node holding a non-zero `team_id` has necessarily been promoted to mobile (`role_enforce`),
  and leave is one-directional (R3, `node.cpp:692`), so `c.is_mobile` is true for every meaningful `team 0` — but the
  transaction should still gate on the **projected** role uniformly rather than relying on that coincidence.

### 3.5 — Non-zero team id enforcement
**CORRECTED v4 — non-zero is NOT SUFFICIENT.** A generated id must satisfy **`t != 0 && t != current_team_id`**.
⚠ Checking only non-zero leaves a rare case where `team new` regenerates the **current** id and therefore becomes a
**same-team re-key** (§3.6.1) instead of creating a new team — the verb would silently do something other than what was
asked, which is §1.2.3's defect in a second guise. ⇒ the id generator belongs to the shared pure builder (§3.9), with a
**bounded resample excluding BOTH values** (essentially free — the same loop) and a **loud** refusal if it cannot
produce an acceptable id.

### 3.6 — ★★ Key staging and the post-save install
**CORRECTED v3 after a second QG HOLD. ⛔ v2 NAMED THE WRONG POST-SAVE PRIMITIVE and its wording is withdrawn below.**

★★ **`team_channel_key_adopt_priv` IS FALLIBLE and must NOT be used after persistence.** `lib/core/node.h:229`
declares it **`bool`**, and its body calls `team_channel_key_derive` and returns `false` on an all-zero/degenerate
scalar. ⓘ **How v2 got this wrong, recorded because it is this project's recurring failure mode:** it relied on the
comment at `src/firmware_config.cpp:1217` — *"adopt_priv cannot fail **here**"* — which is a **scoped** claim about an
already-canonical stored key, and generalised it into an API guarantee. **V1: verify against the declaration, not the
comment.**

⚠ **And `split_team_key_tail` validates SYNTAX ONLY** — it parses hex and never proves the public half matches the
private one. The cross-check lives in `team_channel_key_adopt` (`node.h:228`: *"false = REFUSED (all-zero, or pub
doesn't match priv)"*), which this design may no longer call post-save. ⇒ **the transaction must do the cross-check
itself, at stage time.**

**Required flow:**
1. **Parse with the PURE parser.** `mrfw::split_team_key_tail` (`src/firmware_config_parse.h:158` — no `Print`, no
   Arduino) ⛔ **not** `parse_team_key_tail` (`src/firmware_config.cpp:888`), which is the console-reporting wrapper.
   **CORRECTED v3:** v2 referenced the wrapper while §3.2 step 1 forbids `Print&` — a contradiction. **The console
   handler translates the typed `TeamKeyTail` error into text; the service never formats.**
2. **Derive and CROSS-CHECK, before the save.** Call the pure `team_channel_key_derive(derived_pub, canon_priv,
   supplied_priv)` (`lib/core/identity.h:84`) and, when a `tkpub=` was supplied, **compare `derived_pub` against it**.
   A mismatch or a degenerate scalar refuses **here**, where refusing is free.
   ⓘ Corroboration that derive-and-compare is the house pattern rather than an invention: `lib/core/frame_codec.h:735`
   records that the T-K3 grant deliberately omits `tkpub` from the wire so the receiver re-derives it, making a
   pub/priv mismatch *"structurally impossible instead of something a cross-check must catch"*.
3. **For `team new` with no tail:** draw 32 B from **`IEntropy`** and derive the canonical pair. ⛔ Not
   `team_channel_key_mint()` (`node.h:227`), which draws **and installs**.
4. **Store the CANONICAL pair in the candidate** — the derived public half and the canonicalised private half, so what
   is persisted is exactly what will be installed.
5. **After a successful save, install with the INFALLIBLE primitive:** **`team_channel_key_load(pub, priv, true)`** —
   `lib/core/node.h:230`, returns **`void`**, documented as *"boot restore from NV — VERBATIM, no re-derivation
   (mirrors admin_load)"*. That is precisely the semantic step 6 needs.
6. ⛔⛔ **NEVER call `team_channel_key_adopt`, `team_channel_key_adopt_priv` or `team_channel_key_mint` after
   persistence.** All three are fallible; all three belong to staging or to nothing.

⇒ v1's stash-and-re-apply dance (② installed, ④ destroyed, ⑤ restored) **disappears**: nothing is installed before the
switch, so nothing needs rescuing. ★ The ruling that **a creator always ends up holding a keypair** still holds — now
guaranteed by the candidate plus a `void` install, rather than by ordering luck.

### 3.6.1 — ★★ `KeyAction` (new in v3): the key application must be an EXPLICIT decision
⛔ **v2's step 6b said "load the staged key", unconditionally — which would change or clear the key during a same-team
PHY-only update.** The plan therefore carries an explicit action, and **the candidate and the live apply use the SAME
one**:

```
KeyAction = preserve | install | clear
```

| request | KeyAction | live effect |
|---|---|---|
| same-team, **PHY only** | **preserve** | ⛔ **no key call at all** |
| same-team, **re-key** (`tkpub=`/`tkpriv=`) | **install** | `team_channel_key_load(staged)` |
| **new** team, key generated or supplied | **install** | `team_channel_key_load(staged)` |
| **bare join** to another team (`team <id>`) | **clear** | via `set_team` (a joiner receives one — `:1182-1184`) |
| **leave** (`team 0`) | **clear** | via `set_team` |
| same-team, **no changes** | **no_change** | ★ **zero save, zero apply** (§3.7) |

<details><summary>⛔ WITHDRAWN v2 wording, kept visible (§3 rule 3)</summary>

#### (v2) Key staging and the post-save load — WITHDRAWN
**CORRECTED v2 — this is the finding that made v1 incorrect.**
- **Stage** key material with **pure crypto + `IEntropy`**: mint (for `team new` with no tail) or validate-and-adopt
  (`tkpub=`/`tkpriv=`, using the existing `parse_team_key_tail`, which already validates without mutating).
  ⛔ **All fallible key work happens HERE, before the save.**
- **Put the staged material into the candidate**, so persistence carries the key.
- **After a successful save:** **`set_team` FIRST** (it clears the key by design — §o3-key-lifetime), **then load the
  staged key infallibly.** ⓘ `adopt_priv` is the right primitive: `firmware_config.cpp:1217` records that it *"cannot
  fail here (a stored key is non-zero and canonical by construction)"* — that property is exactly what step 6b needs,
  and the spec relies on it rather than on a re-validation.
- ⇒ v1's stash-and-re-apply dance (② then ④ destroys then ⑤ restores) **disappears**: nothing is installed before the
  switch, so nothing needs rescuing. ★ The ruling that **a creator always ends up holding a keypair** still holds —
  it is now guaranteed by the candidate rather than by ordering luck.


</details>

### 3.7 — ★★ Candidate composition: membership change is the discriminator
**CORRECTED v2 — v1 unconditionally persisted `team_local_id = 0`, which was WRONG.** A same-team re-key or PHY
update would have forced a needless team-DAD after reboot and **discarded a stable local id.**

**Verified: a same-team request is NOT necessarily a no-op.**
- `team <current> tkpub=… tkpriv=…` **replaces the key** — `set_team_id` returns early at `lib/core/node.cpp:667` for
  the same team, so it does **not** clear, and the adopt stands.
- `team <current> freq=…` **changes the PHY** — `:1159` runs regardless of the team comparison.
- **Only** same-team **with no key change and no PHY change** is a true no-op.

⇒ compose on **`membership_changed` (`c.team_id != t`)**, not on "same id":

| case | `team_local_id` | `node_id` | `KeyAction` | PHY |
|---|---|---|---|---|
| membership **changed**, non-zero, keyed | **0** (DAD pending) | loaded value (converges via `join_changed`) | **install** | staged tail, else preserved |
| membership **unchanged**, **re-key** | ★ **PRESERVE current** | ★ **PRESERVE current** | **install** | staged tail, else preserved |
| membership **unchanged**, **PHY only** | ★ **PRESERVE current** | ★ **PRESERVE current** | ★ **preserve** (⛔ no key call) | staged tail |
| membership **unchanged**, **nothing changed** | preserved | preserved | ★ **no_change** | preserved |
| **leave** (`t == 0`) | 0 | preserved | **clear** (via `set_team`) | ★ **PRESERVED** — never reset; ⛔ **any PHY arg is REFUSED before the save** (§3.4) |
| new membership, **keyless** (`team <id>` bare) | 0 | loaded | **clear** (a joiner receives one — `:1182-1184`) | staged tail, else preserved |

★★ **THE `no_change` ROW IS A HARD REQUIREMENT, NOT AN OPTIMISATION (v3): a same-team request that changes nothing must
perform ZERO saves and ZERO live applies**, and report `no_change`. ⓘ Today `mrnv::save` coalesces byte-identical
`/mrcfg` writes (`src/device_nv.h:484-501`), so the flash cost is already avoided — but the **transaction must decide
this explicitly** rather than inherit it from a lower layer, because the OLED will render the outcome and
`mr_ui_on_config_saved()` must **not** fire for a save that never happened.

⛔ And **step 6d fires DAD only when membership changed to a non-zero team** — a same-team re-key must not DAD.

### 3.8 — Typed result; output only after commit
A **typed outcome** (not `Print&`) so the console and the OLED render the same verdict. ⛔ `ADOPTED` / `MINTED` /
`> team PHY:` / `> team ->` and **`mr_ui_on_config_saved()`** may be emitted **only after step 6 completes**
(§1.2.4). ⓘ This is what lets the OLED consume the transaction without manufacturing command strings (design §3.6.1).

### 3.9 — One shared pure builder
The service **owns or shares a single pure builder** for: **`team new` id generation · non-zero enforcement · key
generation (derive + cross-check, §3.6) · candidate construction (including the `KeyAction`, §3.6.1).** ⛔ Without it the console and the future OLED duplicate creation logic — the
S1/L9 field-drop rot U1/U2 exist to prevent.

### 3.10 — ★ Stack and secret lifetime (new in v2)
⛔⛔ **CORRECTED 2026-08-17 (QG) — THIS SECTION'S PREMISE WAS MEASURED AND REFUTED, INCLUDING BY ITS OWN AUTHOR.**
It read: *"A `NodeConfig` copy is 256 B (measured), plus an `mrnv::Blob`, the `TeamPlan` and a 64-byte keypair — on a
console path with recorded stack-overflow history (the loop-task overflow arc; `stackhw` fell to 72 B)."*
★★ **WHAT MEASUREMENT ACTUALLY SHOWS: the 256-B copy is NOT the cost and never was.** ⓘ **The three figures below are
PRE-ROUND-3** — they are the control experiment that refuted the copy, not the shipped result; **for the CURRENT numbers
see the box at the end of this section.** ARM `handle_team` measured
**856 B with the projection first, 856 B with the plan+Blob first, and 856 B with the `NodeConfig` copy DELETED
OUTRIGHT** — at `-Ofast` the copy is **scalarised away**, because `role_enforce` touches exactly three fields
(`lib/core/node_role.h:79-85`: reads `team_id`, writes `is_mobile`, and `team_id = 0` on one arm). ⇒ **the growth comes
from the TYPED CARRIERS** — `TeamRequest` 136 + `TeamPlan` 128 + `ProvResult` 64 against the old frame's 64 B of
`tk_pub`/`tk_priv`. Pre-slice ARM `handle_team` was **536 B**; the split bought **+8 B** (`sizeof(TeamProjection)`), not
a reduction. ⓘ xtensa: pre-slice **704** (496 + 208); the deepest chain **was 976 AT THAT POINT** (`handle_team` 304 →
`apply_team` 592 → `apply_phy` 80) — ⚠ an earlier figure of 928 took a shorter leg and understated the peak by 48 B.
⛔ **All figures in this paragraph are PRE-ROUND-3.**
⛔ **AND THE "72 B" HISTORY IS WITHDRAWN AS AN EXPLANATION OF PRESENT RISK:** that reading predates the dedicated
8 KB mesh-task fix (`src/fw_main.cpp` §stability) which moved this work off the 4 KB Arduino loop stack. Citing it as
current headroom is unsupported. ⇒ ★ **the residual stack risk is a METAL QUALIFICATION (bench 27.1), not a code
blocker** — but Part 27 must be run on the resulting firmware immediately. **Reducing the frame means changing the
seam's carriers (dropping `TeamPlan::phy` ~40 B, letting the plan reference the request's key buffers ~64 B), which is
a design change and is NOT authorised here.**
★★★ **CURRENT FIGURES (post-round-3, the shipped implementation — these are the ones to quote): ARM `handle_team`
888 B · xtensa deepest in-TU chain 928 B.** ⓘ Round 3's live-divergence fix extended `ProvSnapshot`, adding **+32** on
ARM (856 → 888) while xtensa's peak **fell 48** (976 → 928, `stage_team_candidate` stopped being inlined). Pre-slice
baselines for comparison: ARM **536**, xtensa **704**. ⛔ **The 856 / 976 pair is superseded and must not be quoted as
the result.** ⓘ Bench **27.1** carries the same numbers.
**Required:**
- **measured** stack and RAM impact, reported per board (⛔ not asserted);
- ⛔ **no unnecessary simultaneous copies** — the config copy exists only for `role_enforce`; scope it tightly;
- ★★ **explicit wiping of staged PRIVATE key material on EVERY exit — success and failure alike.** ⓘ The existing code
  already keeps secrets off the console frame deliberately (`:1213-1214`, *"the console frame grows by one bool, not by
  32 B of secret"*); the staged buffer must not undo that. ⓘ `Node::team_channel_key_adopt_priv` also `crypto_wipe`s its
  own scratch — **match that discipline.**
- ⛔⛔ **CLARIFIED v3 — WHAT MUST *NOT* BE WIPED.** The wipe applies **only to transient request / plan / workspace
  buffers**. ⛔ It must **NOT** touch the **persisted store copy** or the installed live key: the team key is
  **required** to survive in NV (`device_nv.h:116-118`) and in the node, or the node cannot read team traffic. **Wiping
  the candidate after a successful save would destroy the team.**

### 3.11 — Scope
**IN:** the transaction, the seams, all three `team` forms, §1.2's four corrections, native coverage.
⛔ **OUT:** UI-15 screens · `/mrjoin` and profiles · the fingerprint helper · the unsaved-draft precondition ·
`handle_join`/`handle_create` (**already save-then-apply**, `:777-779` / `:827-829` — do not touch) · any wire,
`kVersion` or `Node` change.

---
## 4 — Open questions for QG
ⓘ **Two former questions are now CLOSED, and one of them was closed against my own reading:**
- **`team 0` + PHY args** — ✅ **refuse loudly** (§3.4 rule 2, v4). ⛔ v3's "honour the tail" is withdrawn, as is its
  claimed endorsement.
- **The blob helper** — ✅ **RULED (v4), and it is better than the overload I proposed:** introduce **ONE
  explicit-material helper** that writes `{present, pub, priv}` into `mrnv::Blob`; make the existing node-reading
  `blob_take_team_channel_key()` (`src/firmware_config.cpp:707-715`) **collect the live material and DELEGATE to it**;
  and have candidate composition call **the same explicit helper** with staged material. ⇒ **one conversion authority
  (U2) with no overload that can drift** — an overload pair is exactly how the field-drop rot starts.

**Remaining, mechanical:**
1. **Should §1.2.3's / §3.5's generated-id guard be its own micro-slice?** It is a one-line correctness fix in a different
   function and is independently testable. My recommendation: **keep it here** — it is inseparable from "stage a
   non-zero team id" — but say so explicitly rather than letting it ride.
2. **Corpus inertness.** `team` is console-only and `firmware_config.cpp:1184` notes the corpus is kept **draw-free**
   because `team <id>` mints nothing. Moving the mint to stage time should be inert ⛔ **but prove it with the
   four-step**; a changed CSPRNG draw order would move every stream.

---
## 5 — Tests

**Native, against fakes, in the `ConfigService` idiom:**
- every refusal (role, incomplete **staged** PHY, bad key tail, zero-id resample exhaustion, `team 0` + keys) leaves
  **all five domains untouched** and `fire_dad` **uncalled**;
- ★★ **save-failure: `store.save` returns false ⇒ ZERO calls on every `IProvLive` method, `fire_dad` count 0, and
  EXACTLY ONE save attempt.** ⛔ No ordering-only test detects the airtime clause — assert the count;
- ★ **key ordering: `set_team` is called BEFORE the key install**, and the key ends up present. A control that swaps
  them must go RED (the v1 defect; it must be impossible to reintroduce silently);
- ★★ **NO FALLIBLE KEY CALL AFTER THE SAVE (the v3 defect).** The `IProvLive` fake must expose **only** a `void`
  install, and a test must assert that `team_channel_key_adopt` / `adopt_priv` / `mint` are **never** reached
  post-persistence. ⓘ Cheapest durable form: a source-shape control asserting those three names do not appear in the
  post-save block — ⛔ with a control that goes RED, since a grep-shaped check is this arc's easiest vacuous instrument;
- ★ **derive-and-cross-check at stage time:** a `tkpub=` that does **not** match `tkpriv=` is refused **before** the
  save (zero saves, zero applies). ⚠ This case would PASS VACUOUSLY against a syntax-only parser, so it must be driven
  by a genuinely mismatched-but-well-formed pair (`split_team_key_tail` validates syntax only — §3.6);
- ★★ **the `KeyAction` matrix (§3.6.1), all six rows** — in particular **same-team PHY-only ⇒ `preserve` and ZERO key
  calls** (the v2 defect), and **same-team-no-change ⇒ `no_change`, ZERO saves, ZERO applies, and
  `mr_ui_on_config_saved()` NOT called**;
- **the candidate's `KeyAction` and the live apply's `KeyAction` are the SAME value** — a control that lets them diverge
  must go RED;
- ★★ **the same-team matrix** — re-key only, PHY only, both, neither — each asserting `team_local_id` and `node_id` are
  **PRESERVED** and `fire_dad` is **NOT** called; and the membership-changed case asserting `team_local_id == 0` and
  `fire_dad` called **once, last**;
- a static node with `team new … freq=…` has its PHY **honoured** (§1.2.1) under the projected role;
- ★★ **`team 0` with ANY PHY argument is REFUSED (v4)** — `freq=` alone, `sf=` alone, `bw=` alone, and combinations —
  each asserting **zero saves, zero `IProvLive` calls, `fire_dad` count 0**, and that the PHY is **unchanged**. ⚠ A
  control that accepts the tail must go RED, so v3's withdrawn behaviour cannot creep back;
- ★ **`team new` never generates the CURRENT team id** (§3.5): with the entropy seam forced to yield the current id
  first, the builder **resamples** and the outcome is a genuine new team, ⛔ **not** a same-team re-key. ⓘ This case is
  only reachable *because* entropy is injectable — it is unreachable against a live CSPRNG, which is a concrete reason
  the seam earns its place;
- staged private material is **zeroed** on both exits — ⛔ **and the persisted candidate's key is NOT zeroed** (§3.10):
  a test must assert the store received a key-bearing blob **and** that the live node holds the key afterwards, so a
  future over-eager wipe cannot silently destroy the team.

⛔ **Every check needs a control that goes RED against the current tree.** This arc has recorded **eight** instruments
that were green against the defect they were written to catch; a "no mutation" assertion whose fake counters are never
incremented on the success path would be the ninth. **Assert both directions.**

### 5.1 — ⛔⛔ WHAT THESE TESTS MAY NOT CLAIM (QG-required boundary)
They prove **"exactly one save attempt, and zero live mutation when the store reports failure."**
⛔ **They do NOT — and must not be described as — proof that physical NV was unchanged during a partial write.** A
fake store cannot demonstrate that. **That property is conditional on [[B193]] §20.5 on real hardware**, and v1's
wording ("a power-cut mid-write yields the whole old or whole new record") is **withdrawn** as claiming it.
⇒ **this slice makes the CALLER transactional; B193 establishes that the STORAGE is. Neither substitutes for the
other and this slice does not close B193.**

---
## 6 — Gate

Native (baseline measured at HEAD; ⛔ no figures pinned here) · both UI probes · `warning_census.sh` at its pins ·
**four-step simulator inertness**, keystone read from `simulation/BASELINE.md` (⛔ no anchor-table edit) · **six board
envs** with RAM/flash attributed — ⚠ classify by the `board =`/`extends =` chain, **not** by panel; per [[B206]]
delete both `__DATE__` objects or build from scratch and confirm the image holds exactly one timestamp · **§3.10's
measured stack/RAM** · **D2:** `sizeof(Node)` **221880**, `kCap` 91 · **M2:** a bench step for anything metal-only.
