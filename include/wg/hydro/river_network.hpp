#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/function_ref.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::hydro {

// River network of stage C6 (R6.1, R6.2, R6.5; A17). Rivers follow the D8 receivers of the
// filled surface, so every river is a chain of the one drainage tree that also carries the
// discharge. The conventions are recorded in docs/adr/0015-river-network.md.

/// River network from its sources (R6.1): the union of the receiver chains that start at the
/// sources, each cut before its first ocean (z < terrain::kSeaLevel) or lake cell (lake_id >=
/// 0). A chain that ends at a lake continues from the lake's outlet, so rivers pass through
/// lakes; a chain that reaches a cell of an earlier chain joins it there. Sources on ocean are
/// skipped, and a source in a lake continues from that lake's outlet. The nodes are the river cells
/// in ascending index order, and each node's downstream node is the node of its receiver (-1 at a
/// mouth). A mouth drains into ocean or a lake, or is the land sink of a map without ocean
/// (receiver -1). Only cell and downstream are set; discharge 0, width class 0, order 0 and name -1
/// are left to river_attributes. receivers must come from d8_receivers on a priority_flood surface
/// and lake_id and lakes from label_lakes (or bulge_lakes). Errors: invalid_argument for an invalid
/// grid, spans without one value per cell, a source, receiver, lake id or outlet out of range;
/// out_of_memory.
[[nodiscard]] core::Expected<core::RiverGraph> trace_rivers(
    std::span<const std::int32_t> sources, std::span<const float> z,
    std::span<const std::int32_t> receivers, std::span<const std::int32_t> lake_id,
    std::span<const core::Lake> lakes, const core::Grid& grid,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Checks the river paths (A17 acceptance): a consistent graph whose cells lie in the grid,
/// each at most once, that is free of cycles; every node that is not a mouth drains to the node
/// of its receiver; every mouth drains into ocean or a lake, or is a land sink (receiver -1).
/// Every path from a node downstream is then simple and ends in ocean, a lake or the land sink.
/// Errors: invalid_argument naming the first violation (also for an invalid grid or spans
/// without one value per cell); out_of_memory.
[[nodiscard]] core::Status check_river_graph(
    const core::RiverGraph& graph, std::span<const float> z,
    std::span<const std::int32_t> receivers, std::span<const std::int32_t> lake_id,
    const core::Grid& grid,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Lowest discharge [m3/yr] of the classes stream, minor river, river and major river.
using WidthClassThresholds = std::array<double, 4>;

/// Width classes of the hydraulic geometry w = a_w (Q / kSecondsPerYear)^b [m] (R6.2, R6.5):
/// the discharge at which w reaches each of rivers.width_class_bounds, from det_exp and det_log
/// so that it is the same on every platform. Comparing Q with these thresholds makes the class
/// exactly monotone in Q. Errors: invalid_argument for a negative or non-finite a_w or b, or
/// bounds that are negative, non-finite or not strictly ascending.
[[nodiscard]] core::Expected<WidthClassThresholds> width_class_thresholds(
    const params::RiversParams& params) noexcept;

/// River class of field river_class for discharge q [m3/yr]: 1 (brook) plus the number of
/// thresholds q reaches, so 1 ... 5. NaN counts as 0.
[[nodiscard]] std::uint8_t river_width_class(double q,
                                             const WidthClassThresholds& thresholds) noexcept;

/// One river (stem): from its head down the main branch to its lowest node, a mouth or the
/// confluence where it is a tributary.
struct RiverStem {
  /// Index of the stem; stems are numbered by descending discharge at their lowest node, ties
  /// to the lower cell.
  std::int32_t id = 0;
  std::int64_t head_node = -1;
  std::int64_t lowest_node = -1;
  std::int32_t nodes = 0;
  /// Discharge at the lowest node [m3/yr].
  double discharge_m3_yr = 0.0;
  /// Tributary order of all its nodes.
  std::uint8_t order = 1;

  friend bool operator==(const RiverStem&, const RiverStem&) = default;
};

/// Naming hook (plan A17): returns the name id of a stem, -1 for none.
using RiverNamer = core::FunctionRef<std::int32_t(const RiverStem&)>;

/// Attributes of the river nodes (R6.2, R6.5): discharge Q = discharge[cell] (the float64
/// accumulation of runoff_weights, stored as float32), the width class from
/// width_class_thresholds(params), the tributary order and the river names. At a confluence the
/// upstream node with the largest Q (ties to the lower node index) continues the main stem with
/// the same order; every other upstream node starts a tributary with order + 1 (saturating at
/// 255). Mouths have order 1. Each stem's name id comes from namer, or is the stem id without
/// one. Returns the stems in id order. Errors: invalid_argument for an invalid grid, a span
/// without one value per cell, invalid params or a graph that is inconsistent, has a cell out
/// of range or a cycle; out_of_memory.
[[nodiscard]] core::Expected<std::vector<RiverStem>> river_attributes(
    core::RiverGraph& graph, std::span<const double> discharge, const core::Grid& grid,
    const params::RiversParams& params, const RiverNamer* namer = nullptr) noexcept;

}  // namespace wg::hydro
