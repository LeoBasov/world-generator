#include "wg/terrain/river_descent.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "support/river_fixture.hpp"

using wg::core::Grid;
using wg::core::RiverGraph;
using wg::core::StatusCode;

namespace {

// Every node lies at least min_drop above its downstream node, compared in float64.
void check_descent(const std::vector<float>& z, const RiverGraph& g, double min_drop) {
  for (std::size_t u = 0; u < g.size(); ++u) {
    const std::int64_t d = g.downstream[u];
    if (d < 0) {
      continue;
    }
    CAPTURE(u);
    CHECK(static_cast<double>(z[static_cast<std::size_t>(g.cell[u])]) >=
          static_cast<double>(z[static_cast<std::size_t>(g.cell[static_cast<std::size_t>(d)])]) +
              min_drop);
  }
}

}  // namespace

TEST_CASE("terrain.enforce_river_descent makes every river cell descend (A13 acceptance)") {
  const Grid grid = wg::test::make_grid(17);
  const RiverGraph g = wg::test::river_fixture(grid);
  // A ramp rising east along the stem, with a dam at (3, 4), a flat stretch and an uphill
  // tributary.
  std::vector<float> z = wg::test::ramp(grid, 300.0, 1.0);
  z[static_cast<std::size_t>(grid.idx(3, 4))] = 330.0F;
  z[static_cast<std::size_t>(grid.idx(7, 4))] = 307.0F;
  z[static_cast<std::size_t>(grid.idx(8, 4))] = 307.0F;
  for (std::int32_t y = 0; y < 4; ++y) {
    z[static_cast<std::size_t>(grid.idx(5, y))] = 290.0F + static_cast<float>(y);
  }
  const std::vector<float> before = z;
  for (const double min_drop : {0.01, 0.0, 0.5}) {
    CAPTURE(min_drop);
    z = before;
    REQUIRE(wg::terrain::enforce_river_descent(z, grid, g, min_drop, 400.0F).ok());
    check_descent(z, g, min_drop);
    // Mouth and cells off the river keep their values; nothing is lowered.
    CHECK(z[static_cast<std::size_t>(g.cell[0])] == before[static_cast<std::size_t>(g.cell[0])]);
    for (std::size_t k = 0; k < z.size(); ++k) {
      CHECK(z[k] >= before[k]);
    }
    CHECK(z[static_cast<std::size_t>(grid.idx(5, 6))] ==
          before[static_cast<std::size_t>(grid.idx(5, 6))]);
  }
  // The dam stays (it already lies above its receiver); its upstream neighbour is raised to
  // the smallest float32 at least 330.01.
  z = before;
  REQUIRE(wg::terrain::enforce_river_descent(z, grid, g, 0.01, 400.0F).ok());
  CHECK(z[static_cast<std::size_t>(grid.idx(3, 4))] == 330.0F);
  const float raised = z[static_cast<std::size_t>(grid.idx(4, 4))];
  CHECK(static_cast<double>(raised) >= 330.01);
  CHECK(static_cast<double>(std::nextafter(raised, 0.0F)) < 330.01);
}

TEST_CASE("terrain.enforce_river_descent with no rivers changes nothing") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::ramp(grid, 300.0, -1.0);
  const std::vector<float> before = z;
  REQUIRE(wg::terrain::enforce_river_descent(z, grid, RiverGraph{}, 0.01, 400.0F).ok());
  CHECK(z == before);
}

TEST_CASE("terrain.enforce_river_descent reports a river that would rise above max_z") {
  const Grid grid = wg::test::make_grid(17);
  const RiverGraph g = wg::test::river_fixture(grid);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 400.0F);
  CHECK(wg::terrain::enforce_river_descent(z, grid, g, 0.01, 400.0F).code ==
        StatusCode::out_of_range);
}

TEST_CASE("terrain.enforce_river_descent refuses malformed graphs") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 100.0F);
  const RiverGraph good = wg::test::river_fixture(grid);
  const auto refused = [&](const RiverGraph& g) {
    std::vector<float> copy = z;
    const bool invalid = wg::terrain::enforce_river_descent(copy, grid, g, 0.01, 400.0F).code ==
                         StatusCode::invalid_argument;
    CHECK(copy == z);
    return invalid;
  };

  RiverGraph inconsistent = good;
  inconsistent.order.pop_back();
  CHECK(refused(inconsistent));
  RiverGraph outside = good;
  outside.cell[3] = grid.cells();
  CHECK(refused(outside));
  RiverGraph twice = good;
  twice.cell[3] = twice.cell[2];
  CHECK(refused(twice));
  RiverGraph bad_index = good;
  bad_index.downstream[2] = static_cast<std::int64_t>(good.size());
  CHECK(refused(bad_index));
  RiverGraph self = good;
  self.downstream[2] = 2;
  CHECK(refused(self));
  RiverGraph cycle = good;
  cycle.downstream[0] = 1;  // 0 -> 1 -> 0, no mouth left
  CHECK(refused(cycle));

  CHECK(wg::terrain::enforce_river_descent(z, grid, good, -0.1, 400.0F).code ==
        StatusCode::invalid_argument);
  CHECK(wg::terrain::enforce_river_descent(z, grid, good, std::numeric_limits<double>::infinity(),
                                           400.0F)
            .code == StatusCode::invalid_argument);
  std::vector<float> short_z(3, 0.0F);
  CHECK(wg::terrain::enforce_river_descent(short_z, grid, good, 0.01, 400.0F).code ==
        StatusCode::invalid_argument);
}
