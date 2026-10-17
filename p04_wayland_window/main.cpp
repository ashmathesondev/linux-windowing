#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>


namespace
{
    xkb_context* context = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    int32_t device_id = -1;

    bool init(Display* display)
    {
        auto connection = XGetXCBConnection(display);
        if (!connection) 
        {
            fprintf(stderr, "Failed to get XCB connection\n");
            return false;
        }

        // Enabel the XKB extension on the server and make sure it is new enough
        if (!xkb_x11_setup_xkb_extension(connection,
            ))
        context = xkb_x11_get_core_keyboard_device_id(display);
        if (!context) 
        {
            fprintf(stderr, "Failed to get XKB context\n");
            return false;
        }

        device_id = xkb_x11_get_core_keyboard_device_id(display);
        if (device_id < 0) 
        {
            fprintf(stderr, "Failed to get core keyboard device ID\n");
            return false;
        }

        keymap = xkb_x11_keymap_new_from_device(xkb_context, display, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) 
        {
            fprintf(stderr, "Failed to create XKB keymap\n");
            return false;
        }

        state = xkb_x11_state_new_from_device(keymap, display, device_id);
        if (!state) 
        {
            fprintf(stderr, "Failed to create XKB state\n");
            return false;
        }

        return true;
    }
}

int main() 
{


    return 0;
}