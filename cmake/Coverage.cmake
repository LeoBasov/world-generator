# WG_COVERAGE: source-based coverage with Clang (llvm-profdata / llvm-cov).
# Only worldgen targets are instrumented, through wg_build_flags.

option(WG_COVERAGE "Instrument worldgen targets for llvm-cov" OFF)

if(WG_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WG_COVERAGE requires Clang")
  endif()
  message(STATUS "worldgen_core: coverage instrumentation enabled")
  target_compile_options(wg_build_flags INTERFACE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(wg_build_flags INTERFACE -fprofile-instr-generate)
endif()
