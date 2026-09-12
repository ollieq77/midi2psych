#ifdef _WIN32

#include "gui.h"
#include "theme.h"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>
#include <set>
#include <algorithm>

#include "gui_logger.h"
#include "psych_converter.h"
#include "midi_parser.h"
#include "utils.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

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
HWND g_hSwapBtn = nullptr, g_hDetectBtn = nullptr, g_hSavePresetBtn = nullptr,
     g_hLoadPresetBtn = nullptr, g_hOpenFolderBtn = nullptr;
HWND g_hMidiInfoLabelP1 = nullptr;
HWND g_hMidiInfoLabelP2 = nullptr;
HWND g_hPatternModeCheck = nullptr;
HWND g_hPatternListP1 = nullptr, g_hPatternListP2 = nullptr;
HFONT g_hFont = nullptr, g_hTitleFont = nullptr, g_hSubFont = nullptr,
      g_hConsoleFont = nullptr, g_hIconFont = nullptr;
bool  g_converting   = false;

std::vector<FieldBox> g_fields;
std::unordered_map<HWND, bool> g_checkState;

// ─── Module-local state ───────────────────────────────────────────────────────
namespace {
    struct Panel { RECT r; std::string title; };
    std::vector<Panel> g_panels;

    std::set<HWND> g_invalidFields;
    std::set<HWND> g_hoverButtons;

    std::string g_toastText;
    bool        g_toastSuccess = true;
    bool        g_toastVisible = false;
    RECT        g_toastRect{};

    std::string g_lastOutFile;

    // Pattern Mode: full file paths for each side's pattern list, index 0 = pattern 1 = C3.
    // The listboxes only display "N: filename" — these vectors hold the real paths.
    std::vector<std::string> g_patternPathsP1;
    std::vector<std::string> g_patternPathsP2;

    struct MidiInfo {
        int    player;
        bool   ok;
        int    trackCount;
        int    noteCount;
        double bpm;
        double durationSec;
    };
}

// ─── Small helpers ────────────────────────────────────────────────────────────

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

// ─── Pattern Mode file-picking helpers ───────────────────────────────────────

static std::string BaseName(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// Multi-select file picker — returns every path chosen, in listbox order.
static std::vector<std::string> BrowseForFilesMulti(HWND hwnd, const char* filter) {
    std::vector<std::string> result;
    std::vector<char> buf(65536, 0);   // large: Explorer-style multi-select can be long

    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = static_cast<DWORD>(buf.size());
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileName(&ofn)) return result;

    // Explorer-style multi-select result: first NUL-terminated string is the
    // directory (or the single full path, if only one file was picked),
    // followed by NUL-separated filenames, with a final extra NUL to end it.
    const char* p = buf.data();
    std::string dir = p;
    p += dir.size() + 1;

    if (*p == '\0') {
        result.push_back(dir);  // only one file selected — 'dir' IS the full path
        return result;
    }

    while (*p != '\0') {
        std::string name = p;
        std::string full = dir;
        if (!full.empty() && full.back() != '\\' && full.back() != '/') full += '\\';
        full += name;
        result.push_back(full);
        p += name.size() + 1;
    }
    return result;
}

static std::string BrowseForFolder(HWND hwnd) {
    char path[MAX_PATH] = "";
    BROWSEINFOA bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Select a folder of pattern MIDI files";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";
    SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    return std::string(path);
}

// Every *.mid directly inside a folder, sorted by filename — pattern 1, 2, 3...
static std::vector<std::string> ListMidiFilesInFolder(const std::string& folder) {
    std::vector<std::string> files;
    std::string base = folder;
    if (!base.empty() && base.back() != '\\' && base.back() != '/') base += '\\';

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((base + "*.mid").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back(base + fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Rebuilds a pattern listbox's display strings from its path vector — call
// after any add/remove so the "N:" numbering always matches current order.
static void RefreshPatternListBox(HWND list, const std::vector<std::string>& paths) {
    SendMessage(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string label = std::to_string(i + 1) + ": " + BaseName(paths[i]);
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
}

std::string GetWindowTextStr(HWND hwnd) {
    int len = GetWindowTextLength(hwnd);
    if (len == 0) return "";
    std::vector<char> buf(len + 1);
    GetWindowText(hwnd, buf.data(), len + 1);
    return std::string(buf.data());
}

void MarkFieldInvalid(HWND edit, bool invalid) {
    if (invalid) g_invalidFields.insert(edit);
    else         g_invalidFields.erase(edit);
    for (auto& f : g_fields) {
        if (f.edit == edit) {
            RECT r = f.box; InflateRect(&r, 2, 2);
            InvalidateRect(g_hMainWnd, &r, FALSE);
        }
    }
}

void ShowToast(const std::string& text, bool success) {
    g_toastText    = text;
    g_toastSuccess = success;
    g_toastVisible = true;
    InvalidateRect(g_hMainWnd, &g_toastRect, FALSE);
    SetTimer(g_hMainWnd, TIMER_TOAST_HIDE, 3200, NULL);
}

static void SetEditAndFlagBad(HWND edit) { MarkFieldInvalid(edit, true); }

// The MIDI-info labels paint with a transparent background (WM_CTLCOLORSTATIC
// returns NULL_BRUSH) so they sit cleanly on the custom-drawn panel instead of
// showing a visible rectangle. That means a plain SetWindowText only redraws the
// *new* glyphs — the old ones are never erased first, so old and new text overlap
// into garbage unless we force-erase. P1 and P2 also used to share ONE label,
// so whichever analyze thread finished last silently clobbered the other
// player's result — that's the "stacked text" you saw. Each player now gets
// its own label, and each is erased+repainted only after its own new text is set.
static void SetMidiInfoText(int player, const char* text) {
    HWND h = (player == 1) ? g_hMidiInfoLabelP1 : g_hMidiInfoLabelP2;
    SetWindowTextA(h, text);
    InvalidateRect(h, NULL, TRUE);
    UpdateWindow(h);
}

// ─── MIDI analysis (background thread → posts result to UI thread) ──────────

void AnalyzeMidiAsync(const std::string& path, int player) {
    if (path.empty()) return;
    std::thread([path, player]() {
        auto* info = new MidiInfo{ player, false, 0, 0, 0.0, 0.0 };
        MIDIParser parser;
        if (parser.parse(path, false, 0)) {
            info->ok         = true;
            info->trackCount = static_cast<int>(parser.tracks.size());
            info->bpm        = parser.bpm > 0 ? parser.bpm : 120.0;

            uint32_t maxTick = 0;
            int notes = 0;
            for (auto& track : parser.tracks) {
                notes += static_cast<int>(track.size());
                for (auto& n : track)
                    if (n.tick > maxTick) maxTick = n.tick;
            }
            info->noteCount  = notes;
            double minutes   = (maxTick / static_cast<double>(parser.ppq)) * (60.0 / info->bpm);
            info->durationSec = minutes * 60.0;
        }
        PostMessage(g_hMainWnd, WM_APP_MIDI_INFO, (WPARAM)player, (LPARAM)info);
    }).detach();
}

// ─── Presets (plain "key=value" text files, .m2p) ────────────────────────────

static void PutKV(std::ofstream& f, const char* k, const std::string& v) { f << k << "=" << v << "\n"; }

void SavePreset(HWND hwnd) {
    std::string path = BrowseForFile(hwnd, "MIDI2Psych Preset (*.m2p)\0*.m2p\0All Files\0*.*\0", true);
    if (path.empty()) return;
    if (path.find('.') == std::string::npos) path += ".m2p";

    std::ofstream f(path);
    if (!f) { ShowToast("Couldn't write preset file.", false); return; }

    PutKV(f, "song",   GetWindowTextStr(g_hSongEdit));
    PutKV(f, "bpm",    GetWindowTextStr(g_hBPMEdit));
    PutKV(f, "speed",  GetWindowTextStr(g_hSpeedEdit));
    PutKV(f, "mania",  GetWindowTextStr(g_hManiaEdit));
    PutKV(f, "vel",    GetWindowTextStr(g_hVelEdit));
    PutKV(f, "prec",   GetWindowTextStr(g_hPrecEdit));
    PutKV(f, "offset", GetWindowTextStr(g_hOffsetEdit));
    PutKV(f, "round",  GetWindowTextStr(g_hRoundEdit));
    PutKV(f, "p1char", GetWindowTextStr(g_hP1CharEdit));
    PutKV(f, "p2char", GetWindowTextStr(g_hP2CharEdit));
    PutKV(f, "gfchar", GetWindowTextStr(g_hGFCharEdit));
    PutKV(f, "stage",  GetWindowTextStr(g_hStageEdit));
    PutKV(f, "splitn", GetWindowTextStr(g_hSplitNotesEdit));
    PutKV(f, "sustain",      IsChecked(g_hSustainCheck)   ? "1" : "0");
    PutKV(f, "precisionchk", IsChecked(g_hPrecisionCheck) ? "1" : "0");
    PutKV(f, "split",        IsChecked(g_hSplitCheck)     ? "1" : "0");
    PutKV(f, "minify",       IsChecked(g_hMinifyCheck)    ? "1" : "0");
    PutKV(f, "smartmap",     IsChecked(g_hSmartMapCheck)  ? "1" : "0");

    ShowToast("Preset saved!", true);
}

void LoadPreset(HWND hwnd) {
    std::string path = BrowseForFile(hwnd, "MIDI2Psych Preset (*.m2p)\0*.m2p\0All Files\0*.*\0", false);
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) { ShowToast("Couldn't open preset file.", false); return; }

    std::unordered_map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto set = [&](const char* k, HWND edit) {
        auto it = kv.find(k);
        if (it != kv.end()) SetWindowTextA(edit, it->second.c_str());
    };
    set("song", g_hSongEdit); set("bpm", g_hBPMEdit); set("speed", g_hSpeedEdit);
    set("mania", g_hManiaEdit); set("vel", g_hVelEdit); set("prec", g_hPrecEdit);
    set("offset", g_hOffsetEdit); set("round", g_hRoundEdit);
    set("p1char", g_hP1CharEdit); set("p2char", g_hP2CharEdit);
    set("gfchar", g_hGFCharEdit); set("stage", g_hStageEdit);
    set("splitn", g_hSplitNotesEdit);

    auto setChk = [&](const char* k, HWND h) {
        auto it = kv.find(k);
        if (it != kv.end()) g_checkState[h] = (it->second == "1");
        InvalidateRect(h, NULL, FALSE);
    };
    setChk("sustain", g_hSustainCheck);
    setChk("precisionchk", g_hPrecisionCheck);
    setChk("split", g_hSplitCheck);
    setChk("minify", g_hMinifyCheck);
    setChk("smartmap", g_hSmartMapCheck);

    ShowToast("Preset loaded!", true);
}

// ─── DoConversion ─────────────────────────────────────────────────────────────

void DoConversion() {
    g_converting = true;

    std::string p1File  = GetWindowTextStr(g_hP1Edit);
    std::string p2File  = GetWindowTextStr(g_hP2Edit);
    std::string outFile = GetWindowTextStr(g_hOutEdit);

    bool missing = false;
    if (p1File.empty())  { SetEditAndFlagBad(g_hP1Edit);  missing = true; }
    if (p2File.empty())  { SetEditAndFlagBad(g_hP2Edit);  missing = true; }
    if (outFile.empty()) { SetEditAndFlagBad(g_hOutEdit); missing = true; }
    if (missing) {
        ShowToast("Please pick P1, P2 and an output file.", false);
        g_converting = false;
        return;
    }

    PsychConverter converter;
    auto& cfg = converter.getConfig();

    auto tryDouble = [&](HWND edit, double& target) -> bool {
        try { target = std::stod(GetWindowTextStr(edit)); MarkFieldInvalid(edit, false); return true; }
        catch (...) { MarkFieldInvalid(edit, true); return false; }
    };
    auto tryInt = [&](HWND edit, int& target) -> bool {
        try { target = std::stoi(GetWindowTextStr(edit)); MarkFieldInvalid(edit, false); return true; }
        catch (...) { MarkFieldInvalid(edit, true); return false; }
    };

    bool ok = true;
    cfg.songName = GetWindowTextStr(g_hSongEdit);
    ok &= tryDouble(g_hBPMEdit, cfg.bpmMultiplier);
    ok &= tryDouble(g_hOffsetEdit, cfg.noteOffset);
    ok &= tryInt(g_hVelEdit, cfg.minVelocity);
    ok &= tryInt(g_hPrecEdit, cfg.decimalPlaces);
    ok &= tryDouble(g_hSpeedEdit, cfg.speed);

    std::string maniaStr = GetWindowTextStr(g_hManiaEdit);
    if (!maniaStr.empty()) {
        try { cfg.mania = std::stoi(maniaStr); MarkFieldInvalid(g_hManiaEdit, false); }
        catch (...) { MarkFieldInvalid(g_hManiaEdit, true); ok = false; }
    }

    if (!ok) {
        ShowToast("Check the highlighted fields — invalid numbers.", false);
        g_converting = false;
        return;
    }

    cfg.p1Char = GetWindowTextStr(g_hP1CharEdit);
    cfg.p2Char = GetWindowTextStr(g_hP2CharEdit);
    cfg.gfChar = GetWindowTextStr(g_hGFCharEdit);
    cfg.stage  = GetWindowTextStr(g_hStageEdit);

    cfg.sustainNotes      = IsChecked(g_hSustainCheck);
    cfg.highPrecision     = IsChecked(g_hPrecisionCheck);
    cfg.splitOutput       = IsChecked(g_hSplitCheck);
    cfg.minifyJSON        = IsChecked(g_hMinifyCheck);
    cfg.smartPitchMapping = IsChecked(g_hSmartMapCheck);

    cfg.patternMode = IsChecked(g_hPatternModeCheck);
    if (cfg.patternMode) {
        if (g_patternPathsP1.empty() && g_patternPathsP2.empty()) {
            ShowToast("Pattern Mode is on but no pattern files were added.", false);
            g_converting = false;
            return;
        }
        cfg.patternFilesP1 = g_patternPathsP1;
        cfg.patternFilesP2 = g_patternPathsP2;
    }

    std::string splitStr = GetWindowTextStr(g_hSplitNotesEdit);
    if (!splitStr.empty()) {
        try { cfg.notesPerSplit = std::stoi(splitStr); } catch (...) { cfg.notesPerSplit = 1000; }
    }

    std::string roundStr = GetWindowTextStr(g_hRoundEdit);
    if (!roundStr.empty() && roundStr != "-1") {
        try { cfg.roundTimesTo = std::stoi(roundStr); } catch (...) { cfg.roundTimesTo = -1; }
    }

    converter.setConfig(cfg);
    converter.setProgressHandle(g_hProgress);
    g_lastOutFile = outFile;

    std::thread([=]() mutable {
        bool result = converter.convert(p1File, p2File, outFile);
        g_converting = false;
        SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
        PostMessage(g_hMainWnd, WM_APP_CONVERT_DONE, result ? 1 : 0, 0);
    }).detach();
}

// ─── Owner-draw: buttons & checkboxes ────────────────────────────────────────

static LRESULT CALLBACK ButtonHoverProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // WM_DRAWITEM always paints the entire rect; skip the default white erase
    case WM_MOUSEMOVE: {
        if (!g_hoverButtons.count(hwnd)) {
            g_hoverButtons.insert(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    }
    case WM_MOUSELEAVE:
        g_hoverButtons.erase(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ButtonHoverProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// refData on the subclass is 1 for "primary" (accent-filled) buttons, 0 otherwise.
static HWND MakeButton(HWND parent, const char* label, int id, int x, int y, int w, int h,
                       bool primary) {
    HWND btn = CreateWindow("BUTTON", label, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SetWindowSubclass(btn, ButtonHoverProc, 1, primary ? 1 : 0);
    // Opt this control out of UxTheme entirely — otherwise comctl32 v6's hot-track/
    // transition animation can paint a themed background frame on top of (or instead
    // of) our WM_DRAWITEM output, which is exactly the "blank white control" bug.
    SetWindowTheme(btn, L" ", L" ");
    return btn;
}

static HWND MakeCheckbox(HWND parent, const char* label, int id, int x, int y, int w,
                         bool checked) {
    HWND cb = CreateWindow("BUTTON", label, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        x, y, w, 22, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(cb, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SetWindowSubclass(cb, ButtonHoverProc, 1, 0);
    SetWindowTheme(cb, L" ", L" ");
    g_checkState[cb] = checked;
    return cb;
}

static void DrawOwnerButton(DRAWITEMSTRUCT* dis) {
    bool isCheckbox = g_checkState.count(dis->hwndItem) > 0;
    bool checked    = isCheckbox && IsChecked(dis->hwndItem);
    bool hot        = g_hoverButtons.count(dis->hwndItem) > 0;
    bool pressed    = (dis->itemState & ODS_SELECTED) != 0;

    bool primary = false;
    DWORD_PTR rd = 0;
    if (GetWindowSubclass(dis->hwndItem, ButtonHoverProc, 1, &rd)) primary = (rd == 1);

    char text[256]; GetWindowTextA(dis->hwndItem, text, 256);
    RECT r = dis->rcItem;
    HDC hdc = dis->hDC;

    if (isCheckbox) {
        // Fill the WHOLE control rect first — otherwise the system's default
        // button-face colour (white) shows through everywhere we don't
        // explicitly paint, which is what caused the white bar artifact.
        Theme::FillRoundRect(hdc, r, 0, Theme::Panel);

        RECT box = { r.left, r.top + (r.bottom - r.top - 16) / 2, r.left + 16, r.top + (r.bottom - r.top - 16) / 2 + 16 };
        COLORREF fill   = checked ? Theme::Accent : Theme::InputBg;
        COLORREF border = checked ? Theme::Accent : (hot ? Theme::Accent2 : Theme::InputBorder);
        Theme::FillRoundRect(hdc, box, 4, fill, border, 1);
        if (checked) {
            HPEN oldPen = (HPEN)SelectObject(hdc, Theme::SolidPen(RGB(20, 20, 26), 2));
            MoveToEx(hdc, box.left + 3, box.top + 8, NULL);
            LineTo(hdc, box.left + 6, box.top + 12);
            LineTo(hdc, box.left + 13, box.top + 4);
            SelectObject(hdc, oldPen);
        }
        RECT txt = { box.right + 8, r.top, r.right, r.bottom };
        Theme::DrawLabel(hdc, txt, text, Theme::Text, g_hFont);
        return;
    }

    COLORREF fill = primary ? (pressed ? Theme::AccentDark : Theme::Accent)
                             : (pressed ? Theme::ButtonFace : (hot ? Theme::ButtonFaceHot : Theme::ButtonFace));
    COLORREF border = primary ? Theme::Accent : (hot ? Theme::Accent2 : Theme::PanelBorder);
    Theme::FillRoundRect(hdc, r, 6, fill, border, 1);
    Theme::DrawLabel(hdc, r, text, primary ? RGB(20, 8, 16) : Theme::Text, g_hFont,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ─── Progress bar subclass: custom gradient fill + percentage text ──────────

static LRESULT CALLBACK ProgressSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR, DWORD_PTR) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT r; GetClientRect(hwnd, &r);
        int pos = (int)SendMessage(hwnd, PBM_GETPOS, 0, 0);

        Theme::FillRoundRect(hdc, r, 10, Theme::InputBg, Theme::PanelBorder, 1);
        if (pos > 0) {
            RECT fillR = r;
            fillR.right = r.left + (int)((r.right - r.left) * (pos / 100.0));
            if (fillR.right > fillR.left + 4) {
                HRGN clip = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, 10, 10);
                SelectClipRgn(hdc, clip);
                Theme::FillRoundRect(hdc, fillR, 10, Theme::Accent);
                SelectClipRgn(hdc, NULL);
                DeleteObject(clip);
            }
        }
        char buf[16]; wsprintfA(buf, "%d%%", pos);
        Theme::DrawLabel(hdc, r, buf, Theme::Text, g_hFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ProgressSubclassProc, 2);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ─── Layout builder ───────────────────────────────────────────────────────────

static HWND MakeField(HWND parent, const char* label, int id, int x, int y, int w, const char* def) {
    CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, 200, 16, parent, NULL, NULL, NULL);
    HWND edit = CreateWindow("EDIT", def, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        x, y + 18, w, 24, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    RECT box = { x - 4, y + 14, x + w + 4, y + 18 + 24 + 4 };
    g_fields.push_back({ edit, box });
    return edit;
}

static void BuildUI(HWND hwnd) {
    g_hFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hTitleFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hSubFont = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hConsoleFont = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_hIconFont = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    const int PAD = 20;
    int panelW = 860 - PAD * 2;
    int y = 84;

    // Panel heights are content-driven, computed once here so the beginPanel() call
    // and the y-accumulation after each section can never drift out of sync again
    // (that mismatch — CONVERSION SETTINGS declared at 96px but needing ~122px for
    // its two field rows — was exactly what caused OPTIONS to overlap it).
    const int FILES_H = 168 + 40;   // 3 file rows + swap/analyze row + P1/P2 info lines below it
    const int CONV_H  = 140;   // 2 rows of 4 fields
    const int OPT_H   = 92;    // 2 rows of checkboxes
    const int CHAR_H  = 84;    // 1 row of 4 fields
    const int CONSOLE_H = 190; // toolbar row + edit box

    auto beginPanel = [&](const char* title, int height) -> RECT {
        RECT r = { PAD, y, PAD + panelW, y + height };
        g_panels.push_back({ r, title });
        return r;
    };

    // ── FILES panel ──
    RECT filesPanel = beginPanel("FILES", FILES_H);
    int fx = filesPanel.left + 16, fy = filesPanel.top + 30;
    auto fileRow = [&](const char* label, HWND& edit, int id, int btnId) {
        CreateWindow("STATIC", label, WS_VISIBLE | WS_CHILD | SS_LEFT, fx, fy, 30, 16, hwnd, NULL, NULL, NULL);
        edit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            fx + 34, fy - 2, panelW - 34 - 200, 24, hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
        SendMessage(edit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        RECT box = { fx + 30, fy - 6, fx + 34 + (panelW - 34 - 200) + 4, fy - 2 + 24 + 4 };
        g_fields.push_back({ edit, box });
        MakeButton(hwnd, "Browse", btnId, fx + panelW - 190, fy - 2, 90, 24, false);
        fy += 34;
    };
    fileRow("P1", g_hP1Edit, ID_EDIT_P1, ID_BTN_P1_BROWSE);
    fileRow("P2", g_hP2Edit, ID_EDIT_P2, ID_BTN_P2_BROWSE);
    fileRow("Out", g_hOutEdit, ID_EDIT_OUT, ID_BTN_OUT_BROWSE);
    SetWindowText(g_hOutEdit, "chart.json");

    g_hSwapBtn   = MakeButton(hwnd, "<-> Swap P1/P2", ID_BTN_SWAP, fx, fy - 2, 130, 24, false);
    g_hDetectBtn = MakeButton(hwnd, "Analyze", ID_BTN_DETECT_BPM, fx + 138, fy - 2, 110, 24, false);
    fy += 30; // drop below the button row instead of squeezing beside it — that's what caused the overlap
    g_hMidiInfoLabelP1 = CreateWindow("STATIC", "P1: pick a MIDI file to see track info.",
        WS_VISIBLE | WS_CHILD | SS_LEFT, fx, fy, panelW - 32, 16, hwnd, NULL, NULL, NULL);
    SendMessage(g_hMidiInfoLabelP1, WM_SETFONT, (WPARAM)g_hSubFont, TRUE);
    fy += 18;
    g_hMidiInfoLabelP2 = CreateWindow("STATIC", "P2: pick a MIDI file to see track info.",
        WS_VISIBLE | WS_CHILD | SS_LEFT, fx, fy, panelW - 32, 16, hwnd, NULL, NULL, NULL);
    SendMessage(g_hMidiInfoLabelP2, WM_SETFONT, (WPARAM)g_hSubFont, TRUE);
    y += FILES_H + 14;

    // ── PATTERNS panel ──
    // Pattern Mode reuses the P1/P2/Out fields above as MASTER MIDI files —
    // no separate master-file fields needed. This panel only adds the toggle
    // and each side's list of pattern MIDI files.
    const int PATTERNS_H = 190;
    RECT patternsPanel = beginPanel("PATTERN MODE", PATTERNS_H);
    int px = patternsPanel.left + 16, py = patternsPanel.top + 30;

    g_hPatternModeCheck = MakeCheckbox(hwnd, "Enable Pattern Mode (P1 / P2 above become Master MIDI files)",
        ID_CHECK_PATTERN_MODE, px, py, panelW - 32, false);
    py += 30;

    int halfW  = (panelW - 32 - 20) / 2;   // 20px gap between the two columns
    int rightX = px + halfW + 20;

    CreateWindow("STATIC", "P1 Patterns  (C3 = pattern 1, C#3 = 2, D3 = 3, ...)",
        WS_VISIBLE | WS_CHILD | SS_LEFT, px, py, halfW, 16, hwnd, NULL, NULL, NULL);
    CreateWindow("STATIC", "P2 Patterns  (C3 = pattern 1, C#3 = 2, D3 = 3, ...)",
        WS_VISIBLE | WS_CHILD | SS_LEFT, rightX, py, halfW, 16, hwnd, NULL, NULL, NULL);
    py += 18;

    g_hPatternListP1 = CreateWindowA("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        px, py, halfW, 76, hwnd, (HMENU)ID_LIST_PATTERN_P1, NULL, NULL);
    SendMessage(g_hPatternListP1, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    { RECT box = { px - 4, py - 4, px + halfW + 4, py + 76 + 4 }; g_fields.push_back({ g_hPatternListP1, box }); }

    g_hPatternListP2 = CreateWindowA("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        rightX, py, halfW, 76, hwnd, (HMENU)ID_LIST_PATTERN_P2, NULL, NULL);
    SendMessage(g_hPatternListP2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    { RECT box = { rightX - 4, py - 4, rightX + halfW + 4, py + 76 + 4 }; g_fields.push_back({ g_hPatternListP2, box }); }

    py += 84;

    int btnW = (halfW - 8) / 3;
    MakeButton(hwnd, "+ Files",  ID_BTN_PATTERN_P1_ADD,    px,                  py, btnW, 22, false);
    MakeButton(hwnd, "+ Folder", ID_BTN_PATTERN_P1_FOLDER, px + btnW + 4,       py, btnW, 22, false);
    MakeButton(hwnd, "Remove",   ID_BTN_PATTERN_P1_REMOVE, px + (btnW + 4) * 2, py, btnW, 22, false);

    MakeButton(hwnd, "+ Files",  ID_BTN_PATTERN_P2_ADD,    rightX,                  py, btnW, 22, false);
    MakeButton(hwnd, "+ Folder", ID_BTN_PATTERN_P2_FOLDER, rightX + btnW + 4,       py, btnW, 22, false);
    MakeButton(hwnd, "Remove",   ID_BTN_PATTERN_P2_REMOVE, rightX + (btnW + 4) * 2, py, btnW, 22, false);

    y += PATTERNS_H + 14;

    // ── CONVERSION panel ──
    RECT convPanel = beginPanel("CONVERSION SETTINGS", CONV_H);
    int cx = convPanel.left + 16, cy = convPanel.top + 30;
    int colW = (panelW - 32) / 4;
    g_hSongEdit  = MakeField(hwnd, "Song",  ID_EDIT_SONG,  cx,            cy, colW - 14, "Converted");
    g_hBPMEdit   = MakeField(hwnd, "BPM x", ID_EDIT_BPM,   cx + colW,     cy, colW - 14, "1.0");
    g_hSpeedEdit = MakeField(hwnd, "Speed", ID_EDIT_SPEED, cx + colW * 2, cy, colW - 14, "2.5");
    g_hManiaEdit = MakeField(hwnd, "Mania", ID_EDIT_MANIA, cx + colW * 3, cy, colW - 14, "3");
    cy += 50;
    g_hVelEdit    = MakeField(hwnd, "Min Vel",  ID_EDIT_VELOCITY,  cx,            cy, colW - 14, "0");
    g_hPrecEdit   = MakeField(hwnd, "Decimals", ID_EDIT_PRECISION, cx + colW,     cy, colW - 14, "6");
    g_hOffsetEdit = MakeField(hwnd, "Offset",   ID_EDIT_OFFSET,    cx + colW * 2, cy, colW - 14, "0");
    g_hRoundEdit  = MakeField(hwnd, "Round",    ID_EDIT_ROUND,     cx + colW * 3, cy, colW - 14, "-1");
    y += CONV_H + 14;

    // ── OPTIONS panel ──
    RECT optPanel = beginPanel("OPTIONS", OPT_H);
    int ox = optPanel.left + 16, oy = optPanel.top + 30;
    int checkColW = (panelW - 32) / 4;
    g_hPrecisionCheck = MakeCheckbox(hwnd, "High Precision",      ID_CHECK_PRECISION, ox, oy, checkColW, true);
    g_hSustainCheck   = MakeCheckbox(hwnd, "Sustain Notes",       ID_CHECK_SUSTAIN,   ox + checkColW, oy, checkColW, false);
    g_hMinifyCheck    = MakeCheckbox(hwnd, "Minify JSON",         ID_CHECK_MINIFY,    ox + checkColW * 2, oy, checkColW, true);
    g_hSmartMapCheck  = MakeCheckbox(hwnd, "Smart Pitch Mapping", ID_CHECK_SMART_MAP, ox + checkColW * 3, oy, checkColW, true);
    oy += 32;
    g_hSplitCheck = MakeCheckbox(hwnd, "Split Output", ID_CHECK_SPLIT, ox, oy, checkColW, false);
    CreateWindow("STATIC", "Notes/File", WS_VISIBLE | WS_CHILD | SS_LEFT, ox + checkColW, oy + 3, 80, 16, hwnd, NULL, NULL, NULL);
    g_hSplitNotesEdit = CreateWindow("EDIT", "1000", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
        ox + checkColW + 84, oy, 70, 22, hwnd, (HMENU)ID_EDIT_SPLIT_NOTES, NULL, NULL);
    SendMessage(g_hSplitNotesEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    { RECT box = { ox + checkColW + 80, oy - 4, ox + checkColW + 84 + 70 + 4, oy + 22 + 4 };
      g_fields.push_back({ g_hSplitNotesEdit, box }); }
    y += OPT_H + 14;

    // ── CHARACTERS panel ──
    RECT charPanel = beginPanel("CHARACTERS", CHAR_H);
    int chx = charPanel.left + 16, chy = charPanel.top + 30;
    int charColW = (panelW - 32) / 4;
    g_hP1CharEdit = MakeField(hwnd, "P1",    ID_EDIT_P1CHAR, chx,                chy, charColW - 14, "bf");
    g_hP2CharEdit = MakeField(hwnd, "P2",    ID_EDIT_P2CHAR, chx + charColW,     chy, charColW - 14, "dad");
    g_hGFCharEdit = MakeField(hwnd, "GF",    ID_EDIT_GFCHAR, chx + charColW * 2, chy, charColW - 14, "gf");
    g_hStageEdit  = MakeField(hwnd, "Stage", ID_EDIT_STAGE,  chx + charColW * 3, chy, charColW - 14, "stage");
    y += CHAR_H + 16;

    // ── Preset row ──
    g_hSavePresetBtn = MakeButton(hwnd, "Save Preset", ID_BTN_SAVE_PRESET, PAD, y, 150, 28, false);
    g_hLoadPresetBtn = MakeButton(hwnd, "Load Preset", ID_BTN_LOAD_PRESET, PAD + 158, y, 150, 28, false);
    y += 28 + 16;

    // ── ACTION BAR: progress + convert ──
    g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_VISIBLE | WS_CHILD,
        PAD, y, panelW - 160, 32, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
    SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SetWindowSubclass(g_hProgress, ProgressSubclassProc, 2, 0);
    MakeButton(hwnd, "CONVERT", ID_BTN_CONVERT, PAD + panelW - 150, y, 150, 32, true);
    y += 32 + 16;

    // ── CONSOLE ──
    RECT consolePanel = beginPanel("CONSOLE OUTPUT", CONSOLE_H);
    MakeButton(hwnd, "Clear", ID_BTN_CLEAR_LOG, consolePanel.right - 90, consolePanel.top + 4, 74, 22, false);
    g_hOpenFolderBtn = MakeButton(hwnd, "Open Folder", ID_BTN_OPEN_FOLDER,
        consolePanel.right - 250, consolePanel.top + 4, 150, 22, false);

    LoadLibrary("Msftedit.dll");
    g_hConsole = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        consolePanel.left + 12, consolePanel.top + 32, panelW - 24, 150, hwnd, (HMENU)ID_CONSOLE, NULL, NULL);
    SendMessage(g_hConsole, WM_SETFONT, (WPARAM)g_hConsoleFont, TRUE);
    SendMessage(g_hConsole, EM_SETBKGNDCOLOR, 0, Theme::InputBg);

    guiLogger.setConsole(g_hConsole);
    guiLogger.logColored("MIDI to Psych Converter V2.0 ready!\n", CYAN);
    guiLogger.logColored("Drag & drop MIDI files onto the window, or browse manually.\n\n", MAGENTA);

    y += CONSOLE_H + PAD;

    // Toast area (bottom strip, drawn on demand)
    g_toastRect = { PAD, y, PAD + panelW, y + 40 };

    DragAcceptFiles(hwnd, TRUE);
}

// ─── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DWORD corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        BuildUI(hwnd);
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        HWND ctrl = (HWND)lParam;

        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
            for (auto& f : g_fields) if (f.edit == ctrl) {
                RECT r = f.box; InflateRect(&r, 2, 2);
                InvalidateRect(hwnd, &r, FALSE);
            }
            break;
        }
        if (code == EN_CHANGE && g_invalidFields.count(ctrl)) {
            MarkFieldInvalid(ctrl, false);
        }
        if (code == BN_CLICKED && g_checkState.count(ctrl)) {
            g_checkState[ctrl] = !g_checkState[ctrl];
            InvalidateRect(ctrl, NULL, FALSE);
            break;
        }

        switch (id) {
            case ID_BTN_P1_BROWSE: {
                auto f = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                if (!f.empty()) { SetWindowText(g_hP1Edit, f.c_str()); MarkFieldInvalid(g_hP1Edit, false); AnalyzeMidiAsync(f, 1); }
                break;
            }
            case ID_BTN_P2_BROWSE: {
                auto f = BrowseForFile(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                if (!f.empty()) { SetWindowText(g_hP2Edit, f.c_str()); MarkFieldInvalid(g_hP2Edit, false); AnalyzeMidiAsync(f, 2); }
                break;
            }
            case ID_BTN_OUT_BROWSE: {
                auto f = BrowseForFile(hwnd, "JSON Files (*.json)\0*.json\0All Files\0*.*\0", true);
                if (!f.empty()) { SetWindowText(g_hOutEdit, f.c_str()); MarkFieldInvalid(g_hOutEdit, false); }
                break;
            }
            case ID_BTN_SWAP: {
                std::string p1 = GetWindowTextStr(g_hP1Edit), p2 = GetWindowTextStr(g_hP2Edit);
                SetWindowText(g_hP1Edit, p2.c_str()); SetWindowText(g_hP2Edit, p1.c_str());
                std::string c1 = GetWindowTextStr(g_hP1CharEdit), c2 = GetWindowTextStr(g_hP2CharEdit);
                SetWindowText(g_hP1CharEdit, c2.c_str()); SetWindowText(g_hP2CharEdit, c1.c_str());
                ShowToast("Swapped P1 and P2.", true);
                break;
            }
            case ID_BTN_DETECT_BPM: {
                std::string p1 = GetWindowTextStr(g_hP1Edit), p2 = GetWindowTextStr(g_hP2Edit);
                if (p1.empty() && p2.empty()) { ShowToast("Pick a MIDI file first.", false); break; }
                if (!p1.empty()) { SetMidiInfoText(1, "Analyzing..."); AnalyzeMidiAsync(p1, 1); }
                if (!p2.empty()) { SetMidiInfoText(2, "Analyzing..."); AnalyzeMidiAsync(p2, 2); }
                break;
            }
            case ID_BTN_SAVE_PRESET: SavePreset(hwnd); break;
            case ID_BTN_LOAD_PRESET: LoadPreset(hwnd); break;
            case ID_BTN_OPEN_FOLDER: {
                std::string path = g_lastOutFile.empty() ? GetWindowTextStr(g_hOutEdit) : g_lastOutFile;
                if (path.empty()) { ShowToast("No output file yet.", false); break; }
                auto pos = path.find_last_of("/\\");
                std::string dir = pos == std::string::npos ? "." : path.substr(0, pos);
                ShellExecuteA(hwnd, "explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                break;
            }
            case ID_BTN_CONVERT:
                if (!g_converting) DoConversion();
                else ShowToast("Conversion already in progress!", false);
                break;
            case ID_BTN_CLEAR_LOG:
                SetWindowText(g_hConsole, "");
                guiLogger.logColored("Console cleared.\n", CYAN);
                break;

            case ID_BTN_PATTERN_P1_ADD: {
                auto files = BrowseForFilesMulti(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                for (auto& f : files) g_patternPathsP1.push_back(f);
                RefreshPatternListBox(g_hPatternListP1, g_patternPathsP1);
                break;
            }
            case ID_BTN_PATTERN_P2_ADD: {
                auto files = BrowseForFilesMulti(hwnd, "MIDI Files (*.mid)\0*.mid\0All Files\0*.*\0");
                for (auto& f : files) g_patternPathsP2.push_back(f);
                RefreshPatternListBox(g_hPatternListP2, g_patternPathsP2);
                break;
            }
            case ID_BTN_PATTERN_P1_FOLDER: {
                auto folder = BrowseForFolder(hwnd);
                if (folder.empty()) break;
                auto files = ListMidiFilesInFolder(folder);
                if (files.empty()) { ShowToast("No .mid files found in that folder.", false); break; }
                for (auto& f : files) g_patternPathsP1.push_back(f);
                RefreshPatternListBox(g_hPatternListP1, g_patternPathsP1);
                break;
            }
            case ID_BTN_PATTERN_P2_FOLDER: {
                auto folder = BrowseForFolder(hwnd);
                if (folder.empty()) break;
                auto files = ListMidiFilesInFolder(folder);
                if (files.empty()) { ShowToast("No .mid files found in that folder.", false); break; }
                for (auto& f : files) g_patternPathsP2.push_back(f);
                RefreshPatternListBox(g_hPatternListP2, g_patternPathsP2);
                break;
            }
            case ID_BTN_PATTERN_P1_REMOVE: {
                int sel = (int)SendMessage(g_hPatternListP1, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR && sel < (int)g_patternPathsP1.size())
                    g_patternPathsP1.erase(g_patternPathsP1.begin() + sel);
                RefreshPatternListBox(g_hPatternListP1, g_patternPathsP1);
                break;
            }
            case ID_BTN_PATTERN_P2_REMOVE: {
                int sel = (int)SendMessage(g_hPatternListP2, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR && sel < (int)g_patternPathsP2.size())
                    g_patternPathsP2.erase(g_patternPathsP2.begin() + sel);
                RefreshPatternListBox(g_hPatternListP2, g_patternPathsP2);
                break;
            }
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        char path[MAX_PATH];
        UINT count = DragQueryFileA(drop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count && i < 2; ++i) {
            DragQueryFileA(drop, i, path, MAX_PATH);
            HWND target = GetWindowTextStr(g_hP1Edit).empty() ? g_hP1Edit
                        : (GetWindowTextStr(g_hP2Edit).empty() ? g_hP2Edit : g_hP1Edit);
            SetWindowTextA(target, path);
            MarkFieldInvalid(target, false);
            AnalyzeMidiAsync(path, target == g_hP1Edit ? 1 : 2);
        }
        DragFinish(drop);
        ShowToast("MIDI file(s) loaded.", true);
        break;
    }

    case WM_APP_MIDI_INFO: {
        auto* mi = reinterpret_cast<::MidiInfo*>(lParam);
        char buf[256];
        if (mi->ok) {
            wsprintfA(buf, "P%d: %d tracks - %d notes - ~%.1f BPM - %d:%02d",
                mi->player, mi->trackCount, mi->noteCount, mi->bpm,
                (int)(mi->durationSec) / 60, (int)(mi->durationSec) % 60);
        } else {
            wsprintfA(buf, "P%d: couldn't read that MIDI file.", mi->player);
        }
        SetMidiInfoText(mi->player, buf);
        delete mi;
        break;
    }

    case WM_APP_CONVERT_DONE: {
        if (wParam) ShowToast("Conversion completed successfully!", true);
        else        ShowToast("Conversion failed - check the console.", false);
        break;
    }

    case WM_TIMER:
        if (wParam == TIMER_TOAST_HIDE) {
            KillTimer(hwnd, TIMER_TOAST_HIDE);
            g_toastVisible = false;
            InvalidateRect(hwnd, &g_toastRect, FALSE);
        }
        break;

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType == ODT_BUTTON) { DrawOwnerButton(dis); return TRUE; }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ((HWND)lParam == g_hMidiInfoLabelP1 || (HWND)lParam == g_hMidiInfoLabelP2) ? Theme::Accent2 : Theme::TextDim);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, Theme::InputBg);
        SetTextColor(hdc, Theme::Text);
        return (LRESULT)Theme::SolidBrush(Theme::InputBg);
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, Theme::InputBg);
        SetTextColor(hdc, Theme::Text);
        return (LRESULT)Theme::SolidBrush(Theme::InputBg);
    }

    case WM_ERASEBKGND:
        return 1; // fully repainted in WM_PAINT, avoid flicker

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client; GetClientRect(hwnd, &client);

        Theme::FillRoundRect(hdc, client, 0, Theme::Background);

        // Header
        RECT header = { 0, 0, client.right, 74 };
        Theme::FillRoundRect(hdc, header, 0, Theme::HeaderTop);
        RECT logo = { 20, 16, 62, 58 };
        Theme::FillRoundRect(hdc, logo, 12, Theme::Accent);
        // Hand-drawn eighth-note glyph — avoids any font/encoding pitfalls entirely.
        {
            HBRUSH noteBrush = Theme::SolidBrush(RGB(20, 8, 16));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, noteBrush);
            HPEN   oldPen   = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
            int cx = logo.left, cy = logo.top;
            Ellipse(hdc, cx + 10, cy + 26, cx + 20, cy + 34);      // notehead
            Rectangle(hdc, cx + 18, cy + 8, cx + 21, cy + 30);     // stem
            Rectangle(hdc, cx + 18, cy + 8, cx + 27, cy + 14);     // flag
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }
        RECT titleR = { 74, 14, client.right - 20, 42 };
        Theme::DrawLabel(hdc, titleR, "MIDI2PSYCH", Theme::Text, g_hTitleFont);
        RECT subR = { 74, 42, client.right - 20, 62 };
        Theme::DrawLabel(hdc, subR, "MIDI -> Psych Engine chart converter  -  v2.0", Theme::TextDim, g_hSubFont);

        // Section panels
        for (auto& p : g_panels) {
            Theme::FillRoundRect(hdc, p.r, 10, Theme::Panel, Theme::PanelBorder, 1);
            RECT titleArea = { p.r.left + 14, p.r.top + 8, p.r.right - 14, p.r.top + 26 };
            Theme::DrawLabel(hdc, titleArea, p.title, Theme::Accent2, g_hSubFont);
        }

        // Field frames
        for (auto& f : g_fields) {
            bool invalid = g_invalidFields.count(f.edit) > 0;
            bool focused = GetFocus() == f.edit;
            COLORREF border = invalid ? Theme::InputBorderBad
                             : focused ? Theme::InputBorderFocus
                             : Theme::InputBorder;
            Theme::FillRoundRect(hdc, f.box, 5, Theme::InputBg, border, (focused || invalid) ? 2 : 1);
        }

        // Toast
        if (g_toastVisible) {
            COLORREF c = g_toastSuccess ? Theme::Success : Theme::Error;
            Theme::FillRoundRect(hdc, g_toastRect, 8, Theme::Panel, c, 2);
            RECT textR = g_toastRect; textR.left += 14; textR.right -= 14;
            Theme::DrawLabel(hdc, textR, g_toastText, c, g_hFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        DeleteObject(g_hFont);
        DeleteObject(g_hTitleFont);
        DeleteObject(g_hSubFont);
        DeleteObject(g_hConsoleFont);
        DeleteObject(g_hIconFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

#endif // _WIN32