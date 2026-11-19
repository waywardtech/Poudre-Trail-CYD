// XPT2046 touch for the 2.8" ESP32-2432S028R, using the same LovyanGFX driver
// and configuration as waywardtech/CYD-Diagnostic (firmware/src/display.h),
// whose touch is known to work on this board: software SPI on CLK 25,
// MOSI 32, MISO 39, CS 33, 1 MHz, PENIRQ unused.
//
// Kept in its own translation unit: LovyanGFX declares colour constants
// (TFT_BLACK, ...) that TFT_eSPI defines as macros, so the two headers must
// never meet in one file.
#if defined(CYD_BOARD_2432S028R)

#include <LovyanGFX.hpp>

namespace cyd {

static lgfx::Touch_XPT2046 g_touch;

bool lgfxTouchBegin(int sclk, int mosi, int miso, int cs) {
    auto cfg = g_touch.config();
    cfg.x_min = 0;      cfg.x_max = 4095;   // calibration is applied in main.cpp
    cfg.y_min = 0;      cfg.y_max = 4095;
    cfg.pin_int = -1;
    cfg.bus_shared = false;
    cfg.offset_rotation = 0;
    cfg.spi_host = -1;  // software SPI: the controller is on its own pins
    cfg.freq = 1000000;
    cfg.pin_sclk = sclk;
    cfg.pin_mosi = mosi;
    cfg.pin_miso = miso;
    cfg.pin_cs = cs;
    g_touch.config(cfg);
    return g_touch.init();
}

// Raw controller values (LovyanGFX naming: x = cmd 0xD1, y = cmd 0x91).
bool lgfxTouchRaw(int& x, int& y, int& z) {
    lgfx::touch_point_t tp;
    if (!g_touch.getTouchRaw(&tp, 1)) return false;
    x = tp.x; y = tp.y; z = tp.size;
    return true;
}

}  // namespace cyd

#endif
