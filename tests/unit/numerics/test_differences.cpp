#include "wg/numerics/differences.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::Dir8;
using wg::core::SerialExecutor;
using wg::core::StatusCode;

TEST_CASE("numerics.differences of a ramp are constant, borders included") {
  const wg::core::Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = wg::test::ramp(grid, 5.0, 0.5);
  std::vector<float> dzdx(z.size());
  std::vector<float> dzdy(z.size());
  SerialExecutor exec;
  REQUIRE(wg::num::central_differences(z, grid, dzdx, dzdy, exec).ok());
  for (std::size_t i = 0; i < z.size(); ++i) {
    CAPTURE(i);
    CHECK(dzdx[i] == 0.5F);
    CHECK(dzdy[i] == 0.0F);
  }
}

TEST_CASE("numerics.differences of a quadratic are central inside, one-sided on the border") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  const auto f = [](std::int32_t x, std::int32_t y) {
    return (static_cast<double>(x) * x) + (2.0 * y * y);
  };
  const std::vector<float> z = wg::test::make_field(grid, f);
  std::vector<float> dzdx(z.size());
  std::vector<float> dzdy(z.size());
  SerialExecutor exec;
  REQUIRE(wg::num::central_differences(z, grid, dzdx, dzdy, exec).ok());
  const std::int32_t n = grid.n;
  for (std::int32_t y = 0; y < n; ++y) {
    for (std::int32_t x = 0; x < n; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      const auto i = static_cast<std::size_t>(grid.idx(x, y));
      // Central: ((x+1)^2 - (x-1)^2) / 2 = 2x; one-sided on the border.
      const double ex = x == 0 ? 1.0 : x == n - 1 ? f(x, y) - f(x - 1, y) : 2.0 * x;
      const double ey = y == 0 ? 2.0 : y == n - 1 ? f(x, y) - f(x, y - 1) : 4.0 * y;
      CHECK(dzdx[i] == static_cast<float>(ex));
      CHECK(dzdy[i] == static_cast<float>(ey));
    }
  }
}

TEST_CASE("numerics.differences D8 slope of a ramp points downhill to the west") {
  const wg::core::Grid grid = wg::test::make_grid(17, 100.0, 2.0);
  const std::vector<float> z = wg::test::ramp(grid, 10.0, 0.5);
  std::vector<float> slope(z.size());
  std::vector<std::uint8_t> dir(z.size());
  SerialExecutor exec;
  REQUIRE(wg::num::d8_slope(z, grid, slope, dir, exec).ok());
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      const auto i = static_cast<std::size_t>(grid.idx(x, y));
      if (x == 0) {
        // No lower neighbour on the west edge.
        CHECK(slope[i] == 0.0F);
        CHECK(dir[i] == wg::num::kNoDir);
      } else {
        // g * hu / dx = 0.5 * 2 / 100.
        CHECK(slope[i] == doctest::Approx(0.01).epsilon(1e-6));
        CHECK(dir[i] == static_cast<std::uint8_t>(Dir8::w));
      }
    }
  }
}

TEST_CASE("numerics.differences D8 slope ties go to the first direction") {
  const wg::core::Grid grid = wg::test::make_grid(5);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 0.0F);
  const auto peak = static_cast<std::size_t>(grid.idx(2, 2));
  z[peak] = 1.0F;
  std::vector<float> slope(z.size());
  std::vector<std::uint8_t> dir(z.size());
  SerialExecutor exec;
  REQUIRE(wg::num::d8_slope(z, grid, slope, dir, exec).ok());
  CHECK(dir[peak] == static_cast<std::uint8_t>(Dir8::e));
  CHECK(slope[peak] == doctest::Approx(0.01));
  // Flat cells have no direction; the direction output is optional.
  CHECK(dir[0] == wg::num::kNoDir);
  std::vector<float> slope_only(z.size());
  REQUIRE(wg::num::d8_slope(z, grid, slope_only, {}, exec).ok());
  CHECK(slope_only == slope);
}

TEST_CASE("numerics.differences are identical on a thread pool") {
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<float> z = wg::test::white_noise(grid, 1);
  std::vector<float> a(z.size());
  std::vector<float> b(z.size());
  std::vector<float> c(z.size());
  std::vector<float> d(z.size());
  SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(8);
  REQUIRE(pool);
  REQUIRE(wg::num::d8_slope(z, grid, a, {}, serial).ok());
  REQUIRE(wg::num::d8_slope(z, grid, b, {}, **pool).ok());
  CHECK(a == b);
  REQUIRE(wg::num::central_differences(z, grid, a, b, serial).ok());
  REQUIRE(wg::num::central_differences(z, grid, c, d, **pool).ok());
  CHECK(a == c);
  CHECK(b == d);
}

TEST_CASE("numerics.differences reject bad arguments") {
  const wg::core::Grid grid = wg::test::make_grid(5);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()));
  std::vector<float> out(z.size());
  std::vector<float> short_out(3);
  std::vector<std::uint8_t> short_dir(3);
  SerialExecutor exec;
  CHECK(wg::num::central_differences(z, grid, short_out, out, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::central_differences(z, wg::test::make_grid(4), out, out, exec).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::d8_slope(z, grid, short_out, {}, exec).code == StatusCode::invalid_argument);
  CHECK(wg::num::d8_slope(z, grid, out, short_dir, exec).code == StatusCode::invalid_argument);
}
