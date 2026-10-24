#pragma once

#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::hydro {

/// Parameters of the epsilon fill.
struct PriorityFloodParams {
  /// Rise epsilon [eu] from a cell to a neighbour the flood raises; must be finite and > 0.
  float epsilon = 1e-3F;
};

// Both fills follow Barnes, Lehman and Mulla (2014). The flood starts from the ocean cells
// (z < terrain::kSeaLevel), which keep their values; a map without ocean starts from its lowest
// cell (ties to the lower index) instead. The map edge is not an outlet. Cells leave the open
// set in (value, cell index) order, so the result is the same on every platform.

/// Plain priority-flood: the water level w_i = max(z_i, w of the cell that reached i), i.e. the
/// lowest surface without closed depressions. Depressions fill flat to their spill elevation;
/// w - z is the lake depth. Errors: invalid_argument for an invalid grid, a span without one
/// value per cell or a non-finite z, out_of_memory.
[[nodiscard]] core::Status fill_depressions(
    std::span<const float> z, const core::Grid& grid, std::span<float> water_level,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Priority-flood with an epsilon gradient: a neighbour n reached from c gets
/// z~_n = z_n if z_n > z~_c, and otherwise the smallest float32 >= z~_c + epsilon (computed in
/// float64). Every cell except the seeds therefore lies strictly above the cell that reached
/// it, so flats and filled depressions drain towards their outlet. z~ >= w >= z. Errors as
/// fill_depressions, plus invalid_argument for an epsilon that is not finite and > 0.
[[nodiscard]] core::Status priority_flood(
    std::span<const float> z, const core::Grid& grid, std::span<float> z_filled,
    const PriorityFloodParams& params = {},
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::hydro
