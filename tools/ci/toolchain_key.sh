#!/usr/bin/env bash
# Prints the golden-hash toolchain key <os>-<arch>-<compiler>-<major version> (plan section 6.8)
# for a configured CMake build directory, e.g. linux-x64-gnu-14 or windows-x64-msvc-19.44.
# MSVC keeps the minor version: Visual Studio releases share major version 19.
# Usage: tools/ci/toolchain_key.sh build/release
set -euo pipefail

build_dir=${1:?usage: toolchain_key.sh <build dir>}
compiler_file=$(ls "$build_dir"/CMakeFiles/*/CMakeCXXCompiler.cmake | head -n 1)

id=$(tr -d '\r' < "$compiler_file" | sed -n 's/^set(CMAKE_CXX_COMPILER_ID "\(.*\)")$/\1/p')
version=$(tr -d '\r' < "$compiler_file" | sed -n 's/^set(CMAKE_CXX_COMPILER_VERSION "\(.*\)")$/\1/p')

case "$id" in
  MSVC) major=$(echo "$version" | cut -d. -f1-2) ;;
  *) major=$(echo "$version" | cut -d. -f1) ;;
esac

os=$(echo "${RUNNER_OS:-$(uname -s)}" | tr '[:upper:]' '[:lower:]')
arch=$(echo "${RUNNER_ARCH:-$(uname -m)}" | tr '[:upper:]' '[:lower:]')

echo "${os}-${arch}-$(echo "$id" | tr '[:upper:]' '[:lower:]')-${major}"
