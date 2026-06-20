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

}  // namespace meshroute
