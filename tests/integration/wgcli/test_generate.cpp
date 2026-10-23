#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"
#include "wg/io/world_file.hpp"

using wg::test::contains;
using wg::test::kCliSize;
using wg::test::mock_profile;
using wg::test::mock_seed;
using wg::test::run_cli;

namespace {

std::vector<std::string> generate_args(const std::filesystem::path& out, std::uint64_t seed) {
  return {"generate", "--profile",          mock_profile(), "--size",    std::string(kCliSize),
          "--seed",   std::to_string(seed), "--out",        out.string()};
}

std::size_t files_in(const std::filesystem::path& dir) {
  std::size_t n = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    n += entry.is_regular_file() ? 1 : 0;
  }
  return n;
}

}  // namespace

TEST_CASE("wgcli.generate writes a world of the mock profile") {
  wg::test::TempDir dir("wgcli_generate");
  const auto out = dir / "w.wgw";
  const std::uint64_t seed = mock_seed(false);
  const auto r = run_cli(generate_args(out, seed));
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "world hash: 0x"));
  CHECK(contains(r.out, "rejections: 0"));

  auto world = wg::io::read_world_file(out);
  REQUIRE(world);
  const wg::core::Provenance& p = world->state.provenance();
  CHECK(p.profile == "mock");
  CHECK(p.seed == seed);
  CHECK(p.attempt == 0);
  CHECK(p.last_completed_stage() == "m03_verify");
  CHECK(world->state.grid().n == 33);
  CHECK(world->state.has(wg::core::FieldId::biome));
  CHECK_FALSE(std::filesystem::exists(dir / "w.wgw.snapshots"));
}

TEST_CASE("wgcli.generate records a rejection and retries with the next attempt") {
  wg::test::TempDir dir("wgcli_generate");
  const auto out = dir / "w.wgw";
  const std::uint64_t seed = mock_seed(true);
  const auto r = run_cli(generate_args(out, seed));
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "rejections: 1"));
  CHECK(contains(r.out, "m03_verify MOCK_VERIFY"));

  auto world = wg::io::read_world_file(out);
  REQUIRE(world);
  const wg::core::Provenance& p = world->state.provenance();
  CHECK(p.attempt == 1);
  REQUIRE(p.rejections.size() == 1);
  CHECK(p.rejections[0].stage == "m03_verify");
  CHECK(p.rejections[0].check_id == "MOCK_VERIFY");
}

TEST_CASE("wgcli.generate writes snapshots as requested") {
  wg::test::TempDir dir("wgcli_generate");
  // One rejected attempt: m01 and m02 complete in attempt 0 (m03 rejects), all 3 in attempt 1.
  const std::uint64_t seed = mock_seed(true);

  auto args = generate_args(dir / "all.wgw", seed);
  args.insert(args.end(), {"--snapshots", "all"});
  REQUIRE(run_cli(args).code == 0);
  CHECK(files_in(dir / "all.wgw.snapshots") == 5);
  CHECK(std::filesystem::exists(dir / "all.wgw.snapshots" / "a0_01_m02_shape.wgw"));
  CHECK(std::filesystem::exists(dir / "all.wgw.snapshots" / "a1_02_m03_verify.wgw"));

  args = generate_args(dir / "final.wgw", seed);
  args.insert(args.end(), {"--snapshots", "final"});
  REQUIRE(run_cli(args).code == 0);
  CHECK(files_in(dir / "final.wgw.snapshots") == 1);

  args = generate_args(dir / "none.wgw", seed);
  args.insert(args.end(), {"--snapshots", "none"});
  REQUIRE(run_cli(args).code == 0);
  CHECK_FALSE(std::filesystem::exists(dir / "none.wgw.snapshots"));

  args = generate_args(dir / "bad.wgw", seed);
  args.insert(args.end(), {"--snapshots", "some"});
  CHECK(run_cli(args).code == 2);
}

TEST_CASE("wgcli.generate exits with 1 when every attempt is rejected") {
  wg::test::TempDir dir("wgcli_generate");
  const auto params = dir / "one_attempt.json";
  std::ofstream(params) << R"({"schema_version": 1, "checks": {"max_attempts": 1}})";
  auto args = generate_args(dir / "w.wgw", mock_seed(true));
  args.insert(args.end(), {"--params", params.string()});
  const auto r = run_cli(args);
  CHECK(r.code == 1);
  CHECK(contains(r.err, "generation failed"));
  CHECK(contains(r.err, "MOCK_VERIFY"));
  CHECK_FALSE(std::filesystem::exists(dir / "w.wgw"));
}

TEST_CASE("wgcli.generate exits with 2 for invalid input") {
  wg::test::TempDir dir("wgcli_generate");
  const auto out = (dir / "w.wgw").string();

  CHECK(run_cli({"generate", "--profile", mock_profile()}).code == 2);  // no --out
  CHECK(run_cli({"generate", "--profile", "no_such_profile", "--out", out}).code == 2);
  CHECK(run_cli({"generate", "--out", out}).code == 2);  // no profile anywhere
  CHECK(run_cli({"generate", "--profile", mock_profile(), "--size", "30", "--out", out}).code == 2);
  CHECK(run_cli({"generate", "--profile", mock_profile(), "--seed", "x", "--out", out}).code == 2);
  CHECK(run_cli({"generate", "--profile", mock_profile(), "--threads", "-1", "--out", out}).code ==
        2);
  CHECK(run_cli({"generate", "--bogus"}).code == 2);

  const auto params = dir / "bad.json";
  std::ofstream(params) << R"({"schema_version": 1, "grid": {"size": "big"}})";
  const auto r =
      run_cli({"generate", "--profile", mock_profile(), "--params", params.string(), "--out", out});
  CHECK(r.code == 2);
  CHECK(contains(r.err, "$.grid.size"));

  // A profile whose stages are not causally ordered.
  const auto profile = dir / "unordered.json";
  std::ofstream(profile)
      << R"({"schema_version": 1, "name": "x", "stages": ["m02_shape", "m01_seed"]})";
  CHECK(run_cli({"generate", "--profile", profile.string(), "--out", out}).code == 2);
  CHECK_FALSE(std::filesystem::exists(out));
}

TEST_CASE("wgcli.generate gives the same world for 1 and 8 threads") {
  wg::test::TempDir dir("wgcli_generate");
  const std::uint64_t seed = mock_seed(true);
  auto one = generate_args(dir / "one.wgw", seed);
  one.insert(one.end(), {"--threads", "1"});
  auto eight = generate_args(dir / "eight.wgw", seed);
  eight.insert(eight.end(), {"--threads", "8"});
  REQUIRE(run_cli(one).code == 0);
  REQUIRE(run_cli(eight).code == 0);
  const auto h1 = run_cli({"hash", (dir / "one.wgw").string()});
  const auto h8 = run_cli({"hash", (dir / "eight.wgw").string()});
  REQUIRE(h1.code == 0);
  CHECK(h1.out == h8.out);
}
