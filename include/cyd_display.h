// cyd_display.h — per-board pin map, touch and SD bring-up for the CYD targets.
//
// Select the board with a build flag (see platformio.ini):
//   CYD_BOARD_2432S028R  2.8" ILI9341 / ST7789 320x240  (pins: waywardtech/CYD-Diagnostic)
//   CYD_BOARD_3248S035R  3.5" ST7796 480x320
//
// The two boards are wired very differently:
//   2.8"  LCD on HSPI (14/12/13), microSD on VSPI (18/19/23, CS 5),
//         XPT2046 touch on its own pins (CLK 25, MOSI 32, MISO 39, CS 33),
//         read through LovyanGFX's software-SPI driver exactly as
//         CYD-Diagnostic does (src/cyd_touch_lgfx.cpp).
//   3.5"  LCD, touch (CS 33) and microSD (CS 5) all share one bus on 14/12/13.
//
// Both paths return raw touch values aligned to the landscape screen axes;
// each board has its own TOUCH_* constants that map them to pixels.
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#if !defined(CYD_BOARD_2432S028R) && !defined(CYD_BOARD_3248S035R)
#define CYD_BOARD_2432S028R 1
#endif

namespace cyd {

#if defined(CYD_BOARD_2432S028R)
// ─── ESP32-2432S028R (2.8") ──────────────────────────────────────────────────
constexpr const char* BOARD_NAME = "ESP32-2432S028R 2.8in";
constexpr int PIN_TFT_BL     = 21;

constexpr int PIN_TOUCH_SCLK = 25;
constexpr int PIN_TOUCH_MOSI = 32;
constexpr int PIN_TOUCH_MISO = 39;
constexpr int PIN_TOUCH_CS   = 33;
constexpr int PIN_TOUCH_IRQ  = 36;

constexpr int PIN_SD_SCLK = 18;
constexpr int PIN_SD_MISO = 19;
constexpr int PIN_SD_MOSI = 23;
constexpr int PIN_SD_CS   = 5;

// Raw touch extents for the landscape screen (setRotation(1)), measured on
// the test board: screen X follows the chip's Y channel (cmd 0x91) and screen
// Y its X channel (cmd 0xD1). Clone-to-clone variance is real; build with
// -DCYD_TOUCH_DEBUG to print raw values if taps land off.
constexpr int TOUCH_MIN_X = 130;
constexpr int TOUCH_MAX_X = 3732;
constexpr int TOUCH_MIN_Y = 279;
constexpr int TOUCH_MAX_Y = 4035;
constexpr bool TOUCH_FLIP_X = false;
constexpr bool TOUCH_FLIP_Y = false;

// TFT_eSPI runs on HSPI (USE_HSPI_PORT); microSD gets VSPI to itself.
inline SPIClass& sdSpi() { static SPIClass bus(VSPI); return bus; }

// ── XPT2046 via LovyanGFX (src/cyd_touch_lgfx.cpp) ──
bool lgfxTouchBegin(int sclk, int mosi, int miso, int cs);
bool lgfxTouchRaw(int& x, int& y, int& z);

inline bool touchBegin() {
    return lgfxTouchBegin(PIN_TOUCH_SCLK, PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_CS);
}

// Returns true while pressed; x/y are raw values already oriented to the
// landscape screen axes, mapped to pixels by TOUCH_* in main.cpp.
inline bool touchRead(int& x, int& y) {
    int rx, ry, z;
    if (!lgfxTouchRaw(rx, ry, z)) return false;   // rx = 0xD1, ry = 0x91
    x = ry;
    y = rx;
    return true;
}

// Diagnostic: raw readings while pressed (build with -DCYD_TOUCH_DEBUG).
inline void touchDebugDump() {
    int x, y, z;
    if (lgfxTouchRaw(x, y, z)) Serial.printf("[TDBG] x=%d y=%d size=%d\n", x, y, z);
}

inline bool sdBegin() {
    sdSpi().begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    return SD.begin(PIN_SD_CS, sdSpi(), 4000000);
}

#else
// ─── ESP32-3248S035R (3.5") ──────────────────────────────────────────────────
}  // namespace cyd
#include <XPT2046_Touchscreen.h>
namespace cyd {

constexpr const char* BOARD_NAME = "ESP32-3248S035R 3.5in";
constexpr int PIN_TFT_BL    = 27;
constexpr int PIN_TOUCH_CS  = 33;
constexpr int PIN_TOUCH_IRQ = 36;
constexpr int PIN_SD_CS     = 5;

constexpr int TOUCH_MIN_X = 240;
constexpr int TOUCH_MAX_X = 3850;
constexpr int TOUCH_MIN_Y = 240;
constexpr int TOUCH_MAX_Y = 3850;
constexpr bool TOUCH_FLIP_X = false;
constexpr bool TOUCH_FLIP_Y = false;

inline XPT2046_Touchscreen& touchDev() {
    static XPT2046_Touchscreen t(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
    return t;
}

inline bool touchBegin() {
    touchDev().begin();          // calls SPI.begin() with ESP32 default pins...
    touchDev().setRotation(1);
    SPI.begin(14, 12, 13, -1);   // ...so put the shared bus back on the CYD pins
    delay(10);
    return true;
}

inline bool touchRead(int& x, int& y) {
    if (!touchDev().touched()) return false;
    TS_Point p = touchDev().getPoint();
    x = p.x; y = p.y;
    return true;
}

inline bool sdBegin() { return SD.begin(PIN_SD_CS, SPI, 4000000); }
#endif

}  // namespace cyd
