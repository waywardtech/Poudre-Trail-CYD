// Host-side stand-in for the parts of the Arduino core that src/main.cpp uses.
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define PROGMEM

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 1; }
extern uint32_t g_random;
inline uint32_t esp_random() { return g_random; }

class String {
    std::string s;
public:
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& x) : s(x) {}
    String(char c) : s(1, c) {}
    template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value && !std::is_same<T, char>::value>::type>
    String(T v) : s(std::to_string(v)) {}

    const char* c_str() const { return s.c_str(); }
    size_t length() const { return s.size(); }
    bool isEmpty() const { return s.empty(); }
    void reserve(size_t) {}
    char operator[](size_t i) const { return s[i]; }
    String substring(unsigned from) const { return from >= s.size() ? String() : String(s.substr(from)); }
    String substring(unsigned from, unsigned to) const {
        if (from >= s.size() || to <= from) return String();
        return String(s.substr(from, to - from));
    }
    int indexOf(char c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(char c) const { size_t p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    void remove(unsigned idx) { if (idx < s.size()) s.erase(idx); }
    void remove(unsigned idx, unsigned n) { if (idx < s.size()) s.erase(idx, n); }
    void trim() {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        s = s.substr(a, b - a);
    }
    bool endsWith(const String& o) const { return s.size() >= o.s.size() && s.compare(s.size() - o.s.size(), o.s.size(), o.s) == 0; }
    void toLowerCase() { for (auto& c : s) c = (char)tolower((unsigned char)c); }
    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(char c) { s += c; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
    bool operator==(const String& o) const { return s == o.s; }
    bool operator!=(const String& o) const { return s != o.s; }
};

struct SerialShim {
    void begin(int) {}
    void printf(const char* f, ...) __attribute__((format(printf, 2, 3))) {
        va_list a; va_start(a, f); std::vprintf(f, a); va_end(a);
    }
    void println(const char* t) { std::puts(t); }
};
extern SerialShim Serial;

struct EspShim {
    uint32_t getFreeHeap() { return 200000; }
};
extern EspShim ESP;
