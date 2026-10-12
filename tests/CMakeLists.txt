add_executable(ui_test ui_test.cpp)
ww_target_defaults(ui_test)
target_include_directories(ui_test PRIVATE ${CMAKE_SOURCE_DIR}/p08_widgets)
target_link_libraries(ui_test PRIVATE ww_ui ww_renderer ww_platform)
add_test(NAME ui_test COMMAND ui_test)
# fontconfig (loaded by Pango) keeps global caches that LeakSanitizer reports; ignore those, keep the rest of ASan.
set_tests_properties(ui_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
