// MeshRoute — src/firmware_config.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The CONFIG / PROVISIONING cluster extracted from fw_main.cpp (cleanup 2026-07-14, codebase-review triage
// "split firmware by responsibility"). Staged in two increments (both land here): A = apply_radio_live +
// handle_cfg_set + gw_*/handle_gateway; B = seed_blob_from_live + provision_apply_live + the join/create/team/
// mobile/leave/password verbs. This file grows as each increment lands.
//
// Increment A step 1: apply_radio_live is the shared radio-retune helper that BOTH handle_cfg_set (moving) and
// fw_main's still-resident provision_apply_live call — so it is exposed here first, validating the module seam
// before the larger moves. Shared device state comes from fw_context.h; mrnv::Blob from device_nv.h.
//
// DEVICE-layer header.
#pragma once
#include <Arduino.h>     // Print
#include "device_nv.h"   // mrnv::Blob

namespace mrfw {

// Apply the RADIO operating point from the (just-saved) NV blob LIVE — no reboot. `reconfig` re-tunes the radio
// (freq/SF/BW/CR changed); a tx_power-only change skips the re-tune (it is set per-TX via the Hal).
void apply_radio_live(const mrnv::Blob& b, bool reconfig);

// `cfg set <key> <value>` — accumulate onto the pending NV blob + apply live where possible (dispatch verb).
void handle_cfg_set(const char* args, Print& out);

// `gateway l0=…:l1=… [win0=…] [beacon=…] [gateway_only]` — one-command dual-layer gateway provisioning (dispatch verb).
void handle_gateway(const char* args, Print& out);

}  // namespace mrfw
