#pragma once
// An OpenGL 3.3 core context created through EGL, without any window.
//
// EGL is the Khronos API that connects OpenGL to the system: it creates contexts and surfaces. It
// works on X11, Wayland and with no display server at all ("surfaceless"). We use the last mode:
// the context renders into a framebuffer object and we read the pixels back, so the same code
// presents through our existing X11 and Wayland windows.

#include <memory>
#include <string>

namespace ww::gl {

class EglContext {
public:
    // Returns nullptr and fills `error` when no OpenGL 3.3 core context can be made.
    static std::unique_ptr<EglContext> create(std::string* error);
    ~EglContext();
    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

    bool makeCurrent();
    static void* getProcAddress(const char* name);

    // "surfaceless platform, EGL 1.5" and similar, for logs.
    const std::string& description() const { return description_; }

private:
    EglContext() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string description_;
};

} // namespace ww::gl
