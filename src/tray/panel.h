// The detail panel shown when the tray icon is clicked.
#pragma once

#include <windows.h>

#include "../common/usage.h"

namespace cwut {

class DetailPanel {
public:
    bool create(HINSTANCE instance);
    void destroy();

    void setSnapshot(const UsageSnapshot& snapshot);
    void show(const RECT& anchor);
    void hide();
    void toggle(const RECT& anchor);
    bool visible() const;

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void paint(HDC dc, const RECT& client);
    int measureHeight(int dpi) const;

    HWND hwnd_ = nullptr;
    UsageSnapshot snapshot_;
    RECT closeButton_{};
    // Opening from the notification-area overflow flyout causes a burst of
    // focus changes as the flyout closes. Deactivations inside this grace
    // period are ignored so the panel does not vanish the instant it appears.
    unsigned long long shownTick_ = 0;
};

}  // namespace cwut
