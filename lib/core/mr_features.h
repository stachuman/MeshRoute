// MeshRoute — lib/core/mr_features.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Compile-time feature split. An env sets ONE MR_PROFILE_* in build_flags; this header derives the MR_FEAT_* set,
// resolves dependencies, and #errors on an illegal combo. No profile set => every MR_FEAT_* defaults to 1 (a full/dev
// build, incl. native + the lus sim). Feature STATE is #if MR_FEAT_X-declared; each feature's API stubs to an inert
// value when off (call sites unchanged). See docs/superpowers/specs/2026-07-12-firmware-feature-split.md.
#pragma once

// ---- profiles (each defines the slim feature set; later slices flip more OFF as their boundaries land) ----
#if defined(MR_PROFILE_GATEWAY)          // pure static relay + cross-layer bridge
#  define MR_FEAT_TEAM 0                  // slice 1: the team plane is compiled out (frees ~45 KB of _rt_team ×2 layers)
#  define MR_FEAT_MOBILE 0                // slice 2: the mobile-MEMBER (roaming endpoint) plane is compiled out (a gateway never registers to a host)
   // (MR_FEAT_MOBILE_HOST flips to 0 in slice 3, once its boundary exists)
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
