# ADR 0014: Test-river incision and thermal erosion

- Status: accepted
- Date: 2026-10-24
- Work package: A16

## Context

A16 adds the erosion part of stage C6 in `wg::hydro` (`include/wg/hydro/`, `src/hydro/`): the test rivers of R§6.1 and R§6.3, and the cliff (thermal) erosion of R§6.4 behind `PERIODICALLY_ERODE_EXTREMES`.

The sources fix only the outline:
- R§6.1 sends test rivers from the mountain edges for `EROSION_CYCLE_COUNT` cycles. They step to the lowest neighbour, dig where none is lower, and run until they reach the ocean or get stuck.
- R§6.3 gives the walker rule: move to the lowest neighbour j* if z_j* < z_i, otherwise dig z_j* −= ε and move. It claims that this lowers saddles and cuts outlets through closed basins.
- R§6.4 moves c/2 (Δ − Θ) from the higher to the lower cell wherever a neighbour difference Δ exceeds the talus threshold Θ.
- Plan 7.1 sets the sources at z ≥ 300 with an 8-neighbour below 300. It gives each source its own RNG stream and stops walkers at z < 100, at a lake, or after L_max steps.

The parameters already exist in `erosion.*` (`params::ErosionParams`). C6 itself comes with A17, so A16 ships kernels only, as A15 did. They run as step 2 of the C6 order in ADR 0013.

## Decision

- **Erosion lattice.**
  - The driver `erode` first snaps z, and the dig depth, to multiples of q = 2⁻¹⁵ eu (`snap_to_lattice`, moving each value by at most q/2).
  - Every multiple of q below 512 in magnitude is a float32, so digging and thermal transfers are exact.
  - `thermal_erode` works in whole quanta (int32) and rejects values off the lattice.
  - This is what makes the thermal step conserve Σz bit-exactly. The snap is far below any other rounding in the pipeline.
- **Sources.**
  - `find_river_sources` returns the cells with z ≥ `terrain::kMountainElevation` (300, new) and an 8-neighbour below 300, in ascending index order.
  - They are recomputed at the start of every cycle.
  - `ErosionReport` gives the counts before the first and after the last cycle, for `RIVER_MINS` (checked once C6 exists).
- **Walker (R§6.3 reconstruction).**
  - A walker never returns to a cell it visited. The visited set is a caller-owned byte per cell, cleared again on return.
  - At cell i it moves to its lowest unvisited 8-neighbour if that is strictly lower than z_i.
  - Equal lowest neighbours are chosen uniformly with one draw of the source's stream. This is the only use of the RNG.
  - Otherwise i is a pit or a flat. The walker floods the depression from i:
    - It takes unvisited cells in (z, index) order, the water level being the highest z taken so far, until it takes a cell below the level, i.e. beyond the saddle.
    - It digs the highest cell on the flood-tree route from i to that cell (the saddle, the first on the route on ties) down to max(z − ε, 100), and continues from the cell beyond.
    - Cells queued but not flooded are released for the rest of the walk.
  - A move or a flooded cell counts as one step against L_max (`walker_step_limit`).
  - The walker stops:
    - on ocean (z < 100);
    - on a cell of the optional stop mask (lakes; C6 runs the walkers before the fill, so it passes none);
    - at L_max;
    - when stuck, i.e. no unvisited cell is left to move to or to flood.
  - The path, if requested, is a chain of 8-neighbours that includes the flood routes.
- **Digging never creates ocean.** The dig is floored at sea level, so test rivers keep the edge-ocean and small-ocean invariants of C3 and C6.
- **RNG layout (R1).**
  - The walker from source s in cycle c uses `RngStream(key.derive(c), s)`, with `key` C6's stage key.
  - A cycle draws the same numbers whatever the cycle count, so a run of n cycles is a prefix of a run of n + 1.
- **Thermal step (R§6.4).**
  - The transfer is d = c/2 (Δ − Θ), rounded up to the lattice, at least one quantum and at most ⌊Δ/2⌋. Both cells therefore move towards their mean, and the step always ends.
  - Θ applies to all eight neighbours alike. Θ below one quantum counts as one quantum.
  - The cells are relaxed from a FIFO work list. It starts with every cell that has a steep pair, in row-major order. A popped cell checks its neighbours in `core::kDir8` order, and every cell whose value changes is queued again.
  - The step runs until the list is empty, so every neighbour difference ends ≤ Θ.
  - c = 0 changes nothing. `max_transfers` (2⁴⁰) is a safety net that returns `internal`.
- **Thermal frequency.** With `PERIODICALLY_ERODE_EXTREMES`, the thermal step runs after every cycle. The report says only "periodically", and the cost after the first cycle is small (below).
- **Serial.** Walkers within a cycle run in ascending source order, because each one changes z for the next. The work list is sequential too. Results are bit-identical on every platform and thread count, as in ADR 0013.
- **Cancellation.** `erode` polls an optional `CancelFlag` before every cycle.

## Consequences

- **Cost.** Measured at N = 257 on two C2 + C3 elevation fields with the defaults (250 cycles, ε = 1, L_max = 1024, Θ = 30, c = 0.5), MSVC /O2 on the dev machine:
  - The walkers take 2.3 to 3.7 s (18 to 32 million steps).
  - The thermal step adds about 1 s in all: about 0.02 s for the first call and 0.004 s for each call after.
  - Ponded volume drops by about 75 %, and land relief Σ max(0, z − 100) by 1 to 4 %.
  - The walkers dominate C6 and are the place to optimise if the R6 budget (A30) requires it.
- **Frozen once C6 runs.** These rules change every world once C6 exists (`golden/**` branch): the lattice, the source rule, the walker and flood order, the saddle choice, the stream layout, the thermal transfer and work-list order, and the thermal frequency.
- **Unchanged.** No parameter or field changes, so the parameter hash and the world format stay as they are.
- **For A17.** It calls `erode` with C6's stage key after `remove_small_oceans` and before the fill. It reads `sources_before` and `sources_after` for `RIVER_MINS`.

## Alternatives considered

- **The literal R§6.3 rule.** It does not cut outlets. In a pit, the walker digs its lowest neighbour, steps there, steps back into the pit (now the lowest neighbour), digs it, and so on. The pair sinks as a two-cell hole down to the floor, while the rim stays as it is. The closed-basin acceptance test fails, and so does a single-cell pit next to a slope.
- **Self-avoidance alone.** The walker then spirals across the basin floor, digging every cell it climbs onto. It lowers the floor as fast as the rim, so the depth of the basin stays the same.
- **Breaching (dig each step below the previous cell).** One walker then cuts a full-depth channel along a greedy spiral, which is far stronger than ε per pass and gives unnatural spiral trenches.
- **A global fill per walker or per cycle.** It routes correctly but costs O(n log n) per walker. The local flood touches only the depression the walker is in.
- **Full Gauss–Seidel sweeps for the thermal step.** These were the first implementation. They needed about 100 to 300 sweeps per cycle to settle a few local spots and added 15 to 26 s per world.
- **Float32 transfers without a lattice.** z_i − d and z_j + d round independently, so Σz drifts and the acceptance test "conserves Σz exactly" cannot hold.
- **Thermal only every k cycles, or only at the end.** This saves little, since the per-cycle cost is small, and it lets walker channels grow into canyons between calls, which the option exists to prevent.
