// Poudre Trail - CYD main sketch
// Boards: ESP32-2432S028R (2.8" 320x240, default) and ESP32-3248S035R (3.5" 480x320).
// Display: TFT_eSPI.  Pins, touch (LovyanGFX XPT2046 driver on the 2.8") and SD bring-up
// per board live in include/cyd_display.h; the 2.8" pin map is from CYD-Diagnostic.
//
// The whole game is compiled in (include/world_data.hpp), so it runs with no SD card.
// Optional SD layout, which overrides the built-in content when present and valid:
//   /art/*.gif|*.png      scene animations (GIF) and portraits (PNG), 96x72
//   /data/locations.csv  /data/events.csv  /data/trades.csv
//   /data/events/*.txt    event body text
//   /saves/slot1.sav      save data

#include <Arduino.h>
#include <new>
#include <SPI.h>
#include <SD.h>
#include <AnimatedGIF.h>
#include <PNGdec.h>
#include <TFT_eSPI.h>
#include "cyd_display.h"
#include <vector>
#include "poudre_trail_engine.hpp"

// Touch calibration (measured per board) is in include/cyd_display.h
using cyd::TOUCH_MIN_X; using cyd::TOUCH_MAX_X;
using cyd::TOUCH_MIN_Y; using cyd::TOUCH_MAX_Y;
using cyd::TOUCH_FLIP_X; using cyd::TOUCH_FLIP_Y;

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
// Built in setup(), not during static init: the constructor parses the whole
// embedded story, and a failure there would abort before Serial is up.
alignas(GameEngine) static unsigned char gameStorage[sizeof(GameEngine)];
static GameEngine& game = *reinterpret_cast<GameEngine*>(gameStorage);

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
    bool btnGrid;
    int  footLines;
    int  artVisH;
    int  footY, footH;
    int  artX, artY, artW, artH, artSrcW, artSrcH;
    int  hlX, hlY, hlW;
    int  bodyX, bodyY, bodyW, bodyLines;
} L;

static void computeLayout() {      // size-dependent parts, once
    L.big    = TFT_W >= 400;
    L.uiFont = L.big ? 2 : 1;
    L.lh     = L.big ? 18 : 10;
    L.titleH = L.big ? 30 : 22;
    int rows = L.big ? 2 : 3;
    L.statusY = L.titleH + 1;
    L.statusH = rows * L.lh + 6;
    L.panelY  = L.statusY + L.statusH + 2;
    L.btnGap  = L.big ? 3 : 4;
    L.artSrcW = 96; L.artSrcH = 72;
    L.artW = L.big ? 144 : 96;                 // big panels draw art at 3/2 scale
    L.artH = L.big ? 108 : 72;
    L.artX = TFT_W - 6 - 4 - L.artW;
    L.hlX = 12;
    L.bodyX = 12;
}

// Per-screen layout, built bottom-up: footer (1 or 2 lines), then the option
// buttons (2x2 grid for four short labels, otherwise one per row), then the
// event panel takes whatever height is left. The art is centre-cropped when the
// panel is too short for all of it.
static void layoutFor(const Snapshot& s) {
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    String foot = (transientUntil > millis()) ? transientMsg : String(s.footer.c_str());
    foot.trim();
    L.footLines = tft.textWidth(foot) > TFT_W - 8 ? 2 : 1;
    const int footLineH = L.big ? 18 : 10;
    L.footH = 4 + L.footLines * footLineH;
    L.footY = TFT_H - L.footH;

    int count = (int)s.optionLabels.size();
    if (count > 4) count = 4;
    const int fullW = TFT_W - 12, halfW = (fullW - L.btnGap) / 2;
    bool grid = count == 4;
    for (int i = 0; grid && i < count; ++i)
        if (tft.textWidth(String(s.optionLabels[i].c_str())) > halfW - 16) grid = false;
    L.btnGrid = grid;
    const int rows = grid ? 2 : count;
    L.btnH = rows <= 2 ? (L.big ? 30 : 26) : (rows == 3 ? (L.big ? 24 : 20) : (L.big ? 22 : 18));
    const int area = rows > 0 ? rows * L.btnH + (rows - 1) * L.btnGap : 0;
    L.btnTop = L.footY - L.btnGap - area;
    L.panelH = L.btnTop - L.btnGap - L.panelY;

    L.hlY = L.panelY + 4;
    L.bodyY = L.panelY + (L.big ? 26 : 20);
    L.artY = L.panelY + (L.big ? 4 : 20);
    int avail = L.panelH - (L.artY - L.panelY) - 4;
    L.artVisH = avail >= 24 ? (avail < L.artH ? avail : L.artH) : 0;
    L.hlW = (L.big && L.artVisH) ? L.artX - 8 - L.hlX : TFT_W - 24;
    L.bodyW = (L.artVisH ? L.artX - 8 : TFT_W - 6) - L.bodyX;
    L.bodyLines = (L.panelY + L.panelH - 3 - L.bodyY) / L.lh;
    if (L.bodyLines < 1) L.bodyLines = 1;
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
    const int fullW = TFT_W - 12, halfW = (fullW - L.btnGap) / 2;
    for (int i = 0; i < count; ++i) {
        const int row = L.btnGrid ? i / 2 : i;
        const int col = L.btnGrid ? i % 2 : 0;
        buttons[i].x       = 6 + col * (halfW + L.btnGap);
        buttons[i].y       = L.btnTop + row * (L.btnH + L.btnGap);
        buttons[i].w       = L.btnGrid ? halfW : fullW;
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
    if (L.artVisH <= 0 || srcY < 0 || srcY >= L.artSrcH || x0 + w > L.artSrcW) return;
    const int crop = (L.artH - L.artVisH) / 2;       // rows hidden above and below
    if (!L.big) {
        int dy = srcY - crop;
        if (dy >= 0 && dy < L.artVisH) pushRuns(L.artX + x0, L.artY + dy, w, px, tr);
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
    for (int dy = dy0; dy < dy1; ++dy) {
        int vy = dy - crop;
        if (vy >= 0 && vy < L.artVisH) pushRuns(L.artX + dx0, L.artY + vy, n, dpx, tr ? dtr : nullptr);
    }
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
    tft.fillRect(L.artX, L.artY, L.artW, L.artVisH, COL_BAR);
    tft.drawRect(L.artX, L.artY, L.artW, L.artVisH, COL_DIM);
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    tft.setTextColor(COL_DIM);
    tft.setCursor(L.artX + 8, L.artY + L.artVisH / 2 - L.lh);
    tft.print("No art");
    tft.setCursor(L.artX + 8, L.artY + L.artVisH / 2);
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
    if (L.artVisH <= 0) return;                // no room for art on this screen
    tft.fillRect(L.artX - 3, L.artY - 3, L.artW + 6, L.artVisH + 6, 0x1082);
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

static void wrapInto(std::vector<String>& out, const String& text, int maxW, int* deltaStart = nullptr) {
    out.clear();
    if (deltaStart) *deltaStart = -1;
    tft.setTextFont(L.uiFont);
    tft.setTextSize(1);
    int n = text.length(), start = 0;
    int lastNl = text.lastIndexOf('\n');
    while (start <= n) {
        int nl = text.indexOf('\n', start);
        String p = nl < 0 ? text.substring(start) : text.substring(start, nl);
        if (deltaStart && lastNl >= 0 && start == lastNl + 1) *deltaStart = (int)out.size();
        String line;
        int pos = 0, plen = p.length();
        while (pos < plen) {
            int sp = p.indexOf(' ', pos);
            String word = sp < 0 ? p.substring(pos) : p.substring(pos, sp);
            String cand = line.length() ? line + " " + word : word;
            if (line.length() == 0 || tft.textWidth(cand) <= maxW) line = cand;
            else { out.push_back(line); line = word; }
            pos = sp < 0 ? plen : sp + 1;
        }
        if (line.length() || plen == 0) out.push_back(line);
        if (nl < 0) break;
        start = nl + 1;
    }
}

static void wrapBody(const String& text, int maxW) {
    wrapInto(bodyLinesAll, text, maxW, &bodyDeltaStart);
    int total = (int)bodyLinesAll.size();
    bodyPerPage = L.bodyLines;
    bodyPages = 1;
    if (total > bodyPerPage) {
        bodyPerPage = L.bodyLines - 1;               // last row of the panel shows the page marker
        if (bodyPerPage < 1) bodyPerPage = 1;
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
    if (!L.big) {
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setCursor(160, (L.titleH - 8) / 2);
        tft.print(s.dateLabel.c_str());
    }
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
    tft.setCursor(6, y);
    const Resources& r = s.resources;
    printStat("Food ", r.food, 35, 18);
    printStat("Ammo ", r.ammunition, 6, 3);
    printStat("Med ", r.medicine, 2, 1);
    printStat("Goods ", r.tradeGoods, 2, 1);
    if (L.big) printStat("Day ", s.day, 0, 0);
    else {                                           // location, right-aligned on the first row
        String loc = s.locationName.c_str();
        tft.setTextColor(COL_AMBER);
        tft.setCursor(TFT_W - 6 - tft.textWidth(loc), y);
        tft.print(loc);
    }
    y += L.lh;
    tft.setCursor(6, y);
    printStat("Horses ", r.horseCondition, 40, 20);
    printStat("Morale ", r.morale, 35, 15);
    printStat("Health ", r.health, 40, 20);
    if (!L.big) printStat("Day ", s.day, 0, 0);
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
    foot.trim();
    std::vector<String> lines;
    wrapInto(lines, foot, TFT_W - 8);
    const int lineH = L.big ? 18 : 10;
    for (int i = 0; i < L.footLines && i < (int)lines.size(); ++i) {
        String ln = lines[i];
        if (i == L.footLines - 1 && (int)lines.size() > L.footLines) {      // did not fit: end in "..."
            while (ln.length() > 3 && tft.textWidth(ln + "...") > TFT_W - 8) ln.remove(ln.length() - 1);
            ln += "...";
        }
        tft.setCursor(4, L.footY + 3 + i * lineH);
        tft.print(ln);
    }
}

static void renderSnapshot(const Snapshot& s) {
    layoutFor(s);
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

    const int footH = L.big ? 20 : 12, footY = TFT_H - footH;
    const int mx = 4, my = L.titleH + 3, mw = TFT_W - 8, mh = footY - my - 3;
    tft.fillRoundRect(mx, my, mw, mh, 6, 0x2A02);                       // dry-grass land
    auto X = [&](int pct) { return mx + pct * mw / 100; };
    auto Y = [&](int pct) { return my + pct * mh / 100; };

    // foothills along the west edge
    for (int i = 0; i < 5; ++i) {
        int bx = X(i * 5), by = Y(88 - (i % 2) * 8), w = mw / 9;
        tft.fillTriangle(bx, by, bx + w, by, bx + w / 2, Y(18 + (i * 11) % 22), 0x4A49);
    }

    // the river
    static const int8_t rv[][2] = {{0,30},{14,36},{24,42},{32,46},{34,56},{40,64},{52,62},{66,50},{78,54},{90,58},{100,56}};
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
        bool below = px > mx + mw * 75 / 100;         // far-right stop: label underneath, clamped
        int tx = below ? px - tw / 2 : px + (L.big ? 12 : 7);
        int ty = below ? py + (L.big ? 12 : 8) : py - tft.fontHeight() / 2;
        tx = clampInt(tx, mx + 2, mx + mw - tw - 2);
        tft.setCursor(tx, ty);
        tft.print(nm);
    }

    tft.fillRect(0, footY, TFT_W, footH, COL_BAR);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(4, footY + (footH - tft.fontHeight()) / 2);
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
    if (millis() - lastTouchMs < 250) return;
    int rx, ry;
    if (!cyd::touchRead(rx, ry)) return;
    lastTouchMs = millis();                   // debounce every press, not only button hits
    int x = clampInt(mapTouchX(rx), 0, TFT_W - 1);
    int y = clampInt(mapTouchY(ry), 0, TFT_H - 1);
    Serial.printf("[TOUCH] raw=%d,%d -> %d,%d\n", rx, ry, x, y);

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
    // The parser reads columns by position, so a card from an older build
    // (different columns) would "parse" into a broken story. Only accept files
    // whose header rows match the current format.
    if (!locs.startsWith("id,name,summary,milesFromStart,hasTradingPost,image,mapX,mapY") ||
        !events.startsWith("id,locationId,title,bodyFile,image,kind,requiresFlag,")) {
        bootMessage = "SD story is an old format - built-in story";
        Serial.println("[SD] /data CSV headers do not match this build; using built-in story");
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
    Serial.printf("[MEM] before engine: free=%u maxBlock=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    new (gameStorage) GameEngine();
    Serial.printf("[MEM] after engine:  free=%u maxBlock=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    pinMode(cyd::PIN_TFT_BL, OUTPUT);
    digitalWrite(cyd::PIN_TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);        // art lines are native RGB565; the panel wants big-endian
    TFT_W = tft.width();
    TFT_H = tft.height();
    computeLayout();
    tft.fillScreen(COL_BG);
    Serial.printf("[TFT] %s %dx%d big=%d\n", cyd::BOARD_NAME, TFT_W, TFT_H, (int)L.big);

    bool touchOk = cyd::touchBegin();
    Serial.printf("[TOUCH] init=%s cs=%d irq=%d cal x=%d..%d y=%d..%d\n",
                  touchOk ? "ok" : "FAILED", cyd::PIN_TOUCH_CS, cyd::PIN_TOUCH_IRQ,
                  TOUCH_MIN_X, TOUCH_MAX_X, TOUCH_MIN_Y, TOUCH_MAX_Y);

    Serial.printf("[SD] Calling SD.begin(cs=%d)...\n", cyd::PIN_SD_CS);
    if (!cyd::sdBegin()) {
        Serial.println("[SD] FAILED - built-in story, no art");
        bootMessage = "No SD card - built-in story, no art";
    } else {
        sdMounted = true;
        bootMessage = "SD ready";
        int artCount = 0;
        if (File art = SD.open("/art")) {
            for (File f = art.openNextFile(); f; f = art.openNextFile()) {
                String n = f.name();
                n.toLowerCase();
                if (n.endsWith(".gif") || n.endsWith(".png")) ++artCount;
                f.close();
            }
            art.close();
        }
        Serial.printf("[SD] OK type=%d size=%lluMB /art: %d gif/png files; /data/locations.csv %s\n",
                      (int)SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL), artCount,
                      SD.exists("/data/locations.csv") ? "present" : "MISSING");
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
#ifdef CYD_TOUCH_DEBUG
    static uint32_t lastDbg = 0;
    if (millis() - lastDbg > 250) { lastDbg = millis(); cyd::touchDebugDump(); }
#endif
    delay(15);
}
