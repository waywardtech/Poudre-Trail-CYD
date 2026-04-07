#pragma once
#include <Arduino.h>

struct PagedText {
    String pages[12];
    int pageCount = 0;
    int pageIndex = 0;
};

struct RenderCache {
    String eventKey;
    String loadedBody;
    PagedText paged;
};

PagedText paginateText(const String& text, int charsPerLine, int linesPerPage);
