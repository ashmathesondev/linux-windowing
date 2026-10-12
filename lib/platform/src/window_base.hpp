#pragma once
// Internal helpers shared by the X11 and Wayland backends.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>

#include "ww/window.hpp"

namespace ww::detail {

using SteadyClock = std::chrono::steady_clock;

// Counts consecutive clicks (double click, triple click) the same way for both backends.
class ClickTracker {
public:
    int registerPress(MouseButton button, float x, float y, SteadyClock::time_point now) {
        const bool sameSpot = std::abs(x - lastX_) <= 4.0f && std::abs(y - lastY_) <= 4.0f;
        const bool soon = now - lastTime_ <= std::chrono::milliseconds(400);
        if (button == lastButton_ && sameSpot && soon && count_ < 3) {
            ++count_;
        } else {
            count_ = 1;
        }
        lastButton_ = button;
        lastX_ = x;
        lastY_ = y;
        lastTime_ = now;
        return count_;
    }

private:
    MouseButton lastButton_ = MouseButton::Other;
    float lastX_ = -100.0f;
    float lastY_ = -100.0f;
    SteadyClock::time_point lastTime_{};
    int count_ = 0;
};

// Reads WW_SCALE (for example WW_SCALE=2 or 1.5) to force a HiDPI scale for testing.
inline float scaleFromEnvironment() {
    if (const char* s = std::getenv("WW_SCALE")) {
        const float v = std::strtof(s, nullptr);
        if (v >= 0.5f && v <= 8.0f) return v;
    }
    return 0.0f;   // not set
}

class WindowBase : public Window {
public:
    // Internal API: backends and their callback functions queue events with push().
    void push(Event e) { queue_.push_back(std::move(e)); }
    bool pop(Event& out) {
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

protected:
    std::deque<Event> queue_;
    ClickTracker clicks_;
};

// Remaining milliseconds until `deadline`, clamped to >= 0.
inline int msUntil(SteadyClock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - SteadyClock::now()).count();
    return left < 0 ? 0 : static_cast<int>(left);
}

std::unique_ptr<Window> createX11Window(const WindowConfig& config);
std::unique_ptr<Window> createWaylandWindow(const WindowConfig& config);

} // namespace ww::detail
