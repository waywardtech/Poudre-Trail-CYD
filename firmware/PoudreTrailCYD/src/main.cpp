#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "poudre_trail_engine.hpp"
#include "ui_helpers.hpp"
#include "csv_content.hpp"

#ifndef TFT_BL
#define TFT_BL 21
#endif
#ifndef TOUCH_CS
#define TOUCH_CS 33
#endif
#ifndef TOUCH_IRQ
#define TOUCH_IRQ 36
#endif
#ifndef TOUCH_MIN_X
#define TOUCH_MIN_X 200
#endif
#ifndef TOUCH_MAX_X
#define TOUCH_MAX_X 3800
#endif
#ifndef TOUCH_MIN_Y
#define TOUCH_MIN_Y 240
#endif
#ifndef TOUCH_MAX_Y
#define TOUCH_MAX_Y 3800
#endif

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
GameEngine engine;
RenderCache gCache;
Snapshot gSnap;
bool gDataLoaded = false;
int touchMinX = TOUCH_MIN_X;
int touchMaxX = TOUCH_MAX_X;
int touchMinY = TOUCH_MIN_Y;
int touchMaxY = TOUCH_MAX_Y;

static const int MAX_EVENT_ROWS = 32;
static const int MAX_LOCATION_ROWS = 16;
CsvEventRow gEventRows[MAX_EVENT_ROWS];
CsvLocationRow gLocationRows[MAX_LOCATION_ROWS];
int gEventRowCount = 0;
int gLocationRowCount = 0;

struct Button { int x=0,y=0,w=0,h=0; String label; bool visible=false; };
Button btnNewGame, btnLoadSave, btnCalibrate, btnContinue, btnTrade, btnSave, btnPrev, btnNext, btnBack, btnRestart, btnChoices[3];
unsigned long lastTouchMs = 0;

static uint16_t bg=TFT_BLACK, panel=0x18E3, textCol=TFT_WHITE, accent=TFT_CYAN, danger=TFT_RED;

static void clearButtons(){ btnNewGame.visible=btnLoadSave.visible=btnCalibrate.visible=btnContinue.visible=btnTrade.visible=btnSave.visible=btnPrev.visible=btnNext.visible=btnBack.visible=btnRestart.visible=false; for(auto &b:btnChoices) b.visible=false; }
static void fillButton(Button& b,int x,int y,int w,int h,const String& label){ b.x=x;b.y=y;b.w=w;b.h=h;b.label=label;b.visible=true; }
static bool hit(const Button& b,int x,int y){ return b.visible && x>=b.x && x<(b.x+b.w) && y>=b.y && y<(b.y+b.h); }
static void drawButton(const Button& b,uint16_t fill,uint16_t outline=TFT_WHITE){ if(!b.visible) return; tft.fillRoundRect(b.x,b.y,b.w,b.h,6,fill); tft.drawRoundRect(b.x,b.y,b.w,b.h,6,outline); tft.setTextColor(textCol,fill); tft.setTextDatum(MC_DATUM); tft.drawString(b.label,b.x+b.w/2,b.y+b.h/2,2); tft.setTextDatum(TL_DATUM); }
static void drawStatusBar(const Snapshot& s){ tft.fillRect(0,0,320,58,panel); tft.setTextColor(textCol,panel); tft.drawString("Food:"+String(s.resources.food)+" H:"+String(s.resources.health),6,4,2); tft.drawString("J:"+s.reputationSummaryJanis,6,24,2); tft.drawString("Fr:"+s.reputationSummaryFriday,110,24,2); tft.drawString("Ma:"+s.reputationSummaryMason,220,24,2); }
static void drawWrapped(const String& text,int x,int y,int w,int lineH){ int cursorY=y,start=0,charsPerLine=max(8,w/6); while(start<text.length()){ int remaining=text.length()-start; String line; if(remaining<=charsPerLine){ line=text.substring(start); start=text.length(); } else { int cut=start+charsPerLine; int space=text.lastIndexOf(' ',cut); if(space<start){ line=text.substring(start,start+charsPerLine-1)+"-"; start+=charsPerLine-1; } else { line=text.substring(start,space); start=space+1; } } tft.drawString(line,x,cursorY,2); cursorY+=lineH; } }
static void rebuildCacheIfNeeded(const Snapshot& s){ String key=s.eventId+"|"+String((int)s.currentTone); if(gCache.eventKey==key) return; gCache.eventKey=key; gCache.loadedBody=s.eventBody; gCache.paged=paginateText(s.eventBody,31,7); }

static void renderTitle(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); tft.setTextColor(accent,bg); tft.setTextDatum(MC_DATUM); tft.drawString("Poudre Trail",160,34,4); tft.drawString("Poudre Valley Edition",160,68,2); tft.setTextDatum(TL_DATUM); tft.setTextColor(textCol,bg); drawWrapped("A valley under pressure.",24,100,270,16); fillButton(btnNewGame,34,172,92,34,"New Game"); fillButton(btnLoadSave,134,172,92,34,"Load Save"); fillButton(btnCalibrate,234,172,92,34,"Calibrate"); drawButton(btnNewGame,0x2A69,accent); drawButton(btnLoadSave,0x2A69,accent); drawButton(btnCalibrate,0x2A69,accent); }
static void renderTravel(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); drawStatusBar(s); tft.setTextColor(accent,bg); tft.drawString("Travel",8,66,4); tft.setTextColor(textCol,bg); tft.drawString("Location: "+s.locationName,8,104,2); drawWrapped(s.footer,8,128,210,16); fillButton(btnContinue,10,190,92,34,"Continue"); fillButton(btnTrade,114,190,92,34,"Trade"); fillButton(btnSave,218,190,92,34,"Save"); drawButton(btnContinue,0x2A69,accent); drawButton(btnTrade,0x2A69,accent); drawButton(btnSave,0x2A69,accent); }
static void renderEvent(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); drawStatusBar(s); rebuildCacheIfNeeded(s); tft.setTextColor(accent,bg); tft.drawString(s.eventTitle,8,64,2); tft.fillRoundRect(8,110,196,78,6,panel); tft.drawRoundRect(8,110,196,78,6,TFT_DARKGREY); tft.setTextColor(textCol,panel); drawWrapped(gCache.paged.pages[gCache.paged.pageIndex],14,118,182,14); if(gCache.paged.pageCount>1){ if(gCache.paged.pageIndex>0) fillButton(btnPrev,8,194,54,24,"Prev"); if(gCache.paged.pageIndex<gCache.paged.pageCount-1) fillButton(btnNext,68,194,54,24,"Next"); } drawButton(btnPrev,0x39C7,accent); drawButton(btnNext,0x39C7,accent); if(gCache.paged.pageIndex==gCache.paged.pageCount-1){ for(int i=0;i<s.visibleChoiceCount && i<3;++i){ fillButton(btnChoices[i],208,144+i*28,106,24,s.visibleChoices[i]); drawButton(btnChoices[i],0x2A69,accent); } } tft.setTextColor(textCol,bg); tft.drawString(s.footer,8,224,2); }
static void renderTrade(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); drawStatusBar(s); tft.setTextColor(accent,bg); tft.drawString("Trade",8,66,4); tft.setTextColor(textCol,bg); drawWrapped("Choose a local offer.",8,104,240,16); for(int i=0;i<s.tradeChoiceCount && i<2;++i){ fillButton(btnChoices[i],10,160+i*34,180,28,s.tradeChoices[i]); drawButton(btnChoices[i],0x2A69,accent); } fillButton(btnBack,220,194,86,28,"Back"); drawButton(btnBack,0x39C7,accent); }
static void renderVictory(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); drawStatusBar(s); tft.setTextColor(accent,bg); tft.setTextDatum(MC_DATUM); tft.drawString("Journey Complete",160,36,4); tft.setTextDatum(TL_DATUM); tft.setTextColor(textCol,bg); drawWrapped(s.footer,18,94,284,18); fillButton(btnRestart,170,184,88,32,"Restart"); drawButton(btnRestart,0x2A69,accent); }
static void renderGameOver(const Snapshot& s){ clearButtons(); tft.fillScreen(bg); drawStatusBar(s); tft.setTextColor(danger,bg); tft.setTextDatum(MC_DATUM); tft.drawString("Game Over",160,36,4); tft.setTextDatum(TL_DATUM); tft.setTextColor(textCol,bg); drawWrapped(s.footer,18,94,284,18); fillButton(btnRestart,170,184,88,32,"Restart"); drawButton(btnRestart,0x2A69,accent); }
static void renderScreen(const Snapshot& s){ switch(s.mode){ case GameMode::TITLE: renderTitle(s); break; case GameMode::TRAVEL: renderTravel(s); break; case GameMode::EVENT: renderEvent(s); break; case GameMode::TRADE: renderTrade(s); break; case GameMode::VICTORY: renderVictory(s); break; case GameMode::GAME_OVER: renderGameOver(s); break; } }
static bool readTouch(int& sx,int& sy){ if(!ts.touched()) return false; TS_Point p=ts.getPoint(); int tx=map(p.x,touchMinX,touchMaxX,0,320); int ty=map(p.y,touchMinY,touchMaxY,0,240); sx=constrain(tx,0,319); sy=constrain(ty,0,239); return true; }

static void renderCalibrationTarget(int cx, int cy, const char* label) {
    tft.fillScreen(bg);
    tft.setTextColor(textCol,bg);
    tft.drawString("Touch the target", 20, 20, 2);
    tft.drawString(label, 20, 50, 2);
    tft.fillCircle(cx, cy, 8, TFT_WHITE);
    tft.drawCircle(cx, cy, 16, accent);
    tft.drawCircle(cx, cy, 24, accent);
}

static bool waitForTouchRaw(int& rx, int& ry) {
    while (true) {
        if (ts.touched()) {
            TS_Point p = ts.getPoint();
            rx = p.x;
            ry = p.y;
            return true;
        }
        delay(20);
    }
}

static bool loadTouchCalibration() {
    if (!SD.exists("/touch_calib.txt")) return false;
    File f = SD.open("/touch_calib.txt");
    if (!f) return false;
    String content;
    while (f.available()) {
        char c = (char)f.read();
        if (c != '\r') content += c;
    }
    f.close();
    String lines[4];
    int count = 0;
    int start = 0;
    while (count < 4) {
        int nl = content.indexOf('\n', start);
        if (nl < 0) {
            lines[count++] = content.substring(start);
            break;
        }
        lines[count++] = content.substring(start, nl);
        start = nl + 1;
    }
    if (count < 4) return false;
    touchMinX = lines[0].toInt();
    touchMaxX = lines[1].toInt();
    touchMinY = lines[2].toInt();
    touchMaxY = lines[3].toInt();
    return touchMinX < touchMaxX && touchMinY < touchMaxY;
}

static bool saveTouchCalibration(int minx, int maxx, int miny, int maxy) {
    File f = SD.open("/touch_calib.txt", FILE_WRITE);
    if (!f) return false;
    f.print(minx);
    f.print('\n');
    f.print(maxx);
    f.print('\n');
    f.print(miny);
    f.print('\n');
    f.print(maxy);
    f.print('\n');
    f.close();
    return true;
}

static void runTouchCalibration() {
    const int targetX[4] = {30, 290, 290, 30};
    const int targetY[4] = {30, 30, 210, 210};
    const char* labels[4] = {"Top-left", "Top-right", "Bottom-right", "Bottom-left"};
    int rawX[4];
    int rawY[4];
    for (int i = 0; i < 4; ++i) {
        renderCalibrationTarget(targetX[i], targetY[i], labels[i]);
        delay(500);
        waitForTouchRaw(rawX[i], rawY[i]);
        while (ts.touched()) delay(20);
    }
    touchMinX = rawX[0];
    touchMaxX = rawX[0];
    touchMinY = rawY[0];
    touchMaxY = rawY[0];
    for (int i = 1; i < 4; ++i) {
        touchMinX = min(touchMinX, rawX[i]);
        touchMaxX = max(touchMaxX, rawX[i]);
        touchMinY = min(touchMinY, rawY[i]);
        touchMaxY = max(touchMaxY, rawY[i]);
    }
    saveTouchCalibration(touchMinX, touchMaxX, touchMinY, touchMaxY);
    tft.fillScreen(bg);
    tft.setTextColor(textCol,bg);
    tft.drawString("Calibration complete", 20, 100, 2);
    delay(1000);
}

static void refresh(){ gSnap=engine.snapshot(); renderScreen(gSnap); }
static void handleTouch(int x,int y){ unsigned long now=millis(); if(now-lastTouchMs<220) return; lastTouchMs=now; if(hit(btnNewGame,x,y)){ engine.startGame(); gCache.eventKey=""; refresh(); return; } if(hit(btnLoadSave,x,y)){ File f=SD.open("/save.txt"); if(f){ String text; while(f.available()) text+=(char)f.read(); f.close(); if(engine.loadSaveText(text)){ gCache.eventKey=""; refresh(); return; } } engine.startGame(); gCache.eventKey=""; refresh(); return; } if(hit(btnSave,x,y)){ String text=engine.serializeSave(); File f=SD.open("/save.txt", FILE_WRITE); if(f){ f.print(text); f.close(); } return; } if(hit(btnContinue,x,y)){ engine.continueTravel(); gCache.eventKey=""; refresh(); return; } if(hit(btnTrade,x,y)){ engine.openTrade(); refresh(); return; } if(hit(btnBack,x,y)){ engine.backFromTrade(); refresh(); return; } if(hit(btnRestart,x,y)){ engine.restartToTitle(); gCache.eventKey=""; refresh(); return; } if(hit(btnCalibrate,x,y)){ runTouchCalibration(); refresh(); return; } if(hit(btnPrev,x,y) && gCache.paged.pageIndex>0){ gCache.paged.pageIndex--; renderEvent(gSnap); return; } if(hit(btnNext,x,y) && gCache.paged.pageIndex<gCache.paged.pageCount-1){ gCache.paged.pageIndex++; renderEvent(gSnap); return; } if(gSnap.mode==GameMode::EVENT){ for(int i=0;i<3;++i){ if(hit(btnChoices[i],x,y)){ engine.chooseEventOption(i); gCache.eventKey=""; refresh(); return; } } } else if(gSnap.mode==GameMode::TRADE){ for(int i=0;i<3;++i){ if(hit(btnChoices[i],x,y)){ engine.applyTrade(i); refresh(); return; } } } }

void setup(){
    pinMode(TFT_BL,OUTPUT);
    digitalWrite(TFT_BL,HIGH);
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(bg);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(textCol,bg);
    touchSPI.begin();
    ts.begin();
    ts.setRotation(1);
    SPI.begin();

    bool sdOk = SD.begin(5);
    if (!sdOk) {
        Serial.println("SD init failed");
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawString("SD init failed", 20, 100, 4);
        return;
    }

    bool eventsLoaded = loadEventsCsv("/events.csv", gEventRows, MAX_EVENT_ROWS, gEventRowCount);
    bool locationsLoaded = loadLocationsCsv("/locations.csv", gLocationRows, MAX_LOCATION_ROWS, gLocationRowCount);
    gDataLoaded = eventsLoaded && locationsLoaded;
    if (!gDataLoaded) {
        Serial.println("Game data loading failed");
        tft.fillScreen(TFT_RED);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawString("Game data load error", 10, 100, 4);
        return;
    }

    if (!loadTouchCalibration()) {
        runTouchCalibration();
    }

    refresh();
}
void loop(){ if(!gDataLoaded){ delay(1000); return; } int x,y; if(readTouch(x,y)) handleTouch(x,y); delay(20); }
