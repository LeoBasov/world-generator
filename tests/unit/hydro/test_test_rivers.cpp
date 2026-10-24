#include "wg/hydro/test_rivers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <vector>

#include "support/fixtures.hpp"

using wg::core::Grid;
using wg::core::RngKey;
using wg::core::RngStream;
using wg::core::StatusCode;
using wg::hydro::WalkerParams;
using wg::hydro::WalkerResult;
using wg::hydro::WalkerStop;

namespace {

const RngKey kKey = wg::core::stage_key(7, "test.hydro.walker", 0);

std::size_t at(const Grid& grid, std::int32_t x, std::int32_t y) {
  return static_cast<std::size_t>(grid.idx(x, y));
}

bool adjacent(const Grid& grid, std::int32_t a, std::int32_t b) {
  const std::int32_t ddx = grid.x_of(a) - grid.x_of(b);
  const std::int32_t ddy = grid.y_of(a) - grid.y_of(b);
  return a != b && ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1;
}

// Runs one walker from source with the stream (key, source).
WalkerResult walk(std::vector<float>& z, const Grid& grid, std::int32_t source,
                  const WalkerParams& params, RngKey key = kKey,
                  std::vector<std::int32_t>* path = nullptr,
                  const std::vector<std::uint8_t>& mask = {}) {
  RngStream stream(key, static_cast<std::uint64_t>(source));
  std::vector<std::uint8_t> visited(z.size(), 0);
  wg::core::Expected<WalkerResult> r =
      wg::hydro::run_walker(z, grid, source, params, stream, visited, mask, path);
  REQUIRE(r.has_value());
  // The scratch is clean again.
  CHECK(std::all_of(visited.begin(), visited.end(), [](std::uint8_t v) { return v == 0; }));
  return *r;
}

}  // namespace

TEST_CASE("hydro.snap_to_lattice moves values to the nearest multiple of 2^-15") {
  CHECK(wg::hydro::on_lattice(1.0));
  CHECK(wg::hydro::on_lattice(-0.5));
  CHECK(wg::hydro::on_lattice(511.0));
  CHECK_FALSE(wg::hydro::on_lattice(512.0));
  CHECK_FALSE(wg::hydro::on_lattice(1e-6));
  CHECK_FALSE(wg::hydro::on_lattice(static_cast<double>(0.1F)));
  CHECK_FALSE(wg::hydro::on_lattice(std::numeric_limits<double>::quiet_NaN()));

  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::white_noise(grid, 3);
  for (float& v : z) {
    v = 100.0F + (600.0F * v);  // (-200, 400)
  }
  const std::vector<float> before = z;
  REQUIRE(wg::hydro::snap_to_lattice(z).ok());
  for (std::size_t i = 0; i < z.size(); ++i) {
    CHECK(wg::hydro::on_lattice(static_cast<double>(z[i])));
    CHECK(std::abs(static_cast<double>(z[i]) - static_cast<double>(before[i])) <=
          wg::hydro::kErosionQuantum / 2.0);
  }
  // Snapping is idempotent.
  const std::vector<float> snapped = z;
  REQUIRE(wg::hydro::snap_to_lattice(z).ok());
  CHECK(z == snapped);

  // Invalid values leave the field unchanged.
  z[5] = std::numeric_limits<float>::quiet_NaN();
  z[0] = 0.1F;
  CHECK(wg::hydro::snap_to_lattice(z).code == StatusCode::invalid_argument);
  CHECK(z[0] == 0.1F);
  z[5] = 600.0F;
  CHECK(wg::hydro::snap_to_lattice(z).code == StatusCode::invalid_argument);
}

TEST_CASE("hydro.find_river_sources returns the mountain edge in ascending order") {
  // Plateau: the flat top at 300 starts 4 cells from the edge and falls by 60 per cell outside,
  // so the sources are the ring of top cells at Chebyshev distance 4 from the map edge.
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = wg::test::plateau(grid, 4);
  wg::core::Expected<std::vector<std::int32_t>> sources = wg::hydro::find_river_sources(z, grid);
  REQUIRE(sources.has_value());
  CHECK(sources->size() == 32);  // the ring of a 9 x 9 square
  for (std::size_t k = 0; k < sources->size(); ++k) {
    const std::int32_t i = (*sources)[k];
    const std::int32_t x = grid.x_of(i);
    const std::int32_t y = grid.y_of(i);
    CHECK(std::min(std::min(x, y), std::min(grid.n - 1 - x, grid.n - 1 - y)) == 4);
    if (k > 0) {
      CHECK((*sources)[k - 1] < i);
    }
  }

  // No mountains, no sources.
  const std::vector<float> low = wg::test::ramp(grid, 120.0, 10.0);
  sources = wg::hydro::find_river_sources(low, grid);
  REQUIRE(sources.has_value());
  CHECK(sources->empty());

  // A field entirely above the threshold has no edge either.
  const std::vector<float> high(static_cast<std::size_t>(grid.cells()), 350.0F);
  sources = wg::hydro::find_river_sources(high, grid);
  REQUIRE(sources.has_value());
  CHECK(sources->empty());

  CHECK(wg::hydro::find_river_sources(std::vector<float>(5), grid).error().code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::find_river_sources(z, Grid{}).error().code == StatusCode::invalid_argument);
}

TEST_CASE("hydro.run_walker descends to the ocean without digging") {
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = wg::test::v_valley(grid, 16, 2.0);
  const std::vector<float> before = z;
  std::vector<std::int32_t> path;
  const auto source = static_cast<std::int32_t>(grid.idx(25, 30));
  const WalkerResult r = walk(z, grid, source, WalkerParams{}, kKey, &path);
  CHECK(r.stop == WalkerStop::ocean);
  CHECK(grid.y_of(r.end) == 0);
  CHECK(z == before);
  REQUIRE(path.size() == static_cast<std::size_t>(r.steps) + 1);
  CHECK(path.front() == source);
  CHECK(path.back() == r.end);
  for (std::size_t k = 1; k < path.size(); ++k) {
    CHECK(adjacent(grid, path[k - 1], path[k]));
    CHECK(z[static_cast<std::size_t>(path[k])] < z[static_cast<std::size_t>(path[k - 1])]);
  }
}

TEST_CASE("hydro.run_walker digs one neighbour to leave a pit") {
  // A ramp rising east from an ocean column, with a single-cell pit at (8, 8): the walker digs
  // its lowest neighbour by exactly the dig depth, then runs down to the ocean.
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::ramp(grid, 120.0, 5.0);
  for (std::int32_t y = 0; y < grid.n; ++y) {
    z[at(grid, 0, y)] = 50.0F;
  }
  z[at(grid, 8, 8)] = 130.0F;
  const std::vector<float> before = z;
  const WalkerResult r =
      walk(z, grid, static_cast<std::int32_t>(grid.idx(8, 8)), WalkerParams{.dig_depth = 1.0});
  CHECK(r.stop == WalkerStop::ocean);
  CHECK(grid.x_of(r.end) == 0);
  std::int32_t changed = 0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (z[i] != before[i]) {
      ++changed;
      CHECK(grid.x_of(static_cast<std::int64_t>(i)) == 7);
      CHECK(z[i] == before[i] - 1.0F);
    }
  }
  CHECK(changed == 1);
}

TEST_CASE("hydro.run_walker stops at the step limit, the stop mask and when stuck") {
  const Grid grid = wg::test::make_grid(33);
  const auto source = static_cast<std::int32_t>(grid.idx(25, 30));

  SUBCASE("step limit") {
    std::vector<float> z = wg::test::v_valley(grid, 16, 2.0);
    std::vector<std::int32_t> path;
    WalkerResult r = walk(z, grid, source, WalkerParams{.step_limit = 3}, kKey, &path);
    CHECK(r.stop == WalkerStop::step_limit);
    CHECK(r.steps == 3);
    CHECK(path.size() == 4);
    CHECK(r.end == path.back());
    r = walk(z, grid, source, WalkerParams{.step_limit = 0});
    CHECK(r.stop == WalkerStop::step_limit);
    CHECK(r.steps == 0);
    CHECK(r.end == source);
  }

  SUBCASE("stop mask") {
    std::vector<float> z = wg::test::v_valley(grid, 16, 2.0);
    std::vector<std::int32_t> path;
    const WalkerResult free_walk = walk(z, grid, source, WalkerParams{}, kKey, &path);
    REQUIRE(free_walk.steps > 4);
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(grid.cells()), 0);
    mask[static_cast<std::size_t>(path[4])] = 1;
    const WalkerResult r = walk(z, grid, source, WalkerParams{}, kKey, nullptr, mask);
    CHECK(r.stop == WalkerStop::mask);
    CHECK(r.end == path[4]);
    CHECK(r.steps == 4);
  }

  SUBCASE("stuck when every neighbour was visited") {
    // A 3 x 3 map without ocean: the walker never revisits a cell, so it runs out of
    // neighbours after at most eight steps.
    const Grid small = wg::test::make_grid(3);
    std::vector<float> z(9, 200.0F);
    std::vector<std::int32_t> path;
    const WalkerResult r = walk(z, small, 4, WalkerParams{}, kKey, &path);
    CHECK(r.stop == WalkerStop::stuck);
    CHECK(r.steps <= 8);
    std::vector<std::int32_t> sorted = path;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
  }

  SUBCASE("without a dig depth nothing changes") {
    std::vector<float> z = wg::test::closed_basin(grid);
    const std::vector<float> before = z;
    walk(z, grid, static_cast<std::int32_t>(grid.idx(16, 16)), WalkerParams{.dig_depth = 0.0});
    CHECK(z == before);
  }

  SUBCASE("digging never creates ocean") {
    std::vector<float> z = wg::test::closed_basin(grid);
    const std::vector<float> before = z;
    walk(z, grid, static_cast<std::int32_t>(grid.idx(16, 16)), WalkerParams{.dig_depth = 1000.0});
    std::int32_t at_sea_level = 0;
    for (std::size_t i = 0; i < z.size(); ++i) {
      if (before[i] >= 100.0F) {
        CHECK(z[i] >= 100.0F);
      }
      at_sea_level += z[i] == 100.0F ? 1 : 0;
    }
    CHECK(at_sea_level == 1);
  }
}

TEST_CASE("hydro.run_walker floods a basin and digs its saddle") {
  // From the bottom of the closed basin the walker floods up to the rim at 320, digs the rim
  // cell on its route by the dig depth, and runs down the outer slope to the ocean.
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> z = wg::test::closed_basin(grid);
  const std::vector<float> before = z;
  std::vector<std::int32_t> path;
  const WalkerResult r = walk(z, grid, static_cast<std::int32_t>(grid.idx(16, 16)),
                              WalkerParams{.dig_depth = 2.0}, kKey, &path);
  CHECK(r.stop == WalkerStop::ocean);
  CHECK(grid.is_border(r.end));
  std::int32_t changed = 0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    if (z[i] != before[i]) {
      ++changed;
      CHECK(before[i] == 320.0F);
      CHECK(z[i] == 318.0F);
      CHECK(std::find(path.begin(), path.end(), static_cast<std::int32_t>(i)) != path.end());
    }
  }
  CHECK(changed == 1);
  for (std::size_t k = 1; k < path.size(); ++k) {
    CHECK(adjacent(grid, path[k - 1], path[k]));
  }
  // Every cell of the basin was flooded, so the steps exceed the moves.
  CHECK(static_cast<std::size_t>(r.steps) > path.size());

  // A step limit inside the flood stops the walker at the pit, and nothing is dug.
  std::vector<float> limited = before;
  const WalkerResult stopped = walk(limited, grid, static_cast<std::int32_t>(grid.idx(16, 16)),
                                    WalkerParams{.step_limit = 20});
  CHECK(stopped.stop == WalkerStop::step_limit);
  CHECK(stopped.steps == 20);
  CHECK(stopped.end == static_cast<std::int32_t>(grid.idx(16, 16)));
  CHECK(limited == before);
}

TEST_CASE("hydro.run_walker paths are reproducible per source (A16 acceptance)") {
  // Random relief with an ocean ring, and a flat plateau where every step is a tie.
  const Grid grid = wg::test::make_grid(33);
  std::vector<float> relief = wg::test::white_noise(grid, 11, "test.hydro.walker_relief");
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      float& v = relief[at(grid, x, y)];
      v = grid.is_border(grid.idx(x, y)) ? 50.0F : 200.0F + (100.0F * v);
    }
  }
  const std::vector<float> plateau = wg::test::plateau(grid, 8);
  for (const std::vector<float>* field :
       std::initializer_list<const std::vector<float>*>{&relief, &plateau}) {
    const auto source = static_cast<std::int32_t>(grid.idx(16, 16));
    std::vector<float> z1 = *field;
    std::vector<float> z2 = *field;
    std::vector<std::int32_t> p1;
    std::vector<std::int32_t> p2;
    const WalkerResult r1 = walk(z1, grid, source, WalkerParams{}, kKey, &p1);
    const WalkerResult r2 = walk(z2, grid, source, WalkerParams{}, kKey, &p2);
    CHECK(p1 == p2);
    CHECK(z1 == z2);
    CHECK(r1.end == r2.end);
    CHECK(r1.steps == r2.steps);
    for (std::size_t k = 1; k < p1.size(); ++k) {
      CHECK(adjacent(grid, p1[k - 1], p1[k]));
    }
  }

  // On the plateau the ties follow the stream: other keys give other paths.
  const auto source = static_cast<std::int32_t>(grid.idx(16, 16));
  std::vector<std::int32_t> reference;
  std::vector<float> z = plateau;
  walk(z, grid, source, WalkerParams{}, kKey, &reference);
  std::int32_t different = 0;
  for (std::uint64_t salt = 1; salt <= 4; ++salt) {
    std::vector<std::int32_t> other;
    z = plateau;
    walk(z, grid, source, WalkerParams{}, kKey.derive(salt), &other);
    different += other != reference ? 1 : 0;
  }
  CHECK(different >= 3);
}

TEST_CASE("hydro.run_walker rejects invalid arguments") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = wg::test::ramp(grid, 120.0, 5.0);
  RngStream stream(kKey, 0);
  std::vector<std::uint8_t> visited(z.size(), 0);
  const WalkerParams ok;
  const auto code = [&](std::span<float> zs, const Grid& g, std::int32_t source,
                        const WalkerParams& p, std::span<std::uint8_t> scratch,
                        std::span<const std::uint8_t> mask = {}) {
    return wg::hydro::run_walker(zs, g, source, p, stream, scratch, mask).error().code;
  };
  CHECK(code(z, Grid{}, 0, ok, visited) == StatusCode::invalid_argument);
  std::vector<float> short_z(10);
  CHECK(code(short_z, grid, 0, ok, visited) == StatusCode::invalid_argument);
  std::vector<std::uint8_t> short_visited(10, 0);
  CHECK(code(z, grid, 0, ok, short_visited) == StatusCode::invalid_argument);
  CHECK(code(z, grid, -1, ok, visited) == StatusCode::invalid_argument);
  CHECK(code(z, grid, 17 * 17, ok, visited) == StatusCode::invalid_argument);
  CHECK(code(z, grid, 0, WalkerParams{.dig_depth = -1.0}, visited) == StatusCode::invalid_argument);
  CHECK(code(z, grid, 0, WalkerParams{.dig_depth = std::numeric_limits<double>::infinity()},
             visited) == StatusCode::invalid_argument);
  CHECK(code(z, grid, 0, WalkerParams{.step_limit = -1}, visited) == StatusCode::invalid_argument);
  const std::vector<std::uint8_t> mask(3, 0);
  CHECK(code(z, grid, 0, ok, visited, mask) == StatusCode::invalid_argument);
}
