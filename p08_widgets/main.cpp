// Stage 8: a small immediate mode widget toolkit.
//
// The window loop below is the whole "framework". Everything interesting is in:
//   gallery.hpp                  what the UI looks like (a function you call every frame)
//   lib/ui/include/ww/ui.hpp     the toolkit's API
//   lib/ui/src/ui.cpp            how widgets, layout, focus and text editing work
//
// Run:
//   ./p08_widgets                         interactive
//   ./p08_widgets --screenshot out/shot   write out/shot_*.png without a window
//
// The loop sleeps until an event arrives (or the text caret needs to blink), so
// the program uses no CPU while you are not touching it.

#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "gallery.hpp"
#include "ww/renderer.hpp"
#include "ww/ui.hpp"
#include "ww/ui_headless.hpp"
#include "ww/window.hpp"

namespace {

constexpr int kWidth = 900;
constexpr int kHeight = 640;

// Scripted run without a window: clicks, types, and saves a PNG after every step.
int writeScreenshots(const std::string& prefix) {
    gallery::State state;
    ww::ui::HeadlessUi h(kWidth, kHeight);
    auto build = [&](ww::ui::Context& ui) { gallery::build(ui, state); };
    auto clickAt = [&](ww::Rect r, float dx = 0.5f) {
        const float x = r.x + r.w * dx, y = r.y + r.h / 2;
        h.mouseDown(x, y);
        h.mouseUp(x, y);
        h.frame(build);
    };
    auto shot = [&](const char* name) {
        const std::string path = prefix + "_" + name + ".png";
        if (!h.savePng(path)) std::fprintf(stderr, "could not write %s\n", path.c_str());
        else std::printf("wrote %s\n", path.c_str());
    };

    h.frame(build);
    h.mouseMove(state.primaryButtonRect.x + 20, state.primaryButtonRect.y + 10);   // hover the primary button
    h.frame(build);
    shot("1_basics");

    // Tab 2: type into the name field and select everything.
    h.mouseDown(state.tabsRect.x + 130, state.tabsRect.y + 15);
    h.mouseUp(state.tabsRect.x + 130, state.tabsRect.y + 15);
    h.frame(build);
    clickAt(state.nameFieldRect, 0.9f);
    h.type(" Matheson");
    h.key(ww::Key::A, ww::Modifiers{.ctrl = true});
    h.frame(build);
    shot("2_text");

    // Tab 3: open the dropdown.
    h.mouseDown(state.tabsRect.x + 300, state.tabsRect.y + 15);
    h.mouseUp(state.tabsRect.x + 300, state.tabsRect.y + 15);
    h.frame(build);
    clickAt(state.dropdownRect);
    shot("3_lists_dropdown");

    // Light theme.
    h.key(ww::Key::Escape);
    h.frame(build);
    clickAt(state.themeButtonRect);
    shot("4_light");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && std::strcmp(argv[1], "--screenshot") == 0) return writeScreenshots(argv[2]);

    ww::WindowConfig config;
    config.title = "Stage 8: widgets";
    config.appId = "linux-windowing.p08";
    config.width = kWidth;
    config.height = kHeight;
    config.minWidth = 640;
    config.minHeight = 480;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    gallery::State state;
    ww::ui::Context ui;
    ui.setClipboard([&](const std::string& t) { window->setClipboardText(t); }, [&] { return window->clipboardText(); });

    bool running = true;
    bool dirty = true;
    bool hadInput = false;

    while (running) {
        ww::Event ev;
        // Sleep until something happens. The UI asks for a wake up only when it needs one (caret blink).
        const int timeout = ui.nextWakeupMs();
        bool have = window->waitEvent(ev, timeout);
        if (!have) dirty = true;   // timer expired: redraw the blinking caret

        while (have) {
            if (std::holds_alternative<ww::CloseEvent>(ev)) {
                running = false;
            } else if (std::holds_alternative<ww::ResizeEvent>(ev) || std::holds_alternative<ww::RedrawEvent>(ev)) {
                dirty = true;
            } else {
                ui.handleEvent(ev);   // mouse, keyboard, text, scroll, focus
                dirty = hadInput = true;
            }
            have = window->pollEvent(ev);
        }
        if (!dirty || !running) continue;

        ww::Framebuffer fb = window->acquireFramebuffer();
        ww::Renderer renderer(fb);
        ww::ui::drawFrame(ui, renderer, [&](ww::ui::Context& u) { gallery::build(u, state); }, hadInput);
        window->setCursor(ui.cursor());
        window->present();
        dirty = hadInput = false;
    }
    return 0;
}
