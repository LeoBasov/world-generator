# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This repository is `worldgen-core`, Phase 1 of the world generator: an engine-independent C++20 library (`worldgen_core`) plus the `wgcli` tool. The specification is the project plan doc "World Generator – Implementation Project Plan" (https://claude.ai/artifact/QRYJWfqWmhjwVwjr3fb97t). Work is organised as work packages A01–A32. Read the plan section for a package before implementing it. Its acceptance tests define "done".

Current state: A01 to A09 done. A01 is the folder tree, CMake, a placeholder `wg::core::version()`, a placeholder `wgcli`, the doctest runner, the nanobench runner `wg_bench`, the example `wg_example` and the three workflows. A02 is the field registry `schema/field_registry.json` (with its JSON Schema), the generator `tools/codegen/gen_field_ids.py` and the committed header `include/wg/generated/field_ids.hpp` (`wg::core::FieldId`, `kFields`, `field_info`, `field_id_from_name`, `field_t<F>`). A03 is the core data model in `wg::core`: `Status`/`Expected` (tl::expected), `WG_CHECK`/`WG_ASSERT`, `Grid` with D8 neighbours, `FieldView`, `MemoryResource`, `FieldStore`, `WorldState` (typed read/write, zero-copy `adopt`, `release`), `VectorLayers`, `Provenance` and the scratch `Arena`. A04 is the RNG in `wg::core` (`rng.hpp`: counter hash, `stage_key`, `uniform01`, `RngStream`; `detmath.hpp`: platform-independent `det_exp`/`det_log`; `distributions.hpp`: normal, log-normal, Poisson, `WeightedBins`) with its frozen layout in ADR 0002 and a bit-exact Python reference in `tools/rng/`. A05 is parallel execution in `wg::core`:
  - `executor.hpp` has the `Executor` interface, the fixed chunking `chunk_count`/`chunk_at`, `SerialExecutor`, and `parallel_for`/`parallel_reduce`, which cancel per chunk and reduce in order.
  - `thread_pool.hpp` is the internal pool. The caller is worker 0, and nested calls run inline.
  - Also `function_ref.hpp`, `cancel.hpp` (`CancelFlag`) and `sinks.hpp` (`LogSink`, `ProgressSink` and null sinks).
  - ADR 0003 records the chunking rule. Shared test helpers live in `tests/support/` (`fixtures.hpp` has grids, ramps, random masks and white noise). Empty module folders hold `.gitkeep` files.

A06 is the numerics library in `wg::num` (`include/wg/numerics/`, `src/numerics/`): neighbourhoods, `IndexHeap`, central differences and D8 slope, masked Laplacian and smoothing, BFS distance and exact EDT, union–find labelling, empirical CDF and quantile mapping, histograms, FFT, radial power spectrum and variogram. ADR 0004 records the kernel conventions (zero-flux edges, tie rules, 16-row grain). The FFT and spectrum are diagnostics only and not bit-identical across platforms. Internal kernel helpers are in `src/numerics/common.hpp`.

A07 is parameters and data tables in `wg::params` (`include/wg/params/`, `src/params/`):
  - `schema/params.schema.json` is the source of the parameter model. `tools/codegen/gen_params.py` generates `include/wg/generated/gen_params.hpp` (`GenParams` with the schema defaults, `kParamDescriptors`, `find_param`); `get_param`/`set_param` access a parameter through its descriptor.
  - `load.hpp` (`read_params`, `load_params`, `load_params_file`, `to_json`; files list only what differs, issues carry JSON paths), `validation.hpp` (`validate`, errors and warnings), `hash.hpp` (`params_hash`, XXH3-64 over values; seed and profile excluded).
  - `tables.hpp`: biome, climate, salinity, rock, soil and mineral tables from `data/tables/` and `data/geology/` with per-table and cross-table rules and `tables_hash`. Presets are in `data/presets/`. Every data file names its JSON Schema in `schema/` via `$schema`.
  - JSON parsing is confined to the internal `src/io/json.{hpp,cpp}` (nlohmann/json, exceptions caught there); `wg::io::xxh3_64` wraps xxHash. Internal headers are included from `src/` (`"io/json.hpp"`), also by tests. ADR 0005 records the model and the hash.

A08 is the pipeline framework in `wg::pipeline` (`include/wg/pipeline/`, `src/pipeline/`):
  - `stage.hpp`: `Stage` (`id`, `inputs`, `outputs`, `run(StageContext&)`, repair hook `repair`), `StageResult` (ok, repaired, reject, cancelled, error) and `StageMetrics`. `field_set.hpp`: `FieldSet` over `FieldId` and `LayerId` (the vector layers).
  - `registry.hpp`: `StageRegistry` (id to factory, no global instance; `is_valid_id`). `profile.hpp`: `Profile` and `load_profile`/`load_profile_file` (`schema/profile.schema.json`; the mock profile is in `tests/fixtures/profiles/`).
  - `pipeline.hpp`: `Pipeline::create` checks R2 (optionally with `provided` synthetic inputs). `Pipeline::run` is the attempt loop: a fresh state per attempt, key `stage_key(seed, id, a)`, `RejectionRecord`s, cancel polled before each stage, `SnapshotHook` after each completed stage, `StageRecord` metrics. It returns `RunOutput` or a `FailureReport`.
  - Tests script stages with `tests/support/mock_stages.hpp` (`MockScript`, `MockStage`, `add_mock`). ADR 0006 records the design.

A09 is the world file and export in `wg::io` (`include/wg/io/`, `src/io/`); `docs/file_format.md` specifies `.wgw`, ADR 0007 records the decisions:
  - `world_file.hpp`: `write_world` / `write_world_file` (params hash must match the provenance; `<path>.tmp` then rename), `WorldReader` (`open`, `open_file`, `open_buffer`; reads header, table and metadata, then one chunk per `read_field`; `read_into`), `read_world*`. Chunks are zstd (raw if not smaller) with XXH3 of the raw bytes; the table is at the end; metadata is the `$metadata` chunk (provenance, full params, layers as JSON).
  - `byte_source.hpp` (`ByteSource`, `MemorySource`, `FileSource`), `snapshot.hpp` (`SnapshotWriter` with `SnapshotPolicy`), `export.hpp` (PNG16, EXR with `WG_WITH_EXR`, layers JSON).
  - Internal: `src/io/binary.hpp` (little-endian packing), `src/io/layers_json.{hpp,cpp}` (layers to and from JSON, reused by export). Tests use `tests/support/sample_world.hpp` and `temp_dir.hpp`.

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

Parameters work the same way: change `schema/params.schema.json`, then run `python tools/codegen/gen_params.py` and commit the header (it needs only the standard library). Adding or renaming a parameter changes every parameter hash (ADR 0005). Data files are checked against their schemas with `python tools/data/validate_data.py` (needs jsonschema); the C++ loaders check the rules JSON Schema cannot express.

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
- Third-party dependencies come only through FetchContent at pinned tags in `cmake/dependencies.cmake` (currently doctest, nanobench, tl::expected, nlohmann/json, xxHash, zstd, lodepng and optionally tinyexr; all but the first three are private to the library, and zstd, lodepng and tinyexr are compiled from their sources as object libraries linked into `worldgen_core`).
- Large buffers come from an injected `wg::core::MemoryResource` (not `std::pmr`, which Apple's libc++ only has from macOS 14). Factories (`WorldState::create`, `Arena::create`) return `Expected` instead of throwing constructors.
- No logging to stdout/stderr from the library. Use the injected sinks (from A05 on).

## CI

All workflows are triggered only by `git push` (no `pull_request`, schedule or manual triggers). Shared setup (Ninja, MSVC env, ccache/sccache, FetchContent cache) lives in `.github/actions/setup-build`.
- `ci.yml`, every push to any branch: `lint` (clang-format, generated-file checks, codegen tests, data-schema validation), release build and test on ubuntu-24.04 (GCC 14, Clang 18), windows-2025-vs2026 (MSVC) and macos-15 (Apple Clang), ASan+UBSan, llvm-cov coverage with an 80 % line gate, clang-tidy on `.cpp` files changed against `main`. These are the required checks in `.github/rulesets/main.json`; renaming a job means updating the ruleset.
- `ci.yml`, pushes to `main` only: TSan, every preset on the primary plus extended matrix (windows-2022, ubuntu-24.04-arm without asan, macos-26), `wg_bench` run.
- `release.yml`, tags `v*.*.*`: packages, Doxygen (`docs/Doxyfile`) to Pages, GitHub Release. The tag must match the CMake project version and have a `CHANGELOG.md` section.
- `golden-update.yml`, branches `golden/**`: per-toolchain hash files (`tools/ci/toolchain_key.sh`) as artifacts. The hash step activates once `tests/golden/seeds.json` exists (A10).

Runner labels and action SHAs are pinned. Keep them pinned, because golden hashes depend on compiler versions. Update pins by hand (no Dependabot); resolve SHAs with `git ls-remote git@github.com:<owner>/<repo>.git`.
