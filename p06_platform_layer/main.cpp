// Stage 6: one program, two display systems.
//
// This is stages 3 and 5 again, but written once against ww::Window. The same
// executable runs on X11 and on Wayland; the backend is picked at runtime.
//
//   ./p06_platform_layer              automatic (Wayland if available, else X11)
//   ./p06_platform_layer --x11        force X11 (works under XWayland too)
//   ./p06_platform_layer --wayland    force Wayland
//   WW_SCALE=2 ./p06_platform_layer   pretend to be on a HiDPI display
//
// Things to try: move the mouse (the cursor changes between the top and bottom
// half), type, scroll, resize, press C to copy text and V to print the clipboard.
//
// Read lib/platform/include/ww/window.hpp first: it is the whole API.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

#include "ww/window.hpp"

namespace {

constexpr uint32_t argb(unsigned r, unsigned g, unsigned b) { return 0xff000000u | (r << 16) | (g << 8) | b; }

const char* keyName(ww::Key key) {
    static char buf[16];
    const auto k = static_cast<unsigned>(key);
    if (k >= 'a' && k <= 'z') { std::snprintf(buf, sizeof buf, "%c", static_cast<char>(k - 32)); return buf; }
    if (k >= '0' && k <= '9') { std::snprintf(buf, sizeof buf, "%c", static_cast<char>(k)); return buf; }
    switch (key) {
    case ww::Key::Backspace: return "Backspace";
    case ww::Key::Tab: return "Tab";
    case ww::Key::Enter: return "Enter";
    case ww::Key::Escape: return "Escape";
    case ww::Key::Space: return "Space";
    case ww::Key::Delete: return "Delete";
    case ww::Key::Left: return "Left";
    case ww::Key::Right: return "Right";
    case ww::Key::Up: return "Up";
    case ww::Key::Down: return "Down";
    case ww::Key::Home: return "Home";
    case ww::Key::End: return "End";
    default: return "(other)";
    }
}

std::string modsText(const ww::Modifiers& m) {
    std::string s;
    if (m.ctrl) s += "Ctrl+";
    if (m.alt) s += "Alt+";
    if (m.shift) s += "Shift+";
    if (m.super) s += "Super+";
    return s;
}

void fillRect(const ww::Framebuffer& fb, int x, int y, int w, int h, uint32_t color) {
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, fb.width), y1 = std::min(y + h, fb.height);
    for (int yy = y0; yy < y1; ++yy) {
        auto* row = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb.pixels) + static_cast<size_t>(yy) * static_cast<size_t>(fb.stride));
        std::fill(row + x0, row + std::max(x0, x1), color);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // show log lines immediately, even when piped
    ww::Backend backend = ww::Backend::Auto;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--x11") == 0) backend = ww::Backend::X11;
        else if (std::strcmp(argv[i], "--wayland") == 0) backend = ww::Backend::Wayland;
    }

    ww::WindowConfig config;
    config.title = "Stage 6: platform layer";
    config.appId = "linux-windowing.p06";
    config.width = 800;
    config.height = 600;

    std::unique_ptr<ww::Window> window = ww::Window::create(config, backend);
    if (!window) return 1;

    std::printf("Backend: %s, scale %.2f\n", ww::backendName(window->backend()), static_cast<double>(window->scale()));
    window->setTitle(std::string("Stage 6: platform layer (") + ww::backendName(window->backend()) + ")");

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();

    float mouseX = 0, mouseY = 0;
    bool mouseInside = false;
    bool running = true;

    while (running) {
        // Wait for at most one frame (16 ms): the window animates continuously.
        ww::Event ev;
        bool have = window->waitEvent(ev, 16);
        while (have) {
            std::visit(
                [&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, ww::CloseEvent>) {
                        running = false;
                    } else if constexpr (std::is_same_v<T, ww::ResizeEvent>) {
                        std::printf("Resize  %dx%d px (scale %.2f)\n", e.widthPx, e.heightPx, static_cast<double>(e.scale));
                    } else if constexpr (std::is_same_v<T, ww::FocusEvent>) {
                        std::printf("Focus   %s\n", e.focused ? "gained" : "lost");
                    } else if constexpr (std::is_same_v<T, ww::MouseMoveEvent>) {
                        mouseX = e.x;
                        mouseY = e.y;
                        mouseInside = true;
                    } else if constexpr (std::is_same_v<T, ww::MouseLeaveEvent>) {
                        mouseInside = false;
                    } else if constexpr (std::is_same_v<T, ww::MouseButtonEvent>) {
                        std::printf("Mouse   button %d %s at (%.0f,%.0f) clicks=%d\n", static_cast<int>(e.button),
                                    e.pressed ? "down" : "up", static_cast<double>(e.x), static_cast<double>(e.y), e.clickCount);
                    } else if constexpr (std::is_same_v<T, ww::ScrollEvent>) {
                        std::printf("Scroll  dx=%.1f dy=%.1f\n", static_cast<double>(e.dx), static_cast<double>(e.dy));
                    } else if constexpr (std::is_same_v<T, ww::KeyEvent>) {
                        if (!e.pressed) return;
                        std::printf("Key     %s%s%s\n", modsText(e.mods).c_str(), keyName(e.key), e.repeat ? " (repeat)" : "");
                        if (e.key == ww::Key::Escape) running = false;
                        if (e.key == ww::Key::C && e.mods.ctrl == false) {
                            window->setClipboardText("Hello from stage 6");
                            std::printf("        -> copied \"Hello from stage 6\" to the clipboard\n");
                        }
                        if (e.key == ww::Key::V && e.mods.ctrl == false) {
                            std::printf("        -> clipboard contains: \"%s\"\n", window->clipboardText().c_str());
                        }
                    } else if constexpr (std::is_same_v<T, ww::TextEvent>) {
                        std::printf("Text    \"%s\"\n", e.utf8.c_str());
                    }
                },
                ev);
            have = window->pollEvent(ev);
        }

        // Cursor demo: arrow in the top half, hand in the bottom half.
        window->setCursor(mouseY > static_cast<float>(window->heightPx()) / window->scale() / 2 ? ww::Cursor::Hand
                                                                                                : ww::Cursor::Arrow);

        // ---- draw a frame ---------------------------------------------------------------
        const ww::Framebuffer fb = window->acquireFramebuffer();
        const double t = std::chrono::duration<double>(Clock::now() - start).count();
        const unsigned phase = static_cast<unsigned>(t * 60.0);
        for (int y = 0; y < fb.height; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fb.pixels) + static_cast<size_t>(y) * static_cast<size_t>(fb.stride));
            const unsigned g = static_cast<unsigned>(y * 255 / std::max(fb.height - 1, 1));
            for (int x = 0; x < fb.width; ++x) {
                const unsigned r = static_cast<unsigned>(x * 255 / std::max(fb.width - 1, 1));
                const unsigned b = (phase + static_cast<unsigned>(x + y) / 4) & 0xff;
                row[x] = argb(r, g, b < 128 ? b * 2 : (255 - b) * 2);
            }
        }
        // Positions are logical pixels: multiply by the scale to get framebuffer pixels.
        const float s = fb.scale;
        const int bar = static_cast<int>((std::sin(t * 2.0) * 0.5 + 0.5) * (static_cast<double>(fb.width) - 80.0 * static_cast<double>(s)));
        fillRect(fb, bar, fb.height - static_cast<int>(24 * s), static_cast<int>(80 * s), static_cast<int>(12 * s), argb(255, 255, 255));
        if (mouseInside) {
            const int half = static_cast<int>(22 * s);
            const int cx = static_cast<int>(mouseX * s), cy = static_cast<int>(mouseY * s);
            fillRect(fb, cx - half, cy - half, 2 * half, 2 * half, argb(0, 0, 0));
            fillRect(fb, cx - half + 2, cy - half + 2, 2 * half - 4, 2 * half - 4, argb(255, 220, 80));
        }
        window->present();
    }
    return 0;
}
