# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This repository is `worldgen-core`, Phase 1 of the world generator: an engine-independent C++20 library (`worldgen_core`) plus the `wgcli` tool. The specification is the project plan doc "World Generator – Implementation Project Plan" (https://claude.ai/artifact/QRYJWfqWmhjwVwjr3fb97t). Work is organised as work packages A01–A32. Read the plan section for a package before implementing it. Its acceptance tests define "done".

Current state: A01 to A06 done. A01 is the folder tree, CMake, a placeholder `wg::core::version()`, a placeholder `wgcli`, the doctest runner, the nanobench runner `wg_bench`, the example `wg_example` and the three workflows. A02 is the field registry `schema/field_registry.json` (with its JSON Schema), the generator `tools/codegen/gen_field_ids.py` and the committed header `include/wg/generated/field_ids.hpp` (`wg::core::FieldId`, `kFields`, `field_info`, `field_id_from_name`, `field_t<F>`). A03 is the core data model in `wg::core`: `Status`/`Expected` (tl::expected), `WG_CHECK`/`WG_ASSERT`, `Grid` with D8 neighbours, `FieldView`, `MemoryResource`, `FieldStore`, `WorldState` (typed read/write, zero-copy `adopt`, `release`), `VectorLayers`, `Provenance` and the scratch `Arena`. A04 is the RNG in `wg::core` (`rng.hpp`: counter hash, `stage_key`, `uniform01`, `RngStream`; `detmath.hpp`: platform-independent `det_exp`/`det_log`; `distributions.hpp`: normal, log-normal, Poisson, `WeightedBins`) with its frozen layout in ADR 0002 and a bit-exact Python reference in `tools/rng/`. A05 is parallel execution in `wg::core`:
  - `executor.hpp` has the `Executor` interface, the fixed chunking `chunk_count`/`chunk_at`, `SerialExecutor`, and `parallel_for`/`parallel_reduce`, which cancel per chunk and reduce in order.
  - `thread_pool.hpp` is the internal pool. The caller is worker 0, and nested calls run inline.
  - Also `function_ref.hpp`, `cancel.hpp` (`CancelFlag`) and `sinks.hpp` (`LogSink`, `ProgressSink` and null sinks).
  - ADR 0003 records the chunking rule. Shared test helpers live in `tests/support/` (`fixtures.hpp` has grids, ramps, random masks and white noise). Empty module folders hold `.gitkeep` files.

A06 is the numerics library in `wg::num` (`include/wg/numerics/`, `src/numerics/`): neighbourhoods, `IndexHeap`, central differences and D8 slope, masked Laplacian and smoothing, BFS distance and exact EDT, union–find labelling, empirical CDF and quantile mapping, histograms, FFT, radial power spectrum and variogram. ADR 0004 records the kernel conventions (zero-flux edges, tie rules, 16-row grain). The FFT and spectrum are diagnostics only and not bit-identical across platforms. Internal kernel helpers are in `src/numerics/common.hpp`.

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

Generated code: never edit `include/wg/generated/` by hand. Change `schema/field_registry.json`, then regenerate and commit both (needs `pip install -r tools/codegen/requirements.txt`; CMake also offers the target `wg_codegen`). Field ids are append-only: new fields go at the end and an id is never reused.

```bash
python tools/codegen/gen_field_ids.py           # regenerate; --check only compares
python -m unittest discover -s tools/codegen/tests -v
```

RNG known-answer vectors: `tests/unit/core/rng_kat.inc` is generated from the Python reference `tools/rng/rng_reference.py`, which must mirror `rng.hpp`, `detmath.cpp` and `distributions.cpp` operation by operation (standard library only). After changing either side, regenerate and commit:

```bash
python tools/rng/gen_rng_kat.py                 # --check only compares
python -m unittest discover -s tools/rng/tests -v
```

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
- Third-party dependencies come only through FetchContent at pinned tags in `cmake/dependencies.cmake` (currently doctest, nanobench and tl::expected).
- Large buffers come from an injected `wg::core::MemoryResource` (not `std::pmr`, which Apple's libc++ only has from macOS 14). Factories (`WorldState::create`, `Arena::create`) return `Expected` instead of throwing constructors.
- No logging to stdout/stderr from the library. Use the injected sinks (from A05 on).

## CI

All workflows are triggered only by `git push` (no `pull_request`, schedule or manual triggers). Shared setup (Ninja, MSVC env, ccache/sccache, FetchContent cache) lives in `.github/actions/setup-build`.
- `ci.yml`, every push to any branch: `lint` (clang-format, generated-file check, codegen tests), release build and test on ubuntu-24.04 (GCC 14, Clang 18), windows-2025-vs2026 (MSVC) and macos-15 (Apple Clang), ASan+UBSan, llvm-cov coverage with an 80 % line gate, clang-tidy on `.cpp` files changed against `main`. These are the required checks in `.github/rulesets/main.json`; renaming a job means updating the ruleset.
- `ci.yml`, pushes to `main` only: TSan, every preset on the primary plus extended matrix (windows-2022, ubuntu-24.04-arm, macos-26), `wg_bench` run.
- `release.yml`, tags `v*.*.*`: packages, Doxygen (`docs/Doxyfile`) to Pages, GitHub Release. The tag must match the CMake project version and have a `CHANGELOG.md` section.
- `golden-update.yml`, branches `golden/**`: per-toolchain hash files (`tools/ci/toolchain_key.sh`) as artifacts. The hash step activates once `tests/golden/seeds.json` exists (A10).

Runner labels and action SHAs are pinned. Keep them pinned, because golden hashes depend on compiler versions. Update pins by hand (no Dependabot); resolve SHAs with `git ls-remote git@github.com:<owner>/<repo>.git`.
