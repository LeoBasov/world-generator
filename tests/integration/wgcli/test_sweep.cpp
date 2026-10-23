#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"

using wg::test::contains;
using wg::test::kCliSize;
using wg::test::mock_profile;
using wg::test::run_cli;

namespace {

std::vector<std::string> lines_of(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace

TEST_CASE("wgcli.sweep writes one metrics row per seed") {
  wg::test::TempDir dir("wgcli_sweep");
  const auto csv = dir / "metrics.csv";
  const auto r = run_cli({"sweep", "--profile", mock_profile(), "--size", std::string(kCliSize),
                          "--seeds", "0..9,20", "--metrics", csv.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "seeds: 11"));
  CHECK(contains(r.out, "failed: 0"));

  const auto lines = lines_of(csv);
  REQUIRE(lines.size() == 12);
  CHECK(lines[0] ==
        "seed,status,attempts,rejections,last_check,wall_ms,m01_seed.cells,m02_shape.cells,"
        "m03_verify.cells");
  CHECK(lines[1].rfind("0,ok,", 0) == 0);
  CHECK(lines[11].rfind("20,ok,", 0) == 0);
  CHECK(contains(lines[1], ",1089"));  // 33 * 33 cells

  // A seed whose first attempt was rejected names the check.
  for (std::uint64_t s = 0; s < 10; ++s) {
    CHECK(contains(lines[s + 1], "MOCK_VERIFY") == wg::cli::mock_verify_rejects(s, 0));
  }
}

TEST_CASE("wgcli.sweep keeps failure reports and still exits with 0") {
  wg::test::TempDir dir("wgcli_sweep");
  const auto params = dir / "one_attempt.json";
  std::ofstream(params) << R"({"schema_version": 1, "checks": {"max_attempts": 1}})";
  const std::uint64_t bad = wg::test::mock_seed(true);
  const auto failures = dir / "failures";
  const auto r = run_cli({"sweep", "--profile", mock_profile(), "--params", params.string(),
                          "--size", std::string(kCliSize), "--seeds", std::to_string(bad),
                          "--keep-failures", failures.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "failed: 1"));
  const auto report = failures / ("seed_" + std::to_string(bad) + ".json");
  REQUIRE(std::filesystem::exists(report));
  std::ifstream in(report);
  std::stringstream text;
  text << in.rdbuf();
  CHECK(contains(text.str(), "\"check_id\": \"MOCK_VERIFY\""));
  CHECK(contains(text.str(), "\"status\": \"failed_precondition\""));
}

TEST_CASE("wgcli.sweep refuses a bad seed list") {
  CHECK(run_cli({"sweep", "--profile", mock_profile()}).code == 2);
  CHECK(run_cli({"sweep", "--profile", mock_profile(), "--seeds", "9..1"}).code == 2);
  CHECK(run_cli({"sweep", "--profile", mock_profile(), "--seeds", "1,,2"}).code == 2);
  CHECK(run_cli({"sweep", "--profile", mock_profile(), "--seeds", "0..20000000"}).code == 2);
}
