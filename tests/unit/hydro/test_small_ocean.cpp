#include "wg/hydro/small_ocean.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"

using wg::core::Grid;
using wg::core::StatusCode;

namespace {

// Land at 200 with a 3 x 3 pond around (8, 8), a diagonal pair (3, 3)-(4, 4) and a pond of two
// cells on the west edge, all at 50.
std::vector<float> ponds(const Grid& grid) {
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 200.0F);
  for (std::int32_t y = 7; y <= 9; ++y) {
    for (std::int32_t x = 7; x <= 9; ++x) {
      z[static_cast<std::size_t>(grid.idx(x, y))] = 50.0F;
    }
  }
  z[static_cast<std::size_t>(grid.idx(3, 3))] = 50.0F;
  z[static_cast<std::size_t>(grid.idx(4, 4))] = 50.0F;
  z[static_cast<std::size_t>(grid.idx(0, 12))] = 50.0F;
  z[static_cast<std::size_t>(grid.idx(0, 13))] = 50.0F;
  return z;
}

float at(const std::vector<float>& z, const Grid& grid, std::int32_t x, std::int32_t y) {
  return z[static_cast<std::size_t>(grid.idx(x, y))];
}

}  // namespace

TEST_CASE(
    "hydro.remove_small_oceans raises interior components below the threshold (A15 "
    "acceptance)") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> before = ponds(grid);

  // min_area 3: the diagonal pair (one D8 component of 2) goes, the 3 x 3 pond stays and the
  // edge pond stays although it is small.
  std::vector<float> z = before;
  wg::core::Expected<std::int32_t> removed = wg::hydro::remove_small_oceans(z, grid, 3);
  REQUIRE(removed.has_value());
  CHECK(*removed == 1);
  CHECK(at(z, grid, 3, 3) == 100.0F);
  CHECK(at(z, grid, 4, 4) == 100.0F);
  CHECK(at(z, grid, 8, 8) == 50.0F);
  CHECK(at(z, grid, 0, 12) == 50.0F);

  // A component of exactly min_area cells stays; one cell more removes the pond.
  z = before;
  removed = wg::hydro::remove_small_oceans(z, grid, 9);
  REQUIRE(removed.has_value());
  CHECK(*removed == 1);
  CHECK(at(z, grid, 8, 8) == 50.0F);
  z = before;
  removed = wg::hydro::remove_small_oceans(z, grid, 10);
  REQUIRE(removed.has_value());
  CHECK(*removed == 2);
  for (std::int32_t y = 7; y <= 9; ++y) {
    for (std::int32_t x = 7; x <= 9; ++x) {
      CHECK(at(z, grid, x, y) == 100.0F);
    }
  }
  CHECK(at(z, grid, 0, 13) == 50.0F);
  // Land is never touched.
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (before[i] >= 100.0F) {
      CHECK(z[i] == before[i]);
    }
  }
}

TEST_CASE("hydro.remove_small_oceans with min_area 0 changes nothing") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = ponds(grid);
  const std::vector<float> before = z;
  wg::core::Expected<std::int32_t> removed = wg::hydro::remove_small_oceans(z, grid, 0);
  REQUIRE(removed.has_value());
  CHECK(*removed == 0);
  CHECK(z == before);
}

TEST_CASE("hydro.remove_small_oceans rejects invalid input") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = ponds(grid);
  CHECK(wg::hydro::remove_small_oceans(z, grid, -1).error().code == StatusCode::invalid_argument);
  std::vector<float> short_z(10, 50.0F);
  CHECK(wg::hydro::remove_small_oceans(short_z, grid, 4).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::remove_small_oceans(z, Grid{}, 4).error().code == StatusCode::invalid_argument);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::hydro::remove_small_oceans(z, grid, 4, resource).error().code ==
        StatusCode::out_of_memory);
}
