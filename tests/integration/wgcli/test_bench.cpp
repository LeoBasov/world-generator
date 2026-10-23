#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"

using wg::test::contains;
using wg::test::kCliSize;
using wg::test::mock_profile;
using wg::test::run_cli;

TEST_CASE("wgcli.bench times every stage of the mock profile") {
  wg::test::TempDir dir("wgcli_bench");
  const auto json = dir / "bench.json";
  const auto r = run_cli({"bench", "--profile", mock_profile(), "--size", std::string(kCliSize),
                          "--repeat", "2", "--json", json.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "median_ms"));
  CHECK(contains(r.out, "m01_seed"));
  CHECK(contains(r.out, "m02_shape"));
  CHECK(contains(r.out, "m03_verify"));

  std::ifstream in(json);
  std::stringstream text;
  text << in.rdbuf();
  CHECK(contains(text.str(), "\"runs\": 2"));
  CHECK(contains(text.str(), "\"grid_size\": 33"));
}

TEST_CASE("wgcli.bench exits with 1 when generation fails and 2 for bad input") {
  wg::test::TempDir dir("wgcli_bench");
  const auto params = dir / "one_attempt.json";
  std::ofstream(params) << R"({"schema_version": 1, "checks": {"max_attempts": 1}})";
  CHECK(run_cli({"bench", "--profile", mock_profile(), "--params", params.string(), "--size",
                 std::string(kCliSize), "--seed", std::to_string(wg::test::mock_seed(true))})
            .code == 1);
  CHECK(run_cli({"bench", "--profile", mock_profile(), "--repeat", "0"}).code == 2);
}
