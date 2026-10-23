# ADR 0011: Elevation post-processing (C3, C5, C7)

- Status: accepted
- Date: 2026-10-23
- Work package: A13

## Context

A13 adds three classic stages (plan section 4.3), whose kernels live in `wg::terrain`:
- **C3 Fit and first check** remaps the elevation onto a target hypsometry (R§4.1) and checks the edge oceans and PEAK_NUMBER_MIN.
- **C5 Terrain shaping** smooths the mid-level elevations into plains (R§4.4), places the volcanoes and checks VOLCANO_MIN (R§4.5).
- **C7 Final smoothing** adds the peak and volcano bumps (R§4.3, R§4.5), smooths the land and keeps every river flowing downhill (plan 7.1).

The report describes these steps only in outline. Each open detail below changes every world, so it is recorded here.

## Decision

- **Stage ids.** The ids are `c03_fit`, `c05_shape` and `c07_smooth`. They never change. None of the three stages draws random numbers.
- **Target hypsometry G (R§4.1).** G is a piecewise-linear CDF through five knots, given by `elevation_post.hypsometry_z` (strictly rising) and `hypsometry_cdf` (rising weakly from 0 to 1). Five knots is the fixed length the parameter generator allows; the knots need not sit on the thresholds 100 and 300. `hypsometry_enabled = false` skips the remap.
  - The remap is z' = G⁻¹(F(z)), with F the empirical CDF of `num::empirical_cdf`, i.e. #{z_j ≤ z_i} / m.
  - G⁻¹(p) is the smallest z with G(z) ≥ p. A flat stretch of G is jumped over.
  - Equal values stay equal. The clamp atom at 400 has F = 1, so it maps to the last knot (400 by default) and the peaks survive. With continuous input, KS(z′, G) is about 1/m.
- **Edge oceans (R§11).** An edge is the outermost row or column. It is partly ocean if at least one of its cells is below 100, and completely ocean if all are. A complete edge also counts as partial.
- **Checks in C3.** The order is COMPLETE_OCEAN_EDGE_MIN, then PARTIAL_OCEAN_EDGE_MIN, then PEAK_NUMBER_MIN. The first failure rejects with the measured count and the threshold.
  - Before the checks, C3 drops every peak whose cell now lies below 400, so the remap and the repair can remove peaks.
  - The `*_RANGES` checks (low, mid and high squares) are left to A22, because the report does not define where low, mid and high start.
- **Edge-ocean repair (R8).** C3's `repair` hook (`terrain::enforce_edge_oceans`) handles a failed edge-ocean check and then runs all checks again. A failed PEAK_NUMBER_MIN is not repaired. Cells are only ever lowered, to cap(d) = 99 + (d / w)·(400 − 99) for d < w, with w = `elevation_post.edge_ocean_width`:
  - **Complete edges.** d is the distance in cells to the edge line. Edges are taken in order of most ocean cells first, ties in the order north, east, south, west.
  - **Partial edges, after the complete step.** d is the Euclidean distance to the edge's lowest cell (ties go to the lower index), which opens a bay. Only edges without ocean are taken, lowest minimum first, ties in edge order.
  - The repair does not remap again afterwards, so the hypsometry drifts a little where it acts.
- **Plains (R§4.4).** These are Jacobi steps z ← z + λ·w(z)·(mean of the 8 neighbours − z).
  - w is 1 in [z_lo, z_hi] and tapers linearly to 0 over `plains_taper`. It is evaluated on the values before each step.
  - A neighbour off the grid counts as the cell itself (zero flux, ADR 0004).
  - A cell with w = 0 is copied, so cells that start outside [z_lo − taper, z_hi + taper] stay bit-identical.
  - `num::laplacian_smooth` is not reused here: it has four neighbours and no weight.
- **Volcanoes (R§4.5).** There is one volcano per 8-connected area of volcanism ≥ 100.
  - C2 has clamped volcanism by then, so an area is flat and has no highest cell. The volcano goes to the cell nearest the area's centroid, ties to the lower index.
  - The volcanoes are ordered by the first cell of their area, and every volcano is `magma_connected`.
  - If `fields.volcanism.max` < 100, there are no volcanoes.
- **C7 order.**
  1. Peak bumps, then volcano bumps, in layer order. A bump is z ← max(z, min(400, z + A·exp(−r²/2ℓ²))) out to r = 4ℓ, using `det_exp`. Peaks use `peak_height` and their `radius_cells`; volcanoes use `volcano_height` and `volcano_width`.
  2. `num::laplacian_smooth` with `final_alpha` and `final_iterations` on land cells (z ≥ 100), leaving out the peak and volcano cells. Ocean cells do not change.
  3. The river constraint, applied last.
- **River constraint (plan 7.1).** Starting from the mouths, z_i ← max(z_i, z_r(i) + ε_z) breadth first, with ε_z = `rivers.min_drop`.
  - A raised value is the smallest float32 ≥ z_r(i) + ε_z computed in float64. float(300 + 0.01) < 300.01, so plain rounding would break the invariant.
  - The graph is checked before any cell changes: consistent arrays, cells inside the grid and unique, valid downstream indices, and no cycle.
  - A cell that would rise above 400 makes the stage fail with `out_of_range` instead of a clamp that would break the invariant. A17 builds strictly descending rivers, so this should not happen.
- **Missing inputs.** The stages modify `elevation` through `update_float`, which reports `not_found` instead of creating a zero field.

## Consequences

- G, the tie rule, the repair geometry, the volcano rule and the C7 order are frozen. Changing any of them changes every world (`golden/**` branch).
- The new parameters change every parameter hash (ADR 0005).
- C7 cannot run in a profile before C6 provides `river_graph` (R2). Its tests use the fixture graph `tests/support/river_fixture.hpp`, and the test profile `classic_seed.json` runs c01, c02, c03 and c05.
- The `pangaea` and `small_island` presets (COMPLETE_OCEAN_EDGE_MIN = 4) are now repaired rather than rejected.

## Alternatives considered

- **Derive G from `fields.elevation.weights`.** This needs no new parameters, but its five equal bins over [min, max] do not line up with sea level and the mountain threshold.
- **Edge oceans by regenerating with `fractal::pin_edges`.** This is the report's method. It needs a second fractal run with C2's key and a fresh peak search, which would be a C2 concern. It remains possible for A22.
- **Break ties in the remap by cell index.** This matches G better when large atoms exist, but it splits the clamp atom at 400 and breaks Spearman ρ = 1 against the input.
- **Clamp the river constraint at 400.** This silently breaks the invariant the constraint exists for.
