# Fetches Dear ImGui (and nothing else) and builds its core as the static library `ww_imgui_core`.
#
# Dear ImGui has no CMakeLists.txt of its own: it is meant to be copied into a project. FetchContent
# downloads a fixed release tag, and we compile the five core .cpp files ourselves. Our own
# backends (platform glue, software rasterizer, OpenGL renderer) live in lib/imgui, not in
# imgui's backends/ directory.
#
# Offline? Point CMake at a checkout instead of downloading:
#   cmake --preset gcc-debug -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui

set(WW_IMGUI_TAG "v1.93.0" CACHE STRING "Dear ImGui release tag to fetch")

# One download shared by all presets (build/gcc-debug, build/clang-asan, ...). This must be set
# before FetchContent is included, which gives the variable its own default.
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
  set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/_deps" CACHE PATH "Where FetchContent keeps downloads")
endif()
include(FetchContent)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        ${WW_IMGUI_TAG}
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)   # imgui has no CMakeLists.txt, so this only downloads

add_library(ww_imgui_core STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp)
# Third party code: no ww_target_defaults (our strict warnings), and ImGui's header is a SYSTEM
# include so its warnings do not show up in ours.
target_include_directories(ww_imgui_core SYSTEM PUBLIC ${imgui_SOURCE_DIR})
target_compile_definitions(ww_imgui_core PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(WW_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(ww_imgui_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(ww_imgui_core PUBLIC -fsanitize=address,undefined)
endif()
