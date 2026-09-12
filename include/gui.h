#pragma once

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

// ─── Control IDs ──────────────────────────────────────────────────────────────
#define ID_BTN_P1_BROWSE   1001
#define ID_BTN_P2_BROWSE   1002
#define ID_BTN_OUT_BROWSE  1003
#define ID_BTN_CONVERT     1004
#define ID_BTN_CLEAR_LOG   1005
#define ID_EDIT_P1         1010
#define ID_EDIT_P2         1011
#define ID_EDIT_OUT        1012
#define ID_EDIT_SONG       1013
#define ID_EDIT_BPM        1014
#define ID_EDIT_OFFSET     1015
#define ID_EDIT_VELOCITY   1016
#define ID_EDIT_PRECISION  1017
#define ID_EDIT_SPEED      1018
#define ID_EDIT_P1CHAR     1019
#define ID_EDIT_P2CHAR     1020
#define ID_EDIT_GFCHAR     1021
#define ID_EDIT_STAGE      1022
#define ID_EDIT_MANIA      1024
#define ID_EDIT_SPLIT_NOTES 1023
#define ID_CHECK_SUSTAIN   1030
#define ID_CHECK_PRECISION 1031
#define ID_CHECK_SPLIT     1032
#define ID_CHECK_MINIFY    1033
#define ID_EDIT_ROUND      1034
#define ID_PROGRESS        1040
#define ID_CONSOLE         1050
#define ID_CHECK_SMART_MAP 1035

// ── New in the overhaul ──
#define ID_BTN_SWAP         1060
#define ID_BTN_DETECT_BPM   1061
#define ID_BTN_SAVE_PRESET  1062
#define ID_BTN_LOAD_PRESET  1063
#define ID_BTN_OPEN_FOLDER  1064

// ── Pattern Mode ──
#define ID_CHECK_PATTERN_MODE     1070
#define ID_LIST_PATTERN_P1        1071
#define ID_LIST_PATTERN_P2        1072
#define ID_BTN_PATTERN_P1_ADD     1073
#define ID_BTN_PATTERN_P1_FOLDER  1074
#define ID_BTN_PATTERN_P1_REMOVE  1075
#define ID_BTN_PATTERN_P2_ADD     1076
#define ID_BTN_PATTERN_P2_FOLDER  1077
#define ID_BTN_PATTERN_P2_REMOVE  1078

// ── Custom messages (posted from worker threads back to the UI thread) ──
#define WM_APP_MIDI_INFO    (WM_APP + 1)   // wParam: 1=P1, 2=P2 | lParam: MidiInfo*
#define WM_APP_CONVERT_DONE (WM_APP + 2)   // wParam: 1=ok, 0=fail

#define TIMER_TOAST_HIDE    9001

// ─── Globals (defined in gui.cpp) ────────────────────────────────────────────
extern HWND g_hMainWnd;
extern HWND g_hP1Edit, g_hP2Edit, g_hOutEdit, g_hConsole, g_hProgress;
extern HWND g_hSongEdit, g_hBPMEdit, g_hOffsetEdit, g_hVelEdit, g_hPrecEdit, g_hSpeedEdit;
extern HWND g_hP1CharEdit, g_hP2CharEdit, g_hGFCharEdit, g_hStageEdit;
extern HWND g_hSplitNotesEdit, g_hRoundEdit, g_hManiaEdit;
extern HWND g_hSustainCheck, g_hPrecisionCheck, g_hSplitCheck, g_hMinifyCheck, g_hSmartMapCheck;
extern HWND g_hSwapBtn, g_hDetectBtn, g_hSavePresetBtn, g_hLoadPresetBtn, g_hOpenFolderBtn;
extern HWND g_hMidiInfoLabelP1;
extern HWND g_hMidiInfoLabelP2;
extern HWND g_hPatternModeCheck, g_hPatternListP1, g_hPatternListP2;
extern HFONT g_hFont, g_hTitleFont, g_hSubFont, g_hConsoleFont, g_hIconFont;
extern bool  g_converting;

// A labelled input "card slot" the background painter draws a frame around.
struct FieldBox {
    HWND edit;
    RECT box;       // outer frame, edit control sits inset within it
};
extern std::vector<FieldBox> g_fields;

// Owner-drawn checkbox state (BS_OWNERDRAW disables automatic BM_GETCHECK bookkeeping)
extern std::unordered_map<HWND, bool> g_checkState;
inline bool IsChecked(HWND h) {
    auto it = g_checkState.find(h);
    return it != g_checkState.end() && it->second;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
std::string BrowseForFile(HWND hwnd, const char* filter, bool save = false);
std::string GetWindowTextStr(HWND hwnd);
void        DoConversion();
void        ShowToast(const std::string& text, bool success);
void        AnalyzeMidiAsync(const std::string& path, int player);
void        SavePreset(HWND hwnd);
void        LoadPreset(HWND hwnd);
void        MarkFieldInvalid(HWND edit, bool invalid);

// ─── Window procedure ────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif // _WIN32
