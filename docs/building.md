# Building worldgen-core

This guide covers building the library `worldgen_core`, the tool `wgcli`, the tests, the
benchmarks and the example on Linux, macOS, Windows and WSL.

## Requirements

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.25 or newer | |
| Build tool | Ninja (presets) or Make | The presets use Ninja; Make works with a manual configure. |
| C++ compiler | GCC 12+, Clang 16+, MSVC 2022+, Apple Clang 15+ | C++20. CI uses GCC 14, Clang 18, MSVC and Apple Clang. |
| git and network access | | Configuring downloads the dependencies (see below). |
| Python 3 | optional | Only to regenerate committed sources and to run the tool tests. |

On Windows, run every command from a Visual Studio developer shell (x64 Native Tools Command
Prompt or Developer PowerShell), so that `cl` and Ninja are on `PATH`.

## Always configure the repository root

Run CMake on the repository root, never on a subfolder such as `tools/wgcli/`. The
subfolders are not standalone projects. They depend on targets that the root
`CMakeLists.txt` defines (`worldgen_core`, `wg_build_flags`, the dependencies). If you
configure a subfolder, the build fails with errors like
`fatal error: wg/core/expected.hpp: No such file or directory`.

## Quick start with presets

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The build output goes to `build/<preset>/`.

| Preset | Build type | Purpose |
|---|---|---|
| `debug` | Debug | Day-to-day development |
| `release` | Release | Optimised build, used for most CI test jobs |
| `relwithdebinfo` | RelWithDebInfo | Profiling |
| `asan` | Debug | AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang only) |
| `tsan` | Debug | ThreadSanitizer (GCC/Clang only) |
| `coverage` | Debug | Source-based coverage with llvm-cov (Clang only) |

To build only one target, pass it with `--target`:

```bash
cmake --build --preset release --target wgcli
```

## Manual configure (Make or other generators)

Without the presets you choose the generator, the build directory and the build type
yourself. For example, with Make:

```bash
cmake -S . -B build/make-release -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
make -C build/make-release -j"$(nproc)"
ctest --test-dir build/make-release --output-on-failure
```

To build only `wgcli` and skip the rest:

```bash
cmake -S . -B build/cli -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DWG_BUILD_TESTS=OFF -DWG_BUILD_BENCH=OFF -DWG_BUILD_EXAMPLES=OFF
make -C build/cli wgcli -j"$(nproc)"
```

## Targets and outputs

| Target | What it is | Location in the build tree |
|---|---|---|
| `worldgen_core` | The library (static by default) | `build/<dir>/` |
| `wgcli` | Command-line tool, see [cli.md](cli.md) | `build/<dir>/tools/wgcli/wgcli` |
| `wg_tests` | doctest runner with all unit and integration tests | `build/<dir>/tests/wg_tests` |
| `wg_bench` | nanobench benchmarks | `build/<dir>/bench/wg_bench` |
| `wg_example` | Minimal example program | `build/<dir>/examples/wg_example` |
| `wg_codegen` | Regenerates the committed headers (not built by default) | |

On Windows the executables have the `.exe` suffix.

## Options

Set options at configure time with `-D<OPTION>=<value>`.

| Option | Default | Effect |
|---|---|---|
| `WG_BUILD_TESTS` | ON when top level | Build `wg_tests` and register the CTest tests |
| `WG_BUILD_CLI` | ON | Build `wgcli` |
| `WG_BUILD_BENCH` | ON when top level | Build `wg_bench` |
| `WG_BUILD_EXAMPLES` | ON when top level | Build `wg_example` |
| `WG_WITH_EXR` | OFF | EXR export through tinyexr |
| `WG_SHARED` | OFF | Build `worldgen_core` as a shared library |
| `WG_NATIVE_ARCH` | OFF | Add `-march=native`. Never use it for golden-hash builds. |
| `WG_SANITIZE` | empty | Sanitizer list, e.g. `address,undefined` or `thread` (set by the presets) |
| `WG_COVERAGE` | OFF | llvm-cov instrumentation, Clang only (set by the `coverage` preset) |

"When top level" means the default is ON when you build this repository directly and OFF
when another project includes it with `add_subdirectory` or FetchContent.

## Compiler flags

Every worldgen target links the interface target `wg_build_flags`
(`cmake/CompilerWarnings.cmake`), which sets:

- warnings as errors: `-Wall -Wextra -Wpedantic -Werror` or `/W4 /WX /permissive-`;
- reproducible floating point: `-ffp-contract=off` or `/fp:precise`.

Never add `-ffast-math`, and never change the floating-point flags. Both change the
generated worlds and break the golden hashes. Because warnings are errors, a newer
compiler than CI's can fail on new warnings. In that case, fix the code rather than
disable the flag.

## Dependencies

`cmake/dependencies.cmake` pulls every third-party library through FetchContent at a pinned
tag: doctest, nanobench, tl::expected, nlohmann/json, xxHash, zstd, lodepng and, with
`WG_WITH_EXR`, tinyexr. Nothing needs to be installed on the system, but the first
configure clones these repositories, so it needs git and network access.

To reuse the downloads across build directories or offline, point FetchContent at an
existing checkout:

```bash
cmake --preset debug -DFETCHCONTENT_BASE_DIR=$HOME/.cache/wg-deps
cmake --preset release -DFETCHCONTENT_BASE_DIR=$HOME/.cache/wg-deps -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
```

## Running tests

Every doctest `TEST_CASE` is its own CTest test:

```bash
ctest --preset debug                       # all tests
ctest --preset debug -R "core.version"     # tests matching a regex
build/debug/tests/wg_tests -tc="core.version*"   # the runner directly
```

The Python tools have their own tests. They need only the standard library, except the
data validation, which needs `jsonschema` (`pip install -r tools/codegen/requirements.txt`):

```bash
python -m unittest discover -s tools/codegen/tests -v
python -m unittest discover -s tools/rng/tests -v
python tools/data/validate_data.py
```

## Generated sources

The headers in `include/wg/generated/` and `tests/unit/core/rng_kat.inc` are committed, so
a normal build never runs Python. After changing their sources, regenerate and commit
both:

| Source | Command | Output |
|---|---|---|
| `schema/field_registry.json` | `python tools/codegen/gen_field_ids.py` | `include/wg/generated/field_ids.hpp` |
| `schema/params.schema.json` | `python tools/codegen/gen_params.py` | `include/wg/generated/gen_params.hpp` |
| `tools/rng/rng_reference.py` | `python tools/rng/gen_rng_kat.py` | `tests/unit/core/rng_kat.inc` |
| `wgcli` help texts | `python tools/wgcli/sync_cli_doc.py --wgcli <path to wgcli>` | `docs/cli.md` |

Each generator accepts `--check`, which only compares. When CMake finds Python, the
target `wg_codegen` runs the first two.

## Formatting and static analysis

CI checks formatting with clang-format 18 and runs clang-tidy on the changed `.cpp` files:

```bash
clang-format --dry-run --Werror <files>
clang-tidy -p build/debug <files>
```

`CMAKE_EXPORT_COMPILE_COMMANDS` is on, so every build directory has the
`compile_commands.json` that clang-tidy and editors use.

## Installing and consuming the library

```bash
cmake --install build/release --prefix /opt/worldgen
```

This installs the library, the public headers, `schema/` and `data/` (under
`share/worldgen_core/`), `wgcli` and a CMake package. A consumer then writes:

```cmake
find_package(worldgen_core 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE worldgen_core::worldgen_core)
```

and configures with `-DCMAKE_PREFIX_PATH=/opt/worldgen`.

## Platform notes

### Windows

- Use a Visual Studio developer shell. Without it CMake cannot find `cl`, and Ninja may be
  missing. Visual Studio ships Ninja as part of its CMake component.
- Debug information is embedded in the object files (`/Z7`), so compiler caches like
  sccache work.

### WSL

- Use a compiler that is new enough: Ubuntu 22.04 ships GCC 11, which is too old. Install
  `g++-13` or newer (or Clang 16+) and select it with
  `-DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_C_COMPILER=gcc-13`.
- Building on the Windows file system (`/mnt/c/...`) works, but it is several times slower
  than on the Linux file system. For regular work, clone the repository into the WSL home
  directory.
- Do not share a build directory between Windows and WSL. Use a separate directory for each
  (e.g. `build/wsl-debug`).

### macOS

- Apple Clang 15 or newer (Xcode 15+). Install CMake and Ninja with Homebrew.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `wg/core/...: No such file or directory` while compiling `wgcli` | You configured `tools/wgcli/` instead of the repository root. Delete the build directory and configure the root. |
| `CMake 3.25 or higher is required` | Install a newer CMake (e.g. from cmake.org or `pip install cmake`). |
| Errors about C++20 features (`<span>`, `<concepts>`, designated initialisers) | The compiler is too old. See the requirements above. |
| Configure hangs or fails in `FetchContent` | No network or git missing. Install git, or reuse a dependency cache with `FETCHCONTENT_BASE_DIR`. |
| `Could not find Ninja` / `CMAKE_MAKE_PROGRAM is not set` | Install Ninja, or configure manually with `-G "Unix Makefiles"`. |
| `WG_SANITIZE is only supported with GCC and Clang` | The `asan` and `tsan` presets do not work with MSVC. |
| A build that worked on CI fails locally with a warning-as-error | Your compiler warns about more things. Fix the warning in the code. |
| `wgcli.docs` test fails | A `--help` text changed. Run `tools/wgcli/sync_cli_doc.py` (see Generated sources). |
