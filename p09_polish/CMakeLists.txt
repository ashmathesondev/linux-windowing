add_executable(p09_polish main.cpp)
ww_target_defaults(p09_polish)
target_link_libraries(p09_polish PRIVATE ww_ui ww_renderer ww_platform)