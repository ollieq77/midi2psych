#pragma once

#include <string>
#include <mutex>

#ifdef _WIN32
  #include <windows.h>
  #include <richedit.h>
#endif

#include "utils.h"   // colour-code defines

// Thread-safe logger that writes to either a Win32 RichEdit control or stdout.
class GUILogger {
public:
    GUILogger();

    void setConsole(HWND hwnd);

    // Append plain text (with optional COLORREF colour on Windows).
#ifdef _WIN32
    void log(const std::string& text, COLORREF color = RGB(220, 220, 220));
#else
    void log(const std::string& text);
#endif

    // Append text, mapping ANSI colour-code strings to Win32 COLORREF.
    void logColored(const std::string& text, const std::string& colorCode);

private:
#ifdef _WIN32
    HWND        m_console;
#endif
    std::mutex  m_mutex;
};

// Single global instance shared across all translation units.
extern GUILogger guiLogger;
