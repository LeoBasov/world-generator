#pragma once

#include <cstdint>
#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"

namespace wg::hydro {

/// D8 receivers (field flow_receiver): -1 (a sink) for ocean cells (z < terrain::kSeaLevel)
/// and for cells without a strictly lower neighbour; otherwise the neighbour with the steepest
/// drop (z_i - z_j) / step length, computed in float64, the first in core::kDir8 order on
/// equal drops. On a surface from priority_flood the only land sink is the lowest cell of a map
/// without ocean. Errors: invalid_argument for an invalid grid or spans without one value per
/// cell.
[[nodiscard]] core::Status d8_receivers(std::span<const float> z, const core::Grid& grid,
                                        std::span<std::int32_t> receivers) noexcept;

/// Stack order of Braun and Willett (2013): every sink in ascending index order, each followed
/// depth first by the cells that drain into it, donors in ascending index order. Every
/// receiver comes before its donors, and each drainage tree is one contiguous run. Errors:
/// invalid_argument for an invalid grid, spans without one value per cell, a receiver that is
/// neither -1 nor a D8 neighbour, or a cycle; out_of_memory.
[[nodiscard]] core::Status stack_order(
    std::span<const std::int32_t> receivers, const core::Grid& grid, std::span<std::int32_t> order,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

/// Flow accumulation in float64: a_i = w_i + the sum of a over the donors of i, visiting the
/// cells in reverse stack order. With w = cell area on land and 0 on ocean, a is the upstream
/// drainage area (field flow_accum); with w = r P a it is the discharge (A17). The sum of a over
/// the sinks equals the sum of w. order must come from stack_order for these receivers.
/// Errors: invalid_argument for an invalid grid, spans without one value per cell or an index
/// out of range.
[[nodiscard]] core::Status accumulate_flow(std::span<const std::int32_t> receivers,
                                           std::span<const std::int32_t> order,
                                           std::span<const double> weights, const core::Grid& grid,
                                           std::span<double> accumulation) noexcept;

}  // namespace wg::hydro
