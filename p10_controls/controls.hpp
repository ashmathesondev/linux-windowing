#pragma once
// Stage 10: the controls gallery. One function that describes a small "file manager"
// style window using the controls a Win32 programmer would reach for: menu bar,
// context menu, tooltips, tree view, spin box, list box, combo box, check and radio
// buttons, slider, progress bar, status bar and modal dialogs.
//
// It is shared by the interactive app (main.cpp) and the tests (tests/controls_test.cpp).

#include <map>
#include <string>
#include <vector>

#include "ww/ui.hpp"

namespace controls {

struct Node {
    std::string name;
    std::vector<Node> children;
};

inline const Node& projectTree() {
    static const Node root{"linux-windowing",
                           {{"src", {{"main.cpp", {}}, {"window.cpp", {}}, {"ui.cpp", {}}}},
                            {"include", {{"window.hpp", {}}, {"ui.hpp", {}}}},
                            {"docs", {{"book", {{"ch01.md", {}}, {"ch02.md", {}}}}, {"ROADMAP.md", {}}}},
                            {"CMakeLists.txt", {}},
                            {"README.md", {}}}};
    return root;
}

inline int countNodes(const Node& n) {
    int c = 1;
    for (const Node& child : n.children) c += countNodes(child);
    return c;
}

struct State {
    // Document
    bool dirty = false;
    std::string action = "Ready";

    // View menu
    bool wordWrap = true;
    bool showStatusBar = true;
    bool darkTheme = true;

    // Dialogs
    bool aboutOpen = false;
    bool quitAsk = false;
    bool quit = false;

    // Tree
    int selectedNode = -1;
    std::string selectedName;

    // Controls
    int copies = 1;
    float volume = 0.35f;
    int quality = 1;
    bool notifications = true;
    std::vector<std::string> items = {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot"};
    int item = 0;
    std::vector<std::string> units = {"Pixels", "Points", "Millimetres"};
    int unit = 0;

    // Filled in while drawing, so tests (and the screenshot script) can find things on screen.
    std::map<std::string, ww::Rect> rects;
    std::vector<std::string> visibleNodes;
};

inline void rec(State& s, ww::ui::Context& ui, const char* name) { s.rects[name] = ui.lastRect(); }

inline void drawNode(ww::ui::Context& ui, State& s, const Node& n, int& counter) {
    const int myId = counter++;
    const bool selected = s.selectedNode == myId;
    s.visibleNodes.push_back(n.name);

    if (n.children.empty()) {
        if (ui.treeLeaf(n.name, selected)) {
            s.selectedNode = myId;
            s.selectedName = n.name;
            s.action = "Clicked " + n.name;
        }
        rec(s, ui, ("node:" + n.name).c_str());
        return;
    }
    const auto node = ui.treeNode(n.name, selected, /*defaultOpen=*/myId == 0);
    rec(s, ui, ("node:" + n.name).c_str());
    if (node.clicked) {
        s.selectedNode = myId;
        s.selectedName = n.name;
        s.action = "Clicked " + n.name;
    }
    if (node.open) {
        for (const Node& child : n.children) drawNode(ui, s, child, counter);
        ui.treePop();
    } else {
        // Ids must not depend on whether a node is open, so skip over the hidden subtree.
        for (const Node& child : n.children) counter += countNodes(child);
    }
}

inline void build(ww::ui::Context& ui, State& s) {
    using ww::ui::LabelStyle;
    s.visibleNodes.clear();
    s.rects.clear();   // only what is drawn right now (menu items exist only while their menu is open)

    // ---- keyboard shortcuts (before any widget that could take the key) ------------------------
    if (ui.shortcut(ww::Key::N, true)) { s.action = "New document"; s.dirty = false; }
    if (ui.shortcut(ww::Key::S, true) && s.dirty) { s.action = "Saved"; s.dirty = false; }
    if (ui.shortcut(ww::Key::Q, true)) s.quitAsk = true;

    // ---- menu bar -------------------------------------------------------------------------------
    if (ui.beginMenuBar()) {
        if (ui.beginMenu("File")) {
            rec(s, ui, "File");
            if (ui.menuItem("New", "Ctrl+N")) { s.action = "New document"; s.dirty = false; }
            rec(s, ui, "File/New");
            if (ui.menuItem("Open...", "Ctrl+O")) s.action = "Open was chosen";
            rec(s, ui, "File/Open");
            if (ui.menuItem("Save", "Ctrl+S", nullptr, s.dirty)) { s.action = "Saved"; s.dirty = false; }
            rec(s, ui, "File/Save");
            ui.menuSeparator();
            if (ui.menuItem("Exit", "Ctrl+Q")) s.quitAsk = true;
            rec(s, ui, "File/Exit");
            ui.endMenu();
        } else {
            rec(s, ui, "File");
        }
        if (ui.beginMenu("View")) {
            rec(s, ui, "View");
            ui.menuItem("Word wrap", {}, &s.wordWrap);
            rec(s, ui, "View/Wrap");
            ui.menuItem("Status bar", {}, &s.showStatusBar);
            rec(s, ui, "View/Status");
            ui.menuSeparator();
            const bool wasDark = s.darkTheme;
            ui.menuItem("Dark theme", {}, &s.darkTheme);
            if (wasDark != s.darkTheme)
                ui.setTheme(s.darkTheme ? ww::ui::Theme::dark() : ww::ui::Theme::light());
            ui.endMenu();
        } else {
            rec(s, ui, "View");
        }
        if (ui.beginMenu("Help")) {
            rec(s, ui, "Help");
            if (ui.menuItem("About")) s.aboutOpen = true;
            rec(s, ui, "Help/About");
            ui.endMenu();
        } else {
            rec(s, ui, "Help");
        }
        ui.endMenuBar();
    }
    ui.space(2);

    const float left = 250;
    ui.beginRow();

    // ---- left: tree view -----------------------------------------------------------------------
    ui.beginColumn(left);
    ui.beginCard("Project");
    ui.beginScroll("tree", 300);
    int counter = 0;
    drawNode(ui, s, projectTree(), counter);
    ui.endScroll();
    ui.endCard();
    ui.endColumn();

    // ---- right: assorted controls -----------------------------------------------------------------
    ui.beginColumn(-1);
    ui.beginCard("Controls");
    ui.beginRow();
    if (ui.button("Open", true)) s.action = "Open button";
    rec(s, ui, "button:Open");
    ui.tooltip("Open a file (Ctrl+O)");
    if (!s.dirty) ui.beginDisabled();   // nothing to save: the button is drawn dimmed and ignores input
    const bool saveClicked = ui.button("Save");
    if (!s.dirty) ui.endDisabled();
    if (saveClicked) { s.action = "Saved"; s.dirty = false; }
    rec(s, ui, "button:Save");
    ui.tooltip("Save the document (Ctrl+S)");
    ui.endRow();

    ui.beginRow();
    ui.width(70);
    ui.label("Copies");
    if (ui.spinBox("copies", s.copies, 1, 20)) {
        s.dirty = true;
        s.action = "Copies: " + std::to_string(s.copies);
    }
    rec(s, ui, "spin");
    ui.endRow();

    if (ui.checkbox("Notifications", s.notifications)) s.dirty = true;
    rec(s, ui, "check");
    ui.beginRow();
    ui.radio("Low", s.quality, 0);
    ui.radio("Medium", s.quality, 1);
    ui.radio("High", s.quality, 2);
    rec(s, ui, "radioHigh");
    ui.endRow();

    ui.slider("volume", s.volume, 0, 1);
    ui.progressBar(s.volume, std::to_string(static_cast<int>(s.volume * 100)) + "%");
    ui.endCard();

    ui.beginCard("List and combo box");
    ui.listBox("items", s.items, s.item, 4);
    rec(s, ui, "list");
    if (ui.beginContextMenu("listctx")) {
        if (ui.menuItem("Add item")) {
            s.items.push_back("Item " + std::to_string(s.items.size() + 1));
            s.action = "Added an item";
        }
        rec(s, ui, "ctx/Add");
        if (ui.menuItem("Remove selected", {}, nullptr, s.item >= 0 && s.item < static_cast<int>(s.items.size()))) {
            s.items.erase(s.items.begin() + s.item);
            s.item = std::min(s.item, static_cast<int>(s.items.size()) - 1);
            s.action = "Removed an item";
        }
        rec(s, ui, "ctx/Remove");
        ui.endContextMenu();
    }
    ui.beginRow();
    ui.width(70);
    ui.label("Units");
    ui.dropdown("units", s.units, s.unit);
    ui.endRow();
    ui.endCard();
    ui.endColumn();
    ui.endRow();

    // ---- status bar and dialogs: last, so they are drawn on top ---------------------------------
    if (s.showStatusBar) {
        ui.statusBar({s.action, s.selectedName.empty() ? "Nothing selected" : "Selected: " + s.selectedName,
                      "Copies: " + std::to_string(s.copies), s.dirty ? "Modified" : "Saved"});
    }

    const bool aboutWasOpen = s.aboutOpen;
    if (ui.messageBox("About", "Stage 10: controls gallery\nBuilt with Xlib/Wayland, Cairo and Pango.", {"OK"},
                      s.aboutOpen) == 0) {
        s.action = "About closed";
    }
    if (aboutWasOpen) rec(s, ui, "about/OK");

    const bool quitWasOpen = s.quitAsk;
    const int answer = ui.messageBox("Quit?", "Leave the program?\nNothing has been saved.", {"Quit", "Stay"}, s.quitAsk);
    if (answer == 0) s.quit = true;
    if (quitWasOpen) rec(s, ui, "quit/Stay");   // the last button of the row
}

} // namespace controls
