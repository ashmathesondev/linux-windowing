# Native Windowing on Linux with C++

A textbook that teaches how graphical applications work on Linux, from the kernel boundary to a working widget toolkit, using only low level libraries (Xlib, libwayland, xkbcommon, Cairo, Pango). No Qt, GTK, SDL or GLFW.

Every chapter explains concepts, walks through real code from this repository, lists pitfalls and debugging tips, and ends with review questions and exercises. Solutions are in separate files.

**Assumed background:** comfortable C++ (templates, RAII, `std::variant`), a Linux terminal, and basic CMake. No prior knowledge of X11, Wayland or graphics programming is needed.

## How to use this book

1. Read a chapter.
2. Build and run the stage it refers to, then change something.
3. Answer the review questions without looking back.
4. Do at least two exercises. The harder ones are marked at the end of each list.
5. Compare with the solutions.

If you only have a few evenings: chapters 1, 3, 6, 8, 10 and 11 give the shortest path through the ideas.

## Contents

### Part I. Foundations
- [1. How a window appears on a Linux screen](01-how-a-window-appears.md)
- [2. Toolchain and environment](02-toolchain-and-environment.md) (stage 0)

### Part II. X11
- [3. Your first X11 window](03-x11-first-window.md) (stage 1)
- [4. Events, keyboard and mouse on X11](04-x11-events-and-keyboard.md) (stage 2)
- [5. Pixels, shared memory and frame pacing](05-x11-pixels-and-shared-memory.md) (stage 3)

### Part III. Wayland
- [6. Your first Wayland window](06-wayland-first-window.md) (stage 4)
- [7. Wayland input, frame pacing, decorations and HiDPI](07-wayland-input-and-decorations.md) (stage 5)

### Part IV. One program for both
- [8. A platform layer over two backends](08-platform-layer.md) (stage 6)

### Part V. Drawing and UI
- [9. 2D rendering with Cairo and Pango](09-rendering-with-cairo-and-pango.md) (stage 7)
- [10. An immediate mode UI toolkit](10-immediate-mode-ui.md) (stage 8)
- [11. The widget catalogue: standard controls for a Win32 programmer](11-widget-catalogue.md) (stage 10)
- [12. Building a real application, and testing it without a screen](12-building-and-testing-an-app.md) (stage 9)

### Part VI. Beyond
- [13. Where to go next](13-where-next.md)

### Appendices
- [A. Solutions](appendix-a-solutions.md)
- [B. Cheat sheets](appendix-b-cheatsheets.md)
- [C. Troubleshooting](appendix-c-troubleshooting.md)
- [D. Glossary](appendix-d-glossary.md)

## Chapter and stage map

| Chapter | Stage | Code |
|---|---|---|
| 2 | 0 | `p00_env_check` |
| 3 | 1 | `p01_x11_window` |
| 4 | 2 | `p02_x11_events` |
| 5 | 3 | `p03_x11_pixels` |
| 6 | 4 | `p04_wayland_window` |
| 7 | 5 | `p05_wayland_input` |
| 8 | 6 | `p06_platform_layer`, `lib/platform` |
| 9 | 7 | `p07_renderer`, `lib/renderer` |
| 10 | 8 | `p08_widgets`, `lib/ui` |
| 11 | 10 | `p10_controls`, `lib/ui/src/ui_controls.cpp` |
| 12 | 9 | `p09_polish`, `tests/` |

Chapter 11 extends the toolkit of chapter 10 (stage 10) and is read before chapter 12 (stage 9), which then uses it as an application. Stage 11 (GPU rendering) is described in chapter 13 and not implemented.

## A note on the code

Chapters quote code from the repository, sometimes shortened. When a snippet and the file disagree, the file is right. The code is written for clarity first. Known limitations are listed in chapter 13, section 13.1.
