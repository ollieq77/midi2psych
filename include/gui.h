#pragma once

#ifdef _WIN32

#include <windows.h>
#include <string>

// ─── Control IDs ──────────────────────────────────────────────────────────────
#define ID_BTN_P1_BROWSE  1001
#define ID_BTN_P2_BROWSE  1002
#define ID_BTN_OUT_BROWSE 1003
#define ID_BTN_CONVERT    1004
#define ID_BTN_CLEAR_LOG  1005
#define ID_EDIT_P1        1010
#define ID_EDIT_P2        1011
#define ID_EDIT_OUT       1012
#define ID_EDIT_SONG      1013
#define ID_EDIT_BPM       1014
#define ID_EDIT_OFFSET    1015
#define ID_EDIT_VELOCITY  1016
#define ID_EDIT_PRECISION 1017
#define ID_EDIT_SPEED     1018
#define ID_EDIT_P1CHAR    1019
#define ID_EDIT_P2CHAR    1020
#define ID_EDIT_GFCHAR    1021
#define ID_EDIT_STAGE     1022
#define ID_EDIT_MANIA     1024  // 0=1-key, 2=3-key, 3=4-key(default), 4=5-key...
#define ID_EDIT_SPLIT_NOTES 1023
#define ID_CHECK_SUSTAIN  1030
#define ID_CHECK_PRECISION 1031
#define ID_CHECK_SPLIT    1032
#define ID_CHECK_MINIFY   1033
#define ID_EDIT_ROUND     1034
#define ID_PROGRESS       1040
#define ID_CONSOLE        1050

// ─── Globals (defined in gui.cpp) ────────────────────────────────────────────
extern HWND g_hMainWnd;
extern HWND g_hP1Edit, g_hP2Edit, g_hOutEdit, g_hConsole, g_hProgress;
extern HWND g_hSongEdit, g_hBPMEdit, g_hOffsetEdit, g_hVelEdit, g_hPrecEdit, g_hSpeedEdit;
extern HWND g_hP1CharEdit, g_hP2CharEdit, g_hGFCharEdit, g_hStageEdit;
extern HWND g_hSplitNotesEdit, g_hRoundEdit, g_hManiaEdit;
extern HWND g_hSustainCheck, g_hPrecisionCheck, g_hSplitCheck, g_hMinifyCheck;
extern HFONT g_hFont, g_hTitleFont, g_hConsoleFont;
extern bool  g_converting;

// ─── Helpers ─────────────────────────────────────────────────────────────────
std::string BrowseForFile(HWND hwnd, const char* filter, bool save = false);
std::string GetWindowTextStr(HWND hwnd);
void        DoConversion();

// ─── Window procedure ────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif // _WIN32
