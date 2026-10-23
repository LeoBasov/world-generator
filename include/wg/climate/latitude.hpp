#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

#include "wg/core/cancel.hpp"
#include "wg/core/executor.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/status.hpp"
#include "wg/generated/gen_params.hpp"

namespace wg::climate {

// Latitude of the region-tile grid (R5.3, A12): phi = (pi / 2) d_eq(y) / d_max, 0 at the equator
// and pi / 2 at a pole. Row 0 is the north edge. The conventions are recorded in
// docs/adr/0010-classic-seed-stages.md.

/// Latitude phi [rad] of row y of a grid with n rows for the pole option:
///   north: pole at row 0, equator at row n - 1;
///   south: pole at row n - 1, equator at row 0;
///   both:  poles at both edges, equator at the middle row;
///   none:  no poles, phi = 0 everywhere.
/// Requires n >= 2 and 0 <= y < n (n odd for both, which every grid size 2^k + 1 is).
[[nodiscard]] constexpr double latitude_of_row(params::Poles poles, std::int32_t n,
                                               std::int32_t y) noexcept {
  const auto last = static_cast<double>(n - 1);
  const auto row = static_cast<double>(y);
  constexpr double kHalfPi = std::numbers::pi / 2.0;
  switch (poles) {
    case params::Poles::north:
      return kHalfPi * (last - row) / last;
    case params::Poles::south:
      return kHalfPi * row / last;
    case params::Poles::both: {
      const double centre = last / 2.0;
      const double d = row < centre ? centre - row : row - centre;
      return kHalfPi * d / centre;
    }
    case params::Poles::none:
      break;
  }
  return 0.0;
}

/// Writes latitude_of_row for every cell of grid into phi. Errors: invalid_argument for an
/// invalid grid or a span without one value per cell; those of core::parallel_for, cancelled
/// included.
[[nodiscard]] core::Status fill_latitude(params::Poles poles, const core::Grid& grid,
                                         std::span<float> phi, core::Executor& exec,
                                         const core::CancelFlag* cancel = nullptr) noexcept;

/// Latitude term of the temperature model, T_phi = -Delta T sin^2(phi) (R5.3): 0 at the
/// equator, -delta_t at a pole.
[[nodiscard]] inline double latitude_term(double phi, double delta_t) noexcept {
  const double s = std::sin(phi);
  return -delta_t * s * s;
}

}  // namespace wg::climate
