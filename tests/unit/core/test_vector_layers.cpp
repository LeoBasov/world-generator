#include "wg/core/vector_layers.hpp"

#include <doctest/doctest.h>

using wg::core::kNoCell;
using wg::core::VectorLayers;

TEST_CASE("core.vector_layers default to empty with no-cell markers") {
  const VectorLayers layers;
  CHECK(layers.empty());
  CHECK(wg::core::Peak{}.cell == kNoCell);
  CHECK(wg::core::Lake{}.outlet_cell == kNoCell);
  CHECK(wg::core::MineralDeposit{}.cell == kNoCell);
  CHECK(wg::core::Region{}.name_id == -1);
}

TEST_CASE("core.vector_layers river graph checks array lengths") {
  wg::core::RiverGraph g;
  CHECK(g.empty());
  CHECK(g.consistent());
  g.cell = {10, 11};
  g.downstream = {1, -1};
  g.discharge_m3_yr = {5.0F, 9.0F};
  g.width_class = {1, 2};
  g.order = {1, 1};
  CHECK(g.size() == 2);
  CHECK_FALSE(g.consistent());
  g.name_id = {-1, -1};
  CHECK(g.consistent());
  g.clear();
  CHECK(g.empty());
  CHECK(g.consistent());
}

TEST_CASE("core.vector_layers clear empties every layer") {
  VectorLayers layers;
  layers.peaks.push_back({});
  layers.volcanoes.push_back({});
  layers.river_graph.cell.push_back(0);
  layers.lakes.push_back({});
  layers.regions.push_back({});
  layers.plates.push_back({});
  layers.geo_columns.push_back({});
  layers.minerals.push_back({});
  CHECK_FALSE(layers.empty());
  layers.clear();
  CHECK(layers.empty());
}
