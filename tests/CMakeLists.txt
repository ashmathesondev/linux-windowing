add_executable(ui_test ui_test.cpp)
ww_target_defaults(ui_test)
target_include_directories(ui_test PRIVATE ${CMAKE_SOURCE_DIR}/p08_widgets)
target_link_libraries(ui_test PRIVATE ww_ui ww_renderer ww_platform)
add_test(NAME ui_test COMMAND ui_test)
# fontconfig (loaded by Pango) keeps global caches that LeakSanitizer reports; ignore those, keep the rest of ASan.
set_tests_properties(ui_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

add_executable(controls_test controls_test.cpp)
ww_target_defaults(controls_test)
target_include_directories(controls_test PRIVATE ${CMAKE_SOURCE_DIR}/p10_controls)
target_link_libraries(controls_test PRIVATE ww_ui ww_renderer ww_platform)
add_test(NAME controls_test COMMAND controls_test)
set_tests_properties(controls_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

if(WW_BUILD_IMGUI)
  add_executable(imgui_soft_test imgui_soft_test.cpp)
  ww_target_defaults(imgui_soft_test)
  target_link_libraries(imgui_soft_test PRIVATE ww_imgui ww_renderer ww_platform)
  add_test(NAME imgui_soft_test COMMAND imgui_soft_test)
  set_tests_properties(imgui_soft_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()

if(WW_BUILD_IMGUI AND WW_BUILD_GL)
  add_executable(imgui_gl_test imgui_gl_test.cpp)
  ww_target_defaults(imgui_gl_test)
  target_link_libraries(imgui_gl_test PRIVATE ww_imgui_gl ww_renderer ww_platform)
  add_test(NAME imgui_gl_test COMMAND imgui_gl_test)
  # Exit code 77 means "no OpenGL context here": report a skip, not a failure.
  set_tests_properties(imgui_gl_test PROPERTIES SKIP_RETURN_CODE 77
                       ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;LIBGL_ALWAYS_SOFTWARE=1")
endif()
