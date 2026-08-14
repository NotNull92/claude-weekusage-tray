// The one window this program shows when it needs an answer.
//
// It replaces a stack of stock message boxes: the offer, the result of acting
// on it, and any error all appear in the same window, drawn in the same style
// as the detail panel.
#pragma once

#include <windows.h>

#include <string>

namespace cwut {

struct DialogText {
    std::wstring headline;
    std::wstring body;
    // Optional. A command or path, shown in a boxed line so it cannot be
    // mistaken for prose.
    std::wstring detail;
    std::wstring primaryLabel;
    // Leave empty for a single-button window.
    std::wstring secondaryLabel;
    // Draws the headline in the warning colour.
    bool warning = false;
};

class SetupDialog {
public:
    bool create(HINSTANCE instance);
    void destroy();

    // Shows the text and blocks until a button is chosen. Returns true when the
    // primary button was pressed. The window is reused, so a follow-up result
    // appears in place rather than as a second window.
    bool ask(const DialogText& text);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    int render(HDC dc, int width, int dpi, bool draw);
    void paint(HDC dc, const RECT& client);
    void finish(bool primary);

    HWND hwnd_ = nullptr;
    DialogText text_;
    RECT primaryRect_{};
    RECT secondaryRect_{};
    int hovered_ = 0;  // 0 none, 1 primary, 2 secondary
    bool done_ = false;
    bool result_ = false;
};

}  // namespace cwut
