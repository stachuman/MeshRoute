// MeshRoute — tools/probe_inbox_verbs/fakes/Preferences.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D host-probe stand-in for an ESP32/arduino-esp32 platform header, so the REAL
// `src/firmware_commands.cpp` + `src/firmware_inbox.cpp` compile on the host under the `[env:heltec_v3]`
// `-D` arm. ⛔ NOTHING THE PROBE ASSERTS GOES THROUGH THIS FILE — the inbox verbs never touch NV, the FS or
// the RNG. Each entry point therefore answers the LEAST capable honest value (absent / would-not-mount), so a
// probe check that accidentally started depending on one would FAIL rather than read a pretend success.
// ⓘ Kept in THIS probe's own dir rather than added to the shared `probe_board_ui`/`probe_device_radio` fakes:
//   they are this arm's platform, not a shared Arduino surface (U1 — reuse what is shared, do not widen it).
#pragma once
#include <cstddef>
#include <cstdint>
class Preferences {
public:
    bool begin(const char*, bool = false) { return false; }
    void end() {}
    bool isKey(const char*) { return false; }
    size_t getBytesLength(const char*) { return 0; }
    size_t getBytes(const char*, void*, size_t) { return 0; }
    size_t putBytes(const char*, const void*, size_t n) { return n; }
    bool remove(const char*) { return true; }
    bool clear() { return true; }
};
