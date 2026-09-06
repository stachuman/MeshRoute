// MeshRoute — tools/probe_features/probe_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §remote-admin v2 SLICE 1 FEATURE-MATRIX DRIVER — the REAL `lib/core/mr_features.h`, host-compiled and host-RUN
// under one build configuration per invocation.
//
// WHY THIS EXISTS. Slice 1 adds `MR_FEAT_RADMIN_CLIENT` / `MR_FEAT_RADMIN_ACCEPT` and NO consumer, so no existing
// gate can see either flag: native compiles one configuration, the simulator compiles two, and both are HOST
// builds — none of them is a board, and a board is where the whole exclusivity rule lives. This TU is the only
// executed reader of the pair, and the runner is what puts every real configuration through it.
//
// ⛔ IT CONTAINS NO COPY OF THE DERIVATION. Every expected value arrives as an `EXP_*` macro from the runner's
//    ruled matrix (R-RA-26); every measured value is read from the production header this TU includes. If this
//    file computed either side from the other, the probe would be a model of itself.
//
// ★ SOURCE-INTEGRITY PIN (S1). "Compile a copy, inspect the live tree" is the classic worthless probe. The runner
//   compiles in an FNV-1a-64 of the header it INTENDED to compile; this TU recomputes that hash over the file it
//   is handed at argv[1] and fails if they differ. That is what makes "the mutant was compiled" a measurement.
//
// USAGE (the runner's, never by hand):
//   g++ -DPROBE_CELL='"board_static_oled0"' -DPROBE_HDR_HASH=0x...ULL -DEXP_TEAM=1 ... probe_main.cpp
//   ./probe <path-to-the-header-that-was-compiled>

#include "mr_features.h"      // THE FILE UNDER TEST (a control's mutant shadows it via -I)

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#ifndef PROBE_CELL
#  error "probe_features: the runner must name the configuration cell with -DPROBE_CELL"
#endif
#ifndef PROBE_HDR_HASH
#  error "probe_features: the runner must pin the compiled header with -DPROBE_HDR_HASH"
#endif
// The ruled expectations. Absent ones are a runner defect, never a silently-skipped assertion (C2).
#if !defined(EXP_TEAM) || !defined(EXP_MOBILE) || !defined(EXP_MOBILE_HOST) || !defined(EXP_GATEWAY) \
 || !defined(EXP_OLED) || !defined(EXP_REMOTE_MGMT) || !defined(EXP_RADMIN_CLIENT) || !defined(EXP_RADMIN_ACCEPT)
#  error "probe_features: the runner must supply all eight EXP_* expectations"
#endif

namespace {

int g_checks = 0;
int g_failed = 0;

void check(const char* name, long measured, long expected)
{
    ++g_checks;
    if (measured == expected) {
        std::printf("  ok   %s.%s = %ld\n", PROBE_CELL, name, measured);
    } else {
        ++g_failed;
        std::printf("  FAIL %s.%s = %ld, expected %ld\n", PROBE_CELL, name, measured, expected);
    }
}

// FNV-1a-64 over the file's bytes. The runner computes the same hash in Python; the two implementations must
// agree, which is precisely what forbids "hash something else and call it the header".
bool fnv1a64(const char* path, uint64_t& out)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    uint64_t h = 14695981039346656037ULL;
    int c;
    while ((c = std::fgetc(f)) != EOF) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    std::fclose(f);
    out = h;
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("  FAIL %s.usage = expected exactly one argument (the header path)\n", PROBE_CELL);
        return 1;
    }

    // S1 — the compiled header IS the inspected header.
    uint64_t measured_hash = 0;
    ++g_checks;
    if (!fnv1a64(argv[1], measured_hash)) {
        ++g_failed;
        std::printf("  FAIL %s.hdr_integrity = could not read %s\n", PROBE_CELL, argv[1]);
    } else if (measured_hash != (PROBE_HDR_HASH)) {
        ++g_failed;
        std::printf("  FAIL %s.hdr_integrity = 0x%016llx, compiled 0x%016llx (a DIFFERENT header was compiled)\n",
                    PROBE_CELL, static_cast<unsigned long long>(measured_hash),
                    static_cast<unsigned long long>(PROBE_HDR_HASH));
    } else {
        std::printf("  ok   %s.hdr_integrity = 0x%016llx\n",
                    PROBE_CELL, static_cast<unsigned long long>(measured_hash));
    }

    // The six pre-existing capabilities...
    check("MR_FEAT_TEAM",          MR_FEAT_TEAM,          EXP_TEAM);
    check("MR_FEAT_MOBILE",        MR_FEAT_MOBILE,        EXP_MOBILE);
    check("MR_FEAT_MOBILE_HOST",   MR_FEAT_MOBILE_HOST,   EXP_MOBILE_HOST);
    check("MR_FEAT_GATEWAY",       MR_FEAT_GATEWAY,       EXP_GATEWAY);
    check("MR_FEAT_OLED",          MR_FEAT_OLED,          EXP_OLED);
    check("MR_FEAT_REMOTE_MGMT",   MR_FEAT_REMOTE_MGMT,   EXP_REMOTE_MGMT);
#ifndef PROBE_DROP_CHECK
    // ... and the two this slice adds. ⓘ PROBE_DROP_CHECK is the runner's OWN sabotage switch (MR_PROBE_DROP=check):
    //     dropping an assertion must move the check pin and FAIL the gate, which the wrapper executes.
    check("MR_FEAT_RADMIN_CLIENT", MR_FEAT_RADMIN_CLIENT, EXP_RADMIN_CLIENT);
#endif
    check("MR_FEAT_RADMIN_ACCEPT", MR_FEAT_RADMIN_ACCEPT, EXP_RADMIN_ACCEPT);

    std::printf("   derived: %s TEAM=%d MOBILE=%d MOBILE_HOST=%d GATEWAY=%d OLED=%d REMOTE_MGMT=%d "
                "RADMIN_CLIENT=%d RADMIN_ACCEPT=%d\n",
                PROBE_CELL, MR_FEAT_TEAM, MR_FEAT_MOBILE, MR_FEAT_MOBILE_HOST, MR_FEAT_GATEWAY,
                MR_FEAT_OLED, MR_FEAT_REMOTE_MGMT, MR_FEAT_RADMIN_CLIENT, MR_FEAT_RADMIN_ACCEPT);
    std::printf("   cell %s: %d checks, %d failed\n", PROBE_CELL, g_checks, g_failed);
    return g_failed ? 1 : 0;
}
