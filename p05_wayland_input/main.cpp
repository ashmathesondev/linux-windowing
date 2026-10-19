// Stage 5: Wayland input, resizing, frame pacing and window decorations.
//
// This is the Wayland counterpart of stages 2 and 3: an animated software
// framebuffer, a square that follows the mouse, and keyboard/mouse events
// printed to the terminal. On top of that, Wayland needs a few things X11 gave
// us for free:
//
//   * Input comes from a wl_seat. It announces capabilities (pointer, keyboard)
//     and we create wl_pointer / wl_keyboard objects for them.
//   * The keyboard sends its *keymap* as a file descriptor; we feed it to
//     libxkbcommon ourselves.
//   * Key repeat is the client's job. The compositor only tells us the rate and
//     delay (wl_keyboard.repeat_info). We use a timerfd.
//   * The cursor image is the client's job too (wl_cursor + a cursor surface).
//   * Window decorations: we ask the compositor for server-side decorations
//     (xdg-decoration). GNOME refuses, so we fall back to drawing a title bar and
//     border ourselves (client-side decorations) and implement move/resize with
//     xdg_toplevel.move / .resize, which hand the drag over to the compositor.
//   * Frame pacing: wl_surface.frame tells us when the compositor wants the next
//     frame, so we never render faster than the display or when hidden.
//   * Buffers are shared with the compositor. A buffer is "busy" from commit
//     until wl_buffer.release; we never draw into a busy buffer.
//
// Keys: Escape quits. Right-click the title bar for the window menu,
// double-click the title bar to maximize.

#include <dirent.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

namespace {

constexpr int kTitleBarHeight = 32;
constexpr int kBorder = 6;          // resize handle thickness (CSD only)
constexpr int kCloseButtonWidth = 40;

constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) { return (r << 16) | (g << 8) | b; }

enum class Region {
    Client, TitleBar, CloseButton,
    Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight
};

struct App;

// ---------------------------------------------------------------------------
// Shared-memory buffer with busy tracking
// ---------------------------------------------------------------------------
struct Buffer {
    App* app = nullptr;
    wl_buffer* buffer = nullptr;
    uint32_t* data = nullptr;
    size_t size = 0;
    int width = 0;
    int height = 0;
    bool busy = false;       // true from commit until the compositor releases it
    bool obsolete = false;   // wrong size now; destroy as soon as it is released
};

struct App {
    // Globals
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wmBase = nullptr;
    zxdg_decoration_manager_v1* decorationManager = nullptr;
    wl_seat* seat = nullptr;

    // Window
    wl_surface* surface = nullptr;
    xdg_surface* xdgSurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    zxdg_toplevel_decoration_v1* decoration = nullptr;
    wl_callback* frameCallback = nullptr;
    bool clientSideDecorations = true;   // until the compositor says otherwise
    bool configured = false;
    bool running = true;

    int width = 640;
    int height = 400;
    int floatingWidth = 640;    // size to restore when leaving maximized/fullscreen
    int floatingHeight = 400;
    int pendingWidth = 0;
    int pendingHeight = 0;
    bool pendingActivated = false;
    bool pendingMaximized = false;
    bool pendingFullscreen = false;
    bool pendingTiled = false;
    bool activated = false;
    bool maximized = false;
    bool fullscreen = false;
    bool tiled = false;

    std::vector<Buffer*> buffers;

    // Input
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    uint32_t pointerEnterSerial = 0;
    double mouseX = 0, mouseY = 0;
    bool mouseInside = false;
    Region hoverRegion = Region::Client;
    uint32_t lastTitleClickTime = 0;

    // Cursor
    wl_cursor_theme* cursorTheme = nullptr;
    wl_surface* cursorSurface = nullptr;
    std::string currentCursorName;

    // Keyboard (xkbcommon)
    xkb_context* xkbContext = nullptr;
    xkb_keymap* xkbKeymap = nullptr;
    xkb_state* xkbState = nullptr;
    int repeatTimer = -1;
    int repeatRate = 25;     // characters per second; 0 = disabled
    int repeatDelay = 600;   // ms before the first repeat
    xkb_keycode_t repeatKey = 0;

    timespec startTime{};
};

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
void destroyBuffer(Buffer* b) {
    wl_buffer_destroy(b->buffer);
    munmap(b->data, b->size);
    auto& v = b->app->buffers;
    v.erase(std::remove(v.begin(), v.end(), b), v.end());
    delete b;
}

void onBufferRelease(void* userData, wl_buffer*) {
    auto* b = static_cast<Buffer*>(userData);
    b->busy = false;
    if (b->obsolete) destroyBuffer(b);
}
const wl_buffer_listener kBufferListener = {onBufferRelease};

Buffer* createBuffer(App& app, int width, int height) {
    const int stride = width * 4;
    const size_t size = static_cast<size_t>(stride) * height;

    int fd = memfd_create("p05-buffer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) < 0) {
        std::perror("memfd_create/ftruncate");
        std::exit(EXIT_FAILURE);
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::perror("mmap");
        std::exit(EXIT_FAILURE);
    }
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, static_cast<int32_t>(size));
    wl_buffer* wlBuf = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    auto* b = new Buffer;
    b->app = &app;
    b->buffer = wlBuf;
    b->data = static_cast<uint32_t*>(data);
    b->size = size;
    b->width = width;
    b->height = height;
    wl_buffer_add_listener(wlBuf, &kBufferListener, b);
    app.buffers.push_back(b);
    return b;
}

// Returns a buffer of the right size that the compositor is not using.
Buffer* acquireBuffer(App& app, int width, int height) {
    // Retire buffers of the wrong size.
    for (Buffer* b : std::vector<Buffer*>(app.buffers)) {
        if (b->width != width || b->height != height) {
            b->obsolete = true;
            if (!b->busy) destroyBuffer(b);
        }
    }
    for (Buffer* b : app.buffers) {
        if (!b->busy && !b->obsolete) return b;
    }
    return createBuffer(app, width, height);   // typically we end up with 2 or 3 buffers
}

// ---------------------------------------------------------------------------
// Software drawing
// ---------------------------------------------------------------------------
struct Canvas {
    uint32_t* pixels;
    int width;
    int height;
};

void fillRect(Canvas& c, int x, int y, int w, int h, uint32_t color) {
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = std::min(x + w, c.width), y1 = std::min(y + h, c.height);
    for (int yy = y0; yy < y1; ++yy) {
        std::fill(c.pixels + static_cast<size_t>(yy) * c.width + x0,
                  c.pixels + static_cast<size_t>(yy) * c.width + std::max(x0, x1), color);
    }
}

void drawLine(Canvas& c, int x0, int y0, int x1, int y1, uint32_t color) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < c.width && y0 < c.height) c.pixels[static_cast<size_t>(y0) * c.width + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void drawContent(Canvas& c, int originX, int originY, int w, int h, double seconds, const App& app) {
    const unsigned phase = static_cast<unsigned>(seconds * 60.0);
    for (int y = 0; y < h; ++y) {
        const int py = originY + y;
        if (py < 0 || py >= c.height) continue;
        const unsigned g = static_cast<unsigned>(y * 255 / std::max(h - 1, 1));
        for (int x = 0; x < w; ++x) {
            const int px = originX + x;
            if (px < 0 || px >= c.width) continue;
            const unsigned r = static_cast<unsigned>(x * 255 / std::max(w - 1, 1));
            const unsigned b = (phase + static_cast<unsigned>(x + y) / 4) & 0xff;
            c.pixels[static_cast<size_t>(py) * c.width + px] = rgb(r, g, b < 128 ? b * 2 : (255 - b) * 2);
        }
    }
    const int bar = static_cast<int>((std::sin(seconds * 2.0) * 0.5 + 0.5) * std::max(w - 80, 0));
    fillRect(c, originX + bar, originY + h - 24, 80, 12, rgb(255, 255, 255));

    if (app.mouseInside) {
        const int mx = static_cast<int>(app.mouseX), my = static_cast<int>(app.mouseY);
        fillRect(c, mx - 22, my - 22, 44, 44, rgb(0, 0, 0));
        fillRect(c, mx - 20, my - 20, 40, 40, rgb(255, 220, 80));
    }
}

void drawDecorations(Canvas& c, const App& app) {
    const uint32_t bar = app.activated ? rgb(52, 62, 84) : rgb(90, 90, 96);
    const uint32_t edge = app.activated ? rgb(34, 40, 56) : rgb(70, 70, 76);
    fillRect(c, 0, 0, c.width, kTitleBarHeight, bar);
    if (!app.maximized && !app.fullscreen) {   // outline only when floating
        fillRect(c, 0, kTitleBarHeight, 1, c.height - kTitleBarHeight, edge);
        fillRect(c, c.width - 1, kTitleBarHeight, 1, c.height - kTitleBarHeight, edge);
        fillRect(c, 0, c.height - 1, c.width, 1, edge);
    }
    // Close button
    const int bx = c.width - kCloseButtonWidth;
    if (app.hoverRegion == Region::CloseButton) fillRect(c, bx, 0, kCloseButtonWidth, kTitleBarHeight, rgb(200, 60, 60));
    const int cx = bx + kCloseButtonWidth / 2, cy = kTitleBarHeight / 2;
    drawLine(c, cx - 6, cy - 6, cx + 6, cy + 6, rgb(240, 240, 240));
    drawLine(c, cx - 6, cy + 6, cx + 6, cy - 6, rgb(240, 240, 240));
    drawLine(c, cx - 5, cy - 6, cx + 7, cy + 6, rgb(240, 240, 240));
    drawLine(c, cx - 5, cy + 6, cx + 7, cy - 6, rgb(240, 240, 240));
    // Stage 7 adds text rendering; until then the title bar has no title.
}

// ---------------------------------------------------------------------------
// Hit testing for our own decorations
// ---------------------------------------------------------------------------
Region hitTest(const App& app, double x, double y) {
    if (!app.clientSideDecorations) return Region::Client;
    const bool resizable = !app.maximized && !app.fullscreen && !app.tiled;
    const bool left = x < kBorder, right = x >= app.width - kBorder;
    const bool top = y < kBorder, bottom = y >= app.height - kBorder;
    if (resizable) {
        if (top && left) return Region::TopLeft;
        if (top && right) return Region::TopRight;
        if (bottom && left) return Region::BottomLeft;
        if (bottom && right) return Region::BottomRight;
        if (top) return Region::Top;
        if (bottom) return Region::Bottom;
        if (left) return Region::Left;
        if (right) return Region::Right;
    }
    if (y < kTitleBarHeight) {
        return x >= app.width - kCloseButtonWidth ? Region::CloseButton : Region::TitleBar;
    }
    return Region::Client;
}

const char* cursorNameFor(Region r) {
    switch (r) {
    case Region::Top: return "top_side";
    case Region::Bottom: return "bottom_side";
    case Region::Left: return "left_side";
    case Region::Right: return "right_side";
    case Region::TopLeft: return "top_left_corner";
    case Region::TopRight: return "top_right_corner";
    case Region::BottomLeft: return "bottom_left_corner";
    case Region::BottomRight: return "bottom_right_corner";
    default: return "default";
    }
}

xdg_toplevel_resize_edge edgeFor(Region r) {
    switch (r) {
    case Region::Top: return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case Region::Bottom: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case Region::Left: return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case Region::Right: return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case Region::TopLeft: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case Region::TopRight: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case Region::BottomLeft: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case Region::BottomRight: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    default: return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------
void setCursor(App& app, const char* name) {
    if (!app.pointer || !app.cursorTheme || !app.mouseInside) return;
    if (app.currentCursorName == name) return;
    wl_cursor* cursor = wl_cursor_theme_get_cursor(app.cursorTheme, name);
    if (!cursor) cursor = wl_cursor_theme_get_cursor(app.cursorTheme, "left_ptr");
    if (!cursor) return;
    app.currentCursorName = name;

    wl_cursor_image* image = cursor->images[0];
    wl_surface_attach(app.cursorSurface, wl_cursor_image_get_buffer(image), 0, 0);
    wl_surface_damage_buffer(app.cursorSurface, 0, 0, static_cast<int32_t>(image->width),
                             static_cast<int32_t>(image->height));
    wl_surface_commit(app.cursorSurface);
    wl_pointer_set_cursor(app.pointer, app.pointerEnterSerial, app.cursorSurface,
                          static_cast<int32_t>(image->hotspot_x), static_cast<int32_t>(image->hotspot_y));
}

// ---------------------------------------------------------------------------
// Rendering and frame pacing
// ---------------------------------------------------------------------------
double elapsedSeconds(const App& app) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec - app.startTime.tv_sec) +
           static_cast<double>(now.tv_nsec - app.startTime.tv_nsec) * 1e-9;
}

void render(App& app, bool requestNextFrame);

void onFrameDone(void* userData, wl_callback* cb, uint32_t) {
    auto* app = static_cast<App*>(userData);
    wl_callback_destroy(cb);
    app->frameCallback = nullptr;
    render(*app, true);
}
const wl_callback_listener kFrameListener = {onFrameDone};

void render(App& app, bool requestNextFrame) {
    if (!app.configured) return;

    Buffer* buf = acquireBuffer(app, app.width, app.height);
    Canvas canvas{buf->data, app.width, app.height};

    const double t = elapsedSeconds(app);
    if (app.clientSideDecorations) {
        // Content below the title bar, inside the border.
        drawContent(canvas, 0, kTitleBarHeight, app.width, app.height - kTitleBarHeight, t, app);
        drawDecorations(canvas, app);
    } else {
        drawContent(canvas, 0, 0, app.width, app.height, t, app);
    }

    if (requestNextFrame && !app.frameCallback) {
        app.frameCallback = wl_surface_frame(app.surface);
        wl_callback_add_listener(app.frameCallback, &kFrameListener, &app);
    }
    wl_surface_attach(app.surface, buf->buffer, 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
    wl_surface_commit(app.surface);
    buf->busy = true;
}

// ---------------------------------------------------------------------------
// xdg-shell
// ---------------------------------------------------------------------------
void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmBaseListener = {onWmPing};

void onXdgSurfaceConfigure(void* userData, xdg_surface* surf, uint32_t serial) {
    auto* app = static_cast<App*>(userData);
    xdg_surface_ack_configure(surf, serial);

    app->activated = app->pendingActivated;
    app->maximized = app->pendingMaximized;
    app->fullscreen = app->pendingFullscreen;
    app->tiled = app->pendingTiled;

    const bool floating = !app->maximized && !app->fullscreen && !app->tiled;
    if (app->pendingWidth > 0 && app->pendingHeight > 0) {
        app->width = app->pendingWidth;
        app->height = app->pendingHeight;
        if (floating) {
            app->floatingWidth = app->width;
            app->floatingHeight = app->height;
        }
    } else if (floating) {
        // 0x0 = "pick your own size": return to the size we had before maximizing.
        app->width = app->floatingWidth;
        app->height = app->floatingHeight;
    }
    app->configured = true;
    render(*app, app->frameCallback == nullptr);
}
const xdg_surface_listener kXdgSurfaceListener = {onXdgSurfaceConfigure};

void onToplevelConfigure(void* userData, xdg_toplevel*, int32_t w, int32_t h, wl_array* states) {
    auto* app = static_cast<App*>(userData);
    app->pendingWidth = w;
    app->pendingHeight = h;
    app->pendingActivated = app->pendingMaximized = app->pendingFullscreen = app->pendingTiled = false;
    auto* begin = static_cast<uint32_t*>(states->data);
    auto* end = begin + states->size / sizeof(uint32_t);
    for (auto* s = begin; s < end; ++s) {
        switch (*s) {
        case XDG_TOPLEVEL_STATE_ACTIVATED: app->pendingActivated = true; break;
        case XDG_TOPLEVEL_STATE_MAXIMIZED: app->pendingMaximized = true; break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN: app->pendingFullscreen = true; break;
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM: app->pendingTiled = true; break;
        default: break;
        }
    }
}
void onToplevelClose(void* userData, xdg_toplevel*) { static_cast<App*>(userData)->running = false; }
void onToplevelBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void onToplevelCaps(void*, xdg_toplevel*, wl_array*) {}
const xdg_toplevel_listener kToplevelListener = {onToplevelConfigure, onToplevelClose, onToplevelBounds,
                                                 onToplevelCaps};

void onDecorationConfigure(void* userData, zxdg_toplevel_decoration_v1*, uint32_t mode) {
    auto* app = static_cast<App*>(userData);
    app->clientSideDecorations = (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    std::printf("Decorations: %s\n", app->clientSideDecorations ? "client-side (we draw them)" : "server-side");
}
const zxdg_toplevel_decoration_v1_listener kDecorationListener = {onDecorationConfigure};

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
void logKey(App& app, xkb_keycode_t code, bool pressed, bool repeat) {
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, code);
    char name[64];
    xkb_keysym_get_name(sym, name, sizeof name);
    char utf8[16] = {};
    if (pressed) xkb_state_key_get_utf8(app.xkbState, code, utf8, sizeof utf8);
    std::string text;
    for (const char* p = utf8; *p; ++p) {
        const auto ch = static_cast<unsigned char>(*p);
        if (ch < 0x20 || ch == 0x7f) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", ch);
            text += buf;
        } else {
            text += *p;
        }
    }
    std::printf("Key%-7s keycode=%3u sym=%-14s text=\"%s\"%s\n", pressed ? "Press" : "Release", code, name,
                text.c_str(), repeat ? " (repeat)" : "");
    std::fflush(stdout);
    if (pressed && sym == XKB_KEY_Escape) app.running = false;
}

void stopRepeat(App& app) {
    app.repeatKey = 0;
    itimerspec off{};
    timerfd_settime(app.repeatTimer, 0, &off, nullptr);
}

void onKeymap(void* userData, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    auto* app = static_cast<App*>(userData);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    xkb_keymap* keymap = xkb_keymap_new_from_buffer(app->xkbContext, static_cast<const char*>(map),
                                                    strnlen(static_cast<const char*>(map), size),
                                                    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;
    xkb_state* state = xkb_state_new(keymap);

    if (app->xkbState) xkb_state_unref(app->xkbState);
    if (app->xkbKeymap) xkb_keymap_unref(app->xkbKeymap);
    app->xkbKeymap = keymap;
    app->xkbState = state;
}

void onKeyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) { std::printf("Keyboard focus in\n"); }
void onKeyboardLeave(void* userData, wl_keyboard*, uint32_t, wl_surface*) {
    stopRepeat(*static_cast<App*>(userData));
    std::printf("Keyboard focus out\n");
}

void onKey(void* userData, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t keyState) {
    auto* app = static_cast<App*>(userData);
    if (!app->xkbState) return;
    const xkb_keycode_t code = key + 8;   // evdev codes are offset by 8 in xkb
    const bool pressed = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;

    // Unlike X11 (stage 2) we do not call xkb_state_update_key here: on Wayland the
    // compositor sends the modifier state in wl_keyboard.modifiers, and that is
    // the only thing we feed into xkb_state.
    logKey(*app, code, pressed, false);

    if (pressed && app->repeatRate > 0 && xkb_keymap_key_repeats(app->xkbKeymap, code)) {
        app->repeatKey = code;
        itimerspec spec{};
        spec.it_value.tv_sec = app->repeatDelay / 1000;
        spec.it_value.tv_nsec = (app->repeatDelay % 1000) * 1000000L;
        spec.it_interval.tv_sec = 0;
        spec.it_interval.tv_nsec = 1000000000L / app->repeatRate;
        timerfd_settime(app->repeatTimer, 0, &spec, nullptr);
    } else if (!pressed && code == app->repeatKey) {
        stopRepeat(*app);
    }
}

void onModifiers(void* userData, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched,
                 uint32_t locked, uint32_t group) {
    auto* app = static_cast<App*>(userData);
    if (app->xkbState) xkb_state_update_mask(app->xkbState, depressed, latched, locked, 0, 0, group);
}

void onRepeatInfo(void* userData, wl_keyboard*, int32_t rate, int32_t delay) {
    auto* app = static_cast<App*>(userData);
    app->repeatRate = rate;
    app->repeatDelay = delay;
}
const wl_keyboard_listener kKeyboardListener = {onKeymap, onKeyboardEnter, onKeyboardLeave,
                                                onKey,    onModifiers,     onRepeatInfo};

// ---------------------------------------------------------------------------
// Pointer
// ---------------------------------------------------------------------------
void updateHover(App& app) {
    const Region r = app.mouseInside ? hitTest(app, app.mouseX, app.mouseY) : Region::Client;
    app.hoverRegion = r;
    setCursor(app, cursorNameFor(r));
}

void onPointerEnter(void* userData, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
    auto* app = static_cast<App*>(userData);
    app->pointerEnterSerial = serial;
    app->mouseInside = true;
    app->mouseX = wl_fixed_to_double(x);
    app->mouseY = wl_fixed_to_double(y);
    app->currentCursorName.clear();   // the compositor forgets our cursor on every enter
    updateHover(*app);
}
void onPointerLeave(void* userData, wl_pointer*, uint32_t, wl_surface*) {
    auto* app = static_cast<App*>(userData);
    app->mouseInside = false;
    updateHover(*app);
}
void onPointerMotion(void* userData, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    auto* app = static_cast<App*>(userData);
    app->mouseX = wl_fixed_to_double(x);
    app->mouseY = wl_fixed_to_double(y);
    updateHover(*app);
}
void onPointerButton(void* userData, wl_pointer*, uint32_t serial, uint32_t time, uint32_t button,
                     uint32_t state) {
    auto* app = static_cast<App*>(userData);
    const bool pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
    std::printf("Button%-7s button=0x%x at (%.0f,%.0f)\n", pressed ? "Press" : "Release", button, app->mouseX,
                app->mouseY);

    if (!pressed) return;
    const Region r = hitTest(*app, app->mouseX, app->mouseY);
    if (button == BTN_LEFT) {
        if (r == Region::CloseButton) {
            app->running = false;
        } else if (r == Region::TitleBar) {
            if (time - app->lastTitleClickTime < 400) {   // double click
                if (app->maximized) xdg_toplevel_unset_maximized(app->toplevel);
                else xdg_toplevel_set_maximized(app->toplevel);
                app->lastTitleClickTime = 0;
            } else {
                app->lastTitleClickTime = time;
                xdg_toplevel_move(app->toplevel, app->seat, serial);   // compositor runs the drag
            }
        } else if (r != Region::Client) {
            xdg_toplevel_resize(app->toplevel, app->seat, serial, edgeFor(r));
        }
    } else if (button == BTN_RIGHT && r == Region::TitleBar) {
        xdg_toplevel_show_window_menu(app->toplevel, app->seat, serial, static_cast<int32_t>(app->mouseX),
                                      static_cast<int32_t>(app->mouseY));
    }
}
void onPointerAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    std::printf("Scroll %s %.2f\n", axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? "vertical" : "horizontal",
                wl_fixed_to_double(value));
}
void onPointerFrame(void*, wl_pointer*) {}
void onPointerAxisSource(void*, wl_pointer*, uint32_t) {}
void onPointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
void onPointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
void onPointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}
const wl_pointer_listener kPointerListener = {
    onPointerEnter,        onPointerLeave,      onPointerMotion,        onPointerButton,
    onPointerAxis,         onPointerFrame,      onPointerAxisSource,    onPointerAxisStop,
    onPointerAxisDiscrete, onPointerAxisValue120, onPointerAxisRelativeDirection};

// ---------------------------------------------------------------------------
// Seat: devices can appear and disappear at any time
// ---------------------------------------------------------------------------
void onSeatCapabilities(void* userData, wl_seat* seat, uint32_t caps) {
    auto* app = static_cast<App*>(userData);
    const bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
    const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;

    if (hasPointer && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &kPointerListener, app);
    } else if (!hasPointer && app->pointer) {
        wl_pointer_release(app->pointer);
        app->pointer = nullptr;
    }
    if (hasKeyboard && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &kKeyboardListener, app);
    } else if (!hasKeyboard && app->keyboard) {
        wl_keyboard_release(app->keyboard);
        app->keyboard = nullptr;
    }
}
void onSeatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {onSeatCapabilities, onSeatName};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
void onGlobal(void* userData, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* app = static_cast<App*>(userData);
    const std::string_view iface(interface);
    if (iface == wl_compositor_interface.name) {
        app->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (iface == wl_shm_interface.name) {
        app->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (iface == xdg_wm_base_interface.name) {
        app->wmBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 5u)));
        xdg_wm_base_add_listener(app->wmBase, &kWmBaseListener, app);
    } else if (iface == zxdg_decoration_manager_v1_interface.name) {
        app->decorationManager = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
    } else if (iface == wl_seat_interface.name) {
        app->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(app->seat, &kSeatListener, app);
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
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // see log lines immediately, even when piped
    App app;
    clock_gettime(CLOCK_MONOTONIC, &app.startTime);

    const char* requested = displayOption(argc, argv);
    std::string displayName;
    app.display = connectToCompositor(requested, displayName);
    if (!app.display) {
        explainNoCompositor(requested);
        return EXIT_FAILURE;
    }
    std::printf("Connected to Wayland display %s\n", displayName.c_str());
    app.xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    app.repeatTimer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &kRegistryListener, &app);
    wl_display_roundtrip(app.display);   // globals announced
    wl_display_roundtrip(app.display);   // seat capabilities, keymap etc. delivered

    if (!app.compositor || !app.shm || !app.wmBase) {
        std::fprintf(stderr, "Compositor is missing wl_compositor, wl_shm or xdg_wm_base.\n");
        return EXIT_FAILURE;
    }

    // Cursor
    app.cursorTheme = wl_cursor_theme_load(nullptr, 24, app.shm);
    app.cursorSurface = wl_compositor_create_surface(app.compositor);

    // Window
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wmBase, app.surface);
    xdg_surface_add_listener(app.xdgSurface, &kXdgSurfaceListener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
    xdg_toplevel_add_listener(app.toplevel, &kToplevelListener, &app);
    xdg_toplevel_set_title(app.toplevel, "Stage 5: Wayland input");
    xdg_toplevel_set_app_id(app.toplevel, "linux-windowing.p05");
    xdg_toplevel_set_min_size(app.toplevel, 240, 160);

    if (app.decorationManager) {
        app.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(app.decorationManager, app.toplevel);
        zxdg_toplevel_decoration_v1_add_listener(app.decoration, &kDecorationListener, &app);
        zxdg_toplevel_decoration_v1_set_mode(app.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        std::printf("Decorations: client-side (compositor has no xdg-decoration)\n");
    }
    wl_surface_commit(app.surface);   // ask for the first configure

    // Event loop: wayland socket + key repeat timer.
    while (app.running) {
        // Standard libwayland pattern for integrating with poll().
        while (wl_display_prepare_read(app.display) != 0) wl_display_dispatch_pending(app.display);
        if (wl_display_flush(app.display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(app.display);
            break;
        }

        pollfd fds[2] = {{wl_display_get_fd(app.display), POLLIN, 0}, {app.repeatTimer, POLLIN, 0}};
        const int n = poll(fds, 2, -1);
        if (n < 0 && errno != EINTR) {
            wl_display_cancel_read(app.display);
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(app.display) < 0) break;
        } else {
            wl_display_cancel_read(app.display);
        }
        wl_display_dispatch_pending(app.display);

        if (fds[1].revents & POLLIN) {
            uint64_t expirations = 0;
            if (read(app.repeatTimer, &expirations, sizeof expirations) > 0 && app.repeatKey && app.xkbState) {
                for (uint64_t i = 0; i < std::min<uint64_t>(expirations, 4); ++i) logKey(app, app.repeatKey, true, true);
            }
        }
    }

    // Cleanup (order: children before parents)
    for (Buffer* b : std::vector<Buffer*>(app.buffers)) destroyBuffer(b);
    if (app.frameCallback) wl_callback_destroy(app.frameCallback);
    if (app.decoration) zxdg_toplevel_decoration_v1_destroy(app.decoration);
    xdg_toplevel_destroy(app.toplevel);
    xdg_surface_destroy(app.xdgSurface);
    wl_surface_destroy(app.surface);
    if (app.cursorTheme) wl_cursor_theme_destroy(app.cursorTheme);
    wl_surface_destroy(app.cursorSurface);
    if (app.xkbState) xkb_state_unref(app.xkbState);
    if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
    xkb_context_unref(app.xkbContext);
    close(app.repeatTimer);
    wl_display_disconnect(app.display);
    return EXIT_SUCCESS;
}
