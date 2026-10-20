# Warning and floating-point flags for all worldgen targets (plan sections 6.3 and 10).
# Linked privately by every wg target; third-party code is not affected.

add_library(wg_build_flags INTERFACE)

if(MSVC)
  # /fp:precise without /fp:contract keeps results reproducible across machines.
  target_compile_options(wg_build_flags INTERFACE /W4 /WX /permissive- /fp:precise /utf-8)
else()
  # GCC contracts to FMA by default in GNU mode; -ffp-contract=off keeps golden hashes stable.
  # Never add -ffast-math.
  target_compile_options(wg_build_flags INTERFACE -Wall -Wextra -Wpedantic -Werror
                                                  -ffp-contract=off)
endif()

if(WG_NATIVE_ARCH)
  if(MSVC)
    message(WARNING "WG_NATIVE_ARCH has no effect with MSVC")
  else()
    target_compile_options(wg_build_flags INTERFACE -march=native)
  endif()
endif()
