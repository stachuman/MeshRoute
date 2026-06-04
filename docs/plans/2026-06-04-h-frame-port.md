# H-frame (hash-locate) — C++ port instruction

Implementation instruction for the H (hash-locate) plane in the C++ port. Crosschecked
against the Lua baseline `dv_dual_sf.lua`. Implement inline, one phase at a time, verified
against the Lua by **outcome** (not mt19937 lockstep) + the doctest/sim gates per phase.

> **STATUS (2026-06-04):** Phase **A0 is built** (`node_hashlocate.cpp` — table, set/find/age,
> beacon-pop, self-seed, conflict-refuse, cap). The design has since gained **soft/hard H
> variants + verify-on-use (Option A redirect) + an authoritative response flag**. That
> requires **two surgical amendments to A0** (below) and revises A–D. Do the A0 amendments,
> then continue at the revised Phase A. Don't proceed on the pre-revision plan.

---

## Correctness model — read this first

`key_hash32` is the **durable identity**; `node_id` is a **lease** that can change (a node
powers off, its id is reassigned, it rejoins under a *different* id). So a `hash→id` binding
**can go stale**, and correctness must not assume any cache is current. Two layers handle that:

- **Two H variants (1-bit flag on the query):**
  - **SOFT (default):** any node answers from `matches_self` **or** `id_bind_find_by_hash`
    (cache). Fast; may be stale. **Always start soft.**
  - **HARD (flag set):** **owner-only** — skip the cache, answer only on `matches_self`, and
    forward until the query reaches the owner. The owner's answer is **authoritative**.
- **Verify-on-use (the correctness floor, Option A):** a by-hash message carries the intended
  `key_hash32`; the recipient checks `self._key_hash32 == target`. On mismatch it does **not**
  NACK (we can't assume a NACK reaches the sender — and the sender already got its MAC ACK).
  Instead the recipient **holds the message, runs a HARD H itself, and redirects** the held
  message to the owner the hard H resolves. So a stale binding costs one redirect, never a
  silent misdelivery — and the sender is never in the loop.
- **Authoritative overwrite:** a hard-H (owner) response is cached along its return path with
  **authoritative** confidence, overwriting stale bindings (incl. conflicted ones) on every
  hop. Safe *because* verify-on-use is the floor: even a forged authoritative bind is caught
  at the real owner.

This is **beyond the Lua** (which has neither soft/hard nor verify-on-use; it caches the
response only at the destination gateway, dv:11398). It's a deliberate, known-better
divergence — make the payoff measurable (see tests).

---

## Phase A0 — binding cache `id_bind` — BUILT, two amendments required

The agent's `node_hashlocate.cpp` is correct as a faithful Lua mirror. Two additions make it
match the locked design:

**Amendment 1 — dedup-by-hash on set (the rejoin fix).** Today `id_bind_set` finds by
`node_id`, so learning `(id_new → hash)` for a rejoined node *adds* a second entry while the
stale `(id_old → hash)` lingers → `id_bind_find_by_hash` becomes ambiguous (may return the
dead `id_old`). Fix: when inserting/updating `(id_new → hash)`, **evict any other entry
`(id_old → hash)` for the same hash where `id_old != id_new`**, provided the new binding is at
least as fresh/authoritative. Result: one id per hash, and a rejoin self-heals on the first
new beacon. (This is the concrete improvement over the Lua, which never dedups by hash.)

**Amendment 2 — the authoritative path.** Today every non-self set is `"claimed"` and a
conflict (`same id, new hash`) is *refused* (`return false`, line 45). An **authoritative**
source (the owner's hard-H response) must instead **overwrite** — even through a conflict —
and win dedup-by-hash. So:
- Add a **`confidence` field to `IdBind`** (store it; don't only derive it from `source`, because
  an `h_query` response is authoritative iff the owner answered — see Phase B's flag).
- `id_bind_set` takes a confidence/authoritative argument. **Authoritative** ⇒ overwrite on
  conflict + win dedup. **Claimed** ⇒ keep the current conflict-refuse + freshness rules.

**Keep as-is:** `last_seen_ms` is the *hash-confirmation* time (refreshed only inside
`id_bind_set`, which always carries a hash) — that's the right thing to age on (Lua's
`last_key_seen`). Do **not** add a "plain refresh" that bumps it on non-hash traffic.

**Optional (defer):** epoch-ordering via `claim_epoch`/`lease_age_seconds` — not needed for the
MVP, since authoritative-overwrite + dedup + freshness + verify-on-use already cover correctness.

Emits unchanged: `id_bind_set`, `id_bind_aged`, `addr_conflict_observed`, `table_cap_hit`.

---

## Phase A — H flood + soft/hard resolver

Handler on `Cmd::H` (mirror Lua dv:11628-11671, with the variant split):
1. foreign-layer drop (`h.leaf_id != active`, dv:11635); self-query skip (`origin == self.id`, dv:11637).
2. emit `h_rx{origin, key_hash32, ttl, hard}`.
3. **Resolve by variant:**
   - **SOFT:** `node_id = (self._key_hash32==hash) ? self.id : id_bind_find_by_hash(hash)`.
   - **HARD:** `node_id = (self._key_hash32==hash) ? self.id : NONE` — **never the cache.**
4. If `node_id` found (RESOLVER): `mark_hash_query_seen` (dv:11647); emit
   `h_resolved{origin, key_hash32, node, authoritative=matches_self}`; send the response
   (Phase B) with **`authoritative = matches_self`**; **return (suppress forward).**
5. Else (FORWARD): dedup check (dv:11656) → `mark_hash_query_seen` → if `ttl<=0` return →
   emit `h_forward` → re-`pack_h(origin, layer, hash, ttl-1, hard)` → tx on routing SF.
- **Dedup must be variant-aware:** a HARD H must propagate even where a SOFT H for the same
  `(origin, hash)` was already seen. Key the seen-ring on `(origin, hash, hard)`, or let a hard
  query bypass/refresh a soft entry. Otherwise hard can't reach the owner.

## Phase B — hash-bind response (+ authoritative flag)

- **B's first move: make the payload-flags byte the *universal* inner prefix** (the DATA payload's
  first byte holds the flags — and `CRYPTED` applies to user DMs too, so they carry it as well).
  Every inner leads with it: a normal unicast becomes `[payload-flags][origin][body]` (a +1-byte
  DM-inner change — its tests update), an H-answer is
  `[payload-flags(H_ANSWER)][target_layer][node_id][key_hash32]`. Build it as a **reusable inner
  codec** (cross-layer + by-hash reuse it). **Exception:** channel-M keeps its header
  `PAYLOAD_TYPE_M` flag + current inner — a relay reads the prefix only when `PAYLOAD_TYPE_M` is clear.
- **Carried in a routed DATA, typed by the `H_ANSWER` inner payload-flag — no magic, no separate
  flags byte.** (The `\x1fH1` magic the Lua/early draft used is **removed**; `H_ANSWER` types it.)
  Inner = **payload-flags byte** (`CROSS_LAYER`=b0, `H_ANSWER`=b1, `AUTHORITATIVE`=b2, `CRYPTED`=b3
  — here `H_ANSWER`=1, `AUTHORITATIVE`=`matches_self`, `CRYPTED`=0) + body `target_layer`(1) ·
  `node_id`(1) · `key_hash32`(4 LE).
- `send_hash_bind_response(to_origin, authoritative, target_layer, node_id, key_hash32)`:
  enqueue a routed DATA to the origin (dv:5877); emit `hash_bind_response_enqueued{authoritative}`.
- **CRYPTED invariant (load-bearing for cache-on-pass):** the payload-flags byte and the binding
  body are **cleartext** (`CRYPTED`=0), so any relay can read + cache them; `CRYPTED` seals only
  end-user content, never this. (See `frames.md` DATA + memory `data-inner-payload-flags`.)

## Phase C — consume + cache-on-pass (confidence-aware)

- **C.1 destination consume** (origin/querier, Lua dv:11398): `id_bind_set(node_id, hash,
  source=h_query, confidence = authoritative ? authoritative : claimed)`; drain any parked
  send-by-hash; emit `q_hash_binding_rx{authoritative}`. Terminal.
- **C.2 cache-on-pass (forwarders) — beyond the Lua:** when a node *relays* a DATA it reads the
  cleartext inner payload-flags byte; if `H_ANSWER` is set it **also** `id_bind_set(node_id, hash,
  source=h_relay, confidence per the `AUTHORITATIVE` bit)` then forwards normally (does not consume). An **authoritative** response thus
  **overwrites stale bindings on every hop of the return path** — this is your "authoritative
  answer cached by every node." A soft (cache-answered) response writes `claimed` and obeys
  the freshness/dedup rules. Emit `id_bind_set` with the right `source`/`confidence`.

## Phase D — send-by-hash + verify-on-use redirect (Option A)

**By-hash addressing:** a by-hash DATA carries the intended **`key_hash32`** so the recipient
can verify. Put it in a **by-hash unicast inner variant** (`{origin, target_hash, body}`,
detected by a magic/type like `parse_m_inner`) — the DATA flags nibble is full
(`PAYLOAD_TYPE_M|PRIORITY|E2E_IS_ACK|E2E_ACK_REQ`), so the marker lives in the inner, not a flag bit.

**Trigger:** a send-by-hash command (the command seam's deferred "address by hash"). Resolve
SOFT (cache or owner); on hit send to the id (carry `target_hash`); on miss flood a SOFT H,
park the DM, send on the bind.

**Verify-on-use at the recipient (Option A):** the by-hash DATA is delivered and **MAC-ACKed
normally** to the resolved id (the sender's stop-and-wait completes — it is *done*). *After*
acceptance, the recipient checks `self._key_hash32 == target_hash`:
- **match** → deliver to the app.
- **mismatch** → **hold** the message; run a **HARD H** for `target_hash` (origin = self); on the
  **authoritative** response → **forward the held message** to the resolved owner, preserving
  the original `(origin, ctr, target_hash)`, re-routed. The authoritative bind also overwrote
  the local + path-cached stale entries (C.2).
  - **hard-H timeout** (owner genuinely gone) → **drop** the held message; emit
    `hash_redirect_undeliverable`. The sender already got its MAC ACK, so nothing retries —
    the target is gone; that's correct.
  - **loop bound:** carry a redirect flag/count; **cap at 1 redirect.** If the post-hard-H
    recipient *still* mismatches, drop + emit (don't ping-pong).
- Emits: `hash_verify_mismatch`, `hash_redirect_held`, `hash_redirect_sent`,
  `hash_redirect_undeliverable`.

**e2e test (the whole point):** A is at `id_5`, bound everywhere; A powers off; B claims
`id_5`; A rejoins as `id_9` and beacons. Sender does by-hash → A: soft-resolves the **stale**
`id_5` → B (`id_5`) accepts + verify-mismatch → hard H → reaches A@`id_9` → authoritative
response → B redirects the held message → A@`id_9` delivers. **Assert:** delivered to `id_9`;
path/local caches now resolve A→`id_9` (authoritative overwrite); exactly one redirect.

---

## Frame-format changes (consolidated)

| Frame | Change | Why |
|---|---|---|
| **H query** | re-add the 1-byte flags field the C++ dropped (Lua reserved it, dv:2448); H 7→8 B; **bit 0 = HARD** | soft/hard variant |
| **DATA inner — payload-flags byte** | inner byte 0, cleartext: `CROSS_LAYER`=b0, `H_ANSWER`=b1, `AUTHORITATIVE`=b2, `CRYPTED`=b3 | types the inner so relays act/cache without decoding the body |
| **hash-bind response** | typed by the `H_ANSWER` flag — **no `\x1fH1` magic, no separate flags byte**; body = `target_layer + node_id + key_hash32` | H reply; magic redundant |
| **DATA (by-hash)** | a by-hash unicast inner carrying `target_hash` (4 B) | verify-on-use |

## Emit parity (match Lua names; new ones marked)

`h_rx` (+`hard`) · `h_resolved` (+`authoritative`) · `h_forward` · `id_bind_set`
(+`confidence`) · `id_bind_aged` · `addr_conflict_observed` · `hash_bind_response_enqueued`
(+`authoritative`) · `q_hash_binding_rx` (+`authoritative`). **New:** `hash_verify_mismatch`,
`hash_redirect_held`, `hash_redirect_sent`, `hash_redirect_undeliverable`.

## Divergences from the Lua (flag explicitly; justify by outcome)

1. **dedup-by-hash on set** (A0.1) — the Lua never dedups by hash; we do, to keep
   `find_by_hash` unambiguous and self-heal rejoins.
2. **soft/hard variants + verify-on-use redirect** (A, D) — not in the Lua. The correctness
   floor for the lease-churn problem.
3. **cache-on-pass + authoritative overwrite** (C.2) — the Lua caches only at the destination
   gateway. Spreads correct bindings along the return path.
4. **same-layer scope** — MVP; the Lua frames H as cross-layer/gateway. The flood is already
   leaf-scoped; defer only the gateway trigger.

## Verify per phase (gates)

- Each phase: doctests (codec/table/dedup) + a meshroute scenario; `pio test -e native` + the
  `lus` sweep; outcome-compare to the Lua — **not** mt19937 lockstep.
- A0.1: a beacon for a rejoined hash evicts the stale id; `find_by_hash` returns exactly the
  fresh id.
- A: warm soft short-circuit (neighbour answers, flood never reaches owner — assert no `h_rx`
  at owner); hard H **ignores** a cached binding and reaches the owner.
- D: the rejoin e2e above. Add an efficiency assertion (t88-style): after the authoritative
  overwrite propagates, repeat by-hash sends resolve correctly with **no** further redirects.
