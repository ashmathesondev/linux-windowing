#include "ww/egl_context.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdlib>
#include <cstring>

namespace ww::gl {

struct EglContext::Impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
};

namespace {

bool hasExtension(const char* list, const char* name) {
    if (!list) return false;
    const size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

const char* eglErrorName(EGLint e) {
    switch (e) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        default: return "an EGL error";
    }
}

} // namespace

std::unique_ptr<EglContext> EglContext::create(std::string* error) {
    auto fail = [&](const std::string& what) -> std::unique_ptr<EglContext> {
        if (error) *error = what + " (" + eglErrorName(eglGetError()) + ")";
        return nullptr;
    };
    std::unique_ptr<EglContext> ctx(new EglContext);
    ctx->impl_ = std::make_unique<Impl>();
    Impl& e = *ctx->impl_;

    // 1. A display. Prefer the "surfaceless" platform: it needs no window system, so it works over
    //    ssh, in a container and in CI. Fall back to whatever EGL considers the default display.
    const char* platform = "default display";
    EGLint major = 0, minor = 0;
    const char* clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (hasExtension(clientExt, "EGL_EXT_platform_base") && hasExtension(clientExt, "EGL_MESA_platform_surfaceless")) {
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (getPlatformDisplay) {
            e.display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
            if (e.display != EGL_NO_DISPLAY && eglInitialize(e.display, &major, &minor)) platform = "surfaceless platform";
            else e.display = EGL_NO_DISPLAY;
        }
    }
    if (e.display == EGL_NO_DISPLAY) {
        e.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (e.display == EGL_NO_DISPLAY || !eglInitialize(e.display, &major, &minor)) return fail("could not initialize EGL");
    }

    // 2. Desktop OpenGL (not OpenGL ES), and a configuration with 8 bit RGBA that can be a pbuffer.
    if (!eglBindAPI(EGL_OPENGL_API)) return fail("this EGL does not offer desktop OpenGL");
    const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(e.display, configAttribs, &config, 1, &count) || count == 0) return fail("no usable EGL configuration");

    // 3. A core profile 3.3 context. "Core" means the old fixed-function API is absent, which is
    //    exactly what we want to be sure we are not using. Drivers may return a newer version that is
    //    compatible with 3.3 (for example 4.5); what we ask for is the minimum.
    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    e.context = eglCreateContext(e.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (e.context == EGL_NO_CONTEXT) return fail("could not create an OpenGL 3.3 core context");

    // 4. We render into our own framebuffer object, so no surface is needed. Old drivers insist
    //    on one anyway; give them a 1x1 pbuffer.
    if (!hasExtension(eglQueryString(e.display, EGL_EXTENSIONS), "EGL_KHR_surfaceless_context")) {
        const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        e.surface = eglCreatePbufferSurface(e.display, config, pbufferAttribs);
    }

    ctx->description_ = std::string(platform) + ", EGL " + std::to_string(major) + "." + std::to_string(minor);
    if (!ctx->makeCurrent()) return fail("could not make the context current");
    return ctx;
}

EglContext::~EglContext() {
    if (!impl_ || impl_->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (impl_->surface != EGL_NO_SURFACE) eglDestroySurface(impl_->display, impl_->surface);
    if (impl_->context != EGL_NO_CONTEXT) eglDestroyContext(impl_->display, impl_->context);
    eglTerminate(impl_->display);
}

bool EglContext::makeCurrent() {
    return eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) == EGL_TRUE;
}

void* EglContext::getProcAddress(const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); }

} // namespace ww::gl
