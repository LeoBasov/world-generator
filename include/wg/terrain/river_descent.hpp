#pragma once

#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::terrain {

/// Downstream constraint of the final smoothing (plan 7.1, stage C7): visits the river nodes
/// from the mouths upstream (breadth first, node order inside a level) and raises each node's
/// cell to z_i <- max(z_i, z_r(i) + min_drop), r(i) being the cell of the downstream node.
/// A raised value is the smallest float32 >= z_r(i) + min_drop computed in float64, so
/// (double) z_i >= (double) z_r(i) + min_drop holds exactly afterwards. Mouths keep their
/// values. Errors:
/// - invalid_argument for an invalid grid, a span without one value per cell, a negative or
///   non-finite min_drop, an inconsistent graph, a cell outside the grid or used twice, a
///   downstream index out of range, or a cycle;
/// - out_of_range if a raised cell would end above max_z (the cells raised before stay
///   raised, z_i is left as it is);
/// - out_of_memory.
[[nodiscard]] core::Status enforce_river_descent(
    std::span<float> z, const core::Grid& grid, const core::RiverGraph& rivers, double min_drop,
    float max_z, core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::terrain
