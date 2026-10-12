#pragma once
// A native window with a CPU framebuffer, on X11 or Wayland.
//
// Typical use:
//
//   auto window = ww::Window::create({.title = "Hello"});
//   bool running = true;
//   while (running) {
//       ww::Event ev;
//       while (window->waitEvent(ev, /*timeoutMs=*/-1)) {   // block until something happens
//           if (std::holds_alternative<ww::CloseEvent>(ev)) running = false;
//           ...
//       }
//       ww::Framebuffer fb = window->acquireFramebuffer();
//       // draw into fb.pixels
//       window->present();
//   }

#include <cstdint>
#include <memory>
#include <string>

#include "ww/event.hpp"

namespace ww {

enum class Backend { Auto, X11, Wayland };

enum class Cursor { Arrow, IBeam, Hand };

struct WindowConfig {
    std::string title = "Window";
    std::string appId = "linux-windowing.app";   // Wayland app_id / X11 WM_CLASS
    int width = 800;                             // content size in logical pixels
    int height = 600;
    int minWidth = 240;
    int minHeight = 160;
};

// Pixel memory of the window's content area. Format is 0xAARRGGBB in native
// byte order (Cairo's ARGB32). Alpha must be 0xFF.
struct Framebuffer {
    uint32_t* pixels = nullptr;
    int width = 0;       // pixels
    int height = 0;      // pixels
    int stride = 0;      // bytes per row
    float scale = 1.0f;  // framebuffer pixels per logical pixel
};

class Window {
public:
    virtual ~Window() = default;

    // Opens a window on the requested backend. Backend::Auto picks Wayland when
    // $WAYLAND_DISPLAY is set and the compositor can be reached, else X11.
    // Returns nullptr (and prints the reason to stderr) on failure.
    static std::unique_ptr<Window> create(const WindowConfig& config, Backend backend = Backend::Auto);

    virtual Backend backend() const = 0;

    // Waits up to timeoutMs (-1 = forever, 0 = do not wait) for the next event.
    // Returns false if the timeout expired first.
    virtual bool waitEvent(Event& out, int timeoutMs) = 0;
    bool pollEvent(Event& out) { return waitEvent(out, 0); }

    // The framebuffer to draw the next frame into. It matches the current window
    // size; call it once per frame, after handling ResizeEvents.
    virtual Framebuffer acquireFramebuffer() = 0;
    // Shows what was drawn into the framebuffer.
    virtual void present() = 0;

    // Current content size in framebuffer pixels and the HiDPI scale.
    virtual int widthPx() const = 0;
    virtual int heightPx() const = 0;
    virtual float scale() const = 0;

    virtual void setTitle(const std::string& title) = 0;
    virtual void setCursor(Cursor cursor) = 0;

    // Text clipboard. clipboardText() waits up to ~0.5 s for the owning program
    // and returns an empty string if nothing (or no text) is available.
    virtual void setClipboardText(const std::string& utf8) = 0;
    virtual std::string clipboardText() = 0;
};

const char* backendName(Backend backend);

} // namespace ww
