#pragma once
// Drives a ui::Context without a window: renders into memory, feeds synthetic
// input and saves PNGs. Used by the tests and by the --screenshot options of the demos.

#include <functional>
#include <string>

#include "ww/renderer.hpp"
#include "ww/ui.hpp"

namespace ww::ui {

class HeadlessUi {
public:
    HeadlessUi(int logicalWidth, int logicalHeight, float scale = 1.0f) : surface_(logicalWidth, logicalHeight, scale) {}

    Context& context() { return ctx_; }

    // Draws one settled frame (see drawFrame) and returns after it is complete.
    void frame(const std::function<void(Context&)>& build) {
        Renderer renderer(surface_.framebuffer());
        drawFrame(ctx_, renderer, build, pendingInput_);
        pendingInput_ = false;
    }

    void mouseMove(float x, float y) { send(MouseMoveEvent{x, y, mods_}); }
    void mouseDown(float x, float y, int clickCount = 1) {
        send(MouseMoveEvent{x, y, mods_});
        send(MouseButtonEvent{MouseButton::Left, true, x, y, mods_, clickCount});
    }
    void mouseUp(float x, float y) { send(MouseButtonEvent{MouseButton::Left, false, x, y, mods_, 0}); }
    void scroll(float x, float y, float notches) { send(ScrollEvent{0, notches, x, y, mods_}); }

    void key(Key key, Modifiers mods = {}) {
        send(KeyEvent{key, true, false, mods});
        send(KeyEvent{key, false, false, mods});
    }
    void type(const std::string& utf8) { send(TextEvent{utf8}); }

    bool savePng(const std::string& path) {
        Renderer renderer(surface_.framebuffer());
        return renderer.savePng(path);
    }

private:
    void send(const Event& e) {
        ctx_.handleEvent(e);
        pendingInput_ = true;
    }

    OffscreenSurface surface_;
    Context ctx_;
    Modifiers mods_;
    bool pendingInput_ = false;
};

} // namespace ww::ui
