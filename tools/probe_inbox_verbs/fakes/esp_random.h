// MeshRoute — tools/probe_inbox_verbs/fakes/esp_random.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D host-probe stand-in for an ESP32/arduino-esp32 platform header, so the REAL
// `src/firmware_commands.cpp` + `src/firmware_inbox.cpp` compile on the host under the `[env:heltec_v3]` `-D` arm.
// ⓘ Kept in THIS probe's own dir rather than added to the shared `probe_board_ui`/`probe_device_radio` fakes:
//   they are this arm's platform, not a shared Arduino surface (U1 — reuse what is shared, do not widen it).
//
// ★★ §RADMIN-0b / [[B279]]: the constant `0x5A5A5A5A` became a DETERMINISTIC, REPLAYABLE STREAM with a draw
//    counter. It is still only a FACT PROVIDER — the stream is the entropy source's contribution and nothing else;
//    the seed is still assembled by the real `mrrng::fill` (src/device_rng.h:74-79), the identity is still derived
//    by the real `identity_from_seed`, and the record is still written by the real `mrnv::save_id`.
//    ⓘ WHY A STREAM AND NOT THE CONSTANT: a constant cannot tell "production drew 32 fresh bytes in order" from
//      "production re-used the bytes it loaded" — every draw would look alike. The probe replays this same stream
//      through `mrrng::fill` to compute the seed production MUST mint, so a `regen` that skipped the draw, drew
//      the wrong count, or wrote the loaded seed back is a byte mismatch rather than a coincidence.
// ⛔ NOTHING HERE DECIDES ANYTHING: no formatting, no routing, no identity derivation, no production branch.
#pragma once
#include <cstdint>

struct MrProbeRng {
    uint32_t state = 0;    // reset to 0 before each case -> the same stream every run, on every host
    uint32_t draws = 0;    // how many 32-bit draws production consumed
};
inline MrProbeRng& mrprobe_rng() { static MrProbeRng r; return r; }

inline uint32_t esp_random(void) {
    MrProbeRng& r = mrprobe_rng();
    ++r.draws;
    r.state = r.state * 1664525u + 1013904223u;   // a plain LCG: reproducible, and NOT a constant per draw
    return r.state;
}
