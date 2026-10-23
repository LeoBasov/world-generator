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

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string first_line(const std::string& text) { return text.substr(0, text.find('\n')); }

}  // namespace

TEST_CASE("wgcli.hash of a generation equals the hash of its world file") {
  wg::test::TempDir dir("wgcli_hash");
  const std::uint64_t seed = wg::test::mock_seed(true);
  const auto world = dir / "w.wgw";
  REQUIRE(run_cli({"generate", "--profile", mock_profile(), "--size", std::string(kCliSize),
                   "--seed", std::to_string(seed), "--out", world.string()})
              .code == 0);

  const auto from_file = run_cli({"hash", world.string(), "--fields"});
  INFO(from_file.err);
  REQUIRE(from_file.code == 0);
  CHECK(contains(from_file.out, "field elevation 0x"));
  CHECK(contains(from_file.out, "layers 0x"));

  const auto json = dir / "h.json";
  const auto generated =
      run_cli({"hash", "--profile", mock_profile(), "--size", std::string(kCliSize), "--seed",
               std::to_string(seed), "--per-stage", "--fields", "--out", json.string()});
  INFO(generated.err);
  REQUIRE(generated.code == 0);
  CHECK(first_line(generated.out) == first_line(from_file.out));
  CHECK(contains(generated.out, "stage m01_seed 0x"));
  CHECK(contains(generated.out, "stage m03_verify 0x"));

  // The last stage hash is the world hash; per-stage hashes come from the successful attempt.
  const std::string world_hash = first_line(generated.out).substr(6);
  CHECK(contains(generated.out, "stage m03_verify " + world_hash));
  const std::string text = read_text(json);
  CHECK(contains(text, "\"world\": \"" + world_hash + "\""));
  CHECK(contains(text, "\"attempt\": 1"));
  CHECK(contains(text, "\"profile\": \"mock\""));
}

TEST_CASE("wgcli.hash differs between seeds and is stable across thread counts") {
  const auto h = [](std::uint64_t seed, const char* threads) {
    const auto r = run_cli({"hash", "--profile", mock_profile(), "--size", std::string(kCliSize),
                            "--seed", std::to_string(seed), "--threads", threads});
    REQUIRE(r.code == 0);
    return r.out;
  };
  CHECK(h(1, "1") == h(1, "8"));
  CHECK(h(1, "1") != h(2, "1"));
}

TEST_CASE("wgcli.hash runs a golden seed list") {
  wg::test::TempDir dir("wgcli_hash");
  const auto seeds = dir / "seeds.json";
  std::ofstream(seeds) << R"({
  "$schema": "../../schema/golden_seeds.schema.json",
  "schema_version": 1,
  "cases": [{"name": "mock33", "profile": ")"
                       << mock_profile() << R"(", "size": 33, "seeds": [1, 2]},
            {"name": "mock17", "profile": "mock_copy.json", "size": 17, "seeds": [5]}]
})";
  std::filesystem::copy_file(mock_profile(), dir / "mock_copy.json");
  const auto out = dir / "hashes.json";
  const auto r = run_cli({"hash", "--per-stage", "--seeds", seeds.string(), "--out", out.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "mock33 1 world 0x"));
  CHECK(contains(r.out, "mock33 2 stage m02_shape 0x"));
  CHECK(contains(r.out, "mock17 5 world 0x"));
  const std::string text = read_text(out);
  CHECK(contains(text, "\"schema_version\": 1"));
  CHECK(contains(text, "\"name\": \"mock17\""));
  CHECK(contains(text, "\"status\": \"ok\""));

  // The seeds list is reproducible.
  const auto again =
      run_cli({"hash", "--per-stage", "--seeds", seeds.string(), "--out", out.string()});
  CHECK(again.out == r.out);
}

TEST_CASE("wgcli.hash refuses bad input") {
  wg::test::TempDir dir("wgcli_hash");
  const auto world = dir / "w.wgw";
  REQUIRE(run_cli({"generate", "--profile", mock_profile(), "--size", std::string(kCliSize),
                   "--out", world.string()})
              .code == 0);
  CHECK(run_cli({"hash", world.string(), "--per-stage"}).code == 2);
  CHECK(run_cli({"hash", (dir / "missing.wgw").string()}).code == 2);
  CHECK(run_cli({"hash"}).code == 2);  // nothing to hash: no profile

  const auto bad = dir / "bad_seeds.json";
  std::ofstream(bad) << R"({"schema_version": 1, "cases": [{"name": "x", "seeds": [1]}]})";
  CHECK(run_cli({"hash", "--seeds", bad.string()}).code == 2);
  const auto newer = dir / "newer_seeds.json";
  std::ofstream(newer) << R"({"schema_version": 2, "cases": []})";
  CHECK(run_cli({"hash", "--seeds", newer.string()}).code == 2);
  const auto junk = dir / "junk.json";
  std::ofstream(junk) << "{";
  CHECK(run_cli({"hash", "--seeds", junk.string()}).code == 2);
}
