// phA5 PROBE SHIM — a host stand-in for Arduino's TwoWire, added by §UI-6 for the §B91 panel-ACK probe.
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// board_ui.cpp asks the panel to acknowledge its address; this shim lets the probe drive BOTH answers, which is the
// whole point: a presence test that can only ever say "present" is not a test.
// ⚠ U8g2 owns Wire.begin() in the real build (U8x8lib.cpp:1348) and the U8g2 shim does not model it, so nothing here
//   depends on begin() having been called — exactly as on hardware, where the probe runs after s_u8g2.begin().
#pragma once
#include <cstdint>

struct ProbeWire {
    int     begin_tx = 0, end_tx = 0;
    uint8_t last_addr = 0xFF;
    uint8_t end_returns = 0;   // 0 = the device ACKed (Arduino's success code); anything else = no answer
};
extern ProbeWire g_wire;

class ProbeTwoWire {
public:
    void    beginTransmission(uint8_t addr) { g_wire.begin_tx++; g_wire.last_addr = addr; }
    uint8_t endTransmission()               { g_wire.end_tx++; return g_wire.end_returns; }
};
extern ProbeTwoWire Wire;
