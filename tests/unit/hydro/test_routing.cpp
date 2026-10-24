#include "wg/hydro/routing.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/hydro/priority_flood.hpp"

using wg::core::Grid;
using wg::core::StatusCode;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

// Filled surface and receivers of z.
struct Routed {
  std::vector<float> filled;
  std::vector<std::int32_t> receivers;
};

Routed route(const std::vector<float>& z, const Grid& grid) {
  Routed r{std::vector<float>(z.size()), std::vector<std::int32_t>(z.size())};
  REQUIRE(wg::hydro::priority_flood(z, grid, r.filled).ok());
  REQUIRE(wg::hydro::d8_receivers(r.filled, grid, r.receivers).ok());
  return r;
}

// Upstream area in cells (unit weights on land, 0 on ocean).
std::vector<double> land_accumulation(const std::vector<float>& z, const Routed& r,
                                      const Grid& grid) {
  std::vector<std::int32_t> order(z.size());
  REQUIRE(wg::hydro::stack_order(r.receivers, grid, order).ok());
  std::vector<double> weights(z.size());
  for (std::size_t i = 0; i < z.size(); ++i) {
    weights[i] = z[i] < 100.0F ? 0.0 : 1.0;
  }
  std::vector<double> a(z.size());
  REQUIRE(wg::hydro::accumulate_flow(r.receivers, order, weights, grid, a).ok());
  return a;
}

double distance(const Grid& grid, std::int64_t i, std::int32_t cx, std::int32_t cy) {
  const double ddx = grid.x_of(i) - cx;
  const double ddy = grid.y_of(i) - cy;
  return std::sqrt((ddx * ddx) + (ddy * ddy));
}

// Random relief 100 + 200 u with an ocean ring on the map edge.
std::vector<float> random_relief(const Grid& grid, std::uint64_t seed) {
  std::vector<float> z = wg::test::white_noise(grid, seed, "test.hydro.relief");
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    z[u(i)] = grid.is_border(i) ? 50.0F : 100.0F + (200.0F * z[u(i)]);
  }
  return z;
}

}  // namespace

TEST_CASE("hydro.d8_receivers follows a ramp downhill") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = wg::test::ramp(grid, 99.0, 1.0);
  const Routed r = route(z, grid);
  for (std::int32_t y = 0; y < grid.n; ++y) {
    CHECK(r.receivers[u(grid.idx(0, y))] == -1);
    for (std::int32_t x = 1; x < grid.n; ++x) {
      CHECK(r.receivers[u(grid.idx(x, y))] == grid.idx(x - 1, y));
    }
  }
}

TEST_CASE("hydro.d8_receivers drains a cone radially") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = wg::test::cone(grid, 300.0, 12.0, 16, 16);
  const Routed r = route(z, grid);
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    if (z[u(i)] < 100.0F) {
      CHECK(r.receivers[u(i)] == -1);
      continue;
    }
    CAPTURE(i);
    REQUIRE(r.receivers[u(i)] >= 0);
    CHECK(distance(grid, r.receivers[u(i)], 16, 16) > distance(grid, i, 16, 16));
  }
}

TEST_CASE("hydro.accumulate_flow converges on the thalweg of a V-valley") {
  const Grid grid = wg::test::make_grid(33);
  constexpr std::int32_t kCx = 16;
  const std::vector<float> z = wg::test::v_valley(grid, kCx, 2.0);
  const Routed r = route(z, grid);
  const std::vector<double> a = land_accumulation(z, r, grid);
  for (std::int32_t y = 2; y < grid.n; ++y) {
    // The thalweg flows north.
    CHECK(r.receivers[u(grid.idx(kCx, y))] == grid.idx(kCx, y - 1));
  }
  for (std::int32_t y = 1; y < grid.n / 2; ++y) {
    CAPTURE(y);
    const double thalweg = a[u(grid.idx(kCx, y))];
    CHECK(thalweg > a[u(grid.idx(kCx, y + 1))]);
    for (std::int32_t x = 0; x < grid.n; ++x) {
      if (x != kCx) {
        CHECK(a[u(grid.idx(x, y))] < thalweg);
      }
    }
  }
}

TEST_CASE("hydro.d8_receivers routes every plateau cell outwards") {
  const Grid grid = wg::test::make_grid(17);
  constexpr std::int32_t kMargin = 4;
  const std::vector<float> z = wg::test::plateau(grid, kMargin);
  const Routed r = route(z, grid);
  const auto edge = [&](std::int64_t i) {
    const std::int32_t x = grid.x_of(i);
    const std::int32_t y = grid.y_of(i);
    return std::min(std::min(x, y), std::min(grid.n - 1 - x, grid.n - 1 - y));
  };
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    if (edge(i) >= kMargin) {
      CAPTURE(i);
      REQUIRE(r.receivers[u(i)] >= 0);
      CHECK(edge(r.receivers[u(i)]) == edge(i) - 1);
    }
  }
}

TEST_CASE(
    "hydro routing gives every land cell a strictly descending path to a sink (A15 "
    "acceptance)") {
  for (const std::int32_t n : {33, 65}) {
    const Grid grid = wg::test::make_grid(n);
    for (const std::uint64_t seed : {1U, 2U, 3U, 4U}) {
      CAPTURE(n);
      CAPTURE(seed);
      const std::vector<float> z = random_relief(grid, seed);
      const Routed r = route(z, grid);
      for (std::int64_t i = 0; i < grid.cells(); ++i) {
        std::int64_t c = i;
        std::int64_t steps = 0;
        while (r.receivers[u(c)] >= 0 && steps <= grid.cells()) {
          const std::int64_t next = r.receivers[u(c)];
          if (!(r.filled[u(next)] < r.filled[u(c)])) {
            break;
          }
          c = next;
          ++steps;
        }
        // The walk stops only at a sink, and every sink is ocean.
        CHECK(r.receivers[u(c)] == -1);
        CHECK(z[u(c)] < 100.0F);
      }
    }
  }
}

TEST_CASE("hydro accumulation at the sinks equals the land area exactly (A15 acceptance)") {
  const Grid grid = wg::test::make_grid(65);
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    CAPTURE(seed);
    const std::vector<float> z = random_relief(grid, seed);
    const Routed r = route(z, grid);
    const std::vector<double> a = land_accumulation(z, r, grid);
    double at_sinks = 0.0;
    double land = 0.0;
    for (std::size_t i = 0; i < z.size(); ++i) {
      land += z[i] < 100.0F ? 0.0 : 1.0;
      if (r.receivers[i] == -1) {
        at_sinks += a[i];
      }
      // Every land cell counts at least itself.
      if (z[i] >= 100.0F) {
        CHECK(a[i] >= 1.0);
      }
    }
    CHECK(at_sinks == land);
  }
}

TEST_CASE("hydro.stack_order puts receivers before donors, tree by tree") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = random_relief(grid, 7);
  const Routed r = route(z, grid);
  std::vector<std::int32_t> order(z.size());
  REQUIRE(wg::hydro::stack_order(r.receivers, grid, order).ok());
  std::vector<std::int64_t> position(z.size(), -1);
  for (std::size_t k = 0; k < order.size(); ++k) {
    REQUIRE(position[u(order[k])] == -1);
    position[u(order[k])] = static_cast<std::int64_t>(k);
  }
  std::int32_t last_sink = -1;
  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::int32_t i = order[k];
    const std::int32_t rec = r.receivers[u(i)];
    if (rec == -1) {
      // Sinks come in ascending order, each starting its own tree.
      CHECK(i > last_sink);
      last_sink = i;
    } else {
      CHECK(position[u(rec)] < static_cast<std::int64_t>(k));
    }
  }
}

TEST_CASE("hydro.stack_order on a hand-made tree") {
  // Cells of the top row of a 3 x 3 grid: 1 -> 0 and 2 -> 1; the rest drain into 4, the
  // centre, which is a sink like 0.
  const Grid grid = wg::test::make_grid(3);
  const std::vector<std::int32_t> receivers = {-1, 0, 1, 4, -1, 4, 4, 4, 4};
  std::vector<std::int32_t> order(9);
  REQUIRE(wg::hydro::stack_order(receivers, grid, order).ok());
  CHECK(order == std::vector<std::int32_t>{0, 1, 2, 4, 3, 5, 6, 7, 8});
  const std::vector<double> weights = {1, 2, 4, 8, 16, 32, 64, 128, 256};
  std::vector<double> a(9);
  REQUIRE(wg::hydro::accumulate_flow(receivers, order, weights, grid, a).ok());
  CHECK(a == std::vector<double>{7, 6, 4, 8, 504, 32, 64, 128, 256});
}

TEST_CASE("hydro routing rejects invalid input") {
  const Grid grid = wg::test::make_grid(3);
  std::vector<std::int32_t> order(9);
  // A cycle 0 <-> 1, a receiver that is not a neighbour, and one out of range.
  CHECK(wg::hydro::stack_order(std::vector<std::int32_t>{1, 0, -1, -1, -1, -1, -1, -1, -1}, grid,
                               order)
            .code == StatusCode::invalid_argument);
  CHECK(wg::hydro::stack_order(std::vector<std::int32_t>{2, -1, -1, -1, -1, -1, -1, -1, -1}, grid,
                               order)
            .code == StatusCode::invalid_argument);
  CHECK(wg::hydro::stack_order(std::vector<std::int32_t>{9, -1, -1, -1, -1, -1, -1, -1, -1}, grid,
                               order)
            .code == StatusCode::invalid_argument);
  CHECK(wg::hydro::stack_order(std::vector<std::int32_t>{0, -1, -1, -1, -1, -1, -1, -1, -1}, grid,
                               order)
            .code == StatusCode::invalid_argument);
  const std::vector<std::int32_t> sinks(9, -1);
  std::vector<std::int32_t> short_order(3);
  CHECK(wg::hydro::stack_order(sinks, grid, short_order).code == StatusCode::invalid_argument);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::hydro::stack_order(sinks, grid, order, resource).code == StatusCode::out_of_memory);

  std::vector<double> a(9);
  const std::vector<double> w(9, 1.0);
  const std::vector<std::int32_t> bad_order = {0, 1, 2, 3, 4, 5, 6, 7, 9};
  CHECK(wg::hydro::accumulate_flow(sinks, bad_order, w, grid, a).code ==
        StatusCode::invalid_argument);
  CHECK(wg::hydro::accumulate_flow(sinks, order, std::vector<double>(2), grid, a).code ==
        StatusCode::invalid_argument);

  std::vector<float> z(9, 200.0F);
  std::vector<std::int32_t> rec(9);
  std::vector<std::int32_t> short_rec(2);
  CHECK(wg::hydro::d8_receivers(z, grid, short_rec).code == StatusCode::invalid_argument);
  CHECK(wg::hydro::d8_receivers(z, Grid{}, rec).code == StatusCode::invalid_argument);
  // A flat map: no cell has a lower neighbour, so every cell is a sink.
  REQUIRE(wg::hydro::d8_receivers(z, grid, rec).ok());
  CHECK(rec == sinks);
}
