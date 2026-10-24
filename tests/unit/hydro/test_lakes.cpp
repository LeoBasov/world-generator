#include "wg/hydro/lakes.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "support/counting_resource.hpp"
#include "support/fixtures.hpp"
#include "wg/hydro/priority_flood.hpp"
#include "wg/hydro/routing.hpp"

using wg::core::Grid;
using wg::core::Lake;
using wg::core::StatusCode;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

// Water level, filled surface and receivers of z.
struct Hydro {
  std::vector<float> level;
  std::vector<float> filled;
  std::vector<std::int32_t> receivers;
};

Hydro hydrology(const std::vector<float>& z, const Grid& grid) {
  Hydro h{std::vector<float>(z.size()), std::vector<float>(z.size()),
          std::vector<std::int32_t>(z.size())};
  REQUIRE(wg::hydro::fill_depressions(z, grid, h.level).ok());
  REQUIRE(wg::hydro::priority_flood(z, grid, h.filled).ok());
  REQUIRE(wg::hydro::d8_receivers(h.filled, grid, h.receivers).ok());
  return h;
}

wg::core::Expected<std::vector<Lake>> lakes_of(const std::vector<float>& z, const Hydro& h,
                                               const Grid& grid, double min_volume,
                                               std::vector<std::int32_t>& lake_id) {
  lake_id.assign(z.size(), 7);
  return wg::hydro::label_lakes(z, h.level, h.filled, h.receivers, grid, {.min_volume = min_volume},
                                lake_id);
}

// Ramp 99 + x (ocean in column 0) with single-cell pits of the given depth.
std::vector<float> ramp_with_pits(const Grid& grid,
                                  const std::vector<std::pair<std::int32_t, std::int32_t>>& pits,
                                  float depth) {
  std::vector<float> z = wg::test::ramp(grid, 99.0, 1.0);
  for (const auto& [x, y] : pits) {
    // The lowest neighbour lies one column west, at z - 1.
    z[u(grid.idx(x, y))] -= 1.0F + depth;
  }
  return z;
}

}  // namespace

TEST_CASE(
    "hydro.label_lakes finds one lake in the bowl, spilling at rim height (A15 "
    "acceptance)") {
  const Grid grid = wg::test::make_grid(33);
  const std::vector<float> z = wg::test::bowl(grid);
  const Hydro h = hydrology(z, grid);
  std::vector<std::int32_t> lake_id;
  const wg::core::Expected<std::vector<Lake>> lakes = lakes_of(z, h, grid, 10.0, lake_id);
  REQUIRE(lakes.has_value());
  REQUIRE(lakes->size() == 1);
  const Lake& lake = lakes->front();
  CHECK(lake.id == 0);
  CHECK(lake.spill_elevation_eu == 190.0F);
  CHECK(lake.salinity_class == 0);
  // The outlet is the inner notch cell, at the rim's lowest height.
  const std::int32_t c = (grid.n - 1) / 2;
  CHECK(lake.outlet_cell == grid.idx(c + 10, c));
  CHECK(z[u(lake.outlet_cell)] == lake.spill_elevation_eu);
  // Exactly the cells inside the rim, in ascending order.
  std::vector<std::int64_t> inside;
  for (std::int32_t y = 0; y < grid.n; ++y) {
    for (std::int32_t x = 0; x < grid.n; ++x) {
      const bool in = ((x - c) * (x - c)) + ((y - c) * (y - c)) < 100;
      if (in) {
        inside.push_back(grid.idx(x, y));
      }
      CHECK(lake_id[u(grid.idx(x, y))] == (in ? 0 : -1));
    }
  }
  CHECK(lake.cells == inside);
}

TEST_CASE("hydro.label_lakes keeps pits below the volume threshold as land") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = ramp_with_pits(grid, {{8, 8}}, 5.0F);
  const Hydro h = hydrology(z, grid);
  std::vector<std::int32_t> lake_id;
  // The pit fills by exactly 5 to the level of its western neighbour.
  wg::core::Expected<std::vector<Lake>> lakes = lakes_of(z, h, grid, 5.0, lake_id);
  REQUIRE(lakes.has_value());
  REQUIRE(lakes->size() == 1);
  CHECK(lakes->front().cells == std::vector<std::int64_t>{grid.idx(8, 8)});
  CHECK(lakes->front().spill_elevation_eu == 106.0F);
  CHECK(lakes->front().outlet_cell == grid.idx(7, 8));
  CHECK(lake_id[u(grid.idx(8, 8))] == 0);

  lakes = lakes_of(z, h, grid, 5.5, lake_id);
  REQUIRE(lakes.has_value());
  CHECK(lakes->empty());
  for (const std::int32_t id : lake_id) {
    CHECK(id == -1);
  }
}

TEST_CASE("hydro.label_lakes numbers lakes in row-major order of their first cell") {
  const Grid grid = wg::test::make_grid(17);
  const std::vector<float> z = ramp_with_pits(grid, {{6, 12}, {12, 4}, {8, 8}}, 3.0F);
  const Hydro h = hydrology(z, grid);
  std::vector<std::int32_t> lake_id;
  const wg::core::Expected<std::vector<Lake>> lakes = lakes_of(z, h, grid, 1.0, lake_id);
  REQUIRE(lakes.has_value());
  REQUIRE(lakes->size() == 3);
  CHECK((*lakes)[0].cells.front() == grid.idx(12, 4));
  CHECK((*lakes)[1].cells.front() == grid.idx(8, 8));
  CHECK((*lakes)[2].cells.front() == grid.idx(6, 12));
  for (std::size_t k = 0; k < lakes->size(); ++k) {
    CHECK((*lakes)[k].id == static_cast<std::int32_t>(k));
    CHECK(lake_id[u((*lakes)[k].cells.front())] == static_cast<std::int32_t>(k));
  }
}

TEST_CASE("hydro.label_lakes rejects invalid or mismatched input") {
  const Grid grid = wg::test::make_grid(17);
  std::vector<float> z = ramp_with_pits(grid, {{8, 8}}, 5.0F);
  Hydro h = hydrology(z, grid);
  std::vector<std::int32_t> lake_id(z.size());
  const auto call = [&](double min_volume) {
    return wg::hydro::label_lakes(z, h.level, h.filled, h.receivers, grid,
                                  {.min_volume = min_volume}, lake_id);
  };
  CHECK(call(-1.0).error().code == StatusCode::invalid_argument);
  CHECK(call(std::numeric_limits<double>::infinity()).error().code == StatusCode::invalid_argument);
  std::vector<std::int32_t> short_id(3);
  CHECK(
      wg::hydro::label_lakes(z, h.level, h.filled, h.receivers, grid, {}, short_id).error().code ==
      StatusCode::invalid_argument);
  wg::test::CountingResource resource;
  resource.fail = true;
  CHECK(wg::hydro::label_lakes(z, h.level, h.filled, h.receivers, grid, {}, lake_id, resource)
            .error()
            .code == StatusCode::out_of_memory);

  // An outlet inside the lake.
  const auto pit = u(grid.idx(8, 8));
  std::vector<std::int32_t> saved = h.receivers;
  h.receivers[pit] = -1;
  CHECK(call(1.0).error().code == StatusCode::invalid_argument);
  h.receivers = saved;
  // A water level below the ground.
  h.level[pit] = z[pit] - 1.0F;
  CHECK(call(1.0).error().code == StatusCode::invalid_argument);
  // Two levels in one depression.
  h = hydrology(z, grid);
  z[u(grid.idx(9, 8))] -= 4.0F;
  h.level[u(grid.idx(9, 8))] = h.level[pit] + 0.5F;
  CHECK(call(1.0).error().code == StatusCode::invalid_argument);
}
