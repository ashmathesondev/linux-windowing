#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "window_base.hpp"

namespace ww {

const char* backendName(Backend backend) {
    switch (backend) {
    case Backend::X11: return "X11";
    case Backend::Wayland: return "Wayland";
    default: return "Auto";
    }
}

std::unique_ptr<Window> Window::create(const WindowConfig& config, Backend backend) {
    // $WW_BACKEND=x11|wayland overrides Auto, handy for testing both paths.
    if (backend == Backend::Auto) {
        if (const char* forced = std::getenv("WW_BACKEND")) {
            if (std::strcmp(forced, "x11") == 0) backend = Backend::X11;
            else if (std::strcmp(forced, "wayland") == 0) backend = Backend::Wayland;
        }
    }

    const bool haveWaylandSession = std::getenv("WAYLAND_DISPLAY") != nullptr;
    std::unique_ptr<Window> window;

#ifdef WW_HAVE_WAYLAND
    if (backend == Backend::Wayland || (backend == Backend::Auto && haveWaylandSession)) {
        window = detail::createWaylandWindow(config);
        if (window || backend == Backend::Wayland) {
            if (!window) std::fprintf(stderr, "ww: could not create a Wayland window\n");
            return window;
        }
        std::fprintf(stderr, "ww: Wayland is not usable, falling back to X11\n");
    }
#else
    (void)haveWaylandSession;
#endif

#ifdef WW_HAVE_X11
    if (backend == Backend::X11 || backend == Backend::Auto) {
        window = detail::createX11Window(config);
        if (!window) std::fprintf(stderr, "ww: could not create an X11 window (is DISPLAY set?)\n");
        return window;
    }
#endif

    std::fprintf(stderr, "ww: the requested backend (%s) was not compiled in\n", backendName(backend));
    return nullptr;
}

} // namespace ww
