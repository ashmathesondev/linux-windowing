add_executable(p01_x11_window main.cpp)
ww_target_defaults(p01_x11_window)
target_link_libraries(p01_x11_window PRIVATE PkgConfig::X11)
