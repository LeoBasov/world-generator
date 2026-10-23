#include "image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include <lodepng.h>

#include "common.hpp"

namespace wg::cli {

namespace {

core::Status write_png(const std::filesystem::path& path, std::int32_t n,
                       const std::vector<unsigned char>& pixels, LodePNGColorType type,
                       unsigned bitdepth) {
  std::vector<unsigned char> png;
  const auto size = static_cast<unsigned>(n);
  const unsigned error = lodepng::encode(png, pixels, size, size, type, bitdepth);
  if (error != 0) {
    return core::make_status(core::StatusCode::internal,
                             std::string("PNG encoding failed: ") + lodepng_error_text(error));
  }
  return write_file(path, std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
}

struct Stop {
  double t;
  Rgb c;
};

// Piecewise-linear colour maps; viridis is sampled from matplotlib's at nine points.
constexpr std::array<Stop, 2> kGrey = {{{0.0, {0, 0, 0}}, {1.0, {255, 255, 255}}}};
constexpr std::array<Stop, 6> kTerrain = {{{0.0, {32, 54, 128}},
                                           {0.25, {64, 160, 200}},
                                           {0.3, {90, 160, 80}},
                                           {0.55, {220, 210, 120}},
                                           {0.8, {130, 95, 70}},
                                           {1.0, {255, 255, 255}}}};
constexpr std::array<Stop, 9> kViridis = {{{0.0, {68, 1, 84}},
                                           {0.125, {71, 44, 122}},
                                           {0.25, {59, 81, 139}},
                                           {0.375, {44, 113, 142}},
                                           {0.5, {33, 144, 141}},
                                           {0.625, {39, 173, 129}},
                                           {0.75, {92, 200, 99}},
                                           {0.875, {170, 220, 50}},
                                           {1.0, {253, 231, 37}}}};

template <std::size_t N>
Rgb interpolate(const std::array<Stop, N>& stops, double t) noexcept {
  for (std::size_t i = 1; i < N; ++i) {
    if (t <= stops[i].t) {
      const Stop& a = stops[i - 1];
      const Stop& b = stops[i];
      const double f = (t - a.t) / (b.t - a.t);
      const auto mix = [f](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (f * (y - x))));
      };
      return {mix(a.c.r, b.c.r), mix(a.c.g, b.c.g), mix(a.c.b, b.c.b)};
    }
  }
  return stops[N - 1].c;
}

}  // namespace

core::Status write_grey16_png(const std::filesystem::path& path, std::int32_t n,
                              const std::vector<std::uint16_t>& grey) {
  std::vector<unsigned char> pixels(grey.size() * 2);
  for (std::size_t i = 0; i < grey.size(); ++i) {
    pixels[2 * i] = static_cast<unsigned char>(grey[i] >> 8U);  // PNG samples are big-endian
    pixels[(2 * i) + 1] = static_cast<unsigned char>(grey[i] & 0xFFU);
  }
  return write_png(path, n, pixels, LCT_GREY, 16);
}

std::optional<Colormap> colormap_from_name(std::string_view name) noexcept {
  if (name == "grey") {
    return Colormap::grey;
  }
  if (name == "terrain") {
    return Colormap::terrain;
  }
  if (name == "viridis") {
    return Colormap::viridis;
  }
  return std::nullopt;
}

Rgb map_color(Colormap map, double t) noexcept {
  if (!std::isfinite(t)) {
    return {};
  }
  t = std::clamp(t, 0.0, 1.0);
  switch (map) {
    case Colormap::grey:
      return interpolate(kGrey, t);
    case Colormap::terrain:
      return interpolate(kTerrain, t);
    case Colormap::viridis:
      return interpolate(kViridis, t);
  }
  return {};
}

core::Status write_colormap_png(const std::filesystem::path& path, std::int32_t n,
                                const std::vector<double>& values, double lo, double hi,
                                Colormap map) {
  std::vector<unsigned char> pixels(values.size() * 3);
  const double span = hi > lo ? hi - lo : 1.0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    const Rgb c = map_color(map, (values[i] - lo) / span);
    pixels[3 * i] = c.r;
    pixels[(3 * i) + 1] = c.g;
    pixels[(3 * i) + 2] = c.b;
  }
  return write_png(path, n, pixels, LCT_RGB, 8);
}

}  // namespace wg::cli
