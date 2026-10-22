#include "wg/io/snapshot.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "support/mock_stages.hpp"
#include "support/sample_world.hpp"
#include "support/temp_dir.hpp"
#include "wg/core/executor.hpp"
#include "wg/io/world_file.hpp"
#include "wg/pipeline/pipeline.hpp"

using wg::core::FieldId;
using wg::io::SnapshotPolicy;
using wg::io::SnapshotWriter;
using wg::pipeline::LayerId;
using wg::pipeline::Pipeline;
using wg::pipeline::RunContext;

namespace {

constexpr std::uint64_t kSeed = 11;

wg::params::GenParams run_params() {
  wg::params::GenParams p = wg::test::sample_params();
  p.checks.max_attempts = 3;
  return p;
}

// Two mock stages; the second rejects attempt 0, so attempt 1 succeeds.
std::vector<std::string> run_with(SnapshotPolicy policy, const std::filesystem::path& dir) {
  wg::pipeline::StageRegistry registry;
  auto seed = wg::test::mock("m01_seed", {}, {FieldId::latitude});
  auto shape =
      wg::test::mock("m02_shape", {FieldId::latitude}, {FieldId::elevation, LayerId::peaks});
  shape->reject_on = {0};
  REQUIRE(wg::test::add_mock(registry, seed).ok());
  REQUIRE(wg::test::add_mock(registry, shape).ok());
  auto pipeline = Pipeline::create(
      wg::pipeline::Profile{.name = "mock", .stages = {"m01_seed", "m02_shape"}}, registry);
  REQUIRE(pipeline);

  const wg::params::GenParams params = run_params();
  SnapshotWriter writer(dir, params, policy);
  wg::core::SerialExecutor exec;
  auto result = pipeline->run(params, kSeed, RunContext{.exec = exec, .snapshot = &writer});
  REQUIRE(result);

  std::vector<std::string> names;
  names.reserve(writer.written().size());
  for (const auto& p : writer.written()) {
    CHECK(p.parent_path() == dir);
    CHECK(std::filesystem::exists(p));
    names.push_back(p.filename().string());
  }
  return names;
}

}  // namespace

TEST_CASE("io.snapshot policy all writes every completed stage of every attempt") {
  const wg::test::TempDir dir;
  const auto out = dir / "snaps";
  const std::vector<std::string> names = run_with(SnapshotPolicy::all, out);
  CHECK(names == std::vector<std::string>{"a0_00_m01_seed.wgw", "a1_00_m01_seed.wgw",
                                          "a1_01_m02_shape.wgw"});

  // Each snapshot's provenance names its last completed stage.
  auto first = wg::io::read_world_file(out / "a1_00_m01_seed.wgw");
  REQUIRE(first);
  CHECK(first->state.provenance().last_completed_stage() == "m01_seed");
  CHECK(first->state.provenance().attempt == 1);
  CHECK(first->state.provenance().rejections.size() == 1);
  CHECK(first->state.has(FieldId::latitude));
  CHECK_FALSE(first->state.has(FieldId::elevation));
  CHECK(first->params == run_params());

  auto last = wg::io::read_world_file(out / "a1_01_m02_shape.wgw");
  REQUIRE(last);
  CHECK(last->state.provenance().last_completed_stage() == "m02_shape");
  CHECK(last->state.provenance().seed == kSeed);
  CHECK(last->state.has(FieldId::elevation));
  CHECK(last->state.layers().peaks.size() == 1);
}

TEST_CASE("io.snapshot policy final writes only the finished world") {
  const wg::test::TempDir dir;
  CHECK(run_with(SnapshotPolicy::final, dir.path()) ==
        std::vector<std::string>{"a1_01_m02_shape.wgw"});
}

TEST_CASE("io.snapshot policy none writes nothing and creates no directory") {
  const wg::test::TempDir dir;
  const auto out = dir / "never";
  CHECK(run_with(SnapshotPolicy::none, out).empty());
  CHECK_FALSE(std::filesystem::exists(out));
}

TEST_CASE("io.snapshot a write failure stops the run") {
  const wg::test::TempDir dir;
  const auto blocker = dir / "file";
  {
    std::ofstream f(blocker);
    f << "x";
  }
  const wg::core::WorldState state = wg::test::sample_world();
  SnapshotWriter writer(blocker / "sub", wg::test::sample_params(), SnapshotPolicy::all);
  const wg::pipeline::SnapshotInfo info{
      .profile = "mock", .stage = "m01_seed", .stage_index = 0, .attempt = 0, .final = false};
  CHECK(writer.on_stage(state, info).code == wg::core::StatusCode::io_error);
  CHECK(writer.written().empty());
}

TEST_CASE("io.snapshot file names sort in run order") {
  const wg::pipeline::SnapshotInfo info{
      .profile = "p", .stage = "c04_rivers", .stage_index = 3, .attempt = 0, .final = false};
  CHECK(SnapshotWriter::snapshot_name(info) == "a0_03_c04_rivers.wgw");
  const wg::pipeline::SnapshotInfo late{
      .profile = "p", .stage = "p11_verify", .stage_index = 10, .attempt = 12, .final = true};
  CHECK(SnapshotWriter::snapshot_name(late) == "a12_10_p11_verify.wgw");
}
