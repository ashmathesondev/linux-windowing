// Tests for the stage 12 OpenGL 3.3 renderer. They need an EGL context; Mesa's software
// rasterizer (llvmpipe) is enough, so no GPU is required. Without any EGL the test is skipped
// (exit code 77, which CTest reports as "skipped").

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "imgui.h"
#include "ww/egl_context.hpp"
#include "ww/imgui_gl.hpp"
#include "ww/imgui_host.hpp"
#include "ww/imgui_soft.hpp"
#include "ww/renderer.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

ww::gl::EglContext* egl = nullptr;

uint32_t px(const ww::Framebuffer& fb, int x, int y) {
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const unsigned char*>(fb.pixels) + y * fb.stride + x * 4);
}

// One ImGui context with the platform half and ONE renderer. A texture's id belongs to the renderer
// that created it, so a context cannot feed two renderers; to compare them the tests run the same
// deterministic frames twice (fixed time step, same input) with a different renderer each time.
struct Rig {
    ww::imgui::Host host;
    std::unique_ptr<ww::imgui::GlRenderer> gl;
    std::unique_ptr<ww::imgui::SoftRenderer> soft;
    ww::OffscreenSurface surface;
    ww::Framebuffer fb;

    Rig(bool useGl, int w, int h, float scale = 1.0f, ImTextureFormat fontFormat = ImTextureFormat_RGBA32)
        : surface(w, h, scale), fb(surface.framebuffer()) {
        if (useGl) {
            std::string error;
            gl = ww::imgui::GlRenderer::create(*egl, &error);
            if (!gl) std::fprintf(stderr, "GlRenderer: %s\n", error.c_str());
        } else {
            soft = std::make_unique<ww::imgui::SoftRenderer>();
        }
        host.setFixedDeltaTime(1.0f / 60.0f);
        host.setDisplay(fb.width, fb.height, scale);
        ImGui::GetIO().Fonts->TexDesiredFormat = fontFormat;
    }
    template <class F>
    void frame(F&& build) {
        host.prepareFrame();
        ImGui::NewFrame();
        build();
        ImGui::Render();
        if (gl) gl->render(ImGui::GetDrawData(), fb, ImVec4(0, 0, 0, 1));
        else soft->render(ImGui::GetDrawData(), fb, ImVec4(0, 0, 0, 1));
    }
    std::vector<uint32_t> snapshot() const {
        std::vector<uint32_t> v;
        for (int y = 0; y < fb.height; ++y)
            for (int x = 0; x < fb.width; ++x) v.push_back(px(fb, x, y));
        return v;
    }
};

void testOrientationAndExactRectangle() {
    Rig rig(true, 64, 64);
    CHECK(rig.gl != nullptr);
    if (!rig.gl) return;
    rig.frame([] { ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 20), ImVec2(30, 40), IM_COL32(255, 0, 0, 255)); });
    const ww::Framebuffer& fb = rig.fb;
    CHECK(px(fb, 10, 20) == 0xFFFF0000u);   // not upside down: (10, 20) is near the top
    CHECK(px(fb, 29, 39) == 0xFFFF0000u);
    CHECK(px(fb, 30, 39) == 0xFF000000u);
    CHECK(px(fb, 29, 40) == 0xFF000000u);
    CHECK(px(fb, 10, 63 - 20) == 0xFF000000u);   // the mirrored position is empty
    CHECK(rig.gl->stats().drawCalls == 1);
}

void testClipRectangle() {
    Rig rig(true, 64, 64);
    if (!rig.gl) return;
    rig.frame([] {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->PushClipRect(ImVec2(20, 20), ImVec2(40, 40), true);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(64, 64), IM_COL32(0, 255, 0, 255));
        dl->PopClipRect();
    });
    const ww::Framebuffer& fb = rig.fb;
    CHECK(px(fb, 19, 30) == 0xFF000000u);
    CHECK(px(fb, 20, 30) == 0xFF00FF00u);
    CHECK(px(fb, 39, 39) == 0xFF00FF00u);
    CHECK(px(fb, 40, 30) == 0xFF000000u);
    CHECK(px(fb, 30, 40) == 0xFF000000u);
}

void testAlphaBlendAndOpaqueOutput() {
    Rig rig(true, 32, 32);
    if (!rig.gl) return;
    rig.frame([] { ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(32, 32), IM_COL32(255, 0, 0, 128)); });
    const uint32_t p = px(rig.fb, 16, 16);
    CHECK((p >> 24) == 0xFF);                         // alpha stays 255
    const uint32_t red = (p >> 16) & 0xFF;
    CHECK(red >= 127 && red <= 129);                  // 128/255 of red over black
}

// Compares both renderers on the whole gallery-style UI: they are different implementations of the
// same specification, so they must agree to within rounding.
void compareOnWidgets(float scale, ImTextureFormat format) {
    const int w = static_cast<int>(480 * scale), h = static_cast<int>(320 * scale);
    auto run = [&](bool useGl) {
        Rig rig(useGl, 480, 320, scale, format);
        bool check = false;
        float slider = 0.3f;
        char text[32] = "hello";
        auto build = [&] {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_Always);
            ImGui::Begin("Compare");
            ImGui::Text("Hello, OpenGL and CPU");
            ImGui::Button("Button");
            ImGui::Checkbox("Check", &check);
            ImGui::SliderFloat("Slider", &slider, 0, 1);
            ImGui::InputText("Input", text, sizeof(text));
            ImGui::ProgressBar(0.6f);
            ImGui::End();
            ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(400, 100), 40, IM_COL32(240, 160, 30, 255), 48);
        };
        for (int i = 0; i < 4; ++i) rig.frame(build);
        return rig.snapshot();
    };
    const std::vector<uint32_t> gpu = run(true), cpu = run(false);
    CHECK(gpu.size() == cpu.size() && gpu.size() == static_cast<size_t>(w) * static_cast<size_t>(h));
    if (gpu.size() != cpu.size()) return;

    long different = 0, maxDiff = 0, painted = 0;
    for (size_t i = 0; i < gpu.size(); ++i) {
        if (gpu[i] != 0xFF000000u) ++painted;
        long worst = 0;
        for (int shift : {0, 8, 16})
            worst = std::max(worst, std::labs(static_cast<long>((gpu[i] >> shift) & 0xFF) - static_cast<long>((cpu[i] >> shift) & 0xFF)));
        maxDiff = std::max(maxDiff, worst);
        different += worst > 8;
    }
    const long total = static_cast<long>(gpu.size());
    CHECK(painted > total / 8);                // something real was drawn
    CHECK(different * 200 < total);            // fewer than 0.5% of pixels differ by more than 8/255
    std::printf("  scale %.0f, font format %d: %ld of %ld pixels differ by more than 8 (max %ld)\n", static_cast<double>(scale),
                static_cast<int>(format), different, total, maxDiff);
}

void testTextureUpdateKeepsWorking() {
    // Changing the font size makes ImGui bake new glyphs into the atlas: a partial texture update.
    Rig rig(true, 240, 100);
    if (!rig.gl) return;
    float size = 13.0f;
    auto build = [&] {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(240, 100));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::PushFont(nullptr, size);
        ImGui::TextUnformatted("Grow");
        ImGui::PopFont();
        ImGui::End();
    };
    for (int i = 0; i < 3; ++i) rig.frame(build);
    size = 30.0f;
    rig.frame(build);
    rig.frame(build);
    ImTextureData* atlas = ImGui::GetPlatformIO().Textures[0];
    CHECK(atlas->Status == ImTextureStatus_OK);
    long white = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 240; ++x) white += px(rig.fb, x, y) == 0xFFFFFFFFu || ((px(rig.fb, x, y) & 0xFF) > 200);
    CHECK(white > 100);   // big text appeared
}

} // namespace

int main() {
    std::string error;
    auto context = ww::gl::EglContext::create(&error);
    if (!context) {
        std::printf("imgui_gl_test: SKIPPED, no OpenGL 3.3 core context (%s)\n", error.c_str());
        return 77;
    }
    egl = context.get();
    std::printf("imgui_gl_test: %s\n", context->description().c_str());

    testOrientationAndExactRectangle();
    testClipRectangle();
    testAlphaBlendAndOpaqueOutput();
    compareOnWidgets(1.0f, ImTextureFormat_RGBA32);
    compareOnWidgets(1.0f, ImTextureFormat_Alpha8);
    compareOnWidgets(2.0f, ImTextureFormat_RGBA32);
    testTextureUpdateKeepsWorking();
    if (failures == 0) std::puts("imgui_gl_test: all passed");
    return failures == 0 ? 0 : 1;
}
