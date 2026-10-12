add_executable(p03_x11_pixels main.cpp)
ww_target_defaults(p03_x11_pixels)
target_link_libraries(p03_x11_pixels PRIVATE PkgConfig::X11 PkgConfig::XEXT)
