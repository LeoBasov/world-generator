#include "wg/hydro/discharge.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"

using wg::core::Grid;
using wg::core::StatusCode;

TEST_CASE("hydro.area_weights: cell area on land, 0 on ocean") {
  const Grid grid = wg::test::make_grid(17, 250.0);
  const std::vector<float> z = wg::test::ramp(grid, 98.0, 1.0);  // columns 0, 1 ocean
  std::vector<double> w(z.size(), -1.0);
  REQUIRE(wg::hydro::area_weights(z, grid, w).ok());
  CHECK(w[static_cast<std::size_t>(grid.idx(0, 3))] == 0.0);
  CHECK(w[static_cast<std::size_t>(grid.idx(1, 3))] == 0.0);
  CHECK(w[static_cast<std::size_t>(grid.idx(2, 3))] == 62500.0);
  std::vector<double> short_w(3);
  CHECK(wg::hydro::area_weights(z, grid, short_w).code == StatusCode::invalid_argument);
}

TEST_CASE("hydro.runoff_weights: r P a in m3/yr (R6.5)") {
  const Grid grid = wg::test::make_grid(17, 1000.0);
  std::vector<float> z = wg::test::ramp(grid, 99.0, 1.0);  // column 0 ocean
  std::vector<float> rain(z.size(), 50.0F);
  std::vector<float> drain(z.size(), 25.0F);
  std::vector<double> w(z.size());
  REQUIRE(wg::hydro::runoff_weights(z, rain, drain, grid, w).ok());
  // r = 0.75, P_mm = 1500 mm/yr, a = 1e6 m2.
  CHECK(w[static_cast<std::size_t>(grid.idx(3, 3))] == doctest::Approx(0.75 * 1.5 * 1e6));
  CHECK(w[static_cast<std::size_t>(grid.idx(0, 3))] == 0.0);

  // Fully drained or dry cells shed nothing; values outside [0, 100] are clamped.
  const auto i = static_cast<std::size_t>(grid.idx(5, 5));
  drain[i] = 100.0F;
  rain[i + 1] = 0.0F;
  rain[i + 2] = 150.0F;
  drain[i + 2] = -20.0F;
  REQUIRE(wg::hydro::runoff_weights(z, rain, drain, grid, w).ok());
  CHECK(w[i] == 0.0);
  CHECK(w[i + 1] == 0.0);
  CHECK(w[i + 2] == doctest::Approx(1.0 * 3.0 * 1e6));

  rain[i] = std::numeric_limits<float>::quiet_NaN();
  CHECK(wg::hydro::runoff_weights(z, rain, drain, grid, w).code == StatusCode::invalid_argument);
  std::vector<float> short_rain(4);
  CHECK(wg::hydro::runoff_weights(z, short_rain, drain, grid, w).code ==
        StatusCode::invalid_argument);
}
