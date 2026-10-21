#include "wg/core/grid.hpp"

#include <cmath>
#include <string>

namespace wg::core {

namespace {

bool is_power_of_two(std::int32_t v) noexcept { return v > 0 && (v & (v - 1)) == 0; }

bool is_positive_finite(double v) noexcept { return std::isfinite(v) && v > 0.0; }

}  // namespace

Status validate(const Grid& grid) noexcept {
  if (grid.n < kMinGridN || grid.n > kMaxGridN || !is_power_of_two(grid.n - 1)) {
    std::string message = "grid n = " + std::to_string(grid.n) + " is not 2^k + 1 in [";
    message += std::to_string(kMinGridN) + ", " + std::to_string(kMaxGridN) + "]";
    return make_status(StatusCode::invalid_argument, message);
  }
  if (!is_positive_finite(grid.dx_m)) {
    return make_status(StatusCode::invalid_argument, "grid dx_m must be finite and positive");
  }
  if (!is_positive_finite(grid.hu_m)) {
    return make_status(StatusCode::invalid_argument, "grid hu_m must be finite and positive");
  }
  return {};
}

}  // namespace wg::core
