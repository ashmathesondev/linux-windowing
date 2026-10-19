# Native Windowing on Linux with C++ (X11 + Wayland, no UI toolkit)

Goal: build a windowed application on Ubuntu that draws its own buttons, text and other standard UI elements, without Qt, GTK or any library that brings its own widget system.

Toolchain: CMake + Make, Clang and GCC, developed in CLion, C++20.

---

## 1. What is allowed and what is not

| Category | Allowed (low level helpers) | Avoided (own UI system) |
|---|---|---|
| Display protocol | Xlib or XCB, libwayland-client | Qt, GTK, wxWidgets, FLTK |
| Windowing wrappers | none needed | SDL, GLFW (they hide exactly what you want to learn) |
| Keyboard | libxkbcommon | |
| 2D drawing | Cairo (optional, see stage 7) or your own rasterizer | Dear ImGui, Nuklear (they are UI systems) |
| Text | FreeType + HarfBuzz, or Pango | |
| GPU (optional) | EGL, OpenGL, Vulkan loader | |

---

## 2. Mental model

- **X11**: a server owns the screen. Your program connects over a socket, asks for a window, and receives events. The window manager draws the title bar and borders for you.
- **Wayland**: the compositor is the server. You never draw "to the screen"; you fill a shared memory buffer, attach it to a surface, and commit. Window decorations are often your job (client side decorations), unless the compositor supports server side ones.
- Both give you an event loop and a pixel buffer. Everything above that (widgets, text, layout) is yours, so the plan puts a platform abstraction layer between the two.

---

## 3. Roadmap

Each stage is one CMake project that builds and runs on its own, and builds on the previous one.

### Stage 0: Environment
- Install: `build-essential clang cmake pkg-config libx11-dev libxcb1-dev libxkbcommon-dev libwayland-dev wayland-protocols libcairo2-dev libfreetype-dev libharfbuzz-dev libegl-dev libgl-dev`
- Confirm which session you run: `echo $XDG_SESSION_TYPE`.
- CLion: CMake profiles for GCC and Clang, Debug and Release.
- Learn: `pkg-config`, `find_package(PkgConfig)` and `pkg_check_modules` in CMake.
- **Deliverable:** a CMake skeleton that finds all dependencies and builds an empty main.

### Stage 1: X11 hello window (Xlib)
- `XOpenDisplay`, `XCreateSimpleWindow`, `XMapWindow`, `XNextEvent`.
- Close button handling via `WM_DELETE_WINDOW`.
- Window title and size hints.
- **Deliverable:** a window that opens, stays open, and closes cleanly.

### Stage 2: X11 events and input
- Expose, ConfigureNotify (resize), KeyPress, ButtonPress, MotionNotify, Enter/Leave, FocusIn/Out.
- Keyboard through xkbcommon instead of `XLookupString`.
- **Deliverable:** an event logger that prints every event with sensible names.

### Stage 3: X11 software drawing
- Own a pixel buffer (`uint32_t*` ARGB), then present it with `XPutImage`.
- Upgrade to MIT-SHM (`XShmPutImage`) for speed.
- Double buffering, redraw on Expose and resize.
- **Deliverable:** a resizable window with an animated gradient and a filled rectangle that follows the mouse.

### Stage 4: Wayland hello window
- Connect with `wl_display_connect`, bind globals from `wl_registry`.
- Run `wayland-scanner` in CMake to generate the xdg-shell protocol code.
- Objects: `wl_compositor`, `wl_surface`, `xdg_wm_base`, `xdg_surface`, `xdg_toplevel`.
- Buffers: `wl_shm` pool created with `memfd_create` and `mmap`.
- Handle the configure/ack_configure handshake, then commit.
- **Deliverable:** a solid colour Wayland window that closes cleanly.

### Stage 5: Wayland input, resize and decorations
- `wl_seat` with `wl_pointer` and `wl_keyboard`; keymap delivered as an fd to xkbcommon.
- Resize: reallocate buffers on configure.
- Frame callbacks (`wl_surface.frame`) for paced redraw.
- Decorations: try `xdg-decoration` (server side), fall back to drawing your own title bar, or use libdecor as a helper.
- Cursor shapes (cursor-shape-v1 protocol or wl_cursor).
- **Deliverable:** feature parity with stage 3, on Wayland.

### Stage 6: Platform abstraction layer
- Interface: `Window` with `create`, `pollEvents`, `presentPixels`, `size`, `scale`, `setTitle`.
- One neutral `Event` variant type (`std::variant`): Key, Mouse, Resize, Close, Focus.
- Backends `X11Window` and `WaylandWindow`; choose at runtime from `WAYLAND_DISPLAY` / `DISPLAY`.
- Event loop built on `poll`/`epoll` over the display fd, plus timers (`timerfd`).
- **Deliverable:** one executable that runs on either backend with the same app code.

### Stage 7: 2D renderer and text
Pick one path (decision point for you, see questions at the end):
- **A. Cairo (pragmatic):** vector shapes, rounded rects, gradients, clipping, plus Pango or Cairo's own text. Fast to a good looking result.
- **B. Own rasterizer (educational):** rect fill, rounded rect with antialiasing, alpha blending, clip stack; glyphs via FreeType, shaping via HarfBuzz, glyph atlas cache.
- **Deliverable:** a `Renderer` class with `fillRect`, `strokeRect`, `drawText`, `pushClip`, `popClip`, and a text measurement function.

### Stage 8: Mini widget toolkit
Recommended approach is **immediate mode** (simplest to get right), with retained mode as a later experiment.
- Core: UI context, widget ids, hot/active/focus state, per frame input snapshot.
- Widgets: label, button, checkbox, radio, slider, progress bar, text field (cursor, selection, backspace, arrows), scroll area, list box, dropdown.
- Layout: row/column stacks, spacing, padding, simple flex.
- Theme struct: colours, fonts, metrics.
- **Deliverable:** a small demo app (settings panel) using every widget.

### Stage 9: Production details
- HiDPI: Wayland buffer scale / fractional-scale-v1, X11 `Xft.dpi`.
- Clipboard: X11 selections (`CLIPBOARD`, INCR), Wayland `wl_data_device`.
- Key repeat (Wayland leaves it to the client), cursor blink timers.
- IME basics (text-input-v3, XIM): read about it, implement last.
- Idle redraw: render only when something changed.
- **Deliverable:** a polished demo that copy/pastes text and looks sharp on a scaled display.

### Stage 10: Standard desktop controls
- Menu bar, popup and context menus (checkable and disabled items, separators, keyboard navigation, shortcut text).
- Tooltips with a hover delay, modal dialogs and a message box, tree view, spin box, status bar, key shortcuts.
- Mapping from Win32 controls (`BUTTON`, `EDIT`, `COMBOBOX`, `SysTreeView32`, `MessageBox`) to the immediate mode calls.
- **Deliverable:** `p10_controls`, a file manager style window using every control, with headless tests.

### Stage 11 (optional): GPU path
- EGL context on both backends (`EGL_PLATFORM_X11_KHR`, `EGL_PLATFORM_WAYLAND_KHR`).
- Draw your UI batches with OpenGL, or create a Vulkan surface with `VK_KHR_xlib_surface` / `VK_KHR_wayland_surface`.
- Natural fit given your DX12 and OpenXR background: swapchain, frame pacing, present modes.

---

## 4. Suggested repository layout

```
linux-windowing/
  CMakeLists.txt
  cmake/                    # FindXXX helpers, wayland-scanner macro
  protocols/                # xdg-shell.xml and friends
  p00_env_check/
  p01_x11_window/
  p02_x11_events/
  p03_x11_pixels/
  p04_wayland_window/
  p05_wayland_input/
  p06_platform_layer/       # library: window abstraction
  p07_renderer/             # library: 2D + text
  p08_widgets/              # library: UI + demo app
  docs/                     # one notes file per stage
```

Libraries from stages 6 to 8 are static libs so later projects link against them.

---

## 5. Common pitfalls

- **Wayland has no global window position and no screenshot of your own window**; do not design around either.
- **Do not draw before the first configure** on Wayland, or the surface is never shown.
- **Never write to a buffer the compositor still holds**; track `wl_buffer.release` (use double or triple buffering).
- **X11 events are asynchronous**: a request returning does not mean it took effect; `XSync` while debugging.
- **Key repeat and cursor shape differ by backend**; the abstraction layer must absorb this.
- **Text is the hard part**: shaping, caching and measuring dominate the widget work. Budget time for it.
- On GNOME Wayland, expect no server side decorations; on KDE and wlroots compositors expect them.

---

## 6. Reading list

- *The Wayland Protocol* book by Drew DeVault (wayland-book.com)
- Wayland protocol docs and the xdg-shell specification (wayland.app)
- Xlib Programming Manual and the XCB tutorial
- xkbcommon documentation (quick start)
- FreeType and HarfBuzz tutorials
- Casey Muratori's Immediate Mode GUI talk and the Dear ImGui source (to read, not to use)

---

## 7. Time estimate (evenings, rough)

| Stages | Effort |
|---|---|
| 0 to 3 (X11 window with pixels) | 3 to 5 |
| 4 to 5 (Wayland window with input) | 5 to 8 |
| 6 (abstraction) | 2 to 3 |
| 7 (renderer and text) | 5 to 10 |
| 8 (widgets) | 8 to 15 |
| 9 to 11 (polish, controls, GPU) | open ended |
