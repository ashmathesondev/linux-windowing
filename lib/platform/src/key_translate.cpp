#include "key_translate.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

namespace ww::detail {

namespace {

Key keyFromSym(xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_BackSpace: return Key::Backspace;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: return Key::Enter;
    case XKB_KEY_Escape: return Key::Escape;
    case XKB_KEY_space: return Key::Space;
    case XKB_KEY_Delete:
    case XKB_KEY_KP_Delete: return Key::Delete;
    case XKB_KEY_Insert:
    case XKB_KEY_KP_Insert: return Key::Insert;
    case XKB_KEY_Home:
    case XKB_KEY_KP_Home: return Key::Home;
    case XKB_KEY_End:
    case XKB_KEY_KP_End: return Key::End;
    case XKB_KEY_Prior:
    case XKB_KEY_KP_Prior: return Key::PageUp;
    case XKB_KEY_Next:
    case XKB_KEY_KP_Next: return Key::PageDown;
    case XKB_KEY_Left:
    case XKB_KEY_KP_Left: return Key::Left;
    case XKB_KEY_Right:
    case XKB_KEY_KP_Right: return Key::Right;
    case XKB_KEY_Up:
    case XKB_KEY_KP_Up: return Key::Up;
    case XKB_KEY_Down:
    case XKB_KEY_KP_Down: return Key::Down;
    default: break;
    }
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
        return static_cast<Key>(static_cast<uint16_t>(Key::F1) + (sym - XKB_KEY_F1));
    }
    const xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    if (lower >= 'a' && lower <= 'z') return static_cast<Key>(lower);
    if (sym >= '0' && sym <= '9') return static_cast<Key>(sym);
    return Key::Unknown;
}

} // namespace

Modifiers currentModifiers(xkb_state* state) {
    Modifiers m;
    if (!state) return m;
    auto active = [&](const char* name) {
        return xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    m.shift = active(XKB_MOD_NAME_SHIFT);
    m.ctrl = active(XKB_MOD_NAME_CTRL);
    m.alt = active(XKB_MOD_NAME_ALT);
    m.super = active(XKB_MOD_NAME_LOGO);
    m.capsLock = active(XKB_MOD_NAME_CAPS);
    return m;
}

KeyInfo translateKey(xkb_keymap* keymap, xkb_state* state, xkb_keycode_t code, bool wantText) {
    KeyInfo info;
    if (!keymap || !state) return info;

    info.key = keyFromSym(xkb_state_key_get_one_sym(state, code));

    // On non-Latin layouts (Russian, Greek, ...) shortcuts such as Ctrl+C should
    // still work, so fall back to what the key produces on the first layout.
    if (info.key == Key::Unknown) {
        const xkb_keysym_t* syms = nullptr;
        if (xkb_keymap_key_get_syms_by_level(keymap, code, 0, 0, &syms) > 0) info.key = keyFromSym(syms[0]);
    }

    if (wantText) {
        const Modifiers mods = currentModifiers(state);
        if (!mods.ctrl && !mods.alt && !mods.super) {
            char buf[32];
            const int n = xkb_state_key_get_utf8(state, code, buf, sizeof buf);
            if (n > 0 && static_cast<size_t>(n) < sizeof buf) {
                // Control characters (Enter "\r", Backspace "\b", Tab, Escape, Delete) are keys, not text.
                const auto first = static_cast<unsigned char>(buf[0]);
                if (first >= 0x20 && first != 0x7f) info.text.assign(buf, static_cast<size_t>(n));
            }
        }
    }
    return info;
}

} // namespace ww::detail
