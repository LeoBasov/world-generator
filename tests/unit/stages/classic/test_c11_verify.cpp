#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"

using wg::core::FieldId;
using wg::core::StatusCode;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::uint64_t kSeed = 5;

GenParams small_params() {
  GenParams p;
  p.grid.size = 33;
  p.erosion.erosion_cycle_count = 4;
  p.rivers.small_ocean_min_area = 4;
  p.elevation_post.final_iterations = 5;
  return p;
}

const std::vector<std::string> kClassicBiomes = {"c01_allocate",   "c02_seed",    "c03_fit",
                                                 "c04_vegetation", "c05_shape",   "c06_rivers",
                                                 "c07_smooth",     "c08_climate", "c09_biomes"};

StageResult run_c11(WorldState& state, const GenParams& p,
                    const wg::params::DataTables* tables = &wg::test::repo_tables()) {
  auto stage = wg::test::make_classic_stage("c11_verify");
  wg::core::SerialExecutor exec;
  return wg::test::run_stage(*stage, state, p, kSeed, exec, wg::pipeline::never_cancelled(),
                             tables);
}

// Regions of the named region type in the layer.
std::int32_t regions_of_type(const WorldState& state, const std::string& type) {
  const std::vector<std::string>& types = wg::test::repo_tables().biomes.region_types;
  const auto it = std::find(types.begin(), types.end(), type);
  REQUIRE(it != types.end());
  const auto index = static_cast<std::uint16_t>(std::distance(types.begin(), it));
  return static_cast<std::int32_t>(
      std::count_if(state.layers().regions.begin(), state.layers().regions.end(),
                    [&](const wg::core::Region& r) { return r.type == index; }));
}

}  // namespace

TEST_CASE("stages.c11_verify declares its contract") {
  auto stage = wg::test::make_classic_stage("c11_verify");
  CHECK(stage->inputs() ==
        wg::pipeline::FieldSet{FieldId::region_id, wg::pipeline::LayerId::regions});
  CHECK(stage->outputs().empty());
}

TEST_CASE("stages.c11_verify checks the final REGION_COUNTS and SUBREGION_MAX (A22)") {
  GenParams p = small_params();
  wg::core::SerialExecutor exec;
  WorldState state = wg::test::run_classic(kClassicBiomes, p, kSeed, exec);
  const auto regions = static_cast<std::int32_t>(state.layers().regions.size());
  REQUIRE(regions > 1);

  const StageResult ok = run_c11(state, p);
  CHECK(ok.kind == StageResult::Kind::ok);
  CHECK(ok.metrics.value("regions") == static_cast<double>(regions));

  // REGION_COUNTS.OCEAN[2]: passes at the count, fails one above it.
  const std::int32_t oceans = regions_of_type(state, "ocean");
  p.checks.region_counts.ocean = {0, 0, oceans};
  CHECK(run_c11(state, p).kind == StageResult::Kind::ok);
  p.checks.region_counts.ocean = {0, 0, oceans + 1};
  StageResult r = run_c11(state, p);
  CHECK(r.kind == StageResult::Kind::reject);
  CHECK(r.check_id == "REGION_COUNTS.OCEAN[2]");
  CHECK(r.measured == static_cast<double>(oceans));
  CHECK(r.required == static_cast<double>(oceans + 1));

  // The initial entries [0] and [1] are not checked here (C4 does that).
  p.checks.region_counts.ocean = {100000, 100000, 0};
  CHECK(run_c11(state, p).kind == StageResult::Kind::ok);

  // SUBREGION_MAX: passes at the region count, fails one below it.
  p.checks.region_counts.ocean = {0, 0, 0};
  p.checks.subregion_max = regions;
  CHECK(run_c11(state, p).kind == StageResult::Kind::ok);
  p.checks.subregion_max = regions - 1;
  r = run_c11(state, p);
  CHECK(r.kind == StageResult::Kind::reject);
  CHECK(r.check_id == "SUBREGION_MAX");
  CHECK(r.measured == static_cast<double>(regions));
  CHECK(r.required == static_cast<double>(regions - 1));

  // REGION_COUNTS comes before SUBREGION_MAX.
  p.checks.region_counts.ocean = {0, 0, oceans + 1};
  CHECK(run_c11(state, p).check_id == "REGION_COUNTS.OCEAN[2]");
}

TEST_CASE("stages.c11_verify needs the data tables and a consistent region layer") {
  const GenParams p = small_params();
  wg::core::SerialExecutor exec;
  WorldState state = wg::test::run_classic(kClassicBiomes, p, kSeed, exec);

  const StageResult no_tables = run_c11(state, p, nullptr);
  CHECK(no_tables.kind == StageResult::Kind::error);
  CHECK(no_tables.status.code == StatusCode::invalid_argument);

  state.layers().regions.pop_back();
  const StageResult stale = run_c11(state, p);
  CHECK(stale.kind == StageResult::Kind::error);
  CHECK(stale.status.code == StatusCode::invalid_argument);

  auto empty = WorldState::create(wg::test::make_grid(33));
  REQUIRE(empty);
  const StageResult missing = run_c11(*empty, p);
  CHECK(missing.kind == StageResult::Kind::error);
  CHECK(missing.status.code == StatusCode::not_found);
}
