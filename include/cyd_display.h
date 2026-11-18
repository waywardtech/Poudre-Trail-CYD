#pragma once
// CYD hardware definition: ESP32-2432S028R (2.8", 320x240).
//
// Pins, bus layout, panel variants and touch calibration come from the
// CYD System Inspector (waywardtech/CYD-Diagnostic, firmware/src/config.h and
// display.h), the project that holds the current, tested CYD details:
//   - LCD on HSPI:        SCLK 14, MOSI 13, MISO 12, DC 2, CS 15, backlight 21
//   - Touch (XPT2046) on its own pins, polled in software SPI:
//                         SCLK 25, MOSI 32, MISO 39, CS 33
//   - microSD on VSPI:    SCLK 18, MISO 19, MOSI 23, CS 5
//   - Expansion P3:       GPIO22 / GPIO27 (I2C); GPIO21 is the backlight
// Two panel controllers ship under this board name: micro-USB boards are
// ILI9341; many USB-C boards are ST7789 with inverted colour (-DCYD_PANEL_ST7789=1).

#include <LovyanGFX.hpp>

constexpr int PIN_TFT_SCLK = 14;
constexpr int PIN_TFT_MOSI = 13;
constexpr int PIN_TFT_MISO = 12;
constexpr int PIN_TFT_DC   = 2;
constexpr int PIN_TFT_CS   = 15;
constexpr int PIN_TFT_BL   = 21;

constexpr int PIN_TOUCH_SCLK = 25;
constexpr int PIN_TOUCH_MOSI = 32;
constexpr int PIN_TOUCH_MISO = 39;
constexpr int PIN_TOUCH_CS   = 33;

constexpr int PIN_SD_SCLK = 18;
constexpr int PIN_SD_MISO = 19;
constexpr int PIN_SD_MOSI = 23;
constexpr int PIN_SD_CS   = 5;

// Touch calibration (clone-to-clone variance is real). Adjust if taps land
// offset or mirrored; the Y range is deliberately inverted.
constexpr int TOUCH_X_MIN = 300;
constexpr int TOUCH_X_MAX = 3900;
constexpr int TOUCH_Y_MIN = 3700;
constexpr int TOUCH_Y_MAX = 200;

#if defined(CYD_PANEL_ST7789) && CYD_PANEL_ST7789
using CYDPanel = lgfx::Panel_ST7789;
constexpr bool PANEL_INVERT = true;
constexpr const char* PANEL_NAME = "ST7789";
#else
using CYDPanel = lgfx::Panel_ILI9341;
constexpr bool PANEL_INVERT = false;
constexpr const char* PANEL_NAME = "ILI9341";
#endif

class CYDDisplay : public lgfx::LGFX_Device {
    CYDPanel panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM backlight_;
    lgfx::Touch_XPT2046 touch_;

public:
    CYDDisplay() {
        {
            auto cfg = bus_.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_TFT_SCLK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = PIN_TFT_MISO;
            cfg.pin_dc = PIN_TFT_DC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 2;
            cfg.readable = true;
            cfg.invert = PANEL_INVERT;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }
        {
            auto cfg = backlight_.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            backlight_.config(cfg);
            panel_.setLight(&backlight_);
        }
        {
            auto cfg = touch_.config();
            cfg.x_min = TOUCH_X_MIN;
            cfg.x_max = TOUCH_X_MAX;
            cfg.y_min = TOUCH_Y_MIN;
            cfg.y_max = TOUCH_Y_MAX;
            cfg.pin_int = -1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.spi_host = -1;          // software SPI: own pins, no bus sharing
            cfg.freq = 1000000;
            cfg.pin_sclk = PIN_TOUCH_SCLK;
            cfg.pin_mosi = PIN_TOUCH_MOSI;
            cfg.pin_miso = PIN_TOUCH_MISO;
            cfg.pin_cs = PIN_TOUCH_CS;
            touch_.config(cfg);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
