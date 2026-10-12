#pragma once
// 2D drawing for the UI: shapes, clipping and text.
//
// The renderer draws into a window's Framebuffer (CPU pixels). It is implemented
// with Cairo (vector graphics) and Pango (text layout + font fallback), the same
// building blocks GTK uses, but it exposes only a handful of primitives so it can
// be swapped for your own rasterizer later without touching the UI code.
//
// All coordinates are logical pixels; the renderer applies the HiDPI scale.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ww/window.hpp"

namespace ww {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
        return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
                static_cast<float>(rgb & 0xff) / 255.0f, alpha};
    }
    constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    // Linear blend: t = 0 gives *this, t = 1 gives other.
    constexpr Color mix(Color other, float t) const {
        return {r + (other.r - r) * t, g + (other.g - g) * t, b + (other.b - b) * t, a + (other.a - a) * t};
    }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    constexpr Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr float right() const { return x + w; }
    constexpr float bottom() const { return y + h; }
};

struct Font {
    std::string family = "Sans";
    float size = 14.0f;   // logical pixels
    bool bold = false;
    bool italic = false;
};

struct TextSize {
    float width = 0;
    float height = 0;
};

class Renderer {
public:
    explicit Renderer(const Framebuffer& framebuffer);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Size in logical pixels.
    float width() const;
    float height() const;
    float scale() const;

    void clear(Color color);

    void fillRect(Rect r, Color color);
    void strokeRect(Rect r, Color color, float lineWidth = 1.0f);   // the line lies inside r
    void fillRoundRect(Rect r, float radius, Color color);
    void strokeRoundRect(Rect r, float radius, Color color, float lineWidth = 1.0f);
    void fillRoundRectGradient(Rect r, float radius, Color top, Color bottom);
    void fillCircle(float cx, float cy, float radius, Color color);
    void strokeCircle(float cx, float cy, float radius, Color color, float lineWidth = 1.0f);
    void fillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Color color);
    void line(float x1, float y1, float x2, float y2, Color color, float lineWidth = 1.0f);
    // Soft shadow made of stacked translucent rounded rectangles.
    void shadow(Rect r, float radius, float spread, Color color);

    // y is the top of the text box. Returns nothing; use measureText for layout.
    void drawText(float x, float y, std::string_view text, const Font& font, Color color);
    TextSize measureText(std::string_view text, const Font& font);
    // x offset of the caret in front of the character at byteIndex (UTF-8).
    float caretX(std::string_view text, const Font& font, size_t byteIndex);
    // Inverse of caretX: the byte index of the character boundary closest to x.
    size_t indexAtX(std::string_view text, const Font& font, float x);

    void pushClip(Rect r);
    void popClip();

    // Writes the current pixels as a PNG (framebuffer pixel size). Useful for tests and screenshots.
    bool savePng(const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A framebuffer in ordinary memory instead of a window. Handy for unit tests,
// screenshots and rendering on machines without a display server.
class OffscreenSurface {
public:
    OffscreenSurface(int logicalWidth, int logicalHeight, float scale = 1.0f);
    Framebuffer framebuffer();

private:
    std::vector<uint32_t> pixels_;
    Framebuffer fb_;
};

} // namespace ww
