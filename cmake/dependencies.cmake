# Third-party dependencies via FetchContent at pinned tags (plan section 6.1).
# CI cache keys hash this file, so every pin change refreshes the cache.

include(FetchContent)

# tl::expected, the Expected<T, Status> of the public API (header-only, so always needed).
# It installs its own header next to ours; worldgen_core links it as a build interface only.
FetchContent_Declare(
  expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected.git
  GIT_TAG v1.3.1
  GIT_SHALLOW TRUE
  SYSTEM)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPECTED_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)  # keeps its CPack setup out of our build
FetchContent_MakeAvailable(expected)  # provides tl::expected

# nlohmann/json for parameters, data tables and metadata (A07). Used only inside the library
# (src/io/json.*, which catches its exceptions); worldgen_core links it as a build interface.
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
  SYSTEM)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_ImplicitConversions OFF CACHE BOOL "" FORCE)  # explicit get<T>() only
FetchContent_MakeAvailable(nlohmann_json)  # provides nlohmann_json::nlohmann_json

# xxHash (XXH3-64) for the parameter hash, golden hashes and chunk checksums. The repository
# root has no CMakeLists.txt, so this only downloads it; the library compiles it inline in one
# translation unit (XXH_INLINE_ALL) through the interface target wg_xxhash.
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(xxhash)
add_library(wg_xxhash INTERFACE)
target_include_directories(wg_xxhash SYSTEM INTERFACE "${xxhash_SOURCE_DIR}")

# zstd for world-file chunks (A09). The repository root has no CMakeLists.txt (its CMake build
# is in build/cmake), so this only downloads it; the library sources are compiled as the object library
# wg_zstd_objects, whose objects go into worldgen_core, so the installed archive needs no extra
# package. Single-threaded, no assembly (portable and identical output on every runner).
FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG v1.5.7
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zstd)
file(GLOB WG_ZSTD_SOURCES CONFIGURE_DEPENDS "${zstd_SOURCE_DIR}/lib/common/*.c"
     "${zstd_SOURCE_DIR}/lib/compress/*.c" "${zstd_SOURCE_DIR}/lib/decompress/*.c")
add_library(wg_zstd_objects OBJECT ${WG_ZSTD_SOURCES})
target_compile_definitions(wg_zstd_objects PRIVATE ZSTD_DISABLE_ASM=1 XXH_NAMESPACE=ZSTD_)
set_target_properties(wg_zstd_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wg_zstd INTERFACE)
target_include_directories(wg_zstd SYSTEM INTERFACE "${zstd_SOURCE_DIR}/lib")

# lodepng for 16-bit PNG export (A09). No tags upstream, so it is pinned by commit; no CMake
# build either, so lodepng.cpp becomes the object library wg_lodepng_objects (as zstd above).
FetchContent_Declare(
  lodepng
  GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
  GIT_TAG e584a8490db1b76b81bf298c11c09d3b55d9c270)
FetchContent_MakeAvailable(lodepng)
add_library(wg_lodepng_objects OBJECT "${lodepng_SOURCE_DIR}/lodepng.cpp")
target_compile_definitions(wg_lodepng_objects PUBLIC LODEPNG_NO_COMPILE_DISK)
set_target_properties(wg_lodepng_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(wg_lodepng INTERFACE)
target_include_directories(wg_lodepng SYSTEM INTERFACE "${lodepng_SOURCE_DIR}")
target_compile_definitions(wg_lodepng INTERFACE LODEPNG_NO_COMPILE_DISK)

# tinyexr for EXR export (A09), only with WG_WITH_EXR. Uses its bundled miniz; the library
# sees WG_HAVE_EXR. Compiled like zstd above.
if(WG_WITH_EXR)
  FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG v1.0.9
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)  # a directory without CMakeLists.txt: download only
  FetchContent_MakeAvailable(tinyexr)
  set(WG_TINYEXR_DEFINITIONS TINYEXR_USE_MINIZ=1 TINYEXR_USE_OPENMP=0 TINYEXR_USE_THREAD=0)
  add_library(wg_tinyexr_objects OBJECT "${tinyexr_SOURCE_DIR}/tinyexr.cc"
                                        "${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c")
  target_include_directories(wg_tinyexr_objects PRIVATE "${tinyexr_SOURCE_DIR}"
                                                        "${tinyexr_SOURCE_DIR}/deps/miniz")
  target_compile_definitions(wg_tinyexr_objects PRIVATE ${WG_TINYEXR_DEFINITIONS})
  set_target_properties(wg_tinyexr_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
  add_library(wg_tinyexr INTERFACE)
  target_include_directories(wg_tinyexr SYSTEM INTERFACE "${tinyexr_SOURCE_DIR}"
                                                         "${tinyexr_SOURCE_DIR}/deps/miniz")
  target_compile_definitions(wg_tinyexr INTERFACE ${WG_TINYEXR_DEFINITIONS} WG_HAVE_EXR)
endif()

if(WG_BUILD_TESTS)
  FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
    SYSTEM)  # keep our -Werror flags away from third-party headers
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
  # Provides doctest_discover_tests().
  list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
endif()

if(WG_BUILD_BENCH)
  FetchContent_Declare(
    nanobench
    GIT_REPOSITORY https://github.com/martinus/nanobench.git
    GIT_TAG v4.6.0
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(nanobench)  # provides nanobench::nanobench
endif()
