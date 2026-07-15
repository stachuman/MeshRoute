// MeshRoute — src/firmware_commands.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The console command cluster (see firmware_commands.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-15).
// Shared device state comes from fw_context.h; the STAY-set board-glue (do_reboot/do_ota/dump_faults/
// handle_crashtest/handle_prep_restart) is reached ONLY via the fw_context.h wrappers — this TU MUST NOT include
// device_fault.h (its ISR vectors + the MRFAULT_HW/MRFAULT_ESP32 macros are single-TU). Behaviour-preserving.
#include "firmware_commands.h"
#include "fw_context.h"        // g_node + the shared state
#include "device_nv.h"         // mrnv::PeerBlob / load_peers / save_peers
#include <cstdio>              // snprintf
#include <cstring>             // memcpy

namespace mrfw {

static bool persist_pinned_peer(uint32_t kh, const uint8_t ed_pub[32]) {
    mrnv::PeerBlob pb{};
    if (!mrnv::load_peers(pb)) { pb = mrnv::PeerBlob{}; pb.magic = mrnv::kPeersMagic; pb.version = mrnv::kPeersVersion; pb.count = 0; }
    for (uint16_t i = 0; i < pb.count && i < mrnv::kMaxPinnedPeers; ++i)
        if (pb.rec[i].key_hash32 == kh) { memcpy(pb.rec[i].ed_pub, ed_pub, 32); return mrnv::save_peers(pb); }   // update in place
    if (pb.count >= mrnv::kMaxPinnedPeers) return false;                                                          // store full
    pb.rec[pb.count].key_hash32 = kh; memcpy(pb.rec[pb.count].ed_pub, ed_pub, 32); pb.count++;
    pb.magic = mrnv::kPeersMagic; pb.version = mrnv::kPeersVersion;
    return mrnv::save_peers(pb);
}
// E2E §3: a `peerkey` command -> install the RAM PINNED key (Node::on_command) + persist to /mrpeers + the contract ack.
size_t handle_peerkey(char* out, size_t cap, const meshroute::Command& cmd) {
    const uint8_t* ep = cmd.u.peerkey.ed_pub;
    const uint32_t kh = (uint32_t)ep[0] | ((uint32_t)ep[1] << 8) | ((uint32_t)ep[2] << 16) | ((uint32_t)ep[3] << 24);
    if (g_node.on_command(cmd).code != meshroute::CmdCode::queued)        // false only when the cache is full of pinned keys
        return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_err\",\"reason\":\"full\"}\n");
    persist_pinned_peer(kh, ep);                                          // best-effort NV (bench); the RAM key works regardless
    return (size_t)snprintf(out, cap, "{\"ev\":\"peerkey_set\",\"hash\":%lu,\"pinned\":true}\n", (unsigned long)kh);
}

}  // namespace mrfw
