add_library(ww_renderer STATIC src/renderer.cpp)
ww_target_defaults(ww_renderer)
target_include_directories(ww_renderer PUBLIC include)
target_link_libraries(ww_renderer
  PUBLIC ww_platform                      # for ww::Framebuffer
  PRIVATE PkgConfig::PANGOCAIRO PkgConfig::CAIRO)
