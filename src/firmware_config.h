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
#include "node.h"        // §UI-16 N6: meshroute::Node::TeamKeyGrantTx / meshroute::Plane — the ONE type below that
                         // cannot be forward-declared (see `device_team_grant`); Arduino-free lib/core, guarded
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

// ★★★ §UI-15 slice 5 — THE THREE DEVICE PRIMITIVES §3.6.3's OLED TEAM-CREATE NEEDS FROM THIS CLUSTER, AND NOTHING
// MORE. ⛔ NO DECISION IS EXPORTED: the adapter is `src/firmware_ui_prov.h`, which is PURE and natively compiled;
// these are the three facts the OLED layer cannot reach on its own, and each is a forward.
// ⓘ WHY THE ADAPTER IS NOT IMPLEMENTED HERE, stated because it is a boundary and not a preference: this file must not
//   learn that a panel exists (`tools/probe_board_ui`'s W13 is that rule, and the §notify-every-save hook is
//   deliberately feature-NEUTRAL). It exports facts; the OLED layer composes them.
// ⚠ `ProvSnapshot` / `ProvPhyFloor` / `ProvisioningService` are FORWARD-DECLARED rather than included: this header is
//   pulled in by `fw_main.cpp` and by both UI probes, and `firmware_provisioning_service.h` drags monocypher +
//   identity + node_role behind it. Every caller of these three already includes it.
struct ProvSnapshot;
struct ProvPhyFloor;
class  ProvisioningService;
#if MR_N_LAYERS < 2
// The ONE transaction instance (function-local static, like the two bindings above, so its construction order against
// the OLED layer's adapter is defined). `handle_team` and the OLED path share it — ⛔ never two services over one store.
ProvisioningService& prov_service();
// The LIVE facts the transaction reads plus the BUILD floor a `0` in the persisted record resolves to.
// ⚠⚠ IT IS A SECOND SITE, AND THE DUPLICATION IS DECLARED RATHER THAN ACCIDENTAL (U1): `handle_team` fills the same
//    fields inline, and `tools/probe_prov_tx`'s S11 pins those six assignments INSIDE that function's body — so the
//    console's copy cannot be routed through here without changing that probe, which is not this slice's to change.
//    ⇒ ★ THE TWO ARE EDITED TOGETHER; a field added to `ProvSnapshot` belongs in BOTH, and S11 is what catches the
//    console half going stale.
void prov_device_facts(ProvSnapshot& snap, ProvPhyFloor& floor);
// Sync fw_main's team-DAD persist tracker to what the transaction actually WROTE (`ProvResult::persisted_team_local_id`
// — the service reports it for exactly this purpose). ⛔ Without it a newly assigned `team_local_id` can never reach NV;
// `handle_team` does the same assignment inline, and `src/firmware_ui.cpp` cannot: `fw_context.h` is barred from that
// TU (`tools/probe_firmware_ui`'s C0).
void prov_note_persisted_team_local_id(uint8_t v);

// ★★★ §UI-15 slice 6 — THE TWO SERVICE INSTANCES §3.6.3's OLED STATIC JOIN NEEDS, AND NOTHING MORE. Both already
// existed here for the console verbs and were file-static; exporting them is what makes the OLED path use the SAME
// transaction and the SAME store service the `join` / `joinprofile` verbs use. ⛔ NEVER two services over one record
// — that is the whole argument `prov_service()` above was exported on, one feature over.
// ⚠ FORWARD-DECLARED for the reason the three above are: this header is pulled in by `fw_main.cpp` and both UI
// probes, and `firmware_join_profiles.h` drags `device_nv.h` + `firmware_config_parse.h` behind it. Every caller of
// these two already includes it.
class JoinService;
class JoinProfileService;
// Slice 1's ONE typed static-join transaction (validate -> load -> ONE save -> the live retune + re-DAD).
JoinService& join_service();
// Slice 2's ONE `/mrjoin` preset store service (the absent/invalid/io_failed matrix and the write-coalescing rule).
JoinProfileService& join_profile_service();

// ★★★ §UI-16 K1/K2 ([[B240]]) — THE `/mrteams` KEYRING: ONE service instance, and ONE boot forward.
// ⚠ FORWARD-DECLARED for the reason every declaration above is: this header is pulled in by `fw_main.cpp` and both
//   UI probes, and `firmware_team_keyring.h` drags `device_nv.h` + monocypher behind it. ⓘ An `enum class` with a
//   FIXED underlying type may be declared opaquely, which is what keeps the boot forward's return type honest here
//   without the include — ⛔ the alternative (returning a bare `uint8_t`) would launder five distinct outcomes
//   through an untyped byte at exactly the seam that reports whether a team key survived a reboot.
class TeamKeyringService;
enum class KeyringRestore : uint8_t;
// The ONE keyring service instance. `prov_service()` holds a reference to it (create/import store the key here
// BEFORE the `/mrcfg` candidate that marks it active is written), and §UI-16 K3's grant-receive persistence will
// reach for the SAME one — ⛔ never a second service over one record.
TeamKeyringService& team_keyring_service();
// ★★★ THE BOOT RESTORE — THE **ONE AUTHORITY** over the live team content key, called ONCE from `fw_main`'s startup
// right after `/mrcfg` is loaded. ⛔ `fw_main` no longer installs `/mrcfg`'s v22 copy itself: that key is passed in
// here as the COMMITTED WITNESS, so this call's verdict governs instead of following (QG blocker 1, 2026-08-22).
// It takes the whole record because the exact match is FIVE terms — active · membership == binding · a record for
// that team · the record's pub == the committed pub · the pub derives from the stored priv. ⛔ Keying it on the
// PUBLIC membership id alone would let anyone who heard a team beacon reactivate a retained key (P-2b); keying it on
// the binding alone let a stale binding install another team's key and let a FAILED re-key become effective after a
// reboot (QG blockers 2 and 3).
// ★ ZERO writes on every path, ZERO reads when there is no binding — and every non-installing path leaves the node
// KEYLESS, actively.
KeyringRestore team_keyring_restore_boot(const mrnv::Blob& nv);

// ★★★ §UI-16 N6 — THE OLED GRANT'S ONE DEVICE FORWARD (the body, and the full argument for where it lives, are at
// its definition in `firmware_config.cpp`, beside `handle_team`'s own `grantkey` arm). It is `team_key_grant_send`
// minus the two arguments the panel FIXES: no team `name=` (F-3), and the PLANE supplied by the pure unit that owns
// that decision (`mrui::kInviteGrantPlane`) rather than chosen down here.
// ⚠ IT IS THE ONE DECLARATION IN THIS HEADER THAT COULD NOT BE FORWARD-DECLARED: `TeamKeyGrantTx` is nested inside
//   `meshroute::Node`, so the opaque-enum trick used for `KeyringRestore` above does not reach it — and laundering
//   ELEVEN distinct outcomes through a bare `uint8_t` at the seam that ships a PRIVATE KEY is exactly the trade that
//   note refuses. ⇒ `node.h` is included at the top of this header; every one of its five includers already compiles
//   it (it is `lib/core`, Arduino-free, and guarded).
// ⓘ §UI-16 N6b: `out_dst` is the core's SEND-TIME resolved destination — the panel's `send_aired` correlation term.
meshroute::Node::TeamKeyGrantTx device_team_grant(uint32_t key_hash32, meshroute::Plane plane, uint16_t* out_ctr, uint8_t* out_dst);
#endif

// `cfg set <key> <value>` — accumulate onto the pending NV blob + apply live where possible (dispatch verb).
void handle_cfg_set(const char* args, Print& out);

// `gateway l0=…:l1=… [win0=…] [beacon=…] [gateway_only]` — one-command dual-layer gateway provisioning (dispatch verb).
void handle_gateway(const char* args, Print& out);

// Normal-node provisioning verbs (dispatch; compiled out on the gateway build).
#if MR_N_LAYERS < 2
void handle_join(const char* args, Print& out);       // set the radio floor + (re-)DAD; auto-pull the leaf config
void handle_joinprofile(const char* args, Print& out);// §UI-15 slice 2: the /mrjoin preset store — list/set/clear/reset confirm (⛔ storage only, NO UI)
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
