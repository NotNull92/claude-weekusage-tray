#include "panel.h"

#include <windowsx.h>

#include <string>

#include "../common/winutil.h"
#include "theme.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

const wchar_t* kPanelClass = L"ClaudeWeekUsageTray.Panel";
constexpr int kBaseWidth = 322;
constexpr unsigned long long kFocusGraceMs = 700;

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
    const Palette colors = ThemePalette(light);
    const long long now = NowUnix();
    const bool stale = IsStale(snapshot, now);
    const bool haveData = snapshot.receivedAtUnix != 0 && snapshot.hasAnyUsage();

    HFONT fontTitle = MakeFont(150, FW_BOLD, dpi);
    HFONT fontSection = MakeFont(85, FW_SEMIBOLD, dpi);
    HFONT fontValue = MakeFont(150, FW_BOLD, dpi);
    HFONT fontBody = MakeFont(95, FW_NORMAL, dpi);
    HFONT fontSmall = MakeFont(85, FW_NORMAL, dpi);

    const int padding = Scaled(16, dpi);
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
        RECT rect{padding, y, padding + contentWidth, y + Scaled(200, dpi)};
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
        const int barHeight = Scaled(6, dpi);
        RECT track{padding, y, padding + contentWidth, y + barHeight};
        if (draw) {
            FillRect(dc, track, colors.track);
            if (window.hasUsage) {
                int filled = MulDiv(contentWidth, window.remainingPercent(), 100);
                if (filled > 0) {
                    RECT bar{padding, y, padding + filled, y + barHeight};
                    FillRect(dc, bar, colors.accent);
                }
            }
        }
        y += barHeight;
    };

    auto drawSection = [&](const wchar_t* label, const RateWindow& window) {
        drawText(label, fontSection, colors.muted, Scaled(18, dpi), 0);
        drawText(remainingText(window), fontValue, window.hasUsage ? colors.text : colors.muted,
                 Scaled(26, dpi), 0);
        y += Scaled(4, dpi);
        drawBar(window);
        y += Scaled(6, dpi);
        const std::wstring reset = L"Resets " + ToWide(FormatResetTime(window, now));
        drawText(window.hasReset ? reset : L"Reset time unavailable", fontBody, colors.muted,
                 Scaled(19, dpi), 0);
        y += Scaled(14, dpi);
    };

    // Header.
    if (draw && closeButtonOut != nullptr) {
        const int box = Scaled(22, dpi);
        RECT close{width - padding - box, padding - Scaled(2, dpi), width - padding,
                   padding - Scaled(2, dpi) + box};
        *closeButtonOut = close;
        HFONT closeFont = MakeFont(110, FW_NORMAL, dpi);
        HGDIOBJ previous = SelectObject(dc, closeFont);
        SetTextColor(dc, colors.muted);
        DrawTextW(dc, L"✕", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        DeleteObject(closeFont);
    }
    drawText(L"CLAUDE", fontTitle, colors.text, Scaled(24, dpi), 0);
    drawText(L"Claude Code subscription usage", fontSmall, colors.muted, Scaled(20, dpi), 0);
    y += Scaled(8, dpi);

    if (!haveData) {
        drawWrapped(L"No usage data yet. Run Claude Code with the ClaudeWeekUsageTray status line "
                    L"configured; this panel fills in as soon as Claude Code sends a payload.",
                    fontBody, colors.muted);
        y += Scaled(12, dpi);
    } else if (stale) {
        drawWrapped(L"Stale: Claude Code has not sent an update recently. The figures below are "
                    L"the last ones received, not a fresh reading.",
                    fontBody, colors.warning);
        y += Scaled(12, dpi);
    }

    drawSection(L"5-HOUR LIMIT", snapshot.fiveHour);
    drawSection(L"7-DAY LIMIT", snapshot.sevenDay);

    if (draw) {
        RECT rule{padding, y, width - padding, y + 1};
        FillRect(dc, rule, colors.border);
    }
    y += Scaled(1, dpi) + Scaled(10, dpi);

    const std::wstring updated =
        L"Last update: " + ToWide(FormatRelativeAge(snapshot.receivedAtUnix, now)) +
        (stale ? L" (stale)" : L"");
    drawText(updated, fontSmall, stale ? colors.warning : colors.muted, Scaled(18, dpi), 0);
    drawText(L"Updates arrive only while Claude Code is running.", fontSmall, colors.muted,
             Scaled(18, dpi), 0);
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
    int height = renderPanel(memory, Scaled(kBaseWidth, dpi), dpi, snapshot_, false, nullptr);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return height;
}

void DetailPanel::show(const RECT& anchor) {
    if (hwnd_ == nullptr) return;
    lastAnchor_ = anchor;
    hasAnchor_ = true;
    const int dpi = DpiForWindow(hwnd_);
    const int width = Scaled(kBaseWidth, dpi);
    const int height = measureHeight(dpi);

    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const RECT work = mi.rcWork;

    int x = anchor.right - width;
    int y = anchor.top - height - Scaled(8, dpi);
    if (x < work.left) x = work.left;
    if (x + width > work.right) x = work.right - width;
    if (y < work.top) y = anchor.bottom + Scaled(8, dpi);
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
    const int dpi = DpiForWindow(hwnd_);
    const bool light = SystemUsesLightTheme();
    const Palette colors = ThemePalette(light);

    // Double-buffered so the panel never flickers on repaint.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(buffer, bitmap);

    FillRect(buffer, client, colors.background);
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
