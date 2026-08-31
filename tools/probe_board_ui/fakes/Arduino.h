// phA5 PROBE SHIM — a host stand-in for Arduino.h so the REAL variants/heltec_common/board_ui.cpp can be compiled and
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
// ⓘ The values mirror arduino-esp32's `esp32-hal-gpio.h` bit flags; only their DISTINCTNESS is load-bearing here.
static constexpr uint8_t INPUT = 0x01, OUTPUT = 0x03, INPUT_PULLUP = 0x05, INPUT_PULLDOWN = 0x09;

struct ProbeGpio {
    int  pinmode_calls = 0, write_calls = 0, read_calls = 0;
    int  mode[64];        // per-pin last pinMode
    int  level[64];       // per-pin last digitalWrite
    int  read_returns = HIGH;   // what digitalRead() answers when the pin's mode has no scripted answer
    // ★ §UI-9 POLARITY PROBE. board_init() reads the CONTROL line twice, once under INPUT_PULLUP and once under
    //   INPUT_PULLDOWN, and treats a DISAGREEMENT as "the line is floating, refuse to guess". Modelling that needs a
    //   shim that can answer DIFFERENTLY PER PULL — with one global answer the floating case is unreachable and the
    //   whole guard would be untestable (the instrument-that-cannot-fail shape this project keeps finding).
    //   -1 = "not scripted, fall back to read_returns", which is what every pre-UI-9 case relies on.
    int  read_under_pullup   = -1;
    int  read_under_pulldown = -1;
    // Which pull the CODE asked for, per pin. Explicit booleans rather than an OR of the mode bits: the check that
    // matters is "never a BARE INPUT on the control line", and bit arithmetic over arduino-esp32's flag values would
    // make that assertion depend on those values instead of on the intent.
    bool saw_pullup[64] = {}, saw_pulldown[64] = {}, saw_bare_input[64] = {};
    // ---- §UI-9 (plan Task 9): the battery ADC ---------------------------------------------------------------------
    // `analog_returns` is the raw count analogRead() hands back; `analog_calls` / `analog_pin` count and identify the
    // burst; `analog_res_bits` records analogReadResolution's argument.
    // ★ `ctrl_pin` + the two per-read tallies are the ONLY way to prove the divider was live AROUND the conversions
    //   rather than merely toggled somewhere inside the function — an enable/disable pair placed after the burst would
    //   satisfy every "the line was driven" check while every sample read a dead divider.
    int  analog_calls = 0, analog_pin = -1, analog_res_bits = -1, analog_returns = 0;
    int  ctrl_pin = -1;               // which pin to snapshot at each conversion; -1 = snapshot nothing
    int  ctrl_low_during_read = 0;    // conversions taken while the watched pin was LOW
    int  ctrl_high_during_read = 0;   // ... and while it was HIGH
    ProbeGpio() { for (int i = 0; i < 64; ++i) { mode[i] = -1; level[i] = -1; } }
};
extern ProbeGpio g_gpio;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t level);
int  digitalRead(uint8_t pin);
void analogReadResolution(uint8_t bits);
int  analogRead(uint8_t pin);
void delayMicroseconds(uint32_t us);   // §UI-9: the polarity probe's settle between two opposite internal pulls

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
    // ★★ THE NUMERIC + CHAR OVERLOADS, ADDED 2026-08-31 BY §CUSTODY-D's `tools/probe_inbox_verbs/`. ⛔ THIS IS A
    //    FAITHFULNESS FIX, NOT A CONVENIENCE: real Arduino `Print` carries `print(int/long/unsigned/char/double,
    //    base)` and `println` twins, and WITHOUT them a TU that prints a number does not host-compile at all — the
    //    third probe compiles `src/firmware_commands.cpp`, whose `[route] dest=… next=…` dumps print integers by
    //    the hundred. A fake NARROWER than the API it stands in for is a fake that decides which production TUs may
    //    be probed, which is the wrong thing for a fake to decide.
    // ⓘ ADDITIVE ONLY — no existing signature changed — so `probe_firmware_ui` / `probe_board_ui` cannot resolve any
    //   call differently than before (measured: both re-run at their published counts after this edit).
    // ⓘ The base argument is ACCEPTED AND IGNORED for text purposes (HEX prints decimal here). Nothing this probe
    //   asserts reads a based number; a check that did would have to pin the digits itself rather than trust a shim.
    size_t print(char c)                    { return write(uint8_t(c)); }
    size_t print(unsigned char v, int = 10) { return print_num_(static_cast<long long>(v)); }
    size_t print(int v, int = 10)           { return print_num_(v); }
    size_t print(unsigned int v, int = 10)  { return print_num_(static_cast<long long>(v)); }
    size_t print(long v, int = 10)          { return print_num_(v); }
    size_t print(unsigned long v, int = 10) { return print_num_(static_cast<long long>(v)); }
    size_t print(long long v, int = 10)     { return print_num_(v); }
    size_t print(unsigned long long v, int = 10) { return print_num_(static_cast<long long>(v)); }
    size_t print(short v, int = 10)         { return print_num_(v); }
    size_t print(unsigned short v, int = 10){ return print_num_(static_cast<long long>(v)); }
    size_t print(double v, int = 2)         { return print_num_(static_cast<long long>(v)); }
    template <class T> size_t println(T v)          { return print(v) + write("\r\n"); }
    template <class T> size_t println(T v, int b)   { return print(v, b) + write("\r\n"); }
private:
    size_t print_num_(long long v) {
        char b[24]; int i = 0; bool neg = v < 0; unsigned long long u = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
        do { b[i++] = char('0' + int(u % 10)); u /= 10; } while (u && i < 20);
        if (neg) b[i++] = '-';
        size_t n = 0; while (i) n += write(uint8_t(b[--i]));
        return n;
    }
};
// The base constants callers pass as `print(x, HEX)`. Real Arduino spells them as macros/enums in the same header.
enum { DEC = 10, HEX = 16, OCT = 8, BIN = 2 };

// ---- Serial: what `console_sink.h`'s MR_CONSOLE=1 stage hands its bytes to. The probe mirrors the board env
//      (`-DMR_CONSOLE=1`) rather than compiling the NullPrint arm, so a console line the firmware emits is a line the
//      probe can actually ASSERT — a shim that swallowed output would make §B91's dead-panel report unmeasurable.
struct ProbeSerial {
    bool  present = true;      // `if (!Serial)` — a host attached or not
    // ⓘ ADDED 2026-08-31 with the `Print` overloads above and for the same reason: `lib/core/frame_trace.h`'s
    //   decoded «rx/»tx trace prints through `Serial` directly, so a TU that includes it needs these to compile.
    //   Deliberately SINKS to the same `out` buffer, so a probe can still assert what reached "the wire".
    template <class T> size_t print(T v)        { char b[24]; int n = fmt_(b, (long long)v); return write(reinterpret_cast<const uint8_t*>(b), size_t(n)); }
    template <class T> size_t print(T v, int)   { return print(v); }
    size_t print(const char* s)                 { size_t n = 0; while (s && s[n]) ++n; return write(reinterpret_cast<const uint8_t*>(s), n); }
    size_t print(char c)                        { return write(reinterpret_cast<const uint8_t*>(&c), 1); }
    size_t println()                            { return write(reinterpret_cast<const uint8_t*>("\r\n"), 2); }
    template <class T> size_t println(T v)      { return print(v) + println(); }
    template <class T> size_t println(T v, int) { return print(v) + println(); }
    static int fmt_(char* b, long long v) {
        char t[24]; int i = 0; bool neg = v < 0; unsigned long long u = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
        do { t[i++] = char('0' + int(u % 10)); u /= 10; } while (u && i < 20);
        if (neg) t[i++] = '-';
        int n = 0; while (i) b[n++] = t[--i];
        return n;
    }
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
