// Headless tests for the widget toolkit. No window or display server needed:
// HeadlessUi renders into memory and feeds synthetic input. The widget gallery
// from stage 8 records where each widget was drawn, so tests can click them.

#include <cmath>
#include <cstdio>
#include <string>

#include "gallery.hpp"
#include "ww/ui_headless.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

struct Fixture {
    gallery::State state;
    ww::ui::HeadlessUi h{900, 640};
    std::string clipboard;

    Fixture() {
        h.context().setClipboard([this](const std::string& t) { clipboard = t; }, [this] { return clipboard; });
        frame();
    }
    void frame() { h.frame([this](ww::ui::Context& ui) { gallery::build(ui, state); }); }
    void click(ww::Rect r, float dx = 0.5f) {
        const float x = r.x + r.w * dx, y = r.y + r.h / 2;
        h.mouseDown(x, y);
        h.mouseUp(x, y);
        frame();
    }
    void key(ww::Key k, ww::Modifiers m = {}) {
        h.key(k, m);
        frame();
    }
    void type(const std::string& s) {
        h.type(s);
        frame();
    }
    // Tab headers are as wide as their text, so click at fixed offsets from the left.
    void gotoTab(int tab) {
        const float offsets[] = {40, 130, 300};
        const float x = state.tabsRect.x + offsets[tab], y = state.tabsRect.y + state.tabsRect.h / 2;
        h.mouseDown(x, y);
        h.mouseUp(x, y);
        frame();
        CHECK(state.tab == tab);
    }
};

void testButtons() {
    Fixture f;
    CHECK(f.state.clicks == 0);
    f.click(f.state.primaryButtonRect);
    CHECK(f.state.clicks == 10);
    f.click(f.state.primaryButtonRect);
    CHECK(f.state.clicks == 20);
}

void testToggles() {
    Fixture f;
    const bool before = f.state.notifications;
    f.click(f.state.checkboxRect, 0.1f);
    CHECK(f.state.notifications != before);
    f.click(f.state.checkboxRect, 0.1f);
    CHECK(f.state.notifications == before);

    CHECK(f.state.quality == 1);
    f.click(f.state.radioHighRect, 0.1f);
    CHECK(f.state.quality == 2);
}

void testSlider() {
    Fixture f;
    const ww::Rect r = f.state.volumeSliderRect;
    const float y = r.y + r.h / 2;
    f.h.mouseDown(r.x + r.w * 0.5f, y);
    f.frame();
    f.h.mouseMove(r.x + r.w * 0.9f, y);
    f.frame();
    f.h.mouseUp(r.x + r.w * 0.9f, y);
    f.frame();
    CHECK(f.state.volume > 0.8f && f.state.volume < 0.95f);
    // Dragging far outside clamps to the range.
    f.h.mouseDown(r.x + r.w * 0.5f, y);
    f.frame();
    f.h.mouseMove(r.x + r.w * 3, y);
    f.frame();
    f.h.mouseUp(r.x + r.w * 3, y);
    f.frame();
    CHECK(std::fabs(f.state.volume - 1.0f) < 1e-4f);
}

void testTextField() {
    Fixture f;
    f.gotoTab(1);
    f.click(f.state.nameFieldRect, 0.9f);
    f.type(" M");
    CHECK(f.state.name == "Ash M");
    f.key(ww::Key::Backspace);
    CHECK(f.state.name == "Ash ");

    // Select all, then replace.
    f.key(ww::Key::A, ww::Modifiers{.ctrl = true});
    f.type("Bob");
    CHECK(f.state.name == "Bob");

    // Copy, move to the email field, paste.
    f.key(ww::Key::A, ww::Modifiers{.ctrl = true});
    f.key(ww::Key::C, ww::Modifiers{.ctrl = true});
    CHECK(f.clipboard == "Bob");
    f.click(f.state.emailFieldRect, 0.5f);
    f.key(ww::Key::V, ww::Modifiers{.ctrl = true});
    CHECK(f.state.email == "Bob");

    // Cut leaves the field empty and fills the clipboard.
    f.key(ww::Key::A, ww::Modifiers{.ctrl = true});
    f.key(ww::Key::X, ww::Modifiers{.ctrl = true});
    CHECK(f.state.email.empty());
    CHECK(f.clipboard == "Bob");

    // Shift+Left extends the selection by one character.
    f.click(f.state.nameFieldRect, 0.9f);
    f.key(ww::Key::End);
    f.key(ww::Key::Left, ww::Modifiers{.shift = true});
    f.key(ww::Key::Delete);
    CHECK(f.state.name == "Bo");
}

void testTextFieldUtf8() {
    Fixture f;
    f.gotoTab(1);
    f.click(f.state.emailFieldRect, 0.5f);
    f.type("h\xC3\xA9");   // "hé": the é is two bytes
    CHECK(f.state.email == "h\xC3\xA9");
    f.key(ww::Key::Backspace);   // must remove the whole character, not half of it
    CHECK(f.state.email == "h");
}

void testListAndDropdown() {
    Fixture f;
    f.gotoTab(2);
    // Click the first row of the list box.
    const ww::Rect lr = f.state.listRect;
    f.h.mouseDown(lr.x + 20, lr.y + 12);
    f.h.mouseUp(lr.x + 20, lr.y + 12);
    f.frame();
    CHECK(f.state.fruit == 0);

    // Open the dropdown and pick the entry below the current one.
    CHECK(f.state.language == 0);
    f.click(f.state.dropdownRect);
    const ww::Rect dr = f.state.dropdownRect;
    const float itemH = dr.h;
    f.h.mouseDown(dr.x + 10, dr.y + dr.h + itemH * 1.5f);
    f.h.mouseUp(dr.x + 10, dr.y + dr.h + itemH * 1.5f);
    f.frame();
    CHECK(f.state.language != 0);
}

void testScroll() {
    Fixture f;
    f.gotoTab(2);
    const ww::Rect sr = f.state.scrollRect;
    f.h.scroll(sr.x + 20, sr.y + 20, 3);
    f.frame();
    // Scrolling must not change selections or crash; the frame still builds.
    CHECK(f.state.fruit == 2);
}

void testTheme() {
    Fixture f;
    CHECK(f.state.darkTheme);
    f.click(f.state.themeButtonRect);
    CHECK(!f.state.darkTheme);
}

void testScaled() {
    // Same UI at scale 2 must behave the same: input coordinates are logical.
    gallery::State state;
    ww::ui::HeadlessUi h(900, 640, 2.0f);
    auto build = [&](ww::ui::Context& ui) { gallery::build(ui, state); };
    h.frame(build);
    const ww::Rect r = state.primaryButtonRect;
    h.mouseDown(r.x + r.w / 2, r.y + r.h / 2);
    h.mouseUp(r.x + r.w / 2, r.y + r.h / 2);
    h.frame(build);
    CHECK(state.clicks == 10);
}

} // namespace

int main() {
    testButtons();
    testToggles();
    testSlider();
    testTextField();
    testTextFieldUtf8();
    testListAndDropdown();
    testScroll();
    testTheme();
    testScaled();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all ui tests passed");
    return 0;
}
