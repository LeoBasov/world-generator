#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "support/fixtures.hpp"
#include "support/hydro_chain.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/hydro/river_network.hpp"
#include "wg/hydro/water.hpp"
#include "wg/terrain/elevation.hpp"

using wg::core::FieldId;
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
  return p;
}

template <class T>
std::vector<T> field(const WorldState& state, FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

void put(WorldState& state, FieldId id, const std::vector<float>& values) {
  auto view = state.write<float>(id);
  REQUIRE(view);
  std::copy(values.begin(), values.end(), view->data.begin());
}

// The rough island of the river-network tests as C6 input.
WorldState island(std::uint64_t seed) {
  const wg::core::Grid grid = wg::test::make_grid(kN, 1000.0, 10.0);
  const wg::test::HydroChain h = wg::test::rough_island(grid, seed);
  auto state = WorldState::create(grid);
  REQUIRE(state);
  put(*state, FieldId::elevation, h.z);
  put(*state, FieldId::rainfall, h.rainfall);
  put(*state, FieldId::drainage, h.drainage);
  return std::move(*state);
}

StageResult run_c06(WorldState& state, const GenParams& p, std::uint64_t seed,
                    wg::core::Executor& exec,
                    const wg::core::CancelFlag& cancel = wg::pipeline::never_cancelled()) {
  auto stage = wg::test::make_classic_stage("c06_rivers");
  return wg::test::run_stage(*stage, state, p, seed, exec, cancel);
}

}  // namespace

TEST_CASE("stages.c06_rivers writes a river network that ends in ocean or lakes (A17)") {
  const GenParams p = small_params();
  wg::core::SerialExecutor exec;
  for (std::uint64_t seed = 1; seed <= 5; ++seed) {
    CAPTURE(seed);
    WorldState state = island(seed);
    const StageResult r = run_c06(state, p, seed, exec);
    REQUIRE(r.kind == StageResult::Kind::ok);
    const wg::core::Grid& grid = state.grid();
    const std::vector<float> z = field<float>(state, FieldId::elevation);
    const std::vector<std::int32_t> receivers = field<std::int32_t>(state, FieldId::flow_receiver);
    const std::vector<std::int32_t> lake_id = field<std::int32_t>(state, FieldId::lake_id);
    const std::vector<float> area = field<float>(state, FieldId::flow_accum);
    const std::vector<float> q = field<float>(state, FieldId::discharge);
    const std::vector<std::uint8_t> water = field<std::uint8_t>(state, FieldId::water);
    const std::vector<std::uint8_t> river_class = field<std::uint8_t>(state, FieldId::river_class);
    const wg::core::VectorLayers& layers = state.layers();
    const wg::core::RiverGraph& g = layers.river_graph;

    REQUIRE(!g.empty());
    CHECK(wg::hydro::check_river_graph(g, z, receivers, lake_id, grid).ok());
    CHECK(r.metrics.value("river_nodes") == static_cast<double>(g.size()));
    CHECK(r.metrics.value("sources_after").value_or(0.0) > 0.0);
    CHECK(r.metrics.value("lake_bulges").value_or(-1.0) <= p.rivers.lake_bulge_count);
    for (std::size_t k = 0; k < g.size(); ++k) {
      const auto c = u(g.cell[k]);
      CHECK(water[c] == static_cast<std::uint8_t>(wg::hydro::WaterClass::river));
      CHECK(river_class[c] == g.width_class[k]);
      CHECK(g.discharge_m3_yr[k] == q[c]);
      if (g.downstream[k] >= 0) {
        CHECK(g.discharge_m3_yr[u(g.downstream[k])] >= g.discharge_m3_yr[k]);
      }
    }
    const double cell_area = grid.dx_m * grid.dx_m;
    for (std::size_t i = 0; i < z.size(); ++i) {
      if (z[i] < wg::terrain::kSeaLevel) {
        CHECK(water[i] == static_cast<std::uint8_t>(wg::hydro::WaterClass::ocean));
        CHECK(receivers[i] == -1);
      } else {
        CHECK(static_cast<double>(area[i]) >= cell_area);
        CHECK(q[i] >= 0.0F);
      }
      if (lake_id[i] >= 0) {
        CHECK(water[i] == static_cast<std::uint8_t>(wg::hydro::WaterClass::lake));
      }
    }
    for (const wg::core::Lake& lake : layers.lakes) {
      for (const std::int64_t c : lake.cells) {
        CHECK(lake_id[u(c)] == lake.id);
      }
    }
  }
}

TEST_CASE("stages.c06_rivers is deterministic and independent of the thread count") {
  const GenParams p = small_params();
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  WorldState a = island(11);
  WorldState b = island(11);
  REQUIRE(run_c06(a, p, 3, serial).kind == StageResult::Kind::ok);
  REQUIRE(run_c06(b, p, 3, **pool).kind == StageResult::Kind::ok);
  CHECK(field<float>(a, FieldId::elevation) == field<float>(b, FieldId::elevation));
  CHECK(field<std::uint8_t>(a, FieldId::water) == field<std::uint8_t>(b, FieldId::water));
  CHECK(a.layers().river_graph == b.layers().river_graph);
  CHECK(a.layers().lakes == b.layers().lakes);
}

TEST_CASE("stages.c06_rivers rejects on RIVER_MINS before and after erosion") {
  GenParams p = small_params();
  wg::core::SerialExecutor exec;
  WorldState ok = island(2);
  const StageResult base = run_c06(ok, p, 1, exec);
  REQUIRE(base.kind == StageResult::Kind::ok);
  const double before = base.metrics.value("sources_before").value_or(-1.0);
  const double after = base.metrics.value("sources_after").value_or(-1.0);

  p.checks.river_mins = {static_cast<std::int32_t>(before) + 1, 0};
  WorldState s1 = island(2);
  const StageResult r1 = run_c06(s1, p, 1, exec);
  CHECK(r1.kind == StageResult::Kind::reject);
  CHECK(r1.check_id == "RIVER_MINS[0]");
  CHECK(r1.measured == before);
  CHECK(r1.required == before + 1.0);

  p.checks.river_mins = {static_cast<std::int32_t>(before), static_cast<std::int32_t>(after) + 1};
  WorldState s2 = island(2);
  const StageResult r2 = run_c06(s2, p, 1, exec);
  CHECK(r2.kind == StageResult::Kind::reject);
  CHECK(r2.check_id == "RIVER_MINS[1]");
  CHECK(r2.measured == after);
}

TEST_CASE("stages.c06_rivers reports missing inputs and cancel") {
  const GenParams p = small_params();
  wg::core::SerialExecutor exec;
  auto state = WorldState::create(wg::test::make_grid(kN, 1000.0, 10.0));
  REQUIRE(state);
  put(*state, FieldId::elevation, std::vector<float>(u(state->grid().cells()), 150.0F));
  const StageResult missing = run_c06(*state, p, 1, exec);
  CHECK(missing.kind == StageResult::Kind::error);
  CHECK(missing.status.code == wg::core::StatusCode::not_found);

  WorldState fresh = island(4);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(run_c06(fresh, p, 1, exec, cancel).kind == StageResult::Kind::cancelled);
}

TEST_CASE("stages.c06_rivers feeds C7 through the classic pipeline") {
  GenParams p = small_params();
  p.elevation_post.final_iterations = 5;
  const wg::pipeline::StageRegistry registry = wg::test::classic_registry();
  const wg::pipeline::Profile profile{
      .name = "classic_rivers",
      .stages = {"c01_allocate", "c02_seed", "c03_fit", "c05_shape", "c06_rivers", "c07_smooth"}};
  auto pipeline = wg::pipeline::Pipeline::create(profile, registry);
  REQUIRE(pipeline);
  wg::core::SerialExecutor exec;
  auto out = pipeline->run(p, 5, wg::pipeline::RunContext{.exec = exec});
  REQUIRE(out);
  const WorldState& state = out->state;
  const wg::core::RiverGraph& g = state.layers().river_graph;
  REQUIRE(!g.empty());
  // After C7 every river cell still lies above its downstream cell.
  const std::vector<float> z = field<float>(state, FieldId::elevation);
  for (std::size_t k = 0; k < g.size(); ++k) {
    if (g.downstream[k] >= 0) {
      CHECK(static_cast<double>(z[u(g.cell[k])]) >=
            static_cast<double>(z[u(g.cell[u(g.downstream[k])])]) + p.rivers.min_drop);
    }
  }
}
