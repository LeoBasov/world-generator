#pragma once

#include <cstddef>
#include <cstdint>
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
