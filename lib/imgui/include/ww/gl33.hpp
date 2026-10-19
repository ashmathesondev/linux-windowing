#pragma once
// The OpenGL 3.3 core functions this project uses, declared by hand.
//
// A real project normally pulls these in from a generated loader (glad, glew, ...). Our needs are
// small enough to list: about 60 functions. The point of doing it by hand is to see what a loader
// is: a table of function pointers filled in with eglGetProcAddress() (or glXGetProcAddress(),
// wglGetProcAddress()) once a context exists. No <GL/gl.h> and no libGL are needed.

#include <cstddef>
#include <cstdint>

namespace ww::gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum FALSE_ = 0, TRUE_ = 1;
constexpr GLenum NO_ERROR_ = 0;
constexpr GLenum ZERO = 0, ONE = 1;
constexpr GLenum TRIANGLES = 0x0004;
constexpr GLenum SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum FUNC_ADD = 0x8006;
constexpr GLenum BLEND = 0x0BE2, CULL_FACE = 0x0B44, DEPTH_TEST = 0x0B71, SCISSOR_TEST = 0x0C11;
constexpr GLenum UNPACK_ROW_LENGTH = 0x0CF2, UNPACK_ALIGNMENT = 0x0CF5, PACK_ROW_LENGTH = 0x0D02, PACK_ALIGNMENT = 0x0D05;
constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, UNSIGNED_INT = 0x1405, FLOAT = 0x1406;
constexpr GLenum RED = 0x1903, RGBA = 0x1908;
constexpr GLenum VENDOR = 0x1F00, RENDERER = 0x1F01, VERSION = 0x1F02;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803;
constexpr GLenum CLAMP_TO_EDGE = 0x812F;
constexpr GLenum BGRA = 0x80E1;
constexpr GLenum RGBA8 = 0x8058, R8 = 0x8229;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893, STREAM_DRAW = 0x88E0;
constexpr GLenum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31;
constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum SHADING_LANGUAGE_VERSION = 0x8B8C;
constexpr GLenum TEXTURE_SWIZZLE_RGBA = 0x8E46;
constexpr GLenum FRAMEBUFFER = 0x8D40, RENDERBUFFER = 0x8D41, COLOR_ATTACHMENT0 = 0x8CE0, FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum MAJOR_VERSION = 0x821B, MINOR_VERSION = 0x821C, CONTEXT_PROFILE_MASK = 0x9126;
constexpr GLenum CONTEXT_CORE_PROFILE_BIT = 0x00000001;
constexpr GLbitfield COLOR_BUFFER_BIT = 0x00004000;

// The table. One member per function, named like the C function without the "gl" prefix.
#define WW_GL_FUNCTIONS(X)                                                                                                  \
    X(const GLubyte*, GetString, (GLenum name))                                                                              \
    X(void, GetIntegerv, (GLenum pname, GLint* data))                                                                        \
    X(GLenum, GetError, ())                                                                                                  \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                                              \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                                               \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                                        \
    X(void, Clear, (GLbitfield mask))                                                                                        \
    X(void, Enable, (GLenum cap))                                                                                            \
    X(void, Disable, (GLenum cap))                                                                                           \
    X(void, BlendEquation, (GLenum mode))                                                                                    \
    X(void, BlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcA, GLenum dstA))                                     \
    X(void, PixelStorei, (GLenum pname, GLint param))                                                                        \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* pixels))                  \
    X(void, Finish, ())                                                                                                      \
    X(void, GenTextures, (GLsizei n, GLuint* textures))                                                                      \
    X(void, DeleteTextures, (GLsizei n, const GLuint* textures))                                                             \
    X(void, BindTexture, (GLenum target, GLuint texture))                                                                    \
    X(void, ActiveTexture, (GLenum texture))                                                                                 \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                                                       \
    X(void, TexParameteriv, (GLenum target, GLenum pname, const GLint* params))                                              \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, \
                         GLenum type, const void* pixels))                                                                   \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,  \
                            const void* pixels))                                                                             \
    X(GLuint, CreateShader, (GLenum type))                                                                                   \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))                  \
    X(void, CompileShader, (GLuint shader))                                                                                  \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                                                       \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))                            \
    X(void, DeleteShader, (GLuint shader))                                                                                   \
    X(GLuint, CreateProgram, ())                                                                                             \
    X(void, AttachShader, (GLuint program, GLuint shader))                                                                   \
    X(void, LinkProgram, (GLuint program))                                                                                   \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                                                     \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))                          \
    X(void, DeleteProgram, (GLuint program))                                                                                 \
    X(void, UseProgram, (GLuint program))                                                                                    \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                                                       \
    X(void, Uniform1i, (GLint location, GLint v0))                                                                           \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))                    \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                                                    \
    X(void, BindVertexArray, (GLuint array))                                                                                 \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                                           \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                                                        \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                                                      \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                                    \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                                               \
    X(void, EnableVertexAttribArray, (GLuint index))                                                                         \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,               \
                                  const void* pointer))                                                                      \
    X(void, DrawElementsBaseVertex, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLint basevertex))        \
    X(void, GenFramebuffers, (GLsizei n, GLuint* ids))                                                                       \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                                                            \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* ids))                                                              \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint renderbuffer))               \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                                                       \
    X(void, GenRenderbuffers, (GLsizei n, GLuint* ids))                                                                      \
    X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer))                                                          \
    X(void, RenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei w, GLsizei h))                               \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint* ids))

struct Functions {
#define WW_GL_DECLARE(ret, name, args) ret(*name) args = nullptr;
    WW_GL_FUNCTIONS(WW_GL_DECLARE)
#undef WW_GL_DECLARE
};

// Fills `out` using `getProc` (e.g. eglGetProcAddress). Returns the name of the first function that
// could not be found, or nullptr on success.
inline const char* load(Functions& out, void* (*getProc)(const char*)) {
#define WW_GL_LOAD(ret, name, args)                                                  \
    out.name = reinterpret_cast<ret(*) args>(getProc("gl" #name));                   \
    if (!out.name) return "gl" #name;
    WW_GL_FUNCTIONS(WW_GL_LOAD)
#undef WW_GL_LOAD
    return nullptr;
}

} // namespace ww::gl
