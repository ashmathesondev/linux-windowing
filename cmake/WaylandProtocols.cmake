# Generates C glue code for the Wayland protocols we use and bundles it into one
# static library, `ww_wayland_protocols`.
#
# Wayland protocols are described as XML. `wayland-scanner` turns each XML file into:
#   - a header with the request/event declarations (client-header)
#   - a C file with the marshalling tables (private-code)
#
# The XML files are vendored in protocols/ so the build does not depend on the
# wayland-protocols package version installed on your machine.

set(WW_PROTOCOL_DIR "${CMAKE_SOURCE_DIR}/protocols")
set(WW_PROTOCOL_GEN_DIR "${CMAKE_BINARY_DIR}/generated/protocols")
file(MAKE_DIRECTORY "${WW_PROTOCOL_GEN_DIR}")

set(WW_PROTOCOL_XMLS
  xdg-shell
  xdg-decoration-unstable-v1)

set(WW_PROTOCOL_SOURCES "")
foreach(proto IN LISTS WW_PROTOCOL_XMLS)
  set(xml "${WW_PROTOCOL_DIR}/${proto}.xml")
  set(hdr "${WW_PROTOCOL_GEN_DIR}/${proto}-client-protocol.h")
  set(src "${WW_PROTOCOL_GEN_DIR}/${proto}-protocol.c")

  add_custom_command(
    OUTPUT "${hdr}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${hdr}"
    DEPENDS "${xml}"
    VERBATIM)
  add_custom_command(
    OUTPUT "${src}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${src}"
    DEPENDS "${xml}"
    VERBATIM)

  list(APPEND WW_PROTOCOL_SOURCES "${hdr}" "${src}")
endforeach()

add_library(ww_wayland_protocols STATIC ${WW_PROTOCOL_SOURCES})
target_include_directories(ww_wayland_protocols PUBLIC "${WW_PROTOCOL_GEN_DIR}")
target_link_libraries(ww_wayland_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
set_target_properties(ww_wayland_protocols PROPERTIES POSITION_INDEPENDENT_CODE ON)
