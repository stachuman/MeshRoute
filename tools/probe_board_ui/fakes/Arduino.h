// phA5 PROBE SHIM — a host stand-in for Arduino.h so the REAL variants/heltec_v3/board_ui.cpp can be compiled and
// its bus/GPIO behaviour COUNTED. Not part of the firmware build.
//
// ★ SHARED BY TWO PROBES (§B105, 2026-08-06). `tools/probe_firmware_ui/` compiles `src/firmware_ui.cpp` against this
//   same file rather than forking a second Arduino shim (U1). Everything the second probe needs — `millis`, `Print`,
//   `F()`, `Serial` — is added below as HEADER-ONLY (`inline`), so `probe_board_ui`'s probe_main.cpp defines nothing
//   new and its results are unchanged.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

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

// ==================================================================================================================
// §B105 ADDITIONS — needed only by `tools/probe_firmware_ui/`. All `inline`, so no TU has to define them and the
// existing board probe links exactly as before.
// ==================================================================================================================

// ---- the clock. `lib/hal/iclock.h`'s ArduinoClock calls ::millis(); the probe DRIVES it, so a cadence (the 30 s
//      battery period, the 2 Hz throttle) can be stepped deterministically instead of waited out.
inline uint32_t g_probe_millis = 0;
inline uint32_t millis() { return g_probe_millis; }
inline void     delay(uint32_t ms) { g_probe_millis += ms; }

// ---- F(): on a real AVR/ESP this hands back a flash-string handle. Nothing under test dereferences it as anything
//      but text, so the shim keeps it a plain `const char*` — the honest minimum, not a pretend PROGMEM.
#define F(x) (x)

// ---- Print: the base every MeshRoute sink derives from (`mrcon_detail::GuardedConsole : public Print`). Only the
//      three virtuals console_sink.h overrides need to BE virtual; the rest are the convenience layer callers use.
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) { size_t i = 0; for (; i < n; ++i) if (!write(buf[i])) break; return i; }
    virtual void   flush() {}
    size_t write(const char* s)             { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
    size_t write(const char* s, size_t n)   { return write(reinterpret_cast<const uint8_t*>(s), n); }
    size_t print(const char* s)             { return write(s); }
    size_t println(const char* s)           { return write(s) + write("\r\n"); }
    size_t println()                        { return write("\r\n"); }
};

// ---- Serial: what `console_sink.h`'s MR_CONSOLE=1 stage hands its bytes to. The probe mirrors the board env
//      (`-DMR_CONSOLE=1`) rather than compiling the NullPrint arm, so a console line the firmware emits is a line the
//      probe can actually ASSERT — a shim that swallowed output would make §B91's dead-panel report unmeasurable.
struct ProbeSerial {
    bool  present = true;      // `if (!Serial)` — a host attached or not
    int   avail   = 4096;      // what availableForWrite() answers (0 = a full FIFO)
    char  out[4096] = {};      // everything that reached "the wire", concatenated
    size_t n_out  = 0;
    explicit operator bool() const { return present; }
    int    availableForWrite() const { return avail; }
    size_t write(const uint8_t* b, size_t n) {
        for (size_t i = 0; i < n && n_out + 1 < sizeof out; ++i) out[n_out++] = char(b[i]);
        out[n_out] = '\0';
        return n;
    }
    void flush() {}
    void reset() { n_out = 0; out[0] = '\0'; }
};
inline ProbeSerial Serial;
