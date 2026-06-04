// MeshRoute — src/device_nv.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persist the device's runtime RADIO/PROTOCOL CONFIG (NOT identity — node_id/key stay compile-time
// until the Slice-3 join runtime) to on-chip flash, so a `cfg set` over the serial console survives a
// reboot. Stored as a versioned binary blob:
//   nRF52 (Adafruit core) -> Adafruit_LittleFS / InternalFS (a file "/mrcfg")
//   ESP32  (Heltec)        -> Preferences / NVS (namespace "mr", key "cfg")
//
// REALITY SPLIT: this compiles under both board envs here; the actual flash read/write + the wear is
// BENCH-VERIFIED BY THE USER (I cannot exercise on-chip flash from the host). A failed/empty load just
// falls back to the compile-time defaults, so an unprovisioned or mismatched-version chip still boots.
#pragma once
#include <stdint.h>

namespace mrnv {

struct Blob {                  // packed-ish POD; written/read verbatim. Bump kVersion on any layout change.
    uint32_t magic;            // kMagic — distinguishes a real blob from erased flash
    uint16_t version;          // kVersion — a mismatch => ignore (use defaults)
    uint16_t _pad;
    double   freq_mhz;
    uint32_t bw_hz;
    uint32_t beacon_ms;
    double   duty;
    uint16_t allowed_sf_bitmap;
    uint8_t  routing_sf;
    uint8_t  data_sf;
    uint8_t  cr;
    uint8_t  lbt;
    uint8_t  node_id;          // 0 = unprovisioned (no sends until join / `cfg set node_id`)
    int8_t   tx_power;         // dBm (repurposed _pad2); SX1262 range -9..22. `cfg set tx_power`
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 3;             // tx_power (was _pad2); load() ALSO accepts v2 (identical
                                             // layout) + defaults tx_power, so the bump preserves config

}  // namespace mrnv

#if defined(ARDUINO)
// ----- platform flash backends (header-inline; device_nv.h is included by the one device TU) ----------
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262)
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
namespace mrnv {
inline bool load(Blob& out) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open("/mrcfg", FILE_O_READ)) return false;
    const int n = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out));
    f.close();
    return n == static_cast<int>(sizeof(out)) && out.magic == kMagic && (out.version == kVersion || out.version == 2);
}
inline bool save(const Blob& b) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/mrcfg");                       // overwrite (LittleFS append-only otherwise)
    File f(InternalFS);
    if (!f.open("/mrcfg", FILE_O_WRITE)) return false;
    const int n = f.write(reinterpret_cast<const uint8_t*>(&b), sizeof(b));
    f.close();
    return n == static_cast<int>(sizeof(b));
}
}  // namespace mrnv
#elif defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3)
  #include <Preferences.h>
namespace mrnv {
inline bool load(Blob& out) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/true)) return false;
    const size_t n = p.getBytes("cfg", &out, sizeof(out));
    p.end();
    return n == sizeof(out) && out.magic == kMagic && (out.version == kVersion || out.version == 2);
}
inline bool save(const Blob& b) {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const size_t n = p.putBytes("cfg", &b, sizeof(b));
    p.end();
    return n == sizeof(b);
}
}  // namespace mrnv
#else
namespace mrnv {                                       // unknown platform -> no NV (always defaults)
inline bool load(Blob&) { return false; }
inline bool save(const Blob&) { return false; }
}  // namespace mrnv
#endif
#endif  // ARDUINO
