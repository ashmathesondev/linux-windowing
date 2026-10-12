# Stage notes

Short reading guides. Each stage's `main.cpp` starts with a comment block explaining what to look at.

## Stages 1 to 3: X11
- The server owns the screen, you ask for a window and get events. Close requests arrive as a `WM_DELETE_WINDOW` client message, not as a destroy.
- Keys: use XKB (xkbcommon-x11) instead of `XLookupString`, so layouts and modifiers match what the user configured.
- Pixels: `XShmPutImage` uploads through shared memory. Wait for the completion event before reusing the buffer.

## Stages 4 and 5: Wayland
- Nothing is drawn until you attach a buffer and commit. Resizes arrive as `configure`; you must `ack_configure` before the next commit.
- Buffers are shared memory files (`memfd_create`). Track `wl_buffer.release` and keep up to three in flight.
- The compositor sends the keymap as a file descriptor; feed it to xkbcommon. Key repeat is yours: use a `timerfd`.
- Decorations: ask for server side via xdg-decoration, otherwise draw a title bar yourself (done in `lib/platform/src/wayland_window.cpp`).
- HiDPI: integer scale from `wl_output`, then `wl_surface.set_buffer_scale`.

## Stage 6: platform layer
- `ww::Window` has `waitEvent(out, timeoutMs)`, `acquireFramebuffer()`, `present()`. Events are a `std::variant`. Both backends hide behind it, so everything above is portable between them.

## Stage 7: renderer
- Cairo for shapes, Pango for text. Coordinates are logical; the renderer applies the window scale.

## Stage 8: immediate mode UI
- Each frame, call the widgets again. `button()` returns true on click, `slider()` edits a float in place. There is no widget tree.
- Widget ids are hashes of labels and the id stack. Hot, active and focused ids live in the context.
- Layout is a stack of rows, columns, cards and scroll areas. A container's height is only known after its children draw, so `drawFrame` runs a settle pass when sizes change.
- Popups (dropdown list) are recorded and drawn last so they sit on top and block clicks beneath them.
- The loop sleeps in `waitEvent` and wakes only for input or the caret blink.

## Stage 9
- Same toolkit, used as an application. Compare `p09_polish/main.cpp` with `p08_widgets/gallery.hpp`.

## Where to go next (stage 10)
- GPU path: create an EGL context on both X11 and Wayland and swap `ww::Renderer` for an OpenGL one that batches rectangles and a glyph atlas. Vulkan follows the same shape with more setup.
