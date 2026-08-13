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
#include "firmware_config_service.h"   // §UI-14 / [[B193]]: mrfw::ICfgStore / ICfgLive — the two seams bound below

namespace mrfw {

// apply_radio_live + seed_blob_from_live + provision_apply_live are INTERNAL to firmware_config.cpp (file-static) —
// all their callers live in that TU. (Increment A briefly exposed apply_radio_live to bridge to fw_main's then-
// resident provision_apply_live; Increment B moved provision_apply_live too, so it reverts to static.)

// §nv-ritual (dedup 3-B item 4) — THE load-or-seed/stamp prologue for the /mrcfg config blob. Every write path
// that can run on a node with no (or a version-rejected) blob opens with it, so the version stamp can no longer
// be forgotten at a new site. Exported because fw_main's node_id/ctr lease writer needs it too (the one write
// path outside this TU). ⚠ Pass a VALUE-INITIALIZED blob (`mrnv::Blob b{}; nv_load_stamped(b);`) — a failed
// load() may still have overwritten `b` with the rejected record's bytes, and the fields seed_blob_from_live
// does not set keep whatever is there; the `{}` is what makes that residue deterministic.
void nv_load_stamped(mrnv::Blob& b);

// ★★★ §UI-14 / [[B193]] — THE DEVICE BINDINGS OF §UI-13's TWO SEAMS, and they live in THIS cluster rather than in the
// OLED feature layer for two measured reasons, both of which are the whole content of B193:
//   1. the store's `load()` must be the §nv-ritual `nv_load_stamped` DIRECTLY above — load-or-seed + version stamp —
//      so an unprovisioned (or version-rejected) chip opens the editor on the LIVE config instead of being refused;
//   2. `apply_live()` must reproduce `handle_cfg_set`'s OFF->ON `mobile_register_current()` bridge (see its
//      `mobile_autoregister` arm in firmware_config.cpp). Setting the flag alone leaves a mobile that never starts a
//      home attachment, and the two implementations of that rule must sit in ONE file so they cannot drift.
// ⓘ They are ACCESSORS returning references to function-local statics, not exported objects: that gives a defined
//   initialisation order against the OLED layer's `ConfigService`, which is constructed over them at static-init time.
// ⚠ THE OWNER OF THE SERVICE IS THE CALLER. This cluster binds the seams; it holds no draft and no baseline, and
//   `handle_cfg_set` keeps its own immediate-write path exactly as §3.6.1 requires for companion compatibility.
ICfgStore& device_cfg_store();
ICfgLive&  device_cfg_live();

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
