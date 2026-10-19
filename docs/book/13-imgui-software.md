Chapter 13. Dear ImGui on a CPU rasterizer
You will learn

What Dear ImGui is, how it relates to the toolkit of chapters 10 and 11, and what a backend is
How to add a third party library to a CMake project with FetchContent
How window events become ImGui input, and how the clipboard, cursor and HiDPI scale are wired
What ImGui produces each frame (ImDrawData) and how its font atlas is created on demand
How to rasterize textured, vertex coloured triangles yourself: edge functions, fixed point, the top-left rule, barycentric interpolation, clipping and blending
Why no graphics library is needed to run a full ImGui user interface
Code for this chapter: cmake/DearImGui.cmake (39 lines), lib/imgui/src/imgui_host.cpp (173), lib/imgui/src/imgui_soft.cpp (316), p11_imgui_soft/main.cpp and gallery.hpp, and tests/imgui_soft_test.cpp (291).

Prerequisite: chapter 8 (events and the framebuffer) and chapter 10 (immediate mode). Chapter 9 helps for the comparison with Cairo.

13.1 What Dear ImGui is
Dear ImGui (written "ImGui" here) is a C++ library by Omar Cornut for immediate mode user interfaces. It is widely used in game tools, profilers and editors. It is not part of any desktop toolkit: it knows nothing about windows, keyboards or graphics APIs. You give it mouse, keyboard and time. It gives you back a list of triangles.

You have already written an immediate mode toolkit, so the programming model is familiar:

if (ImGui::Button("Click me")) ++clicks;
ImGui::Text("clicked %d times", clicks);
ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f);

Compare with ui.button("Save") from chapter 11. The two libraries answer the same question the same way. They differ in what they do after the widget logic:

ww::ui::Context (chapters 10, 11)	Dear ImGui
Widgets decide what to draw	yes	yes
Drawing	Calls Renderer methods (fillRoundRect, drawText) right away, Cairo and Pango draw	Appends triangles to draw lists. Nothing is drawn until you render them
Text	Pango (shaping, fallback fonts, bidirectional text)	Built in stb_truetype rasterizer into an atlas texture, one font by default
Windows	One surface, overlays only	Floating, movable, resizable ImGui windows inside your OS window
Backend	The Renderer interface is the seam	Two seams: platform (input) and renderer (triangles)
Size	about 2,000 lines	about 60,000 lines in the five core files and two main headers
The last row is the reason to use it: ImGui has a very large catalogue of widgets (tables, plots, trees, drag and drop, multi select, color pickers, a demo window that shows all of them) that you would otherwise write yourself. The price is that its look is its own and its text is simpler than Pango's.

Which one should I use? For an application with text-heavy UI, right to left text or the platform's look and feel, neither is ideal (see chapter 15). For tools, debug overlays, editors and prototypes, ImGui saves months. Learning it by writing the backend (this chapter and the next) shows exactly what you depend on.

13.2 Two halves of a backend
ImGui deliberately leaves two jobs to you.

The ImGui backend (this chapter)

Our platform layer (chapter 8)

ww::Event

io.AddMousePosEvent, AddKeyEvent ...

ImDrawData

pixels

cursor shape, clipboard

ww::Window
events, framebuffer, clipboard, cursor

ww::imgui::Host
platform half:
events to ImGuiIO

ww::imgui::SoftRenderer
renderer half:
ImDrawData to pixels

Dear ImGui core
widgets, layout, fonts, draw lists

Your UI code
ImGui::Button, ImGui::Text ...

The platform half (Host, section 13.5) gives ImGui the input and the size of the window, and carries ImGui's requests (which cursor to show, copy this text) back to the window.
The renderer half (SoftRenderer, sections 13.7 to 13.10) turns the finished frame into pixels. Chapter 14 replaces only this half with an OpenGL one.
The two halves are independent on purpose. Host never calls the renderer and the renderer never touches events, which is why chapter 14 can swap one without changing the other.

ImGui ships ready-made backends for GLFW, SDL, Win32, OpenGL, Vulkan and others. None of them is used here, because the point of this book is the layer underneath. Everything below is ours, about 550 lines in total.

13.3 Getting ImGui into the build
ImGui is a library you copy into your project: it has no CMakeLists.txt, no install step and no packages in the distribution you can rely on (the versions in apt are old and lack the 1.92 texture API we use). CMake can download it for you at configure time with FetchContent. From cmake/DearImGui.cmake:

set(WW_IMGUI_TAG "v1.93.0" CACHE STRING "Dear ImGui release tag to fetch")

if(NOT DEFINED FETCHCONTENT_BASE_DIR)
  set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/_deps" CACHE PATH "Where FetchContent keeps downloads")
endif()
include(FetchContent)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        ${WW_IMGUI_TAG}
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)   # imgui has no CMakeLists.txt, so this only downloads

add_library(ww_imgui_core STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp)
target_include_directories(ww_imgui_core SYSTEM PUBLIC ${imgui_SOURCE_DIR})
target_compile_definitions(ww_imgui_core PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

Points worth knowing:

A fixed tag, not master. ImGui changes its API regularly (1.92 replaced the font atlas upload code, section 13.8). A tag makes every checkout build the same code. GIT_SHALLOW avoids downloading the history.
FETCHCONTENT_BASE_DIR is set before including FetchContent. The module sets its own default the moment you include it, so setting the variable afterwards has no effect. We point it at build/_deps so the six presets (gcc-debug, clang-asan ...) share one download instead of fetching six times. make distclean deletes it, and the next configure fetches again.
FetchContent_MakeAvailable on a project without a CMakeLists.txt only populates imgui_SOURCE_DIR. We then compile the five core .cpp files ourselves. The imgui_demo.cpp file is the big demo window (ImGui::ShowDemoWindow), which the gallery's View menu opens. You can drop it from the list to compile less.
SYSTEM include directories make GCC and Clang suppress warnings from ImGui's headers, so our -Wall -Wextra -Wconversion stays quiet about code we do not own. The ImGui sources are compiled without ww_target_defaults for the same reason.
IMGUI_DISABLE_OBSOLETE_FUNCTIONS removes old API names, so a tutorial that uses one fails to compile instead of silently working against a deprecated path.
Offline. If you cannot reach GitHub, clone ImGui yourself and run cmake --preset gcc-debug -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui. To skip the ImGui stages entirely: -DWW_BUILD_IMGUI=OFF. The option exists because a configure step that needs the network is a surprise for anyone who only wanted chapters 1 to 12.
In the top level CMakeLists.txt the stages are optional:

option(WW_BUILD_IMGUI "Build the Dear ImGui stages (downloads ImGui with FetchContent)" ON)
...
if(WW_BUILD_IMGUI)
  include(DearImGui)

13.4 The frame
Every ImGui program has the same four steps per frame. From p11_imgui_soft/main.cpp:

void frame(ww::imgui::Host& host, ww::imgui::SoftRenderer& soft, gallery::State& state, const ww::Framebuffer& fb) {
    host.prepareFrame();                       // 1. size, scale, delta time into ImGuiIO
    ImGui::NewFrame();                         // 2. begin: ImGui processes the queued input
    gallery::tick(state, ImGui::GetIO().DeltaTime);
    gallery::build(state);                     //    your UI: ImGui::Begin, Button, Text ...
    ImGui::Render();                           // 3. end: produce ImDrawData
    soft.render(ImGui::GetDrawData(), fb, kClear);   // 4. draw it
    ...
}

SoftRenderer
ImGui
Host
ww::Window
SoftRenderer
ImGui
Host
ww::Window
Nothing is processed yet
Queued input is applied.
Widgets run and decide.
Event (mouse move, key, text ...)
io.AddMousePosEvent(...) and friends (queued)
prepareFrame(): DisplaySize, scale, DeltaTime
NewFrame()
Your UI calls: Begin, Button, Text ...
Render()
ImDrawData (draw lists, textures)
pixels in the framebuffer
present()
Two things differ from our toolkit's drawFrame (chapter 10):

ImGui queues input. AddMousePosEvent does not change the state immediately. NewFrame() applies the queue, and it may spread several events (a move, a press and a release in the same instant) over several frames so that a fast click is not lost. A test that sends a click must therefore run a frame after each event, which tests/imgui_soft_test.cpp does.
ImGui needs more than one frame to settle. A window that opens in frame 1 is laid out with its contents known only after the frame; the auto-sized size takes effect in frame 2. Hover effects and popups likewise. Our toolkit solved the same problem with the two pass drawFrame. ImGui's answer is "draw a few frames after input". The demo does this in the loop:
using Clock = std::chrono::steady_clock;
auto busyUntil = Clock::now();
...
const bool busy = Clock::now() < busyUntil;
bool have = window->waitEvent(ev, busy ? 16 : 500);   // 60 fps while busy, otherwise twice a second
while (have) {
    ...
    if (host.handleEvent(ev)) busyUntil = Clock::now() + std::chrono::milliseconds(400);
    have = window->pollEvent(ev);
}

After any input the program renders at about 60 frames per second for 400 ms, so hover fades, scrolling and popups finish, and then it sleeps. The 500 ms wake up keeps the text cursor blinking. This is a compromise: most ImGui programs render continuously, which is simple and wastes power. Run top while a window is idle to see that this one does not.

13.5 The platform half: Host
ww::imgui::Host (lib/imgui/include/ww/imgui_host.hpp) is a small class. It creates the ImGui context in its constructor and destroys it in its destructor, and it has four jobs.

Setting up
Host::Host(Window* window) : window_(window) {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "linux-windowing";
    io.BackendPlatformUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.IniFilename = nullptr;       // do not litter the working directory
    ...
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = clipboardGet;
    pio.Platform_SetClipboardTextFn = clipboardSet;

IMGUI_CHECKVERSION() compares the header's version and struct sizes with those compiled into the library, and aborts with a message if you mixed them. It catches an include path that picks up an old imgui.h.
Backend flags are how a backend tells ImGui what it supports. HasMouseCursors says "I will honour the cursor shape you ask for". The renderer half adds RendererHasTextures (section 13.8).
IniFilename = nullptr: by default ImGui saves window positions in imgui.ini in the current directory. For a book example that is surprising, so it is off. An application sets a path under $XDG_CONFIG_HOME.
The clipboard. ImGui calls plain C function pointers, with the context as the only argument. Our Host* travels in io.BackendPlatformUserData:
const char* clipboardGet(ImGuiContext* ctx) {
    Host* host = hostOf(ctx);
    host->clipboardOut_ = host->window_ ? host->window_->clipboardText() : host->clipboard;
    return host->clipboardOut_.empty() ? nullptr : host->clipboardOut_.c_str();
}

The returned pointer must stay valid after the function returns, so the string is kept in a member. With no window (tests, screenshots) a plain std::string stands in for the clipboard.

Events to input
Host::handleEvent is a table. The interesting rows:

ww::Event	ImGui call	Remark
MouseMoveEvent{x, y}	AddMousePosEvent(x, y)	Logical units, which is what ImGui expects
MouseLeaveEvent	AddMousePosEvent(-FLT_MAX, -FLT_MAX)	ImGui's way of saying "no pointer", so hover highlights go away
MouseButtonEvent	AddMouseButtonEvent(0 or 1 or 2, pressed)	Left, right, middle. Also sends the position, so a click is never at a stale place
ScrollEvent{dx, dy}	AddMouseWheelEvent(-dx, -dy)	Signs flip. In ww, a positive dy means "content moves up" (scroll down). In ImGui a positive wheel value scrolls up
KeyEvent	AddKeyEvent(toImGuiKey(key), pressed)	Auto-repeat presses are dropped, because ImGui makes its own repeats with io.KeyRepeatDelay
TextEvent	AddInputCharactersUTF8(utf8)	Typed text is separate from keys, as in chapter 8
FocusEvent	AddFocusEvent(focused)	Lets ImGui release held keys when the window loses focus
ResizeEvent	remembered for prepareFrame	
Modifier state travels with every event in ww (event.mods). ImGui wants the modifiers as keys of their own, so updateModifiers sends ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt and ImGuiMod_Super before the real event. Without that, ImGui::IsKeyPressed(ImGuiKey_C) would work but Ctrl+C in a text field would not.

The key map toImGuiKey exploits our key numbering from chapter 8: Key::A equals 'a', so letters become ImGuiKey_A + (k - 'a') and digits likewise. Function keys are consecutive in both enums. The rest is a switch. A key that is not mapped returns ImGuiKey_None, and the event is simply not forwarded.

Per frame state
void Host::prepareFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(widthPx_ / scale_, heightPx_ / scale_);
    io.DisplayFramebufferScale = ImVec2(scale_, scale_);
    ...
    io.DeltaTime = now - lastTime_;   // clamped to 1 ms .. 250 ms
}

DisplaySize is in logical units, the same unit our events use. On a 2x display a 960 x 640 window has a 1920 x 1280 framebuffer, and DisplaySize stays 960 x 640. All widget sizes (button heights, padding) are in these units, so the interface keeps its size.
DisplayFramebufferScale tells ImGui the framebuffer has twice as many pixels. Since version 1.92 ImGui reads it to bake glyphs at that density by itself: text is drawn with twice the detail at the same logical size. In older versions you had to rebuild the font atlas yourself. (This book's code first multiplied the font size by the scale, which made everything twice as big. The screenshot showed it immediately.)
DeltaTime drives animations, tooltip delays and key repeat. It is clamped: a long sleep (the 500 ms idle wake up, or a laptop waking from suspend) must not become one huge animation step. Tests call setFixedDeltaTime(1/60) so frames are deterministic.
Cursor
After the frame, applyCursor() asks ImGui::GetMouseCursor() and forwards it only when it changed:

window_->setCursor(c == ImGuiMouseCursor_TextInput ? Cursor::IBeam
                   : c == ImGuiMouseCursor_Hand ? Cursor::Hand : Cursor::Arrow);

Our ww::Cursor has three shapes. ImGui has more (resize arrows, not allowed); they fall back to the arrow. Extending the platform layer for them is exercise 13.6.

One context at a time
Host's destructor first calls ImGui::SetCurrentContext(context_), then clears the pointers it set in ImGuiIO, then destroys the context. ImGui asserts that a backend cleaned up ("Forgot to shutdown Platform backend?"), which is why BackendPlatformUserData is reset. The first run of this code hit that assertion.

ImGui has a current context global, and its API functions use it. Two Host objects alive at once would fight over it. Treat "one Host per program" as the rule; the tests create and destroy one per test case.

Destruction order matters. The renderer must be destroyed before the Host, because the renderer frees textures through the live context. In main, declare the Host first and the renderer second: C++ destroys locals in reverse order.

ww::imgui::Host host(window.get());   // creates the ImGui context
ww::imgui::SoftRenderer soft;         // declared after the Host, so it is destroyed first

13.6 What ImGui produces: ImDrawData
After ImGui::Render(), ImGui::GetDrawData() returns the whole frame as data.

1

*

1

*

1

*

ImDrawData

DisplayPos, DisplaySize

FramebufferScale

CmdLists[]

Textures[]

ImDrawList

VtxBuffer : ImDrawVert[]

IdxBuffer : ImDrawIdx[]

CmdBuffer : ImDrawCmd[]

ImDrawCmd

ClipRect

VtxOffset, IdxOffset

ElemCount

UserCallback

TexRef / GetTexID()

ImDrawVert

pos : ImVec2

uv : ImVec2

col : ImU32

One draw list per ImGui window (plus lists for the background, the foreground and tooltips). Each has a vertex array and an index array. Vertices are shared between triangles: a rectangle is 4 vertices and 6 indices.
An ImDrawVert is 20 bytes: position, texture coordinate and a colour. The position is in logical units. The colour is a packed ImU32 in the order 0xAABBGGRR, red in the lowest byte, which is not our framebuffer's 0xAARRGGBB. Every conversion between them in this chapter is deliberate.
An ImDrawCmd says: "draw ElemCount indices starting at IdxOffset, with this clip rectangle and this texture". A window usually has one command. A new command starts when the clip rectangle or the texture changes (a scrolling child region, an image).
VtxOffset is added to every index of the command. It lets a draw list with more than 65,535 vertices still use 16 bit indices. Backends declare they handle it with ImGuiBackendFlags_RendererHasVtxOffset; our renderer does, as vtx + cmd.VtxOffset.
UserCallback: a function pointer a program can insert between commands, for example to switch shader. A renderer must call it and skip the triangles. ImDrawCallback_ResetRenderState used to be a magic constant; in 1.93 a backend publishes its own through ImGui::GetPlatformIO().DrawCallback_ResetRenderState. Our renderers keep no state between commands, so theirs is an empty function.
What does ImGui draw? Only triangles, with these patterns:

A filled rectangle (window background, button, glyph quad): 2 triangles, one colour. Exactly the rectangles that make up most of the frame.
Anti-aliasing is geometry. A line, or the edge of a filled shape, gets an extra strip of thin triangles whose outer vertices have alpha 0. The rasterizer interpolates the alpha across the strip and the edge appears smooth. A rasterizer that interpolates colour correctly gets anti-aliasing for free. This is also why the tests switch it off (AntiAliasedFill = false) when they want exact pixel counts.
Text: one textured rectangle per glyph. The texture is the font atlas, the vertex colour is the text colour.
A gradient: a rectangle whose four vertices have different colours.
The Renderer window of the gallery shows the numbers. With the ImGui demo window open at 960 x 640 it reports about 1,550 triangles per frame, of which about 950 are the 477 rectangles of the fast path in section 13.9. For a screen with 614,400 pixels that is few primitives. The cost is in the pixels they cover, not in how many there are (section 13.11).

13.7 The font atlas and ImTextureData
ImGui's text is one textured rectangle per glyph, so the renderer needs the atlas as a texture. Until 1.91 the application called GetTexDataAsRGBA32, uploaded the pixels once and set the texture id. Since 1.92 the atlas is dynamic: glyphs are rasterized on demand when first used, at the size and density needed, and the atlas can grow. The renderer is told what to do through ImTextureData, and a backend that supports this sets ImGuiBackendFlags_RendererHasTextures.

Each frame, ImDrawData::Textures lists the textures. Each has a status:

ImGui needs the atlas

backend creates and uploads (SetTexID, SetStatus)

new glyphs baked into free space

backend copies the changed rectangles

no longer used

backend frees it

Destroyed

WantCreate

OK

WantUpdates

WantDestroy

Our CPU backend handles it in SoftRenderer::updateTexture:

if (tex->Status == ImTextureStatus_WantCreate) {
    auto* t = new Texture;                    // our own struct: width, height, vector<uint32_t> rgba
    ...
    tex->BackendUserData = t;
    tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)));   // the "id" is our pointer
    tex->Updates.push_back(whole image);      // the first upload is everything
    tex->Status = ImTextureStatus_WantUpdates;
}
if (tex->Status == ImTextureStatus_WantUpdates) {
    for (const ImTextureRect& r : tex->Updates) { copy rows r.y .. r.y + r.h }
    tex->SetStatus(ImTextureStatus_OK);
}

A texture id is whatever the backend wants, 64 bits wide. A GPU backend stores a GL texture name (chapter 14). Ours stores the address of a Texture struct; cmd.GetTexID() in the draw loop gives it back. An id of 0 (ImTextureID_Invalid) means "no texture": a solid fill, which our rasterizer detects as tex == nullptr.
The atlas format is either RGBA32 or Alpha8. The default is RGBA32 (4 bytes per texel). A text only atlas needs just alpha, so ImGui can use Alpha8 (io.Fonts->TexDesiredFormat). The CPU renderer converts Alpha8 to white texels with that alpha (0xAAFFFFFF), so the shader-free formula "vertex colour times texel" is the same for both. The OpenGL renderer will keep one byte per texel and use a swizzle instead (section 14.8).
Only the changed rectangles are copied (tex->Updates). With a CPU texture copying all of it would also work, and the incremental version is the one a GPU needs.
Shutdown. The renderer's destructor walks ImGui::GetPlatformIO().Textures, frees what it created and marks them Destroyed, as ImGui's own backends do. If it did not, ImGui would still point to freed memory when the context is destroyed.
One context, one renderer. The texture id belongs to the renderer that created it. Feeding the same ImGui context's draw data to a second renderer would pass our pointer to code that expects a GL name. The comparison tests in chapter 14 therefore run the same deterministic frames twice, once per renderer.
13.8 Rasterizing a triangle
This is the core of the chapter. A rasterizer answers one question for each triangle: which pixels does it cover, and what colour are they?

yes

no

no

yes

Triangle (a, b, c)
positions, colours, UVs

Convert to pixels
pos x scale, snap to 1/256

Area zero?

Skip

Bounding box, clipped
to the scissor rectangle

For each pixel in the box:
centre = (x + 0.5, y + 0.5)

Inside all three edges?
(top-left rule on ties)

Weights from the edge values
colour and UV by interpolation

Sample the texture, multiply by colour

Blend over the pixel

Edge functions
For a triangle a, b, c and a point p, the edge function is the cross product (b - a) x (p - a): positive when p is on one side of the line ab, negative on the other, zero on the line. A point is inside the triangle when it is on the same side of all three edges. The three values are also useful as numbers: divided by the triangle's area (the same formula with c as the point) they are the barycentric coordinates l0, l1, l2 of p, three weights that add up to 1. Any quantity known at the vertices can then be interpolated: colour(p) = l0 * colour(a) + l1 * colour(b) + l2 * colour(c).

inline int64_t edge(const Point& a, const Point& b, const Point& p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

Fixed point
Positions are converted to integers with 8 fractional bits:

constexpr int kSubpixel = 8;
inline int64_t toFixed(float v) { return static_cast<int64_t>(std::lround(v * 256.0f)); }

Why not floats? Two triangles that share an edge (every rectangle is two) must agree on exactly which pixels lie on that edge. With floating point, edge(a, b, p) and edge(b, a, p) can round differently, leaving a gap or a pixel drawn twice. A pixel drawn twice is invisible for opaque colours but is a visible line for a half transparent one. With integers the calculation is exact: products of 24 bit values fit easily in 64 bits.

The top-left rule
A pixel centre can lie exactly on an edge, which happens all the time because rectangles are placed on whole pixels. Whose pixel is it? The rule (the same one Direct3D, OpenGL and Vulkan use) is: the triangle owns pixels on its top and left edges, not on its bottom and right edges.

inline bool isTopLeft(const Point& a, const Point& b) {
    const int64_t dx = b.x - a.x, dy = b.y - a.y;
    return dy < 0 || (dy == 0 && dx > 0);
}
...
const int64_t bias0 = isTopLeft(b, c) ? 0 : -1;
...
if ((w0 | w1 | w2) < 0) continue;   // outside

Edges that are not top or left get a bias of -1 so that a value of exactly zero counts as outside. (w0 | w1 | w2) < 0 is a trick: the sign bit of an OR is set if any operand is negative, so one test checks all three.

Before this, drawTriangle flips the winding of triangles that are the "wrong way round" (area < 0: swap two vertices), so the sign conventions and isTopLeft hold for every triangle. The test testTopLeftRuleNoDoubleBlend draws a half transparent diamond and checks that every covered pixel has exactly one blend applied: 128 for red over black, not 192, which would show along the diagonal.

Interpolating
Most triangles are flat: one colour, no texture variation. The code tests this once per triangle (flatColour, flatUv) and skips the per pixel interpolation for them. For the rest it computes weights from the edge values (the bias is removed first, since it was only for the inside test):

const float l0 = static_cast<float>(w0 - bias0) * invArea;
const float l1 = static_cast<float>(w1 - bias1) * invArea;
const float l2 = 1.0f - l0 - l1;
u = l0 * A->uv.x + l1 * B->uv.x + l2 * C->uv.x;

Colour channels are interpolated separately, in 0 to 255 floats, and rounded. This is non perspective-correct interpolation, which is exactly right for 2D, where w is always 1. (In 3D it would produce visibly warped textures.)

Sampling the texture
inline uint32_t sample(const uint32_t* texels, int w, int h, float u, float v) {
    const int x = std::clamp(static_cast<int>(u * w), 0, w - 1);
    ...
}

Nearest neighbour sampling with clamping to the edge. ImGui lays out glyph rectangles on whole pixel boundaries, and with a one to one mapping of texels to pixels, nearest sampling gives the same result as bilinear filtering would. The test in chapter 14 compares this renderer to the GPU's bilinear one and finds the images equal to within one level of 255, which confirms that. If you scale an image with ImGui::Image, nearest looks blocky: exercise 13.4.

Blending
The framebuffer is opaque 0xFFRRGGBB. The result of drawing a pixel with colour s and alpha a (0 to 255) over destination d is s*a/255 + d*(255-a)/255 per channel. Division by 255 is slow and done millions of times, so there is an exact integer trick:

inline uint32_t div255(uint32_t x) { return (x + 128u + ((x + 128u) >> 8)) >> 8; }

For x between 0 and 65025 (255 x 255) it equals round(x / 255.0). Two shortcuts come first: a == 0 leaves the pixel alone and a == 255 replaces it. Most pixels of a UI are one of those two.

The vertex colour is multiplied by the texel (modulate) before blending. For a solid triangle there is no texel and the vertex colour is used as is.

Clipping
Each ImDrawCmd has a clip rectangle in logical units. render converts it to pixels, rounds outwards (floor for the top left and ceil for the bottom right, so partly covered pixels are kept) and intersects it with the framebuffer. The bounding box of every triangle is intersected with that rectangle, so nothing outside it is ever visited or written. This is the CPU form of OpenGL's scissor test.

13.9 The fast path for rectangles
Look at what is in a frame. More than half of the gallery's triangles come in pairs that make an axis aligned rectangle with one colour (window backgrounds, buttons, progress bars, and every glyph, which is the same shape with a texture). For these the edge functions are wasted work: the inside test is just x0 <= x < x1.

drawQuad recognizes the pattern. ImGui emits a rectangle as indices (0, 1, 2) and (0, 2, 3), so the renderer checks whether the second triangle repeats the first's first and third index:

if (i + 6 <= cmd.ElemCount && ib[i + 3] == ib[i] && ib[i + 4] == ib[i + 2] &&
    drawQuad(base[ib[i]], base[ib[i + 1]], base[ib[i + 2]], base[ib[i + 5]], tex, ...)) {
    ++stats_.quads;
    i += 6;
    continue;
}
drawTriangle(...);

drawQuad then verifies that it really is a rectangle: all four colours equal, the four positions form an axis aligned box (two valid orders of the vertices), and the UVs map linearly onto it. Any check that fails returns false and the pair is drawn as two ordinary triangles. A pixel is inside when its centre is in the half open box [x0, x1) x [y0, y1), which is what the top-left rule gives for a rectangle, so the two paths draw the same pixels. testFastPathMatchesGeneralPath proves it: the same rectangle drawn through the quad path and (as a rotated and reordered polygon) through the triangle path gives identical images.

The "Renderer" window of the demo prints the quads counter so you can see the path being taken.

13.10 The render loop
SoftRenderer::render ties it together in four steps:

Textures. For every entry in data->Textures whose status is not OK, call updateTexture.
Clear. Fill the framebuffer with the clear colour (opaque). The clear colour is the application's choice, not ImGui's.
Draw lists. For each list, for each command: run a user callback if there is one; otherwise compute the clip rectangle, find the texture from cmd.GetTexID() and loop over the indices three (or six, for a rectangle) at a time.
Return. There is no "present" here: the caller hands the framebuffer to window->present(), exactly like our Cairo renderer.
FramebufferScale from ImDrawData multiplies all positions, so one code path handles 1x and 2x. At 2x a glyph rectangle arrives in logical coordinates and is rasterized at twice the size from an atlas that was baked at twice the density, so text stays crisp.

13.11 Running it and measuring
make run-p11                              # needs a window: X11 or Wayland
./build/gcc-debug/p11_imgui_soft/p11_imgui_soft --screenshot /tmp/imgui    # no window needed
./build/gcc-release/p11_imgui_soft/p11_imgui_soft --bench

Try in the window: drag and resize the windows, type in the Name field (the text cursor blinks), Ctrl+C and Ctrl+V in it, open View, ImGui demo window and click through the demo's sections, and watch the frame counter in the Renderer window. Then leave the window alone and check that the process uses no CPU.

--bench renders 300 frames of the gallery with the demo window open and reports the average. On the 2 core virtual machine used while writing this chapter (release build, GCC 13):

Window	Pixels	Time per frame
960 x 640, scale 1	614,400	about 6.6 ms
960 x 640, scale 2	2,457,600	about 25 ms
So the CPU path holds 60 frames per second (16.7 ms) at 1x and does not at 2x, with a single thread and no SIMD. The time is proportional to the number of pixels written, not to the triangle count: four times the pixels cost about four times as much. Where does it go? Each pixel of a UI is touched several times (window background, then the child, then the widget), each time through blend. Ideas to go faster, in order of effort: skip all work for fully opaque solid rectangles with a row std::fill, split the framebuffer in horizontal bands and rasterize them in parallel (the draw lists are read only), or restrict the redraw to the region that changed (damage tracking). Exercise 13.5 asks you to do the first and measure. The next chapter shows what the GPU buys.

In practice most ImGui programs are not full screen animations. The program above only renders while something changes, so the cost shows up as latency during interaction, not as constant load.

13.12 Tests
tests/imgui_soft_test.cpp (13 tests, all headless) checks the backend against pixels it can predict:

Test	What it proves
testSolidRectangle	Exact pixel coverage, right and bottom exclusive, the fast path was used
testTopLeftRuleNoDoubleBlend	Shared edges are drawn once (a half transparent diamond has one colour everywhere)
testFastPathMatchesGeneralPath	The quad shortcut gives the same image as the triangle code
testClipRectangle	Nothing is written outside PushClipRect
testTriangleArea	A 100 x 100 right triangle covers 4,950 pixels (top-left rule, 4,950 of 5,000)
testGradientInterpolates	Four colour rectangle: left dark, middle grey, right white
testFontTextureIsServiced	The atlas goes WantCreate to OK, text pixels appear
testButtonClick	A move, press and release produce exactly one click, a click outside none
testTyping	TextEvent with ASCII and two byte UTF-8, then Backspace
testClipboardRoundTrip	ImGui::SetClipboardText reaches Host::clipboard and back
testKeyMapping	Letters, digits, F keys, arrows
testHiDpi	Scale 2: 10 logical units are 20 pixels, DisplaySize stays logical
testScrollDirection	A downward ww scroll is a negative ImGui wheel
Two details that cost time while writing them are worth remembering. io.MouseWheel is cleared by ImGui::Render() (it is "this frame's wheel"), so a test has to read it between NewFrame and Render. And NewFrame may take a position change and a button press in separate frames, so the click test runs a frame between each event.

Run with make test, and make test-asan for ASan and UBSan. The ImGui library itself is compiled with the sanitizer flags too, so errors in how we call into it surface.

Pitfalls
Forgetting the order of destruction. Renderer before Host. Otherwise the renderer's destructor calls ImGui::GetIO() with no context and ImGui asserts.
Giving ImGui physical pixels. DisplaySize is logical. If you set it to the framebuffer size at 2x, every widget looks half size.
Multiplying the font by the scale. In 1.92 and later DisplayFramebufferScale already makes text sharp. Do not also scale FontSizeBase or FontScaleDpi for HiDPI: that doubles the layout.
Treating io.WantCaptureMouse as optional. When your own code also reacts to the mouse (a 3D view behind the UI), ask io.WantCaptureMouse and io.WantCaptureKeyboard and do not handle input ImGui wants. The gallery has no other consumer, so it ignores them.
Using ImGui from two threads. The context is not thread safe. Build the UI on one thread; parallel rendering of the finished draw data is fine.
Calling ImGui:: functions outside NewFrame and Render. Most widget calls assert. In debug builds the message tells you which.
A stale imgui.ini. If your windows open in strange places, an old imgui.ini from an earlier run is restoring them. This project turns the file off.
Mixing up colour byte order. ImGui is ABGR (IM_COL32(r, g, b, a) hides it), our framebuffer is ARGB. A red and blue swap in a screenshot means one conversion was missed.
Review questions
List the two halves of an ImGui backend and what each one is responsible for. Which one changes in chapter 14, and which does not?
Why does ScrollEvent{0, 1} become AddMouseWheelEvent(0, -1)?
A window is 960 x 640 logical on a 2x display. What are io.DisplaySize and io.DisplayFramebufferScale, and how big is the framebuffer?
Why is the Host destroyed after the renderer?
What does ImTextureStatus_WantUpdates tell a renderer, and why does the CPU renderer copy only tex->Updates instead of the whole image?
Why are vertex positions snapped to 1/256 of a pixel and the edge functions computed with integers?
A rectangle is drawn as two triangles with 50 percent alpha. Without the top-left rule, what would you see along the diagonal?
How does drawQuad decide that two triangles are an axis aligned rectangle, and what happens if the test fails?
Where does ImGui's anti-aliasing come from, and what must a renderer do to preserve it?
The benchmark takes four times longer at 2x. What does that tell you about where the time goes, and what would you change first?
Exercises
Ex 13.1 Add a second ImGui window to gallery::build that shows the last 20 events received (type and position), by having main keep a small ring buffer of ww::Event descriptions. Which part of the program owns that buffer, and why can it not live in Host?

Ex 13.2 Make the program render only when needed instead of for 400 ms after every input. ImGui offers no "dirty" flag, so compare the draw data of consecutive frames: compute a hash of the vertex and index buffers (TotalVtxCount, TotalIdxCount and the bytes) and skip present() when two frames in a row are identical. How many frames does this save while a text cursor blinks?

Ex 13.3 Show an image with ImGui::Image. Add ImTextureID SoftRenderer::createTexture(int w, int h, const uint32_t* rgba) and destroyTexture(ImTextureID) to the renderer (the id is the pointer to a Texture, as for the atlas), create a 64 x 64 gradient and pass ImTextureRef(id) to ImGui::Image. Why does the draw loop need no change? (ImGui 1.93 can also manage such textures for you through ImGui::RegisterUserTexture in imgui_internal.h, which is marked experimental.)

Ex 13.4 Add bilinear texture sampling to sample() and an option to switch it. Compare ImGui::Image of a 16 x 16 pattern scaled to 128 x 128 with both. Does the text change? Why or why not?

Ex 13.5 Add a row fill for fully opaque solid rectangles: in drawQuad, when tex == nullptr and the alpha is 255, write the row with std::fill. Run --bench before and after. Then rasterize in two horizontal bands on two threads (std::jthread), with each band clipping to its own rows. What is shared between the threads, and why is that safe?

Ex 13.6 Support more cursors. Extend ww::Cursor with ResizeEW, ResizeNS and NotAllowed, implement them in the X11 backend (cursor font shapes XC_sb_h_double_arrow and friends) and the Wayland backend (cursor theme names ew-resize, ns-resize, not-allowed), and map ImGuiMouseCursor_ResizeEW, _ResizeNS and _NotAllowed in Host::applyCursor. Hover the edge of an ImGui window to see it.

Ex 13.7 Implement ImGuiConfigFlags_NavEnableKeyboard: set the flag in Host, press Tab and the arrow keys in the demo, and fix any key that does not work. Which toImGuiKey entries did you have to add?

Ex 13.8 (Hard) Write a scanline rasterizer: for each triangle, for each row, compute the left and right x of the span analytically (intersect the row's centre with the edges) and fill between them. Keep the top-left rule exactly. Compare its output with the edge-function version on the gallery frame. Is it faster? Where does it lose precision?

Solutions: solutions/ch13.md

Previous: [Chapter 12](12-building-and-testing-an-app.md) | Next: [Chapter 14, Dear ImGui on OpenGL 3.3](14-imgui-opengl.md)

