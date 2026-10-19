// Stage 7: a 2D renderer with text.
//
// ww::Renderer (lib/renderer) draws shapes and text into the window's
// framebuffer. Cairo does the vector graphics, Pango does text layout (fonts,
// kerning, fallback fonts for other scripts, right-to-left text).
//
// The renderer interface is small on purpose: if you later want to replace Cairo
// with your own rasterizer (or the GPU), this is the only interface to re-implement.
//
// Run:
//   ./p07_renderer                       interactive window
//   ./p07_renderer --screenshot out.png  render once without a window and save a PNG
//   WW_SCALE=2 ./p07_renderer            HiDPI

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "ww/renderer.hpp"
#include "ww/window.hpp"

namespace {

using ww::Color;
using ww::Font;
using ww::Rect;

void drawScene(ww::Renderer& r, float mouseX, float mouseY, bool mouseInside) {
    const Color bg = Color::hex(0x1b1f2b);
    const Color panel = Color::hex(0x242a3a);
    const Color ink = Color::hex(0xe7eaf3);
    const Color dim = Color::hex(0x98a2b9);
    const Color accent = Color::hex(0x4c8dff);
    const Color orange = Color::hex(0xff9f43);
    const Color green = Color::hex(0x2ed573);

    r.clear(bg);

    Font title{"Sans", 26, true, false};
    Font normal{"Sans", 14, false, false};
    Font small{"Sans", 12, false, false};

    r.drawText(24, 18, "ww::Renderer", title, ink);
    r.drawText(24, 54, "shapes, clipping and text in about twenty functions", normal, dim);

    // ---- Shapes -------------------------------------------------------------------------
    float x = 24, y = 96;
    r.drawText(x, y, "Rectangles", small, dim);
    y += 22;
    r.fillRect({x, y, 70, 50}, accent);
    r.strokeRect({x + 84, y, 70, 50}, accent, 2);
    r.fillRoundRect({x + 168, y, 70, 50}, 8, orange);
    r.strokeRoundRect({x + 252, y, 70, 50}, 14, orange, 2);
    r.fillRoundRectGradient({x + 336, y, 70, 50}, 10, Color::hex(0x7ee8a2), Color::hex(0x1fa463));
    r.fillRoundRect({x + 420, y, 70, 50}, 25, green);   // radius = half the height: a pill

    y += 84;
    r.drawText(x, y, "Circles, lines, triangles", small, dim);
    y += 22;
    r.fillCircle(x + 25, y + 25, 25, accent);
    r.strokeCircle(x + 109, y + 25, 25, accent, 3);
    r.line(x + 168, y + 48, x + 238, y + 2, orange, 3);
    r.line(x + 168, y + 2, x + 238, y + 48, green, 1);
    r.fillTriangle(x + 262, y + 48, x + 302, y + 2, x + 342, y + 48, green);

    // ---- Shadow: a card hovering over the background ------------------------------------------------
    const Rect card{x + 380, y - 4, 110, 58};
    r.shadow(card, 10, 14, Color::hex(0x000000, 0.8f));
    r.fillRoundRect(card, 10, panel);
    r.strokeRoundRect(card, 10, Color::hex(0x343b52));
    r.drawText(card.x + 14, card.y + 18, "A card", normal, ink);

    // ---- Text ---------------------------------------------------------------------------------------------
    y += 84;
    r.drawText(x, y, "Text (sizes, weights, scripts)", small, dim);
    y += 22;
    float tx = x;
    for (float size : {12.0f, 16.0f, 22.0f, 32.0f}) {
        Font f{"Sans", size, false, false};
        const std::string s = std::to_string(static_cast<int>(size)) + "px ";
        r.drawText(tx, y, s, f, ink);
        tx += r.measureText(s, f).width;
    }
    r.drawText(tx, y, "bold ", Font{"Sans", 22, true, false}, ink);
    tx += r.measureText("bold ", Font{"Sans", 22, true, false}).width;
    r.drawText(tx, y, "italic", Font{"Sans", 22, false, true}, ink);

    y += 48;
    r.drawText(x, y, "Ünïcödé · Ελληνικά · Русский · 日本語 · العربية · ✓ → ½ ≠ ∞", Font{"Sans", 18, false, false}, ink);

    // ---- measuring: draw a box around a string -----------------------------------------------------------------
    y += 44;
    const std::string measured = "Measured with measureText()";
    const ww::TextSize size = r.measureText(measured, normal);
    r.strokeRect({x - 4, y - 2, size.width + 8, size.height + 4}, orange, 1);
    r.drawText(x, y, measured, normal, ink);

    // caretX: where would a text cursor in front of byte 9 be?
    const float caret = r.caretX(measured, normal, 9);
    r.line(x + caret, y - 4, x + caret, y + size.height + 4, green, 1.5f);

    // ---- clipping ------------------------------------------------------------------------------------------------
    y += 54;
    r.drawText(x, y, "Clipping", small, dim);
    y += 22;
    const Rect clip{x, y, 220, 36};
    r.fillRoundRect(clip, 6, panel);
    r.pushClip(clip.inset(2));
    r.drawText(x + 8, y + 8, "This text is cut off at the edge of its clip rectangle", normal, ink);
    r.popClip();

    // ---- hover: shape follows the mouse -----------------------------------------------------------------------------------
    if (mouseInside) {
        r.fillCircle(mouseX, mouseY, 14, orange.withAlpha(0.55f));
        r.strokeCircle(mouseX, mouseY, 14, orange, 2);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    // Headless mode: no window, no display server needed.
    if (argc >= 3 && std::strcmp(argv[1], "--screenshot") == 0) {
        ww::OffscreenSurface surface(720, 560, 1.0f);
        ww::Renderer renderer(surface.framebuffer());
        drawScene(renderer, 0, 0, false);
        if (!renderer.savePng(argv[2])) return 1;
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }

    ww::WindowConfig config;
    config.title = "Stage 7: renderer";
    config.appId = "linux-windowing.p07";
    config.width = 720;
    config.height = 560;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    float mouseX = 0, mouseY = 0;
    bool inside = false;
    bool running = true;
    bool dirty = true;

    while (running) {
        ww::Event ev;
        bool have = window->waitEvent(ev, -1);   // sleep until something happens: no CPU use while idle
        while (have) {
            if (std::holds_alternative<ww::CloseEvent>(ev)) running = false;
            else if (std::holds_alternative<ww::RedrawEvent>(ev) || std::holds_alternative<ww::ResizeEvent>(ev)) dirty = true;
            else if (const auto* m = std::get_if<ww::MouseMoveEvent>(&ev)) {
                mouseX = m->x;
                mouseY = m->y;
                inside = true;
                dirty = true;
            } else if (std::holds_alternative<ww::MouseLeaveEvent>(ev)) {
                inside = false;
                dirty = true;
            } else if (const auto* k = std::get_if<ww::KeyEvent>(&ev)) {
                if (k->pressed && k->key == ww::Key::Escape) running = false;
            }
            have = window->pollEvent(ev);
        }
        if (dirty && running) {
            ww::Framebuffer fb = window->acquireFramebuffer();
            ww::Renderer renderer(fb);
            drawScene(renderer, mouseX, mouseY, inside);
            window->present();
            dirty = false;
        }
    }
    return 0;
}
