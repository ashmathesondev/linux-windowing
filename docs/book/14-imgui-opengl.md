Chapter 14. Dear ImGui on OpenGL 3.3 core
You will learn

When a graphics API is worth adding, and why this book picks OpenGL 3.3 core instead of 4.6
What EGL is and how to get an OpenGL context without a window, on X11, Wayland or neither
How a GL loader works, by writing one as a table of function pointers
How the same ImDrawData becomes vertex buffers, a shader program and one draw call per command
Why the projection flips the y axis, and how the finished frame gets into our CPU framebuffer
How to test GPU code in a continuous integration job that has no GPU
Code for this chapter: lib/imgui/include/ww/gl33.hpp (134 lines), egl_context.hpp and src/egl_context.cpp (113), imgui_gl.hpp and src/imgui_gl.cpp (302), p12_imgui_gl/main.cpp, and tests/imgui_gl_test.cpp (209).

Prerequisite: chapter 13. Everything about input, the frame loop and ImDrawData is unchanged. Only the renderer half is new.

14.1 Is a graphics library needed?
The previous chapter ran a full ImGui interface with no graphics library at all. That is the honest answer to "do I need OpenGL for ImGui?": no. A CPU rasterizer is enough for tools and utilities, and its output is bit-for-bit predictable, which makes it easy to test.

A GPU renderer earns its keep when the pixel count grows: a 4K window, many layers, animation at 144 Hz, or ImGui windows drawn on top of a 3D scene that already lives on the GPU. Section 13.11 measured about 25 ms per frame for a 1920 x 1280 window on one CPU thread. A GPU does the same work in parallel for all pixels at once.

So this chapter is optional in the way chapter 13 is not. It exists because the best way to understand what ImGui's renderer does is to write the two most common versions, and because OpenGL is what most ImGui programs use.

14.2 Which OpenGL?
OpenGL has existed since 1992 and has two very different styles.

Legacy (fixed function): glBegin, glVertex2f, glMatrixMode. The driver has built-in lighting and texturing. Deprecated since 3.0.
Core profile: no fixed function. You provide shaders, buffers and the matrices yourself. Everything in this chapter is that style.
The core profile has existed since OpenGL 3.2, and the version this book targets is 3.3, released in 2010, for these reasons:

Reason	Detail
ImGui needs little	Its own OpenGL 3 backend needs GLSL 1.30 and vertex array objects. Nothing it draws needs compute shaders, tessellation, direct state access or bindless textures
3.3 is the common floor	Essentially every GPU and driver of the last 15 years offers it, as do Mesa's software rasterizer, virtual machines and WSL. Platforms stop at different newer versions (macOS at 4.1, for example), 3.3 is below all of them
It has what we use	layout(location = N) in shaders, texture swizzle, glDrawElementsBaseVertex (core since 3.2), vertex array objects, framebuffer objects
Newer versions add features, not capability	4.6 is the latest version of the API. 4.5 added glCreateBuffers and friends (direct state access: the same calls without binding first), 4.3 added compute shaders. Each is nicer to write and none changes what we draw
You request a minimum. A driver may give you more: the machine this chapter was tested on has Mesa's software rasterizer, which answers "4.5 (Core Profile) Mesa 25.2" for a request of 3.3. The code checks that what it got is at least 3.3 and a core profile, and runs on that.

Khronos has not added a version since 4.6 (2017) and its newer API is Vulkan (mentioned in chapter 15). OpenGL is still supported everywhere and will be for many years.

14.3 EGL: a context without a window
OpenGL says what to draw, not how to connect to a display. That job belongs to a platform API: WGL on Windows, CGL on macOS, GLX on X11, and EGL on Wayland, embedded systems and, through extensions, on X11 and with no display server at all. EGL is the one we use, because it works the same way everywhere on Linux.

EGL concepts, in the order you meet them:

EGLDisplay
a connection to a
graphics system

EGLConfig
pixel format and
capabilities

EGLContext
OpenGL state, shaders,
buffers, textures

EGLSurface
where the pixels go
(window, pbuffer,
or none)

eglMakeCurrent
bind context to this thread

EglContext::create in lib/imgui/src/egl_context.cpp walks that chain:

1. A display. eglGetDisplay(EGL_DEFAULT_DISPLAY) would connect to whatever EGL decides: the Wayland compositor if $WAYLAND_DISPLAY is set, otherwise X11. We do not want any compositor, because our window is made by chapter 8's platform layer, and a CI job has no compositor. So we ask first for the surfaceless platform (EGL_MESA_platform_surfaceless): a display that is only a GPU, with no window system. It is found through eglGetPlatformDisplayEXT, and we fall back to the default display when the extension is missing.

2. The API. eglBindAPI(EGL_OPENGL_API) selects desktop OpenGL. EGL also offers OpenGL ES, which is what phones use; ImGui can run on that too, with #version 300 es shaders.

3. A config. eglChooseConfig with 8 bits per channel including alpha, EGL_OPENGL_BIT, and a surface type of EGL_PBUFFER_BIT (an off-screen buffer, which exists without any window system).

4. A context with the version and profile:

const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
e.context = eglCreateContext(e.display, config, EGL_NO_CONTEXT, contextAttribs);

If this fails, create returns nullptr and an error string. The failure is common (a server with no GPU, a remote desktop), so the demo prints the reason and suggests LIBGL_ALWAYS_SOFTWARE=1, which tells Mesa to use its CPU rasterizer.

5. No surface. Because we draw into our own framebuffer object (section 14.6), the context can be made current with EGL_NO_SURFACE, if the driver has EGL_KHR_surfaceless_context. Old drivers insist on a surface; we give them a 1 x 1 pbuffer.

6. eglMakeCurrent. An OpenGL context belongs to one thread at a time. Every GL call uses "the current context" of the calling thread, which is why the renderer calls egl_.makeCurrent() at the start of render and in its destructor.

The class stores everything in a private Impl struct, so the header does not include <EGL/egl.h> and nothing outside this file depends on EGL types. That is the pimpl idiom from chapter 9.

14.4 Presenting: why we render off screen and copy back
A "normal" OpenGL program creates a window surface and calls eglSwapBuffers: the GPU presents the frame itself. Our windows are different. ww::Window gives us a CPU framebuffer (shared memory on X11, a wl_shm buffer on Wayland) and present() sends it to the compositor. To keep everything from chapters 3 to 8 unchanged, the GPU renders into an off-screen framebuffer and we read the result back:

ImDrawData

GPU: render into
a framebuffer object (FBO)

glReadPixels
(BGRA)

ww::Framebuffer
(CPU memory)

window->present()

Off screen and read back (this chapter)	GPU swap chain (eglSwapBuffers)
Works with our ww::Window unchanged	yes	needs native handles from the window
Works with no display at all (tests, CI)	yes	no
Same code on X11 and Wayland	yes	different native surface per backend
Cost per frame	A copy of the window's pixels from GPU memory, and a wait for the GPU to finish	None
Latency	One frame of readback	Lowest
The readback glReadPixels is a synchronization point: the CPU waits until the GPU has finished. For a UI that renders only when something changes this is acceptable. For a game it would not be. Section 15.8 describes what changes when you want the real swap chain; the shaders and draw loop below stay exactly the same, and only the target (framebuffer 0 instead of the FBO) and the last step change.

14.5 Loading OpenGL functions
A program cannot link against the OpenGL functions of a driver at build time, because there are many drivers, and a function may exist in one and not another. The standard is to ask the driver at run time, with eglGetProcAddress("glDrawElements"), and keep the returned pointer. A loader library (glad, GLEW) does this for hundreds of functions. We need 57.

lib/imgui/include/ww/gl33.hpp is a loader in 134 lines, written with an X macro: a list of functions that expands twice, once to declare the pointers and once to fill them.

#define WW_GL_FUNCTIONS(X)                                                \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))           \
    X(void, Clear, (GLbitfield mask))                                     \
    X(GLuint, CreateShader, (GLenum type))                                \
    ...

struct Functions {
#define WW_GL_DECLARE(ret, name, args) ret(*name) args = nullptr;
    WW_GL_FUNCTIONS(WW_GL_DECLARE)
#undef WW_GL_DECLARE
};

inline const char* load(Functions& out, void* (*getProc)(const char*)) {
#define WW_GL_LOAD(ret, name, args)                                       \
    out.name = reinterpret_cast<ret(*) args>(getProc("gl" #name));        \
    if (!out.name) return "gl" #name;
    WW_GL_FUNCTIONS(WW_GL_LOAD)
#undef WW_GL_LOAD
    return nullptr;
}

Calls then read g.DrawElementsBaseVertex(...), where g is a ww::gl::Functions. load returns the name of the first missing function, so a driver that is too old fails with "OpenGL function not found: glDrawElementsBaseVertex" instead of a crash on a null pointer.

The constants (GL_TRIANGLES = 0x0004 and so on) are copied into the same header as constexpr values, so the project needs no <GL/gl.h> and does not link libGL. Only libEGL is linked (pkg_check_modules(EGL IMPORTED_TARGET egl) in the root CMakeLists.txt; when it is not found the OpenGL stage is skipped with a message rather than failing).

Two honest caveats. First, with EGL 1.4 only extension functions could be loaded through eglGetProcAddress; with EGL 1.5 (every current driver) it returns all functions including the OpenGL 1.1 ones such as glClear. Second, hand-listing 57 functions is a choice for learning. In a real project, generate the table (glad) so a typo in a constant cannot happen.

14.6 Setting up the renderer
GlRenderer::create builds all objects once. They are the same objects any OpenGL 3.3 program creates; here is what each is for.

Shaders
A shader is a small program that runs on the GPU. The vertex shader runs once per vertex and decides where it lands on screen. The fragment shader runs once per covered pixel and decides its colour.

#version 330 core
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;
uniform mat4 ProjMtx;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main() {
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}

#version 330 core
in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
layout(location = 0) out vec4 Out_Color;
void main() {
    Out_Color = Frag_Color * texture(Texture, Frag_UV);
}

Compare with chapter 13. The fragment shader's one line, Frag_Color * texture(...), is modulate from the CPU renderer. The GPU's rasterizer does everything else: edge functions, the top-left rule, interpolating Frag_UV and Frag_Color between the vertices (the barycentric weights of section 13.8, in hardware). #version 330 core is the GLSL that goes with OpenGL 3.3.

Compilation happens at run time, in the driver: glCreateShader, glShaderSource, glCompileShader, then glGetShaderiv(COMPILE_STATUS). When it fails, the driver's message (glGetShaderInfoLog) is copied into the error string. A shader program is the two shaders linked together (glLinkProgram). The shader objects can be deleted once linked.

The vertex array object
How does the GPU know that the 20 bytes of an ImDrawVert are two floats for the position, two for the UV and four bytes for the colour? You tell it with vertex attribute pointers, and in the core profile you must store them in a vertex array object (VAO), which the context remembers:

g.BindVertexArray(vao_);
g.BindBuffer(ARRAY_BUFFER, vbo_);
g.VertexAttribPointer(0, 2, FLOAT,         FALSE_, sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, pos));
g.VertexAttribPointer(1, 2, FLOAT,         FALSE_, sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, uv));
g.VertexAttribPointer(2, 4, UNSIGNED_BYTE, TRUE_,  sizeof(ImDrawVert), (void*)offsetof(ImDrawVert, col));

The TRUE_ on the third call means normalized: the four bytes 0 to 255 reach the shader as floats 0.0 to 1.0, and the byte order (red in the low byte) matches 0xAABBGGRR with no conversion. The numbers 0, 1, 2 are the layout(location = N) of the shader inputs.

Framebuffer object
glReadPixels reads from the current framebuffer. The default framebuffer belongs to a window; we have none, so we create our own: a renderbuffer (a block of GPU memory that can be drawn into) of RGBA8 format, attached to a framebuffer object (FBO). resizeTarget reallocates the renderbuffer when the window size changes.

14.7 Drawing a frame
GlRenderer::render has the same four steps as the CPU renderer, with different mechanics.

State. Depth test and face culling off (we draw 2D, back to front, in the order given). Blending on:

g.BlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA);

For colour: src * a + dst * (1 - a), the formula of section 13.8. For the alpha channel: src.a + dst.a * (1 - src.a). Because we clear to alpha 1 this stays 1, so the framebuffer we read back is opaque, as ww::Framebuffer requires.

The projection matrix. The vertex positions are in ImGui's logical units, 0 to DisplaySize with y pointing down. The GPU wants clip space, -1 to +1 in both axes. A 4 x 4 matrix converts, scaling by 2/width and 2/height and translating by -1:

const float projection[4][4] = {
    {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
    {0.0f, 2.0f / (B - T), 0.0f, 0.0f},
    {0.0f, 0.0f, -1.0f, 0.0f},
    {-(R + L) / (R - L), -(B + T) / (B - T), 0.0f, 1.0f},
};

Look at the second row: 2 / (B - T) is positive. A textbook OpenGL orthographic projection has 2 / (T - B) there, which maps the top of the screen (ImGui y = 0) to clip +1, the top in OpenGL's y-up convention. That is right for a window but wrong for us: glReadPixels returns rows starting from the bottom of the framebuffer, so the picture would arrive upside down in our top-down framebuffer. With the sign flipped, ImGui's top row lands in the first row of memory, and the readback is already the right way up. The flip costs nothing. (A first attempt at an OpenGL screenshot almost always shows an upside down image. Now you know why.)

Uploading. Per draw list, the vertices and indices are copied to the GPU:

g.BufferData(ARRAY_BUFFER, list->VtxBuffer.Size * sizeof(ImDrawVert), list->VtxBuffer.Data, STREAM_DRAW);
g.BufferData(ELEMENT_ARRAY_BUFFER, list->IdxBuffer.Size * sizeof(ImDrawIdx), list->IdxBuffer.Data, STREAM_DRAW);

STREAM_DRAW is a hint: "written once, used a few times, rewritten every frame". Reusing one buffer for every list is simple and fast enough; faster designs use one buffer for the whole frame or persistent mapping (an OpenGL 4.4 feature, one of the reasons newer is not required).

One draw call per command.

g.Scissor(x0, y0, x1 - x0, y1 - y0);
g.BindTexture(TEXTURE_2D, static_cast<GLuint>(cmd.GetTexID()));
g.DrawElementsBaseVertex(TRIANGLES, cmd.ElemCount, sizeof(ImDrawIdx) == 2 ? UNSIGNED_SHORT : UNSIGNED_INT,
                         (const void*)(cmd.IdxOffset * sizeof(ImDrawIdx)), cmd.VtxOffset);

The scissor test is the GPU's clip rectangle. Because of the flipped projection, GL's bottom-left origin coincides with ImGui's top-left, so the rectangle needs no "height minus y" arithmetic. That is another place where the flip pays off. (A renderer without the trick needs glScissor(x0, fbHeight - y1, w, h).)
cmd.GetTexID() is the OpenGL texture name we stored when we created the atlas. A command with a user callback is called instead of drawn.
glDrawElementsBaseVertex takes the index count, the type of the index (ImDrawIdx is 16 bit by default) and a byte offset into the index buffer. The last argument is cmd.VtxOffset, added to every index: the RendererHasVtxOffset support from section 13.6.
Reading back.

g.PixelStorei(PACK_ROW_LENGTH, fb.stride / 4);
g.ReadPixels(0, 0, fb.width, fb.height, BGRA, UNSIGNED_BYTE, fb.pixels);

BGRA with UNSIGNED_BYTE is the byte sequence B, G, R, A in memory. Read as a little endian 32 bit integer that is 0xAARRGGBB, our framebuffer format, so the data can go straight into the window's pixels with no conversion loop. PACK_ROW_LENGTH tells GL the destination's row length in pixels, because the framebuffer's stride can be larger than width * 4.

14.8 Textures
The atlas arrives through the same ImTextureData protocol as in chapter 13 (state diagram in section 13.7). GlRenderer::updateTexture:

WantCreate: glGenTextures, bind, set filtering to LINEAR and wrapping to CLAMP_TO_EDGE, upload the pixels with glTexImage2D, store the GL name in the texture id.
WantUpdates: bind, then for every rectangle in tex->Updates, glTexSubImage2D. The source is the full image in CPU memory, and UNPACK_ROW_LENGTH = tex->Width tells GL that a row of the source is the whole image's width, so a small rectangle can be read straight out of the middle without copying it first.
WantDestroy: glDeleteTextures.
Alpha8 atlases use one byte per texel, GL_R8, uploaded as GL_RED. A shader that reads the texture gets (r, 0, 0, 1), not the (1, 1, 1, a) we need. Core 3.3 has a swizzle to fix it without touching the shader:

const GLint swizzle[4] = {ONE, ONE, ONE, RED};
g.TexParameteriv(TEXTURE_2D, TEXTURE_SWIZZLE_RGBA, swizzle);

"Red, green and blue are the constant one. Alpha is whatever is in the red channel." The test compareOnWidgets runs both atlas formats.

Filtering. The GPU samples with bilinear interpolation (LINEAR), unlike the nearest sampling of chapter 13. Because glyph quads are aligned to whole pixels, the sample points fall at texel centres and both give the same result. Scaled images differ: exercise 13.4.

14.9 Do the two renderers agree?
You now have two implementations of the same specification, which makes a good test. tests/imgui_gl_test.cpp renders one deterministic UI (a window with text, button, checkbox, slider, input field, progress bar, and a filled circle), once with each renderer, and compares every pixel:

imgui_gl_test: surfaceless platform, EGL 1.5
  scale 1, font format 0: 0 of 153600 pixels differ by more than 8 (max 1)
  scale 1, font format 1: 0 of 153600 pixels differ by more than 8 (max 1)
  scale 2, font format 0: 0 of 614400 pixels differ by more than 8 (max 1)

The largest difference in any colour channel of any pixel is 1 out of 255: rounding of interpolated colours. A hand-written software rasterizer and a GPU driver (here Mesa's llvmpipe) agree, on the rectangles, the anti-aliased circle, the gradient, and the text, at scale 1 and 2, with both atlas formats. The test allows 8 levels of difference in at most 0.5 percent of the pixels, a margin for other drivers. A real GPU may differ more in the least significant bits because its interpolation hardware rounds differently; that is expected and harmless.

This is also the reason the CPU renderer was written first. It is the reference against which the GPU code is checked.

The test cannot feed both renderers from one ImGui context (a texture id belongs to one renderer), so it builds the UI twice with a fixed time step and identical input. Determinism is why the Host has setFixedDeltaTime.

Other tests: the exact pixels of a rectangle (the image is not upside down), clipping with PushClipRect, blending with alpha 128 giving 127 to 129 red, opaque output alpha, and a partial texture update (changing the font size bakes new glyphs into an existing atlas).

14.10 Testing GPU code without a GPU
A continuous integration machine usually has no GPU. Mesa can emulate one in software: the llvmpipe driver compiles GLSL to CPU code with LLVM and runs it on all cores. It reports OpenGL 4.5 and is complete enough for this chapter. The test and the demo select it with LIBGL_ALWAYS_SOFTWARE=1, set in tests/CMakeLists.txt:

set_tests_properties(imgui_gl_test PROPERTIES SKIP_RETURN_CODE 77
                     ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LIBGL_ALWAYS_SOFTWARE=1")

SKIP_RETURN_CODE 77 is a CTest convention (borrowed from Automake): when the test returns 77 it shows as skipped, not failed. imgui_gl_test returns 77 when EglContext::create fails, so a machine with no EGL at all (a minimal container) still gets a green build, with a visible "skipped" line. Install libgl1-mesa-dri for llvmpipe on Ubuntu.

On a machine with a real GPU, run the same program without the variable to test the real driver: LIBGL_ALWAYS_SOFTWARE= ./build/gcc-debug/tests/imgui_gl_test. The first line the test prints tells you which platform and renderer it found. This chapter's results come from llvmpipe only: the code was not run on a physical GPU while it was written, so the first run on yours is real information (see the pitfalls).

14.11 Running it
make run-p12
./build/gcc-debug/p12_imgui_gl/p12_imgui_gl --screenshot /tmp/imgui_gl
LIBGL_ALWAYS_SOFTWARE=1 ./build/gcc-release/p12_imgui_gl/p12_imgui_gl --bench

At startup the demo prints the three lines that identify the context:

EGL: surfaceless platform, EGL 1.5
OpenGL: 4.5 (Core Profile) Mesa 25.2.8
Renderer: llvmpipe (LLVM 20.1.2, 256 bits)

--bench on llvmpipe, which is the CPU in disguise (2 cores):

Window	Pixels	Time per frame, including readback
960 x 640, scale 1	614,400	about 4.8 ms
1920 x 1280, scale 2	2,457,600	about 17 ms
Compare with section 13.11: the emulated GPU is about 1.4 times faster than our hand written rasterizer on the same cores, most likely because llvmpipe's generated code uses SIMD instructions and our pixel loop is scalar. These numbers say nothing about a real GPU, where the frame time is dominated by the readback and the driver, not by drawing; measure on your hardware.

14.12 Debugging OpenGL
OpenGL reports mistakes in a way that is easy to miss: functions return nothing and the error is stored until you ask.

glGetError() returns the first error since the last call (0 is no error). Call it after a suspicious block and print the code (0x0500 invalid enum, 0x0501 invalid value, 0x0502 invalid operation). In this project the loader table has GetError, so a debugging session is a one-line addition.
A black image usually means: the program did not link (check LINK_STATUS), the viewport is wrong, the scissor rectangle is empty, or nothing is bound (no VAO in core profile gives INVALID_OPERATION and no output).
An upside down image: section 14.7.
A texture that is all white or all black is usually an incomplete texture: a minification filter that expects mipmaps (the default) with only level 0. Setting TEXTURE_MIN_FILTER to LINEAR is why the code does it.
Tools. apitrace records every GL call of a program for replay and inspection. Mesa has MESA_DEBUG=1 and LIBGL_DEBUG=verbose; with the debug context flag (EGL_CONTEXT_OPENGL_DEBUG) and glDebugMessageCallback (4.3, or the KHR_debug extension in 3.3 on most drivers) the driver prints readable messages.
Pitfalls
Calling GL without a current context. The call crashes or silently does nothing. Every public method of GlRenderer starts with egl_.makeCurrent().
Using the context from another thread. A context is current on one thread at a time. Either keep all GL work on one thread or release it with eglMakeCurrent(..., EGL_NO_CONTEXT) before handing it over.
Forgetting the VAO. Core profile refuses to draw with the default (zero) vertex array. The symptom is an empty image and INVALID_OPERATION.
Index type mismatch. ImDrawIdx is 16 bit unless you define it as unsigned int in imconfig.h. Then glDrawElements must say UNSIGNED_INT. The code derives the type from sizeof(ImDrawIdx).
Alignment on readback and upload. The default PACK_ALIGNMENT and UNPACK_ALIGNMENT are 4. A 1 byte per pixel texture whose width is not a multiple of 4 reads garbage unless you set UNPACK_ALIGNMENT to 1, which the code does for the atlas.
Row pitch. ww::Framebuffer::stride is in bytes and may exceed width * 4. PACK_ROW_LENGTH is in pixels, hence stride / 4.
Assuming the driver gives exactly 3.3. You get at least what you asked for. Do not enable anything beyond 3.3 without checking GL_VERSION. The renderer rejects older drivers and non core contexts with a message.
Untested on real hardware. Mesa's software driver is more forgiving than some hardware drivers about out-of-range values and unusual formats. If you see a problem on a real GPU that llvmpipe does not show, glGetError and the apitrace tool are the first steps.
EGL on NVIDIA. The surfaceless platform is a Mesa extension. With NVIDIA's proprietary driver the code falls back to eglGetDisplay(EGL_DEFAULT_DISPLAY), which may connect to X11 or Wayland through the driver. If the fallback fails too, exercise 14.4 shows how to select a device with EGL_EXT_platform_device.
Review questions
Why is OpenGL 3.3 core chosen over 4.6 for this renderer? Name two things 4.6 offers that ImGui does not need.
What is the difference between an EGLDisplay, an EGLContext and an EGLSurface? Which of them can be absent in this chapter, and why?
What does the projection matrix do, and why is its second row's sign opposite to the textbook one?
The shader has one multiplication. What other work, done by hand in chapter 13, does the GPU do for it?
Why does the code call glPixelStorei(PACK_ROW_LENGTH, fb.stride / 4) before glReadPixels?
What does glDrawElementsBaseVertex do with its last argument, and why does ImGui need it?
An Alpha8 atlas is uploaded as GL_R8. How does the fragment shader still see white texels with that alpha?
Why can the same ImGui context not feed both renderers in the comparison test, and what does the test do instead?
imgui_gl_test returns 77 in one situation. Which, and what does CTest do with it?
Name two ways the readback design costs performance compared with eglSwapBuffers.
Exercises
Ex 14.1 Print glGetError() after the draw loop in debug builds (#ifndef NDEBUG) and turn it into std::fprintf(stderr, ...). Provoke an error by drawing with the VAO unbound (comment out BindVertexArray(vao_)) and see the code.

Ex 14.2 Add a "GPU" line to the gallery's Renderer window that shows glVersion() and glRenderer(). The gallery is shared with stage 11; how do you pass the strings in without making gallery.hpp depend on OpenGL?

Ex 14.3 Replace the per-list BufferData with one buffer for the whole frame: gather all vertices and indices of all lists into two vectors (draw each list's commands with the base vertex argument set to where that list starts in the shared buffer, plus the command's own VtxOffset), upload once, and draw. Compare the time with --bench. When is it faster?

Ex 14.4 Make EglContext::create try EGL_EXT_platform_device as a third choice: eglQueryDevicesEXT, take the first device, eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, nullptr). This is the way to get a headless context with a proprietary NVIDIA driver. Test with __EGL_VENDOR_LIBRARY_FILENAMES if you have several vendors installed.

Ex 14.5 Add ImTextureID GlRenderer::createTexture(int w, int h, const uint32_t* rgba) and display the same gradient as exercise 13.3 with ImGui::Image. Compare it with the CPU version at 3x magnification: where do the bilinear and nearest versions differ?

Ex 14.6 Run the benchmark with a window at 3840 x 2160 (use kWidth and kHeight). Where does the time go: shading, upload, or readback? Measure the readback alone with std::chrono around ReadPixels plus a Finish() before it.

Ex 14.7 Replace readback with a real swap chain, as a design exercise: list what each of ww::Window, EglContext and GlRenderer would need to change for X11 (EGL_PLATFORM_X11_KHR and an XID) and for Wayland (wl_egl_window), and which tests could still run headless. Section 15.8 has the outline.

Ex 14.8 (Hard) Implement the same renderer for OpenGL ES 3.0 (#version 300 es, precision mediump float;, EGL_OPENGL_ES_API, EGL_OPENGL_ES3_BIT). Which three things in gl33.hpp and the shaders must change? (Hint: BGRA readback is not guaranteed in ES, and DrawElementsBaseVertex is an extension before ES 3.2.)

Solutions: [solutions/ch14.md](./solutions/ch14.md)

Previous: [Chapter 13](13-imgui-software.md) | [Next: Chapter 15, where to go next](15-where-next.md)

