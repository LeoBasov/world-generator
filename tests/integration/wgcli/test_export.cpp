#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <lodepng.h>

#include "support/cli_runner.hpp"
#include "support/temp_dir.hpp"
#include "wg/io/export.hpp"
#include "wg/io/world_file.hpp"

using wg::test::contains;
using wg::test::kCliSize;
using wg::test::mock_profile;
using wg::test::run_cli;

namespace {

std::filesystem::path make_world(const wg::test::TempDir& dir) {
  auto out = dir / "w.wgw";
  REQUIRE(run_cli({"generate", "--profile", mock_profile(), "--size", std::string(kCliSize),
                   "--seed", "4", "--out", out.string()})
              .code == 0);
  return out;
}

std::vector<unsigned char> load_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("wgcli.export writes PNG16 images equal to the scaled field") {
  wg::test::TempDir dir("wgcli_export");
  const auto world = make_world(dir);
  const auto out = dir / "png";
  const auto r = run_cli({"export", world.string(), "--format", "png16", "--field", "elevation",
                          "--out", out.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  CHECK(contains(r.out, "elevation.png range ["));
  CHECK_FALSE(std::filesystem::exists(out / "biome.png"));

  std::vector<unsigned char> pixels;
  unsigned w = 0;
  unsigned h = 0;
  REQUIRE(lodepng::decode(pixels, w, h, load_file(out / "elevation.png"), LCT_GREY, 16) == 0);
  REQUIRE(w == 33);
  REQUIRE(h == 33);

  auto loaded = wg::io::read_world_file(world);
  REQUIRE(loaded);
  auto expected = wg::io::encode_png16(loaded->state, wg::core::FieldId::elevation);
  REQUIRE(expected);
  auto elevation = loaded->state.read<float>(wg::core::FieldId::elevation);
  REQUIRE(elevation);
  for (std::size_t i = 0; i < elevation->data.size(); i += 97) {
    const auto grey = static_cast<std::uint16_t>((pixels[2 * i] << 8U) | pixels[(2 * i) + 1]);
    CHECK(grey == wg::io::png16_value(elevation->data[i], expected->lo, expected->hi));
  }
}

TEST_CASE("wgcli.export writes every field, colour maps and the layers") {
  wg::test::TempDir dir("wgcli_export");
  const auto world = make_world(dir);
  const auto out = dir / "all";
  const auto r = run_cli({"export", world.string(), "--format", "png16", "--colormap", "terrain",
                          "--out", out.string()});
  INFO(r.err);
  REQUIRE(r.code == 0);
  for (const char* name : {"latitude", "elevation", "biome"}) {
    CHECK(std::filesystem::exists(out / (std::string(name) + ".png")));
    std::vector<unsigned char> rgb;
    unsigned w = 0;
    unsigned h = 0;
    CHECK(lodepng::decode(rgb, w, h, load_file(out / (std::string(name) + "_terrain.png")), LCT_RGB,
                          8) == 0);
    CHECK(w == 33);
  }

  const auto json = run_cli({"export", world.string(), "--format", "json", "--out", out.string()});
  REQUIRE(json.code == 0);
  std::ifstream in(out / "layers.json");
  std::stringstream text;
  text << in.rdbuf();
  CHECK(contains(text.str(), "\"peaks\""));
}

TEST_CASE("wgcli.export --map-range data spreads the colour map over the values") {
  wg::test::TempDir dir("wgcli_export");
  const auto world = make_world(dir);
  const auto decode = [](const std::filesystem::path& file) {
    std::vector<unsigned char> rgb;
    unsigned w = 0;
    unsigned h = 0;
    REQUIRE(lodepng::decode(rgb, w, h, load_file(file), LCT_RGB, 8) == 0);
    return rgb;
  };
  for (const char* range : {"registry", "data"}) {
    const auto r =
        run_cli({"export", world.string(), "--format", "png16", "--field", "elevation",
                 "--colormap", "grey", "--map-range", range, "--out", (dir / range).string()});
    INFO(r.err);
    REQUIRE(r.code == 0);
  }
  // With the data range the darkest and the brightest grey of the map are 0 and 255.
  const std::vector<unsigned char> data = decode(dir / "data" / "elevation_grey.png");
  CHECK(*std::min_element(data.begin(), data.end()) == 0);
  CHECK(*std::max_element(data.begin(), data.end()) == 255);
  CHECK(decode(dir / "registry" / "elevation_grey.png") != data);
  // The PNG16 itself keeps the registry range.
  CHECK(load_file(dir / "data" / "elevation.png") == load_file(dir / "registry" / "elevation.png"));

  const auto out = (dir / "x").string();
  CHECK(run_cli({"export", world.string(), "--format", "png16", "--colormap", "grey", "--map-range",
                 "log", "--out", out})
            .code == 2);
  CHECK(
      run_cli({"export", world.string(), "--format", "png16", "--map-range", "data", "--out", out})
          .code == 2);
}

TEST_CASE("wgcli.export writes EXR only with EXR support") {
  wg::test::TempDir dir("wgcli_export");
  const auto world = make_world(dir);
  const auto r = run_cli({"export", world.string(), "--format", "exr", "--field", "biome", "--out",
                          (dir / "exr").string()});
  if (wg::io::exr_supported()) {
    CHECK(r.code == 0);
    CHECK(std::filesystem::exists(dir / "exr" / "biome.exr"));
  } else {
    CHECK(r.code == 2);
    CHECK(contains(r.err, "WG_WITH_EXR"));
  }
}

TEST_CASE("wgcli.export refuses bad input") {
  wg::test::TempDir dir("wgcli_export");
  const auto world = make_world(dir).string();
  const auto out = (dir / "x").string();
  CHECK(run_cli({"export", world, "--out", out}).code == 2);
  CHECK(run_cli({"export", world, "--format", "png16"}).code == 2);
  CHECK(run_cli({"export", world, "--format", "tiff", "--out", out}).code == 2);
  CHECK(
      run_cli({"export", world, "--format", "png16", "--colormap", "rainbow", "--out", out}).code ==
      2);
  CHECK(run_cli({"export", world, "--format", "json", "--colormap", "grey", "--out", out}).code ==
        2);
  CHECK(run_cli({"export", world, "--format", "png16", "--field", "wind_u", "--out", out}).code ==
        2);
  CHECK(
      run_cli({"export", (dir / "missing.wgw").string(), "--format", "png16", "--out", out}).code ==
      2);
}
