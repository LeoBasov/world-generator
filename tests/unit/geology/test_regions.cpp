#include "wg/geology/regions.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "wg/core/executor.hpp"

using wg::core::Grid;
using wg::core::StatusCode;
using wg::geology::classify_environment;
using wg::geology::EnvironmentParams;
using wg::geology::GeoEnvironment;
using wg::geology::label_geo_regions;
using wg::geology::volcanism_band;
using wg::params::BiomeId;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

std::uint8_t env(GeoEnvironment e) { return static_cast<std::uint8_t>(e); }

}  // namespace

TEST_CASE("geology.volcanism_band splits volcanism into five bands") {
  CHECK(volcanism_band(0.0F) == 0);
  CHECK(volcanism_band(19.99F) == 0);
  CHECK(volcanism_band(20.0F) == 1);
  CHECK(volcanism_band(79.9F) == 3);
  CHECK(volcanism_band(80.0F) == 4);
  CHECK(volcanism_band(100.0F) == 4);
  CHECK(volcanism_band(250.0F) == 4);
  CHECK(volcanism_band(-3.0F) == 0);
  CHECK(volcanism_band(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("geology.classify_environment orders ocean depth, mountain and sand desert") {
  const Grid grid = wg::test::make_grid(3);
  const std::vector<float> z = {10.0F, 49.9F, 50.0F, 99.9F, 100.0F, 300.0F, 150.0F, 150.0F, 20.0F};
  const std::vector<BiomeId> biome = {0, 7, 7, 0, 7, 0, 26, 18, 26};
  std::vector<std::uint8_t> out(9, 99);
  wg::core::SerialExecutor exec;
  const EnvironmentParams p{.deep_ocean_elevation = 50.0,
                            .mountain_biome = 0,
                            .has_sand_desert = true,
                            .sand_desert_biome = 26};
  REQUIRE(classify_environment(z, biome, grid, p, out, exec).ok());
  CHECK(out ==
        std::vector<std::uint8_t>{env(GeoEnvironment::deep_ocean), env(GeoEnvironment::deep_ocean),
                                  env(GeoEnvironment::shallow_ocean),
                                  env(GeoEnvironment::shallow_ocean), env(GeoEnvironment::land),
                                  env(GeoEnvironment::mountain), env(GeoEnvironment::sand_desert),
                                  env(GeoEnvironment::land), env(GeoEnvironment::deep_ocean)});
  EnvironmentParams no_desert = p;
  no_desert.has_sand_desert = false;
  REQUIRE(classify_environment(z, biome, grid, no_desert, out, exec).ok());
  CHECK(out[6] == env(GeoEnvironment::land));
  std::vector<std::uint8_t> short_out(8);
  CHECK(classify_environment(z, biome, grid, p, short_out, exec).code ==
        StatusCode::invalid_argument);
}

TEST_CASE("geology.label_geo_regions joins equal environment and band under D8") {
  const Grid grid = wg::test::make_grid(5);
  // Left: land at V 10 (band 0); right two columns: land at V 30 (band 1); cell (2, 2) mountain.
  std::vector<std::uint8_t> e(u(grid.cells()), env(GeoEnvironment::land));
  std::vector<float> v(u(grid.cells()), 10.0F);
  for (std::int32_t y = 0; y < 5; ++y) {
    v[u(grid.idx(3, y))] = 30.0F;
    v[u(grid.idx(4, y))] = 39.0F;
  }
  e[u(grid.idx(2, 2))] = env(GeoEnvironment::mountain);
  std::vector<std::int32_t> ids(u(grid.cells()), -1);
  auto regions = label_geo_regions(e, v, grid, ids);
  REQUIRE(regions);
  REQUIRE(regions->size() == 3);
  CHECK(ids[0] == 0);
  CHECK(ids[u(grid.idx(3, 0))] == 1);
  CHECK(ids[u(grid.idx(2, 2))] == 2);
  CHECK(ids[u(grid.idx(4, 4))] == 1);
  const auto& r0 = regions->regions[0];
  CHECK(r0.environment == GeoEnvironment::land);
  CHECK(r0.volcanism_band == 0);
  CHECK(r0.area_cells == 14);
  CHECK(r0.mean_volcanism == doctest::Approx(10.0));
  CHECK(regions->regions[1].area_cells == 10);
  CHECK(regions->regions[1].mean_volcanism == doctest::Approx(34.5));
  CHECK(regions->regions[2].environment == GeoEnvironment::mountain);
  // CSR: every cell once, ascending within each region, matching the labels.
  std::int64_t total = 0;
  for (std::size_t r = 0; r < regions->size(); ++r) {
    const auto cells = regions->cells_of(r);
    CHECK(static_cast<std::int64_t>(cells.size()) == regions->regions[r].area_cells);
    for (std::size_t k = 0; k < cells.size(); ++k) {
      CHECK(ids[u(cells[k])] == static_cast<std::int32_t>(r));
      if (k > 0) {
        CHECK(cells[k - 1] < cells[k]);
      }
    }
    total += static_cast<std::int64_t>(cells.size());
  }
  CHECK(total == grid.cells());
}

TEST_CASE("geology.label_geo_regions joins diagonal neighbours and refuses bad input") {
  const Grid grid = wg::test::make_grid(3);
  // A diagonal of ocean through land: one ocean region, two land regions... the land corners
  // touch diagonally across the ocean diagonal too, so land is also one region.
  const std::uint8_t o = env(GeoEnvironment::shallow_ocean);
  const std::uint8_t l = env(GeoEnvironment::land);
  const std::vector<std::uint8_t> e = {o, l, l, l, o, l, l, l, o};
  const std::vector<float> v(9, 0.0F);
  std::vector<std::int32_t> ids(9);
  auto regions = label_geo_regions(e, v, grid, ids);
  REQUIRE(regions);
  CHECK(regions->size() == 2);
  CHECK(ids == std::vector<std::int32_t>{0, 1, 1, 1, 0, 1, 1, 1, 0});
  std::vector<std::uint8_t> bad = e;
  bad[3] = 5;
  auto refused = label_geo_regions(bad, v, grid, ids);
  REQUIRE_FALSE(refused);
  CHECK(refused.error().code == StatusCode::invalid_argument);
  std::vector<std::int32_t> short_ids(8);
  CHECK_FALSE(label_geo_regions(e, v, grid, short_ids));
}
