// Colours, fonts, and DPI helpers shared by the windows this program draws.
//
// One place, so the detail panel and the setup dialog cannot drift apart.
#pragma once

#include <windows.h>

namespace cwut {

struct Palette {
    COLORREF background;
    COLORREF border;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF accentPressed;
    COLORREF track;
    COLORREF warning;
    COLORREF onAccent;
};

Palette ThemePalette(bool lightTheme);

int DpiForWindow(HWND hwnd);
int Scaled(int value, int dpi);

// Height is given in tenths of a point, so 95 means 9.5pt.
HFONT MakeFont(int pointsTimesTen, int weight, int dpi);

void FillRect(HDC dc, const RECT& rect, COLORREF color);
void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF color);

}  // namespace cwut
