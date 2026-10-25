#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/geology/aquifer.hpp"
#include "wg/geology/column.hpp"
#include "wg/geology/regions.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::core::WorldState;
using wg::params::BiomeId;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

GenParams small_params(std::int32_t n) {
  GenParams p;
  p.grid.size = n;
  p.erosion.erosion_cycle_count = 4;
  p.rivers.small_ocean_min_area = 4;
  p.elevation_post.final_iterations = 5;
  return p;
}

template <class T>
std::vector<T> field(const WorldState& state, FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

const std::vector<std::string> kClassicGeology = {
    "c01_allocate", "c02_seed",   "c03_fit",     "c04_vegetation", "c05_shape",
    "c06_rivers",   "c07_smooth", "c08_climate", "c09_biomes",     "c10_subsurface"};

const wg::geology::GeologyTables& geo_tables() {
  static const wg::geology::GeologyTables t = [] {
    auto created = wg::geology::GeologyTables::create(wg::test::repo_tables().rocks,
                                                      wg::test::repo_tables().soils);
    REQUIRE(created);
    return std::move(*created);
  }();
  return t;
}

}  // namespace

TEST_CASE("stages.c10_subsurface declares its contract") {
  using wg::pipeline::LayerId;
  auto stage = wg::test::make_classic_stage("c10_subsurface");
  CHECK(stage->inputs() == wg::pipeline::FieldSet{FieldId::volcanism, FieldId::drainage,
                                                  FieldId::elevation, FieldId::biome,
                                                  LayerId::volcanoes});
  CHECK(stage->outputs() == wg::pipeline::FieldSet{FieldId::soil_depth, FieldId::geo_region,
                                                   FieldId::aquifer, LayerId::geo_columns,
                                                   LayerId::minerals});
}

TEST_CASE("stages.c10_subsurface builds valid columns, soils, aquifers and deposits (A21)") {
  for (const std::int32_t n : {33, 65}) {
    CAPTURE(n);
    const GenParams p = small_params(n);
    wg::core::SerialExecutor exec;
    const WorldState state = wg::test::run_classic(kClassicGeology, p, 7, exec);
    const auto z = field<float>(state, FieldId::elevation);
    const auto biome = field<BiomeId>(state, FieldId::biome);
    const auto region = field<std::int32_t>(state, FieldId::geo_region);
    const auto soil = field<float>(state, FieldId::soil_depth);
    const auto aquifer = field<std::uint8_t>(state, FieldId::aquifer);
    const auto& columns = state.layers().geo_columns;
    REQUIRE_FALSE(columns.empty());
    for (std::size_t r = 0; r < columns.size(); ++r) {
      CAPTURE(r);
      CHECK(columns[r].geo_region == static_cast<std::int32_t>(r));
      CHECK(wg::geology::check_column(columns[r], geo_tables()).ok());
    }
    const BiomeId mountain = wg::test::repo_tables().biomes.mountain;
    std::set<std::int32_t> seen;
    for (std::size_t k = 0; k < region.size(); ++k) {
      REQUIRE(region[k] >= 0);
      REQUIRE(u(region[k]) < columns.size());
      seen.insert(region[k]);
      CHECK(soil[k] >= 0.0F);
      CHECK(soil[k] <= static_cast<float>(p.geology.soil_max_depth));
      if (biome[k] == mountain && z[k] >= 100.0F) {
        CHECK(soil[k] == 0.0F);
        CHECK(columns[u(region[k])].soil_layers.empty());
      }
      if (aquifer[k] != static_cast<std::uint8_t>(wg::geology::AquiferClass::none)) {
        CHECK(z[k] >= 100.0F);
        CHECK(wg::geology::column_has_aquifer(columns[u(region[k])], geo_tables()));
      }
    }
    CHECK(seen.size() == columns.size());
    for (const wg::core::Volcano& v : state.layers().volcanoes) {
      if (state.grid().contains(v.cell)) {
        const auto& top = columns[u(region[u(v.cell)])].layers.front();
        CHECK(geo_tables().rock_class(top.rock_id) == wg::params::RockClass::igneous_extrusive);
      }
    }
    const auto& deposits = state.layers().minerals;
    CHECK_FALSE(deposits.empty());
    for (const wg::core::MineralDeposit& d : deposits) {
      REQUIRE(state.grid().contains(d.cell));
      const auto& column = columns[u(region[u(d.cell)])];
      CHECK(d.host_layer < column.layers.size());
      CHECK(d.mineral_id < wg::test::repo_tables().minerals.minerals.size());
    }
  }
}

TEST_CASE("stages.c10_subsurface is reproducible per seed and independent of the thread count") {
  const GenParams p = small_params(33);
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const WorldState a = wg::test::run_classic(kClassicGeology, p, 9, serial);
  const WorldState b = wg::test::run_classic(kClassicGeology, p, 9, **pool);
  CHECK(field<std::int32_t>(a, FieldId::geo_region) == field<std::int32_t>(b, FieldId::geo_region));
  CHECK(field<float>(a, FieldId::soil_depth) == field<float>(b, FieldId::soil_depth));
  CHECK(field<std::uint8_t>(a, FieldId::aquifer) == field<std::uint8_t>(b, FieldId::aquifer));
  CHECK(a.layers().geo_columns == b.layers().geo_columns);
  CHECK(a.layers().minerals == b.layers().minerals);
  const WorldState c = wg::test::run_classic(kClassicGeology, p, 10, serial);
  CHECK(a.layers().geo_columns != c.layers().geo_columns);
}

TEST_CASE("stages.c10_subsurface needs the data tables and its inputs") {
  const GenParams p = small_params(33);
  wg::core::SerialExecutor exec;
  auto stage = wg::test::make_classic_stage("c10_subsurface");
  WorldState state = wg::test::run_classic(kClassicGeology, p, 7, exec);
  StageResult r = wg::test::run_stage(*stage, state, p, 1, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::invalid_argument);

  auto empty = WorldState::create(state.grid());
  REQUIRE(empty);
  r = wg::test::run_stage(*stage, *empty, p, 1, exec, wg::pipeline::never_cancelled(),
                          &wg::test::repo_tables());
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::not_found);
}
