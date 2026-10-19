# Dear ImGui backends written for this project: platform glue, CPU rasterizer and (optionally) OpenGL.
add_library(ww_imgui STATIC src/imgui_host.cpp src/imgui_soft.cpp)
ww_target_defaults(ww_imgui)
target_include_directories(ww_imgui PUBLIC include)
target_link_libraries(ww_imgui PUBLIC ww_imgui_core ww_platform)

if(WW_BUILD_GL)
  # OpenGL 3.3 core through EGL. Only libEGL is linked: the OpenGL functions come from
  # eglGetProcAddress (include/ww/gl33.hpp), so libGL and <GL/gl.h> are not needed.
  add_library(ww_imgui_gl STATIC src/egl_context.cpp src/imgui_gl.cpp)
  ww_target_defaults(ww_imgui_gl)
  target_link_libraries(ww_imgui_gl PUBLIC ww_imgui PkgConfig::EGL)
endif()
