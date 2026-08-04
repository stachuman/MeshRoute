// phA5 PROBE SHIM — a host stand-in for U8g2 2.35.30's U8G2_SSD1306_128X64_NONAME_1_HW_I2C. Its nextPage()
// reproduces the REAL page semantics verified in U8g2 2.35.30 (u8g2_buffer.c:116 u8g2_NextPage): each call PUSHES the
// current 128 B page and the 8th (last) call returns 0. Every entry point counts, so a probe can assert bus traffic.
#pragma once
#include <cstdint>

struct u8g2_cb_t { int dummy; };
extern const u8g2_cb_t u8g2_cb_r0;
#define U8G2_R0 (&u8g2_cb_r0)

extern const uint8_t u8g2_font_6x10_tf[];
extern const uint8_t u8g2_font_10x20_tf[];

struct ProbeU8g2 {
    int begin = 0, firstPage = 0, nextPage = 0, setFont = 0, drawStr = 0, drawHLine = 0, setPowerSave = 0;
    int last_power_save_arg = -1;
    const uint8_t* last_font = nullptr;
    int pages_left = 0;
    int bus_ops() const { return begin + nextPage + setPowerSave; }   // the calls that actually reach the panel
};
extern ProbeU8g2 g_u8;

class U8G2_SSD1306_128X64_NONAME_1_HW_I2C {
public:
    U8G2_SSD1306_128X64_NONAME_1_HW_I2C(const u8g2_cb_t*, uint8_t reset, uint8_t clock, uint8_t data)
        : rst(reset), scl(clock), sda(data) {}
    uint8_t rst, scl, sda;
    bool     begin()      { g_u8.begin++; return true; }
    void     firstPage()  { g_u8.firstPage++; g_u8.pages_left = 8; }   // composes only — touches NO bus
    uint8_t  nextPage()   { g_u8.nextPage++; if (g_u8.pages_left > 0) --g_u8.pages_left;
                            return g_u8.pages_left > 0 ? 1 : 0; }
    void     setFont(const uint8_t* f)                 { g_u8.setFont++; g_u8.last_font = f; }
    uint16_t drawStr(int, int, const char*)            { g_u8.drawStr++; return 0; }
    void     drawHLine(int, int, int)                  { g_u8.drawHLine++; }
    void     setPowerSave(uint8_t v)                   { g_u8.setPowerSave++; g_u8.last_power_save_arg = v; }
};
