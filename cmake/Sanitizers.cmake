# WG_SANITIZE: comma-separated sanitizer list, e.g. "address,undefined" or "thread".
# Applied globally so dependencies are instrumented too (required for TSan).

set(WG_SANITIZE "" CACHE STRING "Sanitizers to enable (address,undefined | thread)")

if(WG_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "WG_SANITIZE is only supported with GCC and Clang")
  endif()
  message(STATUS "worldgen_core: sanitizers enabled: ${WG_SANITIZE}")
  add_compile_options(-fsanitize=${WG_SANITIZE} -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${WG_SANITIZE})
endif()
