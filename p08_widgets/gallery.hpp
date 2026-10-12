#pragma once
// The widget gallery: one function that describes the whole UI. It is shared by
// the interactive app (main.cpp) and the automated tests (tests/ui_test.cpp).
//
// Read it top to bottom: this is what "immediate mode" code looks like. There is
// no widget tree and no callbacks, just calls whose return values you act on.

#include <string>
#include <vector>

#include "ww/ui.hpp"

namespace gallery {

struct State {
    int tab = 0;
    bool darkTheme = true;

    // Basics
    int clicks = 0;
    bool notifications = true;
    bool autoSave = false;
    int quality = 1;               // radio group: 0 low, 1 medium, 2 high
    float volume = 0.35f;
    float brightness = 0.8f;

    // Text
    std::string name = "Ash";
    std::string email;
    std::string search;

    // Lists
    std::vector<std::string> fruits = {"Apple", "Banana", "Cherry", "Dragon fruit", "Elderberry", "Fig",
                                       "Grape", "Honeydew", "Kiwi", "Lemon"};
    int fruit = 2;
    std::vector<std::string> languages = {"C++", "Rust", "Zig", "C", "Python"};
    int language = 0;

    // Filled in while drawing, used by tests to find widgets on screen.
    ww::Rect tabsRect;
    ww::Rect primaryButtonRect;
    ww::Rect nameFieldRect;
    ww::Rect emailFieldRect;
    ww::Rect checkboxRect;
    ww::Rect radioHighRect;
    ww::Rect volumeSliderRect;
    ww::Rect listRect;
    ww::Rect dropdownRect;
    ww::Rect scrollRect;
    ww::Rect themeButtonRect;
};

inline void build(ww::ui::Context& ui, State& s) {
    using ww::ui::LabelStyle;

    // ---- header ------------------------------------------------------------------------------
    ui.beginRow(36);
    ui.label("Widget gallery", LabelStyle::Heading);
    const std::string themeLabel = s.darkTheme ? "Light theme" : "Dark theme";
    ui.space(ui.availableWidth() - ui.textWidth(themeLabel) - 32 - 8);
    if (ui.button(themeLabel)) {
        s.darkTheme = !s.darkTheme;
        ui.setTheme(s.darkTheme ? ww::ui::Theme::dark() : ww::ui::Theme::light());
    }
    s.themeButtonRect = ui.lastRect();
    ui.endRow();

    ui.tabBar("tabs", {"Basics", "Text input", "Lists and scrolling"}, s.tab);
    s.tabsRect = ui.lastRect();
    ui.space(6);

    const float half = (ui.availableWidth() - ui.theme().spacing) / 2;

    // ---- tab 0: buttons, toggles, sliders -------------------------------------------------------------
    if (s.tab == 0) {
        ui.beginRow();
        ui.beginColumn(half);
        ui.beginCard("Buttons and toggles");
        ui.beginRow();
        if (ui.button("Default")) ++s.clicks;
        if (ui.button("Primary", true)) s.clicks += 10;
        s.primaryButtonRect = ui.lastRect();
        ui.beginDisabled();
        ui.button("Disabled");
        ui.endDisabled();
        ui.endRow();
        ui.label("Click counter: " + std::to_string(s.clicks), LabelStyle::Dim);
        ui.separator();
        ui.checkbox("Enable notifications", s.notifications);
        s.checkboxRect = ui.lastRect();
        ui.checkbox("Save automatically", s.autoSave);
        ui.label("Quality", LabelStyle::Dim);
        ui.beginRow();
        ui.radio("Low", s.quality, 0);
        ui.radio("Medium", s.quality, 1);
        ui.radio("High", s.quality, 2);
        s.radioHighRect = ui.lastRect();
        ui.endRow();
        ui.endCard();
        ui.endColumn();

        ui.beginColumn(-1);
        ui.beginCard("Sliders and progress");
        ui.beginRow();
        ui.width(86);
        ui.label("Volume");
        ui.width(-1);
        ui.slider("volume", s.volume, 0, 1);
        s.volumeSliderRect = ui.lastRect();
        ui.endRow();
        ui.beginRow();
        ui.width(86);
        ui.label("Brightness");
        ui.width(-1);
        ui.slider("brightness", s.brightness, 0, 1);
        ui.endRow();
        ui.separator();
        ui.label("Progress follows the volume slider", LabelStyle::Dim);
        ui.progressBar(s.volume, std::to_string(static_cast<int>(s.volume * 100 + 0.5f)) + "%");
        ui.endCard();
        ui.endColumn();
        ui.endRow();
    }

    // ---- tab 1: text fields ------------------------------------------------------------------------------
    if (s.tab == 1) {
        ui.beginCard("Profile");
        ui.beginRow();
        ui.width(80);
        ui.label("Name");
        ui.width(-1);
        ui.textField("name", s.name, "Your name");
        s.nameFieldRect = ui.lastRect();
        ui.endRow();
        ui.beginRow();
        ui.width(80);
        ui.label("Email");
        ui.width(-1);
        ui.textField("email", s.email, "name@example.com");
        s.emailFieldRect = ui.lastRect();
        ui.endRow();
        ui.beginRow();
        ui.width(80);
        ui.label("Search");
        ui.width(-1);
        ui.textField("search", s.search, "Type to search...");
        ui.endRow();
        ui.endCard();

        ui.beginCard("Editing keys");
        ui.label("Click and drag to select, double click for a word, triple click for everything.", LabelStyle::Dim);
        ui.label("Shift+arrows extend the selection, Ctrl+arrows jump by word, Home and End go to the ends.", LabelStyle::Dim);
        ui.label("Ctrl+A select all, Ctrl+C copy, Ctrl+X cut, Ctrl+V paste.", LabelStyle::Dim);
        ui.label("Hello " + (s.name.empty() ? std::string("stranger") : s.name) + ", your name has " +
                 std::to_string(s.name.size()) + " bytes.");
        ui.endCard();
    }

    // ---- tab 2: lists and scrolling ---------------------------------------------------------------------------
    if (s.tab == 2) {
        ui.beginRow();
        ui.beginColumn(half);
        ui.beginCard("List box");
        ui.listBox("fruits", s.fruits, s.fruit, 6);
        s.listRect = ui.lastRect();
        ui.label("Selected: " + s.fruits[static_cast<size_t>(s.fruit)], LabelStyle::Dim);
        ui.endCard();
        ui.endColumn();

        ui.beginColumn(-1);
        ui.beginCard("Dropdown and scroll area");
        ui.beginRow();
        ui.label("Language");
        ui.dropdown("language", s.languages, s.language);
        s.dropdownRect = ui.lastRect();
        ui.endRow();
        ui.beginScroll("log", 150);
        s.scrollRect = ui.lastRect();
        for (int i = 1; i <= 24; ++i) {
            ui.label("Log line " + std::to_string(i) + ": everything is fine", i % 5 == 0 ? LabelStyle::Normal : LabelStyle::Dim);
        }
        ui.endScroll();
        ui.endCard();
        ui.endColumn();
        ui.endRow();
    }

    ui.label("Tab and Shift+Tab move the focus. Space or Enter press the focused widget.", LabelStyle::Dim);
}

} // namespace gallery
