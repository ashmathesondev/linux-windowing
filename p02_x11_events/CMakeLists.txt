add_executable(p02_x11_events main.cpp)
ww_target_defaults(p02_x11_events)
target_link_libraries(p02_x11_events PRIVATE
  PkgConfig::X11 PkgConfig::X11_XCB PkgConfig::XCB
  PkgConfig::XKBCOMMON PkgConfig::XKBCOMMON_X11)
