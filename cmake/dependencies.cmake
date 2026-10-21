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
