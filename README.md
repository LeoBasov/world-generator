# worldgen-core

Engine-independent C++20 world generation library (`worldgen_core`) and command-line tool
`wgcli`. Phase 1 of the world generator; Phase 2 binds it to Godot.

## Build and test

Requires CMake 3.25 or newer, Ninja and a C++20 compiler (GCC 12+, Clang 16+, MSVC 2022+,
Apple Clang 15+). On Windows, run from a Visual Studio developer shell.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Presets: `debug`, `release`, `relwithdebinfo`, `asan` (ASan + UBSan), `tsan`, `coverage` (Clang).
Targets: `worldgen_core`, `wgcli`, `wg_tests`, `wg_bench`, `wg_example`.
Install with `cmake --install build/<preset> --prefix <dir>`, then use
`find_package(worldgen_core)`.

See [docs/building.md](docs/building.md) for the full build guide: options, manual configure
with Make, dependencies, generated sources, platform notes (Windows, WSL, macOS) and
troubleshooting.
