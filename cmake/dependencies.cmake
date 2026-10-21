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
