# nanobench benchmarks per kernel and per stage (plan sections 6.2 and 10, level 6).
add_executable(wg_bench main.cpp)
target_link_libraries(wg_bench PRIVATE worldgen_core::worldgen_core nanobench::nanobench
                                       wg_build_flags)
# The classic-profile benchmark loads the profile and the data tables of the source tree.
target_compile_definitions(wg_bench PRIVATE WG_BENCH_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
