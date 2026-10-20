# One doctest runner for all unit tests (plan section 6.2).
# Test files mirror src/ under tests/unit/.
file(GLOB_RECURSE WG_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")

add_executable(wg_tests main.cpp ${WG_UNIT_TEST_SOURCES})
target_link_libraries(wg_tests PRIVATE worldgen_core::worldgen_core doctest::doctest wg_build_flags)
target_compile_definitions(
  wg_tests PRIVATE WG_TEST_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                   WG_TEST_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                   WG_TEST_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# Register every TEST_CASE as its own CTest test, so `ctest -R <name>` runs one.
include(doctest)
doctest_discover_tests(wg_tests)
