# Chapter 12. Building a real application, and testing it without a screen

**You will learn**

- How to structure an application on top of the toolkit: state, build function, loop
- How to prove the "idle uses no CPU" claim instead of trusting it
- How to test a GUI headlessly: synthetic input, rendered frames, widget rectangles
- How to run the same checks under sanitizers and on both compilers
- What a good test plan for a windowing program contains

**Code for this chapter:** `p09_polish/main.cpp` (174 lines), `tests/ui_test.cpp`, `tests/CMakeLists.txt`, and `p08_widgets/gallery.hpp`.

---

## 12.1 Stage 9: a form that looks like an application

Stage 9 adds no new technique. That is deliberate: it is the toolkit used the way you would use it in a real program, a small "Account settings" form. Run it:

```
./build/gcc-debug/p09_polish/p09_polish
```

It has text fields (name, email, bio), copy and paste buttons that use the system clipboard, a dropdown, a slider, radio buttons, checkboxes, a dark theme toggle, and Save and Reset buttons. The header shows which backend it runs on, the scale, and a **frames drawn** counter.

## 12.2 The shape of an application

Three parts, and every program built on this book has them.

**1. State.** A plain struct holding everything the user can change, and nothing about widgets.

```cpp
struct Settings {
    std::string displayName = "Ash";
    std::string email;
    bool darkTheme = true;
    int visibility = 0;          // 0 public, 1 friends, 2 private
    std::vector<std::string> languages = {...};
    int language = 0;
    float fontScale = 0.5f;
    std::string status = "Ready";
    int framesDrawn = 0;
};
```

**2. A build function.** Describes the UI from the state and applies user actions to it.

```cpp
void build(ww::ui::Context& ui, Settings& s, ww::Window& win) {
    ui.beginCard("Profile");
    ui.beginRow();
    ui.width(110); ui.label("Display name");
    ui.width(-1);  ui.textField("name", s.displayName, "Your name");
    ui.endRow();
    ...
    if (ui.button("Save", true)) s.status = "Saved settings for " + s.displayName;
}
```

**3. A loop.** Waits for events, feeds the toolkit, draws when something changed (chapter 10, section 10.2). It is the same loop as stage 8, and in a larger program you would write it once and reuse it.

Notes on the details:

- **Clipboard buttons** call `win.setClipboardText(...)` and `win.clipboardText()`. Because `build` receives the window, the buttons use the platform layer directly. For ctrl+C and ctrl+V inside text fields, `ui.setClipboard(set, get)` connects the toolkit to the same window functions.
- **Theme toggle** reads the checkbox into a bool and, if it changed, calls `ui.setTheme(...)`. The new theme is used from the next widget call.
- **Reset** replaces the state with a default constructed one, but keeps the theme choice. Because all state is in one struct and the UI is a function of it, "reset" is one line. This is a real benefit of immediate mode: there is no widget tree to reset, nothing can be left half updated.
- **The status line** is plain state, set by actions and displayed by a label. No signals needed.

### Separating logic from the UI

As a program grows, keep application logic out of `build`. A good split:

```
 settings.cpp    pure logic: validate email, save to disk. No UI, no window. Unit testable.
 app_ui.cpp      build(ui, state): calls widgets, calls logic on actions.
 main.cpp        window, loop, wiring.
```

`build` should decide *what to show*, and call small functions to *do things*. When logic hides inside the UI code, you can only test it by clicking.

## 12.3 Proving that idle means idle

Stage 9 shows `framesDrawn` in its header. Use it as an instrument:

1. Start the program, click once in an empty area, then leave the mouse and keyboard alone.
2. Watch the counter and `top -p $(pidof p09_polish)`.

You should see CPU near 0 percent and a frame counter that **does not change**. Now click into a text field. The counter rises about twice per second (the caret blink, every 530 ms, from `nextWakeupMs`) and CPU stays near zero. Click outside. It stops again.

If the counter rises when nothing is happening, something is requesting redraws it should not. Typical causes: an animation left on, `dirty = true` set unconditionally, or an event type you treat as input that the compositor sends continuously.

A second check: `strace -c -p <pid>` for ten seconds on an idle window should show it almost entirely asleep in `poll` or `ppoll`.

## 12.4 Why test a GUI without a screen?

GUI programs are often untested because testing seems to need clicking. But the toolkit here separates concerns cleanly enough that almost everything is testable without any window:

- **Input** is plain data (`MouseButtonEvent`, `KeyEvent`, `TextEvent`). A test can synthesise it.
- **Drawing** goes through the `Renderer`, which can target memory (`OffscreenSurface`).
- **State** belongs to your program, so you can read it after the "user" acted.

So a test is: build the UI, send events, build again, look at the state.

## 12.5 HeadlessUi

`ui_headless.hpp` is a small helper that plays the role of the window and the event loop:

```cpp
class HeadlessUi {
public:
    HeadlessUi(int logicalWidth, int logicalHeight, float scale = 1.0f);
    Context& context();

    void frame(const std::function<void(Context&)>& build);   // one settled frame, like drawFrame

    void mouseMove(float x, float y);
    void mouseDown(float x, float y, int clickCount = 1);
    void mouseUp(float x, float y);
    void scroll(float x, float y, float notches);
    void key(Key key, Modifiers mods = {});
    void type(const std::string& utf8);

    bool savePng(const std::string& path);
};
```

It owns an `OffscreenSurface` and a `Context`. `frame` renders one settled frame (with the two pass rule when input is pending), and the input methods call `handleEvent` with the same event types a real window produces. Because it feeds the real toolkit with the real event types, a passing test says something true about the real program.

## 12.6 Finding widgets: `lastRect`

The test must click on a button, but where is it? The toolkit exposes `ui.lastRect()`, the rectangle of the most recent widget. The gallery stores some:

```cpp
if (ui.button("Primary", true)) s.clicks += 10;
s.primaryButtonRect = ui.lastRect();
```

The test then clicks the centre of that rectangle. This keeps tests robust against layout changes: if the button moves, the test follows it. Positions are never hard coded, except for tab headers which are as wide as their text, where the test clicks at fixed offsets from the tab bar's left edge.

## 12.7 Reading the test file

`tests/ui_test.cpp` has a tiny framework, enough for this job:

```cpp
int failures = 0;
#define CHECK(cond)  do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
```

`CHECK` reports the file, line and expression, and **continues**, so one run lists every failure. `main` returns non zero if any failed, which is how `ctest` knows.

A **fixture** holds the state and the headless UI and builds the first frame:

```cpp
struct Fixture {
    gallery::State state;
    ww::ui::HeadlessUi h{900, 640};
    std::string clipboard;

    Fixture() {
        h.context().setClipboard([this](const std::string& t) { clipboard = t; },
                                 [this] { return clipboard; });
        frame();
    }
    void frame() { h.frame([this](ww::ui::Context& ui) { gallery::build(ui, state); }); }
    void click(ww::Rect r, float dx = 0.5f) { ... mouseDown, mouseUp, frame ... }
};
```

Notice the fake clipboard: a string in the fixture. The test checks copy and paste behaviour without needing a window system.

### A button test

```cpp
void testButtons() {
    Fixture f;
    CHECK(f.state.clicks == 0);
    f.click(f.state.primaryButtonRect);
    CHECK(f.state.clicks == 10);
}
```

### A text editing test

```cpp
f.gotoTab(1);
f.click(f.state.nameFieldRect, 0.9f);       // click near the right end: caret goes to the end
f.type(" M");
CHECK(f.state.name == "Ash M");
f.key(ww::Key::Backspace);
CHECK(f.state.name == "Ash ");
f.key(ww::Key::A, ww::Modifiers{.ctrl = true});   // select all
f.type("Bob");                                     // typing replaces the selection
CHECK(f.state.name == "Bob");
```

This tests focus, caret placement from a click, text insertion, Backspace, select all and replace, in about ten lines. The next lines copy to the fake clipboard, paste into the other field, and cut.

### The UTF-8 test

```cpp
f.type("h\xC3\xA9");             // "hé"
CHECK(f.state.email == "h\xC3\xA9");
f.key(ww::Key::Backspace);       // must remove the whole character
CHECK(f.state.email == "h");
```

If Backspace removed one byte, the string would end with a lone `0xC3`, invalid UTF-8. This is the kind of bug that a visual check never finds.

### The drag test

```cpp
f.h.mouseDown(x0, y);   f.frame();    // press on the slider
f.h.mouseMove(x1, y);   f.frame();    // drag
f.h.mouseUp(x1, y);     f.frame();
CHECK(f.state.volume > 0.8f && f.state.volume < 0.95f);
```

Why `frame()` between the events? The toolkit decides hot and active **while drawing**. A press in the same frame in which the mouse arrived has no earlier frame to learn that the slider is hot, so tests, like real input, need a frame between press and drag. (In the real application events arrive between frames naturally.) This bug in a first draft of the test taught the lesson, and it is a good example of how tests teach you about the model.

### The scale test

The same click at scale 2 must have the same effect. The test creates `HeadlessUi h(900, 640, 2.0f)` and clicks the button at logical coordinates. It passes only if input coordinates and layout are both in logical units.

## 12.8 Running the tests, with sanitizers

```
ctest --test-dir build/gcc-debug --output-on-failure
cmake --preset clang-asan && cmake --build --preset clang-asan
ctest --test-dir build/clang-asan --output-on-failure
```

AddressSanitizer would detect an out of bounds read in the text editing code or a use after free in the layout stacks. UndefinedBehaviorSanitizer catches signed overflow and invalid casts.

One practical note. The tests set `ASAN_OPTIONS=detect_leaks=0` in `tests/CMakeLists.txt`. Fontconfig, loaded by Pango, keeps process wide caches that LeakSanitizer reports as leaks. They are not leaks in our code, and filtering them keeps the signal clean. If you want leak checking for your own code, use a suppression file for the fontconfig stack instead of turning detection off globally.

Why run both compilers? Different warnings and different sanitizer runtimes. The repository's presets make it one command each.

## 12.9 Seeing what the tests cannot

Unit tests can say "the state is right". They cannot say "it looks right". For that, render to a PNG and look:

```
./build/gcc-debug/p08_widgets/p08_widgets --screenshot /tmp/shot
```

writes `shot_1_basics.png` and three more. Open them. In a real project you could compare against stored reference images with a tolerance, but start by looking, and add golden images only for things that are stable and important, because any font or Cairo upgrade changes pixels slightly.

The headless approach found real issues while this book's code was written: a progress bar lagging one frame behind a slider (leading to the two pass rule), a card sized with the previous frame's height (leading to the settle loop), and a drag test that passed only after a frame was inserted between press and move.

## 12.10 What this does not test

Be honest about the limits of your test suite.

| Not covered by `ui_test` | How to cover it |
|---|---|
| The real X11 or Wayland backends | Run the stages under `Xvfb` with `xdotool`; headless `weston` for Wayland protocol traffic |
| Real keyboard layouts, IME, dead keys | Manual testing on several layouts |
| Compositor differences (decorations, scale, repeat) | Run on GNOME, KDE, and Sway |
| Real display scaling and fractional scales | A physical HiDPI monitor, or `WW_SCALE` |
| Timing (blink, double click interval) | Inject a fake clock into the toolkit (an exercise below) |
| Accessibility | Not implemented, see chapter 13 |

A test plan for a windowing program has three layers: fast headless tests of logic and the toolkit (this chapter), scripted tests of each backend under virtual displays, and a short manual checklist for things only humans can judge.

## 12.11 A manual checklist

Run through this on each backend, ideally on two compositors:

1. Window opens with the right title, at a sensible size, no flicker.
2. Resize by dragging each edge and corner. No garbage, no stretched content.
3. Minimum size is enforced.
4. Maximise, restore, fullscreen if supported.
5. Move the mouse over every kind of widget: cursor changes (arrow, I-beam, hand).
6. Type text with your normal layout and one other (for example German or French with accents).
7. Hold a key: repeat starts after the delay at the expected rate and stops on release or focus loss.
8. Copy from the app and paste into another app. Copy from another app and paste into yours.
9. Scroll with a wheel and a touchpad.
10. Idle for a minute: CPU is near zero, the frame counter is still.
11. Close with the title bar button and with Escape or your own shortcut: clean exit, exit code 0.
12. Run on a scaled display (`WW_SCALE=2`): crisp text, correct hit targets.

## Pitfalls

- **Testing through pixels.** Test state first, pixels rarely.
- **Hard coded coordinates in tests.** They break on every layout change. Use `lastRect`.
- **A `frame()` missing between events** that depend on hot or active state.
- **Tests that depend on the real clipboard or display.** They fail in CI. Inject fakes.
- **Time dependent tests** (blink, double click) that use real sleeps. They are slow and flaky.
- **Treating "headless passes" as "works on Wayland".** It does not touch the platform layer.
- **No idle check.** An accidental redraw loop is invisible until a laptop battery drains.

## Review questions

1. List the three parts of an application built on this toolkit.
2. How can you tell, from the UI, that an idle window uses no CPU?
3. Why does `HeadlessUi` give meaningful results although it never opens a window?
4. Why do the drag tests call `frame()` between the press and the move?
5. Name two things `ui_test` cannot catch, and how you would test them.

## Exercises

**Ex 12.1** Add a Save validation to stage 9: when the email does not contain `@`, show an error label in red and do not save. Put the validation function in its own file with a unit test that needs no UI.

**Ex 12.2** Write a test for stage 9's Reset button. You will need to make `build` callable from a test: move it and `Settings` into a header. Which widget rectangles do you need to record?

**Ex 12.3** Write a test for Tab focus: press Tab several times and verify that each press moves focus (for example through the position of the focus ring, or by activating the focused button with Space and checking the counter).

**Ex 12.4** Write a test for the dropdown that opens it, presses Escape, and verifies that the selection did not change.

**Ex 12.5** Add golden image testing: render the gallery tab 1 to PNG, save it as a reference, and add a test that renders again and compares with a tolerance of a few levels per channel. Discuss what breaks it.

**Ex 12.6** Introduce a clock abstraction into the toolkit (`std::function<time_point()>`, default `steady_clock::now`), so tests can advance time. Use it to test that the caret blink phase flips every 530 ms and that typing resets it.

**Ex 12.7** Run the tests under `valgrind`. What does it report that ASan did not? (Hint: look at fontconfig and glib.)

Solutions: [solutions/ch12.md](solutions/ch12.md)

Previous: [Chapter 11](11-widget-catalogue.md) | Next: [Chapter 13, where to go next](13-where-next.md)
