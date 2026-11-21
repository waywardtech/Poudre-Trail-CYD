// Host emulator for Poudre Trail: compiles the real src/main.cpp (UI, layout, art
// decoding, input handling) against software shims and drives it through scripted
// game states, writing each rendered frame as a PPM. See build.sh / screenshots.sh.
#include <sys/stat.h>
#include "Arduino.h"
#include "SPI.h"
#include "SD.h"
#include "glcdfont.c"       // the 5x7 GLCD font TFT_eSPI uses for font 1

uint32_t g_millis = 0;
uint32_t g_random = 0xC0FFEE;
SerialShim Serial;
EspShim ESP;
SPIClass SPI;
SDClass SD;
std::string g_dataRoot, g_savesRoot;

#include "TFT_eSPI.h"
void TFT_eSPI::drawChar(unsigned char ch) {
    const unsigned char* g = &font[ch * 5];
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 8; ++j)
            if (g[i] & (1 << j)) fillRect(cx + i * tsize, cy + j * tsize, tsize, tsize, fg);
    cx += 6 * tsize;
}

#include "../../src/main.cpp"

namespace cyd {
bool lgfxTouchBegin(int, int, int, int) { return true; }
bool lgfxTouchRaw(int&, int&, int&) { return false; }
}

static std::string g_out;

static void dump(const char* name) {
    std::string path = g_out + "/" + name + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", tft.W, tft.H);
    for (uint16_t c : tft.fb) {
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                                (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    std::printf("captured %s\n", name);
}

// Render the current state; for animated scenes advance a few GIF frames first.
static void snap(const char* name, int frames = 3) {
    needsRender = true;
    loop();
    for (int i = 0; i < frames; ++i) { g_millis += 200; loop(); }
    dump(name);
}
static void press(int idx) { Snapshot s = game.snapshot(); handleSelection(idx, s); }

static void jump(const char* save) { game.loadSaveText(save); needsRender = true; }

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s outdir datadir savesdir\n", argv[0]); return 2; }
    g_out = argv[1]; g_dataRoot = argv[2]; g_savesRoot = argv[3];
    mkdir(g_out.c_str(), 0755); mkdir(g_savesRoot.c_str(), 0755);

    setup();
    snap("title");                                   // title screen
    press(0);                                        // Start Journey
    snap("event-janis");                             // first event, portrait
    press(0);
    snap("result");                                  // outcome with resource changes

    jump("food=92\nammunition=18\nmedicine=2\nhealth=82\nmorale=68\nhorseCondition=88\n"
         "tradeGoods=5\nrepJanis=4\nrepFriday=0\nrepMason=0\nrouteIndex=2\nday=6\n"
         "flags=met_janis,willow_guide,map_questioned\nseen=janis_store,ames_survey,canyon_willow\nrng=12345\n");
    snap("event-friday");                            // Chief Friday at Council Tree

    jump("food=70\nammunition=15\nmedicine=2\nhealth=78\nmorale=60\nhorseCondition=72\n"
         "tradeGoods=4\nrepJanis=4\nrepFriday=8\nrepMason=0\nrouteIndex=3\nday=9\n"
         "flags=met_janis,willow_guide,heard_friday,friday_names\n"
         "seen=janis_store,ames_survey,canyon_willow,friday_counsel,friday_river,virginia_dale_station,old_point\nrng=777\n");
    snap("event-flood");                             // timed flood, animated scene

    jump("food=70\nammunition=15\nmedicine=2\nhealth=78\nmorale=60\nhorseCondition=72\n"
         "tradeGoods=4\nrepJanis=4\nrepFriday=8\nrepMason=0\nrouteIndex=0\nday=1\n"
         "flags=met_janis\nseen=janis_store,ames_survey\nrng=5\n");
    snap("travel");                                  // travel menu (2x2 grid)
    press(2);
    snap("camp");                                    // camp: rest / hunt

    press(3);                                        // Back
    jump("food=70\nammunition=15\nmedicine=2\nhealth=78\nmorale=60\nhorseCondition=72\n"
         "tradeGoods=4\nrepJanis=4\nrepFriday=8\nrepMason=0\nrouteIndex=4\nday=11\n"
         "flags=met_janis,heard_friday,friday_names\nseen=high_water,janis_store,ames_survey,mason_reveal,mason_point,mason_witness\nrng=9\n");
    showMap = true;
    renderMap(game.snapshot());
    dump("map");                                     // trail map overlay
    showMap = false;

    jump("food=60\nammunition=12\nmedicine=1\nhealth=74\nmorale=70\nhorseCondition=66\n"
         "tradeGoods=3\nrepJanis=4\nrepFriday=9\nrepMason=0\nrouteIndex=5\nday=13\n"
         "flags=met_janis,heard_friday,friday_names,helped_flood\nseen=high_water\nrng=42\n");
    snap("event-finale");                            // the choice about the map
    press(0);
    press(0);                                        // continue -> epilogue
    snap("ending");
    bodyPage = 1;
    snap("ending-page2", 0);                         // long text is paged
    return 0;
}
