# Appendix C. Troubleshooting

## Build problems

| Symptom | Cause and fix |
|---|---|
| `Package 'xkbcommon' not found` (or cairo, wayland-client ...) | The `-dev` package is missing. See chapter 2 for the full `apt install` line |
| `include could not find requested file: WindowingDefaults` | The `cmake/` directory is missing. Extract the whole archive, do not copy files one at a time |
| `add_subdirectory given source "lib" which is not an existing directory` | Same: the `lib/`, `protocols/`, `tests/` folders did not come along |
| `No cmake_minimum_required command is present` | Line 1 of the root `CMakeLists.txt` is missing or the wrong file was opened. Add `cmake_minimum_required(VERSION 3.20)` |
| `wayland-scanner` not found | Install `libwayland-dev` (or `wayland-protocols` and `wayland-utils`) |
| Linker error `cannot find libclang_rt.asan` | Install `libclang-rt-<version>-dev` for your Clang version |
| CLion shows red code but builds | Use **Reset Cache and Reload Project**, and make sure the CMake profile uses the preset |
| `-Wmaybe-uninitialized` warnings from `std::variant` in GCC release builds | A GCC false positive when an event is copied by value into the queue. `WindowBase::push` now constructs it in place, so this repository builds warning free. If you see it in your own code, construct the variant in place with `emplace_back` |

## X11 problems

| Symptom | Cause and fix |
|---|---|
| `Cannot open X display` | `DISPLAY` unset or no X server. On Wayland desktops XWayland provides `:0`. For headless runs start `Xvfb :99 &` and `export DISPLAY=:99` |
| Window never appears | Missing `XMapWindow` or `XFlush`, or waiting for `Expose` that never comes |
| Window closes with "X connection broken" | `WM_DELETE_WINDOW` not registered |
| No keyboard or mouse events | Event mask missing in `XSelectInput` |
| Wrong characters | Not using xkbcommon, or `xkb_state` not updated, or keymap loaded once and layout changed |
| Held key shows release and press pairs | Core protocol auto-repeat. Detect pairs or use `XkbSetDetectableAutoRepeat` |
| Tearing in SHM animation | Buffer reused before `ShmCompletion` |
| `XShmAttach` fails | Remote display or container without shared IPC. Fall back to `XPutImage` |
| Leftover shared memory | `ipcs -m`, and call `shmctl(IPC_RMID)` right after attach |
| Flicker while resizing | Background not disabled: `XSetWindowBackgroundPixmap(dpy, win, None)` |
| Window too small or sizes ignored | `XSizeHints` flags not set |

## Wayland problems

| Symptom | Cause and fix |
|---|---|
| `Cannot connect to a Wayland compositor` | You are in an X11 session (`XDG_SESSION_TYPE=x11`), or `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` were not inherited (IDE, `sudo`, SSH), or the socket is in a container. The message prints what was checked. In an X11 session start a nested compositor (`weston --backend=x11 --socket=wayland-dev &`) and pass `--display wayland-dev` to p00, p04 and p05, or set `WAYLAND_DISPLAY=wayland-dev` for the programs built on `ww::Window` |
| Window never appears | No initial buffer-less commit, no ack of configure, or no buffer committed afterwards |
| Disconnected with a protocol error | Read the error text (`WAYLAND_DEBUG=1`). Typical: buffer before first configure, wrong stride or size, destroyed an object still in use |
| Window has no title bar (GNOME) | GNOME has no server side decorations. Draw client side decorations (chapter 7) |
| Cursor missing or wrong | You must set it on every `wl_pointer.enter` with that event's serial |
| Keys do not repeat | Key repeat is the client's job (timer) |
| Keys repeat forever | Timer not stopped on release or focus loss |
| Wrong Shift or Caps state | Updating xkb state from key events instead of `wl_keyboard.modifiers` |
| Blurry on HiDPI | `set_buffer_scale` not set, or buffer not scaled |
| Window "not responding" dialog | `ping` not answered, or a long handler blocking the loop |
| Clipboard paste empty | Source is gone, no matching mime type, or you wait too short. Check `WAYLAND_DEBUG` for `receive` |
| Events stuck until next input | `poll` loop without `prepare_read`, or events left in the queue |
| Move or resize does nothing | Wrong or stale serial passed to `xdg_toplevel_move` or `resize` |

## Toolkit problems

| Symptom | Cause and fix |
|---|---|
| Two widgets behave as one | Same label. Use `Label##id` or `pushId` |
| Widgets lag one frame behind | Layout depends on later widgets. Let `drawFrame` settle, or change the order |
| CPU high while idle | Something sets `dirty` every loop, or an animation is left running. Watch the frames counter |
| Caret jumps inside a character | Byte index not on a UTF-8 boundary |
| Click does nothing in a test | A frame is missing between press and the event that depends on hot or active |
| Text blurry or fringed | Using sub pixel anti-aliasing on a translucent buffer. We use grayscale |
| Lines blurry | Stroke centred on an integer coordinate. Offset by half the line width |

## Diagnostic commands

```
echo $XDG_SESSION_TYPE $DISPLAY $WAYLAND_DISPLAY
wayland-info | less
xdpyinfo | head
xprop            (then click a window)
xev -id $(xdotool getactivewindow)
WAYLAND_DEBUG=1 ./app 2>&1 | less
setxkbmap -print -verbose 10
ipcs -m
strace -f -e trace=poll,ppoll,recvmsg ./app
```
