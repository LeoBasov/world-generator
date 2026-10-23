#include <doctest/doctest.h>

#include <cstddef>
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
using wg::test::run_cli;

namespace {

std::filesystem::path make_world(const wg::test::TempDir& dir, std::uint64_t seed) {
  auto out = dir / ("w" + std::to_string(seed) + ".wgw");
  const auto r = run_cli({"generate", "--profile", mock_profile(), "--size", std::string(kCliSize),
                          "--seed", std::to_string(seed), "--out", out.string()});
  REQUIRE(r.code == 0);
  return out;
}

/// Flips one byte inside the stored chunk of a field.
void corrupt_chunk(const std::filesystem::path& path, wg::core::FieldId id) {
  auto reader = wg::io::WorldReader::open_file(path);
  REQUIRE(reader);
  const wg::io::ChunkEntry* chunk = reader->chunk(id);
  REQUIRE(chunk != nullptr);
  const std::uint64_t offset = chunk->offset + (chunk->stored_size / 2);
  std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
  f.seekg(static_cast<std::streamoff>(offset));
  char c = 0;
  f.get(c);
  f.seekp(static_cast<std::streamoff>(offset));
  f.put(static_cast<char>(c ^ 0x5A));
}

}  // namespace

TEST_CASE("wgcli.inspect prints header, provenance and field statistics") {
  wg::test::TempDir dir("wgcli_inspect");
  const std::uint64_t seed = wg::test::mock_seed(true);
  const auto world = make_world(dir, seed);
  const auto r = run_cli({"inspect", world.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "format version: 1"));
  CHECK(contains(r.out, "grid: n=33"));
  CHECK(contains(r.out, "seed: " + std::to_string(seed)));
  CHECK(contains(r.out, "profile: mock"));
  CHECK(contains(r.out, "completed stages: m01_seed m02_shape m03_verify"));
  CHECK(contains(r.out, "rejections: 1"));
  CHECK(contains(r.out, "$metadata"));
  CHECK(contains(r.out, "layers: peaks=1"));
  CHECK(contains(r.out, "fields: 3"));
  CHECK(contains(r.out, "  latitude: min="));
  CHECK(contains(r.out, "  elevation: min="));
  CHECK(contains(r.out, "  biome: min="));
  CHECK(contains(r.out, "non_finite=0"));
}

TEST_CASE("wgcli.inspect reads only the requested field") {
  wg::test::TempDir dir("wgcli_inspect");
  const auto world = make_world(dir, 3);
  // A corrupt elevation chunk does not matter as long as only biome is read.
  corrupt_chunk(world, wg::core::FieldId::elevation);
  const auto r = run_cli({"inspect", world.string(), "--field", "biome", "--histogram", "4"});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "fields: 1"));
  CHECK(contains(r.out, "  biome: min="));
  CHECK_FALSE(contains(r.out, "  elevation: min="));
  CHECK(contains(r.out, "    [0, "));

  const auto all = run_cli({"inspect", world.string()});
  CHECK(all.code == 2);
  CHECK(contains(all.err, "elevation"));
}

TEST_CASE("wgcli.inspect refuses bad input") {
  wg::test::TempDir dir("wgcli_inspect");
  CHECK(run_cli({"inspect"}).code == 2);
  CHECK(run_cli({"inspect", (dir / "missing.wgw").string()}).code == 2);
  const auto junk = dir / "junk.wgw";
  std::ofstream(junk) << "not a world file at all, but long enough to have a header......";
  CHECK(run_cli({"inspect", junk.string()}).code == 2);
  const auto world = make_world(dir, 1);
  CHECK(run_cli({"inspect", world.string(), "--field", "nonsense"}).code == 2);
  CHECK(run_cli({"inspect", world.string(), "--field", "rainfall"}).code == 2);
  CHECK(run_cli({"inspect", world.string(), "--histogram", "0"}).code == 2);
}
