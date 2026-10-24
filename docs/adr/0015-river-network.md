# ADR 0015: River network and stage C6

- Status: accepted
- Date: 2026-10-24
- Work package: A17

## Context

A17 completes stage C6 (`c06_rivers`). The erosion (A16) and the fill, routing and lake kernels (A15) exist. A17 adds the real rivers, discharge, tributaries, width classes, brooks, the naming hook and `RIVER_MINS`.

The plan lists these items for A17:
- real rivers along channels;
- a forced-to-ocean fallback;
- loop erasure;
- discharge;
- tributaries;
- width classes w = a_w Q^b;
- brooks;
- a naming hook;
- `RIVER_MINS`.

Its acceptance tests:
- every path is simple and ends in ocean or a lake;
- Q never decreases downstream;
- Σ Q at the mouths equals Σ r·P·a over the land that drains to them, with relative error below 10⁻⁹;
- at a confluence, the smaller-Q branch is the tributary;
- the width class is monotone in Q.

ADR 0013 fixed the C6 order (small oceans → erosion → fill → route → lakes → lake bulges → rivers). It left two questions to A17: the `water` classes, and whether the lake surface enters `elevation`.

## Decision

- **Rivers follow the drainage tree.**
  - A river is a chain of `flow_receiver`, the D8 receivers on the priority-flood surface z̃ (ADR 0013).
  - `trace_rivers` builds the network from the sources: the mountain-edge cells after erosion (`find_river_sources`, the cells `RIVER_MINS[1]` counts). Each chain is cut before its first ocean or lake cell.
  - A chain that ends in a lake continues from the lake's outlet, so rivers pass through lakes. A source inside a lake starts at its outlet, and sources on ocean are skipped.
  - Chains merge where they meet, so the network is the union of the chains. The nodes are the river cells in ascending index order, so the result does not depend on the order of the sources.
  - Where z̃ = z, steepest descent on z̃ follows the channels the test rivers cut, which gives rivers along channels. In pits and on flats, z̃ leads to the spill point, which is the forced-to-ocean fallback.
  - The receivers form a forest, so no path can loop and loop erasure is never needed. `check_river_graph` asserts this in C6: every node drains to the node of its receiver, there is no cycle, and every mouth drains into ocean or a lake. On a map without ocean, a mouth can also be the single land sink.
  - Rivers and discharge share one tree, so "simple paths", "Q non-decreasing" and the mass balance hold by construction.
- **Discharge (R6.5).**
  - The runoff of a cell is w = r · P_mm / 1000 · Δx² in m³/yr, with r = 1 − D/100 and P_mm = 30 P. P and D are clamped to [0, 100].
  - Land and lake cells shed runoff; ocean sheds none. Evaporation from lakes is not modelled.
  - `accumulate_flow` sums the runoff in float64 along the receivers. The field `discharge` and `RiverGraph::discharge_m3_yr` store float32, so the 10⁻⁹ mass balance is tested on the float64 sums.
  - `flow_accum` is the same accumulation with w = Δx² on land.
- **Width classes (R6.2).**
  - w = a_w (Q / 31 557 600)^b: Q is converted to m³/s for the formula, otherwise the defaults a_w = 1, b = 0.5 give widths of kilometres.
  - The new parameter `rivers.width_class_bounds` (default [1, 2, 5, 10] m, strictly ascending) holds the lower widths of the classes 2 stream, 3 minor river, 4 river and 5 major river. Every river cell is at least class 1, a brook; 0 in `river_class` means no river.
  - `width_class_thresholds` inverts the formula once into discharges, Q_k = s · exp(ln(w_k / a_w) / b), with `det_exp` and `det_log` so the thresholds are the same on every platform. Each node is classed by comparing Q with them, which makes the class exactly monotone in Q. Constant widths (a_w = 0 or b = 0) get thresholds of 0 or +∞.
  - At N = 257 with the defaults, rivers reach about 90 to 400 m³/s. About 80 % of the river cells are brooks, and class 5 appears on the largest rivers of some worlds.
- **Tributaries.**
  - At a confluence, the upstream node with the largest Q (in float64) continues the main stem; on a tie, the lower node index wins.
  - `order` is the tributary order: 1 on the main stem down to a mouth, and the order of the river it joins + 1 for a tributary, saturating at 255.
  - A stem runs from its head to its mouth or confluence. Stems are numbered by descending Q at their lowest node, ties to the lower cell.
- **Naming hook.** `river_attributes` takes an optional `RiverNamer`, which maps each stem to a name id. Without one, the name id is the stem id. Phase 1 has no name strings; a later naming system plugs in here.
- **Lake bulges (R6.1).**
  - The candidates are the river nodes that are not mouths, with no ocean or lake cell on them or next to them, in node order.
  - `rivers.lake_bulge_count` of them are drawn without replacement, by a partial Fisher–Yates shuffle. Draw k is `uniform_index(key, k, 0, remaining)`, with key = the stage key derived by `"lake_bulge"`.
  - A drawn cell c floods to L = the smallest float32 ≥ z_c + Δh_lake. The lake is c plus every cell that drains to c through the receivers over cells with z < L.
  - Cells next to ocean, another lake or another bulge are neither taken nor passed through, so lakes never touch. A drawn cell already in or next to a bulge is skipped.
  - The outlet is the receiver of c, and the spill elevation is L.
  - Afterwards all lakes are renumbered by their first cell, the rule of ADR 0013, and the network is traced again so that rivers end at the bulges and continue from their outlets.
  - A bulge lake does not need the volume `lake_min_volume`. Its level lies above its outlet, held as if by a dam.
- **`water`:** ocean for z < 100, else lake for `lake_id` ≥ 0, else river for a node of the network, else land.
- **`elevation`** stays the bed (z after small oceans and erosion). The lake surface lives only in `Lake::spill_elevation_eu`, and z̃ is not written back.
- **`RIVER_MINS`.** C6 rejects right after `erode`:
  - `RIVER_MINS[0]` when the sources before the first cycle are fewer than `checks.RIVER_MINS[0]`;
  - `RIVER_MINS[1]` when the sources after the last cycle are fewer than `checks.RIVER_MINS[1]`.
  - There is no repair hook; a new attempt has a new stage key.
- **Serial.** Like A15 and A16, the new kernels take no executor, and their results are bit-identical on every platform and thread count.

## Consequences

- **Cost.** At N = 257 with the defaults (MSVC /O2, dev machine), the new kernels take about 25 ms per world.
  - C6 is dominated by the A16 erosion: about 3 to 4 s for most seeds.
  - Seed 2 takes 25 s. It needs 229 million walker steps instead of the usual 23 to 31 million.
  - A30 has to optimise the walkers if the R6 budget requires it.
- **Frozen once C6 runs.** These rules change every world (`golden/**` branch): the source rule, receiver tracing and lake continuation, the runoff formula, the width thresholds, the main-stem tie rule, the stem numbering, and the bulge draw and flood rule.
- **Parameter hash.** `rivers.width_class_bounds` is a new parameter, so every parameter hash changes (ADR 0005). The fields and the world format are unchanged.
- **Smoothing after C6.** C7 runs after C6 and smooths `elevation`. Afterwards `flow_receiver` may no longer descend on z everywhere, and lake beds may move slightly. Only the rivers are kept descending (`enforce_river_descent`).
- **Test profile.** Until `data/profiles/classic.json` arrives (A23), `tests/fixtures/profiles/classic_rivers.json` runs c01, c02, c03, c05, c06 and c07.

## Alternatives considered

- **Tracing on the raw eroded z, with a z̃ fallback only in pits and explicit loop erasure.** This is closer to the wording of the report. Mixing two descent rules can create loops, though, and then the discharge needs a second, merged receiver field to stay monotone and mass-conserving. It costs more and adds no requirement.
- **Rivers wherever Q exceeds a threshold.** It would put rivers in lowlands without mountains and decouple the network from the sources that `RIVER_MINS` counts (R6.1 starts rivers in the mountains).
- **Strahler order.** It ignores discharge. The acceptance test asks for the smaller-Q branch to be the tributary.
- **`std::pow` per node for the width.** Its last bit differs between platforms (ADR 0002). Thresholds computed once, compared with Q, are exact and monotone.
- **Flooding a bulge to the 8-connected component of z < L around c.** On a descending river this floods downstream without bound. The upstream drainage set is bounded by L and needs no dam cell.
- **Writing the lake surface into `elevation`.** Later stages (C8, C9) read lakes from `water`. A flat surface would also hide the bed that C10 and the export may want.
