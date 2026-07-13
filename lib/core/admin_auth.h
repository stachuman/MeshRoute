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
#include "dm_crypto.h"   // dm_seal/open/kdf/nonce + DM_TAG_LEN / DM_NONCE_LEN / DM_NONCE_SEED_LEN

namespace meshroute {

// KDF stretch: N BLAKE2b iterations (CPU-hard, no memory work-area — Argon2 dropped, spec §2). This is a MULTI-SECOND
// blocking op on the nRF52 (~25 us/iter measured => 300k blew the 8 s watchdog); the caller MUST pass a `yield` that
// feeds the device watchdog. TUNE `ADMIN_KDF_ITERS` at the bench for ~1-2 s (trades local-`password` latency against
// offline-grind cost). Salt = a fixed protocol constant (spec §2; a per-deployment salt is a noted later hardening).
inline constexpr uint32_t ADMIN_KDF_ITERS = 12000;   // ~2 s on nRF52 (MEASURED ~175 us/iter: 80k => 14 s; 12k => ~2 s).
                                                     // ⚠ Changing this changes the derived admin_pubkey for a given password
                                                     // -> effectively FROZEN once a fleet is provisioned (re-`password` all nodes to change it).
inline constexpr uint8_t  ADMIN_SALT[16] = { 'M','R','a','d','m','n','S','a','l','t','v','1',0,0,0,0 };

// password -> 32-B iterated-BLAKE2b seed -> identity_from_seed(out). `out` holds ed_pub / x_secret / key_hash32.
// `yield` (if given) is called periodically DURING the stretch loop so the caller can feed the watchdog / service the
// loop — mandatory on-device (the loop is otherwise blocked for seconds). nullptr on the host (native test).
void admin_key_from_password(const char* password, size_t pw_len, Identity& out, void (*yield)() = nullptr);

// ===== sealed-command codec =====
// A gated command's sealed plaintext = [node_key_hash u32][counter u32][command …].
struct AdminCmd { uint32_t node_key_hash = 0; uint32_t counter = 0; const uint8_t* cmd = nullptr; uint8_t cmd_len = 0; };

// Seal `cmd` for `node_ed_pub` from the admin identity. Frame = [rand8(8)][nonce_ctr u16][ct][tag 16]. Returns frame
// len, 0 on overflow. (`rand8` = 8 fresh random bytes; `nonce_ctr` = a per-session monotonic to vary the nonce.)
size_t admin_cmd_seal(uint8_t* out, size_t cap, const Identity& admin, const uint8_t node_ed_pub[32],
                      uint32_t node_key_hash, uint32_t counter, const uint8_t* cmd, uint8_t cmd_len,
                      const uint8_t rand8[8], uint16_t nonce_ctr);
// Trial-open a frame with `admin_ed_pub` against the node identity. true ONLY on a valid tag; fills `res` (pointing
// into `pt_buf`). `pt_buf`/`pt_cap` receive the decrypted plaintext.
bool admin_cmd_open(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node,
                    AdminCmd& res, uint8_t* pt_buf, size_t pt_cap);

// Replay floor: a command counter is accepted only strictly above the persisted floor.
inline bool admin_counter_ok(uint32_t floor, uint32_t counter) { return counter > floor; }

// The node-side verdict: open + node_key_hash match + counter-floor. `ok` => caller advances the floor + executes;
// every non-`ok` is a silent drop (spec §5) — except `replay` (a valid open but stale counter) MAY emit the reject-hint.
enum class AdminVerdict { ok, bad_tag, wrong_node, replay };
AdminVerdict admin_cmd_verify(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node,
                              uint32_t counter_floor, AdminCmd& out, uint8_t* pt_buf, size_t pt_cap);

}  // namespace meshroute
