add_executable(p12_imgui_gl main.cpp)
ww_target_defaults(p12_imgui_gl)
target_include_directories(p12_imgui_gl PRIVATE ${CMAKE_SOURCE_DIR}/p11_imgui_soft)   # shares gallery.hpp
target_link_libraries(p12_imgui_gl PRIVATE ww_imgui_gl ww_renderer ww_platform)
