<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Code cleanup — safe consolidation + fail-loud (Tier 1) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-10). Post-mobile-v1 tech-debt review. Scope = **"Safe cleanup + fail-loud"** (user-chosen envelope): byte-identical-safe helper consolidations + the Tier-1 fail-loud/correctness fixes + pure dead-symbol removal + `._*` untrack. **EXCLUDED (deferred, see bottom):** monster-function decomposition; NV/wire-layout-changing deletions; feature-gating config-field removal; the gateway NV partial-seed; the remaining Tier-2 duplication clusters. Each **TASK** below is an independent, separately-committable slice with its own gate. The user commits; I quality-gate.

## Ground rules (apply to every task)
- **s18 byte-identical keystone:** `md5(s18) == 3ac88d40e00d2605ff66659f696d52bf`, **253088 events** (`lus -e meshroute simulation/s18_meshroute.json`). Groups **A** (pure refactors) and **C** (fail-loud) MUST keep s18 byte-identical — Group C only changes behaviour on **malformed / out-of-range inputs that s18 does not contain**, so byte-identity proves no regression on well-formed traffic. Re-establish per task.
- **Native:** `pio test -e native` then `./.pio/build/native/program 2>&1 | tail -3` (the pio wrapper lies; read the real doctest line). Baseline **671/671, 0 failed**. Each **C** task ADDS a native test for its new reject/fix path; **D** must not silently drop coverage — report the count delta and its cause.
- **Boards (device-only tasks B, C3, D):** build all 7 — `xiao_sx1262 heltec_v3 xiao_esp32s3 gateway gateway_heltec gateway_esp32s3 production` (build sequentially; the `.pio` par-race gives false nRF52 failures in parallel).
- **Do NOT touch the intentional patterns:** the partial-class TU split; the heavy inline comments; the `team_id`/`is_mobile`-inert gating; the documented `0⇒inherit`/`0⇒default` NV sentinels; the Lua-parity clamps (cited `dv:` lines); `seen_bitmap` OFF-by-default.
- **Every site list below is a CANDIDATE list** — confirm each site matches the described shape before transforming it; skip (and note) any that differ.

---

## GROUP A — byte-identical-safe pure refactors
*Gate for every A task: s18 byte-identical + native green. No behaviour change whatsoever.*

### A1 — DATA-SF airtime helper (kill ~11 open-coded `airtime_ms(...)` copies)
There is a routing-SF helper (`Node::airtime_routing_ms(uint16_t)` at `node_mac.cpp:43`) but no DATA-SF sibling, so the data-SF airtime is open-coded everywhere. Add the sibling and route the matching sites through it.

1. `node.h` (beside the `airtime_routing_ms` decl): `uint32_t airtime_data_ms(uint8_t data_sf, uint16_t len) const;`
2. `node_mac.cpp` (beside `airtime_routing_ms`, ~:43):
```cpp
uint32_t Node::airtime_data_ms(uint8_t data_sf, uint16_t len) const {   // §cleanup: DATA-SF sibling of airtime_routing_ms
    return airtime_ms(data_sf, active_bw_hz(), active_cr(), protocol::preamble_sym, len);
}
```
3. Route ONLY the sites that EXACTLY match `airtime_ms(<data_sf>, active_bw_hz(), active_cr(), protocol::preamble_sym, <len>)` → `airtime_data_ms(<data_sf>, <len>)`. **Candidate sites (verify each):** `node_mac.cpp:645, 655, 882, 995, 1086, 1106`; `node_mac_rx.cpp:108, 135, 432`; `node_routing.cpp:114`; `node.cpp:653`. Skip any that pass a different bw/cr/preamble (e.g. a fixed BW) and note it.
- Byte-identical: `airtime_data_ms` is a pure inline-equivalent wrapper.

### A2 — promote `seen_set`/`seen_test` to a shared header (kill ~13 open-coded bitset ops)
`seen_set`/`seen_test` are `static inline` **file-local** in `node_channel.cpp:35,51` — not reusable, so the same `bm[i>>3] & (1u<<(i&7))` / `|= (1u<<(i&7))` is hand-rolled in ≥3 other TUs.
1. Create `lib/core/bitset.h` (self-contained, only `<cstdint>` — must be includable from `frame_codec.cpp`, which has no `Node`):
```cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#pragma once
#include <cstdint>
namespace MESHROUTE_NS {
inline bool bit_test(const uint8_t* bm, uint8_t i) { return (bm[i >> 3] >> (i & 7)) & 1u; }
inline bool bit_set(uint8_t* bm, uint8_t i) {          // returns true if NEWLY set (mirrors the old seen_set)
    const uint8_t mask = static_cast<uint8_t>(1u << (i & 7));
    if (bm[i >> 3] & mask) return false;
    bm[i >> 3] |= mask; return true;
}
}
```
2. In `node_channel.cpp`, delete the local `seen_set`/`seen_test`, `#include "bitset.h"`, and replace their uses with `bit_set`/`bit_test`.
3. Route the open-coded sites → `bit_test`/`bit_set`. **Candidate sites (verify each is the `i>>3`/`1<<(i&7)` convention):** `frame_codec.cpp:278,279`; `node_budget.cpp:85`; `node_beacon.cpp:114,115,301,306,308,585,678,682,730`.
- Byte-identical: a bare `|=` becomes `bit_set(...)` with the return ignored — idempotent, same bits. Verify no site depended on a *different* bit order.

### A3 — `key_hash32` open-coding → `key_hash32_of()`
`key_hash32_of(const uint8_t ed_pub[32])` already exists (`identity.h:48`; `node_hashlocate.cpp:270` even comments the equivalence), but the 4-byte LE assembly is hand-rolled on the hash-locate path (security-adjacent: a mis-shift mis-binds pubkey→routing-handle).
- Replace the open-coded `LE(ed_pub[0..3])` at **`node_hashlocate.cpp:271-272, 563-564, 664-665, 753`** with `key_hash32_of(<the 32-B ed_pub buffer at that site>)`. Identify each site's source buffer (`ed_pub` / `h.requester_ed_pub` / `o->ed_pub` / `ed`).
- Byte-identical: same LE math, one implementation.

---

## GROUP B — NV-Blob seed consolidation (device-only)
*Gate: 7-board build + a field-by-field equivalence TABLE for my review (fw_main.cpp is not in the native/sim build, so the review IS the gate — there is no byte-identity oracle here).*

### B1 — route the two field-identical inline blob-seeds through `seed_blob_from_live`
`seed_blob_from_live(b)` (`fw_main.cpp:970-986`) is the canonical ~30-field population, but it's re-inlined field-for-field at two "no blob yet → seed from live" sites. Every new Blob field must be hand-added at all of them (just happened with `team_local_id`).
- Replace the inline seed block at **`fw_main.cpp:516-535`** (`handle_cfg_set`, the `if (!mrnv::load(b))` branch) and **`fw_main.cpp:2150-2169`** (`persist_cfg_if_needed`, same branch) with a single `seed_blob_from_live(b);`.
- **VERIFY (this is the gate deliverable):** produce a table showing, for each of the two sites, that every field the old inline block set is still set to the identical value afterwards — either by `seed_blob_from_live` or by a site-specific override that runs AFTER the call (`b.node_id = canonical_node_id()`, the `cfg set` target key, `b.magic`/`b.version`, the `2150` deferred `node_id`). If `seed_blob_from_live` omits a field an inline site set, ADD it to `seed_blob_from_live` (don't special-case the site).
- **LEAVE the gateway partial-seed (`fw_main.cpp:919-922`) AS-IS** — routing it through the helper would change persisted values on a fresh gateway (non-byte-identical); deferred.

---

## GROUP C — fail-loud / correctness fixes
*Gate for every C task: s18 byte-identical (the input that triggers the fix does not occur in s18) + a NEW native test for the fixed path. C3 is device-only → boards + review.*

### C1 — align the `origin` fallback with the raw-body contract
`node_mac_rx.cpp:449` is `const uint8_t origin = ui ? ui->origin : from;`. When the inner fails `parse_unicast_inner`, it falls back to the **link previous-hop** `from`, but the deliver path treats a non-parsing inner as raw body with **origin at `inner[0]`** (the comment near `:772`; `parse_unicast_inner` reads `u.origin = inner[off]`). For a multi-hop plaintext DM `from != origin`, so the malformed-inner path corrupts the PLAINTEXT dedup/loop/NACK key (`sokey`, `:488`).
- Change `:449` to:
```cpp
const uint8_t origin = ui ? ui->origin : (inner.size() > 0 ? inner[0] : from);   // §cleanup: a non-parsing inner still carries origin at inner[0] (raw-body contract, matches the deliver path); the old `from` used the link prev-hop and corrupted the plaintext dedup key
```
- **Recommended:** extract `uint8_t dm_origin(const std::optional<unicast_inner>& ui, std::span<const uint8_t> inner, uint8_t from) const;` and call it at BOTH `:449` and the `:772` deliver site so they can never diverge again.
- CRYPTED is unaffected: `parse_unicast_inner` returns non-null with `origin=0` by design, and CRYPTED dedups in a separate nonce-seed namespace.
- **New native test:** a plaintext multi-hop DM whose inner PARSES-FAIL (e.g. `DATA_FLAG_DST_HASH` set but inner too short to hold the hash) yet has a valid `inner[0]` → `origin == inner[0]`, NOT `from`; assert the resulting dedup/NACK identity keys on the true origin.

### C2 — M_BROADCAST short-frame fails loud (no fabricated `id=0`)
`frame_codec.cpp:455-463`: the FLOOD path rejects a short frame (`return std::nullopt`), but a 7–8 B M_BROADCAST skips the id read and returns a struct with `m_payload_id_lo16 = 0` — a fabricated 0 that can collide in broadcast dedup.
- Make M_BROADCAST symmetric with FLOOD:
```cpp
} else if (o.m_broadcast) {
    if (frame.size() < 9) return std::nullopt;   // §cleanup: an M_BROADCAST RTS MUST carry the 2-B id tail; a short frame is malformed, not id=0
    o.m_payload_id_lo16 = r.u16_be();
}
```
- **VERIFY** no caller relies on the old id=0-on-short return (grep `m_broadcast` + `m_payload_id_lo16`).
- **New native test:** a 7-B M_BROADCAST RTS → `parse_rts` returns `nullopt` (was: a struct with id 0). A full-length M_BROADCAST still parses (regression guard).

### C3 — console setters validate-or-reject (device-only)
`handle_cfg_set` validates inconsistently: `bw` rejects out-of-range (`fw_main.cpp:563`), but several keys take raw `atoi`/`atol` and silently accept garbage→0, some persisted. Add ONE helper and route the offenders through it, mirroring the existing `bw` reject shape (`> cfg err bad_value (...)`; `return;`).
1. Add near the top of the cfg-set seam:
```cpp
// §cleanup: parse an integer in [lo,hi] fully or FAIL LOUD. Returns false (caller prints the err + returns) on garbage/range.
static bool cfg_parse_int(const char* val, long lo, long hi, long& out) {
    char* end = nullptr; const long v = strtol(val, &end, 0);
    if (end == val || *end != '\0' || v < lo || v > hi) return false;
    out = v; return true;
}
```
2. Route these keys (reject with `mrcon.println(F("> cfg err bad_value (<key> <lo>..<hi>)")); return;` on false):
   - **`routing_sf`/`control_sf`** (`:562`) → range **5..12** (the protocol SF range; do NOT enforce the SF6 HW floor — that's the documented "left configurable for future SF5 HW" decision; reject 0/13/garbage, which the comment does NOT license).
   - **`hop_cap`** (`:588`) → reject 0 (0 disables all multi-hop routing); upper bound = the protocol DV hop max (grep `dv_hop_cap` / the hop constant).
   - **`leaf_id`** (`:620`) → range **0..15** (verify the leaf nibble width in `frame_codec` `cmd_byte`).
   - **`gw_herd_slack`** (`:598`) → make the existing silent clamp a reject over its 1..255 range.
   - **`gw_announce_interval`** (`:597`) → reject garbage (resolve the code/comment drift: the header says "0 keeps prior", the code sets `atol`).
3. **`sf_list`** (`:575`) needs the nuance (an empty list = the agreed "relay-only, don't originate data" value, but a TYPO also parses to 0): make `parse_sf_list` report whether ANY token was seen; **reject** when tokens were present but ALL invalid (bitmap 0 from non-empty input), **allow** a truly-empty input (relay-only). Mirror the `l1_sf_list` reject at `:690-693` for the all-invalid case.
- **Gate:** 7 boards + a manual check that each routed key rejects garbage and accepts a valid value. If a native console-parse harness exists (`test/` — the prior `console_parse` tests), add reject-case asserts there.

### C4 — wire `peer_key_age_out()` into the aging sweep
`peer_key_age_out()` (`node_hashlocate.cpp:328`) is defined but has NO production caller (tests only) — it's absent from the `kAgingTimerId` sweep. The lazy path (`peer_key_find` returns false on a stale entry, `:316`) already covers correctness, so this is a **slot-reclamation** gap, not a bug — but the periodic sweep was clearly intended (it mirrors the six sibling `age_out_*` methods).
- In `node.cpp` `on_timer` `case kAgingTimerId:` (~:735-742), add alongside the other `age_out_*` calls:
```cpp
peer_key_age_out();   // §cleanup: the periodic peer-key TTL sweep was defined but never wired (lazy eviction in peer_key_find covered correctness; this reclaims stale slots)
```
- **New native test:** insert a peer key, advance `_hal.now()` past `peer_key_ttl_ms`, fire the aging timer → the stale (non-pinned) entry is evicted (`peer_keys_n` drops); a PINNED key survives.
- (Alternative if you'd rather not add periodic work: delete `peer_key_age_out()` + its decl as dead. Wiring-in is the recommended default.)

---

## GROUP D — dead-symbol removal + repo hygiene
*Gate: native green (report the count delta + cause) + 7-board build. Nothing here may change an NV/wire layout (those are deferred).*

### D1 — remove the clearly-dead symbols
Each below was grep-verified as declaration-only / unreachable. Remove the symbol AND its now-dead dependents:
- `NodeConfig::loc_in_m` — `node.h:192` (appears once; verify `NodeConfig` is not bulk-memcpy'd anywhere).
- `NodeConfig::join_required` — `node.h:106` + the unreachable guard `if (_cfg.join_required) return;` at `node_beacon.cpp:795`.
- `w_u16` / `r_u16` — `inbox.cpp:15,18` (zero callers incl. tests).
- `sf_demod_threshold_q4(uint8_t)` **the function** — `protocol_constants.h:475` (superseded by `sf_demod_threshold_q4_table[]`; verify no test CALLS it — tests assert the table).
- `wire::Cmd::EXT` (0xF) — `wire.h:26` (verify 0xF is never emitted/matched on wire).
- `wire::Reader::remaining()` — `wire.h:72`.
- `CompanionPolicy::mode()` — `companion_policy.h:55`.
- The `#if 0 flood_log_coverage()` block — `node_channel.cpp:40-50` + the commented decl at `node.h:993`.

**Verify-then-remove (remove ONLY if the check passes; else leave for a follow-up + note it):**
- `JoinCmd` payload fields — `command.h:36`: confirm the `Cmd` union / `CmdKind::join` handler (`node.cpp:869-878`) and both producers (`fw_main.cpp:1024,2049`) touch only `.kind`, and nothing serializes `JoinCmd`.
- `CmdCode::err_priority_capped` / `err_no_binding` + their `console_json.cpp:106,107` stringifier arms — remove ONLY if `CmdCode` is not serialized by ordinal AND removal does not renumber other enumerators (i.e. explicitly-numbered or trailing values).

### D2 — untrack the committed `._*` sidecars
Git-tracked macOS AppleDouble/temp junk sits in the tree: `lib/core/._node_channel.cpp`, `src/._device_nv.h`, `src/._fw_main.cpp`, `src/._fw_main.cpp.tmp.*`, and many under `ios-companion/`.
- `git rm --cached` each tracked `._*` path (untrack; leave on disk; **do NOT delete files, do NOT commit** — the user commits). `.gitignore` already has `._*` (line 31), so they won't re-add.
- Gate: `git status` shows them staged for untracking; the build is unaffected.

---

## Deferred — explicitly OUT of scope for this spec
- **NV/wire-layout deletions:** the self-admitted-vestigial `_claim_epoch` chain (`node.h:1266`, `Blob::claim_epoch`, `Blob::l1_claim_epoch`/`l1_joined` at `device_nv.h:55-56`). Removal shrinks the Blob → `kVersion` bump → fleet reprovision. Separate decision.
- **Feature-gating config fields** read-never-written on device (`seen_bitmap_enabled`, `sync_response_enabled`, `originator_max_per_window`): they gate real (dormant) code paths and tests write them → removal is feature-removal, not cleanup.
- **Monster-function decomposition** (`do_post_ack`, `ingest_beacon`, `handle_rts`, `handle_data`, `emit_beacon`): correct + well-tested today; high risk; "don't OO-decompose mid-port".
- **Gateway NV partial-seed** consolidation (`fw_main.cpp:919-922`): non-byte-identical.
- **Remaining Tier-2 duplication clusters** (F/NACK/CTS/H-answer build idioms, TLV ext-framing skeleton, the 1-hop-neighbour predicate, LBT/NAV carrier-sense + RNG draw): valid, but larger; a follow-up cleanup spec.

## Suggested order
A1 → A2 → A3 (pure refactors, warm up the byte-identical gate) → C4 → C2 → C1 (core fail-loud, each + a native test) → B1 → C3 (device-only, boards + review) → D1 → D2 (hygiene). Commit per task; I gate each.
