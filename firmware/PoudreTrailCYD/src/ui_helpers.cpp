#include "ui_helpers.hpp"

PagedText paginateText(const String& text, int charsPerLine, int linesPerPage) {
    PagedText out;
    String currentPage;
    int currentLineCount = 0;
    int start = 0;

    while (start < (int)text.length()) {
        int remaining = text.length() - start;
        String line;
        if (remaining <= charsPerLine) {
            line = text.substring(start);
            start = text.length();
        } else {
            int cut = start + charsPerLine;
            int space = text.lastIndexOf(' ', cut);
            if (space < start) {
                line = text.substring(start, start + charsPerLine - 1) + "-";
                start += charsPerLine - 1;
            } else {
                line = text.substring(start, space);
                start = space + 1;
            }
        }

        currentPage += line + "\n";
        currentLineCount++;

        if (currentLineCount >= linesPerPage && out.pageCount < 12) {
            out.pages[out.pageCount++] = currentPage;
            currentPage = "";
            currentLineCount = 0;
        }
    }

    if ((currentPage.length() > 0 || out.pageCount == 0) && out.pageCount < 12) {
        out.pages[out.pageCount++] = currentPage;
    }

    out.pageIndex = 0;
    return out;
}
