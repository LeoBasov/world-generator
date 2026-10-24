#include "wg/bio/salinity.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <vector>

#include "support/fixtures.hpp"
#include "support/source_files.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/water.hpp"

using wg::bio::SalinityInputs;
using wg::core::StatusCode;
using wg::hydro::WaterClass;
using wg::params::SalinityClasses;
using wg::params::SalinityParams;

namespace {

constexpr auto kLand = static_cast<std::uint8_t>(WaterClass::land);
constexpr auto kOcean = static_cast<std::uint8_t>(WaterClass::ocean);
constexpr auto kRiver = static_cast<std::uint8_t>(WaterClass::river);

// The shipped classes of data/tables/salinity_classes.json.
SalinityClasses shipped_classes() {
  auto classes = wg::params::load_salinity_classes(
      wg::test::read_source_file("data/tables/salinity_classes.json"));
  REQUIRE(classes.has_value());
  return *classes;
}

// Water with ocean in the columns x < ocean_columns.
std::vector<std::uint8_t> west_ocean(const wg::core::Grid& grid, std::int32_t ocean_columns) {
  std::vector<std::uint8_t> water(static_cast<std::size_t>(grid.cells()), kLand);
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < ocean_columns; ++x) {
      water[static_cast<std::size_t>(grid.idx(x, y))] = kOcean;
    }
  }
  return water;
}

std::vector<std::uint8_t> salinity(const wg::core::Grid& grid, const SalinityInputs& in,
                                   const SalinityParams& p = {}) {
  wg::core::SerialExecutor serial;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(grid.cells()), 255);
  REQUIRE(wg::bio::compute_salinity(grid, in, p, shipped_classes(), out, serial).ok());
  return out;
}

// River graph over the given cells; downstream holds node indices, -1 at a mouth.
wg::core::RiverGraph river_graph(const std::vector<std::int64_t>& cells,
                                 const std::vector<std::int64_t>& downstream) {
  wg::core::RiverGraph g;
  g.cell = cells;
  g.downstream = downstream;
  g.discharge_m3_yr.assign(cells.size(), 1.0F);
  g.width_class.assign(cells.size(), 1);
  g.order.assign(cells.size(), 1);
  g.name_id.assign(cells.size(), -1);
  return g;
}

}  // namespace

TEST_CASE("bio.coastal_salinity and fresh_distance") {
  CHECK(wg::bio::coastal_salinity(0.0, 1.5) == 100.0);
  CHECK(wg::bio::coastal_salinity(1.0, 1.5) == doctest::Approx(51.3417119032592).epsilon(1e-12));
  CHECK(wg::bio::coastal_salinity(3.0, 1.5) == doctest::Approx(13.53352832366127).epsilon(1e-12));
  CHECK(wg::bio::coastal_salinity(std::numeric_limits<double>::infinity(), 1.5) == 0.0);

  const SalinityClasses classes = shipped_classes();
  REQUIRE(classes.classes.size() == 4);
  const double df = wg::bio::fresh_distance(classes, 1.5);
  CHECK(df == doctest::Approx(1.5 * std::log(100.0 / 16.5)).epsilon(1e-12));
  CHECK(classes.quantise(wg::bio::coastal_salinity(df * (1.0 - 1e-9), 1.5)) == 33);
  CHECK(classes.quantise(wg::bio::coastal_salinity(df * (1.0 + 1e-9), 1.5)) == 0);

  CHECK(wg::bio::fresh_distance(SalinityClasses{}, 1.5) == 0.0);
  SalinityClasses odd = classes;
  odd.classes[1].min_salinity = 0.0;
  CHECK(wg::bio::fresh_distance(odd, 1.5) == std::numeric_limits<double>::infinity());
  odd.classes[1].min_salinity = 100.0;
  CHECK(wg::bio::fresh_distance(odd, 1.5) == 0.0);
}

TEST_CASE("bio.compute_salinity: 100 on ocean, 0 beyond the decay distance") {
  const wg::core::Grid grid = wg::test::make_grid(65);
  const std::vector<std::uint8_t> mask = wg::test::random_mask(grid, 0.01, 71);
  std::vector<std::uint8_t> water(mask.size(), kLand);
  std::vector<std::int64_t> ocean;
  for (std::size_t i = 0; i < mask.size(); ++i) {
    if (mask[i] != 0) {
      water[i] = kOcean;
      ocean.push_back(static_cast<std::int64_t>(i));
    }
  }
  REQUIRE(!ocean.empty());
  const SalinityParams p;
  const SalinityClasses classes = shipped_classes();
  const double df = wg::bio::fresh_distance(classes, p.decay_length);
  const std::vector<std::uint8_t> out = salinity(grid, {.water = water});
  for (std::int64_t i = 0; i < grid.cells(); ++i) {
    const std::uint8_t s = out[static_cast<std::size_t>(i)];
    if (water[static_cast<std::size_t>(i)] == kOcean) {
      CHECK(s == 100);
      continue;
    }
    // Brute-force nearest ocean cell.
    double d2 = std::numeric_limits<double>::infinity();
    for (const std::int64_t o : ocean) {
      const auto dx = static_cast<double>(grid.x_of(i) - grid.x_of(o));
      const auto dy = static_cast<double>(grid.y_of(i) - grid.y_of(o));
      d2 = std::min(d2, (dx * dx) + (dy * dy));
    }
    const double d = std::sqrt(d2);
    CHECK(s == classes.quantise(wg::bio::coastal_salinity(d, p.decay_length)));
    if (d > df) {
      CHECK(s == 0);
    } else {
      CHECK(s > 0);
      CHECK(s < 100);
    }
  }
}

TEST_CASE("bio.compute_salinity gives exactly the four classes") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<std::uint8_t> water = west_ocean(grid, 4);
  const std::vector<std::uint8_t> out = salinity(grid, {.water = water});
  std::set<int> seen;
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      const int s = out[static_cast<std::size_t>(grid.idx(x, y))];
      seen.insert(s);
      // d = x - 3: S(1) = 51.3 saline, S(2) = 26.4 brackish, S(3) = 13.5 fresh.
      const int expected = x < 4 ? 100 : x == 4 ? 66 : x == 5 ? 33 : 0;
      CHECK(s == expected);
    }
  }
  CHECK(seen == std::set<int>{0, 33, 66, 100});

  // A shorter decay length narrows the band; no ocean leaves everything fresh.
  SalinityParams p;
  p.decay_length = 0.5;
  const std::vector<std::uint8_t> narrow = salinity(grid, {.water = water}, p);
  CHECK(narrow[static_cast<std::size_t>(grid.idx(4, 0))] == 0);  // S(1) = 13.5
  const std::vector<std::uint8_t> land(water.size(), kLand);
  for (const std::uint8_t s : salinity(grid, {.water = land})) {
    CHECK(s == 0);
  }
}

TEST_CASE("bio.compute_salinity: rivers inherit the class of their mouth cell") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  std::vector<std::uint8_t> water = west_ocean(grid, 4);
  // River A along row 8 from its mouth (4, 8) to (8, 8); river B diagonal from its mouth (4, 2)
  // through (5, 3) to (6, 4). Nodes are listed out of order.
  const auto at = [&](std::int32_t x, std::int32_t y) { return grid.idx(x, y); };
  const wg::core::RiverGraph rivers =
      river_graph({at(7, 8), at(5, 3), at(4, 8), at(6, 4), at(6, 8), at(4, 2), at(5, 8), at(8, 8)},
                  {4, 5, -1, 1, 6, -1, 2, 0});
  for (const std::int64_t c : rivers.cell) {
    water[static_cast<std::size_t>(c)] = kRiver;
  }
  const std::vector<std::uint8_t> plain = salinity(grid, {.water = water});
  const std::vector<std::uint8_t> out = salinity(grid, {.water = water, .rivers = &rivers});
  const auto s = [&](const std::vector<std::uint8_t>& v, std::int32_t x, std::int32_t y) {
    return static_cast<int>(v[static_cast<std::size_t>(at(x, y))]);
  };
  // Channel distances to the mouth (class 66): 1 and 2 inherit, 3 > 2.70 keeps its own class.
  CHECK(s(out, 4, 8) == 66);
  CHECK(s(plain, 5, 8) == 33);
  CHECK(s(out, 5, 8) == 66);
  CHECK(s(plain, 6, 8) == 0);
  CHECK(s(out, 6, 8) == 66);
  CHECK(s(out, 7, 8) == 0);
  CHECK(s(out, 8, 8) == 0);
  // Diagonal steps count sqrt(2): 1.41 inherits, 2.83 > 2.70 does not.
  CHECK(s(out, 4, 2) == 66);
  CHECK(s(out, 5, 3) == 66);
  CHECK(s(out, 6, 4) == 0);
  // Only river cells change.
  std::size_t changed = 0;
  for (std::size_t i = 0; i < out.size(); ++i) {
    CHECK(out[i] >= plain[i]);
    changed += out[i] != plain[i] ? 1 : 0;
  }
  CHECK(changed == 3);

  const wg::core::RiverGraph empty;
  CHECK(salinity(grid, {.water = water, .rivers = &empty}) == plain);
}

TEST_CASE("bio.compute_salinity rejects bad river graphs") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<std::uint8_t> water = west_ocean(grid, 4);
  const SalinityClasses classes = shipped_classes();
  std::vector<std::uint8_t> out(water.size());
  const auto code = [&](const wg::core::RiverGraph& g) {
    return wg::bio::compute_salinity(grid, {.water = water, .rivers = &g}, {}, classes, out, serial)
        .code;
  };
  CHECK(code(river_graph({20, 21}, {-1, 0})) == StatusCode::ok);
  wg::core::RiverGraph inconsistent = river_graph({20, 21}, {-1, 0});
  inconsistent.order.pop_back();
  CHECK(code(inconsistent) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, grid.cells()}, {-1, 0})) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, -1}, {-1, 0})) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, 21}, {-1, 2})) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, 21}, {-2, 0})) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, 21, 22}, {1, 2, 0})) == StatusCode::invalid_argument);
  CHECK(code(river_graph({20, 21, 22}, {-1, 2, 1})) == StatusCode::invalid_argument);
}

TEST_CASE("bio.compute_salinity does not depend on the thread count") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const wg::core::Grid grid = wg::test::make_grid(129);
  const std::vector<std::uint8_t> mask = wg::test::random_mask(grid, 0.005, 72);
  std::vector<std::uint8_t> water(mask.size(), kLand);
  for (std::size_t i = 0; i < mask.size(); ++i) {
    water[i] = mask[i] != 0 ? kOcean : kLand;
  }
  const SalinityClasses classes = shipped_classes();
  std::vector<std::uint8_t> a(water.size());
  std::vector<std::uint8_t> b(water.size());
  REQUIRE(wg::bio::compute_salinity(grid, {.water = water}, {}, classes, a, serial).ok());
  REQUIRE(wg::bio::compute_salinity(grid, {.water = water}, {}, classes, b, **pool).ok());
  CHECK(a == b);
}

TEST_CASE("bio.compute_salinity rejects bad arguments and honours cancel") {
  wg::core::SerialExecutor serial;
  const wg::core::Grid grid = wg::test::make_grid(17);
  const std::vector<std::uint8_t> water = west_ocean(grid, 4);
  const std::vector<std::uint8_t> short_water(water.size() - 1);
  const SalinityClasses classes = shipped_classes();
  std::vector<std::uint8_t> out(water.size());
  std::vector<std::uint8_t> short_out(short_water.size());
  const auto code = [&](const SalinityInputs& in, std::span<std::uint8_t> o) {
    return wg::bio::compute_salinity(grid, in, {}, classes, o, serial).code;
  };
  CHECK(code({}, out) == StatusCode::invalid_argument);
  CHECK(code({.water = short_water}, out) == StatusCode::invalid_argument);
  CHECK(code({.water = water}, short_out) == StatusCode::invalid_argument);
  std::vector<std::uint8_t> v64(64);
  CHECK(wg::bio::compute_salinity(wg::test::make_grid(8), {.water = v64}, {}, classes, v64, serial)
            .code == StatusCode::invalid_argument);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::bio::compute_salinity(grid, {.water = water}, {}, classes, out, serial, &cancel).code ==
        StatusCode::cancelled);
}

TEST_CASE("bio.assign_lake_salinity takes the highest class of the lake's cells") {
  const wg::core::Grid grid = wg::test::make_grid(17);
  std::vector<std::uint8_t> s(static_cast<std::size_t>(grid.cells()), 0);
  s[10] = 33;
  s[11] = 66;
  s[40] = 33;
  std::vector<wg::core::Lake> lakes(3);
  lakes[0].cells = {9, 10, 11};
  lakes[1].cells = {40, 41};
  lakes[2].cells = {100};
  lakes[2].salinity_class = 100;
  REQUIRE(wg::bio::assign_lake_salinity(s, grid, lakes).ok());
  CHECK(lakes[0].salinity_class == 66);
  CHECK(lakes[1].salinity_class == 33);
  CHECK(lakes[2].salinity_class == 0);

  std::vector<wg::core::Lake> bad(1);
  bad[0].cells = {grid.cells()};
  CHECK(wg::bio::assign_lake_salinity(s, grid, bad).code == StatusCode::invalid_argument);
  const std::vector<std::uint8_t> short_s(s.size() - 1);
  CHECK(wg::bio::assign_lake_salinity(short_s, grid, lakes).code == StatusCode::invalid_argument);
}
