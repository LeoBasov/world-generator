// A23 end-to-end runs of the classic profile data/profiles/classic.json (plan section 10, levels
// 3 and 4): small worlds finish or fail with a complete report, finished worlds keep the
// invariants of the review checklist (section 10.3) that a program can judge, and the world is
// bit-identical for 1 and 8 threads (R1).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "support/sample_world.hpp"
#include "support/source_files.hpp"
#include "support/stage_run.hpp"
#include "wg/core/thread_pool.hpp"
#include "wg/pipeline/profile.hpp"

using wg::core::DType;
using wg::core::FieldId;
using wg::core::WorldState;
using wg::params::GenParams;
using wg::pipeline::RunResult;

namespace {

wg::pipeline::Pipeline classic_pipeline() {
  auto profile =
      wg::pipeline::load_profile_file(wg::test::source_path("data/profiles/classic.json"));
  REQUIRE(profile);
  CHECK(profile->name == "classic");
  CHECK(profile->stages.size() == 11);
  auto pipeline = wg::pipeline::Pipeline::create(*profile, wg::test::classic_registry());
  REQUIRE(pipeline);
  return std::move(*pipeline);
}

RunResult run(wg::pipeline::Pipeline& pipeline, std::int32_t size, std::uint64_t seed,
              wg::core::Executor& exec) {
  GenParams p;
  p.grid.size = size;
  return pipeline.run(
      p, seed, wg::pipeline::RunContext{.exec = exec},
      wg::pipeline::RunInputs{.tables = &wg::test::repo_tables(), .initial = nullptr});
}

// Raw bytes of a present field, whatever its dtype.
std::span<const std::byte> raw_bytes(const WorldState& state, FieldId id) {
  switch (wg::core::field_info(id).dtype) {
    case DType::u8:
      return wg::test::field_bytes<std::uint8_t>(state, id);
    case DType::u16:
      return wg::test::field_bytes<std::uint16_t>(state, id);
    case DType::i32:
      return wg::test::field_bytes<std::int32_t>(state, id);
    case DType::f32:
      return wg::test::field_bytes<float>(state, id);
  }
  return {};
}

// Values of a present field as doubles.
template <class T>
std::vector<double> as_doubles(const WorldState& state, FieldId id) {
  auto view = state.read<T>(id);
  REQUIRE(view);
  return {view->data.begin(), view->data.end()};
}

std::vector<double> values(const WorldState& state, FieldId id) {
  switch (wg::core::field_info(id).dtype) {
    case DType::u8:
      return as_doubles<std::uint8_t>(state, id);
    case DType::u16:
      return as_doubles<std::uint16_t>(state, id);
    case DType::i32:
      return as_doubles<std::int32_t>(state, id);
    case DType::f32:
      return as_doubles<float>(state, id);
  }
  return {};
}

// Every value of every present field is finite and inside its registry range. A float32 field
// is compared with its bounds rounded to float32 (latitude stores float(pi / 2) > pi / 2).
void check_field_ranges(const WorldState& state) {
  for (const wg::core::FieldInfo& info : wg::core::kFields) {
    if (!state.has(info.id)) {
      continue;
    }
    CAPTURE(info.name);
    const wg::core::FieldRange& range = info.range;
    const bool f32 = info.dtype == DType::f32;
    const double lo = f32 ? static_cast<double>(static_cast<float>(range.min)) : range.min;
    const double hi = f32 ? static_cast<double>(static_cast<float>(range.max)) : range.max;
    std::int64_t bad = 0;
    for (const double v : values(state, info.id)) {
      const bool ok =
          std::isfinite(v) && (!range.has_min || (range.min_exclusive ? v > lo : v >= lo)) &&
          (!range.has_max || v <= hi) &&
          (range.values.empty() ||
           std::find(range.values.begin(), range.values.end(), v) != range.values.end());
      bad += ok ? 0 : 1;
    }
    CHECK(bad == 0);
  }
}

// Every river ends in the ocean or a lake (review checklist): the receiver of each mouth is an
// ocean or lake cell of the final water field.
void check_river_mouths(const WorldState& state) {
  const wg::core::RiverGraph& rivers = state.layers().river_graph;
  REQUIRE(rivers.consistent());
  auto receivers = state.read<FieldId::flow_receiver>();
  auto water = state.read<FieldId::water>();
  REQUIRE(receivers);
  REQUIRE(water);
  std::int64_t mouths = 0;
  for (std::size_t k = 0; k < rivers.size(); ++k) {
    if (rivers.downstream[k] >= 0) {
      continue;
    }
    ++mouths;
    const std::int32_t r = receivers->data[static_cast<std::size_t>(rivers.cell[k])];
    CAPTURE(rivers.cell[k]);
    REQUIRE(r >= 0);
    const auto w = static_cast<wg::core::Water>(water->data[static_cast<std::size_t>(r)]);
    CHECK((w == wg::core::Water::ocean || w == wg::core::Water::lake));
  }
  CHECK(mouths > 0);
}

// Every cell belongs to a region of the regions layer.
void check_regions(const WorldState& state) {
  auto region_id = state.read<FieldId::region_id>();
  REQUIRE(region_id);
  const auto count = static_cast<std::int32_t>(state.layers().regions.size());
  REQUIRE(count > 0);
  for (const std::int32_t id : region_id->data) {
    REQUIRE((id >= 0 && id < count));
  }
}

}  // namespace

TEST_CASE("classic.profile runs end to end at N = 33 and 65 (A23)") {
  wg::pipeline::Pipeline pipeline = classic_pipeline();
  wg::core::SerialExecutor exec;
  for (const std::int32_t size : {33, 65}) {
    std::int32_t finished = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
      CAPTURE(size);
      CAPTURE(seed);
      const RunResult result = run(pipeline, size, seed, exec);
      if (result) {
        ++finished;
        const WorldState& state = result->state;
        CHECK(state.provenance().completed_stages.size() == 11);
        check_field_ranges(state);
        check_river_mouths(state);
        check_regions(state);
        continue;
      }
      // A failure is a complete report: every attempt rejected, each rejection naming the stage,
      // the check and its measured and required values.
      const wg::pipeline::FailureReport& failure = result.error();
      CHECK(failure.status.code == wg::core::StatusCode::failed_precondition);
      const auto& rejections = failure.report.provenance.rejections;
      REQUIRE_FALSE(rejections.empty());
      for (const wg::core::RejectionRecord& r : rejections) {
        CHECK_FALSE(r.stage.empty());
        CHECK_FALSE(r.check_id.empty());
        CHECK(std::isfinite(r.measured));
        CHECK(std::isfinite(r.required));
      }
    }
    CHECK(finished > 0);
  }
}

TEST_CASE("classic.worlds are bit-identical for 1 and 8 threads (R1, A23)") {
  wg::pipeline::Pipeline pipeline = classic_pipeline();
  wg::core::SerialExecutor serial;
  auto pool = wg::core::ThreadPool::create(8);
  REQUIRE(pool);
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const RunResult one = run(pipeline, 65, seed, serial);
    const RunResult eight = run(pipeline, 65, seed, **pool);
    REQUIRE(one);
    REQUIRE(eight);
    CHECK(one->state.provenance().attempt == eight->state.provenance().attempt);
    for (const wg::core::FieldInfo& info : wg::core::kFields) {
      CAPTURE(info.name);
      REQUIRE(one->state.has(info.id) == eight->state.has(info.id));
      if (one->state.has(info.id)) {
        const std::span<const std::byte> a = raw_bytes(one->state, info.id);
        const std::span<const std::byte> b = raw_bytes(eight->state, info.id);
        CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
      }
    }
    CHECK(one->state.layers() == eight->state.layers());
  }
}
