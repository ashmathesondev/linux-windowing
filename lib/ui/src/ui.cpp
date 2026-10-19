#include "ww/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace ww::ui {

namespace {

// ---- small helpers ---------------------------------------------------------------

Id hashString(std::string_view s, Id seed) {
    Id h = seed ? seed : 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;   // FNV-1a
    return h ? h : 1;
}

// "Label##id" shows "Label" but uses the whole string for the id, so two
// buttons with the same caption can be told apart.
std::string_view displayText(std::string_view label) {
    const size_t pos = label.find("##");
    return pos == std::string_view::npos ? label : label.substr(0, pos);
}

Rect intersect(Rect a, Rect b) {
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
    return {x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f)};
}

// ---- UTF-8 navigation -------------------------------------------------------------------

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

size_t prevBoundary(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && isContinuation(s[i])) --i;
    return i;
}

size_t nextBoundary(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && isContinuation(s[i])) ++i;
    return i;
}

bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u == '_' || u >= 0x80;
}

size_t wordLeft(const std::string& s, size_t i) {
    while (i > 0 && !isWordChar(s[prevBoundary(s, i)])) i = prevBoundary(s, i);
    while (i > 0 && isWordChar(s[prevBoundary(s, i)])) i = prevBoundary(s, i);
    return i;
}

size_t wordRight(const std::string& s, size_t i) {
    while (i < s.size() && !isWordChar(s[i])) i = nextBoundary(s, i);
    while (i < s.size() && isWordChar(s[i])) i = nextBoundary(s, i);
    return i;
}

// Run of same-class characters (word / non-word) around index i, for double click selection.
void wordAt(const std::string& s, size_t i, size_t& start, size_t& end) {
    if (s.empty()) {
        start = end = 0;
        return;
    }
    i = std::min(i, s.size() - 1);
    const bool word = isWordChar(s[i]);
    start = i;
    while (start > 0 && isWordChar(s[prevBoundary(s, start)]) == word) start = prevBoundary(s, start);
    end = i;
    while (end < s.size() && isWordChar(s[end]) == word) end = nextBoundary(s, end);
}

} // namespace

// =====================================================================================
// Themes
// =====================================================================================
Theme Theme::dark() {
    Theme t;
    t.background = Color::hex(0x1b1f2b);
    t.panel = Color::hex(0x242a3a);
    t.panelBorder = Color::hex(0x343b52);
    t.text = Color::hex(0xe7eaf3);
    t.textDim = Color::hex(0x98a2b9);
    t.textOnAccent = Color::hex(0xffffff);
    t.accent = Color::hex(0x4c8dff);
    t.accentHover = Color::hex(0x6ba0ff);
    t.accentPressed = Color::hex(0x3a74dc);
    t.widget = Color::hex(0x323951);
    t.widgetHover = Color::hex(0x3d4663);
    t.widgetPressed = Color::hex(0x293048);
    t.widgetBorder = Color::hex(0x474f6d);
    t.track = Color::hex(0x1b2030);
    t.selection = Color::hex(0x4c8dff, 0.45f);
    t.shadow = Color::hex(0x000000, 0.7f);
    t.font = {"Sans", 14.0f, false, false};
    t.headingFont = {"Sans", 18.0f, true, false};
    return t;
}

Theme Theme::light() {
    Theme t;
    t.background = Color::hex(0xeef1f7);
    t.panel = Color::hex(0xffffff);
    t.panelBorder = Color::hex(0xd8dce8);
    t.text = Color::hex(0x1e2230);
    t.textDim = Color::hex(0x687089);
    t.textOnAccent = Color::hex(0xffffff);
    t.accent = Color::hex(0x2f6df0);
    t.accentHover = Color::hex(0x4a82f5);
    t.accentPressed = Color::hex(0x2458c8);
    t.widget = Color::hex(0xe8ebf3);
    t.widgetHover = Color::hex(0xdce1ee);
    t.widgetPressed = Color::hex(0xcfd5e6);
    t.widgetBorder = Color::hex(0xc3c9db);
    t.track = Color::hex(0xf5f6fa);
    t.selection = Color::hex(0x2f6df0, 0.30f);
    t.shadow = Color::hex(0x20263a, 0.5f);
    t.font = {"Sans", 14.0f, false, false};
    t.headingFont = {"Sans", 18.0f, true, false};
    return t;
}

// =====================================================================================
// Context: setup, input, frame
// =====================================================================================
Context::Context() : theme_(Theme::dark()) {}

void Context::setClipboard(std::function<void(const std::string&)> set, std::function<std::string()> get) {
    clipboardSet_ = std::move(set);
    clipboardGet_ = std::move(get);
}

std::string Context::readClipboard() { return clipboardGet_ ? clipboardGet_() : memoryClipboard_; }

void Context::writeClipboard(const std::string& text) {
    memoryClipboard_ = text;
    if (clipboardSet_) clipboardSet_(text);
}

void Context::handleEvent(const Event& event) {
    std::visit(
        [this](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, MouseMoveEvent>) {
                input_.mouseX = e.x;
                input_.mouseY = e.y;
                input_.mods = e.mods;
                input_.moved = true;
            } else if constexpr (std::is_same_v<T, MouseLeaveEvent>) {
                input_.mouseX = input_.mouseY = -10000;
            } else if constexpr (std::is_same_v<T, MouseButtonEvent>) {
                input_.mouseX = e.x;
                input_.mouseY = e.y;
                input_.mods = e.mods;
                int idx = -1;
                if (e.button == MouseButton::Left) idx = 0;
                else if (e.button == MouseButton::Middle) idx = 1;
                else if (e.button == MouseButton::Right) idx = 2;
                if (idx >= 0) {
                    if (e.pressed) {
                        input_.down[idx] = true;
                        input_.pressed[idx] = true;
                        if (idx == 0) input_.clickCount = e.clickCount;
                    } else {
                        input_.down[idx] = false;
                        input_.released[idx] = true;
                    }
                }
            } else if constexpr (std::is_same_v<T, ScrollEvent>) {
                input_.scrollX += e.dx;
                input_.scrollY += e.dy;
                input_.mouseX = e.x;
                input_.mouseY = e.y;
            } else if constexpr (std::is_same_v<T, KeyEvent>) {
                input_.mods = e.mods;
                if (e.pressed) input_.keyboard.emplace_back(e);
            } else if constexpr (std::is_same_v<T, TextEvent>) {
                input_.keyboard.emplace_back(e);
            } else if constexpr (std::is_same_v<T, FocusEvent>) {
                if (!e.focused) {   // do not leave a button "held" when the window loses focus
                    input_.down[0] = input_.down[1] = input_.down[2] = false;
                    activeId_ = 0;
                }
            }
            // CloseEvent, RedrawEvent and ResizeEvent are for the application, not the widgets.
        },
        event);
}

void Context::beginFrame(Renderer& renderer) {
    renderer_ = &renderer;
    layouts_.clear();
    idStack_.clear();
    clipStack_.clear();
    overlays_.clear();
    prevPopupActive_ = popupOpenNow_;
    prevPopupRect_ = popupRect_;
    popupOpenNow_ = false;
    disabledDepth_ = 0;
    nextWidth_ = 0;
    hotId_ = 0;
    lastFocusable_ = 0;
    firstFocusable_ = 0;
    needsRedraw_ = false;
    cursor_ = Cursor::Arrow;
    textFocusActive_ = false;
    scrollConsumed_ = false;
    pressClaimed_ = false;
    cardHeightsNow_.clear();
    cardStack_.clear();
    cardStartY_.clear();
    scrollStack_.clear();
    menuSeenNow_ = false;
    mb_ = {};
    tipHoveredNow_ = false;
    tipShownNow_ = false;
    tipWakeMs_ = -1;
    blockOutside_ = modalOpenNow_;   // what was drawn last frame decides what is inert now
    modalOpenNow_ = false;
    insideModal_ = false;
    modalStack_.clear();
    treeDepth_ = 0;

    // Tab / Shift+Tab move focus; take them out of the keyboard queue.
    tabRequested_ = shiftTabRequested_ = false;
    for (size_t i = 0; i < input_.keyboard.size();) {
        const auto* k = std::get_if<KeyEvent>(&input_.keyboard[i]);
        if (k && k->key == Key::Tab) {
            if (k->mods.shift) shiftTabRequested_ = true;
            else tabRequested_ = true;
            input_.keyboard.erase(input_.keyboard.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    renderer.clear(theme_.background);

    Layout root;
    root.bounds = {theme_.padding, theme_.padding, renderer.width() - 2 * theme_.padding, 1e9f};
    root.x = root.bounds.x;
    root.y = root.bounds.y;
    root.startY = root.y;
    layouts_.push_back(root);
}

void Context::endFrame() {
    for (auto& draw : overlays_) draw(*renderer_);
    overlays_.clear();

    // Tab navigation that ran past the last (or before the first) widget wraps around.
    if (tabRequested_ && firstFocusable_) {
        focusId_ = firstFocusable_;
        needsRedraw_ = true;
    }
    if (shiftTabRequested_ && lastFocusable_) {
        focusId_ = lastFocusable_;
        needsRedraw_ = true;
    }
    // A click that no widget wanted removes keyboard focus.
    if (input_.pressed[0] && !pressClaimed_) focusId_ = 0;
    if (!input_.down[0]) activeId_ = 0;
    if (!popupOpenNow_) openDropdown_ = 0;
    if (!menuSeenNow_) openMenu_ = 0;
    if (!tipHoveredNow_) tipKey_ = 0;
    tipShown_ = tipShownNow_;

    // Card backgrounds use the height measured in the previous frame. If it changed, draw once more.
    for (const auto& [id, h] : cardHeightsNow_) {
        auto it = cardHeights_.find(id);
        if (it == cardHeights_.end() || std::abs(it->second - h) > 0.5f) needsRedraw_ = true;
    }
    cardHeights_ = cardHeightsNow_;

    input_.pressed[0] = input_.pressed[1] = input_.pressed[2] = false;
    input_.released[0] = input_.released[1] = input_.released[2] = false;
    input_.scrollX = input_.scrollY = 0;
    input_.moved = false;
    input_.keyboard.clear();
    renderer_ = nullptr;
}

int Context::nextWakeupMs() const {
    int result = -1;
    if (textFocusActive_) {
        constexpr int period = 530;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - blinkEpoch_).count();
        result = std::max(1, period - static_cast<int>(elapsed % period));
    }
    if (tipWakeMs_ >= 0) result = result < 0 ? std::max(tipWakeMs_, 1) : std::min(result, std::max(tipWakeMs_, 1));
    return result;
}

// =====================================================================================
// Layout
// =====================================================================================
Rect Context::allocate(float naturalWidth, float height) {
    Layout& l = layout();
    float w = nextWidth_ != 0 ? nextWidth_ : naturalWidth;
    nextWidth_ = 0;

    if (l.horizontal) {
        if (w <= 0) w = std::max(l.bounds.right() - l.x, 0.0f);   // fill the rest of the row
        const float y = height >= l.rowHeight ? l.rowTop : l.rowTop + (l.rowHeight - height) / 2.0f;
        const Rect r{l.x, y, w, height};
        l.x += w + theme_.spacing;
        l.rowExtent = std::max(l.rowExtent, height);
        lastRect_ = r;
        return r;
    }
    if (w <= 0) w = l.bounds.w;
    const Rect r{l.x, l.y, w, height};
    l.y += height + theme_.spacing;
    lastRect_ = r;
    return r;
}

void Context::beginRow(float height) {
    Layout& parent = layout();
    Layout row;
    row.horizontal = true;
    row.bounds = {parent.x, parent.y, parent.bounds.right() - parent.x, 1e9f};
    row.x = parent.x;
    row.rowTop = parent.y;
    row.rowHeight = height > 0 ? height : theme_.rowHeight;
    row.rowExtent = row.rowHeight;
    layouts_.push_back(row);
}

void Context::endRow() {
    if (layouts_.size() < 2) return;
    const Layout row = layouts_.back();
    layouts_.pop_back();
    layout().y += row.rowExtent + theme_.spacing;
}

void Context::beginColumn(float widthArg) {
    Layout& parent = layout();
    float w = nextWidth_ != 0 ? nextWidth_ : widthArg;
    nextWidth_ = 0;
    if (w <= 0) w = std::max(parent.bounds.right() - parent.x, 0.0f);

    Layout col;
    col.horizontal = false;
    col.bounds = {parent.x, parent.rowTop, w, 1e9f};
    col.x = parent.x;
    col.y = parent.rowTop;
    col.startY = col.y;
    layouts_.push_back(col);
}

void Context::endColumn() {
    if (layouts_.size() < 2) return;
    const Layout col = layouts_.back();
    layouts_.pop_back();
    Layout& parent = layout();
    const float used = std::max(col.y - col.startY - theme_.spacing, 0.0f);
    parent.rowExtent = std::max(parent.rowExtent, used);
    parent.x += col.bounds.w + theme_.spacing;
}

float Context::availableWidth() const {
    const Layout& l = layouts_.back();
    return l.horizontal ? std::max(l.bounds.right() - l.x, 0.0f) : l.bounds.w;
}

float Context::textWidth(std::string_view text) { return std::ceil(measure(text, theme_.font).width); }

void Context::space(float h) {
    Layout& l = layout();
    if (l.horizontal) l.x += h;
    else l.y += h;
}

void Context::separator() {
    const Rect r = allocate(-1, 1);
    renderer_->fillRect(r, theme_.panelBorder);
}

void Context::pushId(int id) {
    const Id base = idStack_.empty() ? 0 : idStack_.back();
    idStack_.push_back(hashString(std::string_view(reinterpret_cast<const char*>(&id), sizeof id), base));
}
void Context::popId() {
    if (!idStack_.empty()) idStack_.pop_back();
}
void Context::beginDisabled() { ++disabledDepth_; }
void Context::endDisabled() {
    if (disabledDepth_ > 0) --disabledDepth_;
}

Id Context::makeId(std::string_view label) const {
    return hashString(label, idStack_.empty() ? 0 : idStack_.back());
}

void Context::beginCard(std::string_view title) {
    Layout& parent = layout();
    const Id id = makeId(title.empty() ? std::string_view("##card") : title);
    float w = nextWidth_ != 0 ? nextWidth_ : -1;
    nextWidth_ = 0;
    if (w <= 0) w = parent.horizontal ? std::max(parent.bounds.right() - parent.x, 0.0f) : parent.bounds.w;

    const float x = parent.x;
    const float y = parent.horizontal ? parent.rowTop : parent.y;
    const float knownHeight = cardHeights_.count(id) ? cardHeights_[id] : 0.0f;

    const Rect r{x, y, w, knownHeight};
    renderer_->shadow(r, theme_.radius + 2, 10, theme_.shadow);
    renderer_->fillRoundRect(r, theme_.radius + 2, theme_.panel);
    renderer_->strokeRoundRect(r, theme_.radius + 2, theme_.panelBorder);

    Layout inner;
    inner.bounds = {x + theme_.padding, y + theme_.padding, w - 2 * theme_.padding, 1e9f};
    inner.x = inner.bounds.x;
    inner.y = inner.bounds.y;
    inner.startY = inner.y;
    layouts_.push_back(inner);
    cardStack_.push_back(id);
    cardStartY_.push_back(y);

    if (!title.empty()) label(displayText(title), LabelStyle::Heading);
}

void Context::endCard() {
    if (cardStack_.empty() || layouts_.size() < 2) return;
    const Layout inner = layouts_.back();
    layouts_.pop_back();
    const Id id = cardStack_.back();
    const float top = cardStartY_.back();
    cardStack_.pop_back();
    cardStartY_.pop_back();

    const float contentBottom = std::max(inner.y - theme_.spacing, inner.startY);
    const float height = contentBottom + theme_.padding - top;
    cardHeightsNow_[id] = height;

    Layout& parent = layout();
    if (parent.horizontal) {
        parent.rowExtent = std::max(parent.rowExtent, height);
        parent.x += inner.bounds.w + 2 * theme_.padding + theme_.spacing;
    } else {
        parent.y = top + height + theme_.spacing + 4;   // extra room for the shadow
    }
}

void Context::beginScroll(std::string_view idStr, float height) {
    const Id id = makeId(idStr);
    const Rect r = allocate(-1, height);
    ScrollState& s = scrollStates_[id];
    s.viewHeight = r.h;

    renderer_->fillRoundRect(r, theme_.radius, theme_.track);
    renderer_->strokeRoundRect(r, theme_.radius, theme_.panelBorder);

    const Rect clip = r.inset(1);
    renderer_->pushClip(clip);
    clipStack_.push_back(clipStack_.empty() ? clip : intersect(clipStack_.back(), clip));

    constexpr float pad = 8;
    constexpr float barWidth = 12;
    Layout inner;
    inner.bounds = {r.x + pad, r.y + pad - s.offset, r.w - 2 * pad - barWidth, 1e9f};
    inner.x = inner.bounds.x;
    inner.y = inner.bounds.y;
    inner.startY = inner.y;
    inner.isScrollRoot = true;
    layouts_.push_back(inner);
    scrollStack_.push_back({id, r});
}

void Context::endScroll() {
    if (scrollStack_.empty() || layouts_.size() < 2) return;
    const Layout inner = layouts_.back();
    layouts_.pop_back();
    const Id id = scrollStack_.back().id;
    const Rect view = scrollStack_.back().view;
    scrollStack_.pop_back();

    ScrollState& s = scrollStates_[id];
    clipStack_.pop_back();
    renderer_->popClip();

    const float contentHeight = std::max(inner.y - inner.startY - theme_.spacing, 0.0f) + 16;
    s.contentHeight = contentHeight;
    const float maxOffset = std::max(contentHeight - s.viewHeight, 0.0f);

    if (!scrollConsumed_ && input_.scrollY != 0 && hovered(view)) {
        s.offset += input_.scrollY * 48.0f;
        scrollConsumed_ = true;
    }

    // Scrollbar
    const Rect track{view.right() - 11, view.y + 3, 8, view.h - 6};
    if (maxOffset > 0) {
        const float thumbH = std::max(track.h * s.viewHeight / contentHeight, 24.0f);
        const float travel = track.h - thumbH;
        float thumbY = track.y + (travel > 0 ? travel * s.offset / maxOffset : 0);
        Rect thumb{track.x, thumbY, track.w, thumbH};

        const Id barId = id ^ 0x9e3779b9u;
        const bool overThumb = enabled() && hovered(thumb);
        if (overThumb && input_.pressed[0] && activeId_ == 0) {
            activeId_ = barId;
            pressClaimed_ = true;
            s.dragGrabY = input_.mouseY - thumbY;
        }
        if (activeId_ == barId && input_.down[0] && travel > 0) {
            thumbY = std::clamp(input_.mouseY - s.dragGrabY, track.y, track.y + travel);
            s.offset = (thumbY - track.y) / travel * maxOffset;
            thumb.y = thumbY;
        }
        s.offset = std::clamp(s.offset, 0.0f, maxOffset);
        thumbY = track.y + (travel > 0 ? travel * s.offset / maxOffset : 0);
        thumb.y = thumbY;
        const bool hot = overThumb || activeId_ == barId;
        renderer_->fillRoundRect(thumb, 4, hot ? theme_.textDim : theme_.widgetBorder);
    } else {
        s.offset = 0;
    }
}

// =====================================================================================
// Shared widget helpers
// =====================================================================================
bool Context::hovered(Rect r) const {
    if (!r.contains(input_.mouseX, input_.mouseY)) return false;
    if (blockOutside_ && !insideModal_) return false;   // a modal dialog owns the mouse
    if (!clipStack_.empty() && !clipStack_.back().contains(input_.mouseX, input_.mouseY)) return false;
    if (popupOpenNow_ && popupRect_.contains(input_.mouseX, input_.mouseY)) return false;
    if (prevPopupActive_ && prevPopupRect_.contains(input_.mouseX, input_.mouseY)) return false;
    return true;
}

bool Context::clickBehavior(Id id, Rect r, bool& hover, bool& held) {
    hover = enabled() && hovered(r);
    lastHovered_ = hover;
    bool clicked = false;
    if (hover && input_.pressed[0] && activeId_ == 0) {
        activeId_ = id;
        focusId_ = id;
        pressClaimed_ = true;
        resetBlink();
    }
    if (activeId_ == id && input_.released[0]) {
        clicked = hover;
        activeId_ = 0;
    }
    held = activeId_ == id && (hover || input_.down[0]);
    return clicked;
}

void Context::registerFocusable(Id id) {
    if (!enabled()) return;
    if (blockOutside_ && !insideModal_) return;         // and the keyboard focus order
    if (!firstFocusable_) firstFocusable_ = id;

    if (tabRequested_ && (focusId_ == 0 || lastFocusable_ == focusId_)) {
        focusId_ = id;
        tabRequested_ = false;
        needsRedraw_ = true;
    }
    if (shiftTabRequested_ && id == focusId_ && lastFocusable_ != 0) {
        focusId_ = lastFocusable_;
        shiftTabRequested_ = false;
        needsRedraw_ = true;
    }
    lastFocusable_ = id;
}

TextSize Context::measure(std::string_view text, const Font& font) {
    char prefix[48];
    std::snprintf(prefix, sizeof prefix, "%.1f%c%c|", font.size, font.bold ? 'b' : 'n', font.italic ? 'i' : 'n');
    std::string key = prefix;
    key.append(text);
    auto it = measureCache_.find(key);
    if (it != measureCache_.end()) return it->second;
    if (measureCache_.size() > 4096) measureCache_.clear();
    const TextSize size = renderer_->measureText(text, font);
    measureCache_.emplace(std::move(key), size);
    return size;
}

void Context::drawFocusRing(Rect r, float radius) {
    renderer_->strokeRoundRect(Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, radius + 2, theme_.accent.withAlpha(0.85f), 2);
}

void Context::drawText(Rect r, std::string_view text, Color color, bool center, const Font* font) {
    const Font& f = font ? *font : theme_.font;
    const TextSize ts = measure(text, f);
    const float s = renderer_->scale();
    const float x = center ? r.x + (r.w - ts.width) / 2.0f : r.x;
    const float y = r.y + (r.h - ts.height) / 2.0f;
    renderer_->drawText(std::round(x * s) / s, std::round(y * s) / s, text, f, color);
}

bool Context::consumeKey(Key key, bool* shift) {
    for (size_t i = 0; i < input_.keyboard.size(); ++i) {
        const auto* k = std::get_if<KeyEvent>(&input_.keyboard[i]);
        if (k && k->pressed && k->key == key) {
            if (shift) *shift = k->mods.shift;
            input_.keyboard.erase(input_.keyboard.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// =====================================================================================
// Widgets
// =====================================================================================
void Context::label(std::string_view text, LabelStyle style) {
    const std::string_view shown = displayText(text);
    const Font& font = style == LabelStyle::Heading ? theme_.headingFont : theme_.font;
    const Color color = style == LabelStyle::Dim ? theme_.textDim : theme_.text;
    const TextSize ts = measure(shown, font);
    const Rect r = allocate(std::max(ts.width, 1.0f), std::ceil(ts.height));
    const float s = renderer_->scale();
    renderer_->drawText(std::round(r.x * s) / s, std::round(r.y * s) / s, shown, font, dim(color));
}

bool Context::button(std::string_view labelText, bool primary) {
    const Id id = makeId(labelText);
    const std::string_view shown = displayText(labelText);
    const TextSize ts = measure(shown, theme_.font);
    const Rect r = allocate(std::ceil(ts.width) + 32, theme_.rowHeight);

    bool hover = false, held = false;
    bool clicked = clickBehavior(id, r, hover, held);
    registerFocusable(id);
    if (focused(id) && enabled() && (consumeKey(Key::Enter) || consumeKey(Key::Space))) clicked = true;
    if (hover) cursor_ = Cursor::Hand;

    Color fill = primary ? (held ? theme_.accentPressed : hover ? theme_.accentHover : theme_.accent)
                         : (held ? theme_.widgetPressed : hover ? theme_.widgetHover : theme_.widget);
    renderer_->fillRoundRect(r, theme_.radius, dim(fill));
    if (!primary) renderer_->strokeRoundRect(r, theme_.radius, dim(theme_.widgetBorder));
    drawText(r, shown, dim(primary ? theme_.textOnAccent : theme_.text), true);
    if (focused(id) && enabled()) drawFocusRing(r, theme_.radius);
    return clicked;
}

bool Context::checkbox(std::string_view labelText, bool& value) {
    const Id id = makeId(labelText);
    const std::string_view shown = displayText(labelText);
    constexpr float box = 18;
    const TextSize ts = measure(shown, theme_.font);
    const Rect r = allocate(box + 10 + std::ceil(ts.width), theme_.rowHeight);

    bool hover = false, held = false;
    bool toggled = clickBehavior(id, r, hover, held);
    registerFocusable(id);
    if (focused(id) && enabled() && (consumeKey(Key::Space) || consumeKey(Key::Enter))) toggled = true;
    if (toggled) value = !value;
    if (hover) cursor_ = Cursor::Hand;

    const Rect b{r.x, r.y + (r.h - box) / 2, box, box};
    if (value) {
        renderer_->fillRoundRect(b, 4, dim(held || hover ? theme_.accentHover : theme_.accent));
        const float x = b.x, y = b.y;
        renderer_->line(x + 4.5f, y + 9.5f, x + 8, y + 13, theme_.textOnAccent, 2.0f);
        renderer_->line(x + 8, y + 13, x + 13.5f, y + 5.5f, theme_.textOnAccent, 2.0f);
    } else {
        renderer_->fillRoundRect(b, 4, dim(held ? theme_.widgetPressed : hover ? theme_.widgetHover : theme_.track));
        renderer_->strokeRoundRect(b, 4, dim(theme_.widgetBorder));
    }
    drawText({r.x + box + 10, r.y, r.w - box - 10, r.h}, shown, dim(theme_.text), false);
    if (focused(id) && enabled()) drawFocusRing(b, 4);
    return toggled;
}

bool Context::radio(std::string_view labelText, int& value, int thisValue) {
    const Id id = makeId(labelText);
    const std::string_view shown = displayText(labelText);
    constexpr float d = 18;
    const TextSize ts = measure(shown, theme_.font);
    const Rect r = allocate(d + 10 + std::ceil(ts.width), theme_.rowHeight);

    bool hover = false, held = false;
    bool picked = clickBehavior(id, r, hover, held);
    registerFocusable(id);
    if (focused(id) && enabled() && (consumeKey(Key::Space) || consumeKey(Key::Enter))) picked = true;
    bool changed = false;
    if (picked && value != thisValue) {
        value = thisValue;
        changed = true;
    }
    if (hover) cursor_ = Cursor::Hand;

    const float cx = r.x + d / 2, cy = r.y + r.h / 2;
    const bool selected = value == thisValue;
    renderer_->fillCircle(cx, cy, d / 2, dim(held ? theme_.widgetPressed : hover ? theme_.widgetHover : theme_.track));
    renderer_->strokeCircle(cx, cy, d / 2, dim(selected ? theme_.accent : theme_.widgetBorder), selected ? 2.0f : 1.0f);
    if (selected) renderer_->fillCircle(cx, cy, 5, dim(theme_.accent));
    drawText({r.x + d + 10, r.y, r.w - d - 10, r.h}, shown, dim(theme_.text), false);
    if (focused(id) && enabled()) renderer_->strokeCircle(cx, cy, d / 2 + 3, theme_.accent.withAlpha(0.85f), 2);
    return changed;
}

bool Context::slider(std::string_view idStr, float& value, float minValue, float maxValue) {
    const Id id = makeId(idStr);
    const Rect r = allocate(-1, theme_.rowHeight);
    constexpr float knob = 9;
    const Rect track{r.x + knob, r.y + r.h / 2 - 3, std::max(r.w - 2 * knob, 1.0f), 6};
    const float range = maxValue - minValue;

    bool hover = enabled() && hovered(r);
    bool changed = false;
    if (hover && input_.pressed[0] && activeId_ == 0) {
        activeId_ = id;
        focusId_ = id;
        pressClaimed_ = true;
    }
    registerFocusable(id);

    const float before = value;
    if (activeId_ == id && input_.down[0] && range != 0) {
        const float t = std::clamp((input_.mouseX - track.x) / track.w, 0.0f, 1.0f);
        value = minValue + t * range;
    }
    if (focused(id) && enabled()) {
        bool shift = false;
        const float step = range * 0.01f;
        while (true) {
            if (consumeKey(Key::Left, &shift)) value -= shift ? step * 10 : step;
            else if (consumeKey(Key::Right, &shift)) value += shift ? step * 10 : step;
            else if (consumeKey(Key::Home)) value = minValue;
            else if (consumeKey(Key::End)) value = maxValue;
            else break;
        }
    }
    value = std::clamp(value, std::min(minValue, maxValue), std::max(minValue, maxValue));
    changed = value != before;
    if (hover) cursor_ = Cursor::Hand;

    const float t = range != 0 ? (value - minValue) / range : 0.0f;
    const float kx = track.x + t * track.w;
    renderer_->fillRoundRect(track, 3, dim(theme_.track));
    renderer_->strokeRoundRect(track, 3, dim(theme_.panelBorder));
    renderer_->fillRoundRect({track.x, track.y, std::max(kx - track.x, 0.0f), track.h}, 3, dim(theme_.accent));
    const bool active = activeId_ == id;
    renderer_->fillCircle(kx, r.y + r.h / 2, active ? knob : hover ? knob : knob - 1, dim(Color::hex(0xffffff)));
    renderer_->strokeCircle(kx, r.y + r.h / 2, active ? knob : hover ? knob : knob - 1,
                            dim(active ? theme_.accentPressed : theme_.accent), 2);
    if (focused(id) && enabled()) renderer_->strokeCircle(kx, r.y + r.h / 2, knob + 3, theme_.accent.withAlpha(0.85f), 2);
    return changed;
}

void Context::progressBar(float fraction, std::string_view overlay) {
    const Rect r = allocate(-1, 20);
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    renderer_->fillRoundRect(r, 10, dim(theme_.track));
    renderer_->strokeRoundRect(r, 10, dim(theme_.panelBorder));
    if (fraction > 0) {
        const Rect fill{r.x, r.y, std::max(r.w * fraction, 20.0f), r.h};
        renderer_->pushClip({r.x, r.y, r.w * fraction + 0.5f, r.h});
        renderer_->fillRoundRect(fill, 10, dim(theme_.accent));
        renderer_->popClip();
    }
    if (!overlay.empty()) {
        Font small = theme_.font;
        small.size = 12;
        small.bold = true;
        drawText(r, overlay, dim(fraction > 0.5f ? theme_.textOnAccent : theme_.text), true, &small);
    }
}

bool Context::tabBar(std::string_view idStr, const std::vector<std::string>& tabs, int& selected) {
    const Id id = makeId(idStr);
    const float height = theme_.rowHeight + 4;
    const Rect strip = allocate(-1, height);   // the tabs are placed inside this strip
    bool changed = false;

    registerFocusable(id);
    if (focused(id) && enabled()) {
        if (consumeKey(Key::Left) && selected > 0) { --selected; changed = true; }
        if (consumeKey(Key::Right) && selected + 1 < static_cast<int>(tabs.size())) { ++selected; changed = true; }
    }

    renderer_->fillRect({strip.x, strip.bottom() - 1, strip.w, 1}, theme_.panelBorder);
    float x = strip.x;
    for (size_t i = 0; i < tabs.size(); ++i) {
        const float w = std::ceil(measure(tabs[i], theme_.font).width) + 28;
        const Rect tab{x, strip.y, w, strip.h};
        const bool hover = enabled() && hovered(tab);
        if (hover) cursor_ = Cursor::Hand;
        if (hover && input_.pressed[0] && activeId_ == 0) {
            pressClaimed_ = true;
            focusId_ = id;
            if (selected != static_cast<int>(i)) {
                selected = static_cast<int>(i);
                changed = true;
            }
        }
        const bool on = selected == static_cast<int>(i);
        if (hover && !on) renderer_->fillRoundRect({tab.x + 2, tab.y + 4, tab.w - 4, tab.h - 8}, 6, dim(theme_.widgetHover.withAlpha(0.5f)));
        drawText(tab, tabs[i], dim(on ? theme_.text : theme_.textDim), true);
        if (on) renderer_->fillRoundRect({tab.x + 8, tab.bottom() - 3, tab.w - 16, 3}, 1.5f, dim(theme_.accent));
        x += w;
    }
    if (focused(id) && enabled()) drawFocusRing(strip, 2);
    return changed;
}

// ---- text field ----------------------------------------------------------------------------------
bool Context::textField(std::string_view idStr, std::string& text, std::string_view placeholder) {
    const Id id = makeId(idStr);
    const Rect r = allocate(-1, theme_.rowHeight);
    TextState& st = textStates_[id];
    const Font& font = theme_.font;

    // The application may have changed the text since last frame.
    st.cursor = std::min(st.cursor, text.size());
    st.anchor = std::min(st.anchor, text.size());
    while (st.cursor > 0 && st.cursor < text.size() && isContinuation(text[st.cursor])) --st.cursor;
    while (st.anchor > 0 && st.anchor < text.size() && isContinuation(text[st.anchor])) --st.anchor;

    const bool hover = enabled() && hovered(r);
    lastHovered_ = hover;
    if (hover) cursor_ = Cursor::IBeam;
    registerFocusable(id);

    constexpr float padX = 9;
    const Rect inner{r.x + padX, r.y, r.w - 2 * padX, r.h};
    bool changed = false;

    // ---- mouse ------------------------------------------------------------------------
    auto indexAtMouse = [&] { return renderer_->indexAtX(text, font, input_.mouseX - inner.x + st.scrollX); };
    if (hover && input_.pressed[0] && activeId_ == 0) {
        activeId_ = id;
        focusId_ = id;
        pressClaimed_ = true;
        const size_t idx = indexAtMouse();
        if (input_.clickCount >= 3) {
            st.anchor = 0;
            st.cursor = text.size();
        } else if (input_.clickCount == 2) {
            wordAt(text, idx, st.anchor, st.cursor);
        } else {
            st.cursor = idx;
            if (!input_.mods.shift) st.anchor = idx;
        }
        resetBlink();
    } else if (activeId_ == id && input_.down[0] && input_.clickCount < 2) {
        const size_t idx = indexAtMouse();   // dragging extends the selection
        if (idx != st.cursor) {
            st.cursor = idx;
            resetBlink();
        }
    }

    // ---- keyboard ---------------------------------------------------------------------------
    const bool hasFocus = enabled() && focused(id);
    if (hasFocus) {
        textFocusActive_ = true;
        auto selStart = [&] { return std::min(st.cursor, st.anchor); };
        auto selEnd = [&] { return std::max(st.cursor, st.anchor); };
        auto hasSelection = [&] { return st.cursor != st.anchor; };
        auto eraseSelection = [&] {
            if (!hasSelection()) return false;
            const size_t a = selStart(), b = selEnd();
            text.erase(a, b - a);
            st.cursor = st.anchor = a;
            changed = true;
            return true;
        };
        auto insert = [&](std::string s) {
            // Single line: newlines and control characters become spaces / are dropped.
            std::string clean;
            for (char c : s) {
                if (c == '\n' || c == '\r' || c == '\t') clean += ' ';
                else if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) clean += c;
            }
            if (clean.empty()) return;
            eraseSelection();
            text.insert(st.cursor, clean);
            st.cursor = st.anchor = st.cursor + clean.size();
            changed = true;
        };
        auto moveTo = [&](size_t pos, bool extend) {
            st.cursor = pos;
            if (!extend) st.anchor = pos;
        };

        for (const auto& item : input_.keyboard) {
            resetBlink();
            if (const auto* t = std::get_if<TextEvent>(&item)) {
                insert(t->utf8);
                continue;
            }
            const KeyEvent& k = std::get<KeyEvent>(item);
            const bool ctrl = k.mods.ctrl, shift = k.mods.shift;
            switch (k.key) {
            case Key::Left:
                if (hasSelection() && !shift && !ctrl) moveTo(selStart(), false);
                else moveTo(ctrl ? wordLeft(text, st.cursor) : prevBoundary(text, st.cursor), shift);
                break;
            case Key::Right:
                if (hasSelection() && !shift && !ctrl) moveTo(selEnd(), false);
                else moveTo(ctrl ? wordRight(text, st.cursor) : nextBoundary(text, st.cursor), shift);
                break;
            case Key::Home:
            case Key::Up: moveTo(0, shift); break;
            case Key::End:
            case Key::Down: moveTo(text.size(), shift); break;
            case Key::Backspace:
                if (!eraseSelection()) {
                    const size_t from = ctrl ? wordLeft(text, st.cursor) : prevBoundary(text, st.cursor);
                    text.erase(from, st.cursor - from);
                    st.cursor = st.anchor = from;
                    changed = true;
                }
                break;
            case Key::Delete:
                if (!eraseSelection()) {
                    const size_t to = ctrl ? wordRight(text, st.cursor) : nextBoundary(text, st.cursor);
                    text.erase(st.cursor, to - st.cursor);
                    st.anchor = st.cursor;
                    changed = true;
                }
                break;
            case Key::A:
                if (ctrl) {
                    st.anchor = 0;
                    st.cursor = text.size();
                }
                break;
            case Key::C:
                if (ctrl && hasSelection()) writeClipboard(text.substr(selStart(), selEnd() - selStart()));
                break;
            case Key::X:
                if (ctrl && hasSelection()) {
                    writeClipboard(text.substr(selStart(), selEnd() - selStart()));
                    eraseSelection();
                }
                break;
            case Key::V:
                if (ctrl) insert(readClipboard());
                break;
            case Key::Insert:
                if (shift) insert(readClipboard());
                break;
            default: break;
            }
        }
        input_.keyboard.clear();   // a focused text field owns the keyboard
    }

    // ---- scrolling so the caret stays visible --------------------------------------------------
    const float textWidth = measure(text, font).width;
    const float caret = renderer_->caretX(text, font, st.cursor);
    const float visibleW = std::max(inner.w - 2, 1.0f);
    if (caret - st.scrollX > visibleW) st.scrollX = caret - visibleW;
    if (caret - st.scrollX < 0) st.scrollX = caret;
    st.scrollX = std::clamp(st.scrollX, 0.0f, std::max(textWidth + 2 - visibleW, 0.0f));

    // ---- drawing ------------------------------------------------------------------------------
    renderer_->fillRoundRect(r, theme_.radius, dim(theme_.track));
    renderer_->strokeRoundRect(r, theme_.radius, hasFocus ? theme_.accent : dim(theme_.widgetBorder), hasFocus ? 2.0f : 1.0f);

    renderer_->pushClip(inner.inset(0));
    const float baseX = inner.x - st.scrollX;
    const TextSize ts = measure(text.empty() ? std::string_view("Ag") : std::string_view(text), font);
    const float textY = std::round((r.y + (r.h - ts.height) / 2) * renderer_->scale()) / renderer_->scale();

    if (hasFocus && st.cursor != st.anchor) {
        const float a = renderer_->caretX(text, font, std::min(st.cursor, st.anchor));
        const float b = renderer_->caretX(text, font, std::max(st.cursor, st.anchor));
        renderer_->fillRect({baseX + a, r.y + 5, b - a, r.h - 10}, theme_.selection);
    }
    if (text.empty() && !placeholder.empty()) {
        renderer_->drawText(baseX, textY, placeholder, font, dim(theme_.textDim.withAlpha(0.8f)));
    } else {
        renderer_->drawText(baseX, textY, text, font, dim(theme_.text));
    }
    if (hasFocus) {
        constexpr int period = 530;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - blinkEpoch_).count();
        if ((elapsed / period) % 2 == 0) {
            const float cx = std::round((baseX + caret) * renderer_->scale()) / renderer_->scale();
            renderer_->fillRect({cx, r.y + 6, 1.5f, r.h - 12}, theme_.text);
        }
    }
    renderer_->popClip();
    return changed;
}

// ---- list box and dropdown --------------------------------------------------------------------------
void Context::drawScrollbar(Rect track, float offset, float contentHeight, float viewHeight, bool hot) {
    if (contentHeight <= viewHeight) return;
    const float thumbH = std::max(track.h * viewHeight / contentHeight, 20.0f);
    const float travel = track.h - thumbH;
    const float maxOffset = contentHeight - viewHeight;
    const float y = track.y + (maxOffset > 0 ? travel * offset / maxOffset : 0);
    renderer_->fillRoundRect({track.x, y, track.w, thumbH}, track.w / 2, hot ? theme_.textDim : theme_.widgetBorder);
}

bool Context::listBox(std::string_view idStr, const std::vector<std::string>& items, int& selected, int visibleRows) {
    const Id id = makeId(idStr);
    constexpr float rowH = 28;
    const float height = rowH * static_cast<float>(visibleRows) + 2;
    const Rect r = allocate(-1, height);
    ListState& ls = listStates_[id];
    const int count = static_cast<int>(items.size());
    const float maxOffset = std::max(static_cast<float>(count) - static_cast<float>(visibleRows), 0.0f);

    registerFocusable(id);
    const bool hover = enabled() && hovered(r);
    bool changed = false;

    if (hover && !scrollConsumed_ && input_.scrollY != 0 && maxOffset > 0) {
        ls.offset += input_.scrollY * 1.5f;
        scrollConsumed_ = true;
    }

    if (focused(id) && enabled() && count > 0) {
        int target = selected;
        while (true) {
            if (consumeKey(Key::Up)) target = std::max(target - 1, 0);
            else if (consumeKey(Key::Down)) target = std::min(target + 1, count - 1);
            else if (consumeKey(Key::PageUp)) target = std::max(target - visibleRows, 0);
            else if (consumeKey(Key::PageDown)) target = std::min(target + visibleRows, count - 1);
            else if (consumeKey(Key::Home)) target = 0;
            else if (consumeKey(Key::End)) target = count - 1;
            else break;
        }
        if (target != selected) {
            selected = target;
            changed = true;
            // keep the selection visible
            if (static_cast<float>(selected) < ls.offset) ls.offset = static_cast<float>(selected);
            if (static_cast<float>(selected + 1) > ls.offset + static_cast<float>(visibleRows))
                ls.offset = static_cast<float>(selected + 1 - visibleRows);
        }
    }
    ls.offset = std::clamp(ls.offset, 0.0f, maxOffset);

    renderer_->fillRoundRect(r, theme_.radius, dim(theme_.track));
    renderer_->strokeRoundRect(r, theme_.radius, focused(id) && enabled() ? theme_.accent : dim(theme_.widgetBorder),
                               focused(id) && enabled() ? 2.0f : 1.0f);

    const Rect clip = r.inset(1);
    renderer_->pushClip(clip);
    clipStack_.push_back(clipStack_.empty() ? clip : intersect(clipStack_.back(), clip));
    const float scrollPx = ls.offset * rowH;
    const bool hasBar = maxOffset > 0;
    for (int i = 0; i < count; ++i) {
        const Rect row{r.x + 1, r.y + 1 + static_cast<float>(i) * rowH - scrollPx, r.w - 2 - (hasBar ? 10 : 0), rowH};
        if (row.bottom() < r.y || row.y > r.bottom()) continue;
        const bool rowHover = enabled() && hovered(row);
        if (rowHover && input_.pressed[0] && activeId_ == 0) {
            focusId_ = id;
            pressClaimed_ = true;
            if (selected != i) {
                selected = i;
                changed = true;
            }
        }
        const bool on = selected == i;
        if (on) renderer_->fillRoundRect(row.inset(2), 4, dim(theme_.accent));
        else if (rowHover) renderer_->fillRoundRect(row.inset(2), 4, dim(theme_.widgetHover));
        drawText({row.x + 10, row.y, row.w - 12, row.h}, items[static_cast<size_t>(i)],
                 dim(on ? theme_.textOnAccent : theme_.text), false);
    }
    clipStack_.pop_back();
    renderer_->popClip();
    drawScrollbar({r.right() - 9, r.y + 3, 6, r.h - 6}, scrollPx, static_cast<float>(count) * rowH, r.h - 2, hover);
    return changed;
}

bool Context::dropdown(std::string_view idStr, const std::vector<std::string>& items, int& selected) {
    const Id id = makeId(idStr);
    float widest = 80;
    for (const auto& s : items) widest = std::max(widest, std::ceil(measure(s, theme_.font).width));
    const Rect r = allocate(widest + 56, theme_.rowHeight);
    const int count = static_cast<int>(items.size());
    bool changed = false;

    bool hover = false, held = false;
    const bool clicked = clickBehavior(id, r, hover, held);
    registerFocusable(id);
    if (hover) cursor_ = Cursor::Hand;
    const bool isOpen = openDropdown_ == id;

    if (clicked) openDropdown_ = isOpen ? 0 : id;

    if (focused(id) && enabled() && count > 0) {
        if (consumeKey(Key::Up) && selected > 0) { --selected; changed = true; }
        if (consumeKey(Key::Down) && selected + 1 < count) { ++selected; changed = true; }
        if (consumeKey(Key::Enter) || consumeKey(Key::Space)) openDropdown_ = openDropdown_ == id ? 0 : id;
        if (openDropdown_ == id && consumeKey(Key::Escape)) openDropdown_ = 0;
    }

    // Header
    renderer_->fillRoundRect(r, theme_.radius, dim(held ? theme_.widgetPressed : hover ? theme_.widgetHover : theme_.widget));
    renderer_->strokeRoundRect(r, theme_.radius, dim(theme_.widgetBorder));
    const std::string current = (selected >= 0 && selected < count) ? items[static_cast<size_t>(selected)] : std::string();
    drawText({r.x + 12, r.y, r.w - 40, r.h}, current, dim(theme_.text), false);
    const float ax = r.right() - 18, ay = r.y + r.h / 2;
    renderer_->fillTriangle(ax - 5, ay - 2, ax + 5, ay - 2, ax, ay + 4, dim(theme_.textDim));
    if (focused(id) && enabled()) drawFocusRing(r, theme_.radius);

    // Popup
    if (openDropdown_ == id) {
        constexpr float rowH = 30;
        const int shown = std::min(count, 8);
        const float popupH = static_cast<float>(shown) * rowH + 8;
        float py = r.bottom() + 4;
        if (py + popupH > renderer_->height() && r.y - popupH - 4 > 0) py = r.y - popupH - 4;   // flip upward
        const Rect popup{r.x, py, std::max(r.w, widest + 24), popupH};
        popupOpenNow_ = true;
        popupRect_ = popup;

        // Input is handled right now, drawing is deferred so it ends up on top of everything.
        int hoverIndex = -1;
        for (int i = 0; i < shown; ++i) {
            const Rect row{popup.x + 4, popup.y + 4 + static_cast<float>(i) * rowH, popup.w - 8, rowH};
            if (row.contains(input_.mouseX, input_.mouseY)) hoverIndex = i;
        }
        if (input_.pressed[0] && !pressClaimed_) {
            if (hoverIndex >= 0) {
                if (selected != hoverIndex) {
                    selected = hoverIndex;
                    changed = true;
                }
                openDropdown_ = 0;
            } else if (!r.contains(input_.mouseX, input_.mouseY)) {
                openDropdown_ = 0;   // click outside closes the popup
            }
            pressClaimed_ = true;    // swallow the click
        }
        if (hoverIndex >= 0) cursor_ = Cursor::Hand;

        const Theme theme = theme_;
        const int sel = selected;
        const std::vector<std::string> copy(items.begin(), items.begin() + shown);
        overlays_.push_back([=](Renderer& rd) {
            rd.shadow(popup, theme.radius, 12, theme.shadow);
            rd.fillRoundRect(popup, theme.radius, theme.panel);
            rd.strokeRoundRect(popup, theme.radius, theme.widgetBorder);
            for (int i = 0; i < shown; ++i) {
                const Rect row{popup.x + 4, popup.y + 4 + static_cast<float>(i) * rowH, popup.w - 8, rowH};
                if (i == sel) rd.fillRoundRect(row, 4, theme.accent);
                else if (i == hoverIndex) rd.fillRoundRect(row, 4, theme.widgetHover);
                const TextSize ts = rd.measureText(copy[static_cast<size_t>(i)], theme.font);
                const float s = rd.scale();
                rd.drawText(std::round((row.x + 10) * s) / s, std::round((row.y + (row.h - ts.height) / 2) * s) / s,
                            copy[static_cast<size_t>(i)], theme.font, i == sel ? theme.textOnAccent : theme.text);
            }
        });
    }
    return changed;
}

} // namespace ww::ui
