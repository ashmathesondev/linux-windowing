# Chapter 2. Toolchain and environment

**You will learn**

- Which packages you need and what each one is for
- How the CMake files in this repository are organised, and why
- How to build with GCC and Clang from the command line and from CLion
- The debugging tools you will use in every later chapter

**Code for this chapter:** `p00_env_check`, plus the root `CMakeLists.txt`, `CMakePresets.json` and `cmake/`.

---

## 2.1 Packages

Programs need two things from a library: the shared object at run time, and the headers plus a `.pc` file at build time. Ubuntu ships the second set in `-dev` packages.

```
sudo apt install build-essential cmake pkg-config clang \
  libx11-dev libxext-dev libxcb1-dev libx11-xcb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev \
  libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev
```

| Package | What you get |
|---|---|
| `build-essential`, `clang` | GCC and Clang, `make` |
| `cmake`, `pkg-config` | Build system and library discovery |
| `libx11-dev` | Xlib, the classic X11 client library |
| `libxext-dev` | X extensions, we need MIT-SHM (shared memory images) |
| `libxcb1-dev`, `libx11-xcb-dev` | XCB, and the bridge that lets Xlib hand its connection to XCB |
| `libxkbcommon-dev`, `libxkbcommon-x11-dev` | Keyboard layouts, with a helper that fetches the layout from an X server |
| `libwayland-dev` | `libwayland-client`, `libwayland-cursor`, and the `wayland-scanner` tool |
| `wayland-protocols` | The XML descriptions of standard protocols (we also vendor the two we use) |
| `libcairo2-dev`, `libpango1.0-dev` | 2D drawing and text layout |

Useful extras for testing without disturbing your desktop:

```
sudo apt install xvfb xdotool imagemagick x11-utils x11-apps weston wayland-utils
```

## 2.2 pkg-config

A library's compile flags depend on where it was installed. `pkg-config` stores them in `.pc` files so you do not hard code paths.

```
pkg-config --cflags --libs xkbcommon
pkg-config --modversion wayland-client
```

CMake wraps this in `pkg_check_modules`. In the root `CMakeLists.txt`:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
```

`IMPORTED_TARGET` creates a target named `PkgConfig::XKBCOMMON` that carries include directories and link flags. You then write `target_link_libraries(app PRIVATE PkgConfig::XKBCOMMON)` and everything follows.

If a package is missing, CMake stops with `Package 'xkbcommon' not found`. The fix is always: find the `-dev` package that provides the `.pc` file (`apt-file search xkbcommon.pc` helps) and install it.

## 2.3 How the repository is laid out

```
CMakeLists.txt            finds libraries, adds every sub directory
CMakePresets.json         named configurations (GCC/Clang, debug/release, ASan)
cmake/
  WindowingDefaults.cmake   warning flags and sanitizers for every target
  WaylandProtocols.cmake    turns protocol XML into C code
protocols/                the XML files, vendored
lib/platform  lib/renderer  lib/ui     the reusable libraries (stages 6 to 8)
p00_ ... p09_             one directory per stage, each with a main.cpp
tests/                    headless tests of the widget toolkit
```

Three design decisions are worth understanding.

**Every stage is its own executable.** Stages 0 to 5 are single files with no dependency on the libraries. You can read each from top to bottom, and each stays small enough to hold in your head.

**Compiler settings live in one place.** `ww_target_defaults(target)` applies `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` and, when `WW_ENABLE_SANITIZERS` is on, AddressSanitizer and UndefinedBehaviorSanitizer. We keep warnings high on purpose. Windowing code is full of integer conversions (pixel sizes, byte strides, protocol integers) where a silent narrowing becomes a visible bug.

**Wayland protocols are generated.** The protocol XML is the source of truth. `wayland-scanner` converts each file to a header (what you call) and a C file (marshalling tables, the machinery that turns calls into bytes on the socket). `WaylandProtocols.cmake` does this with `add_custom_command` and packs the results into a static library, `ww_wayland_protocols`. Chapter 6 explains what is inside the generated code.

## 2.4 Building

Presets are named bundles of settings, so you never type compiler flags.

```
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --test-dir build/gcc-debug
```

Available presets: `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`, `clang-asan`. Output goes to `build/<preset>/`, so the two compilers never overwrite each other.

Why build with both compilers? They have different warnings and different strictness, and each reports things the other misses. A habit of building with both catches bugs early.

## 2.5 CLion

CLion reads `CMakePresets.json`. Open the repository folder, then go to **Settings, Build, Execution, Deployment, CMake**. Each preset appears as a profile. Enable `gcc-debug` and `clang-debug` and you can switch compiler from the toolbar.

Tips that save time:

- Run configurations appear automatically for every executable. Set the environment (for example `WW_BACKEND=x11`) in **Edit Configurations**.
- If CMake behaves oddly after files move, use **Tools, CMake, Reset Cache and Reload Project**.
- On a Wayland desktop, CLion itself may run under XWayland. That does not affect the programs you launch from it, which pick a backend from the environment.

## 2.6 Stage 0: checking the environment

`p00_env_check` opens a connection to each display system and reports what it finds. Read its `main.cpp`, it is 63 lines. The two lines that matter:

```cpp
Display* dpy = XOpenDisplay(nullptr);          // X11: nullptr means "use $DISPLAY"
wl_display* dpy = wl_display_connect(nullptr); // Wayland: nullptr means "use $WAYLAND_DISPLAY"
```

Both calls connect to a Unix socket and return null on failure. The version in the repository does a little more for Wayland: `wl_display_connect(nullptr)` only checks `$WAYLAND_DISPLAY` (default `wayland-0`) inside `$XDG_RUNTIME_DIR`, which fails in a terminal or IDE that did not inherit the desktop session's variables, and when a nested compositor uses another socket name. So `p00_env_check`, `p04_wayland_window` and `p05_wayland_input` use a small helper, `connectToCompositor`, that tries `--display NAME`, then `$WAYLAND_DISPLAY`, then every `wayland-N` socket in `$XDG_RUNTIME_DIR`, and print a diagnosis when nothing answers. Run it:

```
./build/gcc-debug/p00_env_check/p00_env_check
```

On a Wayland desktop with XWayland you should see `ok` for both. On a pure X11 session, Wayland will say it could not connect. That is expected.

Conditional compilation (`#ifdef WW_HAVE_X11`) is driven by the CMake options `WW_BUILD_X11` and `WW_BUILD_WAYLAND`. Turn a backend off with `-DWW_BUILD_WAYLAND=OFF` if you want to build on a machine without Wayland headers.

## 2.7 A debugging toolbox

You will use these constantly.

| Tool | Use |
|---|---|
| `xev` | Prints raw X11 events for a window. Compare against what your program thinks it received |
| `xprop`, `xwininfo` | Inspect a window's properties and geometry (click on it). Check your title and size hints here |
| `xdpyinfo` | Server capabilities and extensions (is MIT-SHM there?) |
| `xtrace` | Prints the X11 protocol between client and server, if installed |
| `WAYLAND_DEBUG=1 ./app` | Makes libwayland print every request and event. The single most useful Wayland tool |
| `wayland-info` | Lists the globals the compositor advertises (is `xdg_decoration` offered?) |
| ASan and UBSan | Out of bounds buffer writes and integer overflow, the usual shared memory bugs |
| `gdb` | As usual. Windowing bugs are often protocol ordering bugs, so a log beats a breakpoint |

**Virtual displays for repeatable testing.** `Xvfb :99 -screen 0 1280x800x24 &` starts an X server with no monitor. Run your program with `DISPLAY=:99`, drive it with `xdotool` and screenshot with ImageMagick's `import -window root out.png`. The book's author used exactly this to test the X11 backend. For Wayland, run `weston --backend=headless` and set `WAYLAND_DISPLAY` to its socket. Headless compositors have no input devices, so you can test drawing and protocol handshakes but not keyboard and mouse.

## Pitfalls

- **Forgetting `-dev`.** `libxkbcommon0` is the run time library. You need `libxkbcommon-dev` to compile.
- **Stale build directory after a system upgrade.** Delete `build/` if library versions changed.
- **Running a Wayland client over SSH.** There is no compositor for it. Use X11 forwarding or run on the machine itself.
- **Assuming `DISPLAY` is set.** In a plain SSH session or a cron job it usually is not.

## Review questions

1. What does `IMPORTED_TARGET` give you compared with using `${XKBCOMMON_LIBRARIES}`?
2. Why are the protocol XML files vendored in `protocols/` instead of read from `/usr/share/wayland-protocols`?
3. Why do the presets build into separate directories?
4. Which tool shows the exact Wayland requests your program sends?

## Exercises

**Ex 2.1** Run `p00_env_check` under your real session, then again with `DISPLAY=` (empty) and with `WAYLAND_DISPLAY=nonexistent`. Predict each result before you run it.

**Ex 2.2** Add one more line to `p00_env_check` that prints whether the MIT-SHM extension is available. Hint: `XShmQueryExtension`, and add `PkgConfig::XEXT` to the link libraries.

**Ex 2.3** Configure the `clang-asan` preset, build, and run the tests. Then introduce a deliberate off by one write in a test copy and watch ASan report it.

**Ex 2.4** Start `Xvfb` on display `:99`, run `p00_env_check` with `DISPLAY=:99`, and compare the screen size it reports with your real screen.

Solutions: [solutions/ch02.md](solutions/ch02.md)

Previous: [Chapter 1](01-how-a-window-appears.md) | Next: [Chapter 3, your first X11 window](03-x11-first-window.md)
