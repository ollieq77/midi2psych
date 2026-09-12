#pragma once

#ifdef _WIN32

#include <windows.h>
#include <string>
#include <unordered_map>
#include <cstdint>

// ─── Palette ──────────────────────────────────────────────────────────────────
// A dark, "Psych Engine" flavoured theme: near-black panels, hot-pink primary
// accent, cyan secondary accent. No Windows-98 grey in sight.
namespace Theme {
    inline constexpr COLORREF Background     = RGB(15, 15, 20);
    inline constexpr COLORREF HeaderTop      = RGB(30, 14, 38);
    inline constexpr COLORREF HeaderBottom   = RGB(20, 10, 28);
    inline constexpr COLORREF Panel         = RGB(24, 24, 32);
    inline constexpr COLORREF PanelBorder   = RGB(45, 45, 58);
    inline constexpr COLORREF InputBg       = RGB(18, 18, 24);
    inline constexpr COLORREF InputBorder   = RGB(55, 55, 70);
    inline constexpr COLORREF InputBorderFocus = RGB(255, 70, 150);
    inline constexpr COLORREF InputBorderBad   = RGB(255, 70, 70);
    inline constexpr COLORREF Text          = RGB(235, 235, 245);
    inline constexpr COLORREF TextDim       = RGB(150, 150, 168);
    inline constexpr COLORREF Accent        = RGB(255, 60, 150);   // hot pink
    inline constexpr COLORREF AccentDark    = RGB(190, 35, 115);
    inline constexpr COLORREF Accent2       = RGB(80, 220, 255);   // cyan
    inline constexpr COLORREF Success       = RGB(90, 230, 150);
    inline constexpr COLORREF Error         = RGB(255, 90, 90);
    inline constexpr COLORREF ButtonFace    = RGB(38, 38, 50);
    inline constexpr COLORREF ButtonFaceHot = RGB(52, 52, 68);

    // ── brush cache so we're not creating/destroying GDI objects every frame ──
    inline HBRUSH SolidBrush(COLORREF c) {
        static std::unordered_map<COLORREF, HBRUSH> cache;
        auto it = cache.find(c);
        if (it != cache.end()) return it->second;
        HBRUSH b = CreateSolidBrush(c);
        cache[c] = b;
        return b;
    }

    inline HPEN SolidPen(COLORREF c, int width = 1) {
        static std::unordered_map<uint64_t, HPEN> cache;
        uint64_t key = (uint64_t)c | ((uint64_t)width << 32);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        HPEN p = CreatePen(PS_SOLID, width, c);
        cache[key] = p;
        return p;
    }

    // Draw a filled rounded rectangle with an optional border.
    inline void FillRoundRect(HDC hdc, RECT r, int radius, COLORREF fill,
                              COLORREF border = CLR_INVALID, int borderWidth = 1) {
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, SolidBrush(fill));
        HPEN   oldPen   = (HPEN)SelectObject(hdc, border == CLR_INVALID
                                ? (HPEN)GetStockObject(NULL_PEN)
                                : SolidPen(border, borderWidth));
        RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }

    // Draw text centred vertically, left/centre aligned horizontally.
    inline void DrawLabel(HDC hdc, RECT r, const std::string& text, COLORREF color,
                          HFONT font, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, color);
        HFONT oldFont = (HFONT)SelectObject(hdc, font);
        DrawTextA(hdc, text.c_str(), -1, &r, format);
        SelectObject(hdc, oldFont);
    }
}

#endif // _WIN32
