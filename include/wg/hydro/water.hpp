#pragma once

#include <cstdint>
#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::hydro {

/// Values of the field water (schema/field_registry.json).
enum class WaterClass : std::uint8_t { land = 0, ocean = 1, lake = 2, river = 3 };

/// Fields water and river_class of stage C6: water is ocean for z < terrain::kSeaLevel, else
/// lake for lake_id >= 0, else river on a node of rivers, else land; river_class is the width
/// class of the node on river cells and 0 elsewhere. Errors: invalid_argument for an invalid
/// grid, spans without one value per cell, or an inconsistent river graph or one with a cell
/// out of range.
[[nodiscard]] core::Status classify_water(std::span<const float> z,
                                          std::span<const std::int32_t> lake_id,
                                          const core::RiverGraph& rivers, const core::Grid& grid,
                                          std::span<std::uint8_t> water,
                                          std::span<std::uint8_t> river_class) noexcept;

}  // namespace wg::hydro
