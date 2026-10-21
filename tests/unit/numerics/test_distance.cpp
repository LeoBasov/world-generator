#include "wg/numerics/distance.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::SerialExecutor;
using wg::core::StatusCode;
using wg::num::Connectivity;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Minimum squared distance to any seed, by brute force.
std::vector<double> brute_force_edt(const wg::core::Grid& grid,
                                    const std::vector<std::uint8_t>& seeds) {
  std::vector<std::int64_t> seed_cells;
  for (std::size_t i = 0; i < seeds.size(); ++i) {
    if (seeds[i] != 0) {
      seed_cells.push_back(static_cast<std::int64_t>(i));
    }
  }
  std::vector<double> d(seeds.size(), kInf);
  for (std::size_t i = 0; i < d.size(); ++i) {
    const auto ii = static_cast<std::int64_t>(i);
    for (const std::int64_t s : seed_cells) {
      const auto dx = static_cast<double>(grid.x_of(ii) - grid.x_of(s));
      const auto dy = static_cast<double>(grid.y_of(ii) - grid.y_of(s));
      d[i] = std::min(d[i], (dx * dx) + (dy * dy));
    }
  }
  return d;
}

std::vector<double> run_edt(wg::core::Executor& exec, const wg::core::Grid& grid,
                            const std::vector<std::uint8_t>& seeds) {
  std::vector<double> out(seeds.size(), -1.0);
  REQUIRE(wg::num::edt_squared(seeds, grid, out, exec).ok());
  return out;
}

}  // namespace

TEST_CASE("numerics.distance EDT equals brute force") {
  SerialExecutor exec;
  constexpr std::array<double, 5> kDensities = {0.001, 0.005, 0.02, 0.1, 0.5};
  for (const std::int32_t n : {33, 65}) {
    const wg::core::Grid grid = wg::test::make_grid(n);
    for (std::size_t k = 0; k < kDensities.size(); ++k) {
      for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        CAPTURE(n);
        CAPTURE(kDensities[k]);
        CAPTURE(seed);
        const std::vector<std::uint8_t> seeds = wg::test::random_mask(grid, kDensities[k], seed);
        CHECK(run_edt(exec, grid, seeds) == brute_force_edt(grid, seeds));
      }
    }
  }
}

TEST_CASE("numerics.distance EDT without seeds or with every cell a seed") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  SerialExecutor exec;
  const std::vector<std::uint8_t> none(static_cast<std::size_t>(grid.cells()), 0);
  const std::vector<std::uint8_t> all(static_cast<std::size_t>(grid.cells()), 1);
  for (const double d : run_edt(exec, grid, none)) {
    CHECK(d == kInf);
  }
  for (const double d : run_edt(exec, grid, all)) {
    CHECK(d == 0.0);
  }
  // One seed in a corner: the far corner is sqrt(2) * 16 away.
  std::vector<std::uint8_t> corner = none;
  corner[0] = 1;
  std::vector<float> dist(corner.size());
  REQUIRE(wg::num::edt(corner, grid, dist, exec).ok());
  CHECK(dist[0] == 0.0F);
  CHECK(dist[static_cast<std::size_t>(grid.idx(16, 0))] == 16.0F);
  CHECK(dist.back() == static_cast<float>(std::sqrt(512.0)));
  REQUIRE(wg::num::edt(none, grid, dist, exec).ok());
  CHECK(std::isinf(dist[5]));
}

TEST_CASE("numerics.distance EDT is identical for 1, 2, 8 and 16 threads") {
  const wg::core::Grid grid = wg::test::make_grid(129);
  const std::vector<std::uint8_t> seeds = wg::test::random_mask(grid, 0.002, 9);
  SerialExecutor serial;
  const std::vector<double> expected = run_edt(serial, grid, seeds);
  for (const std::int32_t threads : {1, 2, 8, 16}) {
    CAPTURE(threads);
    auto pool = wg::core::ThreadPool::create(threads);
    REQUIRE(pool);
    CHECK(run_edt(**pool, grid, seeds) == expected);
  }
}

TEST_CASE("numerics.distance BFS gives Manhattan and Chebyshev distances") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(grid.cells()), 0);
  seeds[static_cast<std::size_t>(grid.idx(5, 9))] = 1;
  std::vector<std::int32_t> d4(seeds.size());
  std::vector<std::int32_t> d8(seeds.size());
  REQUIRE(wg::num::bfs_distance(seeds, {}, grid, Connectivity::d4, d4).ok());
  REQUIRE(wg::num::bfs_distance(seeds, {}, grid, Connectivity::d8, d8).ok());
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      const auto i = static_cast<std::size_t>(grid.idx(x, y));
      CHECK(d4[i] == std::abs(x - 5) + std::abs(y - 9));
      CHECK(d8[i] == std::max(std::abs(x - 5), std::abs(y - 9)));
    }
  }
}

TEST_CASE("numerics.distance BFS stays on passable cells") {
  const wg::core::Grid grid = wg::test::make_grid(9);
  std::vector<std::uint8_t> seeds(static_cast<std::size_t>(grid.cells()), 0);
  std::vector<std::uint8_t> passable(seeds.size(), 0);
  seeds[0] = 1;
  for (std::int32_t x = 0; x < grid.n; ++x) {
    passable[static_cast<std::size_t>(grid.idx(x, 0))] = 1;  // only the north row
  }
  std::vector<std::int32_t> d(seeds.size());
  REQUIRE(wg::num::bfs_distance(seeds, passable, grid, Connectivity::d8, d).ok());
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      CHECK(d[static_cast<std::size_t>(grid.idx(x, y))] == (y == 0 ? x : wg::num::kUnreached));
    }
  }
}

TEST_CASE("numerics.distance rejects bad arguments") {
  const wg::core::Grid grid = wg::test::make_grid(9);
  const std::vector<std::uint8_t> seeds(static_cast<std::size_t>(grid.cells()), 1);
  const std::vector<std::uint8_t> short_mask(4, 1);
  std::vector<std::int32_t> d(seeds.size());
  std::vector<double> sq(seeds.size());
  std::vector<float> e(seeds.size());
  std::vector<float> short_e(4);
  SerialExecutor exec;
  CHECK(wg::num::bfs_distance(short_mask, {}, grid, Connectivity::d4, d).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::bfs_distance(seeds, short_mask, grid, Connectivity::d4, d).code ==
        StatusCode::invalid_argument);
  CHECK(wg::num::edt_squared(short_mask, grid, sq, exec).code == StatusCode::invalid_argument);
  CHECK(wg::num::edt(seeds, grid, short_e, exec).code == StatusCode::invalid_argument);
  CHECK(wg::num::edt(short_mask, grid, e, exec).code == StatusCode::invalid_argument);

  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::num::bfs_distance(seeds, {}, grid, Connectivity::d4, d, resource).code ==
        StatusCode::out_of_memory);
  CHECK(wg::num::edt_squared(seeds, grid, sq, exec, resource).code == StatusCode::out_of_memory);
  CHECK(wg::num::edt(seeds, grid, e, exec, resource).code == StatusCode::out_of_memory);
}
