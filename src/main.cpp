// Poudre Trail - CYD main sketch
// Boards: ESP32-2432S028R (2.8" ILI9341 320x240) and ESP32-3248S035R (3.5" ST7796 480x320)
// Display: TFT_eSPI   Touch: XPT2046   Storage: SD card (shared SPI bus)
//
// The whole game is compiled in (include/world_data.hpp), so it runs with no SD card.
// Optional SD layout, which overrides the built-in content when present and valid:
//   /art/*.gif|*.png      scene animations (GIF) and portraits (PNG), 96x72
//   /data/locations.csv  /data/events.csv  /data/trades.csv
//   /data/events/*.txt    event body text
//   /saves/slot1.sav      save data

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <AnimatedGIF.h>
#include <PNGdec.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <vector>
#include "poudre_trail_engine.hpp"

// --- Pins (shared SPI bus SCK=14 MISO=12 MOSI=13; TFT CS=15) -----------------
static constexpr int PIN_TFT_BL    = 21;   // 2.8" backlight (3.5" boards use 27)
static constexpr int PIN_TFT_BL_35 = 27;
static constexpr int PIN_TOUCH_CS  = 33;
static constexpr int PIN_TOUCH_IRQ = 36;
static constexpr int PIN_SD_CS     = 5;

// Touch calibration (raw ADC range of the XPT2046); flip if taps land mirrored.
static constexpr int  TOUCH_MIN_X = 240;
static constexpr int  TOUCH_MAX_X = 3850;
static constexpr int  TOUCH_MIN_Y = 240;
static constexpr int  TOUCH_MAX_Y = 3850;
static constexpr bool TOUCH_FLIP_X = false;
static constexpr bool TOUCH_FLIP_Y = false;

static constexpr const char* SAVE_PATH = "/saves/slot1.sav";

// Palette (RGB565)
static constexpr uint16_t COL_BG      = 0x0000;
static constexpr uint16_t COL_BAR     = 0x2965;
static constexpr uint16_t COL_PANEL   = 0x18C3;
static constexpr uint16_t COL_AMBER   = 0xFD20;
static constexpr uint16_t COL_TEAL    = 0x5E9B;
static constexpr uint16_t COL_WARN    = 0xFFE0;
static constexpr uint16_t COL_DANGER  = 0xF800;
static constexpr uint16_t COL_DIM     = 0x8410;

// --- Globals ----------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
GameEngine game;

static int  TFT_W = 320, TFT_H = 240;
static bool sdMounted = false;

struct Button { int16_t x, y, w, h; String label; bool enabled = false; };
static Button   buttons[4];
static uint32_t lastTouchMs   = 0;
static bool     needsRender   = true;
static bool     showMap       = false;
static String   bootMessage   = "Booting...";
static String   transientMsg;
static uint32_t transientUntil = 0;
static char     sbuf[96];

// --- Layout (computed once the panel size is known) -------------------------
struct Layout {
    bool big;
    int  uiFont, lh;
    int  titleH, statusY, statusH;
    int  panelY, panelH;
    int  btnTop, btnH, btnGap;
    int  footY, footH;
    int  artX, artY, artW, artH, artSrcW, artSrcH;
    int  hlX, hlY, hlW;
    int  bodyX, bodyY, bodyW, bodyLines;
} L;

static void computeLayout() {
    L.big    = TFT_W >= 400;
    L.uiFont = L.big ? 2 : 1;
    L.lh     = L.big ? 18 : 10;
    L.titleH = L.big ? 30 : 22;
    int rows = L.big ? 2 : 3;
    L.statusY = L.titleH + 1;
    L.statusH = rows * L.lh + 6;
    L.panelY  = L.statusY + L.statusH + 2;
    L.footH   = L.big ? 20 : 12;
    L.footY   = TFT_H - L.footH;
    L.btnH    = L.big ? 20 : 15;
    L.btnGap  = L.big ? 2 : 3;
    int btnArea = 4 * L.btnH + 3 * L.btnGap;
    L.btnTop  = L.footY - 2 - btnArea;
    L.panelH  = L.btnTop - 3 - L.panelY;

    L.artSrcW = 96; L.artSrcH = 72;
    L.artW = L.big ? 144 : 96;                 // big panels draw art at 3/2 scale
    L.artH = L.big ? 108 : 72;
    L.artX = TFT_W - 6 - 4 - L.artW;
    L.artY = L.big ? L.panelY + 4 : L.panelY + 20;

    L.hlX = 12; L.hlY = L.panelY + 4;
    L.hlW = L.big ? L.artX - 8 - L.hlX : TFT_W - 24;
    L.bodyX = 12;
    L.bodyY = L.panelY + (L.big ? 26 : 20);
    L.bodyW = L.artX - 8 - L.bodyX;
    L.bodyLines = (L.panelY + L.panelH - 3 - L.bodyY) / L.lh;
}

// --- Helpers ----------------------------------------------------------------
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
    transientUntil = millis() + 1800;
    needsRender    = true;
}

// --- Buttons ----------------------------------------------------------------
static void buildButtons(const Snapshot& s) {
    for (auto& b : buttons) b = Button{};
    int count = (int)s.optionLabels.size();
    if (count > 4) count = 4;
    for (int i = 0; i < count; ++i) {
        buttons[i].x       = 6;
        buttons[i].y       = L.btnTop + i * (L.btnH + L.btnGap);
        buttons[i].w       = TFT_W - 12;
        buttons[i].h       = L.btnH;
        buttons[i].label   = s.optionLabels[i].c_str();
        buttons[i].enabled = true;
    }
}

static int buttonHit(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        const auto& b = buttons[i];
        if (b.enabled && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
}

// --- Art: GIF (animated scenes) and PNG (portraits), decoded from RAM -------
// Art is authored at 96x72. Big panels draw it at 3/2 scale (nearest neighbour).

static AnimatedGIF* gifDec   = nullptr;
static PNG*         pngDec   = nullptr;
static uint8_t*     artBuf   = nullptr;
static size_t       artLen   = 0;
static bool         artIsGif = false;
static bool         artReady = false;
static uint32_t     artNextFrameMs = 0;
static String       artReason;

static void freeArt() {
    if (gifDec) { gifDec->close(); delete gifDec; gifDec = nullptr; }
    if (pngDec) { delete pngDec; pngDec = nullptr; }
    if (artBuf) { free(artBuf); artBuf = nullptr; }
    artLen = 0; artReady = false; artIsGif = false;
}

static void pushRuns(int x, int y, int w, const uint16_t* px, const uint8_t* transparent) {
    int i = 0;
    while (i < w) {
        if (transparent && transparent[i]) { ++i; continue; }
        int j = i;
        while (j < w && !(transparent && transparent[j])) ++j;
        tft.pushImage(x + i, y, j - i, 1, const_cast<uint16_t*>(px + i));
        i = j;
    }
}

// Draw one source line (width w starting at column x0) at 1x or 3/2 scale.
static void emitSrcLine(int srcY, int x0, int w, const uint16_t* px, const uint8_t* tr) {
    if (srcY < 0 || srcY >= L.artSrcH || x0 + w > L.artSrcW) return;
    if (!L.big) {
        pushRuns(L.artX + x0, L.artY + srcY, w, px, tr);
        return;
    }
    static uint16_t dpx[160];
    static uint8_t  dtr[160];
    int dx0 = (x0 * 3 + 1) / 2;
    int dx1 = ((x0 + w) * 3 + 1) / 2;          // exclusive
    int n = dx1 - dx0;
    if (n <= 0 || n > 160) return;
    for (int i = 0; i < n; ++i) {
        int sx = ((dx0 + i) * 2) / 3 - x0;
        if (sx < 0) sx = 0;
        if (sx >= w) sx = w - 1;
        dpx[i] = px[sx];
        dtr[i] = tr ? tr[sx] : 0;
    }
    int dy0 = (srcY * 3 + 1) / 2;
    int dy1 = ((srcY + 1) * 3 + 1) / 2;        // exclusive
    for (int dy = dy0; dy < dy1; ++dy)
        pushRuns(L.artX + dx0, L.artY + dy, n, dpx, tr ? dtr : nullptr);
}

static void gifDraw(GIFDRAW* d) {
    static uint16_t px[104];
    static uint8_t  tr[104];
    int w = d->iWidth;
    if (d->iX + w > L.artSrcW) w = L.artSrcW - d->iX;
    if (w <= 0 || w > 104) return;
    uint8_t*  s   = d->pPixels;
    uint16_t* pal = d->pPalette;
    bool hasT = d->ucHasTransparency;
    if (d->ucDisposalMethod == 2) {            // restore to background colour
        for (int x = 0; x < w; ++x)
            if (s[x] == d->ucTransparent) s[x] = d->ucBackground;
        hasT = false;
    }
    for (int x = 0; x < w; ++x) {
        px[x] = pal[s[x]];
        tr[x] = hasT && s[x] == d->ucTransparent;
    }
    emitSrcLine(d->iY + d->y, d->iX, w, px, tr);
}

static int pngDraw(PNGDRAW* d) {
    static uint16_t px[104];
    if (d->iWidth > 104) return 0;
    pngDec->getLineAsRGB565(d, px, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    emitSrcLine(d->y, 0, d->iWidth, px, nullptr);
    return 1;
}

static bool loadFileToRam(const String& path) {
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    size_t n = f.size();
    if (n == 0 || n > 90000) { f.close(); artReason = "Art too large"; return false; }
    artBuf = (uint8_t*)malloc(n);
    if (!artBuf) { f.close(); artReason = "Out of memory"; return false; }
    size_t got = f.read(artBuf, n);
    f.close();
    if (got != n) { free(artBuf); artBuf = nullptr; return false; }
    artLen = n;
    return true;
}

static void drawArtPlaceholder() {
    tft.fillRect(L.artX, L.artY, L.artW, L.artH, COL_BAR);
    tft.drawRect(L.artX, L.artY, L.artW, L.artH, COL_DIM);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM);
    tft.setCursor(L.artX + 8, L.artY + L.artH / 2 - L.lh);
    tft.print("No art");
    tft.setCursor(L.artX + 8, L.artY + L.artH / 2);
    tft.print(artReason.length() ? artReason : String("unavailable"));
}

// Look for the file under the given name, then with the other image extension.
static bool openArt(const String& path) {
    int dot = path.lastIndexOf('.');
    String stem = dot > 0 ? path.substring(0, dot) : path;
    const char* exts[] = {nullptr, ".gif", ".png"};
    for (const char* e : exts) {
        String p = String("/") + (e ? stem + e : path);
        if (loadFileToRam(p)) { artIsGif = p.endsWith(".gif"); return true; }
    }
    return false;
}

static void startArt(const std::string& imagePath) {
    freeArt();
    artReason = "";
    tft.fillRect(L.artX - 3, L.artY - 3, L.artW + 6, L.artH + 6, 0x1082);
    if (imagePath.empty()) { artReason = "none"; drawArtPlaceholder(); return; }
    if (!sdMounted) { artReason = "no SD card"; drawArtPlaceholder(); return; }
    if (!openArt(String(imagePath.c_str()))) {
        if (!artReason.length()) artReason = "file missing";
        drawArtPlaceholder();
        return;
    }
    if (artIsGif) {
        gifDec = new AnimatedGIF();
        gifDec->begin(GIF_PALETTE_RGB565_LE);
        if (!gifDec->open(artBuf, (int)artLen, gifDraw)) {
            artReason = "bad GIF"; freeArt(); drawArtPlaceholder(); return;
        }
        int delayMs = 0;
        gifDec->playFrame(false, &delayMs);
        artNextFrameMs = millis() + (delayMs > 20 ? delayMs : 120);
        artReady = true;
    } else {
        pngDec = new PNG();
        int rc = pngDec->openRAM(artBuf, (int)artLen, pngDraw);
        if (rc != PNG_SUCCESS) { artReason = "bad PNG"; freeArt(); drawArtPlaceholder(); return; }
        pngDec->decode(nullptr, 0);
        pngDec->close();
        artReady = true;
    }
}

static void tickArt() {
    if (!artReady || !artIsGif || !gifDec || showMap) return;
    uint32_t now = millis();
    if ((int32_t)(now - artNextFrameMs) < 0) return;
    int delayMs = 0;
    int more = gifDec->playFrame(false, &delayMs);
    if (more <= 0) gifDec->reset();
    artNextFrameMs = millis() + (delayMs > 20 ? delayMs : 120);
}

// --- Text paging ------------------------------------------------------------
static std::vector<String> bodyLinesAll;
static int    bodyDeltaStart = -1;     // first line of the trailing "+/-" summary paragraph
static int    bodyPage = 0, bodyPages = 1, bodyPerPage = 1;
static String bodySig;

static void wrapBody(const String& text, int maxW) {
    bodyLinesAll.clear();
    bodyDeltaStart = -1;
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    int n = text.length(), start = 0;
    int lastNl = text.lastIndexOf('\n');
    while (start <= n) {
        int nl = text.indexOf('\n', start);
        String p = nl < 0 ? text.substring(start) : text.substring(start, nl);
        if (lastNl >= 0 && start == lastNl + 1) bodyDeltaStart = (int)bodyLinesAll.size();
        String line;
        int pos = 0, plen = p.length();
        while (pos < plen) {
            int sp = p.indexOf(' ', pos);
            String word = sp < 0 ? p.substring(pos) : p.substring(pos, sp);
            String cand = line.length() ? line + " " + word : word;
            if (line.length() == 0 || tft.textWidth(cand) <= maxW) line = cand;
            else { bodyLinesAll.push_back(line); line = word; }
            pos = sp < 0 ? plen : sp + 1;
        }
        if (line.length() || plen == 0) bodyLinesAll.push_back(line);
        if (nl < 0) break;
        start = nl + 1;
    }
    int total = (int)bodyLinesAll.size();
    bodyPerPage = L.bodyLines;
    bodyPages = 1;
    if (total > bodyPerPage) {
        bodyPerPage = L.bodyLines - 1;               // last row of the panel shows the page marker
        bodyPages = (total + bodyPerPage - 1) / bodyPerPage;
    }
}

// --- Rendering --------------------------------------------------------------
static void printStat(const char* label, int value, int warnBelow, int dangerBelow) {
    uint16_t c = value < dangerBelow ? COL_DANGER : (value < warnBelow ? COL_WARN : TFT_WHITE);
    tft.setTextColor(c);
    snprintf(sbuf, sizeof(sbuf), "%s%d ", label, value);
    tft.print(sbuf);
}

static void renderTitleBar(const Snapshot& s) {
    tft.fillRect(0, 0, TFT_W, L.titleH, COL_BAR);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.setCursor(8, L.big ? 7 : 4);
    tft.print("Poudre Trail");
    // Map chip (tap the title bar to open the trail map)
    int cw = L.big ? 52 : 34, ch = L.big ? 20 : 14;
    int cx = TFT_W - cw - 6, cy = (L.titleH - ch) / 2;
    tft.drawRoundRect(cx, cy, cw, ch, 4, COL_AMBER);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    tft.setTextColor(COL_AMBER);
    tft.setCursor(cx + (L.big ? 12 : 5), cy + (L.big ? 2 : 3));
    tft.print("Map");
    if (L.big) {
        snprintf(sbuf, sizeof(sbuf), "%s - %s", s.dateLabel.c_str(), s.locationName.c_str());
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(220, 6);
        tft.print(sbuf);
    }
}

static void renderStatus(const Snapshot& s) {
    tft.fillRect(0, L.statusY, TFT_W, L.statusH, COL_PANEL);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    int y = L.statusY + 3;
    if (!L.big) {
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(6, y);
        snprintf(sbuf, sizeof(sbuf), "%s  Day %d  %s", s.dateLabel.c_str(), s.day, s.locationName.c_str());
        tft.print(sbuf);
        y += L.lh;
    }
    tft.setCursor(6, y);
    const Resources& r = s.resources;
    printStat("Food ", r.food, 35, 18);
    printStat("Ammo ", r.ammunition, 6, 3);
    printStat("Med ", r.medicine, 2, 1);
    printStat("Goods ", r.tradeGoods, 2, 1);
    if (L.big) printStat("Day ", s.day, 0, 0);
    y += L.lh;
    tft.setCursor(6, y);
    printStat("Horses ", r.horseCondition, 40, 20);
    printStat("Morale ", r.morale, 35, 15);
    printStat("Health ", r.health, 40, 20);
    y += L.lh;
    tft.setTextColor(COL_TEAL);
    if (!L.big) {
        tft.setCursor(6, y);
        snprintf(sbuf, sizeof(sbuf), "Janis:%s Friday:%s Mason:%s",
                 s.reputationSummaryJanis.c_str(), s.reputationSummaryFriday.c_str(),
                 s.reputationSummaryMason.c_str());
        tft.print(sbuf);
    } else {
        snprintf(sbuf, sizeof(sbuf), "Janis: %s   Friday: %s   Mason: %s",
                 s.reputationSummaryJanis.c_str(), s.reputationSummaryFriday.c_str(),
                 s.reputationSummaryMason.c_str());
        tft.setCursor(TFT_W - tft.textWidth(sbuf) - 8, L.statusY + 3);
        tft.print(sbuf);
    }
}

static void renderPanel(const Snapshot& s) {
    tft.fillRoundRect(6, L.panelY, TFT_W - 12, L.panelH, 6, COL_PANEL);

    // Headline: 2x GLCD if it fits, otherwise the UI font
    String hl = s.headline.c_str();
    tft.setTextColor(COL_AMBER);
    tft.setTextFont(1);
    tft.setTextSize(2);
    if (tft.textWidth(hl) <= L.hlW) {
        tft.setCursor(L.hlX, L.hlY);
        tft.print(hl);
    } else {
        tft.setTextFont(L.uiFont);
        tft.setTextSize(1);
        tft.setCursor(L.hlX, L.hlY + (L.big ? 2 : 4));
        tft.print(hl);
    }

    // Body, paged
    String sig = String(s.headline.c_str()) + "|" + String((unsigned)s.body.length());
    if (sig != bodySig) { bodySig = sig; bodyPage = 0; }
    wrapBody(String(s.body.c_str()), L.bodyW);
    if (bodyPage >= bodyPages) bodyPage = 0;
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    int first = bodyPage * bodyPerPage;
    for (int i = 0; i < bodyPerPage && first + i < (int)bodyLinesAll.size(); ++i) {
        int idx = first + i;
        tft.setTextColor(bodyDeltaStart >= 0 && idx >= bodyDeltaStart ? COL_TEAL : TFT_WHITE);
        tft.setCursor(L.bodyX, L.bodyY + i * L.lh);
        tft.print(bodyLinesAll[idx]);
    }
    if (bodyPages > 1) {
        snprintf(sbuf, sizeof(sbuf), "tap text for more  %d/%d", bodyPage + 1, bodyPages);
        tft.setTextColor(COL_DIM);
        tft.setCursor(L.bodyX, L.bodyY + bodyPerPage * L.lh);
        tft.print(sbuf);
    }
}

static void renderButtons(const Snapshot& s) {
    buildButtons(s);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    for (const auto& b : buttons) {
        if (!b.enabled) continue;
        tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, COL_BAR);
        tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, COL_AMBER);
        tft.setTextColor(TFT_WHITE);
        String lbl = b.label;
        int maxW = b.w - 16;
        bool cut = false;
        while (lbl.length() > 4 && tft.textWidth(lbl) > maxW) { lbl.remove(lbl.length() - 1); cut = true; }
        if (cut) { lbl.remove(lbl.length() - 2); lbl += ".."; }
        tft.setCursor(b.x + 8, b.y + (b.h - tft.fontHeight()) / 2);
        tft.print(lbl);
    }
}

static void renderFooter(const Snapshot& s) {
    tft.fillRect(0, L.footY, TFT_W, L.footH, COL_BAR);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    String foot = (transientUntil > millis()) ? transientMsg : String(s.footer.c_str());
    while (foot.length() > 3 && tft.textWidth(foot) > TFT_W - 8) foot.remove(foot.length() - 1);
    tft.setCursor(4, L.footY + (L.footH - tft.fontHeight()) / 2);
    tft.print(foot);
}

static void renderSnapshot(const Snapshot& s) {
    tft.fillScreen(COL_BG);
    renderTitleBar(s);
    renderStatus(s);
    renderPanel(s);
    renderButtons(s);
    renderFooter(s);
    startArt(s.imagePath);
}

// Stylised trail map (not to scale): river, route, and where the party is.
static void renderMap(const Snapshot& s) {
    freeArt();
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, TFT_W, L.titleH, COL_BAR);
    tft.setTextColor(TFT_WHITE);
    tft.setTextFont(1); tft.setTextSize(2);
    tft.setCursor(8, L.big ? 7 : 4);
    tft.print("Trail Map");
    tft.setTextFont(L.uiFont); tft.setTextSize(1);
    snprintf(sbuf, sizeof(sbuf), "%s", s.dateLabel.c_str());
    tft.setCursor(TFT_W - tft.textWidth(sbuf) - 8, L.big ? 6 : 8);
    tft.print(sbuf);

    const int mx = 4, my = L.titleH + 3, mw = TFT_W - 8, mh = L.footY - my - 3;
    tft.fillRoundRect(mx, my, mw, mh, 6, 0x2A02);                       // dry-grass land
    auto X = [&](int pct) { return mx + pct * mw / 100; };
    auto Y = [&](int pct) { return my + pct * mh / 100; };

    // foothills along the west edge
    for (int i = 0; i < 5; ++i) {
        int bx = X(i * 5), by = Y(88 - (i % 2) * 8), w = mw / 9;
        tft.fillTriangle(bx, by, bx + w, by, bx + w / 2, Y(18 + (i * 11) % 22), 0x4A49);
    }

    // the river
    static const int8_t rv[][2] = {{0,28},{12,30},{24,35},{36,42},{44,52},{54,58},{66,53},{78,60},{90,54},{100,49}};
    for (size_t i = 0; i + 1 < sizeof(rv) / sizeof(rv[0]); ++i)
        for (int o = -1; o <= 1; ++o)
            tft.drawLine(X(rv[i][0]), Y(rv[i][1]) + o, X(rv[i+1][0]), Y(rv[i+1][1]) + o, 0x3D7F);

    // route + stops
    const auto& route = game.route();
    int cur = (int)game.routeIndex();
    for (size_t i = 0; i + 1 < route.size(); ++i) {
        uint16_t c = (int)i < cur ? COL_AMBER : COL_DIM;
        tft.drawLine(X(route[i].mapX), Y(route[i].mapY), X(route[i+1].mapX), Y(route[i+1].mapY), c);
    }
    for (size_t i = 0; i < route.size(); ++i) {
        int px = X(route[i].mapX), py = Y(route[i].mapY);
        uint16_t c = (int)i <= cur ? COL_AMBER : COL_DIM;
        tft.fillCircle(px, py, L.big ? 5 : 3, c);
        if ((int)i == cur) tft.drawCircle(px, py, L.big ? 9 : 6, TFT_WHITE);
        tft.setTextColor((int)i == cur ? TFT_WHITE : c);
        String nm = route[i].name.c_str();
        int tw = tft.textWidth(nm);
        int tx = clampInt(px - tw / 2, mx + 2, mx + mw - tw - 2);
        int ty = py + (L.big ? 12 : 7);
        if (ty + tft.fontHeight() > my + mh - 2) ty = py - (L.big ? 12 : 7) - tft.fontHeight();
        tft.setCursor(tx, ty);
        tft.print(nm);
    }

    tft.fillRect(0, L.footY, TFT_W, L.footH, COL_BAR);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(4, L.footY + (L.footH - tft.fontHeight()) / 2);
    tft.print("Stylised, not to scale.  Tap to close.");
}

// --- Save / Load ------------------------------------------------------------
static bool saveGame() {
    if (!sdMounted) return false;
    SD.mkdir("/saves");
    return writeTextFile(SAVE_PATH, String(game.serializeSave().c_str()));
}

static bool loadGame(String* msgOut = nullptr) {
    String data = sdMounted ? readTextFile(SAVE_PATH) : String();
    std::string err;
    bool ok = !data.isEmpty() && game.loadSaveText(toStd(data), &err);
    if (msgOut) *msgOut = ok ? "Save loaded" : (sdMounted ? "No save found" : "No SD card");
    return ok;
}

// --- Input ------------------------------------------------------------------
static void handleSelection(int idx, const Snapshot& s) {
    if (idx < 0) return;
    String m;
    switch (s.mode) {
    case GameMode::TITLE:
        if (idx == 0) { game.seed(esp_random()); game.startGame(); }
        else if (idx == 1) { loadGame(&m); setTransient(m); }
        break;
    case GameMode::TRAVEL:
        if (idx == 0) game.travel();
        else if (idx == 1) game.openTrade();
        else if (idx == 2) game.openCamp();
        else if (idx == 3) setTransient(saveGame() ? "Game saved to SD" : "Save failed (no SD?)");
        break;
    case GameMode::CAMP:
        if (idx == 2) { loadGame(&m); setTransient(m); }
        else game.chooseOption(idx);
        break;
    case GameMode::EVENT:
    case GameMode::RESULT:
    case GameMode::TRADE:
        game.chooseOption(idx);
        break;
    case GameMode::VICTORY:
    case GameMode::GAME_OVER:
        if (idx == 0) { game.seed(esp_random()); game.startGame(); }
        else if (idx == 1) { loadGame(&m); setTransient(m); }
        break;
    }
    needsRender = true;
}

static void pollTouch(const Snapshot& s) {
    if (!touch.touched()) return;
    if (millis() - lastTouchMs < 250) return;
    TS_Point p = touch.getPoint();
    int x = clampInt(mapTouchX(p.x), 0, TFT_W - 1);
    int y = clampInt(mapTouchY(p.y), 0, TFT_H - 1);
    Serial.printf("[TOUCH] raw=%d,%d -> %d,%d\n", p.x, p.y, x, y);
    lastTouchMs = millis();

    if (showMap) { showMap = false; needsRender = true; return; }
    if (y < L.titleH) { showMap = true; renderMap(s); return; }
    int hit = buttonHit(x, y);
    if (hit >= 0) { handleSelection(hit, s); return; }
    if (y >= L.panelY && y < L.panelY + L.panelH && bodyPages > 1) {
        bodyPage = (bodyPage + 1) % bodyPages;
        needsRender = true;
    }
}

// --- SD content (optional override of the built-in world) -------------------
static void loadGameContent() {
    String locs   = readTextFile("/data/locations.csv");
    String events = readTextFile("/data/events.csv");
    String trades = readTextFile("/data/trades.csv");
    if (locs.isEmpty() || events.isEmpty()) {
        bootMessage = "SD ready - built-in story";
        return;
    }
    auto reader = [](const std::string& path) {
        return toStd(readTextFile((String("/data/") + path.c_str()).c_str()));
    };
    std::string err;
    bool ok = game.loadFromText(toStd(locs), toStd(events), toStd(trades), reader, &err);
    bootMessage = ok ? "SD content loaded" : String("SD content ignored: ") + err.c_str();
    if (!ok) game.loadEmbeddedWorld();
}

// --- Setup / Loop -----------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(PIN_TFT_BL, OUTPUT);    digitalWrite(PIN_TFT_BL, HIGH);
    pinMode(PIN_TFT_BL_35, OUTPUT); digitalWrite(PIN_TFT_BL_35, HIGH);

    tft.init();
    tft.setRotation(1);
    TFT_W = tft.width();
    TFT_H = tft.height();
    computeLayout();
    tft.fillScreen(COL_BG);
    Serial.printf("[TFT] %dx%d big=%d\n", TFT_W, TFT_H, (int)L.big);

    touch.begin();
    touch.setRotation(1);
    // touch.begin() calls SPI.begin() with default pins; put the shared bus back.
    SPI.begin(14, 12, 13, -1);
    delay(10);

    Serial.printf("[SD] Calling SD.begin(cs=%d)...\n", PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
        Serial.println("[SD] FAILED - built-in story, no art");
        bootMessage = "No SD card - built-in story, no art";
    } else {
        Serial.println("[SD] OK");
        sdMounted = true;
        bootMessage = "SD ready";
        loadGameContent();
    }
    game.seed(esp_random());

    Serial.printf("[GAME] route=%u stops, boot=%s, heap=%u\n",
                  (unsigned)game.route().size(), bootMessage.c_str(), (unsigned)ESP.getFreeHeap());

    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(24, TFT_H / 2 - 20); tft.print("Poudre Trail");
    tft.setTextSize(1);
    tft.setCursor(24, TFT_H / 2 + 4);  tft.print(bootMessage);
    delay(1000);
    needsRender = true;
}

void loop() {
    Snapshot s = game.snapshot();
    bool transientExpired = transientUntil > 0 && millis() > transientUntil;
    if (!showMap && (needsRender || transientExpired)) {
        if (transientExpired) transientUntil = 0;
        renderSnapshot(s);
        needsRender = false;
    }
    pollTouch(s);
    tickArt();
    delay(15);
}
