// Headless tests for the stage 10 controls: menus, context menus, tooltips,
// modal dialogs, tree view, spin box. The controls gallery records where it drew
// each control (state.rects), so the tests click on real positions.

#include <cstdio>
#include <string>

#include "controls.hpp"
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
    controls::State state;
    ww::ui::HeadlessUi h{860, 640};

    Fixture() {
        h.context().setTooltipDelayMs(0);
        frame();
    }
    void frame() { h.frame([this](ww::ui::Context& ui) { controls::build(ui, state); }); }
    ww::Rect rect(const char* name) const { return state.rects.at(name); }
    bool has(const char* name) const { return state.rects.count(name) != 0; }
    void clickAt(float x, float y, int clicks = 1) {
        h.mouseDown(x, y, clicks);
        h.mouseUp(x, y);
        frame();
    }
    void click(const char* name, float dx = 0.3f) {
        const ww::Rect r = rect(name);
        clickAt(r.x + r.w * dx, r.y + r.h / 2);
    }
    void key(ww::Key k, ww::Modifiers m = {}) {
        h.key(k, m);
        frame();
    }
    bool nodeVisible(const std::string& name) const {
        for (const auto& n : state.visibleNodes)
            if (n == name) return true;
        return false;
    }
};

void testMenuOpensAndChoosesItem() {
    Fixture f;
    CHECK(!f.h.context().menuOpen());
    CHECK(!f.has("File/New"));              // items only exist while the menu is open
    f.state.dirty = true;
    f.click("File");
    CHECK(f.h.context().menuOpen());
    CHECK(f.has("File/New"));
    f.click("File/New");
    CHECK(f.state.action == "New document");
    CHECK(!f.state.dirty);
    CHECK(!f.h.context().menuOpen());       // choosing an item closes the menu
}

void testMenuClickOutsideCloses() {
    Fixture f;
    f.click("File");
    CHECK(f.h.context().menuOpen());
    const int clicksBefore = f.state.copies;
    f.clickAt(700, 500);                    // empty window area
    CHECK(!f.h.context().menuOpen());
    CHECK(f.state.copies == clicksBefore);
}

void testMenuDoesNotClickThrough() {
    Fixture f;
    f.click("File");
    // "Open..." sits on top of the tree view: the click must not select a tree node.
    f.click("File/Open");
    CHECK(f.state.action == "Open was chosen");
    CHECK(f.state.selectedNode == -1);
}

void testDisabledMenuItem() {
    Fixture f;
    f.click("File");
    f.click("File/Save");                   // not dirty: disabled
    CHECK(f.state.action == "Ready");
    CHECK(f.h.context().menuOpen());        // and the menu stays open
}

void testCheckableMenuItems() {
    Fixture f;
    CHECK(f.state.showStatusBar);
    f.click("View");
    f.click("View/Status");
    CHECK(!f.state.showStatusBar);
    f.click("View");
    f.click("View/Wrap");
    CHECK(!f.state.wordWrap);
}

void testMenuHoverSwitching() {
    Fixture f;
    f.click("File");
    CHECK(f.has("File/New"));
    const ww::Rect view = f.rect("View");
    f.h.mouseMove(view.x + 10, view.y + 10);
    f.frame();
    CHECK(f.has("View/Wrap"));              // pointing at another header switched menus
    CHECK(!f.has("File/New"));
}

void testMenuKeyboard() {
    Fixture f;
    f.state.dirty = true;
    f.click("File");
    f.key(ww::Key::Down);                   // New
    f.key(ww::Key::Down);                   // Open
    f.key(ww::Key::Enter);
    CHECK(f.state.action == "Open was chosen");
    CHECK(!f.h.context().menuOpen());

    f.click("File");
    f.key(ww::Key::Right);                  // moves to the View menu
    CHECK(f.has("View/Wrap"));
    f.key(ww::Key::Escape);
    CHECK(!f.h.context().menuOpen());
}

void testShortcuts() {
    Fixture f;
    f.state.dirty = true;
    f.key(ww::Key::S, ww::Modifiers{.ctrl = true});
    CHECK(f.state.action == "Saved");
    CHECK(!f.state.dirty);
    f.key(ww::Key::Q, ww::Modifiers{.ctrl = true});
    CHECK(f.state.quitAsk);
}

void testContextMenu() {
    Fixture f;
    const ww::Rect list = f.rect("list");
    const size_t before = f.state.items.size();
    f.h.rightClick(list.x + 40, list.y + 20);
    f.frame();
    CHECK(f.h.context().menuOpen());
    CHECK(f.has("ctx/Add"));
    f.click("ctx/Add");
    CHECK(f.state.items.size() == before + 1);
    CHECK(f.state.item == 0);               // the row under the menu was not selected
    CHECK(!f.h.context().menuOpen());

    f.h.rightClick(list.x + 40, list.y + 20);
    f.frame();
    f.click("ctx/Remove");
    CHECK(f.state.items.size() == before);
}

void testTooltip() {
    Fixture f;
    CHECK(!f.h.context().tooltipVisible());
    const ww::Rect open = f.rect("button:Open");
    f.h.mouseMove(open.x + 10, open.y + 10);
    f.frame();
    CHECK(f.h.context().tooltipVisible());
    f.h.mouseMove(700, 560);
    f.frame();
    CHECK(!f.h.context().tooltipVisible());
    // Pressing the button hides it, it is in the way of the click.
    f.h.mouseDown(open.x + 10, open.y + 10);
    f.frame();
    CHECK(!f.h.context().tooltipVisible());
    f.h.mouseUp(open.x + 10, open.y + 10);
    f.frame();
}

void testTooltipDelay() {
    controls::State state;
    ww::ui::HeadlessUi h(860, 640);        // default delay: 500 ms
    auto build = [&](ww::ui::Context& ui) { controls::build(ui, state); };
    h.frame(build);
    const ww::Rect open = state.rects.at("button:Open");
    h.mouseMove(open.x + 10, open.y + 10);
    h.frame(build);
    CHECK(!h.context().tooltipVisible());   // not yet
    const int wake = h.context().nextWakeupMs();
    CHECK(wake > 0 && wake <= 500);         // but the window loop is asked to come back
}

void testAboutDialogIsModal() {
    Fixture f;
    f.click("Help");
    f.click("Help/About");
    CHECK(f.state.aboutOpen);
    f.frame();

    // The window behind is inert: clicking the check box does nothing.
    const bool before = f.state.notifications;
    f.click("check", 0.1f);
    CHECK(f.state.notifications == before);
    CHECK(f.state.aboutOpen);

    f.click("about/OK", 0.5f);
    CHECK(!f.state.aboutOpen);
    f.frame();
    f.click("check", 0.1f);                 // alive again
    CHECK(f.state.notifications != before);
}

void testDialogEscape() {
    Fixture f;
    f.state.aboutOpen = true;
    f.frame();
    f.key(ww::Key::Escape);
    CHECK(!f.state.aboutOpen);
}

void testQuitDialog() {
    Fixture f;
    f.state.quitAsk = true;
    f.frame();
    const ww::Rect stay = f.rect("quit/Stay");
    f.clickAt(stay.x + stay.w / 2, stay.y + stay.h / 2);
    CHECK(!f.state.quitAsk);
    CHECK(!f.state.quit);

    f.state.quitAsk = true;
    f.frame();
    const ww::Rect stay2 = f.rect("quit/Stay");
    f.clickAt(stay2.x - 50, stay2.y + stay2.h / 2);   // the "Quit" button is left of "Stay"
    CHECK(f.state.quit);
}

void testTree() {
    Fixture f;
    CHECK(f.nodeVisible("src"));
    CHECK(!f.nodeVisible("main.cpp"));      // collapsed

    f.click("node:src", 0.04f);             // the arrow
    CHECK(f.nodeVisible("main.cpp"));
    CHECK(f.state.selectedNode == -1);      // expanding does not select

    f.click("node:main.cpp", 0.5f);         // the label
    CHECK(f.state.selectedName == "main.cpp");
    const int id = f.state.selectedNode;
    CHECK(id >= 0);

    f.click("node:src", 0.04f);             // collapse again
    CHECK(!f.nodeVisible("main.cpp"));
    f.click("node:src", 0.04f);
    CHECK(f.state.selectedNode == id);      // the selection id did not shift while collapsed

    // Double click on a label toggles.
    const ww::Rect docs = f.rect("node:docs");
    f.clickAt(docs.x + docs.w * 0.5f, docs.y + docs.h / 2, 2);
    CHECK(f.nodeVisible("ROADMAP.md"));

    // Equal names under different parents do not collide: open both, close one.
    f.click("node:book", 0.04f);
    CHECK(f.nodeVisible("ch01.md"));
}

void testSpinBox() {
    Fixture f;
    CHECK(f.state.copies == 1);
    const ww::Rect r = f.rect("spin");
    f.clickAt(r.right() - 13, r.y + 7);     // up arrow
    CHECK(f.state.copies == 2);
    f.clickAt(r.right() - 13, r.y + r.h - 7);   // down arrow
    CHECK(f.state.copies == 1);
    f.clickAt(r.right() - 13, r.y + r.h - 7);   // clamped at the minimum
    CHECK(f.state.copies == 1);
    CHECK(f.state.dirty);

    f.key(ww::Key::Up);                      // the box has focus after the click
    f.key(ww::Key::Up);
    CHECK(f.state.copies == 3);
    f.key(ww::Key::End);
    CHECK(f.state.copies == 20);
    f.key(ww::Key::Up);
    CHECK(f.state.copies == 20);

    f.h.scroll(r.x + 20, r.y + 10, -1);      // wheel up counts up (already at max) / down counts down
    f.frame();
    f.h.scroll(r.x + 20, r.y + 10, 1);
    f.frame();
    CHECK(f.state.copies == 19);
}

void testSaveButtonEnabledByDirty() {
    Fixture f;
    f.click("button:Save", 0.5f);            // disabled: nothing happens
    CHECK(f.state.action == "Ready");
    f.state.dirty = true;
    f.frame();
    f.click("button:Save", 0.5f);
    CHECK(f.state.action == "Saved");
    CHECK(!f.state.dirty);
}

void testScaled() {
    controls::State state;
    ww::ui::HeadlessUi h(860, 640, 2.0f);
    auto build = [&](ww::ui::Context& ui) { controls::build(ui, state); };
    h.frame(build);
    const ww::Rect file = state.rects.at("File");
    h.mouseDown(file.x + 10, file.y + 10);
    h.mouseUp(file.x + 10, file.y + 10);
    h.frame(build);
    CHECK(h.context().menuOpen());
    CHECK(state.rects.count("File/New") == 1);
}

} // namespace

int main() {
    testMenuOpensAndChoosesItem();
    testMenuClickOutsideCloses();
    testMenuDoesNotClickThrough();
    testDisabledMenuItem();
    testCheckableMenuItems();
    testMenuHoverSwitching();
    testMenuKeyboard();
    testShortcuts();
    testContextMenu();
    testTooltip();
    testTooltipDelay();
    testAboutDialogIsModal();
    testDialogEscape();
    testQuitDialog();
    testTree();
    testSpinBox();
    testSaveButtonEnabledByDirty();
    testScaled();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all controls tests passed");
    return 0;
}
