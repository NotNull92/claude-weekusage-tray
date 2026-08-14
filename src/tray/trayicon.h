// Renders the notification-area glyph: a large bold number, or "--".
#pragma once

#include <windows.h>

#include <string>

namespace cwut {

struct IconStyle {
    int sizePixels = 16;
    bool lightTheme = false;
    // Dimmed rendering for data Claude Code has not refreshed recently.
    bool dimmed = false;
};

// The Claude accent orange the glyph is drawn in, shaded for the system theme
// so it stays legible on both a dark and a light taskbar. Same colour the
// detail panel uses for its bars.
COLORREF GlyphColor(bool lightTheme);

// Caller owns the returned icon and must DestroyIcon it.
HICON CreateLabelIcon(const std::wstring& label, const IconStyle& style);

}  // namespace cwut
