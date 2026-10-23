#pragma once

#include <cstdint>
#include <span>

#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"

namespace wg::terrain {

/// The bump is cut off beyond this many widths, where exp(-r^2 / 2 l^2) < 3.4e-4.
inline constexpr double kBumpCutoff = 4.0;

/// Gaussian bump of R4.3 and R4.5 (peaks and volcanoes in stage C7): z_i <- max(z_i,
/// min(cap, z_i + height * exp(-r^2 / (2 width^2)))) for every cell within kBumpCutoff * width
/// of cell centre, r in cells. The bump never lowers a cell. exp is core::det_exp, so the
/// result is the same on every platform. Errors: invalid_argument for an invalid grid, a span
/// without one value per cell, a centre outside the grid, a negative or non-finite height, a
/// width that is not finite and positive, or a non-finite cap.
[[nodiscard]] core::Status add_gaussian_bump(std::span<float> z, const core::Grid& grid,
                                             std::int64_t centre, double height, double width,
                                             float cap) noexcept;

}  // namespace wg::terrain
