#include "ww/imgui_gl.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ww::imgui {

using namespace gl;

namespace {

// ---- shaders ------------------------------------------------------------------------------------------
// GLSL 3.30 is the shading language that goes with OpenGL 3.3.

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;     // unpacked from 4 bytes, 0..255 -> 0..1 by the vertex fetch
uniform mat4 ProjMtx;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main() {
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
layout(location = 0) out vec4 Out_Color;
void main() {
    Out_Color = Frag_Color * texture(Texture, Frag_UV);
}
)";

unsigned compile(const Functions& g, GLenum type, const char* source, std::string* error) {
    const GLuint shader = g.CreateShader(type);
    g.ShaderSource(shader, 1, &source, nullptr);
    g.CompileShader(shader);
    GLint ok = 0;
    g.GetShaderiv(shader, COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        g.GetShaderiv(shader, INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        g.GetShaderInfoLog(shader, len, nullptr, log.data());
        if (error) *error = std::string(type == VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + log.c_str();
        g.DeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

std::unique_ptr<GlRenderer> GlRenderer::create(EglContext& context, std::string* error) {
    std::unique_ptr<GlRenderer> r(new GlRenderer(context));
    if (!r->init(error)) return nullptr;
    return r;
}

bool GlRenderer::init(std::string* error) {
    egl_.makeCurrent();
    if (const char* missing = load(gl_, &EglContext::getProcAddress)) {
        if (error) *error = std::string("OpenGL function not found: ") + missing;
        return false;
    }
    const Functions& g = gl_;
    version_ = reinterpret_cast<const char*>(g.GetString(VERSION));
    renderer_ = reinterpret_cast<const char*>(g.GetString(RENDERER));
    GLint major = 0, minor = 0, profile = 0;
    g.GetIntegerv(MAJOR_VERSION, &major);
    g.GetIntegerv(MINOR_VERSION, &minor);
    g.GetIntegerv(CONTEXT_PROFILE_MASK, &profile);
    if (major < 3 || (major == 3 && minor < 3) || !(profile & CONTEXT_CORE_PROFILE_BIT)) {
        if (error) *error = "need OpenGL 3.3 core, got " + version_;
        return false;
    }

    // Program
    const GLuint vs = compile(g, VERTEX_SHADER, kVertexShader, error);
    const GLuint fs = vs ? compile(g, FRAGMENT_SHADER, kFragmentShader, error) : 0;
    if (!vs || !fs) return false;
    program_ = g.CreateProgram();
    g.AttachShader(program_, vs);
    g.AttachShader(program_, fs);
    g.LinkProgram(program_);
    g.DeleteShader(vs);
    g.DeleteShader(fs);
    GLint linked = 0;
    g.GetProgramiv(program_, LINK_STATUS, &linked);
    if (!linked) {
        if (error) *error = "shader program did not link";
        return false;
    }
    locProjection_ = g.GetUniformLocation(program_, "ProjMtx");
    locTexture_ = g.GetUniformLocation(program_, "Texture");

    // Vertex array: describes how ImDrawVert's 20 bytes are split into attributes. Core profile
    // requires a VAO, and it remembers this set up so we do it only once.
    g.GenVertexArrays(1, &vao_);
    g.GenBuffers(1, &vbo_);
    g.GenBuffers(1, &ebo_);
    g.BindVertexArray(vao_);
    g.BindBuffer(ARRAY_BUFFER, vbo_);
    g.BindBuffer(ELEMENT_ARRAY_BUFFER, ebo_);
    g.EnableVertexAttribArray(0);
    g.EnableVertexAttribArray(1);
    g.EnableVertexAttribArray(2);
    g.VertexAttribPointer(0, 2, FLOAT, FALSE_, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    g.VertexAttribPointer(1, 2, FLOAT, FALSE_, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    g.VertexAttribPointer(2, 4, UNSIGNED_BYTE, TRUE_, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
    g.BindVertexArray(0);

    g.GenFramebuffers(1, &fbo_);
    g.GenRenderbuffers(1, &colorBuffer_);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "linux-windowing OpenGL 3.3";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetPlatformIO().DrawCallback_ResetRenderState = [](const ImDrawList*, const ImDrawCmd*) {};
    return true;
}

GlRenderer::~GlRenderer() {
    egl_.makeCurrent();
    if (ImGui::GetCurrentContext()) {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
            if (tex->RefCount == 1) destroyTexture(tex);
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendRendererUserData = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset);
    }
    const Functions& g = gl_;
    if (!g.DeleteBuffers) return;
    if (colorBuffer_) g.DeleteRenderbuffers(1, &colorBuffer_);
    if (fbo_) g.DeleteFramebuffers(1, &fbo_);
    if (vbo_) g.DeleteBuffers(1, &vbo_);
    if (ebo_) g.DeleteBuffers(1, &ebo_);
    if (vao_) g.DeleteVertexArrays(1, &vao_);
    if (program_) g.DeleteProgram(program_);
}

// ---- textures ------------------------------------------------------------------------------------------

void GlRenderer::destroyTexture(ImTextureData* tex) {
    if (tex->TexID != ImTextureID_Invalid) {
        const GLuint id = static_cast<GLuint>(tex->TexID);
        gl_.DeleteTextures(1, &id);
    }
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void GlRenderer::updateTexture(ImTextureData* tex) {
    const Functions& g = gl_;
    const bool alpha8 = tex->Format == ImTextureFormat_Alpha8;
    const GLenum internalFormat = alpha8 ? R8 : RGBA8;
    const GLenum format = alpha8 ? RED : RGBA;

    if (tex->Status == ImTextureStatus_WantCreate) {
        GLuint id = 0;
        g.GenTextures(1, &id);
        g.BindTexture(TEXTURE_2D, id);
        g.TexParameteri(TEXTURE_2D, TEXTURE_MIN_FILTER, static_cast<GLint>(LINEAR));
        g.TexParameteri(TEXTURE_2D, TEXTURE_MAG_FILTER, static_cast<GLint>(LINEAR));
        g.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_S, static_cast<GLint>(CLAMP_TO_EDGE));
        g.TexParameteri(TEXTURE_2D, TEXTURE_WRAP_T, static_cast<GLint>(CLAMP_TO_EDGE));
        if (alpha8) {
            // One byte per texel on the GPU; the shader still sees (1, 1, 1, alpha).
            const GLint swizzle[4] = {static_cast<GLint>(ONE), static_cast<GLint>(ONE), static_cast<GLint>(ONE), static_cast<GLint>(RED)};
            g.TexParameteriv(TEXTURE_2D, TEXTURE_SWIZZLE_RGBA, swizzle);
        }
        g.PixelStorei(UNPACK_ALIGNMENT, 1);
        g.PixelStorei(UNPACK_ROW_LENGTH, 0);
        g.TexImage2D(TEXTURE_2D, 0, static_cast<GLint>(internalFormat), tex->Width, tex->Height, 0, format, UNSIGNED_BYTE, tex->GetPixels());
        ++stats_.textureUploads;
        tex->SetTexID(static_cast<ImTextureID>(id));
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        // Only the rectangles that changed. UNPACK_ROW_LENGTH tells GL that the source rows are
        // `Width` texels apart, so a sub-rectangle can be read straight out of the full image.
        g.BindTexture(TEXTURE_2D, static_cast<GLuint>(tex->TexID));
        g.PixelStorei(UNPACK_ALIGNMENT, 1);
        g.PixelStorei(UNPACK_ROW_LENGTH, tex->Width);
        for (const ImTextureRect& r : tex->Updates) {
            g.TexSubImage2D(TEXTURE_2D, 0, r.x, r.y, r.w, r.h, format, UNSIGNED_BYTE, tex->GetPixelsAt(r.x, r.y));
            ++stats_.textureUploads;
        }
        g.PixelStorei(UNPACK_ROW_LENGTH, 0);
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        destroyTexture(tex);
    }
}

// ---- off-screen target ----------------------------------------------------------------------------------

void GlRenderer::resizeTarget(int width, int height) {
    if (width == targetWidth_ && height == targetHeight_) return;
    const Functions& g = gl_;
    g.BindRenderbuffer(RENDERBUFFER, colorBuffer_);
    g.RenderbufferStorage(RENDERBUFFER, RGBA8, width, height);
    g.BindFramebuffer(FRAMEBUFFER, fbo_);
    g.FramebufferRenderbuffer(FRAMEBUFFER, COLOR_ATTACHMENT0, RENDERBUFFER, colorBuffer_);
    targetWidth_ = width;
    targetHeight_ = height;
}

// ---- rendering --------------------------------------------------------------------------------------------

void GlRenderer::render(const ImDrawData* data, const Framebuffer& fb, const ImVec4& clear) {
    stats_ = {};
    egl_.makeCurrent();
    const Functions& g = gl_;

    if (data->Textures)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) updateTexture(tex);

    resizeTarget(fb.width, fb.height);
    g.BindFramebuffer(FRAMEBUFFER, fbo_);
    g.Viewport(0, 0, fb.width, fb.height);
    g.Disable(DEPTH_TEST);
    g.Disable(CULL_FACE);
    g.Enable(BLEND);
    g.BlendEquation(FUNC_ADD);
    // Colour: normal "source over". Alpha: accumulate coverage, so the result stays opaque.
    g.BlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE, ONE_MINUS_SRC_ALPHA);
    g.Disable(SCISSOR_TEST);
    g.ClearColor(clear.x, clear.y, clear.z, 1.0f);
    g.Clear(COLOR_BUFFER_BIT);
    g.Enable(SCISSOR_TEST);

    // Orthographic projection from ImGui's logical coordinates to clip space. ImGui's y axis points
    // down; a textbook projection would flip it, and glReadPixels would then return the image
    // upside down. We want the readback to be top row first, like our CPU framebuffer, so this
    // matrix maps y=top to clip -1 (the first row in memory) and the flip is free.
    const float L = data->DisplayPos.x, R = L + data->DisplaySize.x;
    const float T = data->DisplayPos.y, B = T + data->DisplaySize.y;
    const float projection[4][4] = {
        {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
        {0.0f, 2.0f / (B - T), 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f, 0.0f},
        {-(R + L) / (R - L), -(B + T) / (B - T), 0.0f, 1.0f},
    };
    g.UseProgram(program_);
    g.Uniform1i(locTexture_, 0);
    g.UniformMatrix4fv(locProjection_, 1, FALSE_, &projection[0][0]);
    g.ActiveTexture(TEXTURE0);
    g.BindVertexArray(vao_);

    const float scale = data->FramebufferScale.x;
    for (const ImDrawList* list : data->CmdLists) {
        // Upload this list. STREAM_DRAW tells the driver we will rewrite the buffers every frame.
        g.BindBuffer(ARRAY_BUFFER, vbo_);
        g.BufferData(ARRAY_BUFFER, static_cast<GLsizeiptr>(list->VtxBuffer.Size) * static_cast<GLsizeiptr>(sizeof(ImDrawVert)),
                     list->VtxBuffer.Data, STREAM_DRAW);
        g.BindBuffer(ELEMENT_ARRAY_BUFFER, ebo_);
        g.BufferData(ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(list->IdxBuffer.Size) * static_cast<GLsizeiptr>(sizeof(ImDrawIdx)),
                     list->IdxBuffer.Data, STREAM_DRAW);

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                cmd.UserCallback(list, &cmd);
                continue;
            }
            // Clip rectangle in pixels. Because of the flipped projection, GL's bottom-left origin
            // coincides with ImGui's top-left, so no "height - y" arithmetic is needed.
            const int x0 = std::max(0, static_cast<int>((cmd.ClipRect.x - data->DisplayPos.x) * scale));
            const int y0 = std::max(0, static_cast<int>((cmd.ClipRect.y - data->DisplayPos.y) * scale));
            const int x1 = std::min(fb.width, static_cast<int>(std::ceil((cmd.ClipRect.z - data->DisplayPos.x) * scale)));
            const int y1 = std::min(fb.height, static_cast<int>(std::ceil((cmd.ClipRect.w - data->DisplayPos.y) * scale)));
            if (x1 <= x0 || y1 <= y0) continue;
            g.Scissor(x0, y0, x1 - x0, y1 - y0);
            g.BindTexture(TEXTURE_2D, static_cast<GLuint>(cmd.GetTexID()));
            g.DrawElementsBaseVertex(TRIANGLES, static_cast<GLsizei>(cmd.ElemCount), sizeof(ImDrawIdx) == 2 ? UNSIGNED_SHORT : UNSIGNED_INT,
                                     reinterpret_cast<const void*>(static_cast<uintptr_t>(cmd.IdxOffset) * sizeof(ImDrawIdx)),
                                     static_cast<GLint>(cmd.VtxOffset));
            ++stats_.drawCalls;
            stats_.triangles += static_cast<int>(cmd.ElemCount / 3);
        }
    }
    g.BindVertexArray(0);
    g.Disable(SCISSOR_TEST);

    // Read the pixels back. BGRA + UNSIGNED_BYTE is bytes B, G, R, A in memory, which on a little endian
    // machine is the 32 bit value 0xAARRGGBB our framebuffer uses.
    g.PixelStorei(PACK_ALIGNMENT, 4);
    g.PixelStorei(PACK_ROW_LENGTH, fb.stride / 4);
    g.ReadPixels(0, 0, fb.width, fb.height, BGRA, UNSIGNED_BYTE, fb.pixels);
    g.PixelStorei(PACK_ROW_LENGTH, 0);
}

} // namespace ww::imgui
