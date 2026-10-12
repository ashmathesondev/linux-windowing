add_executable(p05_wayland_input main.cpp)
ww_target_defaults(p05_wayland_input)
target_link_libraries(p05_wayland_input PRIVATE
  ww_wayland_protocols PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_CURSOR PkgConfig::XKBCOMMON)
