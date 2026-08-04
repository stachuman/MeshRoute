// phA5 PROBE SHIM — a host stand-in for Arduino.h so the REAL variants/heltec_v3/board_ui.cpp can be compiled and
// its bus/GPIO behaviour COUNTED. Not part of the firmware build.
#pragma once
#include <cstdint>
#include <cstddef>

static constexpr uint8_t LOW = 0, HIGH = 1;
static constexpr uint8_t INPUT = 0x01, OUTPUT = 0x03, INPUT_PULLUP = 0x05;

struct ProbeGpio {
    int  pinmode_calls = 0, write_calls = 0, read_calls = 0;
    int  mode[64];        // per-pin last pinMode
    int  level[64];       // per-pin last digitalWrite
    int  read_returns = HIGH;   // what digitalRead() answers
    ProbeGpio() { for (int i = 0; i < 64; ++i) { mode[i] = -1; level[i] = -1; } }
};
extern ProbeGpio g_gpio;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t level);
int  digitalRead(uint8_t pin);
void analogReadResolution(uint8_t bits);
int  analogRead(uint8_t pin);
