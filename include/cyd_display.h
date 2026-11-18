// cyd_display.h — per-board pin map, touch and SD bring-up for the CYD targets.
//
// Select the board with a build flag (see platformio.ini):
//   CYD_BOARD_2432S028R  2.8" ILI9341 / ST7789 320x240  (pins: waywardtech/CYD-Diagnostic)
//   CYD_BOARD_3248S035R  3.5" ST7796 480x320
//
// The two boards are wired very differently:
//   2.8"  LCD on HSPI (14/12/13), microSD on VSPI (18/19/23, CS 5),
//         XPT2046 touch on its own pins (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36)
//         which no hardware SPI bus is left for, so it is bit-banged here.
//   3.5"  LCD, touch (CS 33) and microSD (CS 5) all share one bus on 14/12/13.
//
// Both paths return touch coordinates in the same "rotation 1" raw space the
// XPT2046_Touchscreen library uses, so one set of TOUCH_* calibration constants
// per board maps straight onto the landscape screen.
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

// Raw (rotation-1 space) touch extents. Clone-to-clone variance is real:
// [TOUCH] serial lines print raw and mapped coords if you need to retune.
constexpr int TOUCH_MIN_X = 200;
constexpr int TOUCH_MAX_X = 3700;
constexpr int TOUCH_MIN_Y = 240;
constexpr int TOUCH_MAX_Y = 3800;
constexpr bool TOUCH_FLIP_X = false;
constexpr bool TOUCH_FLIP_Y = false;

// TFT_eSPI runs on HSPI (USE_HSPI_PORT); microSD gets VSPI to itself.
inline SPIClass& sdSpi() { static SPIClass bus(VSPI); return bus; }

// ── Bit-banged XPT2046 ──
inline uint16_t xptXfer(uint8_t cmd) {
    for (int i = 7; i >= 0; --i) {
        digitalWrite(PIN_TOUCH_MOSI, (cmd >> i) & 1);
        digitalWrite(PIN_TOUCH_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_SCLK, LOW);
        delayMicroseconds(1);
    }
    // 12 data bits, MSB first; DOUT changes on the falling edge, so sample
    // after it (the busy clock is consumed by the 8th command clock).
    uint16_t v = 0;
    for (int i = 0; i < 12; ++i) {
        digitalWrite(PIN_TOUCH_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_SCLK, LOW);
        delayMicroseconds(1);
        v = (v << 1) | (digitalRead(PIN_TOUCH_MISO) & 1);
    }
    return v;
}

inline bool touchBegin() {
    pinMode(PIN_TOUCH_SCLK, OUTPUT);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);
    digitalWrite(PIN_TOUCH_SCLK, LOW);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    return true;
}

// Returns true while pressed; x/y are rotation-1 raw values (0..4095).
inline bool touchRead(int& x, int& y) {
    if (digitalRead(PIN_TOUCH_IRQ) == HIGH) return false;   // PENIRQ is active low
    digitalWrite(PIN_TOUCH_CS, LOW);
    int z1 = xptXfer(0xB1);
    int z2 = xptXfer(0xC1);
    int z  = z1 + 4095 - z2;
    if (z < 400) { xptXfer(0x90); digitalWrite(PIN_TOUCH_CS, HIGH); return false; }
    int ax = 0, ay = 0;
    xptXfer(0x91); xptXfer(0xD1);              // discard first, settling samples
    for (int i = 0; i < 4; ++i) { ax += xptXfer(0x91); ay += xptXfer(0xD1); }
    xptXfer(0x90);                              // power down, re-enable IRQ
    digitalWrite(PIN_TOUCH_CS, HIGH);
    int rx = ax / 4, ry = ay / 4;               // library naming: x=0x91, y=0xD1
    x = 4095 - ry;                              // same transform as setRotation(1)
    y = rx;
    return true;
}

// Diagnostic: one line of raw controller readings (build with -DCYD_TOUCH_DEBUG).
inline void touchDebugDump() {
    int irq = digitalRead(PIN_TOUCH_IRQ);
    digitalWrite(PIN_TOUCH_CS, LOW);
    int z1 = xptXfer(0xB1), z2 = xptXfer(0xC1);
    int dx = xptXfer(0xD1), dy = xptXfer(0x91);            // differential
    int sx = xptXfer(0xD5), sy = xptXfer(0x95);            // single-ended, ref on
    xptXfer(0x90);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    Serial.printf("[TDBG] irq=%d z1=%d z2=%d z=%d diff x=%d y=%d single x=%d y=%d\n",
                  irq, z1, z2, z1 + 4095 - z2, dx, dy, sx, sy);
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
