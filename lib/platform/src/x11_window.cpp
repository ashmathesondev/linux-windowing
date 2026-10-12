// X11 backend (Xlib + MIT-SHM + XKB + selections for the clipboard).
//
// Stages 1 to 3 explain each piece in isolation; this file combines them behind
// the Window interface and adds: HiDPI scale detection, XKB state tracking with
// detectable auto-repeat, and clipboard support through X selections.

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <poll.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xkbcommon/xkbcommon-x11.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <set>

#include "key_translate.hpp"
#include "window_base.hpp"

namespace ww::detail {

namespace {

bool g_shmAttachFailed = false;
int onXError(Display*, XErrorEvent*) {
    g_shmAttachFailed = true;
    return 0;
}

// Pixels the X server can display: a SHM segment when possible, malloc'd memory otherwise.
class XFramebuffer {
public:
    XFramebuffer(Display* dpy, Visual* visual, int depth, int width, int height, bool tryShm)
        : dpy_(dpy), width_(width), height_(height) {
        if (tryShm && createShm(visual, depth)) {
            shm_ok_ = true;
        } else if (!createPlain(visual, depth)) {
            std::fprintf(stderr, "ww: cannot allocate a %dx%d framebuffer\n", width, height);
            std::abort();
        }
        strideBytes_ = image_->bytes_per_line;
    }

    ~XFramebuffer() {
        if (shm_ok_) {
            XShmDetach(dpy_, &info_);
            XSync(dpy_, False);
            XDestroyImage(image_);
            shmdt(info_.shmaddr);
        } else if (image_) {
            XDestroyImage(image_);
        }
    }
    XFramebuffer(const XFramebuffer&) = delete;
    XFramebuffer& operator=(const XFramebuffer&) = delete;

    uint32_t* pixels() { return reinterpret_cast<uint32_t*>(image_->data); }
    int width() const { return width_; }
    int height() const { return height_; }
    int strideBytes() const { return strideBytes_; }
    bool usesShm() const { return shm_ok_; }

    // Returns true if a ShmCompletion event will follow.
    bool put(::Window win, GC gc) {
        if (shm_ok_) {
            XShmPutImage(dpy_, win, gc, image_, 0, 0, 0, 0, static_cast<unsigned>(width_),
                         static_cast<unsigned>(height_), True);
            XFlush(dpy_);
            return true;
        }
        XPutImage(dpy_, win, gc, image_, 0, 0, 0, 0, static_cast<unsigned>(width_), static_cast<unsigned>(height_));
        XFlush(dpy_);
        return false;
    }

private:
    bool createShm(Visual* visual, int depth) {
        if (!XShmQueryExtension(dpy_)) return false;
        image_ = XShmCreateImage(dpy_, visual, static_cast<unsigned>(depth), ZPixmap, nullptr, &info_,
                                 static_cast<unsigned>(width_), static_cast<unsigned>(height_));
        if (!image_) return false;
        info_.shmid = shmget(IPC_PRIVATE, static_cast<size_t>(image_->bytes_per_line) * image_->height,
                             IPC_CREAT | 0600);
        if (info_.shmid < 0) {
            XDestroyImage(image_);
            image_ = nullptr;
            return false;
        }
        info_.shmaddr = image_->data = static_cast<char*>(shmat(info_.shmid, nullptr, 0));
        info_.readOnly = False;
        shmctl(info_.shmid, IPC_RMID, nullptr);   // freed automatically when the last user detaches
        if (info_.shmaddr == reinterpret_cast<char*>(-1)) {
            XDestroyImage(image_);
            image_ = nullptr;
            return false;
        }
        g_shmAttachFailed = false;
        auto* old = XSetErrorHandler(onXError);
        XShmAttach(dpy_, &info_);
        XSync(dpy_, False);
        XSetErrorHandler(old);
        if (g_shmAttachFailed) {
            shmdt(info_.shmaddr);
            XDestroyImage(image_);
            image_ = nullptr;
            return false;
        }
        return true;
    }

    bool createPlain(Visual* visual, int depth) {
        auto* data = static_cast<char*>(std::calloc(static_cast<size_t>(width_) * static_cast<size_t>(height_), 4));
        if (!data) return false;
        image_ = XCreateImage(dpy_, visual, static_cast<unsigned>(depth), ZPixmap, 0, data,
                              static_cast<unsigned>(width_), static_cast<unsigned>(height_), 32, 0);
        if (!image_) {
            std::free(data);
            return false;
        }
        return true;
    }

    Display* dpy_;
    int width_;
    int height_;
    XImage* image_ = nullptr;
    XShmSegmentInfo info_{};
    bool shm_ok_ = false;
    int strideBytes_ = 0;
};

Modifiers modsFromX(unsigned state) {
    Modifiers m;
    m.shift = state & ShiftMask;
    m.ctrl = state & ControlMask;
    m.alt = state & Mod1Mask;
    m.super = state & Mod4Mask;
    m.capsLock = state & LockMask;
    return m;
}

class X11Window final : public WindowBase {
public:
    ~X11Window() override {
        framebuffer_.reset();   // must go before the display connection
        if (xkbState_) xkb_state_unref(xkbState_);
        if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
        if (xkbContext_) xkb_context_unref(xkbContext_);
        if (dpy_) {
            for (::Cursor c : cursors_) {
                if (c) XFreeCursor(dpy_, c);
            }
            if (gc_) XFreeGC(dpy_, gc_);
            if (win_) XDestroyWindow(dpy_, win_);
            XCloseDisplay(dpy_);
        }
    }

    bool init(const WindowConfig& config) {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return false;

        const int screen = DefaultScreen(dpy_);
        visual_ = DefaultVisual(dpy_, screen);
        depth_ = DefaultDepth(dpy_, screen);
        if (visual_->red_mask != 0xff0000 || visual_->green_mask != 0x00ff00 || visual_->blue_mask != 0x0000ff ||
            depth_ < 24) {
            std::fprintf(stderr, "ww: unsupported X11 visual (need 24 bit TrueColor 0xRRGGBB)\n");
            return false;
        }

        scale_ = detectScale();
        widthPx_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(config.width) * scale_)));
        heightPx_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(config.height) * scale_)));

        XSetWindowAttributes attrs{};
        attrs.background_pixmap = None;   // we paint every pixel; no flicker on resize
        attrs.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                           ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask |
                           FocusChangeMask | PropertyChangeMask;
        win_ = XCreateWindow(dpy_, RootWindow(dpy_, screen), 0, 0, static_cast<unsigned>(widthPx_),
                             static_cast<unsigned>(heightPx_), 0, depth_, InputOutput, visual_,
                             CWBackPixmap | CWEventMask, &attrs);
        gc_ = XCreateGC(dpy_, win_, 0, nullptr);

        internAtoms();
        setTitle(config.title);

        XSizeHints hints{};
        hints.flags = PMinSize;
        hints.min_width = static_cast<int>(static_cast<float>(config.minWidth) * scale_);
        hints.min_height = static_cast<int>(static_cast<float>(config.minHeight) * scale_);
        XSetWMNormalHints(dpy_, win_, &hints);

        XClassHint classHint;
        std::string res = config.appId;
        classHint.res_name = res.data();
        classHint.res_class = res.data();
        XSetClassHint(dpy_, win_, &classHint);
        XSetWMProtocols(dpy_, win_, &atomWmDelete_, 1);

        if (!initKeyboard()) {
            std::fprintf(stderr, "ww: could not initialise XKB keyboard support\n");
            return false;
        }

        const char* display = std::getenv("DISPLAY");
        const bool local = display && display[0] == ':';
        tryShm_ = local;
        shmEventBase_ = XShmGetEventBase(dpy_);

        cursors_[0] = XCreateFontCursor(dpy_, XC_left_ptr);
        cursors_[1] = XCreateFontCursor(dpy_, XC_xterm);
        cursors_[2] = XCreateFontCursor(dpy_, XC_hand2);

        XMapWindow(dpy_, win_);
        XFlush(dpy_);

        // Let the application learn size and scale through the normal event path.
        push(ResizeEvent{widthPx_, heightPx_, scale_});
        push(RedrawEvent{});
        return true;
    }

    Backend backend() const override { return Backend::X11; }
    int widthPx() const override { return widthPx_; }
    int heightPx() const override { return heightPx_; }
    float scale() const override { return scale_; }

    // ---- events ----------------------------------------------------------
    bool waitEvent(Event& out, int timeoutMs) override {
        const auto deadline = SteadyClock::now() + std::chrono::milliseconds(std::max(timeoutMs, 0));
        for (;;) {
            if (pop(out)) return true;
            drainXEvents();
            if (pop(out)) return true;
            if (timeoutMs == 0) return false;

            int wait = -1;
            if (timeoutMs > 0) {
                wait = msUntil(deadline);
                if (wait == 0) return false;
            }
            pollfd pfd{ConnectionNumber(dpy_), POLLIN, 0};
            poll(&pfd, 1, wait);
        }
    }

    // ---- drawing ---------------------------------------------------------
    Framebuffer acquireFramebuffer() override {
        // With SHM the server may still be reading the previous frame.
        pumpUntil([this] { return !shmInFlight_; }, 100);
        shmInFlight_ = false;

        if (!framebuffer_ || framebuffer_->width() != widthPx_ || framebuffer_->height() != heightPx_) {
            framebuffer_.reset();
            framebuffer_ = std::make_unique<XFramebuffer>(dpy_, visual_, depth_, widthPx_, heightPx_, tryShm_);
        }
        return Framebuffer{framebuffer_->pixels(), widthPx_, heightPx_, framebuffer_->strideBytes(), scale_};
    }

    void present() override {
        if (!framebuffer_) return;
        shmInFlight_ = framebuffer_->put(win_, gc_);
    }

    void setTitle(const std::string& title) override {
        XStoreName(dpy_, win_, title.c_str());
        XChangeProperty(dpy_, win_, atomNetWmName_, atomUtf8_, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(title.data()), static_cast<int>(title.size()));
        XFlush(dpy_);
    }

    void setCursor(ww::Cursor cursor) override {
        if (cursor == currentCursor_) return;
        currentCursor_ = cursor;
        XDefineCursor(dpy_, win_, cursors_[static_cast<size_t>(cursor)]);
        XFlush(dpy_);
    }

    // ---- clipboard (X selections) ---------------------------------------------
    void setClipboardText(const std::string& utf8) override {
        clipboardOwn_ = utf8;
        XSetSelectionOwner(dpy_, atomClipboard_, win_, CurrentTime);
        ownsClipboard_ = XGetSelectionOwner(dpy_, atomClipboard_) == win_;
        XFlush(dpy_);
    }

    std::string clipboardText() override {
        if (ownsClipboard_) return clipboardOwn_;
        if (XGetSelectionOwner(dpy_, atomClipboard_) == None) return {};

        XDeleteProperty(dpy_, win_, atomWwSelection_);
        selectionState_ = SelectionState::Waiting;
        XConvertSelection(dpy_, atomClipboard_, atomUtf8_, atomWwSelection_, win_, CurrentTime);
        XFlush(dpy_);
        pumpUntil([this] { return selectionState_ != SelectionState::Waiting; }, 500);
        if (selectionState_ != SelectionState::Received) return {};

        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;
        std::string result;
        if (XGetWindowProperty(dpy_, win_, atomWwSelection_, 0, LONG_MAX / 4, True, AnyPropertyType, &type, &format,
                               &count, &remaining, &data) == Success &&
            data) {
            // type == INCR means "large transfer in chunks": not supported here.
            if (type != atomIncr_ && format == 8) result.assign(reinterpret_cast<char*>(data), count);
            XFree(data);
        }
        return result;
    }

private:
    enum class SelectionState { Idle, Waiting, Received, Failed };

    void internAtoms() {
        atomWmDelete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
        atomNetWmName_ = XInternAtom(dpy_, "_NET_WM_NAME", False);
        atomUtf8_ = XInternAtom(dpy_, "UTF8_STRING", False);
        atomClipboard_ = XInternAtom(dpy_, "CLIPBOARD", False);
        atomTargets_ = XInternAtom(dpy_, "TARGETS", False);
        atomIncr_ = XInternAtom(dpy_, "INCR", False);
        atomWwSelection_ = XInternAtom(dpy_, "WW_SELECTION", False);
    }

    float detectScale() {
        if (const float forced = scaleFromEnvironment(); forced > 0.0f) return forced;
        double dpi = 96.0;
        if (const char* rm = XResourceManagerString(dpy_)) {
            XrmInitialize();
            if (XrmDatabase db = XrmGetStringDatabase(rm)) {
                char* type = nullptr;
                XrmValue value;
                if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
                    const double v = std::atof(value.addr);
                    if (v >= 48.0 && v <= 768.0) dpi = v;
                }
                XrmDestroyDatabase(db);
            }
        }
        const double s = std::clamp(dpi / 96.0, 1.0, 4.0);
        return static_cast<float>(std::round(s * 4.0) / 4.0);   // 1.0, 1.25, 1.5, ...
    }

    // XKB: ask the server for the user's keymap, and listen for state/layout changes.
    bool initKeyboard() {
        int opcode, errorBase, major = XkbMajorVersion, minor = XkbMinorVersion;
        if (!XkbQueryExtension(dpy_, &opcode, &xkbEventBase_, &errorBase, &major, &minor)) return false;

        // With detectable auto-repeat the server sends repeated KeyPress events
        // without KeyRelease in between, so a repeat is simply "pressed while already down".
        Bool supported = False;
        XkbSetDetectableAutoRepeat(dpy_, True, &supported);

        XkbSelectEvents(dpy_, XkbUseCoreKbd, XkbNewKeyboardNotifyMask | XkbStateNotifyMask,
                        XkbNewKeyboardNotifyMask | XkbStateNotifyMask);

        xcb_connection_t* conn = XGetXCBConnection(dpy_);
        if (!conn) return false;
        if (!xkb_x11_setup_xkb_extension(conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                         XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr, nullptr, nullptr)) {
            return false;
        }
        xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        return xkbContext_ && rebuildKeymap();
    }

    bool rebuildKeymap() {
        xcb_connection_t* conn = XGetXCBConnection(dpy_);
        const int32_t device = xkb_x11_get_core_keyboard_device_id(conn);
        if (device < 0) return false;
        xkb_keymap* keymap = xkb_x11_keymap_new_from_device(xkbContext_, conn, device, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) return false;
        xkb_state* state = xkb_x11_state_new_from_device(keymap, conn, device);
        if (!state) {
            xkb_keymap_unref(keymap);
            return false;
        }
        if (xkbState_) xkb_state_unref(xkbState_);
        if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
        xkbKeymap_ = keymap;
        xkbState_ = state;
        return true;
    }

    void drainXEvents() {
        while (XPending(dpy_)) {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            handleXEvent(ev);
        }
    }

    template <typename Pred>
    bool pumpUntil(Pred done, int timeoutMs) {
        const auto deadline = SteadyClock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            drainXEvents();
            if (done()) return true;
            const int wait = msUntil(deadline);
            if (wait == 0) return false;
            pollfd pfd{ConnectionNumber(dpy_), POLLIN, 0};
            poll(&pfd, 1, wait);
        }
    }

    void setSize(int w, int h) {
        if (w == widthPx_ && h == heightPx_) return;
        widthPx_ = std::max(w, 1);
        heightPx_ = std::max(h, 1);
        push(ResizeEvent{widthPx_, heightPx_, scale_});
        push(RedrawEvent{});
    }

    void handleXEvent(XEvent& ev) {
        if (ev.type == shmEventBase_ + ShmCompletion) {
            shmInFlight_ = false;
            return;
        }
        if (ev.type == xkbEventBase_) {
            handleXkbEvent(*reinterpret_cast<XkbEvent*>(&ev));
            return;
        }

        switch (ev.type) {
        case ClientMessage:
            if (static_cast<Atom>(ev.xclient.data.l[0]) == atomWmDelete_) push(CloseEvent{});
            break;

        case ConfigureNotify:
            setSize(ev.xconfigure.width, ev.xconfigure.height);
            break;

        case Expose:
            if (ev.xexpose.count == 0) push(RedrawEvent{});
            break;

        case FocusIn:
            push(FocusEvent{true});
            break;
        case FocusOut:
            pressedKeys_.clear();
            push(FocusEvent{false});
            break;

        case KeyPress:
        case KeyRelease:
            handleKey(ev.xkey);
            break;

        case ButtonPress:
        case ButtonRelease:
            handleButton(ev.xbutton);
            break;

        case MotionNotify:
            push(MouseMoveEvent{static_cast<float>(ev.xmotion.x) / scale_, static_cast<float>(ev.xmotion.y) / scale_,
                                modsFromX(ev.xmotion.state)});
            break;
        case EnterNotify:
            push(MouseMoveEvent{static_cast<float>(ev.xcrossing.x) / scale_,
                                static_cast<float>(ev.xcrossing.y) / scale_, modsFromX(ev.xcrossing.state)});
            break;
        case LeaveNotify:
            if (ev.xcrossing.mode == NotifyNormal || ev.xcrossing.mode == NotifyUngrab) push(MouseLeaveEvent{});
            break;

        case SelectionRequest:
            answerSelectionRequest(ev.xselectionrequest);
            break;
        case SelectionClear:
            ownsClipboard_ = false;
            break;
        case SelectionNotify:
            selectionState_ = ev.xselection.property == None ? SelectionState::Failed : SelectionState::Received;
            break;

        default:
            break;
        }
    }

    void handleXkbEvent(const XkbEvent& xe) {
        switch (xe.any.xkb_type) {
        case XkbStateNotify:
            if (xkbState_) {
                xkb_state_update_mask(xkbState_, xe.state.base_mods, xe.state.latched_mods, xe.state.locked_mods,
                                      static_cast<xkb_layout_index_t>(xe.state.base_group),
                                      static_cast<xkb_layout_index_t>(xe.state.latched_group),
                                      static_cast<xkb_layout_index_t>(xe.state.locked_group));
            }
            break;
        case XkbNewKeyboardNotify:   // the user switched keyboard or loaded a new layout
            rebuildKeymap();
            break;
        default:
            break;
        }
    }

    void handleKey(const XKeyEvent& e) {
        const bool pressed = e.type == KeyPress;
        const xkb_keycode_t code = e.keycode;
        bool repeat = false;
        if (pressed) {
            repeat = !pressedKeys_.insert(code).second;
        } else {
            pressedKeys_.erase(code);
        }

        const KeyInfo info = translateKey(xkbKeymap_, xkbState_, code, pressed);
        const Modifiers mods = currentModifiers(xkbState_);
        push(KeyEvent{info.key, pressed, repeat, mods});
        if (pressed && !info.text.empty()) push(TextEvent{info.text});
    }

    void handleButton(const XButtonEvent& e) {
        const Modifiers mods = modsFromX(e.state);
        const float x = static_cast<float>(e.x) / scale_;
        const float y = static_cast<float>(e.y) / scale_;
        const bool pressed = e.type == ButtonPress;

        if (e.button >= 4 && e.button <= 7) {   // wheel
            if (!pressed) return;
            float dx = 0, dy = 0;
            if (e.button == 4) dy = -1.0f;
            if (e.button == 5) dy = 1.0f;
            if (e.button == 6) dx = -1.0f;
            if (e.button == 7) dx = 1.0f;
            push(ScrollEvent{dx, dy, x, y, mods});
            return;
        }

        MouseButton button = MouseButton::Other;
        if (e.button == Button1) button = MouseButton::Left;
        else if (e.button == Button2) button = MouseButton::Middle;
        else if (e.button == Button3) button = MouseButton::Right;

        const int clicks = pressed ? clicks_.registerPress(button, x, y, SteadyClock::now()) : 0;
        push(MouseButtonEvent{button, pressed, x, y, mods, clicks});
    }

    void answerSelectionRequest(const XSelectionRequestEvent& req) {
        XSelectionEvent reply{};
        reply.type = SelectionNotify;
        reply.display = req.display;
        reply.requestor = req.requestor;
        reply.selection = req.selection;
        reply.target = req.target;
        reply.time = req.time;
        reply.property = None;

        const Atom property = req.property != None ? req.property : req.target;   // old clients omit it
        if (req.selection == atomClipboard_ && ownsClipboard_) {
            if (req.target == atomTargets_) {
                const Atom targets[] = {atomTargets_, atomUtf8_};
                XChangeProperty(dpy_, req.requestor, property, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(targets), 2);
                reply.property = property;
            } else if (req.target == atomUtf8_) {
                XChangeProperty(dpy_, req.requestor, property, atomUtf8_, 8, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(clipboardOwn_.data()),
                                static_cast<int>(clipboardOwn_.size()));
                reply.property = property;
            }
        }
        XSendEvent(dpy_, req.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&reply));
        XFlush(dpy_);
    }

    Display* dpy_ = nullptr;
    ::Window win_ = 0;
    GC gc_ = nullptr;
    Visual* visual_ = nullptr;
    int depth_ = 0;
    int widthPx_ = 0;
    int heightPx_ = 0;
    float scale_ = 1.0f;

    std::unique_ptr<XFramebuffer> framebuffer_;
    bool tryShm_ = false;
    int shmEventBase_ = 0;
    bool shmInFlight_ = false;

    xkb_context* xkbContext_ = nullptr;
    xkb_keymap* xkbKeymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;
    int xkbEventBase_ = 0;
    std::set<xkb_keycode_t> pressedKeys_;

    ::Cursor cursors_[3] = {0, 0, 0};
    ww::Cursor currentCursor_ = static_cast<ww::Cursor>(-1);

    Atom atomWmDelete_ = None, atomNetWmName_ = None, atomUtf8_ = None, atomClipboard_ = None;
    Atom atomTargets_ = None, atomIncr_ = None, atomWwSelection_ = None;
    std::string clipboardOwn_;
    bool ownsClipboard_ = false;
    SelectionState selectionState_ = SelectionState::Idle;
};

} // namespace

std::unique_ptr<Window> createX11Window(const WindowConfig& config) {
    auto window = std::make_unique<X11Window>();
    if (!window->init(config)) return nullptr;
    return window;
}

} // namespace ww::detail
