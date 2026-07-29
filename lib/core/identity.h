// MeshRoute — lib/core/identity.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Node cryptographic identity. ONE 32-byte master seed derives everything:
// an Ed25519 keypair (sign/verify — the join challenge, later) + an X25519
// keypair (ECDH — opt-in DM E2E, later) + the 32-bit routing handle key_hash32.
// See docs/specs/2026-06-05-identity-leaf-membership-join-design.md §1–§2.
//
// HONEST-NODE model (§0): keys are PURELY INTERNAL — monocypher's EdDSA is
// curve25519 + BLAKE2b, NOT RFC-8032 SHA-512 Ed25519, so these keys are not
// interoperable with stock Ed25519 tooling (fine while identity stays internal).
//
// Platform-neutral: depends only on monocypher (vendored, lib/monocypher). The
// callers of this module live in the BACKENDS, not here, and are SEPARATE slices:
//   - Slice A2 (DONE) = the SIM seam — the simulator's SimController derives
//     key_hash32 from a per-node seed via identity_from_seed (lora-universal-
//     simulator/orchestrator/runtime/SimController.cpp). This module is exercised
//     there, NOT in MeshRoute's own src/ yet.
//   - Device wiring (DONE) = HW-RNG seed -> /mrid NV -> identity_from_seed in the
//     fw_main boot path + `regen` / `cfg set name`; set_identity + set_crypto_identity
//     install the derived key_hash32 + X25519/ed_pub (fw_main.cpp boot). The old
//     key_for(id) placeholder is GONE — this module is live on metal.
// `name` (§1.3) is app-level metadata, not a crypto concern, so it is NOT here.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include <cstdint>
#include <cstddef>

namespace MESHROUTE_NS {

// All key material for one node. ~196 B; one per node, loaded from /mrid on device.
struct Identity {
    uint8_t  seed[32];        // the master secret — everything below derives from it
    uint8_t  ed_pub[32];      // Ed25519 public key = THE identity (the full pubkey is the trust anchor)
    uint8_t  ed_secret[64];   // monocypher eddsa secret_key (seed || ed_pub); used to sign (join, later)
    uint8_t  x_secret[32];    // X25519 secret scalar (ECDH) — birationally matched to ed_pub
    uint8_t  x_pub[32];       // X25519 public key (== ed_pub_to_x25519(ed_pub))
    uint32_t key_hash32;      // routing handle = LE(ed_pub[0..3]); NOT a security anchor (32-bit, grindable)
};

// Derive a full identity from a 32-byte master seed. Does NOT modify/wipe the caller's `seed`
// (monocypher wipes its seed argument, so this passes a scratch copy).
void identity_from_seed(Identity& out, const uint8_t seed[32]);

// The canonical routing handle: the first 4 bytes of ed_pub, little-endian (matches the wire u32_le).
uint32_t key_hash32_of(const uint8_t ed_pub[32]);

// Convert a peer's advertised Ed25519 pubkey to its X25519 pubkey (so we can ECDH to that peer
// while only its ed_pub is ever advertised). The receiver advertises ed_pub; the sender converts.
void ed_pub_to_x25519(uint8_t x_pub_out[32], const uint8_t peer_ed_pub[32]);

// X25519 ECDH: our x_secret · peer_x_pub -> a 32-byte raw shared point. KDF it (BLAKE2b) before
// using it as a key — the AEAD framing is the DM-E2E slice, not this module.
void ecdh_shared(uint8_t shared_out[32], const Identity& self, const uint8_t peer_x_pub[32]);

// ---- TEAM CHANNEL keypair (spec 2026-07-26 §2.1, slice T-K1) --------------------------------------
// A team's CONTENT key: a dedicated X25519 pair gating who may READ the team's channel posts. It is
// deliberately INDEPENDENT of everything above — NOT derived from the identity master seed, and NOT an
// input to the team_id (which stays FNV(key_hash32 ‖ nonce): a membership handle, never a secret).
// Membership is open-by-knowledge; readership is not. ONE derivation path serves both uses:
//   * MINT  (`team new`)               — `scalar_in` = 32 fresh bytes from IHal::rand_bytes;
//   * ADOPT (`tkpriv=`, T-K3 grant, T-K4 QR) — `scalar_in` = the supplied private key.
//
// ★ THE CLAMPING CONTRACT — code-verified against the vendored monocypher, not assumed:
//   `crypto_x25519(out, sk, pk)` (monocypher.c:1546) trims a LOCAL COPY of `sk` via
//   `crypto_eddsa_trim_scalar` (:1469 — `out[0] &= 248; out[31] &= 127; out[31] |= 64`) and never
//   writes back to the caller's scalar; `crypto_x25519_public_key(pk, sk)` (:1557) is literally
//   `crypto_x25519(pk, sk, {9})`, so it clamps too.
//   ⇒ monocypher clamps at USE, never at STORE. An UNCLAMPED stored scalar therefore still round-trips
//   correctly *within* monocypher — which is exactly the trap: those same bytes handed to a
//   store-clamping implementation, or shipped over the T-K3 grant / T-K4 QR to one, would derive a
//   DIFFERENT public key, and the pair would look fine here while being non-interoperable there. So we
//   store the CLAMPED scalar and only ever that: the persisted/distributed bytes are then the canonical
//   RFC-7748 scalar and `pub == X25519(priv, 9)` holds under BOTH conventions. Clamping is idempotent
//   (bit 6 of byte 31 survives `& 127`), so re-deriving from an already-canonical scalar — the adopt
//   path — reproduces the identical pair byte-for-byte. Pinned by the RFC 7748 §6.1 KAT in
//   test/test_identity.cpp; a round-trip test alone would pass even with both halves wrong together.
//
// C2 (fail loud, no half-keypair): returns false and writes NOTHING to pub/priv when `scalar_in` is
// all-zero (a dead RNG — the twin of e2e_seal_inner's R7 seed guard) or when the derived public key is
// degenerate (all-zero — the L10 low-order-point idiom). A caller that gets false must mint nothing.
bool team_channel_key_derive(uint8_t pub[32], uint8_t priv[32], const uint8_t scalar_in[32]);

}  // namespace meshroute
