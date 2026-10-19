#pragma once
// The renderer half of a Dear ImGui backend, on the CPU.
//
// Dear ImGui's output is a list of textured, vertex coloured triangles ("draw lists") plus a
// clip rectangle per batch. Every GPU renderer for ImGui is a loop over those triangles; this one
// does the same loop with ordinary C++ and writes pixels into a ww::Framebuffer.

#include <cstdint>
#include <vector>

#include "imgui.h"
#include "ww/window.hpp"

namespace ww::imgui {

class SoftRenderer {
public:
    SoftRenderer();
    ~SoftRenderer();   // destroy before the ImGui context (see Host)
    SoftRenderer(const SoftRenderer&) = delete;
    SoftRenderer& operator=(const SoftRenderer&) = delete;

    // Clears the framebuffer to `clear` and draws everything in `data` on top.
    // Also services texture requests (the font atlas) found in data->Textures.
    void render(const ImDrawData* data, const Framebuffer& fb, const ImVec4& clear);

    // Statistics of the last render() call, for the demo's overlay and the tests.
    struct Stats {
        int triangles = 0;  // all triangles, including the two of every quad
        int quads = 0;      // triangle pairs drawn with the axis-aligned fast path
        int commands = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Texture {
        int width = 0, height = 0;
        std::vector<uint32_t> rgba;   // 0xAABBGGRR, same byte order as ImGui's vertex colours
    };

    void updateTexture(ImTextureData* tex);
    void drawTriangle(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c, const Texture* tex,
                      int clipX0, int clipY0, int clipX1, int clipY1, const Framebuffer& fb, float scale);
    bool drawQuad(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, const ImDrawVert& v3,
                  const Texture* tex, int clipX0, int clipY0, int clipX1, int clipY1, const Framebuffer& fb, float scale);

    Stats stats_;
};

} // namespace ww::imgui
