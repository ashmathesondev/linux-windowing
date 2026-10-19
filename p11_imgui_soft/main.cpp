// Stage 11: Dear ImGui drawn by our own CPU rasterizer.
//
// No GPU, no graphics library. ImGui produces triangles; ww::imgui::SoftRenderer fills the pixels
// of the window's framebuffer; ww::imgui::Host feeds input in.
//
// Run:
//   ./p11_imgui_soft                          interactive
//   ./p11_imgui_soft --screenshot out/shot    write out/shot_*.png without a window
//   ./p11_imgui_soft --bench                  time 300 frames of the gallery, no window

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>

#include "gallery.hpp"
#include "imgui.h"
#include "ww/imgui_host.hpp"
#include "ww/imgui_soft.hpp"
#include "ww/renderer.hpp"
#include "ww/window.hpp"

namespace {

constexpr int kWidth = 960;
constexpr int kHeight = 640;
const ImVec4 kClear(0.10f, 0.11f, 0.13f, 1.0f);

// One complete ImGui frame: begin, build the UI, end, rasterize.
void frame(ww::imgui::Host& host, ww::imgui::SoftRenderer& soft, gallery::State& state, const ww::Framebuffer& fb) {
    host.prepareFrame();
    ImGui::NewFrame();
    gallery::tick(state, ImGui::GetIO().DeltaTime);
    gallery::build(state);
    ImGui::Render();
    soft.render(ImGui::GetDrawData(), fb, kClear);
    state.triangles = soft.stats().triangles;
    state.commands = soft.stats().commands;
    state.quads = soft.stats().quads;
}

int writeScreenshots(const std::string& prefix) {
    for (const float scale : {1.0f, 2.0f}) {
        ww::imgui::Host host;
        ww::imgui::SoftRenderer soft;
        host.setFixedDeltaTime(1.0f / 60.0f);
        gallery::State state;
        state.rendererName = "CPU rasterizer";
        state.showDemo = scale == 1.0f;
        ww::OffscreenSurface surface(kWidth, kHeight, scale);
        const ww::Framebuffer fb = surface.framebuffer();
        host.setDisplay(fb.width, fb.height, scale);
        // ImGui needs a few frames to lay windows out and to rasterize the font.
        for (int i = 0; i < 4; ++i) frame(host, soft, state, fb);
        ww::Renderer r(fb);
        const std::string path = prefix + (scale == 1.0f ? "_1x.png" : "_2x.png");
        if (r.savePng(path)) std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}


int runBenchmark() {
    for (const float scale : {1.0f, 2.0f}) {
        ww::imgui::Host host;
        ww::imgui::SoftRenderer soft;
        host.setFixedDeltaTime(1.0f / 60.0f);
        gallery::State state;
        state.rendererName = "CPU rasterizer";
        state.showDemo = true;
        ww::OffscreenSurface surface(kWidth, kHeight, scale);
        const ww::Framebuffer fb = surface.framebuffer();
        host.setDisplay(fb.width, fb.height, scale);
        for (int i = 0; i < 5; ++i) frame(host, soft, state, fb);   // warm up: font atlas, first layout
        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 300;
        for (int i = 0; i < kFrames; ++i) frame(host, soft, state, fb);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
        std::printf("%dx%d pixels (scale %.0f): %.2f ms per frame, %d triangles\n", fb.width, fb.height, static_cast<double>(scale), ms,
                    state.triangles);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && std::strcmp(argv[1], "--screenshot") == 0) return writeScreenshots(argv[2]);
    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0) return runBenchmark();

    ww::WindowConfig config;
    config.title = "Stage 11: Dear ImGui on the CPU";
    config.appId = "linux-windowing.p11";
    config.width = kWidth;
    config.height = kHeight;
    auto window = ww::Window::create(config);
    if (!window) return 1;

    ww::imgui::Host host(window.get());   // creates the ImGui context
    ww::imgui::SoftRenderer soft;         // declared after the Host, so it is destroyed first
    gallery::State state;
    state.rendererName = "CPU rasterizer";

    using Clock = std::chrono::steady_clock;
    auto busyUntil = Clock::now();   // ImGui animates for a moment after input (hover fades, scrolling)
    bool running = true;

    while (running) {
        // Sleep until input arrives. While something may still be animating, wake up every frame;
        // otherwise every half second, which is enough for the text cursor to blink.
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
        frame(host, soft, state, fb);
        host.applyCursor();
        window->present();
    }
    return 0;
}
