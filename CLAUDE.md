# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This repository is `worldgen-core`, Phase 1 of the world generator: an engine-independent C++20 library (`worldgen_core`) plus the `wgcli` tool. The specification is the project plan doc "World Generator – Implementation Project Plan" (https://claude.ai/artifact/QRYJWfqWmhjwVwjr3fb97t). Work is organised as work packages A01–A32. Read the plan section for a package before implementing it. Its acceptance tests define "done".

Current state: A01 to A21 done. A01 is the folder tree, CMake, a placeholder `wg::core::version()`, a placeholder `wgcli`, the doctest runner, the nanobench runner `wg_bench`, the example `wg_example` and the three workflows. A02 is the field registry `schema/field_registry.json` (with its JSON Schema), the generator `tools/codegen/gen_field_ids.py` and the committed header `include/wg/generated/field_ids.hpp` (`wg::core::FieldId`, `kFields`, `field_info`, `field_id_from_name`, `field_t<F>`). A03 is the core data model in `wg::core`: `Status`/`Expected` (tl::expected), `WG_CHECK`/`WG_ASSERT`, `Grid` with D8 neighbours, `FieldView`, `MemoryResource`, `FieldStore`, `WorldState` (typed read/write, zero-copy `adopt`, `release`), `VectorLayers`, `Provenance` and the scratch `Arena`. A04 is the RNG in `wg::core` (`rng.hpp`: counter hash, `stage_key`, `uniform01`, `RngStream`; `detmath.hpp`: platform-independent `det_exp`/`det_log`; `distributions.hpp`: normal, log-normal, Poisson, `WeightedBins`) with its frozen layout in ADR 0002 and a bit-exact Python reference in `tools/rng/`. A05 is parallel execution in `wg::core`:
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

A10 is `wgcli` (`tools/wgcli/`, namespace `wg::cli`); `docs/cli.md` is the reference, ADR 0008 defines the world and stage hashes:
  - The commands are in the static library `wgcli_app`; `main.cpp` only forwards argv to `cli::run(args, out, err)`, which returns the exit code (0 ok, 1 generation failed / compare differs, 2 invalid input, 3 internal). One `cmd_<name>.cpp` per command, each with a `CommandSpec` (table-driven parser in `args.hpp`) and `run_<name>`; shared loading in `common.hpp` (`make_generator`, `make_registry`, `exit_code_for`).
  - `make_registry()` holds the library's classic stages (`add_classic_stages`) and the mock stages of `tests/fixtures/profiles/mock.json` (`mock_profile.hpp`; `m03_verify` rejects about 25 % of attempts by its key).
  - Tests run the commands in-process: `tests/unit/wgcli/` for the pieces, `tests/integration/wgcli/` for one file per command (`tests/support/cli_runner.hpp`), plus the CTest `wgcli.binary.exit_codes` on the real executable. `tests/CMakeLists.txt` also globs `tests/integration/`.
  - `docs/cli.md` repeats every `--help` text between markers; the test `wgcli.docs` fails when they differ. After changing a command's options or texts, rebuild and run `python tools/wgcli/sync_cli_doc.py --wgcli <path to wgcli>`.
  - `wgcli hash --per-stage --seeds FILE --out FILE` is what `golden-update.yml` runs; the seed-list format is `schema/golden_seeds.schema.json`. `tests/golden/seeds.json` itself arrives with the classic profile (A23).

A11 is the fractal field generator in `wg::fractal` (`include/wg/fractal/fractal.hpp`, `src/fractal/`); ADR 0009 records the frozen RNG layout and scheme:
  - `control_grid` (M = min(2^(m-1), N-1) cells), `sample_controls` (weighted bins, keyed by control index so values do not depend on N; m = 1 ignores the weights), the edge-pinning hook `pin_edges` / `pinned_bins`, `diamond_square` (unclamped; per level an edge step from the two collinear parents with sigma_x / sigma_y, then a centre step from the four edge points; xi ~ N(0, 1); 2^(-kH) via `det_exp`), `clamp_field` and `generate_field`. The kernel takes `params::FieldGenParams` directly. The map is a bounded square (no wrap).
  - The statistical acceptance tests are in `tests/unit/fractal/test_fractal_stats.cpp`; KS and chi^2 helpers live in `tests/support/stats.hpp`.

A12 is the classic stages C1 and C2 (`src/stages/classic/`). ADR 0010 records the conventions:
  - `c01_allocate` writes `latitude`. The kernels are in `wg::climate` (`include/wg/climate/latitude.hpp`): `latitude_of_row`, `fill_latitude` and `latitude_term` = -Delta T sin^2 phi. Row 0 is north, and poles `none` gives phi = 0.
  - `c02_seed` writes the six primary fields, each drawn from `key.derive(<field name>)`. Temperature is T_0 clamped to its parameter range, plus `latitude_term`. Peaks come from `fractal::find_peak_candidates` (`peaks.hpp`) on the raw elevation of `fractal::generate_unclamped_field`, before clamping: one per 8-connected area >= 400, at its highest cell.
  - `wg::stages::add_classic_stages` (`include/wg/stages/classic.hpp`) registers the classic stages; the internal factories are in `src/stages/classic/classic_stages.hpp`. Later classic stages add a file there and an entry in `classic.cpp`.
  - Stage tests run single stages with `tests/support/stage_run.hpp` (`run_stage`, or `run_stage_with_repair` to follow a reject with the repair hook as the runner does). The profile `tests/fixtures/profiles/classic_seed.json` runs the stages that need no rivers (c01, c02, c03, c05) until `data/profiles/classic.json` arrives (A23).

A13 is elevation post-processing: kernels in `wg::terrain` (`include/wg/terrain/`, `src/terrain/`), stages C3, C5 and C7. ADR 0011 records the rules:
  - `hypsometry.hpp`: `hypsometric_remap` z' = G^-1(F(z)) onto the five-knot curve `elevation_post.hypsometry_z`/`hypsometry_cdf`. Equal values stay equal, so the 400 atom stays at the last knot. `edge_ocean.hpp`: `count_ocean_edges`, `enforce_edge_oceans` (lowers only). `bumps.hpp`, `plains.hpp`, `volcanoes.hpp` (one volcano per area of volcanism 100, nearest the centroid), `river_descent.hpp` (smallest float >= z_r + min_drop). `elevation.hpp` has `kSeaLevel` (100) and `kMaxElevation` (400).
  - `c03_fit`: remap, drop peaks below 400, then COMPLETE/PARTIAL_OCEAN_EDGE_MIN and PEAK_NUMBER_MIN; its `repair` fixes only the edge oceans. The `*_RANGES` checks are left to A22. `c05_shape`: plains, volcanoes, VOLCANO_MIN. `c07_smooth`: bumps, land smoothing (`num::laplacian_smooth`), river descent; until C6 exists it is tested with `tests/support/river_fixture.hpp`.
  - Stages that modify a field an earlier stage wrote use `update_float` from `classic_stages.hpp` (not_found if absent); `write_float` creates the field.

A14 is the temperature model in `wg::climate` (`include/wg/climate/temperature.hpp`); ADR 0012 records the rules:
  - Point terms `lapse_term` (−k Γ max(0, z − 100) h_u / 1000, Γ in K/km, k = `game_per_kelvin`), `vegetation_term` (−α V), `rain_term` (−d P / 100), and `celsius_to_game` / `game_to_celsius`.
  - `compute_temperature` sums base, latitude, lapse, vegetation and rain in float64 in that order and clamps to [−1000, 1000]. Empty optional spans leave their term out, and `out` may alias the base.
  - C2 keeps T₀ + T_φ; C8 adds the other terms in place (base = C2 temperature, no latitude).

A15 is filling, routing and lakes in `wg::hydro` (`include/wg/hydro/`, `src/hydro/`); ADR 0013 records the rules. Ocean is z < `terrain::kSeaLevel` throughout:
  - `small_ocean.hpp`: `remove_small_oceans` raises 8-connected ocean components below `min_area` to exactly 100, except components on the map edge.
  - `priority_flood.hpp`: `fill_depressions` (flat water level w) and `priority_flood` (surface z̃, rising by `PriorityFloodParams::epsilon` on flats, rounded up in float64). The ocean cells are the seeds; a map without ocean starts from its lowest cell. The map edge is not an outlet.
  - `routing.hpp`: `d8_receivers` (−1 for ocean and cells without a lower neighbour, steepest drop, kDir8 ties), `stack_order` (Braun–Willett, sinks and donors ascending) and `accumulate_flow` (generic float64 weights, reused by A17 for discharge).
  - `lakes.hpp`: `label_lakes` turns components with w > z and volume Σ(w − z) ≥ `min_volume` into `core::Lake`s, numbered by first cell. The outlet is the receiver of the lowest-z̃ lake cell.
  - Kernels only: stage `c06_rivers` comes with A17, after A16. Lake bulges along rivers are left to A17 too. The bowl, V-valley and plateau fixtures are in `tests/support/fixtures.hpp`.

A16 is test-river incision and thermal erosion in `wg::hydro`; ADR 0014 records the rules:
  - `test_rivers.hpp`: the erosion lattice `kErosionQuantum` = 2^-15 eu (`snap_to_lattice`, `on_lattice`; values below 512 in magnitude are exact float32), `find_river_sources` (z >= `terrain::kMountainElevation` 300 with an 8-neighbour below, ascending), `run_walker`.
  - The walker never revisits a cell (caller-owned `visited` bytes, cleared on return) and moves to its lowest lower neighbour, ties drawn from its stream. In a pit or on a flat it floods the depression in (z, index) order to the first cell below the level and digs the highest route cell (the saddle) by epsilon, floored at sea level. The literal R6.3 rule only digs pits deeper.
  - `thermal.hpp`: `thermal_erode` in int32 quanta from a FIFO work list (exact sum, ends with every 8-neighbour difference <= Theta). `erosion.hpp`: `erode` snaps, then per cycle c runs sources, walkers with `RngStream(key.derive(c), source)`, and the thermal step when `PERIODICALLY_ERODE_EXTREMES`; it returns `ErosionReport` (source counts for `RIVER_MINS`).
  - Kernels only: A17 calls `erode` in `c06_rivers` after `remove_small_oceans`. The closed-basin fixture is `closed_basin` in `tests/support/fixtures.hpp`.

A17 is the river network in `wg::hydro` and stage C6 `c06_rivers`; ADR 0015 records the rules:
  - `discharge.hpp`: `area_weights` (Δx² on land) and `runoff_weights` (r·P·a in m³/yr, r = 1 − D/100, P_mm = 30 P), summed by `accumulate_flow`; `kSecondsPerYear`.
  - `river_network.hpp`: `trace_rivers` (receiver chains from the sources, cut before ocean and lakes, continued from lake outlets; nodes in ascending cell order), `check_river_graph`, `width_class_thresholds` / `river_width_class` (w = a_w (Q/s)^b against `rivers.width_class_bounds`, classes 1 brook … 5 major), `river_attributes` (Q, class, tributary order with the larger-Q branch as main stem, `RiverStem`s, naming hook `RiverNamer`).
  - `lake_bulge.hpp`: `bulge_lakes` (drawn river cells flood their upstream drainage below z + Δh; all lakes renumbered by first cell). `water.hpp`: `WaterClass`, `classify_water` (fields `water`, `river_class`).
  - `c06_rivers` order: small oceans, `erode` + `RIVER_MINS[0]`/`[1]` rejects, fill, routing, area and discharge, lakes, first trace, bulges, final trace, attributes, `water`. Elevation stays the bed (no lake surface).
  - The test profile with rivers is `tests/fixtures/profiles/classic_rivers.json` (c01, c02, c03, c05, c06, c07). Kernel tests build the A15 chain with `tests/support/hydro_chain.hpp` (`hydro_chain`, `rough_island`).

A18 is orographic precipitation in `wg::climate` (`include/wg/climate/precipitation.hpp`); ADR 0016 records the rules:
  - `orographic_precipitation` sweeps each row from west to east: P = P_base q/q_max + k_o q max(0, Δz), q −= η P, q += E (`recharge_ocean` for z < 100, else `recharge_land`). Δs = 1 tile, the air enters saturated, the slope uses max(z, 100), P is clamped to [0, 100] and q to [0, q_max].
  - With `df_drainage_quirk`, cells with D < 50 keep P_base but still deplete q (drainage is then required). With `OROGRAPHIC_PRECIPITATION` off, the output is a copy of the base. The output may alias the base; `moisture` is an optional output.
  - C8 applies it to `rainfall` in place. The ridge fixture is `gaussian_ridge` in `tests/support/fixtures.hpp`.

A19 is vegetation and salinity in `wg::bio` (`include/wg/bio/`, `src/bio/`); ADR 0017 records the rules:
  - `vegetation.hpp`: `miami_npp` (min of the temperature and precipitation limits, `det_exp`), `is_ice` (glacier: T <= -5 game, D >= 75) and `compute_vegetation` (T via `climate::game_to_celsius` of the peak-summer temperature, P_mm = `rain_mm_per_index` P; V = NPP/3000, 0 on ocean, on lake cells of the optional `water`, above `tree_line` and on ice).
  - `salinity.hpp`: `coastal_salinity` S = 100 e^(-d/lambda), `fresh_distance` (lambda ln(100/16.5) ~ 2.70 tiles), `compute_salinity` (d from `num::edt_squared` to `water == ocean`, quantised with `SalinityClasses`; river nodes within `fresh_distance` along the channel take their mouth cell's class if higher) and `assign_lake_salinity` (highest class of the lake's cells).
  - C4 (vegetation without water) and C8 (vegetation, salinity, lake classes) use them.

A20 is biomes and regions in `wg::classify` (`include/wg/classify/`, `src/classify/`) and the stages C4, C8 and C9; ADR 0018 records the rules:
  - `biome.hpp`: `BiomeClassifier::create(params::BiomeTable)` compiles the table into lookups (refuses gaps and overlaps). `classify(CellInputs)` is the pure per-cell R§9.1 order: water first (ocean, lake by band and salinity; without `water`, z ≤ 99 is ocean), mountain, frozen band (tundra / glacier), base row, variant (wetlands saline at ≥ 66), mangrove. Values are rounded down. River cells keep their land biome (`river_biome` is a lookup only). Base rows with `hills` give region type `hills`. `classify_biomes` is the grid kernel.
  - `regions.hpp`: `label_regions` (8-connected components of equal region type over every cell, ocean and lakes included; ids 0 … K−1 by first cell; centroid from exact sums; ascending biome set; `region_size_class`; naming hook `RegionNamer`), `count_region_types`. `checks.hpp`: `check_region_counts` (initial phase [0] cells and [1] regions, final phase [2]; ids `REGION_COUNTS.<TYPE>[k]`), `check_subregion_max` (for C11 in A22).
  - `c04_vegetation`: vegetation, provisional biome (no water, no salinity), initial REGION_COUNTS reject. `c08_climate`: rainfall, temperature (lapse + C4 vegetation + new rain), vegetation with water, salinity, lake classes written back to the lake cells. `c09_biomes`: `biome`, `region_id`, `regions`. The three stages need `StageContext::tables` (else invalid_argument).
  - Stage helpers `write_field`, `read_field` and `data_tables` are in `classic_stages.hpp`. `tests/support/stage_run.hpp` has `repo_tables()`, a `tables` argument for `run_stage`, and `run_classic(stages, params, seed, exec)`. The profile `tests/fixtures/profiles/classic_biomes.json` runs c01–c09.

A21 is geology and subsurface in `wg::geology` (`include/wg/geology/`, `src/geology/`) and the stage C10; ADR 0019 records the rules:
  - `regions.hpp`: `GeoEnvironment` (deep/shallow ocean by `geology.deep_ocean_elevation` and sea level, mountain and `DESERT_SAND` by biome, else land), `classify_environment`, `label_geo_regions` (8-connected components of equal environment and volcanism band min(4, floor(V/20)); ids by first cell; `GeoRegions` keeps CSR cells, area and mean V).
  - `column.hpp`: `GeologyTables` (rocks per class, ocean-flagged sedimentary rocks, soils per environment), `draw_column` (L uniform in the layer counts, top E with V/100 or always with a volcano, else S with `sedimentary_top_probability`, else M; successors S->{S,M,I}, E->{E,M,I}, M->{M,I}, I->{I,M}; last layer I and twice as thick; thickness log-normal z-levels times `hu_m`; soil layers share the region's mean soil depth; caverns at soil + stone (k+1)/4, magma below the stone), `check_column` (the invariants). Streams `key.derive("column" | "soil" | "caverns")` per region.
  - `soil.hpp`: `steady_soil_depth` / `compute_soil_depth` (h = h_0 ln(P_0 / (kappa C)), C = -laplacian(z) in 1/m, capped at `geology.soil_max_depth`, 0 on mountains). `aquifer.hpp`: `assign_aquifers` (land cells of regions with an AQUIFER layer, chance (1 - D/100)(1 - clamp((z-100)/200)), heavy by the D mod 20 = 7 quirk or 1/20). `minerals.hpp`: `MineralHosts`, `expected_deposits`, `place_minerals` (Poisson per layer and matching environment, uniform cell, nested hosts one level deep).
  - `core::GeoColumn` has `caverns` (`core::Cavern`) and `magma_depth_m` instead of `cavern_levels`. `c10_subsurface` also reads the `volcanoes` layer and needs the data tables. The profile `tests/fixtures/profiles/classic_geology.json` runs c01–c10; `tests/support/stats.hpp` has `binomial_interval99` and `poisson_interval99`.

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
- `golden-update.yml`, branches `golden/**`: per-toolchain hash files (`tools/ci/toolchain_key.sh`) as artifacts. The hash step activates once `tests/golden/seeds.json` exists (A23).

Runner labels and action SHAs are pinned. Keep them pinned, because golden hashes depend on compiler versions. Update pins by hand (no Dependabot); resolve SHAs with `git ls-remote git@github.com:<owner>/<repo>.git`.
