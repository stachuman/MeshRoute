// MeshRoute — lib/core/admin_auth.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "admin_auth.h"
#include "monocypher.h"   // crypto_blake2b(+incremental) / crypto_wipe (own C++ linkage guards, as identity.cpp)

namespace meshroute {

void admin_key_from_password(const char* password, size_t pw_len, Identity& out, void (*yield)()) {
    uint8_t h[64];
    // initial mix: BLAKE2b(password || salt) via the incremental API (no concat buffer)
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, sizeof h);
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(password), pw_len);
    crypto_blake2b_update(&ctx, ADMIN_SALT, sizeof ADMIN_SALT);
    crypto_blake2b_final(&ctx, h);
    // stretch: N iterations of BLAKE2b over the 64-B state (CPU-hard, zero extra RAM). This blocks the loop for seconds
    // on-device, so feed the watchdog via `yield` every 1024 iters (the whole point: the caller can't be reset here).
    for (uint32_t i = 0; i < ADMIN_KDF_ITERS; ++i) {
        crypto_blake2b(h, sizeof h, h, sizeof h);
        if (yield && (i & 0x3FF) == 0) yield();
    }
    identity_from_seed(out, h);         // consumes h[0..31] as the seed
    crypto_wipe(h, sizeof h);           // don't leave seed material on the stack
}

// ===== sealed-command codec (mirrors node_hashlocate.cpp:352-409) =====
static uint32_t hash32_of(const uint8_t ed_pub[32]) {
    return uint32_t(ed_pub[0]) | (uint32_t(ed_pub[1])<<8) | (uint32_t(ed_pub[2])<<16) | (uint32_t(ed_pub[3])<<24);
}

size_t admin_cmd_seal(uint8_t* out, size_t cap, const Identity& admin, const uint8_t node_ed_pub[32],
                      uint32_t node_key_hash, uint32_t counter, const uint8_t* cmd, uint8_t cmd_len,
                      const uint8_t rand8[8], uint16_t nonce_ctr) {
    const size_t pt_len    = 8u + cmd_len;                   // [node_key_hash][counter][cmd]
    const size_t frame_len = 8u + 2u + pt_len + DM_TAG_LEN;  // [rand8][ctr][ct][tag]
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
    uint8_t key[32];   dm_kdf(key, shared, node.key_hash32, hash32_of(admin_ed_pub));
    uint8_t nonce[DM_NONCE_LEN]; dm_nonce(nonce, rand8, nonce_ctr, node.key_hash32);
    const bool ok = dm_open(pt_buf, key, nonce, nullptr, 0, ct, pt_len, tag);
    crypto_wipe(key, 32); crypto_wipe(shared, 32);
    if (!ok) return false;
    res.node_key_hash = uint32_t(pt_buf[0]) | (uint32_t(pt_buf[1])<<8) | (uint32_t(pt_buf[2])<<16) | (uint32_t(pt_buf[3])<<24);
    res.counter       = uint32_t(pt_buf[4]) | (uint32_t(pt_buf[5])<<8) | (uint32_t(pt_buf[6])<<16) | (uint32_t(pt_buf[7])<<24);
    res.cmd = pt_buf + 8; res.cmd_len = uint8_t(pt_len - 8);
    return true;
}

AdminVerdict admin_cmd_verify(const uint8_t* frame, size_t len, const uint8_t admin_ed_pub[32], const Identity& node,
                              uint32_t counter_floor, AdminCmd& out, uint8_t* pt_buf, size_t pt_cap) {
    if (!admin_cmd_open(frame, len, admin_ed_pub, node, out, pt_buf, pt_cap)) return AdminVerdict::bad_tag;
    if (out.node_key_hash != node.key_hash32)          return AdminVerdict::wrong_node;
    if (!admin_counter_ok(counter_floor, out.counter)) return AdminVerdict::replay;
    return AdminVerdict::ok;
}

}  // namespace meshroute
