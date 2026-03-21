#ifdef _WIN32

#include "gui.h"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <thread>
#include <vector>

#include "gui_logger.h"
#include "psych_converter.h"
#include "utils.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ─── Global definitions ───────────────────────────────────────────────────────
HWND g_hMainWnd      = nullptr;
HWND g_hP1Edit       = nullptr, g_hP2Edit    = nullptr, g_hOutEdit  = nullptr;
HWND g_hConsole      = nullptr, g_hProgress  = nullptr;
HWND g_hSongEdit     = nullptr, g_hBPMEdit   = nullptr, g_hOffsetEdit = nullptr;
HWND g_hVelEdit      = nullptr, g_hPrecEdit  = nullptr, g_hSpeedEdit  = nullptr;
HWND g_hP1CharEdit   = nullptr, g_hP2CharEdit = nullptr, g_hGFCharEdit = nullptr;
HWND g_hStageEdit    = nullptr, g_hSplitNotesEdit = nullptr, g_hRoundEdit = nullptr, g_hManiaEdit = nullptr;
HWND g_hSustainCheck = nullptr, g_hPrecisionCheck = nullptr;
HWND g_hSplitCheck   = nullptr, g_hMinifyCheck    = nullptr;
HFONT g_hFont        = nullptr, g_hTitleFont = nullptr, g_hConsoleFont = nullptr;
bool  g_converting   = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────

std::string BrowseForFile(HWND hwnd, const char* filter, bool save) {
    OPENFILENAME ofn = {};
    char filename[MAX_PATH] = "";

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = filename;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = save ? (OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT)
                     : (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST);

    return (save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn))
           ? std::string(filename) : "";
}

std::string GetWindowTextStr(HWND hwnd) {
    int len = GetWindowTextLength(hwnd);
    if (len == 0) return "";
    std::vector<char> buf(len + 1);
    GetWindowText(hwnd, buf.data(), len + 1);
    return std::string(buf.data());
}

// ─── DoConversion ─────────────────────────────────────────────────────────────

void DoConversion() {
    g_converting = true;

    std::string p1File  = GetWindowTextStr(g_hP1Edit);
    std::string p2File  = GetWindowTextStr(g_hP2Edit);
    std::string outFile = GetWindowTextStr(g_hOutEdit);

    if (p1File.empty() || p2File.empty() || outFile.empty()) {
        MessageBox(g_hMainWnd, "Please specify all files!", "Error", MB_OK | MB_ICONERROR);
        g_converting = false;
        return;
    }

    PsychConverter converter;
    auto& cfg = converter.getConfig();

    try {
        cfg.songName      = GetWindowTextStr(g_hSongEdit);
        cfg.bpmMultiplier = std::stod(GetWindowTextStr(g_hBPMEdit));
        cfg.noteOffset    = std::stod(GetWindowTextStr(g_hOffsetEdit));
        cfg.minVelocity   = std::stoi(GetWindowTextStr(g_hVelEdit));
        cfg.decimalPlaces = std::stoi(GetWindowTextStr(g_hPrecEdit));
        cfg.speed         = std::stod(GetWindowTextStr(g_hSpeedEdit));
        
        std::string maniaStr = GetWindowTextStr(g_hManiaEdit);
        if (!maniaStr.empty()) {
            cfg.mania = std::stoi(maniaStr);
        }
        
        cfg.p1Char        = GetWindowTextStr(g_hP1CharEdit);
        cfg.p2Char        = GetWindowTextStr(g_hP2CharEdit);
        cfg.gfChar        = GetWindowTextStr(g_hGFCharEdit);
        cfg.stage         = GetWindowTextStr(g_hStageEdit);
    } catch (const std::exception& e) {
        MessageBox(g_hMainWnd, "Invalid input value! Check all numeric fields.", "Error",
                   MB_OK | MB_ICONERROR);
        g_converting = false;
        return;
    }
    cfg.sustainNotes  = (SendMessage(g_hSustainCheck,   BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.highPrecision = (SendMessage(g_hPrecisionCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.splitOutput   = (SendMessage(g_hSplitCheck,     BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.minifyJSON    = (SendMessage(g_hMinifyCheck,    BM_GETCHECK, 0, 0) == BST_CHECKED);

    std::string splitStr = GetWindowTextStr(g_hSplitNotesEdit);
    if (!splitStr.empty()) {
        try {
            cfg.notesPerSplit = std::stoi(splitStr);
        } catch (...) {
            cfg.notesPerSplit = 1000;  // Default if parse fails
        }
    }

    std::string roundStr = GetWindowTextStr(g_hRoundEdit);
    if (!roundStr.empty() && roundStr != "-1") {
        try {
            cfg.roundTimesTo = std::stoi(roundStr);
        } catch (...) {
            cfg.roundTimesTo = -1;  // Default if parse fails
        }
    }

    converter.setConfig(cfg);
    converter.setProgressHandle(g_hProgress);

    std::thread([=]() mutable {
        bool ok = converter.convert(p1File, p2File, outFile);
        g_converting = false;
        SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
        if (ok)
            MessageBox(g_hMainWnd, "Conversion completed successfully!", "Success",
                       MB_OK | MB_ICONINFORMATION);
        else
            MessageBox(g_hMainWnd, "Conversion failed! Check the console for details.", "Error",
                       MB_OK | MB_ICONERROR);
    }).detach();
}

// ─── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g_hTitleFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g_hConsoleFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        int y = 10;

        // Title
        HWND hTitle = CreateWindow("STATIC", "MIDI to Psych Engine Converter",
            WS_VISIBLE | WS_CHILD | SS_CENTER, 10, y, 760, 30, hwnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);
        y += 40;

        // ── File inputs ──────────────────────────────────────────────────
        CreateWindow("STATIC", "Input Files:", WS_VISIBLE | WS_CHILD,
            10, y, 200, 20, hwnd, NULL, NULL, NULL);
        y += 25;

        auto makeFileLine = [&](const char* label, HWND& edit, int id, HMENU btnId,
                                const char* defText) {
            CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD,
                10, y, 100, 20, hwnd, NULL, NULL, NULL);
            edit = CreateWindow("EDIT", defText, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                110, y, 550, 22, hwnd, (HMENU)id, NULL, NULL);
            CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                670, y, 100, 22, hwnd, btnId, NULL, NULL);
            SendMessage(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            y += 30;
        };

        makeFileLine("Player 1 MIDI:", g_hP1Edit,  ID_EDIT_P1,  (HMENU)ID_BTN_P1_BROWSE,  "");
        makeFileLine("Player 2 MIDI:", g_hP2Edit,  ID_EDIT_P2,  (HMENU)ID_BTN_P2_BROWSE,  "");
        makeFileLine("Output JSON:",   g_hOutEdit, ID_EDIT_OUT, (HMENU)ID_BTN_OUT_BROWSE, "chart.json");
        y += 10;

        // ── Chart settings ───────────────────────────────────────────────
        CreateWindow("STATIC", "Chart Settings:", WS_VISIBLE | WS_CHILD,
            10, y, 200, 20, hwnd, NULL, NULL, NULL);
        y += 25;

        // Row 1
        auto label = [&](const char* t, int x) {
            CreateWindow("STATIC", t, WS_VISIBLE | WS_CHILD, x, y, 80, 20, hwnd, NULL, NULL, NULL);
        };
        auto edit = [&](HWND& h, int id, int x, int w, const char* def) {
            h = CreateWindow("EDIT", def, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                x, y, w, 22, hwnd, (HMENU)id, NULL, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        };

        label("Song Name:", 10);   edit(g_hSongEdit,  ID_EDIT_SONG,   95,  120, "Converted");
        label("BPM Mult:",  230);  edit(g_hBPMEdit,   ID_EDIT_BPM,    305,  60, "1.0");
        label("Speed:",     380);  edit(g_hSpeedEdit, ID_EDIT_SPEED,  435,  60, "2.5");
        label("Mania:",     510);  edit(g_hManiaEdit, ID_EDIT_MANIA,  565,  40, "3");
        y += 30;

        // Row 2
        label("Min Vel:",  10);   edit(g_hVelEdit,  ID_EDIT_VELOCITY,  95, 60, "0");
        label("Precision:", 170); edit(g_hPrecEdit, ID_EDIT_PRECISION, 245, 40, "6");
        label("Offset(ms):", 300); edit(g_hOffsetEdit, ID_EDIT_OFFSET, 385, 60, "0");

        g_hSustainCheck = CreateWindow("BUTTON", "Sustain Notes",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 300, y, 120, 22,
            hwnd, (HMENU)ID_CHECK_SUSTAIN, NULL, NULL);
        SendMessage(g_hSustainCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hPrecisionCheck = CreateWindow("BUTTON", "High Precision",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 430, y, 130, 22,
            hwnd, (HMENU)ID_CHECK_PRECISION, NULL, NULL);
        SendMessage(g_hPrecisionCheck, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_hPrecisionCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += 30;

        // Row 3 – split / minify / rounding
        g_hSplitCheck = CreateWindow("BUTTON", "Split Output",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 10, y, 110, 22,
            hwnd, (HMENU)ID_CHECK_SPLIT, NULL, NULL);
        SendMessage(g_hSplitCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        CreateWindow("STATIC", "Notes/File:", WS_VISIBLE | WS_CHILD,
            130, y, 80, 20, hwnd, NULL, NULL, NULL);
        edit(g_hSplitNotesEdit, ID_EDIT_SPLIT_NOTES, 215, 70, "1000");

        g_hMinifyCheck = CreateWindow("BUTTON", "Minify JSON",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 300, y, 110, 22,
            hwnd, (HMENU)ID_CHECK_MINIFY, NULL, NULL);
        SendMessage(g_hMinifyCheck, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hMinifyCheck, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("STATIC", "Round to:", WS_VISIBLE | WS_CHILD,
            420, y, 70, 20, hwnd, NULL, NULL, NULL);
        edit(g_hRoundEdit, ID_EDIT_ROUND, 495, 50, "-1");

        CreateWindow("STATIC", "(-1=off, 0=int, 1=0.1)", WS_VISIBLE | WS_CHILD | SS_RIGHT,
            555, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 30;

        // Row 4 – character names
        label("P1 Char:",  10);  edit(g_hP1CharEdit, ID_EDIT_P1CHAR,  75,  80, "bf");
        label("P2 Char:", 170);  edit(g_hP2CharEdit, ID_EDIT_P2CHAR, 235,  80, "dad");
        label("GF Char:", 330);  edit(g_hGFCharEdit, ID_EDIT_GFCHAR, 395,  80, "gf");
        label("Stage:",   490);  edit(g_hStageEdit,  ID_EDIT_STAGE,  545, 110, "stage");
        y += 35;

        // Progress bar + Convert button
        g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            10, y, 660, 25, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
        SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hProgress, PBM_SETSTEP,  1, 0);

        HWND hConvert = CreateWindow("BUTTON", "CONVERT",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            680, y, 90, 25, hwnd, (HMENU)ID_BTN_CONVERT, NULL, NULL);
        SendMessage(hConvert, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += 35;

        // Console
        CreateWindow("STATIC", "Console Output:", WS_VISIBLE | WS_CHILD,
            10, y, 200, 20, hwnd, NULL, NULL, NULL);
        HWND hClear = CreateWindow("BUTTON", "Clear",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            680, y, 90, 22, hwnd, (HMENU)ID_BTN_CLEAR_LOG, NULL, NULL);
        SendMessage(hClear, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += 25;

        LoadLibrary("Msftedit.dll");
        g_hConsole = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, y, 760, 220, hwnd, (HMENU)ID_CONSOLE, NULL, NULL);
        SendMessage(g_hConsole, WM_SETFONT, (WPARAM)g_hConsoleFont, TRUE);
        SendMessage(g_hConsole, EM_SETBKGNDCOLOR, 0, RGB(20, 20, 30));

        guiLogger.setConsole(g_hConsole);

        guiLogger.logColored("MIDI to Psych Engine Converter v2.4\n", CYAN);
        guiLogger.logColored("Ready to convert!\n\n", GREEN);
        guiLogger.logColored("Optimizations active:\n", YELLOW);
        guiLogger.log("  - Parallel MIDI parsing\n");
        guiLogger.log("  - Smart decimal removal\n");
        guiLogger.log("  - Minify JSON (enabled by default)\n");
        guiLogger.log("  - Round times option\n");
        guiLogger.log("  - Parallel sort (auto for 10k+ notes)\n");
        guiLogger.log("  - Two-pointer O(n) section sweep\n");
        guiLogger.log("  - Split output\n\n");
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case ID_BTN_P1_BROWSE: {
                auto f = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                if (!f.empty()) SetWindowText(g_hP1Edit, f.c_str());
                break;
            }
            case ID_BTN_P2_BROWSE: {
                auto f = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                if (!f.empty()) SetWindowText(g_hP2Edit, f.c_str());
                break;
            }
            case ID_BTN_OUT_BROWSE: {
                auto f = BrowseForFile(hwnd, "JSON Files (*.json)\0*.json\0All Files\0*.*\0", true);
                if (!f.empty()) SetWindowText(g_hOutEdit, f.c_str());
                break;
            }
            case ID_BTN_CONVERT:
                if (!g_converting) DoConversion();
                else MessageBox(hwnd, "Conversion already in progress!", "Info",
                                MB_OK | MB_ICONINFORMATION);
                break;
            case ID_BTN_CLEAR_LOG:
                SetWindowText(g_hConsole, "");
                guiLogger.logColored("Console cleared.\n", CYAN);
                break;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(45, 45, 48));
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DESTROY:
        DeleteObject(g_hFont);
        DeleteObject(g_hTitleFont);
        DeleteObject(g_hConsoleFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

#endif // _WIN32
