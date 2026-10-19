#include "ww/imgui_host.hpp"

#include <cfloat>
#include <chrono>

namespace ww::imgui {

ImGuiKey toImGuiKey(Key key) {
    const auto k = static_cast<unsigned>(key);
    if (k >= 'a' && k <= 'z') return static_cast<ImGuiKey>(ImGuiKey_A + (k - 'a'));
    if (k >= '0' && k <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (k - '0'));
    if (key >= Key::F1 && key <= Key::F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (static_cast<unsigned>(key) - static_cast<unsigned>(Key::F1)));
    switch (key) {
        case Key::Backspace: return ImGuiKey_Backspace;
        case Key::Tab: return ImGuiKey_Tab;
        case Key::Enter: return ImGuiKey_Enter;
        case Key::Escape: return ImGuiKey_Escape;
        case Key::Space: return ImGuiKey_Space;
        case Key::Delete: return ImGuiKey_Delete;
        case Key::Insert: return ImGuiKey_Insert;
        case Key::Home: return ImGuiKey_Home;
        case Key::End: return ImGuiKey_End;
        case Key::PageUp: return ImGuiKey_PageUp;
        case Key::PageDown: return ImGuiKey_PageDown;
        case Key::Left: return ImGuiKey_LeftArrow;
        case Key::Right: return ImGuiKey_RightArrow;
        case Key::Up: return ImGuiKey_UpArrow;
        case Key::Down: return ImGuiKey_DownArrow;
        default: return ImGuiKey_None;
    }
}

namespace {
Host* hostOf(ImGuiContext* ctx) {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    auto* host = static_cast<Host*>(ImGui::GetIO().BackendPlatformUserData);
    ImGui::SetCurrentContext(previous);
    return host;
}
} // namespace

const char* clipboardGet(ImGuiContext* ctx) {
    Host* host = hostOf(ctx);
    if (!host) return nullptr;
    host->clipboardOut_ = host->window_ ? host->window_->clipboardText() : host->clipboard;
    return host->clipboardOut_.empty() ? nullptr : host->clipboardOut_.c_str();
}

void clipboardSet(ImGuiContext* ctx, const char* text) {
    Host* host = hostOf(ctx);
    if (!host) return;
    if (host->window_) host->window_->setClipboardText(text);
    else host->clipboard = text;
}

Host::Host(Window* window) : window_(window) {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "linux-windowing";
    io.BackendPlatformUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.IniFilename = nullptr;   // do not litter the working directory; an app can set its own path
    io.LogFilename = nullptr;

    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = clipboardGet;
    pio.Platform_SetClipboardTextFn = clipboardSet;

    if (window_) setDisplay(window_->widthPx(), window_->heightPx(), window_->scale());
}

Host::~Host() {
    // ImGui asserts that backends have cleaned up after themselves before the context goes away.
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    ImGui::DestroyContext();
}

void Host::setDisplay(int widthPx, int heightPx, float scale) {
    widthPx_ = widthPx;
    heightPx_ = heightPx;
    scale_ = scale > 0.0f ? scale : 1.0f;
}

void Host::updateModifiers(const Modifiers& mods) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, mods.ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, mods.shift);
    io.AddKeyEvent(ImGuiMod_Alt, mods.alt);
    io.AddKeyEvent(ImGuiMod_Super, mods.super);
}

bool Host::handleEvent(const Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (const auto* e = std::get_if<ResizeEvent>(&event)) {
        setDisplay(e->widthPx, e->heightPx, e->scale);
        return false;
    }
    if (const auto* e = std::get_if<MouseMoveEvent>(&event)) {
        updateModifiers(e->mods);
        io.AddMousePosEvent(e->x, e->y);
        return true;
    }
    if (std::holds_alternative<MouseLeaveEvent>(event)) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // ImGui's way of saying "no mouse"
        return true;
    }
    if (const auto* e = std::get_if<MouseButtonEvent>(&event)) {
        updateModifiers(e->mods);
        io.AddMousePosEvent(e->x, e->y);
        const int button = e->button == MouseButton::Left ? 0 : e->button == MouseButton::Right ? 1
                         : e->button == MouseButton::Middle ? 2 : 3;
        io.AddMouseButtonEvent(button, e->pressed);
        return true;
    }
    if (const auto* e = std::get_if<ScrollEvent>(&event)) {
        updateModifiers(e->mods);
        io.AddMousePosEvent(e->x, e->y);
        // ww: positive dy scrolls down. ImGui: positive wheel scrolls up.
        io.AddMouseWheelEvent(-e->dx, -e->dy);
        return true;
    }
    if (const auto* e = std::get_if<KeyEvent>(&event)) {
        updateModifiers(e->mods);
        const ImGuiKey key = toImGuiKey(e->key);
        if (key != ImGuiKey_None && !(e->repeat && e->pressed)) io.AddKeyEvent(key, e->pressed);   // ImGui does its own repeat
        return true;
    }
    if (const auto* e = std::get_if<TextEvent>(&event)) {
        io.AddInputCharactersUTF8(e->utf8.c_str());
        return true;
    }
    if (const auto* e = std::get_if<FocusEvent>(&event)) {
        io.AddFocusEvent(e->focused);
        return true;
    }
    return false;
}

void Host::prepareFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(widthPx_) / scale_, static_cast<float>(heightPx_) / scale_);
    io.DisplayFramebufferScale = ImVec2(scale_, scale_);
    // Nothing else to do for HiDPI: ImGui (1.92 and later) reads DisplayFramebufferScale and bakes
    // glyphs at that density by itself, so text is sharp while the layout stays in logical units.

    if (fixedDt_ > 0.0f) {
        io.DeltaTime = fixedDt_;
    } else {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        io.DeltaTime = lastTime_ < 0.0 ? 1.0f / 60.0f : static_cast<float>(now - lastTime_);
        if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 1000.0f;
        if (io.DeltaTime > 0.25f) io.DeltaTime = 0.25f;   // a long sleep must not become one giant animation step
        lastTime_ = now;
    }
}

void Host::applyCursor() {
    if (!window_) return;
    const ImGuiMouseCursor c = ImGui::GetMouseCursor();
    if (c == lastCursor_) return;
    lastCursor_ = c;
    window_->setCursor(c == ImGuiMouseCursor_TextInput ? Cursor::IBeam
                       : c == ImGuiMouseCursor_Hand ? Cursor::Hand : Cursor::Arrow);
}

} // namespace ww::imgui
