cmake_minimum_required(VERSION 3.25)

project(
  worldgen_core
  VERSION 0.1.0
  DESCRIPTION "Engine-independent world generation library"
  LANGUAGES CXX C)  # C for the zstd and miniz sources

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Embed MSVC debug info in the objects (/Z7) so sccache can cache them in CI (CMP0141).
if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

# Options (plan section 6.2).
option(WG_BUILD_TESTS "Build the doctest runner wg_tests" ${PROJECT_IS_TOP_LEVEL})
option(WG_BUILD_CLI "Build the command-line tool wgcli" ON)
option(WG_BUILD_BENCH "Build the nanobench benchmarks wg_bench" ${PROJECT_IS_TOP_LEVEL})
option(WG_BUILD_EXAMPLES "Build the example program wg_example" ${PROJECT_IS_TOP_LEVEL})
option(WG_WITH_EXR "Enable EXR export through tinyexr (from A09)" OFF)
option(WG_SHARED "Build worldgen_core as a shared library" OFF)
option(WG_NATIVE_ARCH "Optimise for the build machine (never for golden-hash jobs)" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(dependencies)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
if(WG_SHARED)
  set(WG_LIBRARY_TYPE SHARED)
else()
  set(WG_LIBRARY_TYPE STATIC)
endif()

add_library(
  worldgen_core ${WG_LIBRARY_TYPE}
  src/climate/latitude.cpp
  src/climate/temperature.cpp
  src/core/arena.cpp
  src/core/assert.cpp
  src/core/detmath.cpp
  src/core/distributions.cpp
  src/core/executor.cpp
  src/core/field_store.cpp
  src/core/grid.cpp
  src/core/memory.cpp
  src/core/sinks.cpp
  src/core/status.cpp
  src/core/thread_pool.cpp
  src/core/version.cpp
  src/core/world_state.cpp
  src/fractal/fractal.cpp
  src/fractal/peaks.cpp
  src/hydro/discharge.cpp
  src/hydro/erosion.cpp
  src/hydro/lake_bulge.cpp
  src/hydro/lakes.cpp
  src/hydro/priority_flood.cpp
  src/hydro/river_network.cpp
  src/hydro/routing.cpp
  src/hydro/small_ocean.cpp
  src/hydro/test_rivers.cpp
  src/hydro/thermal.cpp
  src/hydro/water.cpp
  src/io/byte_source.cpp
  src/io/export.cpp
  src/io/hash.cpp
  src/io/json.cpp
  src/io/layers_json.cpp
  src/io/snapshot.cpp
  src/io/world_file.cpp
  src/numerics/differences.cpp
  src/numerics/distance.cpp
  src/numerics/fft.cpp
  src/numerics/histogram.cpp
  src/numerics/labeling.cpp
  src/numerics/laplacian.cpp
  src/numerics/quantile.cpp
  src/numerics/spectrum.cpp
  src/numerics/variogram.cpp
  src/params/hash.cpp
  src/params/load.cpp
  src/params/params.cpp
  src/params/reader.cpp
  src/params/tables.cpp
  src/params/validation.cpp
  src/pipeline/field_set.cpp
  src/pipeline/pipeline.cpp
  src/pipeline/profile.cpp
  src/pipeline/registry.cpp
  src/pipeline/stage.cpp
  src/stages/classic/c01_allocate.cpp
  src/stages/classic/c02_seed.cpp
  src/stages/classic/c03_fit.cpp
  src/stages/classic/c05_shape.cpp
  src/stages/classic/c06_rivers.cpp
  src/stages/classic/c07_smooth.cpp
  src/stages/classic/classic.cpp
  src/terrain/bumps.cpp
  src/terrain/edge_ocean.cpp
  src/terrain/hypsometry.cpp
  src/terrain/plains.cpp
  src/terrain/river_descent.cpp
  src/terrain/volcanoes.cpp)
add_library(worldgen_core::worldgen_core ALIAS worldgen_core)

target_include_directories(
  worldgen_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                       $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(worldgen_core PUBLIC cxx_std_20)
# Installed consumers find tl/expected.hpp in the same include directory (installed by tl-expected).
target_link_libraries(worldgen_core PUBLIC $<BUILD_INTERFACE:tl::expected>)
# Internal headers (src/io/json.hpp, ...) are included as "io/json.hpp"; never installed.
target_include_directories(worldgen_core PRIVATE ${PROJECT_SOURCE_DIR}/src)
# nlohmann/json, xxHash, zstd, lodepng and tinyexr are implementation details: no public header
# includes them. The compiled ones are object libraries whose objects become part of the archive.
target_link_libraries(
  worldgen_core PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json> $<BUILD_INTERFACE:wg_xxhash>
                        $<BUILD_INTERFACE:wg_zstd> $<BUILD_INTERFACE:wg_lodepng>)
target_sources(worldgen_core PRIVATE $<TARGET_OBJECTS:wg_zstd_objects>
                                     $<TARGET_OBJECTS:wg_lodepng_objects>)
if(WG_WITH_EXR)
  target_link_libraries(worldgen_core PRIVATE $<BUILD_INTERFACE:wg_tinyexr>)
  target_sources(worldgen_core PRIVATE $<TARGET_OBJECTS:wg_tinyexr_objects>)
endif()
# ThreadPool (A05) uses std::thread; consumers of the static library need the thread library too.
target_link_libraries(worldgen_core PUBLIC Threads::Threads)
target_compile_definitions(
  worldgen_core
  PRIVATE WG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
          WG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
          WG_VERSION_PATCH=${PROJECT_VERSION_PATCH}
          WG_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(worldgen_core PRIVATE $<BUILD_INTERFACE:wg_build_flags>)
set_target_properties(worldgen_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ---------------------------------------------------------------------------
# Tools, tests, benchmarks and examples
# ---------------------------------------------------------------------------
if(WG_BUILD_TESTS OR WG_BUILD_EXAMPLES)
  enable_testing()
endif()

if(WG_BUILD_CLI)
  add_subdirectory(tools/wgcli)
endif()

if(WG_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(WG_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(WG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Regenerate committed sources (plan section 4). Not part of ALL: the generated header is
# committed, so a normal build never needs Python. Requires tools/codegen/requirements.txt.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(
    wg_codegen
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/codegen/gen_field_ids.py"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/codegen/gen_params.py"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating include/wg/generated/field_ids.hpp and gen_params.hpp"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Install and CMake package: find_package(worldgen_core)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(
  TARGETS worldgen_core
  EXPORT worldgen_coreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/wg DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY schema/ DESTINATION ${CMAKE_INSTALL_DATADIR}/worldgen_core/schema)
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/worldgen_core/data)
install(
  EXPORT worldgen_coreTargets
  NAMESPACE worldgen_core::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/worldgen_core)

configure_package_config_file(
  cmake/worldgen_coreConfig.cmake.in "${PROJECT_BINARY_DIR}/worldgen_coreConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/worldgen_core)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/worldgen_coreConfigVersion.cmake"
  COMPATIBILITY SameMajorVersion)
install(FILES "${PROJECT_BINARY_DIR}/worldgen_coreConfig.cmake"
              "${PROJECT_BINARY_DIR}/worldgen_coreConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/worldgen_core)
