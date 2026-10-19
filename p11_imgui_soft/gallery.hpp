#pragma once
// The Dear ImGui user interface shared by stage 11 (CPU rasterizer) and stage 12 (OpenGL).
// It only talks to ImGui, so it does not know or care which renderer draws it.

#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace gallery {

struct State {
    // widgets
    int clicks = 0;
    bool enabled = true;
    float volume = 0.35f;
    int mode = 1;
    float tint[3] = {0.25f, 0.55f, 0.95f};
    char name[64] = "type here";
    bool showDemo = false;
    bool showStats = true;

    // plot data
    float samples[96] = {};
    int sampleCount = 0;
    float phase = 0.0f;

    // set by the caller each frame, shown in the stats window
    const char* rendererName = "";
    int triangles = 0;
    int commands = 0;
    int quads = -1;
};

inline void build(State& s) {
    // ---- a main menu bar, the way ImGui does it: no state object, just calls ----------------------
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("ImGui demo window", nullptr, &s.showDemo);
            ImGui::MenuItem("Renderer statistics", nullptr, &s.showStats);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ---- an ordinary window of widgets. The code IS the layout. --------------------------------------
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Widgets")) {
        ImGui::TextUnformatted("Immediate mode: this window is rebuilt every frame.");
        ImGui::Separator();

        if (ImGui::Button("Click me")) ++s.clicks;
        ImGui::SameLine();
        ImGui::Text("clicked %d times", s.clicks);

        ImGui::Checkbox("Enabled", &s.enabled);
        ImGui::BeginDisabled(!s.enabled);
        ImGui::SliderFloat("Volume", &s.volume, 0.0f, 1.0f);
        ImGui::ProgressBar(s.volume, ImVec2(-FLT_MIN, 0));
        ImGui::RadioButton("Low", &s.mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Medium", &s.mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("High", &s.mode, 2);
        ImGui::EndDisabled();

        ImGui::ColorEdit3("Tint", s.tint);
        ImGui::InputText("Name", s.name, sizeof(s.name));

        const char* items[] = {"Alpha", "Bravo", "Charlie", "Delta"};
        static int item = 0;
        ImGui::Combo("Combo", &item, items, 4);

        if (ImGui::TreeNode("A tree")) {
            if (ImGui::TreeNode("src")) {
                ImGui::BulletText("main.cpp");
                ImGui::BulletText("window.cpp");
                ImGui::TreePop();
            }
            ImGui::BulletText("CMakeLists.txt");
            ImGui::TreePop();
        }
        if (ImGui::CollapsingHeader("Plot")) {
            ImGui::PlotLines("sine", s.samples, 96, 0, nullptr, -1.0f, 1.0f, ImVec2(0, 60));
        }
    }
    ImGui::End();

    // ---- drawing with the ImDrawList directly: this is what widgets use underneath -------------------
    ImGui::SetNextWindowPos(ImVec2(400, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Canvas")) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, 180.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(s.tint[0], s.tint[1], s.tint[2], 1.0f));
        dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(20, 20, 30, 255), tint,
                                    IM_COL32(255, 255, 255, 255), IM_COL32(10, 10, 10, 255));
        dl->AddCircleFilled(ImVec2(origin.x + size.x * 0.3f, origin.y + size.y * 0.5f), 34.0f, IM_COL32(255, 200, 60, 230), 48);
        dl->AddCircle(ImVec2(origin.x + size.x * 0.7f, origin.y + size.y * 0.5f), 40.0f, IM_COL32(255, 255, 255, 255), 48, 3.0f);
        dl->AddTriangleFilled(ImVec2(origin.x + 20, origin.y + size.y - 10), ImVec2(origin.x + 60, origin.y + size.y - 70),
                              ImVec2(origin.x + 100, origin.y + size.y - 10), IM_COL32(220, 60, 90, 255));
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6), IM_COL32(255, 255, 255, 255), "AddText on a gradient");
        ImGui::Dummy(size);   // reserves the space so the layout moves on
    }
    ImGui::End();

    if (s.showStats) {
        ImGui::SetNextWindowPos(ImVec2(400, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Renderer", &s.showStats, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("renderer: %s", s.rendererName);
            ImGui::Text("draw commands: %d", s.commands);
            ImGui::Text("triangles: %d", s.triangles);
            if (s.quads >= 0) ImGui::Text("fast-path quads: %d", s.quads);
            ImGui::Text("%.1f frames/s (ImGui estimate)", static_cast<double>(ImGui::GetIO().Framerate));
        }
        ImGui::End();
    }

    if (s.showDemo) ImGui::ShowDemoWindow(&s.showDemo);
}

// Animates the plot. Called once per frame by the demos.
inline void tick(State& s, float dt) {
    s.phase += dt * 3.0f;
    for (int i = 0; i < 96; ++i) s.samples[i] = std::sin(s.phase + static_cast<float>(i) * 0.15f);
}

} // namespace gallery
