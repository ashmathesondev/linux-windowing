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

struct Keyboard
{
    xkb_context* context = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    int32_t device_id = -1;

    bool init (Display* display)
    {
        xcb_connection_t* connection = XGetXCBConnection(display);
        if (!connection) 
        {
            fprintf(stderr, "Failed to get XCB connection from X11 display\n");
            return false;
        }

        if (!xkb_x11_setup_xkb_extension(connection, 
            XKB_X11_MIN_MAJOR_XKB_VERSION, 
            XKB_X11_MIN_MINOR_XKB_VERSION, 
            XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, 
            nullptr, nullptr, nullptr, nullptr)) 
        {
            fprintf(stderr, "Failed to setup XKB extension\n");
            return false;
        }

        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) 
        {
            fprintf(stderr, "Failed to create XKB context\n");
            std::exit(EXIT_FAILURE);
        }

        device_id = xkb_x11_get_core_keyboard_device_id(connection);
        if (device_id < 0) 
        {
            fprintf(stderr, "Failed to get core keyboard device ID\n");
            std::exit(EXIT_FAILURE);
        }

        keymap = xkb_x11_keymap_new_from_device(context, connection, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) 
        {
            fprintf(stderr, "Failed to create XKB keymap\n");
            std::exit(EXIT_FAILURE);
        }

        state = xkb_x11_state_new_from_device(keymap, connection, device_id);
        if (!state) 
        {
            fprintf(stderr, "Failed to create XKB state\n");
            std::exit(EXIT_FAILURE);
        }

        return true;
    }

    ~Keyboard()
    {
        if (state) xkb_state_unref(state);
        if (keymap) xkb_keymap_unref(keymap);
        if (context) xkb_context_unref(context);
    }
};

std::string modsToString(unsigned int mods)
{
    std::string result;
    if (mods & ShiftMask) result += "Shift ";
    if (mods & ControlMask) result += "Control ";
    if (mods & Mod1Mask) result += "Alt ";
    if (mods & Mod4Mask) result += "Logo ";
    if (mods & LockMask) result += "NumLock ";
    if (mods & Mod2Mask) result += "CapsLock ";

    if (!result.empty()) 
    {
        return "-";
    }
    result.pop_back(); // Remove the trailing space
    return result.empty() ? "None" : result;
}

const char* notifyMode(int mode)
{
    switch (mode) 
    {
        case NotifyNormal: return "NotifyNormal";
        case NotifyGrab: return "NotifyGrab";
        case NotifyUngrab: return "NotifyUngrab";
        case NotifyWhileGrabbed: return "NotifyWhileGrabbed";
        default: return "Other";
    }
}

void logKey(Keyboard& keyboard, XKeyEvent& keyEvent, bool pressed, bool repeat)
{
    const xkb_keycode_t keycode = keyEvent.keycode;
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard.state, keycode);

    char keysymName[64] = {};
    xkb_keysym_get_name(keysym, keysymName, sizeof(keysymName));

    char utf8Buffer[16] = {};
    if (pressed) xkb_state_key_get_utf8(keyboard.state, keycode, utf8Buffer, sizeof(utf8Buffer));

    std::string text;

    for (const char* p = utf8Buffer; *p; ++p) 
    {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c == 127)
        {
            char innerbuffer[8];
            snprintf(innerbuffer, sizeof(innerbuffer), "\\x%02X", c);
            text += innerbuffer;
        }
        else
        {
            text += *p;
        }
    }

    std::printf("Key%-7s keycode=%3u sym=%-14s text=\"%s\" mods=[%s]%s\n",
                pressed ? "Press" : "Release", keycode, keysymName, text.c_str(),
                modsToString(keyEvent.state).c_str(), repeat ? " (repeat)" : "");

    if (!repeat && pressed) 
    {
        xkb_state_update_key(keyboard.state, keycode, XKB_KEY_DOWN);
    } 
    else if (!pressed) 
    {
        xkb_state_update_key(keyboard.state, keycode, XKB_KEY_UP);
    }
}

int main() 
{
    Display* display = XOpenDisplay(nullptr); // nullptr means use the $DISPLAY environment variable
    if (display == nullptr) {
        fprintf(stderr, "Failed to open X display\n");
        return EXIT_FAILURE;
    }

    Keyboard keyboard;
    if (!keyboard.init(display))
    {
        fprintf(stderr, "Failed to initialize keyboard\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    const int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    const int width = 800;
    const int height = 600;
    const unsigned long background_color = 0x2b3a55; // Dark blue color in hexadecimal
    
    Window window = XCreateSimpleWindow(display, root, 
        0, 0, width, height,                            // x, y, width, height
        1,                                              // border width
        BlackPixel(display, screen), background_color);

    const char* window_title = "Stage 2: X11 Events (see output in terminal)";
    XStoreName(display, window, window_title);

    Atom netWmName = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8String = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, window, netWmName, utf8String, 8, PropModeReplace,
        reinterpret_cast<const unsigned char*>(window_title), static_cast<int>(std::strlen(window_title)));
    
    XSizeHints hints{};
    hints.flags = PPosition | PMinSize;
    hints.x = 100;
    hints.y = 100;
    hints.width = width;
    hints.height = height;
    XSetWMNormalHints(display, window, &hints);

    // We need this, so that the window manager knows we want to handle the close event
    Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDeleteWindow, 1);

    XSelectInput(display, window, 
        StructureNotifyMask |
        ExposureMask | 
        KeyPressMask | KeyReleaseMask |
        PointerMotionMask | 
        EnterWindowMask | LeaveWindowMask |
        FocusChangeMask |
        ButtonPressMask | ButtonReleaseMask );

    XMapWindow(display, window);
    // XFlush(display);

    bool running = true;
    while (running) 
    {
        XEvent event;
        XNextEvent(display, &event);

        switch (event.type) 
        {
            case KeyRelease:
            {
                if (XPending(display)) 
                {
                    XEvent nextEvent;
                    XPeekEvent(display, &nextEvent);
                    if (nextEvent.type == KeyPress && nextEvent.xkey.time == event.xkey.time && nextEvent.xkey.keycode == event.xkey.keycode) 
                    {
                        // This is a key repeat event, ignore the release
                        XNextEvent(display, &nextEvent); // Consume the next event
                        // Handle key release event
                        logKey(keyboard, event.xkey, true, true);
                        break;
                    }
                }  
                logKey(keyboard, event.xkey, false, false); 
                break;
            }
            case KeyPress:
                logKey(keyboard, event.xkey, true, false); 
                break;
            case ButtonPress:
            case ButtonRelease:
            {
                const bool down = event.type == ButtonPress;
                const unsigned int button = event.xbutton.button;
                if (button >= 4 && button <= 7)
                {
                    if (down) 
                    {
                        std::printf("Mouse Wheel %s button=%d x=%d y=%d mods=[%s]\n",
                                    "Scroll", button, event.xbutton.x, event.xbutton.y,
                                    modsToString(event.xbutton.state).c_str());
                    }
                }
                else
                {
                    std::printf("Mouse %s button=%d x=%d y=%d mods=[%s]\n",
                                down ? "Press" : "Release",
                                button, event.xbutton.x, event.xbutton.y,
                                modsToString(event.xbutton.state).c_str());
                }
                break;
            }

            case MotionNotify:
                std::printf("Mouse Move x=%d y=%d mods=[%s]\n",
                            event.xmotion.x, event.xmotion.y,
                            modsToString(event.xmotion.state).c_str());
                break;
            case EnterNotify:
                std::printf("Mouse Enter x=%d y=%d mods=[%s] mode=%s\n",
                            event.xcrossing.x, event.xcrossing.y,
                            modsToString(event.xcrossing.state).c_str(),
                            notifyMode(event.xcrossing.mode));
                break;
            case LeaveNotify:
                std::printf("Mouse Leave x=%d y=%d mods=[%s] mode=%s\n",
                            event.xcrossing.x, event.xcrossing.y,
                            modsToString(event.xcrossing.state).c_str(),
                            notifyMode(event.xcrossing.mode));
                break;
            case FocusIn:
                std::printf("Focus In mode=%s\n", notifyMode(event.xfocus.mode));
                break;
            case FocusOut:
                std::printf("Focus Out mode=%s\n", notifyMode(event.xfocus.mode));
                break;
            case ConfigureNotify:   
                std::printf("Window Configure x=%d y=%d width=%d height=%d border_width=%d\n",
                            event.xconfigure.x, event.xconfigure.y,
                            event.xconfigure.width, event.xconfigure.height,
                            event.xconfigure.border_width);
                break;
            case MapNotify:
                std::printf("Window Map\n");
                break;
            case UnmapNotify:
                std::printf("Window Unmap\n");
                break;
            case DestroyNotify:
                std::printf("Window Destroy\n");
                break;
            case ReparentNotify:
                std::printf("Window Reparent x=%d y=%d parent=%lu\n",
                            event.xreparent.x, event.xreparent.y,
                            event.xreparent.parent);
                break;
            case ConfigureRequest:
                std::printf("Window ConfigureRequest x=%d y=%d width=%d height=%d border_width=%d\n",
                            event.xconfigurerequest.x, event.xconfigurerequest.y,
                            event.xconfigurerequest.width, event.xconfigurerequest.height,
                            event.xconfigurerequest.border_width);
                break;
            case Expose:
                // Handle window expose event (redraw)
                std::printf("Window Expose x=%d y=%d width=%d height=%d count=%d\n",
                            event.xexpose.x, event.xexpose.y,
                            event.xexpose.width, event.xexpose.height,
                            event.xexpose.count);
                break;
            case ClientMessage:
                if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteWindow) {
                    running = false; // Exit on window close
                }
                break;
            default:
                break;
        }

        std::fflush(stdout); // Ensure output is flushed to terminal
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
    // TIP See CLion help at <a href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>. Also, you can try interactive lessons for CLion by selecting 'Help | Learn IDE Features' from the main menu.
}