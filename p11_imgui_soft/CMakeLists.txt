add_executable(p11_imgui_soft main.cpp gallery.hpp)
ww_target_defaults(p11_imgui_soft)
target_link_libraries(p11_imgui_soft PRIVATE ww_imgui ww_renderer ww_platform)
