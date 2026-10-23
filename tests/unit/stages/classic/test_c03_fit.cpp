#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/terrain/edge_ocean.hpp"
#include "wg/terrain/hypsometry.hpp"

using wg::core::FieldId;
using wg::core::Peak;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::Pipeline;
using wg::pipeline::StageResult;

namespace {

constexpr std::int32_t kN = 33;

std::vector<float> elevation_of(const WorldState& state) {
  auto view = state.read<float>(FieldId::elevation);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

// A world holding only elevation z and the given peaks.
WorldState world_with(const std::vector<float>& z, std::vector<Peak> peaks) {
  auto state = WorldState::create(wg::test::make_grid(kN));
  REQUIRE(state);
  auto view = state->write<float>(FieldId::elevation);
  REQUIRE(view);
  std::copy(z.begin(), z.end(), view->data.begin());
  state->layers().peaks = std::move(peaks);
  return std::move(*state);
}

// Land at 200 with three cells at 400.
std::vector<float> three_summits(const wg::core::Grid& grid) {
  std::vector<float> z(static_cast<std::size_t>(grid.cells()), 200.0F);
  for (const std::int32_t x : {8, 16, 24}) {
    z[static_cast<std::size_t>(grid.idx(x, 16))] = 400.0F;
  }
  return z;
}

GenParams without_remap() {
  GenParams p;
  p.grid.size = kN;
  p.elevation_post.hypsometry_enabled = false;
  return p;
}

StageResult run_c03(WorldState& state, const GenParams& p) {
  auto stage = wg::test::make_classic_stage("c03_fit");
  wg::core::SerialExecutor exec;
  return wg::test::run_stage_with_repair(*stage, state, p, 1, exec);
}

// c01 to c03 through a Pipeline.
WorldState fitted_world(const GenParams& params, std::uint64_t seed, wg::core::Executor& exec,
                        std::vector<float>* seeded = nullptr) {
  const wg::pipeline::StageRegistry registry = wg::test::classic_registry();
  const wg::pipeline::Profile profile{.name = "c03",
                                      .stages = {"c01_allocate", "c02_seed", "c03_fit"}};
  auto pipeline = Pipeline::create(profile, registry);
  REQUIRE(pipeline);
  auto out = pipeline->run(params, seed, wg::pipeline::RunContext{.exec = exec});
  REQUIRE(out);
  if (seeded != nullptr) {
    // Re-run c01 and c02 alone for the elevation before the remap.
    const wg::pipeline::Profile seed_only{.name = "seed", .stages = {"c01_allocate", "c02_seed"}};
    auto p2 = Pipeline::create(seed_only, registry);
    REQUIRE(p2);
    auto o2 = p2->run(params, seed, wg::pipeline::RunContext{.exec = exec});
    REQUIRE(o2);
    *seeded = elevation_of(o2->state);
  }
  return std::move(out->state);
}

}  // namespace

TEST_CASE("stages.c03_fit remaps elevation monotonically onto G") {
  GenParams p;
  p.grid.size = 65;
  wg::core::SerialExecutor exec;
  std::vector<float> seeded;
  const WorldState state = fitted_world(p, 3, exec, &seeded);
  const std::vector<float> z = elevation_of(state);
  REQUIRE(z.size() == seeded.size());
  // The order of every pair of horizontal and vertical neighbours is kept.
  std::int64_t order_changes = 0;
  for (std::size_t i = 0; i < z.size(); ++i) {
    for (const std::size_t j : {i + 1, i + 65}) {
      if (j < z.size() && ((seeded[i] < seeded[j]) != (z[i] < z[j]) ||
                           (seeded[i] == seeded[j]) != (z[i] == z[j]))) {
        ++order_changes;
      }
    }
  }
  CHECK(order_changes == 0);
  const auto [mn, mx] = std::minmax_element(z.begin(), z.end());
  CHECK(*mn >= 1.0F);
  CHECK(*mx <= 400.0F);
  // Every surviving peak sits at 400.
  for (const Peak& peak : state.layers().peaks) {
    CHECK(z[static_cast<std::size_t>(peak.cell)] == 400.0F);
  }

  GenParams off = p;
  off.elevation_post.hypsometry_enabled = false;
  CHECK(elevation_of(fitted_world(off, 3, exec)) == seeded);
}

TEST_CASE("stages.c03_fit PEAK_NUMBER_MIN fails just above the peak count") {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  const std::vector<float> z = three_summits(grid);
  // Three peaks at 400 and a stale one whose cell is not at the top.
  std::vector<Peak> peaks;
  for (const std::int32_t x : {8, 16, 24, 4}) {
    peaks.push_back(Peak{.cell = grid.idx(x, 16), .height_eu = 400.0F, .radius_cells = 3.0F});
  }
  GenParams p = without_remap();

  p.checks.peak_number_min = 3;
  WorldState ok = world_with(z, peaks);
  const StageResult passed = run_c03(ok, p);
  CHECK(passed.kind == StageResult::Kind::ok);
  CHECK(ok.layers().peaks.size() == 3);
  CHECK(passed.metrics.value("peaks") == 3.0);

  p.checks.peak_number_min = 4;
  WorldState too_few = world_with(z, peaks);
  const StageResult failed = run_c03(too_few, p);
  CHECK(failed.kind == StageResult::Kind::reject);
  CHECK(failed.check_id == "PEAK_NUMBER_MIN");
  CHECK(failed.measured == 3.0);
  CHECK(failed.required == 4.0);
}

TEST_CASE("stages.c03_fit repairs missing edge oceans") {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  const std::vector<float> z = three_summits(grid);
  GenParams p = without_remap();

  WorldState plain = world_with(z, {});
  const StageResult nothing_required = run_c03(plain, p);
  CHECK(nothing_required.kind == StageResult::Kind::ok);
  CHECK(nothing_required.metrics.value("ocean_edges_partial") == 0.0);

  p.checks.complete_ocean_edge_min = 2;
  p.checks.partial_ocean_edge_min = 3;
  WorldState state = world_with(z, {});
  const StageResult r = run_c03(state, p);
  CHECK(r.kind == StageResult::Kind::ok);  // the runner turns an ok repair into repaired
  auto edges = wg::terrain::count_ocean_edges(elevation_of(state), grid);
  REQUIRE(edges);
  CHECK(edges->complete >= 2);
  CHECK(edges->partial >= 3);

  // The run alone rejects with the measured and required values.
  auto stage = wg::test::make_classic_stage("c03_fit");
  wg::core::SerialExecutor exec;
  WorldState fresh = world_with(z, {});
  const StageResult rejected = wg::test::run_stage(*stage, fresh, p, 1, exec);
  CHECK(rejected.kind == StageResult::Kind::reject);
  CHECK(rejected.check_id == "COMPLETE_OCEAN_EDGE_MIN");
  CHECK(rejected.measured == 0.0);
  CHECK(rejected.required == 2.0);
}

TEST_CASE("stages.c03_fit is reproducible per seed and independent of the thread count") {
  GenParams p;
  p.grid.size = 65;
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const std::vector<float> a = elevation_of(fitted_world(p, 11, serial));
  CHECK(elevation_of(fitted_world(p, 11, **pool)) == a);
  CHECK(elevation_of(fitted_world(p, 12, serial)) != a);
}

TEST_CASE("stages.c03_fit returns cancelled when cancel is requested") {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  WorldState state = world_with(three_summits(grid), {});
  GenParams p;
  p.grid.size = kN;
  auto stage = wg::test::make_classic_stage("c03_fit");
  wg::core::SerialExecutor exec;
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::test::run_stage(*stage, state, p, 1, exec, cancel).kind ==
        StageResult::Kind::cancelled);
}

TEST_CASE("stages.c03_fit reports an error without elevation") {
  auto state = WorldState::create(wg::test::make_grid(kN));
  REQUIRE(state);
  auto stage = wg::test::make_classic_stage("c03_fit");
  wg::core::SerialExecutor exec;
  GenParams p;
  p.grid.size = kN;
  const StageResult r = wg::test::run_stage(*stage, *state, p, 1, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == wg::core::StatusCode::not_found);
}
