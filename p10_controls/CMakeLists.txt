add_executable(p10_controls main.cpp controls.hpp)
ww_target_defaults(p10_controls)
target_link_libraries(p10_controls PRIVATE ww_ui ww_renderer ww_platform)
