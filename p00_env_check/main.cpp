// Stage 0: environment check.
//
// Prints which session you are running, then tries to open a connection to the
// X server and to the Wayland compositor. No window is created. If this program
// prints "ok" for the display system you want to target, the rest of the
// stages will run on your machine.

#include <cstdio>
#include <cstdlib>

#ifdef WW_HAVE_X11
#include <X11/Xlib.h>
#endif

#ifdef WW_HAVE_WAYLAND
#include <wayland-client.h>
#endif

static const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

int main() {
    std::printf("XDG_SESSION_TYPE = %s\n", envOr("XDG_SESSION_TYPE", "(unset)"));
    std::printf("DISPLAY          = %s\n", envOr("DISPLAY", "(unset)"));
    std::printf("WAYLAND_DISPLAY  = %s\n", envOr("WAYLAND_DISPLAY", "(unset)"));
    std::printf("\n");

#ifdef WW_HAVE_X11
    if (Display* dpy = XOpenDisplay(nullptr)) {
        std::printf("X11:     ok  (server vendor: %s, release %d, screen %dx%d)\n",
                    ServerVendor(dpy), VendorRelease(dpy),
                    DisplayWidth(dpy, DefaultScreen(dpy)),
                    DisplayHeight(dpy, DefaultScreen(dpy)));
        XCloseDisplay(dpy);
    } else {
        std::printf("X11:     could not connect (is DISPLAY set and an X server running?)\n");
    }
#else
    std::printf("X11:     support not compiled in\n");
#endif

#ifdef WW_HAVE_WAYLAND
    if (wl_display* dpy = wl_display_connect(nullptr)) {
        std::printf("Wayland: ok  (connected to the compositor)\n");
        wl_display_disconnect(dpy);
    } else {
        std::printf("Wayland: could not connect (is WAYLAND_DISPLAY set and a compositor running?)\n");
    }
#else
    std::printf("Wayland: support not compiled in\n");
#endif

    std::printf("\nCompiler: ");
#if defined(__clang__)
    std::printf("Clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("GCC %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    std::printf(", C++ standard %ld\n", __cplusplus);
    return 0;
}
