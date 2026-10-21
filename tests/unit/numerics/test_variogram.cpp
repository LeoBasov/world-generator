#include "wg/numerics/variogram.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/numerics/neighborhood.hpp"

using wg::core::SerialExecutor;
using wg::core::StatusCode;
using wg::num::VariogramPoint;

TEST_CASE("numerics.variogram of a ramp grows with the squared lag along the slope") {
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = wg::test::ramp(grid, 1.0, 0.5);
  SerialExecutor exec;
  const auto along = wg::num::directional_variogram(z, {}, grid, {.max_lag = 8}, exec);
  const auto across =
      wg::num::directional_variogram(z, {}, grid, {.step_x = 0, .step_y = 1, .max_lag = 8}, exec);
  const auto diagonal =
      wg::num::directional_variogram(z, {}, grid, {.step_x = 1, .step_y = -1, .max_lag = 8}, exec);
  REQUIRE(along);
  REQUIRE(across);
  REQUIRE(diagonal);
  REQUIRE(along->size() == 8);
  for (std::size_t k = 0; k < 8; ++k) {
    const auto h = static_cast<std::int32_t>(k + 1);
    CAPTURE(h);
    // (g h)^2 / 2 with g = 0.5.
    const double expected = 0.125 * h * h;
    CHECK((*along)[k].lag == h);
    CHECK((*along)[k].distance == static_cast<double>(h));
    CHECK((*along)[k].gamma == expected);
    CHECK((*along)[k].pairs == static_cast<std::int64_t>(33) * (33 - h));
    CHECK((*across)[k].gamma == 0.0);
    CHECK((*across)[k].pairs == static_cast<std::int64_t>(33) * (33 - h));
    CHECK((*diagonal)[k].gamma == expected);
    CHECK((*diagonal)[k].distance == doctest::Approx(h * wg::num::kSqrt2));
    CHECK((*diagonal)[k].pairs == (std::int64_t{33} - h) * (std::int64_t{33} - h));
  }
}

TEST_CASE("numerics.variogram counts only pairs inside the mask") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = wg::test::ramp(grid, 0.0, 1.0);
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 0);
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < 10; ++x) {
      mask[static_cast<std::size_t>(grid.idx(x, y))] = 1;
    }
  }
  SerialExecutor exec;
  const auto v = wg::num::directional_variogram(z, mask, grid, {.max_lag = 12}, exec);
  REQUIRE(v);
  for (const VariogramPoint& p : *v) {
    CAPTURE(p.lag);
    if (p.lag < 10) {
      CHECK(p.pairs == static_cast<std::int64_t>(17) * (10 - p.lag));
      CHECK(p.gamma == 0.5 * p.lag * p.lag);
    } else {
      CHECK(p.pairs == 0);
      CHECK(std::isnan(p.gamma));
    }
  }
}

TEST_CASE("numerics.variogram is identical for 1, 2, 8 and 16 threads") {
  const wg::core::Grid grid = wg::test::make_grid(129);
  const std::vector<float> z = wg::test::white_noise(grid, 6);
  const std::vector<std::uint8_t> mask = wg::test::random_mask(grid, 0.8, 6);
  const wg::num::VariogramParams params{.step_x = 2, .step_y = 1, .max_lag = 20};
  SerialExecutor serial;
  const auto expected = wg::num::directional_variogram(z, mask, grid, params, serial);
  REQUIRE(expected);
  for (const std::int32_t threads : {1, 2, 8, 16}) {
    CAPTURE(threads);
    auto pool = wg::core::ThreadPool::create(threads);
    REQUIRE(pool);
    const auto got = wg::num::directional_variogram(z, mask, grid, params, **pool);
    REQUIRE(got);
    REQUIRE(got->size() == expected->size());
    for (std::size_t k = 0; k < got->size(); ++k) {
      CHECK((*got)[k].gamma == (*expected)[k].gamma);
      CHECK((*got)[k].pairs == (*expected)[k].pairs);
    }
  }
}

TEST_CASE("numerics.variogram rejects bad parameters") {
  const wg::core::Grid grid = wg::test::make_grid(9);
  const std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  const std::vector<float> short_z(3);
  SerialExecutor exec;
  CHECK(wg::num::directional_variogram(z, {}, grid, {.step_x = 0, .step_y = 0}, exec)
            .error()
            .code == StatusCode::invalid_argument);
  CHECK(wg::num::directional_variogram(z, {}, grid, {.step_x = 10}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::directional_variogram(z, {}, grid, {.max_lag = 0}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::directional_variogram(z, {}, grid, {.max_lag = 10}, exec).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::directional_variogram(short_z, {}, grid, {}, exec).error().code ==
        StatusCode::invalid_argument);
}
