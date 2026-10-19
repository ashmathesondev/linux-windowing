# linux-windowing

A learning project: build native windowed applications on Linux in C++20 with no UI toolkit. Everything is drawn by us. The only libraries used are low level helpers (Xlib, libwayland-client, xkbcommon, Cairo, Pango). The one third party UI library is Dear ImGui (MIT license), used in stages 11 and 12 only, where the point is to write its backend ourselves. Stage 12 also uses EGL and OpenGL 3.3 core, and no other graphics library.

Start with the textbook in `docs/book/` (15 chapters, exercises with solutions, appendices). `docs/ROADMAP.md` has the original plan, and `docs/STAGE_NOTES.md` has short per-stage reading guides. Work through the stages in order, reading each chapter next to its code.

## Stages

| Stage | Directory | What it teaches |
|---|---|---|
| 0 | `p00_env_check` | Toolchain and library check |
| 1 | `p01_x11_window` | Connect to the X server, create a window, close it properly |
| 2 | `p02_x11_events` | Event loop, keyboard through XKB, mouse, resize, focus |
| 3 | `p03_x11_pixels` | Draw pixels yourself into a shared memory image |
| 4 | `p04_wayland_window` | Registry, wl_shm buffers, xdg-shell handshake |
| 5 | `p05_wayland_input` | Seat, keymap, key repeat, cursors, frame callbacks, window decorations |
| 6 | `p06_platform_layer` | One `ww::Window` interface over both backends (`lib/platform`) |
| 7 | `p07_renderer` | 2D renderer with text, shapes, clipping (`lib/renderer`) |
| 8 | `p08_widgets` | Immediate mode widget toolkit (`lib/ui`) and a widget gallery |
| 9 | `p09_polish` | A small settings form that uses everything |
| 10 | `p10_controls` | Menus, context menus, tooltips, tree view, spin box, status bar, modal dialogs |
| 11 | `p11_imgui_soft` | Dear ImGui on our own CPU triangle rasterizer, no graphics library (`lib/imgui`) |
| 12 | `p12_imgui_gl` | The same ImGui interface on OpenGL 3.3 core through EGL, read back into the window |

Stage 12 targets OpenGL 3.3 core on purpose: ImGui needs nothing newer, and 3.3 runs on almost every driver. A driver may still give a newer context (4.5 on Mesa), and the code accepts that. Try the ImGui stages with `make run-p11` and `make run-p12`.

Stages 0 to 5 are standalone programs, so each one can be read in a single file. Stages 6 to 12 share the libraries in `lib/`. Stages 11 and 12 use [Dear ImGui](https://github.com/ocornut/imgui), which CMake downloads at configure time (see below).

## Build

Ubuntu packages:

```
sudo apt install build-essential cmake pkg-config clang \
  libx11-dev libxext-dev libxcb1-dev libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev \
  git libegl-dev libgl1-mesa-dri
```

`git` and network access are needed once, at configure time: CMake downloads Dear ImGui v1.93.0 into `build/_deps`. `libegl-dev` is only for stage 12 (OpenGL), and `libgl1-mesa-dri` provides Mesa's software renderer, which the OpenGL test uses when there is no GPU.

Command line, with presets for GCC and Clang:

```
cmake --preset gcc-debug          # or clang-debug, gcc-release, clang-release, clang-asan
cmake --build --preset gcc-debug
ctest --test-dir build/gcc-debug
./build/gcc-debug/p09_polish/p09_polish
```

### Makefile shortcuts

A `Makefile` wraps the presets so the common jobs are one word. CMake stays the source of truth.

```
make                      # configure and build the default preset (gcc-debug)
make PRESET=clang-release # pick another preset; see `make presets`
make test                 # build, then run ctest
make test-asan            # tests under the clang-asan preset
make run-p09              # build, then run a stage (ARGS="..." passes arguments)
make clean                # clean build outputs, keep the configuration
make rebuild              # clean, then build
make distclean            # delete the whole build/ directory
make reconfigure          # re-run CMake from scratch
make all-presets          # build and test every preset
make deps                 # apt install the dependencies
make docs-check           # render every Mermaid block (needs mmdc)
make zip                  # zip the project, without build/
make help                 # the full list
```

Environment variables such as `WW_BACKEND=x11` and `WW_SCALE=2` pass through, for example `WW_SCALE=2 make run-p09`. `JOBS=4` limits parallelism.

CLion: open the folder, and the presets appear under Settings, Build, CMake. Enable both `gcc-debug` and `clang-debug` to switch compilers from the toolbar.

Options: `-DWW_BUILD_X11=OFF` or `-DWW_BUILD_WAYLAND=OFF` skips a backend, `-DWW_ENABLE_SANITIZERS=ON` adds ASan and UBSan, `-DWW_BUILD_IMGUI=OFF` skips stages 11 and 12 (and the download), `-DWW_BUILD_GL=OFF` skips only stage 12. Offline: clone ImGui yourself and pass `-DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui`. If EGL is not found, stage 12 is skipped automatically.

## Running

Stages 6 to 12 choose Wayland when `WAYLAND_DISPLAY` is set and fall back to X11. Stages 0, 4 and 5 accept `--display NAME` and otherwise look for the compositor themselves (`$WAYLAND_DISPLAY`, then any `wayland-N` socket in `$XDG_RUNTIME_DIR`). On an X11 desktop, test Wayland with a nested compositor:

```
weston --backend=x11 --socket=wayland-dev &
./build/gcc-debug/p05_wayland_input/p05_wayland_input --display wayland-dev
WAYLAND_DISPLAY=wayland-dev ./build/gcc-debug/p10_controls/p10_controls   # stages 6 to 12
```

Environment variables:

- `WW_BACKEND=x11` or `wayland` forces a backend
- `WW_SCALE=2` forces a HiDPI scale factor (otherwise read from the compositor or `Xft.dpi`)

`p07_renderer --screenshot out.png`, `p08_widgets --screenshot prefix`, `p10_controls --screenshot prefix`, `p11_imgui_soft --screenshot prefix` and `p12_imgui_gl --screenshot prefix` render without any window. `p11_imgui_soft --bench` and `p12_imgui_gl --bench` time 300 frames. If stage 12 cannot create an OpenGL context (no GPU, or a remote session), try `LIBGL_ALWAYS_SOFTWARE=1`.

## Tests

`tests/ui_test.cpp` drives the widget toolkit headlessly (clicks, typing, selection, clipboard, dropdowns, sliders, scale 2), and `tests/controls_test.cpp` does the same for menus, context menus, tooltips, modal dialogs, the tree view and the spin box. No display server is needed. `tests/imgui_soft_test.cpp` checks the ImGui CPU rasterizer pixel by pixel, and `tests/imgui_gl_test.cpp` draws the same interface with OpenGL 3.3 through EGL (Mesa's software driver is enough) and requires it to match the CPU renderer to within one level of 255. The OpenGL test is reported as *skipped* when no EGL context is available.

## Layout of the libraries

- `lib/platform`: `ww::Window`, events, X11 and Wayland backends
- `lib/renderer`: `ww::Renderer` on Cairo and Pango, the only file to replace if you want another drawing backend
- `lib/ui`: `ww::ui::Context`, the immediate mode toolkit
- `lib/imgui`: the Dear ImGui backend written for this project: `Host` (input, clipboard, cursor), `SoftRenderer` (CPU), and `EglContext`, `gl33.hpp` and `GlRenderer` (OpenGL 3.3 core)

## Known limitations

- The OpenGL stage renders off screen and reads the image back into the window's CPU framebuffer. It was tested on Mesa's software driver (llvmpipe) only, not on a physical GPU. A real swap chain is described in chapter 15

- No input method (IME), compose or dead keys
- X11 clipboard has no INCR transfers (large pastes) and no PRIMARY selection
- Wayland scale is integer only (no fractional scale protocol)
- No drag and drop
- Wayland pointer and keyboard code was verified by review and compilation only, because the headless compositor used for testing has no input devices. Run `p05_wayland_input` and `p06_platform_layer` on a real session and report anything odd
- Window decorations on Wayland are drawn by us when the compositor does not provide them (GNOME), and are simple
