#include "trayicon.h"

#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

HFONT createFont(int height) {
    LOGFONTW lf{};
    lf.lfHeight = -height;
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    // Grayscale antialiasing, not ClearType: subpixel colour fringes would
    // corrupt the alpha mask derived from the rendered pixels below.
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// Largest font height whose rendering of `label` still fits inside the icon.
HFONT pickFittingFont(HDC dc, const std::wstring& label, int size, SIZE& outExtent) {
    const int maxHeight = size + 2;
    for (int height = maxHeight; height >= 6; --height) {
        HFONT font = createFont(height);
        if (font == nullptr) continue;
        HGDIOBJ previous = SelectObject(dc, font);
        SIZE extent{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
        SelectObject(dc, previous);
        if (extent.cx <= size && extent.cy <= size) {
            outExtent = extent;
            return font;
        }
        DeleteObject(font);
    }
    HFONT fallback = createFont(6);
    if (fallback != nullptr) {
        HGDIOBJ previous = SelectObject(dc, fallback);
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &outExtent);
        SelectObject(dc, previous);
    }
    return fallback;
}

}  // namespace

HICON CreateLabelIcon(const std::wstring& label, const IconStyle& style) {
    const int size = style.sizePixels < 8 ? 8 : style.sizePixels;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (colorBitmap == nullptr || memory == nullptr || bits == nullptr) {
        if (colorBitmap != nullptr) DeleteObject(colorBitmap);
        if (memory != nullptr) DeleteDC(memory);
        return nullptr;
    }

    HGDIOBJ previousBitmap = SelectObject(memory, colorBitmap);

    // Draw white text on black, then read the luminance back as the alpha
    // mask. This keeps antialiased edges without needing GDI+ or Direct2D.
    RECT full{0, 0, size, size};
    FillRect(memory, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));

    SIZE extent{};
    HFONT font = pickFittingFont(memory, label, size, extent);
    HGDIOBJ previousFont = (font != nullptr) ? SelectObject(memory, font) : nullptr;
    const int x = (size - extent.cx) / 2;
    const int y = (size - extent.cy) / 2;
    TextOutW(memory, x < 0 ? 0 : x, y < 0 ? 0 : y, label.c_str(), static_cast<int>(label.size()));
    GdiFlush();
    if (previousFont != nullptr) SelectObject(memory, previousFont);
    if (font != nullptr) DeleteObject(font);

    // Convert to a premultiplied BGRA icon in the theme's foreground colour.
    unsigned char* pixels = static_cast<unsigned char*>(bits);
    const int dimNumerator = style.dimmed ? 55 : 100;
    for (int index = 0; index < size * size; ++index) {
        unsigned char* pixel = pixels + static_cast<size_t>(index) * 4;
        unsigned int intensity = pixel[0];
        if (pixel[1] > intensity) intensity = pixel[1];
        if (pixel[2] > intensity) intensity = pixel[2];
        unsigned int alpha = intensity * static_cast<unsigned int>(dimNumerator) / 100u;
        if (alpha > 255u) alpha = 255u;
        const unsigned char channel = style.lightTheme ? 0 : static_cast<unsigned char>(alpha);
        pixel[0] = channel;  // B
        pixel[1] = channel;  // G
        pixel[2] = channel;  // R
        pixel[3] = static_cast<unsigned char>(alpha);
    }

    SelectObject(memory, previousBitmap);

    // Fully opaque mask; the alpha channel above does the real work.
    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    if (maskBitmap != nullptr) {
        HGDIOBJ previousMask = SelectObject(memory, maskBitmap);
        PatBlt(memory, 0, 0, size, size, BLACKNESS);
        SelectObject(memory, previousMask);
    }
    DeleteDC(memory);

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);

    if (maskBitmap != nullptr) DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    return icon;
}

}  // namespace cwut
