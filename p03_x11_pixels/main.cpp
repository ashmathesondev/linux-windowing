#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/keysym.h>
#include <poll.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>


namespace {
    bool g_x11ErrorSeen = false;

    int onXError(Display*, XErrorEvent*)
    {
        g_x11ErrorSeen = true;
        return 0;
    }

    class Framebuffer
    {
    public:
        Framebuffer() = delete;
        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        Framebuffer(Display* display, Visual* visual, int depth, int width, int height, bool tryShm)
            : m_display(display), m_width(width), m_height(height) 
        {
            if (tryShm && createShm(visual, depth))
            {
                m_usingShm = true;
            }
            else if (createPlain(visual, depth))
            {
                fprintf(stderr, "Using plain XImage and could not allocate framebuffer\n");
                std::exit(EXIT_FAILURE);
            }
            m_pixels = reinterpret_cast<uint32_t*>(m_image->data);
            m_strideInPixels = m_image->bytes_per_line / sizeof(uint32_t);
        }

        ~Framebuffer() 
        {
            XShmDetach(m_display, &m_shmInfo);
            shmdt(m_shmInfo.shmaddr);
            shmctl(m_shmInfo.shmid, IPC_RMID, nullptr);
            XDestroyImage(m_image);
        }

        bool Present(Window window, GC gc, int x, int y) 
        {
            if (m_usingShm) 
            {
                g_x11ErrorSeen = false;
                auto* oldHandler = XSetErrorHandler(onXError);
                XShmPutImage(m_display, window, gc, m_image, 0, 0, x, y, m_width, m_height, False);
                XSync(m_display, False);
                XSetErrorHandler(oldHandler);
                return !g_x11ErrorSeen;
            } 
            else 
            {
                XPutImage(m_display, window, gc, m_image, 0, 0, x, y, m_width, m_height);
                XFlush(m_display);
                return true;
            }
        }

        int width() const { return m_width; }
        int height() const { return m_height; }
        int stride() const { return m_strideInPixels; }
        uint32_t* pixels() const { return reinterpret_cast<uint32_t*>(m_pixels); }
        bool usingShm() const { return m_usingShm; }

    private:

        bool createShm(Visual* visual, int depth) 
        {
            if (!XShmQueryExtension(m_display)) {
                std::fprintf(stderr, "X server does not support XShm extension\n");
                return false;
            }

            m_image = XShmCreateImage(m_display, visual, depth, ZPixmap, nullptr, &m_shmInfo, m_width, m_height);
            if (!m_image) 
            {
                std::fprintf(stderr, "Failed to create shared memory image\n");
                return false;
            }

            m_shmInfo.shmid = shmget(IPC_PRIVATE, m_image->bytes_per_line * m_image->height,
                                     IPC_CREAT | 0600);
            if (m_shmInfo.shmid < 0)
            {
                std::fprintf(stderr, "Failed to allocate shared memory segment\n");
                XDestroyImage(m_image);
                m_image = nullptr;
                return false;
            }

            m_shmInfo.shmaddr = m_image->data = static_cast<char*>(shmat(m_shmInfo.shmid, nullptr, 0));
            m_shmInfo.readOnly = False;

            // Mark the segment for deletion now, it'll live until the last process detaches from it
            shmctl(m_shmInfo.shmid, IPC_RMID, nullptr);

            if (m_shmInfo.shmaddr == reinterpret_cast<char*>(-1))
            {
                std::fprintf(stderr, "Failed to attach shared memory segment\n");
                XDestroyImage(m_image);
                m_image = nullptr;
                return false;
            }

            g_x11ErrorSeen = false;
            auto* oldHandler = XSetErrorHandler(onXError);
            XShmAttach(m_display, &m_shmInfo);
            XSync(m_display, False);
            XSetErrorHandler(oldHandler);
            if (g_x11ErrorSeen) 
            {
                std::fprintf(stderr, "X server does not support shared memory\n");
                shmdt(m_shmInfo.shmaddr);
                XDestroyImage(m_image);
                m_image = nullptr;
                return false;
            }

            return true;
        }

        bool createPlain(Visual* visual, int depth) 
        {
            m_image = XCreateImage(m_display, visual, depth, ZPixmap, 0, nullptr, m_width, m_height, 32, 0);
            if (!m_image) 
            {
                std::fprintf(stderr, "Failed to create plain XImage\n");
                return false;
            }

            m_image->data = static_cast<char*>(std::malloc(m_image->bytes_per_line * m_image->height));
            if (!m_image->data) 
            {
                std::fprintf(stderr, "Failed to allocate memory for plain XImage\n");
                XDestroyImage(m_image);
                m_image = nullptr;
                return false;
            }

            return true;
        }   

        Display* m_display;
        int m_width;
        int m_height;
        XImage* m_image;
        XShmSegmentInfo m_shmInfo;
        bool m_usingShm = false;
        int m_strideInPixels = 0;
        uint32_t* m_pixels = nullptr;
    };
}

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) 
{
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

void fillRectangle(Framebuffer& framebuffer, int x, int y, int width, int height, uint32_t color) 
{
    for (int j = 0; j < height; ++j) 
    {
        for (int i = 0; i < width; ++i) 
        {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < framebuffer.width() && py >= 0 && py < framebuffer.height()) 
            {
                framebuffer.pixels()[py * framebuffer.stride() + px] = color;
            }
        }
    }
}

void fillGradient(Framebuffer& framebuffer) 
{
    for (int y = 0; y < framebuffer.height(); ++y) 
    {
        for (int x = 0; x < framebuffer.width(); ++x) 
        {
            uint8_t r = static_cast<uint8_t>((x * 255) / framebuffer.width());
            uint8_t g = static_cast<uint8_t>((y * 255) / framebuffer.height());
            uint8_t b = 128;
            framebuffer.pixels()[y * framebuffer.stride() + x] = rgb(r, g, b);
        }
    }
}

void drawCircle(Framebuffer& framebuffer, int centerX, int centerY, int radius, uint32_t color) 
{
    for (int y = -radius; y <= radius; ++y) 
    {
        for (int x = -radius; x <= radius; ++x) 
        {
            if (x * x + y * y <= radius * radius) 
            {
                int px = centerX + x;
                int py = centerY + y;
                if (px >= 0 && px < framebuffer.width() && py >= 0 && py < framebuffer.height()) 
                {
                    framebuffer.pixels()[py * framebuffer.stride() + px] = color;
                }
            }
        }
    }
}

void drawGradientCircle(Framebuffer& framebuffer, int centerX, int centerY, int radius) 
{
    for (int y = -radius; y <= radius; ++y) 
    {
        for (int x = -radius; x <= radius; ++x) 
        {
            if (x * x + y * y <= radius * radius) 
            {
                int px = centerX + x;
                int py = centerY + y;
                if (px >= 0 && px < framebuffer.width() && py >= 0 && py < framebuffer.height()) 
                {
                    uint8_t r = static_cast<uint8_t>((px * 255) / framebuffer.width());
                    uint8_t g = static_cast<uint8_t>((py * 255) / framebuffer.height());
                    uint8_t b = 128;
                    framebuffer.pixels()[py * framebuffer.stride() + px] = rgb(r, g, b);
                }
            }
        }
    }
}   

void drawFrame(Framebuffer& framebuffer, double seconds, int mouseX, int mouseY, uint32_t color, bool mouseInside)
{
    const int w = framebuffer.width();
    const int h = framebuffer.height();

    const unsigned phase = static_cast<unsigned>(seconds * 60.0);
    // Draw top and bottom borders
    for (int y = 0; y < h; ++y) 
    {
        uint32_t* row = framebuffer.pixels() + static_cast<size_t>(y) * framebuffer.stride();
        const unsigned g = static_cast<unsigned>(y * 255 / std::max(1, h - 1));
        for (int x = 0; x < w; ++x) 
        {
            const unsigned r = static_cast<unsigned>(x * 255 / std::max(1, w - 1));
            const unsigned b = phase + static_cast<unsigned>((x + y)/4) % 0xff;
            row[x] = rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
        }

        const int bar = static_cast<int>(std::sin(seconds * 2.0) * 0.5 * w + 0.5 * (w - 80));
        fillRectangle(framebuffer, bar, h - 24, 80, 12, rgb(255, 255, 255));

        if (mouseInside) 
        {
            fillRectangle(framebuffer, mouseX - 22, mouseY - 22, 44, 44, rgb(0, 0, 0));
            fillRectangle(framebuffer, mouseX - 20, mouseY - 20, 40, 40, rgb(255, 255, 255));
        }
    }
}

int main() 
{

    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        fprintf(stderr, "Failed to open X display\n");
        return EXIT_FAILURE;
    }

    const int screen = DefaultScreen(display);
    Visual* visual = DefaultVisual(display, screen);
    const int depth = DefaultDepth(display, screen);

    if (visual->red_mask != 0xff0000 || visual->green_mask != 0x00ff00 || visual->blue_mask != 0x0000ff || depth != 24) 
    {
        fprintf(stderr, "Unsupported visual format. Expected 24-bit RGB.\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    int width = 800;
    int height = 600;
    const unsigned long background_color = 0x2b3a55; // Dark blue color in hexadecimal

    Window win = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 
        static_cast<unsigned>(width), static_cast<unsigned>(height), 
        0, 0, background_color);

    XStoreName(display, win, "Stage 3: X11 Pixels Example");

    // for repainting the window, we need to select the ExposureMask event
    XSetWindowBackgroundPixmap(display, win, None);

    Atom wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, win, &wmDeleteWindow, 1);
    XSelectInput(display, win, StructureNotifyMask | ExposureMask | 
        KeyPressMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask);

    GC gc = XCreateGC(display, win, 0, nullptr);
    XMapWindow(display, win);

    const bool localDisplay = std::getenv("DISPLAY") == nullptr || std::getenv("DISPLAY")[0] == ':';
    const int shmEventBase = XShmGetEventBase(display);

    auto* framebuffer = new Framebuffer(display, visual, depth, width, height, localDisplay);

    std::printf("Transfer method: %s\n", framebuffer->usingShm() ? "MIT-SHM" : "XPutImage");

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto nextFrame = start;
    constexpr auto frameTime = std::chrono::microseconds(16667); // ~60 FPS

    int mouseX = 0;
    int mouseY = 0;

    bool mouseInside = false;
    bool inFlight = false; // SHM: Server may not have processed the previous frame yet
    int pendingW = 0;
    int pendingH = 0;
    bool running = true;

    while (running) 
    {
        while (XPending(display) > 0) 
        {
            XEvent event;
            XNextEvent(display, &event);

            if (event.type == shmEventBase + ShmCompletion) 
            {
                inFlight = false;
                continue;
            }

            switch (event.type) 
            {
                case ClientMessage:
                    if (static_cast<Atom>(event.xclient.data.l[0]) == wmDeleteWindow) 
                    {
                        running = false; // Exit on window close
                    }
                    break;

                case KeyPress:
                    if (XLookupKeysym(&event.xkey, 0) == XK_Escape) 
                    {
                        running = false; // Exit on Escape key
                    }
                    break;
                case ConfigureNotify:
                    // Handle window resize event
                    if (event.xconfigure.width != framebuffer->width() || event.xconfigure.height != framebuffer->height()) 
                    {
                        pendingW = event.xconfigure.width;
                        pendingH = event.xconfigure.height;
                    }
                    break;
                case MotionNotify:
                    mouseX = event.xmotion.x;
                    mouseY = event.xmotion.y;
                    mouseInside = true;
                    break;
                case EnterNotify:
                    mouseInside = true;
                    break;
                case LeaveNotify:
                    mouseInside = false;
                    break;
                case Expose:
                        // Handle window expose event (redraw)
                        break;
                default:
                    break;
            }
        }

        // Apply a resize once the old bvuffer is no longer in use.
        if (pendingW > 0 && !inFlight) 
        {
            width = pendingW;
            height = pendingH;
            delete framebuffer;
            framebuffer = new Framebuffer(display, visual, depth, width, height, localDisplay);
            pendingW = 0;
            pendingH = 0;
        }

        const auto now = Clock::now();
        if (now >= nextFrame && (!inFlight || !framebuffer->usingShm())) 
        {
            const double seconds = std::chrono::duration<double>(now - start).count();
            fillGradient(*framebuffer);
            drawGradientCircle(*framebuffer, mouseX, mouseY, 50);
            drawFrame(*framebuffer, seconds, mouseX, mouseY,  rgb(255, 255, 255), mouseInside);

            inFlight = !framebuffer->Present(win, gc, 0, 0);
            nextFrame = std::max(nextFrame + frameTime, now);
        } 
        else 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        pollfd pfd {ConnectionNumber(display), POLLIN, 0};
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame - Clock::now());
        const int timeoutMs = inFlight ? 5 : std::max<int>(static_cast<int>(wait.count()), 1);
        poll(&pfd, 1, timeoutMs);
    }

    delete framebuffer;
    XFreeGC(display, gc);
    XDestroyWindow(display, win);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
}