#include "wg/terrain/volcanoes.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::core::Volcano;

namespace {

std::vector<Volcano> placed(const std::vector<float>& v, const Grid& grid) {
  std::vector<Volcano> out = {Volcano{.cell = 3}};  // replaced
  REQUIRE(wg::terrain::place_volcanoes(v, grid, wg::terrain::kVolcanoVolcanism, out).ok());
  return out;
}

void set(std::vector<float>& v, const Grid& grid, std::int32_t x, std::int32_t y) {
  v[static_cast<std::size_t>(grid.idx(x, y))] = 100.0F;
}

}  // namespace

TEST_CASE("terrain.place_volcanoes puts one volcano per area at the cell nearest its centroid") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> v(static_cast<std::size_t>(grid.cells()), 40.0F);
  // A 3 x 3 plateau at (2..4, 2..4): centroid (3, 3).
  for (std::int32_t y = 2; y <= 4; ++y) {
    for (std::int32_t x = 2; x <= 4; ++x) {
      set(v, grid, x, y);
    }
  }
  // Two cells joined diagonally (D8) at (10, 1) and (11, 2): centroid (10.5, 1.5), a tie that
  // goes to the lower index (10, 1).
  set(v, grid, 10, 1);
  set(v, grid, 11, 2);
  // A single cell, and a cell just below 100 that does not count.
  set(v, grid, 14, 14);
  v[static_cast<std::size_t>(grid.idx(6, 12))] = 99.99F;

  const std::vector<Volcano> out = placed(v, grid);
  REQUIRE(out.size() == 3);
  // Ordered by the first cell of the area in row-major order.
  CHECK(out[0].cell == grid.idx(10, 1));
  CHECK(out[1].cell == grid.idx(3, 3));
  CHECK(out[2].cell == grid.idx(14, 14));
  for (const Volcano& volcano : out) {
    CHECK(volcano.magma_connected);
  }
}

TEST_CASE("terrain.place_volcanoes finds none below the threshold") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> v(static_cast<std::size_t>(grid.cells()), 99.0F);
  CHECK(placed(v, grid).empty());
}

TEST_CASE("terrain.place_volcanoes refuses bad input") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<Volcano> out;
  const std::vector<float> short_v(5, 100.0F);
  CHECK(wg::terrain::place_volcanoes(short_v, grid, 100.0F, out).code ==
        StatusCode::invalid_argument);
  const std::vector<float> v(static_cast<std::size_t>(grid.cells()), 100.0F);
  CHECK(wg::terrain::place_volcanoes(v, grid, std::numeric_limits<float>::quiet_NaN(), out).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::place_volcanoes(v, Grid{}, 100.0F, out).code == StatusCode::invalid_argument);
}
