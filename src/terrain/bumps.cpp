#include "wg/terrain/bumps.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "numerics/common.hpp"
#include "wg/core/detmath.hpp"

namespace wg::terrain {

namespace detail = num::detail;

core::Status add_gaussian_bump(std::span<float> z, const core::Grid& grid, std::int64_t centre,
                               double height, double width, float cap) noexcept {
  if (core::Status s = detail::first_error(
          {core::validate(grid),
           detail::check_field(z, grid, "add_gaussian_bump: z must have one value per cell")});
      !s.ok()) {
    return s;
  }
  if (!grid.contains(centre) || !std::isfinite(height) || height < 0.0 || !std::isfinite(width) ||
      width <= 0.0 || !std::isfinite(cap)) {
    return core::make_status(core::StatusCode::invalid_argument,
                             "add_gaussian_bump: bad centre, height, width or cap");
  }
  const double reach = kBumpCutoff * width;
  const auto r = static_cast<std::int32_t>(std::floor(reach));
  const std::int32_t cx = grid.x_of(centre);
  const std::int32_t cy = grid.y_of(centre);
  const double two_w2 = 2.0 * width * width;
  for (std::int32_t y = std::max(0, cy - r); y <= std::min(grid.n - 1, cy + r); ++y) {
    for (std::int32_t x = std::max(0, cx - r); x <= std::min(grid.n - 1, cx + r); ++x) {
      const double ddx = x - cx;
      const double ddy = y - cy;
      const double r2 = (ddx * ddx) + (ddy * ddy);
      if (r2 > reach * reach) {
        continue;
      }
      float& v = z[static_cast<std::size_t>(grid.idx(x, y))];
      const double raised = static_cast<double>(v) + (height * core::det_exp(-r2 / two_w2));
      v = std::max(v, static_cast<float>(std::min(static_cast<double>(cap), raised)));
    }
  }
  return {};
}

}  // namespace wg::terrain
