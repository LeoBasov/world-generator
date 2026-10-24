#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "wg/core/grid.hpp"
#include "wg/core/rng.hpp"

namespace wg::test {

// Synthetic fixtures of plan section 10.2 and random inputs for kernel tests. Random values
// come from the counter-based RNG, so every test input is the same on every platform.

/// Valid grid of n cells per axis (n = 2^k + 1).
inline wg::core::Grid make_grid(std::int32_t n, double dx_m = 100.0, double hu_m = 1.0) {
  return wg::core::Grid{.n = n, .dx_m = dx_m, .hu_m = hu_m};
}

/// Field with value f(x, y) in every cell, rounded to float32.
template <class F>
std::vector<float> make_field(const wg::core::Grid& grid, const F& f) {
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      z[static_cast<std::size_t>(grid.idx(x, y))] = static_cast<float>(f(x, y));
    }
  }
  return z;
}

/// Ramp z = z0 + g * x, rising to the east.
inline std::vector<float> ramp(const wg::core::Grid& grid, double z0, double g) {
  return make_field(grid, [=](std::int32_t x, std::int32_t /*y*/) { return z0 + (g * x); });
}

/// Cone z = z0 - g * r with r the distance in cells from (cx, cy).
inline std::vector<float> cone(const wg::core::Grid& grid, double z0, double g, std::int32_t cx,
                               std::int32_t cy) {
  return make_field(grid, [=](std::int32_t x, std::int32_t y) {
    const double ddx = x - cx;
    const double ddy = y - cy;
    return z0 - (g * std::sqrt((ddx * ddx) + (ddy * ddy)));
  });
}

/// Bowl of plan 10.2 around the centre c = (n - 1) / 2, for n >= 33, with r the distance to c:
/// a paraboloid 150 + 0.2 r^2 for r < 10, a rim at 200 for 10 <= r < 12, except a notch at the
/// rim height 190 east of the centre (y = c), and outside a slope 180 - 10 (r - 12) down to
/// an ocean ring of 50 on the map edge. The bowl fills to 190 and spills through the notch.
inline std::vector<float> bowl(const wg::core::Grid& grid) {
  const std::int32_t c = (grid.n - 1) / 2;
  return make_field(grid, [=](std::int32_t x, std::int32_t y) {
    if (x == 0 || y == 0 || x == grid.n - 1 || y == grid.n - 1) {
      return 50.0;
    }
    const double ddx = x - c;
    const double ddy = y - c;
    const double r = std::sqrt((ddx * ddx) + (ddy * ddy));
    if (r < 10.0) {
      return 150.0 + (0.2 * r * r);
    }
    if (r < 12.0) {
      return y == c && x > c ? 190.0 : 200.0;
    }
    return 180.0 - (10.0 * (r - 12.0));
  });
}

/// Closed basin for the test rivers (A16) around the centre c = (n - 1) / 2, for n >= 33, with
/// r the distance to c: a paraboloid 220 + r^2 for r < 6, a closed mountain rim at 320 for
/// 6 <= r < 8, and outside a slope 300 - 12 (r - 8) down to an ocean ring of 50 on the map
/// edge. Without erosion the basin fills to the rim height 320.
inline std::vector<float> closed_basin(const wg::core::Grid& grid) {
  const std::int32_t c = (grid.n - 1) / 2;
  return make_field(grid, [=](std::int32_t x, std::int32_t y) {
    if (x == 0 || y == 0 || x == grid.n - 1 || y == grid.n - 1) {
      return 50.0;
    }
    const double ddx = x - c;
    const double ddy = y - c;
    const double r = std::sqrt((ddx * ddx) + (ddy * ddy));
    if (r < 6.0) {
      return 220.0 + (r * r);
    }
    if (r < 8.0) {
      return 320.0;
    }
    return 300.0 - (12.0 * (r - 8.0));
  });
}

/// V-valley of plan 10.2: two planes z = 100 + side |x - cx| + y meeting in a thalweg along
/// column cx that falls to the north, where row 0 is ocean (50).
inline std::vector<float> v_valley(const wg::core::Grid& grid, std::int32_t cx, double side) {
  return make_field(grid, [=](std::int32_t x, std::int32_t y) {
    return y == 0 ? 50.0 : 100.0 + (side * std::abs(x - cx)) + y;
  });
}

/// Plateau of plan 10.2: a flat top at 300 on the cells at least margin cells from the edge,
/// falling by 60 per cell of Chebyshev distance outside it, with the map edge set to ocean (50).
inline std::vector<float> plateau(const wg::core::Grid& grid, std::int32_t margin) {
  return make_field(grid, [=](std::int32_t x, std::int32_t y) {
    const std::int32_t edge = std::min(std::min(x, y), std::min(grid.n - 1 - x, grid.n - 1 - y));
    if (edge == 0) {
      return 50.0;
    }
    return edge >= margin ? 300.0 : 300.0 - (60.0 * (margin - edge));
  });
}

/// Mask with each cell set with probability density.
inline std::vector<std::uint8_t> random_mask(const wg::core::Grid& grid, double density,
                                             std::uint64_t seed,
                                             std::string_view stream = "test.mask") {
  const wg::core::RngKey key = wg::core::stage_key(seed, stream, 0);
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()));
  for (std::size_t i = 0; i < mask.size(); ++i) {
    mask[i] = static_cast<std::uint8_t>(wg::core::uniform01(key, i, 0) < density ? 1 : 0);
  }
  return mask;
}

/// Uniform white noise in [-0.5, 0.5), variance 1/12.
inline std::vector<float> white_noise(const wg::core::Grid& grid, std::uint64_t seed,
                                      std::string_view stream = "test.noise") {
  const wg::core::RngKey key = wg::core::stage_key(seed, stream, 0);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  for (std::size_t i = 0; i < z.size(); ++i) {
    z[i] = static_cast<float>(wg::core::uniform01(key, i, 0) - 0.5);
  }
  return z;
}

}  // namespace wg::test
