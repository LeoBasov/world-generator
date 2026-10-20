# Third-party dependencies via FetchContent at pinned tags (plan section 6.1).
# CI cache keys hash this file, so every pin change refreshes the cache.

include(FetchContent)

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
