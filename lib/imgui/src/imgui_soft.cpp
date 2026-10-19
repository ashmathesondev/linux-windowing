#include "ww/imgui_soft.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ww::imgui {

namespace {

// ---- colour helpers ---------------------------------------------------------------------------------
// ImGui vertex colours are 0xAABBGGRR (red in the low byte). The framebuffer is 0xAARRGGBB.

inline uint32_t channel(uint32_t c, int shift) { return (c >> shift) & 0xFFu; }

inline uint32_t div255(uint32_t x) { return (x + 128u + ((x + 128u) >> 8)) >> 8; }   // exact x/255, rounded

// Source-over blend of (r,g,b) with alpha `a` onto a framebuffer pixel. The framebuffer is opaque.
inline uint32_t blend(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (a == 0) return dst;
    if (a == 255) return 0xFF000000u | (r << 16) | (g << 8) | b;
    const uint32_t ia = 255u - a;
    const uint32_t dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db = dst & 0xFFu;
    return 0xFF000000u | (div255(r * a + dr * ia) << 16) | (div255(g * a + dg * ia) << 8) | div255(b * a + db * ia);
}

// Multiplies two 0xAABBGGRR colours component by component.
inline void modulate(uint32_t vertex, uint32_t texel, uint32_t& r, uint32_t& g, uint32_t& b, uint32_t& a) {
    r = div255(channel(vertex, 0) * channel(texel, 0));
    g = div255(channel(vertex, 8) * channel(texel, 8));
    b = div255(channel(vertex, 16) * channel(texel, 16));
    a = div255(channel(vertex, 24) * channel(texel, 24));
}

// ---- triangle set up ----------------------------------------------------------------------------------
// Positions are snapped to 1/256 of a pixel so the edge functions are exact integers. With floating
// point, two triangles that share an edge could both claim (or both miss) a pixel on that edge.

constexpr int kSubpixel = 8;                       // bits of sub-pixel precision
constexpr int64_t kOne = int64_t{1} << kSubpixel;

inline int64_t toFixed(float v) { return static_cast<int64_t>(std::lround(v * static_cast<float>(kOne))); }

struct Point { int64_t x, y; };

// Twice the signed area of triangle (a, b, p): positive when p is on the left of a->b.
inline int64_t edge(const Point& a, const Point& b, const Point& p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

// Top-left rule: a pixel centre exactly on an edge belongs to the triangle only for top and left edges.
inline bool isTopLeft(const Point& a, const Point& b) {
    const int64_t dx = b.x - a.x, dy = b.y - a.y;
    return dy < 0 || (dy == 0 && dx > 0);   // for the winding we normalize to: positive area, i.e. clockwise on screen (y points down)
}

inline uint32_t sample(const uint32_t* texels, int w, int h, float u, float v) {
    const int x = std::clamp(static_cast<int>(u * static_cast<float>(w)), 0, w - 1);
    const int y = std::clamp(static_cast<int>(v * static_cast<float>(h)), 0, h - 1);
    return texels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
}

} // namespace

SoftRenderer::SoftRenderer() {
    ImGuiIO& io = ImGui::GetIO();
    // ImGui lets a renderer publish the standard "reset render state" callback. We keep no mutable
    // state between commands, so the callback has nothing to do.
    ImGui::GetPlatformIO().DrawCallback_ResetRenderState = [](const ImDrawList*, const ImDrawCmd*) {};
    io.BackendRendererName = "linux-windowing software";
    io.BackendRendererUserData = this;
    // We understand ImTextureData requests, so ImGui may grow the font atlas and rasterize glyphs on demand.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
}

SoftRenderer::~SoftRenderer() {
    if (!ImGui::GetCurrentContext()) return;
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount == 1 && tex->BackendUserData) {
            delete static_cast<Texture*>(tex->BackendUserData);
            tex->BackendUserData = nullptr;
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset);
}

// ---- textures ------------------------------------------------------------------------------------------

void SoftRenderer::updateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        auto* t = new Texture;
        t->width = tex->Width;
        t->height = tex->Height;
        t->rgba.assign(static_cast<size_t>(t->width) * static_cast<size_t>(t->height), 0);
        tex->BackendUserData = t;
        tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)));
        tex->UpdateRect = {0, 0, static_cast<unsigned short>(tex->Width), static_cast<unsigned short>(tex->Height)};
        tex->Updates.clear();   // the first upload is the whole image
        tex->Updates.push_back(tex->UpdateRect);
        tex->Status = ImTextureStatus_WantUpdates;   // fall through to the copy below
    }
    if (tex->Status == ImTextureStatus_WantUpdates) {
        auto* t = static_cast<Texture*>(tex->BackendUserData);
        // Copy only the rectangles that changed. Alpha8 atlases become white texels with that alpha, so
        // "vertex colour * texel" works the same for text and for coloured images.
        for (const ImTextureRect& r : tex->Updates) {
            for (int y = r.y; y < r.y + r.h; ++y) {
                const unsigned char* src = static_cast<const unsigned char*>(tex->GetPixelsAt(r.x, y));
                uint32_t* dst = &t->rgba[static_cast<size_t>(y) * static_cast<size_t>(t->width) + r.x];
                if (tex->Format == ImTextureFormat_Alpha8) {
                    for (int x = 0; x < r.w; ++x) dst[x] = (static_cast<uint32_t>(src[x]) << 24) | 0x00FFFFFFu;
                } else {
                    std::memcpy(dst, src, static_cast<size_t>(r.w) * 4);
                }
            }
        }
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        delete static_cast<Texture*>(tex->BackendUserData);
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

// ---- drawing -------------------------------------------------------------------------------------------

// A general triangle: edge functions, barycentric interpolation of colour and UV.
void SoftRenderer::drawTriangle(const ImDrawVert& va, const ImDrawVert& vb, const ImDrawVert& vc, const Texture* tex,
                                int clipX0, int clipY0, int clipX1, int clipY1, const Framebuffer& fb, float scale) {
    ++stats_.triangles;
    Point a{toFixed(va.pos.x * scale), toFixed(va.pos.y * scale)};
    Point b{toFixed(vb.pos.x * scale), toFixed(vb.pos.y * scale)};
    Point c{toFixed(vc.pos.x * scale), toFixed(vc.pos.y * scale)};
    const ImDrawVert* A = &va;
    const ImDrawVert* B = &vb;
    const ImDrawVert* C = &vc;

    int64_t area = edge(a, b, c);
    if (area == 0) return;   // degenerate: a line or a point covers no pixel centre
    if (area < 0) {          // make every triangle wind the same way
        std::swap(b, c);
        std::swap(B, C);
        area = -area;
    }

    // Bounding box of the triangle, clipped to the scissor rectangle.
    const int minX = std::max(clipX0, static_cast<int>(std::min({a.x, b.x, c.x}) >> kSubpixel));
    const int maxX = std::min(clipX1 - 1, static_cast<int>(std::max({a.x, b.x, c.x}) >> kSubpixel));
    const int minY = std::max(clipY0, static_cast<int>(std::min({a.y, b.y, c.y}) >> kSubpixel));
    const int maxY = std::min(clipY1 - 1, static_cast<int>(std::max({a.y, b.y, c.y}) >> kSubpixel));
    if (minX > maxX || minY > maxY) return;

    // Does the triangle need per-pixel interpolation at all? Most UI triangles are flat.
    const bool flatColour = A->col == B->col && B->col == C->col;
    const bool flatUv = A->uv.x == B->uv.x && B->uv.x == C->uv.x && A->uv.y == B->uv.y && B->uv.y == C->uv.y;

    // Fill-rule bias: a pixel centre on a bottom or right edge must NOT be inside, so shift those by one unit.
    const int64_t bias0 = isTopLeft(b, c) ? 0 : -1;
    const int64_t bias1 = isTopLeft(c, a) ? 0 : -1;
    const int64_t bias2 = isTopLeft(a, b) ? 0 : -1;
    const float invArea = 1.0f / static_cast<float>(area);

    for (int y = minY; y <= maxY; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(fb.pixels) + static_cast<size_t>(y) * static_cast<size_t>(fb.stride));
        for (int x = minX; x <= maxX; ++x) {
            const Point p{(int64_t{x} << kSubpixel) + kOne / 2, (int64_t{y} << kSubpixel) + kOne / 2};   // pixel centre
            const int64_t w0 = edge(b, c, p) + bias0;   // weight of vertex a (twice the area opposite it)
            const int64_t w1 = edge(c, a, p) + bias1;
            const int64_t w2 = edge(a, b, p) + bias2;
            if ((w0 | w1 | w2) < 0) continue;           // outside

            uint32_t col;
            float u, v;
            if (flatColour && flatUv) {
                col = A->col;
                u = A->uv.x;
                v = A->uv.y;
            } else {
                const float l0 = static_cast<float>(w0 - bias0) * invArea;
                const float l1 = static_cast<float>(w1 - bias1) * invArea;
                const float l2 = 1.0f - l0 - l1;
                u = l0 * A->uv.x + l1 * B->uv.x + l2 * C->uv.x;
                v = l0 * A->uv.y + l1 * B->uv.y + l2 * C->uv.y;
                if (flatColour) {
                    col = A->col;
                } else {
                    auto mix = [&](int shift) {
                        const float f = l0 * static_cast<float>(channel(A->col, shift)) + l1 * static_cast<float>(channel(B->col, shift)) +
                                        l2 * static_cast<float>(channel(C->col, shift));
                        return static_cast<uint32_t>(std::clamp(f + 0.5f, 0.0f, 255.0f)) << shift;
                    };
                    col = mix(0) | mix(8) | mix(16) | mix(24);
                }
            }
            uint32_t r, g, bl, al;
            if (tex) modulate(col, sample(tex->rgba.data(), tex->width, tex->height, u, v), r, g, bl, al);
            else { r = channel(col, 0); g = channel(col, 8); bl = channel(col, 16); al = channel(col, 24); }
            row[x] = blend(row[x], r, g, bl, al);
        }
    }
}

// ImGui draws every rectangle (window backgrounds, buttons, glyphs) as two triangles that share a
// diagonal. If a pair really is an axis-aligned rectangle of one colour, we can skip the edge
// functions and fill rows directly. Returns false if the pair is not such a rectangle.
bool SoftRenderer::drawQuad(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, const ImDrawVert& v3,
                            const Texture* tex, int clipX0, int clipY0, int clipX1, int clipY1, const Framebuffer& fb, float scale) {
    // Triangles are (v0, v1, v2) and (v0, v2, v3). For a rectangle v0 and v2 are opposite corners,
    // v1 and v3 the other two.
    if (v0.col != v1.col || v0.col != v2.col || v0.col != v3.col) return false;
    const bool a = v0.pos.x == v1.pos.x && v1.pos.y == v2.pos.y && v2.pos.x == v3.pos.x && v3.pos.y == v0.pos.y;   // v0 v1 vertical first
    const bool b = v0.pos.y == v1.pos.y && v1.pos.x == v2.pos.x && v2.pos.y == v3.pos.y && v3.pos.x == v0.pos.x;   // v0 v1 horizontal first
    if (!a && !b) return false;
    // The UVs must follow the same axis-aligned mapping (no rotated or mirrored-diagonal images), or
    // the cheap interpolation below would be wrong.
    const ImVec2 corner1 = a ? ImVec2(v0.uv.x, v2.uv.y) : ImVec2(v2.uv.x, v0.uv.y);   // expected UV of v1
    const ImVec2 corner3 = a ? ImVec2(v2.uv.x, v0.uv.y) : ImVec2(v0.uv.x, v2.uv.y);   // expected UV of v3
    if (v1.uv.x != corner1.x || v1.uv.y != corner1.y || v3.uv.x != corner3.x || v3.uv.y != corner3.y) return false;

    const float x0f = std::min(v0.pos.x, v2.pos.x) * scale, x1f = std::max(v0.pos.x, v2.pos.x) * scale;
    const float y0f = std::min(v0.pos.y, v2.pos.y) * scale, y1f = std::max(v0.pos.y, v2.pos.y) * scale;
    if (x1f <= x0f || y1f <= y0f) return true;   // empty: nothing to draw, nothing left over

    // A pixel is inside when its centre is in [min, max): the same answer the top-left rule gives.
    const int xs = std::max(clipX0, static_cast<int>(std::ceil(x0f - 0.5f)));
    const int xe = std::min(clipX1, static_cast<int>(std::ceil(x1f - 0.5f)));
    const int ys = std::max(clipY0, static_cast<int>(std::ceil(y0f - 0.5f)));
    const int ye = std::min(clipY1, static_cast<int>(std::ceil(y1f - 0.5f)));

    // UV at the rectangle's minimum and maximum corners (the UVs vary linearly across it).
    auto uvAt = [&](float px, float py, float& u, float& v) {
        // Interpolate between v0 and v2 (opposite corners): u depends on x only, v on y only.
        const float tx = (v2.pos.x == v0.pos.x) ? 0.0f : (px / scale - v0.pos.x) / (v2.pos.x - v0.pos.x);
        const float ty = (v2.pos.y == v0.pos.y) ? 0.0f : (py / scale - v0.pos.y) / (v2.pos.y - v0.pos.y);
        u = v0.uv.x + (v2.uv.x - v0.uv.x) * tx;
        v = v0.uv.y + (v2.uv.y - v0.uv.y) * ty;
    };

    const bool flatUv = v0.uv.x == v2.uv.x && v0.uv.y == v2.uv.y;
    for (int y = ys; y < ye; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(fb.pixels) + static_cast<size_t>(y) * static_cast<size_t>(fb.stride));
        for (int x = xs; x < xe; ++x) {
            uint32_t r, g, bl, al;
            if (!tex) {
                r = channel(v0.col, 0); g = channel(v0.col, 8); bl = channel(v0.col, 16); al = channel(v0.col, 24);
            } else {
                float u = v0.uv.x, v = v0.uv.y;
                if (!flatUv) uvAt(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, u, v);
                modulate(v0.col, sample(tex->rgba.data(), tex->width, tex->height, u, v), r, g, bl, al);
            }
            row[x] = blend(row[x], r, g, bl, al);
        }
    }
    return true;
}

void SoftRenderer::render(const ImDrawData* data, const Framebuffer& fb, const ImVec4& clear) {
    stats_ = {};
    // 1. Textures first: the draw commands below refer to them.
    if (data->Textures)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) updateTexture(tex);

    // 2. Clear.
    const uint32_t clearColour = 0xFF000000u | (static_cast<uint32_t>(std::clamp(clear.x, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
                                 (static_cast<uint32_t>(std::clamp(clear.y, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
                                 static_cast<uint32_t>(std::clamp(clear.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = 0; y < fb.height; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(fb.pixels) + static_cast<size_t>(y) * static_cast<size_t>(fb.stride));
        std::fill(row, row + fb.width, clearColour);
    }

    // 3. Draw lists.
    const float scale = data->FramebufferScale.x;
    for (const ImDrawList* list : data->CmdLists) {
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            ++stats_.commands;
            if (cmd.UserCallback) {   // an application hook (including the no-op "reset render state")
                cmd.UserCallback(list, &cmd);
                continue;
            }
            // Clip rectangle: logical units -> pixels, and never outside the framebuffer.
            const int cx0 = std::max(0, static_cast<int>(std::floor((cmd.ClipRect.x - data->DisplayPos.x) * scale)));
            const int cy0 = std::max(0, static_cast<int>(std::floor((cmd.ClipRect.y - data->DisplayPos.y) * scale)));
            const int cx1 = std::min(fb.width, static_cast<int>(std::ceil((cmd.ClipRect.z - data->DisplayPos.x) * scale)));
            const int cy1 = std::min(fb.height, static_cast<int>(std::ceil((cmd.ClipRect.w - data->DisplayPos.y) * scale)));
            if (cx1 <= cx0 || cy1 <= cy0) continue;

            const Texture* tex = reinterpret_cast<const Texture*>(static_cast<uintptr_t>(cmd.GetTexID()));
            const ImDrawVert* base = vtx + cmd.VtxOffset;
            const ImDrawIdx* ib = idx + cmd.IdxOffset;
            for (unsigned i = 0; i + 3 <= cmd.ElemCount;) {
                // Rectangles come as two triangles that share a diagonal: (0,1,2) (0,2,3).
                if (i + 6 <= cmd.ElemCount && ib[i + 3] == ib[i] && ib[i + 4] == ib[i + 2] &&
                    drawQuad(base[ib[i]], base[ib[i + 1]], base[ib[i + 2]], base[ib[i + 5]], tex, cx0, cy0, cx1, cy1, fb, scale)) {
                    ++stats_.quads;
                    stats_.triangles += 2;   // a quad is two triangles
                    i += 6;
                    continue;
                }
                drawTriangle(base[ib[i]], base[ib[i + 1]], base[ib[i + 2]], tex, cx0, cy0, cx1, cy1, fb, scale);
                i += 3;
            }
        }
    }
}

} // namespace ww::imgui
