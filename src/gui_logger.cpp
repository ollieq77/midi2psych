#include "gui_logger.h"
#include <iostream>

// ─── Global instance ──────────────────────────────────────────────────────────
GUILogger guiLogger;

// ─── Constructor ──────────────────────────────────────────────────────────────
GUILogger::GUILogger()
#ifdef _WIN32
    : m_console(nullptr)
#endif
{}

// ─── setConsole ───────────────────────────────────────────────────────────────
void GUILogger::setConsole(HWND hwnd) {
#ifdef _WIN32
    m_console = hwnd;
#else
    (void)hwnd;
#endif
}

// ─── log ──────────────────────────────────────────────────────────────────────
#ifdef _WIN32
void GUILogger::log(const std::string& text, COLORREF color) {
    if (!m_console) {
        std::cout << text;
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    int len = GetWindowTextLength(m_console);
    SendMessage(m_console, EM_SETSEL, len, len);

    CHARFORMAT2 cf = {};
    cf.cbSize    = sizeof(CHARFORMAT2);
    cf.dwMask    = CFM_COLOR;
    cf.crTextColor = color;
    SendMessage(m_console, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    SendMessage(m_console, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessage(m_console, WM_VSCROLL, SB_BOTTOM, 0);
}
#else
void GUILogger::log(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << text;
}
#endif

// ─── logColored ───────────────────────────────────────────────────────────────
void GUILogger::logColored(const std::string& text, const std::string& colorCode) {
#ifdef _WIN32
    COLORREF color = RGB(220, 220, 220);
    if      (colorCode == RED)     color = RGB(255,  80,  80);
    else if (colorCode == GREEN)   color = RGB( 80, 255, 120);
    else if (colorCode == YELLOW)  color = RGB(255, 220,  80);
    else if (colorCode == BLUE)    color = RGB(100, 150, 255);
    else if (colorCode == CYAN)    color = RGB( 80, 220, 255);
    else if (colorCode == MAGENTA) color = RGB(255, 100, 255);
    log(text, color);
#else
    log(colorCode + text + RESET);
#endif
}
