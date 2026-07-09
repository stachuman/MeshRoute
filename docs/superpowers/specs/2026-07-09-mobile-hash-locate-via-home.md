<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — hash-locate resolves to the HOME (local id off the static plane) + E2E pubkey via the home — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09). **The addressing spine.** Found on the bench: a mobile's LOCAL id (17) leaked into a sender's static plane — routing table (`dest=17 next=17`), hash-locate, and DM `dst=17` — so the DM was addressed to the local id and the home never got to last-mile it. The user commits; I quality-gate. **The rule:** a mobile's local id is INVISIBLE to the static plane; a static node reaches a mobile ONLY as `home_id + dst_hash`. Two parts: **Part 1 (location)** unblocks the *unencrypted* DM with no new wire; **Part 2 (pubkey)** adds the wire so *encrypted* DMs to mobiles work (Option 1 — the home carries the key).

## Root cause (all confirmed in code)
1. `node_hashlocate.cpp:512` — a hash OWNER answers `AUTHORITATIVE_H_ANSWER` with its own id, **before** the `!h.hard` gate. So a registered mobile answers even a HARD query with its **local id**.
2. `node_hashlocate.cpp:521` — the home's `mobile_proxy` is gated `!h.hard`, so a HARD locate (which `e2e_ack_req` drives) **bypasses the home** and floods to the mobile owner.
3. `node_beacon.cpp:633` — `learn_direct_neighbor(b.src, …)` runs for **every** beacon incl. a mobile's, minting a static `dest=<local id>` route (no `is_mobile` guard).

The sender side is already correct: `on_mobile_hash_bind_response` caches `mobile_home` ONLY (no id_bind, no route). So the fix is entirely on the answering/beacon side — make the sender receive the home's `MOBILE_H_ANSWER`, never the mobile's authoritative answer.

## ★★ Static-safety
- Fix 1/2 touch the mobile-hash / `_mobile_reg` branches only; a node hosting no mobiles and a query for a non-mobile hash are unchanged. Fix 3 gates on `b.is_mobile`. **s18 (no mobiles) byte-identical** (`3ac88d40…`).
- **Gate:** native + s18 byte-identical + s07/s21 + a NEW hard-locate scenario/test (the sim's DM goes via `dst=home_id`, so it won't catch this — see Tests).

---

# PART 1 — LOCATION (unblocks the unencrypted DM; no wire change)

## Fix 1 — a registered mobile answers ONLY its teammates, not the static plane (`node_hashlocate.cpp:512`)
Two planes: on the **static** plane the mobile is invisible (the home proxies); on the **team** plane the mobile IS the endpoint, so it must answer teammates directly (they route to its local id via the 6.2 team-scoped table). It tells them apart by the query's team scope:
```cpp
const bool same_team = h.team_scoped && _cfg.team_id != 0 && h.team_id == _cfg.team_id;   // §mobile-team: a teammate's locate
if (h.key_hash32 == _key_hash32 && (!(_cfg.is_mobile && _my_mobile_reg.active) || same_team)) { node_id = _node_id; authoritative = true; }
```
- **Static query** → registered mobile skips own-hash resolution → `node_id` stays −1 → forwards; the home proxies. Local id never leaks. (Also suppresses its `want_pubkey` owner-answer — Part 2 makes the home answer that.)
- **Team query** (`same_team`) → the mobile answers authoritatively with its local id, for the team plane. The home's proxy still fires (harmless — the teammate prefers the authoritative answer).

## Fix 1b — H-query carries the team scope (wire, `want_pubkey`-style append)
A teammate's locate sets a **team flag** in the H flags byte and **appends `team_id` (4 B)** to the H frame — exactly like `want_pubkey` appends `requester_ed_pub`. A non-team (static) query sets nothing and is **byte-identical** (s18 safe). `frame_codec.h`: add `bool team_scoped=false; uint32_t team_id=0;` to `h_in`/`h_out`, pack/parse the flag + conditional 4 B. The querier sets them when `_cfg.team_id != 0` and it's addressing a hash it believes is a teammate (6.2 decides *when* to team-scope; the addressing wire is defined here). **Inert until 6.2** (today `team_id==0` everywhere → `team_scoped` never set → static behaviour).

## Fix 2 — the home answers HARD too, gated on LIVENESS (`node_hashlocate.cpp:521-533`)
Remove `!h.hard` (the home is the mobile's **location authority**, soft AND hard) and gate the *direct* proxy on liveness:
```cpp
if (node_id < 0 && _active->_mobile_reg_n > 0) {          // was `&& !h.hard`
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == h.key_hash32) {
            if (_active->_mobile_reg[i].redirect_home_id != 0) {          // §4b/5b redirect — NOT liveness-gated (forwards to the new home)
                node_id = _active->_mobile_reg[i].redirect_home_id;
                mobile_epoch = _active->_mobile_reg[i].redirect_epoch;
                mobile_layer = _active->_mobile_reg[i].redirect_home_layer;
                authoritative = false; mobile_proxy = true;
            } else if (_hal.now() - _active->_mobile_reg[i].last_heard_ms < protocol::mobile_liveness_ms) {   // §mobile: I'm the home — proxy ONLY if the mobile is recently alive
                node_id = _node_id;
                mobile_epoch = static_cast<uint8_t>(_active->_mobile_reg[i].epoch);
                mobile_layer = active_layer_id();
                authoritative = false; mobile_proxy = true;
            }
            break;   // matched (live/stale/redirect) — stop; STALE leaves node_id=-1 -> forward -> the locate times out -> "unreachable" (NOT a black hole)
        }
}
```
- `protocol_constants.h`: `inline constexpr uint32_t mobile_liveness_ms = 1500000;` (**25 min ≈ 2.5× the 10-min re-CLAIM** — kills the long-term black hole; a just-died mobile is proxied ≤~25 min then goes silent).
- **⚠ Verify the heartbeat:** the re-CLAIM recording (`node_join.cpp:224`, and any refresh) MUST set `last_heard_ms = _hal.now()` so a live mobile's entry stays fresh. If it doesn't, add it — the liveness check is only as good as this refresh.

## Fix 3 — a mobile's beacon never mints a static route (`node_beacon.cpp:633`)
```cpp
if (!b.is_mobile && learn_direct_neighbor(b.src, meta_snr_q4, b.self_gateway)) rt_changed = true;
```
A static node never routes *to* a mobile's local id (the home is the only bridge; the last-mile is a DIRECT send `addr_len=1`, not `rt_find`, so nobody needs this route). Kills the `dest=17` leak.

## Fix 4 — sender routes via the home (VERIFY only, no code)
`on_mobile_hash_bind_response` already does `mobile_home_set` only. With Fix 1–3 the sender receives the home's `MOBILE_H_ANSWER` (never the mobile's authoritative one) → `mobile_home_cache: hash→home` → DM `dst=home_id, dst_hash=M` → the home's for-me last-mile (`node_mac_rx.cpp:587`) fires. Confirm no id_bind/route to the local id remains.

---

# PART 2 — E2E PUBKEY via the home (Option 1; the wire change)

*Only `want_pubkey` (encrypted-DM) locates need this. Your bench DM is `e2e_dm=0`, so Part 1 alone delivers it; Part 2 makes encrypted DMs to mobiles work.*

## Fix 5 — the home caches the mobile's key (`node.h` HostMobileEntry)
Append: `uint8_t ed_pub[32] = {}; bool has_pubkey = false;`.

## Fix 6 — the mobile pushes its key to the home (new DM TYPE)
After adopt, the mobile sends a **`DATA_TYPE_MOBILE_PUBKEY_PUSH`** (next new code, 12) to `home_id`, body = its `ed_pub[32]`, `SOURCE_HASH = M`. The home (do_post_ack) matches `source_hash` against `_mobile_reg` and caches `ed_pub` + `has_pubkey=true` on that entry. (Direct 1-hop, not flooded; keeps the CLAIM small. Re-sent on re-home so a new home learns it.)

## Fix 7 — the home answers `want_pubkey` for its live mobile (`node_hashlocate.cpp`)
When `mobile_proxy` resolved (Fix 2), the mobile is live, `h.want_pubkey`, and `has_pubkey` → send a **`MOBILE_H_ANSWER` carrying the mobile's `ed_pub`** (home_id + is_mobile + epoch + `ed_pub[32]`) instead of the plain one. (`want_pubkey` no longer requires `node_id==_node_id` for the mobile-proxy path.) If the home lacks the key (`!has_pubkey`) → don't answer want_pubkey → the locate times out (sender retries; the push races registration).

## Fix 8 — the sender caches key + home (`on_mobile_hash_bind_response`)
When the mobile answer carries a pubkey: `peer_key_set(hash, ed_pub, authoritative)` **and** `mobile_home_set(hash→home)`. Sender can now seal to the mobile AND route the sealed DM via the home. Never `id_bind_set` the local id.

---

## Tests
- **★ Mobile silent + home proxies HARD:** a registered mobile fed a HARD H-query for its own hash → emits NOTHING (forwards). The home (hosting it, live) fed the same HARD query → emits a `MOBILE_H_ANSWER(home_id)`. Sender ingest → `mobile_home_cache` has home, **no route/id_bind to the local id**.
- **★ Liveness:** home with a mobile whose `last_heard_ms` is `> mobile_liveness_ms` old → the HARD query gets **no** proxy answer (forward). Refresh `last_heard_ms` (re-CLAIM) → proxy answers again.
- **Beacon no-route:** a static node fed a mobile beacon (src=17) → **no** `dest=17` rt entry; a non-mobile beacon (src=17) → route as before.
- **Part 2:** home fed a `MOBILE_PUBKEY_PUSH` → caches `ed_pub`; a `want_pubkey` HARD query → `MOBILE_H_ANSWER`+key; sender caches peer_key + home.
- **★ Regression:** native + **s18 byte-identical** + s07/s21 deliver (they use `dst=home_id`, so they DON'T exercise the hard-locate leak — the unit tests above are the real proof).

## Gate
- `pio test -e native` green (the new cases). **s18 byte-identical** (`3ac88d40…`). s07/s21 0-failures.
- 4 boards. **Bench: re-flash mobile + home + sender → the sender's routes show `dest=222` (home), NO `dest=17`; the DM delivers.**

## Sites
**Part 1:** `node_hashlocate.cpp:512`(mobile team-aware answer) · `:521-533`(home HARD + liveness) · `frame_codec.h`(`h_in`/`h_out` `team_scoped`+`team_id`, `want_pubkey`-style append) · `protocol_constants.h`(`mobile_liveness_ms`) · `node_beacon.cpp:633`(`!b.is_mobile`) · `node_join.cpp:224`(verify `last_heard_ms=now`) · Fix 4 verify-only. **Part 2:** `node.h`(HostMobileEntry `ed_pub`/`has_pubkey`) · `frame_codec.h`(`DATA_TYPE_MOBILE_PUBKEY_PUSH=12`; MOBILE_H_ANSWER pubkey variant) · `node_mobile.cpp`(push on adopt) · `node_mac_rx.cpp`(ingest push) · `node_hashlocate.cpp`(want_pubkey mobile answer + sender ingest). **No `dst=17` ever leaves a home; the local id lives only home↔mobile.**
