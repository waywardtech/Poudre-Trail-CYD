// Poudre Trail — CYD (ESP32-2432S028R) main sketch
// Display: TFT_eSPI / ILI9341  Touch: XPT2046  Storage: SD card (SPI)
//
// SD card layout expected:
//   /art/*.bmp            — 24-bit BMP images (96x96 or smaller)
//   /data/locations.csv
//   /data/events.csv
//   /data/trades.csv
//   /data/events/*.txt    — event body text files
//   /saves/slot1.sav      — save data (written by game)

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "poudre_trail_engine.hpp"

// ─── Pin assignments (CYD / ESP32-2432S028R) ─────────────────────────────────
static constexpr int PIN_TFT_BL    = 21;
static constexpr int PIN_TOUCH_CS  = 33;
static constexpr int PIN_TOUCH_IRQ = 36;
static constexpr int PIN_SD_CS     = 5;


// Touch calibration (raw ADC values for the CYD panel)
static constexpr int TOUCH_MIN_X = 240;
static constexpr int TOUCH_MAX_X = 3850;
static constexpr int TOUCH_MIN_Y = 240;
static constexpr int TOUCH_MAX_Y = 3850;

static constexpr int TFT_W = 320;
static constexpr int TFT_H = 240;

static constexpr const char* SAVE_PATH = "/saves/slot1.sav";

// ─── Globals ─────────────────────────────────────────────────────────────────

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
GameEngine game;

struct Button {
    int16_t x, y, w, h;
    String  label;
    bool    enabled = false;
};
static Button buttons[4];
static uint32_t lastTouchMs   = 0;
static bool     needsRender   = true;
static String   bootMessage   = "Booting...";
static String   transientMsg;
static uint32_t transientUntil = 0;
static char     sbuf[80];      // scratch buffer for snprintf

// ─── Helpers ─────────────────────────────────────────────────────────────────

static int mapTouchX(int raw) { return map(raw, TOUCH_MIN_X, TOUCH_MAX_X, 0, TFT_W); }
static int mapTouchY(int raw) { return map(raw, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, TFT_H); }
static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static String readTextFile(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String out;
    out.reserve(f.size());
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

static bool writeTextFile(const char* path, const String& text) {
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    f.print(text);
    f.close();
    return true;
}

static std::string toStd(const String& s) { return std::string(s.c_str()); }

static void setTransient(const String& msg) {
    transientMsg   = msg;
    transientUntil = millis() + 1600;
    needsRender    = true;
}

// ─── Button layout ───────────────────────────────────────────────────────────

static void clearButtons() { for (auto& b : buttons) b = Button{}; }

static void buildButtons(const Snapshot& s) {
    clearButtons();
    int count = (int)s.optionLabels.size();
    if (count > 4) count = 4;
    if (count == 0) return;
    const int TOP = 164;
    const int BOT = TFT_H - 6;
    const int GAP = 4;
    int avail = BOT - TOP;
    int bh    = (avail - GAP * (count - 1)) / count;
    if (bh < 14) bh = 14;
    for (int i = 0; i < count; ++i) {
        buttons[i].x       = 6;
        buttons[i].y       = TOP + i * (bh + GAP);
        buttons[i].w       = TFT_W - 12;
        buttons[i].h       = bh;
        buttons[i].label   = s.optionLabels[i].c_str();
        buttons[i].enabled = true;
    }
}

static int buttonHit(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        const auto& b = buttons[i];
        if (!b.enabled) continue;
        if (x >= b.x && x < b.x+b.w && y >= b.y && y < b.y+b.h) return i;
    }
    return -1;
}

// ─── BMP art decoder ─────────────────────────────────────────────────────────
// Reads 24-bit uncompressed BMP from SD and draws up to maxW×maxH pixels at (x,y).

static uint16_t readLE16(File& f) {
    uint8_t lo = f.read(), hi = f.read();
    return (uint16_t)lo | ((uint16_t)hi << 8);
}
static uint32_t readLE32(File& f) {
    uint16_t lo = readLE16(f), hi = readLE16(f);
    return (uint32_t)lo | ((uint32_t)hi << 16);
}

static bool drawBmpFromSd(const String& path, int x, int y, int maxW, int maxH) {
    if (path.length() == 0) return false;
    File bmp = SD.open(path.c_str(), FILE_READ);
    if (!bmp) return false;

    if (readLE16(bmp) != 0x4D42) { bmp.close(); return false; }
    readLE32(bmp); readLE32(bmp);                 // file size, reserved
    uint32_t pixOff   = readLE32(bmp);
    uint32_t hdrSize  = readLE32(bmp);
    if (hdrSize < 40)  { bmp.close(); return false; }
    int32_t  bmpW     = (int32_t)readLE32(bmp);
    int32_t  bmpH     = (int32_t)readLE32(bmp);
    if (readLE16(bmp) != 1) { bmp.close(); return false; } // color planes
    uint16_t depth    = readLE16(bmp);
    uint32_t compress = readLE32(bmp);
    if (depth != 24 || compress != 0 || bmpW <= 0 || bmpH == 0) { bmp.close(); return false; }

    bool flip = true;
    if (bmpH < 0) { bmpH = -bmpH; flip = false; }

    int drawW = (int)bmpW < maxW ? (int)bmpW : maxW;
    int drawH = (int)bmpH < maxH ? (int)bmpH : maxH;
    if (drawW > 96) { bmp.close(); return false; } // guard lineBuf size

    uint32_t rowSize = ((uint32_t)bmpW * 3 + 3) & ~3u;
    uint8_t  rowBuf[288];
    uint16_t lineBuf[96];

    tft.startWrite();
    for (int row = 0; row < drawH; ++row) {
        int srcRow = flip ? (bmpH - 1 - row) : row;
        bmp.seek(pixOff + (uint32_t)srcRow * rowSize);
        bmp.read(rowBuf, (int)bmpW * 3);
        for (int col = 0; col < drawW; ++col) {
            uint8_t b = rowBuf[col*3+0], g = rowBuf[col*3+1], r = rowBuf[col*3+2];
            lineBuf[col] = ((uint16_t)(r & 0xF8) << 8)
                         | ((uint16_t)(g & 0xFC) << 3)
                         | (b >> 3);
        }
        tft.setAddrWindow(x, y + row, drawW, 1);
        tft.pushPixels(lineBuf, drawW);
    }
    tft.endWrite();
    bmp.close();
    return true;
}

// ─── Rendering ───────────────────────────────────────────────────────────────

static void drawWrapped(const String& text, int x, int y, int width, int textSize, int maxLines) {
    int cpl = width / (6 * textSize);
    if (cpl < 3) return;
    String rem = text;
    for (int line = 0; line < maxLines && rem.length() > 0; ++line) {
        int cut = rem.length();
        if (cut > cpl) {
            cut = cpl;
            while (cut > 0 && rem[cut] != ' ') --cut;
            if (cut == 0) cut = cpl;
        }
        String part = rem.substring(0, cut);
        part.trim();
        tft.setCursor(x, y + line * (10 * textSize));
        tft.print(part);
        rem = rem.substring(cut);
        rem.trim();
    }
}

static void renderArt(const Snapshot& s) {
    const int AX = TFT_W - 100, AY = 60, AW = 92, AH = 68;
    tft.fillRoundRect(AX-3, AY-3, AW+6, AH+6, 6, 0x1082);
    bool ok = drawBmpFromSd(String("/") + s.imagePath.c_str(), AX, AY, AW, AH);
    if (!ok) {
        tft.fillRect(AX, AY, AW, AH, 0x2965);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(AX+8, AY+28); tft.print("No BMP art");
        tft.setCursor(AX+8, AY+40); tft.print("on SD card");
    }
}

static void renderSnapshot(const Snapshot& s) {
    tft.fillScreen(TFT_BLACK);

    // Title bar
    tft.fillRect(0, 0, TFT_W, 22, 0x2965);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(8, 4);
    tft.print("Poudre Trail");

    // Status bar (rows 22-58)
    tft.fillRect(0, 23, TFT_W, 36, 0x18C3);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);

    snprintf(sbuf, sizeof(sbuf), "Day %d  %s", s.day, s.locationName.c_str());
    tft.setCursor(6, 28); tft.print(sbuf);

    snprintf(sbuf, sizeof(sbuf), "F%d A%d Med%d T%d H%d Mo%d He%d",
             s.resources.food, s.resources.ammunition, s.resources.medicine,
             s.resources.tradeGoods, s.resources.horseCondition,
             s.resources.morale, s.resources.health);
    tft.setCursor(6, 38); tft.print(sbuf);

    snprintf(sbuf, sizeof(sbuf), "Janis:%s  Friday:%s  Mason:%s",
             s.reputationSummaryJanis.c_str(),
             s.reputationSummaryFriday.c_str(),
             s.reputationSummaryMason.c_str());
    tft.setCursor(6, 48); tft.print(sbuf);

    // Event / body panel
    tft.fillRoundRect(6, 60, TFT_W - 12, 98, 6, 0x18C3);
    tft.setTextColor(0xFD20); // amber
    tft.setTextSize(2);
    tft.setCursor(12, 66);
    String hl = s.headline.c_str();
    if (hl.length() > 16) hl = hl.substring(0, 16);
    tft.print(hl);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    drawWrapped(String(s.body.c_str()), 12, 88, TFT_W - 120, 1, 6);
    renderArt(s);

    // Buttons
    buildButtons(s);
    for (const auto& b : buttons) {
        if (!b.enabled) continue;
        tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, 0x2965);
        tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, 0xFD20);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(b.x+8, b.y + b.h/2 - 4);
        String lbl = b.label;
        if (lbl.length() > 46) lbl = lbl.substring(0, 43) + "...";
        tft.print(lbl);
    }

    // Footer bar
    tft.fillRect(0, TFT_H - 12, TFT_W, 12, 0x2965);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(4, TFT_H - 10);
    String foot = (transientUntil > millis()) ? transientMsg : String(s.footer.c_str());
    if (foot.length() > 53) foot = foot.substring(0, 53);
    tft.print(foot);
}

// ─── Save / Load ─────────────────────────────────────────────────────────────

static bool saveGame() {
    SD.mkdir("/saves");
    return writeTextFile(SAVE_PATH, String(game.serializeSave().c_str()));
}

static bool loadGame(String* msgOut = nullptr) {
    String data = readTextFile(SAVE_PATH);
    std::string err;
    bool ok = game.loadSaveText(toStd(data), &err);
    if (msgOut) *msgOut = ok ? "Save loaded" : (err.empty() ? "Load failed" : String(err.c_str()));
    return ok;
}

// ─── Input handling ──────────────────────────────────────────────────────────

static void handleSelection(int idx, const Snapshot& s) {
    if (idx < 0) return;
    if (s.mode == GameMode::TITLE) {
        if (idx == 0) { game.startGame(); }
        else if (idx == 1) { String m; loadGame(&m); setTransient(m); }
    } else if (s.mode == GameMode::TRAVEL) {
        if (idx == 0) game.travel();
        else if (idx == 1) game.openTrade();
        else if (idx == 2) setTransient(saveGame() ? "Saved to SD" : "Save failed");
        else if (idx == 3) { String m; loadGame(&m); setTransient(m); }
    } else if (s.mode == GameMode::EVENT || s.mode == GameMode::TRADE) {
        game.chooseOption(idx);
    } else if (s.mode == GameMode::VICTORY || s.mode == GameMode::GAME_OVER) {
        if (idx == 0) game.startGame();
        else if (idx == 1) { String m; loadGame(&m); setTransient(m); }
    }
    needsRender = true;
}

static void pollTouch(const Snapshot& s) {
    if (!touch.touched()) return;
    if (millis() - lastTouchMs < 220) return;
    TS_Point p = touch.getPoint();
    int x = clampInt(mapTouchX(p.x), 0, TFT_W - 1);
    int y = clampInt(mapTouchY(p.y), 0, TFT_H - 1);
    int hit = buttonHit(x, y);
    if (hit >= 0) {
        lastTouchMs = millis();
        handleSelection(hit, s);
    }
}

// ─── SD content loading ──────────────────────────────────────────────────────

static void loadGameContent() {
    String locs   = readTextFile("/data/locations.csv");
    String events = readTextFile("/data/events.csv");
    String trades = readTextFile("/data/trades.csv");

    // Body text files — add virginia_dale event body
    std::vector<std::pair<std::string,std::string>> bodies = {
        {"events/janis_store.txt",         toStd(readTextFile("/data/events/janis_store.txt"))},
        {"events/chief_friday.txt",        toStd(readTextFile("/data/events/chief_friday.txt"))},
        {"events/virginia_dale.txt",       toStd(readTextFile("/data/events/virginia_dale.txt"))},
        {"events/mason_reveal.txt",        toStd(readTextFile("/data/events/mason_reveal.txt"))},
        {"events/jack_slade.txt",          toStd(readTextFile("/data/events/jack_slade.txt"))},
        {"events/flood_1864.txt",          toStd(readTextFile("/data/events/flood_1864.txt"))},
    };

    std::string err;
    bool ok = game.loadFromText(toStd(locs), toStd(events), toStd(trades), bodies, &err);
    bootMessage = ok ? "SD content loaded" : (err.empty() ? "Using fallback content" : String(err.c_str()));
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    touch.begin();
    // Touch rotation and axis swap handled in mapTouchX/Y calibration constants

    // Reconfigure global SPI with CYD bus pins before SD init.
    // TFT_eSPI uses its own internal SPIClass(VSPI); the global SPI
    // singleton still has default pins (18/19/23) unless we set it here.
    SPI.begin(14, 12, 13, PIN_SD_CS); // SCK, MISO, MOSI, SS
    if (!SD.begin(PIN_SD_CS)) {
        bootMessage = "SD init failed - fallback world";
    } else {
        loadGameContent();
    }

    // Boot splash
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(24, 90); tft.print("Poudre Trail");
    tft.setTextSize(1);
    tft.setCursor(24, 118); tft.print(bootMessage);
    delay(1200);
    needsRender = true;
}

void loop() {
    Snapshot s = game.snapshot();
    bool transientExpired = transientUntil > 0 && millis() > transientUntil;
    if (needsRender || transientExpired) {
        if (transientExpired) transientUntil = 0;
        renderSnapshot(s);
        needsRender = false;
    }
    pollTouch(s);
    delay(25);
}
