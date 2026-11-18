#pragma once
// Display, touch and SD wiring for the ESP32-2432S028R "Cheap Yellow Display".
//
// Pin map and panel settings are taken from waywardtech/CYD-Diagnostic
// (firmware/src/config.h and firmware/src/display.h). On this board the LCD
// (HSPI), the microSD slot (VSPI) and the XPT2046 touch controller (its own
// pins, software SPI) are on three separate buses, so nothing is shared.
//
// SD.h must be included before LovyanGFX so drawBmpFile(SD, ...) is available.
#include <SD.h>
#include <SPI.h>
#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
// Display (HSPI)
constexpr int PIN_TFT_SCLK = 14;
constexpr int PIN_TFT_MOSI = 13;
constexpr int PIN_TFT_MISO = 12;
constexpr int PIN_TFT_DC = 2;
constexpr int PIN_TFT_CS = 15;
constexpr int PIN_TFT_BL = 21;

// Resistive touch (XPT2046, software SPI)
constexpr int PIN_TOUCH_SCLK = 25;
constexpr int PIN_TOUCH_MOSI = 32;
constexpr int PIN_TOUCH_MISO = 39;
constexpr int PIN_TOUCH_CS = 33;

// microSD (VSPI)
constexpr int PIN_SD_SCLK = 18;
constexpr int PIN_SD_MISO = 19;
constexpr int PIN_SD_MOSI = 23;
constexpr int PIN_SD_CS = 5;

// ---------------------------------------------------------------------------
// Panel selection. Micro-USB boards are ILI9341; several later USB-C boards
// are ST7789 with inverted colour. Every value can be overridden with -D.
// ---------------------------------------------------------------------------
#if defined(CYD_PANEL_ST7789) && CYD_PANEL_ST7789
using CYDPanel = lgfx::Panel_ST7789;
#define CYD_PANEL_NAME "ST7789"
#ifndef CYD_INVERT
#define CYD_INVERT 1
#endif
#else
using CYDPanel = lgfx::Panel_ILI9341;
#define CYD_PANEL_NAME "ILI9341"
#ifndef CYD_INVERT
#define CYD_INVERT 0
#endif
#endif

#ifndef CYD_RGB_ORDER
#define CYD_RGB_ORDER 0
#endif
#ifndef CYD_OFFSET_X
#define CYD_OFFSET_X 0
#endif
#ifndef CYD_OFFSET_Y
#define CYD_OFFSET_Y 0
#endif

// Touch calibration (raw XPT2046 units). Clone-to-clone variance is real: if
// taps land offset or mirrored, read the raw values logged on [TOUCH] lines
// at the screen corners and put them here (or pass them with -D).
#ifndef CYD_TOUCH_X_MIN
#define CYD_TOUCH_X_MIN 300
#endif
#ifndef CYD_TOUCH_X_MAX
#define CYD_TOUCH_X_MAX 3900
#endif
#ifndef CYD_TOUCH_Y_MIN
#define CYD_TOUCH_Y_MIN 3700
#endif
#ifndef CYD_TOUCH_Y_MAX
#define CYD_TOUCH_Y_MAX 200
#endif

// Landscape, USB connector on the right.
constexpr uint8_t CYD_ROTATION = 1;
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

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
      cfg.offset_x = CYD_OFFSET_X;
      cfg.offset_y = CYD_OFFSET_Y;
      cfg.offset_rotation = 2;
      cfg.readable = true;
      cfg.invert = CYD_INVERT;
      cfg.rgb_order = CYD_RGB_ORDER;
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
      cfg.x_min = CYD_TOUCH_X_MIN;
      cfg.x_max = CYD_TOUCH_X_MAX;
      cfg.y_min = CYD_TOUCH_Y_MIN;
      cfg.y_max = CYD_TOUCH_Y_MAX;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = -1;  // software SPI on the touch controller's own pins
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
