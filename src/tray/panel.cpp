#include "panel.h"

#include <windowsx.h>

#include <string>

#include "../common/winutil.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

const wchar_t* kPanelClass = L"ClaudeWeekUsageTray.Panel";
constexpr int kBaseWidth = 322;
constexpr unsigned long long kFocusGraceMs = 700;

struct Palette {
    COLORREF background;
    COLORREF border;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF track;
    COLORREF warning;
};

Palette palette(bool light) {
    Palette p{};
    if (light) {
        p.background = RGB(0xFF, 0xFF, 0xFF);
        p.border = RGB(0xD6, 0xD3, 0xD1);
        p.text = RGB(0x1C, 0x1B, 0x1A);
        p.muted = RGB(0x6B, 0x66, 0x62);
        p.accent = RGB(0xC1, 0x5F, 0x3C);
        p.track = RGB(0xE7, 0xE5, 0xE4);
        p.warning = RGB(0x9A, 0x62, 0x00);
    } else {
        p.background = RGB(0x1B, 0x1A, 0x19);
        p.border = RGB(0x3A, 0x37, 0x35);
        p.text = RGB(0xF5, 0xF3, 0xF1);
        p.muted = RGB(0xA3, 0x9E, 0x99);
        p.accent = RGB(0xD9, 0x77, 0x57);
        p.track = RGB(0x33, 0x30, 0x2E);
        p.warning = RGB(0xE0, 0xA4, 0x4C);
    }
    return p;
}

int dpiForWindow(HWND hwnd) {
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

int scaled(int value, int dpi) { return MulDiv(value, dpi, 96); }

HFONT makeFont(int pointsTimesTen, int weight, int dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pointsTimesTen, dpi, 720);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

std::wstring remainingText(const RateWindow& window) {
    if (!window.hasUsage) return L"Unavailable";
    return std::to_wstring(window.remainingPercent()) + L"% remaining";
}

}  // namespace

// Draws the panel, or measures it when `draw` is false. Both paths walk the
// same cursor so the window height always matches the content.
static int renderPanel(HDC dc, int width, int dpi, const UsageSnapshot& snapshot, bool draw,
                       RECT* closeButtonOut) {
    const bool light = SystemUsesLightTheme();
    const Palette colors = palette(light);
    const long long now = NowUnix();
    const bool stale = IsStale(snapshot, now);
    const bool haveData = snapshot.receivedAtUnix != 0 && snapshot.hasAnyUsage();

    HFONT fontTitle = makeFont(150, FW_BOLD, dpi);
    HFONT fontSection = makeFont(85, FW_SEMIBOLD, dpi);
    HFONT fontValue = makeFont(150, FW_BOLD, dpi);
    HFONT fontBody = makeFont(95, FW_NORMAL, dpi);
    HFONT fontSmall = makeFont(85, FW_NORMAL, dpi);

    const int padding = scaled(16, dpi);
    const int contentWidth = width - padding * 2;
    int y = padding;

    auto drawText = [&](const std::wstring& text, HFONT font, COLORREF color, int lineHeight,
                        int indent) {
        if (draw) {
            HGDIOBJ previous = SelectObject(dc, font);
            SetTextColor(dc, color);
            RECT rect{padding + indent, y, padding + contentWidth, y + lineHeight};
            DrawTextW(dc, text.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, previous);
        }
        y += lineHeight;
    };

    auto drawWrapped = [&](const std::wstring& text, HFONT font, COLORREF color) {
        RECT rect{padding, y, padding + contentWidth, y + scaled(200, dpi)};
        HGDIOBJ previous = SelectObject(dc, font);
        int height = DrawTextW(dc, text.c_str(), -1, &rect,
                               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        if (draw) {
            SetTextColor(dc, color);
            RECT drawRect{padding, y, padding + contentWidth, y + height};
            DrawTextW(dc, text.c_str(), -1, &drawRect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectObject(dc, previous);
        y += height;
    };

    auto drawBar = [&](const RateWindow& window) {
        const int barHeight = scaled(6, dpi);
        RECT track{padding, y, padding + contentWidth, y + barHeight};
        if (draw) {
            fill(dc, track, colors.track);
            if (window.hasUsage) {
                int filled = MulDiv(contentWidth, window.remainingPercent(), 100);
                if (filled > 0) {
                    RECT bar{padding, y, padding + filled, y + barHeight};
                    fill(dc, bar, colors.accent);
                }
            }
        }
        y += barHeight;
    };

    auto drawSection = [&](const wchar_t* label, const RateWindow& window) {
        drawText(label, fontSection, colors.muted, scaled(18, dpi), 0);
        drawText(remainingText(window), fontValue, window.hasUsage ? colors.text : colors.muted,
                 scaled(26, dpi), 0);
        y += scaled(4, dpi);
        drawBar(window);
        y += scaled(6, dpi);
        const std::wstring reset = L"Resets " + ToWide(FormatResetTime(window, now));
        drawText(window.hasReset ? reset : L"Reset time unavailable", fontBody, colors.muted,
                 scaled(19, dpi), 0);
        y += scaled(14, dpi);
    };

    // Header.
    if (draw && closeButtonOut != nullptr) {
        const int box = scaled(22, dpi);
        RECT close{width - padding - box, padding - scaled(2, dpi), width - padding,
                   padding - scaled(2, dpi) + box};
        *closeButtonOut = close;
        HFONT closeFont = makeFont(110, FW_NORMAL, dpi);
        HGDIOBJ previous = SelectObject(dc, closeFont);
        SetTextColor(dc, colors.muted);
        DrawTextW(dc, L"✕", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        DeleteObject(closeFont);
    }
    drawText(L"CLAUDE", fontTitle, colors.text, scaled(24, dpi), 0);
    drawText(L"Claude Code subscription usage", fontSmall, colors.muted, scaled(20, dpi), 0);
    y += scaled(8, dpi);

    if (!haveData) {
        drawWrapped(L"No usage data yet. Run Claude Code with the ClaudeWeekUsageTray status line "
                    L"configured; this panel fills in as soon as Claude Code sends a payload.",
                    fontBody, colors.muted);
        y += scaled(12, dpi);
    } else if (stale) {
        drawWrapped(L"Stale: Claude Code has not sent an update recently. The figures below are "
                    L"the last ones received, not a fresh reading.",
                    fontBody, colors.warning);
        y += scaled(12, dpi);
    }

    drawSection(L"5-HOUR LIMIT", snapshot.fiveHour);
    drawSection(L"7-DAY LIMIT", snapshot.sevenDay);

    if (draw) {
        RECT rule{padding, y, width - padding, y + 1};
        fill(dc, rule, colors.border);
    }
    y += scaled(1, dpi) + scaled(10, dpi);

    const std::wstring updated =
        L"Last update: " + ToWide(FormatRelativeAge(snapshot.receivedAtUnix, now)) +
        (stale ? L" (stale)" : L"");
    drawText(updated, fontSmall, stale ? colors.warning : colors.muted, scaled(18, dpi), 0);
    drawText(L"Updates arrive only while Claude Code is running.", fontSmall, colors.muted,
             scaled(18, dpi), 0);
    y += padding;

    DeleteObject(fontTitle);
    DeleteObject(fontSection);
    DeleteObject(fontValue);
    DeleteObject(fontBody);
    DeleteObject(fontSmall);
    return y;
}

bool DetailPanel::create(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &DetailPanel::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPanelClass, L"Claude usage",
                            WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance, this);
    return hwnd_ != nullptr;
}

void DetailPanel::destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void DetailPanel::setSnapshot(const UsageSnapshot& snapshot) {
    snapshot_ = snapshot;
    if (hwnd_ != nullptr && IsWindowVisible(hwnd_) && hasAnchor_) {
        // Re-lay out against the original anchor, because the content height
        // can change when a stale banner appears or disappears.
        show(lastAnchor_);
    }
}

int DetailPanel::measureHeight(int dpi) const {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    int height = renderPanel(memory, scaled(kBaseWidth, dpi), dpi, snapshot_, false, nullptr);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return height;
}

void DetailPanel::show(const RECT& anchor) {
    if (hwnd_ == nullptr) return;
    lastAnchor_ = anchor;
    hasAnchor_ = true;
    const int dpi = dpiForWindow(hwnd_);
    const int width = scaled(kBaseWidth, dpi);
    const int height = measureHeight(dpi);

    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const RECT work = mi.rcWork;

    int x = anchor.right - width;
    int y = anchor.top - height - scaled(8, dpi);
    if (x < work.left) x = work.left;
    if (x + width > work.right) x = work.right - width;
    if (y < work.top) y = anchor.bottom + scaled(8, dpi);
    if (y + height > work.bottom) y = work.bottom - height;

    shownTick_ = GetTickCount64();
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void DetailPanel::hide() {
    if (hwnd_ != nullptr) ShowWindow(hwnd_, SW_HIDE);
}

void DetailPanel::toggle(const RECT& anchor) {
    if (visible()) {
        hide();
    } else {
        show(anchor);
    }
}

bool DetailPanel::visible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != 0;
}

void DetailPanel::paint(HDC dc, const RECT& client) {
    const int dpi = dpiForWindow(hwnd_);
    const bool light = SystemUsesLightTheme();
    const Palette colors = palette(light);

    // Double-buffered so the panel never flickers on repaint.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(buffer, bitmap);

    fill(buffer, client, colors.background);
    SetBkMode(buffer, TRANSPARENT);
    renderPanel(buffer, client.right, dpi, snapshot_, true, &closeButton_);

    HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
    HGDIOBJ previousPen = SelectObject(buffer, pen);
    HGDIOBJ previousBrush = SelectObject(buffer, GetStockObject(NULL_BRUSH));
    Rectangle(buffer, 0, 0, client.right, client.bottom);
    SelectObject(buffer, previousBrush);
    SelectObject(buffer, previousPen);
    DeleteObject(pen);

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

LRESULT CALLBACK DetailPanel::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    DetailPanel* self = reinterpret_cast<DetailPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DetailPanel*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) return self->handle(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DetailPanel::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            paint(dc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&closeButton_, point)) hide();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) hide();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (GetTickCount64() - shownTick_ < kFocusGraceMs) {
                    // Still settling after the flyout closed; take focus back
                    // so a genuine click elsewhere can dismiss the panel.
                    SetForegroundWindow(hwnd);
                } else {
                    hide();
                }
            }
            return 0;
        case WM_CLOSE:
            hide();  // Close hides the panel; only Exit stops the app.
            return 0;
        case WM_DPICHANGED:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace cwut
