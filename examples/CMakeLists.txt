# Smallest complete use of the public API (plan section 6.2).
add_executable(wg_example minimal.cpp)
target_link_libraries(wg_example PRIVATE worldgen_core::worldgen_core wg_build_flags)

add_test(NAME example.minimal COMMAND wg_example)
