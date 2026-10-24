# ADR 0013: Filling, routing and lakes

- Status: accepted
- Date: 2026-10-24
- Work package: A15

## Context

A15 adds the hydrology kernels in `wg::hydro` (`include/wg/hydro/`, `src/hydro/`): small-ocean removal, priority-flood filling, D8 receivers, stack order, flow accumulation and lakes.

The sources fix only the outline:
- R§6.1 dries small oceans and bulges lakes out along rivers.
- R§6.6 fills each closed depression to its spill elevation with the priority-flood of Barnes et al. (2014) and raises or converts small ocean components.
- Plan 7.1 adds the ε-gradient on flats and the lake volume threshold V_min.

Stage C6 (`c06_rivers`) also needs the test rivers (A16) and the river network (A17), so A15 ships kernels only and no stage uses them yet. Each open detail below changes every world once C6 exists, so it is recorded here.

## Decision

- **Sea level.** A cell with z < `terrain::kSeaLevel` (100) is ocean, in every kernel. There is no separate ocean mask (plan 6.4 sketched one): the classic and physical profiles share the sea level, and one rule keeps the fill, the receivers and the lakes consistent.
- **Small oceans (R§6.6).**
  - `remove_small_oceans` labels the 8-connected ocean components.
  - It raises every component with fewer than `rivers.small_ocean_min_area` cells to exactly 100, i.e. to land.
  - Components that touch the map edge are kept, so the edge-ocean checks of C3 still hold after C6.
  - Raised cells become depressions or flats, which the fill then handles.
- **Seeds of the flood.**
  - All ocean cells are seeds and keep their values.
  - A map without ocean starts from its single lowest cell (ties to the lower index), so the whole map drains into one closed basin.
  - The map edge is not an outlet: rivers end in the sea or in a lake (R§6, review checklist 10.3).
  - The open set is `num::IndexHeap`, ordered by (value, cell index), so the result does not depend on push order (R1).
- **Two fills.**
  - `fill_depressions` gives the water level w_n = max(z_n, w_c), a flat fill of every depression to its spill elevation.
  - `priority_flood` gives the routing surface z̃: z̃_n = z_n if z_n > z̃_c, otherwise the smallest float32 ≥ z̃_c + ε, the sum taken in float64. A plain float32 sum could round back down to z̃_c.
  - ε = `PriorityFloodParams::epsilon`, 1e-3 by default. It is a kernel parameter, not a world parameter, so the parameter hash is unchanged.
  - z̃ ≥ w ≥ z everywhere. The two passes cost O(n log n) each.
  - The flat w keeps lake levels exact. The ε surface gives every flat and filled cell a strictly lower neighbour.
- **Receivers.**
  - `d8_receivers` gives −1 (a sink) to ocean cells and to cells without a strictly lower neighbour.
  - Every other cell gets its steepest D8 neighbour by (z̃_i − z̃_j) / step length, in float64.
  - On equal drops the first neighbour in `core::kDir8` order wins (ADR 0004).
  - On a surface from `priority_flood` every land cell therefore has a strictly descending path to an ocean sink, or to the lowest cell of a map without ocean.
- **Stack order.**
  - Braun and Willett (2013): the sinks in ascending index order, each followed depth first by its drainage tree.
  - Donors are visited in ascending index order (built with a counting sort).
  - Receivers come before their donors, and each tree is contiguous.
  - A receiver that is neither −1 nor a D8 neighbour, or a cycle, is `invalid_argument`.
- **Accumulation.**
  - `accumulate_flow` sums generic float64 weights in reverse stack order: a_i = w_i + Σ a over the donors.
  - C6 uses w = Δx² on land and 0 on ocean for `flow_accum` (stored as float32).
  - A17 uses w = r·P·a for the discharge.
  - The sum over the sinks equals the sum of the weights. With integer weights the sum is exact.
- **Lakes.**
  - The candidates are the 8-connected components of cells with w > z.
  - **Uniform level.** If a and b are neighbouring wet cells, w_a ≤ max(z_a, w_b) = w_b because z_a < w_a, and symmetrically, so w_a = w_b. The kernel checks this.
  - **Volume.** V = Σ(w − z) in float64, in eu·cells like `rivers.lake_min_volume`. A component with V ≥ V_min is a lake. Smaller pits stay land and are only routed over.
  - **Ids.** Lakes are numbered 0 … K−1 in row-major order of their first cell. `lake_id` is −1 elsewhere, and `Lake::cells` is in ascending order.
  - **Spill elevation.** It is w.
  - **Outlet.** The outlet is the receiver of the lake cell with the lowest (z̃, index), which is where the ε surface entered the depression. Its z is at least the spill elevation and equals it on the spill cell.
  - `salinity_class` stays 0 until C8.
- **Serial kernels.** The flood is sequential by nature, and the other kernels are cheap at N = 257, so none of them takes an executor. Their results are bit-identical on every platform and thread count.

## Consequences

- The tie rules, the seed rule, the ε rule and the lake rules are frozen once C6 runs. Changing them changes every world (`golden/**` branch).
- No parameter or field changes, so the parameter hash and the world format are unchanged.
- A17 assembles C6 in this order:
  1. small oceans;
  2. test rivers and thermal erosion (A16);
  3. fill;
  4. route;
  5. lakes;
  6. lake bulges at river cells (R§6.1, `rivers.lake_bulge_*`), which need the river network;
  7. rivers.

  A17 also writes the `water` classes and decides whether the filled lake surface enters `elevation`.
- The fixtures bowl, V-valley and plateau of plan 10.2 are in `tests/support/fixtures.hpp`.

## Alternatives considered

- **One ε fill for both lakes and routing.** The water level along the flood tree is then only approximately flat, so lake levels and volumes would carry ε errors.
- **Raw nextafter steps instead of ε.** This perturbs the terrain least, but the drops on large filled flats become tiny (one ulp, about 3·10⁻⁵ at 400) and give nearly random steepest-descent choices. With ε the drops are clearly larger and still small against the minimal river drop of C7.
- **Flood parent as receiver.** It is always strictly lower too. Steepest descent matches D8 on unfilled slopes and the plan's routing fixtures.
- **Small oceans converted to lakes (R§6.6 option).** DF dries them out (R§6.1), and raising them keeps the lakes to filled depressions only.
- **Map edge as outlet.** This would drain land off the map, against the requirement that rivers end in sea or lake.
