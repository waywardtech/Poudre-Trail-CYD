#pragma once
#include <Arduino.h>

enum class EmotionTone { STEADY, WARY, OPEN, PRESSURED, CONTRADICTED };

struct CsvChoice {
    String label;
    int foodDelta = 0;
    int ammoDelta = 0;
    int medDelta = 0;
    int tradeDelta = 0;
    int horseDelta = 0;
    int moraleDelta = 0;
    int healthDelta = 0;
    int repJanisDelta = 0;
    int repFridayDelta = 0;
    int repMasonDelta = 0;
    String setFlag;
    String resultFile;
};

struct CsvEventRow {
    String eventId;
    String locationId;
    String variantKey;
    String title;
    String bodyFile;
    String portraitBmp;
    String backgroundBmp;
    EmotionTone tone = EmotionTone::STEADY;
    String requiresFlag;
    String blocksFlag;
    int minRepJanis = -999;
    int maxRepJanis = 999;
    int minRepFriday = -999;
    int maxRepFriday = 999;
    int minRepMason = -999;
    int maxRepMason = 999;
    CsvChoice choices[3];
    int choiceCount = 0;
};

struct CsvLocationRow {
    String locationId;
    String name;
    String backgroundBmp;
    String travelTextFile;
};

bool loadEventsCsv(const char* path, CsvEventRow* rows, int maxRows, int& outCount);
bool loadLocationsCsv(const char* path, CsvLocationRow* rows, int maxRows, int& outCount);
EmotionTone parseTone(const String& tone);
