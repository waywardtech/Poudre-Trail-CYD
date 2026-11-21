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
#include "cyd_display.h"
#include "poudre_trail_engine.hpp"

// Pin map, touch calibration, touch + SD bring-up per board: include/cyd_display.h
using cyd::TOUCH_MIN_X; using cyd::TOUCH_MAX_X;
using cyd::TOUCH_MIN_Y; using cyd::TOUCH_MAX_Y;
using cyd::TOUCH_FLIP_X; using cyd::TOUCH_FLIP_Y;

// Set from the panel after setRotation(1): 320x240 (2.8") or 480x320 (3.5")
static int TFT_W = 320;
static int TFT_H = 240;
static bool sdMounted = false;
static String artStatus;

static constexpr const char* SAVE_PATH = "/saves/slot1.sav";

// ─── Globals ─────────────────────────────────────────────────────────────────

TFT_eSPI tft = TFT_eSPI();
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

// Flip TOUCH_FLIP_X/Y in cyd_display.h if taps land mirrored (serial prints raw + mapped coords).
static int mapTouchX(int raw) {
    return TOUCH_FLIP_X ? map(raw, TOUCH_MIN_X, TOUCH_MAX_X, TFT_W, 0)
                        : map(raw, TOUCH_MIN_X, TOUCH_MAX_X, 0, TFT_W);
}
static int mapTouchY(int raw) {
    return TOUCH_FLIP_Y ? map(raw, TOUCH_MIN_Y, TOUCH_MAX_Y, TFT_H, 0)
                        : map(raw, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, TFT_H);
}
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

// ─── Layout ──────────────────────────────────────────────────────────────────
// Fixed bands at the top, footer at the bottom, buttons stacked above the
// footer, and the event panel takes whatever is left. Sizes are in pixels and
// work for both 320x240 and 480x320.

static constexpr int TITLE_H   = 22;
static constexpr int STATUS_Y  = 23;
static constexpr int STATUS_H  = 36;
static constexpr int PANEL_Y   = 61;
static constexpr int MARGIN    = 6;
static constexpr int BTN_GAP   = 4;
static constexpr int ART_W     = 96;   // art BMPs are 96x72
static constexpr int ART_H     = 72;
static constexpr int LINE_H    = 10;   // text size 1: 8px glyphs + 2px leading

static void clearButtons() { for (auto& b : buttons) b = Button{}; }

// Lays out the option buttons so they end at `bottom`; returns their top edge.
// Four short labels (the travel menu) go in a 2x2 grid so every button stays
// a comfortable touch target; otherwise one full-width button per row.
static int buildButtons(const Snapshot& s, int bottom) {
    clearButtons();
    int count = (int)s.optionLabels.size();
    if (count > 4) count = 4;
    if (count == 0) return bottom;

    const int fullW = TFT_W - 2 * MARGIN;
    const int halfW = (fullW - BTN_GAP) / 2;
    bool grid = (count == 4);
    for (int i = 0; grid && i < count; ++i)
        if ((int)s.optionLabels[i].size() > (halfW - 16) / 6) grid = false;

    const int rows = grid ? 2 : count;
    const int bh   = rows <= 2 ? 26 : (rows == 3 ? 20 : 18);
    const int top  = bottom - (rows * bh + (rows - 1) * BTN_GAP);
    for (int i = 0; i < count; ++i) {
        const int row = grid ? i / 2 : i;
        const int col = grid ? i % 2 : 0;
        buttons[i].x       = MARGIN + col * (halfW + BTN_GAP);
        buttons[i].y       = top + row * (bh + BTN_GAP);
        buttons[i].w       = grid ? halfW : fullW;
        buttons[i].h       = bh;
        buttons[i].label   = s.optionLabels[i].c_str();
        buttons[i].enabled = true;
    }
    return top;
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
    if (!sdMounted) { artStatus = "SD not mounted"; return false; }
    File bmp = SD.open(path.c_str(), FILE_READ);
    if (!bmp) { artStatus = "Missing file"; return false; }

    if (readLE16(bmp) != 0x4D42) { bmp.close(); artStatus = "Bad BMP"; return false; }
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
    if (drawW > 96 || bmpW > 96) { bmp.close(); return false; } // guard buffer sizes
    // When the box is smaller than the image, keep its centre rather than its corner.
    const int cropX = ((int)bmpW - drawW) / 2;
    const int cropY = ((int)bmpH - drawH) / 2;

    uint32_t rowSize = ((uint32_t)bmpW * 3 + 3) & ~3u;
    uint8_t  rowBuf[288];
    uint16_t lineBuf[96];

    // lineBuf holds native little-endian RGB565; the panel wants big-endian.
    // Without the swap, colours come out as saturated red/blue/magenta noise.
    const bool prevSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);

    for (int row = 0; row < drawH; ++row) {
        int imgRow = cropY + row;
        int srcRow = flip ? (bmpH - 1 - imgRow) : imgRow;
        bmp.seek(pixOff + (uint32_t)srcRow * rowSize);
        bmp.read(rowBuf, (int)bmpW * 3);
        for (int col = 0; col < drawW; ++col) {
            const uint8_t* px = &rowBuf[(cropX + col) * 3];
            uint8_t b = px[0], g = px[1], r = px[2];
            lineBuf[col] = ((uint16_t)(r & 0xF8) << 8)
                         | ((uint16_t)(g & 0xFC) << 3)
                         | (b >> 3);
        }
        tft.pushImage(x, y + row, drawW, 1, lineBuf);
    }
    tft.setSwapBytes(prevSwap);
    bmp.close();
    return true;
}

// ─── Rendering ───────────────────────────────────────────────────────────────

// Word-wraps `text` into at most maxLines lines; if it does not fit, the last
// line ends in "..." instead of being cut mid-word or dropped silently.
static void drawWrapped(const String& text, int x, int y, int width, int textSize, int maxLines) {
    const int cpl = width / (6 * textSize);
    if (cpl < 4 || maxLines < 1) return;
    String rem = text;
    rem.trim();
    for (int line = 0; line < maxLines && rem.length() > 0; ++line) {
        int cut = rem.length();
        if (cut > cpl) {
            cut = cpl;
            while (cut > 0 && rem[cut] != ' ') --cut;
            if (cut == 0) cut = cpl;
        }
        String part = rem.substring(0, cut);
        part.trim();
        rem = rem.substring(cut);
        rem.trim();
        if (line == maxLines - 1 && rem.length() > 0) {
            int keep = cpl - 3;
            if ((int)part.length() > keep) {
                int sp = part.lastIndexOf(' ', keep);
                part = part.substring(0, sp > 0 ? sp : keep);
                part.trim();
            }
            part += "...";
        }
        tft.setCursor(x, y + line * (LINE_H * textSize));
        tft.print(part);
    }
}

static String fitLabel(const String& text, int maxChars) {
    if ((int)text.length() <= maxChars) return text;
    return text.substring(0, maxChars - 3) + "...";
}

// Draws the scene art in a frame of up to 96x72 at (ax, ay); maxH may shrink it.
static void renderArt(const Snapshot& s, int ax, int ay, int maxH) {
    const int AH = maxH < ART_H ? maxH : ART_H;
    if (AH < 24) return;                       // too little room to be worth it
    tft.fillRoundRect(ax - 3, ay - 3, ART_W + 6, AH + 6, 6, 0x1082);
    bool ok = drawBmpFromSd(String("/") + s.imagePath.c_str(), ax, ay, ART_W, AH);
    if (!ok) {
        tft.fillRect(ax, ay, ART_W, AH, 0x2965);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(ax + 8, ay + AH / 2 - 10); tft.print("No art:");
        tft.setCursor(ax + 8, ay + AH / 2 + 2);
        tft.print(artStatus.length() ? artStatus : String("unavailable"));
    }
}

static void renderSnapshot(const Snapshot& s) {
    tft.fillScreen(TFT_BLACK);

    // Title bar
    tft.fillRect(0, 0, TFT_W, TITLE_H, 0x2965);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(8, 4);
    tft.print("Poudre Trail");

    // Status bar
    tft.fillRect(0, STATUS_Y, TFT_W, STATUS_H, 0x18C3);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    const int statusChars = (TFT_W - 12) / 6;

    snprintf(sbuf, sizeof(sbuf), "Day %d  %s", s.day, s.locationName.c_str());
    tft.setCursor(6, STATUS_Y + 5); tft.print(fitLabel(sbuf, statusChars));

    snprintf(sbuf, sizeof(sbuf), "F%d A%d Med%d T%d H%d Mo%d He%d",
             s.resources.food, s.resources.ammunition, s.resources.medicine,
             s.resources.tradeGoods, s.resources.horseCondition,
             s.resources.morale, s.resources.health);
    tft.setCursor(6, STATUS_Y + 15); tft.print(fitLabel(sbuf, statusChars));

    snprintf(sbuf, sizeof(sbuf), "Janis:%s  Friday:%s  Mason:%s",
             s.reputationSummaryJanis.c_str(),
             s.reputationSummaryFriday.c_str(),
             s.reputationSummaryMason.c_str());
    tft.setCursor(6, STATUS_Y + 25); tft.print(fitLabel(sbuf, statusChars));

    // Footer: one line, or two when the message would otherwise be cut off
    String foot = (transientUntil > millis()) ? transientMsg : String(s.footer.c_str());
    foot.trim();
    const int footChars = (TFT_W - 8) / 6;
    const int footLines = (int)foot.length() > footChars ? 2 : 1;
    const int footH     = 4 + footLines * LINE_H;
    const int footTop   = TFT_H - footH;
    tft.fillRect(0, footTop, TFT_W, footH, 0x2965);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    drawWrapped(foot, 4, footTop + 3, TFT_W - 8, 1, footLines);

    // Buttons sit just above the footer; the event panel fills the rest
    const int btnTop   = buildButtons(s, footTop - BTN_GAP);
    const int panelBot = btnTop - BTN_GAP;
    tft.fillRoundRect(MARGIN, PANEL_Y, TFT_W - 2 * MARGIN, panelBot - PANEL_Y, 6, 0x18C3);

    tft.setTextColor(0xFD20); // amber
    tft.setTextSize(2);
    tft.setCursor(12, PANEL_Y + 5);
    tft.print(fitLabel(s.headline.c_str(), (TFT_W - 24) / 12));

    const int bodyY = PANEL_Y + 27;
    const int artX  = TFT_W - MARGIN - 6 - ART_W;
    renderArt(s, artX, bodyY, panelBot - 5 - bodyY);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    const int bodyLines = (panelBot - 4 - bodyY + 2) / LINE_H;
    drawWrapped(String(s.body.c_str()), 12, bodyY, artX - 8 - 12, 1, bodyLines);

    for (const auto& b : buttons) {
        if (!b.enabled) continue;
        tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, 0x2965);
        tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, 0xFD20);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(b.x + 8, b.y + b.h / 2 - 4);
        tft.print(fitLabel(b.label, (b.w - 16) / 6));
    }
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
    int rx, ry;
    if (!cyd::touchRead(rx, ry)) return;
    if (millis() - lastTouchMs < 220) return;
    lastTouchMs = millis();        // debounce every press, not only button hits
    int x = clampInt(mapTouchX(rx), 0, TFT_W - 1);
    int y = clampInt(mapTouchY(ry), 0, TFT_H - 1);
    int hit = buttonHit(x, y);
    Serial.printf("[TOUCH] raw=%d,%d -> %d,%d hit=%d\n", rx, ry, x, y, hit);
    if (hit >= 0) handleSelection(hit, s);
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
    pinMode(cyd::PIN_TFT_BL, OUTPUT);
    digitalWrite(cyd::PIN_TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);
    TFT_W = tft.width();
    TFT_H = tft.height();
    tft.fillScreen(TFT_BLACK);
    Serial.printf("[TFT] %s %dx%d\n", cyd::BOARD_NAME, TFT_W, TFT_H);

    bool touchOk = cyd::touchBegin();
    Serial.printf("[TOUCH] init=%s cs=%d irq=%d cal x=%d..%d y=%d..%d\n",
                  touchOk ? "ok" : "FAILED", cyd::PIN_TOUCH_CS, cyd::PIN_TOUCH_IRQ,
                  TOUCH_MIN_X, TOUCH_MAX_X, TOUCH_MIN_Y, TOUCH_MAX_Y);

    Serial.printf("[SD] Calling SD.begin(cs=%d)...\n", cyd::PIN_SD_CS);
    if (!cyd::sdBegin()) {
        Serial.println("[SD] FAILED - running fallback world");
        bootMessage = "SD init failed - fallback world";
    } else {
        Serial.printf("[SD] OK type=%d size=%lluMB\n", (int)SD.cardType(),
                      SD.cardSize() / (1024ULL * 1024ULL));
        int bmpCount = 0;
        if (File art = SD.open("/art")) {
            for (File f = art.openNextFile(); f; f = art.openNextFile()) {
                String n = f.name();
                n.toLowerCase();
                if (n.endsWith(".bmp")) ++bmpCount;
                f.close();
            }
            art.close();
        }
        Serial.printf("[SD] /art: %d .bmp files; /data/locations.csv %s\n", bmpCount,
                      SD.exists("/data/locations.csv") ? "present" : "MISSING");
        sdMounted = true;
        loadGameContent();
    }

    game.startGame();

    // Serial state dump so you can probe the game without the display
    {
        Snapshot s = game.snapshot();
        Serial.printf("[GAME] mode=%d loc=%s\n", (int)s.mode,
                      s.locationName.c_str());
        Serial.printf("[GAME] headline=%s\n", s.headline.c_str());
        Serial.printf("[GAME] food=%d ammo=%d med=%d\n",
                      s.resources.food, s.resources.ammunition,
                      s.resources.medicine);
        Serial.printf("[GAME] repJ=%d repF=%d repM=%d\n",
                      s.resources.repJanis, s.resources.repFriday,
                      s.resources.repMason);
        Serial.printf("[GAME] options=%zu\n", s.optionLabels.size());
        for (size_t i = 0; i < s.optionLabels.size(); ++i)
            Serial.printf("[GAME]   [%zu] %s\n", i, s.optionLabels[i].c_str());
        Serial.printf("[GAME] bootMsg=%s\n", bootMessage.c_str());
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
#ifdef CYD_TOUCH_DEBUG
    static uint32_t lastDbg = 0;
    if (millis() - lastDbg > 250) { lastDbg = millis(); cyd::touchDebugDump(); }
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 1000) { lastBeat = millis(); Serial.printf("[BEAT] %lus\n", millis() / 1000); }
#endif
    pollTouch(s);
    delay(25);
}
