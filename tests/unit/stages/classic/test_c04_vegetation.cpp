#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/bio/vegetation.hpp"
#include "wg/classify/biome.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::core::WorldState;
using wg::params::BiomeId;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::int32_t kN = 33;

void put(WorldState& state, FieldId id, const std::vector<float>& values) {
  auto view = state.write<float>(id);
  REQUIRE(view);
  std::copy(values.begin(), values.end(), view->data.begin());
}

template <class T>
std::vector<T> field(const WorldState& state, FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

// Elevation rising eastwards from 40 to 360, rainfall southwards from 0 to 100, drainage and
// temperature in stripes: every base row, the ocean and the mountains appear.
WorldState c04_input() {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  auto state = WorldState::create(grid);
  REQUIRE(state);
  put(*state, FieldId::elevation,
      wg::test::make_field(grid, [](std::int32_t x, std::int32_t) { return 40.0 + (10.0 * x); }));
  put(*state, FieldId::rainfall, wg::test::make_field(grid, [](std::int32_t, std::int32_t y) {
    return 100.0 * y / (kN - 1);
  }));
  put(*state, FieldId::drainage, wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    return static_cast<double>(((7 * x) + (3 * y)) % 101);
  }));
  put(*state, FieldId::temperature, wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    return -20.0 + static_cast<double>(((5 * y) + x) % 120);
  }));
  return std::move(*state);
}

StageResult run_c04(WorldState& state, const GenParams& p, wg::core::Executor& exec,
                    const wg::params::DataTables* tables = &wg::test::repo_tables()) {
  auto stage = wg::test::make_classic_stage("c04_vegetation");
  return wg::test::run_stage(*stage, state, p, 1, exec, wg::pipeline::never_cancelled(), tables);
}

}  // namespace

TEST_CASE("stages.c04_vegetation declares its contract") {
  auto stage = wg::test::make_classic_stage("c04_vegetation");
  CHECK(stage->inputs() == wg::pipeline::FieldSet{FieldId::elevation, FieldId::rainfall,
                                                  FieldId::temperature, FieldId::drainage});
  CHECK(stage->outputs() == wg::pipeline::FieldSet{FieldId::vegetation, FieldId::biome});
}

TEST_CASE("stages.c04_vegetation writes vegetation and the provisional biome") {
  GenParams p;
  p.grid.size = kN;
  WorldState state = c04_input();
  wg::core::SerialExecutor exec;
  const StageResult r = run_c04(state, p, exec);
  REQUIRE(r.kind == StageResult::Kind::ok);
  CHECK(r.metrics.value("initial_regions") > 1.0);

  const auto z = field<float>(state, FieldId::elevation);
  const auto rain = field<float>(state, FieldId::rainfall);
  const auto d = field<float>(state, FieldId::drainage);
  const auto t = field<float>(state, FieldId::temperature);
  const auto v = field<float>(state, FieldId::vegetation);
  const auto biome = field<BiomeId>(state, FieldId::biome);
  std::vector<float> want(v.size());
  REQUIRE(wg::bio::compute_vegetation(
              state.grid(),
              {.temperature = t, .rainfall = rain, .elevation = z, .drainage = d, .water = {}},
              p.vegetation, p.temperature, want, exec)
              .ok());
  CHECK(v == want);
  auto classifier = wg::classify::BiomeClassifier::create(wg::test::repo_tables().biomes);
  REQUIRE(classifier);
  for (std::size_t k = 0; k < biome.size(); ++k) {
    REQUIRE(biome[k] == classifier
                            ->classify({.elevation = z[k],
                                        .rainfall = rain[k],
                                        .drainage = d[k],
                                        .temperature = t[k],
                                        .salinity = 0,
                                        .water = 0,
                                        .has_water = false})
                            .biome);
  }
}

TEST_CASE("stages.c04_vegetation rejects on the initial REGION_COUNTS") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor exec;
  WorldState probe = c04_input();
  REQUIRE(run_c04(probe, p, exec).kind == StageResult::Kind::ok);

  p.checks.region_counts.mountains = {1, 1, 0};
  WorldState enough = c04_input();
  CHECK(run_c04(enough, p, exec).kind == StageResult::Kind::ok);

  p.checks.region_counts.mountains = {100000, 0, 0};
  WorldState too_few_cells = c04_input();
  StageResult r = run_c04(too_few_cells, p, exec);
  CHECK(r.kind == StageResult::Kind::reject);
  CHECK(r.check_id == "REGION_COUNTS.MOUNTAINS[0]");
  CHECK(r.required == 100000.0);

  p.checks.region_counts.mountains = {0, 50, 0};
  WorldState too_few_regions = c04_input();
  r = run_c04(too_few_regions, p, exec);
  CHECK(r.kind == StageResult::Kind::reject);
  CHECK(r.check_id == "REGION_COUNTS.MOUNTAINS[1]");
  CHECK(r.measured == 1.0);
}

TEST_CASE("stages.c04_vegetation is independent of the thread count") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  WorldState a = c04_input();
  WorldState b = c04_input();
  REQUIRE(run_c04(a, p, serial).kind == StageResult::Kind::ok);
  REQUIRE(run_c04(b, p, **pool).kind == StageResult::Kind::ok);
  CHECK(field<float>(a, FieldId::vegetation) == field<float>(b, FieldId::vegetation));
  CHECK(field<BiomeId>(a, FieldId::biome) == field<BiomeId>(b, FieldId::biome));
}

TEST_CASE("stages.c04_vegetation needs the data tables and its inputs, and honours cancel") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor exec;
  WorldState state = c04_input();
  StageResult r = run_c04(state, p, exec, nullptr);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::invalid_argument);

  auto empty = WorldState::create(wg::test::make_grid(kN));
  REQUIRE(empty);
  r = run_c04(*empty, p, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == StatusCode::not_found);

  WorldState fresh = c04_input();
  wg::core::CancelFlag cancel;
  cancel.request();
  auto stage = wg::test::make_classic_stage("c04_vegetation");
  CHECK(wg::test::run_stage(*stage, fresh, p, 1, exec, cancel, &wg::test::repo_tables()).kind ==
        StageResult::Kind::cancelled);
}
