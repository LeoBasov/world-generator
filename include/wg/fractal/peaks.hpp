#pragma once

#include <span>
#include <vector>

#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::fractal {

/// Elevation a peak needs (R4.2, R4.3): the top of the elevation scale.
inline constexpr float kPeakElevation = 400.0F;

/// Peak candidates of stage C2 (R4.3, ADR 0010) from the elevation before clamping: one peak
/// per 8-connected area of cells with raw_z >= threshold, at the area's highest cell (ties go
/// to the lower cell index). Each peak gets height_eu = threshold, the elevation of its cell
/// after clamping, and radius_cells = radius. out is replaced; the peaks are ordered by the
/// first cell of their area in row-major order. Scratch memory comes from resource.
/// Errors: invalid_argument for an invalid grid, a span without one value per cell or a
/// non-finite threshold or radius; out_of_memory.
[[nodiscard]] core::Status find_peak_candidates(
    std::span<const float> raw_z, const core::Grid& grid, float threshold, float radius,
    std::vector<core::Peak>& out,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::fractal
