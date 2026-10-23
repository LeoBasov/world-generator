# ADR 0010: Classic stages C1 and C2

- Status: accepted
- Date: 2026-10-23
- Work package: A12

## Context

A12 adds the first library stages of the classic profile (plan section 4.3):
- **C1 Allocate and orient** writes `latitude`.
- **C2 Seed primary fields** reads `latitude` and writes the six primary fields of R§3 (elevation, rainfall, temperature, drainage, volcanism, savagery) and the `peaks` layer.

The technical report leaves several details open. Each one feeds every world and golden hash, and stage ids key the RNG (R1). So they are recorded here.

## Decision

- **Stage ids.** The ids are `c01_allocate` and `c02_seed`, equal to the file stems in `src/stages/classic/`. They never change.
- **Registration.** `wg::stages::add_classic_stages` (`include/wg/stages/classic.hpp`) adds the classic stages implemented so far, and later work packages append theirs. wgcli's `make_registry()` calls it next to the mock stages.
- **Allocation.** `Pipeline::run` already creates the WorldState on the parameters' grid, so C1 only orients it.
- **Latitude (R§5.3).** φ = (π/2)·d_eq(y)/d_max, computed in float64 and stored as float32 (`climate::latitude_of_row`, `climate::fill_latitude`). Row 0 is the north edge, as in ADR 0009.
  - `north`: d_eq = n − 1 − y, so the pole is at row 0.
  - `south`: d_eq = y, so the pole is at row n − 1.
  - `both`: d_eq = |y − (n − 1)/2| and d_max = (n − 1)/2, so the equator is the middle row.
  - `none`: φ = 0 everywhere. The report says only that there are then no seasons. A world without poles has no equator–pole gradient, so the latitude term vanishes.
- **Keys.** C2 draws each field from `key.derive(<field name>)`, using the registry name (e.g. `"elevation"`), and passes that key to `fractal::generate_field` (ADR 0009). Fields therefore do not share draws, and adding a field later leaves the others unchanged. No control points are pinned. Edge oceans belong to C3 and its repairs (A13, A22).
- **Temperature (R§5.3).** T = clamp(T₀ + T_φ(φ)) with T_φ = −ΔT·sin²φ (`climate::latitude_term`, ΔT = `temperature.equator_pole_delta`).
  - T₀ is the fractal field clamped to the parameters' [min, max].
  - The bias is added after that clamp, so temperature can fall below min, which R§5.1 observes in DF.
  - The sum is clamped to the registry range [−1000, 1000].
  - The lapse-rate and damping terms come with A14 and C8.
- **Peak candidates (R§4.3).** Elevation is generated without clamping (`fractal::generate_unclamped_field`).
  - Each 8-connected area of cells with raw z ≥ 400 yields one peak, at its highest raw cell. Ties go to the lower cell index.
  - Then the field is clamped, so every peak cell ends at exactly 400.
  - Peaks are ordered by the first cell of their area in row-major order. Each has `height_eu` = 400 and `radius_cells` = `elevation_post.peak_width`.
  - If `fields.elevation.max` < 400, no cell can reach 400, so there are no peaks.
  - C2 checks nothing. PEAK_NUMBER_MIN is checked in C3.
- **Trigonometry.** sin²φ uses `std::sin`. The standard does not guarantee its last bit across C libraries, so temperature may differ in the last ulp between toolchains. Golden hashes are kept per toolchain (plan 6.8), which covers this. Results are still bit-identical for any thread count on one toolchain.

## Consequences

- The field order, key names and peak rule are frozen. Changing them changes every world, which makes it an intended output change (`golden/**` branch).
- One plateau at 400 counts as one peak, so PEAK_NUMBER_MIN measures separate summits, not the area at the top of the scale.
- A14 reuses `climate::latitude_term` for the full temperature model.

## Alternatives considered

- **Every cell at 400 is a peak.** This is simpler, but a single flat plateau would yield dozens of peaks and make PEAK_NUMBER_MIN meaningless.
- **D8 local maxima at 400 after clamping.** After clamping, a plateau is flat, so this needs the raw values anyway. It also yields several peaks per rippled plateau.
- **φ = π/4 or a mid-latitude constant for `none`.** This would still bias temperature, which contradicts a world without poles.
- **A `det_sin` in `core::detmath`.** This is bit-exact across platforms, but it is more code to maintain. The per-toolchain golden hashes already absorb the difference.
