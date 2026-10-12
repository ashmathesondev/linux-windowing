# Common compiler settings for every target in this repository.
# Works with GCC and Clang. Call ww_target_defaults(<target>) after add_executable/add_library.

function(ww_target_defaults target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion>
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>)

  if(WW_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
