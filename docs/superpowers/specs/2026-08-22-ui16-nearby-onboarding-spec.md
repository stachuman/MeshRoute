<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 — nearby-team onboarding · BOUNDED IMPLEMENTATION SPEC · 2026-08-22

**Status: REVISED FOR THE OWNER REVIEW OF 2026-08-22 — verdict was *revise-then-approvable*, and every ruling and
every one of the six required corrections is folded in below, at the place it belongs.** No code, no tests, and no
edit to any other file was made by the dispatch that produced or revised it.

⛔⛔ **THIS REVISION SUPERSEDES PARTS OF ITS OWN FIRST DRAFT, AND THE WITHDRAWN TEXT IS KEPT VISIBLE** (the house
correction-in-place idiom) — a reader must be able to see what was asked and what was ruled, not just the answer.
The four largest reversals are collected here so none is discovered by accident:
1. ⛔ **`S6` IS REPLACED WHOLESALE.** The draft proposed a small `src/` patch that persisted the granted key into
   `/mrcfg`. **The owner ruled a dedicated `/mrteams` KEYRING**, and it now lands **FIRST in the arc** — see §4-K.
2. ⛔ **`ctr != 0` DOES NOT MEAN AIRBORNE.** The draft's slice-S5 bullet said it did; that contradicts the landed
   T2/T3 transmit truth. **RULED:** `queued` ⇒ `GRANT QUEUED`; **airing arrives later as `PushKind::send_aired`** —
   see **F-9** and §4-N6.
3. ⛔ **THE DRAFT'S `S6 → S7` ORDERING WAS UNIMPLEMENTABLE.** `mr_ui_on_push(pu)` runs **before** the push switch, so
   a router arm can never mean *"only after durable adoption"*. **RULED: persistence runs FIRST and only a `saved`
   return forwards the push** — see **F-10** and §4-K3.
4. ⛔ **`no_pubkey` WAS A DEAD END.** The draft mapped it to a waiting screen and stopped. **RULED: an explicit
   operator-authorised `REQUEST PUBKEY` step** — see **F-12** and §4-N5. ⚠ And the lexeme **`WAITING FOR KEY` is now
   FORBIDDEN** alongside `JOIN COMPLETE` and `KEYLESS`.

**Precedence, and it is the design's own:**
1. **the OWNER RULINGS of 2026-08-22** (this review) — recorded in reported form in §9 and folded into the normative
   body at the place each one names;
2. `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` **§3.6.4** with its 2026-08-06 owner ruling and its
   six numbered flow points (`:790-837`), plus §3.6.5's interruption/safety rules (`:839-850`) and the §13 slice row
   (`:1537`);
3. `docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md` — the **landed navigation
   contract** every new screen inherits and the house spec **style** this document matches;
4. **the code, which outranks all of them as evidence** (V1): every fact below is cited at `file:line` and was
   checked on 2026-08-22 against the working tree. Line numbers are hints — relocate by symbol (V2).

**Standing constraints for every slice below.**
- ⛔ **WIRE BYTES: ZERO.** No frame, field, flag, TLV or `wire_version` is touched by any slice — **including the
  keyring**: a NEW NV record is not a wire change (the `/mrjoin` precedent, `src/device_nv.h:245-263`). The scan is
  **passive** over the type-5 team-id TLV that already rides every team beacon
  (`lib/core/node_beacon.cpp:413-414`; `pack_team_id_tlv`, `lib/core/frame_codec.cpp:195`); the grant rides the
  **existing sealed type-19 path** (`Node::team_key_grant_send`, `lib/core/node.h:275`); the pubkey request rides the
  **existing** `CmdKind::reqpubkey` WANT_PUBKEY flood (`lib/core/node.cpp:1838`). ⚠ If an implementer finds a step
  that genuinely cannot work without wire, **STOP and report**: M3 makes a bump free to deploy and never a reason to
  stop, but C4 makes it **its own slice and its own commit** (a bump re-anchors all 36 streams at once). ⛔ Do not
  design around it silently and ⛔ do not smuggle it into a UI slice.
- ⛔ **EXACTLY ONE SLICE TOUCHES `lib/core`, AND IT IS N1.** Every other slice is `src/`-only and therefore s18-inert
  **by construction** (the simulator compiles `lib/core` + `lib/console`, not `src/` — measured repeatedly in
  `simulation/BASELINE.md`). N1 **re-runs the corpus** and carries the full D2 obligation set spelled out in §2.2.
  ⛔ **AMENDED 2026-08-25 (K3+K4b and K6 round 2; QG-required):** two later corrections additionally touched **ONE
  `lib/hal` file — `lib/hal/mr_ui.h`**, the UI hook header ([[B243]]'s second door; K6's `keyring_full` hook
  parameter). The boundary now reads: `lib/core` = N1 only; `lib/hal/mr_ui.h` = the one permitted HAL
  declaration/stub site, each touch carrying its measured simulator fact — **`lib/hal` is NOT compiled into `lus`**
  (the sim's `CMakeLists.txt` lists its sources explicitly and references `lib/hal` zero times; `mr_ui.h` is
  included only by `src/` and `tools/` TUs, none of which the simulator builds) ⇒ **s18 remains exact** on those
  touches, verified at each gate. N6b's `lib/core` touch was its own QG-ruled corrective slice with the full
  corpus run.
- **Board gate: TWO envs, and this spec deliberately does not name a third.** Per the standing owner ruling the
  dispatch brief picks exactly two; ⛔ a brief may never pre-authorise more. ⚠ **N1 and K1 are exceptions in KIND, not
  in count:** a `lib/core` change moves `sizeof(Node)` and an NV record moves flash, and D2 requires a **per-board
  `RAM_used`/flash diff**, so those two briefs name which envs are *measured* — a measurement, not a widened build
  gate. ★ **AND FOR N1 THE PAIR IS NOW NAMED, OWNER/QG-RULED 2026-08-23 (N1 re-gate): `heltec_mobile` (Xtensa, full
  team plane) and `gateway` (nRF52/ARM, `MR_FEAT_TEAM=0`)** — ⛔ still exactly two, chosen because they span both
  toolchains and both team arms, which is the axis a `lib/core` change moves. **The same pair carries the RAM/flash
  diff AND the warning comparison (§2.2, §5) — one pair of builds, two readings, ⛔ never a third env.**
  ⓘ They are ⛔ **not** OLED envs and do not need to be: N1 is headless core. A UI slice's own two remain the brief's.
- **C1 throughout:** each slice is a feature XOR a refactor XOR a fix.

---

## 1. INVENTORY — §3.6.4's needs against the as-built tree

### 1.1 The six flow points, point by point

| §3.6.4 point | what it needs | as-built | verdict |
|---|---|---|---|
| **1** — `INVITE MEMBER` opens a bounded window, shows the label when configured plus a **short fingerprint** of the full random `team_id`; ⛔ transmits no content key | a creator-side window + the fingerprint | fingerprint **EXISTS and is unused**: `mrui::ui_fmt_team_fingerprint` (`src/firmware_ui_chrome.h:214-221`, `%06lX` of `team_id & kTeamFpMask` = the low 24 bits, `:212`); its header says it is *"COMPLETE and CURRENTLY UNCALLED … the two consumers arrive later — the INVITE screen (slice 5, §3.6.4 point 1) and the joiner's NEARBY candidate list plus its `JOIN <fingerprint>?` confirmation"* (`:204-208`). The **window** does not exist | fingerprint ✅ · window ❌ (N4) |
| **2** — the joiner listens on its **current effective PHY**, collects recently observed **non-zero team ids**, shows a **de-duplicated** list with fingerprint, **signal strength** and **age**; ⛔ read-only | a retained observation cache | ⛔ **DOES NOT EXIST.** `peer_team` is parsed into a **stack local** at `lib/core/node_beacon.cpp:575-576` and is read at exactly two sites — the leaf-exempt same-team accept (`:577-578`) and the foreign-team digest gate (`:1043`). It is **never stored**, and `same_team()` requires `_cfg.team_id != 0` (`lib/core/node.h:301`), so a **teamless** joiner's `same_team_beacon` is false by construction | ❌ **NEW CORE STATE** (N1) |
| **3** — `double` on a candidate opens `JOIN <fingerprint>?` with **BACK selected initially**; the confirmation selects the **exact full `team_id`**, ⛔ never the list index or the truncated fingerprint; same role/PHY/team-DAD/persistence validation as the guarded `team <id>`; committed atomically before live apply | a confirm screen + the existing team transaction | the transaction **EXISTS**: `TeamRequest{ mint=false, team_id=<id> }` through `ProvisioningService::apply_team` (`src/firmware_provisioning_service.h:86-97`), reached from the OLED by the **already-landed** `mrfw::ITeamCreateDevice` seam (`src/firmware_ui_prov.h:58-63`). The **screen** does not exist | transaction ✅ · screen ❌ (N3) |
| **4** — the joiner is now a **keyless team member**; joining must not imply key possession | `set_team_id` must not carry a key across a switch | ✅ **HOLDS BY CONSTRUCTION.** `Node::set_team_id` calls `team_channel_key_clear()` — *"a content key belongs to the team it was granted for and must not outlive it"* (`lib/core/node.cpp:683`, ruling `node.h:269-272`). ⓘ **AMENDED BY THE KEYRING RULING:** clearing the LIVE key stays; the **stored** record for that team is **RETAINED** and ⛔ never silently reactivated (§4-K2) | ✅ (amended) |
| **5** — opening invitation mode **snapshots** the known member identities; while open, list candidates **first observed after that snapshot** by **team-local id + short identity-hash fingerprint**; ⛔ call them **`NEW MEMBER`**, never `KEYLESS` | a member enumeration + a snapshot + a diff | the **member state EXISTS**: `rt_team_count()` / `rt_team_at(i)` (`node.h:776-777`, already consumed at `src/firmware_ui.cpp:589-598`), `is_team_peer` (`node.h:203`), and id → `key_hash32` via `team_key_of_id` at the **`authoritative`** floor (`node.h:219`, `lib/core/node_routing.cpp:895-906`). The **snapshot + diff** need ⛔ **no core state** — and the draft's single-authority version **mislabels**, see **F-11** | member state ✅ · snapshot/diff ❌ (N4, **corrected**) |
| **6** — `GRANT KEY` resolves the member's **authoritative** public key, then reuses the **existing sealed `team grantkey` payload and send path**; `WAITING FOR KEY` until usable; `KEY SENT` is honest, `JOIN COMPLETE` is not; the **joiner** shows `TEAM KEY RECEIVED` **only after durable adoption succeeds** | the sealed grant, end to end | **send ✅** (`node.h:257-276`) · **install-to-RAM ✅** (`lib/core/node.cpp:250-269`) · ⛔ **the `no_pubkey` arm DEAD-ENDS (F-12)** · ⛔ **`queued` is NOT airborne (F-9)** · ⛔ **durability MISSING (F-6, now the keyring)** · ⛔ **the push reaches the UI BEFORE any persistence (F-10)** | ⛔ **four corrections** — §4-K, N5, N6, K3/K4 |

### 1.2 The joiner's observation path — exactly what the RX path does today with a foreign team's TLV

Traced in order through `Node::ingest_beacon` (`lib/core/node_beacon.cpp:550`):

1. `:558` — `if (wire::flags_of(bytes[0]) != _cfg.leaf_id && _cfg.team_id == 0) return;` ⇒ ★ **a TEAMLESS joiner drops
   every beacon whose LEAF NIBBLE differs, before parse.**
2. `:559-563` — the `wire_version` gate, deliberately kept **before** parse.
3. `:564-566` — `parse_beacon`; failure returns.
4. `:574` — `leaf_match`; `:575-576` — `peer_team = parse_team_id_tlv(...)` into a **stack local**.
5. `:577-578` — `same_team_beacon = b.is_mobile && same_team(peer_team)`; `if (!leaf_match && !same_team_beacon) return;`
6. `:589-645` — the R6.1 leaf-config membership filter, which **can `return` at six sites**
   (`:613 :621 :623 :634 :640 :642`). ⓘ A **mobile** receiver is exempt by arm (C'), `:602-608`.
7. `:1043` — the only other reader: the team digest gate.

⇒ **the foreign team id is parsed, used twice as a predicate, and discarded.** There is **no `src/`-reachable hook on
the beacon path at all**: `lib/hal/mr_ui.h` exposes exactly seven hooks (`:78-84`), none of them a beacon, and no
`PushKind` carries one (`lib/core/command.h:192-260`).

★★★ **THEREFORE THE JOINER'S HALF GENUINELY NEEDS NEW `lib/core` STATE, AND IT IS THE ONLY PART THAT DOES.**

### 1.3 The creator's invitation window — what already exists to snapshot and diff

| the creator needs | the as-built source | new state? |
|---|---|---|
| "who is a member right now" | `rt_team_count()` / `rt_team_at(i)` (`node.h:776-777`); `is_team_peer(id)` (`:203`) | ⛔ none |
| that member's **identity hash** | `team_key_of_id(id, out, IdBindConf::authoritative)` (`node.h:219`; `node_routing.cpp:895-906`) — ⛔ the authoritative floor is the **default**, a `claimed` on-air binding cannot answer, and a row past `id_bind_ttl_ms` (48 h) reads absent | ⛔ none |
| "known **at snapshot time**" | ⛔ nothing stores it, and `last_seen_ms` is **not** it: `team_key_set` refreshes it on every authoritative beacon (`node_routing.cpp:869`) ⇒ it means *last heard* | **a `src/`-side snapshot** (N4) |
| a team label | ⛔ **THERE IS NO TEAM LABEL FIELD ANYWHERE** — no team-name member in `mrnv::Blob` (`src/device_nv.h:117-125`); the grant's `name=` is explicitly **not persisted** (`lib/core/node.cpp:264-266`) | see **F-3**; ⛔ and the keyring ruling repeats it: *labels do not belong in this record* |

★★★ **THE SNAPSHOT NEEDS NO CORE STATE** — but ⛔ **ONE AUTHORITY IS NOT ENOUGH.** See **F-11**: the ruling is **two**
snapshot authorities (authoritative hashes **and** a bitset of team-local ids present at opening).

### 1.4 The landed PROVISION menu, and where NEARBY attaches

- `enum class Provision` has eight arms and the invariant `Settings::provisioning ⟺ Provision != closed`, enforced by
  **exactly two primitives** — `enter_provision` / `close_provisioning` (`src/firmware_ui_model.h:357-359`, `:335-337`,
  `:2729`, `:2744`). ⛔ Never assign either field at a call site.
- The child list is `provision_rows(create_team, join_static)` → `{CREATE TEAM, JOIN NETWORK, BACK}` (`:520`,
  `:553-559`, labels `:577-585`); the two predicates are **separate parameters** on purpose (`:539-547`); the parent
  row is **hidden** when there is no child — `provision_has_child` (`:567-572`), an owner ruling of 2026-08-19.
- ★★★ **THE CODE ITSELF ANTICIPATES THIS FEATURE:** `provision_has_child`'s header says the predicate is derived from
  the child list and not re-spelled *"…because the two predicates would then be two authorities, and slice 6 (**or
  §UI-16's nearby-team child**) could add a child the parent row never learned about"* (`:561-566`). ⇒ a third child
  is picked up with **zero change**, because `back` is excluded **by identity** and not by position.
- ✅ **OQ-1 RULED (owner, 2026-08-22): `JOIN TEAM` opens NEARBY DIRECTLY.** A submenu arrives only when a **second**
  join method exists. Rationale kept: in v1 `NEARBY` is `JOIN TEAM`'s only child (typed entry is out of scope,
  design `:836-837`), so a submenu would be the *"row that costs the operator a walk and a `double` to discover it
  offers nothing"* the 2026-08-19 hiding ruling refuses.

### 1.5 The sealed grant path, end to end, verified

- **request** — the target is a `0x<hash>` or a bare team-local id resolved through *"the beacon-only team key
  cache"*, refusing loud when never heard (`src/firmware_config.cpp:1349-1357`).
- **seal + send** — `Node::team_key_grant_send` returns one of **eight** typed outcomes (`node.h:257-276`):
  `queued`, `no_team`, `no_key`, `no_identity`, **`no_pubkey`**, `self`, **`delegated`**, `too_large`.
- ★★ **THE `no_pubkey` BAR AND ITS BAN, VERIFIED VERBATIM** (`lib/core/node.cpp:185-196`): the grant requires a
  pubkey at `PeerKeyConf::authoritative` or better, and the source states the ban in as many words —
  *"⚠ AND WE DO **NOT** AUTO-ISSUE A WANT_PUBKEY LOCATE HERE — §no-auto-reqpubkey, OWNER-RATIFIED 2026-07-29 … A
  future slice that 'fixes' this by auto-resolving is reversing a ruling."* ⓘ The canonical home of the rule is
  `Node::send_by_hash`'s header (`lib/core/node_hashlocate.cpp`, tagged `§no-auto-reqpubkey`); `node.cpp:185-196`
  restates it *"only because THIS verb is the worst possible place to break it"*.
- **install** — `team_key_grant_receive` refuses unsealed / bad-len / long-name / no-team / **team_mismatch** /
  bad-key, then `team_channel_key_adopt_priv` and a `PushKind::team_key_received` (`lib/core/node.cpp:250-269`).
- ⛔ **and then it stops there** — F-6, F-9, F-10.

### 1.6 The navigation contract every new screen inherits (UI-17, landed)

⛔ **No screen in this spec may re-litigate it:** a top-level screen lands **passive** and `short` passes it in one
press; `double` **enters**; the interactive list's last row is `BACK` and `BACK` returns to the **passive form of the
same screen**, ⛔ never to another screen; a sub-view's exit returns to **its own parent** (`close_provisioning` → the
SETTINGS menu, `src/firmware_ui_model.h:2317`; the join confirm's `BACK` → `join_select`, ⛔ not the menu, `:221`); a
**terminal result is acknowledged by either press** and ⛔ never grows a selectable `BACK` row (UI-17 §9 R-5);
interaction state **survives blank/wake** with the wake press consumed; and ⛔ **a push never navigates**.
✅ **OQ-3's CLARIFICATION LANDS HERE (owner, 2026-08-22): the invitation WINDOW survives blanking; an UNFINISHED
CONFIRMATION does not.** That is `provision_reset_on_leave`'s existing rule — *"a stale `create_confirm` surviving
into the next visit would re-open a confirmation the operator never asked for, which §3.6.5 rule 1 forbids"*
(`src/firmware_ui_model.h:386-389`) — extended to this arc rather than re-invented.

### 1.7 As-built facts that CONTRADICT or COMPLETE §3.6.4 — reported, not silently reconciled

- **F-1 · "the joiner listens on its CURRENT EFFECTIVE PHY" is TRUE but INCOMPLETE — the LEAF NIBBLE also gates it.**
  `node_beacon.cpp:558` drops a beacon whose leaf nibble differs **before parse** whenever `_cfg.team_id == 0`. ⇒ a
  teamless joiner on the right frequency/BW/SF but a different `leaf_id` **hears nothing**. ⛔ **This spec does NOT
  relax that gate** — relaxing a pre-parse drop is a routing-plane change with corpus consequences and is not
  onboarding. ⇒ a design-doc correction-in-place is owed, **drafted by N2** (⛔ the design doc is QG-owned and is not
  edited by the slice), and the panel carries the honest second line (S-4).
- **F-2 · "signal strength" has never been rendered on this panel.** `TeamRow::score_q4` is filled and read by
  nothing (`src/firmware_ui_model.h:794-799`), and none of `firmware_ui_chrome.h`'s five formatters (`:103 :118 :140
  :170 :214`) is a signal token. ⛔ **CORRECTED IN PLACE 2026-08-22, AND THE WITHDRAWN PROPOSAL IS KEPT VISIBLE:**
  the draft proposed *"a THREE-LEVEL coarse token derived from the SNR q4 bucket … `bucket_of_snr_4b`"*. **⛔ THAT
  FUNCTION IS NOT A UI TOKEN SOURCE AND A THREE-LEVEL MAPPING WOULD BE A SECOND DEFINITION OF SIGNAL QUALITY —
  FORBIDDEN (owner, 2026-08-22).** ✅ **RULED: FOUR levels rendered `0/3` `1/3` `2/3` `3/3`, derived from the
  EXISTING `presence_quality_tier()`** — verified at `lib/core/protocol_constants.h:905`, a pure
  `constexpr uint8_t presence_quality_tier(int16_t snr_q4)` over the four `PresenceQuality` tiers
  (`presence_q_critical/weak/ok/strong`, `:898`) with boundaries −12 / −4 / +4 dB (`:899-901`), already *"Shared by
  home (per-mobile EWMA) + mobile (heard candidate EWMA)"* (`:904`). The token is ASCII, fixed-width **3 columns**,
  and fits the 19-column body.
- **F-3 · "displays the team label when one is configured" has NOTHING to configure** (§1.3). §3.6.4 itself permits
  this — a label *"may be shown only if it came from an authenticated local profile"* — so the honest v1 answer is
  that **no label is shown on either side**. ⛔ Do not invent a label store; the keyring ruling repeats the
  prohibition for its own record (*labels do not belong in this record*).
- **F-4 · a nearby JOIN's success has NO honest outcome word in the landed enum.** `UiProvOutcome`
  (`src/firmware_ui_model.h:622-624`) maps `created → "TEAM CREATED"` (`:684`), and a join by id lands
  `ProvVerdict::applied` on the same transaction ⇒ without a new arm the panel says **`TEAM CREATED`** for a join —
  the exact defect that block forbids (*"a JOIN is not a CREATE, and the two verbs' words must differ on the panel or
  the operator cannot tell which operation answered"*, `:615-617`). ⇒ **N3 adds ONE arm** (`team_joined`).
- **F-5 · the OLED path adds a PHY precondition the console verb lacks — deliberately.** `ui_prov_create_team`
  refuses `PHY DIFFERS` / `USE SERIAL` with ⛔ zero writes, zero airtime, zero retunes
  (`src/firmware_ui_prov.h:78-115`), an owner ruling of 2026-08-19. The nearby join **inherits** it; that is a
  narrowing, ⛔ not a divergence from §3.6.4's validation requirement.
- **F-6 · ⛔⛔ A GRANTED TEAM CONTENT KEY IS NOT PERSISTED — §3.6.4 point 6's "durable adoption" DOES NOT HOLD TODAY.**
  Measured: `team_key_grant_receive` adopts into **RAM** (`lib/core/node.cpp:260`; the RAM home is `_team_ch_*`,
  `node.h:224-227`), the push handler at `src/fw_main.cpp:1470-1474` **prints and nothing else**, and the only
  writers of `Blob::team_ch_*` are `blob_put_team_channel_key` (`src/firmware_provisioning_service.h:326-333`) and
  its delegate `blob_take_team_channel_key` (`src/firmware_config.cpp:727`), whose one caller is
  `seed_blob_from_live` (`:732-741`) — reached by the provisioning verbs and by `handle_cfg_set`'s seed path,
  ⛔ **never by the grant receipt**. One `switch` arm away, `PushKind::config_adopted` in the same loop **does**
  `mrnv::save(b)`. ⇒ **a granted key is lost on the next reboot.**
  ✅ **REGISTERED: [[B240]]** (`docs/2026-07-30-open-bug-register.md:130`), which carries this measurement **and the
  owner's re-ruling**. ⛔⛔ **CORRECTED IN PLACE 2026-08-22, AND THE WITHDRAWN PLAN IS KEPT VISIBLE:** the draft
  proposed *"S6 — FIX: a RECEIVED team content key is PERSISTED … persist through the existing single conversion
  path … one whole-record `mrnv::save`"* into `/mrcfg`. **THAT SLICE IS REPLACED WHOLESALE by the `/mrteams`
  KEYRING** (§4-K) — the owner's reasons are structural and are recorded in §9 R-6: a single-slot `/mrcfg` field
  cannot hold a key per team, cannot survive `leave`, and makes "retained but not active" unrepresentable.
- **F-7 · a candidate with no AUTHORITATIVE key binding cannot be granted to.** `team_key_of_id`'s floor defaults to
  `authoritative` (`node_routing.cpp:895-906`); the grant refuses `no_pubkey` without a **verified pubkey**
  (`node.cpp:194-196`); and `_team_peer` bits are set from a **multi-hop DV entry with no key at all**
  (`lib/core/node.cpp:645`). ⇒ **RULED BY FAIL-CLOSED (C2), pinned in N4:** such a member is **not listed as a
  grantable candidate**, ⛔ never listed with a blank or invented fingerprint. ⚠ **AMENDED by F-11:** it is *also*
  recorded in the id-bitset half of the snapshot, so it cannot later be mislabelled `NEW MEMBER`.
- **F-8 · §3.6.4 point 5 uses TWO DIFFERENT FINGERPRINTS in one sentence.** The team-**id** fingerprint is
  `ui_fmt_team_fingerprint` (24 bits of `team_id`); the **member** fingerprint is over a `key_hash32` — a different
  value in a different space. ⛔ They must not share one helper (the shared helper's own U1 argument,
  `src/firmware_ui_chrome.h:209-211`). ⇒ **N4 declares a SECOND, separately-named token** (S-13).
- **F-9 · ⛔⛔ `ctr != 0` DOES NOT MEAN AIRBORNE — the draft said it did, and that contradicts the landed T2/T3
  transmit truth.** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the draft's slice-S5 bullet read *"the `queued` arm
  **splits on `ctr`**: `ctr != 0` = airborne ⇒ `KEY SENT`"*. **A non-zero `ctr` means ADMITTED / QUEUED**; physical
  transmission arrives **later**, as `PushKind::send_aired` — verified at `lib/core/command.h:246`:
  *"§T3 (TX-completion arc, 2026-08-14): a locally-ORIGINATED DM or channel post **PHYSICALLY LEFT THE RADIO** — the
  SX1262 TxDone edge for the frame carrying THIS flight … Carries only the existing `dst`/`ctr`"*.
  ✅ **RULED SEMANTICS (owner, 2026-08-22):** `queued` with `ctr != 0` ⇒ **`GRANT QUEUED`** · a **correlated**
  `send_aired{dst, ctr}` ⇒ **`KEY SENT`** · a correlated failure ⇒ **`GRANT FAILED`** · ⛔ there is **no e2e ack on a
  grant** (*"v1 carries no ack request on a grant; the receiver's `team_key_received` push is the app-level"* signal,
  `lib/core/node_mac_rx.cpp:1723`) ⇒ ⛔ **never `JOIN COMPLETE`, never `KEY RECEIVED` on the granter's panel.**
  ⓘ **`delegated` is UNREACHABLE on the real seam** because the UI always sends with `Plane::TEAM` — ⛔ **but the pure
  mapper still FAILS LOUDLY if a fake returns it** (C2: an unreachable arm that silently returns a plausible word is
  the arm that lies the day it becomes reachable).
- **F-10 · ⛔⛔ `mr_ui_on_push` RUNS BEFORE THE PUSH SWITCH, SO THE DRAFT'S `S6 → S7` ORDERING WAS UNIMPLEMENTABLE.**
  Verified at `src/fw_main.cpp:1310`: inside `while (g_node.next_push(pu))` the **first** statement is
  `mr_ui_on_push(pu);` and the `default`-less rendering switch follows it. ⇒ the raw `team_key_received` push reaches
  the UI **before any persistence attempt could run**, and a router arm can never mean *"only after durable
  adoption"*. ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the draft's S7 said *"ONE arm in the pure push router …
  ⛔ GATED ON S6"* — the gate it named was not expressible.
  ✅ **RULED (owner, 2026-08-22): for `team_key_received`, a config-service PERSISTENCE FUNCTION RUNS FIRST; only a
  `saved` return forwards the push to `mr_ui_on_push`.** On failure the UI ⛔ never shows `TEAM KEY RECEIVED` and the
  failure is reported **explicitly** as RAM-only / lost-on-reboot. ★ **And the function RE-CHECKS AT HANDLING TIME**,
  which closes the delayed-push race where membership changes between RX and drain: `pu.team_id != 0` ·
  `pu.team_id == g_node.config().team_id` · the **live key is present** · the **loaded blob belongs to the same
  team**. ⇒ §4-K3.
- **F-11 · ⛔ SNAPSHOT HASHES ALONE MISLABEL.** A **route-only** member with no authoritative hash at window-open is
  absent from a hash-keyed snapshot; if its binding turns authoritative **later in the window** it is announced as
  `NEW MEMBER` although it was present all along. (The two facts that produce it are already cited: `_team_peer` bits
  set from a keyless DV entry, `lib/core/node.cpp:645`; and `team_key_of_id`'s authoritative floor,
  `node_routing.cpp:895-906`.) ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the draft's §1.3 said *"⛔ It is keyed by
  `key_hash32`, never by team-local id"* — the *never* was wrong; the id is not an identity, but it **is** evidence
  of presence.
  ✅ **RULED (owner, 2026-08-22): TWO SNAPSHOT AUTHORITIES.** (a) the set of **authoritative hashes** at opening —
  which survives a team-local-id change; (b) a **bitset of team-local ids present at opening** — which suppresses an
  already-present member whose hash arrives later. **A candidate is NEW only when NEITHER its hash NOR its current id
  was in the opening snapshot.** ⚠ **The unavoidable double-change case is DOCUMENTED, not engineered away:** an
  **unkeyed** member that changes id **and** acquires a hash inside one window will prompt — a **SAFE FALSE PROMPT**,
  because the operator must still confirm and the fingerprint is shown. ⛔ It is stated in-source so no later slice
  reads it as a bug.
- **F-12 · ⛔⛔ UI-16 AS DRAFTED DEAD-ENDS ON `no_pubkey`.** The grant deliberately refuses without an authoritative
  pubkey and forbids automatic acquisition (`lib/core/node.cpp:185-196`, `§no-auto-reqpubkey`, owner-ratified
  2026-07-29) — so a one-button device with no console has **no way forward at all**. ⛔ **WITHDRAWN WORDING, KEPT
  VISIBLE:** the draft said *"`no_pubkey` shows `WAITING FOR KEY` and ⛔ grants nothing; a timeout does the same"*
  and stopped there.
  ✅ **RULED (owner, 2026-08-22): AN EXPLICIT, OPERATOR-AUTHORISED PUBKEY REQUEST.** `no_pubkey` opens its own
  confirmation with **`BACK` selected** and the action **`REQUEST PUBKEY`**; confirming sends the **EXISTING**
  team-scoped WANT_PUBKEY request; a received **`peer_key_cached`** enables `GRANT KEY`; ⛔ **the private team key is
  never sent automatically.** ★ **THIS PRESERVES THE BAN RATHER THAN REVERSING IT:** §no-auto-reqpubkey forbids a
  *silent, automatic* escalation — here the operator explicitly authorises the on-air identity request, which is
  exactly what typing `reqpubkey` at the console is. **The mechanism exists and is verified:** `CmdKind::reqpubkey`
  is *"the ONLY auto-source of WANT_PUBKEY now"* (`lib/core/node.cpp:1838`), takes an explicit plane
  (`0=AUTO / 1=TEAM (-t) / 2=GLOBAL`, `:1841`), and its answer surfaces as `PushKind::peer_key_cached`
  (`lib/core/command.h:201`; `push_peer_key_cached`, `lib/core/node.cpp:1936`). ⚠ **Lexemes: `NEED PUBKEY` /
  `REQUEST PUBKEY` / `WAITING FOR PUBKEY` — ⛔ NEVER `WAITING FOR KEY`, which is ambiguous between the recipient's
  PUBKEY and the team CONTENT key** (S-34 is now FORBIDDEN).
- **F-13 · REJECT NEEDS PER-WINDOW STATE, AND THE DRAFT WAS SELF-CONTRADICTORY.** ⛔ **WITHDRAWN WORDING, KEPT
  VISIBLE:** the draft pinned *"`REJECT` performs **nothing at all** — ⛔ no send, no state change, no note beyond the
  candidate leaving the list"*. Those two clauses cannot both hold: **without a handled set the next refresh re-adds
  the candidate.**
  ✅ **RULED (owner, 2026-08-22): a VOLATILE PER-WINDOW HANDLED SET.** `REJECT` **and** a queued grant add the
  candidate's **hash** to it; it changes ⛔ no core, radio, membership, key or NV state; and it is **discarded when
  the window closes**.
  ⛔ **CORRECTED 2026-08-24 (N6 QG, owner-relayed; the "and a queued grant" clause WITHDRAWN, KEPT VISIBLE
  ABOVE): the handled set is `REJECT`-ONLY.** The grant result screen closes and discards the window anyway, so
  a grant-side write is **unobservable** — an instrument-shaped rule nothing can measure. The two N4/N6
  mutations over the set (dropped / made persistent) and P-11b are unaffected; N6's pin 2 already says only
  what `REJECT` does.
- **F-14 · INVITE REFRESH IS NOT THE SAME QUESTION AS NEARBY REFRESH, AND THE DRAFT'S §10 CONFLATED THEM.**
  ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the draft's §10 said *"⛔ **no auto-refresh of either list**"*.
  ✅ **RULED (owner, 2026-08-22):** **NEARBY** teams = a **frozen snapshot per entry**, manual refresh only (leave and
  re-enter). **INVITE** candidates = **locally refreshed while the window is active** — which performs **no scan and
  transmits nothing**: it only re-reads member state the node already holds (`rt_team_at` / `team_key_of_id`, both
  `const`). **Selection stays identity-based, and opening a confirmation FREEZES the selected hash/id.**
- **F-15 · ⛔ THE DRAFT BANNED NAMES OUTRIGHT WHERE §3.6.4 ONLY BANS *GUESSED* ONES — THERE IS A LIFECYCLE, AND IT IS
  ALREADY HALF-BUILT.** ⛔ **WITHDRAWN WORDING, KEPT VISIBLE:** the draft's **P-5** read *"rows render fingerprint ·
  signal · age and ⛔ nothing name-shaped"* as a permanent rule on **both** screens.
  ★ **THE THREE AS-BUILT FACTS THAT MAKE THAT TOO STRONG, each verified:**
  **(a)** the **TEAM member list already prefers the cached node name** — the chain
  `team_key_of_id → peer_name_find → 0x<hash> → bare id` is declared at `src/firmware_ui.cpp:359` and implemented at
  `label_from_hash` (`:361`) / `label_for_team_id`. ⇒ an outright ban would make the **invite** view describe a member
  more poorly than the **TEAM** view already describes that same member, on the same panel.
  **(b)** **team creation distributes no names; a name travels WITH the public-key exchange, in BOTH directions** —
  the **request** carries the requester's name across the forward (`lib/core/node_hashlocate.cpp:1178`, *"§name: carry
  the requester's name across the forward (WITH the pubkey)"*; the WANT_PUBKEY H is sized `+1+name_len`, up to 77 B,
  `:1181`), and the **response** appends `effective_name` (`:1251`, *"§1.3: ‖ [name_len][name] (OUR name; the owner
  answers its own key)"*). It is **cached beside the peer key** (`:1142`, *"§name: cache hash->name too (WITH the
  pubkey), symmetric to the TYPE-5 answer"*) through the ONE name writer `peer_key_set → peer_name_set`
  (`:335 :346 :377 :386`).
  ⓘ **REFINED CITE, REPORTED RATHER THAN SILENTLY ADOPTED:** the amendment named `node_hashlocate.cpp:1245`; the
  response-side append is at **`:1251`** and the request side is a **separate** site at **`:1178`**. The claim holds
  in full — only the line was off.
  **(c)** ⇒ such a name is ⛔ **never guessed from an id** — which is the thing `:805` actually forbids.
  ⛔⛔ **REWORDED IN PLACE 2026-08-22 (QG, K1 gate round), AND THE WITHDRAWN CLAIM IS KEPT VISIBLE:** this bullet read
  *"⇒ **the pubkey exchange IS an 'authenticated source' in §3.6.4's own sense**"*. **THAT OVERCLAIMS, and the
  falsifier is this section's own bound two paragraphs down.** A node name is **MUTABLE METADATA CACHED ALONGSIDE a
  verified pubkey**; it is ⛔ **not itself cryptographically authenticated**. The permission is unchanged — what
  `:805` forbids is a label **guessed from an id**, and this one is not guessed: it is **carried by, and cached with,
  an exchange whose KEY half is verified**. ⇒ the ground is **PROVENANCE, ⛔ not authentication.**
  *Falsifier: the QG K1-gate ruling, plus this section's own analysis — the key is verified (`ed_pub[:4] ==
  key_hash32`), the name is refreshed mutable metadata (`lib/core/node_hashlocate.cpp:346`).*
  ★★ **THE TRUST BOUND, STATED HONESTLY BECAUSE IT IS WHAT MAKES RULES 4 AND 5 NECESSARY:** the exchange
  authenticates the **KEY** (`peer_key_set` re-verifies `ed_pub[:4] == key_hash32`), ⛔ **not the truthfulness of the
  NAME.** The name is the peer's own self-assertion and it is **MUTABLE** — *"REFRESH the name (mutable) even when the
  key is unchanged"* (`node_hashlocate.cpp:346`). Same class of accepted bound as `PeerLocSrc`'s (`lib/core/node.h:190-195`).
  ⇒ **a name may DESCRIBE and may ⛔ NEVER IDENTIFY.**
  ✅ **RULED (QA-proposed, owner-forwarded 2026-08-22) — FIVE LIFECYCLE RULES:** (1) NEARBY rows stay identified by the
  **team** fingerprint only, and ⛔ **an advertiser's NODE name is never presented as the TEAM name**; (2) a NEW-MEMBER
  row **initially** shows the **member** fingerprint — the draft's rule, now the **initial state of a lifecycle**
  rather than a permanent ban; (3) after an explicit `REQUEST PUBKEY` **succeeds for that hash**, the invitation **and**
  TEAM views **prefer the cached node name**, through the **existing** resolver (U1 — called, ⛔ never re-spelled);
  (4) the **full hash stays VISIBLE** in the confirmation/detail screen — ⛔ a name is never the only identity shown at
  the moment of an irreversible act; (5) selection, pubkey requests and key grants stay keyed **EXCLUSIVELY by the full
  hash**, ⛔ never by the mutable display name (§B64 identity discipline).
  ⇒ landed at **§3 P-5 / P-5b / P-7c / P-7d**, in slices **N2** (confirmed unchanged, plus one new control), **N4**,
  **N5** and **N6**, at strings **S-6 / S-13 / S-35 / S-36**, and at metal **§7.1.3 · §7.3.3 · §7.4.3b**.
  ⛔ **THE K-SLICES ARE UNTOUCHED BY THIS AMENDMENT:** the keyring record carries **no labels by ruling**, so nothing
  here changes what is stored, written or restored. This is a **display lifecycle only**.

---

## 2. THE CENTRAL INVENTORY ANSWER — what exists, what needs new core state

| capability | status | evidence |
|---|---|---|
| the team id already on the wire | ✅ exists, **zero wire change** | `pack_team_id_tlv` / `parse_team_id_tlv`, `frame_codec.cpp:195/201`; emitted at `node_beacon.cpp:410-414` |
| the shared six-hex team fingerprint | ✅ exists, built for this feature, **currently uncalled** | `src/firmware_ui_chrome.h:204-221` |
| the joiner's **recently-observed foreign team ids** | ⛔ **NEW `lib/core` STATE** (N1) | `peer_team` is a stack local, `node_beacon.cpp:575-576`; no store, no accessor, no `src/`-reachable beacon hook |
| the **signal tier** for that list | ✅ exists — ⛔ do not define a second one | `presence_quality_tier`, `protocol_constants.h:905`; the EWMA it consumes, `:82-92` |
| the creator's **member enumeration** and **identity hash** | ✅ exists | `rt_team_count`/`rt_team_at` (`node.h:776-777`); `team_key_of_id` (`:219`) |
| the creator's **window + two-authority snapshot + diff + handled set** | ⛔ new, but **`src/`-ONLY** | nothing stores "known at time T"; `last_seen_ms` means *last heard* (`node_routing.cpp:869`) |
| the **join by team id** transaction | ✅ exists | `TeamRequest{mint=false, team_id}` → `apply_team`, `src/firmware_provisioning_service.h:86-97` |
| "keyless after joining" | ✅ by construction | `set_team_id` → `team_channel_key_clear()`, `lib/core/node.cpp:683` |
| the **sealed key grant** (request · seal · send · install-to-RAM) | ✅ exists end to end | `node.h:257-276`; `lib/core/node.cpp:250-269` |
| the **operator-authorised pubkey request** | ✅ exists — ⛔ no new verb, no new wire | `CmdKind::reqpubkey` (`node.cpp:1838`), plane-explicit (`:1841`), answered by `peer_key_cached` (`command.h:201`) |
| the **transmit-completion** signal for `KEY SENT` | ✅ exists | `PushKind::send_aired{dst, ctr}`, `command.h:246` |
| **durable** adoption of a received grant | ⛔ **MISSING** ([[B240]]) ⇒ the **`/mrteams` keyring** | no writer of `Blob::team_ch_*` on the receipt path; `fw_main.cpp:1470-1474` prints only |
| the NV **record precedent** the keyring follows | ✅ exists | `/mrjoin`: `JoinBlob{magic, version, reserved, prof[4]}` + three ABI static_asserts (`src/device_nv.h:245-263`); the absent/corrupt/io_failed matrix `ProfileVerdict{ok, unchanged, empty, refused, nv_failed}` with `unchanged ⇒ ZERO writes` as the flash-wear guard (`src/firmware_join_profiles.h:84-90`); factory-reset semantics (`src/device_nv.h:268-270`) |

★★★ **IN ONE LINE: exactly ONE new piece of `lib/core` state is required — the joiner's read-only observation cache
(N1). Everything else §3.6.4 asks for either already exists in `lib/core`, or is `src/` UI state, or is the new NV
record the keyring ruling defines — and ⛔ none of it is a wire change.**

### 2.1 N1's shape, and why it is the smallest thing that can work

A **Node-global** (⛔ not `LayerRuntime`) ordered bounded array of recently-observed team ids, written at one site and
read through two `const` accessors. The precedent is `_peer_loc` and its reasoning is reused verbatim: *"Node-GLOBAL
(not LayerRuntime …): a `key_hash32` is a layer-INDEPENDENT identity … per-leaf copies would cost 2x the RAM to hold
two answers to a question that has one"* (`lib/core/node.h:2752-2757`) — a `team_id` is layer-independent the same
way. And like `_peer_loc` it is **RAM only, deliberately volatile** (`node.h:2758-2764`).

★★★ **THE RECORD LAYOUT IS OWNER-RULED, AND THE WITHDRAWN ONE IS KEPT VISIBLE.** The draft wrote
`{ uint32_t team_id; uint64_t last_ms; int16_t snr_q4; uint8_t src_id; }` — **which pads**: the `uint64_t` needs
8-alignment, so `4 + 4pad + 8 + 2 + 1 + 5pad` measures **24**. ✅ **RULED: reorder to begin with `uint64_t last_ms`**
⇒ `8 + 4 + 2 + 1 + 1pad` should measure **16**. ⚠ **SUBJECT TO THE REQUIRED ABI MEASUREMENT** — the `PeerLoc` lesson
is that a briefed 16 B measured 20, so the implementer **pins `sizeof` and two `offsetof`s beside the struct**
(`node.h:2802-2809` / `:3079-3085` are the two precedents) and reports the measured figure. ⛔ Never infer it.

- ✅ **OQ-2 RULED: a 10-minute retention window and 8 entries**, with three amendments folded in here:
  - ⓘ **THE RATIONALE IS EXACT, NOT APPROXIMATE.** ⛔ **WITHDRAWN, KEPT VISIBLE:** the draft argued *"10 minutes is
    several beacon periods on every `sf_list` in the tree"*. **The real reason: 10 min = EXACTLY TWO default
    team-beacon periods** — `team_beacon_period_ms = 300000` (5 min), *"a TEAM member's STEADY-state beacon period …
    3× more responsive than static's 15 min"* (`lib/core/node_carriers.h:110`). Two periods is the smallest window
    that survives one missed beacon.
  - ★ **SIGNAL UPDATES VIA THE EXISTING SNR EWMA, ⛔ NEVER "strongest seen in window".** ⛔ **WITHDRAWN, KEPT
    VISIBLE:** the draft said the upsert takes *"the **stronger** `snr_q4` within the retention window"*. **Max-seen
    MISLEADS when a mobile moves away** — it latches the best moment and never decays. ⇒ use
    `protocol::snr_ewma_update(ew, sample_q4)` (`lib/core/protocol_constants.h:91-93`, which seeds on the first
    sample and then steps at α = 5/16 ≈ 0.3, `:82-84`). ⛔ **One definition of "how SNR is smoothed", one of "how it
    becomes a tier"** — U1 in both directions.
  - ★ **IT IS AN ORDERED BOUNDED ARRAY: refresh in place; when full, SHIFT OUT THE OLDEST AND APPEND.** ⇒ **OQ-5's
    first-observed order is STRUCTURAL** — ⛔ no extra timestamp field, ⛔ no sorting at read, and the list cannot
    re-order under the operator's cursor. ⛔ **WITHDRAWN, KEPT VISIBLE:** the draft said *"eviction is **evict-oldest**,
    the `_team_keys` / `_peer_loc` idiom"*, which describes the same victim but leaves the *order* unstated.
- **Write site:** immediately after `same_team_beacon` is computed and the leaf/team drop has run
  (`node_beacon.cpp:577-578`), gated on `b.is_mobile && peer_team != 0`. ★ Placed **before** the R6.1 config filter
  deliberately — that filter has six `return` sites (`:613 :621 :623 :634 :640 :642`) and a foreign team's node
  legitimately fails it; observing a node we refuse to **peer** with is exactly what a read-only scan is. ⛔ Placed
  **after** `parse_beacon` and the `wire_version` gate, so nothing unparsed or wire-incompatible is recorded.
- **Read:** `team_seen_count()` / `team_seen_at(i)` — `const`, allocation-free, bounded, and ⛔ **stubbed inert on
  `!MR_FEAT_TEAM`** (the `node.h:238-252` pattern). ⓘ The cache **is** team-gated: it is team-plane-only by
  construction and a gateway can never act on it. **The retention window is applied at the READ**, so a stale entry
  on a silent radio cannot linger readable.
- ⛔ **NO `MR_EMIT` ANYWHERE ON THE NEW PATH** — telemetry would re-anchor the five team scenarios in the same run as
  the behaviour change and make the delta unattributable (the sibling refusal's own reasoning,
  `lib/core/node_routing.cpp:855-858`; C4 applied to telemetry).
- ⛔ **NO NEW TIMER ID.** Retention is a deadline comparison at read time (`kCap` is fully consumed).

### 2.2 Why N1 is expected to be corpus-inert, and why it is measured anyway

- **s18 is inert by construction:** it carries **no type-5 TLV** — the write gate `peer_team != 0` is never true, and
  the source says so at the parse site (`node_beacon.cpp:576`: *"s18 has no TLV -> 0"*).
- **The five team scenarios DO carry TLVs**, so the write **does execute** there. It emits no telemetry and no byte,
  so their streams must stay byte-identical — ⛔ **that is a prediction, not a proof.**
- ⇒ **N1's D2 obligation set, in full:** the s18 md5 **read from `simulation/BASELINE.md`'s `### 36/36 corpus`
  table** (⛔ never hardcoded, ⛔ the table is not edited) · the anchored rows re-run and attributed, with the
  documented pre-existing movers named at their published values · `sizeof(Node)` **re-measured by a compile-only
  template reveal on the native flag set** and the `static_assert` ledger line at `node.h:3388` **extended in place
  with the arithmetic** · a **per-board `RAM_used` diff** on the named envs (native's 8-byte alignment structurally
  hides a 4-byte-align board padding shift) · `TimerWheel::kCap` unchanged · **the TWO-ENV WARNING COMPARISON below.**
- ★★ **THE WARNING CHECK IS A TWO-ENV COMPARISON, ⛔ NOT A CENSUS RUN — OWNER/QG-RULED 2026-08-23 (N1 re-gate),
  AND THE WITHDRAWN WORDING IS KEPT VISIBLE.** This bullet ended *"· `warning_census.sh` at its pins, `-Wswitch`
  zero"*. ⇒ **Compare the warning sets, pre ↔ post, on EXACTLY TWO ENVS:**
  · **`heltec_mobile`** — Xtensa, **full team plane** (`platformio.ini:467`, extends `heltec_v3` + `-DMR_PROFILE_MOBILE`);
  · **`gateway`** — nRF52 / **ARM**, **`MR_FEAT_TEAM=0`** (`:416`, extends `xiao_sx1262` ⇒ `platform = nordicnrf52`,
    plus `gateway_flags`);
  with **`-Wswitch` = 0 on both**, and **no new warning vs the pre-image on either**.
  ★ **THE PAIR IS CHOSEN, NOT ARBITRARY:** it spans **both toolchains** (Xtensa + ARM) and **both team arms**
  (compiled-in + compiled-out), which is exactly the axis a `lib/core` change moves — and N1's cache is
  `MR_FEAT_TEAM`-gated, so `gateway` is the env that proves the **inert** arm.
  ⛔ **NEVER A THIRD ENVIRONMENT** — the owner's standing two-env limit applies here as it does to the board build,
  and ⛔ a brief may not pre-authorise more.
  ⓘ **AND THESE ARE THE SAME TWO THE N1/K1 BRIEF BUILDS** (§0's "exception in KIND, not in count"): the RAM/flash
  diff and the warning comparison are two readings of **one** pair of builds, ⛔ not four builds.
  *Falsifier: the QG N1-gate rulings (findings 2 and 4 of the first round, re-flagged in the re-gate).*

---

## 3. SECURITY / PRIVACY — §3.6.4's trust rules, each mapped to a pin or a named test

| # | rule | where it is enforced | how it is measured |
|---|---|---|---|
| **P-1** | the random 32-bit `team_id` is the **authoritative identity**; ⛔ never name-derived | unchanged — no slice mints or derives an id | N3 pin: the joined `team_id` is **byte-equal** to the observed one; a mutation deriving it from the fingerprint must redden |
| **P-2** | the **id is public**; joining must not imply key possession | `set_team_id` → `team_channel_key_clear()` (`lib/core/node.cpp:683`) | N3 pin: after a nearby join `team_channel_key_present()` is **false**. Metal §7.2 step 6 |
| **P-2b** | ★ **a RETAINED key is ⛔ never silently reactivated by mere knowledge of the public team_id** (keyring ruling) | `/mrcfg`'s new active-binding fact (active team id + `team_key_active`); boot install **only on exact active-team match** | K2 pin: re-joining a previously-known team offers **`SAVED KEY FOUND`** with `BACK` selected and requires an explicit **`USE SAVED KEY`**; a mutation that auto-installs on an id match must redden. Metal §7.5 |
| **P-3** | the **observation path is READ-ONLY** | N1's write site touches **only** the new array; ⛔ it calls no `rt_merge`, `team_key_set`, `id_bind_set`, `peer_key_set` or NV path | ★ the slice's headline controls: three mutations routing the observation into `_rt_team` / `_team_keys` / `id_bind_set`, each RED at match count 1; plus a native case asserting `rt_team_count()`, `_team_keys` occupancy and `team_channel_key_present()` unchanged across N foreign beacons. Metal §7.1 step 5 |
| **P-4** | the scan **transmits nothing** | N1 adds no send site; N2 renders a cache | N2 probe arm: a full NEARBY walk with TX-queue depth and radio starts asserted **zero**. Metal §7.1 step 6 measures it against a STATUS baseline |
| **P-4b** | ★ the **INVITE refresh** transmits nothing either (F-14) | it re-reads `rt_team_at` / `team_key_of_id`, both `const`; ⛔ no scan, no query | N4 probe arm: an INVITE window held open across many refreshes, TX asserted **zero** |
| **P-5** | a label may be shown only from a **trusted PROVENANCE**, ⛔ never **guessed from an id**. ⛔ **REWORDED 2026-08-22 (QG, K1 gate round), WITHDRAWN TEXT KEPT VISIBLE:** this cell read *"from an **authenticated source**"* — ⛔ **overclaims**; see F-15(c) | ⛔⛔ **REWRITTEN IN PLACE 2026-08-22 (F-15), AND THE WITHDRAWN CELL IS KEPT VISIBLE:** it read *"⛔ **no label is shown at all in v1** (F-3) · N2/N4 pin: rows render fingerprint · signal · age and ⛔ nothing name-shaped"*. **That over-applies the rule.** ★ **TEAM-name half unchanged and still absolute:** ⛔ **no TEAM label exists to show** (F-3) — nothing is stored, so nothing is rendered, on either screen. ★ **MEMBER-name half is a LIFECYCLE:** a member's **node name** is **MUTABLE METADATA CACHED ALONGSIDE a verified pubkey** — ⛔ **not itself authenticated** (⛔ **reworded 2026-08-22, QG K1 gate round; the withdrawn phrase was *"is authenticated-by-exchange"***) — and, once cached, **is preferred** (rules 2-3) | N2 pin: a NEARBY row renders the **team** fingerprint · signal · age and ⛔ nothing name-shaped. N4 pins: a candidate row's name column is **blank** until a pubkey lands, then carries the cached name. A mutation substituting `label_for_team_id` (`src/firmware_ui.cpp:359-361`) into a **NEARBY** row must redden |
| **P-5b** | ★ **rule 1 — ⛔ AN ADVERTISER'S NODE NAME IS NEVER PRESENTED AS THE TEAM NAME** | the NEARBY row's identity is the **team fingerprint**, and the pure unit has ⛔ no access to a node-name source for that row | ★ N2's headline privacy control: a mutation that resolves the **beacon sender's** name (via `peer_name_find`) and renders it as the row's team label must **redden**. ⓘ This is a genuinely plausible defect — the sender's hash is in scope at the observation site — which is why it is a control and not a comment. Metal §7.1 step 3 |
| **P-7c** | ★ **rule 4 — the FULL hash stays VISIBLE at the moment of an irreversible act** | the confirmation / detail screen renders the full `0x%08lX` hash beside whatever name is shown | N4/N5/N6 pins: the `REQUEST PUBKEY`, `GRANT KEY` and `REJECT` confirmations each carry the full hash **even when a name is available**; ⛔ a name is never the only identity on those screens. Metal §7.4 |
| **P-7d** | ★ **rule 5 — selection, pubkey requests and key grants are keyed EXCLUSIVELY by the full hash** | the row identity, the `reqpubkey` target and the grant target are all the `key_hash32`; the name is a **render input only** and never reaches a decision | ★ N4/N5/N6 mutations: the selection keyed by the display name · the `reqpubkey` target derived from the name · the grant target derived from the name — each RED at match count 1. ⓘ §B64 discipline, and the name is **mutable** (`node_hashlocate.cpp:346`), so this is not hypothetical |
| **P-6** | ⛔ **`NEW MEMBER`**, never `KEYLESS` | N4's lexeme, declared once | native case pins the exact bytes; ⛔ `grep -R "KEYLESS" src/ test/` hitting this path is a FAIL |
| **P-6b** | ★ a candidate is **NEW only when NEITHER its hash NOR its current id** was in the opening snapshot (F-11) | N4's **two** snapshot authorities | N4 pins: a route-only member whose hash turns authoritative mid-window is ⛔ **not** a candidate; a re-DAD'd member is ⛔ not a candidate; the **double-change** case prompts and is documented in-source as a **safe false prompt** |
| **P-7** | the fingerprint is a **human selection aid**, never authentication | the confirmation carries the **full 32-bit id**; the grant refuses without an authoritative pubkey (`node.cpp:194-196`) | N3 mutations: the id re-derived from the token, or from the cursor index — both RED at match count 1 ([[B48]] shape) |
| **P-7b** | ★ **the pubkey request is EXPLICIT, and the §no-auto-reqpubkey ban survives** (F-12) | a confirmation with `BACK` selected and the action `REQUEST PUBKEY`; the send is the existing `CmdKind::reqpubkey` on `Plane::TEAM` | N5 pins: **no** WANT_PUBKEY is ever emitted without the operator's `double` on `REQUEST PUBKEY`; ★ the mutation that auto-issues it on entering the candidate row is the slice's headline control and must redden |
| **P-8** | ⛔ **no private team key is broadcast or rendered** | sealed inside `team_key_grant_send`; *"the KEY itself is never printed"* (`src/fw_main.cpp:1474`) | N6/K pins: no screen and no probe-captured render contains key material; the receive path refuses `not_sealed` |
| **P-9** | `KEY SENT` is honest; ⛔ `JOIN COMPLETE` is not | ✅ **RULED (F-9):** `queued` ⇒ `GRANT QUEUED`; correlated `send_aired` ⇒ `KEY SENT`; correlated failure ⇒ `GRANT FAILED` | N6 pins: all **eight** `TeamKeyGrantTx` arms driven, each with its own word; ★ **`GRANT QUEUED` may not be `KEY SENT`** (the headline control); ⛔ no arm prints a completion word; `delegated` **fails loudly** from a fake |
| **P-10** | the joiner shows `TEAM KEY RECEIVED` **only after durable adoption** | ✅ **RULED (F-10):** persistence runs **first**; only `saved` forwards the push | K3/K4 pins: a **failed** save ⇒ the panel says `TEAM KEY ACTIVE` / `NOT SAVED — LOST ON REBOOT` and ⛔ never `TEAM KEY RECEIVED`; ★ the four handling-time re-checks each get a mutation |
| **P-11** | expiry closes the UI but ⛔ grants, revokes and rewrites nothing | N4's window is **UI state only**; it calls no core mutator | N4 pin: across open → expire → re-open, `team_channel_key_present()`, `rt_team_count()` and the member set are byte-identical |
| **P-11b** | ★ the **handled set** is volatile and authority-free (F-13) | a per-window set of candidate hashes; discarded on close | N4/N6 pins: `REJECT` changes ⛔ no core, radio, membership, key or NV state **and** the candidate does not return on the next refresh; after the window closes and re-opens, the set is empty |
| **P-12** | ⛔ no unsolicited one-button grant prompt outside a window | the diff runs only while the window arm is live; ⛔ no push navigates | N4 pin: with the window closed, a new member changes ⛔ no screen, cursor or note |
| **P-13** | every state-changing action confirms with the **safe action selected initially** | `ProvConfirm::back` is the **zero value** and every transition primitive re-establishes it (`src/firmware_ui_model.h:361-371`) | N3/N5/N6/K2 pins: the confirmation opens on the safe arm; reaching the act costs `short` then `double` |
| **P-14** | emergency pre-empts everything; an unconfirmed destructive action does not survive it | `provision_reset_on_leave` (`src/firmware_ui_model.h:391-396`) | every UI slice pins it; ✅ and OQ-3's clarification: the **window** survives blanking, the **confirmation** does not |
| **P-15** | ★ **a FULL keyring fails LOUDLY and ⛔ never silently evicts a secret** (keyring ruling) | K1's write policy + K6's explicit removal | K1 pin: a fifth team's key on a full keyring ⇒ **`KEYRING FULL`**, ⛔ zero writes, ⛔ no record replaced; K6 pin: only an operator-confirmed full-id selection may remove an **inactive** record. A mutation that evicts in `put`, deletes ACTIVE, or keys deletion on the short fingerprint must redden — ★ "evict-oldest" remains wrong even though explicit lifecycle management now exists |

---

## 4. SLICING — C1-safe, independently gateable

Each slice: **scope · files · pins · mutation classes · probe/battery impact · pure-vs-renderer-vs-device.**
Every slice ends green and **uncommitted**; ⛔ the user commits (D4).

### 4.0 ORDER — ★ OWNER-RULED 2026-08-22, AND IT REPLACES THE DRAFT'S

⛔ **WITHDRAWN ORDER, KEPT VISIBLE:** the draft ran `S1 → S2 → S3` and `S4 → S5` as two fronts with `S6`/`S7`
trailing. The keyring is now **first**, and the pubkey request is a step of its own.

| # | slice | what it is | corpus | draft name |
|---|---|---|---|---|
| **1** | **K1 + K2** | the `/mrteams` keyring: the store + boot restore, then create/import/leave integrated with the active binding | inert (`src/`) | *(replaces `S6`)* |
| **2** | **N1** | the read-only nearby-team observation cache | ⚠ **RE-RUNS** | `S1` |
| **3** | **N2** | the `JOIN TEAM` child + the read-only NEARBY list | inert | `S2` |
| **4** | **N3** | `JOIN <fingerprint>?` — the confirmed join | inert | `S3` |
| **5** | **N4** | the `INVITE MEMBER` window + the **corrected** two-authority snapshot/diff + the handled set | inert | `S4` (corrected) |
| **6** | **N5** | the explicit **`REQUEST PUBKEY`** step | inert | *(new — F-12)* |
| **7** | **N6** | `GRANT KEY` / `REJECT` + **`send_aired` correlation** | inert | `S5` (corrected) |
| **8** | **K3 + K4** | persistence-FIRST grant receive with the four handling-time re-checks, then the durable `TEAM KEY RECEIVED` note | inert | *(replaces `S7`)* |
| **later** | **K5** | `SAVED KEY FOUND` / `USE SAVED KEY` inside the nearby join | inert | *(new — keyring ruling)* |
| **follow-up** | **K6** | explicit saved-key management: list, protect ACTIVE, confirm `FORGET KEY`, then retry the refused create/grant | inert | *(new — 2026-08-25 retention ruling)* |

ⓘ **Dependencies:** K1 → K2 → K3 → K4 → K5 → K6. N1 → N2 → N3. N4 → N5 → N6. The two families are independent except
that **K3 consumes N6's grant** in the end-to-end metal run, and **K5 consumes N3's join screen**.

---

### K1 — the `/mrteams` KEYRING: the store and boot restore · ★ **LANDS FIRST IN THE ARC**

*Owner-ruled 2026-08-22; register row [[B240]] (`docs/2026-07-30-open-bug-register.md:130`) carries the summary.*

- **Scope.** A NEW indexed NV record and its typed store service. ⛔ **A new NV record is NOT a wire change.**
  ⛔ No UI, no grant handling (that is K3), no join integration (K5).
- **The record**, as ruled:
  `TeamKeyBlob { magic; version; count; records[4] }` ·
  `TeamKeyRecord { uint32_t team_id; uint8_t team_ch_pub[32]; uint8_t team_ch_priv[32]; uint8_t reserved[4] }`
  — **four entries matching the four join profiles**, ≈ **296 B of flash**, and ⛔ **no permanent `Node` RAM growth**
  (the live key already lives in `_team_ch_*`, `node.h:224-227`).
  ★ **BOTH HALVES ARE STORED** so restore **derives pub from priv and rejects corruption/mismatch** — the
  `team_channel_key_adopt` / `adopt_priv` pair already does exactly that derivation (`node.h:246-248`), so the store
  **calls** it rather than re-deriving (U1).
  ⓘ `reserved[4]` is a **NAMED** member, ⛔ never implicit tail padding — the [[AB1]]/`PeerLoc` rule: implicit padding
  is indeterminate after value-initialisation, which makes any whole-record compare unsound, and the
  write-coalescing policy below **is** a whole-record compare.
- **The policy**, as ruled, each clause a pin: `team_id == 0` is **never stored** · **exactly one record per
  `team_id`** · a re-grant / re-key **replaces that team's record atomically** · **identical material writes
  nothing** (the flash-wear guard) · a **FULL keyring fails LOUDLY** and ⛔ **never silently evicts a secret** ·
  **factory reset erases `/mrteams`** · ⛔ **labels do not belong in this record** · **temporary secret buffers are
  wiped** (the `TeamRequest::wipe()` precedent, `src/firmware_provisioning_service.h:94-96`) · **boot install only
  after an exact active-team match**.
- **★ FOLLOW THE `/mrjoin` PRECEDENT, WHICH IS ALREADY MEASURED AND SHIPPED** (U3): the record shape and its three
  ABI static_asserts (`src/device_nv.h:245-263` — magic `'MRJ1'`, **equality** version policy, `sizeof` pinned
  per-ABI *"because `sizeof` IS the migration policy"*); the outcome matrix
  `ProfileVerdict{ ok, unchanged, empty, refused, nv_failed }` (`src/firmware_join_profiles.h:84-90`) whose
  `unchanged ⇒ ★ ZERO writes` **is** the coalescing rule and whose `empty ⇒ an ordinary fresh-device state, ⛔ never
  an error` is the absent arm; and the factory-reset asymmetry already encoded as **data** rather than a forked path
  (`src/device_nv.h:268-270`). ⛔ The keyring gets its **own magic**, never `/mrjoin`'s and never `/mrcfg`'s.
- **Files.** `src/device_nv.h` (the record + the asserts + the slot-table row). NEW pure
  `src/firmware_team_keyring.h` (the typed store service — every decision lives here, because the device TU is
  compiled by neither the native suite nor the simulator). `src/firmware_config.cpp` / `src/fw_main.cpp`
  (**forwards only**: the boot restore call).
- **Pins.** ★★ **(1) — REWRITTEN IN PLACE 2026-08-22 (QG's `KeyringVerdict::empty` NO-PRODUCER finding, K1 gate
  round), AND THE WITHDRAWN PIN IS KEPT VISIBLE:** it read *"(1) Absent record ⇒ **empty, not an error**, zero
  writes."* ⛔ **That pin describes an outcome the resolved contract does not produce**, and it was mirroring
  `/mrjoin`'s `ProfileVerdict::empty` (cited two bullets above) without asking whether the keyring has a **reader**
  that could act on one. It has not: absence means two different things here and each already has a better answer.
  ⇒ **THE ALIGNED CONTRACT, IN TWO ARMS:**
  **(1a) ABSENT ON RESTORE ⇒ `no_record`** — ⛔ **never `empty`** — **zero writes**, and the node comes up **keyless**.
  That is an ordinary fresh-device state, ⛔ never an error (the *substance* of `/mrjoin`'s `empty` survives; only the
  verdict's name and its no-producer shape do not).
  **(1b) ABSENT ON PUT ⇒ the store SEEDS the header and WRITES ONCE, landing `ok`** — ⛔ not `empty`, and ⛔ not two
  writes (a seed-then-put would double the flash cost of every first grant).
  ⓘ **⛔ THE `/mrjoin` PRECEDENT IS FOLLOWED FOR THE MATRIX AND DELIBERATELY *NOT* FOR THIS VERDICT** — the divergence
  is stated here so the spec and the K1 resolution cannot drift apart, and so no later slice "restores" an `empty`
  arm that nothing produces and nothing reads. ⚠ A verdict with no producer is an instrument that cannot fail, which
  is the class this project registers.
  (2) Corrupt / wrong magic / wrong version ⇒
  **refused**, zero writes, and ⛔ `/mrcfg`, identity and the join profiles are untouched. (3) Identical material ⇒
  **`unchanged`, ZERO writes.** (4) A **full** keyring + a fifth team ⇒ **`KEYRING FULL`**, zero writes, ⛔ nothing
  evicted (P-15). (5) `team_id == 0` ⇒ refused. (6) A stored record whose `pub` does not match the derived-from-`priv`
  value ⇒ **rejected on restore**, and the node comes up keyless rather than with a wrong key. (7) Boot restore
  installs **only** on an exact match with the active team binding; a record for another team is ⛔ retained and
  ⛔ not installed. (8) Factory reset erases it.
- **Mutations (new `--target=teamkeyring`).** ★ **ADDED with pin (1):** the restore arm answering an `ok`-shaped
  verdict on an absent record (⇒ a keyless node reads as restored) · **the put arm refusing instead of seeding**
  (⇒ the first grant on a fresh device can never be stored) · **the put arm seeding and writing TWICE**
  (⇒ double flash cost per first grant). Plus: eviction added on full (P-15) · the `unchanged` compare dropped
  (⇒ a write per boot, flash wear) · the version policy relaxed from equality · the pub/priv mismatch check dropped ·
  `team_id == 0` accepted · boot install on a **non**-matching team · the secret buffer left unwiped · two records for
  one `team_id`.
- ⚠ **METAL-ONLY RESIDUE (M2):** a real-flash write, its **power-cut** behaviour and **flash wear** are reachable by
  ⛔ **no automated gate.** The slice **drafts** its own bench part with the exact expected console lines (⛔ the
  slice does not edit the bench script; supervisor-landed after PASS). Precedent: the §UI-15 slice-7 `/mrjoin`
  power-cut and bench Part 20.5.
- **Split.** Pure service: every decision. Device: the two `SlotIo` forwards + the boot call. Renderer: nothing.

### K2 — the ACTIVE BINDING: create / import / leave integrated with the keyring

- **Scope.** `/mrcfg` gains a **small active-binding fact** — the active `team_id` plus `team_key_active` — and the
  three existing paths learn it: **create/import** stores the minted/adopted pair into the keyring and marks it
  active; **`team 0` (leave)** clears `team_key_active` but ⛔ **RETAINS** the indexed key; a **normal reboot
  in-team** restores the matching saved key automatically. ⛔ No UI screen yet (K5 owns `SAVED KEY FOUND`).
- **★ THIS IS THE SLICE THAT MAKES §3.6.4 POINT 4 STILL TRUE.** `set_team_id`'s `team_channel_key_clear()`
  (`lib/core/node.cpp:683`) stays exactly as it is — the **live** key is destroyed on a switch. What changes is that
  the **stored** record survives, and ⛔ **is never reactivated by mere knowledge of the public team_id** (P-2b).
- ⚠ **`/mrcfg` IS A SCHEMA CHANGE ⇒ `mrnv::kVersion` BUMPS** (`src/device_nv.h:125`, currently 23 and carrying its own
  *"REPROVISION-ON-REFLASH"* note). ⛔ **That is an NV version, ⛔ NOT `wire_version`** — no frame moves, nothing
  re-anchors, and M3's *"wire changes are free"* is not being invoked. The slice states the reprovision consequence in
  the record's own comment, as every prior bump does.
- **Pins.** (1) `team new` / import ⇒ the pair is in the keyring **and** active. (2) `team 0` ⇒ `team_key_active`
  false, the live key gone, ★ **the record still there**. (3) Reboot in-team with a matching record ⇒ restored
  automatically. (4) Reboot in-team with **no** matching record ⇒ keyless, ⛔ never a wrong key. (5) ★ Joining a team
  whose key is retained ⇒ ⛔ **nothing is installed** by this slice (K5 adds the explicit offer).
- **Mutations (`teamkeyring`, `config`, `provservice`).** `leave` deleting the record · `leave` leaving
  `team_key_active` true · restore keyed on the record alone rather than on the active binding · the active binding
  written before the durable save returns.

### N1 — CORE: the read-only nearby-team observation cache · ⚠ **THE ONE CORPUS-RERUNNING SLICE**

- **Scope.** §2.1's ordered bounded array, its one write site, its two `const` accessors, its `!MR_FEAT_TEAM` inert
  stubs, and **nothing else**. ⛔ No screen, no console verb, no push, no telemetry, no wire byte, no timer id.
  ★ Per [[meshroute-mark-done-vs-missing-in-code]] the header states **in source** that the cache has no consumer yet
  and names N2 as the one arriving — the `ui_fmt_team_fingerprint` precedent (`src/firmware_ui_chrome.h:204-208`).
- **Files.** `lib/core/node.h` (the record with its **measured** `sizeof`/`offsetof` asserts, the members, the two
  accessors + `#else` stubs, and the `sizeof(Node)` ledger line at `:3388` **extended in place**);
  the upsert body beside `team_key_set` (`lib/core/node_routing.cpp:864`) if the idiom is shared — the implementer
  says which and why; `lib/core/node_beacon.cpp` for the single call at `:577-578`.
  ⛔ `lib/core/frame_codec.*` is **not** touched.
- **Pins.** (1) A foreign-team beacon with a non-zero TLV lands **one** entry with the parsed id, the **EWMA-seeded**
  SNR and its arrival stamp. (2) **De-duplication:** N beacons from M senders of the **same** team leave **one**
  entry. (3) ★ **The SNR is EWMA-updated, ⛔ never max-seen:** a strong sample followed by a run of weak ones **must
  decay** — driven directly, because max-seen passes a single-sample fixture and only fails on the sequence.
  (4) **Retention at the READ:** an entry older than the window is absent from the read. (5) ★ **Order is
  first-observed and STRUCTURAL:** on overflow the **oldest is shifted out and the new one appended**, and a refresh
  of an existing entry ⛔ **does not move it**. (6) ★★★ **READ-ONLY (P-3):** across a stream of foreign beacons,
  `rt_team_count()`, `_team_keys` occupancy, `_team_peer`, `team_channel_key_present()` and `_cfg.team_id` are
  byte-identical. (7) A **zero** `peer_team` records nothing. (8) A beacon failing `parse_beacon` or the
  `wire_version` gate records nothing. (9) **Our own team id** is recorded like any other — the filter is the
  reader's (N2), so each decision lives in one place. (10) `!MR_FEAT_TEAM`: accessors answer `0`/absent, members
  absent.
- **Mutations (native, the `lib/core` suite).** ★ **The read-only controls are the headline, each RED at match count
  1:** the observation additionally calling `team_key_set` · additionally calling `id_bind_set` · additionally merged
  into `_rt_team`. Plus: the `peer_team != 0` gate dropped · the `b.is_mobile` term dropped · the de-dup key changed
  to the sender id · ★ **the EWMA replaced by max-seen** · ★ **the EWMA replaced by last-sample** (no smoothing) ·
  the retention bound `<=`→`<` and the bound itself changed · **a refresh moving the entry to the end** (⇒ the order
  stops being first-observed) · **overflow dropping the NEWEST instead of the oldest** · the write moved above the
  `wire_version` gate · ⛔ **an `MR_EMIT` added on the path** — a control that must be **shown to move the corpus**,
  which is what makes the *absence* of telemetry a measured decision rather than an omission.
- **Gate — the full D2 set of §2.2.**
- **Split.** All core. ⓘ Driven through the existing beacon-injection fixtures; ⛔ no new friend seam.

### N2 — UI: the `JOIN TEAM` child and the read-only NEARBY list

- **Scope.** ONE new `ProvRow` (`join_team`) with its **own** availability parameter; ONE new `Provision` arm; a NEW
  pure unit turning the cache read into rows; the renderer. ⛔ **No join, no confirmation, no act.**
- **Files.** NEW pure `src/firmware_ui_nearby.h` (rows, row **identity**, every lexeme, the own-team filter, the age
  token, and the **tier→`n/3`** rendering). `src/firmware_ui_model.h` (the row enum, the arm, `provision_rows`'s third
  parameter, the landings, the snapshot fields). `src/firmware_ui.cpp` (**forwards only**).
  ⛔ `src/firmware_ui_chrome.h` is **read** (the fingerprint) and **not modified**.

  > ⛔ **CORRECTED IN PLACE 2026-08-23 (N2 as landed, QG-passed), ORIGINAL KEPT VISIBLE.** The ONE pure unit above
  > had to be **TWO**: `src/firmware_ui_chrome.h:36` includes the model, so **no model-included header may include
  > chrome** — and the capture/filter/selection carriers must be model-included (`UiSnapshot` publishes the array,
  > `UiState` freezes it), while the two formatting tokens genuinely need chrome's shared formatters.
  > ⇒ `src/firmware_ui_nearby.h` (model-side: carriers, `nearby_capture` own-team filter, selection, lexemes
  > S-2..S-5) + `src/firmware_ui_nearby_row.h` (downstream: `ui_fmt_nearby_signal` S-7, `ui_fmt_nearby_row` S-6).
  > The same layering as the landed `TeamRow` / `ui_team_row` split. Net effect: two battery targets (`uinearby`,
  > `uinearbyrow`), so the R-4 second-signal-definition control and the own-team-filter control never share a file.
- **The menu shape (✅ OQ-1 ruled).** `provision_rows(create_team, join_static, join_team)` — a **third separate
  parameter**, for the reason the existing two are separate (`src/firmware_ui_model.h:539-547`): a coincidence is not
  a rule, and the native suite must drive the combination the tree cannot build. `provision_has_child` needs ⛔ **no
  change** (`:567-572`).

  > ⛔ **CORRECTED IN PLACE 2026-08-23 (N2 as landed, QG-passed).** "`provision_has_child` needs no change" was
  > wrong: it derives the parent's condition **from the child list**, so the third child means a **third parameter**
  > (`src/firmware_ui_model.h:600`, forwarded at `firmware_ui.cpp:1613` via the hoisted `prov_child`). The intent —
  > parent and children can never disagree — is exactly why it had to move with the list.
- **The signal token (✅ OQ-4 ruled).** `0/3 1/3 2/3 3/3`, **derived from `presence_quality_tier()`**
  (`lib/core/protocol_constants.h:905`) — ⛔ **the UI does not define a second notion of signal quality**, it maps the
  existing tier to four fixed-width ASCII tokens. ⓘ 3 columns; ASCII by ruling, so it survives every font and every
  console transcript.
- **The list is FROZEN PER ENTRY (✅ F-14/OQ-5 ruled).** Read **once**, on the `menu → nearby` transition, into the
  frozen snapshot — the `IUiProvision::profiles()` discipline (`src/firmware_ui_model.h:653-657`: *"CALLED ONCE …
  never per tick or per page"*). ⛔ No auto-refresh; the operator leaves and re-enters to rescan, which is also what
  makes *"the scan never selects by itself"* (§3.6.5) trivially true. Order is the cache's own **first-observed**
  order (structural, §2.1) — ⛔ **never signal-sorted.**
- **The list inherits `join_select`'s shape**, not TEAM's: it opens on its first row, its last row is `BACK`, and
  `BACK` returns to the **PROVISION menu** (`close_provisioning`'s containment, `:2317`).
- **Pins.** (1) PROVISION shows three children on a team-capable mobile and is still hidden when there are none.
  (2) Rows render **fingerprint · `n/3` · age** and ⛔ nothing name-shaped (P-5). (3) **Our own team is filtered out.**
  (4) An empty cache ⇒ `NO TEAMS NEARBY` **and a `BACK` row that still leaves** (`back` is unconditional, `:550-552`).
  (5) `CURRENT PHY ONLY` is on the screen (design `:829-830`) with F-1's honest second line. (6) A row's meaning is
  its **team id**, ⛔ never its index (§B66). (7) Blank/wake retention. (8) `long_arm` arms the alarm; the list is
  intact on return. (9) ★ **Zero traffic** across a full walk. (10) `gateway_heltec` offers no `JOIN TEAM` row.
  (11) ★ All four tier values render, driven directly — ⛔ not a sample.
  ★ **(12) — ADDED 2026-08-22 (F-15 rule 1), AND N2's CONTENT IS OTHERWISE CONFIRMED UNCHANGED BY THAT AMENDMENT:**
  ⛔ **an advertiser's NODE name is never presented as the TEAM name.** A beacon whose sender has a **cached node
  name** still renders a row identified by the **team fingerprint** — driven with the name cache deliberately
  populated, because an empty cache would pass this case for the wrong reason. ⓘ The lifecycle of rules 2-3 belongs
  to the **invite** view (N4/N5); NEARBY has no member in it to name.
- **Mutations (new `--target=uinearby`, plus `model`).** The own-team filter dropped / inverted · the row identified
  by index · the fingerprint re-spelled locally instead of calling the shared helper (the U1 control) · the label
  resolver substituted in (P-5) · the cache re-read per tick · `BACK` leaving the SCREEN · the `join_team` parameter
  folded into `create_team` · the age bound dropped · ★ **the tier mapping re-derived from raw SNR instead of calling
  `presence_quality_tier`** (the second-definition control) · **the list sorted by signal** · ★ **the SENDER's cached
  node name rendered as the row's team label** (P-5b, F-15 rule 1 — the control for the one "improvement" a reviewer's
  reflex would reach for, and the sender's hash **is** in scope at the observation site, so it is plausible rather
  than theoretical).
- **Probe.** A seeded-cache NEARBY arm through the **real renderer** ([[B226]]), the **zero-bus / zero-TX** negative
  arm (the P13f shape), and a PROVISION row census.
- **Battery/probe hygiene.** [[B217]]: read the CURRENT `BASE_CASES`/`BASE_ASSERTS` pin from
  `tools/probe_ui_model_mutations.py` before and after, re-pin **with the derivation written in place**, register the
  new target in `TARGET_SRC` (`:55-81`), and **confirm each battery RAN**. ⚠ Never run a probe and a battery
  concurrently.

### N3 — UI: `JOIN <fingerprint>?` and the act over the existing team transaction

- **Scope.** ONE new `Provision` arm, ONE new `UiProvOp` (`join_team`), ONE new `UiProvOutcome` (`team_joined`) with
  its lexeme, and the adapter arm building `TeamRequest{ mint=false, team_id }`. ⛔ No new seam:
  `IUiProvision::perform` already dispatches `default`-less on `UiProvOp` (`src/firmware_ui_model.h:594-597`).
- **Files.** `src/firmware_ui_model.h`; `src/firmware_ui_prov.h` (the adapter arm, **the same file and the same
  `ITeamCreateDevice` seam** — slice 6's own reason, `:30-36`); `src/firmware_ui.cpp` (rows; forwards only).

  > ⛔ **CORRECTED IN PLACE 2026-08-23 (N3 as landed, QG-passed).** Two reconciliations with the tree:
  > **(a)** the RESULT screen **reuses `Provision::create_result`** rather than adding a second result arm — that
  > is how the Scope's "ONE new `Provision` arm" holds (the one new arm is `nearby_confirm`); the OUTCOME
  > discriminates the words, and both directions of a word swap are mutation-covered.
  > **(b)** a FOURTH file: S-8's `JOIN <fp>?` title lives in `src/firmware_ui_nearby_row.h`
  > (`ui_fmt_nearby_join_title`) — the same layering fact as N2's split (`firmware_ui_chrome.h:36` includes the
  > model, so no model-included header may include chrome, and §B115 forbids composing it in `firmware_ui.cpp`);
  > it buys the title its own `uinearbyrow` battery coverage (Z08/Z09).
- **★ THE PHY PRECONDITION IS REUSED, NOT RE-SPELLED (F-5, U1)** — `live_phy_matches` against the **persisted**
  record with `present = true` (`src/firmware_ui_prov.h:96-108`), while the request's own `ProvPhy` stays
  `present = false` so the transaction preserves the persisted PHY and performs **no retune** (`:78-90`, `:110-115`).
  ⛔ Two `ProvPhy` objects, never assigned to one another.
- **Pins.** (1) `BACK` selected initially; reaching `JOIN` costs `short` then `double`. (2) ★★★ **the act carries the
  FULL 32-bit `team_id` from the row's identity** — ⛔ never the index, ⛔ never re-derived from the token (P-7).
  (3) `BACK` returns to the **NEARBY list**, ⛔ not the menu. (4) `phy_differs` refuses with zero transaction calls,
  writes, airtime and retunes. (5) Success renders **`TEAM JOINED`** — ⛔ never `TEAM CREATED` (F-4) — plus the full
  id and the shared fingerprint. (6) `save_failed` keeps the previous membership and key and says so.
  (7) ★ **After the join the node is KEYLESS** (P-2) and the screen ⛔ never implies readership. (8) Terminal,
  acknowledged by either press, ⛔ no `BACK` row. (9) An unattached seam **fails closed** (`:648-650`).
- **Mutations (`uiprov`, `model`).** The confirmation defaulting to `confirm` · the id from the index · the id
  re-parsed from the token · ★ **`mint` left `true`** (⇒ a "join" that **mints a new team** — the headline control) ·
  `rq.phy.present` set true (⇒ a retune, [[B209]]) · the precondition's `persisted` built with `present = false`
  (⇒ the precondition becomes a no-op that always passes) · `team_joined` mapped onto `created`'s string ·
  `save_failed` rendered as success.

### N4 — UI: the `INVITE MEMBER` window, the TWO-AUTHORITY snapshot/diff, and the handled set

- **Scope.** ONE new `ProvRow` (`invite`, available iff **we are in a team**), the arms, the bounded window, the
  **two** snapshot authorities, the diff, the **volatile handled set**, and the member-hash token. ⛔ **No grant, no
  pubkey request** — this slice can only show candidates.
- **Files.** NEW pure `src/firmware_ui_invite.h` (the deadline + `window_active(now)`, both snapshot authorities, the
  diff, the handled set, the candidate row, the member-hash token, every lexeme). `src/firmware_ui_model.h` (row,
  arms, landings, snapshot fields). `src/firmware_ui.cpp` (**forwards only**).
- **★★ THE DIFF HAS TWO AUTHORITIES (✅ F-11 ruled).** (a) the **authoritative hashes** at opening — survives a
  team-local-id change; (b) a **bitset of team-local ids present at opening** — suppresses an already-present member
  whose hash arrives later. **NEW ⟺ neither the hash nor the current id was in the opening snapshot.**
  ⚠ The **double-change** case (an unkeyed member changing id **and** acquiring a hash in one window) prompts, and is
  **documented in-source as a SAFE FALSE PROMPT** requiring human confirmation — ⛔ it is not engineered away and it
  is not a bug.
- **★ THE CANDIDATE SET IS BUILT THROUGH `team_key_of_id` AT THE AUTHORITATIVE FLOOR (F-7, C2)** — a route-only member
  has no fingerprint and no seal target, so it is ⛔ **not listed as grantable**, while its **id** is still in
  authority (b) so it can never be mislabelled later.
- **★★ THE CANDIDATE ROW, AND ITS NAME LIFECYCLE (F-15 rules 2-3) — ⛔ THE NAME IS AN ADDED COLUMN, NOT A SWAPPED
  TOKEN.** The row is `%c%-6.6s T%-3u %6s` = marker · **name (6, blank until known)** · **team-local id** ·
  **member fingerprint (6)** — width proof `1 + 6 + 1 + 4 + 1 + 6 = 19`, exactly the body budget.
  ★ **THE FINGERPRINT NEVER LEAVES THE ROW.** Rule 2 (*initially the member fingerprint*) and rule 3 (*then prefer the
  cached name*) are therefore ⛔ **not a substitution**: the identity aid the operator learned to read stays put and a
  **blank column fills in**. That is strictly safer than swapping, and it is what lets rule 4 hold at row level too.
  ★ **THE NAME SOURCE IS `Node::peer_name_find` (`lib/core/node.h:1030`) AND NOTHING ELSE (U1)** — the *same* single
  name source the TEAM chain's second step already uses (`src/firmware_ui.cpp:361`). ⛔ **It is NOT
  `label_from_hash`** and ⛔ not `label_for_team_id`: those two fall back to `0x%08lx` (ten columns) and to a bare id,
  which in a 6-column field would render a **truncated `0x` form** — a **third spelling of the hash** beside the full
  id and the fingerprint. ⇒ **one name source, one fingerprint definition (S-13), zero new resolvers.**
  ⓘ **THE 6-COLUMN CLAMP IS DELIBERATE AND MATCHES THE TEAM ROW's `%-6.6s`** (§UI-17 S-11): a member that appears on
  both TEAM and the invite list must not render **two different truncations of one name**.
- **★ THE WINDOW REFRESHES LOCALLY WHILE ACTIVE (✅ F-14 ruled)** — it re-reads `rt_team_at` / `team_key_of_id`, both
  `const`: ⛔ no scan, ⛔ nothing transmitted. **Selection stays identity-based, and opening a confirmation FREEZES the
  selected hash/id** — so a refresh between the two presses cannot move what the operator is about to act on.
- **★ THE WINDOW IS A SEPARATE DEADLINE AND ⛔ MUST NOT WRITE `_last_input_ms`** — the §UI-17 S8 mechanism verbatim
  (that field is written only by a real gesture and the first-tick seed, and it drives the panel blank). ⇒
  `_invite_until_ms` + `window_active(now)` beside the existing `hold_active(now)`, wrap-safe the same way (U3).
  ✅ **OQ-3 ruled: 5 minutes, and it does NOT hold the panel lit** — the panel blanks normally, **the window survives
  the blank**, and an **unfinished confirmation does not** (§1.6).
- **★ THE HANDLED SET (✅ F-13 ruled; ⛔ corrected 2026-08-24 — `REJECT`-ONLY, the queued-grant clause withdrawn
  at §1.7 F-13, see there)** — `REJECT` adds the candidate's **hash**; it changes
  ⛔ no core, radio, membership, key or NV state; it is **discarded when the window closes**.
- **Pins.** (1) The window **expires by itself** and expiry ⛔ grants, revokes and rewrites nothing (P-11).
  (2) A member present at snapshot is ⛔ never a candidate. (3) A member appearing after it is a candidate exactly
  once. (4) ★ A member that **re-ran team-DAD** (same hash, new id) is ⛔ not a candidate. (5) ★ A **route-only**
  member whose hash turns authoritative mid-window is ⛔ **not** a candidate (F-11). (6) ★ The **double-change** case
  **does** prompt, and the case is driven directly so the documented behaviour is measured rather than asserted in
  prose. (7) The candidate word is **`NEW MEMBER`**; ⛔ `KEYLESS` appears nowhere. (8) ★ A `REJECT`ed candidate does
  **not** return on the next refresh, **and** returns after the window is closed and re-opened. (9) The screen shows
  the team's own fingerprint and ⛔ no label. (10) ⛔ **Nothing is transmitted** by opening, holding, refreshing or
  closing the window. (11) With the window closed, a new member changes ⛔ no screen, cursor or note (P-12).
  (12) The row is hidden on a teamless node and on `gateway_heltec`. (13) The window survives blank/wake; an
  unfinished confirmation does not.
  ★ **ADDED 2026-08-22 (F-15):** (14) **rule 2** — a candidate with **no cached name** renders a **blank** name column
  and its **member fingerprint**, and ⛔ the fingerprint column is never empty. (15) **rule 3** — with a name cached
  for that hash, the name column carries it, **clamped to 6**, ⛔ and the fingerprint column is **unchanged**.
  (16) **rule 5** — the row's identity is the **`key_hash32`**; two candidates with the **same cached name** and
  different hashes are two distinct rows and select independently. (17) **rule 4** — every confirmation opened from
  this list carries the **full `0x%08lX` hash**, ⛔ even when a name is shown.
- **Mutations (new `--target=uiinvite`, plus `model`).** ★ **authority (b) dropped** (⇒ the route-only member is
  mislabelled — the correction's own control) · ★ **authority (a) dropped** (⇒ a re-DAD'd member is mislabelled) ·
  the diff keyed by `last_seen_ms` · the snapshot taken at first **render** instead of at window **open** · the
  snapshot never taken · the authoritative floor lowered to `claimed` · the expiry granting/clearing · the window
  writing `_last_input_ms` · `NEW MEMBER` re-spelled as `KEYLESS` · the candidate list produced while closed ·
  ★ **the handled set dropped** (⇒ a rejected candidate returns on the next refresh) · ★ **the handled set made
  persistent** (⇒ it survives the window, which the ruling forbids) · the confirmation **not** freezing the selection
  (⇒ a refresh moves the target between the two presses) ·
  ★ **ADDED 2026-08-22 (F-15):** the row **keyed by the display name** instead of the hash (P-7d — RED at match count
  1; the name is **mutable**, `node_hashlocate.cpp:346`) · the name column **replacing** the fingerprint instead of
  filling beside it (⇒ the identity aid vanishes the moment a name arrives) · the name resolved through
  `label_from_hash` and clamped (⇒ the truncated-`0x` third spelling) · the name rendered **without** the 6-column
  clamp (⇒ a long name pushes the id and the fingerprint off the row) · the confirmation dropping the **full hash**
  once a name is available (P-7c).
- **Probe.** An INVITE arm with a seeded member set asserting the exact candidate rows; a window-expiry arm; a
  held-open-across-refreshes **zero-TX** arm (P-4b).

### N5 — UI: the explicit `REQUEST PUBKEY` step *(new — F-12)*

- **Scope.** The `no_pubkey` landing becomes its own confirmation: **`NEED PUBKEY`** with **`BACK` selected** and the
  action **`REQUEST PUBKEY`**; confirming issues the **existing** `CmdKind::reqpubkey` on **`Plane::TEAM`** for the
  candidate's hash; the screen then shows **`WAITING FOR PUBKEY`** until a **`peer_key_cached`** for that hash
  enables `GRANT KEY`. ⛔ **No new verb, no new frame, no new wire byte, no automatic escalation.**
- **★★★ IT PRESERVES §no-auto-reqpubkey RATHER THAN REVERSING IT.** The ban (`lib/core/node.cpp:185-196`,
  owner-ratified 2026-07-29, canonical home `Node::send_by_hash`'s header) forbids the grant **silently** escalating
  to a WANT_PUBKEY. Here the **operator** authorises the on-air identity request with a deliberate `short` + `double`
  — which is exactly what typing `reqpubkey <hash> -t` at the console is, and the console is the remedy the ban's own
  text names. ⛔ A slice that auto-issues it on entering the row **is** reversing the ruling.
- **⚠ THE LEXEMES ARE RULED AND ONE IS FORBIDDEN:** `NEED PUBKEY` / `REQUEST PUBKEY` / `WAITING FOR PUBKEY`, and
  ⛔ **never `WAITING FOR KEY`** — it is ambiguous between the recipient's **pubkey** and the team **content key**,
  which are the two different secrets this very screen sits between.
- **Files.** `src/firmware_ui_invite.h` (the arm + the words, pure); `src/firmware_ui_model.h` (the arm);
  a device forward for the command (⛔ the decision is not in the device TU).

  > ★ **CLARIFIED 2026-08-24 (N5 coder's boundary question, supervisor-ruled from the tree — how the
  > `no_pubkey` landing is REACHED before N6's grant act exists):** by a **side-effect-free PREFLIGHT**, ⛔ never
  > by attempting the grant (a probe-by-attempting call is the shape that becomes an accidental act, and it
  > overlaps N6). **And the preflight is NOT a new rule — it is the grant's own bar, REUSED (U1):**
  > `Node::team_key_grant_send`'s `no_pubkey` arm (`lib/core/node.cpp:194-196`) is exactly
  > `peer_key_find(target_hash, ed, &conf)` **and** `conf >= PeerKeyConf::authoritative` (deliberately
  > `e2e_seal_inner`'s bar — authoritative OR pinned). The preflight calls the SAME existing accessor
  > (`node.h:1071` — **read-only behaviorally but ⛔ NOT C++ `const`-qualified**, corrected 2026-08-24: QG) at
  > the SAME floor through a device forward, cites the grant's arm in-source ("if that bar moves, this
  > moves with it"), and only the boolean reaches the model — ⛔ no key byte is published (the `ed` out-buffer is
  > public material, discarded). Candidate confirmation: preflight says missing/below-floor ⇒ the `NEED PUBKEY`
  > confirmation; present-at-floor ⇒ `GRANT KEY` **enabled** (the act itself stays N6's). ⓘ `peer_key_find`
  > AGES — an expired key preflights exactly as the grant would refuse it, which is the point of reusing the
  > accessor. **Added mutation (this clarification's own control): the preflight's floor lowered to `overheard`**
  > (⇒ `GRANT KEY` enabled for a spoofable key) — RED at match count 1. When N6 lands, one equivalence case
  > drives the preflight and the real grant against one fixture at `authoritative` and one notch below it, so
  > the two sites can never silently disagree.
- **Pins.** (1) ★ **No WANT_PUBKEY is emitted without the operator's `double` on `REQUEST PUBKEY`** — driven by
  counting emitted commands, ⛔ not by reading a screen. (2) `BACK` is selected initially and `BACK` emits nothing.
  (3) The request is **team-scoped** (`Plane::TEAM`), matching the plane the grant will fly on. (4) A
  `peer_key_cached` **for that hash** enables `GRANT KEY`; one for a **different** hash ⛔ does not. (5) A timeout
  leaves `WAITING FOR PUBKEY` and ⛔ grants nothing. (6) ⛔ The word `WAITING FOR KEY` appears nowhere in the tree on
  this path.
  ★ **ADDED 2026-08-22 (F-15 rule 3) — THE NAME ARRIVES ON THIS EXCHANGE, AND THAT IS WHY THE RULE ATTACHES HERE:**
  (7) after the request **succeeds for that hash**, the candidate's row **fills its name column** from the cache —
  because the answer carried the name (`node_hashlocate.cpp:1251`) and `peer_key_set` cached it beside the key
  (`:1142`, `:377`). ⛔ **No extra lookup, no second request, no new field** — the slice renders what the exchange
  already stored. (8) A request that **fails or times out** leaves the name column **blank** and the fingerprint
  intact. (9) ⛔ **The name never gates anything:** `GRANT KEY` is enabled by the **key**, ⛔ never by the presence of
  a name. (10) The confirmation carries the **full hash** throughout, name or no name (P-7c).
- **Mutations (`uiinvite`, `model`).** ★ **the request auto-issued on entering the row** (the headline control — it
  is the ruling-reversal shape) · the confirmation defaulting to `REQUEST PUBKEY` · the plane changed to
  `GLOBAL`/`AUTO` · **any** `peer_key_cached` enabling the grant (hash not compared) · the timeout enabling the grant ·
  ★ **ADDED 2026-08-22 (F-15):** the **request target derived from the display name** rather than the hash (P-7d) ·
  the name column filled from **any** `peer_key_cached` rather than the one for that hash (⇒ one member wearing
  another's name — the worst shape this lifecycle can take) · **the presence of a name enabling `GRANT KEY`** (⇒ a
  describe-only field making an airtime-and-secret decision, the [[B48]] class).

### N6 — UI: `GRANT KEY` / `REJECT` with `send_aired` correlation

- **Scope.** The two actions with **`REJECT` selected initially**, the confirmation, ONE adapter forward to
  `Node::team_key_grant_send` **with `Plane::TEAM`**, and the **`send_aired` correlation**. ⛔ No new send path, no new
  payload, no new frame type, no wire byte.
- **★★★ THE OUTCOME MAPPING IS THE WHOLE SLICE, IT IS PURE, AND IT IS CORRECTED (✅ F-9 ruled).**
  ⛔ **WITHDRAWN, KEPT VISIBLE:** *"`ctr != 0` = airborne ⇒ `KEY SENT`"*.
  **`queued` with `ctr != 0` ⇒ `GRANT QUEUED`** (admitted to the queue, nothing has left the radio) ·
  **a CORRELATED `PushKind::send_aired{dst, ctr}` ⇒ `KEY SENT`** (`lib/core/command.h:246` — the SX1262 TxDone edge
  for *this* flight) · **a correlated failure ⇒ `GRANT FAILED`** · **`queued` with `ctr == 0` ⇒ PARKED behind an H
  resolve** (`src/firmware_config.cpp:1370-1371`) and says so in its own words · ⛔ **no e2e ack exists on a grant**
  (`lib/core/node_mac_rx.cpp:1723`) ⇒ ⛔ **never `JOIN COMPLETE`, never `KEY RECEIVED` here.**
  ⓘ **`delegated` is unreachable on the real seam** (the UI always sends `Plane::TEAM`) — ⛔ **but the pure mapper
  FAILS LOUDLY if a fake returns it**, and that arm is driven, because an unreachable arm returning a plausible word
  is the arm that lies the day it becomes reachable (C2).
  ★ **The correlation is `{dst, ctr}` and both terms are load-bearing** — a `ctr` alone is a **local** handle and the
  same value legitimately names another flight (`command.h`'s own warning on `channel_sent`'s `ctr`).

  > ⛔⛔ **CORRECTED 2026-08-24 (N6 first-gate QG, owner-relayed; the `ctr`-INFERENCE HALF ABOVE IS WITHDRAWN,
  > KEPT VISIBLE): `queued`/`ctr` infer MORE than the core guarantees, so either word could be FALSE.** Measured:
  > a full TX queue **silently drops the frame but still returns a non-zero counter** (`lib/core/node_mac.cpp:340`);
  > a full parked-send ring **stores nothing** (`node_hashlocate.cpp:1891`); yet `team_key_grant_send()` returns
  > `queued` in every one of those cases (`node.cpp:231`). ⇒ **RULED: the grant returns an EXPLICIT dispatch
  > result** — *actually queued* · *actually parked (stored)* · *a distinct admission refusal* (`GRANT QUEUE
  > FULL`) — **and the actually-RESOLVED destination together with the counter** (the UI's frozen roster id can
  > be stale across a re-DAD between selection and send, `node_hashlocate.cpp:1605` resolves live ⇒ the
  > correlation must carry the send-time `dst`, not the selection-time one). **`GRANT PARKED` may be shown ONLY
  > for an explicitly-stored parked outcome** — ⛔ never inferred from `ctr == 0` (owner ruling on the lexeme).
  > The corrective slice is `lib/core`-touching (return-value plumbing of already-computed facts — behaviour-inert
  > on the wire, argued AND corpus-proven) + the UI mapping consuming it; new tests: full TX queue, full parked
  > ring, re-DAD between selection and send.
- **Files.** `src/firmware_ui_invite.h` (the mapping + the correlation rule, **pure**, so all eight arms and both
  push outcomes are natively drivable); `src/firmware_ui_model.h` (the arms + the op); a device forward;
  `src/firmware_ui_send.h` (the `send_aired` arm — it is already the ONE pure push router reached from the single
  device entry point, and it is compiled by the native suite).
- **Pins.** (1) `REJECT` is selected initially; reaching `GRANT KEY` costs `short` then `double`. (2) `REJECT` sends
  nothing and changes no core/radio/membership/key/NV state — ★ **and adds the hash to the handled set** so the
  candidate does not return on the next refresh (F-13). (3) All **eight** arms driven, each with its own word.
  (4) ★ **`GRANT QUEUED` is not `KEY SENT`** — the headline control. (5) `KEY SENT` requires a **correlated**
  `send_aired`; an **uncorrelated** one (different `dst`, or different `ctr`) ⛔ does not promote it. (6) ⛔ No arm
  prints a completion word. (7) ⛔ No key material reaches any screen (P-8). (8) The grant is unreachable with the
  window closed. (9) Terminal, acknowledged by either press.
  ★ **ADDED 2026-08-22 (F-15 rules 4-5):** (10) the `GRANT KEY` and `REJECT` confirmations carry the **full
  `0x%08lX` hash**, ⛔ **even when a name is shown** — a name is never the only identity at the moment a private key
  is shipped (P-7c). (11) the **grant target is the `key_hash32`** and ⛔ never the display name; a member whose name
  changes between the row and the confirmation is still granted **the same key** (P-7d).
- **Mutations (`uiinvite`, `uisend`, `model`).** ★ **`queued` mapped straight to `KEY SENT`** (the F-9 defect
  restored — the headline control) · the correlation dropped to `ctr` alone · the correlation dropped entirely
  (⇒ any `send_aired` promotes) · the eight arms collapsed to `ok`/`failed` · `delegated` returning a plausible word
  instead of failing loudly · `REJECT` calling the send · `REJECT` not adding to the handled set · any arm printing a
  completion word · the plane changed away from `Plane::TEAM` (⇒ `delegated` becomes reachable) ·
  ★ **ADDED 2026-08-22 (F-15):** ★ **the grant target taken from the display name** (P-7d — RED at match count 1) ·
  the confirmation **dropping the full hash** when a name is present (P-7c) · the handled set keyed by the **name**
  rather than the hash (⇒ rejecting one member silences another that shares its name).
- **Probe.** An INVITE→GRANT arm against a **fake** grant device returning each of the eight outcomes plus both push
  outcomes, asserting the exact panel line for each; ⛔ the fake is the seam, never a hard-wired string.

### K3 — persistence-FIRST grant receive, with the four handling-time re-checks *(replaces the draft's `S6`+`S7` order)*

- **★★★ THE ORDERING RULING (✅ F-10).** `mr_ui_on_push(pu)` is the **first** statement inside the drain loop
  (`src/fw_main.cpp:1310`), so the draft's *"gate S7 on S6"* was not expressible. ⇒ **for `team_key_received`, a
  config-service persistence function runs FIRST, and only a `saved` return forwards the push to `mr_ui_on_push`.**
  On failure the UI ⛔ **never** shows `TEAM KEY RECEIVED`, and the failure is reported **explicitly** as RAM-only /
  lost-on-reboot.
- **★★ THE FOUR HANDLING-TIME RE-CHECKS, AND THEY CLOSE A REAL RACE.** A push is drained some time after RX, and
  membership can change in between. The function re-checks, at handling time: **`pu.team_id != 0`** ·
  **`pu.team_id == g_node.config().team_id`** · **the live key is present** · **the loaded blob belongs to the same
  team**. ⛔ Any one failing ⇒ nothing is written and the UI is not told a key was adopted.
- **Scope.** `src/` only ⇒ corpus inert by construction. Order: **persist {team_id, key} FIRST → mark active →
  then the note (K4).**
- **Files.** `src/firmware_team_keyring.h` (the decision — pure); `src/firmware_config.cpp` (the forward);
  `src/fw_main.cpp` (**the ONE structural change**: the `team_key_received` push is routed through the persistence
  function before `mr_ui_on_push`). ⚠ **`fw_main` is board/runtime glue (U3)** — it gains a call, ⛔ not a decision.
- **⚠ `§notify-every-save`, MEASURED RATHER THAN OMITTED.** The rule (`src/firmware_config.cpp:694-706`) is that every
  **user-initiated** verb notifies after a successful write while **internal** writers stay silent. A grant receipt is
  ⛔ **not** user-initiated on this node and assigns **none** of the four covered `/mrcfg` fields ⇒ **it stays
  silent**, and the slice **records that beside the existing exemption list** so no future reader treats it as an
  oversight. ⓘ K2's active-binding write is a different question and its own slice answered it.
- **Pins.** (1) A successful persist ⇒ the push is forwarded. (2) A **failed** persist ⇒ ⛔ the push is **not**
  forwarded and the failure is surfaced. (3) Each of the four re-checks fails the write on its own — four cases, four
  mutations. (4) A **re-grant** replaces that team's record atomically and idempotently. (5) A grant for a
  **different** team is refused upstream (`team_mismatch`, `lib/core/node.cpp:286` — ⛔ corrected 2026-08-25:
  this read `:258`, which had drifted; verified at implementation) and writes nothing.
  (6) Identical material ⇒ **zero writes**.
- **Mutations (`teamkeyring`, `config`).** The push forwarded before the persist (⇒ F-10 restored — the headline
  control) · the push forwarded on a **failed** persist · each of the four re-checks dropped · the active mark
  written before the durable save returns.

### K4 — the durable `TEAM KEY RECEIVED` note *(replaces the draft's `S7`)*

- **Scope.** ONE arm in the pure push router `mrui::ui_route_recv_push` (`src/firmware_ui_send.h`) plus the transient
  note — reached **only** by a push K3 forwarded, so the words are true by construction rather than by a gate a
  reviewer must trust.
- **The failure wording, ruled:** a save that failed shows **`TEAM KEY ACTIVE`** / **`NOT SAVED — LOST ON REBOOT`** —
  ⛔ never `TEAM KEY RECEIVED`. ★ The key genuinely **is** live in RAM, so the panel says both true things: it works
  now, and it will not survive a reboot.
- **⛔ A PUSH NEVER NAVIGATES:** the note lights nothing, opens nothing, moves no cursor, switches no screen.
  ⓘ Whether it should **wake** a dark panel is ⛔ **not this spec's to decide** — §UI-17 R-7 scoped the wake to a DM
  addressed to us and a **sealed** channel post, and widening it is a new owner ruling. ⇒ **it does not wake** in v1,
  stated so the omission is a decision.
- **Pins.** (1) The words appear only on a K3-forwarded push. (2) The failure wording appears on the failure path.
  (3) ⛔ No navigation, no cursor move, no emergency field write, no wake. (4) The granter's optional `name=` is
  ⛔ **not** rendered as a team label (F-3, P-5). (5) Every other `PushKind` renders nothing here (drive the full
  enum, ⛔ not a sample).
- **Mutations (`uisend`, `model`).** The words rendered off a raw push · the failure path rendering success · the arm
  navigating · the arm waking the panel · the `name=` rendered as a label.

### K5 (LANDED) — `SAVED KEY FOUND` / `USE SAVED KEY` inside the nearby join

*Originally owner-sequenced **later** in the keyring arc; now landed. The historical sequencing remains stated so no
earlier slice is rewritten as though it had always included activation.*

- **Scope.** When N3's join targets a team whose key is **retained** in the keyring, the flow offers **`SAVED KEY
  FOUND`** with **`BACK` selected** and an explicit **`USE SAVED KEY`**. ⛔ **Nothing is installed by mere knowledge
  of the public team id** (P-2b) — that is the whole reason this is a screen and not a rule.
- ⛔ **N3 did NOT anticipate it:** before K5 landed, a nearby join of a previously-known team left the node
  **keyless**, exactly as §3.6.4 point 4 requires, and the retained record stayed untouched.
- ⛔ **WITHDRAWN, KEPT VISIBLE:** this section ended with *"A future explicit `FORGET KEY` removes a record; it is
  not in this spec."* Repeated real-team creation filled the four records and proved that retention without a
  removal path is not an operable lifecycle. The owner ruled K6 below on 2026-08-25: removal is now explicit,
  confirmed and limited to an inactive record; P-15's ban on silent eviction remains unchanged.

### K6 (FOLLOW-UP) — explicit saved-key lifecycle / `FORGET KEY`

*Owner-ruled 2026-08-25 after metal testing filled all four `/mrteams` records. This is a PRODUCT/UX completion of
the keyring lifecycle, not a correction to K1: K1's loud refusal and no-silent-eviction policy remain correct.*

- **Problem.** Re-keying the **same** `team_id` already replaces its record in place and consumes no new slot.
  Repeated `team new`, however, creates distinct random team ids; each retained key consumes one of the four slots.
  The fifth correctly reaches **`KEYRING FULL`**, but today the operator has no safe way to free a deliberately
  retained key. ⛔ Do not describe this as cryptographic key rotation: it is **saved-key retention management**.
- **Policy.** Keep the fixed four-record bound. ⛔ Do not grow the record and ⛔ do not add an automatic FIFO/LRU
  policy. A full store still performs **zero writes and zero eviction** until the operator chooses a specific
  inactive record and confirms `FORGET KEY`. The **active key is protected and cannot be removed**. A key received
  asynchronously never evicts anything to make room.
- **Two explicit transactions, never one disguised transaction.** Removing a key completes first and reports its
  own verdict. Team creation or a renewed grant is then retried. ⛔ `team new` must never delete a record as a side
  effect: `/mrteams` and `/mrcfg` are separate durable records, so "evict then create" cannot be one atomic commit;
  hiding both behind one action would allow a failed create to destroy an unrelated saved key.
- **Pure service.** Add a metadata-only enumeration (`team_id`, active marker; ⛔ never key bytes) and a typed
  `forget(team_id, binding)` operation. The operation: refuse id 0 · fail closed on invalid/unreadable storage ·
  return not-found with zero writes · refuse the active binding with zero writes · remove exactly the selected
  record · compact deterministically · wipe the vacated record · save exactly once. A failed save is reported as a
  failed save; it may not be described as "nothing changed" because the real backend's power-cut outcome is M2.
- **Console.** `team keys` lists the retained team ids/fingerprints and marks the active one, revealing no key
  material. `team forgetkey 0x<team-id> confirm` removes one inactive record. Omitting `confirm`, targeting the
  active key, using id 0, or naming an absent record performs zero writes and fails loudly. `team exportkey` is
  unchanged and is not reused as the list operation.
- **OLED.** PROVISION gains a **`SAVED KEYS`** child. Its list uses the shared team fingerprint helper; the active
  row is visibly marked **`ACTIVE`**. Selection is carried by the full 32-bit `team_id`, never by the six-hex-digit
  display token or by a mutable row index. The irreversible confirmation shows the **full team id**, opens with
  `BACK` selected, and offers `FORGET KEY` only for an inactive row. An active row lands on **`ACTIVE KEY` /
  `CANNOT FORGET`** with no destructive action. An empty list says **`NO SAVED KEYS`**. Successful removal says
  **`KEY FORGOTTEN`** and returns to the refreshed list; a storage failure stays visible and offers no false success.
- **Full-store direction.** A `KEYRING FULL` result does not choose a victim. Its acknowledgement enters the saved
  key list, where `BACK` remains safe and the operator makes the separate selection and confirmation. After a
  successful forget, the original create/grant is ⛔ not replayed automatically; the operator retries it.
- **Identity and secret hygiene.** List rows may abbreviate with the shared fingerprint, but the confirmation must
  carry the full id. Names are metadata and never deletion identity. No key byte reaches the model, renderer,
  console list, outcome token, telemetry or mutation output. Every temporary secret-bearing blob retains K1's wipe
  guard on every return.
- **Pins.** (1) Full + unconfirmed ⇒ zero writes and no eviction. (2) Active target ⇒ zero writes and live key,
  binding, membership and all records unchanged. (3) Inactive target ⇒ exactly one save, the selected id absent,
  every other record byte-identical and the vacated tail zero. (4) Not-found / zero / unreadable ⇒ zero writes.
  (5) Save failure never renders `KEY FORGOTTEN`. (6) The list exposes ids/status only, no material. (7) A short
  fingerprint collision cannot delete the wrong record. (8) `KEYRING FULL` acknowledgement opens management but
  never deletes or retries by itself. (9) Re-keying one existing team remains an in-place replace and never invokes
  K6.
- **Mutation classes.** Silent oldest-record eviction in `put` · active-key deletion accepted · confirmation
  bypassed · delete keyed on the fingerprint/cursor · compaction leaves a duplicate or secret-bearing tail · save
  failure rendered as success · create automatically resumed · list returns key material · missing-store and
  unreadable-store collapsed.
- **Files / boundary.** `src/firmware_team_keyring.h` (pure typed policy), its device forwards in
  `src/firmware_config.cpp`, UI model/renderer/provisioning adapter, native tests and the existing batteries/probes.
  ⛔ No `lib/core`, frame, timer, routing or simulator change. *(Amended 2026-08-25, QG: round 2's typed
  `keyring_full` carrier required the one permitted HAL site — `lib/hal/mr_ui.h:117`, the hook's parameter —
  per §0's amended boundary; `lib/hal` is not compiled into `lus`, s18 exact.)*

### K7 — the roster grant: per-member `GRANT KEY` from the TEAM screen *(owner-ruled 2026-08-25, [[B245]])*

*Born on the bench: H1 creates, H2 joins via NEARBY **before** H1 opens the invite window ⇒ H2 is in the
window's opening snapshot and (N4 pin 2, correctly) can never be a candidate — the panel has no grant path.
Option 1 ruled over widening the F-11 diff (a proxy wrong in both directions) and over snapshot-at-creation
(persistent state, against F-13).*

- **Scope.** An operator-initiated per-member act from the **TEAM screen's roster**: enter TEAM, select a
  member, and an explicit act opens **the landed N5/N6 chain VERBATIM** — the pubkey preflight
  (`invite_grant_preflight`, the grant's own bar) → `NEED PUBKEY` / `REQUEST PUBKEY` / `WAITING FOR PUBKEY`
  (N5's screens, S-18..S-21) → the `GRANT KEY` confirmation (safe default, full `0x%08lX` hash even when
  named — P-7c/P-7d) → the dispatch-truth outcome mapping and `{dst, ctr}` correlation (N6's, S-21..S-24,
  S-37/S-38). ⛔ **No new screen, no new lexeme, no new send path, no new state machine** — this slice is an
  ENTRY POINT to machinery that exists; if any piece cannot be reached verbatim, STOP and report.
- **★ The rulings it must NOT disturb:** the invite window's F-11 diff and F-13 handled set stay byte-for-byte
  (the window remains the watch-for-new-joiners flow); **P-12 stays whole** — nothing here is unsolicited: the
  operator navigates to a member and acts. The TEAM screen's landed passive/entered contract (§UI-17) governs
  where the act hangs; reconcile with the entered-TEAM selection model as built, and report the placement as a
  design decision (the reported-not-assumed precedent).
- **★ The grant target is the roster row's `key_hash32`** (the TEAM chain's own resolution, one lookup per row
  — §UI-17 S5's rule), ⛔ never the display name, ⛔ never the row index; a member whose name or team-local id
  changes between selection and confirmation is still granted the same key (P-7d; the N6b send-time-dst
  correlation already covers the id half).
- ⛔ **No self-grant** (the grant's own `self` arm refuses — drive it from here too). ⛔ A keyless node offers
  no grant act (the `no_key` arm — but prefer the act hidden/absent when `team_channel_key_present()` is
  false, stated as a design decision either way).
- **Pins.** (1) The act exists on an entered-TEAM member row and opens the N5/N6 chain with the row's full
  hash. (2) The early-joiner scenario end-to-end: a member present BEFORE any window opened is grantable here
  (the B245 repro, now green). (3) The invite window's behaviour is byte-identical (its cases re-run
  untouched). (4) Nothing transmits without the operator's explicit confirmations (the N5 command-count idiom
  re-proven from this entry). (5) The self row refuses/offers nothing. (6) A keyless node offers nothing.
  (7) P-7c/P-7d through this entry (full hash shown; target = hash). (8) The outcome words are N6's exactly —
  ⛔ no new word.
- **Mutation classes.** The act auto-issuing on row selection (the no-unsolicited shape) · the target from the
  display name / row index · the self refusal dropped · the keyless offer appearing · a second outcome mapping
  forked (anchor on the reused call) · the invite window's diff disturbed (its landed controls must stay RED).
- **Files / boundary.** UI model (the TEAM-screen act + arms) · renderer forwards · reuse of the landed
  invite/prov adapters. ⛔ No `lib/` of any kind, no wire, no NV.

---

## 5. TEST / MUTATION / PROBE PLAN, per slice

| slice | native suites | battery targets | probe phases |
|---|---|---|---|
| K1 | new `test/test_firmware_team_keyring.cpp` | **new `teamkeyring`**, `devicenv` | ⛔ none (headless); the flash half is **metal-only** (M2) |
| K2 | `test/test_firmware_team_keyring.cpp`, `test/test_firmware_config_service.cpp` | `teamkeyring`, `config`, `provservice` | ⛔ none |
| N1 | the `lib/core` beacon/team suites | native mutations (no per-file battery target exists for `lib/core`; the controls are driven as native mutations at match count 1 and **reported individually**) | ⛔ none (headless core) |
| N2 | new `test/test_firmware_ui_nearby.cpp` + `test/test_firmware_ui_model.cpp` | **new `uinearby`**, `model` | seeded-cache NEARBY arm (real renderer); PROVISION row census; zero-bus/zero-TX arm |
| N3 | `test/test_firmware_ui_prov.cpp` + `test/test_firmware_ui_model.cpp` | `uiprov`, `model` | confirm/refuse arm; `PHY DIFFERS` arm; `BACK`-performs-nothing arm |
| N4 | new `test/test_firmware_ui_invite.cpp` + `test/test_firmware_ui_model.cpp` | **new `uiinvite`**, `model` | seeded-member INVITE arm; window-expiry arm; held-open zero-TX arm |
| N5 | `test/test_firmware_ui_invite.cpp` | `uiinvite`, `model` | a command-counting arm (⛔ zero WANT_PUBKEY without the confirmation) |
| N6 | `test/test_firmware_ui_invite.cpp` + `test/test_firmware_ui_send.cpp` | `uiinvite`, **`uisend`**, `model` | eight-outcome + two-push GRANT arm against the fake seam |
| K3 | `test/test_firmware_team_keyring.cpp` | `teamkeyring`, `config` | ⛔ none; the real-flash half is **metal-only** |
| K4 | `test/test_firmware_ui_send.cpp` | `uisend`, `model` | a push arm asserting the note and the **absence** of navigation/wake |
| K5 | `test/test_firmware_team_keyring.cpp`, `test/test_firmware_ui_prov.cpp`, `test/test_firmware_ui_model.cpp` | `teamkeyring`, `uiprov`, `model` | saved-key offer + explicit activation arms |
| K6 | `test/test_firmware_team_keyring.cpp`, console/parser tests, `test/test_firmware_ui_model.cpp` | `teamkeyring`, `uiprov`, `model` | full-store → management; list, active refusal, full-id confirmation and successful refresh |

**Standing rules for every slice.**
- **[[B217]] re-pin duty.** The runner aborts with `sys.exit(2)` and **applies zero mutations** when the clean
  baseline does not match its pinned `BASE_CASES` / `BASE_ASSERTS`. ⇒ **read the pin from
  `tools/probe_ui_model_mutations.py` at dispatch time; ⛔ never hardcode or carry a number from a document** (this
  spec deliberately states none). Re-pin **with the derivation written in place**, and **prove each battery RAN**.
- Every new battery target is registered in `TARGET_SRC` (`tools/probe_ui_model_mutations.py:55-81`) — a battery is
  **per-source-file**.
- ⚠ **Never run a probe and a battery concurrently** — the runner serialises on `.pio/build/native`.
- New pure files are **headers**, so board builds inherit them; a new `test_*.cpp` joins the native suite.
- ⛔ **Probe check labels stay at or under 64 characters** ([[B229]]).
- **Gate before "ready" (D1/D3):** `pio test -e native` **then run** `./.pio/build/native/program` (the wrapper
  misreports *"0 test cases"*) · the s18 md5 **read from `simulation/BASELINE.md`** — inert by construction for every
  slice except **N1**, which carries §2.2's full set · the two board envs, sequentially · **the TWO-ENV WARNING
  COMPARISON** · `git diff -- lib/` **empty for every slice except N1**. Report failures with output; say what was
  skipped.
- ★★ **THE TWO-ENV WARNING COMPARISON — OWNER/QG-RULED 2026-08-23 (N1 re-gate), AND THE WITHDRAWN WORDING IS KEPT
  VISIBLE.** The gate line above read *"· `warning_census.sh` at its pins, `-Wswitch` zero"*. ⇒ **compare the warning
  sets pre ↔ post on EXACTLY TWO ENVS — `heltec_mobile` (Xtensa, full team plane) and `gateway` (nRF52/ARM,
  `MR_FEAT_TEAM=0`) — with `-Wswitch` = 0 on BOTH and no new warning vs the pre-image on either.**
  ⛔ **NEVER A THIRD ENVIRONMENT** (the owner's standing two-env limit), and ⛔ a brief may not pre-authorise one.
  ⓘ These are the **same two builds** the slice already makes for the board gate — one pair of builds, two readings.
  ⓘ The pair spans both toolchains and both team arms, which is why it needs no third.
  *Falsifier: the QG N1-gate rulings (findings 2 and 4 of the first round, re-flagged in the re-gate).*
  ⛔ **ONE SWEEP, NO LEFTOVER:** these are the **only two** sites in this spec that ever required a census run; both
  are corrected here and at §2.2, and ⛔ no slice may reintroduce one. ⓘ The phrase *"PROVISION row census"* in §4-N2
  and §5 is a **probe phase name** (a row-inventory assertion) and is ⛔ unrelated to warning counting — it is left
  exactly as it stands.

---

## 6. RESOURCE COSTS — ⚠ estimate until measured, and each row says which it is

⛔ **Every figure below is an ESTIMATE and says so.** A landed slice replaces its row with a **MEASURED** figure that
**names its method**. ★★ **THE STANDING LESSON: a static-instance count must be READ OFF THE IMAGE (ELF), never
inferred from the freeze pattern.** `UiSnapshot` is static exactly **once** (`s_frame_snap`) and `s_model` embeds
none ⇒ a growth of *n* costs *n* static **plus** *n* of **transient** loop-task stack, ⛔ not 2*n*. `UiState`
genuinely is static **twice** ⇒ for that struct ~2*n* stands. D2's warning applies on top.

| slice | estimate — ⚠ **UNMEASURED** |
|---|---|
| K1 | **flash ≈ 296 B** for the record (owner's figure: 4 × ~72 B + header) plus the store service; ★ **⛔ NO permanent `Node` RAM growth** — the live key already lives in `_team_ch_*`. ⚠ The 296 B is the **owner's estimate**; the slice measures the real `sizeof(TeamKeyBlob)` and pins it per-ABI, `/mrjoin`-style (*"`sizeof` IS the migration policy"*). ⚠ A **transient** 64-byte secret buffer may appear on the stack — it is **wiped** and its lifetime is stated. |
| K2 | `/mrcfg` grows by a `uint32_t` + a flag ⇒ **`mrnv::kVersion` bumps** and the record's own reprovision note is updated. RAM: expected 0. **Measure.** |
| N1 | `sizeof(Node)` grows by **record × 8**, Node-global and `MR_FEAT_TEAM`-gated ⇒ **+0 on both gateway envs**. ★ The record is **ruled to lead with `uint64_t last_ms`** and is **expected to measure 16 B** (against the draft's padded 24) — ⚠ **SUBJECT TO THE REQUIRED ABI MEASUREMENT**, pinned by `sizeof` + two `offsetof`s. The count byte is expected in existing padding ⇒ **measure by removal, not by argument.** |
| N2 | `UiSnapshot` grows by the published candidate array (8 × row) ⇒ ~that much static **and** transient stack. `UiState` gains an arm + cursor, expected in tail padding ⇒ **0. Measure.** ⛔ **MEASURED 2026-08-23 — the `UiState` estimate was WRONG, and the reason is R-10:** a frozen-per-entry list cannot be expressed by the per-tick snapshot array alone, so `UiState` carries the captured copy ⇒ **+136 B** (200→336; declared above the two bools it would otherwise measure 344 — placement load-bearing, recorded in-source). `UiSnapshot` 712→840 (+128, array at the old 8-aligned end, no hole; `nearby_n` free in the pad at 694). Board truth: heltec_mobile **+416 B RAM** vs the N1 baseline (+400 accounted: snapshot + two `UiState` statics; +16 linker alignment); gateway **RAM-neutral** (QG-measured). |
| N3 | `UiProvIntent` gains a `uint32_t` — it already carries a 112-byte `mrnv::JoinProfile` (`src/firmware_ui_model.h:606`) ⇒ marginal cost likely **0**; the new outcome is an **enumerator**, not a field ⇒ **0**. **Measure both.** ⛔ **MEASURED 2026-08-23 — the "likely 0" half was WRONG:** `UiProvIntent` **28 → 32 B** (+4: the u32 lands after the 24-byte join member with no hole to absorb it — both placements measured 32); `UiState` **336 → 344 B** (+8 host, `nearby_sel_id`; two OLED instances); the enumerator and the `Provision` arm cost **0** as predicted (`UiProvAnswer` 16 unchanged, `UiSnapshot` 840 unchanged). Board truth (QG, matched nogit builds): heltec_mobile RAM 216,892 → 216,908 (**+16 B**), flash 1,303,744 → 1,304,520 (**+776 B**); gateway N3 feature-off — RAM 195,020, flash 481,876. |
| N4 | `UiState` gains `_invite_until_ms` (u32) + an arm; the real cost is the **two snapshot authorities** (hashes: capacity × 4 B; the id bitset: **32 B** for 1..254) **plus the handled set**. ⚠ **The `bool`s are the ones that cost** — the §UI-17 S8 measurement was that a `uint32_t` deadline landed free while a `bool` took the struct's 8-byte tail step ⇒ **offsetof-prove the placement.** ⛔ **MEASURED 2026-08-23 — and this row's `bool` guess is INVERTED here (the arrays are what cost; the placements were offsetof-proved with zero padding):** `InviteMember` **20 B** × 8 ⇒ `UiSnapshot` 840 → **1000** (+160, at the old 8-aligned end); `InviteWindow` **104 B** (hash@0 · handled@32 · id_bits@64 · sel_hash@96 · n@100 · handled_n@101 · sel_id@102 · taken@103) ⇒ `UiState` 344 → **448** — two OLED instances ⇒ **+208 B**; `_invite_until_ms` ⇒ `UiModel` 856 → **864** (+8). Board truth (QG, matched builds): heltec_mobile RAM 216,908 → 217,276 (**+368 B**), flash 1,304,520 → 1,306,416 (**+1,896 B**); gateway RAM unchanged at 195,020, flash effectively unchanged. |
| N5 | ~0 RAM (one arm + three strings). |
| N6 | ~0 RAM (a pure mapping + the correlation pair `{dst, ctr}` in `UiState`). Flash: the outcome words. |
| K3 | 0 RAM. Flash: the persistence call plus four re-checks. ⛔ **Flash WEAR is a separate, unmeasured axis** and is bench-only (M2). |
| K4 | 0 RAM (one arm + an existing transient-note slot). |
| K5 | Landed separately; use its implementation report as the measured authority. |
| K6 | Expected 0 `Node` growth and no NV-record growth. UI state needs one full `team_id` selection plus arms; the list should reuse the existing four-record bound. **Measure**, including transient keyring-blob stack and both permitted board envs. |

**Per-tick cost.**
- N1: one comparison and at most one upsert **per received beacon** — ⛔ not per tick, ⛔ no allocation, flash or radio.
- N2: the cache is copied **once per entry**; the renderer asks the node nothing.
- N4: the member enumeration is **already paid** — `build_snapshot` walks `rt_team_at` today
  (`src/firmware_ui.cpp:591-598`) — so the refresh takes **one** `team_key_of_id` resolution per row and hands it to
  both consumers (U1), ⛔ never two lookups for one row. The refresh runs **only while the window is active**.
- N5/N6/K3/K4/K5/K6: nothing periodic at all.
- ⛔ **No new timer id in any slice.** `TimerWheel::kCap` is untouched — nothing here is protocol time.

---

## 7. METAL — the two-node invite / join / grant walkthrough

⛔ **THE BENCH SCRIPT (`docs/2026-07-31-bench-test-script.md`) REMAINS THE AUTHORITY (M2).** This section is written
in the owner's ruled **inline** format: every command and expected line is written out, with ⛔ **no cross-references
to follow**. When a slice adds a metal-only behaviour its report **drafts** the corresponding bench edit; ⛔ the slice
does not edit the bench script (supervisor-landed after PASS).

### 7.0 Equipment, build, evidence
- **H1** = a Heltec V3 mobile (`heltec_mobile`) that will **join** — start it **teamless**. **H2** = a Heltec V3
  mobile that **owns a team** and will invite. Both on the **same PHY and the same leaf nibble** (F-1: a different
  nibble makes H2 inaudible to a teamless H1, and the run measures nothing).
- ⚠ From a factory-reset node, do `cfg set sf_list 6,7` **first** — otherwise `team new` refuses.
- Flash the revision under test and confirm it: `version` ⇒ a real revision, ⛔ **not `nogit`**.
- **Record the identities once:** on H1 `whoami` ⇒ `<H1-hash>`; on H2 `whoami` ⇒ `<H2-hash>` and `team` ⇒
  `0x<TEAMID>`. Write down the **last six hex digits** of `0x<TEAMID>` — that is the panel's fingerprint
  (the low 24 bits, `src/firmware_ui_chrome.h:212`).
- Record PASS / FAIL per checkbox. A failure carries the console lines **and a panel photo**. Keep every ELF.

### 7.1 The joiner's read-only scan (N1, N2)
1. ☐ On **H2**: `team new` ⇒ `team_id=0x…`. On H1: `team` ⇒ `team_id=0x00000000`.
2. ☐ On **H1**: SETTINGS → `double` → walk to `PROVISION` → `double`. Expected children: `CREATE TEAM` ·
   `JOIN NETWORK` · **`JOIN TEAM`** · `BACK`. ⛔ FAIL if `JOIN TEAM` is absent.
3. ☐ `double` on `JOIN TEAM` — ⛔ **it must open the list directly**, not a submenu. Expected: one row showing the
   **last six hex digits of `0x<TEAMID>`**, a **`n/3`** signal token and an age; a `BACK` row; and the lines
   **`CURRENT PHY ONLY`** and **`SAME RADIO + LEAF`**. ⛔ FAIL if the row shows a **name** of any kind (P-5), or if
   the six hex digits do not match the console's id.
   ★ **AND MAKE THE NEGATIVE MEAN SOMETHING (P-5b, F-15 rule 1):** first give H2 a cached node name on H1 —
   `peername 0x<H2-hash> "Wolfgangetta"` on H1 (⚠ the verb takes a **hash**, never an id) — **then** re-enter NEARBY.
   Expected: the row **still** reads the six-hex **team** fingerprint. ⛔ FAIL if `Wolfga…` appears: an advertiser's
   NODE name is ⛔ never the TEAM name. ⓘ Without seeding the name first this step passes for the wrong reason.
4. ☐ **De-duplication and order:** leave, wait for several of H2's beacons, re-enter ⇒ **still one row**, with a
   **fresher age**, in the **same position**. ⛔ FAIL on a second row, or on a row that moved.
5. ☐ ★★★ **READ-ONLY (P-3).** On H1 run `peers`, the team-route listing and `team`. Expected:
   `team_id=0x00000000` **unchanged**, ⛔ no team route, ⛔ no peer binding for H2, ⛔ no key. Then **power-cycle H1**
   and re-check: the observation is RAM-only and the node comes up exactly as it went down.
6. ☐ ★ **NO ADDITIONAL TRAFFIC — measured against a baseline, because scheduled beacons never stop.** ⛔ A bare
   "watch for no transmission" cannot discriminate. ⇒ (i) with H1's panel on **STATUS**, capture H1's console for
   **five minutes** and count outbound lines by kind; (ii) repeat for the **same five minutes** sitting on NEARBY,
   entering and leaving throughout. Expected: **no ADDITIONAL query, DATA or join-shaped transmission**; beacon
   counts may differ only as their own schedule explains. ⓘ The automated proof is stronger and it exists (the N2
   probe asserts zero); this is the hardware sanity check.
7. ☐ **NEGATIVE — looking does not join.** Land on the row, `short` to `BACK`, `double` ⇒ back at the PROVISION menu
   and `team` still `0x00000000`.
8. ☐ **Signal decay (the EWMA, N1 pin 3):** move H2 to the far end of its range and leave it beaconing for several
   periods, then re-enter NEARBY ⇒ the token **falls** (e.g. `3/3` → `1/3`). ⛔ FAIL if it stays at the best value
   ever seen — that is the max-seen defect the ruling forbids.
9. ☐ **Empty case:** power H2 **off**, wait past **10 minutes** (two team-beacon periods), re-enter ⇒
   **`NO TEAMS NEARBY`** and a `BACK` row that still leaves. Power H2 back on.

### 7.2 The join (N3)
1. ☐ NEARBY → the row → `double`. Expected **`JOIN <six hex>?`** with **`BACK` selected**.
2. ☐ **NEGATIVE FIRST:** `double` on `BACK` ⇒ back at the **NEARBY list**, ⛔ not the menu; `team` still `0x00000000`.
3. ☐ Re-enter, `short` (⇒ `JOIN`), `double`. Expected **`TEAM JOINED`** with the **full `0x<TEAMID>`** and the same
   six-hex fingerprint. ⛔ FAIL on **`TEAM CREATED`** (F-4) or on any digit differing from H2's console id.
4. ☐ Either press acknowledges and returns to the PROVISION menu. ⛔ FAIL if the result carries a `BACK` row.
5. ☐ Console: `team` ⇒ `team_id=0x<TEAMID>` exactly; **power-cycle H1** ⇒ membership survives.
6. ☐ ★★★ **THE JOINER IS KEYLESS (P-2).** On H1 `team exportkey` ⇒ the **no key** answer. From **H2**:
   `send_channel 0 "sealed hello" -t -e` ⇒ H1 prints the **`ENCRYPTED — no team content key`** line and ⛔ no body.
7. ☐ **`PHY DIFFERS` arm.** On H1 `mobile register freq=<a different legal frequency>`, then attempt a NEARBY join
   ⇒ **`PHY DIFFERS`** / **`USE SERIAL`**, ⛔ zero writes, zero retunes. ⓘ If the divergence cannot be provoked on
   this bench, record **not-run with that reason**, ⛔ never FAIL.

### 7.3 The invitation window (N4)
1. ☐ On **H2**: SETTINGS → PROVISION → **`INVITE MEMBER`** → `double`. Expected: H2's team **fingerprint**, ⛔ no
   label, and a candidate list.
2. ☐ ★ **THE SNAPSHOT IS TAKEN AT OPEN.** With H1 already joined and heard **before** the window opened, expected on
   first open: **`NO CANDIDATES`**. ⛔ FAIL if an already-known member appears.
3. ☐ Leave the window. On **H1**: `team 0`, then re-join through NEARBY (§7.2). On **H2**: open `INVITE MEMBER`
   **before** H1 re-joins and wait. Expected: H1 appears as **`NEW MEMBER`** with its team-local id and a
   member-hash fingerprint. ⛔ FAIL on the word **`KEYLESS`** anywhere.
   ★ **RULE 2 — THE INITIAL STATE, READ OFF THE GLASS:** the row's **name column is BLANK** and its **fingerprint
   column is populated**. ⛔ FAIL if a name is already showing — H2 holds no verified pubkey for H1 yet, so no name
   has been cached alongside one. ⛔ FAIL if the fingerprint column is empty.
4. ☐ ★ **THE LOCAL REFRESH TRANSMITS NOTHING (P-4b).** Hold the window open for a full **five minutes**, capturing
   H2's console. Expected: candidates refresh on the panel and ⛔ **no query, DM, channel post or location request
   appears** that a STATUS window would not also show.
5. ☐ ★ **WINDOW EXPIRY (P-11).** Leave the window untouched past **5 minutes** ⇒ **`WINDOW CLOSED`**, the approval UI
   closes by itself, and ⛔ **H1 is still a member and still holds whatever key it held.**
6. ☐ **Blank/wake (OQ-3's clarification).** Re-open the window, let the panel blank (~20 s), press **once** ⇒ the
   panel lights with **the window still open**. Then open a `GRANT KEY` confirmation, let it blank, wake ⇒ ★ **the
   confirmation is GONE and the window is still open.** ⛔ FAIL if an unconfirmed grant survived.
7. ☐ **Outside the window (P-12).** With the window **closed**, have a new member appear ⇒ ⛔ **no prompt of any
   kind** on H2's panel, whatever screen it is on.

### 7.4 The pubkey request and the grant (N5, N6)
1. ☐ On **H2**, with a candidate selected, `double`. **If H2 holds no verified pubkey for H1**, expected:
   **`NEED PUBKEY`** with **`BACK` selected**. ⛔ FAIL if the panel says `WAITING FOR KEY` — that lexeme is forbidden.
2. ☐ ★ **NEGATIVE — nothing is aired without the operator.** `double` on `BACK` ⇒ ⛔ **no WANT_PUBKEY is transmitted**
   (check H2's console). Re-enter, `short` (⇒ `REQUEST PUBKEY`), `double` ⇒ the request airs and the panel reads
   **`WAITING FOR PUBKEY`**.
3. ☐ When H1's key arrives, H2's `peers` shows H1 with a key and the panel enables **`GRANT KEY`**. ⛔ FAIL if
   `GRANT KEY` was enabled by a `peer_key_cached` for a **different** peer.
3b. ☐ ★★ **RULE 3 — THE ROW UPGRADES FROM FINGERPRINT TO NAME, AND THE FINGERPRINT STAYS.** Give H1 a name first
   (on H1: `cfg set name "Wolfgangetta"`, or whatever this build's name verb is — confirm with `whoami`), and make
   sure the pubkey exchange of step 2 happened **after** it. Expected on H2's candidate row: the **name column now
   reads `Wolfga`** (six columns, clamped) **and the member fingerprint column is UNCHANGED** beside it.
   ⛔ FAIL if the fingerprint disappeared — the name is an added column, ⛔ never a swap. ⛔ FAIL if the name is
   clipped at a different width than the same peer's name on the **TEAM** screen (one name, one truncation).
   ⓘ Cross-check the source: on H2 `nameof 0x<H1-hash>` prints the same string — it came from the pubkey exchange
   (`lib/core/node_hashlocate.cpp:1251`), ⛔ not from anything guessed.
3c. ☐ ★★★ **RULE 4 — THE HASH SURVIVES INTO THE IRREVERSIBLE ACT.** Open the `GRANT KEY` confirmation on that
   now-named candidate. Expected: the screen carries the **full `0x<H1-hash>`**, ⛔ **even though a name is
   available**. ⛔ FAIL if the name is the only identity on the confirmation — a mutable, self-asserted label may
   never be the sole thing an operator reads before a private key is shipped.
4. ☐ **NEGATIVE — `REJECT` is the default and does nothing.** `double` on the candidate ⇒ the confirmation opens on
   **`REJECT`**. `double` ⇒ ⛔ nothing is sent, H1 is still keyless, ★ **and the candidate does not come back on the
   next refresh** (F-13). Then close and re-open the window ⇒ ★ **the candidate returns** (the set is volatile).
5. ☐ **The grant.** Re-provoke the candidate, `double`, `short` (⇒ `GRANT KEY`), `double`. Expected **in this order**:
   **`GRANT QUEUED`**, then — once the frame actually leaves the radio — **`KEY SENT`**.
   ⛔ **FAIL if `KEY SENT` appears immediately** — that is the F-9 defect (a non-zero `ctr` is admission, not air).
   ⛔ FAIL on **`JOIN COMPLETE`** or any completion word.
6. ☐ On **H1**: the console prints
   `TEAM KEY RECEIVED team=0x<TEAMID> from=0x<H2-hash> — this node can now read the team channel`.
   ⛔ **No key material may appear on any console or panel** (P-8).
7. ☐ **The round trip.** From H2: `send_channel 0 "sealed again" -t -e` ⇒ H1 prints the body with `[enc]`.

### 7.5 Durability and lifecycle — the keyring (K1…K6)
1. ☐ ★★★ **THE FALSIFIER FOR [[B240]].** **Power-cycle H1.** Then `team exportkey` ⇒ the key is **still there**, and
   a fresh sealed post from H2 is readable. ⛔ **Before the keyring lands this FAILS by construction** — run it as the
   falsifier and record the result either way.
2. ☐ **The panel said the true thing.** On the grant, H1's panel showed **`TEAM KEY RECEIVED`**. Now force a save
   failure (the bench part K1 drafts names the method) and repeat ⇒ the panel must show **`TEAM KEY ACTIVE`** /
   **`NOT SAVED — LOST ON REBOOT`** and ⛔ **never `TEAM KEY RECEIVED`**.
3. ☐ ★ **RETAINED, NOT SILENTLY REACTIVATED (P-2b).** On H1: `team 0` ⇒ keyless and out of the team. Re-join the
   **same** team through NEARBY ⇒ `SAVED KEY FOUND`, with `BACK` selected. ⛔ The key remains inactive until the
   explicit `USE SAVED KEY` action; BACK changes no key state.
4. ☐ **Reboot in-team restores.** With H1 in the team and holding the key, power-cycle ⇒ the key is active again
   automatically.
5. ☐ ★ **A FULL KEYRING FAILS LOUDLY (P-15).** Fill four teams' records, then receive a fifth team's grant ⇒
   **`KEYRING FULL`**, ⛔ zero writes, ⛔ **no record replaced.** ⛔ FAIL on any silent eviction.
6. ☐ **Power-cut part.** Repeat the K1-drafted power-cut procedure across a keyring write ⇒ the record on disk is
   **either the complete old one or the complete new one**, ⛔ never half. ⓘ Precedent: the §UI-15 slice-7 `/mrjoin`
   power-cut.
7. ☐ **Factory reset erases it.** After a factory reset, `/mrteams` holds nothing and the node is keyless.
8. ☐ ★ **EXPLICIT RETENTION MANAGEMENT (K6).** With four records present, `team keys` lists four ids and marks
   exactly the active one; no key bytes appear. Attempt `team forgetkey <active-id> confirm` ⇒ loud refusal, zero
   writes and the active key still works. Omit `confirm` on an inactive id ⇒ zero writes. Then confirm removal of a
   chosen inactive id ⇒ exactly that id disappears; reboot and verify all other records plus the active key survive.
9. ☐ **FULL → MANAGEMENT, NEVER AUTO-EVICTION.** Refill to four, provoke a fifth-team `KEYRING FULL`, acknowledge it
   on OLED ⇒ the saved-key list opens with no row removed and no create/grant retried. Select an inactive row, verify
   the confirmation shows its **full** team id with BACK selected, choose `FORGET KEY`, then manually retry the
   original operation. ⛔ FAIL if an oldest row disappears automatically, ACTIVE can be selected for deletion, or
   the original operation replays itself.

### 7.6 Stop rules
- ⛔ **Stop and report** on: a join or a grant the operator did not confirm; a WANT_PUBKEY aired without an explicit
  `REQUEST PUBKEY`; a foreign team's id appearing in a route, a peer binding or NV; `KEY SENT` shown before the frame
  aired; a key claimed durable that a power-cycle loses; a silently evicted keyring record; any panel claim the
  console contradicts.
- Record the exact image (`version`), the console transcript and a photo per failure.

---

## 8. STRING INVENTORY — every lexeme, for owner ruling

House style: each string is declared **once**, in a pure unit, and **pinned by a native case**, so an owner ruling
changes it in exactly one place. Widths are against the body's **19 columns**.
★★ **THE TEAM-ID FINGERPRINT IS RENDERED THROUGH `mrui::ui_fmt_team_fingerprint` AND NOTHING ELSE — ⛔ ZERO NEW
DEFINITIONS OF IT** (`src/firmware_ui_chrome.h:214`, U1 argument at `:204-211`).
⚠ **REPORTED, NOT INVENTED:** where a ruling settles a SEMANTIC and no lexeme, the wording is this file cluster's
house style applied to it, one line each, pinned by a native case.
ⓘ **RENUMBERED — the draft's entries were `N-1…N-22`; the prefix `N-` now names a SLICE, so lexemes take the house
`S-` prefix (the §UI-17 §8 convention). The surviving rows map 1:1 in order.**

| # | lexeme / format | where | cols | status |
|---|---|---|---|---|
| S-1 | `JOIN TEAM` | PROVISION child row | 10 | **NEW** — the design's own path word (§3.6.4 `:797`) |
| S-2 | `NEARBY` | the scan screen header | 7 | **NEW** — the design's own word (`:797`) |
| S-3 | `CURRENT PHY ONLY` | the scan screen | 16 | **REUSED FROM THE DESIGN, VERBATIM** (`:829-830`) |
| S-4 | `SAME RADIO + LEAF` | the scan screen, second line | 17 | **NEW** — F-1's honest completion of S-3; ⚠ the one line an owner may want reworded |
| S-5 | `NO TEAMS NEARBY` | the scan screen, empty | 15 | **NEW** |
| S-6 | `%s %s %s` → `3D9348 2/3  42s` | a **NEARBY team** row | ≤19 | **NEW format**; fingerprint = the shared helper, age = `ui_fmt_home_age`'s token **REUSED** (`src/firmware_ui_chrome.h:118`). ★ **AMENDED 2026-08-22 (F-15 rule 1):** the row is identified by the **TEAM fingerprint ONLY** — ⛔ **an advertiser's NODE name is never rendered here** (see S-36). ⓘ The header formerly read *"a candidate row"*, which was ambiguous once the invite row got its own format (S-35) |
| S-7 | `0/3` `1/3` `2/3` `3/3` | the signal column | 3 | ★ **NEW, OWNER-RULED 2026-08-22** — **four** levels from `presence_quality_tier()` (`lib/core/protocol_constants.h:905`). ⛔ **WITHDRAWN, KEPT VISIBLE:** the draft proposed a **three**-level token from `bucket_of_snr_4b`; that would be a **second definition of signal quality** and is forbidden |
| S-8 | `JOIN %s?` → `JOIN 3D9348?` | the join confirmation | 12 | **NEW** — §3.6.4 `:806` names this form verbatim |
| S-9 | `JOIN` / `BACK` | the confirmation's actions | 5 | **REUSED** — `join_confirm_label` spells exactly this pair (`src/firmware_ui_join.h:189`); `BACK` is the shipped spelling (`src/firmware_ui_model.h:581`) |
| S-10 | `TEAM JOINED` | the join result | 11 | **NEW** — F-4: ⛔ may not reuse `TEAM CREATED` |
| S-11 | `JOIN REFUSED` · `PHY DIFFERS` · `USE SERIAL` · `SAVE FAILED` | the join result's other arms | 14 | **REUSED** — all four shipped and **called** (`prov_result_head`, `src/firmware_ui_model.h:682-700`) |
| S-12 | `INVITE MEMBER` | PROVISION child row + window screen | 14 | **REUSED FROM THE DESIGN, VERBATIM** (`:800`) |
| S-13 | `%06lX` over a **`key_hash32`** → `6C2971` | a candidate's member fingerprint | 6 | **NEW definition, SEPARATELY NAMED** — F-8; ⛔ never merged with the team-id helper. ★ **AMENDED 2026-08-22 (F-15 rule 2):** it is a **PERSISTENT COLUMN**, ⛔ not a placeholder — when a cached name arrives it fills the **separate** name column and this one is **unchanged**. ⛔ A name never replaces it |
| S-14 | `NEW MEMBER` | the candidate row | 10 | **REUSED FROM THE DESIGN, VERBATIM** (`:815`); ⛔ `KEYLESS` is FORBIDDEN and its absence is a test |
| S-15 | `NO CANDIDATES` | the window, empty | 13 | **NEW** |
| S-16 | `WINDOW CLOSED` | the window, on expiry | 13 | **NEW** — `:824` rules the behaviour and no lexeme |
| S-17 | `GRANT KEY` / `REJECT` | the two actions, `REJECT` default | 9 | **REUSED FROM THE DESIGN, VERBATIM** (`:815-816`) |
| S-18 | `NEED PUBKEY` | the `no_pubkey` landing | 11 | ★ **NEW, OWNER-RULED 2026-08-22** (F-12) |
| S-19 | `REQUEST PUBKEY` | its confirm action | 14 | ★ **NEW, OWNER-RULED** (F-12) |
| S-20 | `WAITING FOR PUBKEY` | after the request | 18 | ★ **NEW, OWNER-RULED** (F-12) — ⛔ **never `WAITING FOR KEY`** |
| S-21 | `GRANT QUEUED` | `queued`, `ctr != 0` | 12 | ★ **NEW, OWNER-RULED 2026-08-22** (F-9) — admission, ⛔ not air |
| S-22 | `KEY SENT` | ONLY on a **correlated `send_aired`** | 8 | **REUSED FROM THE DESIGN, VERBATIM** (`:821`) — ★ its **trigger** is corrected by F-9 |
| S-23 | `GRANT FAILED` | a correlated failure **or a synchronous send-path refusal** | 12 | ★ **NEW, OWNER-RULED** (F-9). ★ **WIDENED 2026-08-24 (N6b, QG-passed):** also the word for the core's synchronous, already-pushed `send_failed` (`SendDispatch::Admit::none` — the no-route/seal-refusal family) — one fact, one word, rather than an unruled 39th lexeme. ⛔ `GRANT QUEUE FULL` (S-38) stays strictly separate |
| S-24 | `NO TEAM KEY` · `NO IDENTITY` · `NOT IN A TEAM` · `SELF` · `NAME TOO LONG` · `WRONG PLANE` | the six remaining `TeamKeyGrantTx` arms (`lib/core/node.h:257-276`) | ≤14 | **NEW**, one line each — ⛔ they may not collapse. ⓘ `WRONG PLANE` is `delegated`'s: unreachable on the real seam (the UI sends `Plane::TEAM`) but the mapper **fails loudly** for it |
| S-25 | `TEAM KEY RECEIVED` | the joiner, **after a `saved` persist** | 17 | **REUSED FROM THE DESIGN, VERBATIM** (`:822`); ★ reachable only via K3 (F-10) |
| S-26 | `TEAM KEY ACTIVE` | the save-failed path, line 1 | 15 | ★ **NEW, OWNER-RULED** (keyring) — it is live in RAM, and that is true |
| S-27 | `NOT SAVED — LOST ON REBOOT` | the save-failed path, line 2 | 26 ⇒ **two rows** | ★ **NEW, OWNER-RULED** (keyring). ⚠ It exceeds 19 columns, so it renders across two body rows exactly as the ruled `PHY DIFFERS` / `USE SERIAL` pair does; ⛔ neither half may be reworded or clipped |
| S-28 | `SAVED KEY FOUND` | K5, on joining a known team | 15 | ★ **NEW, OWNER-RULED** (keyring) — **later** |
| S-29 | `USE SAVED KEY` | K5's explicit action, `BACK` default | 13 | ★ **NEW, OWNER-RULED** (keyring) — **later** |
| S-30 | `KEYRING FULL` | K1, a fifth team | 12 | ★ **NEW, OWNER-RULED** (keyring, P-15) — the loud failure that ⛔ never evicts |
| S-31 | `FORGET KEY` | K6 inactive-key confirmation/action | 10 | ★ **ACTIVATED 2026-08-25 from the previously reserved owner-named lexeme.** BACK is selected by default; this action is absent for the active row |
| S-32 | `JOIN COMPLETE` | — | 13 | ⛔ **FORBIDDEN** — §3.6.4 `:821` refuses it explicitly; absence is a test |
| S-33 | `KEYLESS` | — | 7 | ⛔ **FORBIDDEN** — §3.6.4 `:815`; absence is a test |
| S-34 | `WAITING FOR KEY` | — | 15 | ⛔ **NEWLY FORBIDDEN, OWNER-RULED 2026-08-22.** ⛔ **WITHDRAWN, KEPT VISIBLE:** the draft carried it twice as a live lexeme (the `no_pubkey` landing and the PARKED sub-state). It is **ambiguous between the recipient's PUBKEY and the team CONTENT key** — the two secrets this screen sits between. Its slots are now S-20 and S-21 |
| S-35 | `%c%-6.6s T%-3u %6s` → `>Wolfga T221 6C2971` | the **INVITE candidate** row | **19 exactly** | ★ **NEW format, ADDED 2026-08-22 (F-15 rules 2-3)** — marker · **name (6, BLANK until a name is cached alongside a verified pubkey)** · team-local id · **member fingerprint (S-13)**. Width proof `1+6+1+4+1+6 = 19`. ★ The name comes from **`Node::peer_name_find`** (`lib/core/node.h:1030`) — the TEAM chain's own second step (`src/firmware_ui.cpp:361`), ⛔ **never `label_from_hash`/`label_for_team_id`**, whose `0x%08lx` fallback would render a **truncated `0x` form** = a third spelling of the hash. Clamp is `%-6.6s`, **matching the TEAM row** (§UI-17 S-11) so one name has one truncation |
| S-36 | *(a node name rendered as a **team** name)* | — | — | ⛔ **FORBIDDEN USAGE, ⛔ NOT A LEXEME — ADDED 2026-08-22 (F-15 rule 1, P-5b).** There is no string to declare: the rule is that **no name-shaped value may occupy a NEARBY row's identity**, and it is enforced by a **control** (a mutation resolving the beacon sender's name into that row must redden), ⛔ not by a spelling. ⓘ Recorded here because the string inventory is where a future slice looks before adding a label |

| S-37 | `GRANT PARKED` | the grant's parked sub-state | 12 | ★ **ADDED + OWNER-RULED 2026-08-24** (the N6 wording ruling): derived from the console's shipped `PARKED (resolving…)` in the cluster's `GRANT <state>` shape. ⛔ **Shown ONLY for an EXPLICITLY-STORED parked outcome** reported by the core's dispatch result — ⛔ never inferred from `ctr == 0` (the N6 first-gate correction in §4-N6) |
| S-38 | `GRANT QUEUE FULL` | the grant's admission refusal | 16 | ★ **ADDED 2026-08-24 (N6 first-gate QG)** — the distinct refusal for a full TX queue / full parked ring, which the pre-correction core laundered into `queued`. ⛔ Never collapsed into `GRANT FAILED` (that word is the correlated in-flight failure's) |
| S-39 | `KEY NOT INSTALLED` | K5's `USE SAVED KEY` refusal | 17 | ★ **ADDED + OWNER-RULED 2026-08-25** (the K5 round-2 wording): states **the act's outcome, never the node's key inventory**, so it is true on every refusing arm — incl. the stale-membership race, where another team's key legitimately remains live (which made the withdrawn `NO TEAM KEY` candidate FALSE). Second row = the service token |
| S-40 | `SAVED KEYS` | K6's PROVISION child row + list title | 10 | ★ **ADDED + OWNER-RULED 2026-08-25** (§K6) — management of retained records, ⛔ never an export-key screen |
| S-41 | `NO SAVED KEYS` | K6's list, empty | 13 | ★ **ADDED + OWNER-RULED 2026-08-25** (§K6) — says the keyring holds no retained records; ⛔ not `NO TEAM KEY`, which is a claim about LIVE state |
| S-42 | `KEY FORGOTTEN` | K6's successful removal | 13 | ★ **ADDED + OWNER-RULED 2026-08-25** (§K6) — reachable only after the keyring save SUCCEEDS; ⛔ a storage failure never renders it (K6 pin 5) |
| S-43 | `ACTIVE KEY` / `CANNOT FORGET` | K6's active-row landing, no destructive action | 10 / 13 | ★ **ADDED + OWNER-RULED 2026-08-25** (§K6) — the two-row shape, the `PHY DIFFERS`/`USE SERIAL` precedent |
| S-44 | `ACTIVE` | K6's list marker on the active row | 6 | ★ **ADDED + OWNER-RULED 2026-08-25** (§K6) — a row MARKER, not a screen; status only: the full binding predicate remains the authority and ⛔ the word never authorises deletion |
| S-45 | `KEY NOT FORGOTTEN` | K6's forget failure headline | 17 | ★ **ADDED 2026-08-25 (K6, the S-39 method — reported, then QG/owner-passed with the slice):** the ACT's outcome, true on all six failing arms; second row = the service token. ⛔ Never `KEY FORGOTTEN` on any failure (pin 5) |
| S-46 | `NO KEYRING` | K6's list, no `/mrteams` store exists | 10 | ★ **ADDED 2026-08-25 (K6)** — distinct from an UNREADABLE store, ⛔ never collapsed (the missing-vs-unreadable mutation) |
| S-47 | `CONFIG UNREADABLE` | K6's list, the `/mrcfg` binding read failed | 17 | ★ **ADDED 2026-08-25 (K6)** — the ACTIVE marker cannot be computed, so the list refuses rather than guessing |
| S-48 | `KEY STORE INVALID` | K6's list, the `/mrteams` blob fails validation | 17 | ★ **ADDED 2026-08-25 (K6)** — beside the console's existing `STORAGE FAILURE` for the same fault family; one fact per word |

**Count: 44 entries — 32 NEW, 8 REUSED, 3 FORBIDDEN lexemes, 1 FORBIDDEN USAGE** *(corrected through K6 on
2026-08-25; S-31 moved from reserved to live but remains in the same NEW bucket)*. ⚠ **The arithmetic is written out
so it can be checked rather than trusted:**
- **NEW (32):** S-1, S-2, S-4, S-5, S-6, S-7, S-8, S-10, S-13, S-15, S-16, S-18, S-19, S-20, S-21, S-23, S-24,
  S-26, S-27, S-28, S-29, S-30, S-31, S-35, S-37, S-38, S-39, S-40, S-41, S-42, S-43, S-44 (S-24 is one row
  carrying six arms, counted once).
- **REUSED (8):** **6 are §3.6.4's own words carried verbatim** — S-3, S-12, S-14, S-17, S-22, S-25 — and **2 are
  shipped strings CALLED rather than re-spelled** — S-9 (`join_confirm_label`) and S-11 (`prov_result_head`).
- **FORBIDDEN LEXEMES (3):** S-32, S-33, S-34 — absence is a test, ⛔ not a preference.
- **FORBIDDEN USAGE (1):** **S-36** — ⛔ **not a lexeme and deliberately not given one**: there is no string to
  declare, only a rule about what may occupy a NEARBY row's identity, enforced by a control.
- ⓘ **The team-id fingerprint is NOT an entry**, because it defines nothing: every site calls the one helper. The
  **member name** is likewise not an entry — it is data from `peer_name_find`, not a string this spec declares;
  S-35 is the **format that places it**.
- ⓘ **Delta vs the reviewed draft: 22 → 34 → 36 → 39 → 44 entries.** The first +12 was the owner review (the four ruled
  pubkey/grant lexemes S-18…S-21/S-23, the four keyring lexemes S-26…S-30, the reserved S-31, the split-out signal
  token S-7, and `WAITING FOR KEY` moving from live to forbidden). The **+2 is the name-lifecycle amendment**:
  **S-35** (the invite row format) and **S-36** (the forbidden usage) — plus **in-place amendments to S-6 and S-13**,
  which add no entries. S-37…S-39 came from N6/K5; S-40…S-44 are K6's lifecycle words.

---

## 9. RULINGS — ★ ALL CLOSED. ZERO OPEN QUESTIONS.

⛔ **THE SECTION HEADING IS CORRECTED IN PLACE AND THE EARLIER FORM IS KEPT VISIBLE:** the draft read
*"OPEN QUESTIONS — only the genuinely unresolved, each with a recommended default"* and carried **OQ-1…OQ-5**.
**All five were ruled by the owner on 2026-08-22, together with SIX required corrections (R-6…R-11)** ⇒ **nothing in
this spec is waiting on a decision.** Each ruling is recorded in reported form **and folded into the normative body at
the place named**; each question's own text is kept visible so the ruling can be read against what was asked.
⛔ **The 2026-08-06 rulings of §3.6.4 are not re-opened** — the random `team_id` as identity, labels as optional
metadata, read-only scanning, `NEW MEMBER` over `KEYLESS`, label provenance and the safe-action default all stand and
are mapped in §3.

**R-1 (was OQ-1) · The menu shape — ★ `JOIN TEAM` OPENS NEARBY DIRECTLY.**
*Asked:* one row, or a `JOIN TEAM` submenu containing a `NEARBY` row?
*Ruled:* **directly; a submenu only when a second join method exists.** ⇒ landed at **§1.4**, **§4-N2** and metal
**§7.1 step 3** (which FAILs a submenu).

**R-2 (was OQ-2) · Retention window and capacity — ★ 10 MINUTES / 8 ENTRIES, WITH THREE AMENDMENTS.**
*Asked:* how long is "recently observed", and how many slots?
*Ruled:* **10 min / 8**, and three things the draft got wrong are corrected with it: **(i)** the rationale is that
**10 min = exactly two default 5-minute team-beacon periods** (`team_beacon_period_ms = 300000`,
`lib/core/node_carriers.h:110`), ⛔ not *"several periods on every `sf_list`"*; **(ii)** signal updates via the
**EXISTING SNR EWMA** (`snr_ewma_update`, `lib/core/protocol_constants.h:91-93`), ⛔ **never "strongest seen in
window"** — max-seen misleads when a mobile moves away; **(iii)** the record **begins with `uint64_t last_ms`** (the
draft's order likely pads to 24 B; reordered it should be 16 B, **subject to the required ABI measurement**) and is
implemented as an **ORDERED BOUNDED ARRAY** — refresh in place, and on overflow shift out the oldest and append.
⇒ landed at **§2.1**, **§4-N1** (pins 3/5, and the mutations that attack each), **§6** and metal **§7.1 steps 4/8/9**.

**R-3 (was OQ-3) · Invitation window duration, and does it hold the panel lit? — ★ 5 MINUTES; NO.**
*Asked:* §3.6.4 says *"bounded"* and rules no number; and a lit panel also suppresses light sleep
(`ui_allows_sleep` requires `blanked`, §UI-17 F-10).
*Ruled:* **5 minutes, and it does NOT hold the panel lit** — plus one clarification: **the WINDOW survives blanking;
an UNFINISHED CONFIRMATION does not.** ⇒ landed at **§1.6**, **§4-N4** and metal **§7.3 steps 5-6**.

**R-4 (was OQ-4) · The signal token — ★ REPLACED: FOUR LEVELS, `0/3 1/3 2/3 3/3`.**
*Asked:* what unit and what lexeme, given F-2 (this panel has never rendered a signal token)?
*Ruled:* **four levels rendered `0/3 1/3 2/3 3/3`, derived from the EXISTING `presence_quality_tier()`**
(`lib/core/protocol_constants.h:905` — verified). ⛔ **The draft's `bucket_of_snr_4b` proposal is refused on two
counts: that function is not a UI token source, and a three-level mapping would be a SECOND DEFINITION of signal
quality.** `0/3..3/3` is ASCII, fixed-width and fits the 19 columns. ⇒ landed at **§1.7 F-2**, **§4-N2** (the
"second-definition" mutation), **string S-7** and metal **§7.1 steps 3/8**.

**R-5 (was OQ-5) · The joiner's list cap and ordering — ★ ALL RETAINED ENTRIES, STABLE FIRST-OBSERVED ORDER.**
*Asked:* display cap and ordering, separately from the ring cap.
*Ruled:* **all retained entries, in stable first-observed order, ⛔ never signal-sorted** — and R-2(iii) makes that
**structural** rather than a sorting rule. ⇒ landed at **§2.1**, **§4-N2** (the "sorted by signal" mutation) and
metal **§7.1 step 4**.

**R-6 · Durable adoption — ★ THE DRAFT'S `S6` IS REPLACED WHOLESALE BY A `/mrteams` KEYRING, LANDING FIRST.**
*Asked (implicitly, by F-6):* how is a received team content key made durable?
*Ruled:* **a dedicated `/mrteams` keyring** — record shape, policy set, retention/activation rules, its own metal
power-cut part, the `/mrjoin` storage precedent for the absent/corrupt/io_failed matrix, coalescing and factory
reset, and the explicit note that **a new NV record is not a wire change**. Keys are **RETAINED on leave** and ⛔
**never silently reactivated by mere knowledge of the public team_id** ⇒ `/mrcfg` gains a small active-binding fact.
⇒ landed at **§1.7 F-6**, the whole of **§4-K1/K2/K5/K6**, **§3 P-2b and P-15**, **§6**, metal **§7.5**, strings
**S-26…S-31 and S-39…S-44**, and register **[[B240]]** (`docs/2026-07-30-open-bug-register.md:130`).

**R-7 · `no_pubkey` — ★ AN EXPLICIT, OPERATOR-AUTHORISED `REQUEST PUBKEY` STEP.**
*Asked (implicitly, by the draft):* UI-16 dead-ends where the grant refuses for want of a verified pubkey, and
automatic acquisition is banned (`lib/core/node.cpp:185-196`, `§no-auto-reqpubkey`).
*Ruled:* **a confirmation with `BACK` selected and the action `REQUEST PUBKEY`**; confirming sends the **existing**
team-scoped WANT_PUBKEY; a received `peer_key_cached` enables `GRANT KEY`; ⛔ the private team key is never sent
automatically. **This preserves the ban — the operator explicitly authorises the on-air identity request.**
Lexemes `NEED PUBKEY` / `REQUEST PUBKEY` / `WAITING FOR PUBKEY`, and ⛔ **`WAITING FOR KEY` becomes FORBIDDEN**.
⇒ landed at **§1.7 F-12**, **§4-N5** (its own step in the order), **§3 P-7b**, strings **S-18…S-20, S-34**, metal
**§7.4 steps 1-3**.

**R-8 · `ctr != 0` — ★ IT MEANS ADMITTED, NOT AIRBORNE.**
*Asked (implicitly, by the draft's mapping):* when may the panel say `KEY SENT`?
*Ruled:* **`queued` ⇒ `GRANT QUEUED`; a correlated `PushKind::send_aired{dst, ctr}` ⇒ `KEY SENT`; a correlated
failure ⇒ `GRANT FAILED`; ⛔ no e2e ack ⇒ never `JOIN COMPLETE` or `KEY RECEIVED` on the granter.** The UI always
sends `Plane::TEAM`, so **`delegated` is unreachable on the real seam — but the pure mapper still FAILS LOUDLY if a
fake returns it.** ⇒ landed at **§1.7 F-9**, **§4-N6**, **§3 P-9**, strings **S-21…S-24**, metal **§7.4 step 5**.

**R-9 · The `S6 → S7` ordering — ★ PERSISTENCE RUNS FIRST; ONLY `saved` FORWARDS THE PUSH.**
*Asked (implicitly):* how can the panel mean *"only after durable adoption"* when `mr_ui_on_push` runs before the
push switch (`src/fw_main.cpp:1310` — verified)?
*Ruled:* **a config-service persistence function runs FIRST; only a `saved` return forwards the push; on failure the
UI never shows `TEAM KEY RECEIVED` and the failure is reported explicitly as RAM-only / lost-on-reboot.** The function
**re-checks at handling time** — `pu.team_id != 0` · `pu.team_id == g_node.config().team_id` · the live key is
present · the loaded blob belongs to the same team — closing the delayed-push race. ⇒ landed at **§1.7 F-10**,
**§4-K3/K4**, **§3 P-10**, strings **S-25…S-27**, metal **§7.5 steps 1-2**.

**R-10 · Refresh — ★ NEARBY IS FROZEN; INVITE REFRESHES LOCALLY.**
*Asked (implicitly, by the draft's blanket "no auto-refresh"):* do both lists behave the same?
*Ruled:* **no.** NEARBY teams = a **frozen snapshot per entry**, manual refresh only. INVITE candidates = **locally
refreshed while the window is active** — no scan, transmits nothing, it only re-reads existing member state.
**Selection stays identity-based; opening a confirmation FREEZES the selected hash/id.** ⇒ landed at **§1.7 F-14**,
**§4-N2 and §4-N4**, **§3 P-4b**, **§10**, metal **§7.3 step 4**.

**R-11 · `REJECT` — ★ IT NEEDS A VOLATILE PER-WINDOW HANDLED SET.**
*Asked (implicitly):* the draft claimed `REJECT` both removes the candidate and changes no state — which cannot both
hold, because the next refresh re-adds it.
*Ruled:* **`REJECT` and a queued grant add the candidate hash to a VOLATILE per-window handled set**; it changes ⛔ no
core, radio, membership, key or NV state; **the set is discarded when the window closes.** ⛔ *Corrected 2026-08-24:
`REJECT`-only — the queued-grant clause withdrawn at §1.7 F-13 (unobservable write).* ⇒ landed at **§1.7 F-13**,
**§4-N4/N6**, **§3 P-11b**, metal **§7.4 step 4**.

**R-12 · The snapshot — ★ TWO AUTHORITIES, NOT ONE.**
*Asked (implicitly):* the draft keyed the diff on `key_hash32` alone.
*Ruled:* **authoritative hashes AND a bitset of team-local ids present at opening. A candidate is NEW only when
NEITHER its hash NOR its current id was in the opening snapshot.** The unavoidable **double-change** case (an unkeyed
member changing id **and** acquiring a hash in one window) is **documented as a SAFE FALSE PROMPT** requiring human
confirmation. ⇒ landed at **§1.7 F-11**, **§4-N4**, **§3 P-6b**, metal **§7.3 steps 2-3**.

**R-13 · Name display — ★ A LIFECYCLE, NOT A BAN (QA-proposed, owner-forwarded 2026-08-22).**
*Asked:* the draft forbade names in invitation rows outright and required fingerprint/signal/age only — while the
**TEAM** view on the same panel already prefers the cached node name.
*Ruled:* **five rules.** (1) NEARBY rows stay identified by the **team fingerprint only**, and ⛔ **an advertiser's
NODE name is never presented as the TEAM name**; (2) a NEW-MEMBER row **initially** shows the **member fingerprint** —
the draft's rule, restated as the **initial state of a lifecycle**; (3) after an explicit `REQUEST PUBKEY` **succeeds
for that hash**, the invitation **and** TEAM views **prefer the cached node name**, through the **existing** resolver
(U1 — called, ⛔ never re-spelled); (4) the **full hash stays VISIBLE** in the confirmation/detail screen — ⛔ a name
is never the only identity at the moment of an irreversible act; (5) selection, pubkey requests and key grants stay
keyed **EXCLUSIVELY by the full hash**, ⛔ never by the mutable display name (§B64).
★ **IT REFINES §3.6.4 RATHER THAN CONTRADICTING IT:** the label-provenance rule forbids a label *guessed from an id*
and permits one whose **PROVENANCE is trusted** — and this one is carried by, and cached with, an exchange whose
**KEY half is verified**, in **both** directions (`lib/core/node_hashlocate.cpp:1178 / :1251 / :1142`).
⛔ **REWORDED IN PLACE 2026-08-22 (QG, K1 gate round), WITHDRAWN TEXT KEPT VISIBLE:** this sentence read *"permits one
from an **authenticated source** — and the pubkey exchange **is** one"*. **⛔ THAT OVERCLAIMS: the name is MUTABLE
METADATA CACHED ALONGSIDE a verified pubkey, ⛔ not itself cryptographically authenticated.** The ruling and every
rule under it are unchanged — only the ground shifts from *authentication* to *provenance*, which is what the bound
below already said. *Falsifier: the QG K1-gate ruling plus §1.7 F-15's own analysis (`node_hashlocate.cpp:346`).*
⚠ **AND THE BOUND IS STATED, WHICH IS WHY RULES 4 AND 5 EXIST:** the exchange authenticates the **key**, ⛔ not the
truthfulness of the name, and the name is **mutable** (`:346`) ⇒ **a name may DESCRIBE and may ⛔ NEVER IDENTIFY.**
⇒ landed at **§1.7 F-15**, **§3 P-5 (rewritten in place) / P-5b / P-7c / P-7d**, slices **N2** (confirmed unchanged
plus one control), **N4**, **N5**, **N6**, strings **S-6 / S-13 amended, S-35 / S-36 added**, metal **§7.1.3 ·
§7.3.3 · §7.4.3b · §7.4.3c**. ⛔ **The K-slices are untouched** — the keyring record carries no labels by ruling, so
this is a display lifecycle only and K1 may proceed in implementation unaffected.

**R-14 · Saved-key lifecycle — ★ EXPLICIT REMOVAL, NEVER SILENT ROTATION.**
*Asked after metal testing:* repeated `team new` operations retain four different team keys and the fifth refuses;
should the keyring rotate automatically?
*Ruled:* **provide explicit management instead.** List retained team identities without material · mark and protect
the active key · require a full-id, BACK-default confirmation for `FORGET KEY` · make `KEYRING FULL` lead to that
management view · and require the operator to retry creation/grant afterwards. ⛔ No automatic oldest/LRU victim,
no eviction on asynchronous receipt, and no combined delete+create transaction across `/mrteams` and `/mrcfg`.
⇒ landed at **§3 P-15**, **§4-K6**, **§5/§6 K6**, metal **§7.5 steps 8-9**, strings **S-31 and S-40…S-44**, and
§10's corrected scope boundary.

---

### ⓘ Open questions: **NONE.**
⛔ Nothing in this spec is blocked on an owner decision. A slice that believes it has found one must **STOP and
report** rather than choose a default — that is the standing rule this section was created to serve, and it does not
lapse because the list is empty.

---

## 10. WHAT THIS SPEC DELIBERATELY DOES NOT DO

- ⛔ **no wire change of any kind and no `wire_version` bump** — the scan is passive over the existing type-5 TLV, the
  grant rides the existing sealed type-19 path, the pubkey request rides the existing `CmdKind::reqpubkey` flood, and
  ★ **a new NV record (`/mrteams`) is not a wire change**. ⓘ K2 bumps `mrnv::kVersion`, which is an **NV** version:
  no frame moves and nothing re-anchors. ⚠ If a step genuinely needs wire, **STOP** — M3 makes it free to deploy,
  C4 makes it **its own slice and its own commit**;
- ⛔ **no relaxation of the pre-parse leaf-nibble drop** (`node_beacon.cpp:558`) — F-1 reports the consequence and the
  panel says so honestly (S-3 + S-4);
- ⛔ **no second definition of signal quality** — the panel maps `presence_quality_tier()` and defines nothing (R-4);
- ⛔ **no team LABEL store, on either side and in the keyring record** (F-3, and the keyring ruling repeats it) —
  ⛔ **CORRECTED, KEPT VISIBLE: this bullet is about a TEAM label and must not be read as forbidding the MEMBER-name
  display.** A member's **node name** is **mutable metadata cached alongside a verified pubkey** — ⛔ **not itself
  authenticated** (⛔ **reworded 2026-08-22, QG K1 gate round; the withdrawn phrase was *"is
  authenticated-by-exchange"***) — and, once cached, **is shown** (R-13 rules 2-3);
  what stays absent is a **team** label, and ⛔ **a node name may never stand in for one** (R-13 rule 1, S-36);
- ⛔ **no name is ever an identity** — selection, pubkey requests and key grants are keyed exclusively by the full
  hash, and the full hash stays visible at every irreversible act (R-13 rules 4-5);
- ⛔ **no automatic pubkey resolution** — §no-auto-reqpubkey stands; the operator authorises it explicitly (R-7);
- ⛔ **no silent reactivation of a retained key** by mere knowledge of the public team_id (R-6, P-2b) — and ⛔ **no
  silent eviction from a full keyring** (P-15);
- ⛔ **no `JOIN COMPLETE`, no `KEY RECEIVED` on the granter, and no `KEY SENT` before a correlated `send_aired`**
  (R-8) — there is no e2e ack on a grant;
- ⛔ **no auto-refresh of NEARBY** (frozen per entry) — ⛔ **CORRECTED, KEPT VISIBLE: the draft said "of either list";
  INVITE DOES refresh locally** while its window is active, transmitting nothing (R-10);
- ⛔ **no invitation carrier on the wire**, ⛔ **no cross-PHY onboarding**, ⛔ **no Crockford-Base32 rendering and no
  one-button character entry** — all four are named in §3.6.4 as later or out of scope (`:830-837`);
- ⛔ **WITHDRAWN, KEPT VISIBLE:** the first pass excluded `SAVED KEY FOUND` / `USE SAVED KEY` and `FORGET KEY`.
  K5 and K6 now implement those deliberately separated acts; what remains forbidden is silent activation or silent
  eviction, not the explicit verbs;
- ⛔ **no widening of the §UI-17 R-7 wake scope** — a `team_key_received` push does not wake a dark panel in v1 (K4);
- ⛔ **no unification of the two team-id spellings and no deletion of `TeamRow::score_q4`** — both are refactors
  already claimed by §UI-17 F-6, and ⛔ neither rides a slice here (C1);
- ⛔ **no `MR_EMIT` on any new core path** — telemetry would re-anchor the team scenarios in the same run as the
  behaviour change and make the delta unattributable (C4 applied to telemetry).
