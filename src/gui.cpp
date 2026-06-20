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
HWND g_hSplitCheck   = nullptr, g_hMinifyCheck    = nullptr, g_hSmartMapCheck = nullptr;
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
    cfg.smartPitchMapping = (SendMessage(g_hSmartMapCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

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
        g_hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g_hTitleFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        g_hConsoleFont = CreateFont(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

        int y = 10;
        const int LH = 26;  // Label height
        const int IH = 22;  // Input height
        const int GAP = 8;

        // ════ TITLE ════
        HWND hTitle = CreateWindow("STATIC", "MIDI to Psych Converter V1.0",
            WS_VISIBLE | WS_CHILD | SS_LEFT, 15, y, 600, LH, hwnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);
        y += LH + 8;

        // ════ FILES SECTION ════
        CreateWindow("STATIC", "=====  FILES  =====", WS_VISIBLE | WS_CHILD | SS_LEFT,
            15, y, 750, 14, hwnd, NULL, NULL, NULL);
        y += 18;

        auto makeFileRow = [&](const char* label, HWND& edit, int id, HMENU btnId) {
            CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD | SS_RIGHT,
                15, y, 65, IH, hwnd, NULL, NULL, NULL);
            edit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                85, y, 580, IH, hwnd, (HMENU)id, NULL, NULL);
            CreateWindow("BUTTON", "Browse", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                670, y, 90, IH, hwnd, btnId, NULL, NULL);
            SendMessage(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            y += LH;
        };

        makeFileRow("P1:", g_hP1Edit,  ID_EDIT_P1,  (HMENU)ID_BTN_P1_BROWSE);
        makeFileRow("P2:", g_hP2Edit,  ID_EDIT_P2,  (HMENU)ID_BTN_P2_BROWSE);
        makeFileRow("Out:", g_hOutEdit, ID_EDIT_OUT, (HMENU)ID_BTN_OUT_BROWSE);
        SetWindowText(g_hOutEdit, "chart.json");

        y += GAP;

        // ════ CONVERSION SETTINGS ════
        CreateWindow("STATIC", "=====  CONVERSION  =====", WS_VISIBLE | WS_CHILD | SS_LEFT,
            15, y, 750, 14, hwnd, NULL, NULL, NULL);
        y += 18;

        auto makeConvRow = [&](const char* label, HWND& h, int id, int xPos, int w, const char* def) {
            CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD | SS_RIGHT,
                xPos, y, 65, IH, hwnd, NULL, NULL, NULL);
            h = CreateWindow("EDIT", def, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                xPos + 70, y, w, IH, hwnd, (HMENU)id, NULL, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        };

        // Row 1: Song, BPM, Speed, Mania
        makeConvRow("Song:",   g_hSongEdit,    ID_EDIT_SONG,    15, 150, "Converted");
        makeConvRow("BPM:",    g_hBPMEdit,     ID_EDIT_BPM,     260, 50, "1.0");
        makeConvRow("Speed:",  g_hSpeedEdit,   ID_EDIT_SPEED,   380, 50, "2.5");
        makeConvRow("Mania:",  g_hManiaEdit,   ID_EDIT_MANIA,   520, 50, "3");
        y += LH;

        // Row 2: Vel, Precision, Offset, Round
        makeConvRow("Min Vel:", g_hVelEdit,      ID_EDIT_VELOCITY, 15, 50, "0");
        makeConvRow("Prec:",    g_hPrecEdit,     ID_EDIT_PRECISION, 260, 50, "6");
        makeConvRow("Offset:",  g_hOffsetEdit,   ID_EDIT_OFFSET,    380, 50, "0");
        makeConvRow("Round:",   g_hRoundEdit,    ID_EDIT_ROUND,     520, 50, "-1");
        y += LH + GAP;

        // ════ OPTIONS ════
        CreateWindow("STATIC", "=====  OPTIONS  =====", WS_VISIBLE | WS_CHILD | SS_LEFT,
            15, y, 750, 14, hwnd, NULL, NULL, NULL);
        y += 18;

        auto makeCheckbox = [&](const char* label, HWND& h, int id, int xPos, bool checked = false) {
            h = CreateWindow("BUTTON", label, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                xPos, y, 140, IH, hwnd, (HMENU)id, NULL, NULL);
            if (checked) SendMessage(h, BM_SETCHECK, BST_CHECKED, 0);
            SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        };

        makeCheckbox("High Precision",       g_hPrecisionCheck, ID_CHECK_PRECISION, 15, true);
        makeCheckbox("Sustain Notes",        g_hSustainCheck,   ID_CHECK_SUSTAIN,   165);
        makeCheckbox("Minify JSON",          g_hMinifyCheck,    ID_CHECK_MINIFY,    315, true);
        makeCheckbox("Smart Pitch Mapping",  g_hSmartMapCheck,  ID_CHECK_SMART_MAP, 465, true);
        y += LH;

        makeCheckbox("Split Output", g_hSplitCheck, ID_CHECK_SPLIT, 15);
        CreateWindow("STATIC", "Notes/File:", WS_VISIBLE | WS_CHILD | SS_RIGHT,
            165, y + 2, 65, IH, hwnd, NULL, NULL, NULL);
        g_hSplitNotesEdit = CreateWindow("EDIT", "1000", WS_VISIBLE | WS_CHILD | WS_BORDER,
            235, y, 70, IH, hwnd, (HMENU)ID_EDIT_SPLIT_NOTES, NULL, NULL);
        SendMessage(g_hSplitNotesEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += LH + GAP;

        // ════ CHARACTER SETTINGS ════
        CreateWindow("STATIC", "=====  CHARACTERS  =====", WS_VISIBLE | WS_CHILD | SS_LEFT,
            15, y, 750, 14, hwnd, NULL, NULL, NULL);
        y += 18;

        auto makeCharRow = [&](const char* label, HWND& h, int id, int xPos, const char* def) {
            CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD | SS_RIGHT,
                xPos, y, 55, IH, hwnd, NULL, NULL, NULL);
            h = CreateWindow("EDIT", def, WS_VISIBLE | WS_CHILD | WS_BORDER,
                xPos + 60, y, 90, IH, hwnd, (HMENU)id, NULL, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        };

        makeCharRow("P1:",    g_hP1CharEdit,  ID_EDIT_P1CHAR,  15,  "bf");
        makeCharRow("P2:",    g_hP2CharEdit,  ID_EDIT_P2CHAR,  200, "dad");
        makeCharRow("GF:",    g_hGFCharEdit,  ID_EDIT_GFCHAR,  385, "gf");
        makeCharRow("Stage:", g_hStageEdit,   ID_EDIT_STAGE,   570, "stage");
        y += LH + GAP + 4;

        // ════ ACTION BAR ════
        g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            15, y, 640, 20, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
        SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hProgress, PBM_SETSTEP, 1, 0);

        HWND hConvert = CreateWindow("BUTTON", "► CONVERT",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            665, y, 95, 20, hwnd, (HMENU)ID_BTN_CONVERT, NULL, NULL);
        SendMessage(hConvert, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += 28;

        // ════ CONSOLE ════
        CreateWindow("STATIC", "Console Output", WS_VISIBLE | WS_CHILD,
            15, y, 150, 14, hwnd, NULL, NULL, NULL);
        HWND hClear = CreateWindow("BUTTON", "Clear",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            665, y, 95, 18, hwnd, (HMENU)ID_BTN_CLEAR_LOG, NULL, NULL);
        SendMessage(hClear, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += 22;

        LoadLibrary("Msftedit.dll");
        g_hConsole = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            15, y, 745, 160, hwnd, (HMENU)ID_CONSOLE, NULL, NULL);
        SendMessage(g_hConsole, WM_SETFONT, (WPARAM)g_hConsoleFont, TRUE);
        SendMessage(g_hConsole, EM_SETBKGNDCOLOR, 0, RGB(20, 20, 30));

        guiLogger.setConsole(g_hConsole);
        guiLogger.logColored("MIDI to Psych Converter V1.0 Ready!\n", CYAN);
        guiLogger.logColored("Optimized for spamcharts & dense charts.\n\n", YELLOW);
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
