// MeshRoute — src/fw_context_pure.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// THE PURE HALF OF `fw_context.h` — §B105.
//
// ★ WHY IT IS ITS OWN FILE. `fw_context.h` is a DEVICE-ONLY header: it pulls `<Arduino.h>`, `<RadioLib.h>`, the
//   vendored `CustomSX1262`, the QSPI inbox backend and the fault log. A feature TU that only wants `g_node` and
//   `g_hal` paid for all of it, and the bill was two things, neither of them cosmetic:
//     • §B106 — RadioLib's `#warning` (`-Wcpp`) and `device_radio.h`'s `inline volatile` globals (`-Wvolatile`) are
//       emitted ONCE PER INCLUDING TU, so every new includer was +2 warnings against a pinned census.
//     • §B104 — the TU became impossible to HOST-COMPILE, so its render policy, MAC-idle gate, throttle and battery
//       cadence had no behavioural probe at all. That is the cost that mattered: an entire defect class in the OLED
//       arc was review-detectable instead of machine-detectable.
//   ⇒ the two globals a feature layer actually needs are declared HERE, behind `node.h` + `device_hal.h`, both of
//     which are Arduino- and RadioLib-free BY DESIGN (`device_hal.h:11` says so). `tools/probe_firmware_ui/` exists
//     because of this file.
//
// ★ THE 1:1 RULE IS UNCHANGED, AND THAT IS THE POINT (U1). These are not a second, parallel declaration of the two
//   globals — they are THE declaration. `fw_context.h` #includes this header rather than restating them, so there is
//   still exactly one `extern` per global and one definition in `fw_main.cpp`. Never re-declare either one at a call
//   site; a local `extern` that drifts from the definition is an ODR trap no build catches.
//
// ⚠ WHAT MAY GO IN HERE: a global whose TYPE is reachable through pure headers. `g_iradio` (`Sx1262Radio`) may NOT —
//   it is the concrete RadioLib-backed radio, and hoisting it here would re-import exactly what this file removes.
//   A feature layer that needs the radio seam goes through `g_hal.radio()`, which hands back that same instance as
//   the pure `IRadio&` (device_hal.h, §B105).
#pragma once
#include "device_hal.h"   // meshroute::DeviceHal — "NATIVE-TESTABLE: no RadioLib/Arduino" (device_hal.h:11)
#include "node.h"         // meshroute::Node      — lib/core, Arduino-free by construction (the simulator compiles it)

// Defined once, non-static, in fw_main.cpp (ctor args live with the definition):
//   meshroute::DeviceHal g_hal(g_clock, g_iradio);
//   meshroute::Node      g_node(g_hal, 0, 0, "node");
extern meshroute::DeviceHal g_hal;
extern meshroute::Node      g_node;
