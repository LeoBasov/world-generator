#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "wg/core/executor.hpp"
#include "wg/core/expected.hpp"
#include "wg/core/grid.hpp"

namespace wg::num {

/// Lag direction and range of directional_variogram. The lag h steps (h * step_x, h * step_y)
/// cells for h = 1 ... max_lag.
struct VariogramParams {
  std::int32_t step_x = 1;
  std::int32_t step_y = 0;
  std::int32_t max_lag = 16;  ///< >= 1
};

/// Semivariance at one lag.
struct VariogramPoint {
  std::int32_t lag = 0;    ///< h
  double distance = 0.0;   ///< h * |step|, in cells
  double gamma = 0.0;      ///< semivariance; NaN if there is no pair
  std::int64_t pairs = 0;  ///< |P_h|
};

/// Directional variogram of plan section 10.1, gamma(h) = sum over P_h of (z(x + h e) - z(x))^2
/// / (2 |P_h|), where P_h holds the cells x for which x and x + h e lie in the grid and the mask
/// (empty = every cell). Sums are float64 with per-row-chunk partials added in chunk order, so
/// the result is identical for any executor. Errors: invalid_argument for an invalid grid, a
/// span without one value per cell, a zero step or max_lag < 1; out_of_memory; those of
/// core::parallel_for.
[[nodiscard]] core::Expected<std::vector<VariogramPoint>> directional_variogram(
    std::span<const float> z, std::span<const std::uint8_t> mask, const core::Grid& grid,
    const VariogramParams& params, core::Executor& exec) noexcept;

}  // namespace wg::num
