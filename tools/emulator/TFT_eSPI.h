// TFT_eSPI stand-in: a 16-bit software framebuffer with the calls main.cpp
// makes. Text uses the real 5x7 GLCD font shipped with TFT_eSPI (font 1), at the
// same 6x8 cell and integer scaling the library uses. Only font 1 is emulated
// (the 2.8" layout); font 2 aborts so a layout change does not go unnoticed.
#pragma once
#include "Arduino.h"

#define TFT_WHITE 0xFFFF
#define TFT_BLACK 0x0000

class TFT_eSPI {
public:
    int W = 240, H = 320;
    std::vector<uint16_t> fb;
    int cx = 0, cy = 0, tsize = 1, tfont = 1;
    uint16_t fg = 0xFFFF;

    void init() { fb.assign((size_t)W * H, 0); }
    void setRotation(int r) { if (r & 1) std::swap(W, H); fb.assign((size_t)W * H, 0); }
    void setSwapBytes(bool) {}
    int width() const { return W; }
    int height() const { return H; }

    void px(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) fb[(size_t)y * W + x] = c; }
    void fillScreen(uint16_t c) { std::fill(fb.begin(), fb.end(), c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) px(x + i, y + j, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) {
        for (int i = 0; i < w; ++i) { px(x + i, y, c); px(x + i, y + h - 1, c); }
        for (int j = 0; j < h; ++j) { px(x, y + j, c); px(x + w - 1, y + j, c); }
    }
    static bool inRound(int i, int j, int w, int h, int r) {
        int ex = i < r ? r - i - 1 : (i >= w - r ? i - (w - r) : -1);
        int ey = j < r ? r - j - 1 : (j >= h - r ? j - (h - r) : -1);
        if (ex < 0 || ey < 0) return true;
        return (ex + 0.5) * (ex + 0.5) + (ey + 0.5) * (ey + 0.5) <= (double)r * r;
    }
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
        for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) if (inRound(i, j, w, h, r)) px(x + i, y + j, c);
    }
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
        for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) {
            if (!inRound(i, j, w, h, r)) continue;
            bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1 ||
                        !inRound(i - 1, j, w, h, r) || !inRound(i + 1, j, w, h, r) ||
                        !inRound(i, j - 1, w, h, r) || !inRound(i, j + 1, w, h, r);
            if (edge) px(x + i, y + j, c);
        }
    }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
        int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
        for (;;) {
            px(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
        int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
        int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
        auto s = [](int ax, int ay, int bx, int by, int px_, int py_) { return (bx - ax) * (py_ - ay) - (by - ay) * (px_ - ax); };
        for (int y = miny; y <= maxy; ++y) for (int x = minx; x <= maxx; ++x) {
            int a = s(x0, y0, x1, y1, x, y), b = s(x1, y1, x2, y2, x, y), d = s(x2, y2, x0, y0, x, y);
            if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px(x, y, c);
        }
    }
    void fillCircle(int x, int y, int r, uint16_t c) { for (int j = -r; j <= r; ++j) for (int i = -r; i <= r; ++i) if (i * i + j * j <= r * r) px(x + i, y + j, c); }
    void drawCircle(int x, int y, int r, uint16_t c) {
        for (int j = -r - 1; j <= r + 1; ++j) for (int i = -r - 1; i <= r + 1; ++i) {
            int d = i * i + j * j;
            if (d <= r * r + r && d >= r * r - r) px(x + i, y + j, c);
        }
    }
    void pushImage(int x, int y, int w, int h, uint16_t* data) {
        for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) px(x + i, y + j, data[j * w + i]);
    }

    void setTextColor(uint16_t c) { fg = c; }
    void setTextSize(int s) { tsize = s; }
    void setTextFont(int f) { tfont = f; if (f != 1) { std::fprintf(stderr, "emulator: only font 1 is supported (got %d)\n", f); std::abort(); } }
    void setCursor(int x, int y) { cx = x; cy = y; }
    int fontHeight() const { return 8 * tsize; }
    int textWidth(const String& t) const { return (int)t.length() * 6 * tsize; }

    void print(const String& t) { for (size_t i = 0; i < t.length(); ++i) drawChar((unsigned char)t[i]); }
    void print(const char* t) { print(String(t)); }

private:
    void drawChar(unsigned char ch);
};
