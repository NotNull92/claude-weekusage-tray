#include "trayicon.h"

#include <cmath>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace cwut {
namespace {

// GDI hinting mangles bold digits at notification-area sizes, so nothing is
// drawn at the final size. The label is rendered several times larger, where
// the glyph outlines are reproduced faithfully, and then box-filtered down.
int supersampleFactor(int iconSize) { return iconSize <= 24 ? 4 : 3; }

// Coverage below this is treated as empty when looking for the glyph bounds,
// so stray antialiasing does not widen them.
constexpr unsigned char kInkThreshold = 12;

// Box-filtering a supersampled glyph leaves most of a thin stem at partial
// coverage, which reads as a grey smudge at 16 pixels. This drops the faintest
// fringe and then pushes what is left towards solid, so the strokes look as
// bold as the typeface actually is.
unsigned char sharpenCoverage(unsigned int average) {
    constexpr double kFringeCut = 0.09;  // coverage below this is not ink
    constexpr double kFill = 0.72;       // < 1 thickens what remains
    double t = static_cast<double>(average) / 255.0;
    t = (t - kFringeCut) / (1.0 - kFringeCut);
    if (t <= 0.0) return 0;
    if (t >= 1.0) return 255;
    const double filled = std::pow(t, kFill);
    return static_cast<unsigned char>(filled * 255.0 + 0.5);
}

HFONT createFont(int height) {
    LOGFONTW lf{};
    lf.lfHeight = -height;
    // Heavier than bold: at this size the difference between FW_BOLD and
    // FW_BLACK is the difference between legible and squinting.
    lf.lfWeight = FW_BLACK;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;  // outlines only, never a bitmap face
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    // Grayscale antialiasing, not ClearType: subpixel colour fringes would
    // corrupt the coverage mask read back below.
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

// An oversized scratch surface the label is drawn onto, plus the coverage the
// draw produced and the exact bounds of the ink.
class GlyphCanvas {
public:
    GlyphCanvas(int width, int height) : width_(width), height_(height) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width_;
        info.bmiHeader.biHeight = -height_;  // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        bitmap_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        dc_ = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (dc_ != nullptr && bitmap_ != nullptr) {
            previous_ = SelectObject(dc_, bitmap_);
            SetBkMode(dc_, TRANSPARENT);
            SetTextColor(dc_, RGB(255, 255, 255));
        }
        coverage_.resize(static_cast<size_t>(width_) * height_);
    }

    ~GlyphCanvas() {
        if (dc_ != nullptr) {
            if (previous_ != nullptr) SelectObject(dc_, previous_);
            DeleteDC(dc_);
        }
        if (bitmap_ != nullptr) DeleteObject(bitmap_);
    }

    GlyphCanvas(const GlyphCanvas&) = delete;
    GlyphCanvas& operator=(const GlyphCanvas&) = delete;

    bool valid() const { return dc_ != nullptr && bitmap_ != nullptr && bits_ != nullptr; }

    // Draws the label centred on the canvas and records its coverage. Returns
    // false when the glyph has no ink at all.
    bool draw(const std::wstring& label, int fontHeight) {
        if (!valid()) return false;
        RECT full{0, 0, width_, height_};
        FillRect(dc_, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        HFONT font = createFont(fontHeight);
        if (font == nullptr) return false;
        HGDIOBJ previousFont = SelectObject(dc_, font);
        SIZE extent{};
        GetTextExtentPoint32W(dc_, label.c_str(), static_cast<int>(label.size()), &extent);
        TextOutW(dc_, (width_ - extent.cx) / 2, (height_ - extent.cy) / 2, label.c_str(),
                 static_cast<int>(label.size()));
        GdiFlush();
        SelectObject(dc_, previousFont);
        DeleteObject(font);

        const unsigned char* pixels = static_cast<const unsigned char*>(bits_);
        inkLeft_ = width_;
        inkTop_ = height_;
        inkRight_ = -1;
        inkBottom_ = -1;
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const unsigned char* pixel = pixels + (static_cast<size_t>(y) * width_ + x) * 4;
                unsigned char value = pixel[0];
                if (pixel[1] > value) value = pixel[1];
                if (pixel[2] > value) value = pixel[2];
                coverage_[static_cast<size_t>(y) * width_ + x] = value;
                if (value >= kInkThreshold) {
                    if (x < inkLeft_) inkLeft_ = x;
                    if (x > inkRight_) inkRight_ = x;
                    if (y < inkTop_) inkTop_ = y;
                    if (y > inkBottom_) inkBottom_ = y;
                }
            }
        }
        return inkRight_ >= inkLeft_ && inkBottom_ >= inkTop_;
    }

    int inkLeft() const { return inkLeft_; }
    int inkTop() const { return inkTop_; }
    int inkWidth() const { return inkRight_ - inkLeft_ + 1; }
    int inkHeight() const { return inkBottom_ - inkTop_ + 1; }

    unsigned char at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
        return coverage_[static_cast<size_t>(y) * width_ + x];
    }

private:
    int width_ = 0;
    int height_ = 0;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    void* bits_ = nullptr;
    std::vector<unsigned char> coverage_;
    int inkLeft_ = 0, inkTop_ = 0, inkRight_ = -1, inkBottom_ = -1;
};

}  // namespace

COLORREF GlyphColor(bool lightTheme) {
    // Darker on a light taskbar so the contrast holds either way.
    return lightTheme ? RGB(0xC1, 0x5F, 0x3C) : RGB(0xD9, 0x77, 0x57);
}

HICON CreateLabelIcon(const std::wstring& label, const IconStyle& style) {
    const int size = style.sizePixels < 8 ? 8 : style.sizePixels;
    const int scale = supersampleFactor(size);
    const int large = size * scale;
    // The full width is used, because the shell already spaces tray icons
    // apart, but a pixel is kept clear above and below.
    const int usableWidth = large;
    const int usableHeight = large - 2 * scale;
    // Three digits will not fit across a tray icon at a height worth reading,
    // so the glyph may be condensed by up to this much before the type size is
    // reduced instead. Squeezing "100" beats shrinking it into illegibility.
    constexpr int kMinCondensePercent = 72;
    const int widestInk = usableWidth * 100 / kMinCondensePercent;

    GlyphCanvas canvas(large * 3, large * 2);
    if (!canvas.valid()) return nullptr;

    // Largest font whose actual ink, not its line box, fits the icon. Measuring
    // the ink is what lets digits fill the height instead of leaving the room
    // a descender would have needed.
    int best = 0;
    int low = 4;
    int high = large * 2;
    while (low <= high) {
        const int middle = (low + high) / 2;
        if (canvas.draw(label, middle) && canvas.inkWidth() <= widestInk &&
            canvas.inkHeight() <= usableHeight) {
            best = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    if (best == 0 || !canvas.draw(label, best)) return nullptr;

    // Where the ink lands in the finished icon: its natural size vertically,
    // and condensed horizontally only if it would otherwise overflow.
    const int sourceWidth = canvas.inkWidth();
    const int sourceHeight = canvas.inkHeight();
    int targetHeight = sourceHeight / scale;
    if (targetHeight < 1) targetHeight = 1;
    int targetWidth = sourceWidth / scale;
    if (targetWidth > size) targetWidth = size;
    if (targetWidth < 1) targetWidth = 1;
    const int targetLeft = (size - targetWidth) / 2;
    const int targetTop = (size - targetHeight) / 2;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
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

    const COLORREF color = GlyphColor(style.lightTheme);
    const unsigned int red = GetRValue(color);
    const unsigned int green = GetGValue(color);
    const unsigned int blue = GetBValue(color);
    const unsigned int dimNumerator = style.dimmed ? 55u : 100u;

    unsigned char* pixels = static_cast<unsigned char*>(bits);
    for (int y = 0; y < size; ++y) {
        // Source rows this output row averages over.
        const int row = y - targetTop;
        const int sourceTop = canvas.inkTop() + row * sourceHeight / targetHeight;
        const int sourceBottom = canvas.inkTop() + (row + 1) * sourceHeight / targetHeight;

        for (int x = 0; x < size; ++x) {
            const int column = x - targetLeft;
            unsigned int alpha = 0;
            if (row >= 0 && row < targetHeight && column >= 0 && column < targetWidth) {
                const int sourceLeft = canvas.inkLeft() + column * sourceWidth / targetWidth;
                const int sourceRight = canvas.inkLeft() + (column + 1) * sourceWidth / targetWidth;
                unsigned int total = 0;
                unsigned int samples = 0;
                for (int sy = sourceTop; sy < sourceBottom; ++sy) {
                    for (int sx = sourceLeft; sx < sourceRight; ++sx) {
                        total += canvas.at(sx, sy);
                        ++samples;
                    }
                }
                if (samples != 0) alpha = sharpenCoverage(total / samples);
            }
            alpha = alpha * dimNumerator / 100u;
            if (alpha > 255u) alpha = 255u;

            // Premultiplied BGRA, which is what the shell alpha-blends.
            unsigned char* pixel = pixels + (static_cast<size_t>(y) * size + x) * 4;
            pixel[0] = static_cast<unsigned char>(blue * alpha / 255u);
            pixel[1] = static_cast<unsigned char>(green * alpha / 255u);
            pixel[2] = static_cast<unsigned char>(red * alpha / 255u);
            pixel[3] = static_cast<unsigned char>(alpha);
        }
    }

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
