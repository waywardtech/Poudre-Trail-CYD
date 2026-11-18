// Poudre Trail CYD -- hardware bring-up.
//
// Checks the four things the game depends on and reports each on the serial
// port at 115200 baud:
//   [TFT]   panel type, resolution after rotation, colour/inversion settings,
//           a pixel read-back, and a full-screen colour sequence at boot
//   [SD]    mount, card type and size, and every .bmp found under /art
//   [TOUCH] every tap: raw XPT2046 values, mapped screen position and which
//           on-screen target it landed on (title bar, button, art, footer)
//   [ART]   each frame drawn in the art panel
//
// Screen layout (320 x 240, landscape):
//   title bar   y   0..27   tap: clears tap markers and redraws
//   art panel   x   8..167, y 34..153
//   buttons     x 180..311, three rows
//   swatches    y 162..205  named colour chips, to check colour order
//   footer      y 212..239  shows the last tap
// A 1 px magenta frame runs round the very edge of the screen: if any side
// of it is missing or doubled, the picture is shifted.

#include <Arduino.h>

#include "cyd_display.h"

namespace {

CYDDisplay lcd;
SPIClass sdSpi(VSPI);

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
constexpr int TITLE_H = 28;
constexpr int FOOTER_Y = 212;
constexpr int FOOTER_H = SCREEN_H - FOOTER_Y;

constexpr int ART_X = 8;
constexpr int ART_Y = 34;
constexpr int ART_W = 160;
constexpr int ART_H = 120;

constexpr int BTN_X = 180;
constexpr int BTN_W = 132;
constexpr int BTN_H = 36;
constexpr int BTN_GAP = 8;
constexpr int BTN_Y0 = 34;
constexpr int BUTTON_COUNT = 3;
const char* const BUTTON_LABELS[BUTTON_COUNT] = {"JANIS", "FRIDAY", "MASON"};

constexpr int SWATCH_Y = 162;
constexpr int SWATCH_H = 44;

struct Swatch {
  const char* name;
  uint8_t r, g, b;
};
const Swatch SWATCHES[] = {
    {"RED", 255, 0, 0},     {"GRN", 0, 255, 0},   {"BLU", 0, 0, 255},
    {"YEL", 255, 255, 0},   {"CYN", 0, 255, 255}, {"MAG", 255, 0, 255},
    {"WHT", 255, 255, 255}, {"BLK", 0, 0, 0},
};
constexpr int SWATCH_COUNT = sizeof(SWATCHES) / sizeof(SWATCHES[0]);

// Palette (RGB565 via color565 so the chips and the UI agree)
uint16_t colTitle, colTitleInk, colButton, colButtonHit, colInk, colFooter,
    colFrame, colMarker;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
bool sdReady = false;
constexpr int MAX_ART = 32;
String artFiles[MAX_ART];
int artCount = 0;
int artIndex = 0;
uint32_t artFrame = 0;
uint32_t lastArtAt = 0;
constexpr uint32_t ART_PERIOD_MS = 900;

bool touchDown = false;
uint32_t lastSpinAt = 0;
int spinPhase = 0;
uint32_t tapCount = 0;

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void drawTitle() {
  lcd.fillRect(0, 0, SCREEN_W, TITLE_H, colTitle);
  lcd.setTextColor(colTitleInk, colTitle);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_left);
  lcd.drawString("POUDRE TRAIL", 10, TITLE_H / 2);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_right);
  lcd.drawString(CYD_PANEL_NAME, SCREEN_W - 26, TITLE_H / 2);
}

void drawButton(int i, bool hit) {
  int y = BTN_Y0 + i * (BTN_H + BTN_GAP);
  uint16_t bg = hit ? colButtonHit : colButton;
  lcd.fillRoundRect(BTN_X, y, BTN_W, BTN_H, 6, bg);
  lcd.setTextColor(colInk, bg);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  lcd.drawString(BUTTON_LABELS[i], BTN_X + BTN_W / 2, y + BTN_H / 2);
}

void drawSwatches() {
  int w = SCREEN_W / SWATCH_COUNT;
  lcd.setTextSize(1);
  lcd.setTextDatum(bottom_center);
  for (int i = 0; i < SWATCH_COUNT; ++i) {
    const Swatch& s = SWATCHES[i];
    uint16_t c = lcd.color565(s.r, s.g, s.b);
    int x = i * w;
    lcd.fillRect(x + 2, SWATCH_Y, w - 4, SWATCH_H - 12, c);
    if (s.r == 0 && s.g == 0 && s.b == 0) {
      lcd.drawRect(x + 2, SWATCH_Y, w - 4, SWATCH_H - 12, colInk);
    }
    lcd.setTextColor(colInk, TFT_BLACK);
    lcd.drawString(s.name, x + w / 2, SWATCH_Y + SWATCH_H);
  }
}

void drawFooter(const char* text) {
  lcd.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, colFooter);
  lcd.setTextColor(colInk, colFooter);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_left);
  lcd.drawString(text, 8, FOOTER_Y + FOOTER_H / 2);
}

void drawEdgeFrame() { lcd.drawRect(0, 0, SCREEN_W, SCREEN_H, colFrame); }

void drawArtPlaceholder(const char* why) {
  lcd.fillRect(ART_X, ART_Y, ART_W, ART_H, TFT_BLACK);
  lcd.drawRect(ART_X - 1, ART_Y - 1, ART_W + 2, ART_H + 2, colButton);
  lcd.setTextColor(colInk, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(middle_center);
  lcd.drawString(why, ART_X + ART_W / 2, ART_Y + ART_H - 10);
}

void drawLayout() {
  lcd.fillScreen(TFT_BLACK);
  drawTitle();
  for (int i = 0; i < BUTTON_COUNT; ++i) drawButton(i, false);
  drawSwatches();
  lcd.drawRect(ART_X - 1, ART_Y - 1, ART_W + 2, ART_H + 2, colButton);
  char buf[64];
  snprintf(buf, sizeof(buf), "SD %s  |  %d bmp  |  tap anything",
           sdReady ? "OK" : "NONE", artCount);
  drawFooter(buf);
  drawEdgeFrame();
  lastArtAt = 0;  // repaint art on the next loop
}

// ---------------------------------------------------------------------------
// [TFT]
// ---------------------------------------------------------------------------
void colourSequence() {
  struct Step {
    const char* name;
    uint16_t c;
  };
  const Step steps[] = {{"RED", TFT_RED},     {"GREEN", TFT_GREEN},
                        {"BLUE", TFT_BLUE},   {"WHITE", TFT_WHITE},
                        {"BLACK", TFT_BLACK}};
  for (const Step& s : steps) {
    Serial.printf("[TFT] full screen %s\n", s.name);
    lcd.fillScreen(s.c);
    lcd.setTextColor(s.c == TFT_WHITE ? TFT_BLACK : TFT_WHITE, s.c);
    lcd.setTextSize(3);
    lcd.setTextDatum(middle_center);
    lcd.drawString(s.name, SCREEN_W / 2, SCREEN_H / 2);
    delay(600);
  }
}

void tftBegin() {
  lcd.init();
  lcd.setRotation(CYD_ROTATION);
  lcd.setBrightness(220);

  colTitle = lcd.color565(30, 111, 210);
  colTitleInk = TFT_WHITE;
  colButton = lcd.color565(75, 42, 143);
  colButtonHit = lcd.color565(255, 121, 0);
  colInk = TFT_WHITE;
  colFooter = lcd.color565(40, 40, 40);
  colFrame = TFT_MAGENTA;
  colMarker = TFT_YELLOW;

  Serial.printf("[TFT] panel=%s size=%dx%d rotation=%d invert=%d rgb_order=%d offset=%d,%d\n",
                CYD_PANEL_NAME, (int)lcd.width(), (int)lcd.height(),
                (int)lcd.getRotation(), CYD_INVERT, CYD_RGB_ORDER, CYD_OFFSET_X,
                CYD_OFFSET_Y);
  if (lcd.width() != SCREEN_W || lcd.height() != SCREEN_H) {
    Serial.printf("[TFT] WARNING expected %dx%d\n", SCREEN_W, SCREEN_H);
  }

  // Read-back: write a known colour and read it through MISO. A mismatch is
  // informational only -- some panels do not support reads.
  lcd.fillScreen(TFT_BLACK);
  lcd.drawPixel(10, 10, TFT_RED);
  uint16_t back = lcd.readPixel(10, 10);
  Serial.printf("[TFT] readback wrote 0x%04X read 0x%04X %s\n", TFT_RED, back,
                back == TFT_RED ? "OK" : "(mismatch; reads unsupported or bus issue)");

  colourSequence();
  Serial.println("[TFT] check: RED/GREEN/BLUE/WHITE/BLACK as named? swatch row "
                 "matches its labels? magenta frame visible on all 4 edges?");
}

// ---------------------------------------------------------------------------
// [SD]
// ---------------------------------------------------------------------------
bool isBmp(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".bmp");
}

void scanArt(const char* dirPath, int depth) {
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && artCount < MAX_ART;
       f = dir.openNextFile()) {
    String path = f.path();
    if (f.isDirectory()) {
      if (depth > 0) scanArt(path.c_str(), depth - 1);
    } else if (isBmp(path) && path.indexOf("/._") < 0) {
      artFiles[artCount++] = path;
      Serial.printf("[SD] art %s (%lu bytes)\n", path.c_str(),
                    (unsigned long)f.size());
    }
    f.close();
  }
  dir.close();
}

void sdBegin() {
  sdSpi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS, sdSpi, 10000000);
  if (!sdReady) {
    Serial.println("[SD] mount FAILED (no card, not FAT32, or bad contact)");
    return;
  }
  const char* type = "UNKNOWN";
  switch (SD.cardType()) {
    case CARD_MMC: type = "MMC"; break;
    case CARD_SD: type = "SDSC"; break;
    case CARD_SDHC: type = "SDHC"; break;
    case CARD_NONE: type = "NONE"; break;
    default: break;
  }
  Serial.printf("[SD] mount OK type=%s size=%lluMB used=%lluMB\n", type,
                SD.cardSize() / (1024ULL * 1024ULL),
                SD.usedBytes() / (1024ULL * 1024ULL));

  scanArt("/art", 2);
  if (artCount == 0) scanArt("/", 0);
  // FAT directory order is arbitrary; sort so frame1, frame2, ... play in turn.
  for (int i = 1; i < artCount; ++i) {
    for (int j = i; j > 0 && artFiles[j] < artFiles[j - 1]; --j) {
      String t = artFiles[j];
      artFiles[j] = artFiles[j - 1];
      artFiles[j - 1] = t;
    }
  }
  Serial.printf("[SD] %d bmp file(s) found%s\n", artCount,
                artCount ? "" : " -- copy assets/art/*.bmp to /art on the card");
}

// ---------------------------------------------------------------------------
// [ART]
// ---------------------------------------------------------------------------
// Reads the BMP header so the log says why a file will not draw.
bool bmpInfo(const char* path, int32_t& w, int32_t& h, uint16_t& bpp,
             uint32_t& comp) {
  File f = SD.open(path);
  if (!f) return false;
  uint8_t hdr[34];
  bool ok = f.read(hdr, sizeof(hdr)) == sizeof(hdr) && hdr[0] == 'B' &&
            hdr[1] == 'M';
  f.close();
  if (!ok) return false;
  auto u16 = [&](int o) { return (uint16_t)(hdr[o] | (hdr[o + 1] << 8)); };
  auto u32 = [&](int o) {
    return (uint32_t)hdr[o] | ((uint32_t)hdr[o + 1] << 8) |
           ((uint32_t)hdr[o + 2] << 16) | ((uint32_t)hdr[o + 3] << 24);
  };
  w = (int32_t)u32(18);
  h = (int32_t)u32(22);
  bpp = u16(28);
  comp = u32(30);
  return true;
}

void drawNextArt() {
  ++artFrame;
  if (!sdReady || artCount == 0) {
    // No art on the card: animate a placeholder so the loop is still visibly
    // alive, and say so in the panel.
    lcd.fillRect(ART_X, ART_Y, ART_W, ART_H - 20, TFT_BLACK);
    int cx = ART_X + 20 + (int)((artFrame * 12) % (ART_W - 40));
    lcd.fillCircle(cx, ART_Y + 50, 14, colButtonHit);
    drawArtPlaceholder(sdReady ? "no .bmp under /art" : "no SD card");
    if (artFrame % 10 == 1) {
      Serial.printf("[ART] frame %lu placeholder (%s)\n",
                    (unsigned long)artFrame, sdReady ? "no bmp" : "no SD");
    }
    return;
  }

  const String& path = artFiles[artIndex];
  artIndex = (artIndex + 1) % artCount;

  int32_t w = 0, h = 0;
  uint16_t bpp = 0;
  uint32_t comp = 0;
  if (!bmpInfo(path.c_str(), w, h, bpp, comp)) {
    Serial.printf("[ART] frame %lu %s: not a BMP / unreadable\n",
                  (unsigned long)artFrame, path.c_str());
    drawArtPlaceholder("bad bmp");
    return;
  }

  // Fit inside the panel, never upscale past 2x.
  float sx = (float)ART_W / (float)w;
  float sy = (float)ART_H / (float)abs(h);
  float scale = sx < sy ? sx : sy;
  if (scale > 2.0f) scale = 2.0f;

  uint32_t t0 = millis();
  lcd.fillRect(ART_X, ART_Y, ART_W, ART_H, TFT_BLACK);
  // (x, y, maxW, maxH) is the box the image is fitted and clipped to; the
  // datum then centres the image inside that box.
  bool ok = lcd.drawBmpFile(SD, path.c_str(), ART_X, ART_Y, ART_W, ART_H, 0,
                            0, scale, scale, middle_center);
  Serial.printf("[ART] frame %lu %s %ldx%ld %ubpp comp=%lu scale=%.2f %s %lums\n",
                (unsigned long)artFrame, path.c_str(), (long)w, (long)h, bpp,
                (unsigned long)comp, scale, ok ? "OK" : "FAILED",
                (unsigned long)(millis() - t0));
  if (!ok) drawArtPlaceholder("draw failed");
}

// ---------------------------------------------------------------------------
// [TOUCH]
// ---------------------------------------------------------------------------
const char* hitTest(int x, int y, int& button) {
  button = -1;
  if (y < TITLE_H) return "TITLE";
  if (y >= FOOTER_Y) return "FOOTER";
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    int by = BTN_Y0 + i * (BTN_H + BTN_GAP);
    if (x >= BTN_X && x < BTN_X + BTN_W && y >= by && y < by + BTN_H) {
      button = i;
      return BUTTON_LABELS[i];
    }
  }
  if (x >= ART_X && x < ART_X + ART_W && y >= ART_Y && y < ART_Y + ART_H)
    return "ART";
  if (y >= SWATCH_Y && y < SWATCH_Y + SWATCH_H) {
    return SWATCHES[x * SWATCH_COUNT / SCREEN_W].name;
  }
  return "NONE";
}

void handleTouch() {
  lgfx::touch_point_t raw;
  uint16_t x = 0, y = 0;
  bool pressed = lcd.getTouch(&x, &y);
  if (!pressed) {
    touchDown = false;
    return;
  }
  if (touchDown) return;  // log the press, not every sample while held
  touchDown = true;
  ++tapCount;

  int rawCount = lcd.getTouchRaw(&raw, 1);
  int button = -1;
  const char* target = hitTest(x, y, button);
  Serial.printf("[TOUCH] #%lu x=%u y=%u raw=%d,%d z=%d -> %s\n",
                (unsigned long)tapCount, x, y, rawCount ? raw.x : -1,
                rawCount ? raw.y : -1, rawCount ? raw.size : -1, target);

  char buf[64];
  snprintf(buf, sizeof(buf), "tap #%lu  x=%u y=%u  -> %s",
           (unsigned long)tapCount, x, y, target);

  if (strcmp(target, "TITLE") == 0) {
    drawLayout();
    drawFooter(buf);
    drawEdgeFrame();
    return;
  }
  if (button >= 0) {
    drawButton(button, true);
    delay(150);
    drawButton(button, false);
  }
  drawFooter(buf);
  drawEdgeFrame();
  // Marker where the touch landed (title tap clears them).
  if (y < FOOTER_Y) {
    lcd.drawLine(x - 5, y, x + 5, y, colMarker);
    lcd.drawLine(x, y - 5, x, y + 5, colMarker);
  }
}

void spinner() {
  if (millis() - lastSpinAt < 120) return;
  lastSpinAt = millis();
  static const char* const kPhases[] = {"|", "/", "-", "\\"};
  lcd.setTextColor(colTitleInk, colTitle);
  lcd.setTextSize(2);
  lcd.setTextDatum(middle_center);
  lcd.drawString(kPhases[spinPhase], SCREEN_W - 12, TITLE_H / 2);
  spinPhase = (spinPhase + 1) % 4;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] Poudre Trail CYD bring-up");

  tftBegin();
  sdBegin();

  Serial.printf("[TOUCH] XPT2046 cal x=%d..%d y=%d..%d -- tap the title bar, "
                "each button, the footer and the four corners\n",
                CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX, CYD_TOUCH_Y_MIN,
                CYD_TOUCH_Y_MAX);

  drawLayout();
  Serial.println("[BOOT] ready");
}

void loop() {
  handleTouch();
  if (lastArtAt == 0 || millis() - lastArtAt >= ART_PERIOD_MS) {
    lastArtAt = millis();
    drawNextArt();
  }
  spinner();
  delay(5);
}
