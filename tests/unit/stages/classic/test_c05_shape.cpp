#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/terrain/plains.hpp"

using wg::core::FieldId;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::StageResult;

namespace {

constexpr std::int32_t kN = 33;

void put(WorldState& state, FieldId id, const std::vector<float>& values) {
  auto view = state.write<float>(id);
  REQUIRE(view);
  std::copy(values.begin(), values.end(), view->data.begin());
}

std::vector<float> elevation_of(const WorldState& state) {
  auto view = state.read<float>(FieldId::elevation);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

// Elevation from 1 to 400 across the map; volcanism 100 in two separate areas.
WorldState shaped_input() {
  const wg::core::Grid grid = wg::test::make_grid(kN);
  auto state = WorldState::create(grid);
  REQUIRE(state);
  put(*state, FieldId::elevation, wg::test::make_field(grid, [](std::int32_t x, std::int32_t y) {
    return 1.0 + (12.0 * x) + ((x * y) % 7);
  }));
  std::vector<float> v(static_cast<std::size_t>(grid.cells()), 20.0F);
  for (const std::int64_t i : {grid.idx(3, 3), grid.idx(4, 3), grid.idx(20, 25)}) {
    v[static_cast<std::size_t>(i)] = 100.0F;
  }
  put(*state, FieldId::volcanism, v);
  return std::move(*state);
}

StageResult run_c05(WorldState& state, const GenParams& p, wg::core::Executor& exec) {
  auto stage = wg::test::make_classic_stage("c05_shape");
  return wg::test::run_stage(*stage, state, p, 1, exec);
}

}  // namespace

TEST_CASE("stages.c05_shape smooths only the plains band and places volcanoes") {
  GenParams p;
  p.grid.size = kN;
  WorldState state = shaped_input();
  const std::vector<float> before = elevation_of(state);
  wg::core::SerialExecutor exec;
  const StageResult r = run_c05(state, p, exec);
  REQUIRE(r.kind == StageResult::Kind::ok);
  CHECK(r.metrics.value("volcanoes") == 2.0);

  const std::vector<float> after = elevation_of(state);
  const wg::terrain::PlainsParams band{.z_lo = p.elevation_post.plains_z_lo,
                                       .z_hi = p.elevation_post.plains_z_hi,
                                       .taper = p.elevation_post.plains_taper};
  std::int64_t changed = 0;
  for (std::size_t k = 0; k < after.size(); ++k) {
    if (wg::terrain::band_weight(before[k], band) == 0.0) {
      CHECK(after[k] == before[k]);
    } else if (after[k] != before[k]) {
      ++changed;
    }
  }
  CHECK(changed > 0);

  const wg::core::Grid grid = state.grid();
  const auto& volcanoes = state.layers().volcanoes;
  REQUIRE(volcanoes.size() == 2);
  CHECK(volcanoes[0].cell == grid.idx(3, 3));
  CHECK(volcanoes[1].cell == grid.idx(20, 25));
}

TEST_CASE("stages.c05_shape VOLCANO_MIN fails just above the volcano count") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor exec;
  p.checks.volcano_min = 2;
  WorldState enough = shaped_input();
  CHECK(run_c05(enough, p, exec).kind == StageResult::Kind::ok);

  p.checks.volcano_min = 3;
  WorldState too_few = shaped_input();
  const StageResult r = run_c05(too_few, p, exec);
  CHECK(r.kind == StageResult::Kind::reject);
  CHECK(r.check_id == "VOLCANO_MIN");
  CHECK(r.measured == 2.0);
  CHECK(r.required == 3.0);
}

TEST_CASE("stages.c05_shape is independent of the thread count") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  WorldState a = shaped_input();
  WorldState b = shaped_input();
  REQUIRE(run_c05(a, p, serial).kind == StageResult::Kind::ok);
  REQUIRE(run_c05(b, p, **pool).kind == StageResult::Kind::ok);
  CHECK(elevation_of(a) == elevation_of(b));
  CHECK(a.layers().volcanoes == b.layers().volcanoes);
}

TEST_CASE("stages.c05_shape returns cancelled and reports missing inputs") {
  GenParams p;
  p.grid.size = kN;
  wg::core::SerialExecutor exec;
  WorldState state = shaped_input();
  auto stage = wg::test::make_classic_stage("c05_shape");
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::test::run_stage(*stage, state, p, 1, exec, cancel).kind ==
        StageResult::Kind::cancelled);

  auto empty = WorldState::create(wg::test::make_grid(kN));
  REQUIRE(empty);
  const StageResult r = run_c05(*empty, p, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == wg::core::StatusCode::not_found);
}
