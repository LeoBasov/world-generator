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
- Pipeline framework (A08) in `wg::pipeline`: `Stage` with `StageContext`, `StageResult` and
  `StageMetrics`; `FieldSet` over raster fields and the new `LayerId` vector layers;
  `StageRegistry`; `Profile` with `load_profile` / `load_profile_file` and
  `schema/profile.schema.json`; `Pipeline::create`, which checks the causal order of a profile
  (R2), and `Pipeline::run`, the attempt loop with per-attempt keys, rejection records, the
  repair hook, cancellation, the `SnapshotHook` and per-stage metrics, returning `RunOutput` or
  a `FailureReport`. ADR 0006 records the design. CI `lint` also validates the fixture profiles
  in `tests/fixtures/profiles/`.
- World file and export (A09) in `wg::io`: the `.wgw` writer `write_world` /
  `write_world_file` (atomic rename) and the lazy `WorldReader` (header, table and metadata on
  open; one chunk per `read_field`; `read_into`, also into adopted buffers) with
  `read_world` / `read_world_file` / `read_world_buffer`; `ByteSource` with `MemorySource` and
  `FileSource`; per-chunk zstd compression and XXH3 checksums; format and metadata version
  checks; `SnapshotWriter`, the snapshot hook with the policies none, final and all; PNG16
  export (`encode_png16`, `png16_value`), optional EXR export (`encode_exr`, CMake option
  `WG_WITH_EXR`, built and tested on the linux-gcc CI job) and the vector layers as JSON
  (`layers_to_json`). The format is specified in `docs/file_format.md`; ADR 0007 records the
  decisions. New dependencies zstd v1.5.7, lodepng (pinned commit) and, with `WG_WITH_EXR`,
  tinyexr v1.0.9, all compiled into the library; the project now also enables C.
- `operator==` for `Version`, `RejectionRecord`, `Provenance` and the vector-layer structs.
- World-file benchmarks in `wg_bench`.
- `wgcli` commands (A10, plan section 6.7): `generate` (with `--snapshots none|final|all`),
  `sweep` (metrics CSV, failure reports), `inspect` (header, provenance, chunk table, field
  statistics and histograms from partial reads), `compare` (per-field differences, difference
  PNGs), `hash` (world, field and per-stage hashes; golden seed lists with `--seeds`), `export`
  (PNG16, EXR, layers JSON, colour-mapped review PNGs), `validate` (params, profiles, data
  tables) and `bench` (per-stage times), each with `--help`; exit codes 0 success, 1 generation
  failed, 2 invalid input, 3 internal error. The commands live in the library `wgcli_app` and
  are tested in-process; until A11 wgcli registers the mock stages of the mock profile itself.
  Reference in `docs/cli.md`, kept equal to the `--help` output by the test `wgcli.docs` and
  `tools/wgcli/sync_cli_doc.py`. World and stage hashes are defined in ADR 0008; the golden
  seed list format in `schema/golden_seeds.schema.json`.
- Fractal field generator (A11) in `wg::fractal`: `control_grid`, weighted control values
  (`sample_controls`) with the edge-pinning hook (`pin_edges`), anisotropic midpoint
  displacement (`diamond_square`) with sigma_k = c (V / 3200) (max - min) 2^(-kH) per axis,
  `clamp_field` and `generate_field`. Results are bit-identical for any thread count and
  platform. ADR 0009 records the scheme, the RNG layout and the decision for a bounded map.
  Fractal benchmarks in `wg_bench`; shared statistics helpers `tests/support/stats.hpp`.
- Classic stages C1 and C2 (A12): `c01_allocate` writes `latitude` from the pole option
  (`wg::climate::latitude_of_row`, `fill_latitude`), and `c02_seed` writes the six primary fields
  with the latitude bias of temperature (`climate::latitude_term`, -Delta T sin^2 phi) and one
  peak candidate per summit area (`fractal::find_peak_candidates` on the raw elevation from the
  new `fractal::generate_unclamped_field`). `wg::stages::add_classic_stages` registers them, and
  wgcli's registry includes them. The test profile is `tests/fixtures/profiles/classic_seed.json`.
  ADR 0010 records the conventions.
- Elevation post-processing (A13) in `wg::terrain`: hypsometric remap z' = G^-1(F(z)) onto a
  five-knot target curve (`hypsometric_remap`), edge-ocean counting and repair
  (`count_ocean_edges`, `enforce_edge_oceans`), Gaussian peak and volcano bumps
  (`add_gaussian_bump`), band-limited plains smoothing (`plains_smooth`), volcano placement
  (`place_volcanoes`) and the downstream river constraint (`enforce_river_descent`). Classic
  stages `c03_fit` (remap, edge-ocean and PEAK_NUMBER_MIN checks, edge-ocean repair),
  `c05_shape` (plains, volcanoes, VOLCANO_MIN) and `c07_smooth` (bumps, land smoothing, river
  descent). New parameters `elevation_post.hypsometry_enabled`, `hypsometry_z`,
  `hypsometry_cdf`, `edge_ocean_width`, `final_alpha` and `final_iterations`, so every
  parameter hash changes. The test profile `classic_seed.json` now runs c01, c02, c03 and c05.
  ADR 0011 records the rules.
- Temperature model (A14) in `wg::climate`: the lapse-rate, forest-damping and rain-damping
  terms of R5.3 (`lapse_term`, `vegetation_term`, `rain_term`), the game-scale conversion
  `celsius_to_game` / `game_to_celsius`, and the field kernel `compute_temperature`, which sums
  base, latitude, lapse, vegetation and rain in that order and clamps to the registry range.
  Optional terms are left out by empty spans, and the output may alias the base. No stage uses it
  yet (C8 will); parameters and C2 are unchanged. ADR 0012 records the rules.
- Filling, routing and lakes (A15) in `wg::hydro`: `remove_small_oceans` (interior ocean
  components below `rivers.small_ocean_min_area` raised to land), the priority-flood fills
  `fill_depressions` (flat water level) and `priority_flood` (epsilon gradient on flats), D8
  receivers `d8_receivers`, the Braun–Willett `stack_order`, float64 `accumulate_flow`, and
  `label_lakes` with spill elevation and outlet cell. No stage uses them yet (C6 arrives with
  A17); parameters and fields are unchanged. ADR 0013 records the rules.
- Test-river incision and thermal erosion (A16) in `wg::hydro`: the erosion lattice
  (`snap_to_lattice`, q = 2^-15 eu), `find_river_sources` (mountain-edge cells at z >= 300),
  `run_walker` (steepest descent with ties drawn from the source's stream; in a pit or on a flat
  it floods the depression and digs its saddle), `thermal_erode` (talus relaxation of R6.4 with
  exact transfers, run until every neighbour difference is <= Theta), and the driver `erode`
  for `EROSION_CYCLE_COUNT` and `PERIODICALLY_ERODE_EXTREMES`. Adds
  `terrain::kMountainElevation`. No stage uses them yet (C6 arrives with A17); parameters and
  fields are unchanged. ADR 0014 records the rules.
- River network and stage C6 (A17). `wg::hydro` gains `area_weights` and `runoff_weights`
  (r·P·a in m³/yr), `trace_rivers` (receiver chains from the sources, cut at ocean and lakes,
  continued from lake outlets), `check_river_graph`, `width_class_thresholds` /
  `river_width_class` (w = a_w Q^b against `rivers.width_class_bounds`), `river_attributes`
  (discharge, width class, tributary order, stems and the naming hook `RiverNamer`),
  `bulge_lakes` (R6.1) and `classify_water`. The classic stage `c06_rivers` runs small oceans,
  erosion with `RIVER_MINS`, fill, routing, discharge, lakes, lake bulges and the network, and
  writes `flow_receiver`, `flow_accum`, `discharge`, `water`, `river_class`, `lake_id`,
  `river_graph` and `lakes`. New parameter `rivers.width_class_bounds` (changes every parameter
  hash); test profile `tests/fixtures/profiles/classic_rivers.json`. ADR 0015 records the rules.
- Orographic precipitation (A18) in `wg::climate`: `orographic_precipitation`, the R7.2 upslope
  model with moisture depletion, swept per row from west to east (saturated inflow, slope on
  max(z, sea level), recharge over ocean and land, P clamped to [0, 100], 0 <= q <= q_max), with
  the `OROGRAPHIC_PRECIPITATION` toggle and the DF drainage quirk (D < 50 keeps P_base). The
  output may alias the base rainfall; moisture is an optional output. Test fixture
  `gaussian_ridge`. No stage uses it yet (C8 will); parameters and fields are unchanged. ADR 0016
  records the rules.
- Vegetation and salinity (A19) in `wg::bio`: `miami_npp` and `compute_vegetation` (Miami NPP
  from the game temperature in degC and P_mm = 30 P, normalised by 3000; 0 on ocean, lakes,
  above the tree line and on glacier ice, T <= -5 with D >= 75), `coastal_salinity`,
  `fresh_distance` and `compute_salinity` (S = 100 exp(-d / lambda) over the exact Euclidean
  distance to the ocean, quantised with the salinity classes; rivers inherit the class of their
  mouth cell within the fresh distance along the channel) and `assign_lake_salinity`. No stage
  uses them yet (C4 and C8 will); parameters and fields are unchanged. ADR 0017 records the
  rules.
- Biomes and regions (A20) in `wg::classify`: `BiomeClassifier` (the biome table compiled into
  lookups; the per-cell R9.1 order water, mountain, frozen band, base row, temperature, salinity
  and mangrove variants; values rounded down; river cells keep their land biome; hills rows give
  the region type `hills`), `classify_biomes`, `label_regions` (8-connected regions over every
  cell with area, centroid, biome set, size class and the naming hook `RegionNamer`),
  `count_region_types`, `check_region_counts` and `check_subregion_max`. New classic stages
  `c04_vegetation` (vegetation, provisional biome, initial REGION_COUNTS), `c08_climate`
  (rainfall, temperature, vegetation, salinity and lake classes on the final terrain) and
  `c09_biomes` (`biome`, `region_id`, `regions`); they need the data tables. Test profile
  `classic_biomes` (c01-c09). Parameters, fields and data tables are unchanged. ADR 0018 records
  the rules.
- Geology and subsurface (A21) in the new module `wg::geology`: `classify_environment` and
  `label_geo_regions` (8-connected regions of equal environment and volcanism band),
  `GeologyTables` and `draw_column` (constrained Markov chain over the rock classes with
  P(extrusive top) = V / 100, log-normal thicknesses, soil layers, three cavern layers and the
  magma sea), `check_column`, `compute_soil_depth` (steady state of soil production against
  transport), `assign_aquifers` (AQUIFER layers, low ground and low drainage, the DF heavy quirk)
  and `MineralHosts` / `place_minerals` (Poisson deposits per host layer, nested inclusions). New
  classic stage `c10_subsurface` (`soil_depth`, `geo_region`, `aquifer`, `geo_columns`,
  `minerals`); it needs the data tables. Test profile `classic_geology` (c01-c10).
  `core::GeoColumn` replaces `cavern_levels` with `caverns` (`core::Cavern`) and gains
  `magma_depth_m`. New parameters `geology.deep_ocean_elevation` and `geology.soil_max_depth`;
  `layer_count_min` and `layer_count_max` now start at 2. Every parameter hash changes. ADR 0019
  records the rules.
- Verification and repair (A22): the five `*_RANGES` checks in `c03_fit` (low, mid and high
  squares as thirds of each field's registry scale; `classify::range_bands`, `count_bands`,
  `check_field_ranges`), the hypsometry repair `terrain::fit_elevation_bands` (a monotone
  rank-space remap that moves the band boundaries to the minimums) next to the edge-ocean repair
  in one repair hook, the new classic stage `c11_verify` (final REGION_COUNTS and SUBREGION_MAX),
  attempt statistics `pipeline::add_report` / `AttemptStatistics` (pass rate per stage, measured
  and expected attempts) and validation warnings for unreachable `*_RANGES` bands. Test profile
  `classic_verify` (c01-c11). ADR 0020 records the rules.
