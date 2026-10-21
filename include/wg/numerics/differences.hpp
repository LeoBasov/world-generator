#pragma once

#include <cstdint>
#include <span>

#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"

namespace wg::num {

/// Direction value of a cell without a lower neighbour (pit or flat), see d8_slope.
inline constexpr std::uint8_t kNoDir = 255;

/// Partial derivatives of z in field units per cell (plan section 13.2): central differences
/// (z(x + 1) - z(x - 1)) / 2 inside the grid, one-sided first differences on the border.
/// dzdx grows east (x), dzdy grows south (y). Errors: invalid_argument for an invalid grid or a
/// span without one value per cell.
[[nodiscard]] core::Status central_differences(std::span<const float> z, const core::Grid& grid,
                                               std::span<float> dzdx, std::span<float> dzdy,
                                               core::Executor& exec) noexcept;

/// Steepest-descent D8 slope, dimensionless (m/m): the largest (z_i - z_j) * hu_m / (L_j * dx_m)
/// over the in-grid neighbours j, with L_j = 1 or sqrt(2) cells. Cells without a lower neighbour
/// get slope 0 and direction kNoDir. Ties go to the first direction in D8 order. dir (optional,
/// may be empty) receives the core::Dir8 value of the steepest direction. Errors as for
/// central_differences.
[[nodiscard]] core::Status d8_slope(std::span<const float> z, const core::Grid& grid,
                                    std::span<float> slope, std::span<std::uint8_t> dir,
                                    core::Executor& exec) noexcept;

}  // namespace wg::num
