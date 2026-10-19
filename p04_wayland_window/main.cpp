// Stage 4: the smallest useful Wayland window.
//
// How Wayland differs from X11:
//   * There is no "draw into the window" call. A window is a wl_surface. You
//     create a buffer (here: shared memory), draw into it, attach it to the
//     surface and commit. What you committed is what the compositor shows.
//   * wl_surface only knows about pixels. To get a desktop window (title,
//     resize, close) the surface is given a *role* through the xdg-shell
//     protocol: wl_surface -> xdg_surface -> xdg_toplevel.
//   * Everything is an object with a numeric id on the connection. You learn what
//     the compositor supports from the registry ("globals") and bind what you need.
//   * The compositor *tells* you the window size (xdg_toplevel.configure). You
//     answer with ack_configure and then commit a buffer of a fitting size.
//
// The window shows a solid colour. Resize it and the buffer is re-created.
//
// Usage: ./p04_wayland_window [--display NAME]
// Without --display the compositor is found through $WAYLAND_DISPLAY, then by looking
// for wayland-N sockets in $XDG_RUNTIME_DIR (see connectToCompositor below).

#include <dirent.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "xdg-shell-client-protocol.h"

namespace {

// ---------------------------------------------------------------------------
// A buffer backed by its own shared memory. It destroys itself when the
// compositor says it is done with it (wl_buffer.release). This "one-shot
// buffer" pattern is the simplest correct approach when content rarely changes.
// ---------------------------------------------------------------------------
struct ShmBuffer {
    wl_buffer* buffer = nullptr;
    void* data = nullptr;
    size_t size = 0;
};

void onBufferRelease(void* userData, wl_buffer*) {
    auto* b = static_cast<ShmBuffer*>(userData);
    wl_buffer_destroy(b->buffer);
    munmap(b->data, b->size);
    delete b;
}
const wl_buffer_listener kBufferListener = {onBufferRelease};

ShmBuffer* createShmBuffer(wl_shm* shm, int width, int height, uint32_t argbColor) {
    const int stride = width * 4;
    const size_t size = static_cast<size_t>(stride) * height;

    // memfd_create gives an anonymous file we can share with the compositor.
    int fd = memfd_create("p04-buffer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) < 0) {
        std::perror("memfd_create/ftruncate");
        std::exit(EXIT_FAILURE);
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::perror("mmap");
        std::exit(EXIT_FAILURE);
    }

    // The pool is the shared memory; a buffer is a rectangle inside the pool.
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
    wl_buffer* wlBuffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);   // the buffer keeps the memory alive
    close(fd);                   // the compositor has its own copy of the fd

    auto* b = new ShmBuffer{wlBuffer, data, size};
    wl_buffer_add_listener(wlBuffer, &kBufferListener, b);

    std::fill_n(static_cast<uint32_t*>(data), static_cast<size_t>(width) * height, argbColor);
    return b;
}

// ---------------------------------------------------------------------------
struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wmBase = nullptr;

    wl_surface* surface = nullptr;
    xdg_surface* xdgSurface = nullptr;
    xdg_toplevel* toplevel = nullptr;

    int width = 640;
    int height = 400;
    int pendingWidth = 0;    // size suggested by the latest xdg_toplevel.configure
    int pendingHeight = 0;
    bool running = true;
};

void draw(App& app) {
    ShmBuffer* b = createShmBuffer(app.shm, app.width, app.height, 0x2b3a55);
    wl_surface_attach(app.surface, b->buffer, 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
    wl_surface_commit(app.surface);
}

// ---- xdg_wm_base: the compositor pings us to check that we are alive --------
void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmBaseListener = {onWmPing};

// ---- xdg_surface: configure is the "you may draw now" handshake -------------
void onXdgSurfaceConfigure(void* userData, xdg_surface* surf, uint32_t serial) {
    auto* app = static_cast<App*>(userData);
    xdg_surface_ack_configure(surf, serial);   // must happen before the next commit

    if (app->pendingWidth > 0 && app->pendingHeight > 0) {
        app->width = app->pendingWidth;
        app->height = app->pendingHeight;
    }
    draw(*app);
}
const xdg_surface_listener kXdgSurfaceListener = {onXdgSurfaceConfigure};

// ---- xdg_toplevel: size suggestions and close requests ----------------------
void onToplevelConfigure(void* userData, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    // 0x0 means "you choose". The new size only takes effect after the
    // xdg_surface.configure that follows this event.
    auto* app = static_cast<App*>(userData);
    app->pendingWidth = w;
    app->pendingHeight = h;
}
void onToplevelClose(void* userData, xdg_toplevel*) { static_cast<App*>(userData)->running = false; }
void onToplevelConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void onToplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}
const xdg_toplevel_listener kToplevelListener = {onToplevelConfigure, onToplevelClose,
                                                 onToplevelConfigureBounds, onToplevelWmCapabilities};

// ---- registry: the list of everything the compositor offers -----------------
void onGlobal(void* userData, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<App*>(userData);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wmBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 5u)));
        xdg_wm_base_add_listener(app->wmBase, &kWmBaseListener, app);
    }
}
void onGlobalRemove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

} // namespace

namespace {

// ---------------------------------------------------------------------------
// Finding the compositor.
//
// wl_display_connect(nullptr) only looks at $WAYLAND_DISPLAY (default "wayland-0")
// inside $XDG_RUNTIME_DIR. That fails in a terminal or IDE that did not inherit
// the desktop session's environment, and when a nested compositor such as weston
// uses another socket name. This helper tries, in order:
//   1. the name given with --display NAME (no fallback: you asked for that one)
//   2. $WAYLAND_DISPLAY
//   3. every wayland-N socket in $XDG_RUNTIME_DIR, the first that accepts us
// A relative name is looked up in $XDG_RUNTIME_DIR, an absolute path is used as is.
// ---------------------------------------------------------------------------
std::string runtimeDir() {
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    return dir && *dir ? dir : "/run/user/" + std::to_string(getuid());
}

std::string socketPath(const std::string& name) { return !name.empty() && name[0] == '/' ? name : runtimeDir() + "/" + name; }

std::vector<std::string> listSockets() {
    std::vector<std::string> names;
    if (DIR* dir = opendir(runtimeDir().c_str())) {
        while (const dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            // "wayland-0.lock" is a lock file next to the socket, not a socket.
            if (name.rfind("wayland-", 0) == 0 && name.find('.') == std::string::npos) names.push_back(name);
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}

wl_display* connectToCompositor(const char* requested, std::string& usedName) {
    if (requested && *requested) {
        usedName = requested;
        return wl_display_connect(socketPath(requested).c_str());
    }
    if (const char* env = std::getenv("WAYLAND_DISPLAY"); env && *env) {
        if (wl_display* display = wl_display_connect(socketPath(env).c_str())) {
            usedName = env;
            return display;
        }
    }
    for (const std::string& name : listSockets()) {
        if (wl_display* display = wl_display_connect(socketPath(name).c_str())) {
            usedName = name;
            return display;
        }
    }
    return nullptr;
}

void explainNoCompositor(const char* requested) {
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const char* env = std::getenv("WAYLAND_DISPLAY");
    const std::vector<std::string> sockets = listSockets();
    std::fprintf(stderr, "Cannot connect to a Wayland compositor.\n");
    if (requested && *requested) std::fprintf(stderr, "  --display        = %s (not found or refused the connection)\n", requested);
    std::fprintf(stderr, "  XDG_SESSION_TYPE = %s\n", session && *session ? session : "(unset)");
    std::fprintf(stderr, "  WAYLAND_DISPLAY  = %s\n", env && *env ? env : "(unset)");
    std::fprintf(stderr, "  socket directory = %s (%zu wayland-* socket(s) found)\n", runtimeDir().c_str(), sockets.size());
    if (session && std::strcmp(session, "x11") == 0)
        std::fprintf(stderr, "You are in an X11 session. Log in with a Wayland session, or start a nested compositor:\n"
                             "  weston --backend=x11 --socket=wayland-dev &\n"
                             "  <this program> --display wayland-dev\n");
}

// Returns the value of "--display NAME" or "--display=NAME", or nullptr.
const char* displayOption(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--display") == 0 && i + 1 < argc) return argv[i + 1];
        if (std::strncmp(argv[i], "--display=", 10) == 0) return argv[i] + 10;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    App app;
    const char* requested = displayOption(argc, argv);
    std::string displayName;
    app.display = connectToCompositor(requested, displayName);
    if (!app.display) {
        explainNoCompositor(requested);
        return EXIT_FAILURE;
    }
    std::printf("Connected to Wayland display %s\n", displayName.c_str());
    std::fflush(stdout);

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &kRegistryListener, &app);
    wl_display_roundtrip(app.display);   // wait until all globals have been announced

    if (!app.compositor || !app.shm || !app.wmBase) {
        std::fprintf(stderr, "Compositor is missing wl_compositor, wl_shm or xdg_wm_base.\n");
        return EXIT_FAILURE;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wmBase, app.surface);
    xdg_surface_add_listener(app.xdgSurface, &kXdgSurfaceListener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
    xdg_toplevel_add_listener(app.toplevel, &kToplevelListener, &app);
    xdg_toplevel_set_title(app.toplevel, "Stage 4: Wayland window");
    xdg_toplevel_set_app_id(app.toplevel, "linux-windowing.p04");   // lets the desktop match icons/settings
    xdg_toplevel_set_min_size(app.toplevel, 200, 120);

    // An initial commit *without* a buffer asks the compositor for the first
    // configure event. Attaching a buffer before that would be a protocol error.
    wl_surface_commit(app.surface);

    // Blocks, reads from the socket and calls our listeners.
    while (app.running && wl_display_dispatch(app.display) != -1) {
    }

    xdg_toplevel_destroy(app.toplevel);
    xdg_surface_destroy(app.xdgSurface);
    wl_surface_destroy(app.surface);
    xdg_wm_base_destroy(app.wmBase);
    wl_shm_destroy(app.shm);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);
    return EXIT_SUCCESS;
}
