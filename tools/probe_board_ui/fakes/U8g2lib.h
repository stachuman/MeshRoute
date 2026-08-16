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
    // ★ §CHROME-2. The FOUR bitmap/outline entry points are shimmed, not just the two the board is supposed to use:
    //   `drawXBMP` and `drawBox` exist here PRECISELY SO THE WRONG-CALL CONTROLS COMPILE. A mutation that fails to
    //   build is a weaker control than one that builds and is caught — negctl.py prints "COMPILE FAILS" and moves on,
    //   which would leave "the board picked the wrong U8g2 call" unmeasured by the probe itself.
    int drawXBM = 0, drawXBMP = 0, drawFrame = 0, drawBox = 0;
    // Every argument is recorded, in order, because a transposition (x<->y, w<->h) is invisible to a call COUNT.
    int xbm_x = -1, xbm_y = -1, xbm_w = -1, xbm_h = -1;
    const uint8_t* xbm_bits = nullptr;      // the POINTER, so "forwarded unchanged, never copied" is assertable
    int rect_x = -1, rect_y = -1, rect_w = -1, rect_h = -1;
    int box_x = -1, box_y = -1, box_w = -1, box_h = -1;
    // ⚠ COMPOSE-ONLY CALLS ARE DELIBERATELY ABSENT FROM THIS SUM, which is what makes "no bus traffic outside
    //   next_page()" a MEASUREMENT rather than a claim: drawStr/drawHLine/drawXBM/drawFrame write the page buffer,
    //   so a primitive that reached the panel would have to do it through one of the three counted below.
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
    void     drawXBM(int x, int y, int w, int h, const uint8_t* b) {
                 g_u8.drawXBM++;  g_u8.xbm_x = x; g_u8.xbm_y = y; g_u8.xbm_w = w; g_u8.xbm_h = h; g_u8.xbm_bits = b; }
    void     drawXBMP(int x, int y, int w, int h, const uint8_t* b) {
                 g_u8.drawXBMP++; g_u8.xbm_x = x; g_u8.xbm_y = y; g_u8.xbm_w = w; g_u8.xbm_h = h; g_u8.xbm_bits = b; }
    void     drawFrame(int x, int y, int w, int h)     {
                 g_u8.drawFrame++; g_u8.rect_x = x; g_u8.rect_y = y; g_u8.rect_w = w; g_u8.rect_h = h; }
    void     drawBox(int x, int y, int w, int h)       {
                 g_u8.drawBox++;   g_u8.box_x = x;  g_u8.box_y = y;  g_u8.box_w = w;  g_u8.box_h = h; }
    void     setPowerSave(uint8_t v)                   { g_u8.setPowerSave++; g_u8.last_power_save_arg = v; }
};
