# ADR 0016: Orographic precipitation

- Status: accepted
- Date: 2026-10-24
- Work package: A18

## Context

R§7 corrects the fractal rainfall once the terrain is final: windward slopes get more rain, lee sides less. R§7.2 reconstructs this as an upslope model with moisture depletion, swept once from upwind to downwind. With i − 1 the upwind neighbour of cell i:

- P_i = P_base,i · q_{i−1}/q_max + k_o · q_{i−1} · max(0, (z_i − z_{i−1})/Δs)
- q_i = min(q_max, q_{i−1} − η·P_i·Δs + E_i·Δs)

R§7.1 adds the toggle `OROGRAPHIC_PRECIPITATION` and an observed DF quirk: the effect appears only where drainage D ≥ 50, and the wet and dry patches are shifted east. The plan keeps the quirk as a toggle (`precipitation.df_drainage_quirk`, default on, plan section 16). The parameters have existed since A07.

A18 adds the kernel `climate::orographic_precipitation` (`include/wg/climate/precipitation.hpp`). Like the A14 temperature terms, its first user is C8 (climate recomputation), which also needs the vegetation and salinity of A19. The report leaves the details below open. Each one changes every world, so they are recorded here.

## Decision

- **Wind and boundary.** The prevailing wind blows from west to east (+x). This matches DF's eastward shift and the plan. Each row is one independent sweep. The air enters the map at x = 0 saturated (q = q_max), and cell 0 of a row has no upslope term. The map is a bounded square (ADR 0009), so nothing wraps.
- **Unit of length.** Δs = 1 tile, so η and E are per tile, as `x-unit` `1/tile` in the schema says. The slope is in elevation units per tile. Δx in metres does not enter, which keeps the defaults independent of `grid.dx_m`.
- **Surface.** The slope uses the surface max(z, `terrain::kSeaLevel`), so the bathymetry under the ocean causes no lift. A coast rising from the sea is a slope of z − 100.
- **Ocean and land.** Ocean is z < `terrain::kSeaLevel`, the rule of A15, so the kernel needs no water field. Ocean recharges by `recharge_ocean`, every other cell, lakes and rivers included, by `recharge_land`.
- **Clamps.** P is clamped to the registry range [0, 100] of `rainfall`. The column loses η times the clamped P, so the stored rainfall is what is drawn. q is clamped to [0, q_max]; the report only bounds it from above, and the plan requires 0 ≤ q.
- **Order of evaluation.** The P_base term is computed as P_base · (q/q_max), so saturated air gives exactly P_base. The upslope term is added next, then the clamp. All of it is float64 and stored as float32.
- **DF drainage quirk.** With the quirk on, a cell with D < 50 (`kQuirkDrainageMin`) keeps P = P_base exactly. The air still passes over it and loses η·P_base, so shadows carry across such cells. The quirk needs the drainage field; without it the kernel returns `invalid_argument`.
- **Toggle off.** With `OROGRAPHIC_PRECIPITATION` off, the output is a bit-for-bit copy of the base. No drainage is needed, and the optional moisture output is not written.
- **In place.** The output may be the base span. Each cell is read before it is written, so C8 can update `rainfall` in place.
- **Determinism.** Rows are distributed with `core::parallel_for` at the 16-row grain (ADR 0004), and each row is swept serially. The result is the same for any thread count. The kernel uses no transcendental functions, so it is bit-identical across platforms.

## Consequences

- No parameters, fields or golden hashes change in A18.
- The optional moisture output can feed the `moisture` field of P5 later; the classic profile does not store it.
- With the default gain k_o = 0.5 and q_max = 100, one elevation unit of rise per tile adds 50 rainfall units, so most coastal and windward land cells reach the clamp at 100. The defaults are calibrated in A23 together with `RAIN_RANGES`.
- `tests/support/fixtures.hpp` gains `gaussian_ridge`, the ridge fixture of plan 10.2.

## Alternatives considered

- **Wind by latitude (three-cell circulation).** R§7.2 mentions trade winds, westerlies and polar easterlies. That is the physical profile's job (P5, with `wind_u`/`wind_v`). The classic profile follows DF's implicit westerly.
- **Δs = dx_m.** This makes η and E per metre and ties the defaults to the map scale, while the schema already gives them per tile.
- **Lakes recharge like ocean.** Lakes evaporate more than land, but the plan names only ocean and land, and a lake's z is its bed (ADR 0015), so the elevation alone cannot tell lakes apart. It would need the `water` field as an input.
- **The quirk skips the column too.** Cells with D < 50 would neither change P nor deplete q. Then a dry, poorly drained lowland would let the air pass unchanged, which is less physical than depleting by the rain that actually falls.
- **Draw the unclamped P.** This removes more moisture where the lift is large, but the stored field would no longer be what left the column.
