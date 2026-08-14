#include "setupdialog.h"

#include <windowsx.h>

#include "../common/winutil.h"
#include "theme.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

const wchar_t* kDialogClass = L"ClaudeWeekUsageTray.Setup";
constexpr int kBaseWidth = 404;
constexpr int kButtonHeight = 32;
constexpr int kButtonMinWidth = 92;

// DrawText only breaks at spaces, and a Windows path has none, so a long one
// overflows its box and loses its middle. This folds it at separators, and
// mid-token when it has to, before the text ever reaches DrawText.
std::wstring FoldToWidth(HDC dc, HFONT font, const std::wstring& text, int maxWidth) {
    HGDIOBJ previous = SelectObject(dc, font);
    std::wstring folded;
    std::wstring line;
    size_t lastBreak = 0;  // one past a separator in `line`, 0 when there is none
    for (wchar_t character : text) {
        if (character == L'\n') {
            folded += line;
            folded += L'\n';
            line.clear();
            lastBreak = 0;
            continue;
        }
        line.push_back(character);
        SIZE extent{};
        GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &extent);
        if (extent.cx > maxWidth && line.size() > 1) {
            const size_t cut = lastBreak > 0 ? lastBreak : line.size() - 1;
            folded += line.substr(0, cut);
            folded += L'\n';
            line = line.substr(cut);
            lastBreak = 0;
        }
        if (character == L'\\' || character == L'/' || character == L' ') {
            lastBreak = line.size();
        }
    }
    folded += line;
    SelectObject(dc, previous);
    return folded;
}

}  // namespace

int SetupDialog::render(HDC dc, int width, int dpi, bool draw) {
    const Palette colors = ThemePalette(SystemUsesLightTheme());

    HFONT fontBrand = MakeFont(80, FW_BOLD, dpi);
    HFONT fontHeadline = MakeFont(130, FW_SEMIBOLD, dpi);
    HFONT fontBody = MakeFont(95, FW_NORMAL, dpi);
    HFONT fontDetail = MakeFont(85, FW_NORMAL, dpi);
    HFONT fontButton = MakeFont(95, FW_SEMIBOLD, dpi);

    const int padding = Scaled(20, dpi);
    const int contentWidth = width - padding * 2;
    int y = padding;

    auto line = [&](const std::wstring& text, HFONT font, COLORREF color, int height) {
        if (draw) {
            HGDIOBJ previous = SelectObject(dc, font);
            SetTextColor(dc, color);
            RECT rect{padding, y, padding + contentWidth, y + height};
            DrawTextW(dc, text.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, previous);
        }
        y += height;
    };

    auto wrapped = [&](const std::wstring& text, HFONT font, COLORREF color, int left, int right) {
        RECT measure{left, y, right, y + Scaled(400, dpi)};
        HGDIOBJ previous = SelectObject(dc, font);
        const int height = DrawTextW(dc, text.c_str(), -1, &measure,
                                     DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        if (draw) {
            SetTextColor(dc, color);
            RECT rect{left, y, right, y + height};
            DrawTextW(dc, text.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectObject(dc, previous);
        y += height;
        return height;
    };

    line(L"CLAUDE", fontBrand, colors.muted, Scaled(16, dpi));
    y += Scaled(6, dpi);
    wrapped(text_.headline, fontHeadline, text_.warning ? colors.warning : colors.text, padding,
            padding + contentWidth);
    y += Scaled(10, dpi);
    wrapped(text_.body, fontBody, colors.muted, padding, padding + contentWidth);

    if (!text_.detail.empty()) {
        y += Scaled(12, dpi);
        const int inset = Scaled(10, dpi);
        // Measured before anything is painted, so the box can go down first and
        // the text on top of it.
        const int detailWidth = contentWidth - inset * 2;
        const std::wstring detail = FoldToWidth(dc, fontDetail, text_.detail, detailWidth);
        RECT measure{padding + inset, 0, padding + inset + detailWidth, Scaled(400, dpi)};
        HGDIOBJ previousFont = SelectObject(dc, fontDetail);
        const int textHeight = DrawTextW(dc, detail.c_str(), -1, &measure,
                                         DT_LEFT | DT_TOP | DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, previousFont);
        if (draw) {
            RECT box{padding, y, padding + contentWidth, y + textHeight + inset * 2};
            FillRoundedRect(dc, box, Scaled(6, dpi), colors.track);
        }
        y += inset;
        wrapped(detail, fontDetail, colors.text, padding + inset, padding + contentWidth - inset);
        y += inset;
    }

    y += Scaled(20, dpi);

    // Buttons, right aligned, primary last.
    const int buttonHeight = Scaled(kButtonHeight, dpi);
    auto buttonWidth = [&](const std::wstring& label) {
        HGDIOBJ previous = SelectObject(dc, fontButton);
        SIZE extent{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
        SelectObject(dc, previous);
        const int wanted = extent.cx + Scaled(28, dpi);
        const int minimum = Scaled(kButtonMinWidth, dpi);
        return wanted > minimum ? wanted : minimum;
    };

    auto drawButton = [&](const RECT& rect, const std::wstring& label, bool primary, bool hover) {
        COLORREF fill = primary ? (hover ? colors.accentPressed : colors.accent)
                                : (hover ? colors.border : colors.track);
        FillRoundedRect(dc, rect, Scaled(6, dpi), fill);
        HGDIOBJ previous = SelectObject(dc, fontButton);
        SetTextColor(dc, primary ? colors.onAccent : colors.text);
        RECT text = rect;
        DrawTextW(dc, label.c_str(), -1, &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
    };

    const int primaryWidth = buttonWidth(text_.primaryLabel);
    int right = width - padding;
    RECT primary{right - primaryWidth, y, right, y + buttonHeight};
    RECT secondary{};
    if (!text_.secondaryLabel.empty()) {
        const int secondaryWidth = buttonWidth(text_.secondaryLabel);
        const int gap = Scaled(8, dpi);
        secondary = {primary.left - gap - secondaryWidth, y, primary.left - gap, y + buttonHeight};
    }
    if (draw) {
        primaryRect_ = primary;
        secondaryRect_ = secondary;
        if (!text_.secondaryLabel.empty()) {
            drawButton(secondary, text_.secondaryLabel, false, hovered_ == 2);
        }
        drawButton(primary, text_.primaryLabel, true, hovered_ == 1);
    }
    y += buttonHeight + padding;

    DeleteObject(fontBrand);
    DeleteObject(fontHeadline);
    DeleteObject(fontBody);
    DeleteObject(fontDetail);
    DeleteObject(fontButton);
    return y;
}

bool SetupDialog::create(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SetupDialog::windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kDialogClass,
                            L"ClaudeWeekUsageTray", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr,
                            instance, this);
    return hwnd_ != nullptr;
}

void SetupDialog::destroy() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool SetupDialog::ask(const DialogText& text) {
    if (hwnd_ == nullptr) return false;
    text_ = text;
    done_ = false;
    result_ = false;
    hovered_ = 0;

    const int dpi = DpiForWindow(hwnd_);
    const int width = Scaled(kBaseWidth, dpi);

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    const int height = render(memory, width, dpi, false);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);

    MSG message{};
    while (!done_) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got == 0) {
            // The tray was asked to exit while this was open. Put the quit back
            // so the caller's message loop still sees it.
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (got == -1) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ShowWindow(hwnd_, SW_HIDE);
    return result_;
}

void SetupDialog::finish(bool primary) {
    result_ = primary;
    done_ = true;
    // Nudge the loop so it re-checks done_ even if nothing else is queued.
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void SetupDialog::paint(HDC dc, const RECT& client) {
    const int dpi = DpiForWindow(hwnd_);
    const Palette colors = ThemePalette(SystemUsesLightTheme());

    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(buffer, bitmap);

    FillRect(buffer, client, colors.background);
    SetBkMode(buffer, TRANSPARENT);
    render(buffer, client.right, dpi, true);

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

LRESULT CALLBACK SetupDialog::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SetupDialog* self = reinterpret_cast<SetupDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SetupDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) return self->handle(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SetupDialog::handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
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
        case WM_MOUSEMOVE: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int hovered = 0;
            if (PtInRect(&primaryRect_, point)) {
                hovered = 1;
            } else if (!text_.secondaryLabel.empty() && PtInRect(&secondaryRect_, point)) {
                hovered = 2;
            }
            if (hovered != hovered_) {
                hovered_ = hovered;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&primaryRect_, point)) {
                finish(true);
            } else if (!text_.secondaryLabel.empty() && PtInRect(&secondaryRect_, point)) {
                finish(false);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                finish(true);
            } else if (wParam == VK_ESCAPE) {
                // With one button, escape is that button.
                finish(text_.secondaryLabel.empty());
            }
            return 0;
        case WM_CLOSE:
            finish(text_.secondaryLabel.empty());
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace cwut
