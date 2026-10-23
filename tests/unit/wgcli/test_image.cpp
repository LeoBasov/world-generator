#include "image.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

#include <lodepng.h>

#include "support/temp_dir.hpp"

namespace {

std::vector<unsigned char> load_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("wgcli.image maps colours") {
  using wg::cli::Colormap;
  CHECK(wg::cli::colormap_from_name("viridis") == Colormap::viridis);
  CHECK(wg::cli::colormap_from_name("terrain") == Colormap::terrain);
  CHECK(wg::cli::colormap_from_name("grey") == Colormap::grey);
  CHECK_FALSE(wg::cli::colormap_from_name("rainbow"));

  const auto black = wg::cli::map_color(Colormap::grey, -1.0);
  CHECK(black.r == 0);
  const auto white = wg::cli::map_color(Colormap::grey, 2.0);
  CHECK(white.g == 255);
  const auto mid = wg::cli::map_color(Colormap::grey, 0.5);
  CHECK(mid.b == 128);
  const auto low = wg::cli::map_color(Colormap::viridis, 0.0);
  CHECK(low.r == 68);
  CHECK(low.b == 84);
  const auto nan = wg::cli::map_color(Colormap::terrain, std::nan(""));
  CHECK(nan.r == 0);
  CHECK(nan.g == 0);
}

TEST_CASE("wgcli.image writes grey and colour PNGs") {
  wg::test::TempDir dir("wgcli_image");
  const std::vector<std::uint16_t> grey = {0, 1, 256, 65535};
  REQUIRE(wg::cli::write_grey16_png(dir / "g.png", 2, grey).ok());
  std::vector<unsigned char> pixels;
  unsigned w = 0;
  unsigned h = 0;
  REQUIRE(lodepng::decode(pixels, w, h, load_file(dir / "g.png"), LCT_GREY, 16) == 0);
  REQUIRE(pixels.size() == 8);
  CHECK(pixels[2] == 0);
  CHECK(pixels[3] == 1);
  CHECK(pixels[4] == 1);
  CHECK(pixels[5] == 0);
  CHECK(pixels[6] == 255);

  REQUIRE(wg::cli::write_colormap_png(dir / "c.png", 2, {0.0, 1.0, 2.0, 3.0}, 0.0, 3.0,
                                      wg::cli::Colormap::grey)
              .ok());
  pixels.clear();
  REQUIRE(lodepng::decode(pixels, w, h, load_file(dir / "c.png"), LCT_RGB, 8) == 0);
  REQUIRE(pixels.size() == 12);
  CHECK(pixels[0] == 0);
  CHECK(pixels[9] == 255);
  CHECK_FALSE(wg::cli::write_grey16_png(dir / "no_dir_file" / "", 2, grey).ok());
}
