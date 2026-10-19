// Stage 10 controls: menus, tooltips, modal dialogs, tree view, spin box, status bar.
//
// They are members of the same Context as the stage 8 widgets and use the same
// machinery (ids, layout stack, deferred overlays), only the pieces that are new
// live here. Read ui.cpp first; this file assumes you know how dropdown() works.

#include <algorithm>
#include <cmath>

#include "ww/ui.hpp"

namespace ww::ui {

namespace {

std::string_view shownText(std::string_view label) {
    const size_t pos = label.find("##");
    return pos == std::string_view::npos ? label : label.substr(0, pos);
}

constexpr float kMenuRow = 28;
constexpr float kMenuSeparator = 9;
constexpr float kMenuGutter = 26;   // room for the check mark

} // namespace

// =====================================================================================
// Menus
// =====================================================================================
//
// Only one menu is open at a time (openMenu_). While it is open, its popup is
// "built" by the menuItem calls between beginMenu and endMenu: each call takes
// the next row, reacts to the mouse, and queues its drawing as an overlay so the
// popup ends up on top of the rest of the UI. The popup's size is only known
// after the last item, so (like card heights) it is remembered from the
// previous frame and a redraw is requested when it changed.

bool Context::beginMenuBar() {
    const Rect r = allocate(-1, theme_.rowHeight);
    renderer_->fillRoundRect(r, theme_.radius, dim(theme_.panel));
    renderer_->strokeRoundRect(r, theme_.radius, dim(theme_.panelBorder));
    menuBar_.rect = r;
    menuBar_.x = r.x + 4;
    barMenusNow_.clear();
    return true;
}

void Context::endMenuBar() {
    // Left and Right move between the headers while a bar menu is open.
    if (openMenu_ != 0 && openMenuIsBar_ && barMenusNow_.size() > 1) {
        const auto it = std::find(barMenusNow_.begin(), barMenusNow_.end(), openMenu_);
        if (it != barMenusNow_.end()) {
            const int count = static_cast<int>(barMenusNow_.size());
            const int index = static_cast<int>(it - barMenusNow_.begin());
            int next = index;
            if (consumeKey(Key::Left)) next = (index + count - 1) % count;
            else if (consumeKey(Key::Right)) next = (index + 1) % count;
            if (next != index) {
                openMenu_ = barMenusNow_[static_cast<size_t>(next)];
                menuHighlight_ = -1;
                needsRedraw_ = true;
            }
        }
    }
}

bool Context::beginMenu(std::string_view label) {
    const Id id = makeId(label);
    const std::string_view shown = shownText(label);
    const float w = std::ceil(measure(shown, theme_.font).width) + 22;
    const Rect header{menuBar_.x, menuBar_.rect.y + 3, w, menuBar_.rect.h - 6};
    menuBar_.x += w + 2;
    barMenusNow_.push_back(id);
    lastRect_ = header;

    const bool hover = enabled() && hovered(header);
    if (hover && input_.pressed[0] && activeId_ == 0) {
        // Click: open, or close when it was already open.
        openMenu_ = openMenu_ == id ? 0 : id;
        openMenuIsBar_ = true;
        menuHighlight_ = -1;
        focusId_ = 0;
        pressClaimed_ = true;
    } else if (hover && input_.moved && openMenu_ != 0 && openMenu_ != id && openMenuIsBar_) {
        // Like every desktop: once one menu is open, moving onto another header switches to it.
        // (Only when the pointer moved: a resting pointer must not undo the arrow keys.)
        openMenu_ = id;
        menuHighlight_ = -1;
    }

    const bool open = openMenu_ == id;
    if (open) renderer_->fillRoundRect(header, theme_.radius - 1, dim(theme_.widgetPressed));
    else if (hover) renderer_->fillRoundRect(header, theme_.radius - 1, dim(theme_.widgetHover));
    drawText(header, shown, dim(theme_.text), true);
    if (!open) return false;

    menuSeenNow_ = true;
    beginMenuPopup(id, header.x, header.bottom() + 2);
    return true;
}

void Context::endMenu() { endMenuPopup(); }

bool Context::beginContextMenu(std::string_view idStr) {
    const Id id = makeId(idStr) ^ 0xC0DEC0DEu;
    if (enabled() && hovered(lastRect_) && input_.pressed[2]) {
        openMenu_ = id;
        openMenuIsBar_ = false;
        ctxX_ = input_.mouseX;
        ctxY_ = input_.mouseY;
        menuHighlight_ = -1;
        focusId_ = 0;
        pressClaimed_ = true;
    }
    if (openMenu_ != id) return false;
    menuSeenNow_ = true;
    beginMenuPopup(id, ctxX_, ctxY_);
    return true;
}

void Context::endContextMenu() { endMenuPopup(); }

void Context::beginMenuPopup(Id id, float x, float y) {
    const auto it = menuSizes_.find(id);
    const float w = it != menuSizes_.end() ? it->second.first : 160.0f;
    const float h = it != menuSizes_.end() ? it->second.second : 0.0f;

    // Keep the popup on screen.
    x = std::max(4.0f, std::min(x, renderer_->width() - w - 4));
    if (y + h > renderer_->height() - 4) y = std::max(4.0f, renderer_->height() - 4 - h);

    mb_ = {};
    mb_.active = true;
    mb_.id = id;
    mb_.popup = {x, y, w, h};
    mb_.y = y + 4;
    popupOpenNow_ = true;       // widgets below the popup must not react to the mouse
    popupRect_ = mb_.popup;

    const Rect popup = mb_.popup;
    const Theme theme = theme_;
    overlays_.push_back([=](Renderer& rd) {
        rd.shadow(popup, theme.radius, 12, theme.shadow);
        rd.fillRoundRect(popup, theme.radius, theme.panel);
        rd.strokeRoundRect(popup, theme.radius, theme.widgetBorder);
    });
}

bool Context::menuItem(std::string_view label, std::string_view shortcutText, bool* checked, bool isEnabled) {
    if (!mb_.active) return false;
    const std::string_view shown = shownText(label);
    const int index = mb_.index++;

    const float labelW = std::ceil(measure(shown, theme_.font).width);
    const float shortcutW = shortcutText.empty() ? 0.0f : std::ceil(measure(shortcutText, theme_.font).width) + 32;
    mb_.widest = std::max(mb_.widest, kMenuGutter + labelW + shortcutW + 20 + 8);

    const Rect row{mb_.popup.x + 4, mb_.y, mb_.popup.w - 8, kMenuRow};
    mb_.y += kMenuRow;
    lastRect_ = row;

    const bool usable = isEnabled && enabled();
    const bool hover = row.contains(input_.mouseX, input_.mouseY);
    if (hover) menuHighlight_ = index;

    bool activated = false;
    if (hover && input_.pressed[0]) {
        activated = usable;
        pressClaimed_ = true;
    }
    if (index == menuHighlight_ && usable && consumeKey(Key::Enter)) activated = true;
    if (activated) {
        if (checked) *checked = !*checked;
        openMenu_ = 0;   // choosing an item closes the whole menu
    }

    const bool on = checked && *checked;
    const bool hot = index == menuHighlight_ && usable;
    const Theme theme = theme_;
    const std::string text(shown), accel(shortcutText);
    overlays_.push_back([=](Renderer& rd) {
        const float s = rd.scale();
        const Color textColor = usable ? (hot ? theme.textOnAccent : theme.text) : theme.textDim.withAlpha(0.6f);
        if (hot) rd.fillRoundRect(row, 4, theme.accent);
        if (on) {
            const float x = row.x + 7, y = row.y + (row.h - 14) / 2;
            rd.line(x + 1, y + 7, x + 5, y + 11, textColor, 2.0f);
            rd.line(x + 5, y + 11, x + 12, y + 3, textColor, 2.0f);
        }
        const TextSize ts = rd.measureText(text, theme.font);
        rd.drawText(std::round((row.x + kMenuGutter) * s) / s, std::round((row.y + (row.h - ts.height) / 2) * s) / s,
                    text, theme.font, textColor);
        if (!accel.empty()) {
            const TextSize as = rd.measureText(accel, theme.font);
            rd.drawText(std::round((row.right() - 12 - as.width) * s) / s,
                        std::round((row.y + (row.h - as.height) / 2) * s) / s, accel, theme.font,
                        hot ? theme.textOnAccent : theme.textDim);
        }
    });
    return activated;
}

void Context::menuSeparator() {
    if (!mb_.active) return;
    const Rect line{mb_.popup.x + 10, mb_.y + kMenuSeparator / 2, mb_.popup.w - 20, 1};
    mb_.y += kMenuSeparator;
    const Color c = theme_.panelBorder;
    overlays_.push_back([=](Renderer& rd) { rd.fillRect(line, c); });
}

void Context::endMenuPopup() {
    if (!mb_.active) return;

    // Up and Down move the highlight, wrapping around.
    const int count = mb_.index;
    if (count > 0 && openMenu_ == mb_.id) {
        if (consumeKey(Key::Down)) menuHighlight_ = (menuHighlight_ + 1) % count;
        else if (consumeKey(Key::Up)) menuHighlight_ = menuHighlight_ <= 0 ? count - 1 : menuHighlight_ - 1;
    }
    if (openMenu_ == mb_.id && consumeKey(Key::Escape)) openMenu_ = 0;

    // A click anywhere else closes the menu and is swallowed (it does not also press a widget).
    if (openMenu_ == mb_.id && (input_.pressed[0] || input_.pressed[2]) && !pressClaimed_ &&
        !mb_.popup.contains(input_.mouseX, input_.mouseY)) {
        openMenu_ = 0;
        pressClaimed_ = true;
    }

    const float w = std::max(mb_.widest, 150.0f);
    const float h = (mb_.y - mb_.popup.y) + 4;
    auto& stored = menuSizes_[mb_.id];
    if (std::abs(stored.first - w) > 0.5f || std::abs(stored.second - h) > 0.5f) needsRedraw_ = true;
    stored = {w, h};
    mb_.active = false;
}

bool Context::shortcut(Key key, bool ctrl, bool shift, bool alt) {
    for (size_t i = 0; i < input_.keyboard.size(); ++i) {
        const auto* k = std::get_if<KeyEvent>(&input_.keyboard[i]);
        if (k && k->pressed && k->key == key && k->mods.ctrl == ctrl && k->mods.shift == shift && k->mods.alt == alt) {
            input_.keyboard.erase(input_.keyboard.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// =====================================================================================
// Tooltips
// =====================================================================================
void Context::tooltip(std::string_view text) {
    if (!enabled() || input_.down[0] || !hovered(lastRect_)) return;

    // The rectangle of the widget identifies it from frame to frame.
    const float key[4] = {lastRect_.x, lastRect_.y, lastRect_.w, lastRect_.h};
    Id id = 2166136261u;
    for (unsigned char c : std::string_view(reinterpret_cast<const char*>(key), sizeof key)) id = (id ^ c) * 16777619u;
    id = id ? id : 1;

    tipHoveredNow_ = true;
    if (id != tipKey_) {
        tipKey_ = id;
        tipSince_ = Clock::now();
    }
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - tipSince_).count();
    if (waited < tipDelayMs_) {
        // Ask the window loop to wake up when the tooltip is due, otherwise it would sleep through it.
        const int remaining = static_cast<int>(tipDelayMs_ - waited);
        tipWakeMs_ = tipWakeMs_ < 0 ? remaining : std::min(tipWakeMs_, remaining);
        return;
    }
    tipShownNow_ = true;

    Font small = theme_.font;
    small.size = 13;
    const TextSize ts = measure(text, small);
    const float w = std::ceil(ts.width) + 20, h = std::ceil(ts.height) + 12;
    float x = input_.mouseX + 14, y = input_.mouseY + 22;
    if (x + w > renderer_->width() - 4) x = renderer_->width() - 4 - w;
    if (y + h > renderer_->height() - 4) y = input_.mouseY - h - 8;
    const Rect box{std::max(x, 4.0f), std::max(y, 4.0f), w, h};

    const Theme theme = theme_;
    const std::string copy(text);
    overlays_.push_back([=](Renderer& rd) {
        rd.shadow(box, 5, 8, theme.shadow);
        rd.fillRoundRect(box, 5, theme.panel);
        rd.strokeRoundRect(box, 5, theme.widgetBorder);
        const TextSize t = rd.measureText(copy, small);
        const float s = rd.scale();
        rd.drawText(std::round((box.x + 10) * s) / s, std::round((box.y + (box.h - t.height) / 2) * s) / s, copy, small,
                    theme.text);
    });
}

// =====================================================================================
// Modal dialogs
// =====================================================================================
//
// The dialog's contents are ordinary widget calls, made after the rest of the UI so
// they are drawn on top. Two things make it modal:
//   * the scrim: a translucent rectangle over the whole window, drawn first;
//   * blockOutside_: from the next frame on, hovered() and registerFocusable() refuse
//     every widget that is not drawn between beginModal and endModal.
// Like cards, the dialog's height is measured one frame late and corrected by a redraw.

bool Context::beginModal(std::string_view title, bool& open, float width) {
    if (!open) return false;
    if (consumeKey(Key::Escape)) {
        open = false;
        return false;
    }
    const Id id = makeId(title);
    if (!blockOutside_) focusId_ = 0;   // the dialog just appeared: nothing behind it keeps the keyboard
    modalOpenNow_ = true;

    const float W = renderer_->width(), H = renderer_->height();
    renderer_->fillRect({0, 0, W, H}, Color::hex(0x000000, 0.45f));

    const float w = std::min(width, W - 32);
    const float knownHeight = modalHeights_.count(id) ? modalHeights_[id] : 0.0f;
    const Rect r{(W - w) / 2, std::max((H - knownHeight) / 2, 16.0f), w, knownHeight};
    renderer_->shadow(r, theme_.radius + 4, 18, theme_.shadow);
    renderer_->fillRoundRect(r, theme_.radius + 4, theme_.panel);
    renderer_->strokeRoundRect(r, theme_.radius + 4, theme_.widgetBorder);

    Layout inner;
    inner.bounds = {r.x + theme_.padding, r.y + theme_.padding, w - 2 * theme_.padding, 1e9f};
    inner.x = inner.bounds.x;
    inner.y = inner.bounds.y;
    inner.startY = inner.y;
    layouts_.push_back(inner);
    modalStack_.push_back({id, r.y});
    insideModal_ = true;
    nextWidth_ = 0;

    label(shownText(title), LabelStyle::Heading);
    return true;
}

void Context::endModal() {
    if (modalStack_.empty() || layouts_.size() < 2) return;
    const Layout inner = layouts_.back();
    layouts_.pop_back();
    const ModalFrame frame = modalStack_.back();
    modalStack_.pop_back();

    const float contentBottom = std::max(inner.y - theme_.spacing, inner.startY);
    const float height = contentBottom + theme_.padding - frame.top;
    auto it = modalHeights_.find(frame.id);
    if (it == modalHeights_.end() || std::abs(it->second - height) > 0.5f) needsRedraw_ = true;
    modalHeights_[frame.id] = height;
    insideModal_ = !modalStack_.empty();
}

int Context::messageBox(std::string_view title, std::string_view message, const std::vector<std::string>& buttons,
                        bool& open) {
    if (!beginModal(title, open)) return -1;
    int result = -1;

    // One label per line: labels do not wrap.
    size_t start = 0;
    while (start <= message.size()) {
        const size_t end = message.find('\n', start);
        label(message.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    space(8);

    // Right aligned button row.
    float total = 0;
    for (const auto& b : buttons) total += textWidth(b) + 32 + theme_.spacing;
    beginRow();
    space(std::max(availableWidth() - total + theme_.spacing, 0.0f));
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (button(buttons[i], i == 0)) {
            result = static_cast<int>(i);
            open = false;
        }
    }
    endRow();
    endModal();
    return result;
}

// =====================================================================================
// Tree view
// =====================================================================================
//
// treeNode draws one row and says whether the node is open. If it is, the caller draws
// the children and calls treePop(); the indent and the id scope (so equal names under
// different parents stay distinct) are pushed here and popped there. The open/closed
// flag of every node is the one piece of tree state the Context keeps.

Context::TreeResult Context::treeRow(std::string_view label, bool selected, bool leaf, bool defaultOpen) {
    constexpr float rowH = 26, arrowW = 20, indent = 18;
    const Id id = makeId(label);
    const std::string_view shown = shownText(label);
    const Rect r = allocate(-1, rowH);
    bool& open = treeOpen_.try_emplace(id, defaultOpen).first->second;

    TreeResult result;
    const bool hover = enabled() && hovered(r);
    lastHovered_ = hover;
    if (hover && input_.pressed[0] && activeId_ == 0) {
        pressClaimed_ = true;
        focusId_ = 0;
        if (!leaf && input_.mouseX < r.x + arrowW) {
            open = !open;                               // the arrow toggles
        } else {
            result.clicked = true;                      // the label selects
            if (!leaf && input_.clickCount == 2) open = !open;   // double click toggles too
        }
    }

    if (selected) renderer_->fillRoundRect(r.inset(1), 4, dim(theme_.accent));
    else if (hover) renderer_->fillRoundRect(r.inset(1), 4, dim(theme_.widgetHover));
    const Color ink = selected ? theme_.textOnAccent : theme_.text;
    const Color arrowInk = selected ? theme_.textOnAccent : theme_.textDim;
    if (!leaf) {
        const float cx = r.x + arrowW / 2, cy = r.y + r.h / 2;
        if (open) renderer_->fillTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3, dim(arrowInk));
        else renderer_->fillTriangle(cx - 2, cy - 4, cx - 2, cy + 4, cx + 3, cy, dim(arrowInk));
    }
    drawText({r.x + arrowW, r.y, r.w - arrowW, r.h}, shown, dim(ink), false);

    result.open = !leaf && open;
    if (result.open) {
        Layout& l = layout();
        l.x += indent;
        l.bounds.w -= indent;
        idStack_.push_back(id);
        ++treeDepth_;
    }
    return result;
}

Context::TreeResult Context::treeNode(std::string_view label, bool selected, bool defaultOpen) {
    return treeRow(label, selected, false, defaultOpen);
}

bool Context::treeLeaf(std::string_view label, bool selected) { return treeRow(label, selected, true, false).clicked; }

void Context::treePop() {
    if (treeDepth_ == 0) return;
    Layout& l = layout();
    l.x -= 18;
    l.bounds.w += 18;
    if (!idStack_.empty()) idStack_.pop_back();
    --treeDepth_;
}

// =====================================================================================
// Spin box and status bar
// =====================================================================================
bool Context::spinBox(std::string_view idStr, int& value, int minValue, int maxValue, int step) {
    const Id id = makeId(idStr);
    const Rect r = allocate(110, theme_.rowHeight);
    constexpr float buttonW = 22;
    const Rect up{r.right() - buttonW - 2, r.y + 2, buttonW, r.h / 2 - 2};
    const Rect down{r.right() - buttonW - 2, r.y + r.h / 2, buttonW, r.h / 2 - 2};
    const int before = value;

    registerFocusable(id);
    const bool hover = enabled() && hovered(r);
    const bool overUp = enabled() && hovered(up), overDown = enabled() && hovered(down);
    if (hover && input_.pressed[0] && activeId_ == 0) {
        focusId_ = id;
        pressClaimed_ = true;
        if (overUp) value += step;
        else if (overDown) value -= step;
    }
    if (hover && !scrollConsumed_ && input_.scrollY != 0) {
        value += input_.scrollY < 0 ? step : -step;   // wheel up counts up
        scrollConsumed_ = true;
    }
    if (focused(id) && enabled()) {
        while (true) {
            if (consumeKey(Key::Up)) value += step;
            else if (consumeKey(Key::Down)) value -= step;
            else if (consumeKey(Key::PageUp)) value += step * 10;
            else if (consumeKey(Key::PageDown)) value -= step * 10;
            else if (consumeKey(Key::Home)) value = minValue;
            else if (consumeKey(Key::End)) value = maxValue;
            else break;
        }
    }
    value = std::clamp(value, std::min(minValue, maxValue), std::max(minValue, maxValue));

    const bool hasFocus = focused(id) && enabled();
    renderer_->fillRoundRect(r, theme_.radius, dim(theme_.track));
    renderer_->strokeRoundRect(r, theme_.radius, hasFocus ? theme_.accent : dim(theme_.widgetBorder), hasFocus ? 2.0f : 1.0f);
    drawText({r.x + 10, r.y, r.w - buttonW - 14, r.h}, std::to_string(value), dim(theme_.text), false);

    auto drawButton = [&](Rect b, bool over, bool pointsUp) {
        const bool pressed = over && input_.down[0];
        renderer_->fillRoundRect(b, 3, dim(pressed ? theme_.widgetPressed : over ? theme_.widgetHover : theme_.widget));
        const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        if (pointsUp) renderer_->fillTriangle(cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 3, dim(theme_.textDim));
        else renderer_->fillTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3, dim(theme_.textDim));
    };
    drawButton(up, overUp, true);
    drawButton(down, overDown, false);
    if (hover) cursor_ = Cursor::Arrow;
    lastRect_ = r;
    return value != before;
}

void Context::statusBar(const std::vector<std::string>& panes) {
    constexpr float h = 26;
    const float W = renderer_->width(), H = renderer_->height();
    const Rect bar{0, H - h, W, h};
    renderer_->fillRect(bar, theme_.panel);
    renderer_->fillRect({0, bar.y, W, 1}, theme_.panelBorder);

    float x = theme_.padding;
    for (size_t i = 0; i < panes.size(); ++i) {
        const float w = std::ceil(measure(panes[i], theme_.font).width);
        drawText({x, bar.y, w, h}, panes[i], i == 0 ? theme_.text : theme_.textDim, false);
        x += w;
        if (i + 1 < panes.size()) {
            x += 12;
            renderer_->fillRect({x, bar.y + 6, 1, h - 12}, theme_.panelBorder);
            x += 13;
        }
    }
    lastRect_ = bar;
}

} // namespace ww::ui
