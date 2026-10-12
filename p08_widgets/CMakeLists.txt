add_executable(p08_widgets main.cpp gallery.hpp)
ww_target_defaults(p08_widgets)
target_link_libraries(p08_widgets PRIVATE ww_ui ww_renderer ww_platform)
