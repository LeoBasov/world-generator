# ADR 0017: Vegetation and salinity

- Status: accepted
- Date: 2026-10-25
- Work package: A19

## Context

R§8 derives two fields from the others. Vegetation (R§8.1) is the Miami model of net primary productivity (Lieth, 1975) from temperature T in °C and precipitation P in mm/yr:

NPP = min(3000 / (1 + e^(1.315 − 0.119·T)), 3000·(1 − e^(−0.000664·P))),

normalised by 3000 to V_veg ∈ [0, 1] and set to 0 above a tree line or on ice. Rainfall maps to millimetres as P_mm = 30·P. Salinity (R§8.2) is 100 on the ocean and tapers quickly over the land next to it: S(d) = 100·e^(−d/λ) with d a breadth-first or Euclidean distance to the nearest ocean cell and λ of 1–2 tiles, quantised to {0, 33, 66, 100}. Rivers can inherit the salinity class of the cell at their mouth (brackish estuaries).

The parameters (`vegetation.rain_mm_per_index`, `vegetation.tree_line`, `salinity.decay_length`), the fields `vegetation` and `salinity`, the salinity classes (`data/tables/salinity_classes.json`) and `core::Lake::salinity_class` have existed since A02 and A07. A19 adds the kernels in `wg::bio` (`include/wg/bio/vegetation.hpp`, `salinity.hpp`). The report leaves the details below open; each one changes every world.

## Decision

- **Temperature.** The Miami model expects the annual mean, but the stored temperature is the peak-summer value of the game scale (R§5.2). The kernel converts it with `climate::game_to_celsius` and uses it as is, as the plan says. Cold climates therefore get somewhat more vegetation than the annual mean would give.
- **Ice.** Ice is the glacier of R§9.1: game-scale temperature ≤ −5 (`kIceTemperatureMax`) and drainage ≥ 75 (`kIceDrainageMin`). There V_veg = 0. Tundra (T ≤ −5, D < 75) keeps its Miami value, about 0.13 at −5 °C. `compute_vegetation` therefore needs the drainage field.
- **Other bare cells.** V_veg is 0 on ocean (z < `terrain::kSeaLevel`), on lake cells of the optional `water` field and above the tree line (z > `tree_line`, strictly, as the parameter says). River cells keep their land value.
- **Evaluation.** The exponentials use `core::det_exp`, so the values are the same on every platform. NPP is computed in float64, divided by 3000, clamped to the registry range and stored as float32. Each cell depends only on its own inputs.
- **Distance.** d is the exact Euclidean distance in tiles to the nearest ocean cell (`num::edt_squared`, then `std::sqrt`, which is correctly rounded). Ocean is `water == ocean`, which A17 sets for z < 100. With λ = 1.5 an orthogonal neighbour of the ocean (S = 51) is saline, a diagonal one (S = 39) and d = 2 (S = 26) are brackish, and d = 3 (S = 14) is fresh. This matches DF 2019, which sets salinity on the ocean and the tiles next to it. A map without ocean is fresh everywhere.
- **Quantisation.** S goes through `SalinityClasses::quantise`, so only the class values of the table can appear. The decay distance of the acceptance test is `fresh_distance` = λ·ln(100/s₁), with s₁ = 16.5 the lowest brackish salinity: about 2.70 tiles with the defaults. Beyond it every land cell is fresh.
- **Estuaries.** A river node takes the class of its mouth cell (the node with downstream −1) if its channel distance to the mouth, summed over steps of 1 or √2 tiles, is at most `fresh_distance`, and if that class is higher than its own. All mouth classes are read before any cell is raised. Inheritance needs a limit, or every river would be as saline as its mouth. A class that decays along the channel would add nothing, because the straight-line distance never exceeds the channel distance. The fresh distance needs no new parameter: salt reaches as far up a channel as it reaches over land. The rule does not ask where the mouth drains. A river ending in a lake far inland has a fresh mouth and inherits nothing.
- **Lakes.** `assign_lake_salinity` sets `Lake::salinity_class` to the highest class among the lake's cells, so a lake on the coast is brackish or saline (the report's "brackish lake").
- **Determinism.** The distance transform and the per-cell pass run in fixed chunks; the estuary pass is serial over the river graph. The result does not depend on the thread count.

## Consequences

- No parameters, fields, data tables or golden hashes change in A19.
- No stage uses the kernels yet. C4 (first vegetation) also writes the provisional biome, which needs A20, and C8 reads C4's vegetation. Both stages land after A20. C8 will then recompute temperature and rainfall, write vegetation and salinity, and assign the lake classes.
- C4 and C8 pass `drainage` to the vegetation kernel; C8 passes `water` and the river graph to the salinity kernel.

## Alternatives considered

- **Every cell with T ≤ −5 is ice.** This is simpler and needs no drainage, but it leaves tundra bare, while DF and the real world give it sparse vegetation.
- **Ice at 0 °C.** This ignores the report's own freezing threshold and would strip vegetation from the cold climate class.
- **Breadth-first distance.** The report allows it, but D4 or D8 steps make the salt band square or diamond-shaped. The exact EDT already exists (A06).
- **Parameter `salinity.estuary_length`.** It would make the estuary reach independent of λ, but it changes every parameter hash for a detail the report does not quantify.
- **Rivers inherit only when the mouth drains to the ocean.** This needs the receiver of the mouth, which the river graph does not store, and a mouth inland has a fresh class anyway.
