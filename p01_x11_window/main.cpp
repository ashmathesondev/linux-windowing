#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() 
{
    Display* display = XOpenDisplay(nullptr); // nullptr means use the $DISPLAY environment variable
    if (display == nullptr) {
        fprintf(stderr, "Failed to open X display\n");
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

    const char* window_title = "My X11 Window";
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

    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);

    XMapWindow(display, window);
    XFlush(display);

    bool running = true;
    while (running) 
    {
        XEvent event;
        XNextEvent(display, &event);

        switch (event.type) {
            case Expose:
                // Handle window expose event (redraw)
                break;
            case KeyPress:
                // Handle key press event
                running = false; // Exit on any key press
                break;
            case ClientMessage:
                if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteWindow) {
                    running = false; // Exit on window close
                }
                break;
            default:
                break;
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
    // TIP See CLion help at <a href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>. Also, you can try interactive lessons for CLion by selecting 'Help | Learn IDE Features' from the main menu.
}