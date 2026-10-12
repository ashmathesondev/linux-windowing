#pragma once
// Turns xkbcommon key state into the backend independent Key / text / Modifiers.
// Shared by the X11 and Wayland backends, which both use xkbcommon.

#include <xkbcommon/xkbcommon.h>

#include <string>

#include "ww/event.hpp"

namespace ww::detail {

struct KeyInfo {
    Key key = Key::Unknown;
    std::string text;   // printable text typed by this key press (empty for shortcuts and control keys)
};

Modifiers currentModifiers(xkb_state* state);

// Call for a key press *before* the key is reflected in the xkb state when you
// track state with xkb_state_update_key, or any time when the state comes from
// the compositor/server (Wayland modifiers event, XKB state notify).
KeyInfo translateKey(xkb_keymap* keymap, xkb_state* state, xkb_keycode_t code, bool wantText);

} // namespace ww::detail
