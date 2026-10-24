#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "support/fixtures.hpp"
#include "wg/core/grid.hpp"
#include "wg/core/vector_layers.hpp"
#include "wg/hydro/discharge.hpp"
#include "wg/hydro/lakes.hpp"
#include "wg/hydro/priority_flood.hpp"
#include "wg/hydro/routing.hpp"
#include "wg/hydro/test_rivers.hpp"

namespace wg::test {

// The hydrology chain of stage C6 after erosion (A15, A17), for kernel tests of the river
// network: fill, route, discharge, lakes and sources.

struct HydroChain {
  std::vector<float> z;
  std::vector<float> rainfall;
  std::vector<float> drainage;
  std::vector<float> level;
  std::vector<float> filled;
  std::vector<std::int32_t> receivers;
  std::vector<std::int32_t> order;
  std::vector<double> weights;
  std::vector<double> discharge;
  std::vector<std::int32_t> lake_id;
  std::vector<wg::core::Lake> lakes;
  std::vector<std::int32_t> sources;
};

/// Runs the chain on z with the given rainfall and drainage.
inline HydroChain hydro_chain(std::vector<float> z, std::vector<float> rainfall,
                              std::vector<float> drainage, const wg::core::Grid& grid,
                              double lake_min_volume = 10.0) {
  const std::size_t n = z.size();
  HydroChain h{.z = std::move(z),
               .rainfall = std::move(rainfall),
               .drainage = std::move(drainage),
               .level = std::vector<float>(n),
               .filled = std::vector<float>(n),
               .receivers = std::vector<std::int32_t>(n),
               .order = std::vector<std::int32_t>(n),
               .weights = std::vector<double>(n),
               .discharge = std::vector<double>(n),
               .lake_id = std::vector<std::int32_t>(n),
               .lakes = {},
               .sources = {}};
  REQUIRE(wg::hydro::fill_depressions(h.z, grid, h.level).ok());
  REQUIRE(wg::hydro::priority_flood(h.z, grid, h.filled).ok());
  REQUIRE(wg::hydro::d8_receivers(h.filled, grid, h.receivers).ok());
  REQUIRE(wg::hydro::stack_order(h.receivers, grid, h.order).ok());
  REQUIRE(wg::hydro::runoff_weights(h.z, h.rainfall, h.drainage, grid, h.weights).ok());
  REQUIRE(wg::hydro::accumulate_flow(h.receivers, h.order, h.weights, grid, h.discharge).ok());
  auto lakes = wg::hydro::label_lakes(h.z, h.level, h.filled, h.receivers, grid,
                                      {.min_volume = lake_min_volume}, h.lake_id);
  REQUIRE(lakes);
  h.lakes = std::move(*lakes);
  auto sources = wg::hydro::find_river_sources(h.z, grid);
  REQUIRE(sources);
  h.sources = std::move(*sources);
  return h;
}

/// Rough island for seed: a cone from 380 at the centre falling by 13 per cell, plus white
/// noise of amplitude 60 (pits, lakes, ridges), the map edge set to ocean (50); rainfall and
/// drainage uniform in [0, 100).
inline HydroChain rough_island(const wg::core::Grid& grid, std::uint64_t seed) {
  const std::int32_t c = (grid.n - 1) / 2;
  std::vector<float> z = cone(grid, 380.0, 13.0, c, c);
  const std::vector<float> noise = white_noise(grid, seed, "test.island");
  for (std::size_t i = 0; i < z.size(); ++i) {
    const std::int64_t x = grid.x_of(static_cast<std::int64_t>(i));
    const std::int64_t y = grid.y_of(static_cast<std::int64_t>(i));
    const bool edge = x == 0 || y == 0 || x == grid.n - 1 || y == grid.n - 1;
    z[i] = edge ? 50.0F : std::clamp(z[i] + (60.0F * noise[i]), 0.0F, 400.0F);
  }
  std::vector<float> rain = white_noise(grid, seed, "test.rain");
  std::vector<float> drain = white_noise(grid, seed, "test.drain");
  for (std::size_t i = 0; i < z.size(); ++i) {
    rain[i] = 100.0F * (rain[i] + 0.5F);
    drain[i] = 100.0F * (drain[i] + 0.5F);
  }
  return hydro_chain(std::move(z), std::move(rain), std::move(drain), grid);
}

}  // namespace wg::test
