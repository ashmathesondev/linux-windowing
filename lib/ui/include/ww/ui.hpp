#pragma once
// A small immediate mode UI toolkit.
//
// "Immediate mode" means there are no widget objects. Every frame your code
// describes the UI by calling functions, and each call draws the widget and
// returns what the user did with it:
//
//     if (ui.button("Save")) save();
//     ui.checkbox("Dark mode", darkMode);
//     ui.textField("name", name);
//
// The Context only keeps the small amount of state that has to survive between
// frames (which widget is hovered/active/focused, text caret positions, scroll
// offsets). Your application owns all data.
//
// Per frame:
//
//     ui.handleEvent(ev);            // for every window event received
//     ui.beginFrame(renderer);       // clears the background, starts the layout
//     ... widget calls ...
//     ui.endFrame();
//     if (ui.needsRedraw()) ...      // layout settled? draw one more frame
//
// Layout is a simple flow: widgets stack vertically; beginRow()/endRow() place
// them side by side; beginColumn() nests a vertical stack inside a row.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ww/event.hpp"
#include "ww/renderer.hpp"
#include "ww/window.hpp"

namespace ww::ui {

using Id = uint32_t;

struct Theme {
    Color background, panel, panelBorder;
    Color text, textDim, textOnAccent;
    Color accent, accentHover, accentPressed;
    Color widget, widgetHover, widgetPressed, widgetBorder;
    Color track, selection, shadow;
    Font font;
    Font headingFont;
    float rowHeight = 30;
    float padding = 14;     // window and card padding
    float spacing = 8;      // gap between widgets
    float radius = 6;

    static Theme dark();
    static Theme light();
};

enum class LabelStyle { Normal, Dim, Heading };

class Context {
public:
    Context();

    void setTheme(const Theme& theme) { theme_ = theme; }
    const Theme& theme() const { return theme_; }

    // Clipboard access for the text field (Ctrl+C / X / V). Window::setClipboardText
    // and Window::clipboardText plug straight in; without it an in-memory clipboard is used.
    void setClipboard(std::function<void(const std::string&)> set, std::function<std::string()> get);

    // ---- frame ---------------------------------------------------------------
    void handleEvent(const Event& event);
    // Starts a frame. The renderer must outlive the frame. Pass the area to lay out in logical pixels.
    void beginFrame(Renderer& renderer);
    void endFrame();

    // True when something changed that is only visible one frame later (for
    // example a card that grew). The application should draw another frame.
    bool needsRedraw() const { return needsRedraw_; }
    // Milliseconds until the UI wants a frame even without input (caret blink), or -1.
    int nextWakeupMs() const;
    // Mouse cursor shape the UI wants right now.
    Cursor cursor() const { return cursor_; }
    // True if the UI is using the keyboard (a text field has focus).
    bool wantsTextInput() const { return textFocusActive_; }

    // ---- layout ---------------------------------------------------------------
    void beginRow(float height = 0);            // widgets go left to right; height 0 = theme row height
    void endRow();
    void beginColumn(float width = -1);         // vertical stack inside a row; width -1 = rest of the row
    void endColumn();
    void beginCard(std::string_view title = {});   // padded panel with optional heading
    void endCard();
    void beginScroll(std::string_view id, float height);
    void endScroll();
    void space(float height = 8);
    void separator();
    // Width for the next widget only: > 0 fixed, -1 = fill the remaining space, 0 = natural.
    void width(float w) { nextWidth_ = w; }
    void pushId(int id);
    void popId();
    // Widgets between these calls ignore input and are drawn dimmed.
    void beginDisabled();
    void endDisabled();

    // ---- widgets ----------------------------------------------------------------
    void label(std::string_view text, LabelStyle style = LabelStyle::Normal);
    bool button(std::string_view label, bool primary = false);
    bool checkbox(std::string_view label, bool& value);
    bool radio(std::string_view label, int& value, int thisValue);
    bool slider(std::string_view id, float& value, float minValue, float maxValue);
    void progressBar(float fraction, std::string_view overlay = {});
    // Single line text input. `id` must be unique among siblings. Returns true when the text changed.
    bool textField(std::string_view id, std::string& text, std::string_view placeholder = {});
    bool listBox(std::string_view id, const std::vector<std::string>& items, int& selected, int visibleRows = 5);
    bool dropdown(std::string_view id, const std::vector<std::string>& items, int& selected);
    bool tabBar(std::string_view id, const std::vector<std::string>& tabs, int& selected);

    // ---- menus (stage 10) ---------------------------------------------------------------------
    // A menu bar is a strip of headers; each open menu is a popup built from menuItem calls:
    //
    //     if (ui.beginMenuBar()) {
    //         if (ui.beginMenu("File")) {
    //             if (ui.menuItem("Open...", "Ctrl+O")) openFile();
    //             ui.menuSeparator();
    //             ui.menuItem("Word wrap", {}, &wrap);       // checkable
    //             ui.endMenu();
    //         }
    //         ui.endMenuBar();
    //     }
    //
    // The shortcut text is only displayed. Use shortcut() to react to the keys themselves.
    bool beginMenuBar();
    void endMenuBar();
    bool beginMenu(std::string_view label);      // true while this menu is open: add items, then endMenu()
    void endMenu();
    bool menuItem(std::string_view label, std::string_view shortcut = {}, bool* checked = nullptr, bool enabled = true);
    void menuSeparator();
    // Right click on the most recent widget opens a popup menu at the mouse. Same items as above.
    bool beginContextMenu(std::string_view id);
    void endContextMenu();
    bool menuOpen() const { return openMenu_ != 0; }
    // True (once) when the key combination was pressed. Call it before widgets that own the keyboard.
    bool shortcut(Key key, bool ctrl = false, bool shift = false, bool alt = false);

    // ---- tooltips ---------------------------------------------------------------------------------
    // Call right after a widget: shows `text` near the mouse once the pointer rested on it for a moment.
    void tooltip(std::string_view text);
    void setTooltipDelayMs(int ms) { tipDelayMs_ = ms; }
    bool tooltipVisible() const { return tipShown_; }

    // ---- dialogs ---------------------------------------------------------------------------------
    // A modal dialog dims the window and blocks every widget drawn outside it. Call it LAST in the
    // frame, so it is drawn on top. Returns false (and draws nothing) while `open` is false;
    // Escape sets `open` to false.
    bool beginModal(std::string_view title, bool& open, float width = 380);
    void endModal();
    bool modalOpen() const { return modalOpenNow_ || blockOutside_; }
    // Ready made message box. Returns the index of the clicked button, or -1 while nothing was chosen.
    int messageBox(std::string_view title, std::string_view message, const std::vector<std::string>& buttons, bool& open);

    // ---- tree view ---------------------------------------------------------------------------------
    struct TreeResult {
        bool open = false;      // the node is expanded: draw its children, then call treePop()
        bool clicked = false;   // the row was clicked this frame (select it in your own data)
    };
    // defaultOpen only matters the first time the node is seen; after that the user decides.
    TreeResult treeNode(std::string_view label, bool selected = false, bool defaultOpen = false);
    bool treeLeaf(std::string_view label, bool selected = false);   // returns true when clicked
    void treePop();

    // ---- more controls ---------------------------------------------------------------------------
    // Number field with up/down buttons. Arrow keys and the mouse wheel work too.
    bool spinBox(std::string_view id, int& value, int minValue, int maxValue, int step = 1);
    // Pinned to the bottom edge of the window. Call it last; panes are drawn left to right.
    void statusBar(const std::vector<std::string>& panes);

    // Width left in the current row or column, and the width a text would take in the UI font.
    // Together they allow right aligned widgets: ui.space(ui.availableWidth() - ui.textWidth("Save") - 32).
    float availableWidth() const;
    float textWidth(std::string_view text);

    // ---- queries (valid right after a widget call) ---------------------------------------
    bool lastHovered() const { return lastHovered_; }
    // Rectangle of the most recent widget. Mainly for tests that need to click on a widget.
    Rect lastRect() const { return lastRect_; }

private:
    // --- types -----------------------------------------------------------------------
    struct Layout {
        Rect bounds;               // horizontal extent (and the starting corner)
        float x = 0, y = 0;        // cursor
        bool horizontal = false;
        float rowTop = 0;          // horizontal: top of the row
        float rowHeight = 0;       // horizontal: height reserved for centred widgets
        float rowExtent = 0;       // horizontal: tallest column so far
        float startY = 0;          // vertical: where this stack began
        bool isScrollRoot = false;
    };
    struct TextState {
        size_t cursor = 0;
        size_t anchor = 0;         // selection = [min(cursor, anchor), max(cursor, anchor))
        float scrollX = 0;
    };
    struct ScrollState {
        float offset = 0;
        float contentHeight = 0;   // measured last frame
        float viewHeight = 0;
        bool dragging = false;
        float dragGrabY = 0;
    };
    struct ListState {
        float offset = 0;          // in rows
    };
    struct ScrollFrame {
        Id id;
        Rect view;
    };
    struct Input {
        float mouseX = -10000, mouseY = -10000;
        bool down[3] = {false, false, false};
        bool pressed[3] = {false, false, false};
        bool released[3] = {false, false, false};
        int clickCount = 0;
        float scrollX = 0, scrollY = 0;
        bool moved = false;        // the pointer moved since the last frame
        Modifiers mods;
        std::vector<std::variant<KeyEvent, TextEvent>> keyboard;   // in arrival order
    };

    // --- helpers -----------------------------------------------------------------------
    Layout& layout() { return layouts_.back(); }
    Rect allocate(float naturalWidth, float height);
    Id makeId(std::string_view label) const;
    bool hovered(Rect r) const;
    bool enabled() const { return disabledDepth_ == 0; }
    // Standard press/release logic. Returns true on click.
    bool clickBehavior(Id id, Rect r, bool& hover, bool& held);
    void registerFocusable(Id id);
    bool focused(Id id) const { return focusId_ == id; }
    TextSize measure(std::string_view text, const Font& font);
    Color dim(Color c) const { return enabled() ? c : c.withAlpha(c.a * 0.45f); }
    void drawFocusRing(Rect r, float radius);
    void drawText(Rect r, std::string_view text, Color color, bool center, const Font* font = nullptr);
    void drawScrollbar(Rect track, float offset, float contentHeight, float viewHeight, bool hot);
    bool consumeKey(Key key, bool* shift = nullptr);
    std::string readClipboard();
    void writeClipboard(const std::string& text);
    void resetBlink() { blinkEpoch_ = Clock::now(); }

    using Clock = std::chrono::steady_clock;

    // --- state ---------------------------------------------------------------------------
    Theme theme_;
    Renderer* renderer_ = nullptr;
    Input input_;
    Cursor cursor_ = Cursor::Arrow;
    bool needsRedraw_ = false;
    bool textFocusActive_ = false;
    bool lastHovered_ = false;
    Rect lastRect_{};

    std::vector<Layout> layouts_;
    std::vector<Id> idStack_;
    std::vector<Rect> clipStack_;
    int disabledDepth_ = 0;
    float nextWidth_ = 0;

    Id hotId_ = 0;
    Id activeId_ = 0;
    Id focusId_ = 0;
    Id lastFocusable_ = 0;
    Id firstFocusable_ = 0;
    bool tabRequested_ = false;
    bool shiftTabRequested_ = false;
    bool pressClaimed_ = false;
    bool scrollConsumed_ = false;

    std::unordered_map<Id, TextState> textStates_;
    std::unordered_map<Id, ScrollState> scrollStates_;
    std::unordered_map<Id, ListState> listStates_;
    std::unordered_map<Id, float> cardHeights_;      // measured last frame, used to draw card backgrounds
    std::unordered_map<Id, float> cardHeightsNow_;
    std::vector<ScrollFrame> scrollStack_;
    std::vector<Id> cardStack_;
    std::vector<float> cardStartY_;

    Id openDropdown_ = 0;
    Rect popupRect_{};            // area of the open popup this frame; blocks hover for widgets below
    bool popupOpenNow_ = false;
    bool prevPopupActive_ = false;   // last frame's popup: widgets drawn before it in this frame must still ignore it
    Rect prevPopupRect_{};
    std::vector<std::function<void(Renderer&)>> overlays_;

    Clock::time_point blinkEpoch_ = Clock::now();

    // menus
    struct MenuBarState {
        Rect rect{};
        float x = 0;
    };
    struct MenuBuild {
        bool active = false;
        Id id = 0;
        Rect popup{};
        float y = 0;        // cursor inside the popup
        float widest = 0;   // widest item this frame, used for the width next frame
        int index = 0;      // number of items so far (separators do not count)
    };
    void beginMenuPopup(Id id, float x, float y);
    void endMenuPopup();
    Id openMenu_ = 0;
    bool openMenuIsBar_ = false;
    bool menuSeenNow_ = false;
    int menuHighlight_ = -1;
    float ctxX_ = 0, ctxY_ = 0;
    MenuBarState menuBar_;
    std::vector<Id> barMenusNow_;
    MenuBuild mb_;
    std::unordered_map<Id, std::pair<float, float>> menuSizes_;   // width, height from the previous frame

    // tooltips
    Id tipKey_ = 0;
    bool tipHoveredNow_ = false;
    bool tipShownNow_ = false;
    bool tipShown_ = false;
    int tipWakeMs_ = -1;
    int tipDelayMs_ = 500;
    Clock::time_point tipSince_ = Clock::now();

    // modal dialogs and trees
    struct ModalFrame {
        Id id;
        float top;
    };
    TreeResult treeRow(std::string_view label, bool selected, bool leaf, bool defaultOpen);
    bool modalOpenNow_ = false;    // a modal was drawn this frame
    bool blockOutside_ = false;    // a modal was drawn last frame: widgets outside it are inert
    bool insideModal_ = false;
    std::vector<ModalFrame> modalStack_;
    std::unordered_map<Id, float> modalHeights_;
    std::unordered_map<Id, bool> treeOpen_;
    int treeDepth_ = 0;

    std::function<void(const std::string&)> clipboardSet_;
    std::function<std::string()> clipboardGet_;
    std::string memoryClipboard_;

    std::unordered_map<std::string, TextSize> measureCache_;
};

// Runs the frame callback until the UI has settled. Widgets react to input while
// they are drawn, so something drawn early in the frame (a progress bar) can show
// the value of something that changes later in the same frame (a slider). One extra pass
// after input fixes that, and so does the pass requested by needsRedraw().
template <typename BuildFn>
void drawFrame(Context& ui, Renderer& renderer, BuildFn&& build, bool hadInput) {
    const int minimumPasses = hadInput ? 2 : 1;
    int pass = 0;
    do {
        ui.beginFrame(renderer);
        build(ui);
        ui.endFrame();
        ++pass;
    } while (pass < minimumPasses || (ui.needsRedraw() && pass < 4));
}

} // namespace ww::ui
