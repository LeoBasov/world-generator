#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/fixtures.hpp"
#include "support/source_files.hpp"
#include "support/stage_run.hpp"
#include "wg/climate/latitude.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/fractal/fractal.hpp"
#include "wg/fractal/peaks.hpp"
#include "wg/io/hash.hpp"
#include "wg/params/load.hpp"

using wg::core::FieldId;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::FieldSet;
using wg::pipeline::LayerId;
using wg::pipeline::Pipeline;
using wg::pipeline::StageResult;

namespace {

constexpr std::array<FieldId, 6> kPrimary = {FieldId::elevation,   FieldId::rainfall,
                                             FieldId::temperature, FieldId::drainage,
                                             FieldId::volcanism,   FieldId::savagery};

GenParams params_of_size(std::int32_t n) {
  GenParams p;
  p.grid.size = n;
  return p;
}

// Runs c01_allocate and c02_seed through a Pipeline.
WorldState seed_world(const GenParams& params, std::uint64_t seed, wg::core::Executor& exec) {
  const wg::pipeline::StageRegistry registry = wg::test::classic_registry();
  const wg::pipeline::Profile profile{.name = "classic_seed",
                                      .stages = {"c01_allocate", "c02_seed"}};
  auto pipeline = Pipeline::create(profile, registry);
  REQUIRE(pipeline);
  auto out = pipeline->run(params, seed, wg::pipeline::RunContext{.exec = exec});
  REQUIRE(out);
  CHECK(out->report.stages.size() == 2);
  return std::move(out->state);
}

std::span<const float> values(const WorldState& state, FieldId id) {
  auto view = state.read<float>(id);
  REQUIRE(view);
  return view->data;
}

std::uint64_t hash_of(const WorldState& state) {
  std::vector<std::byte> all;
  for (const FieldId id : kPrimary) {
    const auto bytes = std::as_bytes(values(state, id));
    all.insert(all.end(), bytes.begin(), bytes.end());
  }
  for (const wg::core::Peak& p : state.layers().peaks) {
    const auto bytes = std::as_bytes(std::span<const std::int64_t>(&p.cell, 1));
    all.insert(all.end(), bytes.begin(), bytes.end());
  }
  return wg::io::xxh3_64(all);
}

void check_inside(std::span<const float> v, double lo, double hi) {
  const auto [mn, mx] = std::minmax_element(v.begin(), v.end());
  CHECK(static_cast<double>(*mn) >= lo);
  CHECK(static_cast<double>(*mx) <= hi);
}

void check_ranges(const WorldState& state, const GenParams& p) {
  const wg::params::FieldsParams& f = p.fields;
  check_inside(values(state, FieldId::elevation), f.elevation.min, f.elevation.max);
  check_inside(values(state, FieldId::rainfall), f.rainfall.min, f.rainfall.max);
  check_inside(values(state, FieldId::drainage), f.drainage.min, f.drainage.max);
  check_inside(values(state, FieldId::volcanism), f.volcanism.min, f.volcanism.max);
  check_inside(values(state, FieldId::savagery), f.savagery.min, f.savagery.max);
  check_inside(values(state, FieldId::temperature),
               f.temperature.min - p.temperature.equator_pole_delta, f.temperature.max);
  for (const FieldId id : kPrimary) {
    const wg::core::FieldRange& r = wg::core::field_info(id).range;
    check_inside(values(state, id), r.min, r.max);
  }
}

}  // namespace

TEST_CASE("stages.c02_seed declares its contract") {
  auto stage = wg::test::make_classic_stage("c02_seed");
  CHECK(stage->id() == "c02_seed");
  CHECK(stage->inputs() == FieldSet{FieldId::latitude});
  CHECK(stage->outputs() == FieldSet{FieldId::elevation, FieldId::rainfall, FieldId::temperature,
                                     FieldId::drainage, FieldId::volcanism, FieldId::savagery,
                                     LayerId::peaks});
}

TEST_CASE("stages.c02_seed keeps every field inside its declared range") {
  wg::core::SerialExecutor exec;
  for (const std::int32_t n : {33, 65}) {
    for (const std::uint64_t seed : {1U, 2U, 3U}) {
      CAPTURE(n);
      CAPTURE(seed);
      const GenParams p = params_of_size(n);
      check_ranges(seed_world(p, seed, exec), p);
    }
  }
  for (const char* preset : {"df_default", "pangaea", "small_island"}) {
    CAPTURE(preset);
    auto doc = wg::params::load_params_file(
        wg::test::source_path(std::string("data/presets/") + preset + ".json"));
    REQUIRE(doc);
    doc->params.grid.size = 65;
    check_ranges(seed_world(doc->params, 5, exec), doc->params);
  }
}

TEST_CASE("stages.c02_seed adds the latitude term to the base temperature") {
  wg::core::SerialExecutor exec;
  for (const auto poles :
       {wg::params::Poles::north, wg::params::Poles::both, wg::params::Poles::none}) {
    CAPTURE(static_cast<int>(poles));
    GenParams p = params_of_size(33);
    p.poles = poles;
    const std::uint64_t seed = 21;
    const WorldState state = seed_world(p, seed, exec);
    const wg::core::Grid& grid = state.grid();

    std::vector<float> base(static_cast<std::size_t>(grid.cells()));
    const auto key = wg::core::stage_key(seed, "c02_seed", 0).derive("temperature");
    REQUIRE(wg::fractal::generate_field(grid, p.fields.temperature, key, {}, base, exec).ok());
    const std::span<const float> t = values(state, FieldId::temperature);
    const std::span<const float> phi = values(state, FieldId::latitude);
    for (std::size_t i = 0; i < base.size(); ++i) {
      const double bias = wg::climate::latitude_term(phi[i], p.temperature.equator_pole_delta);
      REQUIRE(static_cast<double>(t[i]) ==
              doctest::Approx(static_cast<double>(base[i]) + bias).epsilon(1e-6));
    }
    if (poles == wg::params::Poles::none) {
      CHECK(std::equal(base.begin(), base.end(), t.begin()));
    }
  }
}

TEST_CASE("stages.c02_seed places peak candidates on cells at 400 only") {
  wg::core::SerialExecutor exec;
  GenParams p = params_of_size(65);
  p.fields.elevation.weights = {1.0, 1.0, 1.0, 1.0, 8.0};
  std::size_t total = 0;
  for (const std::uint64_t seed : {1U, 2U, 3U, 4U}) {
    const WorldState state = seed_world(p, seed, exec);
    const std::span<const float> z = values(state, FieldId::elevation);
    const auto& peaks = state.layers().peaks;
    total += peaks.size();
    for (std::size_t k = 0; k < peaks.size(); ++k) {
      CHECK(z[static_cast<std::size_t>(peaks[k].cell)] == wg::fractal::kPeakElevation);
      CHECK(peaks[k].height_eu == wg::fractal::kPeakElevation);
      CHECK(peaks[k].radius_cells == static_cast<float>(p.elevation_post.peak_width));
      if (k > 0) {
        CHECK(peaks[k].cell != peaks[k - 1].cell);
      }
    }
  }
  CHECK(total > 0);

  p.fields.elevation.max = 399.0;
  CHECK(seed_world(p, 1, exec).layers().peaks.empty());
}

TEST_CASE("stages.c02_seed is reproducible per seed and independent of the thread count") {
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(4);
  REQUIRE(pool);
  const GenParams p = params_of_size(65);
  const std::uint64_t h = hash_of(seed_world(p, 99, serial));
  CHECK(hash_of(seed_world(p, 99, serial)) == h);
  CHECK(hash_of(seed_world(p, 99, **pool)) == h);
  CHECK(hash_of(seed_world(p, 100, serial)) != h);
}

TEST_CASE("stages.c02_seed returns cancelled when cancel is requested") {
  auto c01 = wg::test::make_classic_stage("c01_allocate");
  auto c02 = wg::test::make_classic_stage("c02_seed");
  wg::core::SerialExecutor exec;
  const GenParams p = params_of_size(33);
  auto state = WorldState::create(wg::test::make_grid(33));
  REQUIRE(state);
  REQUIRE(wg::test::run_stage(*c01, *state, p, 1, exec).kind == StageResult::Kind::ok);
  wg::core::CancelFlag cancel;
  cancel.request();
  CHECK(wg::test::run_stage(*c02, *state, p, 1, exec, cancel).kind == StageResult::Kind::cancelled);
}

TEST_CASE("stages.c02_seed reports an error without latitude") {
  auto c02 = wg::test::make_classic_stage("c02_seed");
  wg::core::SerialExecutor exec;
  const GenParams p = params_of_size(33);
  auto state = WorldState::create(wg::test::make_grid(33));
  REQUIRE(state);
  const StageResult r = wg::test::run_stage(*c02, *state, p, 1, exec);
  CHECK(r.kind == StageResult::Kind::error);
  CHECK(r.status.code == wg::core::StatusCode::not_found);
}
