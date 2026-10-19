// Stage 10: the controls a desktop programmer expects (menus, tree view, spin box,
// tooltips, dialogs, status bar), built on the same immediate mode toolkit.
//
// Everything interesting is in controls.hpp (what the UI looks like) and
// lib/ui/src/ui_controls.cpp (how each control works).
//
// Run:
//   ./p10_controls                          interactive
//   ./p10_controls --screenshot out/shot    write out/shot_*.png without a window
//
// Try Ctrl+N, Ctrl+S, Ctrl+Q, right click on the list, hover a button for a moment.

#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "controls.hpp"
#include "ww/renderer.hpp"
#include "ww/ui.hpp"
#include "ww/ui_headless.hpp"
#include "ww/window.hpp"

namespace {

constexpr int kWidth = 860;
constexpr int kHeight = 640;

int writeScreenshots(const std::string& prefix) {
    controls::State state;
    ww::ui::HeadlessUi h(kWidth, kHeight);
    h.context().setTooltipDelayMs(0);
    auto build = [&](ww::ui::Context& ui) { controls::build(ui, state); };
    auto click = [&](const char* name, float dx = 0.3f) {
        const ww::Rect r = state.rects.at(name);
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
    click("node:src", 0.05f);          // the arrow expands the node
    click("node:main.cpp", 0.5f);      // the label selects
    h.mouseMove(state.rects.at("button:Open").x + 10, state.rects.at("button:Open").y + 10);
    h.frame(build);
    shot("1_tooltip_and_tree");

    click("File");
    h.mouseMove(state.rects.at("File/Open").x + 30, state.rects.at("File/Open").y + 10);
    h.frame(build);
    shot("2_file_menu");
    click("View");
    shot("3_view_menu");
    h.key(ww::Key::Escape);
    h.frame(build);

    const ww::Rect list = state.rects.at("list");
    h.rightClick(list.x + 40, list.y + 20);
    h.frame(build);
    shot("4_context_menu");
    click("ctx/Add");

    click("Help");
    click("Help/About");
    shot("5_about_dialog");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && std::strcmp(argv[1], "--screenshot") == 0) return writeScreenshots(argv[2]);

    ww::WindowConfig config;
    config.title = "Stage 10: controls";
    config.appId = "linux-windowing.p10";
    config.width = kWidth;
    config.height = kHeight;
    config.minWidth = 700;
    config.minHeight = 600;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    controls::State state;
    ww::ui::Context ui;
    ui.setClipboard([&](const std::string& t) { window->setClipboardText(t); }, [&] { return window->clipboardText(); });

    bool running = true;
    bool dirty = true;
    bool hadInput = false;

    while (running) {
        ww::Event ev;
        // Sleeps until input arrives, the caret must blink or a tooltip becomes due.
        bool have = window->waitEvent(ev, ui.nextWakeupMs());
        if (!have) dirty = true;

        while (have) {
            if (std::holds_alternative<ww::CloseEvent>(ev)) {
                running = false;
            } else if (std::holds_alternative<ww::ResizeEvent>(ev) || std::holds_alternative<ww::RedrawEvent>(ev)) {
                dirty = true;
            } else {
                ui.handleEvent(ev);
                dirty = hadInput = true;
            }
            have = window->pollEvent(ev);
        }
        if (!dirty || !running) continue;

        ww::Framebuffer fb = window->acquireFramebuffer();
        ww::Renderer renderer(fb);
        ww::ui::drawFrame(ui, renderer, [&](ww::ui::Context& u) { controls::build(u, state); }, hadInput);
        window->setCursor(ui.cursor());
        window->present();
        dirty = hadInput = false;
        if (state.quit) running = false;
    }
    return 0;
}
