# Changelog

All notable changes to worldgen-core are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Repository layout of the project plan (section 8).
- CMake build with presets (debug, release, relwithdebinfo, asan, tsan, coverage) and
  an installable `worldgen_core` CMake package.
- `wg::core::version()` and a placeholder `wgcli` that prints the library version.
- doctest runner `wg_tests` with CTest registration of every test case.
- nanobench benchmark runner `wg_bench` and example program `wg_example`; options
  `WG_BUILD_BENCH`, `WG_BUILD_EXAMPLES` and `WG_WITH_EXR`.
- GitHub Actions, push-triggered only (plan section 6.8):
  - `ci.yml` on every push: `lint` (clang-format, generated-file check, codegen tests), build
    and test on Linux (GCC, Clang), Windows (MSVC) and macOS (Apple Clang), ASan + UBSan, line coverage with an 80 % gate, and clang-tidy on the
    files changed against `main`. On pushes to `main` also TSan, every preset on the primary and
    extended runner matrix, and a benchmark run.
  - `release.yml` on `v*.*.*` tags: packages per platform, Doxygen docs on GitHub Pages and a
    GitHub Release with checksums and the CHANGELOG section.
  - `golden-update.yml` on `golden/**` branches: per-toolchain golden hash files as artifacts.
  - Compiler cache (ccache / sccache) and FetchContent cache.
- Importable ruleset for `main` (`.github/rulesets/main.json`), pull request template,
  CODEOWNERS, clang-format and clang-tidy configuration, ADR template and ADR 0001.
- Field registry (A02): `schema/field_registry.json` with the 28 raster fields of plan
  section 4.1 and its JSON Schema `schema/field_registry.schema.json`; the generator
  `tools/codegen/gen_field_ids.py`, which validates the registry and writes the committed header
  `include/wg/generated/field_ids.hpp` (`wg::core::FieldId`, dtypes, ranges, units, enum labels,
  `field_info`, `field_id_from_name`, `field_t`); optional CMake target `wg_codegen`; schema
  files are installed to `share/worldgen_core/schema`.
- `.gitattributes` with LF line endings for JSON, Python and generated headers.
- Core data model (A03) in `wg::core`: `Status` and `Expected<T>` (tl::expected v1.3.1, new
  dependency), `WG_CHECK` / `WG_ASSERT` with a replaceable failure handler, `Grid` (validation,
  index and D8 neighbour helpers), `FieldView`, `MemoryResource`, `FieldStore`, `WorldState`
  (dtype-checked `read` / `write`, zero-copy `adopt`, `release`), the vector layers of plan
  section 4.2, `Provenance` and the fixed-capacity scratch `Arena`.
- RNG and distributions (A04) in `wg::core`: the counter-based hash RNG of plan section 2
  (`mix64`, `combine`, `RngKey`, `stage_key`, `bits64`, `uniform01`, `uniform_open01`,
  `uniform`, `uniform_index`, `RngStream`), deterministic `det_exp` / `det_log` /
  `log_factorial`, and `inverse_normal_cdf`, `normal`, `log_normal`, `poisson` and
  `WeightedBins`. Draws are bit-identical on all platforms. ADR 0002 records the frozen layout.
- Bit-exact Python reference `tools/rng/rng_reference.py` and the generator
  `tools/rng/gen_rng_kat.py`, which writes the known-answer vectors
  `tests/unit/core/rng_kat.inc`. CI `lint` checks that they are current.
- Executor, sinks and cancellation (A05) in `wg::core`: the `Executor` interface with the fixed
  chunking rule (`chunk_count`, `chunk_at`, `ChunkRange` with a worker index), `SerialExecutor`,
  the internal `ThreadPool`, `parallel_for` with per-chunk cancellation and the ordered
  `parallel_reduce`. Reductions are bit-identical for any thread count. Also `FunctionRef`,
  `CancelFlag`, `LogSink` and `ProgressSink` with null sinks. `worldgen_core` now links
  `Threads::Threads`. ADR 0003 records the chunking and cancellation rules.
- Numerics library (A06) in `wg::num`: D8/D4 neighbourhoods, `IndexHeap` with (key, cell)
  tie-break, central differences and D8 slope, masked Laplacian and Laplacian smoothing, BFS
  distance and exact Euclidean distance transform, `UnionFind` and connected-component
  labelling, empirical CDF and quantile mapping, histograms and value ranges, radix-2 FFT,
  radially averaged power spectrum with spectral-exponent fit, and the directional variogram.
  Grid kernels are bit-identical for any thread count. ADR 0004 records the conventions.
- Parameters and data tables (A07) in `wg::params`: the parameter schema
  `schema/params.schema.json` with every R§3 / R§11 and physical parameter, the generator
  `tools/codegen/gen_params.py` and the committed header `include/wg/generated/gen_params.hpp`
  (`GenParams` with the defaults, `kParamDescriptors`, `find_param`); `load_params`,
  `load_params_file`, `read_params` and `to_json` with errors located by JSON path; cross-field
  validation with warnings for contradictory checks; `params_hash` (XXH3-64 over the values);
  the presets `df_default`, `small_island` and `pangaea`; biome, climate, salinity, rock, soil
  and mineral tables with their JSON Schemas, loaders, rules and `tables_hash`. ADR 0005
  records the model and the hash.
- `wg::io::xxh3_64`; new dependencies nlohmann/json v3.12.0 and xxHash v0.8.3 (private to the
  library). Data files are installed to `share/worldgen_core/data`.
- CI `lint` validates every data file against its schema (`tools/data/validate_data.py`).
- Shared test fixtures `tests/support/fixtures.hpp` and numerics benchmarks in `wg_bench`.
