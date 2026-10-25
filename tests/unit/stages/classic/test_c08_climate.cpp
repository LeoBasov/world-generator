#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "support/stage_run.hpp"
#include "wg/bio/vegetation.hpp"
#include "wg/climate/precipitation.hpp"
#include "wg/climate/temperature.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/water.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::int32_t kN = 33;

std::size_t u(std::int64_t i) { return static_cast<std::size_t>(i); }

GenParams small_params() {
  GenParams p;
  p.grid.size = kN;
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

// The state C8 sees: C1 to C7.
WorldState before_c08(const GenParams& p, std::uint64_t seed) {
  wg::core::SerialExecutor exec;
  return wg::test::run_classic({"c01_allocate", "c02_seed", "c03_fit", "c04_vegetation",
                                "c05_shape", "c06_rivers", "c07_smooth"},
                               p, seed, exec);
}

StageResult run_c08(WorldState& state, const GenParams& p, wg::core::Executor& exec,
                    const wg::params::DataTables* tables = &wg::test::repo_tables()) {
  auto stage = wg::test::make_classic_stage("c08_climate");
  return wg::test::run_stage(*stage, state, p, 1, exec, wg::pipeline::never_cancelled(), tables);
}

}  // namespace

TEST_CASE("stages.c08_climate declares its contract") {
  using wg::pipeline::LayerId;
  auto stage = wg::test::make_classic_stage("c08_climate");
  CHECK(stage->inputs() == wg::pipeline::FieldSet{FieldId::elevation, FieldId::rainfall,
                                                  FieldId::temperature, FieldId::vegetation,
                                                  FieldId::drainage, FieldId::water,
                                                  LayerId::river_graph, LayerId::lakes});
  CHECK(stage->outputs() == wg::pipeline::FieldSet{FieldId::rainfall, FieldId::temperature,
                                                   FieldId::vegetation, FieldId::salinity,
                                                   LayerId::lakes});
}

TEST_CASE("stages.c08_climate recomputes rainfall, temperature, vegetation and salinity in order") {
  const GenParams p = small_params();
  WorldState state = before_c08(p, 3);
  const wg::core::Grid grid = state.grid();
  const auto z = field<float>(state, FieldId::elevation);
  const auto d = field<float>(state, FieldId::drainage);
  const auto water = field<std::uint8_t>(state, FieldId::water);
  const auto rain0 = field<float>(state, FieldId::rainfall);
  const auto t0 = field<float>(state, FieldId::temperature);
  const auto v0 = field<float>(state, FieldId::vegetation);
  wg::core::SerialExecutor exec;
  REQUIRE(run_c08(state, p, exec).kind == StageResult::Kind::ok);

  // 1. Rainfall from the C2 rainfall on the final terrain.
  std::vector<float> rain(rain0.size());
  REQUIRE(wg::climate::orographic_precipitation(
              grid, {.base = rain0, .elevation = z, .drainage = d}, p.precipitation, rain, {}, exec)
              .ok());
  CHECK(field<float>(state, FieldId::rainfall) == rain);
  // 2. Temperature: lapse, C4 vegetation and the new rainfall added to C2's temperature.
  std::vector<float> t(t0.size());
  REQUIRE(wg::climate::compute_temperature(
              grid,
              {.base = t0, .latitude = {}, .elevation = z, .vegetation = v0, .rainfall = rain},
              p.temperature, t, exec)
              .ok());
  CHECK(field<float>(state, FieldId::temperature) == t);
  // 3. Vegetation from the new climate, 0 on lakes.
  std::vector<float> v(v0.size());
  REQUIRE(wg::bio::compute_vegetation(
              grid,
              {.temperature = t, .rainfall = rain, .elevation = z, .drainage = d, .water = water},
              p.vegetation, p.temperature, v, exec)
              .ok());
  CHECK(field<float>(state, FieldId::vegetation) == v);

  // 4.-5. Salinity: 100 on the ocean, one class per lake, equal to the lake's.
  const auto salinity = field<std::uint8_t>(state, FieldId::salinity);
  std::int64_t ocean = 0;
  for (std::size_t k = 0; k < salinity.size(); ++k) {
    CHECK((salinity[k] == 0 || salinity[k] == 33 || salinity[k] == 66 || salinity[k] == 100));
    if (water[k] == static_cast<std::uint8_t>(wg::hydro::WaterClass::ocean)) {
      CHECK(salinity[k] == 100);
      ++ocean;
    }
  }
  CHECK(ocean > 0);
  for (const wg::core::Lake& lake : state.layers().lakes) {
    for (const std::int64_t cell : lake.cells) {
      CHECK(salinity[u(cell)] == lake.salinity_class);
    }
  }
}

TEST_CASE("stages.c08_climate is independent of the thread count") {
  const GenParams p = small_params();
  WorldState a = before_c08(p, 4);
  WorldState b = before_c08(p, 4);
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  REQUIRE(run_c08(a, p, serial).kind == StageResult::Kind::ok);
  REQUIRE(run_c08(b, p, **pool).kind == StageResult::Kind::ok);
  for (const FieldId id : {FieldId::rainfall, FieldId::temperature, FieldId::vegetation}) {
    CHECK(field<float>(a, id) == field<float>(b, id));
  }
  CHECK(field<std::uint8_t>(a, FieldId::salinity) == field<std::uint8_t>(b, FieldId::salinity));
  CHECK(a.layers().lakes == b.layers().lakes);
}

TEST_CASE("stages.c08_climate needs the data tables and its inputs") {
  const GenParams p = small_params();
  wg::core::SerialExecutor exec;
  WorldState state = before_c08(p, 3);
  StageResult r = run_c08(state, p, exec, nullptr);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::invalid_argument);

  auto empty = WorldState::create(state.grid());
  REQUIRE(empty);
  r = run_c08(*empty, p, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::not_found);
}
