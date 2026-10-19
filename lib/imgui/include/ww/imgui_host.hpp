#pragma once
// The platform half of a Dear ImGui backend.
//
// Dear ImGui does not know about windows, keyboards or clipboards. A "backend" has two halves:
//   * the platform half (this file) feeds ww::Event input and the window size into ImGuiIO,
//     and carries the cursor shape and clipboard text back out;
//   * the renderer half (imgui_soft.hpp or imgui_gl.hpp) turns ImDrawData into pixels.
// The halves are independent, so the same Host drives both the CPU and the OpenGL renderer.

#include <string>

#include "imgui.h"
#include "ww/event.hpp"
#include "ww/window.hpp"

namespace ww::imgui {

class Host {
public:
    // Creates the ImGui context. With a window, the cursor shape and the clipboard are wired to it;
    // with nullptr (tests, screenshots) the clipboard is a plain string and the size comes from setDisplay().
    explicit Host(Window* window = nullptr);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Size of the framebuffer in pixels and pixels per logical unit. ImGui works in logical units.
    void setDisplay(int widthPx, int heightPx, float scale);

    // Translates one window event into ImGui input. Returns true if it was input
    // (so the caller knows a new frame is worth drawing).
    bool handleEvent(const Event& event);

    // Call once per frame before ImGui::NewFrame(): size, scale, delta time.
    // Then call ImGui::NewFrame() yourself (it is the line every ImGui program has).
    void prepareFrame();

    // Call after ImGui::EndFrame()/Render(): tells the window which cursor ImGui wants.
    void applyCursor();

    // Programmatic time step for tests; the default is the real clock.
    void setFixedDeltaTime(float seconds) { fixedDt_ = seconds; }

    std::string clipboard;   // used when there is no window

private:
    void updateModifiers(const Modifiers& mods);

    Window* window_;
    ImGuiContext* context_ = nullptr;
    int widthPx_ = 800, heightPx_ = 600;
    float scale_ = 1.0f;
    double lastTime_ = -1.0;
    float fixedDt_ = 0.0f;
    ImGuiMouseCursor lastCursor_ = ImGuiMouseCursor_COUNT;
    std::string clipboardOut_;   // keeps the pointer returned to ImGui alive
    friend const char* clipboardGet(ImGuiContext*);
    friend void clipboardSet(ImGuiContext*, const char*);
};

// ww::Key -> ImGuiKey (ImGuiKey_None when there is no equivalent).
ImGuiKey toImGuiKey(Key key);

} // namespace ww::imgui
