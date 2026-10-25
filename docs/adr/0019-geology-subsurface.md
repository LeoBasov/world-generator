# ADR 0019: Geology, subsurface and the stage C10

- Status: accepted
- Date: 2026-10-25
- Work package: A21

## Context

R§10 describes the subsurface as one layered column per geological region, built after the surface is final. From top to bottom:

- 0 to 10+ soil layers, none on mountains;
- sedimentary rock (the first stone layer in about 80 % of cases);
- extrusive rock, which replaces sedimentary rock in volcanic regions and is never in the same column;
- metamorphic rock, never above sedimentary rock;
- intrusive rock, never at the top and always the deepest layer;
- three cavern layers;
- the magma sea.

Aquifers sit in AQUIFER-flagged layers, more often low and near water. About 1 in 20 is heavy, which DF decides by D mod 20 = 7. Minerals are a Poisson process per host layer, E[N_k] = c f_k / Σf_j · V_h / scarcity, with inclusion shapes and nested hosts. R§10.4 proposes the Markov chain with P(extrusive top) = V/100, log-normal thicknesses and soil from production against transport.

Several pieces have existed since A02, A03 and A07:
- the fields `soil_depth`, `geo_region` and `aquifer`;
- `core::GeoColumn` and `core::MineralDeposit` with their layers;
- `params::GeologyParams`;
- the rock, soil and mineral tables (with the ocean, desert and AQUIFER flags).

A21 adds the kernels in `wg::geology` (`include/wg/geology/`) and the classic stage C10 `c10_subsurface`. The report leaves the details below open. Each one changes every world.

## Decision

- **Environment.** Each cell gets a `GeoEnvironment`, decided in this order:
  1. deep ocean below `geology.deep_ocean_elevation` (new, default 50);
  2. shallow ocean below sea level (100);
  3. mountain where the biome is the table's mountain biome;
  4. sand desert where the biome is `DESERT_SAND` (if the table has it);
  5. land otherwise.

  Lakes count as land, because C10 does not read `water`. The environment selects the eligible sedimentary rocks and soils.
- **Geological regions.** A region is an 8-connected component of equal (environment, volcanism band), with band = min(4, ⌊V/20⌋). Regions are numbered 0 … K−1 by their first cell in row-major order, as in A20, and every cell belongs to one. A region keeps its cells ascending, its area and its mean V, summed in cell order in float64. Banding keeps V nearly constant within a region, so the region's column can use its mean.
- **Stone column.** Each region draws from `RngStream(key.derive("column"), region)`:
  - The layer count L is uniform in [`layer_count_min`, `layer_count_max`]. Both now have a schema minimum of 2, because the top cannot be intrusive and the bottom must be.
  - The top is extrusive with p = V̄/100, or always if the region holds a volcano. Otherwise it is sedimentary with `sedimentary_top_probability`, and metamorphic in the remaining cases. The volcano rule makes "always the top layer near volcanoes" hold, which is why C10 also reads the `volcanoes` layer.
  - Each further layer is a uniform choice among its allowed successors: S → {S, M, I}, E → {E, M, I}, M → {M, I}, I → {I, M}.
  - The last layer is always intrusive.
  - S and E appear only in an unbroken run from the top, and S never follows M. So the three invariants hold by construction, and `check_column` verifies them.
- **Rocks.** A layer's rock is uniform among the rocks of its class. Under an ocean, sedimentary layers use only rocks with the matching `shallow_ocean` / `deep_ocean` flag; if there is none, S is not offered. Other classes are not filtered, because the table flags only basalt and gabbro under the deep ocean, and filtering them would leave no metamorphic or intrusive rock at all.
- **Thickness.** Each layer is `log_normal(mu, sigma)` z-levels, as the parameter units say. One z-level is one elevation unit, so thickness_m = z-levels × `grid.hu_m`. The bottom layer is twice a drawn thickness ("the deepest and usually thickest", R§10.1).
- **Soil depth.** The field holds the steady state of ∂h/∂t = P₀ e^(−h/h₀) + κ∇²z:
  - h = h₀ ln(P₀ / (κ C)), with the convexity C = −∇²z. The 5-point Laplacian is converted to 1/m with hu_m / dx_m².
  - Flat and concave cells (C ≤ 0, κ = 0) cannot balance production, so they reach the cap `geology.soil_max_depth` (new, default 10 m).
  - Mountains get 0.
  - `det_exp` and `det_log` keep the result platform-independent.
- **Soil layers.** A region gets no soil layers if it is a mountain, has a mean soil depth of 0, or has no eligible soil. Otherwise, from `RngStream(key.derive("soil"), region)`, it draws n uniform in [1, `soil_max_layers`] soils, each uniform among the soils flagged for its environment. Their thicknesses are uniform shares of the region's mean soil depth, so the column agrees with the field on average.
- **Caverns and the magma sea.** `GeoColumn::cavern_levels` becomes `caverns` (`core::Cavern`: level, depth_m, openness, passage_density, water), and `magma_depth_m` is added.
  - For each level k, the three values are uniform integers in their [MIN, MAX], drawn from `RngStream(key.derive("caverns"), region)`.
  - A level exists if its passage density is positive.
  - Level k lies at soil + stone · (k + 1)/4, and the magma sea starts below the stone.
- **Aquifers.** Only land and sand-desert cells can hold one, and only if the region's column has an AQUIFER soil or rock layer.
  - The chance is (1 − D/100)(1 − clamp((z − 100)/200, 0, 1)). It is highest on low, poorly drained ground near the coast and zero from z = 300 (mountains).
  - The draw is `uniform01(key.derive("aquifer"), cell, 0)`.
  - With `df_heavy_aquifer_quirk`, an aquifer is heavy when ⌊D⌋ mod 20 = 7. Without it, it is heavy with probability 1/20 (draw 1).
- **Minerals.** For each region r, each stone layer, and each mineral environment that names the layer's class or rock (table order), `RngStream(key.derive("minerals"), r)` draws N ~ Poisson(c · f_k/Σf_j · V_h/`MINERAL_SCARCITY`), where:
  - V_h = region cells × layer thickness in z-levels;
  - Σf_j runs over the environments matching that layer;
  - c = `kDepositRate` = 1.

  Each deposit lies on a uniform cell of the region. Environments that name a mineral as host (diamond in kimberlite) add Poisson(f/100) deposits on each host deposit's cell and layer. Nesting is one level deep.

## Consequences

- Two new parameters, and the minimum of the layer counts rises to 2. Every parameter hash changes (ADR 0005). No golden hashes exist yet.
- The `GeoColumn` layout and its JSON change (`caverns`, `magma_depth_m`). This is allowed before 1.0, and old `.wgw` files with geology do not exist.
- C10 needs the data tables (`StageContext::tables`), like C4, C8 and C9. `tests/fixtures/profiles/classic_geology.json` runs C1 to C10.
- A25 can reuse `draw_column` before erosion and read K from the top layer's rock.

## Alternatives considered

- **Geological regions from the biome regions.** Simple, but a forest region spanning a volcanic and a quiet area would get one column. V/100 is then the mean of a mixture, which the band avoids.
- **Uniform regions (Voronoi or blocks).** These ignore volcanism entirely, so extrusive tops would not follow the volcanic areas.
- **Integrating the soil equation over time.** It needs a time span and a step size the report does not give. The steady state with a cap has one extra parameter.
- **A transition matrix as data.** Uniform successors are enough for the invariants and the acceptance tests. A matrix can replace them later without changing the constraints.
- **Filtering every rock class by ocean flags.** It would leave no column under the shallow ocean.
