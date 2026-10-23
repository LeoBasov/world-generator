#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"

using wg::test::contains;
using wg::test::mock_profile;
using wg::test::run_cli;

TEST_CASE("wgcli.validate accepts the shipped data directory") {
  const auto r = run_cli({"validate"});
  INFO(r.out);
  INFO(r.err);
  CHECK(r.code == 0);
  CHECK(contains(r.out, "tables ok"));
  CHECK(contains(r.out, "df_default.json: ok"));
  CHECK_FALSE(contains(r.out, "error"));
}

TEST_CASE("wgcli.validate checks params and profile files") {
  wg::test::TempDir dir("wgcli_validate");
  CHECK(run_cli({"validate", "--profile", mock_profile()}).code == 0);

  const auto params = dir / "bad.json";
  std::ofstream(params) << R"({"schema_version": 1, "grid": {"size": 30}, "nonsense": 1})";
  const auto r = run_cli({"validate", "--params", params.string()});
  CHECK(r.code == 2);
  CHECK(contains(r.out, "error: $.nonsense"));

  const auto broken = dir / "broken.json";
  std::ofstream(broken) << "{";
  CHECK(run_cli({"validate", "--params", broken.string()}).code == 2);
  CHECK(run_cli({"validate", "--params", (dir / "missing.json").string()}).code == 2);

  const auto profile = dir / "unknown_stage.json";
  std::ofstream(profile) << R"({"schema_version": 1, "name": "x", "stages": ["z99_nope"]})";
  const auto p = run_cli({"validate", "--profile", profile.string()});
  CHECK(p.code == 2);
  CHECK(contains(p.out, "z99_nope"));

  CHECK(run_cli({"validate", "--data-dir", (dir / "empty").string()}).code == 2);
}
