# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This repository is `worldgen-core`, Phase 1 of the world generator: an engine-independent C++20 library (`worldgen_core`) plus the `wgcli` tool. The specification is the project plan doc "World Generator – Implementation Project Plan" (https://claude.ai/artifact/QRYJWfqWmhjwVwjr3fb97t). Work is organised as work packages A01–A32. Read the plan section for a package before implementing it. Its acceptance tests define "done".

Current state: A01 done. That is the folder tree, CMake, a placeholder `wg::core::version()`, a placeholder `wgcli`, the doctest runner, the nanobench runner `wg_bench`, the example `wg_example` and the three workflows. Empty module folders hold `.gitkeep` files.

## Build and test

CMake ≥ 3.25 with Ninja, through presets: `debug`, `release`, `relwithdebinfo`, `asan` (ASan+UBSan), `tsan`, `coverage` (Clang only). Build output goes to `build/<preset>/`. On Windows, run from a VS Developer shell so `cl` and Ninja are on PATH.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Run a single test, either through CTest (each doctest TEST_CASE is its own CTest test) or by calling the doctest runner directly:

```bash
ctest --preset debug -R "core.version"
build/debug/tests/wg_tests -tc="core.version*"
```

Format check (CI uses clang-format 18): `clang-format --dry-run --Werror <files>`.

## Architecture and conventions (from the plan)

- Public headers live in `include/wg/<module>/` and implementations in `src/<module>/`, one folder per module. The namespace is `wg::<module>`. Classic and physical pipeline stages go in `src/stages/{classic,physical}/` as `c01_<name>.cpp` / `p01_<name>.cpp`. A stage id equals the file stem and never changes, because it keys the RNG.
- Every source file needs a unit test at the mirrored path under `tests/unit/`. `tests/CMakeLists.txt` globs `tests/unit/**/*.cpp` into the single `wg_tests` executable.
- Stages talk only through named fields in a `WorldState`. Numerical work goes in stateless kernels: input spans, caller-owned output spans, and one params struct.
- Public functions are `noexcept` and return `Status`/`Expected`. No exception may cross the API.
- Determinism (requirement R1):
  - Use the counter-based hash RNG only. No `std::` distributions and no shared stateful RNG.
  - Sorts and heaps break ties by cell index.
  - Reductions use fixed chunk boundaries.
- Floating point: keep `-ffp-contract=off` / `/fp:precise`. Never use `-ffast-math`. Store float32, accumulate in float64.
- Warnings are errors: `-Wall -Wextra -Wpedantic -Werror` and `/W4 /WX`. They are set on the INTERFACE target `wg_build_flags` (`cmake/CompilerWarnings.cmake`), which every wg target links privately.
- Third-party dependencies come only through FetchContent at pinned tags in `cmake/dependencies.cmake` (currently doctest).
- No logging to stdout/stderr from the library. Use the injected sinks (from A05 on).

## CI

All workflows are triggered only by `git push` (no `pull_request`, schedule or manual triggers). Shared setup (Ninja, MSVC env, ccache/sccache, FetchContent cache) lives in `.github/actions/setup-build`.
- `ci.yml`, every push to any branch: clang-format, release build and test on ubuntu-24.04 (GCC 14, Clang 18), windows-2025-vs2026 (MSVC) and macos-15 (Apple Clang), ASan+UBSan, llvm-cov coverage with an 80 % line gate, clang-tidy on `.cpp` files changed against `main`. These are the required checks in `.github/rulesets/main.json`; renaming a job means updating the ruleset.
- `ci.yml`, pushes to `main` only: TSan, every preset on the primary plus extended matrix (windows-2022, ubuntu-24.04-arm, macos-26), `wg_bench` run.
- `release.yml`, tags `v*.*.*`: packages, Doxygen (`docs/Doxyfile`) to Pages, GitHub Release. The tag must match the CMake project version and have a `CHANGELOG.md` section.
- `golden-update.yml`, branches `golden/**`: per-toolchain hash files (`tools/ci/toolchain_key.sh`) as artifacts. The hash step activates once `tests/golden/seeds.json` exists (A10).

Runner labels and action SHAs are pinned. Keep them pinned, because golden hashes depend on compiler versions. Update pins by hand (no Dependabot); resolve SHAs with `git ls-remote git@github.com:<owner>/<repo>.git`.
