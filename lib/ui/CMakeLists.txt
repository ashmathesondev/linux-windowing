add_library(ww_ui STATIC src/ui.cpp)
ww_target_defaults(ww_ui)
target_include_directories(ww_ui PUBLIC include)
target_link_libraries(ww_ui PUBLIC ww_renderer ww_platform)
