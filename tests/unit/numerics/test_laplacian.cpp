#include "wg/numerics/laplacian.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::SerialExecutor;
using wg::core::StatusCode;

namespace {

// z = 0.5 x^2 + 1.5 y^2 + 0.25 x y + 2 x - 3 y + 7: exact in float32 on a 33 x 33 grid, with
// Laplacian 2 * 0.5 + 2 * 1.5 = 4.
std::vector<float> quadratic(const wg::core::Grid& grid) {
  return wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    const double xd = x;
    const double yd = y;
    return (0.5 * xd * xd) + (1.5 * yd * yd) + (0.25 * xd * yd) + (2.0 * xd) - (3.0 * yd) + 7.0;
  });
}

// Rectangle [x0, x1) x [y0, y1) as a mask.
std::vector<std::uint8_t> rectangle(const wg::core::Grid& grid, std::int32_t x0, std::int32_t y0,
                                    std::int32_t x1, std::int32_t y1) {
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 0);
  for (std::int32_t y = y0; y < y1; ++y) {
    for (std::int32_t x = x0; x < x1; ++x) {
      mask[static_cast<std::size_t>(grid.idx(x, y))] = 1;
    }
  }
  return mask;
}

double masked_sum(const std::vector<float>& z, const std::vector<std::uint8_t>& mask) {
  double s = 0.0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (mask[i] != 0) {
      s += z[i];
    }
  }
  return s;
}

double masked_variance(const std::vector<float>& z, const std::vector<std::uint8_t>& mask) {
  double s = 0.0;
  double s2 = 0.0;
  double m = 0.0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (mask[i] != 0) {
      s += z[i];
      s2 += static_cast<double>(z[i]) * z[i];
      m += 1.0;
    }
  }
  return (s2 / m) - ((s / m) * (s / m));
}

}  // namespace

TEST_CASE("numerics.laplacian of a quadratic is constant") {
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = quadratic(grid);
  std::vector<float> out(z.size());
  SerialExecutor exec;
  REQUIRE(wg::num::laplacian(z, {}, grid, out, exec).ok());
  for (std::int32_t y = 1; y < grid.n - 1; ++y) {
    for (std::int32_t x = 1; x < grid.n - 1; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      CHECK(out[static_cast<std::size_t>(grid.idx(x, y))] == 4.0F);
    }
  }
}

TEST_CASE("numerics.laplacian uses zero flux across the mask and grid edges") {
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = quadratic(grid);
  const std::vector<std::uint8_t> mask = rectangle(grid, 0, 4, 20, 30);
  std::vector<float> out(z.size(), -1.0F);
  SerialExecutor exec;
  REQUIRE(wg::num::laplacian(z, mask, grid, out, exec).ok());
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      const std::int64_t i = grid.idx(x, y);
      double expected = 0.0;
      if (mask[static_cast<std::size_t>(i)] != 0) {
        for (const wg::core::Dir8 d : wg::core::kDir4) {
          const std::int64_t j = grid.neighbor(i, d);
          if (j != wg::core::kNoCell && mask[static_cast<std::size_t>(j)] != 0) {
            expected += static_cast<double>(z[static_cast<std::size_t>(j)]) -
                        z[static_cast<std::size_t>(i)];
          }
        }
      }
      CHECK(out[static_cast<std::size_t>(i)] == static_cast<float>(expected));
    }
  }
  // Inside the mask, away from its edge, the quadratic still gives 4.
  CHECK(out[static_cast<std::size_t>(grid.idx(10, 10))] == 4.0F);
}

TEST_CASE("numerics.laplacian smoothing conserves the mask sum and lowers the variance") {
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<std::uint8_t> mask = rectangle(grid, 3, 5, 60, 50);
  const std::vector<float> z0 = wg::test::white_noise(grid, 2);
  std::vector<float> z = z0;
  SerialExecutor exec;
  const wg::num::SmoothParams params{.alpha = 0.25, .iterations = 20};
  REQUIRE(wg::num::laplacian_smooth(z, mask, grid, params, exec).ok());
  const double before = masked_sum(z0, mask);
  const double after = masked_sum(z, mask);
  // Exact in real arithmetic; float32 storage rounds each step.
  CHECK(std::abs(after - before) <= 1e-3);
  CHECK(masked_variance(z, mask) < 0.2 * masked_variance(z0, mask));
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (mask[i] == 0) {
      CHECK(z[i] == z0[i]);
    }
  }

  // The same result on a thread pool, bit for bit.
  auto pool = wg::core::ThreadPool::create(8);
  REQUIRE(pool);
  std::vector<float> zp = z0;
  REQUIRE(wg::num::laplacian_smooth(zp, mask, grid, params, **pool).ok());
  CHECK(zp == z);
}

TEST_CASE("numerics.laplacian smoothing validates its parameters") {
  const wg::core::Grid grid = wg::test::make_grid(9);
  std::vector<float> z = wg::test::white_noise(grid, 3);
  const std::vector<float> z0 = z;
  SerialExecutor exec;
  CHECK(wg::num::laplacian_smooth(z, {}, grid, {.alpha = 0.3, .iterations = 1}, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::laplacian_smooth(z, {}, grid, {.alpha = 0.0, .iterations = 1}, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::laplacian_smooth(z, {}, grid, {.alpha = 0.1, .iterations = -1}, exec).code ==
        StatusCode::invalid_argument);
  REQUIRE(wg::num::laplacian_smooth(z, {}, grid, {.alpha = 0.1, .iterations = 0}, exec).ok());
  CHECK(z == z0);

  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::laplacian_smooth(z, {}, grid, {}, exec, resource).code ==
        StatusCode::out_of_memory);

  std::vector<float> out(3);
  const std::vector<std::uint8_t> short_mask(3, 1);
  CHECK(wg::num::laplacian(z, {}, grid, out, exec).code == StatusCode::invalid_argument);
  CHECK(wg::num::laplacian(z, short_mask, grid, z, exec).code == StatusCode::invalid_argument);
}
