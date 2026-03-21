#pragma once

#include <string>

#ifdef _WIN32
  #include <windows.h>
#endif

// Simple progress bar wrapper.
// On Windows it drives a HWND progress-bar control; it always logs to the
// GUI console as well so there's a text trace even in CLI mode.
class ProgressBar {
public:
    explicit ProgressBar(const std::string& label, int width = 40);

    void setHandle(HWND hwnd);

    // progress: 0.0 – 1.0
    void update(double progress, const std::string& status = "");
    void finish(const std::string& msg = "Done!");

private:
    std::string m_label;
    int         m_width;
#ifdef _WIN32
    HWND        m_handle;
#endif
};
