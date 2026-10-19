// Wayland backend (wl_shm buffers, xdg-shell, xdg-decoration, wl_seat, wl_data_device).
//
// Stages 4 and 5 explain the pieces one by one; this file puts them behind the
// Window interface and adds:
//   * client-side decorations with a title, minimize/maximize/close buttons
//     (used when the compositor does not draw window frames itself, e.g. GNOME),
//   * HiDPI through integer buffer scales,
//   * the clipboard through wl_data_device,
//   * pointer events translated to content-area coordinates.

#include <cairo.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <dirent.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "key_translate.hpp"
#include "window_base.hpp"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

// Newer versions of libwayland add events to some interfaces (for example
// wl_surface.preferred_buffer_scale). We bind old interface versions on purpose,
// so those listener members stay empty and compile on older distributions too.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

namespace ww::detail {

namespace {

constexpr int kTitleBarHeight = 32;   // logical pixels
constexpr int kBorder = 1;
constexpr int kResizeMargin = 6;
constexpr int kButtonWidth = 40;
constexpr size_t kMaxBuffers = 3;

enum class Zone {
    Content, TitleBar, CloseButton, MaximizeButton, MinimizeButton,
    Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight
};

struct ShmBuffer;

struct DataOffer {
    wl_data_offer* offer = nullptr;
    std::vector<std::string> mimeTypes;
};

struct Output {
    wl_output* output = nullptr;
    uint32_t name = 0;
    int scale = 1;
};

struct WaylandWindow final : public WindowBase {
    // ---- lifecycle ----------------------------------------------------------
    ~WaylandWindow() override;
    bool init(const WindowConfig& config);

    // ---- Window interface ---------------------------------------------------
    Backend backend() const override { return Backend::Wayland; }
    bool waitEvent(Event& out, int timeoutMs) override;
    Framebuffer acquireFramebuffer() override;
    void present() override;
    int widthPx() const override { return contentWPx_; }
    int heightPx() const override { return contentHPx_; }
    float scale() const override { return static_cast<float>(scale_); }
    void setTitle(const std::string& title) override;
    void setCursor(ww::Cursor cursor) override;
    void setClipboardText(const std::string& utf8) override;
    std::string clipboardText() override;

    // ---- event pump ---------------------------------------------------------
    bool pumpOnce(int waitMs);
    void handleKeyRepeat();

    // ---- geometry -----------------------------------------------------------
    void applyConfigure();
    void recomputeContent(bool forceEvent);
    void updateScale();
    void updateMinSize();
    int borderLogical() const { return (csd_ && !maximized_ && !fullscreen_ && !tiled_) ? kBorder : 0; }
    int titleLogical() const { return (csd_ && !fullscreen_) ? kTitleBarHeight : 0; }
    Zone zoneAt(double x, double y) const;

    // ---- buffers ------------------------------------------------------------
    ShmBuffer* createBuffer(int widthPx, int heightPx);
    ShmBuffer* acquireBuffer(int widthPx, int heightPx);
    void destroyBuffer(ShmBuffer* b);
    void drawDecorations(ShmBuffer& b);

    // ---- pointer / cursor ---------------------------------------------------
    void pointerMoved();
    void pointerButton(uint32_t serial, uint32_t button, bool pressed);
    void flushScroll();
    void applyCursor();
    void loadCursorTheme();

    // ---- clipboard ----------------------------------------------------------
    void dataOfferAdded(wl_data_offer* offer);
    void selectionChanged(wl_data_offer* offer);
    void freeOffer(wl_data_offer* offer);

    // ---- wayland objects ----------------------------------------------------
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    xdg_wm_base* wmBase_ = nullptr;
    zxdg_decoration_manager_v1* decorationManager_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_data_device_manager* dataDeviceManager_ = nullptr;
    wl_data_device* dataDevice_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    std::vector<Output> outputs_;
    std::vector<wl_output*> surfaceOutputs_;

    wl_surface* surface_ = nullptr;
    xdg_surface* xdgSurface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;

    // ---- window state ---------------------------------------------------------
    WindowConfig config_;
    std::string title_;
    bool csd_ = true;
    bool decorationDecided_ = false;
    bool configured_ = false;
    bool dead_ = false;
    bool activated_ = false, maximized_ = false, fullscreen_ = false, tiled_ = false;
    bool pendingActivated_ = false, pendingMaximized_ = false, pendingFullscreen_ = false, pendingTiled_ = false;
    int pendingW_ = 0, pendingH_ = 0;
    int surfW_ = 0, surfH_ = 0;                 // logical size of the whole surface
    int floatContentW_ = 0, floatContentH_ = 0; // content size to return to when un-maximizing
    int contentW_ = 0, contentH_ = 0;           // logical content size
    int contentWPx_ = 0, contentHPx_ = 0;       // pixels
    int scale_ = 1;
    int forcedScale_ = 0;

    // ---- buffers ----------------------------------------------------------------
    std::vector<ShmBuffer*> buffers_;
    ShmBuffer* current_ = nullptr;

    // ---- pointer / cursor ---------------------------------------------------------
    uint32_t lastSerial_ = 0;
    uint32_t pointerEnterSerial_ = 0;
    double pointerX_ = 0, pointerY_ = 0;
    bool pointerInside_ = false;
    bool pointerInContent_ = false;
    int contentGrabButtons_ = 0;
    Zone hoverZone_ = Zone::Content;
    ClickTracker decoClicks_;
    double scrollValue_[2] = {0, 0};
    int scrollDiscrete_[2] = {0, 0};
    bool scrollHasDiscrete_[2] = {false, false};
    wl_cursor_theme* cursorTheme_ = nullptr;
    int cursorThemeScale_ = 0;
    wl_surface* cursorSurface_ = nullptr;
    ww::Cursor appCursor_ = ww::Cursor::Arrow;
    std::string appliedCursorName_;

    // ---- keyboard ------------------------------------------------------------------
    xkb_context* xkbContext_ = nullptr;
    xkb_keymap* xkbKeymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;
    int repeatTimer_ = -1;
    int repeatRate_ = 25;
    int repeatDelay_ = 600;
    xkb_keycode_t repeatKey_ = 0;

    // ---- clipboard -------------------------------------------------------------------
    std::unordered_map<wl_data_offer*, DataOffer> offers_;
    wl_data_offer* selectionOffer_ = nullptr;
    wl_data_source* ownSource_ = nullptr;
    std::string clipboardOwn_;
};

struct ShmBuffer {
    WaylandWindow* owner = nullptr;
    wl_buffer* buffer = nullptr;
    uint8_t* data = nullptr;
    size_t size = 0;
    int widthPx = 0;
    int heightPx = 0;
    int strideBytes = 0;
    bool busy = false;
    bool obsolete = false;
};

// ============================================================================
// Listener trampolines
// ============================================================================
WaylandWindow& self(void* data) { return *static_cast<WaylandWindow*>(data); }

// ---- buffers -------------------------------------------------------------------
const wl_buffer_listener kBufferListener = {
    [](void* data, wl_buffer*) {
        auto* b = static_cast<ShmBuffer*>(data);
        b->busy = false;
        if (b->obsolete) b->owner->destroyBuffer(b);
    }};

// ---- wm_base ---------------------------------------------------------------------
const xdg_wm_base_listener kWmBaseListener = {
    [](void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }};

// ---- surface (which outputs we are on) --------------------------------------------
const wl_surface_listener kSurfaceListener = {
    .enter = [](void* data, wl_surface*, wl_output* output) {
        auto& w = self(data);
        w.surfaceOutputs_.push_back(output);
        w.updateScale();
    },
    .leave = [](void* data, wl_surface*, wl_output* output) {
        auto& w = self(data);
        w.surfaceOutputs_.erase(std::remove(w.surfaceOutputs_.begin(), w.surfaceOutputs_.end(), output),
                                w.surfaceOutputs_.end());
        w.updateScale();
    }};

// ---- outputs -----------------------------------------------------------------------
const wl_output_listener kOutputListener = {
    .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    .mode = [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
    .done = [](void*, wl_output*) {},
    .scale = [](void* data, wl_output* output, int32_t factor) {
        auto& w = self(data);
        for (Output& o : w.outputs_) {
            if (o.output == output) o.scale = std::max(factor, 1);
        }
        w.updateScale();
    }};

// ---- xdg_surface / xdg_toplevel ------------------------------------------------------
const xdg_surface_listener kXdgSurfaceListener = {
    [](void* data, xdg_surface* surf, uint32_t serial) {
        auto& w = self(data);
        xdg_surface_ack_configure(surf, serial);
        w.applyConfigure();
    }};

const xdg_toplevel_listener kToplevelListener = {
    .configure = [](void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states) {
        auto& w = self(data);
        w.pendingW_ = width;
        w.pendingH_ = height;
        w.pendingActivated_ = w.pendingMaximized_ = w.pendingFullscreen_ = w.pendingTiled_ = false;
        auto* begin = static_cast<uint32_t*>(states->data);
        auto* end = begin + states->size / sizeof(uint32_t);
        for (auto* s = begin; s < end; ++s) {
            switch (*s) {
            case XDG_TOPLEVEL_STATE_ACTIVATED: w.pendingActivated_ = true; break;
            case XDG_TOPLEVEL_STATE_MAXIMIZED: w.pendingMaximized_ = true; break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN: w.pendingFullscreen_ = true; break;
            case XDG_TOPLEVEL_STATE_TILED_LEFT:
            case XDG_TOPLEVEL_STATE_TILED_RIGHT:
            case XDG_TOPLEVEL_STATE_TILED_TOP:
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM: w.pendingTiled_ = true; break;
            default: break;
            }
        }
    },
    .close = [](void* data, xdg_toplevel*) { self(data).push(CloseEvent{}); },
    .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {}};

const zxdg_toplevel_decoration_v1_listener kDecorationListener = {
    [](void* data, zxdg_toplevel_decoration_v1*, uint32_t mode) {
        auto& w = self(data);
        w.csd_ = mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        w.decorationDecided_ = true;
    }};

// ---- keyboard -----------------------------------------------------------------------------
const wl_keyboard_listener kKeyboardListener = {
    .keymap = [](void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        auto& w = self(data);
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map == MAP_FAILED) return;
        xkb_keymap* keymap = xkb_keymap_new_from_buffer(w.xkbContext_, static_cast<const char*>(map),
                                                        strnlen(static_cast<const char*>(map), size),
                                                        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);
        if (!keymap) return;
        xkb_state* state = xkb_state_new(keymap);
        if (w.xkbState_) xkb_state_unref(w.xkbState_);
        if (w.xkbKeymap_) xkb_keymap_unref(w.xkbKeymap_);
        w.xkbKeymap_ = keymap;
        w.xkbState_ = state;
    },
    .enter = [](void* data, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*) {
        auto& w = self(data);
        w.lastSerial_ = serial;
        w.push(FocusEvent{true});
    },
    .leave = [](void* data, wl_keyboard*, uint32_t, wl_surface*) {
        auto& w = self(data);
        itimerspec off{};
        timerfd_settime(w.repeatTimer_, 0, &off, nullptr);
        w.repeatKey_ = 0;
        w.push(FocusEvent{false});
    },
    .key = [](void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t keyState) {
        auto& w = self(data);
        w.lastSerial_ = serial;
        if (!w.xkbState_) return;
        const xkb_keycode_t code = key + 8;   // evdev -> xkb keycode
        const bool pressed = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;

        const KeyInfo info = translateKey(w.xkbKeymap_, w.xkbState_, code, pressed);
        w.push(KeyEvent{info.key, pressed, false, currentModifiers(w.xkbState_)});
        if (pressed && !info.text.empty()) w.push(TextEvent{info.text});

        if (pressed && w.repeatRate_ > 0 && xkb_keymap_key_repeats(w.xkbKeymap_, code)) {
            w.repeatKey_ = code;
            itimerspec spec{};
            spec.it_value.tv_sec = w.repeatDelay_ / 1000;
            spec.it_value.tv_nsec = (w.repeatDelay_ % 1000) * 1000000L;
            spec.it_interval.tv_nsec = 1000000000L / w.repeatRate_;
            timerfd_settime(w.repeatTimer_, 0, &spec, nullptr);
        } else if (!pressed && code == w.repeatKey_) {
            itimerspec off{};
            timerfd_settime(w.repeatTimer_, 0, &off, nullptr);
            w.repeatKey_ = 0;
        }
    },
    .modifiers = [](void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                    uint32_t group) {
        auto& w = self(data);
        // On Wayland the compositor is the source of truth for modifier state.
        if (w.xkbState_) xkb_state_update_mask(w.xkbState_, depressed, latched, locked, 0, 0, group);
    },
    .repeat_info = [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
        auto& w = self(data);
        w.repeatRate_ = rate;
        w.repeatDelay_ = delay;
    }};

// ---- pointer ---------------------------------------------------------------------------------
const wl_pointer_listener kPointerListener = {
    .enter = [](void* data, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
        auto& w = self(data);
        w.lastSerial_ = w.pointerEnterSerial_ = serial;
        w.pointerInside_ = true;
        w.pointerX_ = wl_fixed_to_double(x);
        w.pointerY_ = wl_fixed_to_double(y);
        w.appliedCursorName_.clear();   // the compositor forgets our cursor image on every enter
        w.pointerMoved();
    },
    .leave = [](void* data, wl_pointer*, uint32_t, wl_surface*) {
        auto& w = self(data);
        w.pointerInside_ = false;
        w.contentGrabButtons_ = 0;
        w.pointerMoved();
    },
    .motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        auto& w = self(data);
        w.pointerX_ = wl_fixed_to_double(x);
        w.pointerY_ = wl_fixed_to_double(y);
        w.pointerMoved();
    },
    .button = [](void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
        auto& w = self(data);
        w.lastSerial_ = serial;
        w.pointerButton(serial, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
    },
    .axis = [](void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        if (axis < 2) self(data).scrollValue_[axis] += wl_fixed_to_double(value);
    },
    .frame = [](void* data, wl_pointer*) { self(data).flushScroll(); },
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void* data, wl_pointer*, uint32_t axis, int32_t discrete) {
        if (axis < 2) {
            auto& w = self(data);
            w.scrollDiscrete_[axis] += discrete;
            w.scrollHasDiscrete_[axis] = true;
        }
    },
    .axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {}};

// ---- seat ----------------------------------------------------------------------------------------
const wl_seat_listener kSeatListener = {
    .capabilities = [](void* data, wl_seat* seat, uint32_t caps) {
        auto& w = self(data);
        const bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
        const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
        if (hasPointer && !w.pointer_) {
            w.pointer_ = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(w.pointer_, &kPointerListener, &w);
        } else if (!hasPointer && w.pointer_) {
            wl_pointer_release(w.pointer_);
            w.pointer_ = nullptr;
        }
        if (hasKeyboard && !w.keyboard_) {
            w.keyboard_ = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(w.keyboard_, &kKeyboardListener, &w);
        } else if (!hasKeyboard && w.keyboard_) {
            wl_keyboard_release(w.keyboard_);
            w.keyboard_ = nullptr;
        }
    },
    .name = [](void*, wl_seat*, const char*) {}};

// ---- clipboard ------------------------------------------------------------------------------------------
const wl_data_offer_listener kDataOfferListener = {
    .offer = [](void* data, wl_data_offer* offer, const char* mime) {
        auto& w = self(data);
        auto it = w.offers_.find(offer);
        if (it != w.offers_.end()) it->second.mimeTypes.emplace_back(mime);
    },
    .source_actions = [](void*, wl_data_offer*, uint32_t) {},
    .action = [](void*, wl_data_offer*, uint32_t) {}};

const wl_data_device_listener kDataDeviceListener = {
    .data_offer = [](void* data, wl_data_device*, wl_data_offer* offer) { self(data).dataOfferAdded(offer); },
    .enter = [](void* data, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
        if (offer) self(data).freeOffer(offer);   // drag and drop is not implemented
    },
    .leave = [](void*, wl_data_device*) {},
    .motion = [](void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {},
    .drop = [](void*, wl_data_device*) {},
    .selection = [](void* data, wl_data_device*, wl_data_offer* offer) { self(data).selectionChanged(offer); }};

const wl_data_source_listener kDataSourceListener = {
    .target = [](void*, wl_data_source*, const char*) {},
    .send = [](void* data, wl_data_source*, const char*, int32_t fd) {
        // Another program pastes: write our text into the pipe it gave us.
        const std::string& text = self(data).clipboardOwn_;
        size_t done = 0;
        while (done < text.size()) {
            const ssize_t n = write(fd, text.data() + done, text.size() - done);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) {
                    pollfd pfd{fd, POLLOUT, 0};
                    poll(&pfd, 1, 200);
                    continue;
                }
                break;
            }
            done += static_cast<size_t>(n);
        }
        close(fd);
    },
    .cancelled = [](void* data, wl_data_source* source) {
        auto& w = self(data);
        if (w.ownSource_ == source) w.ownSource_ = nullptr;
        wl_data_source_destroy(source);
    },
    .dnd_drop_performed = [](void*, wl_data_source*) {},
    .dnd_finished = [](void*, wl_data_source*) {},
    .action = [](void*, wl_data_source*, uint32_t) {}};

// ---- registry -----------------------------------------------------------------------------------------------
const wl_registry_listener kRegistryListener = {
    .global = [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto& w = self(data);
        const std::string_view iface(interface);
        if (iface == wl_compositor_interface.name) {
            w.compositor_ = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
        } else if (iface == wl_shm_interface.name) {
            w.shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        } else if (iface == xdg_wm_base_interface.name) {
            w.wmBase_ = static_cast<xdg_wm_base*>(
                wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 5u)));
            xdg_wm_base_add_listener(w.wmBase_, &kWmBaseListener, &w);
        } else if (iface == zxdg_decoration_manager_v1_interface.name) {
            w.decorationManager_ = static_cast<zxdg_decoration_manager_v1*>(
                wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
        } else if (iface == wl_seat_interface.name) {
            w.seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
            wl_seat_add_listener(w.seat_, &kSeatListener, &w);
        } else if (iface == wl_data_device_manager_interface.name) {
            w.dataDeviceManager_ = static_cast<wl_data_device_manager*>(
                wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
        } else if (iface == wl_output_interface.name) {
            auto* out = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 2u)));
            w.outputs_.push_back(Output{out, name, 1});
            wl_output_add_listener(out, &kOutputListener, &w);
        }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {}};

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
// (Programs built on ww::Window select one with $WAYLAND_DISPLAY; there is no --display option.)
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


} // namespace

// ============================================================================
// Setup and teardown
// ============================================================================
bool WaylandWindow::init(const WindowConfig& config) {
    config_ = config;
    title_ = config.title;
    forcedScale_ = static_cast<int>(std::lround(scaleFromEnvironment()));

    std::string displayName;
    display_ = connectToCompositor(nullptr, displayName);
    if (!display_) {
        explainNoCompositor(nullptr);
        return false;
    }

    xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    repeatTimer_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);   // globals
    wl_display_roundtrip(display_);   // output scales, seat capabilities, keymap

    if (!compositor_ || !shm_ || !wmBase_) {
        std::fprintf(stderr, "ww: the compositor lacks wl_compositor, wl_shm or xdg_wm_base\n");
        return false;
    }
    if (seat_ && dataDeviceManager_) {
        dataDevice_ = wl_data_device_manager_get_data_device(dataDeviceManager_, seat_);
        wl_data_device_add_listener(dataDevice_, &kDataDeviceListener, this);
    }

    cursorSurface_ = wl_compositor_create_surface(compositor_);

    surface_ = wl_compositor_create_surface(compositor_);
    wl_surface_add_listener(surface_, &kSurfaceListener, this);
    xdgSurface_ = xdg_wm_base_get_xdg_surface(wmBase_, surface_);
    xdg_surface_add_listener(xdgSurface_, &kXdgSurfaceListener, this);
    toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, title_.c_str());
    xdg_toplevel_set_app_id(toplevel_, config.appId.c_str());

    if (decorationManager_) {
        decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(decorationManager_, toplevel_);
        zxdg_toplevel_decoration_v1_add_listener(decoration_, &kDecorationListener, this);
        zxdg_toplevel_decoration_v1_set_mode(decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        csd_ = true;
        decorationDecided_ = true;
    }

    floatContentW_ = config.width;
    floatContentH_ = config.height;

    wl_surface_commit(surface_);   // triggers the first configure

    // Wait for the first configure so that size and scale are known when we return.
    const auto deadline = SteadyClock::now() + std::chrono::seconds(2);
    while (!configured_ && !dead_ && msUntil(deadline) > 0) pumpOnce(50);
    updateMinSize();
    return configured_;
}

WaylandWindow::~WaylandWindow() {
    if (repeatTimer_ >= 0) close(repeatTimer_);
    for (ShmBuffer* b : std::vector<ShmBuffer*>(buffers_)) destroyBuffer(b);
    if (ownSource_) wl_data_source_destroy(ownSource_);
    for (auto& [offer, info] : offers_) wl_data_offer_destroy(offer);
    if (dataDevice_) wl_data_device_destroy(dataDevice_);
    if (decoration_) zxdg_toplevel_decoration_v1_destroy(decoration_);
    if (toplevel_) xdg_toplevel_destroy(toplevel_);
    if (xdgSurface_) xdg_surface_destroy(xdgSurface_);
    if (surface_) wl_surface_destroy(surface_);
    if (cursorTheme_) wl_cursor_theme_destroy(cursorTheme_);
    if (cursorSurface_) wl_surface_destroy(cursorSurface_);
    if (pointer_) wl_pointer_release(pointer_);
    if (keyboard_) wl_keyboard_release(keyboard_);
    if (xkbState_) xkb_state_unref(xkbState_);
    if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
    if (xkbContext_) xkb_context_unref(xkbContext_);
    if (display_) wl_display_disconnect(display_);   // the compositor releases everything else
}

// ============================================================================
// Event pump
// ============================================================================
bool WaylandWindow::pumpOnce(int waitMs) {
    if (dead_) return false;

    while (wl_display_prepare_read(display_) != 0) wl_display_dispatch_pending(display_);
    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display_);
        dead_ = true;
        push(CloseEvent{});
        return false;
    }

    pollfd fds[2] = {{wl_display_get_fd(display_), POLLIN, 0}, {repeatTimer_, POLLIN, 0}};
    poll(fds, 2, waitMs);

    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(display_) < 0) {
            dead_ = true;
            push(CloseEvent{});
            return false;
        }
    } else {
        wl_display_cancel_read(display_);
    }
    if (wl_display_dispatch_pending(display_) < 0) {
        dead_ = true;
        push(CloseEvent{});
        return false;
    }
    if (fds[1].revents & POLLIN) handleKeyRepeat();
    return true;
}

void WaylandWindow::handleKeyRepeat() {
    uint64_t expirations = 0;
    if (read(repeatTimer_, &expirations, sizeof expirations) <= 0 || !repeatKey_ || !xkbState_) return;
    for (uint64_t i = 0; i < std::min<uint64_t>(expirations, 4); ++i) {
        const KeyInfo info = translateKey(xkbKeymap_, xkbState_, repeatKey_, true);
        push(KeyEvent{info.key, true, true, currentModifiers(xkbState_)});
        if (!info.text.empty()) push(TextEvent{info.text});
    }
}

bool WaylandWindow::waitEvent(Event& out, int timeoutMs) {
    const auto deadline = SteadyClock::now() + std::chrono::milliseconds(std::max(timeoutMs, 0));
    for (;;) {
        if (pop(out)) return true;
        if (dead_) return false;
        const int wait = timeoutMs < 0 ? -1 : msUntil(deadline);
        pumpOnce(wait);
        if (pop(out)) return true;
        if (timeoutMs >= 0 && msUntil(deadline) == 0) return false;
    }
}

// ============================================================================
// Geometry, scale and decorations
// ============================================================================
void WaylandWindow::applyConfigure() {
    activated_ = pendingActivated_;
    const bool wasMax = maximized_, wasFull = fullscreen_;
    maximized_ = pendingMaximized_;
    fullscreen_ = pendingFullscreen_;
    tiled_ = pendingTiled_;
    const bool floating = !maximized_ && !fullscreen_ && !tiled_;
    (void)wasMax;
    (void)wasFull;

    const int border = borderLogical();
    const int title = titleLogical();
    if (pendingW_ > 0 && pendingH_ > 0) {
        surfW_ = pendingW_;
        surfH_ = pendingH_;
        if (floating) {
            floatContentW_ = std::max(surfW_ - 2 * border, 1);
            floatContentH_ = std::max(surfH_ - title - border, 1);
        }
    } else {
        // "Pick your own size": the size we had before maximizing, or the requested initial size.
        surfW_ = floatContentW_ + 2 * border;
        surfH_ = floatContentH_ + title + border;
    }

    configured_ = true;
    updateScale();   // may also change scale_; cheap if unchanged
    recomputeContent(/*forceEvent=*/true);
    push(RedrawEvent{});
}

void WaylandWindow::recomputeContent(bool forceEvent) {
    const int border = borderLogical();
    const int title = titleLogical();
    const int w = std::max(surfW_ - 2 * border, 1);
    const int h = std::max(surfH_ - title - border, 1);
    const int wPx = w * scale_;
    const int hPx = h * scale_;
    const bool changed = wPx != contentWPx_ || hPx != contentHPx_;
    contentW_ = w;
    contentH_ = h;
    contentWPx_ = wPx;
    contentHPx_ = hPx;
    if (changed || forceEvent) push(ResizeEvent{contentWPx_, contentHPx_, static_cast<float>(scale_)});
}

void WaylandWindow::updateScale() {
    int s = 1;
    if (forcedScale_ > 0) {
        s = forcedScale_;
    } else {
        for (wl_output* o : surfaceOutputs_) {
            for (const Output& out : outputs_) {
                if (out.output == o) s = std::max(s, out.scale);
            }
        }
        if (surfaceOutputs_.empty()) {   // not shown yet: assume the biggest scale around to avoid a blurry first frame
            for (const Output& out : outputs_) s = std::max(s, out.scale);
        }
    }
    if (s == scale_) return;
    scale_ = s;
    if (configured_) {
        recomputeContent(true);
        push(RedrawEvent{});
    }
    loadCursorTheme();
}

void WaylandWindow::updateMinSize() {
    const int border = borderLogical();
    xdg_toplevel_set_min_size(toplevel_, config_.minWidth + 2 * border, config_.minHeight + titleLogical() + border);
}

Zone WaylandWindow::zoneAt(double x, double y) const {
    if (!csd_ || fullscreen_) return Zone::Content;
    const bool resizable = !maximized_ && !tiled_;
    if (resizable) {
        const bool left = x < kResizeMargin, right = x >= surfW_ - kResizeMargin;
        const bool top = y < kResizeMargin, bottom = y >= surfH_ - kResizeMargin;
        if (top && left) return Zone::TopLeft;
        if (top && right) return Zone::TopRight;
        if (bottom && left) return Zone::BottomLeft;
        if (bottom && right) return Zone::BottomRight;
        if (top) return Zone::Top;
        if (bottom) return Zone::Bottom;
        if (left) return Zone::Left;
        if (right) return Zone::Right;
    }
    if (y < kTitleBarHeight) {
        if (x >= surfW_ - kButtonWidth) return Zone::CloseButton;
        if (x >= surfW_ - 2 * kButtonWidth) return Zone::MaximizeButton;
        if (x >= surfW_ - 3 * kButtonWidth) return Zone::MinimizeButton;
        return Zone::TitleBar;
    }
    return Zone::Content;
}

// ============================================================================
// Buffers and drawing
// ============================================================================
ShmBuffer* WaylandWindow::createBuffer(int widthPx, int heightPx) {
    const int stride = widthPx * 4;
    const size_t size = static_cast<size_t>(stride) * static_cast<size_t>(heightPx);

    int fd = memfd_create("ww-buffer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(size)) < 0) {
        std::perror("ww: memfd_create");
        std::abort();
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::perror("ww: mmap");
        std::abort();
    }
    wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, static_cast<int32_t>(size));
    wl_buffer* wlBuf = wl_shm_pool_create_buffer(pool, 0, widthPx, heightPx, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    auto* b = new ShmBuffer;
    b->owner = this;
    b->buffer = wlBuf;
    b->data = static_cast<uint8_t*>(data);
    b->size = size;
    b->widthPx = widthPx;
    b->heightPx = heightPx;
    b->strideBytes = stride;
    wl_buffer_add_listener(wlBuf, &kBufferListener, b);
    buffers_.push_back(b);
    return b;
}

void WaylandWindow::destroyBuffer(ShmBuffer* b) {
    wl_buffer_destroy(b->buffer);
    munmap(b->data, b->size);
    buffers_.erase(std::remove(buffers_.begin(), buffers_.end(), b), buffers_.end());
    if (current_ == b) current_ = nullptr;
    delete b;
}

ShmBuffer* WaylandWindow::acquireBuffer(int widthPx, int heightPx) {
    for (ShmBuffer* b : std::vector<ShmBuffer*>(buffers_)) {
        if (b->widthPx != widthPx || b->heightPx != heightPx) {
            b->obsolete = true;
            if (!b->busy) destroyBuffer(b);
        }
    }
    auto findFree = [&]() -> ShmBuffer* {
        for (ShmBuffer* b : buffers_) {
            if (!b->busy && !b->obsolete) return b;
        }
        return nullptr;
    };
    if (ShmBuffer* b = findFree()) return b;

    // All buffers are in use by the compositor: wait briefly for a release
    // before growing, so a hidden window cannot allocate memory without bound.
    if (buffers_.size() >= kMaxBuffers) {
        const auto deadline = SteadyClock::now() + std::chrono::milliseconds(100);
        while (!dead_ && msUntil(deadline) > 0) {
            pumpOnce(msUntil(deadline));
            if (ShmBuffer* b = findFree()) return b;
        }
    }
    return createBuffer(widthPx, heightPx);
}

Framebuffer WaylandWindow::acquireFramebuffer() {
    const int surfWPx = surfW_ * scale_;
    const int surfHPx = surfH_ * scale_;
    if (current_ && (current_->widthPx != surfWPx || current_->heightPx != surfHPx)) current_ = nullptr;
    if (!current_) current_ = acquireBuffer(surfWPx, surfHPx);

    const int xPx = borderLogical() * scale_;
    const int yPx = titleLogical() * scale_;
    uint8_t* start = current_->data + static_cast<size_t>(yPx) * static_cast<size_t>(current_->strideBytes) +
                     static_cast<size_t>(xPx) * 4;
    return Framebuffer{reinterpret_cast<uint32_t*>(start), contentWPx_, contentHPx_, current_->strideBytes,
                       static_cast<float>(scale_)};
}

void WaylandWindow::present() {
    if (!current_ || !configured_) return;
    ShmBuffer* b = current_;
    current_ = nullptr;

    if (csd_ && !fullscreen_) drawDecorations(*b);

    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_attach(surface_, b->buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, b->widthPx, b->heightPx);
    wl_surface_commit(surface_);
    b->busy = true;
    wl_display_flush(display_);
}

void WaylandWindow::drawDecorations(ShmBuffer& b) {
    cairo_surface_t* surface = cairo_image_surface_create_for_data(b.data, CAIRO_FORMAT_ARGB32, b.widthPx,
                                                                   b.heightPx, b.strideBytes);
    cairo_surface_set_device_scale(surface, scale_, scale_);
    cairo_t* cr = cairo_create(surface);

    const double w = surfW_;
    const double bar = kTitleBarHeight;
    const double ink = activated_ ? 1.0 : 0.6;   // dim everything when the window is inactive

    auto setColor = [&](double r, double g, double bl) { cairo_set_source_rgb(cr, r * ink, g * ink, bl * ink); };

    // Title bar background and window outline.
    setColor(0.14, 0.16, 0.21);
    cairo_rectangle(cr, 0, 0, w, bar);
    cairo_fill(cr);
    const int border = borderLogical();
    if (border > 0) {
        setColor(0.30, 0.33, 0.40);
        cairo_rectangle(cr, 0, bar, border, surfH_ - bar);                 // left
        cairo_rectangle(cr, w - border, bar, border, surfH_ - bar);        // right
        cairo_rectangle(cr, 0, surfH_ - border, w, border);                // bottom
        cairo_fill(cr);
    }

    // Buttons: minimize, maximize, close.
    const double right = w;
    auto button = [&](int indexFromRight, Zone zone, auto drawIcon) {
        const double x = right - kButtonWidth * (indexFromRight + 1);
        if (hoverZone_ == zone) {
            if (zone == Zone::CloseButton) cairo_set_source_rgb(cr, 0.78, 0.25, 0.25);
            else cairo_set_source_rgb(cr, 0.26, 0.29, 0.37);
            cairo_rectangle(cr, x, 0, kButtonWidth, bar);
            cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, 0.92 * ink, 0.93 * ink, 0.95 * ink);
        cairo_set_line_width(cr, 1.2);
        drawIcon(x + kButtonWidth / 2.0, bar / 2.0);
        cairo_stroke(cr);
    };
    button(0, Zone::CloseButton, [&](double cx, double cy) {
        cairo_move_to(cr, cx - 5, cy - 5); cairo_line_to(cr, cx + 5, cy + 5);
        cairo_move_to(cr, cx - 5, cy + 5); cairo_line_to(cr, cx + 5, cy - 5);
    });
    button(1, Zone::MaximizeButton, [&](double cx, double cy) {
        if (maximized_) {
            cairo_rectangle(cr, cx - 4.5, cy - 2.5, 7, 7);
            cairo_move_to(cr, cx - 2.5, cy - 2.5); cairo_line_to(cr, cx - 2.5, cy - 5.5);
            cairo_line_to(cr, cx + 5.5, cy - 5.5); cairo_line_to(cr, cx + 5.5, cy + 2.5);
            cairo_line_to(cr, cx + 2.5, cy + 2.5);
        } else {
            cairo_rectangle(cr, cx - 5, cy - 5, 10, 10);
        }
    });
    button(2, Zone::MinimizeButton, [&](double cx, double cy) {
        cairo_move_to(cr, cx - 5, cy + 4); cairo_line_to(cr, cx + 5, cy + 4);
    });

    // Title text
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(font, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, title_.c_str(), -1);
    pango_layout_set_width(layout, static_cast<int>(std::max(w - 3 * kButtonWidth - 20, 10.0)) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgb(cr, 0.92 * ink, 0.93 * ink, 0.95 * ink);
    cairo_move_to(cr, 12, (bar - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void WaylandWindow::setTitle(const std::string& title) {
    if (title == title_) return;
    title_ = title;
    xdg_toplevel_set_title(toplevel_, title_.c_str());
    if (csd_) push(RedrawEvent{});   // the title bar needs repainting
}

// ============================================================================
// Pointer and cursor
// ============================================================================
void WaylandWindow::loadCursorTheme() {
    if (!shm_ || cursorThemeScale_ == scale_) return;
    if (cursorTheme_) wl_cursor_theme_destroy(cursorTheme_);
    cursorTheme_ = wl_cursor_theme_load(nullptr, 24 * scale_, shm_);
    cursorThemeScale_ = scale_;
    appliedCursorName_.clear();
    applyCursor();
}

void WaylandWindow::applyCursor() {
    if (!pointer_ || !cursorTheme_ || !pointerInside_) return;

    std::vector<const char*> candidates;
    switch (hoverZone_) {
    case Zone::Top: candidates = {"top_side", "n-resize"}; break;
    case Zone::Bottom: candidates = {"bottom_side", "s-resize"}; break;
    case Zone::Left: candidates = {"left_side", "w-resize"}; break;
    case Zone::Right: candidates = {"right_side", "e-resize"}; break;
    case Zone::TopLeft: candidates = {"top_left_corner", "nw-resize"}; break;
    case Zone::TopRight: candidates = {"top_right_corner", "ne-resize"}; break;
    case Zone::BottomLeft: candidates = {"bottom_left_corner", "sw-resize"}; break;
    case Zone::BottomRight: candidates = {"bottom_right_corner", "se-resize"}; break;
    case Zone::Content:
        if (appCursor_ == ww::Cursor::IBeam) candidates = {"text", "xterm"};
        else if (appCursor_ == ww::Cursor::Hand) candidates = {"pointer", "hand2", "hand1"};
        break;
    default: break;
    }
    candidates.push_back("default");
    candidates.push_back("left_ptr");

    wl_cursor* cursor = nullptr;
    for (const char* name : candidates) {
        cursor = wl_cursor_theme_get_cursor(cursorTheme_, name);
        if (cursor) {
            if (appliedCursorName_ == name) return;   // already showing it
            appliedCursorName_ = name;
            break;
        }
    }
    if (!cursor) return;

    wl_cursor_image* image = cursor->images[0];
    wl_surface_set_buffer_scale(cursorSurface_, scale_);
    wl_surface_attach(cursorSurface_, wl_cursor_image_get_buffer(image), 0, 0);
    wl_surface_damage_buffer(cursorSurface_, 0, 0, static_cast<int32_t>(image->width),
                             static_cast<int32_t>(image->height));
    wl_surface_commit(cursorSurface_);
    wl_pointer_set_cursor(pointer_, pointerEnterSerial_, cursorSurface_,
                          static_cast<int32_t>(image->hotspot_x) / scale_,
                          static_cast<int32_t>(image->hotspot_y) / scale_);
}

void WaylandWindow::setCursor(ww::Cursor cursor) {
    if (cursor == appCursor_) return;
    appCursor_ = cursor;
    applyCursor();
}

void WaylandWindow::pointerMoved() {
    const Zone zone = pointerInside_ ? zoneAt(pointerX_, pointerY_) : Zone::Content;
    const bool decoChanged = (zone != hoverZone_) && (zone != Zone::Content || hoverZone_ != Zone::Content);
    const Zone previous = hoverZone_;
    hoverZone_ = zone;
    if (decoChanged) {
        applyCursor();
        const auto isButton = [](Zone z) {
            return z == Zone::CloseButton || z == Zone::MaximizeButton || z == Zone::MinimizeButton;
        };
        if (isButton(zone) || isButton(previous)) push(RedrawEvent{});   // hover highlight
    }

    const float cx = static_cast<float>(pointerX_ - borderLogical());
    const float cy = static_cast<float>(pointerY_ - titleLogical());
    const bool inContent = pointerInside_ && (zone == Zone::Content || contentGrabButtons_ > 0);
    const Modifiers mods = currentModifiers(xkbState_);
    if (inContent) {
        pointerInContent_ = true;
        push(MouseMoveEvent{cx, cy, mods});
    } else if (pointerInContent_) {
        pointerInContent_ = false;
        push(MouseLeaveEvent{});
    }
}

void WaylandWindow::pointerButton(uint32_t serial, uint32_t button, bool pressed) {
    const Zone zone = zoneAt(pointerX_, pointerY_);
    const Modifiers mods = currentModifiers(xkbState_);

    // Content (or a drag that started in the content and left it).
    if (zone == Zone::Content || contentGrabButtons_ > 0) {
        MouseButton b = MouseButton::Other;
        if (button == BTN_LEFT) b = MouseButton::Left;
        else if (button == BTN_MIDDLE) b = MouseButton::Middle;
        else if (button == BTN_RIGHT) b = MouseButton::Right;

        const float cx = static_cast<float>(pointerX_ - borderLogical());
        const float cy = static_cast<float>(pointerY_ - titleLogical());
        if (pressed) ++contentGrabButtons_;
        else if (contentGrabButtons_ > 0) --contentGrabButtons_;
        const int clicks = pressed ? clicks_.registerPress(b, cx, cy, SteadyClock::now()) : 0;
        push(MouseButtonEvent{b, pressed, cx, cy, mods, clicks});
        return;
    }

    // Decorations: only react to presses.
    if (!pressed) return;
    if (button == BTN_LEFT) {
        switch (zone) {
        case Zone::CloseButton: push(CloseEvent{}); break;
        case Zone::MaximizeButton:
            if (maximized_) xdg_toplevel_unset_maximized(toplevel_);
            else xdg_toplevel_set_maximized(toplevel_);
            break;
        case Zone::MinimizeButton: xdg_toplevel_set_minimized(toplevel_); break;
        case Zone::TitleBar:
            if (decoClicks_.registerPress(MouseButton::Left, static_cast<float>(pointerX_),
                                          static_cast<float>(pointerY_), SteadyClock::now()) == 2) {
                if (maximized_) xdg_toplevel_unset_maximized(toplevel_);
                else xdg_toplevel_set_maximized(toplevel_);
            } else {
                xdg_toplevel_move(toplevel_, seat_, serial);   // the compositor runs the drag
            }
            break;
        default: {
            xdg_toplevel_resize_edge edge = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
            switch (zone) {
            case Zone::Top: edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP; break;
            case Zone::Bottom: edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM; break;
            case Zone::Left: edge = XDG_TOPLEVEL_RESIZE_EDGE_LEFT; break;
            case Zone::Right: edge = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT; break;
            case Zone::TopLeft: edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT; break;
            case Zone::TopRight: edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT; break;
            case Zone::BottomLeft: edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; break;
            case Zone::BottomRight: edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; break;
            default: break;
            }
            if (edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE) xdg_toplevel_resize(toplevel_, seat_, serial, edge);
            break;
        }
        }
    } else if (button == BTN_RIGHT && zone == Zone::TitleBar) {
        xdg_toplevel_show_window_menu(toplevel_, seat_, serial, static_cast<int32_t>(pointerX_),
                                      static_cast<int32_t>(pointerY_));
    }
}

void WaylandWindow::flushScroll() {
    float d[2] = {0, 0};   // [0] vertical, [1] horizontal, in wheel notches
    for (int axis = 0; axis < 2; ++axis) {
        if (scrollHasDiscrete_[axis]) d[axis] = static_cast<float>(scrollDiscrete_[axis]);
        else d[axis] = static_cast<float>(scrollValue_[axis] / 10.0);   // smooth scrolling: ~10 units per notch
        scrollValue_[axis] = 0;
        scrollDiscrete_[axis] = 0;
        scrollHasDiscrete_[axis] = false;
    }
    if (d[0] == 0.0f && d[1] == 0.0f) return;
    if (zoneAt(pointerX_, pointerY_) != Zone::Content && contentGrabButtons_ == 0) return;
    push(ScrollEvent{d[1], d[0], static_cast<float>(pointerX_ - borderLogical()),
                     static_cast<float>(pointerY_ - titleLogical()), currentModifiers(xkbState_)});
}

// ============================================================================
// Clipboard
// ============================================================================
void WaylandWindow::dataOfferAdded(wl_data_offer* offer) {
    offers_[offer] = DataOffer{offer, {}};
    wl_data_offer_add_listener(offer, &kDataOfferListener, this);
}

void WaylandWindow::freeOffer(wl_data_offer* offer) {
    auto it = offers_.find(offer);
    if (it == offers_.end()) return;
    wl_data_offer_destroy(offer);
    offers_.erase(it);
    if (selectionOffer_ == offer) selectionOffer_ = nullptr;
}

void WaylandWindow::selectionChanged(wl_data_offer* offer) {
    if (selectionOffer_ && selectionOffer_ != offer) freeOffer(selectionOffer_);
    selectionOffer_ = offer;   // nullptr when the clipboard is empty
}

void WaylandWindow::setClipboardText(const std::string& utf8) {
    if (!dataDeviceManager_ || !dataDevice_) return;
    clipboardOwn_ = utf8;
    if (ownSource_) {
        wl_data_source_destroy(ownSource_);
        ownSource_ = nullptr;
    }
    ownSource_ = wl_data_device_manager_create_data_source(dataDeviceManager_);
    wl_data_source_add_listener(ownSource_, &kDataSourceListener, this);
    wl_data_source_offer(ownSource_, "text/plain;charset=utf-8");
    wl_data_source_offer(ownSource_, "text/plain");
    wl_data_source_offer(ownSource_, "UTF8_STRING");
    // The serial proves that a real user action (key press, click) triggered the copy.
    wl_data_device_set_selection(dataDevice_, ownSource_, lastSerial_);
    wl_display_flush(display_);
}

std::string WaylandWindow::clipboardText() {
    if (ownSource_) return clipboardOwn_;
    auto it = offers_.find(selectionOffer_);
    if (!selectionOffer_ || it == offers_.end()) return {};

    const char* wanted[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT", "STRING"};
    const char* mime = nullptr;
    for (const char* candidate : wanted) {
        if (std::find(it->second.mimeTypes.begin(), it->second.mimeTypes.end(), candidate) != it->second.mimeTypes.end()) {
            mime = candidate;
            break;
        }
    }
    if (!mime) return {};

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) return {};
    wl_data_offer_receive(selectionOffer_, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(display_);

    std::string result;
    const auto deadline = SteadyClock::now() + std::chrono::milliseconds(500);
    char buffer[4096];
    for (;;) {
        pollfd pfd{fds[0], POLLIN, 0};
        const int wait = msUntil(deadline);
        if (wait == 0 || poll(&pfd, 1, wait) <= 0) break;
        const ssize_t n = read(fds[0], buffer, sizeof buffer);
        if (n <= 0) break;   // 0 = the other program closed the pipe: transfer complete
        result.append(buffer, static_cast<size_t>(n));
        if (result.size() > (16u << 20)) break;
    }
    close(fds[0]);
    return result;
}

} // namespace

std::unique_ptr<Window> createWaylandWindow(const WindowConfig& config) {
    auto window = std::make_unique<WaylandWindow>();
    if (!window->init(config)) return nullptr;
    return window;
}

} // namespace ww::detail
