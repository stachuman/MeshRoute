# node_id Auto-Assignment (DAD + Self-Heal) — Design Spec

**Status:** §13 decisions **SIGNED OFF 2026-06-06** (`claim_guard_ms`=20 s · `denied_ids` age=1 day ·
NV-loss→re-run DAD) — **ready for the port slice** (prereq: the `node_beacon.cpp:203` guard fix, §7).
The dedicated deep-dive §5.3/§7.2 of `2026-06-05-identity-leaf-membership-join-design.md`, required
**before** the R6 join slice. Code-grounded against the Lua baseline (`spec/dv_dual_sf.lua`) + the C++ port.
**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com> · **Date:** 2026-06-05

---

## 0. Scope

How a node picks and keeps a **`node_id`** (the 8-bit short routing address) with **no central
authority**, **zero operator touch**, over **lossy links / sleeping nodes / partition merges**,
into a space of **254 usable ids** (`0x01`–`0xFE`; `0x00` = unprovisioned, `0xFF` reserved —
`node.cpp:28` panics). Identity is the **`key_hash32`/pubkey** (stable, §1 of the identity
spec); `node_id` is a **disposable lease** — renumbering is harmless because upper layers
re-bind by `key_hash32` (`id_bind`, hash-locate).

**Not in scope:** the leaf-config join (lineage/epoch/config_hash, §3–4 of the identity spec)
and crypto. This is *only* short-id allocation. It composes with leaf-membership but is
orthogonal.

---

## 1. Model — DAD claim + beacon-driven heal

Two mechanisms, the second is the correctness guarantee:

1. **DAD (Duplicate-Address-Detection) claim/probe** — *cuts churn*. Pick a candidate id,
   broadcast a claim, listen briefly for an objection, adopt if none. Probes get lost, so this
   alone is **not** sufficient.
2. **Beacon-driven heal** — *the guarantee*. Every beacon carries `(src=node_id, key_hash32)`.
   A node that sees its own `node_id` with a **different** `key_hash32` has a live collision; a
   **deterministic, static tiebreak** makes **exactly one** side renumber once. Stable
   comparison ⇒ converges, no ping-pong.

Past ~16 nodes the birthday rate makes collisions likely (254-slot space), so **healing — not
probing — is what makes it correct**; probing only reduces how often a heal is needed.

### 1.1 Directionality (from identity §5.5) — DAD does NOT use the H flood
`H` is hash-keyed (*which id for this hash*). DAD is the dual (*who holds id X*) and there is
**no id-keyed flood**. DAD works off the beacon `(node_id, key_hash32)` pair + the self-
collision guard below. The `H` plane is untouched.

---

## 2. Wire & state (mostly already on the wire)

**Frames — reuse the existing `J CLAIM`/`DENY`** (frames.md P5; codec done, runtime is this slice):
- `J_CLAIM` (11 B): `key_hash32 · proposed_node_id · lease_age_seconds(u16) · claim_epoch · nonce`.
- `J_DENY` (15 B): `denied_node_id · owner_key_hash32 · claimant_key_hash32 · owner_lease_age(u16)
  · owner_claim_epoch · reason` (`reason ∈ {CONFLICT, PENDING_CLAIM, OWN_ID_DEFENSE}`).
- The beacon already carries `(src, key_hash32)` — the heal detector needs no new field. `J_DISCOVER`
  kept (active solicit, sim-evaluate); `J_OFFER`'s id-assignment role is superseded by DAD.

**Per-node state** (bounded, MCU-sized):
| State | Meaning |
|---|---|
| `joined` (bool), `node_id` | adopted yet + the current short id |
| `claim_pending {proposed, key_hash32, claim_epoch, nonce, started_ms}` | one in-flight claim |
| `claim_epoch` (u8) | **persisted in NV**; bumped once per claim (§7) — the static seniority key |
| `denied_ids` | a **256-bit bitmap (32 B)** of ids that lost a claim/heal — the random picker skips them; each entry **ages out after 1 day** (§13) so a transient conflict doesn't permanently burn a slot (needs a small per-id last-denied timestamp, or a periodic full-bitmap clear) |
| `adopted_at_ms` | for the *informational* `lease_age` only (NOT a tiebreak input — §6) |

---

## 3. Candidate selection (`join_choose_candidate_id`)

1. **Prefer the previous id** — `id_bind_find_by_hash(my key_hash32)`. The network (and our NV)
   may still remember our old `node_id`; reclaiming it is the cheapest path and avoids a renumber.
   Skip if it's in `denied_ids`.
2. **Else a random free slot** — scan `0x01..0xFE`, skip occupied (`id_bind`) + `denied_ids`; age
   out expired bindings first. Pick **uniformly at random** among the free (random, not lowest, so
   two simultaneous joiners are unlikely to collide on the same pick).
3. **Else `nil` → leaf full** (§8).

> **Rejoin via the H plane (identity §5.5 synergy):** when our `id_bind`/NV has *no* memory of our
> old id (cold RAM), a **SOFT self-`H` query** for our own `key_hash32` lets a neighbour that still
> caches `(myHash→oldId)` hand it back — then DAD-probe it. (Soft, not hard: only the *owner*
> answers a hard query, and the owner is the amnesiac rejoiner.)

---

## 4. Claim → probe → adopt (`join_start_claim`)

1. `claim_epoch = (claim_epoch + 1) & 0xFF`; **persist to NV**. Pick a nonce. Build
   `claim_pending`. Broadcast `J_CLAIM(proposed, lease_age, claim_epoch, nonce)`.
2. Wait `claim_guard_ms` (**20 s** — §13; longer than the Lua's 3 s so a slow/lossy LoRa
   neighbour has time for ≥1 beacon + a `J_DENY` before we adopt). On expiry:
   - **Objection seen** (an `id_bind[proposed]` with a different hash appeared, or we lost a
     simultaneous-claim compare, §5) → drop the claim, `denied_ids[proposed]=1`, retry after a
     jittered backoff with a fresh candidate.
   - **No objection** → **adopt**: `id_bind_set(proposed, myhash, self/authoritative)`,
     `set_identity(proposed, …)` / `set_protocol_id`, `joined=true`, `adopted_at_ms=now`, persist
     `node_id` to NV, beacon (`sync`) + `REQ_SYNC`.

---

## 5. Receiving a claim/deny

**On `J_CLAIM`** (`handle_j` CLAIM): it's a **conflict** if `proposed == my id` (and joined), OR
an `id_bind[proposed]` exists with a different hash, OR it collides with **my own pending claim**
and **I win the tiebreak** (§6). On conflict → send `J_DENY(owner_key, claimant_key, owner_epoch,
reason)`. If I have a pending claim for the same id and **I lose** → drop mine, `denied_ids`, retry.
Otherwise (no conflict) → `id_bind_set(proposed, claimed)` — passively learn the claim.

**On `J_DENY`** (`handle_j` DENY): `id_bind_set(denied, owner_key, claimed)` — learn the owner.
Then if **I am the claimant being denied my own id** (`joined && denied==my id && claimant==my key
&& owner≠my key`) → run the tiebreak (§6); **if I lose → `forced_rejoin`** (yield, `denied_ids`,
re-claim a new candidate).

---

## 6. THE TIEBREAK (the crux) — static, symmetric, convergent

**Rule (one rule for every collision case):**
> **higher `claim_epoch` wins; on a tie, lower `key_hash32` wins (the other yields).**
> Live `lease_age_seconds` is **NOT** a tiebreak input — it is carried for telemetry only.

### 6.1 Why NOT live `lease_age` (the Lua's primary key) — it is non-convergent
The Lua leads with `lease_age` ("older lease wins"). `lease_age = (now − adopted_at)/1000`, grows
over time, and is carried on the wire as a **snapshot** taken when the frame was built. So a
receiver compares **its own current** lease against the **other side's stale** wire lease. The wire
value is always **≤** the sender's current lease, so **each side systematically under-estimates the
other** → **both can conclude they win → mutual-keep → the collision never resolves**. (Symmetric
mutual-yield in the inverse case.) That is the exact "mutual-yield/keep flapping" the identity spec
§5.3/§9 flagged. **A live, time-varying quantity cannot be a primary tiebreak under wire staleness.**

### 6.2 Why static `claim_epoch → key_hash32` converges
`claim_epoch` and `key_hash32` are **static** between claims, so the wire value **equals** the
current value — **no staleness skew**. Both sides compute the **same** total order from the **same**
static wire fields, agree on the winner, and **exactly one yields**. `key_hash32` is unique per
honest node (32-bit, but a 254-node leaf is ~2⁻²⁴ from a birthday collision), so the order is
**total** — always exactly one winner, never a tie that stalls. Convergence is a property of
*staticness + a total order*, which this has and `lease_age` does not.

### 6.3 `claim_epoch` semantics
Bumped **once per claim** (not per boot), **persisted in NV**. "Higher wins" biases toward the node
that most-recently (re)asserted the id — a mild, deterministic established-bias. **Wrap (u8, 256
claims):** a node claims a handful of times in its life, so wrap is effectively unreachable; if it
ever wrapped, `key_hash32` still backstops *convergence* (only *who* yields could differ). Unify:
this single rule replaces BOTH the Lua's `join_claim_compare(hash,nonce)` (simultaneous unjoined)
and `addr_conflict_tie_break(lease,epoch,key)` (adopted collision) — `nonce` becomes unused by the
comparison (kept on the wire as reserved/telemetry; only relevant if two honest nodes shared a
`key_hash32`, which a 254-leaf never reaches).

---

## 7. The beacon-driven heal — needs the guard fix (identity §5.5)

**Detector — the one prerequisite code change.** Today `node_beacon.cpp:203` drops **any** beacon
with `src == _node_id` as a self-echo, which **silently swallows the collision**. Narrow it:
```
if (b.src == _node_id && b.key_hash32 == _key_hash32) return;   // TRUE self-echo
if (b.src == _node_id /* && hash differs */) -> ADDRESS COLLISION -> emit addr_conflict_observed -> heal
```
The collision fires `addr_conflict_observed` (the same-id/different-hash path,
`node_hashlocate.cpp:63`), and the owner runs `addr_conflict_recovery_send_deny` — a `J_DENY`
(`OWN_ID_DEFENSE`) carrying its `claim_epoch`. The impostor runs §6 on that DENY and yields if it
loses. **Confidence gate (§5.5):** act only on a *first-hand* (authoritative) beacon collision,
never a claimed/snooped rumor — which falls out, since beacon ingest is first-hand by construction.
**`id_bind_set` self-safety:** the guard branches a self-collision to the heal **before**
`id_bind_set`, so a peer's beacon never rewrites our own identity row via the authoritative-overwrite
path (`node_hashlocate.cpp:66`).

### 7.1 Scope of the heal guarantee — KNOWN LIMITS (as built 2026-06-06)
The guarantee is **NOT "every collision, anywhere."** What converges:
- **Claim-time collisions** (two joiners pick the same id while claiming) — the simultaneous-claim
  tiebreak in `handle_j` CLAIM (sim-gated, `t92`).
- **Direct-neighbour adopted collisions** — two same-id owners that hear *each other's* beacons/DENYs
  (unit-tested).

What does **not** heal yet:
- **Multi-hop collisions.** Two same-id nodes **≥2 hops apart** that share a common neighbour: J/DENY
  and beacons are single-hop here, so the two owners never hear each other — the shared neighbour just
  sees a **flapping `id_bind`** (each beacon overwrites the other), no heal. Closing this needs the heal
  to propagate (a forwarded DENY or an id-keyed probe) — **deferred; decide third-party-heal vs
  explicitly scoping the guarantee to single-hop.**
- **`[LANDMINE — documented]` case-(b) third-party DENY carries the wrong epoch.** When a node denies a
  CLAIM that conflicts with a binding it learned *second-hand* (the contested id is **not its own**), it
  sends the DENY with **its own `claim_epoch`**, not the real owner's (whose epoch it doesn't hold).
  Harmless **today** — such a DENY only reaches an *unjoined* claimant, which backs off on the *binding*
  (the `id_bind` conflict), not on the epoch comparison — but it is a latent correctness landmine if the
  heal logic ever starts trusting a third-party DENY's epoch. A real fix carries the owner's epoch in
  `id_bind`, or restricts DENY-with-epoch to the actual owner.

---

## 8. Exhaustion (254 slots)

`join_choose_candidate_id` returns `nil` → emit `join_no_candidate` / `leaf_full` and retry on a
slow timer (ids free as bindings age out). This **bounds leaf size** at 254 — a real cap to surface
in ops, not silently. (Larger deployments are the cross-layer/gateway story, not one leaf.)

---

## 9. NV & flash-wear

Persist `node_id` (once, on adopt) + `claim_epoch` (on each claim). Claims are rare (join + the
occasional heal), so writes are infrequent — no rate-limiting needed beyond "write on the event, not
on a timer." A node that loses NV just re-runs DAD from scratch (re-derives identity from the seed,
§1.4; picks a fresh id) — self-healing, at the cost of one renumber.

---

## 10. Convergence under loss / sleep / partition

- **Lost claim/deny:** the claimant adopts (no objection heard); the collision surfaces later on the
  first beacon either node hears → heal (§7). The beacon cadence bounds time-to-detect.
- **Sleeping node:** wakes, beacons, the collision surfaces, heal. Its persisted `claim_epoch`
  carries its seniority across the sleep.
- **Partition merge:** two sub-meshes that independently assigned the same id merge → the first
  cross-partition beacon triggers the heal → the static tiebreak picks one survivor → the other
  renumbers once. No flap (static comparison); upper layers re-bind by `key_hash32`.

**Liveness (scoped to §7.1):** a **single-hop** collision is detected within ~one beacon period of contact and resolved in one
DENY round-trip; the loser's `denied_ids` prevents immediately re-picking the contested id.

---

## 11. C++ port plan — **CORE LANDED 2026-06-06**

**Built:** `lib/core/node_join.cpp` (the §6 tiebreak, candidate selection + denied-list aging, claim→guard→
adopt, `handle_j` CLAIM/DENY, `forced_rejoin`, `addr_conflict_send_deny`), the `node_beacon.cpp` self-echo-
guard fix (the heal detector), `on_command(join)` / `on_recv` J dispatch / `on_timer` (guard + retry) /
the denied-id aging hook, the constants (`dad_claim_guard_ms`=20 s etc.), the sim `join` command +
the unprovisioned-`node_id`-0 duplicate-exemption. Verified: native **218/218** (+7 `test_node_join.cpp`:
tiebreak, candidate, claim→adopt, guard-objection, CLAIM→deny, DENY→forced_rejoin win/lose, beacon-
collision defense), 6 MAC gates + channel/discovery green, XIAO+Heltec green, full t-suite 80/86 (no new
regressions), sim happy-path gate `test/t91_node_id_dad_convergence.json`.
**NV persistence + auto-join — DONE 2026-06-06 (device):** `/mrcfg` Blob v4 persists `claim_epoch` + a
`joined` flag (repurposing the old `_pad`, same size so v2/v3 blobs still parse); `fw_main` restores them
at boot (`restore_join_state`), **auto-DADs when unprovisioned** (`node_id==0`), and re-persists on any
change (adopt / epoch bump / forced rejoin). So a reboot **keeps its id + tiebreak seniority**; a fresh
node self-provisions. Bench-verified by the user (flash). The spec §3 soft-self-`H` rejoin (recover the id
from a neighbour's cache when NV is lost) is **not yet coded** — NV-loss currently re-runs DAD for a fresh
id, which is the correct fallback.

**Forced-collision heal — sim-gated:** `test/t92_node_id_collision_heal.json` (seed-swept to force two
nodes onto the same candidate → `simultaneous_claim_lost` + one re-picks → distinct ids).

**Deferred:** `J_DISCOVER`/`J_OFFER` (the beacon-listen + Q-pull model doesn't need them yet); the
multi-hop heal + third-party-DENY epoch fix (§7.1); the soft-self-`H` rejoin (§3). The **pure beacon-heal**
(two long-adopted nodes meeting after a partition) stays unit-test-only — the sim has no dynamic link
up/down to stage it.



**Prerequisite (small):** the `node_beacon.cpp:203` self-echo-guard fix (§7) — without it the heal
detector never fires. Ships with a focused gate (two nodes, same id, different seed → exactly one
`addr_conflict_forced_rejoin`, both converge to distinct ids).

**The state machine** (mirrors the Lua, adapted per §6): `on_command(join)` (currently
`err_unsupported`) → `join_start_claim`; `handle_j` CLAIM/DENY; `join_claim_pending` + the guard
timer; `forced_rejoin`; `denied_ids` as a 32-byte bitmap; `claim_epoch` via the NV seam; the
candidate picker. Reuses the done J codec + `id_bind` + `set_identity` + `addr_conflict_observed`.

**Tiebreak = §6** (NOT the Lua's `lease`-first) — the one deliberate behaviour divergence; document
it at the call site. `lease_age` stays on the wire (telemetry); `nonce` stays on the wire (reserved).

**Gates (sim, dual-engine where it stays Lua-comparable):**
- two nodes pick the same id (forced via NV/seed) → converge to distinct ids, exactly one renumber;
- simultaneous claim of a free id → one adopts, one backs off + re-picks;
- partition-merge collision → one survivor;
- a lost-DENY case → beacon heal still converges;
- exhaustion → `leaf_full`.

> **Lua differential caveat:** the tiebreak diverges from the Lua (we drop `lease_age`), so these
> gates compare **outcomes** (convergence, distinct ids, ≤1 renumber), not Lua-vs-C++ event
> lockstep. The convergence property is the assertion, not byte-parity.

---

## 12. Deliberate divergences from the Lua baseline (record so they aren't re-litigated)

| Lua | This design | Why |
|---|---|---|
| Tiebreak `lease_age → epoch → key` | **`claim_epoch → key_hash32`**; drop live `lease_age` | live snapshot ⇒ non-convergent mutual-keep (§6.1) |
| Two tiebreaks (`hash,nonce` simultaneous; `lease,epoch,key` adopted) | **one** rule for both | `key_hash32` is already the unique deterministic key; `nonce` redundant in a 254-leaf |
| `J_OFFER` assigns the id (OTAA) | DAD **self-assigns**; OFFER's role retired | identity §5.5: beacon-listen + Q-pull + DAD, not OTAA |

---

## 13. Decisions — signed off 2026-06-06

- **`claim_guard_ms` = 20 s** (start value; up from the Lua's 3 s). A joiner waits 20 s for an
  objection before adopting — long enough for ≥1 beacon cycle + a `J_DENY` to arrive over a slow,
  lossy LoRa link, at the cost of a slower first join. Backoff jitter keeps the Lua's ~10 s; all
  tunable in sim.
- **`denied_ids` aging = 1 day.** A slot that lost a claim/heal stays denied for **24 h**, then the
  picker may try it again — long enough that a contested slot doesn't thrash, short enough that a
  slot freed by a node leaving is eventually reusable. (Implementation: a per-id last-denied
  timestamp the aging sweep clears, or a coarse daily bitmap reset.)
- **NV loss → re-run DAD** (confirmed). No attempt to persist/restore the exact `node_id` beyond the
  normal NV record; identity is the `key_hash32` (re-derived from the seed, §1.4), so a fresh id +
  one renumber is the correct, simplest recovery.
