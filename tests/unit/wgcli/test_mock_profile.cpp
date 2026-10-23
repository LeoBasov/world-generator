#include "mock_profile.hpp"

#include <doctest/doctest.h>

#include "wg/core/executor.hpp"
#include "wg/pipeline/pipeline.hpp"
#include "wg/pipeline/profile.hpp"
#include "wg/pipeline/registry.hpp"

using wg::core::FieldId;
using wg::pipeline::FieldSet;
using wg::pipeline::LayerId;

TEST_CASE("wgcli.mock_profile match the mock profile") {
  wg::pipeline::StageRegistry registry;
  REQUIRE(wg::cli::add_mock_stages(registry).ok());
  CHECK(registry.ids().size() == 3);
  CHECK_FALSE(wg::cli::add_mock_stages(registry).ok());  // already registered

  auto seed = registry.create("m01_seed");
  REQUIRE(seed);
  CHECK((*seed)->inputs().empty());
  CHECK((*seed)->outputs() == FieldSet{FieldId::latitude});
  auto shape = registry.create("m02_shape");
  REQUIRE(shape);
  CHECK((*shape)->inputs() == FieldSet{FieldId::latitude});
  CHECK((*shape)->outputs() == FieldSet{FieldId::elevation, LayerId::peaks});
  auto verify = registry.create("m03_verify");
  REQUIRE(verify);
  CHECK((*verify)->inputs() == FieldSet{FieldId::elevation, LayerId::peaks});
  CHECK((*verify)->outputs() == FieldSet{FieldId::biome});

  auto profile = wg::pipeline::load_profile_file(std::string(WG_TEST_SOURCE_DIR) +
                                                 "/tests/fixtures/profiles/mock.json");
  REQUIRE(profile);
  CHECK(wg::pipeline::Pipeline::create(*profile, registry));
}

TEST_CASE("wgcli.mock_profile reject a deterministic share of attempts") {
  int rejected = 0;
  constexpr int kSeeds = 4000;
  for (std::uint64_t s = 0; s < kSeeds; ++s) {
    rejected += wg::cli::mock_verify_rejects(s, 0) ? 1 : 0;
    CHECK(wg::cli::mock_verify_rejects(s, 0) == wg::cli::mock_verify_rejects(s, 0));
  }
  CHECK(rejected > kSeeds / 5);
  CHECK(rejected < kSeeds * 3 / 10);
}

TEST_CASE("wgcli.mock_profile run the profile and record the rejection") {
  wg::pipeline::StageRegistry registry;
  REQUIRE(wg::cli::add_mock_stages(registry).ok());
  auto pipeline = wg::pipeline::Pipeline::create(
      {.name = "mock", .stages = {"m01_seed", "m02_shape", "m03_verify"}}, registry);
  REQUIRE(pipeline);
  std::uint64_t seed = 0;
  while (!wg::cli::mock_verify_rejects(seed, 0) || wg::cli::mock_verify_rejects(seed, 1)) {
    ++seed;
  }
  wg::params::GenParams params;
  params.grid.size = 17;
  wg::core::SerialExecutor exec;
  auto result = pipeline->run(params, seed, {.exec = exec});
  REQUIRE(result);
  CHECK(result->state.provenance().attempt == 1);
  REQUIRE(result->state.provenance().rejections.size() == 1);
  CHECK(result->state.provenance().rejections[0].check_id == "MOCK_VERIFY");
  CHECK(result->state.layers().peaks.size() == 1);
  auto biome = result->state.read<wg::core::field_t<FieldId::biome>>(FieldId::biome);
  REQUIRE(biome);
  for (const auto v : biome->data) {
    CHECK(v < 100);
  }
}
