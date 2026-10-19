# Appendix B. Cheat sheets

## B.1 X11 (Xlib) call order for a window

```
XOpenDisplay(nullptr)
XCreateSimpleWindow(dpy, root, x, y, w, h, border, borderColor, background)
XStoreName + _NET_WM_NAME (UTF8_STRING)
XSetWMNormalHints          (size hints, set the flag for each field)
XInternAtom("WM_DELETE_WINDOW") + XSetWMProtocols
XSelectInput(dpy, win, masks)
XMapWindow + XFlush
loop: XPending / XNextEvent  (or poll on ConnectionNumber)
XDestroyWindow, XCloseDisplay
```

| Event | Meaning | Notes |
|---|---|---|
| `ClientMessage` | Close request when data.l[0] is `WM_DELETE_WINDOW` | Not a destroy |
| `ConfigureNotify` | Size or position changed | Trust these values |
| `Expose` | Region needs repaint | `count == 0` is the last of a batch |
| `KeyPress`, `KeyRelease` | Keycode in `xkey.keycode` | Feed to xkbcommon |
| `ButtonPress`, `ButtonRelease` | 1 L, 2 M, 3 R, 4 up, 5 down, 6 left, 7 right | Wheel has only press |
| `MotionNotify` | Pointer moved | |
| `EnterNotify`, `LeaveNotify` | Pointer crossed the edge | Check `mode` |
| `FocusIn`, `FocusOut` | Keyboard focus | |
| `SelectionRequest`, `SelectionNotify` | Clipboard | Answer `TARGETS` too |
| `shmEventBase + ShmCompletion` | SHM buffer is free again | Compute the type at run time |

## B.2 Shared memory pixels on X11

```
XShmQueryExtension
XShmCreateImage -> shmget(IPC_PRIVATE, bytes_per_line*h, IPC_CREAT|0600) -> shmat
shmctl(IPC_RMID)                  (segment dies with the last detach)
XShmAttach + XSync (with error handler)
XShmPutImage(..., send_event=True) ; wait for ShmCompletion before reusing
teardown: XShmDetach, XSync, XDestroyImage, shmdt
```

## B.3 Wayland call order for a window

```
wl_display_connect
wl_display_get_registry + listener, wl_display_roundtrip
bind: wl_compositor, wl_shm, xdg_wm_base (+ wl_seat, zxdg_decoration_manager_v1, wl_data_device_manager)
wl_compositor_create_surface
xdg_wm_base_get_xdg_surface, xdg_surface_get_toplevel, set_title, set_app_id, set_min_size
(optional) decoration: get_toplevel_decoration + set_mode(SERVER_SIDE)
wl_surface_commit            <- no buffer
 ... configure events ...
xdg_surface_ack_configure(serial)
create buffer, draw
wl_surface_attach, wl_surface_damage_buffer, (wl_surface_frame), wl_surface_commit
loop: prepare_read / flush / poll / read_events or cancel_read / dispatch_pending
```

| Object | Key events |
|---|---|
| `wl_registry` | `global`, `global_remove` |
| `xdg_wm_base` | `ping` (answer `pong`) |
| `xdg_surface` | `configure(serial)` (answer `ack_configure`) |
| `xdg_toplevel` | `configure(w, h, states)`, `close` |
| `wl_buffer` | `release` |
| `wl_callback` (frame) | `done` |
| `wl_seat` | `capabilities` |
| `wl_keyboard` | `keymap(fd)`, `enter`, `leave`, `key`, `modifiers`, `repeat_info` |
| `wl_pointer` | `enter`, `leave`, `motion`, `button`, `axis`, `frame` |
| `wl_output` | `geometry`, `mode`, `scale`, `done` |
| `wl_data_device` | `data_offer`, `selection` |

Requests that need a **serial**: `xdg_toplevel_move`, `resize`, `show_window_menu`, `wl_pointer_set_cursor`, `wl_data_device_set_selection`.

## B.4 Coordinates and units

| Thing | Unit |
|---|---|
| Wayland pointer position | `wl_fixed_t` (24.8 fixed point), surface local logical pixels |
| X11 pointer position | integer pixels in the window |
| `ww::` mouse events | float, logical pixels |
| `ww::Framebuffer` | device pixels (`width`, `height`), `stride` in bytes, `scale` |
| Renderer, UI | logical pixels |
| Pango | 1024 units per pixel (`PANGO_SCALE`) |
| Wayland `damage_buffer` | buffer pixels |
| X11 keycode | evdev code + 8 |
| Wayland `key` | evdev code (add 8 for xkbcommon) |

## B.5 Pixel formats

| Name | Layout (little endian word) | Notes |
|---|---|---|
| `WL_SHM_FORMAT_XRGB8888` | `0xXXRRGGBB` | Required by every compositor |
| `WL_SHM_FORMAT_ARGB8888` | `0xAARRGGBB`, premultiplied | Required by every compositor |
| `CAIRO_FORMAT_ARGB32` | same as ARGB8888 | Our framebuffer format |
| X11 TrueColor visual | check `red_mask`, `green_mask`, `blue_mask` | Usually `0xRRGGBB` |

## B.6 libxkbcommon

```
ctx    = xkb_context_new
keymap = xkb_keymap_new_from_buffer(...)  (Wayland)   |  xkb_x11_keymap_new_from_device(...)  (X11)
state  = xkb_state_new(keymap)                         |  xkb_x11_state_new_from_device(...)
sym    = xkb_state_key_get_one_sym(state, code)
utf8   = xkb_state_key_get_utf8(state, code, buf, n)
modifiers: xkb_state_update_mask(state, depressed, latched, locked, 0, 0, group)   (both backends, from the server/compositor)
repeats?:  xkb_keymap_key_repeats(keymap, code)
```

## B.7 Cairo and Pango in one table

| Task | Call |
|---|---|
| Surface over your pixels | `cairo_image_surface_create_for_data(ptr, ARGB32, w, h, stride)` |
| HiDPI | `cairo_surface_set_device_scale(surface, s, s)` |
| Rounded rectangle | four `cairo_arc` with `cairo_new_sub_path` |
| Clip | `cairo_save`, `cairo_rectangle`, `cairo_clip` ... `cairo_restore` |
| Text layout | `pango_cairo_create_layout(cr)`, `pango_layout_set_text`, `pango_layout_set_font_description` |
| Draw text | `cairo_move_to`, `pango_cairo_show_layout` |
| Size | `pango_layout_get_extents(layout, &ink, &logical)` |
| Caret x | `pango_layout_index_to_pos` |
| Click to index | `pango_layout_xy_to_index` |

## B.8 Environment variables used in this repository

| Variable | Effect |
|---|---|
| `WW_BACKEND=x11` or `wayland` | Force a backend in stages 6 to 9 |
| `WW_SCALE=2` | Force a HiDPI scale |
| `DISPLAY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR` | Where to connect |
| `WAYLAND_DEBUG=1` | libwayland prints all protocol traffic |
| `ASAN_OPTIONS=detect_leaks=0` | Used by the ASan test run (fontconfig caches) |

## B.9 Win32 control to toolkit call

| Win32 | Call |
|---|---|
| `STATIC` | `ui.label(text)` |
| Push button | `if (ui.button("Save")) ...` |
| Check box | `ui.checkbox("Wrap", flag)` |
| Radio buttons | `ui.radio("Low", choice, 0)` |
| Group box | `ui.beginCard("Title")` ... `ui.endCard()` |
| `EDIT` | `ui.textField("id", text, "hint")` |
| `COMBOBOX` | `ui.dropdown("id", items, index)` |
| `LISTBOX` | `ui.listBox("id", items, index, rows)` |
| Trackbar | `ui.slider("id", value, min, max)` |
| Progress bar | `ui.progressBar(fraction, "35%")` |
| Tab control | `ui.tabBar("id", names, index)` |
| Scrolling area | `ui.beginScroll("id", height)` ... `ui.endScroll()` |
| Up-down (spin) | `ui.spinBox("id", n, min, max)` |
| Tree view | `if (ui.treeNode("dir").open) { ...; ui.treePop(); }`, `ui.treeLeaf("file")` |
| Menu bar | `ui.beginMenuBar()`, `ui.beginMenu("File")`, `ui.menuItem("Open", "Ctrl+O")`, `ui.endMenu()`, `ui.endMenuBar()` |
| Context menu | `ui.beginContextMenu("id")` ... `ui.endContextMenu()` after the widget |
| Tooltip | `ui.tooltip("text")` right after the widget |
| `MessageBox` | `ui.messageBox("Title", "Text", {"OK"}, open)` |
| Modal dialog | `ui.beginModal("Title", open)` ... `ui.endModal()`, called last |
| Status bar | `ui.statusBar({"Ready", "3 items"})` |
| Accelerator | `if (ui.shortcut(ww::Key::S, true)) save();` |
| `EnableWindow(FALSE)` | `ui.beginDisabled()` ... `ui.endDisabled()` |
