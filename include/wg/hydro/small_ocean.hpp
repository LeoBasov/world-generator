#pragma once

#include <cstdint>
#include <span>

#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"

namespace wg::hydro {

// Hydrology kernels of R6.6 (A15): small-ocean removal, priority-flood filling, D8 routing and
// lakes. The conventions are recorded in docs/adr/0013-filling-routing-lakes.md.

/// Dries out small oceans (R6.1, R6.6): labels the 8-connected components of ocean cells
/// (z < terrain::kSeaLevel) and raises every component with fewer than min_area cells that
/// touches no map edge to exactly kSeaLevel. Components on the edge are kept, so the edge-ocean
/// checks of stage C3 still hold. Returns the number of components removed; min_area 0 changes
/// nothing. Errors: invalid_argument for an invalid grid, a span without one value per cell or a
/// negative min_area, out_of_memory.
[[nodiscard]] core::Expected<std::int32_t> remove_small_oceans(
    std::span<float> z, const core::Grid& grid, std::int32_t min_area,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::hydro
