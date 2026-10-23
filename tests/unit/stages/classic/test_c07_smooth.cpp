#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "support/river_fixture.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"

using wg::core::FieldId;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::int32_t kN = 33;

std::vector<float> elevation_of(const WorldState& state) {
  auto view = state.read<float>(FieldId::elevation);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

// Rough land rising east from the ocean in column 0, the fixture rivers, one peak and one
// volcano. Smoothing and bumps alone would leave the stem with flats and rises.
WorldState final_input() {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  auto state = WorldState::create(grid);
  REQUIRE(state);
  auto z = state->write<float>(FieldId::elevation);
  REQUIRE(z);
  const std::vector<float> field = wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    return x == 0 ? 60.0 : 110.0 + (5.0 * x) + (((x * 7) + (y * 13)) % 11);
  });
  std::copy(field.begin(), field.end(), z->data.begin());
  z->data[static_cast<std::size_t>(grid.idx(24, 24))] = 400.0F;
  state->layers().river_graph = wg::test::river_fixture(grid);
  state->layers().peaks = {
      wg::core::Peak{.cell = grid.idx(24, 24), .height_eu = 400.0F, .radius_cells = 3.0F}};
  state->layers().volcanoes = {
      wg::core::Volcano{.cell = grid.idx(12, 20), .magma_connected = true}};
  return std::move(*state);
}

StageResult run_c07(WorldState& state, const GenParams& p, wg::core::Executor& exec) {
  auto stage = wg::test::make_classic_stage("c07_smooth");
  return wg::test::run_stage(*stage, state, p, 1, exec);
}

}  // namespace

TEST_CASE("stages.c07_smooth keeps every river cell above its receiver (A13 acceptance)") {
  GenParams p;
  p.grid.size = kN;
  p.elevation_post.final_iterations = 20;
  WorldState state = final_input();
  const std::vector<float> before = elevation_of(state);
  wg::core::SerialExecutor exec;
  const StageResult r = run_c07(state, p, exec);
  REQUIRE(r.kind == StageResult::Kind::ok);
  CHECK(r.metrics.value("river_nodes") == 13.0);

  const std::vector<float> z = elevation_of(state);
  const wg::core::Grid& grid = state.grid();
  const wg::core::RiverGraph& g = state.layers().river_graph;
  for (std::size_t u = 0; u < g.size(); ++u) {
    const std::int64_t d = g.downstream[u];
    if (d >= 0) {
      CAPTURE(u);
      CHECK(static_cast<double>(z[static_cast<std::size_t>(g.cell[u])]) >=
            static_cast<double>(z[static_cast<std::size_t>(g.cell[static_cast<std::size_t>(d)])]) +
                p.rivers.min_drop);
    }
  }
  // The peak keeps its top, the volcano is raised, ocean cells off the rivers stay as they are.
  CHECK(z[static_cast<std::size_t>(grid.idx(24, 24))] == 400.0F);
  const auto volcano = static_cast<std::size_t>(grid.idx(12, 20));
  CHECK(z[volcano] > before[volcano]);
  CHECK(z[static_cast<std::size_t>(grid.idx(0, 20))] == 60.0F);
  CHECK(*std::max_element(z.begin(), z.end()) <= 400.0F);
}

TEST_CASE("stages.c07_smooth is independent of the thread count") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  WorldState a = final_input();
  WorldState b = final_input();
  REQUIRE(run_c07(a, p, serial).kind == StageResult::Kind::ok);
  REQUIRE(run_c07(b, p, **pool).kind == StageResult::Kind::ok);
  CHECK(elevation_of(a) == elevation_of(b));
}

TEST_CASE("stages.c07_smooth reports a malformed river graph and cancel") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor exec;
  WorldState state = final_input();
  state.layers().river_graph.downstream[0] = 1;  // a cycle without a mouth
  const StageResult r = run_c07(state, p, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == wg::core::StatusCode::invalid_argument);

  WorldState fresh = final_input();
  auto stage = wg::test::make_classic_stage("c07_smooth");
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::test::run_stage(*stage, fresh, p, 1, exec, cancel).kind ==
        StageResult::Kind::cancelled);
}
