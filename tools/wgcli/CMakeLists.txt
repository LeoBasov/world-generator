# wgcli (A10): the commands live in the static library wgcli_app, so wg_tests runs them
# in-process; the executable only forwards argv.
add_library(
  wgcli_app STATIC
  args.cpp
  cli.cpp
  cmd_bench.cpp
  cmd_compare.cpp
  cmd_export.cpp
  cmd_generate.cpp
  cmd_hash.cpp
  cmd_inspect.cpp
  cmd_sweep.cpp
  cmd_validate.cpp
  common.cpp
  field_values.cpp
  image.cpp
  mock_profile.cpp
  world_hash.cpp)
target_include_directories(wgcli_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# nlohmann/json writes the JSON outputs; lodepng (compiled into worldgen_core) the review PNGs.
target_link_libraries(wgcli_app PUBLIC worldgen_core::worldgen_core
                      PRIVATE nlohmann_json::nlohmann_json wg_lodepng wg_build_flags)
# Default --data-dir: the source tree's data/.
target_compile_definitions(wgcli_app PRIVATE WGCLI_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

add_executable(wgcli main.cpp)
target_link_libraries(wgcli PRIVATE wgcli_app wg_build_flags)

install(TARGETS wgcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
