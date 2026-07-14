// MeshRoute — src/firmware_remote.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The REMOTE-MANAGEMENT cluster extracted from fw_main.cpp (cleanup 2026-07-14, codebase-review triage
// "split firmware by responsibility" — first cluster onto the fw_context.h seam). Both ends of the one protocol
// live in firmware_remote.cpp: the TARGET/exec side (remote_exec + its internal encode/seal/verb helpers) and
// the operator ISSUE side (rcmd/unlock/lock). They share the admin sealing crypto + REMOTE_FLAG_SEALED.
//
// NOT moved (deliberate): handle_password stays in fw_main — it PROVISIONS the node's own admin trust anchor
// via seed_blob_from_live() (config/provisioning family, same as create/join/cfg set), and moves with the
// future firmware_config cluster. g_admin_* stay `extern` in fw_context.h (mesh_service_once opens sealed
// replies with them — they are shared, not cluster-private). remote_exec does NOT call dispatch() — it carries
// its own verb table + remote_encode, so no command-dispatch entry point is exposed here.
//
// DEVICE-layer header (Print). MR_FEAT_REMOTE_MGMT-gated interface.
#pragma once
#include <Arduino.h>      // Print
#include <cstdint>
#include "mr_features.h"   // MR_FEAT_REMOTE_MGMT

// §remote-mgmt wire flag — SHARED (mesh_service_once opens sealed ACK/hint replies; remote_exec/handle_rcmd seal).
// inline constexpr => ONE definition across TUs (fw_main + firmware_remote.cpp read the same value). 0x1B: NOT 0x01
// (a console_binary TLV starts ver=0x01, so an unsealed data response can't be misread as sealed) and below any
// verb-keyword char (so cleartext vs sealed is never ambiguous either).
inline constexpr uint8_t REMOTE_FLAG_SEALED = 0x1B;

namespace mrfw {

// Public entry points fw_main.cpp calls (mesh_service_once + dispatch). The internal helpers
// (remote_encode / remote_verb_open / remote_seal_resp / admin_verb_gated) stay static in firmware_remote.cpp.
void remote_exec(uint8_t from, const uint8_t* query, uint8_t qlen);   // mesh RX -> exec (real, or an inert stub when remote-mgmt is off)
void handle_rcmd(const char* args, Print& out);                       // `rcmd <dst> <verb>` origin (always present; the SEALED path is gated internally)
#if MR_FEAT_REMOTE_MGMT
void handle_unlock(const char* args, Print& out);                     // admin-issue: derive the admin key into RAM
void handle_lock(Print& out);                                         // wipe the unlocked admin key
#endif

}  // namespace mrfw
