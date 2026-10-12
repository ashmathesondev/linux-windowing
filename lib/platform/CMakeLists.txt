add_library(ww_platform STATIC
  src/window.cpp
  src/key_translate.cpp)
ww_target_defaults(ww_platform)
target_include_directories(ww_platform PUBLIC include PRIVATE src)
target_link_libraries(ww_platform PUBLIC PkgConfig::XKBCOMMON)

if(WW_BUILD_X11)
  target_sources(ww_platform PRIVATE src/x11_window.cpp)
  target_compile_definitions(ww_platform PRIVATE WW_HAVE_X11)
  target_link_libraries(ww_platform PRIVATE
    PkgConfig::X11 PkgConfig::XEXT PkgConfig::X11_XCB PkgConfig::XCB PkgConfig::XKBCOMMON_X11)
endif()

if(WW_BUILD_WAYLAND)
  target_sources(ww_platform PRIVATE src/wayland_window.cpp)
  target_compile_definitions(ww_platform PRIVATE WW_HAVE_WAYLAND)
  target_link_libraries(ww_platform PRIVATE
    ww_wayland_protocols PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_CURSOR PkgConfig::PANGOCAIRO)
endif()
