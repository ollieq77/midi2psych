#include "progress_bar.h"
#include "gui_logger.h"
#include "utils.h"

#ifdef _WIN32
  #include <commctrl.h>
#endif

ProgressBar::ProgressBar(const std::string& label, int width)
    : m_label(label), m_width(width)
#ifdef _WIN32
    , m_handle(nullptr)
#endif
{}

void ProgressBar::setHandle(HWND hwnd) {
#ifdef _WIN32
    m_handle = hwnd;
#else
    (void)hwnd;
#endif
}

void ProgressBar::update(double progress, const std::string& status) {
#ifdef _WIN32
    if (m_handle)
        SendMessage(m_handle, PBM_SETPOS, static_cast<int>(progress * 100), 0);
#endif
    std::string line = m_label + " [" + std::to_string(static_cast<int>(progress * 100)) + "%]";
    if (!status.empty()) line += " " + status;
    line += "\n";
    guiLogger.logColored(line, CYAN);
}

void ProgressBar::finish(const std::string& msg) {
    update(1.0, msg);
}
