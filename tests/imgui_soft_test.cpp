// Tests for the stage 11 backend: the CPU rasterizer on shapes with known pixels, and a whole
// ImGui frame driven by synthetic ww::Events (click, typing, clipboard, HiDPI).

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
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

// A little harness: one ImGui context, a CPU renderer and an offscreen framebuffer.
struct Rig {
    ww::imgui::Host host;
    ww::imgui::SoftRenderer soft;
    ww::OffscreenSurface surface;
    ww::Framebuffer fb;
    float scale;

    Rig(int w, int h, float s = 1.0f) : surface(w, h, s), fb(surface.framebuffer()), scale(s) {
        host.setFixedDeltaTime(1.0f / 60.0f);
        host.setDisplay(fb.width, fb.height, s);
        ImGui::GetStyle().AntiAliasedFill = false;    // exact triangles, no fringe
        ImGui::GetStyle().AntiAliasedLines = false;
    }
    template <class F>
    void frame(F&& build) {
        host.prepareFrame();
        ImGui::NewFrame();
        build();
        ImGui::Render();
        soft.render(ImGui::GetDrawData(), fb, ImVec4(0, 0, 0, 1));
    }
    uint32_t px(int x, int y) const {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const unsigned char*>(fb.pixels) + y * fb.stride + x * 4);
    }
};

void testSolidRectangle() {
    Rig rig(64, 64);
    rig.frame([] { ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 20), ImVec2(30, 40), IM_COL32(255, 0, 0, 255)); });
    CHECK(rig.px(10, 20) == 0xFFFF0000u);   // top-left pixel is inside
    CHECK(rig.px(29, 39) == 0xFFFF0000u);   // last inside pixel
    CHECK(rig.px(30, 39) == 0xFF000000u);   // right and bottom edges are exclusive
    CHECK(rig.px(29, 40) == 0xFF000000u);
    CHECK(rig.px(9, 20) == 0xFF000000u);
    CHECK(rig.soft.stats().quads == 1);     // the fast path took it
}

void testTopLeftRuleNoDoubleBlend() {
    // A half transparent diamond is two triangles sharing the diagonal. A pixel on the shared edge
    // must be drawn once, not twice (twice would make a visible line).
    Rig rig(64, 64);
    rig.frame([] {
        ImVec2 pts[4] = {{32, 8}, {56, 32}, {32, 56}, {8, 32}};
        ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(pts, 4, IM_COL32(255, 0, 0, 128));
    });
    std::set<uint32_t> values;
    int inside = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const uint32_t p = rig.px(x, y);
            if (p != 0xFF000000u) { values.insert(p); ++inside; }
        }
    CHECK(values.size() == 1);                       // every covered pixel got the same single blend
    CHECK(values.count(0xFF800000u) == 1);           // 255 * 128/255 = 128
    CHECK(inside > 1100 && inside < 1200);           // diamond area is 24*24*2 = 1152
    CHECK(rig.soft.stats().quads == 0);              // rotated, so the general path
}

std::vector<uint32_t> snapshot(const Rig& rig) {
    std::vector<uint32_t> v;
    for (int y = 0; y < rig.fb.height; ++y)
        for (int x = 0; x < rig.fb.width; ++x) v.push_back(rig.px(x, y));
    return v;
}

void testFastPathMatchesGeneralPath() {
    // The same rectangle through the quad fast path and through the triangle path must be identical.
    // (One ImGui context is current at a time, so the two runs are sequential.)
    std::vector<uint32_t> viaQuad, viaTriangles;
    {
        Rig a(48, 48);
        a.frame([] { ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(5.25f, 7.5f), ImVec2(40.75f, 33.4f), IM_COL32(40, 160, 220, 200)); });
        CHECK(a.soft.stats().quads == 1);
        viaQuad = snapshot(a);
    }
    {
        Rig b(48, 48);
        b.frame([] {
            ImVec2 pts[4] = {{40.75f, 7.5f}, {5.25f, 7.5f}, {5.25f, 33.4f}, {40.75f, 33.4f}};   // different winding and start
            ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(pts, 4, IM_COL32(40, 160, 220, 200));
        });
        viaTriangles = snapshot(b);
    }
    CHECK(viaQuad == viaTriangles);
}

void testClipRectangle() {
    Rig rig(64, 64);
    rig.frame([] {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->PushClipRect(ImVec2(20, 20), ImVec2(40, 40), true);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(64, 64), IM_COL32(0, 255, 0, 255));
        dl->PopClipRect();
    });
    CHECK(rig.px(19, 30) == 0xFF000000u);
    CHECK(rig.px(20, 30) == 0xFF00FF00u);
    CHECK(rig.px(39, 39) == 0xFF00FF00u);
    CHECK(rig.px(40, 30) == 0xFF000000u);
    CHECK(rig.px(30, 40) == 0xFF000000u);
}

void testTriangleArea() {
    Rig rig(128, 128);
    rig.frame([] {
        ImGui::GetBackgroundDrawList()->AddTriangleFilled(ImVec2(10, 10), ImVec2(110, 10), ImVec2(10, 110), IM_COL32(255, 255, 255, 255));
    });
    int white = 0;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) white += rig.px(x, y) == 0xFFFFFFFFu;
    CHECK(white > 4900 && white < 5100);   // 100 * 100 / 2, give or take the hypotenuse pixels   // 100 * 100 / 2
    CHECK(rig.px(12, 12) == 0xFFFFFFFFu);
    CHECK(rig.px(100, 100) == 0xFF000000u);
}

void testGradientInterpolates() {
    Rig rig(64, 32);
    rig.frame([] {
        ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(64, 32), IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255),
                                                                IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
    });
    const uint32_t left = rig.px(1, 16) & 0xFF, mid = rig.px(32, 16) & 0xFF, right = rig.px(62, 16) & 0xFF;
    CHECK(left < 20);
    CHECK(mid > 110 && mid < 145);
    CHECK(right > 235);
}

void testFontTextureIsServiced() {
    Rig rig(200, 60);
    auto build = [] {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(200, 60));
        ImGui::Begin("t", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::TextUnformatted("Hello");
        ImGui::End();
    };
    for (int i = 0; i < 3; ++i) rig.frame(build);   // the atlas needs a frame or two to be created and filled
    int textPixels = 0;   // the default font is white on the window background
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 200; ++x) textPixels += rig.px(x, y) == 0xFFFFFFFFu;
    CHECK(textPixels > 20);
    ImTextureData* atlas = ImGui::GetPlatformIO().Textures[0];
    CHECK(atlas->Status == ImTextureStatus_OK);
    CHECK(atlas->TexID != ImTextureID_Invalid);
}

void testButtonClick() {
    Rig rig(240, 120);
    int clicks = 0;
    auto build = [&] {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(240, 120));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        if (ImGui::Button("Press", ImVec2(100, 30))) ++clicks;
        ImGui::End();
    };
    rig.frame(build);
    rig.frame(build);
    const ww::Modifiers none;
    // The button is at the window padding (8, 8); aim for its middle.
    rig.host.handleEvent(ww::MouseMoveEvent{50, 25, none});
    rig.frame(build);
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, true, 50, 25, none, 1});
    rig.frame(build);
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, false, 50, 25, none, 0});
    rig.frame(build);
    CHECK(clicks == 1);
    // A click elsewhere does nothing.
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, true, 200, 100, none, 1});
    rig.frame(build);
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, false, 200, 100, none, 0});
    rig.frame(build);
    CHECK(clicks == 1);
}

void testTyping() {
    Rig rig(300, 80);
    char text[32] = "";
    auto build = [&] {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(300, 80));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##in", text, sizeof(text));
        ImGui::End();
    };
    for (int i = 0; i < 3; ++i) rig.frame(build);
    const ww::Modifiers none;
    rig.host.handleEvent(ww::MouseMoveEvent{40, 18, none});
    rig.frame(build);
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, true, 40, 18, none, 1});
    rig.frame(build);
    rig.host.handleEvent(ww::MouseButtonEvent{ww::MouseButton::Left, false, 40, 18, none, 0});
    rig.frame(build);
    rig.host.handleEvent(ww::TextEvent{"abc"});
    rig.frame(build);
    rig.host.handleEvent(ww::TextEvent{"\xC3\xA9"});   // e acute, two UTF-8 bytes
    rig.frame(build);
    CHECK(std::string(text) == "abc\xC3\xA9");
    rig.host.handleEvent(ww::KeyEvent{ww::Key::Backspace, true, false, none});
    rig.frame(build);
    rig.host.handleEvent(ww::KeyEvent{ww::Key::Backspace, false, false, none});
    rig.frame(build);
    CHECK(std::string(text) == "abc");
}

void testClipboardRoundTrip() {
    Rig rig(64, 64);
    ImGui::SetClipboardText("from imgui");
    CHECK(rig.host.clipboard == "from imgui");
    rig.host.clipboard = "from the host";
    CHECK(std::string(ImGui::GetClipboardText()) == "from the host");
}

void testKeyMapping() {
    using ww::Key;
    CHECK(ww::imgui::toImGuiKey(Key::A) == ImGuiKey_A);
    CHECK(ww::imgui::toImGuiKey(Key::Z) == ImGuiKey_Z);
    CHECK(ww::imgui::toImGuiKey(Key::Digit5) == ImGuiKey_5);
    CHECK(ww::imgui::toImGuiKey(Key::F12) == ImGuiKey_F12);
    CHECK(ww::imgui::toImGuiKey(Key::Left) == ImGuiKey_LeftArrow);
    CHECK(ww::imgui::toImGuiKey(Key::Enter) == ImGuiKey_Enter);
    CHECK(ww::imgui::toImGuiKey(Key::Unknown) == ImGuiKey_None);
}

void testHiDpi() {
    Rig rig(100, 50, 2.0f);   // 200 x 100 pixels
    rig.frame([] { ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(20, 20), IM_COL32(255, 0, 0, 255)); });
    CHECK(ImGui::GetIO().DisplaySize.x == 100.0f);
    CHECK(rig.px(20, 20) == 0xFFFF0000u);     // 10 logical units = 20 pixels
    CHECK(rig.px(39, 39) == 0xFFFF0000u);
    CHECK(rig.px(40, 40) == 0xFF000000u);
    CHECK(rig.px(19, 20) == 0xFF000000u);
}

void testScrollDirection() {
    Rig rig(64, 64);
    const ww::Modifiers none;
    rig.host.handleEvent(ww::ScrollEvent{0, 1.0f, 10, 10, none});   // one notch down in ww terms
    float seen = 0.0f;   // ImGui may deliver the wheel on the frame after the position change
    for (int i = 0; i < 3; ++i) {
        rig.frame([&] { seen += ImGui::GetIO().MouseWheel; });   // EndFrame clears it, so read it inside the frame
    }
    CHECK(seen < 0.0f);   // ImGui: negative = scroll down   // ImGui: negative = scroll down
}

} // namespace

int main() {
    testSolidRectangle();
    testTopLeftRuleNoDoubleBlend();
    testFastPathMatchesGeneralPath();
    testClipRectangle();
    testTriangleArea();
    testGradientInterpolates();
    testFontTextureIsServiced();
    testButtonClick();
    testTyping();
    testClipboardRoundTrip();
    testKeyMapping();
    testHiDpi();
    testScrollDirection();
    if (failures == 0) std::puts("imgui_soft_test: all passed");
    return failures == 0 ? 0 : 1;
}
