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
#include "mr_features.h" // MR_FEAT_MOBILE / MR_FEAT_REMOTE_MGMT (guards below)

namespace mrfw {

// apply_radio_live + seed_blob_from_live + provision_apply_live are INTERNAL to firmware_config.cpp (file-static) —
// all their callers live in that TU. (Increment A briefly exposed apply_radio_live to bridge to fw_main's then-
// resident provision_apply_live; Increment B moved provision_apply_live too, so it reverts to static.)

// `cfg set <key> <value>` — accumulate onto the pending NV blob + apply live where possible (dispatch verb).
void handle_cfg_set(const char* args, Print& out);

// `gateway l0=…:l1=… [win0=…] [beacon=…] [gateway_only]` — one-command dual-layer gateway provisioning (dispatch verb).
void handle_gateway(const char* args, Print& out);

// Normal-node provisioning verbs (dispatch; compiled out on the gateway build).
#if MR_N_LAYERS < 2
void handle_join(const char* args, Print& out);       // set the radio floor + (re-)DAD; auto-pull the leaf config
void handle_create(const char* args, Print& out);     // join's floor + mint a MANAGED leaf (mother)
void handle_team(const char* args, Print& out);       // `team new` mint / `team <id>` join / `team 0` leave
#if MR_FEAT_MOBILE
void handle_mobile(const char* args, Print& out);     // mobile register/gateways/query/status
#endif
#endif
void handle_leave(Print& out);                        // wipe to default (keep freq); go unprovisioned + idle

#if MR_FEAT_REMOTE_MGMT
void handle_password(const char* args, Print& out);   // LOCAL-only: derive + pin the admin credential
#endif

}  // namespace mrfw
