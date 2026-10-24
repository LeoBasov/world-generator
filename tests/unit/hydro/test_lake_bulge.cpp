#include "wg/hydro/lake_bulge.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "support/fixtures.hpp"
#include "support/hydro_chain.hpp"
#include "wg/hydro/river_network.hpp"
#include "wg/numerics/neighborhood.hpp"

using wg::core::Grid;
using wg::core::Lake;
using wg::core::RiverGraph;
using wg::core::StatusCode;
using wg::hydro::LakeBulgeParams;
using wg::test::HydroChain;

namespace {

constexpr std::int32_t kCx = 16;

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

// The V-valley of plan 10.2 (thalweg z = 100 + y down column kCx, sides rising by 20 per
// column) and a river along the thalweg from row 32 to its mouth at row 1.
struct Valley {
  Grid grid = wg::test::make_grid(33);
  HydroChain h;
  RiverGraph river;
};

Valley valley() {
  Valley v;
  std::vector<float> z = wg::test::v_valley(v.grid, kCx, 20.0);
  const std::vector<float> rain(z.size(), 50.0F);
  const std::vector<float> drain(z.size(), 0.0F);
  v.h = wg::test::hydro_chain(std::move(z), rain, drain, v.grid);
  for (std::int32_t y = 1; y < v.grid.n; ++y) {
    v.river.cell.push_back(v.grid.idx(kCx, y));
    v.river.downstream.push_back(y == 1 ? -1 : y - 2);
    v.river.discharge_m3_yr.push_back(0.0F);
    v.river.width_class.push_back(1);
    v.river.order.push_back(1);
    v.river.name_id.push_back(-1);
  }
  return v;
}

wg::core::Expected<std::int32_t> bulge(Valley& v, const LakeBulgeParams& p, std::uint64_t seed) {
  return wg::hydro::bulge_lakes(v.h.z, v.h.receivers, v.river, v.grid, p, wg::core::seed_key(seed),
                                v.h.lake_id, v.h.lakes);
}

// lake_id holds exactly the cells of the lakes, numbered by first cell.
void check_lakes(const Valley& v) {
  std::vector<std::int32_t> expect(v.h.z.size(), -1);
  for (std::size_t l = 0; l < v.h.lakes.size(); ++l) {
    const Lake& lake = v.h.lakes[l];
    CHECK(lake.id == static_cast<std::int32_t>(l));
    REQUIRE(!lake.cells.empty());
    CHECK(std::is_sorted(lake.cells.begin(), lake.cells.end()));
    if (l > 0) {
      CHECK(v.h.lakes[l - 1].cells.front() < lake.cells.front());
    }
    for (const std::int64_t c : lake.cells) {
      expect[u(c)] = static_cast<std::int32_t>(l);
      CHECK(v.h.z[u(c)] < lake.spill_elevation_eu);
    }
    CHECK(v.h.lake_id[u(lake.outlet_cell)] == -1);
  }
  CHECK(v.h.lake_id == expect);
}

}  // namespace

TEST_CASE("hydro.bulge_lakes floods the thalweg upstream of the drawn cell (R6.1)") {
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    CAPTURE(seed);
    Valley v = valley();
    REQUIRE(v.h.lakes.empty());
    const auto added = bulge(v, {.count = 1, .height = 3.5}, seed);
    REQUIRE(added);
    REQUIRE(*added == 1);
    REQUIRE(v.h.lakes.size() == 1);
    check_lakes(v);
    // A thalweg segment from the drawn cell c up to 3 rows above it, level z_c + 3.5.
    const Lake& lake = v.h.lakes[0];
    const std::int32_t y0 = v.grid.y_of(lake.cells.front());
    CHECK(y0 >= 2);  // row 1 is a mouth next to the ocean
    const std::int32_t rows = std::min(4, v.grid.n - y0);
    REQUIRE(lake.cells.size() == u(rows));
    for (std::int32_t k = 0; k < rows; ++k) {
      CHECK(lake.cells[u(k)] == v.grid.idx(kCx, y0 + k));
    }
    CHECK(lake.spill_elevation_eu == 100.0F + static_cast<float>(y0) + 3.5F);
    CHECK(lake.outlet_cell == v.grid.idx(kCx, y0 - 1));
    CHECK(lake.salinity_class == 0);
  }
}

TEST_CASE("hydro.bulge_lakes is deterministic and keeps bulges apart") {
  Valley a = valley();
  Valley b = valley();
  REQUIRE(bulge(a, {.count = 3, .height = 2.5}, 9));
  REQUIRE(bulge(b, {.count = 3, .height = 2.5}, 9));
  CHECK(a.h.lakes == b.h.lakes);
  CHECK(a.h.lake_id == b.h.lake_id);

  // More draws than candidates: every candidate is drawn, crowded ones are skipped.
  Valley all = valley();
  const auto added = bulge(all, {.count = 1000, .height = 2.5}, 4);
  REQUIRE(added);
  CHECK(*added >= 5);
  CHECK(*added == static_cast<std::int32_t>(all.h.lakes.size()));
  check_lakes(all);
  for (std::int64_t c = 0; c < all.grid.cells(); ++c) {
    const std::int32_t id = all.h.lake_id[u(c)];
    if (id < 0) {
      continue;
    }
    wg::num::for_each_neighbor(all.grid, c, wg::num::Connectivity::d8,
                               [&](std::int64_t j, wg::core::Dir8) {
                                 const std::int32_t other = all.h.lake_id[u(j)];
                                 CHECK((other < 0 || other == id));
                               });
  }

  // The river traced again stops at the bulges and continues from their outlets.
  auto rivers = wg::hydro::trace_rivers(all.h.sources, all.h.z, all.h.receivers, all.h.lake_id,
                                        all.h.lakes, all.grid);
  REQUIRE(rivers);
  CHECK(wg::hydro::check_river_graph(*rivers, all.h.z, all.h.receivers, all.h.lake_id, all.grid)
            .ok());
  for (const Lake& lake : all.h.lakes) {
    CHECK(std::find(rivers->cell.begin(), rivers->cell.end(), lake.outlet_cell) !=
          rivers->cell.end());
  }
}

TEST_CASE("hydro.bulge_lakes renumbers existing lakes by first cell") {
  Valley v = valley();
  // A one-cell lake high on the eastern slope in the last row, after every thalweg cell.
  Lake fake;
  fake.cells = {v.grid.idx(30, 32)};
  fake.spill_elevation_eu = 1000.0F;
  fake.outlet_cell = v.grid.idx(29, 32);
  v.h.lakes = {fake};
  v.h.lake_id[u(fake.cells[0])] = 0;
  REQUIRE(bulge(v, {.count = 1, .height = 1.5}, 2));
  REQUIRE(v.h.lakes.size() == 2);
  check_lakes(v);
  CHECK(v.h.lakes[1].cells == fake.cells);
  CHECK(v.h.lakes[1].id == 1);
}

TEST_CASE("hydro.bulge_lakes without draws or height changes nothing") {
  Valley v = valley();
  const std::vector<std::int32_t> before = v.h.lake_id;
  auto none = bulge(v, {.count = 0, .height = 2.0}, 1);
  REQUIRE(none);
  CHECK(*none == 0);
  auto flat = bulge(v, {.count = 5, .height = 0.0}, 1);
  REQUIRE(flat);
  CHECK(*flat == 0);
  CHECK(v.h.lakes.empty());
  CHECK(v.h.lake_id == before);
}

TEST_CASE("hydro.bulge_lakes rejects bad input and leaves the lakes as they are") {
  Valley v = valley();
  CHECK(bulge(v, {.count = -1, .height = 2.0}, 1).error().code == StatusCode::invalid_argument);
  CHECK(
      bulge(v, {.count = 1, .height = std::numeric_limits<double>::quiet_NaN()}, 1).error().code ==
      StatusCode::invalid_argument);
  v.h.lakes.push_back(Lake{});
  CHECK(bulge(v, {.count = 1, .height = 2.0}, 1).error().code == StatusCode::invalid_argument);
  CHECK(v.h.lakes.size() == 1);
  v.h.lakes.clear();
  v.river.order.pop_back();
  CHECK(bulge(v, {.count = 1, .height = 2.0}, 1).error().code == StatusCode::invalid_argument);
  const std::vector<float> short_z(3);
  CHECK(wg::hydro::bulge_lakes(short_z, v.h.receivers, v.river, v.grid, {}, wg::core::seed_key(1),
                               v.h.lake_id, v.h.lakes)
            .error()
            .code == StatusCode::invalid_argument);
}
