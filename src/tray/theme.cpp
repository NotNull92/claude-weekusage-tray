#include "theme.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {

Palette ThemePalette(bool lightTheme) {
    Palette p{};
    if (lightTheme) {
        p.background = RGB(0xFF, 0xFF, 0xFF);
        p.border = RGB(0xD6, 0xD3, 0xD1);
        p.text = RGB(0x1C, 0x1B, 0x1A);
        p.muted = RGB(0x6B, 0x66, 0x62);
        p.accent = RGB(0xC1, 0x5F, 0x3C);
        p.accentPressed = RGB(0xA5, 0x4E, 0x30);
        p.track = RGB(0xE7, 0xE5, 0xE4);
        p.warning = RGB(0x9A, 0x62, 0x00);
        p.onAccent = RGB(0xFF, 0xFF, 0xFF);
    } else {
        p.background = RGB(0x1B, 0x1A, 0x19);
        p.border = RGB(0x3A, 0x37, 0x35);
        p.text = RGB(0xF5, 0xF3, 0xF1);
        p.muted = RGB(0xA3, 0x9E, 0x99);
        p.accent = RGB(0xD9, 0x77, 0x57);
        p.accentPressed = RGB(0xBE, 0x63, 0x45);
        p.track = RGB(0x33, 0x30, 0x2E);
        p.warning = RGB(0xE0, 0xA4, 0x4C);
        p.onAccent = RGB(0x1B, 0x1A, 0x19);
    }
    return p;
}

int DpiForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn != nullptr && hwnd != nullptr) {
        UINT dpi = fn(hwnd);
        if (dpi >= 72) return static_cast<int>(dpi);
    }
    HDC dc = GetDC(nullptr);
    int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) ReleaseDC(nullptr, dc);
    return dpi >= 72 ? dpi : 96;
}

int Scaled(int value, int dpi) { return MulDiv(value, dpi, 96); }

HFONT MakeFont(int pointsTimesTen, int weight, int dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pointsTimesTen, dpi, 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void FillRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    ::FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

}  // namespace cwut
