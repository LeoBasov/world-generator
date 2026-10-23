#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "support/fixtures.hpp"
#include "support/stage_run.hpp"
#include "wg/climate/latitude.hpp"

using wg::core::FieldId;
using wg::params::Poles;
using wg::pipeline::FieldSet;
using wg::pipeline::StageResult;

TEST_CASE("stages.c01_allocate declares its contract") {
  auto stage = wg::test::make_classic_stage("c01_allocate");
  CHECK(stage->id() == "c01_allocate");
  CHECK(stage->inputs() == FieldSet{});
  CHECK(stage->outputs() == FieldSet{FieldId::latitude});
}

TEST_CASE("stages.c01_allocate writes the latitude of each pole option") {
  auto stage = wg::test::make_classic_stage("c01_allocate");
  wg::core::SerialExecutor exec;
  for (const Poles poles : {Poles::none, Poles::north, Poles::south, Poles::both}) {
    CAPTURE(static_cast<int>(poles));
    const wg::core::Grid grid = wg::test::make_grid(33);
    auto state = wg::core::WorldState::create(grid);
    REQUIRE(state);
    wg::params::GenParams params;
    params.poles = poles;
    const StageResult r = wg::test::run_stage(*stage, *state, params, 7, exec);
    REQUIRE(r.kind == StageResult::Kind::ok);
    CHECK(r.metrics.value("cells") == static_cast<double>(grid.cells()));
    auto phi = state->read<FieldId::latitude>();
    REQUIRE(phi);
    for (std::int32_t y = 0; y < grid.n; ++y) {
      const auto expected = static_cast<float>(wg::climate::latitude_of_row(poles, grid.n, y));
      for (std::int32_t x = 0; x < grid.n; ++x) {
        REQUIRE((*phi)(x, y) == expected);
      }
    }
  }
}

TEST_CASE("stages.c01_allocate returns cancelled when cancel is requested") {
  auto stage = wg::test::make_classic_stage("c01_allocate");
  wg::core::SerialExecutor exec;
  auto state = wg::core::WorldState::create(wg::test::make_grid(17));
  REQUIRE(state);
  wg::core::CancelFlag cancel;
  cancel.request();
  const wg::params::GenParams params;
  CHECK(wg::test::run_stage(*stage, *state, params, 7, exec, cancel).kind ==
        StageResult::Kind::cancelled);
}
