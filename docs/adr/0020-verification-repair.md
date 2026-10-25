# ADR 0020: Verification, repair and attempt statistics

- Status: accepted
- Date: 2026-10-25
- Work package: A22

## Context

R§11 lists the rejection checks of DF world generation: the field statistics (`ELEVATION_RANGES`, `RAIN_RANGES`, `DRAINAGE_RANGES`, `VOLCANISM_RANGES`, `SAVAGERY_RANGES`), the edge oceans, the peaks and volcanoes, `REGION_COUNTS` before and after erosion, `RIVER_MINS` and `SUBREGION_MAX`. If check i passes with probability p_i, a world needs E[attempts] = (∏ p_i)⁻¹ attempts. Design principle R8 asks for repair before rejection, with the hypsometry and the edge oceans as the cheap repairs.

Several pieces already exist:
- the attempt loop, its cap `checks.max_attempts`, the `FailureReport` and the repair hook (A08, ADR 0006);
- the edge-ocean and peak checks with the edge repair in C3 (A13, ADR 0011);
- `VOLCANO_MIN` in C5, `RIVER_MINS` in C6, and the initial `REGION_COUNTS` in C4 (A13, A17, A20);
- the kernels for the final `REGION_COUNTS` and `SUBREGION_MAX` (A20, ADR 0018).

A22 adds the field-statistics checks, the hypsometry repair, the final verification stage C11 and the attempt statistics. ADR 0011 left the `*_RANGES` checks open, because the report does not say where low, mid and high start. Each choice below changes which worlds are accepted.

## Decision

- **Bands.** Low, mid and high are the thirds of the field's fixed registry scale, not of the parameter clamp `fields.<f>.min/max`. With lo and hi the registry minimum and maximum:
  - low is [lo, lo + (hi − lo)/3);
  - mid is [lo + (hi − lo)/3, lo + 2(hi − lo)/3);
  - high is [lo + 2(hi − lo)/3, hi].

  The cuts are computed in float64. Elevation (0–400) is cut at 133.33 and 266.67, and rainfall, drainage, volcanism and savagery (0–100) at 33.33 and 66.67. A value on a cut belongs to the upper band.
  - The check fails when a band holds fewer squares than its minimum.
  - Check ids are `ELEVATION_RANGES[0]` … `SAVAGERY_RANGES[2]`, in the style of `REGION_COUNTS.SWAMP[1]`.
  - measured is the square count and required the minimum.
  - The kernels are `classify::range_bands`, `count_bands` and `check_field_ranges` (`include/wg/classify/field_ranges.hpp`).
- **Where.** All five run in C3, after the hypsometric remap, as plan §4.3 lists them. R§11's "after seeding, after fitting" holds: the other four fields still have C2's values at that point. C3 therefore also reads rainfall, drainage, volcanism and savagery.
  - The check order is `COMPLETE_OCEAN_EDGE_MIN`, `PARTIAL_OCEAN_EDGE_MIN`, `PEAK_NUMBER_MIN`, then the five `*_RANGES` in field order, each low, mid, high.
  - C3 reports every band count as a metric (`elevation_low`, …, `savagery_high`).
- **Hypsometry repair (R8; R§4.1 "adjusts altitudes when it missed").** `terrain::fit_elevation_bands` repairs a failed `ELEVATION_RANGES`. It moves the band boundaries in rank space and remaps through a monotone piecewise-linear map, so the spatial pattern, the rank order and equal values are kept. With m cells, minimums L, M and H, and boundaries only between distinct values:
  1. The low count becomes max(n_low, L), rounded up to a boundary. The high band starts at its current rank, or at m − H rounded down if that holds too few squares.
  2. If fewer than M squares lie between the two, the low boundary moves down towards L, then the high one up towards m − H, again to boundaries.
  3. The map runs through the knots (a, a), (A, 133.33), (B, 266.67) and (b, b), with a = min(z, 0) and b = max(z, 400). A is the cut itself if the low boundary did not move, else the smallest value of the new mid band. B is defined the same way.
  4. Results are clamped strictly into their band in float32, so the counts afterwards are exactly the new boundaries.
  - A band whose boundaries did not move keeps its values.
  - b maps to b, so the peaks at 400 survive.
  - If no boundaries meet the minimums (a plateau larger than the free squares, since equal values cannot be split), the repair leaves the rejection standing.
  - The other four `*_RANGES` are not repaired, because R8 names only the cheap elevation repairs.
- **C3 repair flow.** The runner calls the hook once. So the hook repairs the edge oceans and the elevation bands, each at most once, in the order their checks fail, and runs all checks again after each step. An edge repair can create the band deficit and a band repair the edge deficit, and either order is handled. `PEAK_NUMBER_MIN` stays unrepairable (ADR 0011).
- **C11 `c11_verify`.** Inputs: `region_id` and the `regions` layer. No outputs.
  - It derives each cell's region type from `regions[region_id].type`.
  - It runs `count_region_types`, `check_region_counts(final)` (entry [2], the regions per type) and then `check_subregion_max`. The first failure rejects.
  - It needs the data tables for the region-type names, like C4.
  - Plan §4.3 lists `biome` as an input, but C11 does not read it, so it is not declared (R2 lists what a stage reads).
  - A `region_id` outside the layer is an error, not a rejection.
- **Attempt statistics.** `pipeline::add_report` (`include/wg/pipeline/attempts.hpp`) collects, from any `RunReport`, per stage in first-seen order:
  - the attempts that reached the stage and the rejections (a repaired run counts as passed);
  - p̂_i = (runs − rejections) / runs;
  - the measured attempts per report and E = (∏ p̂_i)⁻¹.

  p̂_i is conditional on reaching stage i, which is what the product needs. Over uncapped runs, E equals the measured mean exactly, because each report ends in one success. The statistics are diagnostics and enter no hash.
- **Validation.** `validate` warns about a `*_RANGES` minimum > 0 for a band the clamp [min, max] of rainfall, drainage, volcanism or savagery cannot reach, since no attempt can then pass. It does not warn about elevation, because the remap and the hypsometry repair can move it into any band.

## Consequences

- The classic profile has eleven stages. `data/profiles/classic.json` (A23, ADR 0021) runs all of them; until then the test fixture `classic_verify.json` did.
- C3's inputs grow by four fields. Every profile that runs C3 also runs C2, so R2 is unaffected.
- The defaults (all minimums 0, `SUBREGION_MAX` 2750) reject nothing new. No parameter or field changes, so no hash changes.
- `tests/integration/checks/test_checks.cpp` holds the acceptance tests:
  - every check passes at its measured value and rejects one step beyond it;
  - contradictory parameters exhaust the attempts with a complete report;
  - two independent coin stages show mean attempts within the 99 % interval of (p₁p₂)⁻¹.

## Alternatives considered

- **Thirds of the parameter clamp [min, max].** The bands would move with the parameters, so the same minimum would mean different things in different presets.
- **Elevation bands by meaning (ocean < 100, land, mountain ≥ 300).** This is physically clearer, but it mixes two rules for one family of checks. The thresholds already have their own checks (edge oceans, peaks).
- **Hypsometry repair by a second remap onto a shifted G.** It needs a rule for shifting five knots and does not hit the counts exactly. The rank-space map hits them exactly and keeps unaffected bands.
- **Edge oceans by regenerating with pinned control points** (ADR 0011). This remains a C2 concern. The in-place repair is cheaper and already frozen.
