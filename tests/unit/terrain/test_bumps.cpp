#include "wg/terrain/bumps.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/detmath.hpp"

using wg::core::Grid;
using wg::core::StatusCode;

TEST_CASE("terrain.add_gaussian_bump on the cone fixture") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> before = wg::test::cone(grid, 380.0, 5.0, 16, 16);
  std::vector<float> z = before;
  const std::int64_t centre = grid.idx(16, 16);
  REQUIRE(wg::terrain::add_gaussian_bump(z, grid, centre, 30.0, 3.0, 400.0F).ok());

  CHECK(z[static_cast<std::size_t>(centre)] == 400.0F);  // 410 capped
  const auto at = [&](std::int32_t x, std::int32_t y) {
    return z[static_cast<std::size_t>(grid.idx(x, y))];
  };
  // Radially symmetric: the four cells at the same distance agree.
  CHECK(at(19, 16) == at(13, 16));
  CHECK(at(19, 16) == at(16, 19));
  CHECK(at(19, 16) == at(16, 13));
  CHECK(at(18, 18) == at(14, 14));
  const double expected = static_cast<double>(before[static_cast<std::size_t>(grid.idx(19, 16))]) +
                          (30.0 * wg::core::det_exp(-9.0 / 18.0));
  CHECK(static_cast<double>(at(19, 16)) == doctest::Approx(expected));

  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const auto k = static_cast<std::size_t>(i);
    const double dx = grid.x_of(i) - 16;
    const double dy = grid.y_of(i) - 16;
    CHECK(z[k] >= before[k]);
    CHECK(z[k] <= 400.0F);
    if (std::sqrt((dx * dx) + (dy * dy)) > wg::terrain::kBumpCutoff * 3.0) {
      CHECK(z[k] == before[k]);
    }
  }
}

TEST_CASE("terrain.add_gaussian_bump never lowers a cell above the cap and clips at the border") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 100.0F);
  z[0] = 405.0F;
  REQUIRE(wg::terrain::add_gaussian_bump(z, grid, 0, 50.0, 2.0, 400.0F).ok());
  CHECK(z[0] == 405.0F);
  CHECK(z[1] > 100.0F);
  REQUIRE(wg::terrain::add_gaussian_bump(z, grid, grid.cells() - 1, 0.0, 2.0, 400.0F).ok());
  CHECK(z[static_cast<std::size_t>(grid.cells() - 1)] == 100.0F);
}

TEST_CASE("terrain.add_gaussian_bump refuses bad input") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 100.0F);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(wg::terrain::add_gaussian_bump(z, grid, -1, 1.0, 1.0, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::add_gaussian_bump(z, grid, grid.cells(), 1.0, 1.0, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::add_gaussian_bump(z, grid, 0, -1.0, 1.0, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::add_gaussian_bump(z, grid, 0, nan, 1.0, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::add_gaussian_bump(z, grid, 0, 1.0, 0.0, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::add_gaussian_bump(z, grid, 0, 1.0, 1.0, std::numeric_limits<float>::infinity())
            .code == StatusCode::invalid_argument);
  std::vector<float> short_z(3, 0.0F);
  CHECK(wg::terrain::add_gaussian_bump(short_z, grid, 0, 1.0, 1.0, 400.0F).code ==
        StatusCode::invalid_argument);
}
