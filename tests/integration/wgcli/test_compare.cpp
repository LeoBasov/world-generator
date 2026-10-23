#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"

using wg::test::contains;
using wg::test::mock_profile;
using wg::test::run_cli;

namespace {

std::filesystem::path make_world(const wg::test::TempDir& dir, std::uint64_t seed,
                                 std::string_view size = wg::test::kCliSize) {
  auto out = dir / ("w" + std::to_string(seed) + "_" + std::string(size) + ".wgw");
  const auto r = run_cli({"generate", "--profile", mock_profile(), "--size", std::string(size),
                          "--seed", std::to_string(seed), "--out", out.string()});
  REQUIRE(r.code == 0);
  return out;
}

}  // namespace

TEST_CASE("wgcli.compare finds no difference between a world and itself") {
  wg::test::TempDir dir("wgcli_compare");
  const auto a = make_world(dir, 1);
  const auto r = run_cli({"compare", a.string(), a.string()});
  INFO(r.err);
  CHECK(r.code == 0);
  CHECK(contains(r.out, "elevation: max_abs_diff=0 cells_over_tolerance=0"));
  CHECK(contains(r.out, "layers: equal"));
  CHECK(contains(r.out, "result: equal"));
}

TEST_CASE("wgcli.compare reports differing worlds with 1 and writes difference images") {
  wg::test::TempDir dir("wgcli_compare");
  const auto a = make_world(dir, 1);
  const auto b = make_world(dir, 2);
  const auto png = dir / "diff";
  const auto r = run_cli({"compare", a.string(), b.string(), "--diff-png", png.string()});
  INFO(r.err);
  CHECK(r.code == 1);
  CHECK(contains(r.out, "result: differ"));
  CHECK(std::filesystem::exists(png / "elevation_diff.png"));
  CHECK(std::filesystem::exists(png / "biome_diff.png"));

  // Mock values lie in [0, 100): a tolerance of 100 accepts every field, not the layers.
  const auto tolerant =
      run_cli({"compare", a.string(), b.string(), "--tolerance", "100", "--field", "biome"});
  CHECK(contains(tolerant.out, "biome: max_abs_diff="));
  CHECK(contains(tolerant.out, "cells_over_tolerance=0"));
  CHECK_FALSE(contains(tolerant.out, "elevation"));
}

TEST_CASE("wgcli.compare refuses bad input") {
  wg::test::TempDir dir("wgcli_compare");
  const auto a = make_world(dir, 1);
  const auto small = make_world(dir, 1, "17");
  CHECK(run_cli({"compare", a.string()}).code == 2);
  CHECK(run_cli({"compare", a.string(), (dir / "missing.wgw").string()}).code == 2);
  CHECK(run_cli({"compare", a.string(), small.string()}).code == 2);
  CHECK(run_cli({"compare", a.string(), a.string(), "--tolerance", "-1"}).code == 2);
  CHECK(run_cli({"compare", a.string(), a.string(), "--tolerance", "abc"}).code == 2);
  CHECK(run_cli({"compare", a.string(), a.string(), "--field", "nonsense"}).code == 2);
}
