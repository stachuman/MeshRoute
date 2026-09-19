// MeshRoute — lib/core/mr_features.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Compile-time feature split. An env MAY set ONE MR_PROFILE_* in build_flags; this header derives the MR_FEAT_* set,
// resolves dependencies, and #errors on an illegal combo. Feature STATE is #if MR_FEAT_X-declared; each feature's API
// stubs to an inert value when off (call sites unchanged).
// ⚠ CORRECTED 2026-09-05 (remote-admin v2 slice 1). Two claims this comment used to make are WITHDRAWN, because the
//   env matrix and this header both already contradicted them:
//   • WAS "An env sets ONE MR_PROFILE_*". FIVE board envs set NONE — production, xiao_sx1262, heltec_v3, heltec_v4,
//     xiao_esp32s3. They are STATIC PRODUCTS, not "profile missing"; the endpoint derivation below treats them so.
//   • WAS "No profile set => every MR_FEAT_* defaults to 1 (a full/dev build, incl. native + the lus sim)". Two
//     counter-examples live below: MR_FEAT_OLED defaults to 0, and the MR_FEAT_RADMIN_* pair is DERIVED rather than
//     defaulted — a no-profile BOARD gets {CLIENT 0, ACCEPT 1}; only a HOST (native + lus) gets {1, 1}.
// ⚠ CORRECTED 2026-09-05 ([[B304]]). WAS "See docs/superpowers/specs/2026-07-12-firmware-feature-split.md" — that path
//   does not exist; the spec was ARCHIVED, not lost. HISTORICAL origin (history, NOT current authority):
//   docs/superpowers/specs/archive/2026-07-12-firmware-feature-split.md. Current endpoint authority = R-RA-26 in
//   docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md + the design it rules on,
//   docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md §19.
#pragma once

// ---- profiles (each defines the slim feature set; later slices flip more OFF as their boundaries land) ----
#if defined(MR_PROFILE_GATEWAY)          // pure static relay + cross-layer bridge
#  define MR_FEAT_TEAM 0                  // slice 1: the team plane is compiled out (frees ~45 KB of _rt_team ×2 layers)
#  define MR_FEAT_MOBILE 0                // slice 2: the mobile-MEMBER (roaming endpoint) plane is compiled out (a gateway never registers to a host)
   // (MR_FEAT_MOBILE_HOST flips to 0 in slice 3, once its boundary exists)
#endif

// ---- remote-admin v2 endpoint capabilities: DERIVED, never defaulted (R-RA-8 / R-RA-17 / R-RA-26) ----
// Exactly two endpoint roles exist and a BOARD is exactly one of them: MANAGING (it issues remote administration =
// CLIENT) or MANAGED (it accepts it = ACCEPT). Transit is ordinary type-agnostic DATA forwarding and needs neither.
//   MR_PROFILE_MOBILE                -> {CLIENT 1, ACCEPT 0}  a roaming personal endpoint is administered LOCALLY and
//                                                             is the most physically-capturable node (R-RA-17)
//   MR_PROFILE_GATEWAY               -> {CLIENT 0, ACCEPT 1}
//   BOARD, no MR_PROFILE_MOBILE      -> {CLIENT 0, ACCEPT 1}  the five no-profile envs are STATIC PRODUCTS
//   HOST (no ARDUINO: native + lus)  -> {CLIENT 1, ACCEPT 1}  ONE test process drives a controller and a target end to
//                                                             end; a host build is not a product configuration
// ★ `defined(ARDUINO)` is the board/host discriminator (R-RA-26, owner-ruled 2026-09-05) — the idiom src/device_rng.h
//   and src/device_ble.h already use, and it costs no platformio.ini change. Nothing else separates all four cases:
//   the absence of MR_PROFILE_* covers both static boards AND hosts; MESHROUTE_NATIVE is native-only; MR_N_LAYERS and
//   MR_GATEWAY_BUILD are shared with the SIMULATOR's gateway core variant, which has no ARDUINO and must keep BOTH
//   endpoints. ⛔ There is deliberately NO MR_PROFILE_STATIC and no -D override surface: an invalid pair is a build
//   failure below, not something an env can dial in.
// ⓘ CORRECTED 2026-09-06 (remote-admin v2 slice 1b), old claim visible: this line read *"SCAFFOLD ONLY (slice 1):
//   this header is the sole production location that names the pair. No consumer exists yet."* — WITHDRAWN. Slice 1b
//   is the FIRST CONSUMER: `lib/core/node_mac_rx.cpp` compiles the two capability-owned remote receive entry
//   points and their one call site under these macros, and `lib/core/node.h` guards their declarations. The
//   derivation, the values and the three board-only refusals below are UNCHANGED — only the no-consumer claim is.
//   ⓘ The exact consumer census is enforced by tools/probe_features/ownership.py, not by this comment.
#if defined(MR_PROFILE_MOBILE)
#  define MR_FEAT_RADMIN_CLIENT 1
#  define MR_FEAT_RADMIN_ACCEPT 0
#elif defined(ARDUINO)
#  define MR_FEAT_RADMIN_CLIENT 0
#  define MR_FEAT_RADMIN_ACCEPT 1
#else
#  define MR_FEAT_RADMIN_CLIENT 1
#  define MR_FEAT_RADMIN_ACCEPT 1
#endif

// ---- defaults: any unset feature is ON (a bare/native/production build is full) ----
#ifndef MR_FEAT_TEAM
#  define MR_FEAT_TEAM 1
#endif
#ifndef MR_FEAT_MOBILE
#  define MR_FEAT_MOBILE 1
#endif
#ifndef MR_FEAT_MOBILE_HOST
#  define MR_FEAT_MOBILE_HOST 1
#endif
#ifndef MR_FEAT_GATEWAY
#  define MR_FEAT_GATEWAY 1
#endif
#ifndef MR_FEAT_OLED
#  define MR_FEAT_OLED 0                  // board UI: OFF by default (opt-in per board); scaffold lands in slice 4
#endif

// ---- dependency + sanity checks ----
#if MR_FEAT_TEAM && !MR_FEAT_MOBILE
#  error "MR_FEAT_TEAM requires MR_FEAT_MOBILE (a team member is is_mobile; the team plane reuses the mobile link-layer)"
#endif

// The two BOARD-ONLY endpoint rules. ⚠ Board-only is load-bearing: a HOST is deliberately {1,1} (R-RA-17), so
// fencing these on defined(ARDUINO) is the rule, not an omission. Each check is separate so each fails on its own.
#if defined(ARDUINO)
#  if MR_FEAT_RADMIN_CLIENT && MR_FEAT_RADMIN_ACCEPT
#    error "board build is BOTH remote-admin endpoints: MR_FEAT_RADMIN_CLIENT and MR_FEAT_RADMIN_ACCEPT are both 1 (R-RA-17: a board is either managed or managing)"
#  endif
#  if !MR_FEAT_RADMIN_CLIENT && !MR_FEAT_RADMIN_ACCEPT
#    error "board build is NEITHER remote-admin endpoint: MR_FEAT_RADMIN_CLIENT and MR_FEAT_RADMIN_ACCEPT are both 0 (R-RA-17: a board is either managed or managing)"
#  endif
#endif
