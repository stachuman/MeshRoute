// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# R6 — Leaf-config membership join (the final port slice) — implementation spec

> ## ⏩ PROGRESS HANDOVER (2026-06-20, survives context-compaction — read this first)
> **R6.1 — COMMITTED** (`9c80173 R6.1`). Shipped: BCN +6-B leaf header `{lineage_id u16, config_epoch u16, config_hash u16}` (`BCN_LEAF_HEADER_LEN=6`, hash `[:2]`); the `ingest_beacon` membership filter; F-frame `+config_hash` (7→9 B) + the `handle_f` divergence gate (1-hop flood containment); the join-participation gate (`leaf_config_synced()`); NV **v14** (`lineage_id` u16 / `config_epoch` / `leaf_name`); `leaf create`/`leaf name` console cmds.
> **R6.2 — DONE, GREEN-SHAPED, UNCOMMITTED** (on the tree; the user does commits). Shipped: `q_opcode::config_pull=2` (+`{lineage,epoch}` on Q); `DATA_TYPE_CONFIG_ANSWER=6` + `pack_/parse_config_answer` (body = `{lineage,epoch,sf_bitmap,duty_ppm,leaf_name}`); pull triggers in the beacon filter (join-pull = unmanaged hears managed on its leaf → adopt lineage + pull; stale-pull = higher epoch → pull; rate-limited `config_pull_retry_ms`); `handle_q` config_pull → `send_config_answer` (any synced member answers); data-RX TYPE 6 → `adopt_config_answer` (→ `config_adopted` Push → fw_main persists NV); **J `wire_version`** in byte-1 rsv nibble (+0 B), `handle_j` rejects mismatch. **3 open-call decisions ACCEPTED by user:** (1) wire_version = rsv-nibble +0 B; (2) CONFIG_ANSWER = full `{lineage,epoch,config}` tuple; (3) join-intent = lineage-0 hears managed beacon on its own leaf_id → auto-adopt+pull (note: two managed lineages on one nibble = first-heard-wins, rare misconfig, fine).
> **R6.2 gate result:** `pio test -e native` = **467/467**; 4 boards build (gateway/xiao_sx1262/heltec_v3/xiao_esp32s3); **s22** (new join scenario) = J pulls→adopts(lineage 41153)→delivers; suite **identical to the R6.1 6-B numbers** (s18 98, s15 34, s16 40, s17 26/30 same-layer) with **0 spurious `leaf_config_conflict`** (gates inert in unmanaged baseline); **s21** still gates (24 conflicts). New native tests in `test_frame_codec.cpp` (Q config_pull, CONFIG_ANSWER body) + `test_node_r3.cpp` (pull/answer, participation block/allow, F-gate). New scenarios: `simulation/s21_leaf_config_divergence.json`, `simulation/s22_leaf_config_join.json`.
> **CROSS-REPO (`~/lora-universal-simulator`):** `CMakeLists.txt` lists `leaf_config.cpp`; `orchestrator/runtime/NodeRuntimeWrapper.cpp` maps `lineage_id`/`config_epoch`/`leaf_name`. **`lus` MUST be rebuilt** (`cmake --build build --target lus -j4`) to reflect any `lib/core` change before gating — the prebuilt binary is stale.
> **PENDING (the gate's call, carried since R6.1):** §0 **re-baseline** — the +6-B FLAG-DAY shifted the suite (s18 single-seed 98 vs pre-R6.1 108, but multi-seed mean ~101 and keystone seed 157373 = **110 ≥ 108**; the moves are airtime+**chaos**, not regression — 0 spurious conflicts prove the gates don't interfere). Do the multi-seed A/B (stash to pre-R6.1, keep `protocol_constants.h`, rebuild lus) and **RE-CAPTURE `simulation/BASELINE.md`** once acceptable.
> **NEXT = R6.3** (§3 R6.3 below): dynamic config write (`cfg set` leaf-field → `epoch=max_seen+1`, recompute hash, LWW by `key_hash32`) + `leaf create` catastrophe re-mint + the gateway 1..16 id reservation (`docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md`). §6.4's honest-over-eager-node tuning stays OUT of R6.

**Status:** READY TO IMPLEMENT (stepped) — cleared for handoff to the coding agent. **Division of labour:** a separate coding agent implements; the **quality-gate runs the §0 delivery gate per slice** (the coder hands off each of R6.1/R6.2/R6.3 green-shaped; the gate verifies against `BASELINE.md` before the next slice starts). The DESIGN is already ratified — this is the build plan. Design of record:
`docs/specs/2026-06-05-identity-leaf-membership-join-design.md` §3/§4/§5.2 + its **Phase-0 resolutions** (top banner, 2026-06-15, which LOCK the wire values) + the Phase-0 doc `docs/superpowers/specs/2026-06-15-join-e2e-phase0.md`. This spec turns that design into ordered, gateable slices.

**Why this is the final slice (verified 2026-06-20, 4-agent audit + firsthand):** everything else in the identity/join workstream is DONE and on metal — Slice A identity, A2 sim seed seam, **E2E DM crypto (sealed-sender 1a/1b/1c/2)**, and **node_id DAD + heal** (key-only tiebreak, the §5.5 beacon-guard fix is IN). The leaf-membership / config-fingerprint plane is **0% built**: the ENTIRE membership filter today is `node_beacon.cpp:361` `if (b.leaf_id != _cfg.leaf_id) return;` — the 4-bit nibble, nothing more. That is exactly the §3.1 pre-design failure (two same-nibble nodes with divergent `data_sf_list` silently break the layer's SF negotiation). R6 is the misconfiguration gate that closes it.

---

## 0. THE GATE — delivery-breakdown, NOT byte-identical (decided 2026-06-20)

R6.1 adds a **fixed +6-B leaf header to every beacon** (§2, right-sized from 10 B) plus +2 B to F ⇒ it is a **wire FLAG-DAY** (reflash-all) and **every beacon's bytes change**, so the s18 `306c3cf4` byte-identical md5 gate is **RETIRED for this work**. The gate is now the **delivery breakdown** via `tools/dm_delivery_breakdown.py` against `simulation/BASELINE.md` — the same discipline the routing-liveness plane uses.

**Per scenario (the gate command):**
```
lus -e meshroute simulation/<s>.json /tmp/<s>.ndjson
python3 tools/dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson --failures
```
**Pass = no regression vs the `simulation/BASELINE.md` reference numbers + `leaks == 0`**, across the suite (s18, s19, s09, s10, s16, s15, s17). Plus: `pio test -e native` green, and **4 boards compile** (`gateway xiao_sx1262 heltec_v3 xiao_esp32s3` — the ESP32-S3 target landed this cycle).

**Re-baseline protocol (do this at R6.1, once):** the +6-B header shifts beacon airtime/timing, so the delivery numbers move (less than the 10-B impl did). After R6.1 lands and is shown **stable-or-better vs the OLD baseline** (no material delivery regression from the extra airtime, `leaks==0`), **RE-CAPTURE `simulation/BASELINE.md`** as the new reference; slices R6.2/R6.3 gate against the new numbers. Document the old→new delta in the BASELINE.md note. This is the keystone shift from byte-identical to **semantic parity** — state it in the commit.

---

## 1. Current state (verified 2026-06-20) — what exists to build on

- **Beacon wire (legacy 8-B header):** `frame_codec.h` byte-map — `byte0 cmd|leaf_id · byte1 src · byte2 flags{has_schedule b7|self_gateway b6|is_mobile b5|has_seen_bitmap b4|has_ext b3|n_entries_lo b2..0} · byte3 n_entries_hi · byte4-7 key_hash32`, then body `[schedule] → entries → [seen-bitmap] → [ext]`. `beacon_in`/`beacon_out` have **no** lineage/epoch/config_hash fields.
- **Membership filter:** `node_beacon.cpp:361` `if (b.leaf_id != _cfg.leaf_id) return;` — the only one.
- **Q opcodes:** `q_opcode` enum = `{ req_sync=1, channel_pull=3 }`. `REQ_SYNC` answers with a full-table beacon page (route bootstrap only — carries NO config).
- **DATA types:** stop at **5** (`H_ANSWER_PUBKEY`). TYPE 6 (`CONFIG_ANSWER`) is **not even reserved** — add it.
- **NV Blob:** `device_nv.h` **v13**. Holds `leaf_id`, `allowed_sf_bitmap`, `duty` as flat scalars — **no** `lineage_id`, config `epoch`, or `leaf_name`. (The `claim_epoch` in the blob is node-id DAD, a DIFFERENT epoch — do not reuse it.)
- **BLAKE2b** is available (vendored monocypher; used by `dm_crypto` + `identity`) — reuse it for `config_hash`.
- **`J wire_version`:** a comment-only TODO (`frame_codec.h:308`); J structs carry no version byte.
- **Gateway 1..16 id reservation (G1):** picker still scans `1..254` (`node_join.cpp:86,105`) — folds into R6.2/R6.3 (see [[gateway-node-id-reservation]] + `docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md`).

## 2. Locked wire values (from Phase-0 — do NOT re-derive)

> **REVISION 2026-06-20b — the implemented R6.1 used the values below at their ORIGINAL 10-B sizing with no F gate; this section is now the TARGET. Coder delta vs the shipped R6.1: (1) BCN leaf header 10 B → 6 B; (2) add the F `config_hash` fingerprint + `handle_f` gate; (3) the R6.2 participation gate. See §3/§6.4/§7.**

- **BCN leaf header** `{ lineage_id u16 · config_epoch u16 · config_hash u16 }` = **+6 B** (RIGHT-SIZED, down from 10 B — §7), a FIXED field written **after the 8-B header, before the schedule block** (survives `beacon_max_bytes=151` truncation; NEVER the cut field, §6.1). Sizing rationale: `lineage_id` 2 B (~256 leaves birthday-safe — a realistic mesh has a handful), `config_epoch` 2 B (65 k writes), `config_hash` 2 B (1/65 k missed-misconfig — honest-node-benign per §3.4). `BCN_LEAF_HEADER_LEN = 6`.
- **F leaf fingerprint** = `config_hash u16` appended to the F frame (**+2 B; F: 7 → 9 B**). The membership gate MUST cover F, not just B — F (route-discovery flood) is the bypass around the beacon gate (§6.4). F carries **only** `config_hash` (the divergence detector); `lineage_id`/`config_epoch` are B-only (they drive the adopt/pull lifecycle, which F doesn't do).
- **`config_hash = BLAKE2b( u16 allowed_sf_bitmap LE ‖ u32 duty_ppm LE ‖ u8 leaf_name_len ‖ leaf_name )[:2]`** — low 2 B of BLAKE2b-512 (matching the 2-B wire field; project `[:N]` convention). Hash the **bitmap** (the ascending set `sf_index` uses), NOT an ordered SF list (Phase-0 dropped §3.4's "ordered, don't sort"). `duty_ppm = round(duty_cycle * 1e6)`. Same hash value gates **both** B and F.
- **Epoch bump** = `max_seen_epoch_from_beacons + 1`; **ties → higher `key_hash32` is canonical** (the lower-key node pulls + adopts, keeping the same epoch — NO bump-war). LWW keyed on the **stable `key_hash32`**, never the disposable `node_id`.
- **`CONFIG_PULL`** = a new `q_opcode`; answered by a routed **DATA TYPE 6 `CONFIG_ANSWER`** carrying `{ data_sf_list (=allowed_sf_bitmap), leaf_name, duty }`.
- **`lineage_id`** minted by an operator **`leaf create`** cfg op (random 4 B; two operators "creating leaf N" = two lineages = two leaves, by design).

## 3. Slices (ordered, each independently gateable)

### R6.1 — beacon + F leaf fingerprint + the misconfiguration gate (NO config transfer yet)
The minimum that closes §3.1: nodes advertise their fingerprint on **both** control floods (B and F) and refuse to peer/relay across a config divergence. Uses the LOCAL/`cfg`-set config (no pull yet).

1. **Wire (B):** add `lineage_id`/`config_epoch`/`config_hash` (all **u16**) to `beacon_in`/`beacon_out`; `pack_beacon`/`parse_beacon` emit/read the **6-B** header right after `key_hash32`, before the schedule. `BCN_LEAF_HEADER_LEN = 6`. Native codec round-trip + golden-hex.
2. **`config_hash` helper** (pure, native-testable): BLAKE2b over the canonical bytes, low 2 B (§2). Pin a golden vector.
3. **Peering filter** in `ingest_beacon` (replace the bare `leaf_id` check, §3.3): after the `leaf_id` nibble match —
   - **`lineage_id == 0` on either side** ⇒ UNMANAGED leaf ⇒ legacy behavior: peer iff `config_hash` matches (backward-compat — see §6.2; this keeps every existing scenario peering since they're uniform-config).
   - different `lineage_id` ⇒ **ignore** (foreign leaf, self-isolating).
   - same lineage, same `(epoch, config_hash)` ⇒ peer.
   - same lineage, **higher** epoch ⇒ I'm stale ⇒ mark-stale (the PULL is R6.2; for now: don't peer on the diverging config, emit `leaf_stale`).
   - same lineage, **lower** epoch ⇒ ignore (neighbor heals later).
   - same epoch, **different** `config_hash` ⇒ the §4.1 concurrent-write tiebreak: higher `key_hash32` canonical; log `leaf_config_conflict` (don't peer while it persists).
4. **Wire + gate (F) — NEW (§6.4):** append `config_hash` (u16) to the F frame (`f_in`/`f_out`, `pack_f`/`parse_f`; F 7→9 B). In `handle_f`, after the existing `leaf_id` nibble match, apply the **same divergence check** as the unmanaged-beacon path: if the F's `config_hash != cfg_config_hash()` ⇒ **drop + do NOT relay** (emit `leaf_config_conflict{src,F}`). This closes the route-discovery bypass AND contains a misconfigured node's flood to 1 hop (its neighbors won't propagate it). Native + a sim assertion (divergent node's RREQ is not relayed).
5. **NV (v14):** add `lineage_id` (**u16**), `config_epoch` (u16), `leaf_name` (len+bytes) to the Blob; bump `kVersion 13→14`. `leaf create` cfg op mints a random `lineage_id` (HW-RNG, never 0) + sets the leaf config; persists. (NB: v14 was shipped at u32 lineage — change to u16 before it locks; v14 is uncommitted, no migration.)
6. **Truncation:** the 6-B leaf header is fixed/pre-entries; confirm `pack_beacon` + `beacon_max_entries` spill ROUTE ENTRIES first and the leaf header is never cut (§6.1).
7. **Gate (R6.1 = the re-baseline point):**
   - **Sim peering gate (s21):** two nodes, same `leaf_id`, **divergent `allowed_sf_bitmap`** ⇒ must NOT peer (`leaf_config_conflict`/hash-mismatch) **and the divergent node's F is not relayed**; a matching pair peers + delivers.
   - **Delivery suite:** run the full `BASELINE.md` set; show **no material regression + `leaks==0`** — the 6-B header (down from 10) should shrink the airtime regression the 10-B impl showed (s18 −6 / s15 xl −6 / s17 −2 vs pre-R6.1); **re-measure the true delta via a clean A/B** (stash R6.1, keep `protocol_constants.h`, rebuild sim), then **RE-CAPTURE BASELINE.md** (§0) only once it's acceptably small.
   - native codec/hash goldens; 4 boards compile.

### R6.2 — CONFIG_PULL / CONFIG_ANSWER (learn the config) + J wire_version
1. **Wire:** add `config_pull` to `q_opcode`; add **DATA TYPE 6 `CONFIG_ANSWER`** + its pack/parse body `{ allowed_sf_bitmap, leaf_name, duty_ppm }` (routed DATA). Native round-trip.
2. **Pull/adopt (`node_query.cpp`):** a stale node (heard higher epoch) OR a fresh joiner (epoch 0 / no config) sends `CONFIG_PULL{lineage, epoch}`; **any member at that epoch** answers `CONFIG_ANSWER` (durability §4.2 — config lives in every puller, survives the originator leaving). On receipt: adopt `{config, epoch}`, recompute `config_hash`, persist → beacons now carry the new epoch → it propagates.
3. **Join composition:** this is §5.2 step 2 — a joiner does node_id DAD (already built) AND config-sync (this slice). `claim-after-listen` already provides the listen window the pull needs.
4. **Join-participation gate — NEW (the sender-side half of §6.4):** until a node trying to join a MANAGED leaf has config-synced, it must **NOT originate F or DATA** — only listen + `CONFIG_PULL`. State: "synced" ⇔ `lineage_id == 0` (UNMANAGED — always allowed, backward-compat) **OR** (managed AND has adopted a lineage/config). Gate `do_send`/`enqueue_data`/`emit_route_request` on it; an un-synced originate returns `err_unprovisioned`-style + a `send_failed{reason=joining}` Push. **No chicken-and-egg:** `CONFIG_PULL` is a **1-hop pull from a heard neighbour** (the joiner hears a beacon → asks that neighbour directly), so it needs no F to bootstrap. This stops the pollution at the source; the R6.1 F-gate (step 4 above) is the receiver-side backstop for a node that originates anyway.
5. **`J wire_version` (§5.2 step 4):** add a 1-byte wire-version to the J frame; reject a wire-incompatible peer. (Width: a full byte — the 4-rsv-bit option is an open call, §7.)
6. **Gate:** a fresh node (no lineage) joins a running MANAGED leaf, **pulls config, becomes a member, and delivers a DM on the learned data-SF** — and a sim assertion that **before** sync it originates NO F/DATA (only CONFIG_PULL). Delivery suite no-regress vs the new baseline. native + 4 boards.

### R6.3 — dynamic config write path + gateway id reservation
1. **Write (§4.1):** an operator `cfg set <leaf-field>` (sf_list / leaf_name / duty) → `epoch = max_seen + 1`, recompute `config_hash`, LWW by `key_hash32`. The operator-command gate is the "deliberate intent" marker (a merely-misconfigured node never bumps epoch, so never propagates).
2. **Catastrophe backstop (§4.3):** `leaf create` re-mints a fresh lineage; the dead lineage stops being beaconed + ages out of route/`id_bind`. No auto-migration (a partition looks like leaf-death).
3. **Gateway 1..16 reservation (G1):** fold in the normal-node-reservation change (`join_choose_candidate_id` → pick from `17..254`; `cfg set node_id`/`l1_node_id` ranges) per `docs/superpowers/specs/2026-06-19-normal-node-id-reservation-design.md`. (Independent of leaf-config but naturally batched here.)
4. **Gate:** an operator config change propagates (epoch bumps; neighbors pull + adopt + re-peer); a concurrent-write tiebreak converges one-sided (higher `key_hash32` wins, no flapping). Delivery no-regress. native + 4 boards.

## 4. NV schema (v13 → v14)
Add to `device_nv.h` Blob: `uint16_t lineage_id; uint16_t config_epoch; uint8_t leaf_name_len; uint8_t leaf_name[N];` (pick N ≤ ~16; `lineage_id` is **u16** per the §2 right-size — the shipped v14 used u32, change it). Bump `kVersion 13→14`; the existing `load()` size-check (`n == sizeof(out)`) already rejects pre-v14 blobs → a node with an old blob falls to compile defaults (lineage_id=0 = unmanaged, backward-compat §6.2). `config_epoch` is monotonic on any node that writes (§4.4); rate-limit NV writes (epoch-in-NV pattern) to spare flash.

## 5. Test scenarios to ADD (sim, `engine:meshroute`)
- **`s21_leaf_config_divergence`** (R6.1, ALREADY ADDED — extend it): same-leaf_id nodes, one with divergent `allowed_sf_bitmap` ⇒ must NOT peer **AND its F (RREQ) must NOT be relayed** (assert no `f_rx`/relay of the divergent origin); matching ⇒ peer + deliver. The misconfig-gate proof over **B and F**.
- **`sNN_leaf_config_join`** (R6.2): a fresh node (no config) + a running leaf ⇒ joiner pulls CONFIG_ANSWER, adopts, delivers on the learned SF.
- **`sNN_leaf_config_epoch_write`** (R6.3): operator bump propagates; a forced same-epoch-different-hash pair converges to the higher-key config (no epoch war).
(Source convention: author in the sim's `scenarios/` if Lua-comparable, else MeshRoute `simulation/` for meshroute-only — these are meshroute-only, like t94.)

## 6. Cross-cutting concerns
**6.1 — BCN truncation (⚠ the watch-item, co-design with the beacon byte-budget).** The beacon is already byte-pressured (151 B): header + schedule + entries + seen-bitmap + ext. Adding the leaf header (right-sized to **+6 B**, §7 — the original 10-B impl drove the s18/s15/s17 regression in the first gate run) sharpens it. RULE: the leaf header is a FIXED pre-entries field and must NEVER be the cut — route entries spill first (they already do; confirm). **Pin the overflow priority** for the rest (entries → then what: seen-bitmap or ext?). This MUST be reconciled with the **seen_bitmap cost-reduction** work (`docs/superpowers/specs/2026-06-18-seen-bitmap-cost-reduction.md` — which moves the 32-B bitmap off full-page beacons) and the **channel-flood** digest: all three compete for the same 151 B.
> **R6.1 is NOT blocked on this.** For R6.1 the minimal rule is sufficient and already the behavior: leaf-header is pre-entries + fixed (never cut), route **entries spill first**. The coder proceeds with that. The full {seen-bitmap, ext} cut-order reconciliation is a SEPARATE decision, co-decided when the seen_bitmap slice lands (it only bites when a beacon overflows *with* the header — and the delivery gate will catch any truncation-induced regression). Do not block R6.1 wire work on it.

**6.2 — `lineage_id == 0` backward-compat (load-bearing for the gate).** Every existing scenario + every pre-v14 NV node has `lineage_id = 0` (no `leaf create`). The filter MUST treat 0 as "unmanaged leaf → peer by `leaf_id` + `config_hash` match," so uniform-config scenarios (s18 etc.) keep peering and the delivery baseline doesn't collapse. Only a `leaf create`'d (lineage≠0) leaf gets the full lineage/epoch machinery. This is what makes R6.1's re-baseline a small delivery delta (airtime only), not a peering cliff.

**6.3 — Hot-path cost is bounded** (§9 of the design): the only per-frame costs are **+6 B on B** and **+2 B on F**; CONFIG_PULL reuses Q; CONFIG_ANSWER is a routed DATA; DAD reuses J. Confirm no per-frame cost beyond those.

**6.4 — the membership gate must cover ALL routing participation (B AND F), not just B.** R6.1's beacon gate stops route-establishment *via beacons* from a divergent node — but **F (route-discovery flood) lays routes by flood, bypassing the beacon gate**: a node with only `leaf_id`+`control_sf` and no synced config can flood F and route/talk before it's a member (the §3.1 failure then happens over those F-laid routes). Two complementary gates close it:
  - **(a) receiver-side (R6.1 step 4):** F carries `config_hash`; `handle_f` drops + does-not-relay a divergent F. Bonus: this **contains a misconfig flood to 1 hop** (neighbours won't propagate it).
  - **(b) sender-side (R6.2 step 4):** a not-yet-synced node does not ORIGINATE F/DATA until `CONFIG_PULL` completes. `CONFIG_PULL` is 1-hop (from a heard neighbour), so no F is needed to bootstrap — no chicken-and-egg.
  (b) is the clean primary; (a) is the backstop. NB the F-gate cuts *misconfigured-node* floods; an **honest over-eager configured** node's floods are a separate **tuning** matter (wait-for-beacon + stepped expanding-ring 1→2→3 instead of 1→`dv_hop_cap`), independent of R6.

## 7. Open decisions (resolve in-slice)
- **Flag-day vs conditional header — DECIDED (user): FLAG-DAY, right-sized.** The header is a FIXED field on every beacon; we deliberately **break s18 byte-identical** and gate on delivery (§0). The airtime cost is mitigated by **right-sizing, not a conditional bit**: 2026-06-20b cut it **10 B → 6 B**. The conditional `has_leaf_header` alternative stays **rejected**; do NOT add a flag bit.
- **Header sizing — DECIDED 2026-06-20b: 6 B (2+2+2)** — `lineage_id` 2 B, `config_epoch` 2 B, `config_hash` 2 B (all u16). Honest-node tradeoffs accepted: ~256 leaves birthday-safe; 1/65 k missed-misconfig (per §3.4 "rare, benign"). Fallback if a stronger gate is wanted: `config_hash` 3 B → 7 B (a 3-byte field, less clean). **F carries only the 2-B `config_hash`** (not lineage/epoch). Re-run the delivery A/B after this re-size to confirm the residual regression is acceptable before re-baselining.
- **`J wire_version` width:** full byte vs 4 rsv bits (+0 B). Lean: full byte (room to grow).
- **`leaf_name` max length** (NV + the config_hash input) — pick ≤16 B; it's in the hashed set so a change re-fingerprints (correct).
- **CONFIG_ANSWER carrier:** routed DATA TYPE 6 (Phase-0 pick) vs a Q response body — go DATA TYPE 6 unless the routed-DATA path proves awkward.

## 8. Done = the final port slice closed
All three sub-slices gated (delivery-breakdown no-regress + `leaks==0` + the 3 new scenarios + native + 4 boards), `BASELINE.md` re-captured, NV at v14. At that point the `docs/specs/2026-06-05-identity-leaf-membership-join-design.md` design is fully implemented and PORT_PLAN's R6 is closed — the last unbuilt slice of the Lua→C++ port.
