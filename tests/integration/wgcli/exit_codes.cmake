# Runs the wgcli executable and checks its exit codes (plan section 6.7).
# Usage: cmake -DWGCLI=<path to wgcli> -P exit_codes.cmake

function(expect_exit code)
  execute_process(COMMAND "${WGCLI}" ${ARGN} RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
  if(NOT result EQUAL code)
    message(FATAL_ERROR "wgcli ${ARGN}: exit code ${result}, expected ${code}")
  endif()
endfunction()

expect_exit(0 --version)
expect_exit(0 help)
expect_exit(0 generate --help)
expect_exit(2)
expect_exit(2 bogus)
expect_exit(2 generate --profile mock)
expect_exit(2 inspect no_such_file.wgw)
