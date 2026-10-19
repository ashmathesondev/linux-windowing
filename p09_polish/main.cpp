// Stage 9: putting it together. A small "Account settings" form.
//
// Nothing new is introduced here on purpose. It is the stage 8 toolkit on top of
// the stage 6 platform layer, used the way a real application would use it:
// one state struct, one build function, one loop that sleeps until needed.
//
// Things to try:
//   * Resize the window: layout follows, the buffer is re-created at the new size.
//   * Type in the fields, select with the mouse, Ctrl+C / Ctrl+V between the fields
//     and other applications.
//   * Watch the "frames drawn" counter: it does not move while you leave the window
//     alone (except for the caret blink), so an idle app uses no CPU.
//   * WW_SCALE=2 ./p09_polish forces a HiDPI scale; WW_BACKEND=x11 or wayland
//     picks the backend.

#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "ww/renderer.hpp"
#include "ww/ui.hpp"
#include "ww/window.hpp"

namespace {

struct Settings {
    std::string displayName = "Ash";
    std::string email;
    std::string bio;
    bool darkTheme = true;
    bool newsletter = false;
    bool twoFactor = true;
    int visibility = 0;   // 0 public, 1 friends, 2 private
    std::vector<std::string> languages = {"English", "Deutsch", "Francais", "Espanol", "Nihongo"};
    int language = 0;
    float fontScale = 0.5f;
    std::string status = "Ready";
    int framesDrawn = 0;
};

void build(ww::ui::Context& ui, Settings& s, ww::Window& win) {
    const std::string backend = ww::backendName(win.backend());
    const float scale = win.scale();
    using ww::ui::LabelStyle;

    ui.label("Account settings", LabelStyle::Heading);
    ui.label("Running on " + backend + " at scale " + std::to_string(scale).substr(0, 4) + ", frames drawn: " +
                 std::to_string(s.framesDrawn),
             LabelStyle::Dim);
    ui.space(6);

    ui.beginCard("Profile");
    ui.beginRow();
    ui.width(110);
    ui.label("Display name");
    ui.width(-1);
    ui.textField("name", s.displayName, "Your name");
    ui.endRow();
    ui.beginRow();
    ui.width(110);
    ui.label("Email");
    ui.width(-1);
    ui.textField("email", s.email, "name@example.com");
    ui.endRow();
    ui.beginRow();
    ui.width(110);
    ui.label("Short bio");
    ui.width(-1);
    ui.textField("bio", s.bio, "A line about you");
    ui.endRow();
    ui.beginRow();
    ui.space(110 + ui.theme().spacing);
    if (ui.button("Copy email")) {
        // The clipboard goes through the platform layer: X selections or wl_data_device.
        win.setClipboardText(s.email);
        s.status = "Copied the email address";
    }
    if (ui.button("Paste into bio")) {
        s.bio = win.clipboardText();
        s.status = "Pasted into the bio";
    }
    ui.endRow();
    ui.endCard();

    ui.beginCard("Preferences");
    ui.beginRow();
    ui.width(110);
    ui.label("Language");
    ui.dropdown("language", s.languages, s.language);
    ui.endRow();
    ui.beginRow();
    ui.width(110);
    ui.label("Text size");
    ui.width(-1);
    ui.slider("fontscale", s.fontScale, 0, 1);
    ui.endRow();
    ui.label("Profile visibility", LabelStyle::Dim);
    ui.beginRow();
    ui.radio("Public", s.visibility, 0);
    ui.radio("Friends", s.visibility, 1);
    ui.radio("Private", s.visibility, 2);
    ui.endRow();
    ui.separator();
    ui.checkbox("Two factor authentication", s.twoFactor);
    ui.checkbox("Subscribe to the newsletter", s.newsletter);
    const bool wasDark = s.darkTheme;
    ui.checkbox("Dark theme", s.darkTheme);
    if (wasDark != s.darkTheme) ui.setTheme(s.darkTheme ? ww::ui::Theme::dark() : ww::ui::Theme::light());
    ui.endCard();

    ui.beginRow();
    if (ui.button("Save", true)) s.status = "Saved settings for " + (s.displayName.empty() ? std::string("nobody") : s.displayName);
    if (ui.button("Reset")) {
        const bool dark = s.darkTheme;
        s = Settings{};
        s.darkTheme = dark;
        s.status = "Reset to defaults";
    }
    ui.endRow();
    ui.label(s.status, LabelStyle::Dim);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    ww::WindowConfig config;
    config.title = "Stage 9: account settings";
    config.appId = "linux-windowing.p09";
    config.width = 560;
    config.height = 700;
    config.minWidth = 420;
    config.minHeight = 520;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    Settings settings;
    ww::ui::Context ui;
    ui.setClipboard([&](const std::string& t) { window->setClipboardText(t); }, [&] { return window->clipboardText(); });

    bool running = true;
    bool dirty = true;
    bool hadInput = false;

    while (running) {
        ww::Event ev;
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
        ++settings.framesDrawn;
        ww::ui::drawFrame(ui, renderer, [&](ww::ui::Context& u) { build(u, settings, *window); }, hadInput);
        window->setCursor(ui.cursor());
        window->present();
        dirty = hadInput = false;
    }
    return 0;
}
