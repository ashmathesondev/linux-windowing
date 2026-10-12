#pragma once
// Backend independent input and window events.
//
// Both the X11 and the Wayland backend translate their native events into the
// types below, so application code never sees a protocol detail.
//
// Coordinates are in *logical pixels*, relative to the top left corner of the
// window's content area. Multiply by Window::scale() to get framebuffer pixels.

#include <cstdint>
#include <string>
#include <variant>

namespace ww {

// Layout independent identity of the keys an application typically reacts to.
// Letters and digits use their ASCII value, so Key::A == 'a'. Printable text
// is delivered separately as TextEvent, so use Key for shortcuts and editing.
enum class Key : uint16_t {
    Unknown = 0,
    Backspace = 8,
    Tab = 9,
    Enter = 13,
    Escape = 27,
    Space = 32,
    Digit0 = '0', Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    A = 'a', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Delete = 128, Insert, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    bool capsLock = false;
};

enum class MouseButton : uint8_t { Left, Middle, Right, Other };

struct CloseEvent {};                                   // user asked to close the window
struct RedrawEvent {};                                  // contents must be repainted
struct ResizeEvent { int widthPx; int heightPx; float scale; };   // framebuffer size in pixels
struct FocusEvent { bool focused; };
struct MouseMoveEvent { float x; float y; Modifiers mods; };
struct MouseLeaveEvent {};
struct MouseButtonEvent {
    MouseButton button;
    bool pressed;
    float x;
    float y;
    Modifiers mods;
    int clickCount;   // 1 = single, 2 = double, 3 = triple click (presses only)
};
struct ScrollEvent {
    float dx;         // wheel notches; positive = right
    float dy;         // wheel notches; positive = down (content moves up)
    float x;
    float y;
    Modifiers mods;
};
struct KeyEvent {
    Key key;
    bool pressed;
    bool repeat;
    Modifiers mods;
};
struct TextEvent { std::string utf8; };                 // typed text, already layout/compose aware

using Event = std::variant<CloseEvent, RedrawEvent, ResizeEvent, FocusEvent, MouseMoveEvent,
                           MouseLeaveEvent, MouseButtonEvent, ScrollEvent, KeyEvent, TextEvent>;

} // namespace ww
