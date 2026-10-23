#include "wg/terrain/edge_ocean.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/terrain/elevation.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::terrain::EdgeOceans;

namespace {

constexpr std::int32_t kN = 33;

EdgeOceans counted(const std::vector<float>& z, const Grid& grid) {
  auto e = wg::terrain::count_ocean_edges(z, grid);
  REQUIRE(e);
  return *e;
}

}  // namespace

TEST_CASE("terrain.count_ocean_edges counts partial and complete edges") {
  const Grid grid = wg::test::make_grid(kN);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 200.0F);
  CHECK(counted(z, grid).partial == 0);
  CHECK(counted(z, grid).complete == 0);

  // West column completely ocean: complete for west, partial for north and south (corners).
  for (std::int32_t y = 0; y < kN; ++y) {
    z[static_cast<std::size_t>(grid.idx(0, y))] = 50.0F;
  }
  // One ocean cell in the middle of the east edge.
  z[static_cast<std::size_t>(grid.idx(kN - 1, 10))] = 99.9F;
  const EdgeOceans e = counted(z, grid);
  CHECK(e.ocean_cells == std::array<std::int32_t, 4>{1, 1, 1, kN});
  CHECK(e.complete == 1);
  CHECK(e.partial == 4);

  // Exactly sea level is land.
  z[static_cast<std::size_t>(grid.idx(kN - 1, 10))] = wg::terrain::kSeaLevel;
  CHECK(counted(z, grid).ocean_cells[1] == 0);
}

TEST_CASE("terrain.enforce_edge_oceans makes complete edges, most ocean first") {
  const Grid grid = wg::test::make_grid(kN);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 250.0F);
  // The south edge has some ocean already, so it is taken first.
  for (std::int32_t x = 0; x < 5; ++x) {
    z[static_cast<std::size_t>(grid.idx(x, kN - 1))] = 80.0F;
  }
  const std::vector<float> before = z;
  REQUIRE(wg::terrain::enforce_edge_oceans(z, grid, 1, 0, 8.0).ok());
  const EdgeOceans e = counted(z, grid);
  CHECK(e.complete == 1);
  CHECK(e.ocean_cells[2] == kN);
  CHECK(e.ocean_cells[0] == 0);
  // Cells at distance >= width from the south edge are unchanged; nothing is raised.
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const auto k = static_cast<std::size_t>(i);
    CHECK(z[k] <= before[k]);
    if (kN - 1 - grid.y_of(i) >= 8) {
      CHECK(z[k] == before[k]);
    }
  }
  // The cap rises linearly from just below sea level.
  CHECK(z[static_cast<std::size_t>(grid.idx(10, kN - 1 - 4))] ==
        doctest::Approx(99.0 + (0.5 * 301.0)));

  // Four complete edges.
  std::vector<float> all(static_cast<std::size_t>(grid.cells()), 250.0F);
  REQUIRE(wg::terrain::enforce_edge_oceans(all, grid, 4, 4, 4.0).ok());
  CHECK(counted(all, grid).complete == 4);
}

TEST_CASE("terrain.enforce_edge_oceans opens a bay at the lowest edge cell for partial edges") {
  const Grid grid = wg::test::make_grid(kN);
  std::vector<float> z = wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    return 150.0 + std::abs(x - 16) + std::abs(y - 10);
  });
  // The lowest edge cell is (16, 0) on the north edge (160); the west and east edges have their
  // minimum 166 at row 10, the south edge 172.
  REQUIRE(wg::terrain::enforce_edge_oceans(z, grid, 0, 1, 6.0).ok());
  EdgeOceans e = counted(z, grid);
  CHECK(e.partial == 1);
  CHECK(e.ocean_cells[0] > 0);
  CHECK(z[static_cast<std::size_t>(grid.idx(16, 0))] == 99.0F);
  CHECK(z[static_cast<std::size_t>(grid.idx(0, 10))] == 166.0F);

  // Already satisfied: nothing changes.
  const std::vector<float> before = z;
  REQUIRE(wg::terrain::enforce_edge_oceans(z, grid, 0, 1, 6.0).ok());
  CHECK(z == before);

  REQUIRE(wg::terrain::enforce_edge_oceans(z, grid, 0, 4, 6.0).ok());
  e = counted(z, grid);
  CHECK(e.partial == 4);
  CHECK(e.complete == 0);
}

TEST_CASE("terrain.enforce_edge_oceans and count_ocean_edges refuse bad input") {
  const Grid grid = wg::test::make_grid(kN);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 200.0F);
  std::vector<float> short_z(10, 200.0F);
  CHECK(wg::terrain::count_ocean_edges(short_z, grid).error().code == StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_edge_oceans(short_z, grid, 1, 1, 4.0).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_edge_oceans(z, grid, 5, 0, 4.0).code == StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_edge_oceans(z, grid, 0, -1, 4.0).code == StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_edge_oceans(z, grid, 1, 0, 0.5).code == StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_edge_oceans(z, Grid{}, 1, 0, 4.0).code ==
        StatusCode::invalid_argument);
}
