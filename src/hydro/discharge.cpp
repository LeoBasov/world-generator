#include "wg/hydro/discharge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "numerics/common.hpp"
#include "wg/terrain/elevation.hpp"

namespace wg::hydro {

namespace detail = num::detail;

core::Status area_weights(std::span<const float> z, const core::Grid& grid,
                          std::span<double> weights) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "area_weights: z must have one value per cell"),
           detail::check_field(weights, grid,
                               "area_weights: weights must have one value per cell")});
      !s.ok()) {
    return s;
  }
  const double area = grid.dx_m * grid.dx_m;
  for (std::size_t i = 0; i < z.size(); ++i) {
    weights[i] = z[i] < terrain::kSeaLevel ? 0.0 : area;
  }
  return {};
}

core::Status runoff_weights(std::span<const float> z, std::span<const float> rainfall,
                            std::span<const float> drainage, const core::Grid& grid,
                            std::span<double> weights) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "runoff_weights: z must have one value per cell"),
           detail::check_field(rainfall, grid,
                               "runoff_weights: rainfall must have one value per cell"),
           detail::check_field(drainage, grid,
                               "runoff_weights: drainage must have one value per cell"),
           detail::check_field(weights, grid,
                               "runoff_weights: weights must have one value per cell")});
      !s.ok()) {
    return s;
  }
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (!std::isfinite(rainfall[i]) || !std::isfinite(drainage[i])) {
      return core::make_status(core::StatusCode::invalid_argument,
                               "runoff_weights: rainfall and drainage must be finite");
    }
  }
  // Metres of rain per rainfall unit (P_mm = 30 P) times the cell area.
  const double volume = 30.0 / 1000.0 * grid.dx_m * grid.dx_m;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (z[i] < terrain::kSeaLevel) {
      weights[i] = 0.0;
      continue;
    }
    const double p = std::clamp(static_cast<double>(rainfall[i]), 0.0, 100.0);
    const double r = 1.0 - (std::clamp(static_cast<double>(drainage[i]), 0.0, 100.0) / 100.0);
    weights[i] = r * p * volume;
  }
  return {};
}

}  // namespace wg::hydro
