add_executable(wgcli main.cpp)
target_link_libraries(wgcli PRIVATE worldgen_core::worldgen_core wg_build_flags)

install(TARGETS wgcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
