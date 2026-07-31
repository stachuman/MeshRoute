<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# DESIGN SPEC — encrypted channel posts (`send_channel -e`) + the location-requires-encryption rule

*2026-07-30. Owner-requested. Line references code-verified today against HEAD `3431e30`; **re-verify before acting
(V1/V2)** — this tree moves several times a day.*

**Status: SPEC'D, NOT DISPATCHED.** No file contention with the address book (`2026-07-29-…`), but see §3 for the
ordering that matters.

> **Owner's words (2026-07-30):** *"We already have teams priv key - but channel messages are lacking -e option at
> all - channel message should accept both -t and -e - then - if location is attached to DM, such message would
> refuse to send if not encrypted."*

---

## 0. Why — two leaks, one of them live today

**(1) A channel post cannot be encrypted at all.** The team content key shipped (`§team-ch-key` T-K1/T-K1b/T-K3)
and nothing can use it: `send_channel` has no `-e`. The **contract says so in its own words** —
*"`send_channel <ch> "<text>"` # **no ack/enc**"*. So every team channel post is in clear, on a shared PHY, to
anyone in range. This is the console half of **T-K2** (`2026-07-26-team-encrypted-channel-design.md` §2.2).

**(2) ★★ `loc_in_dm` leaks coordinates in the clear — this is live.** `node_mac.cpp:149-152`:

```cpp
if (app_dm && _cfg.loc_in_dm && (_cfg.lat_e7 != 0 || _cfg.lon_e7 != 0)) {
    const size_t with_loc = …;
    if (with_loc <= protocol::max_payload_bytes_hard_cap) item.flags |= DATA_FLAG_LOCATION;
}
```

The gate is `app_dm && loc_in_dm && has-a-fix && it-fits`. **There is no crypt check**, and the seal decision
happens *after* (`:154` onward). So on a node with `loc_in_dm = 1`, a **plaintext** DM — `e2e_dm` off, no `-e`, or
simply no peer key — flies with a 6-byte location **in the clear** (`frame_codec.cpp:953`, the unsealed pack path).
⚠ **And `node_carriers.h:233` claims the opposite**: *"piggyback location on originated DMs (DATA_FLAG_LOCATION,
**sealed inner**)"* — true only of a CRYPTED DM. **V1 drift, and it is why the leak reads as intended behaviour.**

## 1. Present state (code-verified 2026-07-30)

| | |
|---|---|
| `send_channel` flags | **`-t`** (TEAM plane) and **`-g`** accepted; plain ⇒ GLOBAL, `-t` ⇒ TEAM only, `-t -g` ⇒ BOTH (§S7 T-B). ★ **No `-e`.** |
| ⚠ contract drift | `INBOX_SYNC_CONTRACT.md:28` says *"`send_channel`/`send_layer` **REJECT** -t"* — **false for `send_channel`**. `console_parse.cpp:121` shows only `send_layer` rejects it. **Fix while here (V1).** |
| location on a DM | set on a **size** check only; **no crypt gate**; unsealed pack path live at `frame_codec.cpp:953` |
| location, sealed case | rides the **origin-onward sealed region** (`frame_codec.cpp:1018`, `node_hashlocate.cpp:414`) — correct today |
| the team content key | `team_ch_pub`/`team_ch_priv` + `team_channel_key_present()`, canonical RFC-7748 clamped (T-K1) |
| `team_channel_crypt` | **does not exist yet** — T-K2 §2.5 specifies it, default ON when a key is held |

## 2. Design

### 2.1 `send_channel -e` — the console half of T-K2

**The crypto is already specified: `2026-07-26-team-encrypted-channel-design.md` §2.2. Do not re-design it here.**
That section owns the `channel_flavor_crypted` bit, sealing to `team_ch_pub` via the existing sealed-sender path
(U2 — the team is "a recipient"), the `[inner_type u8][payload]` inner, the un-keyed-receiver drop + the
`team_channel_no_key` push, and `record_channel(…, enc=1)`.

★★ **Its #1 review point stands and is this slice's main risk: the NONCE.** `dm_nonce` binds a **ctr**, but a
channel post's natural identity is the 32-bit `channel_msg_id`. The nonce must be derived from material that is
**unique per post AND available to every reader**. If `channel_msg_id + origin hash` does not fit `dm_nonce`'s
shape, **carry a `seal_ctr` in the body exactly as `SEALED_RELAY` does** — do not improvise a third scheme.
⚠ **Nonce reuse under a static per-pair key is catastrophic** (see `node_hashlocate.cpp`'s R7 all-zero-seed
refusal for the precedent: it refuses rather than seals under a degenerate nonce). **Refuse, never reuse.**

### 2.2 ★ The flag matrix — and the combination that must REFUSE

`-e` interacts with an existing plane select, so spell out all four cases:

| invocation | behaviour |
|---|---|
| `send_channel <ch> "…"` | GLOBAL, plaintext — **unchanged** |
| `send_channel <ch> "…" -t` | TEAM, plaintext — **unchanged** (a keyless member still posts, and keyholders read it: plaintext is always openable) |
| `send_channel <ch> "…" -t -e` | ★ **the new capability** — TEAM, sealed to `team_ch_pub` |
| `send_channel <ch> "…" -e` (no `-t`) | ❌ **REFUSE loud.** There is no key for a global channel — the only content key is the *team* key. T-K2 §2.2 already requires rejecting `crypted && !team` at origination |
| ★★ `send_channel <ch> "…" -t -g -e` | ❌ **REFUSE loud.** This is the trap: BOTH means one copy to the team and one **global in clear**. Sealing the team copy while airing identical content globally **defeats the encryption completely** — an eavesdropper reads the global copy. **The refusal must say why**, or an operator will read it as an arbitrary limitation |

★ **`team_channel_crypt` (T-K2 §2.5) default-ON is what makes `-e` mostly unnecessary in practice:** when the node
holds a key, `-t` seals by default and a *plaintext* team post needs an explicit opt-out. `-e` then exists to be
**explicit** and to fail loud when sealing is impossible. ⇒ decide whether the opt-out is a new flag or
`cfg set team_channel_crypt 0`; **QA recommends the config toggle only** — a per-send "send this one in clear"
flag is a footgun on a privacy feature.

### 2.2.1 ★★★ OWNER CORRECTION 2026-07-31 — `send_channel -t -l -e` IS THE TARGET, and it REVERSES §2.3

> **Owner:** *"`send_channel -t -l -e` — this is what I want to achieve — sending together with channel encrypted message
> my location. `send_channel -t -l` — that would be refused — location can be sent only encrypted."*

★★ **This strikes §2.3's ruling that `-l` is NOT on `send_channel`.** My argument there was that T-K2 makes location an
**alternative** payload so `-l` would mean a different thing — and the *reasoning was sound about T-K2 as written*, which
is precisely why T-K2 has to change rather than the requirement.

**★★ THE REAL PROBLEM, and it is a payload-format problem, not a flag-letter problem.** T-K2 §2.2 defines the sealed
channel inner as `[inner_type u8][payload]` with **`0` = text, `1` = location** — an **XOR**. ⇒ *"location TOGETHER WITH
the message"* is **not representable**. A flag letter cannot fix an encoding that has no room for the combination.

★ **And the timing is fortunate: T-K2 IS NOT BUILT** — `channel_flavor_crypted` / `team_channel_crypt` /
`team_channel_no_key` have **zero hits in the tree** (QA-verified 2026-07-31). **The `inner_type` encoding is still free.**
Fixing it now, before it ships, is the whole difference between this and the two codepoint spaces that already exhausted.

**Three ways to encode it, and the choice matters more than it looks:**

| | encoding | verdict |
|---|---|---|
| (a) | a **third enumerator**, `inner_type = 2` = location+text | ❌ **This is exactly how `q_opcode` died.** An
ENUMERATED space must spend a codepoint per **COMBINATION** — text, loc, text+loc, telemetry, telemetry+loc, waypoint,
waypoint+loc… Both prior exhaustions in this project (**DATA flags byte = `0xFF`, FULL**; **`q_opcode` = 2 bits, FULL**)
happened for this reason |
| ★ (b) | **`inner_type` becomes a FLAGS byte** — `bit0` = text present, `bit1` = location present | ✅ **QA
RECOMMENDATION.** Each *feature* costs one bit and every *combination* is free. Two bits used, six spare, and the
combinatorial explosion never happens |
| (c) | two separate posts (text, then location) | ❌ doubles airtime, and it is **not atomic** — a reader can get the
text without the position, or the position attributed to the wrong message |

**If (b): the layout must MIRROR THE DATA INNER, and that is a consistency argument, not a taste one.**
`[flags u8][location 6 B if bit1][text if bit0]` — **fixed-size field first, variable last**, exactly as the DATA inner
orders `[dst_hash?][origin][source_hash?][location?][body]`. One mental model then covers both planes.
⚠ **`flags == 0` (neither text nor location) must be REFUSED** — an empty post is a bug, not a feature.

★★ **A SECOND INCONSISTENCY THIS EXPOSED, and it would have shipped: T-K2 sketches the channel location as `lat_e7 i32,
lon_e7 i32` = 8 BYTES, but the DM path carries `pack_loc6` = 6 BYTES** (~11 m quantisation, `frame_codec.cpp`).
⇒ **two different location encodings on two planes**, which would force the companion to carry two decoders and would
make one plane silently more precise than the other. **Use `pack_loc6` on both (U1).** T-K2 §2.2 must be corrected.

**The `send_channel` matrix gains these rows:**

| invocation | behaviour |
|---|---|
| ★ `send_channel <ch> "…" -t -l -e` | ✅ **THE TARGET** — sealed team post carrying text **and** the sender's position |
| `send_channel <ch> "…" -l` (no `-t`) | ❌ **REFUSE** — no team ⇒ no content key ⇒ it cannot be sealed, so it cannot
carry a position. Same reason `-e` without `-t` refuses |
| `send_channel <ch> "…" -t -g -l -e` | ❌ **REFUSE** — already refused for `-t -g -e`; with a position the global copy
would air **coordinates** in clear, so the reason is stronger, not different |
| `send_channel <ch> "…" -t -l` with **no fix** (`lat_e7 == 0 && lon_e7 == 0`) | ❌ **REFUSE `no_location`** — identical
to `send -l`, reusing the enumerator CL3 appended |
| `send_channel <ch> "…" -t -l` that **will not be sealed** | ❌ **REFUSE** — see the OPEN DECISION below |

★★ ~~**O6**~~ ✅ **RULED 2026-07-31 — (ii), refuse only if the post WILL NOT ACTUALLY BE SEALED.** ⇒ **one rule for both
planes**, letter-for-letter with `send -l`:

```
-t -l    + key held, crypt on   ->  OK   (sealed by the node default)
-t -l    + no team key          ->  REFUSE  unsealable
-t -l    + team_channel_crypt 0 ->  REFUSE  unsealable
-t -l -e                        ->  OK   (explicit)
-l       (no -t)                ->  REFUSE  unsealable  (no team => no content key)
-t -g -l -e                     ->  REFUSE  (the global copy would air COORDINATES in clear)
-t -l    + no fix (0,0)         ->  REFUSE  no_location
```

★ **Why this is the right shape and not merely the lenient one:** the rule being enforced is *"a position never travels in
clear"*, and that is a property of **what happens on the wire**, not of which letters the operator typed. Refusing a send
that **would have been sealed** protects nothing and would make the channel plane behave differently from the DM plane
for the **same flag** — the class of inconsistency this whole arc has been closing.
⚠ **Implementation consequence:** the refusal must be decided **after** the effective-crypt decision is known (key held
&& `team_channel_crypt`), exactly as CL3 had to hoist `want_crypt` **above** the location gate in `enqueue_data`. **That
hoist is the precedent to copy (U1) — and getting the order wrong is exactly how B0 became a live leak.**

*(the two readings weighed, kept as the record:)*
- **(i) STRICT — `-l` always demands an explicit `-e`.** Simple to state. ⚠ But `team_channel_crypt` is **default-ON**
  (T-K2 §2.5), so with a key held `-t` **already seals**; under (i) the common case `-t -l` is refused and the user must
  always type `-e`. It also **contradicts CL3 as built**, where `send -l` succeeds under `e2e_dm` with no `-e`.
- ★ **(ii) CONSISTENT — refuse only if the post WILL NOT actually be sealed** (no key, or crypt disabled). `-t -l`
  then succeeds exactly when the content is genuinely encrypted, which is what the privacy rule requires, and it matches
  `send -l` letter for letter. **QA RECOMMENDS (ii)**: it satisfies *"location only encrypted"* without making the
  channel plane behave differently from the DM plane for the same flag.

**Slice impact:** `-l` on `send_channel` **cannot land before CL2** (there is nothing to seal into until then), and the
`inner_type` decision belongs to **T-K2/CL2**, not to a later T-K5. ⇒ **§2.3's "that belongs to T-K5" line is struck**;
the encoding must be settled *when CL2 builds it*, because changing it afterwards is a wire change.

### 2.3 ★★ Location becomes a PER-SEND flag `-l`, `cfg set loc_dm` is REMOVED, and location requires encryption

> **Owner ruling 2026-07-30 (second pass):** *"we have to remove `cfg set loc_dm` and instead use one more switch:
> `-l`."*

★★ **This ruling STRIKES open decision O1.** The blast radius that worried me — *"`loc_in_dm = 1` means
encrypted-DMs-only"* — existed **only because the toggle was global**: with it on, every plaintext DM refused. With
`-l` the intent is **per message**, so a refusal is attributable to the one send that asked for a position, and an
ordinary `send` is untouched. **Strictly better than the design it replaces; O1 is struck, not deferred.**

★ `-l` is **free** — existing letters are `a` `e` `t` `g` `K` (`console_parse.cpp:119-123`).

| invocation | behaviour |
|---|---|
| `send <dst> "…"` | no location — **unchanged**, plaintext fine |
| `send <dst> "…" -l -e` | ✅ location attached, sealed |
| `send <dst> "…" -l` with `e2e_dm` on | ✅ sealed by the node default |
| `send <dst> "…" -l` that would go **plaintext** | ❌ **REFUSE loud** — the rule |
| `send <dst> "…" -l` with **no fix** (`lat_e7 == 0 && lon_e7 == 0`) | ❌ **REFUSE loud** — you asked for a position and there is none (C2: never silently send without it) |
| `send <dst> "…" -l` where **+6 B does not fit** | ❌ **REFUSE loud.** ⚠ Today a **silent drop** (`node_mac.cpp:152`, *"drop the best-effort piggyback"*). With `-l` explicit, best-effort becomes fail-loud — **that is the point of the flag** |

⚠⚠ **SUPERSEDED IN PART BY §2.2.1 (owner correction 2026-07-31): `-l` IS wanted on `send_channel` after all** — the
"do not overload it" reasoning was sound about T-K2 *as written*, so **T-K2's payload changes instead.** The `send_layer`
correction below still stands.

★★ **CORRECTED 2026-07-31 AS BUILT — `send_layer -l` REFUSES.** The verbs are **`send` (id and hash) only**. The dispatch
brief asserted the sealed cross-layer path already carried location (`node_hashlocate.cpp:414`); it does **not** — that line is
inside `e2e_seal_inner`, which returns 0 for `CROSS_LAYER` (`:382`). The real sealed-XL path `build_sealed_relay_body` (`:500`)
hard-codes `lat=0, lon=0`, and the SEALED_RELAY body `[seal_ctr 2][seed8 8][ct‖tag]` **carries no flags word**, so a LOCATION bit
set on the seal side alone would make the peer parse 6 position bytes **as message text**. ⇒ carrying a position cross-layer needs
a **body-format change** = a wire change, its own slice (C1/C4). ⚠ There is also a **THIRD** XL builder the spec never named:
`delegate_send_layer` (`node_mac.cpp:742`), alongside `enqueue_cross_layer` (`:483`).

★ **Verbs (as designed): `send` (id and hash) and `send_layer`. ⚠ NOT `send_channel` — do not overload it.** T-K2's channel
location is `inner_type = 1`, an **alternative** payload (`[inner_type][payload]`, 0 = text **or** 1 = location),
not something *added* to a body — so `-l` would mean a different thing there. **That belongs to T-K5**; §2.4's
crypt rule applies when it lands. Overloading one letter with two meanings is exactly the ambiguity this arc keeps
paying for.

**Refusal shape:** reuse `SendFailReason::unsealable` for the not-sealed case (U1 — no new enumerator, see §5).
The no-fix and does-not-fit cases are distinct user errors and want their own text. ★ **The refusal names `-e` or
`e2e_dm` as the fix — and since `cfg set loc_dm` no longer exists there is no "turn it off" escape to advertise,
because there is nothing to turn off.** That is the cleanliness the ruling buys.

### 2.3.1 Removing `cfg set loc_dm` — wider than a console key

**TWELVE surfaces (corrected 2026-07-31 — the count stopped at MeshRoute’s boundary), and one is an app-facing binary contract:**

★ **The twelfth is in the SIMULATOR:** `orchestrator/runtime/NodeRuntimeWrapper.cpp:412` maps the scenario config key
`loc_in_dm`, so deleting `NodeConfig::loc_in_dm` **breaks the sim build**. No scenario JSON sets it — which is exactly why the
corpus prediction was byte-identical.

| surface | note |
|---|---|
| `lib/core/node_carriers.h:233` | the field. ★ Its *"sealed inner"* comment is **true only of a CRYPTED DM** — the V1 drift that made this leak read as intended |
| `lib/core/node_mac.cpp:149` | the attach gate → becomes per-send |
| `src/firmware_config.cpp` | the `cfg set` setter **and** the blob seed |
| `src/firmware_commands.cpp` | the `cfg` dump line **and** the `cfg set` key list |
| `src/fw_main.cpp` | boot restore |
| `src/device_nv.h` | the NV field ⇒ ★ **`kVersion` 22 → 23** |
| ★★ `lib/console/console_binary.h` + `.cpp` | **`CfgOut.loc_dm` and its `TAG_CFG_*` TLV**, in `enc_cfg` **and** `dec_cfg` — an **app-facing** field |

⚠ **`kVersion` 22 → 23 is another reprovision-on-reflash.** Cheap per the standing undeployed ruling — T-K1 already
did one — but the companion must expect an unprovisioned node again, and it should be a stated consequence.

★★ **THE TLV HAZARD — the Q-opcode lesson again: a RETIRED tag number must NEVER be reused.** Dropping
`TAG_CFG_LOC_DM` leaves a hole; if a later slice recycles that number, an **older app still sending the old tag
silently sets whatever replaced it** — a codepoint whose meaning changed under a peer, the class that already cost
this arc a slice. ⇒ **mark the number RETIRED in-source beside the enum; do not merely delete the line.** `dec_cfg`
already ignores unknown tags (`default: break;`).

**Rule (owner-ruled 2026-07-30):** if location would be attached to a DM and the DM will **not** be sealed, the
send is **REFUSED**, loudly, with its own reason.

Three candidate behaviours; the owner chose the first, and it is the right one:
1. ✅ **refuse the send** — the app learns nothing was sent, and the user's privacy intent is honoured;
2. ❌ silently omit the location — the app believes it shared a position it did not;
3. ❌ send it in clear — **today's behaviour**.

**Implementation shape.** The crypt intent is **already a parameter of `enqueue_data`**, so the check is local:
compute `want_crypt` (the same expression `:154` onward uses — extract it once, U1, do not duplicate) **before**
the `:149` gate, and if `loc_in_dm && has-a-fix && !want_crypt` ⇒ refuse. **Do not** set `DATA_FLAG_LOCATION` and
hope the seal happens.

⚠ **A seal can still FAIL after the intent is set** (`no_pubkey`, `no_identity`, `bad_rng`). Those already refuse
loud today, so a location-bearing send inherits that correctly — **verify it, do not assume it**, and make sure the
reported reason is the *seal* failure rather than the location rule, or the operator will chase the wrong thing.

★ **Kept as the record:** with the *global* toggle this rule made `loc_in_dm = 1` mean "encrypted DMs only" —
every plaintext DM from a node with a fix would refuse. That was **O1**. The `-l` ruling removes the toggle and with
it the problem.

★ **Bonus: the fix makes an existing comment true.** `node_carriers.h:233`'s *"sealed inner"* claim becomes
unconditionally correct. Update it from a claim to a guarantee (V1).

### 2.4 The same rule for a channel post's location inner-type

T-K2 §2.2 defines `inner_type = 1` as location. **A location in a plaintext channel post is the same leak, broadcast
wider** — a channel post goes to every member, and in clear to everyone in range. ⇒ **`inner_type = 1` requires the
crypted flavour; refuse otherwise**, with the same reason shape as §2.3. This is cheap to include now and awkward to
retrofit once the app starts sending positions.

### 2.5 Follow-on the slice should NOT take (C1)

★ **CORRECTED 2026-07-31: `frame_codec.cpp:953` is the *PARSE* path** (`parse_unicast_inner`, 915–964) and must stay live —
the receive side still decodes a peer’s location. **The unsealed *pack* site is `:1018`** (`pack_unicast_inner`, 990+).
Once §2.3 lands, **`:1018`** — the **unsealed** LOCATION pack path — becomes unreachable for DMs.
**Leave it.** Deleting dead-but-reachable-looking codec code is its own slice, and `pack_data` is shared. **Mark it
`✖ MISSING` in-source with the reason** so the next reader knows it is deliberate, per the mark-done-vs-missing rule.

## 3. Slices, and the ordering that matters

- **CL1** — `send_channel` gains `-e` **parse + the four-case refusal matrix (§2.2)**, with `-t -e` wired to a
  **stub** that refuses `unsealable` until CL2 lands. Contract drift on `-t` fixed. **Gate: corpus byte-identical**
  (`lib/console` + `src/` are outside the sim build), native for all four cases.
- **CL2** — the T-K2 crypto: `channel_flavor_crypted`, seal/open, **the nonce design (§2.1 — the review point)**,
  the un-keyed-receiver drop + `team_channel_no_key`, `record_channel(enc=1)`, `team_channel_crypt` default-ON.
  ⚠ **Expect a re-anchor of every team-channel scenario** (s28/s29 hold keys); QA owns the scenario edits.
- ✅ **CL3 — BUILT 2026-07-31 (`§loc-per-send`), QA GO, register B0 CLOSED.** 36/36 byte-identical, keystone unmoved, native
  1006/70360 → 1012/70417, `sizeof(Node)` −8 ⇒ six-env grid taken. **As-built deltas:** `send_layer -l` refuses (above); the
  third refusal is **structural, not a branch** (measured unreachable — the seal refuses at body 211, a gate there could only fire
  above 226); `SendFailReason::no_location` **appended**. Evidence: `simulation/BASELINE.md` note `LOC-PER-SEND`.
- **CL3 (as designed)** — §2.3: **add `-l` to `send`/`send_layer`, REMOVE `cfg set loc_dm` and all eleven surfaces (§2.3.1),
  `kVersion` 22 → 23, retire the TLV number**, and enforce the three refusals. ⚠ **Bigger than it looks** — a
  config-surface removal plus an NV bump, not just a flag. (+ §2.4's channel case once T-K2 has landed.)

★ **CL3 is independent of CL1/CL2 and is the only one closing a LIVE leak** — take it first if the owner wants the
privacy hole shut before the feature. CL1 alone is inert scaffolding; CL2 is the substantial slice.

## 4. Open decisions

| | decision |
|---|---|
| ~~**O1**~~ | ✅ **STRUCK 2026-07-30** by the `-l` ruling (§2.3) — location is per-send, so there is no global toggle and no blast radius to accept |
| ~~**O4**~~ | ✅ **RESOLVED 2026-07-31 — BOTH.** `0x18` is marked **RETIRED — NEVER REUSE** in-source beside the tag enum *and*
stated in `INBOX_SYNC_CONTRACT.md`; belt and braces cost nothing and the Q-opcode lesson says the number must never come back |
| **O4 (as asked)** | `TAG_CFG_LOC_DM`'s retired number: mark RETIRED in-source (QA's recommendation) or also formally reserve it in the contract? |
| **O2** | The plaintext-team-post opt-out: **`cfg set team_channel_crypt 0` only** (QA's recommendation) or also a per-send flag? |
| **O3** | Should `send_channel -a` exist too? The contract's *"no ack/enc"* covers both, and a channel post has no single recipient to ack — **QA recommends NO**, recorded so it is not re-asked |

### 4.1 ★ A `wire_version` bump is NOT a blocker (owner, 2026-07-31)

> **Owner:** *"No wire version bump required, MeshRoute is NOT shipped, it lives only on my test hardware."*

★★ **Read precisely, because C4 bundles two costs and only one is void.** **(1) Fleet reflash — VOID.** There is no
fleet; reflash-all is always available, so a bump is **free to deploy** and is **never a reason to stop or to contort the
design** to avoid one. **(2) Attribution — STILL REAL, and it is a GATE cost, not a deployment one:** a `wire_version`
bump **re-anchors all 36 streams at once**, and when everything moves you cannot tell a genuine regression from the bump.
⇒ **if CL2 needs a bump, take it — but as its OWN slice/commit**, so the behaviour change is measured against a corpus
that moved for exactly one reason. **One extra commit, not a stop.**
⇒ **CL2 may therefore choose the RIGHT wire shape** (e.g. §2.2.1's flags byte) instead of the shape that happens to fit
a spare bit — which is the whole reason the two prior codepoint spaces reached exhaustion.

## 5. Gate expectations

Standard gate — ★ **read `docs/2026-07-26-slice-gate-method.md`, and §0 of `docs/2026-07-30-open-bug-register.md`
for the full dispatch contract** (QA-owned files, the git prohibition, `rm`-before-build, the three-board rule, the
flash noise floor). Specifics here:
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move**, and **no static-only scenario may move** — `loc_in_dm`
  and the team channel are both off by default in the static corpus. If one moves, **stop and report**.
- ★★ **The four detector probes are a hard item** — table in the bug register §0.
- **CL2 will re-anchor team-channel scenarios.** Attribute every mover; `dm_delivery_breakdown` should stay
  `diff`-identical (this changes *content*, not delivery).
- ★ **The location rule needs a NEGATIVE test:** a `loc_in_dm` DM that would go plaintext must **refuse**, and the
  test must assert **no frame was aired at all** — not merely that the location was absent. Show the before-arm
  (revert the guard) airing a frame with the 6 location bytes **in the clear**.
- ★ **If an enum is extended, APPEND** — the sim bridges `PushKind`/`SendFailReason` on their raw `uint8_t` with a
  `static_assert` twinned in two sim files. Prefer **reusing `SendFailReason::unsealable`** for the refusals here.
  And note `-Wswitch` fires for these **only on the board build** (`fw_main.cpp` is outside native).

## 6. Contract additions owed (QA writes them)

The `send_channel` grammar with `-e` and the four-case matrix; the corrected `-t` claim at line 28; the location
refusal reasons; ★ **the `-l` flag on `send`/`send_layer`**, and **the REMOVAL of `loc_dm` from the cfg surface AND the app-facing binary TLV** (an app reading it must stop) — with `kVersion` 22 → 23 meaning **another unprovisioned node on first contact after this flash**; `enc:true` on `channel_recv` /
`inbox_channel` (T-K2 already reserves it); and the `team_channel_no_key` push.
★ **And the app-facing consequence of §2.3, stated plainly**: a `-l` DM to a peer whose key is not
held **will refuse** — so the app must surface `reqpubkey`/QR rather than retrying.
