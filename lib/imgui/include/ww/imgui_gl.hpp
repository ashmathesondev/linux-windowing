#pragma once
// The renderer half of a Dear ImGui backend, on OpenGL 3.3 core.
//
// The same ImDrawData the CPU renderer loops over is uploaded to the GPU as two buffers (vertices and
// indices) and drawn with one glDrawElements per ImDrawCmd. The result is rendered into an off-screen
// framebuffer object and read back into the window's CPU framebuffer, so it can be shown by the
// X11 and Wayland windows we already have.

#include <memory>
#include <string>

#include "imgui.h"
#include "ww/egl_context.hpp"
#include "ww/gl33.hpp"
#include "ww/window.hpp"

namespace ww::imgui {

class GlRenderer {
public:
    // Returns nullptr and fills `error` if the shaders do not compile or functions are missing.
    // `context` must outlive the renderer.
    static std::unique_ptr<GlRenderer> create(gl::EglContext& context, std::string* error);
    ~GlRenderer();   // destroy before the ImGui context (see Host)
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    // Clears the framebuffer to `clear`, draws everything in `data`, and copies the result to `fb`.
    void render(const ImDrawData* data, const Framebuffer& fb, const ImVec4& clear);

    struct Stats {
        int drawCalls = 0;
        int triangles = 0;
        int textureUploads = 0;   // calls that sent pixels to the GPU
    };
    const Stats& stats() const { return stats_; }
    const std::string& glVersion() const { return version_; }   // "4.5 (Core Profile) Mesa ..."
    const std::string& glRenderer() const { return renderer_; }

private:
    GlRenderer(gl::EglContext& context) : egl_(context) {}
    bool init(std::string* error);
    void updateTexture(ImTextureData* tex);
    void destroyTexture(ImTextureData* tex);
    void resizeTarget(int width, int height);

    gl::EglContext& egl_;
    gl::Functions gl_;
    std::string version_, renderer_;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0, ebo_ = 0, fbo_ = 0, colorBuffer_ = 0;
    int locProjection_ = -1, locTexture_ = -1;
    int targetWidth_ = 0, targetHeight_ = 0;
    Stats stats_;
};

} // namespace ww::imgui
