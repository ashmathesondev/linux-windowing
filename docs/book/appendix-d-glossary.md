# Appendix D. Glossary

**Accelerator.** A keyboard shortcut such as Ctrl+S. In Win32 an accelerator table, here `Context::shortcut`.

**Anti-aliasing.** Smoothing the edges of shapes and text by drawing partially covered pixels with partial intensity.

**Atom (X11).** A small integer that stands for a string, such as `WM_DELETE_WINDOW`. Created with `XInternAtom`.

**Buffer (Wayland).** A block of pixels attached to a surface. `wl_shm` buffers live in shared memory.

**Client side decorations (CSD).** The application draws its own title bar and borders. Required on GNOME Wayland.

**Commit (Wayland).** The request that makes all pending surface state take effect atomically.

**Compositor.** The program that combines all windows into the final image. On Wayland it is also the display server.

**Configure (Wayland).** The compositor's message that tells a window its size and state, which the client must acknowledge.

**Damage.** The part of a surface that changed since the last commit.

**Device scale.** The number of buffer pixels per logical pixel on a HiDPI display.

**Display server.** The program that owns the screen and input devices and talks to clients. Xorg, or the Wayland compositor.

**DRM/KMS.** The kernel interfaces for graphics hardware and display mode setting.

**evdev.** The Linux kernel input event interface (`/dev/input/event*`).

**Event loop.** The loop that waits for input and other events, handles them, and redraws.

**Focus.** The window, or widget, that currently receives keyboard input.

**Frame callback.** A Wayland request that tells you when to draw the next frame.

**Framebuffer.** In this book, a block of memory holding the pixels of one frame of a window.

**Global (Wayland).** An object advertised by the registry, such as `wl_compositor`, that a client can bind.

**Hot, active (immediate mode).** Hot is the widget under the mouse. Active is the widget being pressed.

**IME.** Input method editor. Software that composes characters for languages that need it.

**Immediate mode.** A UI style where the application describes the interface each frame by calling functions, without keeping widget objects.

**Keycode.** A number identifying a physical key. X keycode is evdev code plus 8.

**Keysym.** The symbol a key produces under the current layout and modifiers (`XK_a`).

**Logical pixel.** A pixel in the unit the application lays out in. Independent of display density.

**memfd.** An anonymous in-memory file created with `memfd_create`, used to share memory with the compositor.

**MIT-SHM.** The X11 extension for passing images through shared memory.

**Modal dialog.** A dialog that blocks interaction with the rest of the window until it is closed. Here: a scrim plus `blockOutside_`.

**Owner draw.** Win32 term for a control whose appearance the application paints. In this toolkit every control is owner drawn.

**Pango.** Text layout library. **Cairo.** 2D vector drawing library.

**Premultiplied alpha.** Colour channels stored already multiplied by alpha.

**Property (X11).** Named typed data attached to a window, such as its title.

**Proxy (Wayland).** The client side object representing a server object.

**Registry (Wayland).** The object through which the compositor lists the globals it offers.

**Role (Wayland).** The meaning given to a surface, such as `xdg_toplevel` for a desktop window.

**Scrim.** The translucent layer drawn over a window behind a modal dialog.

**Seat (Wayland).** A set of input devices used together.

**Selection (X11).** The mechanism behind the clipboard: an owner offers data that others request.

**Serial.** A number from an input or configure event that must be quoted in a related request.

**Server side decorations (SSD).** The compositor or window manager draws the title bar and borders.

**Shaping.** Turning characters into positioned glyphs, applying ligatures, kerning and script rules.

**Stride.** The distance in bytes between the start of one row of pixels and the next.

**Surface (Wayland).** A rectangle of pixels managed by the compositor.

**Tooltip.** A small text box shown near the pointer after it rested on a widget for a short delay.

**Tree view.** A control that shows nested items with expand and collapse arrows. Win32: `SysTreeView32`; here `treeNode` and `treePop`.

**Visual (X11).** The description of how pixel values map to colours.

**Window manager.** The program that decides how windows are placed and decorated (X11, or part of the Wayland compositor).

**XDG.** The freedesktop.org cross desktop standards: desktop files, base directories, portals, and the `xdg-shell` Wayland protocol.

**XIM, XKB, XInput2.** X11 extensions: the input method protocol, the keyboard extension, and the modern input device extension.

**xkbcommon.** The library that turns keycodes into symbols and text using keyboard layouts.

**XWayland.** An X server that runs as a Wayland client so X11 programs work on Wayland desktops.
