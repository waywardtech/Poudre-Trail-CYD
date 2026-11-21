// SD shim: maps the card layout onto the repo's data/ folder, so the emulator
// reads exactly the files the setup script copies to the real card.
//   /art/x   -> data/art/x      /data/x -> data/x      /saves/x -> scratch dir
#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include "Arduino.h"
#include "SPI.h"

#define FILE_READ "r"
#define FILE_WRITE "w"

extern std::string g_dataRoot;   // path of the repo's data/ directory
extern std::string g_savesRoot;  // scratch directory for /saves

inline std::string sdPath(const std::string& p) {
    if (p.rfind("/art", 0) == 0)   return g_dataRoot + p;                  // data/art...
    if (p.rfind("/data/", 0) == 0) return g_dataRoot + p.substr(5);        // data/<file>
    if (p.rfind("/saves", 0) == 0) return g_savesRoot + p.substr(6);
    return g_dataRoot + p;
}

class File {
    FILE* fp = nullptr;
    std::string nm;
    std::vector<std::string> entries;
    size_t next = 0;
    bool dir = false;
    std::string base;
public:
    File() {}
    explicit File(FILE* f, const std::string& n) : fp(f), nm(n) {}
    static File openDir(const std::string& path) {
        File f; f.dir = true; f.base = path;
        if (DIR* d = opendir(path.c_str())) {
            while (dirent* e = readdir(d)) if (e->d_name[0] != '.') f.entries.push_back(e->d_name);
            closedir(d);
            std::sort(f.entries.begin(), f.entries.end());
        } else f.dir = false;
        return f;
    }
    explicit operator bool() const { return fp != nullptr || dir; }
    size_t size() {
        if (!fp) return 0;
        long cur = ftell(fp); fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, cur, SEEK_SET); return (size_t)n;
    }
    size_t read(uint8_t* buf, size_t n) { return fp ? fread(buf, 1, n, fp) : 0; }
    int read() { return fp ? fgetc(fp) : -1; }
    int available() { return fp && !feof(fp) && (ftell(fp) < (long)size()); }
    void close() { if (fp) { fclose(fp); fp = nullptr; } }
    void print(const String& t) { if (fp) fputs(t.c_str(), fp); }
    const char* name() const { return nm.c_str(); }
    File openNextFile() {
        if (!dir || next >= entries.size()) return File();
        std::string n = entries[next++];
        return File(fopen((base + "/" + n).c_str(), "rb"), n);
    }
};

struct SDClass {
    bool begin(int, SPIClass&, uint32_t) { return true; }
    File open(const char* path, const char* mode = "r") {
        std::string p = sdPath(path);
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return File::openDir(p);
        FILE* f = fopen(p.c_str(), mode[0] == 'w' ? "wb" : "rb");
        return f ? File(f, path) : File();
    }
    bool exists(const char* path) { struct stat st; return stat(sdPath(path).c_str(), &st) == 0; }
    bool mkdir(const char* path) { return ::mkdir(sdPath(path).c_str(), 0755) == 0; }
    bool remove(const char* path) { return ::remove(sdPath(path).c_str()) == 0; }
    uint8_t cardType() { return 3; }
    uint64_t cardSize() { return 64ULL * 1024 * 1024 * 1024; }
};
extern SDClass SD;
