// Stage 12: the same Dear ImGui interface as stage 11, drawn with OpenGL 3.3 core.
//
// Only the renderer changed: ww::imgui::GlRenderer replaces ww::imgui::SoftRenderer. The window,
// the input and the user interface (gallery.hpp, shared with stage 11) are untouched.
//
// The GL context is created with EGL and renders off screen; the finished image is read back
// into the window's framebuffer. See chapter 14 for why, and what a swap chain would change.
//
// Run:
//   ./p12_imgui_gl                          interactive
//   ./p12_imgui_gl --screenshot out/shot    write out/shot_*.png without a window
//   ./p12_imgui_gl --bench                  time 300 frames of the gallery, no window
//
// No GPU? Mesa can draw in software: LIBGL_ALWAYS_SOFTWARE=1 ./p12_imgui_gl

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "gallery.hpp"
#include "imgui.h"
#include "ww/egl_context.hpp"
#include "ww/imgui_gl.hpp"
#include "ww/imgui_host.hpp"
#include "ww/renderer.hpp"
#include "ww/window.hpp"

namespace {

constexpr int kWidth = 960;
constexpr int kHeight = 640;
const ImVec4 kClear(0.10f, 0.11f, 0.13f, 1.0f);

void frame(ww::imgui::Host& host, ww::imgui::GlRenderer& gl, gallery::State& state, const ww::Framebuffer& fb) {
    host.prepareFrame();
    ImGui::NewFrame();
    gallery::tick(state, ImGui::GetIO().DeltaTime);
    gallery::build(state);
    ImGui::Render();
    gl.render(ImGui::GetDrawData(), fb, kClear);
    state.triangles = gl.stats().triangles;
    state.commands = gl.stats().drawCalls;
    state.quads = -1;   // the fast path is a CPU idea; the GPU just draws triangles
}

std::unique_ptr<ww::gl::EglContext> makeContext() {
    std::string error;
    auto egl = ww::gl::EglContext::create(&error);
    if (!egl) {
        std::fprintf(stderr, "Cannot create an OpenGL 3.3 core context: %s\n"
                             "Try software rendering with: LIBGL_ALWAYS_SOFTWARE=1 %s\n", error.c_str(), "./p12_imgui_gl");
    }
    return egl;
}

int writeScreenshots(const std::string& prefix) {
    auto egl = makeContext();
    if (!egl) return 1;
    for (const float scale : {1.0f, 2.0f}) {
        ww::imgui::Host host;
        std::string error;
        auto gl = ww::imgui::GlRenderer::create(*egl, &error);
        if (!gl) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        host.setFixedDeltaTime(1.0f / 60.0f);
        gallery::State state;
        state.rendererName = "OpenGL 3.3 core";
        state.showDemo = scale == 1.0f;
        ww::OffscreenSurface surface(kWidth, kHeight, scale);
        const ww::Framebuffer fb = surface.framebuffer();
        host.setDisplay(fb.width, fb.height, scale);
        for (int i = 0; i < 4; ++i) frame(host, *gl, state, fb);
        ww::Renderer r(fb);
        const std::string path = prefix + (scale == 1.0f ? "_1x.png" : "_2x.png");
        if (r.savePng(path)) std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}


int runBenchmark() {
    auto egl = makeContext();
    if (!egl) return 1;
    for (const float scale : {1.0f, 2.0f}) {
        ww::imgui::Host host;
        std::string error;
        auto gl = ww::imgui::GlRenderer::create(*egl, &error);
        if (!gl) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        host.setFixedDeltaTime(1.0f / 60.0f);
        gallery::State state;
        state.rendererName = "OpenGL 3.3 core";
        state.showDemo = true;
        ww::OffscreenSurface surface(kWidth, kHeight, scale);
        const ww::Framebuffer fb = surface.framebuffer();
        host.setDisplay(fb.width, fb.height, scale);
        for (int i = 0; i < 5; ++i) frame(host, *gl, state, fb);
        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 300;
        for (int i = 0; i < kFrames; ++i) frame(host, *gl, state, fb);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
        std::printf("%dx%d pixels (scale %.0f): %.2f ms per frame including readback, on %s\n", fb.width, fb.height,
                    static_cast<double>(scale), ms, gl->glRenderer().c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && std::strcmp(argv[1], "--screenshot") == 0) return writeScreenshots(argv[2]);
    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0) return runBenchmark();

    auto egl = makeContext();
    if (!egl) return 1;

    ww::WindowConfig config;
    config.title = "Stage 12: Dear ImGui on OpenGL 3.3";
    config.appId = "linux-windowing.p12";
    config.width = kWidth;
    config.height = kHeight;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    ww::imgui::Host host(window.get());
    std::string error;
    auto gl = ww::imgui::GlRenderer::create(*egl, &error);   // declared after the Host, so it is destroyed first
    if (!gl) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("EGL: %s\nOpenGL: %s\nRenderer: %s\n", egl->description().c_str(), gl->glVersion().c_str(), gl->glRenderer().c_str());

    gallery::State state;
    state.rendererName = "OpenGL 3.3 core";

    using Clock = std::chrono::steady_clock;
    auto busyUntil = Clock::now();
    bool running = true;
    while (running) {
        const bool busy = Clock::now() < busyUntil;
        ww::Event ev;
        bool have = window->waitEvent(ev, busy ? 16 : 500);
        while (have) {
            if (std::holds_alternative<ww::CloseEvent>(ev)) running = false;
            if (host.handleEvent(ev)) busyUntil = Clock::now() + std::chrono::milliseconds(400);
            have = window->pollEvent(ev);
        }
        if (!running) break;

        const ww::Framebuffer fb = window->acquireFramebuffer();
        host.setDisplay(fb.width, fb.height, fb.scale);
        frame(host, *gl, state, fb);
        host.applyCursor();
        window->present();
    }
    return 0;
}
