# ADR 0012: Temperature model

- Status: accepted
- Date: 2026-10-24
- Work package: A14

## Context

R§5.3 reconstructs DF's temperature as a base field plus corrections:

T = T₀ + T_φ(φ) − Γ·max(0, z − z_sea)·h_u − α·V_veg + δT_P, with T_φ = −ΔT·sin²φ.

A12 already provides the latitude φ and T_φ (`climate::latitude_term`, ADR 0010), and C2 writes T₀ + T_φ. A14 adds the remaining terms as kernels in `wg::climate` (`include/wg/climate/temperature.hpp`). C8 (climate recomputation) will be their first user. The report does not fix the units of the lapse term on the game scale, the order of the terms or how a stage applies them to a temperature that already holds the latitude term, so those are recorded here.

## Decision

- **Game scale.** Temperature is stored in game units (peak summer, R§5.2). A temperature in °C maps to k·c + `celsius_offset`, with k = `temperature.game_per_kelvin` (`celsius_to_game`, `game_to_celsius`). ΔT, α and the rain damping are given in game units directly.
- **Lapse term.** Γ = `temperature.lapse_rate` is in K/km and h_u = `grid.hu_m` in metres per elevation unit. The term is −k·Γ·h_u/1000 per elevation unit above sea level, so the plan's ∂T/∂z = −Γ·h_u (in kelvin) is −k·Γ·h_u/1000 on the game scale. The rate is computed first and then multiplied by max(0, z − 100), with z_sea = `terrain::kSeaLevel`. The term is exactly 0 at and below sea level, so ocean cells keep their base value.
- **Damping terms.** The forest damping is −α·V_veg with α = `temperature.forest_damping`. The wet-cell term is δT_P = −`rain_damping`·P/100, which is linear in P and ≤ 0.
- **Summation.** `compute_temperature` sums in float64 in a fixed order: base, latitude, lapse, vegetation, rain. It clamps the sum to the registry range [−1000, 1000], as C2 does, and stores float32. Each cell depends only on its own inputs, so the result is bit-identical for any thread count.
- **Optional terms.** Every input except the base is optional: an empty span leaves its term out. `out` may alias the base. C8 can therefore update `temperature` in place, passing the C2 field as the base and no latitude, so the latitude term is not counted twice.
- **C2 is unchanged.** The first temperature stays T₀ + T_φ (ADR 0010). The lapse and damping terms come with C8, after the final terrain and vegetation exist.
- **Trigonometry.** The latitude term uses `std::sin`, with the per-toolchain golden hashes of ADR 0010.

## Consequences

- No parameters, fields or golden hashes change in A14.
- Before C8, C4 sees a temperature without the elevation bias that the report's first pass includes. C4 and the provisional biome are corrected when C8 recomputes temperature and vegetation.
- A19 reuses `game_to_celsius` for the Miami model.

## Alternatives considered

- **Lapse term in C2.** This follows the report's "computed twice" more closely. But C2 runs before C3 remaps the elevation, so the bias would follow the raw fractal heights.
- **Lapse term at the end of C3.** This gives C4 an elevation-biased temperature, but it changes C3's outputs and tests for a correction that C8 repeats anyway.
- **Γ in game units per elevation unit.** This removes the conversion, but it ties the parameter to h_u and k instead of the physical 6.5 K/km.
- **Store T₀ as its own field for C8.** C8 could then rebuild T from scratch. It costs a field, and the additive form gives the same result.
