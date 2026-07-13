<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Authenticated Remote Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gate remote (`rcmd`) writes behind an AEAD-sealed, admin-authenticated DM keyed by one shared **password**; keep `status`/`routes` open; answer with the built binary TLV encoders; compile the whole subsystem out on mobiles.

**Architecture:** A password → **iterated-BLAKE2b** seed → `identity_from_seed` admin keypair; the node pins only `admin_pubkey`. A gated command is a `dm_crypto` X25519-sealed DM (the Poly1305 tag *is* the auth) carrying `{node_key_hash, counter, command}`; the node trial-opens with the pinned `admin_pubkey`, checks the counter floor, executes, and seals a `console_binary` TLV response back. Everything is `#if MR_FEAT_REMOTE_MGMT`.

**Tech Stack:** C++20, monocypher (`crypto_blake2b` + incremental), the `dm_crypto`/`identity` E2E seam, `lib/console/console_binary` (built), `dispatch()` (built), doctest (native), PlatformIO. Spec: `docs/superpowers/specs/2026-07-13-remote-management-auth-design.md`.

## Global Constraints

- **The user does ALL git commits.** Never commit or offer to. Each task ends at "tests green / boards green"; leave it **uncommitted**. ("Commit" steps = report green, don't commit.)
- **Auth = AEAD-sealed DM, NOT a separate signature** (spec §0.1). The `dm_open` success against the pinned `admin_pubkey` is the authentication. No Ed25519 command signature.
- **No-heap:** the KDF is **iterated BLAKE2b** — CPU-only, no work-area, no malloc (a tight loop over a 64-B state). Runs only in the one-time local `password` verb (never the hot path). (Argon2 was dropped 2026-07-13 — its KB work-area blocks the RAM-tight gateway + future small boards; spec §2.)
- **Open set = `status` + `routes` only** (unsealed binary TLV); everything else is sealed + admin-only (spec §4).
- **The `password` verb is LOCAL-ONLY** — never accepted over the mesh (spec §8).
- **Feature-gated:** the entire subsystem is `#if MR_FEAT_REMOTE_MGMT` (default 1; `MR_PROFILE_MOBILE ⇒ 0`), per `mr_features.h` (spec §10).
- **s18/s22 byte-identity:** the auth is device-side + a new sealed-command flag a static node never emits → **s18 `3ac88d40…` + s22 `d5f368a1…` must hold** (lib/core codec must be `0`-flag byte-identical). Verify in the gate.
- **Source header:** `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` on line 2 of every new file.
- **Salt:** a fixed 16-byte protocol constant (spec §2; per-deployment salt is a noted later hardening).

## File Structure

- **`lib/core/admin_auth.h` / `.cpp`** (CREATE) — the password KDF (`admin_key_from_password`), the sealed-command codec (`admin_cmd_seal`/`admin_cmd_open`), and the pure verify helper (`admin_cmd_verify`). Pure functions over buffers + `Identity` → **native doctest-testable**. Reuses `identity`/`dm_crypto`/monocypher.
- **`lib/core/mr_features.h`** (MODIFY) — add `MR_FEAT_REMOTE_MGMT` (default 1) + `MR_PROFILE_MOBILE ⇒ 0`.
- **`src/device_nv.h`** (MODIFY) — Blob v19→v20: `admin_pubkey[32]`, `admin_counter_floor`, `admin_provisioned`.
- **`lib/core/node.h` / `node.cpp`** (MODIFY) — `_admin_*` state + accessors + `admin_set_pubkey`/`admin_counter_check_advance`, all `#if MR_FEAT_REMOTE_MGMT` (stubbed inert off).
- **`src/fw_main.cpp`** (MODIFY) — the `password` dispatch verb (local-only), the reworked `remote_exec` (classify → trial-open → `console_binary` → seal), the admin-issue `unlock`+`rcmd` sealing.
- **`test/test_admin_auth.cpp`** (CREATE) — doctest for the KDF + seal/open + verify + counter (no `main`; `test_airtime.cpp` provides it).

---

## Task 1: Password → admin keypair (iterated-BLAKE2b KDF)

**Files:** Create `lib/core/admin_auth.h`, `lib/core/admin_auth.cpp`; Test `test/test_admin_auth.cpp`.

**Interfaces:**
- Consumes: monocypher `crypto_blake2b` + `crypto_blake2b_init`/`_update`/`_final` + `crypto_wipe`, `meshroute::Identity`, `identity_from_seed`.
- Produces: `constexpr uint8_t ADMIN_SALT[16]`; `void admin_key_from_password(const char* password, size_t pw_len, Identity& out)`; `constexpr uint32_t ADMIN_KDF_ITERS = 300000;`.

- [ ] **Step 1: Write the failing test** — `test/test_admin_auth.cpp`:

```cpp
// MeshRoute — test_admin_auth.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "admin_auth.h"
#include <cstring>
using namespace meshroute;

TEST_CASE("admin KDF — deterministic, password-sensitive") {
    Identity a{}, a2{}, b{};
    admin_key_from_password("correct horse battery staple", 28, a);
    admin_key_from_password("correct horse battery staple", 28, a2);
    admin_key_from_password("Correct horse battery staple", 28, b);   // 1 char differs
    CHECK(std::memcmp(a.ed_pub, a2.ed_pub, 32) == 0);   // deterministic
    CHECK(std::memcmp(a.ed_pub, b.ed_pub, 32) != 0);    // different password -> different key
    // key_hash32 = LE(ed_pub[0..3])
    CHECK(a.key_hash32 == (uint32_t(a.ed_pub[0]) | (uint32_t(a.ed_pub[1])<<8) | (uint32_t(a.ed_pub[2])<<16) | (uint32_t(a.ed_pub[3])<<24)));
}
```

- [ ] **Step 2: Run — verify it fails** (`admin_auth.h` missing). Run: `pio test -e native 2>&1 | grep -E "admin_auth.h|error:" | head`.

- [ ] **Step 3: Create `lib/core/admin_auth.h`:**

```cpp
// MeshRoute — lib/core/admin_auth.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Remote-management admin auth: password -> iterated-BLAKE2b seed -> admin keypair (identity_from_seed); and the
// AEAD-sealed command codec (dm_crypto X25519). The node pins only admin_pubkey and trial-opens gated commands — the
// Poly1305 tag is the authentication. See docs/superpowers/specs/2026-07-13-remote-management-auth-design.md.
#pragma once
#include <cstdint>
#include <cstddef>
#include "identity.h"

namespace meshroute {

// KDF stretch: N BLAKE2b iterations (CPU-hard, no memory work-area — Argon2 dropped, spec §2). 300k ≈ ~1-2 s on the
// nRF52 — TUNE at the bench (the one number that trades local-`password` latency against offline-grind cost).
// Salt = a fixed protocol constant (spec §2; a per-deployment salt is a noted later hardening).
inline constexpr uint32_t ADMIN_KDF_ITERS = 300000;
inline constexpr uint8_t  ADMIN_SALT[16] = { 'M','R','a','d','m','n','S','a','l','t','v','1',0,0,0,0 };

// password -> 32-B iterated-BLAKE2b seed -> identity_from_seed(out). `out` holds ed_pub / x_secret / key_hash32.
void admin_key_from_password(const char* password, size_t pw_len, Identity& out);

}  // namespace meshroute
```

- [ ] **Step 4: Create `lib/core/admin_auth.cpp`** (the KDF body):

```cpp
// MeshRoute — lib/core/admin_auth.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "admin_auth.h"
extern "C" {
#include "monocypher.h"
}

namespace meshroute {

void admin_key_from_password(const char* password, size_t pw_len, Identity& out) {
    uint8_t h[64];
    // initial mix: BLAKE2b(password ‖ salt) via the incremental API (no concat buffer)
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, sizeof h);
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(password), pw_len);
    crypto_blake2b_update(&ctx, ADMIN_SALT, sizeof ADMIN_SALT);
    crypto_blake2b_final(&ctx, h);
    // stretch: N iterations of BLAKE2b over the 64-B state (CPU-hard, zero extra RAM)
    for (uint32_t i = 0; i < ADMIN_KDF_ITERS; ++i) crypto_blake2b(h, sizeof h, h, sizeof h);
    identity_from_seed(out, h);         // consumes h[0..31] as the seed
    crypto_wipe(h, sizeof h);           // don't leave seed material on the stack
}

}  // namespace meshroute
```

- [ ] **Step 5: Run — verify pass.** Run: `pio test -e native -v 2>&1 | grep -E "admin KDF|Status:" | tail -3`. Expected: pass. (300k BLAKE2b iterations run in well under a second on the host — the native test is fast; the ~1-2 s budget is the nRF52 target, tuned at the bench.)

- [ ] **Step 6: Report green (do not commit).**

---

## Task 2: The sealed-command codec (seal / open)

**Files:** Modify `lib/core/admin_auth.h`/`.cpp`, `test/test_admin_auth.cpp`.

**Interfaces:**
- Consumes: `Identity`, `ed_pub_to_x25519`, `ecdh_shared`, `dm_kdf`, `dm_nonce`, `dm_seal`, `dm_open` (from `identity.h`/`dm_crypto.h`); Task 1.
- Produces:
  - `struct AdminCmd { uint32_t node_key_hash; uint32_t counter; const uint8_t* cmd; uint8_t cmd_len; };`
  - `size_t admin_cmd_seal(uint8_t* out, size_t cap, const Identity& admin, const uint8_t node_ed_pub[32], uint32_t node_key_hash, uint32_t counter, const uint8_t* cmd, uint8_t cmd_len, const uint8_t rand8[8], uint16_t nonce_ctr)` — returns frame len (0 on overflow). Frame = `[rand8][nonce_ctr u16][ct ...][tag 16]`; plaintext = `[node_key_hash u32][counter u32][cmd …]`.
  - `bool admin_cmd_open(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node, AdminCmd& out, uint8_t* pt_buf, size_t pt_cap)` — trial-opens with `admin_ed_pub`; true only on a valid tag.

- [ ] **Step 1: Write the failing test:**

```cpp
TEST_CASE("admin cmd — seal/open round-trip; wrong admin key fails") {
    Identity admin{}, node{}, attacker{};
    admin_key_from_password("adminpw", 7, admin);
    uint8_t nseed[32]; for (int i=0;i<32;++i) nseed[i]=uint8_t(i+1);   identity_from_seed(node, nseed);
    admin_key_from_password("attacker", 8, attacker);
    const uint8_t cmd[] = "reboot";
    uint8_t frame[128]; uint8_t rand8[8] = {1,2,3,4,5,6,7,8};
    size_t fl = admin_cmd_seal(frame, sizeof frame, admin, node.ed_pub, node.key_hash32, 5, cmd, 6, rand8, 1);
    CHECK(fl > 0);
    AdminCmd out{}; uint8_t pt[64];
    CHECK(admin_cmd_open(frame, fl, admin.ed_pub, node, out, pt, sizeof pt));   // genuine admin -> opens
    CHECK(out.node_key_hash == node.key_hash32);
    CHECK(out.counter == 5u);
    CHECK(out.cmd_len == 6); CHECK(std::memcmp(out.cmd, "reboot", 6) == 0);
    AdminCmd bad{};
    CHECK_FALSE(admin_cmd_open(frame, fl, attacker.ed_pub, node, bad, pt, sizeof pt));  // wrong pinned key -> tag fails
    frame[fl-1] ^= 0x01;   // flip a tag bit
    AdminCmd tampered{};
    CHECK_FALSE(admin_cmd_open(frame, fl, admin.ed_pub, node, tampered, pt, sizeof pt));
}
```

- [ ] **Step 2: Run — verify fail** (`admin_cmd_seal` undeclared).

- [ ] **Step 3a: Add to `admin_auth.h`** (before the closing namespace): the `AdminCmd` struct + the two decls above + `#include "dm_crypto.h"`.

- [ ] **Step 3b: Add to `admin_auth.cpp`** — mirror the `node_hashlocate.cpp:352-409` seal/open pattern:

```cpp
#include "dm_crypto.h"
// ... in namespace meshroute:
size_t admin_cmd_seal(uint8_t* out, size_t cap, const Identity& admin, const uint8_t node_ed_pub[32],
                      uint32_t node_key_hash, uint32_t counter, const uint8_t* cmd, uint8_t cmd_len,
                      const uint8_t rand8[8], uint16_t nonce_ctr) {
    const size_t pt_len = 8u + cmd_len;                       // [node_key_hash][counter][cmd]
    const size_t frame_len = 8u + 2u + pt_len + DM_TAG_LEN;   // [rand8][ctr][ct][tag]
    if (frame_len > cap) return 0;
    uint8_t pt[8 + 255];
    pt[0]=uint8_t(node_key_hash); pt[1]=uint8_t(node_key_hash>>8); pt[2]=uint8_t(node_key_hash>>16); pt[3]=uint8_t(node_key_hash>>24);
    pt[4]=uint8_t(counter); pt[5]=uint8_t(counter>>8); pt[6]=uint8_t(counter>>16); pt[7]=uint8_t(counter>>24);
    for (uint8_t i=0;i<cmd_len;++i) pt[8+i]=cmd[i];
    uint8_t node_x[32]; ed_pub_to_x25519(node_x, node_ed_pub);
    uint8_t shared[32]; ecdh_shared(shared, admin, node_x);
    uint8_t key[32];    dm_kdf(key, shared, admin.key_hash32, node_key_hash);
    uint8_t nonce[DM_NONCE_LEN]; dm_nonce(nonce, rand8, nonce_ctr, node_key_hash);
    for (int i=0;i<8;++i) out[i]=rand8[i];
    out[8]=uint8_t(nonce_ctr); out[9]=uint8_t(nonce_ctr>>8);
    uint8_t* ct = out + 10; uint8_t* tag = ct + pt_len;
    dm_seal(ct, tag, key, nonce, nullptr, 0, pt, pt_len);
    crypto_wipe(key, 32); crypto_wipe(shared, 32); crypto_wipe(pt, pt_len);
    return frame_len;
}
bool admin_cmd_open(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node,
                    AdminCmd& res, uint8_t* pt_buf, size_t pt_cap) {
    if (len < 8u + 2u + DM_TAG_LEN) return false;
    const size_t pt_len = len - 8u - 2u - DM_TAG_LEN;
    if (pt_len < 8u || pt_len > pt_cap) return false;
    const uint8_t* rand8 = frame;
    const uint16_t nonce_ctr = uint16_t(frame[8]) | (uint16_t(frame[9])<<8);
    const uint8_t* ct = frame + 10; const uint8_t* tag = ct + pt_len;
    uint8_t admin_x[32]; ed_pub_to_x25519(admin_x, admin_ed_pub);
    uint8_t shared[32];  ecdh_shared(shared, node, admin_x);
    const uint32_t admin_hash = uint32_t(admin_ed_pub[0]) | (uint32_t(admin_ed_pub[1])<<8) | (uint32_t(admin_ed_pub[2])<<16) | (uint32_t(admin_ed_pub[3])<<24);
    uint8_t key[32];   dm_kdf(key, shared, node.key_hash32, admin_hash);
    uint8_t nonce[DM_NONCE_LEN]; dm_nonce(nonce, rand8, nonce_ctr, node.key_hash32);
    const bool ok = dm_open(pt_buf, key, nonce, nullptr, 0, ct, pt_len, tag);
    crypto_wipe(key, 32); crypto_wipe(shared, 32);
    if (!ok) return false;
    res.node_key_hash = uint32_t(pt_buf[0]) | (uint32_t(pt_buf[1])<<8) | (uint32_t(pt_buf[2])<<16) | (uint32_t(pt_buf[3])<<24);
    res.counter       = uint32_t(pt_buf[4]) | (uint32_t(pt_buf[5])<<8) | (uint32_t(pt_buf[6])<<16) | (uint32_t(pt_buf[7])<<24);
    res.cmd = pt_buf + 8; res.cmd_len = uint8_t(pt_len - 8);
    return true;
}
```

- [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

> Note the seal binds the admin's *and* node's key_hash via `dm_kdf`, and `dm_nonce` binds `node_key_hash` — a command sealed for node X can't open on node Y (its `dm_kdf`/`dm_nonce` differ), which *also* covers the §5 `node_key_hash` cross-node defense at the crypto layer; the explicit `node_key_hash` field in the plaintext is the belt-and-suspenders check Task 5 enforces.

---

## Task 3: NV — pin `admin_pubkey` + `admin_counter_floor` (Blob v19→v20)

**Files:** Modify `src/device_nv.h`.

- [ ] **Step 1: Add the fields** to `struct Blob` (append at the end, before the reserved/pad, per the existing add-field convention) and bump `kVersion 19 → 20`:

```cpp
// v20: remote-management admin auth (pin the admin pubkey + the replay counter floor).
uint8_t  admin_pubkey[32] = {};    // pinned admin Ed25519 pubkey (0s = unprovisioned); trust anchor for gated rcmds
uint32_t admin_counter_floor = 0;  // highest accepted admin command counter (replay floor; write-coalesced)
uint8_t  admin_provisioned = 0;    // 1 once `password` set the pubkey (distinguishes "all-zero pubkey" from "unset")
```
Update the `kVersion` comment line and confirm `load()` still accepts `version >= 2 && version <= kVersion` (a v19 blob loads with the three new fields **zero-defaulted** = unprovisioned — the safe default). No migration needed (append-only + 0-default is the established pattern, memory `[[meshroute-internalfs-corruption-selfheal]]` / the anti-spam v15→v16 precedent).

- [ ] **Step 2: Build native + a board** (the Blob is `#include`d widely): `pio test -e native 2>&1 | grep -E "error:|Status:" | tail -2` then `pio run -e xiao_sx1262 2>&1 | grep -E "SUCCESS|error:" | tail -1`. Expected: green (no behaviour change yet).

- [ ] **Step 3: Report green (do not commit).**

---

## Task 4: Node admin state + accessors (feature-gated)

**Files:** Modify `lib/core/node.h`, `lib/core/node.cpp`.

**Interfaces:**
- Produces (on `Node`, `#if MR_FEAT_REMOTE_MGMT` real / else inert stubs): `bool admin_provisioned() const`; `const uint8_t* admin_pubkey() const`; `void admin_set_pubkey(const uint8_t ed_pub[32])` (pins + sets provisioned + requests an NV write); `uint32_t admin_counter_floor() const`; `bool admin_counter_check_advance(uint32_t counter)` (true + advance-floor if `counter > floor`, else false — the replay gate); the NV persist reuses the `channel_ctr`-lease write-coalescing.

- [ ] **Step 1: Add the state + accessors** to `node.h` behind `#if MR_FEAT_REMOTE_MGMT` (state members `uint8_t _admin_pubkey[32]`, `uint32_t _admin_counter_floor`, `bool _admin_provisioned`), with `#else` inline stubs (`admin_provisioned()→false`, `admin_pubkey()→nullptr`, `admin_set_pubkey→{}`, `admin_counter_check_advance→false`). Load them from the Blob in the NV-restore path (where the other Blob fields are read) and write them via the coalesced-NV path (mirror how `channel_ctr` / `config_epoch` persist). *(This task defines the seam; the caller wiring is Tasks 7-8.)*

- [ ] **Step 2: Native test** — drive the counter gate directly (add to `test_admin_auth.cpp` only if `admin_counter_check_advance` is reachable from a test Node; otherwise assert the pure logic in a small free helper `admin_counter_ok(floor, counter)` in `admin_auth.h` and test that): `CHECK(admin_counter_ok(5, 6)); CHECK_FALSE(admin_counter_ok(5, 5)); CHECK_FALSE(admin_counter_ok(5, 4));`. Implement `inline bool admin_counter_ok(uint32_t floor, uint32_t counter) { return counter > floor; }` in `admin_auth.h` and have `admin_counter_check_advance` use it.

- [ ] **Step 3: Build native + board.** Expected: green. - [ ] **Step 4: Report green (do not commit).**

---

## Task 5: The node-side verify gate (pure, native)

**Files:** Modify `lib/core/admin_auth.h`/`.cpp`, `test/test_admin_auth.cpp`.

**Interfaces:**
- Produces: `enum class AdminVerdict { ok, bad_tag, wrong_node, replay };`; `AdminVerdict admin_cmd_verify(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node, uint32_t counter_floor, AdminCmd& out, uint8_t* pt_buf, size_t pt_cap)` — composes `admin_cmd_open` + the `node_key_hash == node.key_hash32` check + `admin_counter_ok(floor, counter)`. The caller advances the floor + executes only on `ok`; every non-`ok` is a **silent drop** (spec §5), except the caller MAY emit the reject-hint on `replay` (a valid open but stale counter).

- [ ] **Step 1: Write the failing test** — valid → `ok`; wrong pinned key → `bad_tag`; a command sealed for a different node → `wrong_node`; a valid command with `counter <= floor` → `replay`; the reject-hint distinguishes `replay` (valid open) from `bad_tag` (never opened).

```cpp
TEST_CASE("admin verify — verdicts") {
    Identity admin{}, node{}; admin_key_from_password("pw", 2, admin);
    uint8_t ns[32]; for (int i=0;i<32;++i) ns[i]=uint8_t(0x40+i); identity_from_seed(node, ns);
    uint8_t frame[128], rand8[8]={9,8,7,6,5,4,3,2}, pt[64];
    const uint8_t cmd[]="reboot";
    size_t fl = admin_cmd_seal(frame, sizeof frame, admin, node.ed_pub, node.key_hash32, 10, cmd, 6, rand8, 1);
    AdminCmd o{};
    CHECK(admin_cmd_verify(frame, fl, admin.ed_pub, node, 9, o, pt, sizeof pt) == AdminVerdict::ok);
    CHECK(admin_cmd_verify(frame, fl, admin.ed_pub, node, 10, o, pt, sizeof pt) == AdminVerdict::replay);   // counter==floor
    // command sealed for a DIFFERENT node_key_hash -> wrong_node (open ok, hash mismatch)
    uint8_t frame2[128]; size_t fl2 = admin_cmd_seal(frame2, sizeof frame2, admin, node.ed_pub, node.key_hash32 ^ 0xFF, 11, cmd, 6, rand8, 2);
    // (sealed under a bogus node hash -> its dm_kdf differs from the node's -> the node's open FAILS -> bad_tag, the crypto-layer defense)
    CHECK(admin_cmd_verify(frame2, fl2, admin.ed_pub, node, 9, o, pt, sizeof pt) == AdminVerdict::bad_tag);
}
```

- [ ] **Step 2: Run — verify fail.** - [ ] **Step 3: Implement `admin_cmd_verify`** (open → if !ok `bad_tag`; if `out.node_key_hash != node.key_hash32` `wrong_node`; if `!admin_counter_ok(floor, out.counter)` `replay`; else `ok`). - [ ] **Step 4: Run — verify pass.** - [ ] **Step 5: Report green (do not commit).**

> The test shows the layered defense: a command sealed under a *wrong* `node_key_hash` can't even open on the real node (its `dm_kdf`/`dm_nonce` key on `node.key_hash32`) → `bad_tag`. The explicit `node_key_hash` field + `wrong_node` verdict is the belt-and-suspenders for the case where a genuine-but-misrouted admin command reaches the wrong node.

---

## Task 6: `MR_FEAT_REMOTE_MGMT` feature-split

**Files:** Modify `lib/core/mr_features.h`.

- [ ] **Step 1:** Add the default + the mobile-profile override, mirroring the existing `MR_FEAT_*` blocks:

```cpp
// in the MR_PROFILE_MOBILE block (with MR_FEAT_MOBILE=1 etc.):
#  define MR_FEAT_REMOTE_MGMT 0   // a roaming personal endpoint is managed LOCALLY, never a remotely-administered relay (spec §10)
// in the defaults:
#ifndef MR_FEAT_REMOTE_MGMT
#  define MR_FEAT_REMOTE_MGMT 1    // static relays + gateways + native/full: on
#endif
```
(If `MR_PROFILE_MOBILE` isn't a defined block yet — the feature-split left mobile boards on the all-on default — add the one `#if defined(MR_PROFILE_MOBILE) #define MR_FEAT_REMOTE_MGMT 0 #endif` guard; a mobile env then sets `-DMR_PROFILE_MOBILE`. Confirm against `mr_features.h`'s current shape.)

- [ ] **Step 2:** Wrap the Task 3-5 state/accessors + the Task 7-9 fw_main handlers in `#if MR_FEAT_REMOTE_MGMT` (the stubs from Task 4 make call sites inert). - [ ] **Step 3: Build a mobile-profile env with `MR_FEAT_REMOTE_MGMT=0`** to prove it compiles inert: `PLATFORMIO_BUILD_FLAGS="-DMR_FEAT_REMOTE_MGMT=0" pio run -e xiao_sx1262 2>&1 | grep -E "SUCCESS|error:" | tail -1`. Expected: SUCCESS. - [ ] **Step 4: Report green.**

---

## Task 7: The `password` dispatch verb (local-only)

**Files:** Modify `src/fw_main.cpp`.

- [ ] **Step 1: Add `handle_password(const char* args, Print& out)`** (behind `#if MR_FEAT_REMOTE_MGMT`): parse the passphrase (rest of line, allow spaces); `admin_key_from_password(pw, len, admin_id)`; `g_node.admin_set_pubkey(admin_id.ed_pub)`; persist NV; `crypto_wipe(&admin_id, sizeof admin_id)`; `out.print(F("> admin pubkey pinned: "))` + the hex of `ed_pub[0..3]` (a fingerprint, NOT the password). Wire it into the `dispatch` verb table: `if (len > 9 && !strncmp(line, "password ", 9)) { handle_password(line + 9, out); return true; }`.

- [ ] **Step 2: LOCAL-ONLY guard** — the `password` verb must NOT be reachable via `remote_exec` (Task 8's classifier never routes `password` to a handler; it's not in the remote verb set). Add `password rotate <hex-pubkey>` handling here too (gated path is Task 8/10). Verify the verb is absent from the remote allow-list.

- [ ] **Step 3: Build the boards** (`xiao_sx1262`/`gateway`/`heltec_v3`). Expected: SUCCESS. Bench-note: `password test123` on USB pins a fingerprint; re-running with the same passphrase prints the same fingerprint (determinism, on-device mirror of the Task-1 native test). - [ ] **Step 4: Report green.**

---

## Task 8: Rework `remote_exec` — classify → trial-open → binary → seal

**Files:** Modify `src/fw_main.cpp`.

**Interfaces:** Consumes `admin_cmd_verify` (Task 5), `g_node.admin_pubkey()`/`admin_counter_check_advance()` (Task 4), `console_binary::enc_*` (built), `send_remote_response` (existing).

- [ ] **Step 1: Replace `remote_exec`'s body** (behind `#if MR_FEAT_REMOTE_MGMT`) with:
  1. **Sealed?** peek the leading flag byte (spec §6: a flag/version byte on the `REMOTE_CMD` body distinguishes sealed-vs-open). If sealed → `admin_cmd_verify(body, len, g_node.admin_pubkey(), g_identity, g_node.admin_counter_floor(), cmd, pt, …)`; `ok` → `g_node.admin_counter_check_advance(cmd.counter)` (persist) + execute the inner `cmd`; `replay` → send the reject-hint (`counter-floor=N`, sealed); else **silent drop**. If unsealed → only `status`/`routes` allowed (else silent drop).
  2. **Execute + encode:** classify the verb → data verb: gather the fields (as `dump_*`/`remote_exec` do today) → `console_binary::enc_status/enc_routes/enc_cfg/…` into a `BufferSink`-style `uint8_t[241]` → the response bytes. Action verb (`reboot`/`prep-restart`): arm the deferred action + a sealed `ok`.
  3. **Seal the response** (gated only): seal the TLV back to the admin (reuse `admin_cmd_seal`-style with the roles swapped: node seals to `admin_pubkey`), then `send_remote_response(from, sealed, len)`. Open reads: `send_remote_response(from, tlv, len)` unsealed.

- [ ] **Step 2: Build the boards.** Expected: SUCCESS. - [ ] **Step 3: Report green.** *(End-to-end verify is bench + the Task 11 native round-trip.)*

> Keep the flag-byte framing minimal: `body[0]` bit = sealed; the rest is either the cleartext verb keyword (open) or the Task-2 sealed frame (gated). The lib/core codec stays `0`-flag byte-identical for every non-remote-mgmt frame (s18/s22 gate).

---

## Task 9: Admin-issue side — `unlock` + sealed `rcmd`

**Files:** Modify `src/fw_main.cpp`.

- [ ] **Step 1: `handle_unlock(const char* pw, Print& out)`** (behind `#if MR_FEAT_REMOTE_MGMT`) — derive the admin `Identity` from the passphrase (`admin_key_from_password`) and hold it in a **static transient** `g_admin_unlocked` (+ its per-target counters); `out.print(F("> unlocked"))`. `lock` wipes it. (This is the operator-device side; the credential lives in RAM until `lock`/reboot.)
- [ ] **Step 2: Extend `handle_rcmd`** — if the target verb is gated and `g_admin_unlocked` is set: `admin_cmd_seal(frame, …, g_admin_unlocked, target_ed_pub, target_key_hash, ++counter, cmd, …)` (needs the target's pubkey — from the peer cache / a WANT_PUBKEY, reusing the `reqpubkey` path) → `send_remote_cmd(dst, frame, len)` with the sealed flag byte. Open verbs (`status`/`routes`) send unsealed as today.
- [ ] **Step 3: Build the boards.** Expected: SUCCESS. - [ ] **Step 4: Report green.**

> The counter is per-(credential,target); a fresh admin device starts at 0 and the node's reject-hint (`counter-floor=N`) bumps it on the first try (spec §5 resync). Persist the admin device's counters in NV so a reboot doesn't replay-collide (a small `admin_tx_counter` per known target, or a single monotonic — the plan's call; a single monotonic across targets is simplest and safe since the node floor is per-node).

---

## Task 10: `password rotate` (gated OTA)

**Files:** Modify `src/fw_main.cpp`.

- [ ] **Step 1:** Add `password rotate <new_pubkey_hex>` as a **gated** remote verb: when `remote_exec` opens a sealed `password rotate <hex>` under the *current* `admin_pubkey`, `g_node.admin_set_pubkey(new)` + reset `admin_counter_floor` to 0 + persist. Locally, `password <newpw>` already rotates (Task 7). - [ ] **Step 2: Build.** - [ ] **Step 3: Report green.**

---

## Task 11: Native end-to-end + feature-off

**Files:** Modify `test/test_admin_auth.cpp`.

- [ ] **Step 1: End-to-end test** — password→admin keypair; seal `reboot` for a node; `admin_cmd_verify` → `ok`; advance floor; re-verify same frame → `replay`; a `status` TLV response `enc_status`→`dec_status` round-trips (link `console_binary`). - [ ] **Step 2:** confirm a `-DMR_FEAT_REMOTE_MGMT=0` native compile leaves the `Node` admin stubs inert (a small compile-only assertion, or rely on Task 6's board build). - [ ] **Step 3: Run.** Expected: all `admin *` cases pass. - [ ] **Step 4: Report green.**

---

## Task 12: Gate

- [ ] **Step 1: Native** — `pio test -e native -v 2>&1 | grep -E "test cases:|assertions:|Status:"` → all pass, SUCCESS.
- [ ] **Step 2: All envs build**, incl. a mobile profile with `MR_FEAT_REMOTE_MGMT=0`: `for e in native xiao_sx1262 heltec_v3 gateway gateway_heltec production; do pio run -e $e 2>&1 | grep -E "SUCCESS|FAILED|error:" | tail -1; done` + `PLATFORMIO_BUILD_FLAGS="-DMR_FEAT_REMOTE_MGMT=0" pio run -e xiao_sx1262`. Expected: all SUCCESS.
- [ ] **Step 3: KDF time budget** — no RAM to check (the iterated-BLAKE2b KDF has no work-area). On a flashed `xiao_sx1262`/`gateway`, time the local `password <pw>` command and tune `ADMIN_KDF_ITERS` so it derives in **~1–2 s** (300k is the starting point; halve/double to hit the knee). This is a bench-tune, not a build gate.
- [ ] **Step 4: s18/s22 unchanged** — `cmake --build ~/lora-universal-simulator/build --target lus -j4 >/dev/null && LUS=~/lora-universal-simulator/build/orchestrator/lus && $LUS -e meshroute simulation/s18_meshroute.json /tmp/s18.ndjson >/dev/null 2>&1 && md5sum /tmp/s18.ndjson && $LUS -e meshroute simulation/s22_mobile_team_meshroute.json /tmp/s22.ndjson >/dev/null 2>&1 && md5sum /tmp/s22.ndjson` → s18 `3ac88d40e00d2605ff66659f696d52bf`, s22 `d5f368a1d275cce5b1e1a0bb60b8753f`.
- [ ] **Step 5: Report the full gate green (do not commit).**

---

## Self-Review

- **Spec coverage:** §2 password→keypair → Task 1. §5 sealed-command + counter + node-hash → Tasks 2/5. §6 wire (flag byte, sealed frame, binary response) → Tasks 2/8. §7 node state (`admin_pubkey`/`counter_floor`) → Tasks 3/4. §8 `password` set + rotate → Tasks 7/10. §0.4/§4 binary responses + open=status/routes → Task 8 (reuses `console_binary`). §9 reuses → Tasks 1/2/8 (dm_crypto, identity, console_binary, dispatch). §10 feature-split → Task 6. §12 testing → Tasks 1/2/5/11 + the Task 12 gate. §13 KDF iteration count → Task 1 (`ADMIN_KDF_ITERS`) + the Task 12 Step 3 bench-tune.
- **Placeholder scan:** the crypto tasks (1/2/5) carry complete code; the wiring tasks (7-10) give the exact call sequence + signatures (not "add handling"). The one genuinely deferred design call — the admin-device counter persistence shape (Task 9) — is flagged as an explicit implementation decision with the trade-off stated, not hidden.
- **Type consistency:** `AdminCmd`, `AdminVerdict`, `admin_key_from_password`, `admin_cmd_seal/open/verify`, `admin_counter_ok`, `admin_set_pubkey`/`admin_counter_check_advance` are named consistently across tasks; `Identity`/`dm_seal`/`dm_open`/`ed_pub_to_x25519`/`ecdh_shared`/`dm_kdf`/`dm_nonce` match the real `identity.h`/`dm_crypto.h` signatures; `enc_status`/`dec_status` match the built `console_binary`.
- **Open risks flagged for review:** (1) `ADMIN_KDF_ITERS` bench-tune for ~1–2 s on the nRF52 — Task 12 Step 3 (no RAM risk now — Argon2 dropped); (2) admin-device counter persistence — Task 9; (3) confirm the `MR_PROFILE_MOBILE` block exists in `mr_features.h` or add the guard — Task 6 Step 1.
