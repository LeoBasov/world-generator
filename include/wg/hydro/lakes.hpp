#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::hydro {

/// Parameters of the lake labelling.
struct LakeParams {
  /// Minimal volume V_min [eu * cells] of a lake (rivers.lake_min_volume); finite and >= 0.
  double min_volume = 10.0;
};

/// Lakes from filled depressions (R6.6, plan 7.1). The candidates are the 8-connected
/// components of cells with water_level > z; within a component the water level is uniform. A
/// component whose volume sum(w - z) (float64, in eu * cells) is at least min_volume becomes a
/// lake; smaller pits stay land and are only routed over. Lakes are numbered 0 ... K-1 in
/// row-major order of their first cell; lake_id is the lake of each cell, -1 elsewhere. Each
/// core::Lake holds its cells in ascending order, the spill elevation w, and as outlet the
/// receiver of its cell with the lowest (z_filled, index), where the filled surface enters the
/// depression; its z is at least the spill elevation. salinity_class stays 0 (set by C8).
/// Inputs: water_level from fill_depressions, z_filled from priority_flood and receivers from
/// d8_receivers on z_filled. Errors: invalid_argument for an invalid grid, spans without one
/// value per cell, a min_volume that is not finite and >= 0, or inputs that do not fit together
/// (w < z, a component with two levels, an outlet inside its lake); out_of_memory.
[[nodiscard]] core::Expected<std::vector<core::Lake>> label_lakes(
    std::span<const float> z, std::span<const float> water_level, std::span<const float> z_filled,
    std::span<const std::int32_t> receivers, const core::Grid& grid, const LakeParams& params,
    std::span<std::int32_t> lake_id,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::hydro
