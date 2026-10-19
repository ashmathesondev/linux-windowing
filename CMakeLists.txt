cmake_minimum_required(VERSION 3.20)
project(linux_windowing VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # CLion and clangd pick this up

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(WW_ENABLE_SANITIZERS "Build with ASan + UBSan (Debug only)" OFF)
option(WW_BUILD_X11 "Build the X11 stages and backend" ON)
option(WW_BUILD_WAYLAND "Build the Wayland stages and backend" ON)
option(WW_BUILD_IMGUI "Build the Dear ImGui stages (downloads ImGui with FetchContent)" ON)
option(WW_BUILD_GL "Build the OpenGL stage (needs EGL; part of the ImGui stages)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(WindowingDefaults)

find_package(PkgConfig REQUIRED)

# Shared by everything that opens a window
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)

if(WW_BUILD_X11)
  pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
  pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)          # MIT-SHM
  pkg_check_modules(X11_XCB REQUIRED IMPORTED_TARGET x11-xcb)    # Xlib <-> XCB bridge for xkb-x11
  pkg_check_modules(XKBCOMMON_X11 REQUIRED IMPORTED_TARGET xkbcommon-x11)
  pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
endif()

if(WW_BUILD_WAYLAND)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  include(WaylandProtocols)   # defines the `ww_wayland_protocols` library
endif()

if(WW_BUILD_IMGUI)
  include(DearImGui)
  if(WW_BUILD_GL)
    pkg_check_modules(EGL IMPORTED_TARGET egl)
    if(NOT EGL_FOUND)
      message(STATUS "EGL not found (libegl-dev): skipping the OpenGL stage")
      set(WW_BUILD_GL OFF)
    endif()
  endif()
endif()

add_subdirectory(lib)

enable_testing()
add_subdirectory(tests)

add_subdirectory(p00_env_check)
if(WW_BUILD_X11)
  add_subdirectory(p01_x11_window)
  add_subdirectory(p02_x11_events)
  add_subdirectory(p03_x11_pixels)
endif()
if(WW_BUILD_WAYLAND)
  add_subdirectory(p04_wayland_window)
  add_subdirectory(p05_wayland_input)
endif()
add_subdirectory(p06_platform_layer)
add_subdirectory(p07_renderer)
add_subdirectory(p08_widgets)
add_subdirectory(p09_polish)
add_subdirectory(p10_controls)
if(WW_BUILD_IMGUI)
  add_subdirectory(p11_imgui_soft)
  if(WW_BUILD_GL)
    add_subdirectory(p12_imgui_gl)
  endif()
endif()
