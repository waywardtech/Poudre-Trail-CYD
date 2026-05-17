#include <SD.h>
#include "csv_content.hpp"

static String readLine(File& f) {
    String line;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c == '\n') break;
        line += c;
    }
    return line;
}

static int splitCsv(const String& line, String* out, int maxParts) {
    int count = 0;
    String cur;
    bool inQuotes = false;
    for (int i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            if (count < maxParts) out[count++] = cur;
            cur = "";
        } else {
            cur += c;
        }
    }
    if (count < maxParts) out[count++] = cur;
    return count;
}

EmotionTone parseTone(const String& tone) {
    if (tone == "WARY") return EmotionTone::WARY;
    if (tone == "OPEN") return EmotionTone::OPEN;
    if (tone == "PRESSURED") return EmotionTone::PRESSURED;
    if (tone == "CONTRADICTED") return EmotionTone::CONTRADICTED;
    return EmotionTone::STEADY;
}

bool loadEventsCsv(const char* path, CsvEventRow* rows, int maxRows, int& outCount) {
    outCount = 0;
    File f = SD.open(path);
    if (!f) return false;

    readLine(f);

    while (f.available() && outCount < maxRows) {
        String line = readLine(f);
        if (line.length() == 0) continue;

        String cols[31];
        int n = splitCsv(line, cols, 31);
        if (n < 30) continue;

        CsvEventRow& r = rows[outCount++];
        r.eventId = cols[0];
        r.locationId = cols[1];
        r.variantKey = cols[2];
        r.title = cols[3];
        r.bodyFile = cols[4];
        r.portraitBmp = cols[5];
        r.backgroundBmp = cols[6];
        r.tone = parseTone(cols[7]);

        r.choiceCount = 0;
        if (cols[16].length() > 0) {
            r.choices[r.choiceCount].label = cols[16];
            r.choices[r.choiceCount].foodDelta = cols[17].toInt();
            r.choiceCount++;
        }
        if (n > 29 && cols[29].length() > 0) {
            r.choices[r.choiceCount].label = cols[29];
            if (n > 30) r.choices[r.choiceCount].foodDelta = cols[30].toInt();
            r.choiceCount++;
        }
    }

    f.close();
    return true;
}

bool loadLocationsCsv(const char* path, CsvLocationRow* rows, int maxRows, int& outCount) {
    outCount = 0;
    File f = SD.open(path);
    if (!f) return false;

    readLine(f);

    while (f.available() && outCount < maxRows) {
        String line = readLine(f);
        if (line.length() == 0) continue;

        String cols[8];
        int n = splitCsv(line, cols, 8);
        if (n < 4) continue;

        CsvLocationRow& r = rows[outCount++];
        r.locationId = cols[0];
        r.name = cols[1];
        r.backgroundBmp = cols[2];
        r.travelTextFile = cols[3];
    }

    f.close();
    return true;
}
