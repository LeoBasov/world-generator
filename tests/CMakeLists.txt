# One doctest runner for all unit tests (plan section 6.2).
# Test files mirror src/ under tests/unit/.
file(GLOB_RECURSE WG_UNIT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")

add_executable(wg_tests main.cpp ${WG_UNIT_TEST_SOURCES})
target_link_libraries(wg_tests PRIVATE worldgen_core::worldgen_core doctest::doctest wg_build_flags)
# Shared test helpers: #include "support/<name>.hpp". Tests of internal headers include them
# from src/ ("io/json.hpp"), which need nlohmann/json.
target_include_directories(wg_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(wg_tests PRIVATE nlohmann_json::nlohmann_json)
# DOCTEST_CONFIG_USE_STD_HEADERS: include <ostream> instead of doctest's forward declarations;
# MSVC needs the complete std::ostream to stringify std types such as std::string_view.
target_compile_definitions(
  wg_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS
                   WG_TEST_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                   WG_TEST_VERSION_MINOR=${PROJECT_VERSION_MINOR}
                   WG_TEST_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                   WG_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Register every TEST_CASE as its own CTest test, so `ctest -R <name>` runs one.
include(doctest)
doctest_discover_tests(wg_tests)
