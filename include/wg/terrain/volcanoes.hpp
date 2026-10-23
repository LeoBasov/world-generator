#pragma once

#include <span>
#include <vector>

#include "wg/core/grid.hpp"
#include "wg/core/memory.hpp"
#include "wg/core/status.hpp"
#include "wg/core/vector_layers.hpp"

namespace wg::terrain {

/// Volcanism a volcano needs (R4.5): the top of the volcanism scale.
inline constexpr float kVolcanoVolcanism = 100.0F;

/// Volcano placement of stage C5 (R4.5, ADR 0011): one volcano per 8-connected area of cells
/// with volcanism >= threshold, at the area's cell nearest its centroid (ties go to the lower
/// cell index). The field is clamped by then, so an area is flat and has no highest cell. Every
/// volcano connects to the magma sea. out is replaced; the volcanoes are ordered by the first
/// cell of their area in row-major order. Errors: invalid_argument for an invalid grid, a span
/// without one value per cell or a non-finite threshold; out_of_memory.
[[nodiscard]] core::Status place_volcanoes(
    std::span<const float> volcanism, const core::Grid& grid, float threshold,
    std::vector<core::Volcano>& out,
    core::MemoryResource& resource = core::default_memory_resource()) noexcept;

}  // namespace wg::terrain
