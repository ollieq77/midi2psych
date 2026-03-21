#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>

// ─── Platform setup ──────────────────────────────────────────────────────────
#ifdef _WIN32
  #include <windows.h>
  #define ENABLE_COLORS() \
    { HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); \
      DWORD mode; GetConsoleMode(h, &mode); \
      SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING); }
#else
  #define ENABLE_COLORS()
#endif

// ─── ANSI colour codes ────────────────────────────────────────────────────────
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

// ─── Number → string helpers ──────────────────────────────────────────────────

// Drops unnecessary trailing zeros / decimal point.
// e.g.  3.000000 → "3",   1.500000 → "1.5"
inline std::string smartNumToStr(double num, int maxDecimals = 6) {
    if (std::floor(num) == num) {
        return std::to_string(static_cast<int64_t>(num));
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(maxDecimals) << num;
    std::string result = ss.str();

    size_t dotPos = result.find('.');
    if (dotPos != std::string::npos) {
        size_t lastNonZero = result.find_last_not_of('0');
        if (lastNonZero != std::string::npos && lastNonZero > dotPos)
            result = result.substr(0, lastNonZero + 1);
        if (result.back() == '.')
            result.pop_back();
    }
    return result;
}

// Fast integer-or-fixed conversion.
template<typename T>
inline std::string fastNumToStr(T num, int decimals = -1) {
    if (decimals < 0)
        return std::to_string(static_cast<int64_t>(num));
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << num;
    return ss.str();
}
