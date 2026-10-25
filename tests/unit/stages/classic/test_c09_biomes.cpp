#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "support/stage_run.hpp"
#include "wg/classify/biome.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/water.hpp"

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

const std::vector<std::string> kClassicBiomes = {"c01_allocate",   "c02_seed",    "c03_fit",
                                                 "c04_vegetation", "c05_shape",   "c06_rivers",
                                                 "c07_smooth",     "c08_climate", "c09_biomes"};

}  // namespace

TEST_CASE("stages.c09_biomes declares its contract") {
  using wg::pipeline::LayerId;
  auto stage = wg::test::make_classic_stage("c09_biomes");
  CHECK(stage->inputs() == wg::pipeline::FieldSet{FieldId::elevation, FieldId::rainfall,
                                                  FieldId::drainage, FieldId::temperature,
                                                  FieldId::salinity, FieldId::water});
  CHECK(stage->outputs() ==
        wg::pipeline::FieldSet{FieldId::biome, FieldId::region_id, LayerId::regions});
}

TEST_CASE("stages.c09_biomes classifies every cell and labels the regions (A20)") {
  for (const std::int32_t n : {33, 65}) {
    CAPTURE(n);
    const GenParams p = small_params(n);
    wg::core::SerialExecutor exec;
    const WorldState state = wg::test::run_classic(kClassicBiomes, p, 7, exec);
    const auto z = field<float>(state, FieldId::elevation);
    const auto rain = field<float>(state, FieldId::rainfall);
    const auto d = field<float>(state, FieldId::drainage);
    const auto t = field<float>(state, FieldId::temperature);
    const auto s = field<std::uint8_t>(state, FieldId::salinity);
    const auto water = field<std::uint8_t>(state, FieldId::water);
    const auto biome = field<BiomeId>(state, FieldId::biome);
    const auto ids = field<std::int32_t>(state, FieldId::region_id);
    auto classifier = wg::classify::BiomeClassifier::create(wg::test::repo_tables().biomes);
    REQUIRE(classifier);
    for (std::size_t k = 0; k < biome.size(); ++k) {
      REQUIRE(biome[k] == classifier
                              ->classify({.elevation = z[k],
                                          .rainfall = rain[k],
                                          .drainage = d[k],
                                          .temperature = t[k],
                                          .salinity = s[k],
                                          .water = water[k],
                                          .has_water = true})
                              .biome);
    }
    const std::vector<wg::core::Region>& regions = state.layers().regions;
    REQUIRE(!regions.empty());
    std::vector<std::int64_t> area(regions.size(), 0);
    std::int64_t land = 0;
    for (std::size_t k = 0; k < ids.size(); ++k) {
      REQUIRE(ids[k] >= 0);
      REQUIRE(u(ids[k]) < regions.size());
      ++area[u(ids[k])];
      const auto& r = regions[u(ids[k])];
      CHECK(std::find(r.biomes.begin(), r.biomes.end(), biome[k]) != r.biomes.end());
      if (water[k] != static_cast<std::uint8_t>(wg::hydro::WaterClass::ocean) &&
          water[k] != static_cast<std::uint8_t>(wg::hydro::WaterClass::lake)) {
        ++land;
      }
    }
    std::int64_t total = 0;
    std::int64_t land_regions = 0;
    const auto& types = wg::test::repo_tables().biomes.region_types;
    for (std::size_t r = 0; r < regions.size(); ++r) {
      CHECK(regions[r].area_cells == area[r]);
      total += regions[r].area_cells;
      const std::string& type = types[regions[r].type];
      if (type != "ocean" && type != "lake") {
        land_regions += regions[r].area_cells;
      }
    }
    CHECK(total == state.grid().cells());
    CHECK(land_regions == land);
  }
}

TEST_CASE("stages.c09_biomes is reproducible per seed and independent of the thread count") {
  const GenParams p = small_params(33);
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const WorldState a = wg::test::run_classic(kClassicBiomes, p, 9, serial);
  const WorldState b = wg::test::run_classic(kClassicBiomes, p, 9, **pool);
  CHECK(field<BiomeId>(a, FieldId::biome) == field<BiomeId>(b, FieldId::biome));
  CHECK(field<std::int32_t>(a, FieldId::region_id) == field<std::int32_t>(b, FieldId::region_id));
  CHECK(a.layers().regions == b.layers().regions);
}

TEST_CASE("stages.c09_biomes needs the data tables and its inputs") {
  const GenParams p = small_params(33);
  wg::core::SerialExecutor exec;
  auto stage = wg::test::make_classic_stage("c09_biomes");
  WorldState state = wg::test::run_classic(kClassicBiomes, p, 7, exec);
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
