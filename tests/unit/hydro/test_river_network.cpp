#include "wg/hydro/river_network.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include "support/fixtures.hpp"
#include "support/hydro_chain.hpp"
#include "support/river_fixture.hpp"
#include "wg/hydro/discharge.hpp"
#include "wg/terrain/elevation.hpp"

using wg::core::Grid;
using wg::core::RiverGraph;
using wg::core::StatusCode;
using wg::hydro::RiverStem;
using wg::test::HydroChain;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

RiverGraph trace(const HydroChain& h, const Grid& grid) {
  auto g = wg::hydro::trace_rivers(h.sources, h.z, h.receivers, h.lake_id, h.lakes, grid);
  REQUIRE(g);
  return std::move(*g);
}

bool ocean(const HydroChain& h, std::int64_t c) { return h.z[u(c)] < wg::terrain::kSeaLevel; }

// The acceptance properties of A17 on the network of h.
void check_network(const HydroChain& h, const Grid& grid) {
  RiverGraph g = trace(h, grid);
  REQUIRE(g.consistent());
  const wg::params::RiversParams rp;
  const auto stems = wg::hydro::river_attributes(g, h.discharge, grid, rp);
  REQUIRE(stems);
  REQUIRE(wg::hydro::check_river_graph(g, h.z, h.receivers, h.lake_id, grid).ok());
  const std::size_t n = g.size();
  REQUIRE(n > 0);

  // Every source off the water is a node.
  std::vector<std::int32_t> node(h.z.size(), -1);
  for (std::size_t k = 0; k < n; ++k) {
    node[u(g.cell[k])] = static_cast<std::int32_t>(k);
  }
  for (const std::int32_t s : h.sources) {
    if (!ocean(h, s) && h.lake_id[u(s)] < 0) {
      CHECK(node[u(s)] >= 0);
    }
  }

  // Paths are simple and end in ocean or a lake; Q never decreases downstream.
  for (std::size_t k = 0; k < n; ++k) {
    std::vector<std::uint8_t> seen(n, 0);
    auto v = static_cast<std::int64_t>(k);
    while (g.downstream[u(v)] >= 0) {
      REQUIRE(seen[u(v)] == 0);
      seen[u(v)] = 1;
      const std::int64_t d = g.downstream[u(v)];
      CHECK(g.discharge_m3_yr[u(d)] >= g.discharge_m3_yr[u(v)]);
      CHECK(h.discharge[u(g.cell[u(d)])] >= h.discharge[u(g.cell[u(v)])]);
      v = d;
    }
    const std::int32_t r = h.receivers[u(g.cell[u(v)])];
    REQUIRE(r >= 0);  // every map here has ocean
    CHECK((ocean(h, r) || h.lake_id[u(r)] >= 0));
  }

  // Mass balance: the mouths into the ocean carry exactly the runoff of the cells that drain
  // through them.
  std::vector<std::uint8_t> ocean_mouth(h.z.size(), 0);
  double mouths = 0.0;
  for (std::size_t k = 0; k < n; ++k) {
    if (g.downstream[k] == -1 && ocean(h, h.receivers[u(g.cell[k])])) {
      ocean_mouth[u(g.cell[k])] = 1;
      mouths += h.discharge[u(g.cell[k])];
    }
  }
  double land = 0.0;
  for (std::size_t i = 0; i < h.z.size(); ++i) {
    for (auto c = static_cast<std::int64_t>(i); c >= 0; c = h.receivers[u(c)]) {
      if (ocean_mouth[u(c)] != 0) {
        land += h.weights[i];
        break;
      }
    }
  }
  REQUIRE(land > 0.0);
  CHECK(std::abs(mouths - land) / land < 1e-9);

  // At a confluence the smaller-Q branch is the tributary; orders and classes.
  std::vector<std::vector<std::size_t>> up(n);
  for (std::size_t k = 0; k < n; ++k) {
    if (g.downstream[k] >= 0) {
      up[u(g.downstream[k])].push_back(k);
    } else {
      CHECK(g.order[k] == 1);
    }
  }
  for (std::size_t k = 0; k < n; ++k) {
    std::int32_t mains = 0;
    for (const std::size_t a : up[k]) {
      if (g.order[a] == g.order[k]) {
        ++mains;
        for (const std::size_t b : up[k]) {
          CHECK(h.discharge[u(g.cell[a])] >= h.discharge[u(g.cell[b])]);
        }
      } else {
        CHECK(g.order[a] == g.order[k] + 1);
      }
    }
    CHECK(mains == (up[k].empty() ? 0 : 1));
  }
  std::vector<std::size_t> by_q(n);
  std::iota(by_q.begin(), by_q.end(), std::size_t{0});
  std::sort(by_q.begin(), by_q.end(), [&](std::size_t a, std::size_t b) {
    return h.discharge[u(g.cell[a])] < h.discharge[u(g.cell[b])];
  });
  for (std::size_t k = 0; k < n; ++k) {
    CHECK(g.width_class[k] >= 1);
    CHECK(g.width_class[k] <= 5);
    if (k > 0) {
      CHECK(g.width_class[by_q[k]] >= g.width_class[by_q[k - 1]]);
    }
  }
  // One name per stem, stems by descending discharge.
  for (std::size_t s = 1; s < stems->size(); ++s) {
    CHECK((*stems)[s - 1].discharge_m3_yr >= (*stems)[s].discharge_m3_yr);
  }
  std::int32_t total = 0;
  for (const RiverStem& s : *stems) {
    total += s.nodes;
    CHECK(g.name_id[u(s.lowest_node)] == s.id);
    CHECK(g.name_id[u(s.head_node)] == s.id);
  }
  CHECK(total == static_cast<std::int32_t>(n));
}

}  // namespace

TEST_CASE("hydro.river network: simple paths, monotone Q, mass balance (A17 acceptance)") {
  for (const std::int32_t size : {33, 65}) {
    const Grid grid = wg::test::make_grid(size, 1000.0, 10.0);
    for (std::uint64_t seed = 1; seed <= (size == 33 ? 30U : 8U); ++seed) {
      CAPTURE(size);
      CAPTURE(seed);
      const HydroChain h = wg::test::rough_island(grid, seed);
      check_network(h, grid);
    }
  }
}

TEST_CASE("hydro.trace_rivers follows the V-valley to its mouth") {
  const Grid grid = wg::test::make_grid(33);
  const std::int32_t cx = 16;
  std::vector<float> z = wg::test::v_valley(grid, cx, 20.0);
  const std::vector<float> rain(z.size(), 50.0F);
  const std::vector<float> drain(z.size(), 0.0F);
  const HydroChain h = wg::test::hydro_chain(std::move(z), rain, drain, grid);
  REQUIRE(!h.sources.empty());
  RiverGraph g = trace(h, grid);
  REQUIRE(wg::hydro::river_attributes(g, h.discharge, grid, {}));
  REQUIRE(wg::hydro::check_river_graph(g, h.z, h.receivers, h.lake_id, grid).ok());
  // Every mouth lies next to the ocean row; the thalweg's carries the most water.
  std::size_t thalweg = g.size();
  for (std::size_t k = 0; k < g.size(); ++k) {
    if (g.downstream[k] == -1) {
      CHECK(grid.y_of(g.cell[k]) == 1);
      if (g.cell[k] == grid.idx(cx, 1)) {
        thalweg = k;
      }
    }
  }
  REQUIRE(thalweg < g.size());
  CHECK(*std::max_element(g.discharge_m3_yr.begin(), g.discharge_m3_yr.end()) ==
        g.discharge_m3_yr[thalweg]);
  // The thalweg below the sources is the main stem.
  for (std::int32_t y = 1; y < 4; ++y) {
    const auto it = std::find(g.cell.begin(), g.cell.end(), grid.idx(cx, y));
    REQUIRE(it != g.cell.end());
    CHECK(g.order[u(it - g.cell.begin())] == 1);
  }
}

TEST_CASE("hydro.trace_rivers passes through a lake and leaves at its outlet") {
  const Grid grid = wg::test::make_grid(33);
  const std::int32_t c = 16;
  const std::vector<float> rain(u(grid.cells()), 50.0F);
  const std::vector<float> drain(u(grid.cells()), 0.0F);
  HydroChain h = wg::test::hydro_chain(wg::test::bowl(grid), rain, drain, grid);
  REQUIRE(h.lakes.size() == 1);
  const std::int64_t outlet = h.lakes[0].outlet_cell;
  // A source on the inner edge of the western rim drains into the lake.
  h.sources = {static_cast<std::int32_t>(grid.idx(c - 10, c))};
  const RiverGraph g = trace(h, grid);
  REQUIRE(wg::hydro::check_river_graph(g, h.z, h.receivers, h.lake_id, grid).ok());
  const auto at = [&](std::int64_t cell) {
    return std::find(g.cell.begin(), g.cell.end(), cell) != g.cell.end();
  };
  CHECK(at(grid.idx(c - 10, c)));
  CHECK(at(outlet));
  std::int32_t into_lake = 0;
  std::int32_t into_ocean = 0;
  for (std::size_t k = 0; k < g.size(); ++k) {
    if (g.downstream[k] == -1) {
      const std::int32_t r = h.receivers[u(g.cell[k])];
      (h.lake_id[u(r)] >= 0 ? into_lake : into_ocean) += 1;
    }
  }
  CHECK(into_lake == 1);
  CHECK(into_ocean == 1);

  // A source inside the lake also continues from the outlet; one on ocean adds nothing.
  h.sources = {static_cast<std::int32_t>(grid.idx(c, c)), 0};
  const RiverGraph inside = trace(h, grid);
  CHECK(!inside.empty());
  CHECK(inside.cell.front() != 0);
  CHECK(std::find(inside.cell.begin(), inside.cell.end(), outlet) != inside.cell.end());
}

TEST_CASE("hydro.trace_rivers numbers the nodes by cell, whatever the source order") {
  const Grid grid = wg::test::make_grid(33, 1000.0, 10.0);
  HydroChain h = wg::test::rough_island(grid, 7);
  const RiverGraph a = trace(h, grid);
  std::reverse(h.sources.begin(), h.sources.end());
  const RiverGraph b = trace(h, grid);
  CHECK(a == b);
  CHECK(std::is_sorted(a.cell.begin(), a.cell.end()));
}

TEST_CASE("hydro.trace_rivers and check_river_graph reject bad input") {
  const Grid grid = wg::test::make_grid(33, 1000.0, 10.0);
  const HydroChain h = wg::test::rough_island(grid, 3);
  const std::vector<std::int32_t> bad_source{static_cast<std::int32_t>(grid.cells())};
  CHECK(wg::hydro::trace_rivers(bad_source, h.z, h.receivers, h.lake_id, h.lakes, grid)
            .error()
            .code == StatusCode::invalid_argument);
  std::vector<std::int32_t> bad_lake = h.lake_id;
  bad_lake[5] = static_cast<std::int32_t>(h.lakes.size());
  CHECK(
      wg::hydro::trace_rivers(h.sources, h.z, h.receivers, bad_lake, h.lakes, grid).error().code ==
      StatusCode::invalid_argument);
  const std::vector<float> short_z(10, 0.0F);
  CHECK(wg::hydro::trace_rivers(h.sources, short_z, h.receivers, h.lake_id, h.lakes, grid)
            .error()
            .code == StatusCode::invalid_argument);

  const RiverGraph good = trace(h, grid);
  REQUIRE(good.size() > 3);
  const auto check = [&](const RiverGraph& g) {
    return wg::hydro::check_river_graph(g, h.z, h.receivers, h.lake_id, grid).code;
  };
  CHECK(check(good) == StatusCode::ok);
  RiverGraph twice = good;
  twice.cell[1] = twice.cell[0];
  CHECK(check(twice) == StatusCode::invalid_argument);
  RiverGraph short_arrays = good;
  short_arrays.order.pop_back();
  CHECK(check(short_arrays) == StatusCode::invalid_argument);
  // A mouth cut off a river drains onto land (or into another node).
  RiverGraph cut = good;
  std::size_t inner = 0;
  while (cut.downstream[inner] < 0) {
    ++inner;
  }
  cut.downstream[inner] = -1;
  CHECK(check(cut) == StatusCode::invalid_argument);
  // A node pointing away from its receiver.
  RiverGraph wrong = good;
  wrong.downstream[inner] = static_cast<std::int64_t>(inner);
  CHECK(check(wrong) == StatusCode::invalid_argument);
}

TEST_CASE("hydro.check_river_graph finds a cycle") {
  // Two cells draining into each other (receivers no fill would give).
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z(u(grid.cells()), 150.0F);
  std::vector<std::int32_t> receivers(u(grid.cells()), -1);
  const std::vector<std::int32_t> lake_id(u(grid.cells()), -1);
  const auto a = static_cast<std::int32_t>(grid.idx(4, 4));
  const auto b = static_cast<std::int32_t>(grid.idx(5, 4));
  receivers[u(a)] = b;
  receivers[u(b)] = a;
  RiverGraph g;
  g.cell = {a, b};
  g.downstream = {1, 0};
  g.discharge_m3_yr = {0.0F, 0.0F};
  g.width_class = {1, 1};
  g.order = {1, 1};
  g.name_id = {-1, -1};
  const wg::core::Status s = wg::hydro::check_river_graph(g, z, receivers, lake_id, grid);
  CHECK(s.code == StatusCode::invalid_argument);
  CHECK(s.message == "check_river_graph: the river graph contains a cycle");
  std::vector<double> q(u(grid.cells()), 1.0);
  CHECK(wg::hydro::river_attributes(g, q, grid, {}).error().code == StatusCode::invalid_argument);
}

TEST_CASE("hydro.width_class_thresholds invert the hydraulic geometry") {
  wg::params::RiversParams p;  // a_w = 1, b = 0.5, bounds 1, 2, 5, 10 m
  const auto t = wg::hydro::width_class_thresholds(p);
  REQUIRE(t);
  for (std::size_t k = 0; k < t->size(); ++k) {
    const double w = p.width_class_bounds[k];
    CHECK((*t)[k] == doctest::Approx(wg::hydro::kSecondsPerYear * w * w).epsilon(1e-12));
    CHECK(wg::hydro::river_width_class((*t)[k], *t) == k + 2);
    CHECK(wg::hydro::river_width_class(std::nextafter((*t)[k], 0.0), *t) == k + 1);
  }
  CHECK(wg::hydro::river_width_class(0.0, *t) == 1);
  CHECK(wg::hydro::river_width_class(std::numeric_limits<double>::quiet_NaN(), *t) == 1);
  CHECK(wg::hydro::river_width_class(std::numeric_limits<double>::infinity(), *t) == 5);

  wg::params::RiversParams constant;
  constant.width_exponent = 0.0;
  constant.width_coefficient = 5.0;  // w = 5 m everywhere: a river (class 4), never major
  const auto c = wg::hydro::width_class_thresholds(constant);
  REQUIRE(c);
  CHECK(wg::hydro::river_width_class(0.0, *c) == 4);
  CHECK(std::isinf((*c)[3]));
  wg::params::RiversParams zero;
  zero.width_coefficient = 0.0;
  zero.width_class_bounds = {0.0, 1.0, 2.0, 3.0};
  const auto z = wg::hydro::width_class_thresholds(zero);
  REQUIRE(z);
  CHECK(wg::hydro::river_width_class(1e30, *z) == 2);

  wg::params::RiversParams bad = p;
  bad.width_class_bounds = {1.0, 1.0, 2.0, 3.0};
  CHECK(wg::hydro::width_class_thresholds(bad).error().code == StatusCode::invalid_argument);
  bad = p;
  bad.width_exponent = -0.5;
  CHECK(wg::hydro::width_class_thresholds(bad).error().code == StatusCode::invalid_argument);
  bad = p;
  bad.width_coefficient = std::numeric_limits<double>::infinity();
  CHECK(wg::hydro::width_class_thresholds(bad).error().code == StatusCode::invalid_argument);
}

TEST_CASE("hydro.river_attributes: main stem, tributary order, stems and names") {
  const Grid grid = wg::test::make_grid(17, 1000.0, 10.0);
  RiverGraph g = wg::test::river_fixture(grid);  // stem along row 4, tributary down column 5
  std::vector<double> q(u(grid.cells()), 0.0);
  for (std::int32_t x = 0; x <= 8; ++x) {
    q[u(grid.idx(x, 4))] = 100.0 - (5.0 * x);
  }
  for (std::int32_t y = 0; y <= 3; ++y) {
    q[u(grid.idx(5, y))] = 1.0 + y;
  }
  std::vector<RiverStem> seen;
  const auto namer_body = [&](const RiverStem& s) {
    seen.push_back(s);
    return 100 + s.id;
  };
  const wg::hydro::RiverNamer namer = namer_body;
  const auto stems = wg::hydro::river_attributes(g, q, grid, {}, &namer);
  REQUIRE(stems);
  REQUIRE(stems->size() == 2);
  CHECK(seen == *stems);
  const RiverStem& main = (*stems)[0];
  CHECK(main.lowest_node == 0);
  CHECK(main.head_node == 8);
  CHECK(main.nodes == 9);
  CHECK(main.order == 1);
  CHECK(main.discharge_m3_yr == 100.0);
  const RiverStem& trib = (*stems)[1];
  CHECK(trib.lowest_node == 9);
  CHECK(trib.head_node == 12);
  CHECK(trib.nodes == 4);
  CHECK(trib.order == 2);
  for (std::size_t k = 0; k < g.size(); ++k) {
    CAPTURE(k);
    CHECK(g.order[k] == (k < 9 ? 1 : 2));
    CHECK(g.name_id[k] == (k < 9 ? 100 : 101));
    CHECK(g.discharge_m3_yr[k] == static_cast<float>(q[u(g.cell[k])]));
    CHECK(g.width_class[k] == 1);  // tiny discharges are brooks
  }

  // Equal branches: the lower node continues the stem; without a namer the name is the stem id.
  RiverGraph fork;
  fork.cell = {grid.idx(1, 1), grid.idx(2, 1), grid.idx(1, 2)};
  fork.downstream = {-1, 0, 0};
  fork.discharge_m3_yr.assign(3, 0.0F);
  fork.width_class.assign(3, 0);
  fork.order.assign(3, 0);
  fork.name_id.assign(3, -1);
  std::vector<double> equal(u(grid.cells()), 1.0);
  equal[u(grid.idx(1, 1))] = 3.0;
  const auto fork_stems = wg::hydro::river_attributes(fork, equal, grid, {});
  REQUIRE(fork_stems);
  CHECK(fork.order == std::vector<std::uint8_t>{1, 1, 2});
  CHECK(fork.name_id == std::vector<std::int32_t>{0, 0, 1});

  // Bad input.
  std::vector<double> short_q(3, 0.0);
  CHECK(wg::hydro::river_attributes(fork, short_q, grid, {}).error().code ==
        StatusCode::invalid_argument);
  wg::params::RiversParams bad;
  bad.width_class_bounds = {3.0, 2.0, 5.0, 6.0};
  CHECK(wg::hydro::river_attributes(fork, equal, grid, bad).error().code ==
        StatusCode::invalid_argument);
  RiverGraph out = fork;
  out.downstream[1] = 7;
  CHECK(wg::hydro::river_attributes(out, equal, grid, {}).error().code ==
        StatusCode::invalid_argument);
}

TEST_CASE("hydro.river_attributes saturates the tributary order at 255") {
  // A chain of 300 tributaries, each the smaller of two branches. The cells only need to be
  // distinct, river_attributes does not check adjacency.
  const Grid grid = wg::test::make_grid(65);
  RiverGraph g;
  const auto add = [&](std::int64_t cell, std::int64_t down) {
    g.cell.push_back(cell);
    g.downstream.push_back(down);
    g.discharge_m3_yr.push_back(0.0F);
    g.width_class.push_back(0);
    g.order.push_back(0);
    g.name_id.push_back(-1);
  };
  std::vector<double> q(u(grid.cells()), 0.0);
  add(0, -1);
  q[0] = 1e9;
  std::int64_t trib = 0;
  for (std::int32_t k = 1; k <= 300; ++k) {
    // The tributary continues the chain; its sibling is a dead end with more discharge.
    const std::int64_t below = trib;
    trib = static_cast<std::int64_t>(g.size());
    add(1000 + (2 * k), below);
    q[u(1000 + (2 * k))] = 1.0;
    add(1001 + (2 * k), below);
    q[u(1001 + (2 * k))] = 2.0;
  }
  REQUIRE(wg::hydro::river_attributes(g, q, grid, {}));
  CHECK(g.order[u(trib)] == 255);
  CHECK(g.order[1] == 2);
  CHECK(g.order[2] == 1);
}
